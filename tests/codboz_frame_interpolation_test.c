#include "codboz_frame_interpolation.h"
#include "s3e_host_internal.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#if defined(NDEBUG)
#error "codboz_frame_interpolation_test requires assertions"
#endif

enum {
    IMAGE_SIZE = 0x4b0000,
    DIRECTOR_FIXED_SLOT_OFFSET = 0x3e6490,
    DIRECTOR_INTERPOLATE_SLOT_OFFSET = 0x3e6494,
    DIRECTOR_FIXED_CALLBACK_OFFSET = 0x0ba98d,
    DIRECTOR_INTERPOLATE_CALLBACK_OFFSET = 0x0ba7db,
    VIEW_SETTER_OFFSET = 0x27de3c,
    VIEW_MATRIX_COPY_OFFSET = 0x0c23bd,
    VIEW_DERIVED_UPDATE_OFFSET = 0x27dd48,
    VIEW_STATE_GLOBAL_OFFSET = 0x4a00a0,
    VIEW_STATE_REFERENCE_OFFSET = 0x4124ec,
    CAMERA_VIEW_CALLSITE_OFFSET = 0x0bf5d2,
    CAMERA_TEST_TRAMPOLINE_OFFSET = 0x0bf5da,
    VIEW_STATE_MATRIX_OFFSET = 0x130,
    VIEW_MATRIX_FLOATS = 12,
    INTERPOLATION_STEP_COUNT = 8,
};

typedef uint32_t(S3E_SOFTFP *director_callback_fn)(void *director, uint32_t channel, uint32_t step,
                                                   uint32_t value, uint32_t argument4,
                                                   uint32_t argument5);
typedef void(S3E_SOFTFP *view_setter_fn)(const float *matrix);
typedef void(S3E_SOFTFP *camera_test_fn)(const float *matrix, void *camera);

struct fixed_call_record {
    uint32_t channel;
    uint32_t step;
    uint32_t value;
    uint32_t argument4;
    uint32_t argument5;
    uint32_t calls;
};

static uint32_t g_matrix_copy_calls;
static uint32_t g_derived_update_calls;

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
    struct fixed_call_record *record = director;
    record->channel = channel;
    record->step = step;
    record->value = value;
    record->argument4 = argument4;
    record->argument5 = argument5;
    ++record->calls;
    return 0x77;
}

static S3E_SOFTFP void matrix_copy_stub(float *destination, const float *source) {
    memcpy(destination, source, VIEW_MATRIX_FLOATS * sizeof(float));
    ++g_matrix_copy_calls;
}

static S3E_SOFTFP void derived_update_stub(void) {
    ++g_derived_update_calls;
}

static void write_thumb_trampoline(uint8_t *base, size_t offset, uintptr_t target) {
    static const uint8_t aligned_code[] = {
        0xdf, 0xf8, 0x04, 0xc0, 0x60, 0x47, 0x00, 0xbf,
    };
    static const uint8_t halfword_aligned_code[] = {0xdf, 0xf8, 0x04, 0xc0, 0x60, 0x47};
    assert(target <= UINT32_MAX);
    offset &= ~1u;
    size_t literal_offset;
    if ((offset & 3u) == 0) {
        memcpy(base + offset, aligned_code, sizeof(aligned_code));
        literal_offset = sizeof(aligned_code);
    } else {
        memcpy(base + offset, halfword_aligned_code, sizeof(halfword_aligned_code));
        literal_offset = sizeof(halfword_aligned_code);
    }
    write32(base + offset + literal_offset, (uint32_t)target);
    __builtin___clear_cache((char *)base + offset,
                            (char *)base + offset + literal_offset + sizeof(uint32_t));
}

static void write_arm_trampoline(uint8_t *base, size_t offset, uintptr_t target) {
    assert((offset & 3u) == 0);
    assert(target <= UINT32_MAX);
    write32(base + offset, 0xe51ff004u);
    write32(base + offset + sizeof(uint32_t), (uint32_t)target);
    __builtin___clear_cache((char *)base + offset, (char *)base + offset + 2 * sizeof(uint32_t));
}

static void initialize_image(uint8_t *base) {
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

    uint32_t image_base = pointer32(base);
    memset(base, 0, IMAGE_SIZE);
    memcpy(base + (DIRECTOR_FIXED_CALLBACK_OFFSET & ~1u), fixed_signature, sizeof(fixed_signature));
    memcpy(base + (DIRECTOR_INTERPOLATE_CALLBACK_OFFSET & ~1u), interpolate_signature,
           sizeof(interpolate_signature));
    memcpy(base + VIEW_SETTER_OFFSET, view_setter_signature, sizeof(view_setter_signature));
    memcpy(base + CAMERA_VIEW_CALLSITE_OFFSET, camera_callsite_signature,
           sizeof(camera_callsite_signature));
    memcpy(base + (VIEW_MATRIX_COPY_OFFSET & ~1u), matrix_copy_signature,
           sizeof(matrix_copy_signature));
    memcpy(base + VIEW_DERIVED_UPDATE_OFFSET, derived_update_signature,
           sizeof(derived_update_signature));
    write32(base + DIRECTOR_FIXED_SLOT_OFFSET, image_base + DIRECTOR_FIXED_CALLBACK_OFFSET);
    write32(base + DIRECTOR_INTERPOLATE_SLOT_OFFSET,
            image_base + DIRECTOR_INTERPOLATE_CALLBACK_OFFSET);
    write32(base + VIEW_STATE_REFERENCE_OFFSET, image_base + VIEW_STATE_GLOBAL_OFFSET);
}

static void install_test_stubs(uint8_t *base) {
    static const uint8_t camera_trampoline[] = {
        0x20, 0xb5, 0x0d, 0x46, 0xbe, 0xf1, 0x2e, 0xec, 0x20, 0xbd,
    };
    write_thumb_trampoline(base, DIRECTOR_FIXED_CALLBACK_OFFSET, (uintptr_t)fixed_stub);
    write_thumb_trampoline(base, VIEW_MATRIX_COPY_OFFSET, (uintptr_t)matrix_copy_stub);
    write_arm_trampoline(base, VIEW_DERIVED_UPDATE_OFFSET, (uintptr_t)derived_update_stub);
    memcpy(base + CAMERA_TEST_TRAMPOLINE_OFFSET, camera_trampoline, sizeof(camera_trampoline));
    __builtin___clear_cache((char *)base + CAMERA_TEST_TRAMPOLINE_OFFSET,
                            (char *)base + CAMERA_TEST_TRAMPOLINE_OFFSET +
                                sizeof(camera_trampoline));
}

static void assert_install_rejections(uint8_t *base) {
    struct s3e_loaded_image loaded = {.base = base, .map_size = IMAGE_SIZE};
    assert(!codboz_install_frame_interpolation(NULL));

    loaded.map_size = VIEW_STATE_GLOBAL_OFFSET + sizeof(uint32_t) - 1;
    assert(!codboz_install_frame_interpolation(&loaded));
    loaded.map_size = IMAGE_SIZE;

    const size_t signature_offsets[] = {
        DIRECTOR_FIXED_CALLBACK_OFFSET & ~1u,
        DIRECTOR_INTERPOLATE_CALLBACK_OFFSET & ~1u,
        VIEW_SETTER_OFFSET,
        CAMERA_VIEW_CALLSITE_OFFSET,
        VIEW_MATRIX_COPY_OFFSET & ~1u,
        VIEW_DERIVED_UPDATE_OFFSET,
    };
    for (size_t i = 0; i < sizeof(signature_offsets) / sizeof(signature_offsets[0]); ++i) {
        initialize_image(base);
        base[signature_offsets[i]] ^= 0xffu;
        assert(!codboz_install_frame_interpolation(&loaded));
    }

    const size_t slot_offsets[] = {
        DIRECTOR_FIXED_SLOT_OFFSET,
        DIRECTOR_INTERPOLATE_SLOT_OFFSET,
        VIEW_STATE_REFERENCE_OFFSET,
    };
    for (size_t i = 0; i < sizeof(slot_offsets) / sizeof(slot_offsets[0]); ++i) {
        initialize_image(base);
        write32(base + slot_offsets[i], 0);
        assert(!codboz_install_frame_interpolation(&loaded));
    }
}

static void identity_view(float *matrix, float x, float y, float z) {
    memset(matrix, 0, VIEW_MATRIX_FLOATS * sizeof(float));
    matrix[0] = 1.0f;
    matrix[4] = 1.0f;
    matrix[8] = 1.0f;
    matrix[9] = x;
    matrix[10] = y;
    matrix[11] = z;
}

static void eighth_turn_view(float *matrix) {
    identity_view(matrix, 0.0f, 0.0f, 0.0f);
    matrix[0] = 0.70710678f;
    matrix[1] = 0.70710678f;
    matrix[3] = -0.70710678f;
    matrix[4] = 0.70710678f;
}

static void assert_float_close(float actual, float expected) {
    assert(fabsf(actual - expected) < 0.0005f);
}

static void assert_callback_behavior(uint8_t *base) {
    struct s3e_loaded_image loaded = {.base = base, .map_size = IMAGE_SIZE};
    initialize_image(base);
    assert(codboz_install_frame_interpolation(&loaded));
    assert(read32(base + VIEW_SETTER_OFFSET) == 0xe51ff004u);

    director_callback_fn fixed =
        (director_callback_fn)(uintptr_t)read32(base + DIRECTOR_FIXED_SLOT_OFFSET);
    director_callback_fn interpolate =
        (director_callback_fn)(uintptr_t)read32(base + DIRECTOR_INTERPOLATE_SLOT_OFFSET);
    view_setter_fn view_setter = (view_setter_fn)(uintptr_t)(base + VIEW_SETTER_OFFSET);
    camera_test_fn submit_camera =
        (camera_test_fn)(uintptr_t)(base + CAMERA_TEST_TRAMPOLINE_OFFSET + 1u);

    install_test_stubs(base);
    _Alignas(float) uint8_t view_state[0x260];
    memset(view_state, 0, sizeof(view_state));
    float *submitted = (float *)(view_state + VIEW_STATE_MATRIX_OFFSET);
    float first[VIEW_MATRIX_FLOATS];
    float second[VIEW_MATRIX_FLOATS];
    float other[VIEW_MATRIX_FLOATS];
    float cut[VIEW_MATRIX_FLOATS];
    float eighth_turn[VIEW_MATRIX_FLOATS];
    identity_view(first, 0.0f, 0.0f, 0.0f);
    identity_view(second, -10.0f, 0.0f, 0.0f);
    identity_view(other, -20.0f, 3.0f, 4.0f);
    identity_view(cut, -1000.0f, 0.0f, 0.0f);
    eighth_turn_view(eighth_turn);

    g_matrix_copy_calls = 0;
    g_derived_update_calls = 0;
    view_setter(other);
    assert(g_matrix_copy_calls == 0 && g_derived_update_calls == 0);

    write32(base + VIEW_STATE_GLOBAL_OFFSET, pointer32(view_state));
    view_setter(other);
    assert(memcmp(submitted, other, sizeof(other)) == 0);
    assert(g_matrix_copy_calls == 1 && g_derived_update_calls == 1);

    void *camera_one = (void *)(uintptr_t)0x12340000;
    void *camera_two = (void *)(uintptr_t)0x12350000;
    struct fixed_call_record call = {0};
    uint32_t result = fixed(&call, 1, 0, 0x33, 0x44, 0x55);
    assert(result == 0x77);
    assert(call.channel == 1 && call.step == 0 && call.value == 0x33 && call.argument4 == 0x44 &&
           call.argument5 == 0x55);
    fixed(&call, 1, 1, 0, 0, 0);
    submit_camera(first, camera_one);
    assert(memcmp(submitted, first, sizeof(first)) == 0);

    fixed(&call, 1, 0, 0, 0, 0);
    fixed(&call, 1, 1, 0, 0, 0);
    interpolate(&call, 1, 1, float_bits(0.5f), 0, 0);
    submit_camera(second, camera_one);
    assert_float_close(submitted[9], -5.0f);
    assert_float_close(submitted[10], 0.0f);
    assert_float_close(submitted[11], 0.0f);

    void *rotation_camera = (void *)(uintptr_t)0x12360000;
    fixed(&call, 1, 2, 0, 0, 0);
    submit_camera(first, rotation_camera);
    fixed(&call, 1, 2, 0, 0, 0);
    interpolate(&call, 1, 2, float_bits(0.5f), 0, 0);
    submit_camera(eighth_turn, rotation_camera);
    assert_float_close(submitted[0], 0.92387953f);
    assert_float_close(submitted[1], 0.38268343f);
    assert_float_close(submitted[3], -0.38268343f);
    assert_float_close(submitted[4], 0.92387953f);

    submit_camera(second, camera_two);
    assert(memcmp(submitted, second, sizeof(second)) == 0);

    fixed(&call, 1, 0, 0, 0, 0);
    fixed(&call, 1, 1, 0, 0, 0);
    interpolate(&call, 1, 1, float_bits(0.5f), 0, 0);
    submit_camera(cut, camera_one);
    assert(memcmp(submitted, cut, sizeof(cut)) == 0);

    fixed(&call, 0, 3, 0, 0, 0);
    interpolate(&call, 0, 3, float_bits(0.0f), 0, 0);
    view_setter(other);
    assert(memcmp(submitted, other, sizeof(other)) == 0);
    assert(g_matrix_copy_calls == 8 && g_derived_update_calls == 8);

    float many_first[VIEW_MATRIX_FLOATS];
    float many_second[VIEW_MATRIX_FLOATS];
    identity_view(many_first, 0.0f, 0.0f, 0.0f);
    identity_view(many_second, -8.0f, 0.0f, 0.0f);
    for (uintptr_t i = 0; i < 96; ++i) {
        submit_camera(many_first, (void *)(0x20000000u + i * 0x1000u));
    }
    fixed(&call, 1, 0, 0, 0, 0);
    fixed(&call, 1, 1, 0, 0, 0);
    interpolate(&call, 1, 1, float_bits(0.25f), 0, 0);
    submit_camera(many_second, (void *)(uintptr_t)0x20000000u);
    assert_float_close(submitted[9], -2.0f);

    void *camera_both_steps = (void *)(uintptr_t)0x30000000u;
    fixed(&call, 1, 0, 0, 0, 0);
    submit_camera(many_first, camera_both_steps);
    fixed(&call, 1, 1, 0, 0, 0);
    submit_camera(other, camera_both_steps);
    fixed(&call, 1, 0, 0, 0, 0);
    interpolate(&call, 1, 0, float_bits(0.5f), 0, 0);
    submit_camera(many_second, camera_both_steps);
    assert_float_close(submitted[9], -4.0f);

    uint32_t passthrough_views = codboz_frame_passthrough_views;
    uint32_t unsupported_step_views = codboz_frame_unsupported_step_views;
    fixed(&call, 1, INTERPOLATION_STEP_COUNT, 0, 0, 0);
    submit_camera(other, camera_both_steps);
    assert(memcmp(submitted, other, sizeof(other)) == 0);
    assert(codboz_frame_passthrough_views == passthrough_views);
    assert(codboz_frame_unsupported_step_views == unsupported_step_views + 1);
    fixed(&call, 1, 0, 0, 0, 0);
    interpolate(&call, 1, 0, float_bits(0.5f), 0, 0);
    submit_camera(first, camera_both_steps);
    assert_float_close(submitted[9], -4.0f);

    assert(codboz_frame_history_advances > 0);
    assert(g_matrix_copy_calls == 110 && g_derived_update_calls == 110);
}

int main(void) {
    void *mapping = mmap(NULL, IMAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "failed to map the synthetic guest image: %s\n", strerror(errno));
        return 1;
    }

    uint8_t *base = mapping;
    initialize_image(base);
    assert_install_rejections(base);
    assert_callback_behavior(base);

    assert(munmap(mapping, IMAGE_SIZE) == 0);
    puts("COD BOZ camera interpolation tests passed");
    return 0;
}
