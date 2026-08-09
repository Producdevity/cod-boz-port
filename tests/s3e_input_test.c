#include "s3e_host_internal.h"

#include <assert.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

enum {
    TEST_SDL_BUTTON_BACK = 4,
    TEST_SDL_BUTTON_RIGHTSHOULDER = 10,
    TEST_SDL_AXIS_TRIGGERRIGHT = 5,
    TEST_XPERIA_KEY_SHOOT = 75,
    TEST_KEY_STATE_DOWN = 1,
    TEST_KEY_STATE_PRESSED = 2,
    TEST_KEY_STATE_RELEASED = 4,
};

struct keyboard_event_log {
    struct s3e_keyboard_event events[32];
    size_t count;
};

struct surface_geometry g_surface = {0, 0, 640, 480};
struct callback_slot g_pointer_callbacks[4];
struct callback_slot g_touchpad_callbacks[8];
struct keyboard_callback_slot g_keyboard_callbacks[16];
int32_t g_pointer_x = 110;
int32_t g_pointer_y = 90;
int g_pointer_down;
uint8_t g_pointer_states[5];
int g_cursor_active;

static uint8_t g_fake_buttons[15];
static int16_t g_fake_axes[6];
static int g_fake_joystick_count = 1;
static int g_fake_controller_tokens[4];
static uint8_t g_fake_controller_mapped[4];
static uint8_t g_fake_controller_attached[4];
static uint8_t g_fake_controller_openable[4];
static int g_fake_opened_index;
static int g_fake_pump_count;
static int g_fake_flush_count;
static uint64_t g_fake_now;
static struct keyboard_event_log g_key_events;
static struct keyboard_event_log g_character_events;

static int fake_sdl_init(uint32_t flags) {
    (void)flags;
    return 0;
}

static void fake_sdl_quit(uint32_t flags) {
    (void)flags;
}

static void fake_pump_events(void) {
    ++g_fake_pump_count;
}

static void fake_flush_events(uint32_t min_type, uint32_t max_type) {
    assert(min_type == 0);
    assert(max_type == 0xffff);
    ++g_fake_flush_count;
}

static int fake_num_joysticks(void) {
    return g_fake_joystick_count;
}

static int fake_is_game_controller(int joystick_index) {
    return joystick_index >= 0 && joystick_index < g_fake_joystick_count &&
           g_fake_controller_mapped[joystick_index];
}

static void *fake_game_controller_open(int joystick_index) {
    if (!fake_is_game_controller(joystick_index) || !g_fake_controller_openable[joystick_index] ||
        !g_fake_controller_attached[joystick_index]) {
        return NULL;
    }
    g_fake_opened_index = joystick_index;
    return &g_fake_controller_tokens[joystick_index];
}

static int fake_controller_index(void *controller) {
    for (size_t i = 0; i < ARRAY_SIZE(g_fake_controller_tokens); ++i) {
        if (controller == &g_fake_controller_tokens[i]) {
            return (int)i;
        }
    }
    assert(0 && "unknown fake controller");
    return -1;
}

static void fake_game_controller_close(void *controller) {
    (void)fake_controller_index(controller);
}

static void *fake_game_controller_get_joystick(void *controller) {
    (void)fake_controller_index(controller);
    return controller;
}

static int fake_game_controller_get_attached(void *controller) {
    return g_fake_controller_attached[fake_controller_index(controller)];
}

static const char *fake_joystick_name_for_index(int joystick_index) {
    assert(joystick_index >= 0 && joystick_index < g_fake_joystick_count);
    return joystick_index ? "fake controller 2" : "fake controller 1";
}

static int fake_joystick_num_hats(void *joystick) {
    (void)fake_controller_index(joystick);
    return 0;
}

static uint8_t fake_joystick_get_hat(void *joystick, int hat) {
    (void)fake_controller_index(joystick);
    (void)hat;
    return 0;
}

static void fake_game_controller_update(void) {}

static int16_t fake_game_controller_get_axis(void *controller, int axis) {
    (void)fake_controller_index(controller);
    assert(axis >= 0 && axis < (int)ARRAY_SIZE(g_fake_axes));
    return g_fake_axes[axis];
}

static uint8_t fake_game_controller_get_button(void *controller, int button) {
    (void)fake_controller_index(controller);
    assert(button >= 0 && button < (int)ARRAY_SIZE(g_fake_buttons));
    return g_fake_buttons[button];
}

static const char *fake_sdl_error(void) {
    return "fake input error";
}

void *open_first(const char *const *names) {
    assert(names && names[0]);
    return &g_fake_controller_tokens[0];
}

void *dlsym(void *handle, const char *name) {
    assert(handle == &g_fake_controller_tokens[0]);
#define TEST_SYMBOL(symbol, function)                                                              \
    if (strcmp(name, symbol) == 0) {                                                               \
        return (void *)(uintptr_t)&function;                                                       \
    }
    TEST_SYMBOL("SDL_InitSubSystem", fake_sdl_init)
    TEST_SYMBOL("SDL_QuitSubSystem", fake_sdl_quit)
    TEST_SYMBOL("SDL_PumpEvents", fake_pump_events)
    TEST_SYMBOL("SDL_FlushEvents", fake_flush_events)
    TEST_SYMBOL("SDL_NumJoysticks", fake_num_joysticks)
    TEST_SYMBOL("SDL_IsGameController", fake_is_game_controller)
    TEST_SYMBOL("SDL_JoystickNameForIndex", fake_joystick_name_for_index)
    TEST_SYMBOL("SDL_GameControllerNameForIndex", fake_joystick_name_for_index)
    TEST_SYMBOL("SDL_GameControllerOpen", fake_game_controller_open)
    TEST_SYMBOL("SDL_GameControllerClose", fake_game_controller_close)
    TEST_SYMBOL("SDL_GameControllerGetAttached", fake_game_controller_get_attached)
    TEST_SYMBOL("SDL_GameControllerGetJoystick", fake_game_controller_get_joystick)
    TEST_SYMBOL("SDL_JoystickNumHats", fake_joystick_num_hats)
    TEST_SYMBOL("SDL_JoystickGetHat", fake_joystick_get_hat)
    TEST_SYMBOL("SDL_GameControllerUpdate", fake_game_controller_update)
    TEST_SYMBOL("SDL_GameControllerGetAxis", fake_game_controller_get_axis)
    TEST_SYMBOL("SDL_GameControllerGetButton", fake_game_controller_get_button)
    TEST_SYMBOL("SDL_GetError", fake_sdl_error)
#undef TEST_SYMBOL
    return NULL;
}

int dlclose(void *handle) {
    assert(handle == &g_fake_controller_tokens[0]);
    return 0;
}

uint64_t monotonic_ms(void) {
    g_fake_now += 16;
    return g_fake_now;
}

void dispatch_due_timers(void) {}

static int32_t record_keyboard_event(void *system_data, void *user_data) {
    struct keyboard_event_log *log = user_data;
    assert(log->count < ARRAY_SIZE(log->events));
    log->events[log->count++] = *(const struct s3e_keyboard_event *)system_data;
    return 0;
}

static void reset_input(void) {
    memset(g_keyboard_callbacks, 0, sizeof(g_keyboard_callbacks));
    memset(g_pointer_callbacks, 0, sizeof(g_pointer_callbacks));
    memset(g_touchpad_callbacks, 0, sizeof(g_touchpad_callbacks));
    input_shutdown();

    memset(g_fake_buttons, 0, sizeof(g_fake_buttons));
    memset(g_fake_axes, 0, sizeof(g_fake_axes));
    memset(g_pointer_states, 0, sizeof(g_pointer_states));
    memset(&g_key_events, 0, sizeof(g_key_events));
    memset(&g_character_events, 0, sizeof(g_character_events));
    memset(g_fake_controller_mapped, 0, sizeof(g_fake_controller_mapped));
    memset(g_fake_controller_attached, 0, sizeof(g_fake_controller_attached));
    memset(g_fake_controller_openable, 0, sizeof(g_fake_controller_openable));
    g_fake_joystick_count = 1;
    g_fake_controller_mapped[0] = 1;
    g_fake_controller_attached[0] = 1;
    g_fake_controller_openable[0] = 1;
    g_fake_opened_index = -1;
    g_fake_pump_count = 0;
    g_fake_flush_count = 0;
    g_fake_now = 0;
    g_pointer_down = 0;
    g_cursor_active = 0;
    s3eKeyboardClearState();

    assert(s3eKeyboardRegister(0, (void *)(uintptr_t)&record_keyboard_event, &g_key_events) == 0);
    assert(s3eKeyboardRegister(1, (void *)(uintptr_t)&record_keyboard_event, &g_character_events) ==
           0);
}

static void assert_event(const struct keyboard_event_log *log, size_t index, int pressed) {
    assert(index < log->count);
    assert(log->events[index].key == TEST_XPERIA_KEY_SHOOT);
    assert(log->events[index].pressed == pressed);
}

static void test_skips_unmapped_joysticks(void) {
    reset_input();

    g_fake_joystick_count = 2;
    g_fake_controller_mapped[0] = 0;
    g_fake_controller_mapped[1] = 1;
    g_fake_controller_attached[1] = 1;
    g_fake_controller_openable[1] = 1;

    input_pump();
    assert(g_fake_opened_index == 1);
    assert(g_fake_pump_count == 1);
    assert(g_fake_flush_count == 1);
}

static void test_retries_controller_discovery(void) {
    reset_input();

    g_fake_joystick_count = 0;
    input_pump();
    assert(g_fake_opened_index == -1);

    g_fake_joystick_count = 1;
    for (int i = 0; i < 64 && g_fake_opened_index < 0; ++i) {
        input_pump();
    }
    assert(g_fake_opened_index == 0);
}

static void test_disconnect_releases_input_and_reconnects(void) {
    reset_input();

    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 1;
    input_pump();
    assert(g_fake_opened_index == 0);
    assert(g_key_events.count == 1);

    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 0;
    g_fake_controller_attached[0] = 0;
    g_fake_joystick_count = 2;
    g_fake_controller_mapped[1] = 1;
    g_fake_controller_attached[1] = 1;
    g_fake_controller_openable[1] = 1;
    input_pump();

    assert(g_fake_opened_index == 1);
    assert(g_key_events.count == 2);
    assert_event(&g_key_events, 1, 0);

    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 1;
    input_pump();
    assert(g_key_events.count == 3);
    assert_event(&g_key_events, 2, 1);
}

static void test_first_hold_publishes_down_until_release(void) {
    reset_input();

    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 1;
    input_pump();
    assert(g_key_events.count == 1);
    assert(g_character_events.count == 0);
    assert_event(&g_key_events, 0, 1);
    assert(s3eKeyboardGetState(TEST_XPERIA_KEY_SHOOT) == 0);

    assert(s3eKeyboardUpdate() == 0);
    assert(s3eKeyboardGetState(TEST_XPERIA_KEY_SHOOT) ==
           (TEST_KEY_STATE_DOWN | TEST_KEY_STATE_PRESSED));
    assert(s3eKeyboardAnyKey() == TEST_XPERIA_KEY_SHOOT);

    assert(s3eKeyboardUpdate() == 0);
    assert(s3eKeyboardGetState(TEST_XPERIA_KEY_SHOOT) == TEST_KEY_STATE_DOWN);
    assert(s3eKeyboardAnyKey() == 0);
    assert(g_key_events.count == 1);

    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 0;
    input_pump();
    assert(g_key_events.count == 2);
    assert_event(&g_key_events, 1, 0);
    assert(s3eKeyboardUpdate() == 0);
    assert(s3eKeyboardGetState(TEST_XPERIA_KEY_SHOOT) == TEST_KEY_STATE_RELEASED);
    assert(s3eKeyboardUpdate() == 0);
    assert(s3eKeyboardGetState(TEST_XPERIA_KEY_SHOOT) == 0);
}

static void test_overlapping_shoot_sources_release_once(void) {
    reset_input();

    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 1;
    input_pump();
    g_fake_axes[TEST_SDL_AXIS_TRIGGERRIGHT] = 20000;
    input_pump();
    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 0;
    input_pump();
    assert(g_key_events.count == 1);

    g_fake_axes[TEST_SDL_AXIS_TRIGGERRIGHT] = 12000;
    input_pump();
    assert(g_key_events.count == 2);
    assert_event(&g_key_events, 1, 0);
}

static void test_trigger_hysteresis_ignores_threshold_jitter(void) {
    reset_input();

    g_fake_axes[TEST_SDL_AXIS_TRIGGERRIGHT] = 17000;
    input_pump();
    assert(g_key_events.count == 1);

    g_fake_axes[TEST_SDL_AXIS_TRIGGERRIGHT] = 15000;
    input_pump();
    g_fake_axes[TEST_SDL_AXIS_TRIGGERRIGHT] = 13000;
    input_pump();
    g_fake_axes[TEST_SDL_AXIS_TRIGGERRIGHT] = 16500;
    input_pump();
    assert(g_key_events.count == 1);

    g_fake_axes[TEST_SDL_AXIS_TRIGGERRIGHT] = 12000;
    input_pump();
    assert(g_key_events.count == 2);
    assert_event(&g_key_events, 1, 0);
}

static void test_opposite_edges_survive_until_publication(void) {
    reset_input();

    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 1;
    input_pump();
    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 0;
    input_pump();
    assert(s3eKeyboardUpdate() == 0);
    assert(s3eKeyboardGetState(TEST_XPERIA_KEY_SHOOT) ==
           (TEST_KEY_STATE_PRESSED | TEST_KEY_STATE_RELEASED));

    reset_input();
    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 1;
    input_pump();
    assert(s3eKeyboardUpdate() == 0);
    assert(s3eKeyboardUpdate() == 0);
    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 0;
    input_pump();
    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 1;
    input_pump();
    assert(s3eKeyboardUpdate() == 0);
    assert(s3eKeyboardGetState(TEST_XPERIA_KEY_SHOOT) ==
           (TEST_KEY_STATE_DOWN | TEST_KEY_STATE_PRESSED | TEST_KEY_STATE_RELEASED));
}

static void test_cursor_mode_releases_and_reacquires_held_action(void) {
    reset_input();

    g_fake_buttons[TEST_SDL_BUTTON_RIGHTSHOULDER] = 1;
    input_pump();
    assert(s3eKeyboardUpdate() == 0);

    g_fake_buttons[TEST_SDL_BUTTON_BACK] = 1;
    input_pump();
    assert(g_cursor_active == 1);
    assert(g_key_events.count == 2);
    assert_event(&g_key_events, 1, 0);
    assert(s3eKeyboardUpdate() == 0);
    assert(s3eKeyboardGetState(TEST_XPERIA_KEY_SHOOT) == TEST_KEY_STATE_RELEASED);

    g_fake_buttons[TEST_SDL_BUTTON_BACK] = 0;
    input_pump();
    g_fake_buttons[TEST_SDL_BUTTON_BACK] = 1;
    input_pump();
    assert(g_cursor_active == 0);
    assert(g_key_events.count == 3);
    assert_event(&g_key_events, 2, 1);
    assert(s3eKeyboardUpdate() == 0);
    assert(s3eKeyboardGetState(TEST_XPERIA_KEY_SHOOT) ==
           (TEST_KEY_STATE_DOWN | TEST_KEY_STATE_PRESSED));
}

int main(void) {
    test_skips_unmapped_joysticks();
    test_retries_controller_discovery();
    test_disconnect_releases_input_and_reconnects();
    test_first_hold_publishes_down_until_release();
    test_overlapping_shoot_sources_release_once();
    test_trigger_hysteresis_ignores_threshold_jitter();
    test_opposite_edges_survive_until_publication();
    test_cursor_mode_releases_and_reacquires_held_action();
    reset_input();
    input_shutdown();
    puts("s3e_input_test: ok");
    return 0;
}
