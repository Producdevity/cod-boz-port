#include "s3e_image.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE MAP_FIXED
#endif

#define S3E_MAGIC 0x55334558u

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool range_ok(size_t size, uint32_t offset, uint32_t length) {
    return offset <= size && length <= size - offset;
}

static size_t page_round(size_t size) {
    long page = sysconf(_SC_PAGESIZE);
    size_t mask = (size_t)page - 1;
    return (size + mask) & ~mask;
}

bool s3e_image_load(const char *path, struct s3e_image *image) {
    memset(image, 0, sizeof(*image));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        fprintf(stderr, "stat %s failed\n", path);
        close(fd);
        return false;
    }

    image->file_size = (size_t)st.st_size;
    image->file_data = malloc(image->file_size);
    if (!image->file_data) {
        close(fd);
        return false;
    }

    size_t done = 0;
    while (done < image->file_size) {
        ssize_t got = read(fd, image->file_data + done, image->file_size - done);
        if (got <= 0) {
            fprintf(stderr, "read %s failed\n", path);
            close(fd);
            s3e_image_free(image);
            return false;
        }
        done += (size_t)got;
    }
    close(fd);

    if (image->file_size < 68) {
        fprintf(stderr, "%s is too small for an S3E header\n", path);
        s3e_image_free(image);
        return false;
    }

    const uint8_t *p = image->file_data;
    struct s3e_header *h = &image->header;

    h->ident = rd32(p + 0);
    h->version = rd32(p + 4);
    h->flags = rd16(p + 8);
    h->arch = rd16(p + 10);
    h->fixup_offset = rd32(p + 12);
    h->fixup_size = rd32(p + 16);
    h->code_offset = rd32(p + 20);
    h->code_file_size = rd32(p + 24);
    h->code_mem_size = rd32(p + 28);
    h->sig_offset = rd32(p + 32);
    h->sig_size = rd32(p + 36);
    h->entry_offset = rd32(p + 40);
    h->config_offset = rd32(p + 44);
    h->config_size = rd32(p + 48);
    h->base_addr_orig = rd32(p + 52);
    h->extra_offset = rd32(p + 56);
    h->extra_size = rd32(p + 60);
    h->ext_header_size = rd32(p + 64);

    if (h->ident != S3E_MAGIC) {
        fprintf(stderr, "%s is not an uncompressed S3E image\n", path);
        s3e_image_free(image);
        return false;
    }

    if (h->ext_header_size == 0x0c) {
        if (image->file_size < 76) {
            s3e_image_free(image);
            return false;
        }
        h->data_offset = rd32(p + 68);
        h->is_juice = rd32(p + 72);
    }

    if (!range_ok(image->file_size, h->fixup_offset, h->fixup_size) ||
        !range_ok(image->file_size, h->code_offset, h->code_file_size) ||
        h->code_file_size > h->code_mem_size || h->entry_offset >= h->code_mem_size) {
        fprintf(stderr, "S3E header ranges are invalid\n");
        s3e_image_free(image);
        return false;
    }

    return true;
}

void s3e_image_free(struct s3e_image *image) {
    if (!image) {
        return;
    }
    for (size_t i = 0; i < image->symbols.count; ++i) {
        free(image->symbols.items[i]);
    }
    free(image->symbols.items);
    free(image->file_data);
    memset(image, 0, sizeof(*image));
}

bool s3e_image_parse_symbols(struct s3e_image *image) {
    const struct s3e_header *h = &image->header;
    const uint8_t *data = image->file_data;
    uint32_t pos = h->fixup_offset;
    uint32_t end = h->fixup_offset + h->fixup_size;

    while (pos < end) {
        if (end - pos < 8) {
            return false;
        }

        uint32_t type = rd32(data + pos);
        uint32_t size = rd32(data + pos + 4);
        uint32_t body = pos + 8;
        if (size < 8 || size > end - pos) {
            return false;
        }

        if (type == 0) {
            if (size < 10) {
                return false;
            }
            uint16_t count = rd16(data + body);
            char **items = calloc(count, sizeof(*items));
            if (!items) {
                return false;
            }

            uint32_t cursor = body + 2;
            for (uint16_t i = 0; i < count; ++i) {
                uint32_t start = cursor;
                while (cursor < pos + size && data[cursor] != 0) {
                    cursor++;
                }
                if (cursor >= pos + size) {
                    free(items);
                    return false;
                }

                size_t len = cursor - start;
                items[i] = malloc(len + 1);
                if (!items[i]) {
                    for (uint16_t j = 0; j < i; ++j) {
                        free(items[j]);
                    }
                    free(items);
                    return false;
                }
                memcpy(items[i], data + start, len);
                items[i][len] = 0;
                cursor++;
            }

            image->symbols.items = items;
            image->symbols.count = count;
        }

        pos += size;
    }

    return image->symbols.count > 0;
}

static bool apply_internal_relocs(const struct s3e_image *image, uint8_t *base, uint32_t pos,
                                  uint32_t size, int32_t load_delta) {
    const uint8_t *data = image->file_data;
    uint32_t body = pos + 8;
    if (size < 12) {
        return false;
    }

    uint32_t count = rd32(data + body);
    uint32_t cursor = body + 4;
    if (count > (size - 12) / 4) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset = rd32(data + cursor);
        cursor += 4;
        if (offset > image->header.code_mem_size - 4) {
            return false;
        }
        uint32_t *slot = (uint32_t *)(void *)(base + offset);
        *slot += (uint32_t)load_delta;
    }

    return true;
}

static bool apply_external_relocs(const struct s3e_image *image, uint8_t *base, uint32_t pos,
                                  uint32_t size, void *(*resolve)(const char *symbol)) {
    const uint8_t *data = image->file_data;
    uint32_t body = pos + 8;
    if (size < 12) {
        return false;
    }

    uint32_t count = rd32(data + body);
    uint32_t cursor = body + 4;
    if (count > (size - 12) / 6) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t hi = rd16(data + cursor);
        uint32_t lo = rd16(data + cursor + 2);
        uint16_t symbol_index = rd16(data + cursor + 4);
        cursor += 6;

        uint32_t offset = (hi << 16) | lo;
        if (offset > image->header.code_mem_size - 4 || symbol_index >= image->symbols.count) {
            return false;
        }

        const char *symbol = image->symbols.items[symbol_index];
        void *addr = resolve(symbol);
        if (!addr) {
            fprintf(stderr, "unresolved import: %s\n", symbol);
            return false;
        }

        uint32_t *slot = (uint32_t *)(void *)(base + offset);
        *slot = (uint32_t)(uintptr_t)addr;
    }

    return true;
}

bool s3e_image_map_and_relocate(const struct s3e_image *image, void *(*resolve)(const char *symbol),
                                struct s3e_loaded_image *loaded) {
    memset(loaded, 0, sizeof(*loaded));
    const struct s3e_header *h = &image->header;
    size_t map_size = page_round(h->code_mem_size);
    void *want = (void *)(uintptr_t)h->base_addr_orig;
    uint8_t *base = mmap(want, map_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (base == MAP_FAILED) {
        fprintf(stderr, "mmap 0x%08x: %s\n", h->base_addr_orig, strerror(errno));
        return false;
    }

    memcpy(base, image->file_data + h->code_offset, h->code_file_size);
    memset(base + h->code_file_size, 0, h->code_mem_size - h->code_file_size);

    uint32_t pos = h->fixup_offset;
    uint32_t end = h->fixup_offset + h->fixup_size;
    int32_t load_delta = (int32_t)((uint32_t)(uintptr_t)base - h->base_addr_orig);
    while (pos < end) {
        uint32_t type = rd32(image->file_data + pos);
        uint32_t size = rd32(image->file_data + pos + 4);
        if (size < 8 || size > end - pos) {
            munmap(base, map_size);
            return false;
        }

        bool ok = true;
        if (type == 1) {
            ok = apply_internal_relocs(image, base, pos, size, load_delta);
        } else if (type == 2 || type == 3 || type == 4) {
            ok = apply_external_relocs(image, base, pos, size, resolve);
        }

        if (!ok) {
            munmap(base, map_size);
            return false;
        }

        pos += size;
    }

    loaded->base = base;
    loaded->map_size = map_size;
    loaded->entry_offset = h->entry_offset;
    return true;
}

void s3e_loaded_image_unmap(struct s3e_loaded_image *loaded) {
    if (loaded && loaded->base) {
        munmap(loaded->base, loaded->map_size);
        memset(loaded, 0, sizeof(*loaded));
    }
}
