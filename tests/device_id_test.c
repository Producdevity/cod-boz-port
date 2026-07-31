#include "s3e_host_internal.h"

#include <assert.h>
#include <sys/stat.h>

char g_root[1024];

static void assert_lowercase_hex(const char *value) {
    assert(strlen(value) == 32);
    for (size_t i = 0; i < 32; ++i) {
        assert((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'));
    }
}

static void write_known_id(const char *path) {
    uint8_t bytes[16];
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (uint8_t)i;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
    assert(close(fd) == 0);
}

int main(void) {
    char template[] = "/tmp/codboz-device-id.XXXXXX";
    char *directory = mkdtemp(template);
    assert(directory);
    snprintf(g_root, sizeof(g_root), "%s", directory);

    char first[33];
    char second[33];
    assert(s3e_device_id_load_or_create(g_root, first));
    assert_lowercase_hex(first);
    assert(s3e_device_id_load_or_create(g_root, second));
    assert(strcmp(first, second) == 0);

    char path[1200];
    snprintf(path, sizeof(path), "%s/device-id.bin", g_root);
    struct stat status;
    assert(stat(path, &status) == 0);
    assert(S_ISREG(status.st_mode));
    assert(status.st_size == 16);

    write_known_id(path);
    assert(s3e_device_id_load_or_create(g_root, second));
    assert(strcmp(second, "000102030405060708090a0b0c0d0e0f") == 0);

    assert(unlink(path) == 0);
    assert(rmdir(g_root) == 0);
    puts("device ID tests passed");
    return 0;
}
