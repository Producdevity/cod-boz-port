#ifndef CODBOZ_S3E_HOST_INTERNAL_H
#define CODBOZ_S3E_HOST_INTERNAL_H

#include "s3e_host.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__arm__)
#define S3E_SOFTFP __attribute__((pcs("aapcs")))
#else
#define S3E_SOFTFP
#endif

typedef int32_t(S3E_SOFTFP *s3e_callback_fn)(void *system_data, void *user_data);

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef void GLvoid;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;
typedef int EGLint;
typedef unsigned int EGLBoolean;
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLNativeWindowType;

enum {
    GL_SCISSOR_TEST_VALUE = 0x0c11,
    GL_SCISSOR_BOX_VALUE = 0x0c10,
    GL_COLOR_CLEAR_VALUE = 0x0c22,
    GL_COLOR_BUFFER_BIT_VALUE = 0x00004000,
};

#define S3E_CONFIG_MAX_ENTRIES 512
#define DTRZ_MAX_ENTRIES 512
#define DTRZ_NAME_MAX 192
#define IS_DEVICE_RESOURCES_SIZE 0x728
#define IS_DEVICE_RESOURCE_PATH_A 0x428
#define IS_DEVICE_RESOURCE_PATH_B 0x527
#define IS_DEVICE_RESOURCE_PATH_LEN 0xff
#define S3E_TOUCHPAD_HASH 0x1dbd7ce8u
#define IS_DEVICE_HASH 0xe7c6ef51u
#define XPERIA_TOUCHPAD_WIDTH 960
#define XPERIA_TOUCHPAD_HEIGHT 544

struct s3e_config_entry {
    char section[64];
    char key[96];
    char value[256];
};

struct dtrz_entry {
    char name[DTRZ_NAME_MAX];
    char lower_name[DTRZ_NAME_MAX];
    char lower_base[DTRZ_NAME_MAX];
    uint32_t offset;
    uint32_t size;
};

struct dtrz_index {
    int loaded;
    size_t count;
    char path[1200];
    struct dtrz_entry entries[DTRZ_MAX_ENTRIES];
};

struct memory_file {
    FILE *file;
    void *buffer;
    struct memory_file *next;
};

struct timer_event {
    uint32_t id;
    uint64_t due_ms;
    void *callback;
    void *user_data;
    struct timer_event *next;
};

struct fbdev_window {
    uint16_t width;
    uint16_t height;
};

struct surface_geometry {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
};

struct callback_slot {
    void *callback;
    void *user_data;
};

struct keyboard_callback_slot {
    uint32_t id;
    void *callback;
    void *user_data;
};

struct s3e_user_mem_mgr {
    void *alloc;
    void *realloc;
    void *free;
};

struct s3e_heap {
    void *base;
    uint32_t size;
};

struct s3e_pointer_button_event {
    int32_t button;
    int32_t pressed;
    int32_t x;
    int32_t y;
};

struct s3e_pointer_motion_event {
    int32_t x;
    int32_t y;
};

struct s3e_pointer_touch_event {
    int32_t touch_id;
    int32_t pressed;
    int32_t x;
    int32_t y;
};

struct s3e_pointer_touch_motion_event {
    int32_t touch_id;
    int32_t x;
    int32_t y;
};

struct s3e_keyboard_event {
    int32_t key;
    int32_t pressed;
};

struct s3e_touchpad_button_event {
    int32_t id;
    int32_t pressed;
    int32_t x;
    int32_t y;
};

struct s3e_touchpad_motion_event {
    int32_t id;
    int32_t x;
    int32_t y;
};

extern char g_root[1024];
extern void *g_egl;
extern void *g_gles1;
extern void *g_gles2;
extern uint8_t *g_stub_code;
extern size_t g_stub_code_size;
extern const char *g_stub_names[512];
extern size_t g_stub_count;
extern struct dtrz_index g_dtrz;
extern struct memory_file *g_memory_files;
extern struct timer_event *g_timers;
extern pthread_mutex_t g_timer_mutex;
extern uint32_t g_next_timer_id;
extern int g_memory_error;
extern struct s3e_user_mem_mgr g_user_mem_mgr;
extern int g_user_mem_mgr_set;
extern __thread int g_in_user_mem_mgr;
extern struct s3e_heap g_heaps[8];
extern struct fbdev_window g_native_window;
extern struct surface_geometry g_surface;
extern uint32_t *g_surface_pixels;
extern struct callback_slot g_pointer_callbacks[4];
extern struct callback_slot g_touchpad_callbacks[8];
extern struct keyboard_callback_slot g_keyboard_callbacks[16];
extern int32_t g_pointer_x;
extern int32_t g_pointer_y;
extern int g_pointer_down;
extern uint8_t g_pointer_states[5];
extern int g_cursor_active;
extern uint8_t g_is_device_resources[IS_DEVICE_RESOURCES_SIZE];
extern uint64_t g_host_start_us;

uint64_t monotonic_us(void);
uint64_t monotonic_ms(void);
void sleep_ms(uint32_t ms);
void *open_first(const char *const *names);
void *lookup_gl(const char *symbol);
void *lookup_egl(const char *symbol);
void codboz_hide_virtual_stick_artwork(const char *name, uint8_t *data, size_t size);
bool egl_backend_load_libraries(void);
void *egl_backend_resolve(const char *symbol);
void *egl_backend_get_proc_address(const char *symbol);
void *egl_backend_get_gl_proc(const char *symbol);
EGLBoolean egl_backend_swap_buffers(EGLDisplay display, EGLSurface surface);
void egl_backend_shutdown(void);
void input_pump(void);
void input_shutdown(void);
void audio_shutdown(void);
void dispatch_due_timers(void);
void *make_stub(const char *symbol);

void *s3eMallocBase(uint32_t size, const char *file, int line);
void *s3eReallocBase(void *ptr, uint32_t size, const char *file, int line);
void s3eFreeBase(void *ptr);

void *s3eFileOpen(const char *name, const char *mode);
int32_t s3eFileClose(void *file);
uint32_t s3eFileRead(void *buffer, uint32_t elem_size, uint32_t count, void *file);
uint32_t s3eFileWrite(const void *buffer, uint32_t elem_size, uint32_t count, void *file);
int32_t s3eFileGetChar(void *file);
int32_t s3eFilePutChar(int32_t c, void *file);
int32_t s3eFileFlush(void *file);
int32_t s3eFileSeek(void *file, int32_t offset, int32_t origin);
int32_t s3eFileTell(void *file);
int32_t s3eFileGetSize(void *file);
int32_t s3eFileCheckExists(const char *name);
int32_t s3eFileGetError(void);
const char *s3eFileGetErrorString(void);
void *s3eFileOpenFromMemory(void *buffer, uint32_t size);
int32_t s3eFileGetFileInt(void *file, uint32_t key);
int32_t s3eFileMakeDirectory(const char *name);
int32_t s3eFileDelete(const char *name);
int32_t s3eFileRename(const char *old_name, const char *new_name);
int32_t s3eFileAddUserFileSys(const char *prefix, const char *path);
void *s3eFileListDirectory(const char *path);
int32_t s3eFileListClose(void *list);

void *s3eCompressionDecompInit(uint32_t type);
int32_t s3eCompressionDecompRead(void *context, const void *source, uint32_t source_len,
                                 void *target, uint32_t *target_len);
int32_t s3eCompressionDecompFinal(void *context);
int32_t s3eCompressionDecomp(const void *source, uint32_t source_len, void *target,
                             uint32_t *target_len);

uint64_t s3eTimerGetUST(void);
uint64_t s3eTimerGetMs(void);
int32_t s3eTimerGetInt(uint32_t key);
uint32_t s3eTimerSetTimer(uint32_t period_ms, void *callback, void *user_data);
int32_t s3eTimerCancelTimer(uint32_t id);
uint64_t s3eTimerGetUTC(void);
int64_t s3eTimerGetLocaltimeOffset(const uint64_t *utc_ms);

int32_t s3eDeviceRegister(uint32_t id, void *callback, void *user_data);
int32_t s3eDeviceUnRegister(uint32_t id, void *callback);
uint64_t s3eDeviceYield(int32_t ms);
uint64_t s3eDeviceYieldUntilEvent(int32_t ms);
int32_t s3eDeviceCheckQuitRequest(void);
int32_t s3eDeviceCheckPauseRequest(void);
int32_t s3eDeviceGetInt(uint32_t key);
const char *s3eDeviceGetString(uint32_t key);
int32_t s3eDeviceSetInt(uint32_t key, int32_t value);
int32_t s3eDeviceBacklightOn(void);
int32_t s3eDeviceRequestQuit(void);
int32_t s3eDeviceAbort(void);
int32_t s3eDeviceExit(void);

void s3eDebugOutputString(const char *text);
void s3eDebugPrint(int32_t channel, const char *text, int32_t color);
int32_t s3eDebugGetInt(uint32_t key);
int32_t s3eDebugIsDebuggerPresent(void);
void s3eDebugTraceLine(const char *text);
int32_t s3eDebugAssertShow(void);
int32_t s3eDebugErrorShow(uint32_t flags, const char *text);

int32_t s3eKeyboardRegister(uint32_t id, void *callback, void *user_data);
int32_t s3eKeyboardUnRegister(uint32_t id, void *callback);
int32_t s3eKeyboardUpdate(void);
int32_t s3eKeyboardGetState(uint32_t key);
int32_t s3eKeyboardAnyKey(void);
int32_t s3eKeyboardGetInt(uint32_t key);
int32_t s3eKeyboardSetInt(uint32_t key, int32_t value);
const char *s3eKeyboardGetDisplayName(uint32_t key);
void s3eKeyboardClearState(void);

int32_t s3ePointerRegister(uint32_t id, void *callback, void *user_data);
int32_t s3ePointerUnRegister(uint32_t id, void *callback);
int32_t s3ePointerUpdate(void);
int32_t s3ePointerGetInt(uint32_t key);
int32_t s3ePointerSetInt(uint32_t key, int32_t value);
int32_t s3ePointerGetState(uint32_t button);
int32_t s3ePointerGetX(void);
int32_t s3ePointerGetY(void);
int32_t s3ePointerGetTouchState(uint32_t touch_id);
int32_t s3ePointerGetTouchX(uint32_t touch_id);
int32_t s3ePointerGetTouchY(uint32_t touch_id);
int32_t s3ePointerGetPressure(uint32_t button);
int32_t s3ePointerGetTouchPressure(uint32_t touch_id);
int32_t s3ePointerGetError(void);
const char *s3ePointerGetErrorString(void);

int32_t s3eAccelerometerStart(void);
int32_t s3eAccelerometerStop(void);
int32_t s3eAccelerometerGetX(void);
int32_t s3eAccelerometerGetY(void);
int32_t s3eAccelerometerGetZ(void);
int32_t s3eAccelerometerGetInt(uint32_t key);
int32_t s3eVideoGetInt(uint32_t key);
int32_t s3eVideoPlay(const char *filename, uint32_t repeat);
int32_t s3eVideoStop(void);
int32_t s3eVideoResume(void);

int32_t s3eAudioIsPlaying(void);
int32_t s3eAudioSetInt(uint32_t key, int32_t value);
int32_t s3eAudioGetInt(uint32_t key);
int32_t s3eAudioPlay(const char *filename, uint32_t repeat);
int32_t s3eAudioPlayFromBuffer(const void *buffer, uint32_t size, uint32_t repeat);
int32_t s3eAudioStop(void);
int32_t s3eAudioPause(void);
int32_t s3eAudioResume(void);
int32_t s3eAudioRegister(uint32_t id, void *callback, void *user_data);

int32_t s3eSoundGetFreeChannel(void);
int32_t s3eSoundSetInt(uint32_t key, int32_t value);
int32_t s3eSoundGetInt(uint32_t key);
int32_t s3eSoundChannelRegister(uint32_t id, void *callback, void *user_data);
int32_t s3eSoundChannelUnRegister(uint32_t id, void *callback);
int32_t s3eSoundChannelPlay(int32_t channel, const void *data, uint32_t size, uint32_t repeat);
int32_t s3eSoundChannelStop(int32_t channel);
int32_t s3eSoundChannelPause(int32_t channel);
int32_t s3eSoundChannelResume(int32_t channel);
int32_t s3eSoundChannelSetInt(int32_t channel, uint32_t key, int32_t value);
int32_t s3eSoundChannelGetInt(int32_t channel, uint32_t key);

uint32_t s3eInetHtonl(uint32_t value);
uint32_t s3eInetNtohl(uint32_t value);
uint16_t s3eInetHtons(uint16_t value);
uint16_t s3eInetNtohs(uint16_t value);
int32_t s3eInetAton(const char *address, uint32_t *out);
const char *s3eInetNtoa(uint32_t address);
const char *s3eInetToString(uint32_t address);
int32_t s3eInetLookup(const char *hostname, uint32_t *out, void *callback, void *user_data);
int32_t s3eInetLookupCancel(void *lookup);
void *s3eSocketCreate(uint32_t type, uint32_t protocol, uint32_t flags);
int32_t s3eSocketClose(void *socket);
int32_t s3eSocketBind(void *socket, const void *address, uint16_t port);
int32_t s3eSocketListen(void *socket, int32_t backlog);
void *s3eSocketAccept(void *socket, void *address);
int32_t s3eSocketConnect(void *socket, const void *address, uint16_t port);
int32_t s3eSocketSend(void *socket, const void *buffer, uint32_t length, uint32_t flags);
int32_t s3eSocketSendTo(void *socket, const void *buffer, uint32_t length, uint32_t flags,
                        const void *address, uint16_t port);
int32_t s3eSocketRecv(void *socket, void *buffer, uint32_t length, uint32_t flags);
int32_t s3eSocketRecvFrom(void *socket, void *buffer, uint32_t length, uint32_t flags,
                          void *address);
int32_t s3eSocketReadable(void *socket);
int32_t s3eSocketWritable(void *socket);
int32_t s3eSocketGetInt(void *socket, uint32_t key);
int32_t s3eSocketGetError(void);
const char *s3eSocketGetString(uint32_t key);
int32_t s3eSocketGetLocalName(void *socket, void *address);
int32_t s3eSocketGetPeerName(void *socket, void *address);

int32_t s3eMemoryGetInt(uint32_t key);
int32_t s3eMemorySetInt(uint32_t key, int32_t value);
int32_t s3eMemorySetUserMemMgr(void *mgr);
int32_t s3eMemoryGetUserMemMgr(void *out);
int32_t s3eMemoryHeapCreate(uint32_t heap_index);
int32_t s3eMemoryHeapDestroy(uint32_t heap_index);
void *s3eMemoryHeapAddress(uint32_t heap_index);
int32_t s3eMemoryGetError(void);
const char *s3eMemoryGetErrorString(void);

int32_t s3eSurfaceRegister(uint32_t id, void *callback, void *user_data);
int32_t s3eSurfaceUnRegister(uint32_t id, void *callback);
int32_t s3eSurfaceGetInt(uint32_t key);
void *s3eSurfacePtr(void);
int32_t s3eSurfaceSetup(void);
int32_t s3eSurfaceShow(void);
int32_t s3eGLRegister(uint32_t id, void *callback, void *user_data);
int32_t s3eGLUnRegister(uint32_t id, void *callback);
int32_t s3eGLGetInt(uint32_t key);
void *s3eGLGetNativeWindow(void);

int32_t s3eConfigGetInt(const char *section, const char *key, int32_t *out);
int32_t s3eConfigGetString(const char *section, const char *key, char *out);

uintptr_t s3eReturn0(void);
uintptr_t s3eStub(void);
int32_t s3eTouchpadGetInt(uint32_t key);
int32_t s3eTouchpadRegister(uint32_t id, void *callback, void *user_data);
int32_t s3eTouchpadUnRegister(uint32_t id, void *callback);
int32_t isDeviceCallbackRegister(void *callback, void *user_data);
int32_t isDeviceCallbackUnregister(void *callback);
int32_t isDeviceSetTabletThreshold(int32_t threshold);
int32_t isDeviceGetDisplayType(void);
void *isDeviceGetExternalResources(void);
int32_t s3eExtGetHash(uint32_t hash, void *iface, uint32_t size);
uintptr_t s3e_trampoline_dispatch(uint32_t index);
int32_t s3eRegisterNoop(uint32_t id, void *callback, void *user_data);

#endif
