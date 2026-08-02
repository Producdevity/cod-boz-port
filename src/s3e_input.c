#include "s3e_host_internal.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

enum {
    SDL_INIT_JOYSTICK = 0x00000200u,
    SDL_INIT_GAMECONTROLLER = 0x00002000u,
};

enum {
    SDL_BUTTON_A = 0,
    SDL_BUTTON_B = 1,
    SDL_BUTTON_X = 2,
    SDL_BUTTON_Y = 3,
    SDL_BUTTON_BACK = 4,
    SDL_BUTTON_START = 6,
    SDL_BUTTON_LEFTSHOULDER = 9,
    SDL_BUTTON_RIGHTSHOULDER = 10,
    SDL_BUTTON_DPAD_UP = 11,
    SDL_BUTTON_DPAD_DOWN = 12,
    SDL_BUTTON_DPAD_LEFT = 13,
    SDL_BUTTON_DPAD_RIGHT = 14,
};

enum {
    SDL_AXIS_LEFTX = 0,
    SDL_AXIS_LEFTY = 1,
    SDL_AXIS_RIGHTX = 2,
    SDL_AXIS_RIGHTY = 3,
    SDL_AXIS_TRIGGERLEFT = 4,
    SDL_AXIS_TRIGGERRIGHT = 5,
};

enum {
    SDL_HAT_UP = 0x01,
    SDL_HAT_RIGHT = 0x02,
    SDL_HAT_DOWN = 0x04,
    SDL_HAT_LEFT = 0x08,
};

enum {
    POINTER_STATE_UP = 0,
    POINTER_STATE_DOWN = 1,
    POINTER_STATE_PRESSED = 2,
    POINTER_STATE_RELEASED = 4,
};

enum {
    KEY_STATE_DOWN = 1,
    KEY_STATE_PRESSED = 2,
    KEY_STATE_RELEASED = 4,
};

enum {
    BINDING_KEY_SHOOT = 8,
    BINDING_KEY_CHANGE_WEAPON = 14,
    BINDING_KEY_CROUCH_PRONE = 48,
    BINDING_KEY_RELOAD = 40,
    BINDING_KEY_MELEE = 44,
    BINDING_KEY_ALTERNATE_FIRE = 18,
    BINDING_KEY_AIM = 100,
    BINDING_KEY_THROW_GRENADE = 29,
    BINDING_KEY_TACTICAL_GRENADE = 42,
    BINDING_KEY_ACTION_SPRINT = 28,
    BINDING_KEY_TOGGLE_FREE_MODE = 39,
    XPERIA_KEY_ALTERNATE_FIRE = 9,
    XPERIA_KEY_TACTICAL_GRENADE = 10,
    XPERIA_KEY_CHANGE_WEAPON = 11,
    XPERIA_KEY_CROUCH_PRONE = 12,
    XPERIA_KEY_AIM = 74,
    XPERIA_KEY_SHOOT = 75,
    XPERIA_KEY_ACTION_SPRINT = 78,
    XPERIA_KEY_MELEE = 89,
    XPERIA_KEY_THROW_GRENADE = 90,
    XPERIA_KEY_RELOAD = 126,
    XPERIA_KEY_PAUSE = 72,
    S3E_KEY_ABS_GAME_A = 200,
    S3E_KEY_ABS_GAME_B = 201,
    S3E_KEY_ABS_GAME_C = 202,
    S3E_KEY_ABS_GAME_D = 203,
    S3E_KEY_ABS_UP = 204,
    S3E_KEY_ABS_DOWN = 205,
    S3E_KEY_ABS_LEFT = 206,
    S3E_KEY_ABS_RIGHT = 207,
    S3E_KEY_ABS_OK = 208,
    S3E_KEY_ABS_ASK = 209,
    S3E_KEY_ABS_BSK = 210,
};

enum {
    KEYBOARD_KEY_COUNT = 256,
    TOUCHPAD_COUNT = 2,
    AXIS_DEADZONE = 9000,
    XPERIA_AXIS_DEADZONE = 6000,
    TRIGGER_THRESHOLD = 16384,
};

enum {
    S3E_TOUCHPAD_RELEASED = 0,
    S3E_TOUCHPAD_PRESSED = 1,
};

struct key_map {
    const uint32_t *keys;
    size_t key_count;
};

struct sdl_input_api {
    int (*InitSubSystem)(uint32_t flags);
    void (*QuitSubSystem)(uint32_t flags);
    int (*GameControllerAddMapping)(const char *mapping);
    int (*NumJoysticks)(void);
    int (*IsGameController)(int joystick_index);
    void *(*GameControllerOpen)(int joystick_index);
    void (*GameControllerClose)(void *gamecontroller);
    void *(*GameControllerGetJoystick)(void *gamecontroller);
    int (*JoystickNumHats)(void *joystick);
    uint8_t (*JoystickGetHat)(void *joystick, int hat);
    void (*GameControllerUpdate)(void);
    int16_t (*GameControllerGetAxis)(void *gamecontroller, int axis);
    uint8_t (*GameControllerGetButton)(void *gamecontroller, int button);
    const char *(*GetError)(void);
};

static const uint32_t KEY_ACTION[] = {XPERIA_KEY_ACTION_SPRINT};
static const uint32_t KEY_RELOAD[] = {XPERIA_KEY_RELOAD};
static const uint32_t KEY_MELEE[] = {XPERIA_KEY_MELEE};
static const uint32_t KEY_GRENADE[] = {XPERIA_KEY_THROW_GRENADE};
static const uint32_t KEY_AIM[] = {XPERIA_KEY_AIM};
static const uint32_t KEY_SHOOT[] = {XPERIA_KEY_SHOOT};
static const uint32_t KEY_TACTICAL[] = {XPERIA_KEY_TACTICAL_GRENADE};
static const uint32_t KEY_CROUCH[] = {XPERIA_KEY_CROUCH_PRONE};
static const uint32_t KEY_ALT_FIRE[] = {XPERIA_KEY_ALTERNATE_FIRE};
static const uint32_t KEY_CHANGE_WEAPON[] = {XPERIA_KEY_CHANGE_WEAPON};
static const uint32_t KEY_START[] = {XPERIA_KEY_PAUSE};

#define KEY_MAP(keys) {keys, ARRAY_SIZE(keys)}

static const struct key_map KEYMAP_ACTION = KEY_MAP(KEY_ACTION);
static const struct key_map KEYMAP_RELOAD = KEY_MAP(KEY_RELOAD);
static const struct key_map KEYMAP_MELEE = KEY_MAP(KEY_MELEE);
static const struct key_map KEYMAP_GRENADE = KEY_MAP(KEY_GRENADE);
static const struct key_map KEYMAP_AIM = KEY_MAP(KEY_AIM);
static const struct key_map KEYMAP_SHOOT = KEY_MAP(KEY_SHOOT);
static const struct key_map KEYMAP_TACTICAL = KEY_MAP(KEY_TACTICAL);
static const struct key_map KEYMAP_CROUCH = KEY_MAP(KEY_CROUCH);
static const struct key_map KEYMAP_ALT_FIRE = KEY_MAP(KEY_ALT_FIRE);
static const struct key_map KEYMAP_CHANGE_WEAPON = KEY_MAP(KEY_CHANGE_WEAPON);
static const struct key_map KEYMAP_START = KEY_MAP(KEY_START);

#undef KEY_MAP

static void *g_sdl2;
static struct sdl_input_api g_sdl;
static void *g_controller;
static void *g_joystick;
static int g_sdl_tried;
static int g_input_pumping;
static int g_keyboard_update_active;
static int g_prev_select;
static int g_prev_a;
static uint64_t g_input_last_ms;
static uint8_t g_hat_mask;

static uint8_t g_keyboard_state[KEYBOARD_KEY_COUNT];

static int g_touchpad_active[TOUCHPAD_COUNT];
static int32_t g_touchpad_x[TOUCHPAD_COUNT];
static int32_t g_touchpad_y[TOUCHPAD_COUNT];
static uint8_t g_touchpad_state[TOUCHPAD_COUNT];

static int sdl_load_symbol(void **slot, const char *name) {
    *slot = dlsym(g_sdl2, name);
    return *slot != NULL;
}

static void sdl_load_optional_symbol(void **slot, const char *name) {
    *slot = dlsym(g_sdl2, name);
}

static void input_open(void) {
    if (g_sdl_tried) {
        return;
    }
    g_sdl_tried = 1;

    const char *names[] = {"libSDL2-2.0.so.0", "libSDL2.so", NULL};
    g_sdl2 = open_first(names);
    if (!g_sdl2) {
        return;
    }

    int ok = 1;
    ok &= sdl_load_symbol((void **)&g_sdl.InitSubSystem, "SDL_InitSubSystem");
    ok &= sdl_load_symbol((void **)&g_sdl.QuitSubSystem, "SDL_QuitSubSystem");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerAddMapping, "SDL_GameControllerAddMapping");
    ok &= sdl_load_symbol((void **)&g_sdl.NumJoysticks, "SDL_NumJoysticks");
    ok &= sdl_load_symbol((void **)&g_sdl.IsGameController, "SDL_IsGameController");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerOpen, "SDL_GameControllerOpen");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerClose, "SDL_GameControllerClose");
    sdl_load_optional_symbol((void **)&g_sdl.GameControllerGetJoystick,
                             "SDL_GameControllerGetJoystick");
    sdl_load_optional_symbol((void **)&g_sdl.JoystickNumHats, "SDL_JoystickNumHats");
    sdl_load_optional_symbol((void **)&g_sdl.JoystickGetHat, "SDL_JoystickGetHat");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerUpdate, "SDL_GameControllerUpdate");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerGetAxis, "SDL_GameControllerGetAxis");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerGetButton, "SDL_GameControllerGetButton");
    sdl_load_optional_symbol((void **)&g_sdl.GetError, "SDL_GetError");
    if (!ok || g_sdl.InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        if (!ok) {
            fprintf(stderr, "[input] SDL2 controller symbols unavailable\n");
        } else if (g_sdl.GetError) {
            const char *error = g_sdl.GetError();
            fprintf(stderr, "[input] SDL_InitSubSystem failed: %s\n", error ? error : "unknown");
        }
        return;
    }

    const char *mapping = getenv("SDL_GAMECONTROLLERCONFIG");
    if (mapping && mapping[0]) {
        g_sdl.GameControllerAddMapping(mapping);
    }

    int count = g_sdl.NumJoysticks();
    int selected_index = -1;

    for (int i = 0; i < count; ++i) {
        if (g_sdl.IsGameController(i)) {
            selected_index = i;
            break;
        }
    }

    if (selected_index >= 0) {
        g_controller = g_sdl.GameControllerOpen(selected_index);
        if (g_controller) {
            g_joystick = g_sdl.GameControllerGetJoystick
                             ? g_sdl.GameControllerGetJoystick(g_controller)
                             : NULL;
        }
    }
}

static uint8_t input_current_hat_mask(void) {
    uint8_t mask = 0;
    if (!g_joystick || !g_sdl.JoystickNumHats || !g_sdl.JoystickGetHat) {
        return 0;
    }

    int count = g_sdl.JoystickNumHats(g_joystick);
    for (int i = 0; i < count; ++i) {
        mask |= g_sdl.JoystickGetHat(g_joystick, i);
    }
    return mask;
}

static int input_button(int button) {
    if (!g_controller || !g_sdl.GameControllerGetButton || button < 0) {
        return 0;
    }
    return g_sdl.GameControllerGetButton(g_controller, button) != 0;
}

static int32_t input_axis_raw(int axis) {
    if (!g_controller || !g_sdl.GameControllerGetAxis) {
        return 0;
    }
    return g_sdl.GameControllerGetAxis(g_controller, axis);
}

static int input_trigger(int axis) {
    return input_axis_raw(axis) > TRIGGER_THRESHOLD;
}

static int input_hat(uint8_t mask) {
    return (g_hat_mask & mask) != 0;
}

static int input_dpad_up(void) {
    return input_button(SDL_BUTTON_DPAD_UP) || input_hat(SDL_HAT_UP);
}

static int input_dpad_down(void) {
    return input_button(SDL_BUTTON_DPAD_DOWN) || input_hat(SDL_HAT_DOWN);
}

static int input_dpad_left(void) {
    return input_button(SDL_BUTTON_DPAD_LEFT) || input_hat(SDL_HAT_LEFT);
}

static int input_dpad_right(void) {
    return input_button(SDL_BUTTON_DPAD_RIGHT) || input_hat(SDL_HAT_RIGHT);
}

static int32_t input_axis_deadzone(int axis, int32_t deadzone) {
    int32_t value = input_axis_raw(axis);
    return value < -deadzone || value > deadzone ? value : 0;
}

static int32_t input_axis(int axis) {
    return input_axis_deadzone(axis, AXIS_DEADZONE);
}

static int32_t input_xperia_axis(int axis) {
    return input_axis_deadzone(axis, XPERIA_AXIS_DEADZONE);
}

static int32_t window_width(void) {
    return g_surface.width > 0 ? (int32_t)g_surface.width : 640;
}

static int32_t window_height(void) {
    return g_surface.height > 0 ? (int32_t)g_surface.height : 480;
}

static int32_t clamp_value(int32_t value, int32_t upper_exclusive) {
    if (value < 0) {
        return 0;
    }
    if (value >= upper_exclusive) {
        return upper_exclusive - 1;
    }
    return value;
}

static int32_t clamp_pointer_x(int32_t x) {
    return clamp_value(x, window_width());
}

static int32_t clamp_pointer_y(int32_t y) {
    return clamp_value(y, window_height());
}

static void pointer_dispatch(uint32_t id, void *event) {
    if (id >= ARRAY_SIZE(g_pointer_callbacks)) {
        return;
    }
    struct callback_slot *slot = &g_pointer_callbacks[id];
    if (!slot->callback) {
        return;
    }
    ((s3e_callback_fn)(uintptr_t)slot->callback)(event, slot->user_data);
}

static void touchpad_dispatch(uint32_t id, void *event) {
    if (id >= ARRAY_SIZE(g_touchpad_callbacks)) {
        return;
    }
    struct callback_slot *slot = &g_touchpad_callbacks[id];
    if (!slot->callback) {
        return;
    }
    ((s3e_callback_fn)(uintptr_t)slot->callback)(event, slot->user_data);
}

static void pointer_set_down(int down) {
    if (down) {
        g_pointer_states[0] = g_pointer_down ? POINTER_STATE_DOWN : POINTER_STATE_PRESSED;
        g_pointer_down = 1;
    } else {
        g_pointer_states[0] = g_pointer_down ? POINTER_STATE_RELEASED : POINTER_STATE_UP;
        g_pointer_down = 0;
    }
}

static void pointer_clear_transitions(void) {
    for (size_t i = 0; i < sizeof(g_pointer_states); ++i) {
        if (g_pointer_states[i] == POINTER_STATE_PRESSED) {
            g_pointer_states[i] = POINTER_STATE_DOWN;
        } else if (g_pointer_states[i] == POINTER_STATE_RELEASED) {
            g_pointer_states[i] = POINTER_STATE_UP;
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(g_touchpad_state); ++i) {
        if (g_touchpad_state[i] == POINTER_STATE_PRESSED) {
            g_touchpad_state[i] = POINTER_STATE_DOWN;
        } else if (g_touchpad_state[i] == POINTER_STATE_RELEASED) {
            g_touchpad_state[i] = POINTER_STATE_UP;
        }
    }
}

static void pointer_dispatch_button(uint32_t button, int32_t pressed) {
    struct s3e_pointer_button_event event = {
        .button = (int32_t)button,
        .pressed = pressed,
        .x = g_pointer_x,
        .y = g_pointer_y,
    };
    struct s3e_pointer_touch_event touch_event = {
        .touch_id = (int32_t)button,
        .pressed = pressed,
        .x = g_pointer_x,
        .y = g_pointer_y,
    };
    pointer_dispatch(0, &event);
    pointer_dispatch(2, &touch_event);
}

static void pointer_dispatch_motion(void) {
    struct s3e_pointer_motion_event event = {
        .x = g_pointer_x,
        .y = g_pointer_y,
    };
    struct s3e_pointer_touch_motion_event touch_event = {
        .touch_id = 0,
        .x = g_pointer_x,
        .y = g_pointer_y,
    };
    pointer_dispatch(1, &event);
    pointer_dispatch(3, &touch_event);
}

static void input_release_pointer(void) {
    if (g_pointer_down) {
        pointer_set_down(0);
        pointer_dispatch_button(0, 0);
    }
}

static void keyboard_dispatch_event(uint32_t key, int32_t pressed) {
    struct s3e_keyboard_event event = {
        .key = (int32_t)key,
        .pressed = pressed ? 1 : 0,
    };

    for (size_t i = 0; i < ARRAY_SIZE(g_keyboard_callbacks); ++i) {
        struct keyboard_callback_slot *slot = &g_keyboard_callbacks[i];
        if (slot->callback) {
            ((s3e_callback_fn)(uintptr_t)slot->callback)(&event, slot->user_data);
        }
    }
}

static void keyboard_set_key(uint32_t key, int down, int dispatch_callback) {
    if (key >= KEYBOARD_KEY_COUNT) {
        return;
    }

    int was_down = (g_keyboard_state[key] & KEY_STATE_DOWN) != 0;
    if (was_down == down) {
        return;
    }

    if (down) {
        g_keyboard_state[key] &= (uint8_t)~KEY_STATE_RELEASED;
        g_keyboard_state[key] |= KEY_STATE_DOWN | KEY_STATE_PRESSED;
    } else {
        g_keyboard_state[key] &= (uint8_t)~KEY_STATE_DOWN;
        g_keyboard_state[key] &= (uint8_t)~KEY_STATE_PRESSED;
        g_keyboard_state[key] |= KEY_STATE_RELEASED;
    }
    if (dispatch_callback) {
        keyboard_dispatch_event(key, down);
    }
}

static void keyboard_set_keys(const uint32_t *keys, size_t count, int down, int dispatch_callback) {
    for (size_t i = 0; i < count; ++i) {
        keyboard_set_key(keys[i], down, dispatch_callback);
    }
}

static void keyboard_clear_transitions(void) {
    for (size_t key = 0; key < ARRAY_SIZE(g_keyboard_state); ++key) {
        g_keyboard_state[key] &= (uint8_t)~(KEY_STATE_PRESSED | KEY_STATE_RELEASED);
    }
}

static uint32_t keyboard_abs_target(uint32_t key) {
    switch (key) {
    case S3E_KEY_ABS_GAME_A:
        return BINDING_KEY_ACTION_SPRINT;
    case S3E_KEY_ABS_GAME_B:
        return BINDING_KEY_RELOAD;
    case S3E_KEY_ABS_GAME_C:
        return BINDING_KEY_THROW_GRENADE;
    case S3E_KEY_ABS_GAME_D:
        return BINDING_KEY_MELEE;
    case S3E_KEY_ABS_UP:
        return BINDING_KEY_TACTICAL_GRENADE;
    case S3E_KEY_ABS_DOWN:
        return BINDING_KEY_CROUCH_PRONE;
    case S3E_KEY_ABS_LEFT:
        return BINDING_KEY_ALTERNATE_FIRE;
    case S3E_KEY_ABS_RIGHT:
        return BINDING_KEY_CHANGE_WEAPON;
    case S3E_KEY_ABS_OK:
        return BINDING_KEY_ACTION_SPRINT;
    case S3E_KEY_ABS_ASK:
        return XPERIA_KEY_PAUSE;
    case S3E_KEY_ABS_BSK:
        return BINDING_KEY_RELOAD;
    default:
        return key;
    }
}

static void game_action_apply(int physical_down, const struct key_map *keys) {
    keyboard_set_keys(keys->keys, keys->key_count, physical_down, 1);
}

static void keyboard_release_all(void) {
    for (uint32_t key = 0; key < KEYBOARD_KEY_COUNT; ++key) {
        if (g_keyboard_state[key] & KEY_STATE_DOWN) {
            keyboard_set_key(key, 0, 1);
        }
    }
}

static void touchpad_dispatch_button(uint32_t id, int32_t pressed, int32_t x, int32_t y) {
    struct s3e_touchpad_button_event event = {
        .id = (int32_t)id,
        .pressed = pressed,
        .x = x,
        .y = y,
    };
    touchpad_dispatch(0, &event);
}

static void touchpad_dispatch_motion(uint32_t id, int32_t x, int32_t y) {
    struct s3e_touchpad_motion_event event = {
        .id = (int32_t)id,
        .x = x,
        .y = y,
    };
    touchpad_dispatch(1, &event);
}

static void touchpad_release(uint32_t id) {
    if (id >= TOUCHPAD_COUNT || !g_touchpad_active[id]) {
        return;
    }
    g_touchpad_active[id] = 0;
    g_touchpad_state[id] = POINTER_STATE_RELEASED;
    touchpad_dispatch_button(id, S3E_TOUCHPAD_RELEASED, g_touchpad_x[id], g_touchpad_y[id]);
}

static void touchpad_release_all(void) {
    for (uint32_t id = 0; id < TOUCHPAD_COUNT; ++id) {
        touchpad_release(id);
    }
}

static int32_t touchpad_x_from_axis(int32_t center, int32_t radius, int32_t axis) {
    return clamp_value(center + (int32_t)((int64_t)axis * radius / 32767), XPERIA_TOUCHPAD_WIDTH);
}

static int32_t touchpad_y_from_axis(int32_t center, int32_t radius, int32_t axis) {
    return clamp_value(center + (int32_t)((int64_t)axis * radius / 32767), XPERIA_TOUCHPAD_HEIGHT);
}

static void touchpad_update_stick(uint32_t id, int32_t x_axis, int32_t y_axis, int32_t center_x,
                                  int32_t center_y, int32_t radius_x, int32_t radius_y) {
    if (id >= TOUCHPAD_COUNT) {
        return;
    }

    if (!x_axis && !y_axis) {
        touchpad_release(id);
        return;
    }

    int32_t x = touchpad_x_from_axis(center_x, radius_x, x_axis);
    int32_t y = touchpad_y_from_axis(center_y, radius_y, y_axis);

    if (!g_touchpad_active[id]) {
        g_touchpad_active[id] = 1;
        g_touchpad_x[id] = x;
        g_touchpad_y[id] = y;
        g_touchpad_state[id] = POINTER_STATE_PRESSED;
        touchpad_dispatch_motion(id, x, y);
        touchpad_dispatch_button(id, S3E_TOUCHPAD_PRESSED, x, y);
        return;
    }

    if (g_touchpad_x[id] != x || g_touchpad_y[id] != y) {
        g_touchpad_x[id] = x;
        g_touchpad_y[id] = y;
        touchpad_dispatch_motion(id, g_touchpad_x[id], g_touchpad_y[id]);
    }
}

static void input_update_cursor(uint64_t dt) {
    int a = input_button(SDL_BUTTON_A);
    if (a != g_prev_a) {
        pointer_set_down(a);
        pointer_dispatch_button(0, a ? 1 : 0);
    }
    g_prev_a = a;

    int32_t x_axis = input_axis(SDL_AXIS_LEFTX);
    int32_t y_axis = input_axis(SDL_AXIS_LEFTY);
    if (input_dpad_left()) {
        x_axis = -32767;
    } else if (input_dpad_right()) {
        x_axis = 32767;
    }
    if (input_dpad_up()) {
        y_axis = -32767;
    } else if (input_dpad_down()) {
        y_axis = 32767;
    }
    if (!x_axis && !y_axis) {
        return;
    }

    int32_t old_x = g_pointer_x;
    int32_t old_y = g_pointer_y;
    const int32_t x_speed = (int32_t)((int64_t)900 * window_width() / 640);
    const int32_t y_speed = (int32_t)((int64_t)900 * window_height() / 480);
    g_pointer_x = clamp_pointer_x(
        g_pointer_x + (int32_t)((int64_t)x_axis * (int64_t)dt * x_speed / 32767 / 1000));
    g_pointer_y = clamp_pointer_y(
        g_pointer_y + (int32_t)((int64_t)y_axis * (int64_t)dt * y_speed / 32767 / 1000));
    if (g_pointer_x != old_x || g_pointer_y != old_y) {
        pointer_dispatch_motion();
    }
}

static void input_update_game_keys(void) {
    g_prev_a = input_button(SDL_BUTTON_A);

    game_action_apply(input_button(SDL_BUTTON_A), &KEYMAP_ACTION);
    game_action_apply(input_button(SDL_BUTTON_B), &KEYMAP_RELOAD);
    game_action_apply(input_button(SDL_BUTTON_X), &KEYMAP_MELEE);
    game_action_apply(input_button(SDL_BUTTON_Y), &KEYMAP_GRENADE);
    game_action_apply(input_button(SDL_BUTTON_LEFTSHOULDER) || input_trigger(SDL_AXIS_TRIGGERLEFT),
                      &KEYMAP_AIM);
    game_action_apply(input_button(SDL_BUTTON_RIGHTSHOULDER) ||
                          input_trigger(SDL_AXIS_TRIGGERRIGHT),
                      &KEYMAP_SHOOT);
    game_action_apply(input_button(SDL_BUTTON_START), &KEYMAP_START);
    game_action_apply(input_dpad_up(), &KEYMAP_TACTICAL);
    game_action_apply(input_dpad_down(), &KEYMAP_CROUCH);
    game_action_apply(input_dpad_left(), &KEYMAP_ALT_FIRE);
    game_action_apply(input_dpad_right(), &KEYMAP_CHANGE_WEAPON);
}

static void input_update_game_touchpads(void) {
    int32_t width = XPERIA_TOUCHPAD_WIDTH;
    int32_t height = XPERIA_TOUCHPAD_HEIGHT;
    int32_t center_y = height / 2;
    int32_t look_radius = width / 8;
    touchpad_update_stick(0, input_xperia_axis(SDL_AXIS_LEFTX), input_xperia_axis(SDL_AXIS_LEFTY),
                          width / 5, center_y, width / 5, height / 2);
    touchpad_update_stick(1, input_xperia_axis(SDL_AXIS_RIGHTX), input_xperia_axis(SDL_AXIS_RIGHTY),
                          (width * 4) / 5, center_y, look_radius, look_radius);
}

void input_pump(void) {
    if (g_input_pumping) {
        return;
    }
    g_input_pumping = 1;

    input_open();
    if (!g_controller) {
        goto out;
    }
    g_sdl.GameControllerUpdate();
    g_hat_mask = input_current_hat_mask();

    uint64_t now = monotonic_ms();
    if (!g_input_last_ms) {
        g_input_last_ms = now;
    }
    uint64_t dt = now - g_input_last_ms;
    g_input_last_ms = now;
    if (dt > 50) {
        dt = 50;
    }

    int select = input_button(SDL_BUTTON_BACK);
    if (select && !g_prev_select) {
        if (g_cursor_active) {
            input_release_pointer();
        } else {
            touchpad_release_all();
            keyboard_release_all();
        }
        g_cursor_active = !g_cursor_active;
    }
    g_prev_select = select;

    if (g_cursor_active) {
        touchpad_release_all();
        if (g_keyboard_update_active) {
            keyboard_release_all();
        }
        input_update_cursor(dt);
    } else {
        input_release_pointer();
        input_update_game_touchpads();
        if (g_keyboard_update_active) {
            input_update_game_keys();
        }
    }

out:
    g_input_pumping = 0;
}

void input_shutdown(void) {
    input_release_pointer();
    touchpad_release_all();
    keyboard_release_all();
    if (g_controller && g_sdl.GameControllerClose) {
        g_sdl.GameControllerClose(g_controller);
        g_controller = NULL;
        g_joystick = NULL;
    }
    if (g_sdl2) {
        if (g_sdl.QuitSubSystem) {
            g_sdl.QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
        }
        dlclose(g_sdl2);
        g_sdl2 = NULL;
    }
}

int32_t s3eKeyboardRegister(uint32_t id, void *callback, void *user_data) {
    struct keyboard_callback_slot *free_slot = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(g_keyboard_callbacks); ++i) {
        struct keyboard_callback_slot *slot = &g_keyboard_callbacks[i];
        if (slot->callback == callback && slot->id == id) {
            slot->user_data = user_data;
            return 0;
        }
        if (!slot->callback && !free_slot) {
            free_slot = slot;
        }
    }
    if (free_slot) {
        free_slot->id = id;
        free_slot->callback = callback;
        free_slot->user_data = user_data;
    }
    return 0;
}

int32_t s3eKeyboardUnRegister(uint32_t id, void *callback) {
    for (size_t i = 0; i < ARRAY_SIZE(g_keyboard_callbacks); ++i) {
        struct keyboard_callback_slot *slot = &g_keyboard_callbacks[i];
        if (slot->callback && slot->id == id && (!callback || callback == slot->callback)) {
            slot->callback = NULL;
            slot->user_data = NULL;
        }
    }
    return 0;
}

int32_t s3eKeyboardUpdate(void) {
    keyboard_clear_transitions();
    g_keyboard_update_active = 1;
    input_pump();
    if (g_cursor_active || !g_controller) {
        keyboard_release_all();
    }
    g_keyboard_update_active = 0;
    dispatch_due_timers();
    return 0;
}

int32_t s3eKeyboardGetState(uint32_t key) {
    uint32_t target = keyboard_abs_target(key);
    return target < KEYBOARD_KEY_COUNT ? g_keyboard_state[target] : 0;
}

int32_t s3eKeyboardAnyKey(void) {
    for (size_t i = 0; i < ARRAY_SIZE(g_keyboard_state); ++i) {
        if (g_keyboard_state[i] & (KEY_STATE_DOWN | KEY_STATE_PRESSED)) {
            return 1;
        }
    }
    return 0;
}

int32_t s3eKeyboardGetInt(uint32_t key) {
    switch (key) {
    case 0:
    case 2:
        return 1;
    case 1:
    case 4:
    case 5:
    case 6:
        return 0;
    default:
        return -1;
    }
}

int32_t s3eKeyboardSetInt(uint32_t key, int32_t value) {
    (void)key;
    (void)value;
    return 0;
}

const char *s3eKeyboardGetDisplayName(uint32_t key) {
    switch (key) {
    case BINDING_KEY_SHOOT:
        return "BindingShoot";
    case BINDING_KEY_CHANGE_WEAPON:
        return "BindingChangeWeapon";
    case BINDING_KEY_CROUCH_PRONE:
        return "BindingCrouchProne";
    case BINDING_KEY_RELOAD:
        return "BindingReload";
    case BINDING_KEY_MELEE:
        return "BindingMelee";
    case BINDING_KEY_ALTERNATE_FIRE:
        return "BindingAlternateFire";
    case BINDING_KEY_AIM:
        return "BindingAim";
    case BINDING_KEY_THROW_GRENADE:
        return "BindingThrowGrenade";
    case BINDING_KEY_TACTICAL_GRENADE:
        return "BindingTacticalGrenade";
    case BINDING_KEY_ACTION_SPRINT:
        return "BindingActionSprint";
    case BINDING_KEY_TOGGLE_FREE_MODE:
        return "BindingToggleFreeMode";
    case XPERIA_KEY_ALTERNATE_FIRE:
        return "XperiaAlternateFire";
    case XPERIA_KEY_TACTICAL_GRENADE:
        return "XperiaTacticalGrenade";
    case XPERIA_KEY_CHANGE_WEAPON:
        return "XperiaChangeWeapon";
    case XPERIA_KEY_CROUCH_PRONE:
        return "XperiaCrouchProne";
    case XPERIA_KEY_AIM:
        return "XperiaAim";
    case XPERIA_KEY_SHOOT:
        return "XperiaShoot";
    case XPERIA_KEY_ACTION_SPRINT:
        return "XperiaActionSprint";
    case XPERIA_KEY_MELEE:
        return "XperiaMelee";
    case XPERIA_KEY_THROW_GRENADE:
        return "XperiaThrowGrenade";
    case XPERIA_KEY_RELOAD:
        return "XperiaReload";
    case XPERIA_KEY_PAUSE:
        return "XperiaPause";
    case S3E_KEY_ABS_GAME_A:
        return "KeyAbsGameA";
    case S3E_KEY_ABS_GAME_B:
        return "KeyAbsGameB";
    case S3E_KEY_ABS_GAME_C:
        return "KeyAbsGameC";
    case S3E_KEY_ABS_GAME_D:
        return "KeyAbsGameD";
    case S3E_KEY_ABS_UP:
        return "KeyAbsUp";
    case S3E_KEY_ABS_DOWN:
        return "KeyAbsDown";
    case S3E_KEY_ABS_LEFT:
        return "KeyAbsLeft";
    case S3E_KEY_ABS_RIGHT:
        return "KeyAbsRight";
    case S3E_KEY_ABS_OK:
        return "KeyAbsOk";
    case S3E_KEY_ABS_ASK:
        return "KeyAbsASK";
    case S3E_KEY_ABS_BSK:
        return "KeyAbsBSK";
    default:
        return "";
    }
}

void s3eKeyboardClearState(void) {
    memset(g_keyboard_state, 0, sizeof(g_keyboard_state));
}

int32_t s3ePointerRegister(uint32_t id, void *callback, void *user_data) {
    if (id < ARRAY_SIZE(g_pointer_callbacks)) {
        g_pointer_callbacks[id].callback = callback;
        g_pointer_callbacks[id].user_data = user_data;
    }
    return 0;
}

int32_t s3ePointerUnRegister(uint32_t id, void *callback) {
    if (id < ARRAY_SIZE(g_pointer_callbacks) &&
        (!callback || callback == g_pointer_callbacks[id].callback)) {
        g_pointer_callbacks[id].callback = NULL;
        g_pointer_callbacks[id].user_data = NULL;
    }
    return 0;
}

int32_t s3ePointerUpdate(void) {
    pointer_clear_transitions();
    input_pump();
    dispatch_due_timers();
    return 0;
}

int32_t s3ePointerGetInt(uint32_t key) {
    input_pump();
    switch (key) {
    case 0:
        return 1;
    case 1:
        return g_pointer_x;
    case 2:
        return g_pointer_y;
    default:
        return 0;
    }
}

int32_t s3ePointerSetInt(uint32_t key, int32_t value) {
    (void)key;
    (void)value;
    return 0;
}

int32_t s3ePointerGetState(uint32_t button) {
    input_pump();
    return button < sizeof(g_pointer_states) ? g_pointer_states[button] : 0;
}

int32_t s3ePointerGetX(void) {
    input_pump();
    return g_pointer_x;
}

int32_t s3ePointerGetY(void) {
    input_pump();
    return g_pointer_y;
}

int32_t s3ePointerGetTouchState(uint32_t touch_id) {
    input_pump();
    if (g_cursor_active) {
        return touch_id == 0 ? g_pointer_states[0] : 0;
    }
    return 0;
}

int32_t s3ePointerGetTouchX(uint32_t touch_id) {
    input_pump();
    if (g_cursor_active) {
        return touch_id == 0 ? g_pointer_x : 0;
    }
    return 0;
}

int32_t s3ePointerGetTouchY(uint32_t touch_id) {
    input_pump();
    if (g_cursor_active) {
        return touch_id == 0 ? g_pointer_y : 0;
    }
    return 0;
}

int32_t s3ePointerGetPressure(uint32_t button) {
    input_pump();
    return button == 0 && g_pointer_down ? 1 : 0;
}

int32_t s3ePointerGetTouchPressure(uint32_t touch_id) {
    input_pump();
    if (g_cursor_active) {
        return touch_id == 0 && g_pointer_down ? 1 : 0;
    }
    return 0;
}

int32_t s3ePointerGetError(void) {
    return 0;
}

const char *s3ePointerGetErrorString(void) {
    return "S3E_POINTER_ERR_NONE";
}
