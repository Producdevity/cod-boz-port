#include "s3e_host_internal.h"

char g_root[1024];
void *g_egl;
void *g_gles1;
void *g_gles2;
uint8_t *g_stub_code;
size_t g_stub_code_size;
const char *g_stub_names[512];
size_t g_stub_count;
struct dtrz_index g_dtrz;
struct memory_file *g_memory_files;
struct timer_event *g_timers;
pthread_mutex_t g_timer_mutex = PTHREAD_MUTEX_INITIALIZER;
struct fbdev_window g_native_window = {640, 480};
struct surface_geometry g_surface = {0, 0, 640, 480};
uint32_t *g_surface_pixels;
struct callback_slot g_pointer_callbacks[4];
struct callback_slot g_touchpad_callbacks[8];
struct keyboard_callback_slot g_keyboard_callbacks[16];
int32_t g_pointer_x = 110;
int32_t g_pointer_y = 150;
int g_pointer_down;
uint8_t g_pointer_states[5];
int g_cursor_active = 1;
uint8_t g_is_device_resources[IS_DEVICE_RESOURCES_SIZE];
uint64_t g_host_start_us;

uint64_t monotonic_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

uint64_t monotonic_ms(void) {
    return monotonic_us() / 1000u;
}

void sleep_ms(uint32_t ms) {
    struct timespec req = {
        .tv_sec = ms / 1000u,
        .tv_nsec = (long)(ms % 1000u) * 1000000L,
    };
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

void *open_first(const char *const *names) {
    for (size_t i = 0; names[i]; ++i) {
        void *handle = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (handle) {
            return handle;
        }
    }
    return NULL;
}

void *lookup_gl(const char *symbol) {
    void *addr = egl_backend_get_gl_proc(symbol);
    if (g_gles2) {
        addr = addr ? addr : dlsym(g_gles2, symbol);
    }
    if (!addr && g_gles1) {
        addr = dlsym(g_gles1, symbol);
    }
    if (!addr && g_egl) {
        void *(*egl_get_proc_address)(const char *) = dlsym(g_egl, "eglGetProcAddress");
        if (egl_get_proc_address) {
            addr = egl_get_proc_address(symbol);
        }
    }
    return addr;
}

void *lookup_egl(const char *symbol) {
    return g_egl ? dlsym(g_egl, symbol) : NULL;
}

bool s3e_host_set_display_size(uint32_t width, uint32_t height) {
    if (!width || !height || width > UINT16_MAX || height > UINT16_MAX) {
        return false;
    }

    uint32_t surface_width = width;
    uint32_t surface_height = height;
    uint32_t minimum_height = (width * 3u + 2u) / 4u;
    uint32_t maximum_width = (height * 16u + 4u) / 9u;
    if (height > minimum_height) {
        surface_height = minimum_height;
    } else if (width > maximum_width + 1u) {
        surface_width = maximum_width;
    }

    if (surface_width > SIZE_MAX / surface_height ||
        surface_width * surface_height > SIZE_MAX / sizeof(*g_surface_pixels)) {
        return false;
    }

    uint32_t *pixels = calloc((size_t)surface_width * surface_height, sizeof(*pixels));
    if (!pixels) {
        return false;
    }

    free(g_surface_pixels);
    g_surface_pixels = pixels;
    g_native_window.width = (uint16_t)width;
    g_native_window.height = (uint16_t)height;
    g_surface.x = (uint16_t)((width - surface_width) / 2u);
    g_surface.y = (uint16_t)((height - surface_height) / 2u);
    g_surface.width = (uint16_t)surface_width;
    g_surface.height = (uint16_t)surface_height;
    g_pointer_x = (int32_t)((uint64_t)surface_width * 110u / 640u);
    g_pointer_y = (int32_t)((uint64_t)surface_height * 150u / 480u);
    return true;
}

bool s3e_host_init(const char *root) {
    g_host_start_us = monotonic_us();
    if (root && root[0]) {
        snprintf(g_root, sizeof(g_root), "%s", root);
    } else if (!getcwd(g_root, sizeof(g_root))) {
        snprintf(g_root, sizeof(g_root), ".");
    }
    if (!g_surface_pixels &&
        !s3e_host_set_display_size(g_native_window.width, g_native_window.height)) {
        fprintf(stderr, "failed to allocate %ux%u S3E surface\n", g_native_window.width,
                g_native_window.height);
        return false;
    }
    fprintf(stderr, "[display] drawable=%ux%u surface=%ux%u offset=%u,%u pitch=%zu\n",
            g_native_window.width, g_native_window.height, g_surface.width, g_surface.height,
            g_surface.x, g_surface.y, (size_t)g_surface.width * sizeof(*g_surface_pixels));
    return egl_backend_load_libraries();
}

static void clear_timers(void) {
    pthread_mutex_lock(&g_timer_mutex);
    struct timer_event *timer = g_timers;
    g_timers = NULL;
    pthread_mutex_unlock(&g_timer_mutex);
    while (timer) {
        struct timer_event *next = timer->next;
        free(timer);
        timer = next;
    }
}

static void close_all_memory_files(void) {
    while (g_memory_files) {
        struct memory_file *next = g_memory_files->next;
        fclose(g_memory_files->file);
        free(g_memory_files->buffer);
        free(g_memory_files);
        g_memory_files = next;
    }
}

void s3e_host_shutdown(void) {
    (void)s3eMemorySetUserMemMgr(NULL);
    s3e_zero_conf_shutdown();
    s3e_socket_shutdown();
    audio_shutdown();
    input_shutdown();
    egl_backend_shutdown();
    clear_timers();
    close_all_memory_files();
    free(g_surface_pixels);
    g_surface_pixels = NULL;
    s3e_memory_shutdown();
    for (size_t i = 0; i < g_stub_count; ++i) {
        free((void *)g_stub_names[i]);
    }
    g_stub_count = 0;
    if (g_stub_code) {
        munmap(g_stub_code, g_stub_code_size);
        g_stub_code = NULL;
        g_stub_code_size = 0;
    }
    if (g_egl) {
        dlclose(g_egl);
        g_egl = NULL;
    }
    if (g_gles1) {
        dlclose(g_gles1);
        g_gles1 = NULL;
    }
    if (g_gles2) {
        dlclose(g_gles2);
        g_gles2 = NULL;
    }
}
