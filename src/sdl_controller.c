#include "s3e_host_internal.h"
#include "sdl_controller.h"

enum {
    SDL_INIT_JOYSTICK = 0x00000200u,
    SDL_INIT_GAMECONTROLLER = 0x00002000u,
    SDL_LASTEVENT = 0xffffu,
    CONTROLLER_SCAN_INTERVAL_MS = 1000,
};

struct sdl_joystick_guid {
    uint8_t data[16];
};

struct sdl_input_api {
    int (*InitSubSystem)(uint32_t flags);
    void (*QuitSubSystem)(uint32_t flags);
    int (*NumJoysticks)(void);
    int (*IsGameController)(int joystick_index);
    const char *(*JoystickNameForIndex)(int joystick_index);
    const char *(*GameControllerNameForIndex)(int joystick_index);
    struct sdl_joystick_guid (*JoystickGetDeviceGUID)(int joystick_index);
    void (*JoystickGetGUIDString)(struct sdl_joystick_guid guid, char *text, int size);
    void *(*GameControllerOpen)(int joystick_index);
    void (*GameControllerClose)(void *gamecontroller);
    int (*GameControllerGetAttached)(void *gamecontroller);
    void *(*GameControllerGetJoystick)(void *gamecontroller);
    int (*JoystickNumHats)(void *joystick);
    uint8_t (*JoystickGetHat)(void *joystick, int hat);
    void (*PumpEvents)(void);
    void (*FlushEvents)(uint32_t min_type, uint32_t max_type);
    void (*GameControllerUpdate)(void);
    int16_t (*GameControllerGetAxis)(void *gamecontroller, int axis);
    uint8_t (*GameControllerGetButton)(void *gamecontroller, int button);
    const char *(*GetError)(void);
};

struct input_controller {
    void *handle;
    void *joystick;
};

static void *g_sdl2;
static struct sdl_input_api g_sdl;
static struct input_controller *g_controllers;
static size_t g_controller_count;
static int g_sdl_initialized;
static int g_sdl_unavailable;
static int g_known_joystick_count = -1;
static int g_controller_scan_logged;
static uint64_t g_next_controller_scan_ms;

static int sdl_load_symbol(void **slot, const char *name) {
    *slot = dlsym(g_sdl2, name);
    return *slot != NULL;
}

static void sdl_load_optional_symbol(void **slot, const char *name) {
    *slot = dlsym(g_sdl2, name);
}

static const char *sdl_last_error(void) {
    if (!g_sdl.GetError) {
        return "unknown";
    }
    const char *error = g_sdl.GetError();
    return error && error[0] ? error : "unknown";
}

static int sdl_initialize(void) {
    if (g_sdl_initialized) {
        return 1;
    }
    if (g_sdl_unavailable) {
        return 0;
    }

    const char *names[] = {"libSDL2-2.0.so.0", "libSDL2.so", NULL};
    g_sdl2 = open_first(names);
    if (!g_sdl2) {
        fprintf(stderr, "[input] SDL2 controller library unavailable\n");
        g_sdl_unavailable = 1;
        return 0;
    }

    int ok = 1;
    ok &= sdl_load_symbol((void **)&g_sdl.InitSubSystem, "SDL_InitSubSystem");
    ok &= sdl_load_symbol((void **)&g_sdl.QuitSubSystem, "SDL_QuitSubSystem");
    ok &= sdl_load_symbol((void **)&g_sdl.NumJoysticks, "SDL_NumJoysticks");
    ok &= sdl_load_symbol((void **)&g_sdl.IsGameController, "SDL_IsGameController");
    sdl_load_optional_symbol((void **)&g_sdl.JoystickNameForIndex, "SDL_JoystickNameForIndex");
    sdl_load_optional_symbol((void **)&g_sdl.GameControllerNameForIndex,
                             "SDL_GameControllerNameForIndex");
    sdl_load_optional_symbol((void **)&g_sdl.JoystickGetDeviceGUID, "SDL_JoystickGetDeviceGUID");
    sdl_load_optional_symbol((void **)&g_sdl.JoystickGetGUIDString, "SDL_JoystickGetGUIDString");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerOpen, "SDL_GameControllerOpen");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerClose, "SDL_GameControllerClose");
    sdl_load_optional_symbol((void **)&g_sdl.GameControllerGetAttached,
                             "SDL_GameControllerGetAttached");
    sdl_load_optional_symbol((void **)&g_sdl.GameControllerGetJoystick,
                             "SDL_GameControllerGetJoystick");
    sdl_load_optional_symbol((void **)&g_sdl.JoystickNumHats, "SDL_JoystickNumHats");
    sdl_load_optional_symbol((void **)&g_sdl.JoystickGetHat, "SDL_JoystickGetHat");
    ok &= sdl_load_symbol((void **)&g_sdl.PumpEvents, "SDL_PumpEvents");
    ok &= sdl_load_symbol((void **)&g_sdl.FlushEvents, "SDL_FlushEvents");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerUpdate, "SDL_GameControllerUpdate");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerGetAxis, "SDL_GameControllerGetAxis");
    ok &= sdl_load_symbol((void **)&g_sdl.GameControllerGetButton, "SDL_GameControllerGetButton");
    sdl_load_optional_symbol((void **)&g_sdl.GetError, "SDL_GetError");

    if (!ok || g_sdl.InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        if (!ok) {
            fprintf(stderr, "[input] SDL2 controller symbols unavailable\n");
        } else {
            fprintf(stderr, "[input] SDL_InitSubSystem failed: %s\n", sdl_last_error());
        }
        dlclose(g_sdl2);
        g_sdl2 = NULL;
        memset(&g_sdl, 0, sizeof(g_sdl));
        g_sdl_unavailable = 1;
        return 0;
    }

    g_sdl_initialized = 1;
    const char *mapping = getenv("SDL_GAMECONTROLLERCONFIG");
    if (!mapping || !mapping[0]) {
        fprintf(stderr, "[input] SDL_GAMECONTROLLERCONFIG is empty\n");
    }
    return 1;
}

int sdl_controller_connected(void) {
    return g_controller_count > 0;
}

static void close_controllers(void) {
    for (size_t i = 0; i < g_controller_count; ++i) {
        g_sdl.GameControllerClose(g_controllers[i].handle);
    }
    free(g_controllers);
    g_controllers = NULL;
    g_controller_count = 0;
}

static const char *device_name(int index, int is_gamecontroller) {
    const char *name = NULL;
    if (is_gamecontroller && g_sdl.GameControllerNameForIndex) {
        name = g_sdl.GameControllerNameForIndex(index);
    }
    if (!name && g_sdl.JoystickNameForIndex) {
        name = g_sdl.JoystickNameForIndex(index);
    }
    return name ? name : "unknown";
}

static const char *device_guid(int index, char text[33]) {
    text[0] = '\0';
    if (g_sdl.JoystickGetDeviceGUID && g_sdl.JoystickGetGUIDString) {
        struct sdl_joystick_guid guid = g_sdl.JoystickGetDeviceGUID(index);
        g_sdl.JoystickGetGUIDString(guid, text, 33);
    }
    return text[0] ? text : "unknown";
}

static void scan_controllers(int joystick_count, int report) {
    close_controllers();

    if (joystick_count <= 0) {
        if (report) {
            fprintf(stderr, "[input] no SDL joysticks found\n");
        }
        return;
    }

    struct input_controller *controllers = calloc((size_t)joystick_count, sizeof(*controllers));
    if (!controllers) {
        fprintf(stderr, "[input] failed to allocate controller list\n");
        return;
    }

    size_t opened = 0;
    for (int i = 0; i < joystick_count; ++i) {
        int is_gamecontroller = g_sdl.IsGameController(i) != 0;
        if (report) {
            char guid[33];
            fprintf(stderr, "[input] joystick %d: name=\"%s\" guid=%s mapped=%s\n", i,
                    device_name(i, is_gamecontroller), device_guid(i, guid),
                    is_gamecontroller ? "yes" : "no");
        }
        if (!is_gamecontroller) {
            continue;
        }

        void *handle = g_sdl.GameControllerOpen(i);
        if (!handle) {
            if (report) {
                fprintf(stderr, "[input] failed to open joystick %d: %s\n", i, sdl_last_error());
            }
            continue;
        }

        controllers[opened].handle = handle;
        controllers[opened].joystick =
            g_sdl.GameControllerGetJoystick ? g_sdl.GameControllerGetJoystick(handle) : NULL;
        ++opened;
    }

    if (!opened) {
        free(controllers);
        if (report) {
            fprintf(stderr, "[input] no mapped SDL game controller could be opened\n");
        }
        return;
    }

    g_controllers = controllers;
    g_controller_count = opened;
    fprintf(stderr, "[input] opened %zu SDL game controller%s\n", opened, opened == 1 ? "" : "s");
}

static int controllers_attached(void) {
    if (!g_sdl.GameControllerGetAttached) {
        return 1;
    }
    for (size_t i = 0; i < g_controller_count; ++i) {
        if (!g_sdl.GameControllerGetAttached(g_controllers[i].handle)) {
            return 0;
        }
    }
    return 1;
}

static void refresh_controllers(uint64_t now_ms) {
    int detached = sdl_controller_connected() && !controllers_attached();
    if (!detached && now_ms < g_next_controller_scan_ms) {
        return;
    }
    g_next_controller_scan_ms = now_ms + CONTROLLER_SCAN_INTERVAL_MS;

    int joystick_count = g_sdl.NumJoysticks();
    if (joystick_count < 0) {
        if (!g_controller_scan_logged || g_known_joystick_count != joystick_count) {
            fprintf(stderr, "[input] SDL_NumJoysticks failed: %s\n", sdl_last_error());
        }
        g_known_joystick_count = joystick_count;
        g_controller_scan_logged = 1;
        return;
    }

    int report = !g_controller_scan_logged || joystick_count != g_known_joystick_count || detached;
    if (detached || !sdl_controller_connected() || joystick_count != g_known_joystick_count) {
        g_known_joystick_count = joystick_count;
        scan_controllers(joystick_count, report);
        g_controller_scan_logged = 1;
    }
}

void sdl_controller_update(uint64_t now_ms) {
    if (!sdl_initialize()) {
        return;
    }
    /* The game has no SDL event loop, but Wayland focus changes arrive through it. */
    g_sdl.PumpEvents();
    g_sdl.GameControllerUpdate();
    g_sdl.FlushEvents(0, SDL_LASTEVENT);
    refresh_controllers(now_ms);
}

int sdl_controller_button(int button) {
    if (!g_sdl.GameControllerGetButton || button < 0) {
        return 0;
    }
    for (size_t i = 0; i < g_controller_count; ++i) {
        if (g_sdl.GameControllerGetButton(g_controllers[i].handle, button)) {
            return 1;
        }
    }
    return 0;
}

int32_t sdl_controller_axis(int axis) {
    if (!g_sdl.GameControllerGetAxis) {
        return 0;
    }

    int32_t strongest = 0;
    int32_t strongest_magnitude = 0;
    for (size_t i = 0; i < g_controller_count; ++i) {
        int32_t value = g_sdl.GameControllerGetAxis(g_controllers[i].handle, axis);
        int32_t magnitude = value < 0 ? -value : value;
        if (magnitude > strongest_magnitude) {
            strongest = value;
            strongest_magnitude = magnitude;
        }
    }
    return strongest;
}

uint8_t sdl_controller_hat_mask(void) {
    uint8_t mask = 0;
    if (!g_sdl.JoystickNumHats || !g_sdl.JoystickGetHat) {
        return 0;
    }

    for (size_t controller = 0; controller < g_controller_count; ++controller) {
        void *joystick = g_controllers[controller].joystick;
        if (!joystick) {
            continue;
        }
        int count = g_sdl.JoystickNumHats(joystick);
        for (int hat = 0; hat < count; ++hat) {
            mask |= g_sdl.JoystickGetHat(joystick, hat);
        }
    }
    return mask;
}

void sdl_controller_shutdown(void) {
    close_controllers();
    if (g_sdl2) {
        if (g_sdl_initialized && g_sdl.QuitSubSystem) {
            g_sdl.QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
        }
        dlclose(g_sdl2);
    }

    g_sdl2 = NULL;
    memset(&g_sdl, 0, sizeof(g_sdl));
    g_sdl_initialized = 0;
    g_sdl_unavailable = 0;
    g_known_joystick_count = -1;
    g_controller_scan_logged = 0;
    g_next_controller_scan_ms = 0;
}
