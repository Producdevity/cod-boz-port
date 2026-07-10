#include "s3e_host_internal.h"

#define GL_WRAP_FLOAT1(name, t1)                                                                   \
    static S3E_SOFTFP void host_##name(t1 a) {                                                     \
        void (*real)(t1) = lookup_gl(#name);                                                       \
        if (real)                                                                                  \
            real(a);                                                                               \
    }
#define GL_WRAP_FLOAT2(name, t1, t2)                                                               \
    static S3E_SOFTFP void host_##name(t1 a, t2 b) {                                               \
        void (*real)(t1, t2) = lookup_gl(#name);                                                   \
        if (real)                                                                                  \
            real(a, b);                                                                            \
    }
#define GL_WRAP_FLOAT3(name, t1, t2, t3)                                                           \
    static S3E_SOFTFP void host_##name(t1 a, t2 b, t3 c) {                                         \
        void (*real)(t1, t2, t3) = lookup_gl(#name);                                               \
        if (real)                                                                                  \
            real(a, b, c);                                                                         \
    }
#define GL_WRAP_FLOAT4(name, t1, t2, t3, t4)                                                       \
    static S3E_SOFTFP void host_##name(t1 a, t2 b, t3 c, t4 d) {                                   \
        void (*real)(t1, t2, t3, t4) = lookup_gl(#name);                                           \
        if (real)                                                                                  \
            real(a, b, c, d);                                                                      \
    }
#define GL_WRAP_FLOAT5(name, t1, t2, t3, t4, t5)                                                   \
    static S3E_SOFTFP void host_##name(t1 a, t2 b, t3 c, t4 d, t5 e) {                             \
        void (*real)(t1, t2, t3, t4, t5) = lookup_gl(#name);                                       \
        if (real)                                                                                  \
            real(a, b, c, d, e);                                                                   \
    }
#define GL_WRAP_FLOAT6(name, t1, t2, t3, t4, t5, t6)                                               \
    static S3E_SOFTFP void host_##name(t1 a, t2 b, t3 c, t4 d, t5 e, t6 f) {                       \
        void (*real)(t1, t2, t3, t4, t5, t6) = lookup_gl(#name);                                   \
        if (real)                                                                                  \
            real(a, b, c, d, e, f);                                                                \
    }
#define GL_WRAP_FLOAT1_ALIAS(name, fallback, t1)                                                    \
    static S3E_SOFTFP void host_##name(t1 a) {                                                     \
        void (*real)(t1) = lookup_gl(#name);                                                       \
        if (!real)                                                                                 \
            real = lookup_gl(#fallback);                                                           \
        if (real)                                                                                  \
            real(a);                                                                               \
    }
#define GL_WRAP_FLOAT2_ALIAS(name, fallback, t1, t2)                                                \
    static S3E_SOFTFP void host_##name(t1 a, t2 b) {                                               \
        void (*real)(t1, t2) = lookup_gl(#name);                                                   \
        if (!real)                                                                                 \
            real = lookup_gl(#fallback);                                                           \
        if (real)                                                                                  \
            real(a, b);                                                                            \
    }
#define GL_WRAP_FLOAT6_ALIAS(name, fallback, t1, t2, t3, t4, t5, t6)                                \
    static S3E_SOFTFP void host_##name(t1 a, t2 b, t3 c, t4 d, t5 e, t6 f) {                       \
        void (*real)(t1, t2, t3, t4, t5, t6) = lookup_gl(#name);                                   \
        if (!real)                                                                                 \
            real = lookup_gl(#fallback);                                                           \
        if (real)                                                                                  \
            real(a, b, c, d, e, f);                                                                \
    }

enum {
    FRAME_INTERVAL_US = 16667,
    FRAME_RESET_US = FRAME_INTERVAL_US * 4,
};

static void sleep_until_us(uint64_t target_us) {
    for (;;) {
        uint64_t now = monotonic_us();
        if (now >= target_us) {
            return;
        }
        uint64_t remaining = target_us - now;
        struct timespec req = {
            .tv_sec = (time_t)(remaining / 1000000u),
            .tv_nsec = (long)(remaining % 1000000u) * 1000L,
        };
        while (nanosleep(&req, &req) != 0 && errno == EINTR) {
        }
    }
}

static void pace_frame(void) {
    static uint64_t next_frame_us;
    uint64_t now = monotonic_us();

    if (!next_frame_us || now > next_frame_us + FRAME_RESET_US) {
        next_frame_us = now + FRAME_INTERVAL_US;
    }

    sleep_until_us(next_frame_us);
    next_frame_us += FRAME_INTERVAL_US;
}

GL_WRAP_FLOAT2(glAlphaFunc, GLenum, GLfloat)
GL_WRAP_FLOAT4(glBlendColor, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT4(glClearColor, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT1(glClearDepthf, GLfloat)
GL_WRAP_FLOAT1_ALIAS(glClearDepthfOES, glClearDepthf, GLfloat)
GL_WRAP_FLOAT4(glColor4f, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT2(glDepthRangef, GLfloat, GLfloat)
GL_WRAP_FLOAT2_ALIAS(glDepthRangefOES, glDepthRangef, GLfloat, GLfloat)
GL_WRAP_FLOAT5(glDrawTexfOES, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT2(glFogf, GLenum, GLfloat)
GL_WRAP_FLOAT6(glFrustumf, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT6_ALIAS(glFrustumfOES, glFrustumf, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat,
                     GLfloat)
GL_WRAP_FLOAT2(glLightModelf, GLenum, GLfloat)
GL_WRAP_FLOAT3(glLightf, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT1(glLineWidth, GLfloat)
GL_WRAP_FLOAT3(glMaterialf, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT5(glMultiTexCoord4f, GLenum, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT3(glNormal3f, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT6(glOrthof, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT6_ALIAS(glOrthofOES, glOrthof, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT2(glPointParameterf, GLenum, GLfloat)
GL_WRAP_FLOAT1(glPointSize, GLfloat)
GL_WRAP_FLOAT2(glPolygonOffset, GLfloat, GLfloat)
GL_WRAP_FLOAT4(glRotatef, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT2(glSampleCoverage, GLfloat, GLboolean)
GL_WRAP_FLOAT3(glScalef, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT3(glTexEnvf, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT3(glTexGenfOES, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT3(glTexParameterf, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT3(glTranslatef, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT2(glUniform1f, GLint, GLfloat)
GL_WRAP_FLOAT3(glUniform2f, GLint, GLfloat, GLfloat)
GL_WRAP_FLOAT4(glUniform3f, GLint, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT5(glUniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT5(glVertexAttrib4f, GLuint, GLfloat, GLfloat, GLfloat, GLfloat)

static void *resolve_wrapped_host_proc(const char *symbol);

static void *host_eglGetProcAddress(const char *procname) {
    if (!procname) {
        return NULL;
    }
    void *wrapped = resolve_wrapped_host_proc(procname);
    if (wrapped) {
        return wrapped;
    }

    void *(*real)(const char *) = lookup_egl("eglGetProcAddress");
    return real ? real(procname) : NULL;
}

static void cursor_clear_rect(GLint x, GLint y, GLsizei w, GLsizei h,
                              void (*gl_scissor)(GLint, GLint, GLsizei, GLsizei),
                              void (*gl_clear)(GLbitfield)) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > g_native_window.width) {
        w = g_native_window.width - x;
    }
    if (y + h > g_native_window.height) {
        h = g_native_window.height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    GLint gl_y = (GLint)g_native_window.height - y - h;
    gl_scissor(x, gl_y, w, h);
    gl_clear(GL_COLOR_BUFFER_BIT_VALUE);
}

static void frontend_cursor_gl_present(void) {
    if (!g_cursor_active) {
        return;
    }
    void (*gl_disable)(GLenum) = lookup_gl("glDisable");
    void (*gl_enable)(GLenum) = lookup_gl("glEnable");
    GLboolean (*gl_is_enabled)(GLenum) = lookup_gl("glIsEnabled");
    void (*gl_scissor)(GLint, GLint, GLsizei, GLsizei) = lookup_gl("glScissor");
    void (*gl_clear_color)(GLfloat, GLfloat, GLfloat, GLfloat) = lookup_gl("glClearColor");
    void (*gl_clear)(GLbitfield) = lookup_gl("glClear");
    void (*gl_get_integerv)(GLenum, GLint *) = lookup_gl("glGetIntegerv");
    void (*gl_get_floatv)(GLenum, GLfloat *) = lookup_gl("glGetFloatv");
    if (!gl_disable || !gl_enable || !gl_is_enabled || !gl_scissor || !gl_clear_color ||
        !gl_clear || !gl_get_integerv || !gl_get_floatv) {
        return;
    }
    GLboolean scissor_was_enabled = gl_is_enabled(GL_SCISSOR_TEST_VALUE);
    GLint scissor_box[4] = {0, 0, 0, 0};
    GLfloat clear_color[4] = {0, 0, 0, 0};
    gl_get_integerv(GL_SCISSOR_BOX_VALUE, scissor_box);
    gl_get_floatv(GL_COLOR_CLEAR_VALUE, clear_color);
    if (!scissor_was_enabled) {
        gl_enable(GL_SCISSOR_TEST_VALUE);
    }
    int x = g_pointer_x;
    int y = g_pointer_y;

    const int outline_radius = 10;
    const int outline_thickness = 5;
    const int inner_radius = 8;
    const int inner_thickness = 3;

    gl_clear_color(0.0f, 0.0f, 0.0f, 1.0f); // outline
    cursor_clear_rect(x - outline_radius, y - outline_thickness / 2, outline_radius * 2 + 1,
                      outline_thickness, gl_scissor, gl_clear);
    cursor_clear_rect(x - outline_thickness / 2, y - outline_radius, outline_thickness,
                      outline_radius * 2 + 1, gl_scissor, gl_clear);
    gl_clear_color(0.1f, 0.55f, 1.0f, 1.0f);  // inner color
    cursor_clear_rect(x - inner_radius, y - inner_thickness/ 2, inner_radius * 2 + 1,
                      inner_thickness, gl_scissor, gl_clear);
    cursor_clear_rect(x - inner_thickness / 2, y - inner_radius, inner_thickness,
                      inner_radius * 2 + 1, gl_scissor, gl_clear);
    gl_clear_color(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    gl_scissor(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
    if (!scissor_was_enabled) {
        gl_disable(GL_SCISSOR_TEST_VALUE);
    }
}

static EGLBoolean host_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    EGLBoolean (*real)(EGLDisplay, EGLSurface) = lookup_egl("eglSwapBuffers");
    input_pump();
    dispatch_due_timers();
    frontend_cursor_gl_present();
    EGLBoolean result = real ? real(display, surface) : 0;
    pace_frame();
    return result;
}

struct host_symbol {
    const char *name;
    void *fn;
};

#define HOST(name) {#name, (void *)(uintptr_t)&name}

static const struct host_symbol HOST_SYMBOLS[] = {
    HOST(s3eMallocBase),
    HOST(s3eReallocBase),
    HOST(s3eFreeBase),
    HOST(s3eFileOpen),
    HOST(s3eFileClose),
    HOST(s3eFileRead),
    HOST(s3eFileWrite),
    HOST(s3eFileGetChar),
    HOST(s3eFilePutChar),
    HOST(s3eFileFlush),
    HOST(s3eFileSeek),
    HOST(s3eFileTell),
    HOST(s3eFileGetSize),
    HOST(s3eFileCheckExists),
    HOST(s3eFileGetError),
    HOST(s3eFileGetErrorString),
    HOST(s3eFileOpenFromMemory),
    HOST(s3eFileGetFileInt),
    HOST(s3eFileMakeDirectory),
    HOST(s3eFileDelete),
    HOST(s3eFileRename),
    HOST(s3eFileAddUserFileSys),
    HOST(s3eFileListDirectory),
    HOST(s3eFileListClose),
    HOST(s3eCompressionDecomp),
    HOST(s3eCompressionDecompInit),
    HOST(s3eCompressionDecompRead),
    HOST(s3eCompressionDecompFinal),
    HOST(s3eTimerGetUST),
    HOST(s3eTimerGetMs),
    HOST(s3eTimerGetInt),
    HOST(s3eTimerSetTimer),
    HOST(s3eTimerCancelTimer),
    HOST(s3eTimerGetUTC),
    HOST(s3eTimerGetLocaltimeOffset),
    HOST(s3eDeviceRegister),
    HOST(s3eDeviceUnRegister),
    HOST(s3eDeviceYield),
    HOST(s3eDeviceYieldUntilEvent),
    HOST(s3eDeviceCheckQuitRequest),
    HOST(s3eDeviceCheckPauseRequest),
    HOST(s3eDeviceGetInt),
    HOST(s3eDeviceGetString),
    HOST(s3eDeviceSetInt),
    HOST(s3eDeviceBacklightOn),
    HOST(s3eDeviceRequestQuit),
    HOST(s3eDeviceAbort),
    HOST(s3eDeviceExit),
    HOST(s3eDebugOutputString),
    HOST(s3eDebugPrint),
    HOST(s3eDebugGetInt),
    HOST(s3eDebugIsDebuggerPresent),
    HOST(s3eDebugTraceLine),
    HOST(s3eDebugAssertShow),
    HOST(s3eDebugErrorShow),
    HOST(s3eKeyboardRegister),
    HOST(s3eKeyboardUnRegister),
    HOST(s3eKeyboardUpdate),
    HOST(s3eKeyboardGetState),
    HOST(s3eKeyboardAnyKey),
    HOST(s3eKeyboardGetInt),
    HOST(s3eKeyboardSetInt),
    HOST(s3eKeyboardGetDisplayName),
    HOST(s3eKeyboardClearState),
    HOST(s3ePointerRegister),
    HOST(s3ePointerUnRegister),
    HOST(s3ePointerUpdate),
    HOST(s3ePointerGetInt),
    HOST(s3ePointerSetInt),
    HOST(s3ePointerGetState),
    HOST(s3ePointerGetX),
    HOST(s3ePointerGetY),
    HOST(s3ePointerGetTouchState),
    HOST(s3ePointerGetTouchX),
    HOST(s3ePointerGetTouchY),
    HOST(s3ePointerGetPressure),
    HOST(s3ePointerGetTouchPressure),
    HOST(s3ePointerGetError),
    HOST(s3ePointerGetErrorString),
    HOST(s3eAccelerometerStart),
    HOST(s3eAccelerometerStop),
    HOST(s3eAccelerometerGetX),
    HOST(s3eAccelerometerGetY),
    HOST(s3eAccelerometerGetZ),
    HOST(s3eAccelerometerGetInt),
    HOST(s3eVideoGetInt),
    HOST(s3eVideoPlay),
    HOST(s3eVideoStop),
    HOST(s3eVideoResume),
    HOST(s3eAudioIsPlaying),
    HOST(s3eAudioSetInt),
    HOST(s3eAudioGetInt),
    HOST(s3eAudioPlay),
    HOST(s3eAudioPlayFromBuffer),
    HOST(s3eAudioStop),
    HOST(s3eAudioPause),
    HOST(s3eAudioResume),
    HOST(s3eAudioRegister),
    HOST(s3eSoundGetFreeChannel),
    HOST(s3eSoundSetInt),
    HOST(s3eSoundGetInt),
    HOST(s3eSoundChannelRegister),
    HOST(s3eSoundChannelUnRegister),
    HOST(s3eSoundChannelPlay),
    HOST(s3eSoundChannelStop),
    HOST(s3eSoundChannelPause),
    HOST(s3eSoundChannelResume),
    HOST(s3eSoundChannelSetInt),
    HOST(s3eSoundChannelGetInt),
    HOST(s3eInetHtonl),
    HOST(s3eInetNtohl),
    HOST(s3eInetHtons),
    HOST(s3eInetNtohs),
    HOST(s3eInetAton),
    HOST(s3eInetNtoa),
    HOST(s3eInetToString),
    HOST(s3eInetLookup),
    HOST(s3eInetLookupCancel),
    HOST(s3eSocketCreate),
    HOST(s3eSocketClose),
    HOST(s3eSocketBind),
    HOST(s3eSocketListen),
    HOST(s3eSocketAccept),
    HOST(s3eSocketConnect),
    HOST(s3eSocketSend),
    HOST(s3eSocketSendTo),
    HOST(s3eSocketRecv),
    HOST(s3eSocketRecvFrom),
    HOST(s3eSocketReadable),
    HOST(s3eSocketWritable),
    HOST(s3eSocketGetInt),
    HOST(s3eSocketGetError),
    HOST(s3eSocketGetString),
    HOST(s3eSocketGetLocalName),
    HOST(s3eSocketGetPeerName),
    HOST(s3eMemoryGetInt),
    HOST(s3eMemorySetInt),
    HOST(s3eMemorySetUserMemMgr),
    HOST(s3eMemoryGetUserMemMgr),
    HOST(s3eMemoryHeapCreate),
    HOST(s3eMemoryHeapDestroy),
    HOST(s3eMemoryHeapAddress),
    HOST(s3eMemoryGetError),
    HOST(s3eMemoryGetErrorString),
    HOST(s3eSurfaceRegister),
    HOST(s3eSurfaceUnRegister),
    HOST(s3eSurfaceGetInt),
    HOST(s3eSurfacePtr),
    HOST(s3eSurfaceSetup),
    HOST(s3eSurfaceShow),
    HOST(s3eGLRegister),
    HOST(s3eGLUnRegister),
    HOST(s3eGLGetInt),
    HOST(s3eGLGetNativeWindow),
    HOST(s3eConfigGetInt),
    HOST(s3eConfigGetString),
    HOST(s3eExtGetHash),
};

static void *resolve_wrapped_host_proc(const char *symbol) {
    if (strcmp(symbol, "glAlphaFunc") == 0)
        return host_glAlphaFunc;
    if (strcmp(symbol, "glBlendColor") == 0)
        return host_glBlendColor;
    if (strcmp(symbol, "glClearColor") == 0)
        return host_glClearColor;
    if (strcmp(symbol, "glClearDepthf") == 0)
        return host_glClearDepthf;
    if (strcmp(symbol, "glClearDepthfOES") == 0)
        return host_glClearDepthfOES;
    if (strcmp(symbol, "glColor4f") == 0)
        return host_glColor4f;
    if (strcmp(symbol, "glDepthRangef") == 0)
        return host_glDepthRangef;
    if (strcmp(symbol, "glDepthRangefOES") == 0)
        return host_glDepthRangefOES;
    if (strcmp(symbol, "glDrawTexfOES") == 0)
        return host_glDrawTexfOES;
    if (strcmp(symbol, "glFogf") == 0)
        return host_glFogf;
    if (strcmp(symbol, "glFrustumf") == 0)
        return host_glFrustumf;
    if (strcmp(symbol, "glFrustumfOES") == 0)
        return host_glFrustumfOES;
    if (strcmp(symbol, "glLightf") == 0)
        return host_glLightf;
    if (strcmp(symbol, "glLightModelf") == 0)
        return host_glLightModelf;
    if (strcmp(symbol, "glLineWidth") == 0)
        return host_glLineWidth;
    if (strcmp(symbol, "glMaterialf") == 0)
        return host_glMaterialf;
    if (strcmp(symbol, "glMultiTexCoord4f") == 0)
        return host_glMultiTexCoord4f;
    if (strcmp(symbol, "glNormal3f") == 0)
        return host_glNormal3f;
    if (strcmp(symbol, "glOrthof") == 0)
        return host_glOrthof;
    if (strcmp(symbol, "glOrthofOES") == 0)
        return host_glOrthofOES;
    if (strcmp(symbol, "glPointParameterf") == 0)
        return host_glPointParameterf;
    if (strcmp(symbol, "glPointSize") == 0)
        return host_glPointSize;
    if (strcmp(symbol, "glPolygonOffset") == 0)
        return host_glPolygonOffset;
    if (strcmp(symbol, "glRotatef") == 0)
        return host_glRotatef;
    if (strcmp(symbol, "glSampleCoverage") == 0)
        return host_glSampleCoverage;
    if (strcmp(symbol, "glScalef") == 0)
        return host_glScalef;
    if (strcmp(symbol, "glTexEnvf") == 0)
        return host_glTexEnvf;
    if (strcmp(symbol, "glTexGenfOES") == 0)
        return host_glTexGenfOES;
    if (strcmp(symbol, "glTexParameterf") == 0)
        return host_glTexParameterf;
    if (strcmp(symbol, "glTranslatef") == 0)
        return host_glTranslatef;
    if (strcmp(symbol, "glUniform1f") == 0)
        return host_glUniform1f;
    if (strcmp(symbol, "glUniform2f") == 0)
        return host_glUniform2f;
    if (strcmp(symbol, "glUniform3f") == 0)
        return host_glUniform3f;
    if (strcmp(symbol, "glUniform4f") == 0)
        return host_glUniform4f;
    if (strcmp(symbol, "glVertexAttrib4f") == 0)
        return host_glVertexAttrib4f;
    if (strcmp(symbol, "eglGetProcAddress") == 0)
        return host_eglGetProcAddress;
    if (strcmp(symbol, "eglSwapBuffers") == 0)
        return host_eglSwapBuffers;
    return NULL;
}

void *s3e_host_resolve(const char *symbol) {
    void *wrapped = resolve_wrapped_host_proc(symbol);
    if (wrapped) {
        return wrapped;
    }
    if (strncmp(symbol, "egl", 3) == 0) {
        void *addr = lookup_egl(symbol);
        return addr ? addr : make_stub(symbol);
    }
    if (strncmp(symbol, "gl", 2) == 0) {
        void *addr = lookup_gl(symbol);
        return addr ? addr : make_stub(symbol);
    }
    for (size_t i = 0; i < sizeof(HOST_SYMBOLS) / sizeof(HOST_SYMBOLS[0]); ++i) {
        if (strcmp(symbol, HOST_SYMBOLS[i].name) == 0) {
            return HOST_SYMBOLS[i].fn;
        }
    }
    if (strncmp(symbol, "s3e", 3) == 0) {
        return make_stub(symbol);
    }
    return NULL;
}
