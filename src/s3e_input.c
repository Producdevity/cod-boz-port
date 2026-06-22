#include "s3e_host_internal.h"

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

static void *g_sdl2;
static struct sdl_input_api g_sdl;
static void *g_controller;
static int g_sdl_tried;
static int g_prev_select;
static int g_prev_a;
static uint64_t g_input_last_ms;

#define SDL_INIT_JOYSTICK 0x00000200u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_BUTTON_A 0
#define SDL_BUTTON_BACK 4
#define SDL_BUTTON_DPAD_UP 11
#define SDL_BUTTON_DPAD_DOWN 12
#define SDL_BUTTON_DPAD_LEFT 13
#define SDL_BUTTON_DPAD_RIGHT 14
#define SDL_AXIS_LEFTX 0
#define SDL_AXIS_LEFTY 1

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

static int32_t input_axis(int axis) {
    if (!g_controller || !g_sdl.GameControllerGetAxis) {
        return 0;
    }
    int32_t value = g_sdl.GameControllerGetAxis(g_controller, axis);
    return value < -8000 || value > 8000 ? value : 0;
}

static int32_t clamp_pointer_x(int32_t x) {
    if (x < 0) {
        return 0;
    }
    if (x >= g_native_window.width) {
        return g_native_window.width - 1;
    }
    return x;
}

static int32_t clamp_pointer_y(int32_t y) {
    if (y < 0) {
        return 0;
    }
    if (y >= g_native_window.height) {
        return g_native_window.height - 1;
    }
    return y;
}

static void pointer_dispatch(uint32_t id, void *event) {
    if (id >= sizeof(g_pointer_callbacks) / sizeof(g_pointer_callbacks[0])) {
        return;
    }
    struct callback_slot *slot = &g_pointer_callbacks[id];
    if (!slot->callback) {
        return;
    }
    typedef int32_t (*callback_fn)(void *system_data, void *user_data);
    ((callback_fn)(uintptr_t)slot->callback)(event, slot->user_data);
}

static void pointer_set_down(int down) {
    if (down) {
        g_pointer_states[0] = g_pointer_down ? 1u : 2u;
        g_pointer_down = 1;
    } else {
        g_pointer_states[0] = g_pointer_down ? 4u : 0u;
        g_pointer_down = 0;
    }
}

static void pointer_clear_transitions(void) {
    for (size_t i = 0; i < sizeof(g_pointer_states); ++i) {
        if (g_pointer_states[i] == 2u) {
            g_pointer_states[i] = 1u;
        } else if (g_pointer_states[i] == 4u) {
            g_pointer_states[i] = 0u;
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

void input_pump(void) {
    input_open();
    if (!g_controller) {
        return;
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
        }
        g_cursor_active = !g_cursor_active;
    }
    g_prev_select = select;

    int a = input_button(SDL_BUTTON_A);
    if (!g_cursor_active) {
        g_prev_a = a;
        return;
    }

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

void input_shutdown(void) {
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
    for (size_t i = 0; i < sizeof(g_keyboard_callbacks) / sizeof(g_keyboard_callbacks[0]); ++i) {
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
    for (size_t i = 0; i < sizeof(g_keyboard_callbacks) / sizeof(g_keyboard_callbacks[0]); ++i) {
        struct keyboard_callback_slot *slot = &g_keyboard_callbacks[i];
        if (slot->callback && slot->id == id && (!callback || callback == slot->callback)) {
            slot->callback = NULL;
            slot->user_data = NULL;
        }
    }
    return 0;
}

int32_t s3eKeyboardUpdate(void) {
    input_pump();
    dispatch_due_timers();
    return 0;
}

int32_t s3eKeyboardGetState(uint32_t key) {
    (void)key;
    input_pump();
    return 0;
}

int32_t s3eKeyboardAnyKey(void) {
    input_pump();
    return 0;
}

int32_t s3eKeyboardGetInt(uint32_t key) {
    return key == 2 ? 1 : 0;
}

int32_t s3eKeyboardSetInt(uint32_t key, int32_t value) {
    (void)key;
    (void)value;
    return 0;
}

const char *s3eKeyboardGetDisplayName(uint32_t key) {
    switch (key) {
    case 19:
        return "Left";
    case 20:
        return "Up";
    case 21:
        return "Right";
    case 22:
        return "Down";
    case 82:
        return "Start";
    case 4:
        return "Select";
    case 102:
        return "L";
    case 103:
        return "R";
    case 23:
        return "Cross";
    case 99:
        return "Square";
    case 100:
        return "Triangle";
    case 44:
        return "Circle";
    default:
        return "";
    }
}

void s3eKeyboardClearState(void) {}

int32_t s3ePointerRegister(uint32_t id, void *callback, void *user_data) {
    if (id < sizeof(g_pointer_callbacks) / sizeof(g_pointer_callbacks[0])) {
        g_pointer_callbacks[id].callback = callback;
        g_pointer_callbacks[id].user_data = user_data;
    }
    return 0;
}

int32_t s3ePointerUnRegister(uint32_t id, void *callback) {
    if (id < sizeof(g_pointer_callbacks) / sizeof(g_pointer_callbacks[0]) &&
        (!callback || callback == g_pointer_callbacks[id].callback)) {
        g_pointer_callbacks[id].callback = NULL;
        g_pointer_callbacks[id].user_data = NULL;
    }
    return 0;
}

int32_t s3ePointerUpdate(void) {
    input_pump();
    dispatch_due_timers();
    pointer_clear_transitions();
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
    return touch_id == 0 ? g_pointer_states[0] : 0;
}

int32_t s3ePointerGetTouchX(uint32_t touch_id) {
    input_pump();
    return touch_id == 0 ? g_pointer_x : 0;
}

int32_t s3ePointerGetTouchY(uint32_t touch_id) {
    input_pump();
    return touch_id == 0 ? g_pointer_y : 0;
}

int32_t s3ePointerGetPressure(uint32_t button) {
    input_pump();
    return button == 0 && g_pointer_down ? 1 : 0;
}

int32_t s3ePointerGetTouchPressure(uint32_t touch_id) {
    input_pump();
    return touch_id == 0 && g_pointer_down ? 1 : 0;
}

int32_t s3ePointerGetError(void) {
    return 0;
}

const char *s3ePointerGetErrorString(void) {
    return "S3E_POINTER_ERR_NONE";
}
