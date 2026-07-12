#include "s3e_host.h"
#include "s3e_image.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

static uintptr_t g_loaded_base;
static const char g_empty_string[8] __attribute__((aligned(8))) = "";
static uint32_t g_bucket_allocator_table[33] __attribute__((aligned(8)));

enum {
    BUCKET_ALLOCATOR_TABLE_SLOT = 0x4cu,
};

static void attach_bucket_allocator_table(uint32_t object) {
    uint32_t *table_slot = (uint32_t *)(uintptr_t)(object + BUCKET_ALLOCATOR_TABLE_SLOT);
    *table_slot = (uint32_t)(uintptr_t)g_bucket_allocator_table;
}

static void prepare_bucket_allocator_table(uint32_t object) {
    if (!object) {
        return;
    }

    attach_bucket_allocator_table(object);
    memset(g_bucket_allocator_table, 0, sizeof(g_bucket_allocator_table));
}

static bool recover_bucket_allocator_fault(ucontext_t *uc) {
    uintptr_t pc = uc->uc_mcontext.arm_pc;
    if (pc != g_loaded_base + 0x374be8u && pc != g_loaded_base + 0x374bf4u) {
        return false;
    }

    uint32_t object = uc->uc_mcontext.arm_r5 ? uc->uc_mcontext.arm_r5 : uc->uc_mcontext.arm_r0;
    uint32_t index = uc->uc_mcontext.arm_r4 ? uc->uc_mcontext.arm_r4 : uc->uc_mcontext.arm_r1;
    if (!object) {
        return false;
    }
    if (index >= 32 && pc == g_loaded_base + 0x374be8u) {
        index = 1;
    }
    if (index >= 32) {
        return false;
    }

    attach_bucket_allocator_table(object);

    uc->uc_mcontext.arm_r0 = object;
    uc->uc_mcontext.arm_r1 = index;
    uc->uc_mcontext.arm_r2 = (uint32_t)(uintptr_t)g_bucket_allocator_table;
    uc->uc_mcontext.arm_r4 = index;
    uc->uc_mcontext.arm_r5 = object;

    if (pc == g_loaded_base + 0x374be8u) {
        prepare_bucket_allocator_table(object);
        uc->uc_mcontext.arm_pc = g_loaded_base + 0x374be8u;
    } else {
        g_bucket_allocator_table[index] = 0;
        uc->uc_mcontext.arm_pc = g_loaded_base + 0x374c34u;
    }
    return true;
}

static bool recover_null_buffer_write(ucontext_t *uc) {
    if (uc->uc_mcontext.arm_pc != g_loaded_base + 0xcb8eeu || uc->uc_mcontext.arm_r0 != 0) {
        return false;
    }

    uint32_t *sp = (uint32_t *)(uintptr_t)uc->uc_mcontext.arm_sp;
    uc->uc_mcontext.arm_r4 = sp[0];
    uc->uc_mcontext.arm_r5 = sp[1];
    uc->uc_mcontext.arm_r6 = sp[2];
    uc->uc_mcontext.arm_r0 = 0;
    uc->uc_mcontext.arm_sp += 16;
    uc->uc_mcontext.arm_pc = sp[3] & ~1u;
    return true;
}

static bool recover_null_buffer_slot(ucontext_t *uc) {
    uintptr_t pc = uc->uc_mcontext.arm_pc;
    if ((pc != g_loaded_base + 0xd291cu && pc != g_loaded_base + 0xd293au) ||
        uc->uc_mcontext.arm_r2 != 0) {
        return false;
    }

    uint32_t *sp = (uint32_t *)(uintptr_t)uc->uc_mcontext.arm_sp;
    uc->uc_mcontext.arm_r3 = sp[0];
    uc->uc_mcontext.arm_r4 = sp[1];
    uc->uc_mcontext.arm_r5 = sp[2];
    uc->uc_mcontext.arm_r6 = sp[3];
    uc->uc_mcontext.arm_r7 = sp[4];
    uc->uc_mcontext.arm_r0 = 0;
    uc->uc_mcontext.arm_sp += 24;
    uc->uc_mcontext.arm_pc = sp[5] & ~1u;
    return true;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--run] [--root DIR] [--display-size WIDTHxHEIGHT] "
            "IMAGE.s3e.unpacked\n",
            argv0);
}

static bool parse_display_size(const char *value, uint32_t *width, uint32_t *height) {
    char *width_end = NULL;
    char *height_end = NULL;
    unsigned long parsed_width = strtoul(value, &width_end, 10);
    if (width_end == value || (*width_end != 'x' && *width_end != 'X')) {
        return false;
    }
    unsigned long parsed_height = strtoul(width_end + 1, &height_end, 10);
    if (height_end == width_end + 1 || *height_end != '\0' || !parsed_width || !parsed_height ||
        parsed_width > UINT16_MAX || parsed_height > UINT16_MAX) {
        return false;
    }
    *width = (uint32_t)parsed_width;
    *height = (uint32_t)parsed_height;
    return true;
}

static void crash_handler(int sig, siginfo_t *info, void *context) {
    ucontext_t *uc = (ucontext_t *)context;
    if (sig == SIGSEGV && recover_bucket_allocator_fault(uc)) {
        return;
    }
    if (sig == SIGSEGV && recover_null_buffer_write(uc)) {
        return;
    }
    if (sig == SIGSEGV && recover_null_buffer_slot(uc)) {
        return;
    }
    if (sig == SIGSEGV && uc->uc_mcontext.arm_pc == g_loaded_base + 0x368ddcu &&
        uc->uc_mcontext.arm_r1 == 0) {
        uc->uc_mcontext.arm_r1 = (unsigned long)(uintptr_t)g_empty_string;
        return;
    }
    if (sig == SIGSEGV && uc->uc_mcontext.arm_pc == g_loaded_base + 0x368d2cu &&
        uc->uc_mcontext.arm_r1 == 0) {
        uc->uc_mcontext.arm_r1 = (unsigned long)(uintptr_t)g_empty_string;
        return;
    }
    if (sig == SIGSEGV &&
        (uc->uc_mcontext.arm_pc == g_loaded_base + 0x24ba00u ||
         uc->uc_mcontext.arm_pc == g_loaded_base + 0x24ba30u) &&
        uc->uc_mcontext.arm_r0 == 0) {
        uc->uc_mcontext.arm_r0 = (unsigned long)(uintptr_t)g_empty_string;
        return;
    }
    if (sig == SIGSEGV && uc->uc_mcontext.arm_pc == g_loaded_base + 0x368fa4u &&
        uc->uc_mcontext.arm_r1 == 0) {
        uc->uc_mcontext.arm_r0 = 0;
        uc->uc_mcontext.arm_r2 = 0;
        uc->uc_mcontext.arm_pc = g_loaded_base + 0x369024u;
        return;
    }

    fprintf(stderr,
            "signal %d addr=%p pc=0x%08lx lr=0x%08lx sp=0x%08lx r0=0x%08lx r1=0x%08lx r2=0x%08lx "
            "r3=0x%08lx\n",
            sig, info ? info->si_addr : NULL, (unsigned long)uc->uc_mcontext.arm_pc,
            (unsigned long)uc->uc_mcontext.arm_lr, (unsigned long)uc->uc_mcontext.arm_sp,
            (unsigned long)uc->uc_mcontext.arm_r0, (unsigned long)uc->uc_mcontext.arm_r1,
            (unsigned long)uc->uc_mcontext.arm_r2, (unsigned long)uc->uc_mcontext.arm_r3);
    uint32_t *sp = (uint32_t *)(uintptr_t)uc->uc_mcontext.arm_sp;
    fprintf(stderr, "stack:");
    for (int i = 0; i < 32; ++i) {
        fprintf(stderr, " %08x", sp[i]);
    }
    fprintf(stderr, "\n");
    _Exit(128 + sig);
}

static void install_crash_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

static void terminate_handler(int sig) {
    (void)sig;
    _Exit(0);
}

static void install_terminate_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = terminate_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

int main(int argc, char **argv) {
    bool run = false;
    const char *root = NULL;
    const char *image_path = NULL;
    uint32_t display_width = 640;
    uint32_t display_height = 480;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--run") == 0) {
            run = true;
        } else if (strcmp(argv[i], "--root") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            root = argv[++i];
        } else if (strcmp(argv[i], "--display-size") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            if (!parse_display_size(argv[++i], &display_width, &display_height)) {
                fprintf(stderr, "invalid display size: %s\n", argv[i]);
                return 2;
            }
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 2;
        } else if (!image_path) {
            image_path = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!image_path) {
        usage(argv[0]);
        return 2;
    }

    if (!s3e_host_set_display_size(display_width, display_height)) {
        fprintf(stderr, "unable to allocate display surface: %ux%u\n", display_width,
                display_height);
        return 1;
    }

    install_crash_handlers();
    install_terminate_handlers();

    struct s3e_image image;
    if (!s3e_image_load(image_path, &image)) {
        return 1;
    }
    if (!s3e_image_parse_symbols(&image)) {
        s3e_image_free(&image);
        return 1;
    }

    fprintf(stderr, "S3E version=0x%x arch=0x%x symbols=%zu code=0x%x mem=0x%x\n",
            image.header.version, image.header.arch, image.symbols.count,
            image.header.code_file_size, image.header.code_mem_size);

    if (!s3e_host_init(root)) {
        s3e_image_free(&image);
        return 1;
    }
    s3e_host_set_config(image.file_data + image.header.config_offset, image.header.config_size);

    struct s3e_loaded_image loaded;
    if (!s3e_image_map_and_relocate(&image, s3e_host_resolve, &loaded)) {
        s3e_host_shutdown();
        s3e_image_free(&image);
        return 1;
    }

    fprintf(stderr, "mapped S3E at %p, entry=%p\n", (void *)loaded.base,
            (void *)(loaded.base + loaded.entry_offset));
    g_loaded_base = (uintptr_t)loaded.base;
    if (run) {
        int (*entry)(void) = (int (*)(void))(uintptr_t)(loaded.base + loaded.entry_offset);
        int rc = entry();
        fprintf(stderr, "S3E entry returned %d\n", rc);
    }

    s3e_loaded_image_unmap(&loaded);
    s3e_host_shutdown();
    s3e_image_free(&image);
    return 0;
}
