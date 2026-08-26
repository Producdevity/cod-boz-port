#include "codboz_frame_interpolation.h"
#include "s3e_host_internal.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#if defined(NDEBUG)
#error "codboz_frame_interpolation_test requires assertions"
#endif

enum {
    IMAGE_BASE = 0x60000000,
    IMAGE_SIZE = 0x4b0000,
    ACTIVE_SET_POINTER_OFFSET = 0x45f2b0,
    DIRECTOR_FIXED_SLOT_OFFSET = 0x3e6490,
    DIRECTOR_INTERPOLATE_SLOT_OFFSET = 0x3e6494,
    DIRECTOR_FIXED_CALLBACK_OFFSET = 0x0ba98d,
    DIRECTOR_INTERPOLATE_CALLBACK_OFFSET = 0x0ba7db,
    CAPTURE_TRANSFORMS_OFFSET = 0x0e8787,
    APPLY_FACTOR_OFFSET = 0x0e87c9,
};

typedef uint32_t(S3E_SOFTFP *director_callback_fn)(void *director, uint32_t channel, uint32_t step,
                                                   uint32_t value, uint32_t argument4,
                                                   uint32_t argument5);

struct call_record {
    void *active_set;
    uint32_t channel;
    uint32_t step;
    uint32_t value;
    uint32_t argument4;
    uint32_t argument5;
    int32_t factor;
    uint32_t fixed_calls;
    uint32_t capture_calls;
    uint32_t apply_calls;
};

static struct call_record g_calls;

static uint32_t read32(const void *address) {
    uint32_t value;
    memcpy(&value, address, sizeof(value));
    return value;
}

static void write32(void *address, uint32_t value) {
    memcpy(address, &value, sizeof(value));
}

static uint32_t pointer32(const void *pointer) {
    uintptr_t value = (uintptr_t)pointer;
    assert(value <= UINT32_MAX);
    return (uint32_t)value;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static S3E_SOFTFP uint32_t fixed_stub(void *director, uint32_t channel, uint32_t step,
                                      uint32_t value, uint32_t argument4, uint32_t argument5) {
    (void)director;
    g_calls.channel = channel;
    g_calls.step = step;
    g_calls.value = value;
    g_calls.argument4 = argument4;
    g_calls.argument5 = argument5;
    ++g_calls.fixed_calls;
    return 0x77;
}

static S3E_SOFTFP void capture_stub(void *active_set) {
    g_calls.active_set = active_set;
    ++g_calls.capture_calls;
}

static S3E_SOFTFP void apply_stub(void *active_set, int32_t factor) {
    g_calls.active_set = active_set;
    g_calls.factor = factor;
    ++g_calls.apply_calls;
}

static void write_thumb_trampoline(uint8_t *base, size_t offset, uintptr_t target) {
    static const uint8_t aligned_code[] = {
        0xdf, 0xf8, 0x04, 0xc0, 0x60, 0x47, 0x00, 0xbf,
    };
    static const uint8_t halfword_aligned_code[] = {0xdf, 0xf8, 0x04, 0xc0, 0x60, 0x47};
    offset &= ~1u;
    size_t literal_offset;
    if ((offset & 3u) == 0) {
        memcpy(base + offset, aligned_code, sizeof(aligned_code));
        literal_offset = sizeof(aligned_code);
    } else {
        memcpy(base + offset, halfword_aligned_code, sizeof(halfword_aligned_code));
        literal_offset = sizeof(halfword_aligned_code);
    }
    write32(base + offset + literal_offset, pointer32((void *)target));
    __builtin___clear_cache((char *)base + offset,
                            (char *)base + offset + literal_offset + sizeof(uint32_t));
}

static void initialize_image(uint8_t *base) {
    static const uint8_t fixed_signature[] = {
        0x02, 0x29, 0x2d, 0xe9, 0xf0, 0x41, 0x80, 0x46,
        0x0f, 0x46, 0x16, 0x46, 0xdd, 0xe9, 0x06, 0x45,
    };
    static const uint8_t interpolate_signature[] = {0x70, 0x47};
    static const uint8_t capture_signature[] = {
        0x38, 0xb5, 0x05, 0x46, 0x00, 0x24, 0xab, 0x68,
        0x9c, 0x42, 0x06, 0xd2, 0x6b, 0x68, 0x53, 0xf8,
    };
    static const uint8_t apply_signature[] = {
        0x70, 0xb5, 0x05, 0x46, 0x0e, 0x46, 0x00, 0x24,
        0xab, 0x68, 0x9c, 0x42, 0x07, 0xd2, 0x6b, 0x68,
    };

    memset(base, 0, IMAGE_SIZE);
    memcpy(base + (DIRECTOR_FIXED_CALLBACK_OFFSET & ~1u), fixed_signature, sizeof(fixed_signature));
    memcpy(base + (DIRECTOR_INTERPOLATE_CALLBACK_OFFSET & ~1u), interpolate_signature,
           sizeof(interpolate_signature));
    memcpy(base + (CAPTURE_TRANSFORMS_OFFSET & ~1u), capture_signature, sizeof(capture_signature));
    memcpy(base + (APPLY_FACTOR_OFFSET & ~1u), apply_signature, sizeof(apply_signature));
    write32(base + DIRECTOR_FIXED_SLOT_OFFSET, pointer32(base) + DIRECTOR_FIXED_CALLBACK_OFFSET);
    write32(base + DIRECTOR_INTERPOLATE_SLOT_OFFSET,
            pointer32(base) + DIRECTOR_INTERPOLATE_CALLBACK_OFFSET);
}

static void install_test_stubs(uint8_t *base) {
    write_thumb_trampoline(base, DIRECTOR_FIXED_CALLBACK_OFFSET, (uintptr_t)fixed_stub);
    write_thumb_trampoline(base, CAPTURE_TRANSFORMS_OFFSET, (uintptr_t)capture_stub);
    write_thumb_trampoline(base, APPLY_FACTOR_OFFSET, (uintptr_t)apply_stub);
}

static void assert_install_rejections(uint8_t *base) {
    struct s3e_loaded_image loaded = {.base = base, .map_size = IMAGE_SIZE};
    assert(!codboz_install_frame_interpolation(NULL));

    loaded.map_size = ACTIVE_SET_POINTER_OFFSET + sizeof(uint32_t) - 1;
    assert(!codboz_install_frame_interpolation(&loaded));
    loaded.map_size = IMAGE_SIZE;

    const size_t signature_offsets[] = {
        DIRECTOR_FIXED_CALLBACK_OFFSET & ~1u,
        DIRECTOR_INTERPOLATE_CALLBACK_OFFSET & ~1u,
        CAPTURE_TRANSFORMS_OFFSET & ~1u,
        APPLY_FACTOR_OFFSET & ~1u,
    };
    for (size_t i = 0; i < sizeof(signature_offsets) / sizeof(signature_offsets[0]); ++i) {
        initialize_image(base);
        base[signature_offsets[i]] ^= 0xffu;
        assert(!codboz_install_frame_interpolation(&loaded));
    }

    const size_t slot_offsets[] = {
        DIRECTOR_FIXED_SLOT_OFFSET,
        DIRECTOR_INTERPOLATE_SLOT_OFFSET,
    };
    for (size_t i = 0; i < sizeof(slot_offsets) / sizeof(slot_offsets[0]); ++i) {
        initialize_image(base);
        write32(base + slot_offsets[i], 0);
        assert(!codboz_install_frame_interpolation(&loaded));
    }
}

static void assert_callback_behavior(uint8_t *base) {
    struct s3e_loaded_image loaded = {.base = base, .map_size = IMAGE_SIZE};
    initialize_image(base);
    assert(codboz_install_frame_interpolation(&loaded));

    director_callback_fn fixed =
        (director_callback_fn)(uintptr_t)read32(base + DIRECTOR_FIXED_SLOT_OFFSET);
    director_callback_fn interpolate =
        (director_callback_fn)(uintptr_t)read32(base + DIRECTOR_INTERPOLATE_SLOT_OFFSET);
    install_test_stubs(base);

    memset(&g_calls, 0, sizeof(g_calls));
    write32(base + ACTIVE_SET_POINTER_OFFSET, pointer32(&g_calls));
    assert(fixed(&g_calls, 1, 3, 4, 5, 6) == 0x77);
    assert(g_calls.capture_calls == 1);
    assert(g_calls.fixed_calls == 1);
    assert(g_calls.active_set == &g_calls);
    assert(g_calls.channel == 1 && g_calls.step == 3 && g_calls.value == 4);
    assert(g_calls.argument4 == 5 && g_calls.argument5 == 6);

    assert(fixed(&g_calls, 0, 7, 8, 9, 10) == 0x77);
    assert(g_calls.capture_calls == 1);
    assert(g_calls.fixed_calls == 2);

    assert(interpolate(&g_calls, 1, 0, float_bits(0.5f), 0, 0) == pointer32(&g_calls));
    assert(g_calls.apply_calls == 1 && g_calls.factor == 2048);
    interpolate(&g_calls, 1, 0, float_bits(2.0f), 0, 0);
    assert(g_calls.apply_calls == 2 && g_calls.factor == 4096);
    interpolate(&g_calls, 1, 0, float_bits(-1.0f), 0, 0);
    assert(g_calls.apply_calls == 3 && g_calls.factor == 0);
    interpolate(&g_calls, 1, 0, float_bits(NAN), 0, 0);
    assert(g_calls.apply_calls == 4 && g_calls.factor == 0);
    interpolate(&g_calls, 0, 0, float_bits(0.25f), 0, 0);
    assert(g_calls.apply_calls == 4);

    write32(base + ACTIVE_SET_POINTER_OFFSET, 0);
    fixed(&g_calls, 1, 0, 0, 0, 0);
    interpolate(&g_calls, 1, 0, float_bits(0.25f), 0, 0);
    assert(g_calls.capture_calls == 1);
    assert(g_calls.apply_calls == 4);
    assert(g_calls.fixed_calls == 3);

    assert(read32(base + 0x27de3c) == 0);
}

int main(void) {
    uint8_t *base = mmap((void *)IMAGE_BASE, IMAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(base != MAP_FAILED);
    assert(pointer32(base) == IMAGE_BASE);

    assert_install_rejections(base);
    assert_callback_behavior(base);
    assert(munmap(base, IMAGE_SIZE) == 0);
    puts("codboz frame interpolation tests passed");
    return 0;
}
