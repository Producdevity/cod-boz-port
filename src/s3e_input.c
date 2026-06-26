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
    POINTER_STATE_UP = 0,
    POINTER_STATE_DOWN = 1,
    POINTER_STATE_PRESSED = 2,
    POINTER_STATE_RELEASED = 4,
};

enum {
    KEY_STATE_UP = 0,
    KEY_STATE_DOWN = 1,
    KEY_STATE_PRESSED = 2,
    KEY_STATE_RELEASED = 4,
};

enum {
    S3E_KEY_ENTER = 4,

    XPERIA_KEY_ALTERNATE_FIRE = 9,
    XPERIA_KEY_TACTICAL_GRENADE = 10,
    XPERIA_KEY_CHANGE_WEAPON = 11,
    XPERIA_KEY_CROUCH_PRONE = 12,
    XPERIA_KEY_AIM = 74,
    XPERIA_KEY_SHOOT = 75,
    XPERIA_KEY_ACTION_SPRINT = 78,
    XPERIA_KEY_MELEE = 89,
    XPERIA_KEY_THROW_GRENADE = 90,
    XPERIA_KEY_RELOAD_CHANGE_WEAPON = 126,
    XPERIA_KEY_SELECT = 127,

    S3E_KEY_DPAD_LEFT_STATE = 23,
    S3E_KEY_DPAD_RIGHT_STATE = 26,
    S3E_KEY_DPAD_DOWN_STATE = 41,
    S3E_KEY_DPAD_UP_STATE = 45,
    S3E_KEY_ACTION_CALLBACK = 96,
    S3E_KEY_RELOAD_CALLBACK = 97,
    S3E_KEY_GAMEPAD_Y = 100,
    S3E_KEY_SHOOT_CALLBACK = 103,
    S3E_KEY_START_CALLBACK = 108,

    XPERIA_COMPANION_ACTION = 0xd0,
    XPERIA_COMPANION_RELOAD = 0xd2,
    XPERIA_COMPANION_RELOAD_ALT = 0xc9,
    XPERIA_COMPANION_GRENADE = 0xca,
    XPERIA_COMPANION_DPAD_UP = 0xcc,
    XPERIA_COMPANION_DPAD_DOWN = 0xcd,
    XPERIA_COMPANION_DPAD_LEFT = 0xce,
    XPERIA_COMPANION_DPAD_RIGHT = 0xcf,
    XPERIA_COMPANION_ACTION_ALT = 200,
};

enum {
    GAME_ACTION_UP,
    GAME_ACTION_DOWN,
    GAME_ACTION_LEFT,
    GAME_ACTION_RIGHT,
    GAME_ACTION_ACTION,
    GAME_ACTION_RELOAD,
    GAME_ACTION_MELEE,
    GAME_ACTION_GRENADE,
    GAME_ACTION_AIM,
    GAME_ACTION_SHOOT,
    GAME_ACTION_START,
    GAME_ACTION_COUNT,
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
    const uint32_t *state_keys;
    size_t state_count;
    const uint32_t *callback_keys;
    size_t callback_count;
};

struct sdl_input_api {
    int (*InitSubSystem)(uint32_t flags);
    void (*QuitSubSystem)(uint32_t flags);
    int (*GameControllerAddMapping)(const char *mapping);
    int (*NumJoysticks)(void);
    int (*IsGameController)(int joystick_index);
    void *(*GameControllerOpen)(int joystick_index);
    void (*GameControllerClose)(void *gamecontroller);
    void (*GameControllerUpdate)(void);
    int16_t (*GameControllerGetAxis)(void *gamecontroller, int axis);
    uint8_t (*GameControllerGetButton)(void *gamecontroller, int button);
};

static const uint32_t STATE_DPAD_UP[] = {
    XPERIA_KEY_TACTICAL_GRENADE,
    XPERIA_COMPANION_DPAD_UP,
    S3E_KEY_DPAD_UP_STATE,
};
static const uint32_t CALLBACK_DPAD_UP[] = {
    XPERIA_KEY_TACTICAL_GRENADE,
};
static const uint32_t STATE_DPAD_DOWN[] = {
    XPERIA_KEY_CROUCH_PRONE,
    XPERIA_COMPANION_DPAD_DOWN,
    S3E_KEY_DPAD_DOWN_STATE,
};
static const uint32_t CALLBACK_DPAD_DOWN[] = {
    XPERIA_KEY_CROUCH_PRONE,
};
static const uint32_t STATE_DPAD_LEFT[] = {
    XPERIA_KEY_ALTERNATE_FIRE,
    XPERIA_COMPANION_DPAD_LEFT,
    S3E_KEY_DPAD_LEFT_STATE,
};
static const uint32_t CALLBACK_DPAD_LEFT[] = {
    XPERIA_KEY_ALTERNATE_FIRE,
};
static const uint32_t STATE_DPAD_RIGHT[] = {
    XPERIA_KEY_CHANGE_WEAPON,
    XPERIA_COMPANION_DPAD_RIGHT,
};
static const uint32_t CALLBACK_DPAD_RIGHT[] = {
    XPERIA_KEY_CHANGE_WEAPON,
};
static const uint32_t STATE_ACTION[] = {
    XPERIA_KEY_ACTION_SPRINT,
    XPERIA_COMPANION_ACTION,
    XPERIA_COMPANION_ACTION_ALT,
};
static const uint32_t CALLBACK_ACTION[] = {
    XPERIA_KEY_ACTION_SPRINT,
    S3E_KEY_ACTION_CALLBACK,
};
static const uint32_t STATE_RELOAD[] = {
    XPERIA_KEY_RELOAD_CHANGE_WEAPON,
    XPERIA_COMPANION_RELOAD,
    XPERIA_COMPANION_RELOAD_ALT,
};
static const uint32_t CALLBACK_RELOAD[] = {
    XPERIA_KEY_RELOAD_CHANGE_WEAPON,
    S3E_KEY_RELOAD_CALLBACK,
};
static const uint32_t STATE_MELEE[] = {
    XPERIA_KEY_MELEE,
};
static const uint32_t CALLBACK_MELEE[] = {
    XPERIA_KEY_MELEE,
};
static const uint32_t STATE_GRENADE[] = {
    XPERIA_KEY_THROW_GRENADE,
    XPERIA_COMPANION_GRENADE,
};
static const uint32_t CALLBACK_GRENADE[] = {
    XPERIA_KEY_THROW_GRENADE,
    S3E_KEY_GAMEPAD_Y,
};
static const uint32_t STATE_AIM[] = {
    XPERIA_KEY_AIM,
};
static const uint32_t CALLBACK_AIM[] = {
    XPERIA_KEY_AIM,
};
static const uint32_t STATE_SHOOT[] = {
    XPERIA_KEY_SHOOT,
};
static const uint32_t CALLBACK_SHOOT[] = {
    XPERIA_KEY_SHOOT,
    S3E_KEY_SHOOT_CALLBACK,
};
static const uint32_t STATE_START[] = {
    S3E_KEY_ENTER,
};
static const uint32_t CALLBACK_START[] = {
    S3E_KEY_ENTER,
    S3E_KEY_START_CALLBACK,
};

#define KEY_MAP(state, callback) {state, ARRAY_SIZE(state), callback, ARRAY_SIZE(callback)}

static const struct key_map KEYMAP_DPAD_UP = KEY_MAP(STATE_DPAD_UP, CALLBACK_DPAD_UP);
static const struct key_map KEYMAP_DPAD_DOWN = KEY_MAP(STATE_DPAD_DOWN, CALLBACK_DPAD_DOWN);
static const struct key_map KEYMAP_DPAD_LEFT = KEY_MAP(STATE_DPAD_LEFT, CALLBACK_DPAD_LEFT);
static const struct key_map KEYMAP_DPAD_RIGHT = KEY_MAP(STATE_DPAD_RIGHT, CALLBACK_DPAD_RIGHT);
static const struct key_map KEYMAP_ACTION = KEY_MAP(STATE_ACTION, CALLBACK_ACTION);
static const struct key_map KEYMAP_RELOAD = KEY_MAP(STATE_RELOAD, CALLBACK_RELOAD);
static const struct key_map KEYMAP_MELEE = KEY_MAP(STATE_MELEE, CALLBACK_MELEE);
static const struct key_map KEYMAP_GRENADE = KEY_MAP(STATE_GRENADE, CALLBACK_GRENADE);
static const struct key_map KEYMAP_AIM = KEY_MAP(STATE_AIM, CALLBACK_AIM);
static const struct key_map KEYMAP_SHOOT = KEY_MAP(STATE_SHOOT, CALLBACK_SHOOT);
static const struct key_map KEYMAP_START = KEY_MAP(STATE_START, CALLBACK_START);

#undef KEY_MAP

static void *g_sdl2;
static struct sdl_input_api g_sdl;
static void *g_controller;
static int g_sdl_tried;
static int g_input_pumping;
static int g_prev_select;
static int g_prev_a;
static uint64_t g_input_last_ms;

static uint8_t g_keyboard_state[KEYBOARD_KEY_COUNT];
static uint8_t g_keyboard_pending[KEYBOARD_KEY_COUNT];
static int g_keyboard_updating;

static int g_touchpad_active[TOUCHPAD_COUNT];
static int32_t g_touchpad_x[TOUCHPAD_COUNT];
static int32_t g_touchpad_y[TOUCHPAD_COUNT];
static uint8_t g_touchpad_state[TOUCHPAD_COUNT];

static int sdl_load_symbol(void **slot, const char *name) {
    *slot = dlsym(g_sdl2, name);
    return *slot != NULL;
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
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerUpdate, "SDL_GameControllerUpdate");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerGetAxis, "SDL_GameControllerGetAxis");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerGetButton, "SDL_GameControllerGetButton");
    if (!ok || g_sdl.InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        return;
    }

    const char *mapping = getenv("SDL_GAMECONTROLLERCONFIG");
    if (mapping && mapping[0]) {
        g_sdl.GameControllerAddMapping(mapping);
    }

    int count = g_sdl.NumJoysticks();
    for (int i = 0; i < count; ++i) {
        if (!g_sdl.IsGameController(i)) {
            continue;
        }
        g_controller = g_sdl.GameControllerOpen(i);
        if (g_controller) {
            break;
        }
    }
}

static int input_button(int button) {
    if (!g_controller || !g_sdl.GameControllerGetButton) {
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
    return g_native_window.width > 0 ? (int32_t)g_native_window.width : 640;
}

static int32_t window_height(void) {
    return g_native_window.height > 0 ? (int32_t)g_native_window.height : 480;
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
    typedef int32_t (*callback_fn)(void *system_data, void *user_data);
    ((callback_fn)(uintptr_t)slot->callback)(event, slot->user_data);
}

static void touchpad_dispatch(uint32_t id, void *event) {
    if (id >= ARRAY_SIZE(g_touchpad_callbacks)) {
        return;
    }
    struct callback_slot *slot = &g_touchpad_callbacks[id];
    if (!slot->callback) {
        return;
    }
    typedef int32_t (*callback_fn)(void *system_data, void *user_data);
    ((callback_fn)(uintptr_t)slot->callback)(event, slot->user_data);
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

    typedef int32_t (*callback_fn)(void *system_data, void *user_data);
    for (size_t i = 0; i < ARRAY_SIZE(g_keyboard_callbacks); ++i) {
        struct keyboard_callback_slot *slot = &g_keyboard_callbacks[i];
        if (slot->callback) {
            ((callback_fn)(uintptr_t)slot->callback)(&event, slot->user_data);
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
        g_keyboard_state[key] |= KEY_STATE_RELEASED;
    }

    if (!g_keyboard_updating) {
        g_keyboard_pending[key] |= down ? KEY_STATE_PRESSED : KEY_STATE_RELEASED;
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
        uint8_t down = g_keyboard_state[key] & KEY_STATE_DOWN;
        uint8_t pending = g_keyboard_pending[key] & (KEY_STATE_PRESSED | KEY_STATE_RELEASED);
        g_keyboard_state[key] = down | pending;
        g_keyboard_pending[key] = 0;
    }
}

static void game_action_apply(uint32_t action, int physical_down, const struct key_map *keys,
                              uint64_t now) {
    (void)action;
    (void)now;
    keyboard_set_keys(keys->callback_keys, keys->callback_count, physical_down, 1);
    keyboard_set_keys(keys->state_keys, keys->state_count, physical_down, 0);
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
    return clamp_value(center + (int32_t)((int64_t)axis * radius / 32767), window_width());
}

static int32_t touchpad_y_from_axis(int32_t center, int32_t radius, int32_t axis) {
    return clamp_value(center + (int32_t)((int64_t)axis * radius / 32767), window_height());
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
    if (input_button(SDL_BUTTON_DPAD_LEFT)) {
        x_axis = -32767;
    } else if (input_button(SDL_BUTTON_DPAD_RIGHT)) {
        x_axis = 32767;
    }
    if (input_button(SDL_BUTTON_DPAD_UP)) {
        y_axis = -32767;
    } else if (input_button(SDL_BUTTON_DPAD_DOWN)) {
        y_axis = 32767;
    }
    if (!x_axis && !y_axis) {
        return;
    }

    int32_t old_x = g_pointer_x;
    int32_t old_y = g_pointer_y;
    const int32_t speed = 900;
    g_pointer_x = clamp_pointer_x(g_pointer_x +
                                  (int32_t)((int64_t)x_axis * (int64_t)dt * speed / 32767 / 1000));
    g_pointer_y = clamp_pointer_y(g_pointer_y +
                                  (int32_t)((int64_t)y_axis * (int64_t)dt * speed / 32767 / 1000));
    if (g_pointer_x != old_x || g_pointer_y != old_y) {
        pointer_dispatch_motion();
    }
}

static void input_update_game_keys(uint64_t now) {
    g_prev_a = input_button(SDL_BUTTON_A);

    game_action_apply(GAME_ACTION_UP, input_button(SDL_BUTTON_DPAD_UP), &KEYMAP_DPAD_UP, now);
    game_action_apply(GAME_ACTION_DOWN, input_button(SDL_BUTTON_DPAD_DOWN), &KEYMAP_DPAD_DOWN, now);
    game_action_apply(GAME_ACTION_LEFT, input_button(SDL_BUTTON_DPAD_LEFT), &KEYMAP_DPAD_LEFT, now);
    game_action_apply(GAME_ACTION_RIGHT, input_button(SDL_BUTTON_DPAD_RIGHT), &KEYMAP_DPAD_RIGHT,
                      now);
    game_action_apply(GAME_ACTION_ACTION, input_button(SDL_BUTTON_A), &KEYMAP_ACTION, now);
    game_action_apply(GAME_ACTION_RELOAD, input_button(SDL_BUTTON_B), &KEYMAP_RELOAD, now);
    game_action_apply(GAME_ACTION_MELEE, input_button(SDL_BUTTON_X), &KEYMAP_MELEE, now);
    game_action_apply(GAME_ACTION_GRENADE, input_button(SDL_BUTTON_Y), &KEYMAP_GRENADE, now);
    game_action_apply(GAME_ACTION_AIM,
                      input_button(SDL_BUTTON_LEFTSHOULDER) ||
                          input_axis_raw(SDL_AXIS_TRIGGERLEFT) > TRIGGER_THRESHOLD,
                      &KEYMAP_AIM, now);
    game_action_apply(GAME_ACTION_SHOOT,
                      input_button(SDL_BUTTON_RIGHTSHOULDER) ||
                          input_axis_raw(SDL_AXIS_TRIGGERRIGHT) > TRIGGER_THRESHOLD,
                      &KEYMAP_SHOOT, now);
    game_action_apply(GAME_ACTION_START, input_button(SDL_BUTTON_START), &KEYMAP_START, now);
}

static void input_update_game_touchpads(void) {
    int32_t width = window_width();
    int32_t height = window_height();
    int32_t center_y = height / 2;
    touchpad_update_stick(0, input_xperia_axis(SDL_AXIS_LEFTX), input_xperia_axis(SDL_AXIS_LEFTY),
                          width / 5, center_y, width / 5, height / 2);
    touchpad_update_stick(1, input_xperia_axis(SDL_AXIS_RIGHTX), input_xperia_axis(SDL_AXIS_RIGHTY),
                          (width * 4) / 5, center_y, width / 8, height / 8);
}

static void keyboard_refresh(uint64_t now) {
    (void)now;
    input_pump();
    if (g_cursor_active || !g_controller) {
        keyboard_release_all();
    }
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
        keyboard_release_all();
        input_update_cursor(dt);
    } else {
        input_release_pointer();
        input_update_game_touchpads();
        input_update_game_keys(now);
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
    g_keyboard_updating = 1;
    keyboard_refresh(monotonic_ms());
    g_keyboard_updating = 0;
    dispatch_due_timers();
    return 0;
}

int32_t s3eKeyboardGetState(uint32_t key) {
    keyboard_refresh(monotonic_ms());
    return key < KEYBOARD_KEY_COUNT ? g_keyboard_state[key] : 0;
}

int32_t s3eKeyboardAnyKey(void) {
    keyboard_refresh(monotonic_ms());
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
    case XPERIA_KEY_ALTERNATE_FIRE:
        return "Left";
    case XPERIA_KEY_TACTICAL_GRENADE:
        return "Up";
    case XPERIA_KEY_CHANGE_WEAPON:
        return "Right";
    case XPERIA_KEY_CROUCH_PRONE:
        return "Down";
    case S3E_KEY_ENTER:
        return "Start";
    case XPERIA_KEY_SELECT:
        return "Select";
    case XPERIA_KEY_AIM:
        return "L";
    case XPERIA_KEY_SHOOT:
        return "R";
    case XPERIA_KEY_ACTION_SPRINT:
        return "Cross";
    case XPERIA_KEY_MELEE:
        return "Square";
    case XPERIA_KEY_THROW_GRENADE:
        return "Triangle";
    case XPERIA_KEY_RELOAD_CHANGE_WEAPON:
        return "Circle";
    default:
        return "";
    }
}

void s3eKeyboardClearState(void) {
    memset(g_keyboard_state, 0, sizeof(g_keyboard_state));
    memset(g_keyboard_pending, 0, sizeof(g_keyboard_pending));
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
