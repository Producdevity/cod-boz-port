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
#include <stddef.h>
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
typedef int32_t(S3E_SOFTFP *s3e_socket_callback_fn)(void *socket, void *system_data,
                                                    void *user_data);
typedef int32_t(S3E_SOFTFP *s3e_zeroconf_callback_fn)(void *search, void *system_data,
                                                      void *user_data);

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
#define S3E_ZERO_CONF_HASH 0x9f590656u
#define IS_AUDIO_UNIT_HASH 0x9b347d33u
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
    uint64_t due_ms;
    uint64_t sequence;
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

enum s3e_socket_address_type {
    S3E_SOCKET_ADDR_UNRESOLVED = 0,
    S3E_SOCKET_ADDR_IPV4 = 1 << 0,
    S3E_SOCKET_ADDR_IPV6 = 1 << 1,
    S3E_SOCKET_ADDR_LOCAL = 1 << 2,
};

enum s3e_socket_domain {
    S3E_SOCKET_UNSPEC = 0,
    S3E_SOCKET_LOCAL = 1,
    S3E_SOCKET_INET = 2,
    S3E_SOCKET_INET6 = 30,
};

enum s3e_socket_error {
    S3E_SOCKET_ERR_NONE = 0,
    S3E_SOCKET_ERR_PARAM = 1,
    S3E_SOCKET_ERR_TOO_MANY = 2,
    S3E_SOCKET_ERR_ALREADY_REG = 3,
    S3E_SOCKET_ERR_NOT_FOUND = 4,
    S3E_SOCKET_ERR_UNAVAIL = 5,
    S3E_SOCKET_ERR_UNSUPPORTED = 7,
    S3E_SOCKET_ERR_WOULDBLOCK = 1000,
    S3E_SOCKET_ERR_INPROGRESS = 1001,
    S3E_SOCKET_ERR_ALREADY = 1002,
    S3E_SOCKET_ERR_NOTSOCK = 1003,
    S3E_SOCKET_ERR_MSGSIZE = 1004,
    S3E_SOCKET_ERR_ADDRINUSE = 1005,
    S3E_SOCKET_ERR_NETDOWN = 1006,
    S3E_SOCKET_ERR_CONNRESET = 1007,
    S3E_SOCKET_ERR_ISCONN = 1008,
    S3E_SOCKET_ERR_NOTCONN = 1009,
    S3E_SOCKET_ERR_SHUTDOWN = 1010,
    S3E_SOCKET_ERR_TIMEDOUT = 1011,
    S3E_SOCKET_ERR_CONNREFUSED = 1012,
    S3E_SOCKET_ERR_UNKNOWN_HOST = 1013,
    S3E_SOCKET_ERR_NOTPERM = 1014,
};

enum s3e_socket_type {
    S3E_SOCKET_TCP = 0,
    S3E_SOCKET_UDP = 1,
    S3E_SOCKET_RAW = 2,
};

enum s3e_socket_property {
    S3E_SOCKET_MAX_SOCKETS = 0,
    S3E_SOCKET_NETWORK_AVAILABLE = 1,
    S3E_SOCKET_NETWORK_TYPE = 2,
    S3E_SOCKET_DOMAINNAME = 3,
    S3E_SOCKET_HOSTNAME = 4,
    S3E_SOCKET_HTTP_PROXY = 5,
    S3E_SOCKET_UDP_AVAILABLE = 6,
};

struct s3e_inet_address {
    int32_t type;
    char local;
    char local_address[128];
    char abstract;
    uint8_t padding_0[2];
    uint32_t ip_address;
    uint8_t ipv6_address[16];
    uint16_t port;
    char string[128];
    uint8_t padding_1[2];
    uint32_t next;
};

_Static_assert(offsetof(struct s3e_inet_address, ip_address) == 0x88,
               "s3eInetAddress IPv4 offset changed");
_Static_assert(offsetof(struct s3e_inet_address, ipv6_address) == 0x8c,
               "s3eInetAddress IPv6 offset changed");
_Static_assert(offsetof(struct s3e_inet_address, port) == 0x9c,
               "s3eInetAddress port offset changed");
_Static_assert(offsetof(struct s3e_inet_address, next) == 0x120,
               "s3eInetAddress next offset changed");
_Static_assert(sizeof(struct s3e_inet_address) == 0x124, "s3eInetAddress size changed");

struct s3e_zeroconf_found_data {
    void *service_id;
    uint32_t reserved_04;
    const char *name;
    const char *service_type;
    const char *domain;
    const char *host;
    uint16_t port;
    uint16_t txt_count;
    const char **txt_records;
    uint32_t ipv4_address;
    uint8_t reserved_24[0x14];
};

struct s3e_zeroconf_txt_update_data {
    void *service_id;
    uint16_t reserved_04;
    uint16_t txt_count;
    const char **txt_records;
};

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(struct s3e_zeroconf_found_data, name) == 0x08,
               "s3eZeroConf found name offset changed");
_Static_assert(offsetof(struct s3e_zeroconf_found_data, port) == 0x18,
               "s3eZeroConf found port offset changed");
_Static_assert(offsetof(struct s3e_zeroconf_found_data, txt_records) == 0x1c,
               "s3eZeroConf found TXT offset changed");
_Static_assert(offsetof(struct s3e_zeroconf_found_data, ipv4_address) == 0x20,
               "s3eZeroConf found IPv4 offset changed");
_Static_assert(sizeof(struct s3e_zeroconf_found_data) == 0x38,
               "s3eZeroConf found data size changed");
_Static_assert(offsetof(struct s3e_zeroconf_txt_update_data, txt_count) == 0x06,
               "s3eZeroConf TXT count offset changed");
_Static_assert(offsetof(struct s3e_zeroconf_txt_update_data, txt_records) == 0x08,
               "s3eZeroConf TXT pointer offset changed");
_Static_assert(sizeof(struct s3e_zeroconf_txt_update_data) == 0x0c,
               "s3eZeroConf TXT update size changed");
#endif

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
int32_t audio_unit_backend_set_callbacks(void *capture_callback, void *render_callback);
int32_t audio_unit_backend_start(void);
int32_t audio_unit_backend_stop(void);
void audio_unit_backend_shutdown(void);
void dispatch_due_timers(void);
void s3e_socket_pump(void);
void s3e_socket_shutdown(void);
bool s3e_multiplayer_server_enabled(void);
bool s3e_multiplayer_proxy_enabled(void);
/* The configured-server result is borrowed and remains valid until the config is reloaded. */
const char *s3e_multiplayer_resolve_hostname(const char *hostname);
void s3e_memory_shutdown(void);
void s3e_zero_conf_pump(void);
void s3e_zero_conf_shutdown(void);
void s3e_zero_conf_process_packet(const uint8_t *packet, size_t packet_size,
                                  const struct sockaddr_in *source);
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
int32_t s3eTimerSetTimer(uint32_t period_ms, void *callback, void *user_data);
int32_t s3eTimerCancelTimer(void *callback, void *user_data);
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
bool s3e_device_id_load_or_create(const char *root, char output[33]);
const char *s3e_device_id_get(void);
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
void audio_pump(void);

int32_t s3eSoundGetFreeChannel(void);
int32_t s3eSoundSetInt(uint32_t key, int32_t value);
int32_t s3eSoundGetInt(uint32_t key);
int32_t s3eSoundChannelRegister(int32_t channel, uint32_t callback_type, void *callback,
                                void *user_data);
int32_t s3eSoundChannelUnRegister(int32_t channel, uint32_t callback_type);
int32_t s3eSoundChannelPlay(int32_t channel, const void *data, uint32_t size, uint32_t repeat,
                            uint32_t loop_index);
int32_t s3eSoundChannelStop(int32_t channel);
int32_t s3eSoundChannelPause(int32_t channel);
int32_t s3eSoundChannelResume(int32_t channel);
int32_t s3eSoundChannelSetInt(int32_t channel, uint32_t key, int32_t value);
int32_t s3eSoundChannelGetInt(int32_t channel, uint32_t key);

uint32_t s3eInetHtonl(uint32_t value);
uint32_t s3eInetNtohl(uint32_t value);
uint16_t s3eInetHtons(uint16_t value);
uint16_t s3eInetNtohs(uint16_t value);
int32_t s3eInetAton(uint32_t *out, const char *address);
char *s3eInetNtoa(uint32_t address, char *buffer, int32_t length);
const char *s3eInetToString(const struct s3e_inet_address *address, int32_t include_port);
int32_t s3eInetLookup(const char *hostname, struct s3e_inet_address *address, void *callback,
                      void *user_data);
void s3eInetLookupCancel(void);
void *s3eSocketCreate(uint32_t type, int32_t domain);
int32_t s3eSocketClose(void *socket);
int32_t s3eSocketBind(void *socket, const struct s3e_inet_address *address, uint8_t reuse_address);
int32_t s3eSocketListen(void *socket, uint16_t backlog);
void *s3eSocketAccept(void *socket, struct s3e_inet_address *address,
                      s3e_socket_callback_fn callback, void *user_data);
int32_t s3eSocketConnect(void *socket, const struct s3e_inet_address *address,
                         s3e_socket_callback_fn callback, void *user_data);
int32_t s3eSocketSend(void *socket, const char *buffer, uint32_t length, int32_t flags);
int32_t s3eSocketSendTo(void *socket, const char *buffer, uint32_t length, int32_t flags,
                        const struct s3e_inet_address *address);
int32_t s3eSocketRecv(void *socket, char *buffer, uint32_t length, int32_t flags);
int32_t s3eSocketRecvFrom(void *socket, char *buffer, uint32_t length, int32_t flags,
                          struct s3e_inet_address *address);
int32_t s3eSocketReadable(void *socket, s3e_socket_callback_fn callback, void *user_data);
int32_t s3eSocketWritable(void *socket, s3e_socket_callback_fn callback, void *user_data);
int32_t s3eSocketGetInt(uint32_t key);
int32_t s3eSocketGetError(void);
const char *s3eSocketGetString(uint32_t key);
int32_t s3eSocketGetLocalName(void *socket, struct s3e_inet_address *address);
int32_t s3eSocketGetPeerName(void *socket, struct s3e_inet_address *address);

void *s3eZeroConfStartSearch(const char *service_type, const char *domain,
                             s3e_zeroconf_callback_fn found_callback,
                             s3e_zeroconf_callback_fn update_callback,
                             s3e_zeroconf_callback_fn lost_callback, void *user_data);
void s3eZeroConfStopSearch(void *search);
void *s3eZeroConfPublish(uint16_t port, const char *name, const char *service_type,
                         const char *domain, uint16_t txt_count, const char **txt_records);
int32_t s3eZeroConfUpdateTxtRecord(void *service, uint16_t txt_count, const char **txt_records);
int32_t s3eZeroConfUnpublish(void *service);

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
int32_t is_audio_unit_get_interface(void *iface, uint32_t size);
int32_t s3eExtGetHash(uint32_t hash, void *iface, uint32_t size);
uintptr_t s3e_trampoline_dispatch(uint32_t index);
int32_t s3eRegisterNoop(uint32_t id, void *callback, void *user_data);

#endif
