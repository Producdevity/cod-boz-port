#include "codboz_frame_interpolation.h"
#include "s3e_host_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    ACTIVE_SET_POINTER_OFFSET = 0x45f2b0,
    DIRECTOR_FIXED_SLOT_OFFSET = 0x3e6490,
    DIRECTOR_INTERPOLATE_SLOT_OFFSET = 0x3e6494,
    DIRECTOR_FIXED_CALLBACK_OFFSET = 0x0ba98d,
    DIRECTOR_INTERPOLATE_CALLBACK_OFFSET = 0x0ba7db,
    CAPTURE_TRANSFORMS_OFFSET = 0x0e8787,
    APPLY_FACTOR_OFFSET = 0x0e87c9,
    INTERPOLATION_CHANNEL = 1,
    FIXED_FACTOR_ONE = 4096,
};

typedef uint32_t(S3E_SOFTFP *director_callback_fn)(void *director, uint32_t channel, uint32_t step,
                                                   uint32_t value, uint32_t argument4,
                                                   uint32_t argument5);
typedef void(S3E_SOFTFP *capture_transforms_fn)(void *active_set);
typedef void(S3E_SOFTFP *apply_factor_fn)(void *active_set, int32_t factor);

static uint8_t *g_active_set_pointer;
static director_callback_fn g_original_fixed_callback;
static capture_transforms_fn g_capture_transforms;
static apply_factor_fn g_apply_factor;

static bool image_range_valid(const struct s3e_loaded_image *loaded, size_t offset, size_t size) {
    return loaded && loaded->base && offset <= loaded->map_size &&
           size <= loaded->map_size - offset;
}

static bool image_matches(const struct s3e_loaded_image *loaded, size_t offset,
                          const uint8_t *expected, size_t size) {
    return image_range_valid(loaded, offset, size) &&
           memcmp(loaded->base + offset, expected, size) == 0;
}

static uint32_t read32(const void *address) {
    uint32_t value;
    memcpy(&value, address, sizeof(value));
    return value;
}

static void write32(void *address, uint32_t value) {
    memcpy(address, &value, sizeof(value));
}

static void *active_transform_set(void) {
    return (void *)(uintptr_t)read32(g_active_set_pointer);
}

static int32_t interpolation_factor(uint32_t bits) {
    float factor;
    memcpy(&factor, &bits, sizeof(factor));
    if (!isfinite(factor) || factor <= 0.0f) {
        return 0;
    }
    if (factor >= 1.0f) {
        return FIXED_FACTOR_ONE;
    }
    return (int32_t)(factor * (float)FIXED_FACTOR_ONE);
}

static S3E_SOFTFP uint32_t director_fixed_callback(void *director, uint32_t channel, uint32_t step,
                                                   uint32_t value, uint32_t argument4,
                                                   uint32_t argument5) {
    if (channel == INTERPOLATION_CHANNEL) {
        void *active_set = active_transform_set();
        if (active_set) {
            g_capture_transforms(active_set);
        }
    }
    return g_original_fixed_callback(director, channel, step, value, argument4, argument5);
}

static S3E_SOFTFP uint32_t director_interpolate_callback(void *director, uint32_t channel,
                                                         uint32_t step, uint32_t factor_bits,
                                                         uint32_t argument4, uint32_t argument5) {
    (void)step;
    (void)argument4;
    (void)argument5;
    if (channel == INTERPOLATION_CHANNEL) {
        void *active_set = active_transform_set();
        if (active_set) {
            g_apply_factor(active_set, interpolation_factor(factor_bits));
        }
    }
    return (uint32_t)(uintptr_t)director;
}

bool codboz_install_frame_interpolation(struct s3e_loaded_image *loaded) {
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

    if (!image_range_valid(loaded, ACTIVE_SET_POINTER_OFFSET, sizeof(uint32_t)) ||
        !image_range_valid(loaded, DIRECTOR_FIXED_SLOT_OFFSET, 2 * sizeof(uint32_t)) ||
        !image_matches(loaded, DIRECTOR_FIXED_CALLBACK_OFFSET & ~1u, fixed_signature,
                       sizeof(fixed_signature)) ||
        !image_matches(loaded, DIRECTOR_INTERPOLATE_CALLBACK_OFFSET & ~1u, interpolate_signature,
                       sizeof(interpolate_signature)) ||
        !image_matches(loaded, CAPTURE_TRANSFORMS_OFFSET & ~1u, capture_signature,
                       sizeof(capture_signature)) ||
        !image_matches(loaded, APPLY_FACTOR_OFFSET & ~1u, apply_signature,
                       sizeof(apply_signature))) {
        return false;
    }

    uintptr_t image_base = (uintptr_t)loaded->base;
    uintptr_t fixed_hook = (uintptr_t)director_fixed_callback;
    uintptr_t interpolate_hook = (uintptr_t)director_interpolate_callback;
    if (image_base > UINT32_MAX || fixed_hook > UINT32_MAX || interpolate_hook > UINT32_MAX ||
        read32(loaded->base + DIRECTOR_FIXED_SLOT_OFFSET) !=
            (uint32_t)image_base + DIRECTOR_FIXED_CALLBACK_OFFSET ||
        read32(loaded->base + DIRECTOR_INTERPOLATE_SLOT_OFFSET) !=
            (uint32_t)image_base + DIRECTOR_INTERPOLATE_CALLBACK_OFFSET) {
        return false;
    }

    g_active_set_pointer = loaded->base + ACTIVE_SET_POINTER_OFFSET;
    g_original_fixed_callback = (director_callback_fn)(image_base + DIRECTOR_FIXED_CALLBACK_OFFSET);
    g_capture_transforms = (capture_transforms_fn)(image_base + CAPTURE_TRANSFORMS_OFFSET);
    g_apply_factor = (apply_factor_fn)(image_base + APPLY_FACTOR_OFFSET);

    write32(loaded->base + DIRECTOR_FIXED_SLOT_OFFSET, (uint32_t)fixed_hook);
    write32(loaded->base + DIRECTOR_INTERPOLATE_SLOT_OFFSET, (uint32_t)interpolate_hook);
    return true;
}
