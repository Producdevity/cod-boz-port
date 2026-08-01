#include "s3e_host_internal.h"

#include <sys/stat.h>

enum {
    DEVICE_ID_BYTE_COUNT = 16,
    DEVICE_ID_HEX_LENGTH = 32,
};

static pthread_once_t g_device_id_once = PTHREAD_ONCE_INIT;
static char g_device_id[DEVICE_ID_HEX_LENGTH + 1];

static bool read_all(int fd, uint8_t *buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t received = read(fd, buffer + offset, length - offset);
        if (received > 0) {
            offset += (size_t)received;
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

static bool write_all(int fd, const uint8_t *buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t written = write(fd, buffer + offset, length - offset);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

static bool load_bytes(const char *path, uint8_t bytes[DEVICE_ID_BYTE_COUNT]) {
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags);
    if (fd < 0) {
        return false;
    }
    struct stat status;
    bool valid = fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
                 status.st_size == DEVICE_ID_BYTE_COUNT &&
                 read_all(fd, bytes, DEVICE_ID_BYTE_COUNT);
    close(fd);
    return valid;
}

static bool random_bytes(uint8_t bytes[DEVICE_ID_BYTE_COUNT]) {
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    bool valid = read_all(fd, bytes, DEVICE_ID_BYTE_COUNT);
    close(fd);
    return valid;
}

static bool store_bytes(const char *path, const char *root,
                        const uint8_t bytes[DEVICE_ID_BYTE_COUNT]) {
    char temporary[1200];
    int length = snprintf(temporary, sizeof(temporary), "%s/device-id.bin.tmp.XXXXXX", root);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        return false;
    }
    int fd = mkstemp(temporary);
    if (fd < 0) {
        return false;
    }
    bool stored =
        fchmod(fd, 0600) == 0 && write_all(fd, bytes, DEVICE_ID_BYTE_COUNT) && fsync(fd) == 0;
    int close_result = close(fd);
    stored = stored && close_result == 0;
    if (stored) {
        stored = rename(temporary, path) == 0;
    }
    if (!stored) {
        unlink(temporary);
    }
    return stored;
}

bool s3e_device_id_load_or_create(const char *root, char output[33]) {
    static const char digits[] = "0123456789abcdef";
    if (!root || !root[0] || !output) {
        return false;
    }
    char path[1200];
    int length = snprintf(path, sizeof(path), "%s/device-id.bin", root);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return false;
    }
    uint8_t bytes[DEVICE_ID_BYTE_COUNT];
    if (!load_bytes(path, bytes)) {
        if (!random_bytes(bytes) || !store_bytes(path, root, bytes)) {
            return false;
        }
    }
    for (size_t i = 0; i < DEVICE_ID_BYTE_COUNT; ++i) {
        output[i * 2] = digits[bytes[i] >> 4];
        output[i * 2 + 1] = digits[bytes[i] & 0x0f];
    }
    output[DEVICE_ID_HEX_LENGTH] = 0;
    return true;
}

static void initialize_device_id(void) {
    if (!s3e_device_id_load_or_create(g_root, g_device_id)) {
        g_device_id[0] = 0;
        fprintf(stderr, "[device] unable to load or create persistent device ID\n");
    }
}

const char *s3e_device_id_get(void) {
    pthread_once(&g_device_id_once, initialize_device_id);
    return g_device_id;
}
