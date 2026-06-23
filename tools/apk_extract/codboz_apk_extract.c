#include "LzmaDec.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ZIP_EOCD_SIG 0x06054b50u
#define ZIP_CENTRAL_SIG 0x02014b50u
#define ZIP_LOCAL_SIG 0x04034b50u
#define ZIP_METHOD_STORED 0u
#define ZIP_FLAG_ENCRYPTED 0x0001u
#define ZIP64_SENTINEL_32 0xffffffffu
#define ZIP64_SENTINEL_16 0xffffu

#define COPY_BUFFER_SIZE 65536u
#define MAX_ZIP_COMMENT 65535u
#define LZMA_HEADER_SIZE 13u
#define MAX_OUTPUT_SIZE (64u * 1024u * 1024u)

struct zip_entry {
    char *name;
    uint16_t flags;
    uint16_t method;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
};

struct entry_list {
    struct zip_entry *items;
    size_t count;
};

static uint32_t g_crc32_table[256];
static bool g_crc32_ready;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) {
        v |= (uint64_t)p[i] << (8u * i);
    }
    return v;
}

static void fatal(const char *message) {
    fprintf(stderr, "codboz_apk_extract: %s\n", message);
    exit(1);
}

static void fatal_errno(const char *message) {
    fprintf(stderr, "codboz_apk_extract: %s: %s\n", message, strerror(errno));
    exit(1);
}

static void *xmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) {
        fatal_errno("malloc");
    }
    return p;
}

static char *xstrndup(const char *data, size_t len) {
    char *out = xmalloc(len + 1);
    memcpy(out, data, len);
    out[len] = 0;
    return out;
}

static void crc32_init(void) {
    if (g_crc32_ready) {
        return;
    }
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit) {
            c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc32_table[i] = c;
    }
    g_crc32_ready = true;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc32_init();
    crc ^= 0xffffffffu;
    for (size_t i = 0; i < len; ++i) {
        crc = g_crc32_table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

static uint64_t file_size(FILE *file) {
    if (fseek(file, 0, SEEK_END) != 0) {
        fatal_errno("seek end");
    }
    long size = ftell(file);
    if (size < 0) {
        fatal_errno("tell");
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fatal_errno("seek start");
    }
    return (uint64_t)size;
}

static void read_exact(FILE *file, void *buffer, size_t size) {
    if (size && fread(buffer, 1, size, file) != size) {
        fatal_errno("read");
    }
}

static void seek_abs(FILE *file, uint64_t offset) {
    if (offset > (uint64_t)LONG_MAX) {
        fatal("file offset is too large for this platform");
    }
    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        fatal_errno("seek");
    }
}

static void mkdir_one(const char *path) {
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        fatal_errno(path);
    }
}

static void mkdirs_for_file(const char *path) {
    char *copy = xstrndup(path, strlen(path));
    for (char *p = copy + 1; *p; ++p) {
        if (*p == '/') {
            *p = 0;
            mkdir_one(copy);
            *p = '/';
        }
    }
    free(copy);
}

static bool is_safe_asset_name(const char *name) {
    if (strncmp(name, "assets/", 7) != 0 || name[7] == 0) {
        return false;
    }
    if (name[0] == '/' || strstr(name, "../") || strstr(name, "/..") || strstr(name, "//")) {
        return false;
    }
    return true;
}

static bool should_extract_asset(const char *name) {
    return is_safe_asset_name(name);
}

static char *join_asset_path(const char *asset_dir, const char *zip_name) {
    const char *relative = zip_name + 7;
    size_t len = strlen(asset_dir) + 1 + strlen(relative) + 1;
    char *out = xmalloc(len);
    snprintf(out, len, "%s/%s", asset_dir, relative);
    return out;
}

static uint64_t find_eocd(FILE *file, uint64_t size) {
    size_t tail_size = size < (MAX_ZIP_COMMENT + 22u) ? (size_t)size : (MAX_ZIP_COMMENT + 22u);
    uint8_t *tail = xmalloc(tail_size);
    seek_abs(file, size - tail_size);
    read_exact(file, tail, tail_size);
    for (size_t pos = tail_size - 22u + 1u; pos-- > 0;) {
        if (rd32(tail + pos) == ZIP_EOCD_SIG) {
            uint16_t comment_len = rd16(tail + pos + 20);
            if (pos + 22u + comment_len == tail_size) {
                uint64_t eocd = size - tail_size + pos;
                free(tail);
                return eocd;
            }
        }
        if (pos == 0) {
            break;
        }
    }
    free(tail);
    fatal("APK does not contain a valid ZIP end record");
    return 0;
}

static void entry_list_free(struct entry_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].name);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static struct entry_list read_zip_entries(FILE *file) {
    uint64_t size = file_size(file);
    if (size < 22u) {
        fatal("APK is too small to be a ZIP file");
    }

    uint64_t eocd_offset = find_eocd(file, size);
    uint8_t eocd[22];
    seek_abs(file, eocd_offset);
    read_exact(file, eocd, sizeof(eocd));

    if (rd16(eocd + 4) != 0 || rd16(eocd + 6) != 0) {
        fatal("multi-disk ZIP/APK files are not supported");
    }

    uint16_t entry_count_disk = rd16(eocd + 8);
    uint16_t entry_count_total = rd16(eocd + 10);
    uint32_t central_size = rd32(eocd + 12);
    uint32_t central_offset = rd32(eocd + 16);
    if (entry_count_disk != entry_count_total) {
        fatal("inconsistent ZIP central directory entry count");
    }
    if (entry_count_total == ZIP64_SENTINEL_16 || central_size == ZIP64_SENTINEL_32 ||
        central_offset == ZIP64_SENTINEL_32) {
        fatal("ZIP64 APK files are not supported");
    }
    if ((uint64_t)central_offset + central_size > size) {
        fatal("ZIP central directory is outside the APK");
    }

    struct entry_list list = {0};
    list.items = xmalloc((size_t)entry_count_total * sizeof(list.items[0]));

    seek_abs(file, central_offset);
    uint64_t consumed = 0;
    while (consumed < central_size) {
        uint8_t header[46];
        read_exact(file, header, sizeof(header));
        consumed += sizeof(header);
        if (rd32(header) != ZIP_CENTRAL_SIG) {
            entry_list_free(&list);
            fatal("invalid ZIP central directory entry");
        }

        uint16_t name_len = rd16(header + 28);
        uint16_t extra_len = rd16(header + 30);
        uint16_t comment_len = rd16(header + 32);
        size_t variable_len = (size_t)name_len + extra_len + comment_len;
        if (consumed + variable_len > central_size) {
            entry_list_free(&list);
            fatal("truncated ZIP central directory entry");
        }

        char *name = xmalloc((size_t)name_len + 1);
        read_exact(file, name, name_len);
        name[name_len] = 0;
        if (extra_len && fseek(file, extra_len, SEEK_CUR) != 0) {
            fatal_errno("skip extra field");
        }
        if (comment_len && fseek(file, comment_len, SEEK_CUR) != 0) {
            fatal_errno("skip comment field");
        }
        consumed += variable_len;

        struct zip_entry entry = {0};
        entry.name = name;
        entry.flags = rd16(header + 8);
        entry.method = rd16(header + 10);
        entry.crc32 = rd32(header + 16);
        entry.compressed_size = rd32(header + 20);
        entry.uncompressed_size = rd32(header + 24);
        entry.local_header_offset = rd32(header + 42);
        list.items[list.count++] = entry;
    }

    return list;
}

static uint64_t local_data_offset(FILE *file, const struct zip_entry *entry) {
    uint8_t header[30];
    seek_abs(file, entry->local_header_offset);
    read_exact(file, header, sizeof(header));
    if (rd32(header) != ZIP_LOCAL_SIG) {
        fatal("invalid ZIP local file header");
    }
    uint16_t name_len = rd16(header + 26);
    uint16_t extra_len = rd16(header + 28);
    return (uint64_t)entry->local_header_offset + sizeof(header) + name_len + extra_len;
}

static void copy_stored_entry(FILE *apk, const struct zip_entry *entry, const char *asset_dir) {
    if (!should_extract_asset(entry->name)) {
        return;
    }
    if ((entry->flags & ZIP_FLAG_ENCRYPTED) != 0) {
        fprintf(stderr, "Skipping encrypted APK entry: %s\n", entry->name);
        return;
    }
    if (entry->method != ZIP_METHOD_STORED) {
        fprintf(stderr, "Skipping compressed APK entry: %s\n", entry->name);
        return;
    }

    char *out_path = join_asset_path(asset_dir, entry->name);
    mkdirs_for_file(out_path);

    uint64_t data_offset = local_data_offset(apk, entry);
    seek_abs(apk, data_offset);

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fatal_errno(out_path);
    }

    uint8_t *buffer = xmalloc(COPY_BUFFER_SIZE);
    uint32_t crc = 0;
    uint32_t remaining = entry->compressed_size;
    while (remaining > 0) {
        size_t chunk = remaining > COPY_BUFFER_SIZE ? COPY_BUFFER_SIZE : remaining;
        read_exact(apk, buffer, chunk);
        if (fwrite(buffer, 1, chunk, out) != chunk) {
            fatal_errno(out_path);
        }
        crc = crc32_update(crc, buffer, chunk);
        remaining -= (uint32_t)chunk;
    }
    free(buffer);

    if (fclose(out) != 0) {
        fatal_errno(out_path);
    }
    if (crc != entry->crc32) {
        fatal("CRC mismatch while extracting APK asset");
    }

    printf("extracted %s\n", entry->name);
    free(out_path);
}

static void *read_whole_file(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fatal_errno(path);
    }
    uint64_t size64 = file_size(file);
    if (size64 > SIZE_MAX) {
        fatal("file is too large");
    }
    size_t size = (size_t)size64;
    uint8_t *data = xmalloc(size);
    read_exact(file, data, size);
    fclose(file);
    *size_out = size;
    return data;
}

static void *SzAllocImpl(ISzAllocPtr allocator, size_t size) {
    (void)allocator;
    return malloc(size);
}

static void SzFreeImpl(ISzAllocPtr allocator, void *address) {
    (void)allocator;
    free(address);
}

static void decompress_lzma_file(const char *src_path, const char *dst_path) {
    size_t src_size = 0;
    uint8_t *src = read_whole_file(src_path, &src_size);
    if (src_size < LZMA_HEADER_SIZE) {
        fatal("boz.s3e is too small for an LZMA-alone header");
    }

    uint64_t unpacked_size = rd64(src + 5);
    if (unpacked_size == UINT64_MAX || unpacked_size == 0 || unpacked_size > MAX_OUTPUT_SIZE) {
        fatal("boz.s3e has an unsupported unpacked size");
    }

    SizeT dst_len = (SizeT)unpacked_size;
    SizeT compressed_len = (SizeT)(src_size - LZMA_HEADER_SIZE);
    uint8_t *dst = xmalloc((size_t)dst_len);
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    ISzAlloc alloc = {SzAllocImpl, SzFreeImpl};
    SRes res = LzmaDecode(dst, &dst_len, src + LZMA_HEADER_SIZE, &compressed_len, src,
                          LZMA_PROPS_SIZE, LZMA_FINISH_END, &status, &alloc);
    if (res != SZ_OK || dst_len != (SizeT)unpacked_size) {
        fprintf(stderr,
                "codboz_apk_extract: LZMA decode failed: res=%d status=%d out=%zu expected=%" PRIu64
                "\n",
                (int)res, (int)status, (size_t)dst_len, unpacked_size);
        exit(1);
    }

    mkdirs_for_file(dst_path);
    FILE *out = fopen(dst_path, "wb");
    if (!out) {
        fatal_errno(dst_path);
    }
    if (fwrite(dst, 1, (size_t)dst_len, out) != (size_t)dst_len) {
        fatal_errno(dst_path);
    }
    if (fclose(out) != 0) {
        fatal_errno(dst_path);
    }

    free(dst);
    free(src);
    printf("decompressed boz.s3e.unpacked\n");
}

static void extract_apk_assets(const char *apk_path, const char *asset_dir) {
    mkdir_one(asset_dir);

    FILE *apk = fopen(apk_path, "rb");
    if (!apk) {
        fatal_errno(apk_path);
    }

    struct entry_list entries = read_zip_entries(apk);
    bool saw_s3e = false;
    bool saw_loader = false;
    for (size_t i = 0; i < entries.count; ++i) {
        struct zip_entry *entry = &entries.items[i];
        if (!should_extract_asset(entry->name)) {
            continue;
        }
        if (strcmp(entry->name, "assets/boz.s3e") == 0) {
            saw_s3e = true;
        } else if (strcmp(entry->name, "assets/blackops_loader.dz") == 0) {
            saw_loader = true;
        }
        copy_stored_entry(apk, entry, asset_dir);
    }

    fclose(apk);
    entry_list_free(&entries);

    if (!saw_s3e) {
        fatal("APK is missing assets/boz.s3e");
    }
    if (!saw_loader) {
        fatal("APK is missing assets/blackops_loader.dz");
    }

    size_t path_len = strlen(asset_dir) + strlen("/boz.s3e") + 1;
    char *s3e_path = xmalloc(path_len);
    snprintf(s3e_path, path_len, "%s/boz.s3e", asset_dir);
    size_t unpacked_len = strlen(asset_dir) + strlen("/boz.s3e.unpacked") + 1;
    char *unpacked_path = xmalloc(unpacked_len);
    snprintf(unpacked_path, unpacked_len, "%s/boz.s3e.unpacked", asset_dir);
    decompress_lzma_file(s3e_path, unpacked_path);
    free(s3e_path);
    free(unpacked_path);
}

static char *find_next_url(const uint8_t *data, size_t size, size_t *offset) {
    static const char needle[] = "ResDownloadLink=\"";
    size_t needle_len = sizeof(needle) - 1;
    for (size_t i = *offset; i + needle_len < size; ++i) {
        if (memcmp(data + i, needle, needle_len) == 0) {
            size_t start = i + needle_len;
            size_t end = start;
            while (end < size && data[end] && data[end] != '"' && data[end] != '\n' &&
                   data[end] != '\r') {
                ++end;
            }
            *offset = end;
            return xstrndup((const char *)data + start, end - start);
        }
    }
    return NULL;
}

static void print_cdn_url(const char *s3e_path) {
    size_t size = 0;
    uint8_t *data = read_whole_file(s3e_path, &size);
    char *best = NULL;
    size_t offset = 0;
    for (;;) {
        char *url = find_next_url(data, size, &offset);
        if (!url) {
            break;
        }
        if (!best || strstr(url, "callofduty.com")) {
            free(best);
            best = url;
        } else {
            free(url);
        }
    }
    free(data);
    if (!best || best[0] == 0) {
        fatal("could not find ResDownloadLink in unpacked S3E image");
    }
    puts(best);
    free(best);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s extract <game.apk> <asset-dir>\n"
            "  %s print-cdn <asset-dir>/boz.s3e.unpacked\n",
            argv0, argv0);
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "extract") == 0) {
        extract_apk_assets(argv[2], argv[3]);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "print-cdn") == 0) {
        print_cdn_url(argv[2]);
        return 0;
    }
    usage(argv[0]);
    return 2;
}
