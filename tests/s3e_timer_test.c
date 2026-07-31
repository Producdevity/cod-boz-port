#include "s3e_host_internal.h"

#include <assert.h>

enum {
    TEST_RESULT_SUCCESS = 0,
    TEST_RESULT_ERROR = 1,
    TEST_TIMER_CAPACITY = 32,
};

struct timer_event *g_timers;
pthread_mutex_t g_timer_mutex = PTHREAD_MUTEX_INITIALIZER;
uint64_t g_host_start_us;

static uint64_t g_now_ms;
static uintptr_t g_callback_order[64];
static size_t g_callback_count;

uint64_t monotonic_us(void) {
    return g_now_ms * 1000u;
}

uint64_t monotonic_ms(void) {
    return g_now_ms;
}

static void *callback_pointer(s3e_callback_fn callback) {
    return (void *)(uintptr_t)callback;
}

static size_t timer_count(void) {
    size_t count = 0;
    for (struct timer_event *timer = g_timers; timer; timer = timer->next) {
        ++count;
    }
    return count;
}

static void reset_timers(void) {
    while (g_timers) {
        struct timer_event *next = g_timers->next;
        free(g_timers);
        g_timers = next;
    }
    g_now_ms = 100;
    g_host_start_us = 0;
    g_callback_count = 0;
    memset(g_callback_order, 0, sizeof(g_callback_order));
}

static int32_t record_callback(void *system_data, void *user_data) {
    assert(system_data == NULL);
    assert(g_callback_count < sizeof(g_callback_order) / sizeof(g_callback_order[0]));
    g_callback_order[g_callback_count++] = (uintptr_t)user_data;
    return 37;
}

static void test_set_and_cancel_contract(void) {
    reset_timers();
    void *callback = callback_pointer(record_callback);
    void *first_data = (void *)(uintptr_t)1;
    void *second_data = (void *)(uintptr_t)2;

    assert(s3eTimerSetTimer(10, NULL, first_data) == TEST_RESULT_ERROR);
    assert(s3eTimerCancelTimer(NULL, first_data) == TEST_RESULT_ERROR);
    assert(s3eTimerCancelTimer(callback, first_data) == TEST_RESULT_ERROR);

    assert(s3eTimerSetTimer(50, callback, first_data) == TEST_RESULT_SUCCESS);
    assert(timer_count() == 1);
    assert(g_timers->due_ms == 150);
    assert(g_timers->callback == callback);
    assert(g_timers->user_data == first_data);

    g_now_ms = 110;
    assert(s3eTimerSetTimer(20, callback, first_data) == TEST_RESULT_SUCCESS);
    assert(timer_count() == 1);
    assert(g_timers->due_ms == 130);

    assert(s3eTimerSetTimer(5, callback, second_data) == TEST_RESULT_SUCCESS);
    assert(timer_count() == 2);
    assert(g_timers->user_data == second_data);
    assert(g_timers->next->user_data == first_data);

    assert(s3eTimerCancelTimer(callback, first_data) == TEST_RESULT_SUCCESS);
    assert(timer_count() == 1);
    assert(g_timers->user_data == second_data);
    assert(s3eTimerCancelTimer(callback, first_data) == TEST_RESULT_ERROR);
}

static void test_due_order_and_one_shot_dispatch(void) {
    reset_timers();
    void *callback = callback_pointer(record_callback);

    assert(s3eTimerSetTimer(30, callback, (void *)(uintptr_t)3) == TEST_RESULT_SUCCESS);
    assert(s3eTimerSetTimer(10, callback, (void *)(uintptr_t)1) == TEST_RESULT_SUCCESS);
    assert(s3eTimerSetTimer(20, callback, (void *)(uintptr_t)2) == TEST_RESULT_SUCCESS);
    assert(s3eTimerSetTimer(20, callback, (void *)(uintptr_t)4) == TEST_RESULT_SUCCESS);

    const uintptr_t expected_queue[] = {1, 2, 4, 3};
    struct timer_event *timer = g_timers;
    for (size_t i = 0; i < sizeof(expected_queue) / sizeof(expected_queue[0]); ++i) {
        assert(timer != NULL);
        assert((uintptr_t)timer->user_data == expected_queue[i]);
        timer = timer->next;
    }
    assert(timer == NULL);

    g_now_ms = 109;
    dispatch_due_timers();
    assert(g_callback_count == 0);

    g_now_ms = 110;
    dispatch_due_timers();
    assert(g_callback_count == 1);
    assert(g_callback_order[0] == 1);

    g_now_ms = 120;
    dispatch_due_timers();
    assert(g_callback_count == 3);
    assert(g_callback_order[1] == 2);
    assert(g_callback_order[2] == 4);

    g_now_ms = 130;
    dispatch_due_timers();
    assert(g_callback_count == 4);
    assert(g_callback_order[3] == 3);
    assert(g_timers == NULL);

    g_now_ms = 1000;
    dispatch_due_timers();
    assert(g_callback_count == 4);
}

struct cancel_context {
    void *target_callback;
    void *target_user_data;
    int32_t result;
};

static int32_t cancel_callback(void *system_data, void *user_data) {
    assert(system_data == NULL);
    struct cancel_context *context = user_data;
    context->result = s3eTimerCancelTimer(context->target_callback, context->target_user_data);
    return 0;
}

static void test_callback_can_cancel_pending_timer(void) {
    reset_timers();
    void *target_callback = callback_pointer(record_callback);
    void *target_data = (void *)(uintptr_t)7;
    struct cancel_context context = {
        .target_callback = target_callback,
        .target_user_data = target_data,
        .result = TEST_RESULT_ERROR,
    };

    assert(s3eTimerSetTimer(10, callback_pointer(cancel_callback), &context) ==
           TEST_RESULT_SUCCESS);
    assert(s3eTimerSetTimer(10, target_callback, target_data) == TEST_RESULT_SUCCESS);

    g_now_ms = 110;
    dispatch_due_timers();
    assert(context.result == TEST_RESULT_SUCCESS);
    assert(g_callback_count == 0);
    assert(g_timers == NULL);
}

struct reschedule_context {
    unsigned int calls;
};

static int32_t reschedule_callback(void *system_data, void *user_data) {
    assert(system_data == NULL);
    struct reschedule_context *context = user_data;
    ++context->calls;
    if (context->calls == 1) {
        assert(s3eTimerSetTimer(5, callback_pointer(reschedule_callback), context) ==
               TEST_RESULT_SUCCESS);
    }
    return 1;
}

static void test_callback_can_reschedule_itself(void) {
    reset_timers();
    struct reschedule_context context = {0};
    assert(s3eTimerSetTimer(0, callback_pointer(reschedule_callback), &context) ==
           TEST_RESULT_SUCCESS);

    dispatch_due_timers();
    assert(context.calls == 1);
    assert(timer_count() == 1);
    assert(g_timers->due_ms == 105);

    g_now_ms = 105;
    dispatch_due_timers();
    assert(context.calls == 2);
    assert(g_timers == NULL);
}

static void test_timer_capacity(void) {
    reset_timers();
    void *callback = callback_pointer(record_callback);
    for (uintptr_t i = 1; i <= TEST_TIMER_CAPACITY; ++i) {
        assert(s3eTimerSetTimer((uint32_t)i, callback, (void *)i) == TEST_RESULT_SUCCESS);
    }
    assert(timer_count() == TEST_TIMER_CAPACITY);
    assert(s3eTimerSetTimer(1, callback, (void *)(uintptr_t)(TEST_TIMER_CAPACITY + 1)) ==
           TEST_RESULT_ERROR);
    assert(timer_count() == TEST_TIMER_CAPACITY);

    assert(s3eTimerSetTimer(100, callback, (void *)(uintptr_t)1) == TEST_RESULT_SUCCESS);
    assert(timer_count() == TEST_TIMER_CAPACITY);
    assert(g_timers->next != NULL);
}

static void test_elapsed_timer(void) {
    reset_timers();
    assert(s3eTimerGetMs() == 0);
    g_now_ms = 107;
    assert(s3eTimerGetUST() == 7);
    g_now_ms = 99;
    assert(s3eTimerGetMs() == 0);
}

int main(void) {
    test_set_and_cancel_contract();
    test_due_order_and_one_shot_dispatch();
    test_callback_can_cancel_pending_timer();
    test_callback_can_reschedule_itself();
    test_timer_capacity();
    test_elapsed_timer();
    reset_timers();
    puts("s3e timer tests passed");
    return 0;
}
