#include "s3e_host_internal.h"

static int g_device_quit_requested;

static void wait_with_timers(uint32_t ms) {
    uint64_t deadline = monotonic_ms() + ms;
    while (1) {
        audio_pump();
        s3e_zero_conf_pump();
        s3e_socket_pump();
        dispatch_due_timers();
        uint64_t now = monotonic_ms();
        if (now >= deadline) {
            break;
        }
        uint32_t step = (uint32_t)(deadline - now);
        if (step > 10) {
            step = 10;
        }
        sleep_ms(step);
    }
    audio_pump();
    s3e_zero_conf_pump();
    s3e_socket_pump();
}

int32_t s3eDeviceRegister(uint32_t id, void *callback, void *user_data);
int32_t s3eDeviceUnRegister(uint32_t id, void *callback);

uint64_t s3eDeviceYield(int32_t ms) {
    input_pump();
    audio_pump();
    s3e_zero_conf_pump();
    s3e_socket_pump();
    if (ms == INT32_MIN) {
        dispatch_due_timers();
        sleep_ms(1);
    } else if (ms > 0) {
        wait_with_timers((uint32_t)ms);
    } else {
        dispatch_due_timers();
    }
    return s3eTimerGetMs();
}

uint64_t s3eDeviceYieldUntilEvent(int32_t ms) {
    return s3eDeviceYield(ms ? ms : INT32_MIN);
}

int32_t s3eDeviceCheckQuitRequest(void) {
    audio_pump();
    return g_device_quit_requested;
}

int32_t s3eDeviceCheckPauseRequest(void) {
    return 0;
}

// Used to determine the device type and what controller binding set to use
int32_t s3eDeviceGetInt(uint32_t key) {
    switch (key) {
    case 0:
        return 0x12; // XperiaPlayBO
    case 10:
        return 0x00080101; // BOPlayerControlsWin
    default:
        return 0; // falls back to BOPlayerControlsWin?
    }
}

// This doesn't affect the active binding set,
// but does affect things like hud controller hints
const char *s3eDeviceGetString(uint32_t key) {
    switch (key) {
    case 0:
        return "Android";
    case 2:
        return "R800i";
    case 8:
        return "ARM7A";
    case 0x0d:
        return "4.1.2";
    case 0x13:
        return "8.6";
    case 0x14:
        return "en_US";
    case 0x15:
        return "Sony Ericsson Xperia Play";
    case 0x19:
        return s3e_device_id_get();
    case 0x1f:
    case 0x24:
    case 0x25:
        return "UTC";
    default:
        return "";
    }
}

int32_t s3eDeviceSetInt(uint32_t key, int32_t value) {
    (void)key;
    (void)value;
    return 0;
}

int32_t s3eDeviceBacklightOn(void) {
    return 0;
}

int32_t s3eDeviceRequestQuit(void) {
    g_device_quit_requested = 1;
    return 0;
}

int32_t s3eDeviceAbort(void) {
    _Exit(1);
}

int32_t s3eDeviceExit(void) {
    _Exit(0);
}

void s3eDebugOutputString(const char *text) {
    (void)text;
}

void s3eDebugPrint(int32_t channel, const char *text, int32_t color) {
    (void)channel;
    (void)text;
    (void)color;
}

int32_t s3eDebugGetInt(uint32_t key) {
    (void)key;
    return 0;
}

int32_t s3eDebugIsDebuggerPresent(void) {
    return 0;
}

void s3eDebugTraceLine(const char *text) {
    (void)text;
}

int32_t s3eDebugAssertShow(void) {
    return 0;
}

int32_t s3eDebugErrorShow(uint32_t flags, const char *text) {
    (void)flags;
    (void)text;
    return 0;
}

int32_t s3eAccelerometerStart(void) {
    return 0;
}
int32_t s3eAccelerometerStop(void) {
    return 0;
}
int32_t s3eAccelerometerGetX(void) {
    return 0;
}
int32_t s3eAccelerometerGetY(void) {
    return 0;
}
int32_t s3eAccelerometerGetZ(void) {
    return 0;
}
int32_t s3eAccelerometerGetInt(uint32_t key) {
    (void)key;
    return 0;
}
int32_t s3eVideoGetInt(uint32_t key) {
    (void)key;
    return 0;
}

int32_t s3eVideoPlay(const char *filename, uint32_t repeat) {
    (void)filename;
    (void)repeat;
    return 0;
}

int32_t s3eVideoStop(void) {
    return 0;
}

int32_t s3eVideoResume(void) {
    return 0;
}

void *s3eCompressionDecompInit(uint32_t type) {
    (void)type;
    return NULL;
}

int32_t s3eCompressionDecompRead(void *context, const void *source, uint32_t source_len,
                                 void *target, uint32_t *target_len) {
    (void)context;
    (void)source;
    (void)source_len;
    (void)target;
    if (target_len) {
        *target_len = 0;
    }
    return -1;
}

int32_t s3eCompressionDecompFinal(void *context) {
    (void)context;
    return -1;
}

int32_t s3eCompressionDecomp(const void *source, uint32_t source_len, void *target,
                             uint32_t *target_len) {
    (void)source;
    (void)source_len;
    (void)target;
    if (target_len) {
        *target_len = 0;
    }
    return -1;
}

int32_t s3eSurfaceRegister(uint32_t id, void *callback, void *user_data) {
    (void)id;
    (void)callback;
    (void)user_data;
    return 0;
}
int32_t s3eSurfaceUnRegister(uint32_t id, void *callback) {
    (void)id;
    (void)callback;
    return 0;
}

int32_t s3eSurfaceGetInt(uint32_t key) {
    switch (key) {
    case 0:
        return g_surface.width;
    case 1:
        return g_surface.height;
    case 2:
        return (int32_t)(g_surface.width * sizeof(uint32_t));
    case 3:
        return 4;
    default:
        return 0;
    }
}

void *s3eSurfacePtr(void) {
    return g_surface_pixels;
}
int32_t s3eSurfaceSetup(void) {
    return 0;
}
int32_t s3eSurfaceShow(void) {
    return 0;
}
int32_t s3eGLRegister(uint32_t id, void *callback, void *user_data) {
    (void)id;
    (void)callback;
    (void)user_data;
    return 0;
}
int32_t s3eGLUnRegister(uint32_t id, void *callback) {
    (void)id;
    (void)callback;
    return 0;
}

int32_t s3eGLGetInt(uint32_t key) {
    switch (key) {
    case 0:
        return g_surface.width;
    case 1:
        return g_surface.height;
    case 2:
        return 2;
    default:
        return 0;
    }
}

void *s3eGLGetNativeWindow(void) {
    return &g_native_window;
}

uintptr_t s3eReturn0(void) {
    return 0;
}
uintptr_t s3eStub(void) {
    return 0;
}

int32_t s3eTouchpadGetInt(uint32_t key) {
    switch (key) {
    case 0:
        return 1;
    case 1:
        return XPERIA_TOUCHPAD_WIDTH;
    case 2:
        return XPERIA_TOUCHPAD_HEIGHT;
    default:
        return -1;
    }
}

int32_t s3eTouchpadRegister(uint32_t id, void *callback, void *user_data) {
    if (id < sizeof(g_touchpad_callbacks) / sizeof(g_touchpad_callbacks[0])) {
        g_touchpad_callbacks[id].callback = callback;
        g_touchpad_callbacks[id].user_data = user_data;
    }
    return 0;
}

int32_t s3eTouchpadUnRegister(uint32_t id, void *callback) {
    if (id < sizeof(g_touchpad_callbacks) / sizeof(g_touchpad_callbacks[0]) &&
        (!callback || callback == g_touchpad_callbacks[id].callback)) {
        g_touchpad_callbacks[id].callback = NULL;
        g_touchpad_callbacks[id].user_data = NULL;
    }
    return 0;
}

int32_t isDeviceCallbackRegister(void *callback, void *user_data) {
    (void)callback;
    (void)user_data;
    return 0;
}
int32_t isDeviceCallbackUnregister(void *callback) {
    (void)callback;
    return 0;
}
int32_t isDeviceSetTabletThreshold(int32_t threshold) {
    return threshold;
}
int32_t isDeviceGetDisplayType(void) {
    return 2;
}

void *isDeviceGetExternalResources(void) {
    memset(g_is_device_resources, 0, sizeof(g_is_device_resources));
    g_is_device_resources[0x10] = 1;
    char *data_path = (char *)g_is_device_resources + IS_DEVICE_RESOURCE_PATH_A;
    char *expansion_path = (char *)g_is_device_resources + IS_DEVICE_RESOURCE_PATH_B;
    const char assets_suffix[] = "/assets/";
    size_t data_root_len = strnlen(g_root, IS_DEVICE_RESOURCE_PATH_LEN - 2);
    size_t expansion_root_len =
        strnlen(g_root, IS_DEVICE_RESOURCE_PATH_LEN - sizeof(assets_suffix));
    memcpy(data_path, g_root, data_root_len);
    data_path[data_root_len++] = '/';
    data_path[data_root_len] = 0;
    memcpy(expansion_path, g_root, expansion_root_len);
    memcpy(expansion_path + expansion_root_len, assets_suffix, sizeof(assets_suffix));
    return g_is_device_resources;
}

int32_t s3eExtGetHash(uint32_t hash, void *iface, uint32_t size) {
    if (hash == IS_AUDIO_UNIT_HASH) {
        return is_audio_unit_get_interface(iface, size);
    }
    if (hash == IS_DEVICE_HASH) {
        void *device_table[5] = {
            (void *)(uintptr_t)&isDeviceCallbackRegister,
            (void *)(uintptr_t)&isDeviceCallbackUnregister,
            (void *)(uintptr_t)&isDeviceSetTabletThreshold,
            (void *)(uintptr_t)&isDeviceGetDisplayType,
            (void *)(uintptr_t)&isDeviceGetExternalResources,
        };
        if (iface && size == sizeof(device_table)) {
            memcpy(iface, device_table, size);
            return 0;
        }
        return 1;
    }
    if (hash == S3E_TOUCHPAD_HASH) {
        void *touchpad_table[5] = {
            (void *)(uintptr_t)&s3eTouchpadRegister, (void *)(uintptr_t)&s3eTouchpadUnRegister,
            (void *)(uintptr_t)&s3eReturn0,          (void *)(uintptr_t)&s3eReturn0,
            (void *)(uintptr_t)&s3eTouchpadGetInt,
        };
        if (iface && size == sizeof(touchpad_table)) {
            memcpy(iface, touchpad_table, size);
            return 0;
        }
        return 1;
    }
    if (hash == S3E_ZERO_CONF_HASH) {
        void *zero_conf_table[5] = {
            (void *)(uintptr_t)&s3eZeroConfStartSearch,
            (void *)(uintptr_t)&s3eZeroConfStopSearch,
            (void *)(uintptr_t)&s3eZeroConfPublish,
            (void *)(uintptr_t)&s3eZeroConfUpdateTxtRecord,
            (void *)(uintptr_t)&s3eZeroConfUnpublish,
        };
        if (iface && size == sizeof(zero_conf_table)) {
            memcpy(iface, zero_conf_table, size);
            return 0;
        }
        return 1;
    }
    if (iface && size > 0) {
        memset(iface, 0, size);
    }
    return 1;
}

uintptr_t s3e_trampoline_dispatch(uint32_t index) {
    (void)index;
    return 0;
}

void *make_stub(const char *symbol) {
    enum { STUB_SIZE = 20 };
    if (!g_stub_code) {
        g_stub_code_size = 16384;
        g_stub_code = mmap(NULL, g_stub_code_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (g_stub_code == MAP_FAILED) {
            g_stub_code = NULL;
            return (void *)(uintptr_t)&s3eStub;
        }
    }
    if (g_stub_count >= sizeof(g_stub_names) / sizeof(g_stub_names[0]) ||
        (g_stub_count + 1) * STUB_SIZE > g_stub_code_size) {
        return (void *)(uintptr_t)&s3eStub;
    }
    size_t index = g_stub_count++;
    g_stub_names[index] = strdup(symbol ? symbol : "unknown");
    uint32_t *code = (uint32_t *)(void *)(g_stub_code + index * STUB_SIZE);
    code[0] = 0xe59f0004u;
    code[1] = 0xe59ff004u;
    code[2] = 0xe1a00000u;
    code[3] = (uint32_t)index;
    code[4] = (uint32_t)(uintptr_t)&s3e_trampoline_dispatch;
    __builtin___clear_cache((char *)code, (char *)(code + 5));
    return code;
}

int32_t s3eRegisterNoop(uint32_t id, void *callback, void *user_data) {
    (void)id;
    (void)callback;
    (void)user_data;
    return 0;
}

int32_t s3eDeviceRegister(uint32_t id, void *callback, void *user_data) {
    return s3eRegisterNoop(id, callback, user_data);
}

int32_t s3eDeviceUnRegister(uint32_t id, void *callback) {
    return s3eRegisterNoop(id, callback, NULL);
}
