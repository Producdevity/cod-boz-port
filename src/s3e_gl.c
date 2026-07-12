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
#define GL_WRAP_FLOAT1_ALIAS(name, fallback, t1)                                                   \
    static S3E_SOFTFP void host_##name(t1 a) {                                                     \
        void (*real)(t1) = lookup_gl(#name);                                                       \
        if (!real)                                                                                 \
            real = lookup_gl(#fallback);                                                           \
        if (real)                                                                                  \
            real(a);                                                                               \
    }
#define GL_WRAP_FLOAT2_ALIAS(name, fallback, t1, t2)                                               \
    static S3E_SOFTFP void host_##name(t1 a, t2 b) {                                               \
        void (*real)(t1, t2) = lookup_gl(#name);                                                   \
        if (!real)                                                                                 \
            real = lookup_gl(#fallback);                                                           \
        if (real)                                                                                  \
            real(a, b);                                                                            \
    }
#define GL_WRAP_FLOAT6_ALIAS(name, fallback, t1, t2, t3, t4, t5, t6)                               \
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
    REFERENCE_SURFACE_WIDTH = 640,
    REFERENCE_SURFACE_HEIGHT = 480,
    GL_VIEWPORT_VALUE = 0x0ba2,
    GL_COLOR_WRITEMASK_VALUE = 0x0c23,
    GL_FRAMEBUFFER_VALUE = 0x8d40,
};

static GLuint g_bound_framebuffer;

static void driver_bind_framebuffer(GLuint framebuffer) {
    void (*real)(GLenum, GLuint) = lookup_gl("glBindFramebuffer");
    if (!real) {
        real = lookup_gl("glBindFramebufferOES");
    }
    if (real) {
        real(GL_FRAMEBUFFER_VALUE, framebuffer);
    }
}

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
GL_WRAP_FLOAT1(glPointSize, GLfloat)
GL_WRAP_FLOAT2(glPolygonOffset, GLfloat, GLfloat)
GL_WRAP_FLOAT4(glRotatef, GLfloat, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT3(glScalef, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT3(glTexEnvf, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT3(glTexGenfOES, GLenum, GLenum, GLfloat)
GL_WRAP_FLOAT3(glTranslatef, GLfloat, GLfloat, GLfloat)
GL_WRAP_FLOAT2(glUniform1f, GLint, GLfloat)
GL_WRAP_FLOAT5(glUniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat)

static void bind_framebuffer(GLenum target, GLuint framebuffer) {
    if (target == GL_FRAMEBUFFER_VALUE) {
        driver_bind_framebuffer(framebuffer);
        g_bound_framebuffer = framebuffer;
        return;
    }

    void (*real)(GLenum, GLuint) = lookup_gl("glBindFramebuffer");
    if (!real) {
        real = lookup_gl("glBindFramebufferOES");
    }
    if (real) {
        real(target, framebuffer);
    }
}

static S3E_SOFTFP void host_glBindFramebuffer(GLenum target, GLuint framebuffer) {
    bind_framebuffer(target, framebuffer);
}

static S3E_SOFTFP void host_glBindFramebufferOES(GLenum target, GLuint framebuffer) {
    bind_framebuffer(target, framebuffer);
}

static S3E_SOFTFP void host_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    void (*real)(GLint, GLint, GLsizei, GLsizei) = lookup_gl("glViewport");
    if (real) {
        if (!g_bound_framebuffer) {
            x += g_surface.x;
            y += g_surface.y;
        }
        real(x, y, width, height);
    }
}

static S3E_SOFTFP void host_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    void (*real)(GLint, GLint, GLsizei, GLsizei) = lookup_gl("glScissor");
    if (real) {
        if (!g_bound_framebuffer) {
            x += g_surface.x;
            y += g_surface.y;
        }
        real(x, y, width, height);
    }
}

static void map_drawable_rect_to_surface(GLint *rect) {
    GLint left = rect[0] > g_surface.x ? rect[0] : g_surface.x;
    GLint bottom = rect[1] > g_surface.y ? rect[1] : g_surface.y;
    GLint right = rect[0] + rect[2];
    GLint top = rect[1] + rect[3];
    GLint surface_right = g_surface.x + g_surface.width;
    GLint surface_top = g_surface.y + g_surface.height;
    if (right > surface_right) {
        right = surface_right;
    }
    if (top > surface_top) {
        top = surface_top;
    }
    if (left > surface_right) {
        left = surface_right;
    }
    if (bottom > surface_top) {
        bottom = surface_top;
    }
    rect[0] = left - g_surface.x;
    rect[1] = bottom - g_surface.y;
    rect[2] = right > left ? right - left : 0;
    rect[3] = top > bottom ? top - bottom : 0;
}

static S3E_SOFTFP void host_glGetIntegerv(GLenum name, GLint *values) {
    void (*real)(GLenum, GLint *) = lookup_gl("glGetIntegerv");
    if (!real) {
        return;
    }
    real(name, values);
    if (!g_bound_framebuffer && values &&
        (name == GL_VIEWPORT_VALUE || name == GL_SCISSOR_BOX_VALUE)) {
        map_drawable_rect_to_surface(values);
    }
}

static S3E_SOFTFP void host_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                                         GLenum format, GLenum type, void *pixels) {
    void (*real)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) =
        lookup_gl("glReadPixels");
    if (real) {
        if (!g_bound_framebuffer) {
            x += g_surface.x;
            y += g_surface.y;
        }
        real(x, y, width, height, format, type, pixels);
    }
}

static S3E_SOFTFP void host_glCopyTexImage2D(GLenum target, GLint level, GLenum internal_format,
                                             GLint x, GLint y, GLsizei width, GLsizei height,
                                             GLint border) {
    void (*real)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint) =
        lookup_gl("glCopyTexImage2D");
    if (real) {
        if (!g_bound_framebuffer) {
            x += g_surface.x;
            y += g_surface.y;
        }
        real(target, level, internal_format, x, y, width, height, border);
    }
}

static S3E_SOFTFP void host_glCopyTexSubImage2D(GLenum target, GLint level, GLint x_offset,
                                                GLint y_offset, GLint x, GLint y, GLsizei width,
                                                GLsizei height) {
    void (*real)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei) =
        lookup_gl("glCopyTexSubImage2D");
    if (real) {
        if (!g_bound_framebuffer) {
            x += g_surface.x;
            y += g_surface.y;
        }
        real(target, level, x_offset, y_offset, x, y, width, height);
    }
}

struct host_symbol {
    const char *name;
    void *fn;
};

static void *resolve_wrapped_host_proc(const char *symbol);

static void *host_eglGetProcAddress(const char *procname) {
    if (!procname) {
        return NULL;
    }
    void *wrapped = resolve_wrapped_host_proc(procname);
    if (wrapped) {
        return wrapped;
    }
    void *egl = egl_backend_resolve(procname);
    return egl ? egl : egl_backend_get_proc_address(procname);
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
    if (x + w > g_surface.width) {
        w = g_surface.width - x;
    }
    if (y + h > g_surface.height) {
        h = g_surface.height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    GLint gl_y = (GLint)g_surface.height - y - h;
    gl_scissor(g_surface.x + x, g_surface.y + gl_y, w, h);
    gl_clear(GL_COLOR_BUFFER_BIT_VALUE);
}

static int cursor_scaled_size(int size) {
    uint32_t width_scale = (uint32_t)g_surface.width * REFERENCE_SURFACE_HEIGHT;
    uint32_t height_scale = (uint32_t)g_surface.height * REFERENCE_SURFACE_WIDTH;
    uint32_t scale = width_scale < height_scale ? width_scale : height_scale;
    uint32_t reference_area = REFERENCE_SURFACE_WIDTH * REFERENCE_SURFACE_HEIGHT;
    int result = (int)(((uint64_t)size * scale + reference_area / 2u) / reference_area);
    return result > 0 ? result : 1;
}

static void clear_letterbox_rect(GLint x, GLint y, GLsizei width, GLsizei height,
                                 void (*gl_scissor)(GLint, GLint, GLsizei, GLsizei),
                                 void (*gl_clear)(GLbitfield)) {
    if (width > 0 && height > 0) {
        gl_scissor(x, y, width, height);
        gl_clear(GL_COLOR_BUFFER_BIT_VALUE);
    }
}

static void frontend_overlay_gl_present(void) {
    int has_letterbox = g_surface.x || g_surface.y;
    if (!has_letterbox && !g_cursor_active) {
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
    void (*gl_get_booleanv)(GLenum, GLboolean *) = lookup_gl("glGetBooleanv");
    void (*gl_color_mask)(GLboolean, GLboolean, GLboolean, GLboolean) = lookup_gl("glColorMask");
    if (!gl_disable || !gl_enable || !gl_is_enabled || !gl_scissor || !gl_clear_color ||
        !gl_clear || !gl_get_integerv || !gl_get_floatv || !gl_get_booleanv || !gl_color_mask) {
        return;
    }

    GLboolean scissor_was_enabled = gl_is_enabled(GL_SCISSOR_TEST_VALUE);
    GLint scissor_box[4] = {0, 0, 0, 0};
    GLfloat clear_color[4] = {0, 0, 0, 0};
    GLboolean color_mask[4] = {1, 1, 1, 1};
    gl_get_integerv(GL_SCISSOR_BOX_VALUE, scissor_box);
    gl_get_floatv(GL_COLOR_CLEAR_VALUE, clear_color);
    gl_get_booleanv(GL_COLOR_WRITEMASK_VALUE, color_mask);

    if (!scissor_was_enabled) {
        gl_enable(GL_SCISSOR_TEST_VALUE);
    }
    gl_color_mask(1, 1, 1, 1);
    gl_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    if (has_letterbox) {
        GLint right = g_surface.x + g_surface.width;
        GLint top = g_surface.y + g_surface.height;
        clear_letterbox_rect(0, 0, g_surface.x, g_native_window.height, gl_scissor, gl_clear);
        clear_letterbox_rect(right, 0, g_native_window.width - right, g_native_window.height,
                             gl_scissor, gl_clear);
        clear_letterbox_rect(g_surface.x, 0, g_surface.width, g_surface.y, gl_scissor, gl_clear);
        clear_letterbox_rect(g_surface.x, top, g_surface.width, g_native_window.height - top,
                             gl_scissor, gl_clear);
    }

    if (g_cursor_active) {
        int x = g_pointer_x;
        int y = g_pointer_y;
        const int outline_radius = cursor_scaled_size(10);
        const int outline_thickness = cursor_scaled_size(5);
        const int inner_radius = cursor_scaled_size(8);
        const int inner_thickness = cursor_scaled_size(3);

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
    }

    gl_clear_color(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    gl_color_mask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
    gl_scissor(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
    if (!scissor_was_enabled) {
        gl_disable(GL_SCISSOR_TEST_VALUE);
    }
}

static EGLBoolean host_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    input_pump();
    dispatch_due_timers();
    GLuint previous_framebuffer = g_bound_framebuffer;
    if (previous_framebuffer) {
        driver_bind_framebuffer(0);
    }
    frontend_overlay_gl_present();
    if (previous_framebuffer) {
        driver_bind_framebuffer(previous_framebuffer);
    }
    EGLBoolean result = egl_backend_swap_buffers(display, surface);
    pace_frame();
    return result;
}

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

#define WRAPPED(name) {#name, (void *)(uintptr_t)&host_##name}

static const struct host_symbol WRAPPED_SYMBOLS[] = {
    WRAPPED(glAlphaFunc),
    WRAPPED(glBindFramebuffer),
    WRAPPED(glBindFramebufferOES),
    WRAPPED(glBlendColor),
    WRAPPED(glClearColor),
    WRAPPED(glClearDepthf),
    WRAPPED(glClearDepthfOES),
    WRAPPED(glColor4f),
    WRAPPED(glCopyTexImage2D),
    WRAPPED(glCopyTexSubImage2D),
    WRAPPED(glDepthRangef),
    WRAPPED(glDepthRangefOES),
    WRAPPED(glDrawTexfOES),
    WRAPPED(glFogf),
    WRAPPED(glFrustumf),
    WRAPPED(glFrustumfOES),
    WRAPPED(glGetIntegerv),
    WRAPPED(glLightModelf),
    WRAPPED(glLightf),
    WRAPPED(glLineWidth),
    WRAPPED(glMaterialf),
    WRAPPED(glMultiTexCoord4f),
    WRAPPED(glNormal3f),
    WRAPPED(glOrthof),
    WRAPPED(glOrthofOES),
    WRAPPED(glPointSize),
    WRAPPED(glPolygonOffset),
    WRAPPED(glReadPixels),
    WRAPPED(glRotatef),
    WRAPPED(glScalef),
    WRAPPED(glScissor),
    WRAPPED(glTexEnvf),
    WRAPPED(glTexGenfOES),
    WRAPPED(glTranslatef),
    WRAPPED(glUniform1f),
    WRAPPED(glUniform4f),
    WRAPPED(glViewport),
    WRAPPED(eglGetProcAddress),
    WRAPPED(eglSwapBuffers),
};

static void *resolve_wrapped_host_proc(const char *symbol) {
    for (size_t i = 0; i < sizeof(WRAPPED_SYMBOLS) / sizeof(WRAPPED_SYMBOLS[0]); ++i) {
        if (strcmp(symbol, WRAPPED_SYMBOLS[i].name) == 0) {
            return WRAPPED_SYMBOLS[i].fn;
        }
    }
    return NULL;
}

void *s3e_host_resolve(const char *symbol) {
    void *wrapped = resolve_wrapped_host_proc(symbol);
    if (wrapped) {
        return wrapped;
    }
    if (strncmp(symbol, "egl", 3) == 0) {
        void *addr = egl_backend_resolve(symbol);
        if (!addr) {
            addr = lookup_egl(symbol);
        }
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
