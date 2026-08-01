#include "s3e_host_internal.h"

enum {
    IS_AUDIO_UNIT_INTERFACE_ENTRIES = 13,
};

static int32_t is_audio_unit_register(uint32_t id, void *callback, void *user_data) {
    (void)id;
    (void)callback;
    (void)user_data;
    return 0;
}

static int32_t is_audio_unit_unregister(uint32_t id, void *callback) {
    (void)id;
    (void)callback;
    return 0;
}

static int32_t is_audio_unit_init(void) {
    return 0;
}

static int32_t is_audio_unit_terminate(void) {
    audio_unit_backend_shutdown();
    return 0;
}

static int32_t is_audio_unit_set_callbacks(void *capture_callback, void *render_callback) {
    return audio_unit_backend_set_callbacks(capture_callback, render_callback);
}

static int32_t is_audio_unit_start(void) {
    return audio_unit_backend_start();
}

static int32_t is_audio_unit_stop(void) {
    return audio_unit_backend_stop();
}

int32_t is_audio_unit_get_interface(void *iface, uint32_t size) {
    void *table[IS_AUDIO_UNIT_INTERFACE_ENTRIES] = {
        (void *)(uintptr_t)&is_audio_unit_register,
        (void *)(uintptr_t)&is_audio_unit_unregister,
        (void *)(uintptr_t)&is_audio_unit_init,
        (void *)(uintptr_t)&is_audio_unit_terminate,
        (void *)(uintptr_t)&is_audio_unit_set_callbacks,
        (void *)(uintptr_t)&is_audio_unit_start,
        (void *)(uintptr_t)&is_audio_unit_stop,
        (void *)(uintptr_t)&s3eAudioPlay,
        (void *)(uintptr_t)&s3eAudioStop,
        (void *)(uintptr_t)&s3eAudioResume,
        (void *)(uintptr_t)&s3eAudioPause,
        (void *)(uintptr_t)&s3eAudioSetInt,
        (void *)(uintptr_t)&s3eAudioGetInt,
    };

    if (!iface || size != sizeof(table)) {
        return 1;
    }
    memcpy(iface, table, sizeof(table));
    return 0;
}
