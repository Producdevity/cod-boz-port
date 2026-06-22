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
uint32_t g_next_timer_id = 1;
int g_memory_error;
struct s3e_user_mem_mgr g_user_mem_mgr;
int g_user_mem_mgr_set;
__thread int g_in_user_mem_mgr;
struct s3e_heap g_heaps[8];
struct fbdev_window g_native_window = {640, 480};
uint32_t g_surface_pixels[640 * 480];
struct callback_slot g_pointer_callbacks[4];
struct callback_slot g_touchpad_callbacks[8];
struct keyboard_callback_slot g_keyboard_callbacks[16];
int32_t g_pointer_x = 110;
int32_t g_pointer_y = 150;
int g_pointer_down;
uint8_t g_pointer_states[5];
int g_cursor_active = 1;
void (*g_debug_line_callback)(const char *text);
uint8_t g_is_device_resources[IS_DEVICE_RESOURCES_SIZE];

uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
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
    void *addr = NULL;
    if (g_gles2) {
        addr = dlsym(g_gles2, symbol);
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

bool s3e_host_init(const char *root) {
    if (root && root[0]) {
        snprintf(g_root, sizeof(g_root), "%s", root);
    } else if (!getcwd(g_root, sizeof(g_root))) {
        snprintf(g_root, sizeof(g_root), ".");
    }
    const char *egl_names[] = {"libEGL.so.1", "libEGL.so", NULL};
    const char *gles1_names[] = {"libGLESv1_CM.so.1", "libGLESv1_CM.so", "libmali.so", NULL};
    const char *gles2_names[] = {"libGLESv2.so.2", "libGLESv2.so", "libmali.so", NULL};
    g_egl = open_first(egl_names);
    g_gles1 = open_first(gles1_names);
    g_gles2 = open_first(gles2_names);
    return true;
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
    input_shutdown();
    clear_timers();
    close_all_memory_files();
    for (size_t i = 0; i < sizeof(g_heaps) / sizeof(g_heaps[0]); ++i) {
        free(g_heaps[i].base);
        g_heaps[i].base = NULL;
        g_heaps[i].size = 0;
    }
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

void s3e_host_set_debug_line_callback(void (*callback)(const char *text)) {
    g_debug_line_callback = callback;
}

void s3e_host_mark_gameplay_ready(void) {}
