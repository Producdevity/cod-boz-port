#include "s3e_host_internal.h"

static void make_path(char *out, size_t out_size, const char *name) {
    if (name && name[0] == '/') {
        snprintf(out, out_size, "%s", name);
    } else {
        snprintf(out, out_size, "%s/%s", g_root, name ? name : "");
    }
}

static int path_exists(const char *path) {
    return access(path, F_OK) == 0;
}

static int use_existing_path(char *out, size_t out_size, const char *path) {
    if (!path_exists(path)) {
        return 0;
    }
    snprintf(out, out_size, "%s", path);
    return 1;
}

static int try_asset_path(char *out, size_t out_size, const char *prefix, const char *name) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s/%s", g_root, prefix, name);
    return use_existing_path(out, out_size, path);
}

static const char *base_name(const char *name) {
    const char *slash = strrchr(name ? name : "", '/');
    return slash ? slash + 1 : (name ? name : "");
}

static int flat_group_name(const char *name, char *out, size_t out_size) {
    const char *slash = strchr(name ? name : "", '/');
    if (!slash || !strstr(name, ".group.bin")) {
        return 0;
    }
    size_t section_len = (size_t)(slash - name);
    if (section_len == 0 || section_len >= 128) {
        return 0;
    }
    char section[128];
    memcpy(section, name, section_len);
    section[section_len] = 0;
    const char *leaf = strrchr(name, '/') + 1;
    if (strcmp(leaf, ".group.bin") == 0) {
        snprintf(out, out_size, "%s.group.bin", section);
    } else {
        snprintf(out, out_size, "%s", leaf);
    }
    return 1;
}

static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void lower_copy(char *out, size_t out_size, const char *in) {
    size_t i = 0;
    if (!out_size) {
        return;
    }
    for (; in && in[i] && i + 1 < out_size; ++i) {
        out[i] = (char)tolower((unsigned char)in[i]);
    }
    out[i] = 0;
}

static int read_c_string(FILE *file, char *out, size_t out_size) {
    size_t len = 0;
    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (ch == 0) {
            if (out_size) {
                out[len < out_size ? len : out_size - 1] = 0;
            }
            return 1;
        }
        if (len + 1 < out_size) {
            out[len++] = (char)ch;
        }
    }
    if (out_size) {
        out[len < out_size ? len : out_size - 1] = 0;
    }
    return 0;
}

static int dtrz_load_index(void) {
    if (g_dtrz.loaded) {
        return g_dtrz.count > 0;
    }
    g_dtrz.loaded = 1;
    static const char *archives[] = {
        "blackops_etc.dz",
        "blackops_atitc.dz",
        "blackops_dxt.dz",
        "blackops_gles1.dz",
    };
    for (size_t i = 0; i < sizeof(archives) / sizeof(archives[0]); ++i) {
        char candidate[sizeof(g_dtrz.path)];
        snprintf(candidate, sizeof(candidate), "%s/assets/%s", g_root, archives[i]);
        if (path_exists(candidate)) {
            snprintf(g_dtrz.path, sizeof(g_dtrz.path), "%s", candidate);
            break;
        }
    }
    if (!g_dtrz.path[0]) {
        snprintf(g_dtrz.path, sizeof(g_dtrz.path), "%s/assets/blackops_gles1.dz", g_root);
    }

    FILE *file = fopen(g_dtrz.path, "rb");
    if (!file) {
        return 0;
    }
    uint8_t header[9];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "DTRZ", 4) != 0) {
        fclose(file);
        return 0;
    }
    uint16_t file_count = read_le16(header + 4);
    uint16_t group_count = read_le16(header + 6);
    if (group_count > 0) {
        group_count--;
    }
    if (file_count > DTRZ_MAX_ENTRIES) {
        fclose(file);
        return 0;
    }
    for (uint16_t i = 0; i < file_count; ++i) {
        if (!read_c_string(file, g_dtrz.entries[i].name, sizeof(g_dtrz.entries[i].name))) {
            fclose(file);
            return 0;
        }
    }
    char scratch[DTRZ_NAME_MAX];
    for (uint16_t i = 0; i < group_count; ++i) {
        if (!read_c_string(file, scratch, sizeof(scratch))) {
            fclose(file);
            return 0;
        }
    }
    uint8_t marker[4];
    if (fread(marker, 1, sizeof(marker), file) != sizeof(marker) || read_le32(marker) != 1 ||
        fseek(file, (long)file_count * 6L, SEEK_CUR) != 0) {
        fclose(file);
        return 0;
    }
    for (uint16_t i = 0; i < file_count; ++i) {
        uint8_t record[16];
        if (fread(record, 1, sizeof(record), file) != sizeof(record)) {
            fclose(file);
            return 0;
        }
        struct dtrz_entry *entry = &g_dtrz.entries[i];
        entry->offset = read_le32(record);
        entry->size = read_le32(record + 4);
        lower_copy(entry->lower_name, sizeof(entry->lower_name), entry->name);
        lower_copy(entry->lower_base, sizeof(entry->lower_base), base_name(entry->name));
    }
    g_dtrz.count = file_count;
    fclose(file);
    return 1;
}

static int dtrz_archive_redirect_path(const char *name, char *out, size_t out_size) {
    const char *requested = base_name(name ? name : "");
    if (strcasecmp(requested, "blackops_gles1.dz") != 0) {
        return 0;
    }
    if (!dtrz_load_index() || !g_dtrz.path[0]) {
        return 0;
    }
    const char *selected = base_name(g_dtrz.path);
    if (strcasecmp(selected, requested) == 0 || !path_exists(g_dtrz.path)) {
        return 0;
    }
    snprintf(out, out_size, "%s", g_dtrz.path);
    return 1;
}

static const struct dtrz_entry *dtrz_find_entry(const char *name) {
    if (!dtrz_load_index()) {
        return NULL;
    }
    char lower_name[DTRZ_NAME_MAX];
    lower_copy(lower_name, sizeof(lower_name), name ? name : "");
    const char *lower_base = base_name(lower_name);
    for (size_t i = 0; i < g_dtrz.count; ++i) {
        const struct dtrz_entry *entry = &g_dtrz.entries[i];
        if (strcmp(entry->lower_name, lower_name) == 0 ||
            strcmp(entry->lower_base, lower_base) == 0) {
            return entry;
        }
    }
    return NULL;
}

static int dtrz_entry_exists(const char *name) {
    return dtrz_find_entry(name) != NULL;
}

static int dtrz_prefer_entry(const char *name) {
    char lower[256];
    lower_copy(lower, sizeof(lower), name ? name : "");
    return strncmp(lower, "data-etc/", 9) == 0 || strncmp(lower, "data-dxt/", 9) == 0 ||
           strncmp(lower, "data-atitc/", 11) == 0;
}

static void track_memory_file(FILE *file, void *buffer) {
    struct memory_file *item = malloc(sizeof(*item));
    if (!item) {
        fclose(file);
        free(buffer);
        return;
    }
    item->file = file;
    item->buffer = buffer;
    item->next = g_memory_files;
    g_memory_files = item;
}

static int close_memory_file(FILE *file) {
    struct memory_file **link = &g_memory_files;
    while (*link) {
        struct memory_file *item = *link;
        if (item->file == file) {
            *link = item->next;
            fclose(item->file);
            free(item->buffer);
            free(item);
            return 1;
        }
        link = &item->next;
    }
    return 0;
}

static FILE *open_dtrz_entry(const char *name, char *opened_path, size_t opened_path_size) {
    const struct dtrz_entry *entry = dtrz_find_entry(name);
    if (!entry) {
        return NULL;
    }
    FILE *archive = fopen(g_dtrz.path, "rb");
    if (!archive) {
        return NULL;
    }
    size_t alloc_size = entry->size ? entry->size : 1;
    void *buffer = malloc(alloc_size);
    if (!buffer) {
        fclose(archive);
        return NULL;
    }
    if (fseek(archive, (long)entry->offset, SEEK_SET) != 0 ||
        fread(buffer, 1, entry->size, archive) != entry->size) {
        fclose(archive);
        free(buffer);
        return NULL;
    }
    fclose(archive);
    FILE *file = fmemopen(buffer, alloc_size, "rb");
    if (!file) {
        free(buffer);
        return NULL;
    }
    track_memory_file(file, buffer);
    snprintf(opened_path, opened_path_size, "DTRZ:%s", entry->name);
    return file;
}

static int resolve_read_path(const char *name, char *out, size_t out_size) {
    char path[1200];
    const char *safe_name = name ? name : "";
    if (dtrz_archive_redirect_path(safe_name, out, out_size)) {
        return 1;
    }
    make_path(path, sizeof(path), safe_name);
    if (use_existing_path(out, out_size, path)) {
        return 1;
    }
    if (safe_name[0] == '/') {
        return 0;
    }
    if (try_asset_path(out, out_size, "assets", safe_name) ||
        try_asset_path(out, out_size, "assets/data-gles1", safe_name) ||
        try_asset_path(out, out_size, "assets/data-sw", safe_name)) {
        return 1;
    }
    char flat_name[512];
    if (flat_group_name(safe_name, flat_name, sizeof(flat_name)) &&
        (try_asset_path(out, out_size, "assets/data-gles1", flat_name) ||
         try_asset_path(out, out_size, "assets/data-sw", flat_name) ||
         try_asset_path(out, out_size, "assets", flat_name))) {
        return 1;
    }
    const char *leaf = base_name(safe_name);
    if (leaf != safe_name && (try_asset_path(out, out_size, "assets/data-gles1", leaf) ||
                              try_asset_path(out, out_size, "assets/data-sw", leaf) ||
                              try_asset_path(out, out_size, "assets", leaf))) {
        return 1;
    }
    return 0;
}

static int is_read_mode(const char *mode) {
    return mode && mode[0] == 'r';
}

static int is_archive_read_mode(const char *mode) {
    return is_read_mode(mode) && strchr(mode, '+') == NULL;
}

static int is_user_file_mode(const char *mode) {
    return mode && (strchr(mode, 'U') || strchr(mode, '+') || mode[0] == 'w' || mode[0] == 'a');
}

static int is_user_file_name(const char *name) {
    const char *dot = strrchr(name ? name : "", '.');
    return dot && strcasecmp(dot, ".i3d") == 0;
}

static void sanitize_file_mode(const char *mode, char *out, size_t out_size) {
    size_t n = 0;
    if (!out_size) {
        return;
    }
    for (const char *p = mode ? mode : "rb"; *p && n + 1 < out_size; ++p) {
        if (*p != 'U') {
            out[n++] = *p;
        }
    }
    out[n] = 0;
    if (!out[0]) {
        snprintf(out, out_size, "rb");
    }
}

static void make_user_path(char *out, size_t out_size, const char *name) {
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        home = g_root;
    }
    if (name && name[0] == '/') {
        snprintf(out, out_size, "%s", name);
    } else {
        snprintf(out, out_size, "%s/%s", home, name ? name : "");
    }
}

static void make_parent_dirs(const char *path) {
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s", path ? path : "");
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return 0;
    }
    make_parent_dirs(dst);
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    uint8_t buffer[8192];
    size_t got;
    int ok = 1;
    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, got, out) != got) {
            ok = 0;
            break;
        }
    }
    if (ferror(in)) {
        ok = 0;
    }
    fclose(out);
    fclose(in);
    return ok;
}

static long file_size_for_seek(FILE *file) {
    long here = ftell(file);
    if (here < 0 || fseek(file, 0, SEEK_END) != 0) {
        return -1;
    }
    long size = ftell(file);
    fseek(file, here, SEEK_SET);
    return size;
}

void *s3eFileOpen(const char *name, const char *mode) {
    char path[1200];
    char opened_path[1200] = "";
    const char *safe_name = name ? name : "";
    const char *safe_mode = mode ? mode : "rb";
    char fopen_mode[16];
    sanitize_file_mode(safe_mode, fopen_mode, sizeof(fopen_mode));
    FILE *file = NULL;

    if (is_user_file_mode(safe_mode) || is_user_file_name(safe_name)) {
        char seed_path[1200];
        make_user_path(path, sizeof(path), safe_name);
        make_parent_dirs(path);
        if (!is_user_file_name(safe_name) && safe_mode[0] == 'r' && !path_exists(path) &&
            resolve_read_path(safe_name, seed_path, sizeof(seed_path))) {
            copy_file(seed_path, path);
        }
        file = fopen(path, fopen_mode);
        if (file) {
            snprintf(opened_path, sizeof(opened_path), "%s", path);
        }
    } else if (is_archive_read_mode(safe_mode) && dtrz_prefer_entry(safe_name) &&
               (file = open_dtrz_entry(safe_name, opened_path, sizeof(opened_path))) != NULL) {
    } else if (is_read_mode(safe_mode) && resolve_read_path(safe_name, path, sizeof(path))) {
        file = fopen(path, fopen_mode);
    } else {
        make_path(path, sizeof(path), safe_name);
        if (!is_read_mode(safe_mode)) {
            make_parent_dirs(path);
        }
        file = fopen(path, fopen_mode);
    }
    if (file && !opened_path[0]) {
        snprintf(opened_path, sizeof(opened_path), "%s", path);
    }
    if (!file && is_read_mode(safe_mode) && safe_name[0] != '/') {
        snprintf(path, sizeof(path), "%s/assets/%s", g_root, safe_name);
        file = fopen(path, fopen_mode);
    }
    if (!file && is_archive_read_mode(safe_mode)) {
        file = open_dtrz_entry(safe_name, opened_path, sizeof(opened_path));
    }
    if (!file && is_read_mode(safe_mode) && strcmp(base_name(safe_name), "console.bin") == 0) {
        file = fopen("/dev/null", "rb");
    }
    return file;
}

int32_t s3eFileClose(void *file) {
    if (!file) {
        return -1;
    }
    if (close_memory_file((FILE *)file)) {
        return 0;
    }
    return fclose((FILE *)file);
}

uint32_t s3eFileRead(void *buffer, uint32_t elem_size, uint32_t count, void *file) {
    return file ? (uint32_t)fread(buffer, elem_size, count, (FILE *)file) : 0;
}

uint32_t s3eFileWrite(const void *buffer, uint32_t elem_size, uint32_t count, void *file) {
    return file ? (uint32_t)fwrite(buffer, elem_size, count, (FILE *)file) : 0;
}

int32_t s3eFileGetChar(void *file) {
    return file ? fgetc((FILE *)file) : -1;
}

int32_t s3eFilePutChar(int32_t c, void *file) {
    return file ? fputc(c, (FILE *)file) : -1;
}

int32_t s3eFileFlush(void *file) {
    return file ? fflush((FILE *)file) : -1;
}

int32_t s3eFileSeek(void *file, int32_t offset, int32_t origin) {
    return file ? fseek((FILE *)file, offset, origin) : -1;
}

int32_t s3eFileTell(void *file) {
    return file ? (int32_t)ftell((FILE *)file) : -1;
}

int32_t s3eFileGetSize(void *file) {
    return file ? (int32_t)file_size_for_seek((FILE *)file) : -1;
}

int32_t s3eFileCheckExists(const char *name) {
    char path[1200];
    const char *safe_name = name ? name : "";
    if (is_user_file_name(safe_name)) {
        make_user_path(path, sizeof(path), safe_name);
        return access(path, F_OK) == 0 ? 1 : 0;
    }
    if (dtrz_prefer_entry(safe_name) && dtrz_entry_exists(safe_name)) {
        return 1;
    }
    if (resolve_read_path(safe_name, path, sizeof(path))) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/assets/%s", g_root, safe_name);
    if (access(path, F_OK) == 0) {
        return 1;
    }
    return dtrz_entry_exists(safe_name) ? 1 : 0;
}

int32_t s3eFileGetError(void) {
    return errno;
}

const char *s3eFileGetErrorString(void) {
    return strerror(errno);
}

void *s3eFileOpenFromMemory(void *buffer, uint32_t size) {
    return buffer && size ? fmemopen(buffer, size, "rb") : NULL;
}

int32_t s3eFileGetFileInt(void *file, uint32_t key) {
    (void)file;
    (void)key;
    return 0;
}

int32_t s3eFileMakeDirectory(const char *name) {
    char path[1200];
    make_path(path, sizeof(path), name);
    return mkdir(path, 0777) == 0 || errno == EEXIST ? 0 : -1;
}

int32_t s3eFileDelete(const char *name) {
    char path[1200];
    make_path(path, sizeof(path), name);
    return unlink(path);
}

int32_t s3eFileRename(const char *old_name, const char *new_name) {
    char old_path[1200];
    char new_path[1200];
    make_path(old_path, sizeof(old_path), old_name);
    make_path(new_path, sizeof(new_path), new_name);
    return rename(old_path, new_path);
}

int32_t s3eFileAddUserFileSys(const char *prefix, const char *path) {
    (void)prefix;
    (void)path;
    return 0;
}

void *s3eFileListDirectory(const char *path) {
    (void)path;
    return NULL;
}

int32_t s3eFileListClose(void *list) {
    (void)list;
    return 0;
}
