#include "s3e_host_internal.h"

#include <stdatomic.h>

enum {
    SDL_INIT_AUDIO = 0x00000010u,
    AUDIO_S16LSB = 0x8010u,
    MIX_INIT_MP3 = 0x00000008u,
    MIX_MAX_VOLUME = 128,
    S3E_MAX_VOLUME = 256,
    SOUND_CHANNELS = 24,
    AUDIO_CHANNELS = 16,
    MIXER_CHANNELS = SOUND_CHANNELS + AUDIO_CHANNELS,
    AUDIO_STATUS_STOPPED = 0,
    AUDIO_STATUS_PLAYING = 1,
    AUDIO_STATUS_PAUSED = 2,
    SOUND_FREQUENCY = 22050,
    SOUND_RATE_DEFAULT = 22050,
    SOUND_RATE_SCALE_DEFAULT = 0x10000,
    SOUND_RATE_MAX = 0x40000,
    SOUND_OUTPUT_CHANNELS = 2,
    SOUND_BUFFER_SAMPLES = 1024,
    SOUND_GENERATOR_CHUNK_SAMPLES = 4096,
    SOUND_GENERATOR_MAX_EXPANSION = 8,
    WAV_HEADER_BYTES = 44,
    S3E_CHANNEL_END_SAMPLE = 0,
    S3E_CHANNEL_GEN_AUDIO = 1,
    S3E_CHANNEL_STOP_AUDIO = 2,
    S3E_CHANNEL_GEN_AUDIO_STEREO = 3,
    S3E_CHANNEL_CALLBACK_COUNT = 4,
    S3E_AUDIO_STOP = 2,
    S3E_AUDIO_CALLBACK_COUNT = 3,
};

struct s3e_sound_end_sample_info {
    int32_t channel;
    int32_t reps_remaining;
    uint32_t new_data;
    uint32_t num_samples;
};

struct s3e_sound_gen_audio_info {
    int32_t channel;
    uint32_t target;
    uint32_t num_samples;
    int32_t mix;
    uint32_t orig_start;
    uint32_t orig_num_samples;
    int32_t reps_remaining;
    uint32_t end_sample;
};

_Static_assert(sizeof(struct s3e_sound_end_sample_info) == 16,
               "s3eSoundEndSampleInfo ABI mismatch");
_Static_assert(sizeof(struct s3e_sound_gen_audio_info) == 32, "s3eSoundGenAudioInfo ABI mismatch");

struct sdl_audio_api {
    int (*InitSubSystem)(uint32_t flags);
    void (*QuitSubSystem)(uint32_t flags);
    void *(*RWFromConstMem)(const void *mem, int size);
    const char *(*GetError)(void);
};

struct sdl_mixer_api {
    int (*Init)(int flags);
    void (*Quit)(void);
    int (*OpenAudio)(int frequency, uint16_t format, int channels, int chunksize);
    void (*CloseAudio)(void);
    int (*AllocateChannels)(int num_channels);
    void (*ChannelFinished)(void (*callback)(int channel));
    int (*PlayChannelTimed)(int channel, void *chunk, int loops, int ticks);
    int (*Playing)(int channel);
    int (*HaltChannel)(int channel);
    void (*Pause)(int channel);
    void (*Resume)(int channel);
    int (*Volume)(int channel, int volume);
    void *(*LoadWAV_RW)(void *src, int freesrc);
    void (*FreeChunk)(void *chunk);
    const char *(*GetError)(void);
};

struct sound_callback {
    void *function;
    void *user_data;
};

struct sound_slot {
    void *chunk;
    atomic_int finished;
    struct sound_callback callbacks[S3E_CHANNEL_CALLBACK_COUNT];
    int volume_s3e;
    int volume_mix;
    int rate;
    int rate_scale;
    int user_value;
    int generator_decoding;
    const void *source_data;
    uint32_t source_samples;
    uint32_t reps_remaining;
    uint32_t loop_index;
};

struct audio_slot {
    void *chunk;
    atomic_int finished;
    struct sound_callback callbacks[S3E_AUDIO_CALLBACK_COUNT];
    int volume_s3e;
    int volume_mix;
    int paused;
};

static void *g_sdl2_audio;
static void *g_sdl_mixer;
static struct sdl_audio_api g_sdl_audio;
static struct sdl_mixer_api g_mixer;
static struct sound_slot g_sound_slots[SOUND_CHANNELS];
static struct audio_slot g_audio_slots[AUDIO_CHANNELS];
static int g_audio_tried;
static int g_audio_ready;
static int g_audio_channel;
static int g_sound_volume_s3e = S3E_MAX_VOLUME;
static int g_sound_volume_mix = MIX_MAX_VOLUME;
static int g_sound_rate = SOUND_RATE_DEFAULT;
static int g_mixer_mp3_ready;
static int g_audio_slots_initialized;
static int g_sound_slots_initialized;
static int g_audio_pumping;

static void service_finished_audio(void);

static int load_symbol(void *handle, void **slot, const char *name) {
    *slot = dlsym(handle, name);
    return *slot != NULL;
}

static void load_optional_symbol(void *handle, void **slot, const char *name) {
    *slot = dlsym(handle, name);
}

static const char *mixer_error(void) {
    if (g_mixer.GetError) {
        const char *error = g_mixer.GetError();
        if (error && error[0]) {
            return error;
        }
    }
    if (g_sdl_audio.GetError) {
        const char *error = g_sdl_audio.GetError();
        if (error && error[0]) {
            return error;
        }
    }
    return "unknown error";
}

static int clamp_volume_256(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > S3E_MAX_VOLUME) {
        return S3E_MAX_VOLUME;
    }
    return value;
}

static int mix_volume_from_s3e(int value) {
    return clamp_volume_256(value) * MIX_MAX_VOLUME / S3E_MAX_VOLUME;
}

static int audio_mixer_loops(uint32_t repeat) {
    return repeat == 0 ? -1 : (int)repeat - 1;
}

static int mixed_sound_volume(const struct sound_slot *slot) {
    return slot->volume_mix * g_sound_volume_mix / MIX_MAX_VOLUME;
}

static int sound_channel_valid(int channel) {
    return channel >= 0 && channel < SOUND_CHANNELS;
}

static int audio_channel_valid(int channel) {
    return channel >= 0 && channel < AUDIO_CHANNELS;
}

static int audio_mixer_channel(int channel) {
    return SOUND_CHANNELS + channel;
}

static int clamp_rate(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > SOUND_RATE_MAX) {
        return SOUND_RATE_MAX;
    }
    return value;
}

static void init_sound_slots(void) {
    if (g_sound_slots_initialized) {
        return;
    }
    for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
        g_sound_slots[channel].volume_s3e = S3E_MAX_VOLUME;
        g_sound_slots[channel].volume_mix = MIX_MAX_VOLUME;
        g_sound_slots[channel].rate = SOUND_RATE_DEFAULT;
        g_sound_slots[channel].rate_scale = SOUND_RATE_SCALE_DEFAULT;
    }
    g_sound_slots_initialized = 1;
}

static void init_audio_slots(void) {
    if (g_audio_slots_initialized) {
        return;
    }
    for (int channel = 0; channel < AUDIO_CHANNELS; ++channel) {
        g_audio_slots[channel].volume_s3e = S3E_MAX_VOLUME;
        g_audio_slots[channel].volume_mix = MIX_MAX_VOLUME;
    }
    g_audio_slots_initialized = 1;
}

static int rate_from_scale(int scale) {
    int scaled = scale >> 8;
    int64_t rate = (int64_t)g_sound_rate * scaled;
    if (rate < 0) {
        rate -= 0xff;
    } else {
        rate += 0xff;
    }
    return clamp_rate((int)(rate >> 8));
}

static void write_le16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
    dst[2] = (uint8_t)((value >> 16) & 0xffu);
    dst[3] = (uint8_t)(value >> 24);
}

static uint8_t *create_pcm16_mono_wav(const int16_t *data, uint32_t samples, int sample_rate,
                                      uint32_t *out_byte_count) {
    if (!data || samples == 0 || samples > (UINT32_MAX - WAV_HEADER_BYTES) / sizeof(int16_t)) {
        return NULL;
    }
    if (sample_rate <= 0 || sample_rate > SOUND_RATE_MAX) {
        sample_rate = SOUND_RATE_DEFAULT;
    }

    uint32_t data_size = samples * (uint32_t)sizeof(int16_t);
    uint32_t byte_count = WAV_HEADER_BYTES + data_size;
    uint8_t *buffer = malloc(byte_count);
    if (!buffer) {
        return NULL;
    }

    memcpy(buffer, "RIFF", 4);
    write_le32(buffer + 4, byte_count - 8u);
    memcpy(buffer + 8, "WAVEfmt ", 8);
    write_le32(buffer + 16, 16);
    write_le16(buffer + 20, 1);
    write_le16(buffer + 22, 1);
    write_le32(buffer + 24, (uint32_t)sample_rate);
    write_le32(buffer + 28, (uint32_t)sample_rate * sizeof(int16_t));
    write_le16(buffer + 32, sizeof(int16_t));
    write_le16(buffer + 34, 16);
    memcpy(buffer + 36, "data", 4);
    write_le32(buffer + 40, data_size);
    memcpy(buffer + WAV_HEADER_BYTES, data, data_size);

    *out_byte_count = byte_count;
    return buffer;
}

static int32_t invoke_callback(const struct sound_callback *callback, void *system_data) {
    if (!callback->function) {
        return 0;
    }
    return ((s3e_callback_fn)(uintptr_t)callback->function)(system_data, callback->user_data);
}

static int render_generator_audio(int channel, const void *data, uint32_t samples,
                                  int16_t **out_pcm, uint32_t *out_samples) {
    struct sound_slot *slot = &g_sound_slots[channel];
    const struct sound_callback *callback = &slot->callbacks[S3E_CHANNEL_GEN_AUDIO];
    if (!callback->function) {
        return 0;
    }

    if (samples > (UINT32_MAX - SOUND_GENERATOR_CHUNK_SAMPLES) / SOUND_GENERATOR_MAX_EXPANSION) {
        return -1;
    }
    uint32_t limit = samples * SOUND_GENERATOR_MAX_EXPANSION + SOUND_GENERATOR_CHUNK_SAMPLES;
    uint32_t capacity = SOUND_GENERATOR_CHUNK_SAMPLES;
    if (capacity > limit) {
        capacity = limit;
    }
    int16_t *pcm = malloc((size_t)capacity * sizeof(*pcm));
    if (!pcm) {
        return -1;
    }

    uint32_t written = 0;
    for (;;) {
        if (capacity == written) {
            uint32_t next = capacity > limit / 2 ? limit : capacity * 2;
            if (next <= capacity) {
                free(pcm);
                return -1;
            }
            int16_t *grown = realloc(pcm, (size_t)next * sizeof(*pcm));
            if (!grown) {
                free(pcm);
                return -1;
            }
            pcm = grown;
            capacity = next;
        }

        uint32_t requested = capacity - written;
        if (requested > SOUND_GENERATOR_CHUNK_SAMPLES) {
            requested = SOUND_GENERATOR_CHUNK_SAMPLES;
        }
        struct s3e_sound_gen_audio_info info = {
            .channel = channel,
            .target = (uint32_t)(uintptr_t)(pcm + written),
            .num_samples = requested,
            .mix = 0,
            .orig_start = (uint32_t)(uintptr_t)data,
            .orig_num_samples = samples,
            .reps_remaining = 0,
            .end_sample = 0,
        };

        slot->generator_decoding = 1;
        int32_t produced = invoke_callback(callback, &info);
        slot->generator_decoding = 0;
        if (produced < 0 || (uint32_t)produced > requested || (produced == 0 && !info.end_sample)) {
            free(pcm);
            return -1;
        }
        written += (uint32_t)produced;
        if (info.end_sample) {
            break;
        }
    }

    if (!written) {
        free(pcm);
        return -1;
    }
    *out_pcm = pcm;
    *out_samples = written;
    return 1;
}

static void free_sound_slot(int channel) {
    if (!sound_channel_valid(channel)) {
        return;
    }
    struct sound_slot *slot = &g_sound_slots[channel];
    if (slot->chunk && g_mixer.FreeChunk) {
        g_mixer.FreeChunk(slot->chunk);
    }
    slot->chunk = NULL;
    atomic_store_explicit(&slot->finished, 0, memory_order_release);
    slot->generator_decoding = 0;
    slot->source_data = NULL;
    slot->source_samples = 0;
    slot->reps_remaining = 0;
    slot->loop_index = 0;
}

static void free_audio_slot(int channel) {
    if (!audio_channel_valid(channel)) {
        return;
    }
    struct audio_slot *slot = &g_audio_slots[channel];
    if (slot->chunk && g_mixer.FreeChunk) {
        g_mixer.FreeChunk(slot->chunk);
    }
    slot->chunk = NULL;
    atomic_store_explicit(&slot->finished, 0, memory_order_release);
    slot->paused = 0;
}

static void mixer_channel_finished(int channel) {
    if (sound_channel_valid(channel)) {
        atomic_store_explicit(&g_sound_slots[channel].finished, 1, memory_order_release);
        return;
    }
    int audio_channel = channel - SOUND_CHANNELS;
    if (audio_channel_valid(audio_channel)) {
        atomic_store_explicit(&g_audio_slots[audio_channel].finished, 1, memory_order_release);
    }
}

static void service_finished_sound(void) {
    if (!g_audio_ready) {
        return;
    }
    for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
        struct sound_slot *slot = &g_sound_slots[channel];
        if (!slot->chunk) {
            continue;
        }
        if (!atomic_load_explicit(&slot->finished, memory_order_acquire) && g_mixer.Playing &&
            g_mixer.Playing(channel) != 0) {
            continue;
        }

        const void *source_data = slot->source_data;
        uint32_t source_samples = slot->source_samples;
        uint32_t reps_remaining = slot->reps_remaining;
        int repeat_forever = reps_remaining == 0;
        uint32_t loop_index = slot->loop_index;
        if (reps_remaining > 0) {
            --reps_remaining;
        }
        free_sound_slot(channel);

        struct s3e_sound_end_sample_info info = {
            .channel = channel,
            .reps_remaining = (int32_t)reps_remaining,
            .new_data = 0,
            .num_samples = source_samples,
        };
        const struct sound_callback *end_callback = &slot->callbacks[S3E_CHANNEL_END_SAMPLE];
        int32_t keep_playing = end_callback->function ? invoke_callback(end_callback, &info)
                                                      : repeat_forever || info.reps_remaining > 0;
        if (keep_playing) {
            const void *next_data =
                info.new_data ? (const void *)(uintptr_t)info.new_data : source_data;
            uint32_t next_samples = info.new_data ? info.num_samples : source_samples;
            uint32_t next_repeat = info.reps_remaining > 0 ? (uint32_t)info.reps_remaining : 0;
            if (next_data && next_samples &&
                s3eSoundChannelPlay(channel, next_data, next_samples, next_repeat, loop_index) ==
                    0) {
                continue;
            }
        }
        invoke_callback(&slot->callbacks[S3E_CHANNEL_STOP_AUDIO], &info);
    }
}

void audio_pump(void) {
    if (g_audio_pumping) {
        return;
    }
    g_audio_pumping = 1;
    service_finished_audio();
    service_finished_sound();
    g_audio_pumping = 0;
}

static int init_audio_subsystem(void) {
    if (g_sdl_audio.InitSubSystem(SDL_INIT_AUDIO) == 0) {
        return 1;
    }

    const char *requested_driver = getenv("SDL_AUDIODRIVER");
    if (!requested_driver || !requested_driver[0]) {
        fprintf(stderr, "[audio] SDL audio init failed: %s\n", mixer_error());
        return 0;
    }

    fprintf(stderr, "[audio] SDL audio driver '%s' failed (%s); retrying automatic selection\n",
            requested_driver, mixer_error());
    unsetenv("SDL_AUDIODRIVER");
    if (g_sdl_audio.InitSubSystem(SDL_INIT_AUDIO) == 0) {
        return 1;
    }

    fprintf(stderr, "[audio] SDL audio init failed: %s\n", mixer_error());
    return 0;
}

static int audio_open(void) {
    if (g_audio_tried) {
        return g_audio_ready;
    }
    g_audio_tried = 1;

    const char *sdl_names[] = {"libSDL2-2.0.so.0", "libSDL2.so", NULL};
    const char *mixer_names[] = {"libSDL2_mixer-2.0.so.0", "libSDL2_mixer.so", NULL};
    g_sdl2_audio = open_first(sdl_names);
    g_sdl_mixer = open_first(mixer_names);
    if (!g_sdl2_audio || !g_sdl_mixer) {
        fprintf(stderr, "[audio] SDL2_mixer unavailable\n");
        return 0;
    }

    int ok = 1;
    ok &= load_symbol(g_sdl2_audio, (void **)&g_sdl_audio.InitSubSystem, "SDL_InitSubSystem");
    ok &= load_symbol(g_sdl2_audio, (void **)&g_sdl_audio.QuitSubSystem, "SDL_QuitSubSystem");
    ok &= load_symbol(g_sdl2_audio, (void **)&g_sdl_audio.RWFromConstMem, "SDL_RWFromConstMem");
    load_optional_symbol(g_sdl2_audio, (void **)&g_sdl_audio.GetError, "SDL_GetError");

    load_optional_symbol(g_sdl_mixer, (void **)&g_mixer.Init, "Mix_Init");
    load_optional_symbol(g_sdl_mixer, (void **)&g_mixer.Quit, "Mix_Quit");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.OpenAudio, "Mix_OpenAudio");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.CloseAudio, "Mix_CloseAudio");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.AllocateChannels, "Mix_AllocateChannels");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.ChannelFinished, "Mix_ChannelFinished");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.PlayChannelTimed, "Mix_PlayChannelTimed");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.Playing, "Mix_Playing");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.HaltChannel, "Mix_HaltChannel");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.Pause, "Mix_Pause");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.Resume, "Mix_Resume");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.Volume, "Mix_Volume");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.LoadWAV_RW, "Mix_LoadWAV_RW");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.FreeChunk, "Mix_FreeChunk");
    load_optional_symbol(g_sdl_mixer, (void **)&g_mixer.GetError, "Mix_GetError");

    if (!ok) {
        fprintf(stderr, "[audio] SDL2_mixer symbols unavailable\n");
        return 0;
    }
    if (!init_audio_subsystem()) {
        return 0;
    }
    if (g_mixer.Init) {
        g_mixer_mp3_ready = (g_mixer.Init(MIX_INIT_MP3) & MIX_INIT_MP3) != 0;
    }
    if (g_mixer.OpenAudio(SOUND_FREQUENCY, AUDIO_S16LSB, SOUND_OUTPUT_CHANNELS,
                          SOUND_BUFFER_SAMPLES) != 0) {
        fprintf(stderr, "[audio] SDL mixer open failed: %s\n", mixer_error());
        return 0;
    }

    if (g_mixer.AllocateChannels(MIXER_CHANNELS) != MIXER_CHANNELS) {
        fprintf(stderr, "[audio] SDL mixer channel allocation failed\n");
        g_mixer.CloseAudio();
        return 0;
    }
    g_mixer.ChannelFinished(mixer_channel_finished);
    init_audio_slots();
    init_sound_slots();
    for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
        g_mixer.Volume(channel, mixed_sound_volume(&g_sound_slots[channel]));
    }
    for (int channel = 0; channel < AUDIO_CHANNELS; ++channel) {
        g_mixer.Volume(audio_mixer_channel(channel), g_audio_slots[channel].volume_mix);
    }
    if (!g_mixer_mp3_ready) {
        fprintf(stderr, "[audio] MP3 decoder not reported by SDL2_mixer\n");
    }
    g_audio_ready = 1;
    return 1;
}

static void notify_audio_stopped(int channel) {
    int32_t system_data = channel;
    invoke_callback(&g_audio_slots[channel].callbacks[S3E_AUDIO_STOP], &system_data);
}

static void stop_audio_channel(int channel, int notify) {
    struct audio_slot *slot = &g_audio_slots[channel];
    int had_audio = slot->chunk != NULL;
    if (g_audio_ready && had_audio) {
        g_mixer.HaltChannel(audio_mixer_channel(channel));
    }
    free_audio_slot(channel);
    if (notify && had_audio) {
        notify_audio_stopped(channel);
    }
}

static void service_finished_audio(void) {
    if (!g_audio_ready) {
        return;
    }
    for (int channel = 0; channel < AUDIO_CHANNELS; ++channel) {
        struct audio_slot *slot = &g_audio_slots[channel];
        if (!slot->chunk) {
            continue;
        }
        int mixer_channel = audio_mixer_channel(channel);
        if (!atomic_load_explicit(&slot->finished, memory_order_acquire) &&
            g_mixer.Playing(mixer_channel) != 0) {
            continue;
        }
        free_audio_slot(channel);
        notify_audio_stopped(channel);
    }
}

static int resolve_audio_path(const char *name, char *out, size_t out_size) {
    if (!name || !name[0]) {
        return 0;
    }
    if (name[0] == '/') {
        snprintf(out, out_size, "%s", name);
        return access(out, R_OK) == 0;
    }

    const char *patterns[] = {
        "%s/%s",
        "%s/assets/%s",
        "%s/assets/data-gles1/%s",
        "%s/assets/data-sw/%s",
    };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        snprintf(out, out_size, patterns[i], g_root, name);
        if (access(out, R_OK) == 0) {
            return 1;
        }
    }
    return 0;
}

static uint8_t *read_audio_file(const char *path, uint32_t *out_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > INT32_MAX) {
        close(fd);
        return NULL;
    }

    uint32_t size = (uint32_t)st.st_size;
    uint8_t *data = malloc(size);
    if (!data) {
        close(fd);
        return NULL;
    }

    uint32_t offset = 0;
    while (offset < size) {
        ssize_t count = read(fd, data + offset, size - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            free(data);
            close(fd);
            return NULL;
        }
        offset += (uint32_t)count;
    }
    close(fd);
    *out_size = size;
    return data;
}

static int play_audio_buffer(const void *buffer, uint32_t size, uint32_t repeat) {
    if (!buffer || size == 0 || size > INT32_MAX) {
        return 1;
    }

    int channel = g_audio_channel;
    stop_audio_channel(channel, 1);

    uint8_t *copy = malloc(size);
    if (!copy) {
        return 1;
    }
    memcpy(copy, buffer, size);
    void *rw = g_sdl_audio.RWFromConstMem(copy, (int)size);
    if (!rw) {
        free(copy);
        return 1;
    }
    void *chunk = g_mixer.LoadWAV_RW(rw, 1);
    free(copy);
    if (!chunk) {
        fprintf(stderr, "[audio] stream load failed on channel %d: %s\n", channel, mixer_error());
        return 1;
    }

    struct audio_slot *slot = &g_audio_slots[channel];
    int mixer_channel = audio_mixer_channel(channel);
    slot->chunk = chunk;
    atomic_store_explicit(&slot->finished, 0, memory_order_release);
    slot->paused = 0;
    g_mixer.Volume(mixer_channel, slot->volume_mix);
    if (g_mixer.PlayChannelTimed(mixer_channel, chunk, audio_mixer_loops(repeat), -1) < 0) {
        fprintf(stderr, "[audio] stream play failed on channel %d: %s\n", channel, mixer_error());
        free_audio_slot(channel);
        return 1;
    }
    return 0;
}

void audio_shutdown(void) {
    for (int channel = 0; channel < AUDIO_CHANNELS; ++channel) {
        stop_audio_channel(channel, 0);
        memset(g_audio_slots[channel].callbacks, 0, sizeof(g_audio_slots[channel].callbacks));
    }
    for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
        if (g_audio_ready && g_mixer.HaltChannel) {
            g_mixer.HaltChannel(channel);
        }
        free_sound_slot(channel);
        memset(g_sound_slots[channel].callbacks, 0, sizeof(g_sound_slots[channel].callbacks));
    }
    if (g_audio_ready && g_mixer.CloseAudio) {
        g_mixer.CloseAudio();
    }
    if (g_mixer.Quit) {
        g_mixer.Quit();
    }
    if (g_sdl_audio.QuitSubSystem) {
        g_sdl_audio.QuitSubSystem(SDL_INIT_AUDIO);
    }
    if (g_sdl_mixer) {
        dlclose(g_sdl_mixer);
    }
    if (g_sdl2_audio) {
        dlclose(g_sdl2_audio);
    }
    memset(&g_mixer, 0, sizeof(g_mixer));
    memset(&g_sdl_audio, 0, sizeof(g_sdl_audio));
    g_sdl_mixer = NULL;
    g_sdl2_audio = NULL;
    g_audio_ready = 0;
    g_audio_tried = 0;
    g_mixer_mp3_ready = 0;
    g_audio_channel = 0;
    g_audio_slots_initialized = 0;
    g_sound_slots_initialized = 0;
    g_audio_pumping = 0;
}

int32_t s3eAudioIsPlaying(void) {
    return s3eAudioGetInt(1) == AUDIO_STATUS_PLAYING ? 1 : 0;
}

int32_t s3eAudioSetInt(uint32_t key, int32_t value) {
    init_audio_slots();
    if (key == 0) {
        struct audio_slot *slot = &g_audio_slots[g_audio_channel];
        slot->volume_s3e = clamp_volume_256(value);
        slot->volume_mix = mix_volume_from_s3e(slot->volume_s3e);
        if (audio_open()) {
            g_mixer.Volume(audio_mixer_channel(g_audio_channel), slot->volume_mix);
        }
        return 0;
    }
    if (key == 4 && audio_channel_valid(value)) {
        g_audio_channel = value;
        return 0;
    }
    return 1;
}

int32_t s3eAudioGetInt(uint32_t key) {
    init_audio_slots();
    struct audio_slot *slot = &g_audio_slots[g_audio_channel];
    switch (key) {
    case 0:
        return slot->volume_s3e;
    case 1:
        if (!audio_open() || !slot->chunk ||
            !g_mixer.Playing(audio_mixer_channel(g_audio_channel))) {
            return AUDIO_STATUS_STOPPED;
        }
        return slot->paused ? AUDIO_STATUS_PAUSED : AUDIO_STATUS_PLAYING;
    case 4:
        return g_audio_channel;
    case 5:
        return AUDIO_CHANNELS;
    case 6:
    case 9:
        return audio_open() ? 1 : 0;
    default:
        return -1;
    }
}

int32_t s3eAudioPlay(const char *filename, uint32_t repeat) {
    if (!audio_open()) {
        return 1;
    }

    char path[1200];
    if (!resolve_audio_path(filename, path, sizeof(path))) {
        fprintf(stderr, "[audio] audio file not found: %s\n", filename ? filename : "(null)");
        return 1;
    }
    uint32_t size = 0;
    uint8_t *data = read_audio_file(path, &size);
    if (!data) {
        fprintf(stderr, "[audio] stream read failed: %s\n", path);
        return 1;
    }
    int32_t result = play_audio_buffer(data, size, repeat);
    free(data);
    return result;
}

int32_t s3eAudioPlayFromBuffer(const void *buffer, uint32_t size, uint32_t repeat) {
    if (!audio_open()) {
        return 1;
    }
    return play_audio_buffer(buffer, size, repeat);
}

int32_t s3eAudioStop(void) {
    if (audio_open()) {
        stop_audio_channel(g_audio_channel, 1);
    }
    return 0;
}

int32_t s3eAudioPause(void) {
    if (audio_open()) {
        struct audio_slot *slot = &g_audio_slots[g_audio_channel];
        if (slot->chunk) {
            g_mixer.Pause(audio_mixer_channel(g_audio_channel));
            slot->paused = 1;
        }
    }
    return 0;
}

int32_t s3eAudioResume(void) {
    if (audio_open()) {
        struct audio_slot *slot = &g_audio_slots[g_audio_channel];
        if (slot->chunk) {
            g_mixer.Resume(audio_mixer_channel(g_audio_channel));
            slot->paused = 0;
        }
    }
    return 0;
}

int32_t s3eAudioRegister(uint32_t id, void *callback, void *user_data) {
    init_audio_slots();
    if (id >= S3E_AUDIO_CALLBACK_COUNT || !callback) {
        return 1;
    }
    g_audio_slots[g_audio_channel].callbacks[id].function = callback;
    g_audio_slots[g_audio_channel].callbacks[id].user_data = user_data;
    return 0;
}

int32_t s3eSoundGetFreeChannel(void) {
    init_sound_slots();
    if (!audio_open()) {
        return -1;
    }
    audio_pump();
    for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
        if (!g_sound_slots[channel].chunk) {
            return channel;
        }
    }
    return -1;
}

int32_t s3eSoundSetInt(uint32_t key, int32_t value) {
    init_sound_slots();
    if (key == 0) {
        g_sound_volume_s3e = clamp_volume_256(value);
        g_sound_volume_mix = mix_volume_from_s3e(g_sound_volume_s3e);
        if (audio_open()) {
            for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
                g_mixer.Volume(channel, mixed_sound_volume(&g_sound_slots[channel]));
            }
        }
        return 0;
    }
    if (key == 2) {
        g_sound_rate = clamp_rate(value);
        for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
            if (!g_sound_slots[channel].chunk) {
                g_sound_slots[channel].rate = g_sound_rate;
            }
        }
        return 0;
    }
    return 1;
}

int32_t s3eSoundGetInt(uint32_t key) {
    switch (key) {
    case 0:
        return g_sound_volume_s3e;
    case 1:
        return SOUND_FREQUENCY;
    case 2:
        return g_sound_rate;
    case 3:
        return SOUND_CHANNELS;
    case 5:
        return audio_open() ? 1 : 0;
    case 7:
        return SOUND_OUTPUT_CHANNELS == 2 ? 1 : 0;
    default:
        return -1;
    }
}

int32_t s3eSoundChannelRegister(int32_t channel, uint32_t callback_type, void *callback,
                                void *user_data) {
    init_sound_slots();
    if (!sound_channel_valid(channel) || callback_type >= S3E_CHANNEL_CALLBACK_COUNT || !callback) {
        return 1;
    }
    g_sound_slots[channel].callbacks[callback_type].function = callback;
    g_sound_slots[channel].callbacks[callback_type].user_data = user_data;
    return 0;
}

int32_t s3eSoundChannelUnRegister(int32_t channel, uint32_t callback_type) {
    init_sound_slots();
    if (!sound_channel_valid(channel) || callback_type >= S3E_CHANNEL_CALLBACK_COUNT) {
        return 1;
    }
    g_sound_slots[channel].callbacks[callback_type].function = NULL;
    g_sound_slots[channel].callbacks[callback_type].user_data = NULL;
    return 0;
}

int32_t s3eSoundChannelPlay(int32_t channel, const void *data, uint32_t size, uint32_t repeat,
                            uint32_t loop_index) {
    init_sound_slots();
    if (!audio_open() || !data || size == 0 || size > UINT32_MAX / 2u) {
        return 1;
    }
    audio_pump();

    if (channel < 0) {
        channel = s3eSoundGetFreeChannel();
    }
    if (!sound_channel_valid(channel)) {
        return 1;
    }

    struct sound_slot *slot = &g_sound_slots[channel];
    uint32_t pcm_samples = size;
    int16_t *decoded = NULL;
    if (render_generator_audio(channel, data, size, &decoded, &pcm_samples) <= 0) {
        fprintf(stderr, "[audio] sound generator failed on channel %d\n", channel);
        return 1;
    }

    uint32_t wav_size = 0;
    uint8_t *wav = create_pcm16_mono_wav(decoded, pcm_samples, SOUND_FREQUENCY, &wav_size);
    free(decoded);
    if (!wav) {
        return 1;
    }

    void *rw = g_sdl_audio.RWFromConstMem(wav, (int)wav_size);
    if (!rw) {
        free(wav);
        return 1;
    }

    void *chunk = g_mixer.LoadWAV_RW(rw, 1);
    free(wav);
    if (!chunk) {
        fprintf(stderr, "[audio] sound load failed: %s\n", mixer_error());
        return 1;
    }

    g_mixer.HaltChannel(channel);
    free_sound_slot(channel);
    slot->chunk = chunk;
    atomic_store_explicit(&slot->finished, 0, memory_order_release);
    slot->source_data = data;
    slot->source_samples = size;
    slot->reps_remaining = repeat;
    slot->loop_index = loop_index;
    g_mixer.Volume(channel, mixed_sound_volume(slot));

    int played_channel = g_mixer.PlayChannelTimed(channel, chunk, 0, -1);
    if (played_channel < 0) {
        fprintf(stderr, "[audio] sound play failed: %s\n", mixer_error());
        free_sound_slot(channel);
        return 1;
    }
    return 0;
}

int32_t s3eSoundChannelStop(int32_t channel) {
    if (!audio_open() || !sound_channel_valid(channel)) {
        return 1;
    }
    g_mixer.HaltChannel(channel);
    free_sound_slot(channel);
    return 0;
}

int32_t s3eSoundChannelPause(int32_t channel) {
    if (!audio_open() || !sound_channel_valid(channel)) {
        return 1;
    }
    g_mixer.Pause(channel);
    return 0;
}

int32_t s3eSoundChannelResume(int32_t channel) {
    if (!audio_open() || !sound_channel_valid(channel)) {
        return 1;
    }
    g_mixer.Resume(channel);
    return 0;
}

int32_t s3eSoundChannelSetInt(int32_t channel, uint32_t key, int32_t value) {
    init_sound_slots();
    if (!sound_channel_valid(channel)) {
        return 1;
    }
    struct sound_slot *slot = &g_sound_slots[channel];

    switch (key) {
    case 0:
        slot->rate_scale = value;
        slot->rate = rate_from_scale(value);
        return 0;
    case 1:
        slot->rate = clamp_rate(value);
        return 0;
    case 2:
        slot->user_value = value;
        return 0;
    case 3:
        slot->volume_s3e = clamp_volume_256(value);
        slot->volume_mix = mix_volume_from_s3e(slot->volume_s3e);
        if (audio_open()) {
            g_mixer.Volume(channel, mixed_sound_volume(slot));
        }
        return 0;
    default:
        return 1;
    }
}

int32_t s3eSoundChannelGetInt(int32_t channel, uint32_t key) {
    init_sound_slots();
    if (!sound_channel_valid(channel)) {
        return -1;
    }
    struct sound_slot *slot = &g_sound_slots[channel];

    switch (key) {
    case 0:
        return slot->rate_scale;
    case 1:
        return slot->rate;
    case 2:
        return slot->user_value;
    case 3:
        return slot->generator_decoding ? S3E_MAX_VOLUME : slot->volume_s3e;
    case 4:
        if (!audio_open()) {
            return 0;
        }
        return g_mixer.Playing(channel) != 0 ? 1 : 0;
    case 5:
        if (!audio_open()) {
            return 0;
        }
        return g_mixer.Playing(channel) != 0 ? 1 : 0;
    default:
        return -1;
    }
}
