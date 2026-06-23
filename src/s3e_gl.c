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
#define GL_WRAP_FLOAT6(name, t1, t2, t3, t4, t5, t6)                                               \
    static S3E_SOFTFP void host_##name(t1 a, t2 b, t3 c, t4 d, t5 e, t6 f) {                       \
        void (*real)(t1, t2, t3, t4, t5, t6) = lookup_gl(#name);                                   \
        if (real)                                                                                  \
            real(a, b, c, d, e, f);                                                                \
    }

GL_WRAP_FLOAT2(glAlphaFunc, GLenum, GLfloat)
GL_WRAP_FLOAT4(glBlendColor, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT4(glClearColor, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT1(glClearDepthf, GLfloat)
GL_WRAP_FLOAT4(glColor4f, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT2(glDepthRangef, GLfloat, GLfloat)
GL_WRAP_FLOAT2(glFogf, GLenum, GLfloat)
GL_WRAP_FLOAT6(glFrustumf, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT3(glLightf, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT1(glLineWidth, GLfloat)
GL_WRAP_FLOAT3(glMaterialf, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT4(glMultiTexCoord4f, GLenum, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT3(glNormal3f, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT6(glOrthof, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT1(glPointSize, GLfloat)
GL_WRAP_FLOAT2(glPolygonOffset, GLfloat, GLfloat)
GL_WRAP_FLOAT4(glRotatef, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT3(glScalef, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT3(glTexEnvf, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT3(glTranslatef, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT2(glUniform1f, GLint, GLfloat)
GL_WRAP_FLOAT4(glUniform4f, GLint, GLfloat, GLfloat, GLfloat)

static EGLBoolean host_eglChooseConfig(EGLDisplay display, const EGLint *attrib_list,
                                       EGLConfig *configs, EGLint config_size,
                                       EGLint *num_config) {
    EGLBoolean (*real)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) =
        lookup_egl("eglChooseConfig");
    if (!real) {
        return 0;
    }

    EGLint attrs[64];
    size_t out = 0;
    int found_surface_type = 0;
    if (attrib_list) {
        for (const EGLint *in = attrib_list; out + 3 < sizeof(attrs) / sizeof(attrs[0]); in += 2) {
            if (in[0] == EGL_NONE_VALUE) {
                break;
            }
            attrs[out++] = in[0];
            if (in[0] == EGL_SURFACE_TYPE_VALUE) {
                attrs[out++] = in[1] | EGL_SWAP_BEHAVIOR_PRESERVED_BIT_VALUE;
                found_surface_type = 1;
            } else {
                attrs[out++] = in[1];
            }
        }
    }
    if (!found_surface_type && out + 3 < sizeof(attrs) / sizeof(attrs[0])) {
        attrs[out++] = EGL_SURFACE_TYPE_VALUE;
        attrs[out++] = EGL_WINDOW_BIT_VALUE | EGL_SWAP_BEHAVIOR_PRESERVED_BIT_VALUE;
    }
    attrs[out++] = EGL_NONE_VALUE;

    EGLint preserved_count = 0;
    EGLint *count = num_config ? num_config : &preserved_count;
    EGLBoolean ok = real(display, attrs, configs, config_size, count);
    if (ok && *count > 0) {
        return ok;
    }
    return real(display, attrib_list, configs, config_size, num_config);
}

static EGLSurface host_eglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                              EGLNativeWindowType window,
                                              const EGLint *attrib_list) {
    EGLSurface (*real)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *) =
        lookup_egl("eglCreateWindowSurface");
    if (!real) {
        return NULL;
    }
    EGLSurface surface = real(display, config, window, attrib_list);
    if (surface) {
        EGLBoolean (*surface_attrib)(EGLDisplay, EGLSurface, EGLint, EGLint) =
            lookup_egl("eglSurfaceAttrib");
        if (surface_attrib) {
            surface_attrib(display, surface, EGL_SWAP_BEHAVIOR_VALUE, EGL_BUFFER_PRESERVED_VALUE);
        }
    }
    return surface;
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

    const int outline_radius = 7;
    const int outline_thickness = 3;
    const int inner_radius = 5;
    const int inner_thickness = 1;

    gl_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    cursor_clear_rect(x - outline_radius, y - outline_thickness / 2, outline_radius * 2 + 1,
                      outline_thickness, gl_scissor, gl_clear);
    cursor_clear_rect(x - outline_thickness / 2, y - outline_radius, outline_thickness,
                      outline_radius * 2 + 1, gl_scissor, gl_clear);
    gl_clear_color(0.1f, 0.55f, 1.0f, 1.0f);
    cursor_clear_rect(x - inner_radius, y - inner_thickness / 2, inner_radius * 2 + 1,
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
    return real ? real(display, surface) : 0;
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

void *s3e_host_resolve(const char *symbol) {
    if (strcmp(symbol, "glAlphaFunc") == 0)
        return host_glAlphaFunc;
    if (strcmp(symbol, "glBlendColor") == 0)
        return host_glBlendColor;
    if (strcmp(symbol, "glClearColor") == 0)
        return host_glClearColor;
    if (strcmp(symbol, "glClearDepthf") == 0)
        return host_glClearDepthf;
    if (strcmp(symbol, "glColor4f") == 0)
        return host_glColor4f;
    if (strcmp(symbol, "glDepthRangef") == 0)
        return host_glDepthRangef;
    if (strcmp(symbol, "glFogf") == 0)
        return host_glFogf;
    if (strcmp(symbol, "glFrustumf") == 0)
        return host_glFrustumf;
    if (strcmp(symbol, "glLightf") == 0)
        return host_glLightf;
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
    if (strcmp(symbol, "glPointSize") == 0)
        return host_glPointSize;
    if (strcmp(symbol, "glPolygonOffset") == 0)
        return host_glPolygonOffset;
    if (strcmp(symbol, "glRotatef") == 0)
        return host_glRotatef;
    if (strcmp(symbol, "glScalef") == 0)
        return host_glScalef;
    if (strcmp(symbol, "glTexEnvf") == 0)
        return host_glTexEnvf;
    if (strcmp(symbol, "glTranslatef") == 0)
        return host_glTranslatef;
    if (strcmp(symbol, "glUniform1f") == 0)
        return host_glUniform1f;
    if (strcmp(symbol, "glUniform4f") == 0)
        return host_glUniform4f;
    if (strcmp(symbol, "eglChooseConfig") == 0)
        return host_eglChooseConfig;
    if (strcmp(symbol, "eglCreateWindowSurface") == 0)
        return host_eglCreateWindowSurface;
    if (strcmp(symbol, "eglSwapBuffers") == 0)
        return host_eglSwapBuffers;
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
