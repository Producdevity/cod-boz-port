#include "s3e_host_internal.h"

#include <assert.h>

enum {
    TEST_MIXER_CHANNELS = 40,
    TEST_SOUND_END_CALLBACK = 0,
    TEST_SOUND_STOP_CALLBACK = 2,
    TEST_WAV_HEADER_SIZE = 44,
};

struct test_chunk {
    uint8_t *bytes;
    uint32_t size;
};

struct test_end_sample_info {
    int32_t channel;
    int32_t reps_remaining;
    uint32_t new_data;
    uint32_t num_samples;
};

char g_root[1024] = ".";

static struct test_chunk *g_channels[TEST_MIXER_CHANNELS];
static int g_playing[TEST_MIXER_CHANNELS];
static int g_last_loops;
static void (*g_finished_callback)(int channel);
static int g_end_calls;
static int g_stop_calls;
static int g_last_reps_remaining;

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static int fake_sdl_init(uint32_t flags) {
    (void)flags;
    return 0;
}

static void fake_sdl_quit(uint32_t flags) {
    (void)flags;
}

static void *fake_rw_from_const_mem(const void *memory, int size) {
    (void)size;
    return (void *)memory;
}

static const char *fake_error(void) {
    return "fake audio error";
}

static int fake_mix_init(int flags) {
    return flags;
}

static void fake_mix_quit(void) {}

static int fake_open_audio(int frequency, uint16_t format, int channels, int chunk_size) {
    (void)frequency;
    (void)format;
    (void)channels;
    (void)chunk_size;
    return 0;
}

static void fake_close_audio(void) {}

static int fake_allocate_channels(int channel_count) {
    assert(channel_count == TEST_MIXER_CHANNELS);
    return channel_count;
}

static void fake_channel_finished(void (*callback)(int channel)) {
    g_finished_callback = callback;
}

static int fake_play_channel(int channel, void *chunk, int loops, int ticks) {
    assert(channel >= 0 && channel < TEST_MIXER_CHANNELS);
    assert(ticks == -1);
    g_channels[channel] = chunk;
    g_playing[channel] = 1;
    g_last_loops = loops;
    return channel;
}

static int fake_playing(int channel) {
    return g_playing[channel];
}

static int fake_halt_channel(int channel) {
    if (g_playing[channel]) {
        g_playing[channel] = 0;
        g_finished_callback(channel);
    }
    return 0;
}

static void fake_pause(int channel) {
    (void)channel;
}

static void fake_resume(int channel) {
    (void)channel;
}

static int fake_volume(int channel, int volume) {
    (void)channel;
    return volume;
}

static void *fake_load_wav(void *source, int free_source) {
    (void)free_source;
    const uint8_t *bytes = source;
    uint32_t size = read_le32(bytes + 4) + 8;
    assert(size >= TEST_WAV_HEADER_SIZE);

    struct test_chunk *chunk = malloc(sizeof(*chunk));
    assert(chunk);
    chunk->bytes = malloc(size);
    assert(chunk->bytes);
    memcpy(chunk->bytes, bytes, size);
    chunk->size = size;
    return chunk;
}

static void fake_free_chunk(void *opaque_chunk) {
    struct test_chunk *chunk = opaque_chunk;
    for (int channel = 0; channel < TEST_MIXER_CHANNELS; ++channel) {
        if (g_channels[channel] == chunk) {
            g_channels[channel] = NULL;
        }
    }
    free(chunk->bytes);
    free(chunk);
}

static int fake_query_spec(int *frequency, uint16_t *format, int *channels) {
    *frequency = 22050;
    *format = 0x8010;
    *channels = 2;
    return 1;
}

static void fake_set_post_mix(void (*callback)(void *, uint8_t *, int), void *user_data) {
    (void)callback;
    (void)user_data;
}

void *open_first(const char *const *names) {
    assert(names && names[0]);
    return strstr(names[0], "mixer") ? (void *)(uintptr_t)2 : (void *)(uintptr_t)1;
}

void *dlsym(void *handle, const char *name) {
    (void)handle;
#define TEST_SYMBOL(symbol, function)                                                              \
    if (strcmp(name, symbol) == 0) {                                                               \
        return (void *)(uintptr_t)&function;                                                       \
    }
    TEST_SYMBOL("SDL_InitSubSystem", fake_sdl_init)
    TEST_SYMBOL("SDL_QuitSubSystem", fake_sdl_quit)
    TEST_SYMBOL("SDL_RWFromConstMem", fake_rw_from_const_mem)
    TEST_SYMBOL("SDL_GetError", fake_error)
    TEST_SYMBOL("Mix_Init", fake_mix_init)
    TEST_SYMBOL("Mix_Quit", fake_mix_quit)
    TEST_SYMBOL("Mix_OpenAudio", fake_open_audio)
    TEST_SYMBOL("Mix_CloseAudio", fake_close_audio)
    TEST_SYMBOL("Mix_AllocateChannels", fake_allocate_channels)
    TEST_SYMBOL("Mix_ChannelFinished", fake_channel_finished)
    TEST_SYMBOL("Mix_PlayChannelTimed", fake_play_channel)
    TEST_SYMBOL("Mix_Playing", fake_playing)
    TEST_SYMBOL("Mix_HaltChannel", fake_halt_channel)
    TEST_SYMBOL("Mix_Pause", fake_pause)
    TEST_SYMBOL("Mix_Resume", fake_resume)
    TEST_SYMBOL("Mix_Volume", fake_volume)
    TEST_SYMBOL("Mix_LoadWAV_RW", fake_load_wav)
    TEST_SYMBOL("Mix_FreeChunk", fake_free_chunk)
    TEST_SYMBOL("Mix_QuerySpec", fake_query_spec)
    TEST_SYMBOL("Mix_SetPostMix", fake_set_post_mix)
    TEST_SYMBOL("Mix_GetError", fake_error)
#undef TEST_SYMBOL
    return NULL;
}

int dlclose(void *handle) {
    (void)handle;
    return 0;
}

static int32_t end_callback(void *system_data, void *user_data) {
    (void)user_data;
    struct test_end_sample_info *info = system_data;
    g_end_calls++;
    g_last_reps_remaining = info->reps_remaining;
    return info->reps_remaining > 0;
}

static int32_t stop_callback(void *system_data, void *user_data) {
    (void)system_data;
    (void)user_data;
    g_stop_calls++;
    return 0;
}

static const int16_t *chunk_samples(int channel, uint32_t *sample_count) {
    struct test_chunk *chunk = g_channels[channel];
    assert(chunk && chunk->size >= TEST_WAV_HEADER_SIZE);
    assert(memcmp(chunk->bytes, "RIFF", 4) == 0);
    uint32_t byte_count = read_le32(chunk->bytes + 40);
    assert(TEST_WAV_HEADER_SIZE + byte_count == chunk->size);
    *sample_count = byte_count / sizeof(int16_t);
    return (const int16_t *)(const void *)(chunk->bytes + TEST_WAV_HEADER_SIZE);
}

static void finish_channel(int channel) {
    assert(g_playing[channel]);
    g_playing[channel] = 0;
    g_finished_callback(channel);
    audio_pump();
}

static void test_sound_repeat_and_loop(void) {
    const int16_t pcm[] = {100, 200, 300, 400, 500};
    assert(s3eSoundChannelRegister(0, TEST_SOUND_END_CALLBACK, (void *)(uintptr_t)&end_callback,
                                   NULL) == 0);
    assert(s3eSoundChannelRegister(0, TEST_SOUND_STOP_CALLBACK, (void *)(uintptr_t)&stop_callback,
                                   NULL) == 0);
    assert(s3eSoundChannelPlay(0, pcm, 5, 2, 2) == 0);

    uint32_t count = 0;
    const int16_t *samples = chunk_samples(0, &count);
    assert(count == 5 && memcmp(samples, pcm, sizeof(pcm)) == 0);

    finish_channel(0);
    samples = chunk_samples(0, &count);
    assert(g_end_calls == 1 && g_last_reps_remaining == 1);
    assert(count == 3 && memcmp(samples, pcm + 2, 3 * sizeof(*pcm)) == 0);

    finish_channel(0);
    assert(g_end_calls == 2 && g_last_reps_remaining == 0);
    assert(g_stop_calls == 1 && !g_channels[0]);
}

static void test_raw_pcm_and_explicit_stop(void) {
    const int16_t pcm[] = {-1000, 0, 1000};
    assert(s3eSoundChannelPlay(1, pcm, 3, 1, 0) == 0);
    uint32_t count = 0;
    const int16_t *samples = chunk_samples(1, &count);
    assert(count == 3 && memcmp(samples, pcm, sizeof(pcm)) == 0);

    assert(s3eSoundChannelRegister(1, TEST_SOUND_STOP_CALLBACK, (void *)(uintptr_t)&stop_callback,
                                   NULL) == 0);
    assert(s3eSoundChannelStop(1) == 0);
    assert(g_stop_calls == 2 && !g_channels[1]);
}

static void test_stream_repeat_contract(void) {
    uint8_t wav[TEST_WAV_HEADER_SIZE + sizeof(int16_t)] = {0};
    memcpy(wav, "RIFF", 4);
    uint32_t riff_size = sizeof(wav) - 8;
    memcpy(wav + 4, &riff_size, sizeof(riff_size));

    assert(s3eAudioPlayFromBuffer(wav, sizeof(wav), 0) == 0);
    assert(g_last_loops == -1);
    assert(s3eAudioPlayFromBuffer(wav, sizeof(wav), 1) == 0);
    assert(g_last_loops == 0);
    assert(s3eAudioPlayFromBuffer(wav, sizeof(wav), 3) == 0);
    assert(g_last_loops == 2);
    assert(s3eAudioStop() == 0);
}

int main(void) {
    test_sound_repeat_and_loop();
    test_raw_pcm_and_explicit_stop();
    test_stream_repeat_contract();
    audio_shutdown();
    puts("s3e audio tests passed");
    return 0;
}
