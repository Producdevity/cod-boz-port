#include "codboz_frame_interpolation.h"
#include "s3e_host_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    DIRECTOR_FIXED_SLOT_OFFSET = 0x3e6490,
    DIRECTOR_INTERPOLATE_SLOT_OFFSET = 0x3e6494,
    DIRECTOR_FIXED_CALLBACK_OFFSET = 0x0ba98d,
    DIRECTOR_INTERPOLATE_CALLBACK_OFFSET = 0x0ba7db,
    VIEW_SETTER_OFFSET = 0x27de3c,
    VIEW_MATRIX_COPY_OFFSET = 0x0c23bd,
    VIEW_DERIVED_UPDATE_OFFSET = 0x27dd48,
    VIEW_STATE_GLOBAL_OFFSET = 0x4a00a0,
    VIEW_STATE_REFERENCE_OFFSET = 0x4124ec,
    CAMERA_VIEW_RETURN_OFFSET = 0x0bf5e3,
    CAMERA_VIEW_CALLSITE_OFFSET = 0x0bf5d2,
    INTERPOLATION_CHANNEL = 1,
    VIEW_MATRIX_FLOATS = 12,
    VIEW_STATE_MATRIX_OFFSET = 0x130,
    CAMERA_HISTORY_COUNT = 256,
    INTERPOLATION_STEP_COUNT = 8,
};

static const float CAMERA_CUT_DISTANCE_SQUARED = 256.0f * 256.0f;
static const float CAMERA_CUT_QUATERNION_DOT = 0.70710678f;

typedef uint32_t(S3E_SOFTFP *director_callback_fn)(void *director, uint32_t channel, uint32_t step,
                                                   uint32_t value, uint32_t argument4,
                                                   uint32_t argument5);
typedef void(S3E_SOFTFP *view_matrix_copy_fn)(float *destination, const float *source);
typedef void(S3E_SOFTFP *view_derived_update_fn)(void);

struct quaternion {
    float x;
    float y;
    float z;
    float w;
};

struct camera_history {
    void *camera;
    float previous[VIEW_MATRIX_FLOATS];
    float current[VIEW_MATRIX_FLOATS];
    uint32_t generation;
    uint32_t step;
    uint32_t last_used;
    bool initialized;
};

static uint8_t *g_image_base;
static director_callback_fn g_original_fixed_callback;
static view_matrix_copy_fn g_view_matrix_copy;
static view_derived_update_fn g_view_derived_update;
static uint8_t *g_view_state_global;
static struct camera_history g_camera_histories[CAMERA_HISTORY_COUNT];
static float g_interpolation_factors[INTERPOLATION_STEP_COUNT];
static uint32_t g_fixed_generations[INTERPOLATION_STEP_COUNT];
static uint32_t g_fixed_step;
static uint32_t g_history_serial;
static bool g_logged_active;

volatile uint32_t codboz_frame_fixed_ticks;
volatile uint32_t codboz_frame_camera_views;
volatile uint32_t codboz_frame_interpolated_views;
volatile uint32_t codboz_frame_history_advances;
volatile uint32_t codboz_frame_snap_views;
volatile uint32_t codboz_frame_passthrough_views;
volatile uint32_t codboz_frame_initial_views;
volatile uint32_t codboz_frame_stale_views;
volatile uint32_t codboz_frame_cut_views;
volatile uint32_t codboz_frame_invalid_views;

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

static uint32_t image_read32(const struct s3e_loaded_image *loaded, size_t offset) {
    return read32(loaded->base + offset);
}

static void image_write32(struct s3e_loaded_image *loaded, size_t offset, uint32_t value) {
    write32(loaded->base + offset, value);
}

static void *read_pointer(const void *base, size_t offset) {
    return (void *)(uintptr_t)read32((const uint8_t *)base + offset);
}

static float clamp_factor(float factor) {
    if (!(factor > 0.0f)) {
        return 0.0f;
    }
    if (factor >= 1.0f) {
        return 1.0f;
    }
    return factor;
}

static bool normalize_quaternion(struct quaternion *value) {
    float length_squared =
        value->x * value->x + value->y * value->y + value->z * value->z + value->w * value->w;
    if (!isfinite(length_squared) || length_squared < 0.000001f) {
        return false;
    }

    float inverse_length = 1.0f / sqrtf(length_squared);
    value->x *= inverse_length;
    value->y *= inverse_length;
    value->z *= inverse_length;
    value->w *= inverse_length;
    return true;
}

static bool matrix_rotation_to_quaternion(const float *matrix, struct quaternion *result) {
    float trace = matrix[0] + matrix[4] + matrix[8];
    if (trace > 0.0f) {
        float scale = sqrtf(trace + 1.0f) * 2.0f;
        if (!(scale > 0.0f)) {
            return false;
        }
        result->w = 0.25f * scale;
        result->x = (matrix[5] - matrix[7]) / scale;
        result->y = (matrix[6] - matrix[2]) / scale;
        result->z = (matrix[1] - matrix[3]) / scale;
    } else if (matrix[0] > matrix[4] && matrix[0] > matrix[8]) {
        float scale = sqrtf(1.0f + matrix[0] - matrix[4] - matrix[8]) * 2.0f;
        if (!(scale > 0.0f)) {
            return false;
        }
        result->w = (matrix[5] - matrix[7]) / scale;
        result->x = 0.25f * scale;
        result->y = (matrix[3] + matrix[1]) / scale;
        result->z = (matrix[6] + matrix[2]) / scale;
    } else if (matrix[4] > matrix[8]) {
        float scale = sqrtf(1.0f + matrix[4] - matrix[0] - matrix[8]) * 2.0f;
        if (!(scale > 0.0f)) {
            return false;
        }
        result->w = (matrix[6] - matrix[2]) / scale;
        result->x = (matrix[3] + matrix[1]) / scale;
        result->y = 0.25f * scale;
        result->z = (matrix[7] + matrix[5]) / scale;
    } else {
        float scale = sqrtf(1.0f + matrix[8] - matrix[0] - matrix[4]) * 2.0f;
        if (!(scale > 0.0f)) {
            return false;
        }
        result->w = (matrix[1] - matrix[3]) / scale;
        result->x = (matrix[6] + matrix[2]) / scale;
        result->y = (matrix[7] + matrix[5]) / scale;
        result->z = 0.25f * scale;
    }
    return normalize_quaternion(result);
}

static void quaternion_to_matrix_rotation(const struct quaternion *value, float *matrix) {
    float xx = value->x * value->x;
    float yy = value->y * value->y;
    float zz = value->z * value->z;
    float xy = value->x * value->y;
    float xz = value->x * value->z;
    float yz = value->y * value->z;
    float wx = value->w * value->x;
    float wy = value->w * value->y;
    float wz = value->w * value->z;

    matrix[0] = 1.0f - 2.0f * (yy + zz);
    matrix[1] = 2.0f * (xy + wz);
    matrix[2] = 2.0f * (xz - wy);
    matrix[3] = 2.0f * (xy - wz);
    matrix[4] = 1.0f - 2.0f * (xx + zz);
    matrix[5] = 2.0f * (yz + wx);
    matrix[6] = 2.0f * (xz + wy);
    matrix[7] = 2.0f * (yz - wx);
    matrix[8] = 1.0f - 2.0f * (xx + yy);
}

static void camera_position_from_view(const float *matrix, float *position) {
    position[0] = -(matrix[0] * matrix[9] + matrix[1] * matrix[10] + matrix[2] * matrix[11]);
    position[1] = -(matrix[3] * matrix[9] + matrix[4] * matrix[10] + matrix[5] * matrix[11]);
    position[2] = -(matrix[6] * matrix[9] + matrix[7] * matrix[10] + matrix[8] * matrix[11]);
}

static void view_translation_from_camera_position(float *matrix, const float *position) {
    matrix[9] = -(matrix[0] * position[0] + matrix[3] * position[1] + matrix[6] * position[2]);
    matrix[10] = -(matrix[1] * position[0] + matrix[4] * position[1] + matrix[7] * position[2]);
    matrix[11] = -(matrix[2] * position[0] + matrix[5] * position[1] + matrix[8] * position[2]);
}

static bool interpolate_view_matrix(const float *previous, const float *current, float factor,
                                    float *output, bool *cut) {
    struct quaternion before_rotation;
    struct quaternion current_rotation;
    float before_position[3];
    float current_position[3];

    *cut = false;
    for (size_t i = 0; i < VIEW_MATRIX_FLOATS; ++i) {
        if (!isfinite(previous[i]) || !isfinite(current[i])) {
            return false;
        }
    }
    if (!matrix_rotation_to_quaternion(previous, &before_rotation) ||
        !matrix_rotation_to_quaternion(current, &current_rotation)) {
        return false;
    }

    camera_position_from_view(previous, before_position);
    camera_position_from_view(current, current_position);
    float dx = current_position[0] - before_position[0];
    float dy = current_position[1] - before_position[1];
    float dz = current_position[2] - before_position[2];
    float distance_squared = dx * dx + dy * dy + dz * dz;
    float rotation_dot =
        before_rotation.x * current_rotation.x + before_rotation.y * current_rotation.y +
        before_rotation.z * current_rotation.z + before_rotation.w * current_rotation.w;
    if (!isfinite(distance_squared) || !isfinite(rotation_dot)) {
        return false;
    }
    if (rotation_dot < 0.0f) {
        current_rotation.x = -current_rotation.x;
        current_rotation.y = -current_rotation.y;
        current_rotation.z = -current_rotation.z;
        current_rotation.w = -current_rotation.w;
        rotation_dot = -rotation_dot;
    }
    if (distance_squared > CAMERA_CUT_DISTANCE_SQUARED ||
        rotation_dot < CAMERA_CUT_QUATERNION_DOT) {
        *cut = true;
        return true;
    }

    factor = clamp_factor(factor);
    struct quaternion interpolated = {
        .x = before_rotation.x + (current_rotation.x - before_rotation.x) * factor,
        .y = before_rotation.y + (current_rotation.y - before_rotation.y) * factor,
        .z = before_rotation.z + (current_rotation.z - before_rotation.z) * factor,
        .w = before_rotation.w + (current_rotation.w - before_rotation.w) * factor,
    };
    if (!normalize_quaternion(&interpolated)) {
        return false;
    }

    quaternion_to_matrix_rotation(&interpolated, output);
    float interpolated_position[3] = {
        before_position[0] + dx * factor,
        before_position[1] + dy * factor,
        before_position[2] + dz * factor,
    };
    view_translation_from_camera_position(output, interpolated_position);
    return true;
}

static struct camera_history *camera_history_for(void *camera, uint32_t step) {
    struct camera_history *replacement = &g_camera_histories[0];
    for (size_t i = 0; i < CAMERA_HISTORY_COUNT; ++i) {
        struct camera_history *history = &g_camera_histories[i];
        if (history->camera == camera && history->step == step) {
            history->last_used = ++g_history_serial;
            return history;
        }
        if (!history->initialized || history->last_used < replacement->last_used) {
            replacement = history;
        }
    }

    memset(replacement, 0, sizeof(*replacement));
    replacement->camera = camera;
    replacement->step = step;
    replacement->last_used = ++g_history_serial;
    return replacement;
}

__attribute__((used, noinline)) static void
submit_view_matrix(const float *matrix, uintptr_t return_address, void *camera) {
    void *view_state = read_pointer(g_view_state_global, 0);
    // The guest setter dereferences this state at +0x130, so a null state cannot be forwarded.
    if (!view_state || !matrix) {
        return;
    }

    const float *submitted = matrix;
    float interpolated[VIEW_MATRIX_FLOATS];
    uintptr_t camera_return = (uintptr_t)g_image_base + CAMERA_VIEW_RETURN_OFFSET;
    if (return_address == camera_return && camera) {
        ++codboz_frame_camera_views;
        uint32_t step = g_fixed_step;
        if (step >= INTERPOLATION_STEP_COUNT) {
            ++codboz_frame_passthrough_views;
        } else {
            uint32_t generation = g_fixed_generations[step];
            struct camera_history *history = camera_history_for(camera, step);
            if (!history->initialized) {
                memcpy(history->previous, matrix, sizeof(history->previous));
                memcpy(history->current, matrix, sizeof(history->current));
                history->generation = generation;
                history->initialized = true;
                ++codboz_frame_initial_views;
                ++codboz_frame_snap_views;
            } else if (generation - history->generation > 1u) {
                memcpy(history->previous, matrix, sizeof(history->previous));
                memcpy(history->current, matrix, sizeof(history->current));
                history->generation = generation;
                ++codboz_frame_stale_views;
                ++codboz_frame_snap_views;
            } else {
                if (history->generation != generation) {
                    memcpy(history->previous, history->current, sizeof(history->previous));
                    history->generation = generation;
                    ++codboz_frame_history_advances;
                }
                memcpy(history->current, matrix, sizeof(history->current));

                float factor = g_interpolation_factors[step];
                bool cut;
                if (interpolate_view_matrix(history->previous, history->current, factor,
                                            interpolated, &cut) &&
                    !cut) {
                    submitted = interpolated;
                    ++codboz_frame_interpolated_views;
                    if (!g_logged_active && factor > 0.0f && factor < 1.0f &&
                        memcmp(interpolated, matrix, sizeof(interpolated)) != 0) {
                        fprintf(stderr, "[frame-interpolation] camera interpolation active\n");
                        g_logged_active = true;
                    }
                } else {
                    memcpy(history->previous, matrix, sizeof(history->previous));
                    memcpy(history->current, matrix, sizeof(history->current));
                    if (cut) {
                        ++codboz_frame_cut_views;
                    } else {
                        ++codboz_frame_invalid_views;
                    }
                    ++codboz_frame_snap_views;
                }
            }
        }
    } else {
        ++codboz_frame_passthrough_views;
    }

    g_view_matrix_copy((float *)((uint8_t *)view_state + VIEW_STATE_MATRIX_OFFSET), submitted);
    g_view_derived_update();
}

#if defined(__arm__)
__attribute__((naked, used)) static void view_matrix_gateway(void) {
    __asm__ volatile("mov r2, r5\n"
                     "mov r1, lr\n"
                     "push {r4, lr}\n"
                     "bl submit_view_matrix\n"
                     "pop {r4, pc}\n");
}
#else
static void view_matrix_gateway(const float *matrix) {
    submit_view_matrix(matrix, (uintptr_t)__builtin_return_address(0), NULL);
}
#endif

static S3E_SOFTFP uint32_t director_fixed_callback(void *director, uint32_t channel, uint32_t step,
                                                   uint32_t value, uint32_t argument4,
                                                   uint32_t argument5) {
    if (channel == INTERPOLATION_CHANNEL) {
        if (step < INTERPOLATION_STEP_COUNT) {
            g_fixed_step = step;
            ++g_fixed_generations[step];
        } else {
            g_fixed_step = INTERPOLATION_STEP_COUNT;
        }
        ++codboz_frame_fixed_ticks;
    }
    return g_original_fixed_callback(director, channel, step, value, argument4, argument5);
}

static S3E_SOFTFP uint32_t director_interpolate_callback(void *director, uint32_t channel,
                                                         uint32_t step, uint32_t factor_bits,
                                                         uint32_t argument4, uint32_t argument5) {
    (void)argument4;
    (void)argument5;
    if (channel == INTERPOLATION_CHANNEL && step < INTERPOLATION_STEP_COUNT) {
        float factor;
        memcpy(&factor, &factor_bits, sizeof(factor));
        g_interpolation_factors[step] = clamp_factor(factor);
    }
    return (uint32_t)(uintptr_t)director;
}

bool codboz_install_frame_interpolation(struct s3e_loaded_image *loaded) {
    static const uint8_t fixed_signature[] = {
        0x02, 0x29, 0x2d, 0xe9, 0xf0, 0x41, 0x80, 0x46,
        0x0f, 0x46, 0x16, 0x46, 0xdd, 0xe9, 0x06, 0x45,
    };
    static const uint8_t interpolate_signature[] = {0x70, 0x47};
    static const uint8_t view_setter_signature[] = {
        0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1,
        0x1c, 0x30, 0x9f, 0xe5, 0x1c, 0x20, 0x9f, 0xe5,
    };
    static const uint8_t camera_callsite_signature[] = {
        0x6c, 0x6b, 0x20, 0x46, 0x02, 0xf0, 0x57, 0xff, 0x04, 0xf1, 0x38,
        0x00, 0xbe, 0xf1, 0x2e, 0xec, 0x19, 0xb0, 0xbd, 0xe8, 0xf0, 0x8f,
    };
    static const uint8_t matrix_copy_signature[] = {
        0xf0, 0xb5, 0x0d, 0x46, 0x06, 0x46, 0x0f, 0x46,
        0x04, 0x46, 0x0f, 0xcd, 0x0f, 0xc4, 0x0f, 0xcd,
    };
    static const uint8_t derived_update_signature[] = {
        0x70, 0x40, 0x2d, 0xe9, 0x60, 0xd0, 0x4d, 0xe2,
        0xdc, 0x40, 0x9f, 0xe5, 0x53, 0xff, 0xff, 0xeb,
    };

    if (!image_range_valid(loaded, DIRECTOR_FIXED_SLOT_OFFSET, 2 * sizeof(uint32_t)) ||
        !image_range_valid(loaded, VIEW_STATE_GLOBAL_OFFSET, sizeof(uint32_t)) ||
        !image_matches(loaded, DIRECTOR_FIXED_CALLBACK_OFFSET & ~1u, fixed_signature,
                       sizeof(fixed_signature)) ||
        !image_matches(loaded, DIRECTOR_INTERPOLATE_CALLBACK_OFFSET & ~1u, interpolate_signature,
                       sizeof(interpolate_signature)) ||
        !image_matches(loaded, VIEW_SETTER_OFFSET, view_setter_signature,
                       sizeof(view_setter_signature)) ||
        !image_matches(loaded, CAMERA_VIEW_CALLSITE_OFFSET, camera_callsite_signature,
                       sizeof(camera_callsite_signature)) ||
        !image_matches(loaded, VIEW_MATRIX_COPY_OFFSET & ~1u, matrix_copy_signature,
                       sizeof(matrix_copy_signature)) ||
        !image_matches(loaded, VIEW_DERIVED_UPDATE_OFFSET, derived_update_signature,
                       sizeof(derived_update_signature))) {
        return false;
    }

    uintptr_t image_base = (uintptr_t)loaded->base;
    uintptr_t fixed_hook = (uintptr_t)director_fixed_callback;
    uintptr_t interpolate_hook = (uintptr_t)director_interpolate_callback;
    uintptr_t view_hook = (uintptr_t)view_matrix_gateway;
    if (image_base > UINT32_MAX || fixed_hook > UINT32_MAX || interpolate_hook > UINT32_MAX ||
        view_hook > UINT32_MAX ||
        image_read32(loaded, DIRECTOR_FIXED_SLOT_OFFSET) !=
            (uint32_t)image_base + DIRECTOR_FIXED_CALLBACK_OFFSET ||
        image_read32(loaded, DIRECTOR_INTERPOLATE_SLOT_OFFSET) !=
            (uint32_t)image_base + DIRECTOR_INTERPOLATE_CALLBACK_OFFSET ||
        image_read32(loaded, VIEW_STATE_REFERENCE_OFFSET) !=
            (uint32_t)image_base + VIEW_STATE_GLOBAL_OFFSET) {
        return false;
    }

    g_image_base = loaded->base;
    g_original_fixed_callback = (director_callback_fn)(image_base + DIRECTOR_FIXED_CALLBACK_OFFSET);
    g_view_matrix_copy = (view_matrix_copy_fn)(image_base + VIEW_MATRIX_COPY_OFFSET);
    g_view_derived_update = (view_derived_update_fn)(image_base + VIEW_DERIVED_UPDATE_OFFSET);
    g_view_state_global = loaded->base + VIEW_STATE_GLOBAL_OFFSET;
    memset(g_camera_histories, 0, sizeof(g_camera_histories));
    for (size_t i = 0; i < INTERPOLATION_STEP_COUNT; ++i) {
        g_interpolation_factors[i] = 1.0f;
    }
    memset(g_fixed_generations, 0, sizeof(g_fixed_generations));
    g_fixed_step = INTERPOLATION_STEP_COUNT;
    g_history_serial = 0;
    g_logged_active = false;
    codboz_frame_fixed_ticks = 0;
    codboz_frame_camera_views = 0;
    codboz_frame_interpolated_views = 0;
    codboz_frame_history_advances = 0;
    codboz_frame_snap_views = 0;
    codboz_frame_passthrough_views = 0;
    codboz_frame_initial_views = 0;
    codboz_frame_stale_views = 0;
    codboz_frame_cut_views = 0;
    codboz_frame_invalid_views = 0;

    image_write32(loaded, DIRECTOR_FIXED_SLOT_OFFSET, (uint32_t)fixed_hook);
    image_write32(loaded, DIRECTOR_INTERPOLATE_SLOT_OFFSET, (uint32_t)interpolate_hook);
    image_write32(loaded, VIEW_SETTER_OFFSET, 0xe51ff004u);
    image_write32(loaded, VIEW_SETTER_OFFSET + sizeof(uint32_t), (uint32_t)view_hook);
    __builtin___clear_cache((char *)loaded->base + VIEW_SETTER_OFFSET,
                            (char *)loaded->base + VIEW_SETTER_OFFSET + 2 * sizeof(uint32_t));
    return true;
}
