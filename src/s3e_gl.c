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

enum {
    GL_TEXTURE_2D_VALUE = 0x0de1,
    GL_BLEND_VALUE = 0x0be2,
    GL_ALPHA_TEST_VALUE = 0x0bc0,
    GL_DEPTH_TEST_VALUE = 0x0b71,
    GL_CULL_FACE_VALUE = 0x0b44,
    GL_FOG_VALUE = 0x0b60,
    GL_LIGHTING_VALUE = 0x0b50,
    GL_STENCIL_TEST_VALUE = 0x0b90,
    GL_VERTEX_ARRAY_VALUE = 0x8074,
    GL_NORMAL_ARRAY_VALUE = 0x8075,
    GL_COLOR_ARRAY_VALUE = 0x8076,
    GL_TEXTURE_COORD_ARRAY_VALUE = 0x8078,
    GL_TEXTURE_BINDING_2D_VALUE = 0x8069,
    GL_TEXTURE_MIN_FILTER_VALUE = 0x2801,
    GL_TEXTURE_MAG_FILTER_VALUE = 0x2800,
    GL_TEXTURE_WRAP_S_VALUE = 0x2802,
    GL_TEXTURE_WRAP_T_VALUE = 0x2803,
    GL_CLAMP_TO_EDGE_VALUE = 0x812f,
    GL_LINEAR_VALUE = 0x2601,
    GL_RGBA_VALUE = 0x1908,
    GL_UNSIGNED_BYTE_VALUE = 0x1401,
    GL_FLOAT_VALUE = 0x1406,
    GL_TRIANGLE_STRIP_VALUE = 0x0005,
    GL_PACK_ALIGNMENT_VALUE = 0x0d05,
    GL_UNPACK_ALIGNMENT_VALUE = 0x0cf5,
    GL_VIEWPORT_VALUE = 0x0ba2,
    GL_MATRIX_MODE_VALUE = 0x0ba0,
    GL_MODELVIEW_VALUE = 0x1700,
    GL_PROJECTION_VALUE = 0x1701,
    GL_CURRENT_COLOR_VALUE = 0x0b00,
    GL_COLOR_WRITEMASK_VALUE = 0x0c23,
    GL_ACTIVE_TEXTURE_VALUE = 0x84e0,
    GL_CLIENT_ACTIVE_TEXTURE_VALUE = 0x84e1,
    GL_TEXTURE0_VALUE = 0x84c0,
    GL_ARRAY_BUFFER_BINDING_VALUE = 0x8894,
    GL_ARRAY_BUFFER_VALUE = 0x8892,
    GL_TEXTURE_ENV_VALUE = 0x2300,
    GL_TEXTURE_ENV_MODE_VALUE = 0x2200,
    GL_REPLACE_VALUE = 0x1e01,
};

struct backbuffer_preserve {
    uint8_t *pixels;
    size_t pixels_size;
    GLsizei width;
    GLsizei height;
    GLsizei texture_width;
    GLsizei texture_height;
    GLuint texture;
    int texture_allocated;
    int ready;
};

static struct backbuffer_preserve g_backbuffer_preserve;

static GLsizei next_power_of_two(GLsizei value) {
    uint32_t out = 1;
    while (out < (uint32_t)value && out <= UINT32_MAX / 2) {
        out <<= 1;
    }
    return (GLsizei)out;
}

static int preserve_resize(GLsizei width, GLsizei height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    uint64_t required = (uint64_t)width * (uint64_t)height * 4u;
    if (required == 0 || required > SIZE_MAX) {
        return 0;
    }

    if (g_backbuffer_preserve.pixels_size != (size_t)required) {
        uint8_t *pixels = realloc(g_backbuffer_preserve.pixels, (size_t)required);
        if (!pixels) {
            g_backbuffer_preserve.ready = 0;
            free(g_backbuffer_preserve.pixels);
            memset(&g_backbuffer_preserve, 0, sizeof(g_backbuffer_preserve));
            return 0;
        }
        g_backbuffer_preserve.pixels = pixels;
        g_backbuffer_preserve.pixels_size = (size_t)required;
    }

    if (g_backbuffer_preserve.width != width || g_backbuffer_preserve.height != height) {
        g_backbuffer_preserve.texture_allocated = 0;
        g_backbuffer_preserve.texture_width = next_power_of_two(width);
        g_backbuffer_preserve.texture_height = next_power_of_two(height);
    }
    g_backbuffer_preserve.width = width;
    g_backbuffer_preserve.height = height;
    return 1;
}

static void preserve_capture_frame(void) {
    GLsizei width = g_native_window.width;
    GLsizei height = g_native_window.height;
    if (!preserve_resize(width, height)) {
        return;
    }

    void (*gl_read_pixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) =
        lookup_gl("glReadPixels");
    void (*gl_pixel_storei)(GLenum, GLint) = lookup_gl("glPixelStorei");
    void (*gl_get_integerv)(GLenum, GLint *) = lookup_gl("glGetIntegerv");
    if (!gl_read_pixels) {
        g_backbuffer_preserve.ready = 0;
        return;
    }

    GLint pack_alignment = 4;
    if (gl_get_integerv) {
        gl_get_integerv(GL_PACK_ALIGNMENT_VALUE, &pack_alignment);
    }
    if (gl_pixel_storei) {
        gl_pixel_storei(GL_PACK_ALIGNMENT_VALUE, 1);
    }
    gl_read_pixels(0, 0, width, height, GL_RGBA_VALUE, GL_UNSIGNED_BYTE_VALUE,
                   g_backbuffer_preserve.pixels);
    if (gl_pixel_storei) {
        gl_pixel_storei(GL_PACK_ALIGNMENT_VALUE, pack_alignment);
    }
    g_backbuffer_preserve.ready = 1;
}

static GLboolean gl_cap_enabled(GLenum cap, GLboolean (*gl_is_enabled)(GLenum)) {
    return gl_is_enabled ? gl_is_enabled(cap) : 0;
}

static void restore_gl_cap(GLenum cap, GLboolean enabled, void (*gl_enable)(GLenum),
                           void (*gl_disable)(GLenum)) {
    if (enabled) {
        gl_enable(cap);
    } else {
        gl_disable(cap);
    }
}

static int preserve_upload_texture(void) {
    void (*gl_gen_textures)(GLsizei, GLuint *) = lookup_gl("glGenTextures");
    void (*gl_bind_texture)(GLenum, GLuint) = lookup_gl("glBindTexture");
    void (*gl_tex_parameteri)(GLenum, GLenum, GLint) = lookup_gl("glTexParameteri");
    void (*gl_tex_image_2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                            const void *) = lookup_gl("glTexImage2D");
    void (*gl_tex_sub_image_2d)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                                const void *) = lookup_gl("glTexSubImage2D");
    if (!gl_gen_textures || !gl_bind_texture || !gl_tex_parameteri || !gl_tex_image_2d ||
        !gl_tex_sub_image_2d) {
        return 0;
    }

    if (!g_backbuffer_preserve.texture) {
        gl_gen_textures(1, &g_backbuffer_preserve.texture);
        if (!g_backbuffer_preserve.texture) {
            return 0;
        }
        g_backbuffer_preserve.texture_allocated = 0;
    }

    gl_bind_texture(GL_TEXTURE_2D_VALUE, g_backbuffer_preserve.texture);
    if (!g_backbuffer_preserve.texture_allocated) {
        gl_tex_parameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MIN_FILTER_VALUE, GL_LINEAR_VALUE);
        gl_tex_parameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_MAG_FILTER_VALUE, GL_LINEAR_VALUE);
        gl_tex_parameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_S_VALUE, GL_CLAMP_TO_EDGE_VALUE);
        gl_tex_parameteri(GL_TEXTURE_2D_VALUE, GL_TEXTURE_WRAP_T_VALUE, GL_CLAMP_TO_EDGE_VALUE);
        gl_tex_image_2d(GL_TEXTURE_2D_VALUE, 0, GL_RGBA_VALUE,
                        g_backbuffer_preserve.texture_width,
                        g_backbuffer_preserve.texture_height, 0, GL_RGBA_VALUE,
                        GL_UNSIGNED_BYTE_VALUE, NULL);
        g_backbuffer_preserve.texture_allocated = 1;
    }

    gl_tex_sub_image_2d(GL_TEXTURE_2D_VALUE, 0, 0, 0, g_backbuffer_preserve.width,
                        g_backbuffer_preserve.height, GL_RGBA_VALUE, GL_UNSIGNED_BYTE_VALUE,
                        g_backbuffer_preserve.pixels);
    return 1;
}

static void preserve_restore_frame(void) {
    if (!g_backbuffer_preserve.ready) {
        return;
    }

    void (*gl_enable)(GLenum) = lookup_gl("glEnable");
    void (*gl_disable)(GLenum) = lookup_gl("glDisable");
    GLboolean (*gl_is_enabled)(GLenum) = lookup_gl("glIsEnabled");
    void (*gl_get_integerv)(GLenum, GLint *) = lookup_gl("glGetIntegerv");
    void (*gl_get_floatv)(GLenum, GLfloat *) = lookup_gl("glGetFloatv");
    void (*gl_get_booleanv)(GLenum, GLboolean *) = lookup_gl("glGetBooleanv");
    void (*gl_color_mask)(GLboolean, GLboolean, GLboolean, GLboolean) =
        lookup_gl("glColorMask");
    void (*gl_viewport)(GLint, GLint, GLsizei, GLsizei) = lookup_gl("glViewport");
    void (*gl_matrix_mode)(GLenum) = lookup_gl("glMatrixMode");
    void (*gl_push_matrix)(void) = lookup_gl("glPushMatrix");
    void (*gl_pop_matrix)(void) = lookup_gl("glPopMatrix");
    void (*gl_load_identity)(void) = lookup_gl("glLoadIdentity");
    void (*gl_orthof)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat) =
        lookup_gl("glOrthof");
    void (*gl_color4f)(GLfloat, GLfloat, GLfloat, GLfloat) = lookup_gl("glColor4f");
    void (*gl_enable_client_state)(GLenum) = lookup_gl("glEnableClientState");
    void (*gl_disable_client_state)(GLenum) = lookup_gl("glDisableClientState");
    void (*gl_vertex_pointer)(GLint, GLenum, GLsizei, const void *) = lookup_gl("glVertexPointer");
    void (*gl_tex_coord_pointer)(GLint, GLenum, GLsizei, const void *) =
        lookup_gl("glTexCoordPointer");
    void (*gl_draw_arrays)(GLenum, GLint, GLsizei) = lookup_gl("glDrawArrays");
    void (*gl_bind_texture)(GLenum, GLuint) = lookup_gl("glBindTexture");
    void (*gl_pixel_storei)(GLenum, GLint) = lookup_gl("glPixelStorei");
    void (*gl_active_texture)(GLenum) = lookup_gl("glActiveTexture");
    void (*gl_client_active_texture)(GLenum) = lookup_gl("glClientActiveTexture");
    void (*gl_bind_buffer)(GLenum, GLuint) = lookup_gl("glBindBuffer");
    void (*gl_tex_envi)(GLenum, GLenum, GLint) = lookup_gl("glTexEnvi");
    void (*gl_get_tex_enviv)(GLenum, GLenum, GLint *) = lookup_gl("glGetTexEnviv");

    if (!gl_enable || !gl_disable || !gl_is_enabled || !gl_get_integerv || !gl_get_floatv ||
        !gl_get_booleanv || !gl_color_mask || !gl_viewport || !gl_matrix_mode ||
        !gl_push_matrix || !gl_pop_matrix || !gl_load_identity || !gl_orthof || !gl_color4f ||
        !gl_enable_client_state || !gl_disable_client_state || !gl_vertex_pointer ||
        !gl_tex_coord_pointer || !gl_draw_arrays || !gl_bind_texture) {
        return;
    }

    GLboolean texture_2d = gl_cap_enabled(GL_TEXTURE_2D_VALUE, gl_is_enabled);
    GLboolean blend = gl_cap_enabled(GL_BLEND_VALUE, gl_is_enabled);
    GLboolean alpha_test = gl_cap_enabled(GL_ALPHA_TEST_VALUE, gl_is_enabled);
    GLboolean depth_test = gl_cap_enabled(GL_DEPTH_TEST_VALUE, gl_is_enabled);
    GLboolean cull_face = gl_cap_enabled(GL_CULL_FACE_VALUE, gl_is_enabled);
    GLboolean fog = gl_cap_enabled(GL_FOG_VALUE, gl_is_enabled);
    GLboolean lighting = gl_cap_enabled(GL_LIGHTING_VALUE, gl_is_enabled);
    GLboolean stencil_test = gl_cap_enabled(GL_STENCIL_TEST_VALUE, gl_is_enabled);
    GLboolean scissor_test = gl_cap_enabled(GL_SCISSOR_TEST_VALUE, gl_is_enabled);
    GLboolean vertex_array = gl_cap_enabled(GL_VERTEX_ARRAY_VALUE, gl_is_enabled);
    GLboolean texcoord_array = gl_cap_enabled(GL_TEXTURE_COORD_ARRAY_VALUE, gl_is_enabled);
    GLboolean color_array = gl_cap_enabled(GL_COLOR_ARRAY_VALUE, gl_is_enabled);
    GLboolean normal_array = gl_cap_enabled(GL_NORMAL_ARRAY_VALUE, gl_is_enabled);

    GLint texture_binding = 0;
    GLint viewport[4] = {0, 0, g_backbuffer_preserve.width, g_backbuffer_preserve.height};
    GLint matrix_mode = GL_MODELVIEW_VALUE;
    GLint unpack_alignment = 4;
    GLint active_texture = GL_TEXTURE0_VALUE;
    GLint client_active_texture = GL_TEXTURE0_VALUE;
    GLint array_buffer = 0;
    GLint texture_env_mode = GL_REPLACE_VALUE;
    int texture_env_saved = gl_get_tex_enviv && gl_tex_envi;
    GLfloat color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    GLboolean color_mask[4] = {1, 1, 1, 1};

    gl_get_integerv(GL_TEXTURE_BINDING_2D_VALUE, &texture_binding);
    gl_get_integerv(GL_VIEWPORT_VALUE, viewport);
    gl_get_integerv(GL_MATRIX_MODE_VALUE, &matrix_mode);
    gl_get_integerv(GL_UNPACK_ALIGNMENT_VALUE, &unpack_alignment);
    gl_get_floatv(GL_CURRENT_COLOR_VALUE, color);
    gl_get_booleanv(GL_COLOR_WRITEMASK_VALUE, color_mask);
    if (gl_active_texture) {
        gl_get_integerv(GL_ACTIVE_TEXTURE_VALUE, &active_texture);
        gl_active_texture(GL_TEXTURE0_VALUE);
    }
    if (gl_client_active_texture) {
        gl_get_integerv(GL_CLIENT_ACTIVE_TEXTURE_VALUE, &client_active_texture);
        gl_client_active_texture(GL_TEXTURE0_VALUE);
    }
    if (gl_bind_buffer) {
        gl_get_integerv(GL_ARRAY_BUFFER_BINDING_VALUE, &array_buffer);
        gl_bind_buffer(GL_ARRAY_BUFFER_VALUE, 0);
    }
    if (texture_env_saved) {
        gl_get_tex_enviv(GL_TEXTURE_ENV_VALUE, GL_TEXTURE_ENV_MODE_VALUE, &texture_env_mode);
    }

    if (gl_pixel_storei) {
        gl_pixel_storei(GL_UNPACK_ALIGNMENT_VALUE, 1);
    }
    if (!preserve_upload_texture()) {
        if (gl_pixel_storei) {
            gl_pixel_storei(GL_UNPACK_ALIGNMENT_VALUE, unpack_alignment);
        }
        gl_bind_texture(GL_TEXTURE_2D_VALUE, (GLuint)texture_binding);
        if (gl_active_texture) {
            gl_active_texture((GLenum)active_texture);
        }
        if (gl_client_active_texture) {
            gl_client_active_texture((GLenum)client_active_texture);
        }
        if (gl_bind_buffer) {
            gl_bind_buffer(GL_ARRAY_BUFFER_VALUE, (GLuint)array_buffer);
        }
        return;
    }
    if (gl_pixel_storei) {
        gl_pixel_storei(GL_UNPACK_ALIGNMENT_VALUE, unpack_alignment);
    }

    const GLfloat tex_right =
        (GLfloat)g_backbuffer_preserve.width / (GLfloat)g_backbuffer_preserve.texture_width;
    const GLfloat tex_top =
        (GLfloat)g_backbuffer_preserve.height / (GLfloat)g_backbuffer_preserve.texture_height;
    const GLfloat vertices[] = {
        0.0f,
        0.0f,
        (GLfloat)g_backbuffer_preserve.width,
        0.0f,
        0.0f,
        (GLfloat)g_backbuffer_preserve.height,
        (GLfloat)g_backbuffer_preserve.width,
        (GLfloat)g_backbuffer_preserve.height,
    };
    const GLfloat texcoords[] = {
        0.0f, 0.0f, tex_right, 0.0f, 0.0f, tex_top, tex_right, tex_top,
    };

    gl_disable(GL_SCISSOR_TEST_VALUE);
    gl_disable(GL_BLEND_VALUE);
    gl_disable(GL_ALPHA_TEST_VALUE);
    gl_disable(GL_DEPTH_TEST_VALUE);
    gl_disable(GL_CULL_FACE_VALUE);
    gl_disable(GL_FOG_VALUE);
    gl_disable(GL_LIGHTING_VALUE);
    gl_disable(GL_STENCIL_TEST_VALUE);
    gl_enable(GL_TEXTURE_2D_VALUE);
    gl_color_mask(1, 1, 1, 1);
    gl_color4f(1.0f, 1.0f, 1.0f, 1.0f);
    if (texture_env_saved) {
        gl_tex_envi(GL_TEXTURE_ENV_VALUE, GL_TEXTURE_ENV_MODE_VALUE, GL_REPLACE_VALUE);
    }

    gl_viewport(0, 0, g_backbuffer_preserve.width, g_backbuffer_preserve.height);
    gl_matrix_mode(GL_PROJECTION_VALUE);
    gl_push_matrix();
    gl_load_identity();
    gl_orthof(0.0f, (GLfloat)g_backbuffer_preserve.width, 0.0f,
              (GLfloat)g_backbuffer_preserve.height, -1.0f, 1.0f);
    gl_matrix_mode(GL_MODELVIEW_VALUE);
    gl_push_matrix();
    gl_load_identity();

    gl_disable_client_state(GL_COLOR_ARRAY_VALUE);
    gl_disable_client_state(GL_NORMAL_ARRAY_VALUE);
    gl_enable_client_state(GL_VERTEX_ARRAY_VALUE);
    gl_enable_client_state(GL_TEXTURE_COORD_ARRAY_VALUE);
    gl_vertex_pointer(2, GL_FLOAT_VALUE, 0, vertices);
    gl_tex_coord_pointer(2, GL_FLOAT_VALUE, 0, texcoords);
    gl_draw_arrays(GL_TRIANGLE_STRIP_VALUE, 0, 4);

    restore_gl_cap(GL_VERTEX_ARRAY_VALUE, vertex_array, gl_enable_client_state,
                   gl_disable_client_state);
    restore_gl_cap(GL_TEXTURE_COORD_ARRAY_VALUE, texcoord_array, gl_enable_client_state,
                   gl_disable_client_state);
    restore_gl_cap(GL_COLOR_ARRAY_VALUE, color_array, gl_enable_client_state,
                   gl_disable_client_state);
    restore_gl_cap(GL_NORMAL_ARRAY_VALUE, normal_array, gl_enable_client_state,
                   gl_disable_client_state);

    gl_matrix_mode(GL_MODELVIEW_VALUE);
    gl_pop_matrix();
    gl_matrix_mode(GL_PROJECTION_VALUE);
    gl_pop_matrix();
    gl_matrix_mode((GLenum)matrix_mode);

    gl_viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    gl_color4f(color[0], color[1], color[2], color[3]);
    gl_color_mask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
    if (texture_env_saved) {
        gl_tex_envi(GL_TEXTURE_ENV_VALUE, GL_TEXTURE_ENV_MODE_VALUE, texture_env_mode);
    }
    gl_bind_texture(GL_TEXTURE_2D_VALUE, (GLuint)texture_binding);
    restore_gl_cap(GL_TEXTURE_2D_VALUE, texture_2d, gl_enable, gl_disable);
    restore_gl_cap(GL_BLEND_VALUE, blend, gl_enable, gl_disable);
    restore_gl_cap(GL_ALPHA_TEST_VALUE, alpha_test, gl_enable, gl_disable);
    restore_gl_cap(GL_DEPTH_TEST_VALUE, depth_test, gl_enable, gl_disable);
    restore_gl_cap(GL_CULL_FACE_VALUE, cull_face, gl_enable, gl_disable);
    restore_gl_cap(GL_FOG_VALUE, fog, gl_enable, gl_disable);
    restore_gl_cap(GL_LIGHTING_VALUE, lighting, gl_enable, gl_disable);
    restore_gl_cap(GL_STENCIL_TEST_VALUE, stencil_test, gl_enable, gl_disable);
    restore_gl_cap(GL_SCISSOR_TEST_VALUE, scissor_test, gl_enable, gl_disable);
    if (gl_active_texture) {
        gl_active_texture((GLenum)active_texture);
    }
    if (gl_client_active_texture) {
        gl_client_active_texture((GLenum)client_active_texture);
    }
    if (gl_bind_buffer) {
        gl_bind_buffer(GL_ARRAY_BUFFER_VALUE, (GLuint)array_buffer);
    }
}

static EGLBoolean host_eglChooseConfig(EGLDisplay display, const EGLint *attrib_list,
                                       EGLConfig *configs, EGLint config_size, EGLint *num_config) {
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
    preserve_capture_frame();
    frontend_cursor_gl_present();
    EGLBoolean result = real ? real(display, surface) : 0;
    if (result) {
        preserve_restore_frame();
    }
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
