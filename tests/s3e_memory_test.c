#include "s3e_host_internal.h"

#include <assert.h>
#include <stdio.h>

static int g_alloc_calls;
static int g_realloc_calls;
static int g_free_calls;

static void *counting_alloc(uint32_t size) {
    ++g_alloc_calls;
    return malloc(size ? size : 1);
}

static void *counting_realloc(void *ptr, uint32_t size) {
    ++g_realloc_calls;
    return realloc(ptr, size ? size : 1);
}

static void counting_free(void *ptr) {
    ++g_free_calls;
    free(ptr);
}

static void *thread_alloc(uint32_t size) {
    return malloc(size ? size : 1);
}

static void *thread_realloc(void *ptr, uint32_t size) {
    return realloc(ptr, size ? size : 1);
}

static void thread_free(void *ptr) {
    free(ptr);
}

static struct s3e_user_mem_mgr default_manager(void) {
    return (struct s3e_user_mem_mgr){
        .alloc = (void *)(uintptr_t)&s3eMallocBase,
        .realloc = (void *)(uintptr_t)&s3eReallocBase,
        .free = (void *)(uintptr_t)&s3eFreeBase,
    };
}

static void assert_manager_equal(const struct s3e_user_mem_mgr *actual,
                                 const struct s3e_user_mem_mgr *expected) {
    assert(actual->alloc == expected->alloc);
    assert(actual->realloc == expected->realloc);
    assert(actual->free == expected->free);
}

static void *thread_main(void *unused) {
    (void)unused;
    struct s3e_user_mem_mgr actual;
    struct s3e_user_mem_mgr expected_default = default_manager();
    assert(s3eMemoryGetUserMemMgr(&actual) == 0);
    assert_manager_equal(&actual, &expected_default);

    struct s3e_user_mem_mgr thread_manager = {
        .alloc = (void *)(uintptr_t)&thread_alloc,
        .realloc = (void *)(uintptr_t)&thread_realloc,
        .free = (void *)(uintptr_t)&thread_free,
    };
    assert(s3eMemorySetUserMemMgr(&thread_manager) == 0);
    assert(s3eMemoryGetUserMemMgr(&actual) == 0);
    assert_manager_equal(&actual, &thread_manager);
    return NULL;
}

static void test_base_allocator_bypasses_user_manager(void) {
    struct s3e_user_mem_mgr manager = {
        .alloc = (void *)(uintptr_t)&counting_alloc,
        .realloc = (void *)(uintptr_t)&counting_realloc,
        .free = (void *)(uintptr_t)&counting_free,
    };
    assert(s3eMemorySetUserMemMgr(&manager) == 0);

    void *ptr = s3eMallocBase(32, NULL, 0);
    assert(ptr);
    memset(ptr, 0xa5, 32);
    ptr = s3eReallocBase(ptr, 64, NULL, 0);
    assert(ptr);
    s3eFreeBase(ptr);

    assert(g_alloc_calls == 0);
    assert(g_realloc_calls == 0);
    assert(g_free_calls == 0);
}

static void test_user_manager_is_thread_local(void) {
    struct s3e_user_mem_mgr main_manager;
    assert(s3eMemoryGetUserMemMgr(&main_manager) == 0);

    pthread_t thread;
    assert(pthread_create(&thread, NULL, thread_main, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);

    struct s3e_user_mem_mgr actual;
    assert(s3eMemoryGetUserMemMgr(&actual) == 0);
    assert_manager_equal(&actual, &main_manager);
}

static void test_user_manager_validation_and_reset(void) {
    struct s3e_user_mem_mgr before;
    assert(s3eMemoryGetUserMemMgr(&before) == 0);

    struct s3e_user_mem_mgr invalid = before;
    invalid.free = NULL;
    assert(s3eMemorySetUserMemMgr(&invalid) == 1);
    assert(s3eMemoryGetError() == EINVAL);

    struct s3e_user_mem_mgr actual;
    assert(s3eMemoryGetUserMemMgr(&actual) == 0);
    assert_manager_equal(&actual, &before);

    assert(s3eMemorySetUserMemMgr(NULL) == 0);
    struct s3e_user_mem_mgr expected_default = default_manager();
    assert(s3eMemoryGetUserMemMgr(&actual) == 0);
    assert_manager_equal(&actual, &expected_default);
    assert(s3eMemoryGetUserMemMgr(NULL) == 1);
}

int main(void) {
    test_base_allocator_bypasses_user_manager();
    test_user_manager_is_thread_local();
    test_user_manager_validation_and_reset();
    puts("s3e memory tests passed");
    return 0;
}
