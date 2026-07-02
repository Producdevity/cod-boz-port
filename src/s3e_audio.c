#include "s3e_host_internal.h"

enum {
    SDL_INIT_AUDIO = 0x00000010u,
    AUDIO_S16LSB = 0x8010u,
    MIX_INIT_MP3 = 0x00000008u,
    MIX_MAX_VOLUME = 128,
    S3E_MAX_VOLUME = 256,
    SOUND_CHANNELS = 32,
    AUDIO_STATUS_STOPPED = 0,
    AUDIO_STATUS_PLAYING = 1,
    AUDIO_STATUS_PAUSED = 2,
    IMA_ADPCM_BLOCK_BYTES = 512,
    SOUND_FREQUENCY = 22050,
    SOUND_RATE_DEFAULT = 0x10000,
    SOUND_RATE_MAX = 0x40000,
    SOUND_OUTPUT_CHANNELS = 2,
    SOUND_BUFFER_SAMPLES = 1024,
};

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
    void *(*QuickLoad_RAW)(uint8_t *mem, uint32_t len);
    void (*FreeChunk)(void *chunk);
    void *(*LoadMUS)(const char *file);
    void *(*LoadMUS_RW)(void *rw, int freesrc);
    int (*PlayMusic)(void *music, int loops);
    int (*PlayingMusic)(void);
    int (*HaltMusic)(void);
    void (*PauseMusic)(void);
    void (*ResumeMusic)(void);
    int (*VolumeMusic)(int volume);
    void (*FreeMusic)(void *music);
    const char *(*GetError)(void);
};

struct sound_slot {
    uint8_t *data;
    void *chunk;
    int finished;
    int volume_s3e;
    int volume_mix;
    int rate;
    int rate_scale;
    int position;
};

static void *g_sdl2_audio;
static void *g_sdl_mixer;
static struct sdl_audio_api g_sdl_audio;
static struct sdl_mixer_api g_mixer;
static struct sound_slot g_sound_slots[SOUND_CHANNELS];
static void *g_music;
static uint8_t *g_music_buffer;
static int g_audio_tried;
static int g_audio_ready;
static int g_audio_paused;
static int g_audio_volume_s3e = S3E_MAX_VOLUME;
static int g_audio_volume_mix = MIX_MAX_VOLUME;
static int g_sound_volume_s3e = S3E_MAX_VOLUME;
static int g_sound_volume_mix = MIX_MAX_VOLUME;
static int g_sound_rate = SOUND_RATE_DEFAULT;
static int g_mixer_mp3_ready;
static int g_sound_slots_initialized;

static const int8_t IMA_INDEX_TABLE[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

static const int16_t IMA_STEP_TABLE[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

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

static int mixed_channel_volume(const struct sound_slot *slot) {
    return slot->volume_mix * g_sound_volume_mix / MIX_MAX_VOLUME;
}

static int channel_valid(int channel) {
    return channel >= 0 && channel < SOUND_CHANNELS;
}

static int16_t clamp_s16(int value) {
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)value;
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
        g_sound_slots[channel].rate_scale = SOUND_RATE_DEFAULT;
    }
    g_sound_slots_initialized = 1;
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

static int looks_like_ima_adpcm(const uint8_t *data, size_t byte_count) {
    if (!data || byte_count < IMA_ADPCM_BLOCK_BYTES) {
        return 0;
    }

    for (size_t offset = 0; offset < byte_count;) {
        size_t block_size = byte_count - offset;
        if (block_size > IMA_ADPCM_BLOCK_BYTES) {
            block_size = IMA_ADPCM_BLOCK_BYTES;
        }
        if (block_size < 4) {
            return 0;
        }

        const uint8_t *header = data + offset;
        if (header[2] > 88 || header[3] != 0) {
            return 0;
        }
        offset += block_size;
    }
    return 1;
}

static void write_stereo_sample(int16_t **dst, int16_t sample) {
    *(*dst)++ = sample;
    *(*dst)++ = sample;
}

static int decode_ima_nibble(int nibble, int *predictor, int *step_index) {
    int step = IMA_STEP_TABLE[*step_index];
    int diff = step >> 3;
    if (nibble & 1) {
        diff += step >> 2;
    }
    if (nibble & 2) {
        diff += step >> 1;
    }
    if (nibble & 4) {
        diff += step;
    }

    if (nibble & 8) {
        *predictor -= diff;
    } else {
        *predictor += diff;
    }
    *predictor = clamp_s16(*predictor);

    *step_index += IMA_INDEX_TABLE[nibble & 0x0f];
    if (*step_index < 0) {
        *step_index = 0;
    } else if (*step_index > 88) {
        *step_index = 88;
    }
    return *predictor;
}

static uint8_t *decode_ima_adpcm_stereo(const uint8_t *data, size_t byte_count,
                                        uint32_t *out_byte_count) {
    if (!looks_like_ima_adpcm(data, byte_count)) {
        return NULL;
    }

    size_t output_samples = 0;
    for (size_t offset = 0; offset < byte_count;) {
        size_t block_size = byte_count - offset;
        if (block_size > IMA_ADPCM_BLOCK_BYTES) {
            block_size = IMA_ADPCM_BLOCK_BYTES;
        }
        size_t block_output = (1u + (block_size - 4u) * 2u) * SOUND_OUTPUT_CHANNELS;
        if (output_samples > SIZE_MAX - block_output) {
            return NULL;
        }
        output_samples += block_output;
        offset += block_size;
    }
    if (output_samples > UINT32_MAX / sizeof(int16_t)) {
        return NULL;
    }

    uint8_t *buffer = malloc(output_samples * sizeof(int16_t));
    if (!buffer) {
        return NULL;
    }

    int16_t *dst = (int16_t *)buffer;
    for (size_t offset = 0; offset < byte_count;) {
        size_t block_size = byte_count - offset;
        if (block_size > IMA_ADPCM_BLOCK_BYTES) {
            block_size = IMA_ADPCM_BLOCK_BYTES;
        }

        const uint8_t *src = data + offset;
        int predictor = (int16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
        int step_index = src[2];
        write_stereo_sample(&dst, (int16_t)predictor);

        for (size_t i = 4; i < block_size; ++i) {
            uint8_t byte = src[i];
            write_stereo_sample(&dst, decode_ima_nibble(byte & 0x0f, &predictor, &step_index));
            write_stereo_sample(&dst, decode_ima_nibble(byte >> 4, &predictor, &step_index));
        }
        offset += block_size;
    }

    *out_byte_count = (uint32_t)(output_samples * sizeof(int16_t));
    return buffer;
}

static uint8_t *copy_pcm16_mono_to_stereo(const int16_t *data, uint32_t samples,
                                          uint32_t *out_byte_count) {
    if (!data || samples == 0 || samples > UINT32_MAX / 4u) {
        return NULL;
    }

    size_t byte_count = (size_t)samples * SOUND_OUTPUT_CHANNELS * sizeof(int16_t);
    uint8_t *buffer = malloc(byte_count);
    if (!buffer) {
        return NULL;
    }

    int16_t *dst = (int16_t *)buffer;
    for (uint32_t i = 0; i < samples; ++i) {
        write_stereo_sample(&dst, data[i]);
    }

    *out_byte_count = (uint32_t)byte_count;
    return buffer;
}

static void free_sound_slot(int channel) {
    if (!channel_valid(channel)) {
        return;
    }
    struct sound_slot *slot = &g_sound_slots[channel];
    if (slot->chunk && g_mixer.FreeChunk) {
        g_mixer.FreeChunk(slot->chunk);
    }
    free(slot->data);
    slot->data = NULL;
    slot->chunk = NULL;
    slot->finished = 0;
}

static void sound_channel_finished(int channel) {
    if (channel_valid(channel)) {
        g_sound_slots[channel].finished = 1;
    }
}

static void service_finished_channels(void) {
    if (!g_audio_ready) {
        return;
    }
    for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
        struct sound_slot *slot = &g_sound_slots[channel];
        if (!slot->chunk) {
            continue;
        }
        if (!slot->finished && g_mixer.Playing && g_mixer.Playing(channel) != 0) {
            continue;
        }
        free_sound_slot(channel);
    }
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
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.QuickLoad_RAW, "Mix_QuickLoad_RAW");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.FreeChunk, "Mix_FreeChunk");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.LoadMUS, "Mix_LoadMUS");
    load_optional_symbol(g_sdl_mixer, (void **)&g_mixer.LoadMUS_RW, "Mix_LoadMUS_RW");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.PlayMusic, "Mix_PlayMusic");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.PlayingMusic, "Mix_PlayingMusic");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.HaltMusic, "Mix_HaltMusic");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.PauseMusic, "Mix_PauseMusic");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.ResumeMusic, "Mix_ResumeMusic");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.VolumeMusic, "Mix_VolumeMusic");
    ok &= load_symbol(g_sdl_mixer, (void **)&g_mixer.FreeMusic, "Mix_FreeMusic");
    load_optional_symbol(g_sdl_mixer, (void **)&g_mixer.GetError, "Mix_GetError");

    if (!ok) {
        fprintf(stderr, "[audio] SDL2_mixer symbols unavailable\n");
        return 0;
    }
    if (g_sdl_audio.InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[audio] SDL audio init failed: %s\n", mixer_error());
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

    g_mixer.AllocateChannels(SOUND_CHANNELS);
    g_mixer.ChannelFinished(sound_channel_finished);
    g_mixer.VolumeMusic(g_audio_volume_mix);
    init_sound_slots();
    for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
        g_mixer.Volume(channel, mixed_channel_volume(&g_sound_slots[channel]));
    }
    if (!g_mixer_mp3_ready) {
        fprintf(stderr,
                "[audio] MP3 decoder not reported by SDL2_mixer; music may be unavailable\n");
    }
    g_audio_ready = 1;
    return 1;
}

static void free_music(void) {
    if (g_music && g_mixer.FreeMusic) {
        g_mixer.FreeMusic(g_music);
    }
    g_music = NULL;
    free(g_music_buffer);
    g_music_buffer = NULL;
    g_audio_paused = 0;
}

static void stop_music(void) {
    if (g_audio_ready && g_mixer.HaltMusic) {
        g_mixer.HaltMusic();
    }
    free_music();
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

void audio_shutdown(void) {
    stop_music();
    for (int channel = 0; channel < SOUND_CHANNELS; ++channel) {
        if (g_audio_ready && g_mixer.HaltChannel) {
            g_mixer.HaltChannel(channel);
        }
        free_sound_slot(channel);
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
    g_audio_paused = 0;
    g_sound_slots_initialized = 0;
}

int32_t s3eAudioIsPlaying(void) {
    return s3eAudioGetInt(1) == AUDIO_STATUS_PLAYING ? 1 : 0;
}

int32_t s3eAudioSetInt(uint32_t key, int32_t value) {
    if (key == 0) {
        g_audio_volume_s3e = clamp_volume_256(value);
        g_audio_volume_mix = mix_volume_from_s3e(g_audio_volume_s3e);
        if (audio_open()) {
            g_mixer.VolumeMusic(g_audio_volume_mix);
        }
        return 0;
    }
    return 0;
}

int32_t s3eAudioGetInt(uint32_t key) {
    switch (key) {
    case 0:
        return g_audio_volume_s3e;
    case 1:
        if (!audio_open() || !g_music || !g_mixer.PlayingMusic()) {
            return AUDIO_STATUS_STOPPED;
        }
        return g_audio_paused ? AUDIO_STATUS_PAUSED : AUDIO_STATUS_PLAYING;
    case 4:
    case 5:
        return 0;
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
        fprintf(stderr, "[audio] music file not found: %s\n", filename ? filename : "(null)");
        return 1;
    }

    stop_music();
    g_music = g_mixer.LoadMUS(path);
    if (!g_music) {
        fprintf(stderr, "[audio] music load failed: %s: %s\n", path, mixer_error());
        return 1;
    }

    g_audio_paused = 0;
    g_mixer.VolumeMusic(g_audio_volume_mix);
    if (g_mixer.PlayMusic(g_music, repeat ? -1 : 0) != 0) {
        fprintf(stderr, "[audio] music play failed: %s\n", mixer_error());
        stop_music();
        return 1;
    }
    return 0;
}

int32_t s3eAudioPlayFromBuffer(const void *buffer, uint32_t size, uint32_t repeat) {
    if (!audio_open() || !buffer || size == 0 || size > INT32_MAX || !g_mixer.LoadMUS_RW) {
        return 1;
    }

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

    stop_music();
    g_music_buffer = copy;
    g_music = g_mixer.LoadMUS_RW(rw, 1);
    if (!g_music) {
        fprintf(stderr, "[audio] buffered music load failed: %s\n", mixer_error());
        stop_music();
        return 1;
    }
    g_audio_paused = 0;
    g_mixer.VolumeMusic(g_audio_volume_mix);
    if (g_mixer.PlayMusic(g_music, repeat ? -1 : 0) != 0) {
        fprintf(stderr, "[audio] buffered music play failed: %s\n", mixer_error());
        stop_music();
        return 1;
    }
    return 0;
}

int32_t s3eAudioStop(void) {
    if (audio_open()) {
        stop_music();
    }
    return 0;
}

int32_t s3eAudioPause(void) {
    if (audio_open()) {
        g_mixer.PauseMusic();
        g_audio_paused = 1;
    }
    return 0;
}

int32_t s3eAudioResume(void) {
    if (audio_open()) {
        g_mixer.ResumeMusic();
        g_audio_paused = 0;
    }
    return 0;
}

int32_t s3eAudioRegister(uint32_t id, void *callback, void *user_data) {
    (void)id;
    (void)callback;
    (void)user_data;
    return 0;
}

int32_t s3eSoundGetFreeChannel(void) {
    init_sound_slots();
    if (!audio_open()) {
        return -1;
    }
    service_finished_channels();
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
                g_mixer.Volume(channel, mixed_channel_volume(&g_sound_slots[channel]));
            }
        }
        return 0;
    }
    if (key == 2) {
        g_sound_rate = clamp_rate(value);
        return 0;
    }
    return 1;
}

int32_t s3eSoundGetInt(uint32_t key) {
    switch (key) {
    case 0:
        return g_sound_volume_s3e;
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

int32_t s3eSoundChannelRegister(uint32_t id, void *callback, void *user_data) {
    (void)id;
    (void)callback;
    (void)user_data;
    return 0;
}

int32_t s3eSoundChannelUnRegister(uint32_t id, void *callback) {
    (void)id;
    (void)callback;
    return 0;
}

int32_t s3eSoundChannelPlay(int32_t channel, const void *data, uint32_t size, uint32_t repeat) {
    init_sound_slots();
    if (!audio_open() || !data || size == 0 || size > UINT32_MAX / 2u) {
        return 1;
    }
    service_finished_channels();

    if (channel < 0) {
        channel = s3eSoundGetFreeChannel();
    }
    if (!channel_valid(channel)) {
        return 1;
    }

    uint32_t byte_count = 0;
    uint8_t *copy = decode_ima_adpcm_stereo(data, (size_t)size * sizeof(int16_t), &byte_count);
    if (!copy) {
        copy = copy_pcm16_mono_to_stereo(data, size, &byte_count);
    }
    if (!copy) {
        return 1;
    }

    void *chunk = g_mixer.QuickLoad_RAW(copy, byte_count);
    if (!chunk) {
        free(copy);
        return 1;
    }

    g_mixer.HaltChannel(channel);
    free_sound_slot(channel);
    g_sound_slots[channel].data = copy;
    g_sound_slots[channel].chunk = chunk;
    g_sound_slots[channel].finished = 0;
    g_mixer.Volume(channel, mixed_channel_volume(&g_sound_slots[channel]));

    int played_channel = g_mixer.PlayChannelTimed(channel, chunk, repeat ? -1 : 0, -1);
    if (played_channel < 0) {
        free_sound_slot(channel);
        return 1;
    }
    return 0;
}

int32_t s3eSoundChannelStop(int32_t channel) {
    if (!audio_open() || !channel_valid(channel)) {
        return 1;
    }
    g_mixer.HaltChannel(channel);
    free_sound_slot(channel);
    return 0;
}

int32_t s3eSoundChannelPause(int32_t channel) {
    if (!audio_open() || !channel_valid(channel)) {
        return 1;
    }
    g_mixer.Pause(channel);
    return 0;
}

int32_t s3eSoundChannelResume(int32_t channel) {
    if (!audio_open() || !channel_valid(channel)) {
        return 1;
    }
    g_mixer.Resume(channel);
    return 0;
}

int32_t s3eSoundChannelSetInt(int32_t channel, uint32_t key, int32_t value) {
    init_sound_slots();
    if (!channel_valid(channel)) {
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
        slot->rate_scale = slot->rate;
        return 0;
    case 2:
        slot->position = value;
        return 0;
    case 3:
        slot->volume_s3e = clamp_volume_256(value);
        slot->volume_mix = mix_volume_from_s3e(slot->volume_s3e);
        if (audio_open()) {
            g_mixer.Volume(channel, mixed_channel_volume(slot));
        }
        return 0;
    default:
        return 1;
    }
}

int32_t s3eSoundChannelGetInt(int32_t channel, uint32_t key) {
    init_sound_slots();
    if (!channel_valid(channel)) {
        return -1;
    }
    struct sound_slot *slot = &g_sound_slots[channel];

    switch (key) {
    case 0:
        return slot->rate_scale;
    case 1:
        return slot->rate;
    case 2:
        return slot->position;
    case 3:
        return slot->volume_s3e;
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
