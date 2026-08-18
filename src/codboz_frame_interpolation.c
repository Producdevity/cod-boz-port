#include "codboz_frame_interpolation.h"
#include "s3e_host_internal.h"

enum {
    ACTIVE_SET_POINTER_OFFSET = 0x45f2b0,
    DIRECTOR_FIXED_SLOT_OFFSET = 0x3e6490,
    DIRECTOR_INTERPOLATE_SLOT_OFFSET = 0x3e6494,
    DIRECTOR_FIXED_CALLBACK_OFFSET = 0x0ba98d,
    DIRECTOR_INTERPOLATE_CALLBACK_OFFSET = 0x0ba7db,
    CAPTURE_TRANSFORMS_OFFSET = 0x0e8787,
    APPLY_FACTOR_OFFSET = 0x0e87c9,
    INTERPOLATION_CHANNEL = 1,
    INTERPOLATION_SCALE = 4096,
};

typedef uint32_t(S3E_SOFTFP *director_callback_fn)(void *director, uint32_t channel, uint32_t step,
                                                   uint32_t value, uint32_t argument4,
                                                   uint32_t argument5);
typedef void(S3E_SOFTFP *capture_transforms_fn)(void *active_set);
typedef void(S3E_SOFTFP *apply_factor_fn)(void *active_set, uint32_t factor);

static uint8_t *g_image_base;
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

static uint32_t image_read32(const struct s3e_loaded_image *loaded, size_t offset) {
    uint32_t value;
    memcpy(&value, loaded->base + offset, sizeof(value));
    return value;
}

static void image_write32(struct s3e_loaded_image *loaded, size_t offset, uint32_t value) {
    memcpy(loaded->base + offset, &value, sizeof(value));
}

static void *active_transform_set(void) {
    uint32_t address;
    memcpy(&address, g_image_base + ACTIVE_SET_POINTER_OFFSET, sizeof(address));
    return (void *)(uintptr_t)address;
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
    if (channel != INTERPOLATION_CHANNEL) {
        return (uint32_t)(uintptr_t)director;
    }

    void *active_set = active_transform_set();
    if (!active_set) {
        return (uint32_t)(uintptr_t)director;
    }

    float factor;
    memcpy(&factor, &factor_bits, sizeof(factor));
    uint32_t fixed_factor;
    if (!(factor > 0.0f)) {
        fixed_factor = 0;
    } else if (factor >= 1.0f) {
        fixed_factor = INTERPOLATION_SCALE;
    } else {
        fixed_factor = (uint32_t)(factor * INTERPOLATION_SCALE + 0.5f);
    }
    g_apply_factor(active_set, fixed_factor);
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
        image_read32(loaded, DIRECTOR_FIXED_SLOT_OFFSET) !=
            (uint32_t)image_base + DIRECTOR_FIXED_CALLBACK_OFFSET ||
        image_read32(loaded, DIRECTOR_INTERPOLATE_SLOT_OFFSET) !=
            (uint32_t)image_base + DIRECTOR_INTERPOLATE_CALLBACK_OFFSET) {
        return false;
    }

    g_image_base = loaded->base;
    g_original_fixed_callback = (director_callback_fn)(image_base + DIRECTOR_FIXED_CALLBACK_OFFSET);
    g_capture_transforms = (capture_transforms_fn)(image_base + CAPTURE_TRANSFORMS_OFFSET);
    g_apply_factor = (apply_factor_fn)(image_base + APPLY_FACTOR_OFFSET);

    image_write32(loaded, DIRECTOR_FIXED_SLOT_OFFSET, (uint32_t)fixed_hook);
    image_write32(loaded, DIRECTOR_INTERPOLATE_SLOT_OFFSET, (uint32_t)interpolate_hook);
    return true;
}
