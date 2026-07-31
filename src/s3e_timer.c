#include "s3e_host_internal.h"

enum {
    S3E_TIMER_SUCCESS = 0,
    S3E_TIMER_ERROR = 1,
    S3E_TIMER_MAX_EVENTS = 32,
};

static uint64_t g_timer_sequence;

static uint64_t timer_elapsed_ms(void) {
    uint64_t now = monotonic_us();
    if (!g_host_start_us || now < g_host_start_us) {
        g_host_start_us = now;
    }
    return (now - g_host_start_us) / 1000u;
}

static struct timer_event *remove_timer_locked(void *callback, void *user_data) {
    struct timer_event **link = &g_timers;
    while (*link) {
        struct timer_event *timer = *link;
        if (timer->callback == callback && timer->user_data == user_data) {
            *link = timer->next;
            timer->next = NULL;
            return timer;
        }
        link = &timer->next;
    }
    return NULL;
}

static size_t timer_count_locked(void) {
    size_t count = 0;
    for (struct timer_event *timer = g_timers; timer; timer = timer->next) {
        ++count;
    }
    return count;
}

uint64_t s3eTimerGetUST(void) {
    return timer_elapsed_ms();
}

uint64_t s3eTimerGetMs(void) {
    return timer_elapsed_ms();
}

int32_t s3eTimerGetInt(uint32_t key) {
    return key == 0 ? 1 : -1;
}

void dispatch_due_timers(void) {
    uint64_t now = monotonic_ms();
    pthread_mutex_lock(&g_timer_mutex);
    uint64_t sequence_limit = g_timer_sequence;
    pthread_mutex_unlock(&g_timer_mutex);
    while (1) {
        pthread_mutex_lock(&g_timer_mutex);
        struct timer_event *timer = g_timers;
        if (!timer || timer->due_ms > now || timer->sequence > sequence_limit) {
            pthread_mutex_unlock(&g_timer_mutex);
            break;
        }
        g_timers = timer->next;
        timer->next = NULL;
        pthread_mutex_unlock(&g_timer_mutex);

        ((s3e_callback_fn)(uintptr_t)timer->callback)(NULL, timer->user_data);
        free(timer);
    }
}

int32_t s3eTimerSetTimer(uint32_t period_ms, void *callback, void *user_data) {
    pthread_mutex_lock(&g_timer_mutex);
    struct timer_event *timer = remove_timer_locked(callback, user_data);

    if (!callback) {
        pthread_mutex_unlock(&g_timer_mutex);
        free(timer);
        return S3E_TIMER_ERROR;
    }
    if (!timer && timer_count_locked() >= S3E_TIMER_MAX_EVENTS) {
        pthread_mutex_unlock(&g_timer_mutex);
        return S3E_TIMER_ERROR;
    }
    if (!timer) {
        timer = calloc(1, sizeof(*timer));
        if (!timer) {
            pthread_mutex_unlock(&g_timer_mutex);
            return S3E_TIMER_ERROR;
        }
    }

    timer->due_ms = monotonic_ms() + period_ms;
    timer->sequence = ++g_timer_sequence;
    if (!timer->sequence) {
        timer->sequence = ++g_timer_sequence;
    }
    timer->callback = callback;
    timer->user_data = user_data;

    struct timer_event **link = &g_timers;
    while (*link && (*link)->due_ms <= timer->due_ms) {
        link = &(*link)->next;
    }
    timer->next = *link;
    *link = timer;
    pthread_mutex_unlock(&g_timer_mutex);
    return S3E_TIMER_SUCCESS;
}

int32_t s3eTimerCancelTimer(void *callback, void *user_data) {
    if (!callback) {
        return S3E_TIMER_ERROR;
    }

    pthread_mutex_lock(&g_timer_mutex);
    struct timer_event *timer = remove_timer_locked(callback, user_data);
    pthread_mutex_unlock(&g_timer_mutex);
    free(timer);
    return timer ? S3E_TIMER_SUCCESS : S3E_TIMER_ERROR;
}

uint64_t s3eTimerGetUTC(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int64_t s3eTimerGetLocaltimeOffset(const uint64_t *utc_ms) {
    time_t now = utc_ms ? (time_t)(*utc_ms / 1000u) : time(NULL);
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    return (int64_t)local_tm.tm_gmtoff * 1000;
}
