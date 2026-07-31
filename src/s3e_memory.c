#include "s3e_host_internal.h"

int g_memory_error;
__thread struct s3e_user_mem_mgr g_user_mem_mgr;
__thread int g_user_mem_mgr_set;
struct s3e_heap g_heaps[8];

static struct s3e_user_mem_mgr default_user_mem_mgr(void) {
    return (struct s3e_user_mem_mgr){
        .alloc = (void *)(uintptr_t)&s3eMallocBase,
        .realloc = (void *)(uintptr_t)&s3eReallocBase,
        .free = (void *)(uintptr_t)&s3eFreeBase,
    };
}

/* Marmalade's Base entry points back user managers and never dispatch through one. */
void *s3eMallocBase(uint32_t size, const char *file, int line) {
    (void)file;
    (void)line;
    return malloc(size ? size : 1);
}

void *s3eReallocBase(void *ptr, uint32_t size, const char *file, int line) {
    (void)file;
    (void)line;
    return realloc(ptr, size ? size : 1);
}

void s3eFreeBase(void *ptr) {
    free(ptr);
}

int32_t s3eMemoryGetInt(uint32_t key) {
    (void)key;
    return 768 * 1024 * 1024;
}

int32_t s3eMemorySetInt(uint32_t key, int32_t value) {
    (void)key;
    (void)value;
    return 0;
}

int32_t s3eMemorySetUserMemMgr(void *mgr) {
    struct s3e_user_mem_mgr candidate;
    if (mgr) {
        memcpy(&candidate, mgr, sizeof(candidate));
        if (!candidate.alloc || !candidate.realloc || !candidate.free) {
            g_memory_error = EINVAL;
            return 1;
        }
    } else {
        candidate = default_user_mem_mgr();
    }

    g_user_mem_mgr = candidate;
    g_user_mem_mgr_set = 1;
    return 0;
}

int32_t s3eMemoryGetUserMemMgr(void *out) {
    if (!out) {
        g_memory_error = EINVAL;
        return 1;
    }

    struct s3e_user_mem_mgr current = g_user_mem_mgr_set ? g_user_mem_mgr : default_user_mem_mgr();
    memcpy(out, &current, sizeof(current));
    return 0;
}

int32_t s3eMemoryHeapCreate(uint32_t heap_index) {
    if (heap_index >= sizeof(g_heaps) / sizeof(g_heaps[0])) {
        g_memory_error = EINVAL;
        return 1;
    }
    if (!g_heaps[heap_index].base) {
        uint32_t size = heap_index == 0 ? 128u * 1024u * 1024u : 16u * 1024u * 1024u;
        g_heaps[heap_index].base = calloc(1, size);
        if (!g_heaps[heap_index].base) {
            g_memory_error = ENOMEM;
            return 1;
        }
        g_heaps[heap_index].size = size;
    }
    return 0;
}

int32_t s3eMemoryHeapDestroy(uint32_t heap_index) {
    if (heap_index >= sizeof(g_heaps) / sizeof(g_heaps[0])) {
        g_memory_error = EINVAL;
        return 1;
    }
    free(g_heaps[heap_index].base);
    g_heaps[heap_index].base = NULL;
    g_heaps[heap_index].size = 0;
    return 0;
}

void *s3eMemoryHeapAddress(uint32_t heap_index) {
    if (heap_index >= sizeof(g_heaps) / sizeof(g_heaps[0])) {
        g_memory_error = EINVAL;
        return NULL;
    }
    if (!g_heaps[heap_index].base && s3eMemoryHeapCreate(heap_index) != 0) {
        return NULL;
    }
    return g_heaps[heap_index].base;
}

int32_t s3eMemoryGetError(void) {
    return g_memory_error;
}

const char *s3eMemoryGetErrorString(void) {
    return strerror(g_memory_error);
}
