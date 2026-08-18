#include "codboz_frame_interpolation.h"
#include "s3e_host_internal.h"

#include <assert.h>

enum {
    IMAGE_BASE = 0x4a000000,
    IMAGE_SIZE = 0x460000,
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

struct fixed_call_record {
    uint32_t channel;
    uint32_t step;
    uint32_t value;
    uint32_t argument4;
    uint32_t argument5;
};

struct active_set_record {
    uint32_t captured;
    uint32_t factor;
};

static uint32_t read32(const uint8_t *base, size_t offset) {
    uint32_t value;
    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static void write32(uint8_t *base, size_t offset, uint32_t value) {
    memcpy(base + offset, &value, sizeof(value));
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
    write32(base, DIRECTOR_FIXED_SLOT_OFFSET, IMAGE_BASE + DIRECTOR_FIXED_CALLBACK_OFFSET);
    write32(base, DIRECTOR_INTERPOLATE_SLOT_OFFSET,
            IMAGE_BASE + DIRECTOR_INTERPOLATE_CALLBACK_OFFSET);
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

    initialize_image(base);
    write32(base, DIRECTOR_FIXED_SLOT_OFFSET, 0);
    assert(!codboz_install_frame_interpolation(&loaded));

    initialize_image(base);
    write32(base, DIRECTOR_INTERPOLATE_SLOT_OFFSET, 0);
    assert(!codboz_install_frame_interpolation(&loaded));
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void install_test_stubs(uint8_t *base) {
    static const uint8_t fixed_stub[] = {
        0x01, 0x60, 0x42, 0x60, 0x83, 0x60, 0x00, 0x99, 0xc1,
        0x60, 0x01, 0x99, 0x01, 0x61, 0x77, 0x20, 0x70, 0x47,
    };
    static const uint8_t capture_stub[] = {0x01, 0x21, 0x01, 0x60, 0x70, 0x47};
    static const uint8_t apply_stub[] = {0x41, 0x60, 0x70, 0x47};

    memcpy(base + (DIRECTOR_FIXED_CALLBACK_OFFSET & ~1u), fixed_stub, sizeof(fixed_stub));
    memcpy(base + (CAPTURE_TRANSFORMS_OFFSET & ~1u), capture_stub, sizeof(capture_stub));
    memcpy(base + (APPLY_FACTOR_OFFSET & ~1u), apply_stub, sizeof(apply_stub));
    __builtin___clear_cache((char *)base + (DIRECTOR_FIXED_CALLBACK_OFFSET & ~1u),
                            (char *)base + (DIRECTOR_FIXED_CALLBACK_OFFSET & ~1u) +
                                sizeof(fixed_stub));
    __builtin___clear_cache((char *)base + (CAPTURE_TRANSFORMS_OFFSET & ~1u),
                            (char *)base + (CAPTURE_TRANSFORMS_OFFSET & ~1u) +
                                sizeof(capture_stub));
    __builtin___clear_cache((char *)base + (APPLY_FACTOR_OFFSET & ~1u),
                            (char *)base + (APPLY_FACTOR_OFFSET & ~1u) + sizeof(apply_stub));
}

static void assert_callback_behavior(uint8_t *base) {
    struct s3e_loaded_image loaded = {.base = base, .map_size = IMAGE_SIZE};
    initialize_image(base);
    assert(codboz_install_frame_interpolation(&loaded));

    director_callback_fn fixed =
        (director_callback_fn)(uintptr_t)read32(base, DIRECTOR_FIXED_SLOT_OFFSET);
    director_callback_fn interpolate =
        (director_callback_fn)(uintptr_t)read32(base, DIRECTOR_INTERPOLATE_SLOT_OFFSET);
    assert((uintptr_t)fixed != IMAGE_BASE + DIRECTOR_FIXED_CALLBACK_OFFSET);
    assert((uintptr_t)interpolate != IMAGE_BASE + DIRECTOR_INTERPOLATE_CALLBACK_OFFSET);

    install_test_stubs(base);
    struct fixed_call_record call = {0};
    struct active_set_record active_set = {0};
    write32(base, ACTIVE_SET_POINTER_OFFSET, (uint32_t)(uintptr_t)&active_set);

    uint32_t result = fixed(&call, 1, 0x22, 0x33, 0x44, 0x55);
    assert(result == 0x77);
    assert(active_set.captured == 1);
    assert(call.channel == 1);
    assert(call.step == 0x22);
    assert(call.value == 0x33);
    assert(call.argument4 == 0x44);
    assert(call.argument5 == 0x55);

    result = interpolate(&call, 1, 0, float_bits(0.25f), 0, 0);
    assert(result == (uint32_t)(uintptr_t)&call);
    assert(active_set.factor == 1024);

    active_set.factor = 0;
    interpolate(&call, 0, 0, float_bits(0.5f), 0, 0);
    assert(active_set.factor == 0);

    interpolate(&call, 1, 0, float_bits(2.0f), 0, 0);
    assert(active_set.factor == 4096);
}

int main(void) {
    void *mapping =
        mmap((void *)(uintptr_t)IMAGE_BASE, IMAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(mapping == (void *)(uintptr_t)IMAGE_BASE);

    uint8_t *base = mapping;
    initialize_image(base);
    assert_install_rejections(base);
    assert_callback_behavior(base);

    assert(munmap(mapping, IMAGE_SIZE) == 0);
    puts("COD BOZ frame interpolation tests passed");
    return 0;
}
