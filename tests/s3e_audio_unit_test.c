#include "s3e_host_internal.h"

#include <assert.h>

enum {
    INTERFACE_ENTRIES = 13,
};

static void *g_capture_callback;
static void *g_render_callback;
static int g_start_calls;
static int g_stop_calls;
static int g_shutdown_calls;
static int g_audio_call;
static uint32_t g_audio_key;
static int32_t g_audio_value;

int32_t audio_unit_backend_set_callbacks(void *capture_callback, void *render_callback) {
    g_capture_callback = capture_callback;
    g_render_callback = render_callback;
    return 0;
}

int32_t audio_unit_backend_start(void) {
    g_start_calls++;
    return 0;
}

int32_t audio_unit_backend_stop(void) {
    g_stop_calls++;
    return 0;
}

void audio_unit_backend_shutdown(void) {
    g_shutdown_calls++;
}

int32_t s3eAudioPlay(const char *filename, uint32_t repeat) {
    assert(strcmp(filename, "music.mp3") == 0);
    g_audio_call = 1;
    g_audio_value = (int32_t)repeat;
    return 17;
}

int32_t s3eAudioStop(void) {
    g_audio_call = 2;
    return 18;
}

int32_t s3eAudioResume(void) {
    g_audio_call = 3;
    return 19;
}

int32_t s3eAudioPause(void) {
    g_audio_call = 4;
    return 20;
}

int32_t s3eAudioSetInt(uint32_t key, int32_t value) {
    g_audio_call = 5;
    g_audio_key = key;
    g_audio_value = value;
    return 21;
}

int32_t s3eAudioGetInt(uint32_t key) {
    g_audio_call = 6;
    g_audio_key = key;
    return 22;
}

static void test_interface_shape(void) {
    void *table[INTERFACE_ENTRIES];
    memset(table, 0, sizeof(table));

    assert(is_audio_unit_get_interface(NULL, sizeof(table)) == 1);
    assert(is_audio_unit_get_interface(table, sizeof(table) - 1) == 1);
    assert(is_audio_unit_get_interface(table, sizeof(table)) == 0);
    for (size_t index = 0; index < INTERFACE_ENTRIES; ++index) {
        assert(table[index]);
    }
}

static void test_lifecycle_and_callbacks(void) {
    void *table[INTERFACE_ENTRIES];
    assert(is_audio_unit_get_interface(table, sizeof(table)) == 0);

    int32_t (*init_fn)(void) = (int32_t (*)(void))(uintptr_t)table[2];
    int32_t (*terminate_fn)(void) = (int32_t (*)(void))(uintptr_t)table[3];
    int32_t (*set_callbacks_fn)(void *, void *) = (int32_t (*)(void *, void *))(uintptr_t)table[4];
    int32_t (*start_fn)(void) = (int32_t (*)(void))(uintptr_t)table[5];
    int32_t (*stop_fn)(void) = (int32_t (*)(void))(uintptr_t)table[6];

    assert(init_fn() == 0);
    assert(set_callbacks_fn((void *)(uintptr_t)0x1111, (void *)(uintptr_t)0x2222) == 0);
    assert(g_capture_callback == (void *)(uintptr_t)0x1111);
    assert(g_render_callback == (void *)(uintptr_t)0x2222);
    assert(start_fn() == 0);
    assert(stop_fn() == 0);
    assert(terminate_fn() == 0);
    assert(g_start_calls == 1);
    assert(g_stop_calls == 1);
    assert(g_shutdown_calls == 1);
}

static void test_music_delegates(void) {
    void *table[INTERFACE_ENTRIES];
    assert(is_audio_unit_get_interface(table, sizeof(table)) == 0);

    int32_t (*play_fn)(const char *, uint32_t) =
        (int32_t (*)(const char *, uint32_t))(uintptr_t)table[7];
    int32_t (*simple_fn)(void);
    int32_t (*set_int_fn)(uint32_t, int32_t) = (int32_t (*)(uint32_t, int32_t))(uintptr_t)table[11];
    int32_t (*get_int_fn)(uint32_t) = (int32_t (*)(uint32_t))(uintptr_t)table[12];

    assert(play_fn("music.mp3", 1) == 17);
    assert(g_audio_call == 1 && g_audio_value == 1);
    simple_fn = (int32_t (*)(void))(uintptr_t)table[8];
    assert(simple_fn() == 18 && g_audio_call == 2);
    simple_fn = (int32_t (*)(void))(uintptr_t)table[9];
    assert(simple_fn() == 19 && g_audio_call == 3);
    simple_fn = (int32_t (*)(void))(uintptr_t)table[10];
    assert(simple_fn() == 20 && g_audio_call == 4);
    assert(set_int_fn(7, 99) == 21);
    assert(g_audio_call == 5 && g_audio_key == 7 && g_audio_value == 99);
    assert(get_int_fn(8) == 22);
    assert(g_audio_call == 6 && g_audio_key == 8);
}

int main(void) {
    test_interface_shape();
    test_lifecycle_and_callbacks();
    test_music_delegates();
    puts("s3e audio unit tests passed");
    return 0;
}
