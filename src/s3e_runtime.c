#include "s3e_host_internal.h"

static int g_device_quit_requested;

static uint64_t timer_elapsed_ms(void) {
    uint64_t now = monotonic_us();
    if (!g_host_start_us || now < g_host_start_us) {
        g_host_start_us = now;
    }
    return (now - g_host_start_us) / 1000u;
}

void *s3eMallocBase(uint32_t size, const char *file, int line) {
    (void)file;
    (void)line;
    if (g_user_mem_mgr_set && g_user_mem_mgr.alloc &&
        g_user_mem_mgr.alloc != (void *)(uintptr_t)&s3eMallocBase && !g_in_user_mem_mgr) {
        typedef void *(*alloc_fn)(uint32_t size);
        g_in_user_mem_mgr = 1;
        void *ptr = ((alloc_fn)(uintptr_t)g_user_mem_mgr.alloc)(size);
        g_in_user_mem_mgr = 0;
        return ptr;
    }
    return calloc(1, size ? size : 1);
}

void *s3eReallocBase(void *ptr, uint32_t size, const char *file, int line) {
    (void)file;
    (void)line;
    if (g_user_mem_mgr_set && g_user_mem_mgr.realloc &&
        g_user_mem_mgr.realloc != (void *)(uintptr_t)&s3eReallocBase && !g_in_user_mem_mgr) {
        typedef void *(*realloc_fn)(void *ptr, uint32_t size);
        g_in_user_mem_mgr = 1;
        void *new_ptr = ((realloc_fn)(uintptr_t)g_user_mem_mgr.realloc)(ptr, size);
        g_in_user_mem_mgr = 0;
        return new_ptr;
    }
    if (!ptr) {
        return calloc(1, size ? size : 1);
    }
    return realloc(ptr, size ? size : 1);
}

void s3eFreeBase(void *ptr) {
    if (g_user_mem_mgr_set && g_user_mem_mgr.free &&
        g_user_mem_mgr.free != (void *)(uintptr_t)&s3eFreeBase && !g_in_user_mem_mgr) {
        typedef void (*free_fn)(void *ptr);
        g_in_user_mem_mgr = 1;
        ((free_fn)(uintptr_t)g_user_mem_mgr.free)(ptr);
        g_in_user_mem_mgr = 0;
        return;
    }
    free(ptr);
}

uint64_t s3eTimerGetUST(void) {
    return timer_elapsed_ms();
}

uint64_t s3eTimerGetMs(void) {
    return timer_elapsed_ms();
}

int32_t s3eTimerGetInt(uint32_t key) {
    return key == 0 ? 1 : -1;
}

void dispatch_due_timers(void) {
    uint64_t now = monotonic_ms();
    struct timer_event *ready = NULL;
    pthread_mutex_lock(&g_timer_mutex);
    struct timer_event **link = &g_timers;
    while (*link) {
        struct timer_event *timer = *link;
        if (timer->due_ms <= now) {
            *link = timer->next;
            timer->next = ready;
            ready = timer;
        } else {
            link = &timer->next;
        }
    }
    pthread_mutex_unlock(&g_timer_mutex);
    while (ready) {
        struct timer_event *next = ready->next;
        if (ready->callback) {
            ((s3e_callback_fn)(uintptr_t)ready->callback)(NULL, ready->user_data);
        }
        free(ready);
        ready = next;
    }
}

static void wait_with_timers(uint32_t ms) {
    uint64_t deadline = monotonic_ms() + ms;
    while (1) {
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
}

uint32_t s3eTimerSetTimer(uint32_t period_ms, void *callback, void *user_data) {
    struct timer_event *timer = calloc(1, sizeof(*timer));
    if (!timer) {
        return 0;
    }
    uint32_t id = g_next_timer_id++;
    if (g_next_timer_id == 0) {
        g_next_timer_id = 1;
    }
    timer->id = id;
    timer->due_ms = monotonic_ms() + period_ms;
    timer->callback = callback;
    timer->user_data = user_data;
    pthread_mutex_lock(&g_timer_mutex);
    timer->next = g_timers;
    g_timers = timer;
    pthread_mutex_unlock(&g_timer_mutex);
    return id;
}

int32_t s3eTimerCancelTimer(uint32_t id) {
    int found = 0;
    pthread_mutex_lock(&g_timer_mutex);
    struct timer_event **link = &g_timers;
    while (*link) {
        struct timer_event *timer = *link;
        if (timer->id == id) {
            *link = timer->next;
            free(timer);
            found = 1;
            break;
        }
        link = &timer->next;
    }
    pthread_mutex_unlock(&g_timer_mutex);
    return found ? 0 : -1;
}

uint64_t s3eTimerGetUTC(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int64_t s3eTimerGetLocaltimeOffset(const uint64_t *utc_ms) {
    time_t now = utc_ms ? (time_t)(*utc_ms / 1000u) : time(NULL);
    struct tm local_tm;
    struct tm utc_tm;
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &utc_tm);
    time_t local = mktime(&local_tm);
    time_t utc = mktime(&utc_tm);
    return (int64_t)difftime(local, utc) * 1000;
}

int32_t s3eDeviceRegister(uint32_t id, void *callback, void *user_data);
int32_t s3eDeviceUnRegister(uint32_t id, void *callback);

uint64_t s3eDeviceYield(int32_t ms) {
    input_pump();
    audio_pump();
    if (ms == INT32_MIN) {
        dispatch_due_timers();
        sleep_ms(1);
    } else if (ms > 0) {
        wait_with_timers((uint32_t)ms);
    } else {
        dispatch_due_timers();
    }
    return timer_elapsed_ms();
}

uint64_t s3eDeviceYieldUntilEvent(int32_t ms) {
    return s3eDeviceYield(ms ? ms : INT32_MIN);
}

int32_t s3eDeviceCheckQuitRequest(void) {
    input_pump();
    audio_pump();
    dispatch_due_timers();
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
    case 0x14:
        return "en_US";
    case 0x15:
        return "Sony Ericsson Xperia Play";
    case 0x13:
    case 0x1f:
    case 0x24:
    case 0x25:
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

uint32_t s3eInetHtonl(uint32_t value) {
    return htonl(value);
}
uint32_t s3eInetNtohl(uint32_t value) {
    return ntohl(value);
}
uint16_t s3eInetHtons(uint16_t value) {
    return htons(value);
}
uint16_t s3eInetNtohs(uint16_t value) {
    return ntohs(value);
}

int32_t s3eInetAton(const char *address, uint32_t *out) {
    struct in_addr parsed;
    if (!address || inet_aton(address, &parsed) == 0) {
        return 1;
    }
    if (out) {
        *out = parsed.s_addr;
    }
    return 0;
}

const char *s3eInetNtoa(uint32_t address) {
    static __thread char buffer[INET_ADDRSTRLEN];
    struct in_addr in;
    in.s_addr = address;
    const char *result = inet_ntop(AF_INET, &in, buffer, sizeof(buffer));
    return result ? buffer : "0.0.0.0";
}

const char *s3eInetToString(uint32_t address) {
    return s3eInetNtoa(address);
}

int32_t s3eInetLookup(const char *hostname, uint32_t *out, void *callback, void *user_data) {
    (void)hostname;
    (void)callback;
    (void)user_data;
    if (out) {
        *out = 0;
    }
    return -1;
}

int32_t s3eInetLookupCancel(void *lookup) {
    (void)lookup;
    return 0;
}
void *s3eSocketCreate(uint32_t type, uint32_t protocol, uint32_t flags) {
    (void)type;
    (void)protocol;
    (void)flags;
    return NULL;
}
int32_t s3eSocketClose(void *socket) {
    (void)socket;
    return 0;
}
int32_t s3eSocketBind(void *socket, const void *address, uint16_t port) {
    (void)socket;
    (void)address;
    (void)port;
    return 0;
}
int32_t s3eSocketListen(void *socket, int32_t backlog) {
    (void)socket;
    (void)backlog;
    return 0;
}
void *s3eSocketAccept(void *socket, void *address) {
    (void)socket;
    (void)address;
    return NULL;
}
int32_t s3eSocketConnect(void *socket, const void *address, uint16_t port) {
    (void)socket;
    (void)address;
    (void)port;
    return -1;
}
int32_t s3eSocketSend(void *socket, const void *buffer, uint32_t length, uint32_t flags) {
    (void)socket;
    (void)buffer;
    (void)length;
    (void)flags;
    return -1;
}
int32_t s3eSocketSendTo(void *socket, const void *buffer, uint32_t length, uint32_t flags,
                        const void *address, uint16_t port) {
    (void)socket;
    (void)buffer;
    (void)length;
    (void)flags;
    (void)address;
    (void)port;
    return -1;
}
int32_t s3eSocketRecv(void *socket, void *buffer, uint32_t length, uint32_t flags) {
    (void)socket;
    (void)buffer;
    (void)length;
    (void)flags;
    return 0;
}
int32_t s3eSocketRecvFrom(void *socket, void *buffer, uint32_t length, uint32_t flags,
                          void *address) {
    (void)socket;
    (void)buffer;
    (void)length;
    (void)flags;
    (void)address;
    return 0;
}
int32_t s3eSocketReadable(void *socket) {
    (void)socket;
    return 0;
}
int32_t s3eSocketWritable(void *socket) {
    (void)socket;
    return 0;
}
int32_t s3eSocketGetInt(void *socket, uint32_t key) {
    (void)socket;
    (void)key;
    return 0;
}
int32_t s3eSocketGetError(void) {
    return -1;
}
const char *s3eSocketGetString(uint32_t key) {
    (void)key;
    return "network disabled";
}
int32_t s3eSocketGetLocalName(void *socket, void *address) {
    (void)socket;
    (void)address;
    return 1;
}
int32_t s3eSocketGetPeerName(void *socket, void *address) {
    (void)socket;
    (void)address;
    return 1;
}

int32_t s3eMemoryGetInt(uint32_t key) {
    (void)key;
    return 768 * 1024 * 1024;
}

int32_t s3eMemorySetInt(uint32_t key, int32_t value) {
    (void)key;
    (void)value;
    return 0;
}

int32_t s3eMemorySetUserMemMgr(void *mgr) {
    if (!mgr) {
        memset(&g_user_mem_mgr, 0, sizeof(g_user_mem_mgr));
        g_user_mem_mgr_set = 0;
        return 0;
    }
    struct s3e_user_mem_mgr candidate;
    memcpy(&candidate, mgr, sizeof(candidate));
    if (!candidate.alloc || !candidate.realloc || !candidate.free) {
        g_memory_error = EINVAL;
        return 1;
    }
    g_user_mem_mgr = candidate;
    g_user_mem_mgr_set = 1;
    return 0;
}

int32_t s3eMemoryGetUserMemMgr(void *out) {
    if (!out) {
        g_memory_error = EINVAL;
        return 1;
    }
    struct s3e_user_mem_mgr current = g_user_mem_mgr;
    if (!g_user_mem_mgr_set) {
        current.alloc = (void *)(uintptr_t)&s3eMallocBase;
        current.realloc = (void *)(uintptr_t)&s3eReallocBase;
        current.free = (void *)(uintptr_t)&s3eFreeBase;
    }
    memcpy(out, &current, sizeof(current));
    return 0;
}

int32_t s3eMemoryHeapCreate(uint32_t heap_index) {
    if (heap_index >= sizeof(g_heaps) / sizeof(g_heaps[0])) {
        g_memory_error = EINVAL;
        return 1;
    }
    if (!g_heaps[heap_index].base) {
        uint32_t size = heap_index == 0 ? 128u * 1024u * 1024u : 16u * 1024u * 1024u;
        g_heaps[heap_index].base = calloc(1, size);
        if (!g_heaps[heap_index].base) {
            g_memory_error = ENOMEM;
            return 1;
        }
        g_heaps[heap_index].size = size;
    }
    return 0;
}

int32_t s3eMemoryHeapDestroy(uint32_t heap_index) {
    if (heap_index >= sizeof(g_heaps) / sizeof(g_heaps[0])) {
        g_memory_error = EINVAL;
        return 1;
    }
    free(g_heaps[heap_index].base);
    g_heaps[heap_index].base = NULL;
    g_heaps[heap_index].size = 0;
    return 0;
}

void *s3eMemoryHeapAddress(uint32_t heap_index) {
    if (heap_index >= sizeof(g_heaps) / sizeof(g_heaps[0])) {
        g_memory_error = EINVAL;
        return NULL;
    }
    if (!g_heaps[heap_index].base && s3eMemoryHeapCreate(heap_index) != 0) {
        return NULL;
    }
    return g_heaps[heap_index].base;
}

int32_t s3eMemoryGetError(void) {
    return g_memory_error;
}
const char *s3eMemoryGetErrorString(void) {
    return strerror(g_memory_error);
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
