/**********************************************************************
 * @file    screen_render.c
 * @brief   Renders a world-locked virtual screen in the glasses.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Implementation notes:
 *  - The quad is a unit square in the XY plane, centred on the origin.
 *    Where and how big the virtual screen is comes entirely from the
 *    model matrix the caller supplies, so resizing or moving it never
 *    touches the vertex buffer.
 *  - The desktop texture is uploaded with GL_BGRA / GL_RGBA to match the
 *    Wayland shm layout directly, avoiding a per-pixel swizzle on the
 *    CPU. GL_UNPACK_ROW_LENGTH handles a stride wider than the image.
 *  - A border is drawn around the quad so the screen's edges remain
 *    visible even where the desktop content is black (and therefore
 *    invisible on an additive display).
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "screen_render.h"

#include "xr_math.h"

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_W                (960)
#define WINDOW_H                (540)
#define GLASSES_DISPLAY_TAG     "VITURE"

/* Vertices for the textured quad: position (xyz) + uv. */
#define QUAD_FLOATS_PER_VERTEX  (5)
#define QUAD_VERTEX_COUNT       (6)

/* Border outline: position (xyz) only. */
#define BORDER_VERTEX_COUNT     (8)

/**********************************************************************
 * @brief  Renderer state.
 **********************************************************************/
struct screen_render_ctx
{
    SDL_Window *  p_window;
    SDL_GLContext gl_context;

    /* Textured-quad program. */
    GLuint        quad_program;
    GLuint        quad_vao;
    GLuint        quad_vbo;
    GLint         q_view;
    GLint         q_projection;
    GLint         q_model;
    GLint         q_texture;

    /* Border program (solid colour lines). */
    GLuint        line_program;
    GLuint        line_vao;
    GLuint        line_vbo;
    GLint         l_view;
    GLint         l_projection;
    GLint         l_model;

    GLint         l_color;

    GLuint        textures[SCREEN_MAX_PANELS];
    int           has_texture[SCREEN_MAX_PANELS];  /* Has valid pixels. */
    int           slot_used[SCREEN_MAX_PANELS];    /* Slot is claimed.  */

    GLuint        help_texture;
    int           has_help;
    int           help_visible;

    uint32_t      mod_mask;    /* Required modifiers (SCREEN_MOD_*). */

    int           width;
    int           height;
    int           logged_w;
    int           logged_h;
};

/* --- Shaders ------------------------------------------------------- */

static const char * const gp_quad_vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "out vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = u_projection * u_view * u_model\n"
    "                * vec4(a_position, 1.0);\n"
    "}\n";

static const char * const gp_quad_fs =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_texture;\n"
    "out vec4 frag_color;\n"
    "void main()\n"
    "{\n"
    "    frag_color = vec4(texture(u_texture, v_uv).rgb, 1.0);\n"
    "}\n";

static const char * const gp_line_vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_position;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "void main()\n"
    "{\n"
    "    gl_Position = u_projection * u_view * u_model\n"
    "                * vec4(a_position, 1.0);\n"
    "}\n";

static const char * const gp_line_fs =
    "#version 330 core\n"
    "uniform vec3 u_color;\n"
    "out vec4 frag_color;\n"
    "void main()\n"
    "{\n"
    "    frag_color = vec4(u_color, 1.0);\n"
    "}\n";

/**********************************************************************
 * @brief  Compile one shader stage.
 **********************************************************************/
static GLuint compile_shader(GLenum stage, const char * p_source)
{
    GLuint shader   = 0;
    GLint  compiled = GL_FALSE;
    char * p_log    = NULL;

    shader = glCreateShader(stage);
    if (0 == shader)
    {
        goto cleanup;
    }

    glShaderSource(shader, 1, &p_source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if (GL_TRUE != compiled)
    {
        p_log = (char *)calloc(1024U, sizeof(char));
        if (NULL != p_log)
        {
            glGetShaderInfoLog(shader, 1024, NULL, p_log);
            (void)fprintf(stderr,
                          "[render] shader compile failed:\n%s\n", p_log);
            free(p_log);
        }
        glDeleteShader(shader);
        shader = 0;
    }

cleanup:
    return shader;
}

/**********************************************************************
 * @brief  Link a vertex + fragment shader pair into a program.
 **********************************************************************/
static GLuint build_program(const char * p_vs, const char * p_fs)
{
    GLuint program  = 0;
    GLuint vertex   = 0;
    GLuint fragment = 0;
    GLint  linked   = GL_FALSE;

    vertex = compile_shader(GL_VERTEX_SHADER, p_vs);
    if (0 == vertex)
    {
        goto cleanup;
    }

    fragment = compile_shader(GL_FRAGMENT_SHADER, p_fs);
    if (0 == fragment)
    {
        goto cleanup;
    }

    program = glCreateProgram();
    if (0 == program)
    {
        goto cleanup;
    }

    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);

    if (GL_TRUE != linked)
    {
        (void)fprintf(stderr, "[render] shader link failed\n");
        glDeleteProgram(program);
        program = 0;
    }

cleanup:
    if (0 != vertex)
    {
        glDeleteShader(vertex);
    }
    if (0 != fragment)
    {
        glDeleteShader(fragment);
    }
    return program;
}

/**********************************************************************
 * @brief  Locate the display the VITURE glasses present as.
 **********************************************************************/
int screen_render_find_glasses_display(void)
{
    int          result = SCREEN_DISPLAY_NOT_FOUND;
    int          count  = 0;
    int          index  = 0;
    const char * p_name = NULL;

    if (0 != SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        (void)fprintf(stderr, "[render] SDL video init failed: %s\n",
                      SDL_GetError());
        goto cleanup;
    }

    count = SDL_GetNumVideoDisplays();
    for (index = 0; index < count; index++)
    {
        p_name = SDL_GetDisplayName(index);
        if (NULL == p_name)
        {
            continue;
        }
        if (NULL != strstr(p_name, GLASSES_DISPLAY_TAG))
        {
            result = index;
        }
    }

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Create a GL window on the given display.
 **********************************************************************/
screen_render_ctx_t * screen_render_create(int display_index,
                                           int windowed)
{
    screen_render_ctx_t * p_ctx    = NULL;
    screen_render_ctx_t * p_result = NULL;
    uint32_t              flags    = 0U;
    int                   pos      = 0;

    /* Unit quad in the XY plane, centred on the origin. Two triangles.
     * UV origin is top-left, matching the captured desktop image. */
    static const float quad[QUAD_VERTEX_COUNT * QUAD_FLOATS_PER_VERTEX] =
    {
        -0.5F, -0.5F, 0.0F,  0.0F, 1.0F,
         0.5F, -0.5F, 0.0F,  1.0F, 1.0F,
         0.5F,  0.5F, 0.0F,  1.0F, 0.0F,

        -0.5F, -0.5F, 0.0F,  0.0F, 1.0F,
         0.5F,  0.5F, 0.0F,  1.0F, 0.0F,
        -0.5F,  0.5F, 0.0F,  0.0F, 0.0F
    };

    /* Border: four edges of the same unit quad, as GL_LINES. */
    static const float border[BORDER_VERTEX_COUNT * 3] =
    {
        -0.5F, -0.5F, 0.0F,   0.5F, -0.5F, 0.0F,
         0.5F, -0.5F, 0.0F,   0.5F,  0.5F, 0.0F,
         0.5F,  0.5F, 0.0F,  -0.5F,  0.5F, 0.0F,
        -0.5F,  0.5F, 0.0F,  -0.5F, -0.5F, 0.0F
    };

    p_ctx = (screen_render_ctx_t *)calloc(
                1U, sizeof(screen_render_ctx_t));
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    if (0 != SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        (void)fprintf(stderr, "[render] SDL video init failed: %s\n",
                      SDL_GetError());
        goto cleanup;
    }

    (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                              SDL_GL_CONTEXT_PROFILE_CORE);
    (void)SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    (void)SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    pos   = (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display_index);
    flags = SDL_WINDOW_OPENGL;
    if (0 == windowed)
    {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    p_ctx->p_window = SDL_CreateWindow("VITURE virtual screen", pos, pos,
                                       WINDOW_W, WINDOW_H, flags);
    if (NULL == p_ctx->p_window)
    {
        (void)fprintf(stderr, "[render] window create failed: %s\n",
                      SDL_GetError());
        goto cleanup;
    }

    p_ctx->gl_context = SDL_GL_CreateContext(p_ctx->p_window);
    if (NULL == p_ctx->gl_context)
    {
        (void)fprintf(stderr, "[render] GL context failed: %s\n",
                      SDL_GetError());
        goto cleanup;
    }

    (void)SDL_GL_SetSwapInterval(1);
    SDL_GL_GetDrawableSize(p_ctx->p_window, &p_ctx->width,
                           &p_ctx->height);
    (void)fprintf(stderr, "[render] window on display %d (%s)\n",
                  display_index,
                  (0 == windowed) ? "fullscreen" : "windowed");

    p_ctx->quad_program = build_program(gp_quad_vs, gp_quad_fs);
    p_ctx->line_program = build_program(gp_line_vs, gp_line_fs);
    if ((0 == p_ctx->quad_program) || (0 == p_ctx->line_program))
    {
        goto cleanup;
    }

    p_ctx->q_view       = glGetUniformLocation(p_ctx->quad_program,
                                               "u_view");
    p_ctx->q_projection = glGetUniformLocation(p_ctx->quad_program,
                                               "u_projection");
    p_ctx->q_model      = glGetUniformLocation(p_ctx->quad_program,
                                               "u_model");
    p_ctx->q_texture    = glGetUniformLocation(p_ctx->quad_program,
                                               "u_texture");

    p_ctx->l_view       = glGetUniformLocation(p_ctx->line_program,
                                               "u_view");
    p_ctx->l_projection = glGetUniformLocation(p_ctx->line_program,
                                               "u_projection");
    p_ctx->l_model      = glGetUniformLocation(p_ctx->line_program,
                                               "u_model");
    p_ctx->l_color      = glGetUniformLocation(p_ctx->line_program,
                                               "u_color");

    /* Textured quad. */
    glGenVertexArrays(1, &p_ctx->quad_vao);
    glGenBuffers(1, &p_ctx->quad_vbo);
    glBindVertexArray(p_ctx->quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, p_ctx->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(quad), quad,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          (GLsizei)(QUAD_FLOATS_PER_VERTEX
                                    * sizeof(float)), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)(QUAD_FLOATS_PER_VERTEX
                                    * sizeof(float)),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    /* Border outline. */
    glGenVertexArrays(1, &p_ctx->line_vao);
    glGenBuffers(1, &p_ctx->line_vbo);
    glBindVertexArray(p_ctx->line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, p_ctx->line_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(border), border,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          (GLsizei)(3 * sizeof(float)), (void *)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glLineWidth(2.0F);

    p_ctx->mod_mask = SCREEN_MOD_CTRL | SCREEN_MOD_ALT;

    p_result = p_ctx;
    p_ctx    = NULL;

cleanup:
    if (NULL != p_ctx)
    {
        screen_render_destroy(p_ctx);
    }
    return p_result;
}

/**********************************************************************
 * @brief  Current drawable aspect ratio.
 **********************************************************************/
float screen_render_aspect(screen_render_ctx_t * p_ctx)
{
    float aspect = 16.0F / 9.0F;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    SDL_GL_GetDrawableSize(p_ctx->p_window, &p_ctx->width,
                           &p_ctx->height);

    if (0 >= p_ctx->height)
    {
        goto cleanup;
    }

    aspect = (float)p_ctx->width / (float)p_ctx->height;

cleanup:
    return aspect;
}

/**********************************************************************
 * @brief  Acquire a free panel slot and its GL texture.
 **********************************************************************/
int screen_render_acquire_panel(screen_render_ctx_t * p_ctx)
{
    int    panel   = -1;
    int    slot    = 0;
    GLuint texture = 0;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    for (slot = 0; slot < SCREEN_MAX_PANELS; slot++)
    {
        if (0 == p_ctx->slot_used[slot])
        {
            panel = slot;
            break;
        }
    }
    if (0 > panel)
    {
        goto cleanup;   /* All slots in use. */
    }

    /* Reuse the slot's texture object if it already has one. */
    if (0 == p_ctx->textures[panel])
    {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        p_ctx->textures[panel] = texture;
    }

    p_ctx->slot_used[panel]   = 1;
    p_ctx->has_texture[panel] = 0;

cleanup:
    return panel;
}

/**********************************************************************
 * @brief  Release a panel slot so it stops drawing and can be reused.
 **********************************************************************/
void screen_render_release_panel(screen_render_ctx_t * p_ctx,
                                 int panel)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if ((0 > panel) || (panel >= SCREEN_MAX_PANELS))
    {
        goto cleanup;
    }

    /* Keep the texture object around for cheap reuse; just stop
     * treating the slot as active. */
    p_ctx->slot_used[panel]   = 0;
    p_ctx->has_texture[panel] = 0;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Upload a captured desktop frame into a panel's GL texture.
 **********************************************************************/
void screen_render_upload(screen_render_ctx_t * p_ctx, int panel,
                          const uint8_t * p_pixels, uint32_t width,
                          uint32_t height, uint32_t stride,
                          screen_pixel_format_t format, int y_invert)
{
    GLenum gl_format = GL_BGRA;

    if ((NULL == p_ctx) || (NULL == p_pixels))
    {
        goto cleanup;
    }
    if ((0 > panel) || (panel >= SCREEN_MAX_PANELS))
    {
        goto cleanup;
    }
    if (0 == p_ctx->slot_used[panel])
    {
        goto cleanup;
    }
    if ((0U == width) || (0U == height))
    {
        goto cleanup;
    }

    /* Match the Wayland shm byte order directly: no CPU swizzle. */
    if (SCREEN_PIXEL_RGBA == format)
    {
        gl_format = GL_RGBA;
    }
    else if (SCREEN_PIXEL_BGRA == format)
    {
        gl_format = GL_BGRA;
    }
    else
    {
        goto cleanup;
    }

    glBindTexture(GL_TEXTURE_2D, p_ctx->textures[panel]);

    /* The compositor's stride can be wider than the image itself. */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(stride / 4U));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width,
                 (GLsizei)height, 0, gl_format, GL_UNSIGNED_BYTE,
                 p_pixels);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    glBindTexture(GL_TEXTURE_2D, 0);

    /* y_invert is handled in the model matrix by the caller flipping
     * the quad; we simply record that the texture is valid. */
    (void)y_invert;
    p_ctx->has_texture[panel] = 1;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Upload the help-overlay image and show or hide it.
 **********************************************************************/
void screen_render_set_help(screen_render_ctx_t * p_ctx,
                            const uint8_t * p_pixels, uint32_t width,
                            uint32_t height, int visible)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    if ((NULL != p_pixels) && (0U != width) && (0U != height))
    {
        if (0 == p_ctx->help_texture)
        {
            glGenTextures(1, &p_ctx->help_texture);
            glBindTexture(GL_TEXTURE_2D, p_ctx->help_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_EDGE);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, p_ctx->help_texture);
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width,
                     (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     p_pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        p_ctx->has_help = 1;
    }

    p_ctx->help_visible = visible;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Draw one frame and present it.
 **********************************************************************/
void screen_render_frame(screen_render_ctx_t * p_ctx,
                         const float * p_view,
                         const float * p_projection,
                         const float * p_models, int selected,
                         const float * p_help_model, int draw_scene)
{
    int panel = 0;

    if ((NULL == p_ctx) || (NULL == p_view) ||
        (NULL == p_projection) || (NULL == p_models))
    {
        goto cleanup;
    }

    SDL_GL_GetDrawableSize(p_ctx->p_window, &p_ctx->width,
                           &p_ctx->height);

    if ((p_ctx->width != p_ctx->logged_w) ||
        (p_ctx->height != p_ctx->logged_h))
    {
        (void)fprintf(stderr, "[render] drawable %dx%d\n",
                      p_ctx->width, p_ctx->height);
        p_ctx->logged_w = p_ctx->width;
        p_ctx->logged_h = p_ctx->height;
    }

    glViewport(0, 0, p_ctx->width, p_ctx->height);

    /* Black is fully transparent on an additive combiner. */
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear((GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    if (0 != draw_scene)
    {
        for (panel = 0; panel < SCREEN_MAX_PANELS; panel++)
        {
            const float * p_model = &p_models[panel * MAT4_ELEMENTS];

            if ((0 == p_ctx->slot_used[panel]) ||
                (0 == p_ctx->has_texture[panel]))
            {
                continue;
            }

            glUseProgram(p_ctx->quad_program);
            glUniformMatrix4fv(p_ctx->q_view, 1, GL_FALSE, p_view);
            glUniformMatrix4fv(p_ctx->q_projection, 1, GL_FALSE,
                               p_projection);
            glUniformMatrix4fv(p_ctx->q_model, 1, GL_FALSE, p_model);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, p_ctx->textures[panel]);
            glUniform1i(p_ctx->q_texture, 0);

            glBindVertexArray(p_ctx->quad_vao);
            glDrawArrays(GL_TRIANGLES, 0, QUAD_VERTEX_COUNT);

            /* Border, so each screen's edges stay visible even where the
             * desktop content is black (invisible in the optics). The
             * selected panel is drawn amber instead of cyan. */
            glUseProgram(p_ctx->line_program);
            glUniformMatrix4fv(p_ctx->l_view, 1, GL_FALSE, p_view);
            glUniformMatrix4fv(p_ctx->l_projection, 1, GL_FALSE,
                               p_projection);
            glUniformMatrix4fv(p_ctx->l_model, 1, GL_FALSE, p_model);

            if (panel == selected)
            {
                glUniform3f(p_ctx->l_color, 1.0F, 0.65F, 0.0F);
            }
            else
            {
                glUniform3f(p_ctx->l_color, 0.0F, 0.75F, 0.9F);
            }

            glBindVertexArray(p_ctx->line_vao);
            glDrawArrays(GL_LINES, 0, BORDER_VERTEX_COUNT);
        }

        /* Help overlay: a textured quad above the wall. Same additive
         * rule - the black background is transparent, only the glyphs
         * glow - so no border is needed. Depth test off so it always
         * reads on top of any panel it might overlap. */
        if ((0 != p_ctx->help_visible) && (0 != p_ctx->has_help) &&
            (NULL != p_help_model))
        {
            glDisable(GL_DEPTH_TEST);

            glUseProgram(p_ctx->quad_program);
            glUniformMatrix4fv(p_ctx->q_view, 1, GL_FALSE, p_view);
            glUniformMatrix4fv(p_ctx->q_projection, 1, GL_FALSE,
                               p_projection);
            glUniformMatrix4fv(p_ctx->q_model, 1, GL_FALSE,
                               p_help_model);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, p_ctx->help_texture);
            glUniform1i(p_ctx->q_texture, 0);

            glBindVertexArray(p_ctx->quad_vao);
            glDrawArrays(GL_TRIANGLES, 0, QUAD_VERTEX_COUNT);

            glEnable(GL_DEPTH_TEST);
        }

        glBindVertexArray(0);
    }

    SDL_GL_SwapWindow(p_ctx->p_window);

cleanup:
    return;
}

/**********************************************************************
 * @brief  Set which modifiers must be held for controls to register.
 **********************************************************************/
void screen_render_set_modifiers(screen_render_ctx_t * p_ctx,
                                 uint32_t mask)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if (0U == mask)
    {
        goto cleanup;
    }

    p_ctx->mod_mask = mask;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Pump the window event queue into an input tick.
 **********************************************************************/
void screen_render_poll(screen_render_ctx_t * p_ctx,
                        screen_input_t * p_input)
{
    SDL_Event event = { 0 };

    if (NULL == p_input)
    {
        goto cleanup;
    }

    (void)memset(p_input, 0, sizeof(*p_input));

    while (0 != SDL_PollEvent(&event))
    {
        SDL_Scancode code = SDL_SCANCODE_UNKNOWN;
        SDL_Keymod   mod  = KMOD_NONE;
        uint32_t     held = 0U;
        uint32_t     mask = 0U;

        if (SDL_QUIT == event.type)
        {
            p_input->quit = 1;
            continue;
        }
        if (SDL_KEYDOWN != event.type)
        {
            continue;
        }

        code = event.key.keysym.scancode;
        mod  = event.key.keysym.mod;

        /* Every binding requires modifiers held down. The virtual screen
         * shows a live desktop the user is typing into, so a bare
         * keypress must never be stolen by the overlay. */
        if (0U != (mod & (SDL_Keymod)KMOD_CTRL))
        {
            held |= SCREEN_MOD_CTRL;
        }
        if (0U != (mod & (SDL_Keymod)KMOD_ALT))
        {
            held |= SCREEN_MOD_ALT;
        }
        if (0U != (mod & (SDL_Keymod)KMOD_SHIFT))
        {
            held |= SCREEN_MOD_SHIFT;
        }
        if (0U != (mod & (SDL_Keymod)KMOD_GUI))
        {
            held |= SCREEN_MOD_SUPER;
        }

        mask = (NULL != p_ctx) ? p_ctx->mod_mask
                               : (SCREEN_MOD_CTRL | SCREEN_MOD_ALT);

        if (mask != (held & mask))
        {
            continue;
        }

        /* Matched on SCANCODE, not keycode: with Shift held the keycode
         * for '=' becomes '+' and '[' becomes '{', which would silently
         * break those bindings. A scancode is the physical key and is
         * unaffected by modifiers or layout. */
        switch (code)
        {
            case SDL_SCANCODE_Q:
                p_input->quit = 1;
                break;
            case SDL_SCANCODE_R:
                p_input->recenter = 1;
                break;
            case SDL_SCANCODE_EQUALS:
                p_input->scale_step = 1;
                break;
            case SDL_SCANCODE_MINUS:
                p_input->scale_step = -1;
                break;
            case SDL_SCANCODE_RIGHTBRACKET:
                p_input->dist_step = 1;
                break;
            case SDL_SCANCODE_LEFTBRACKET:
                p_input->dist_step = -1;
                break;
            /* Spawn a new screen relative to the selected one. IJKL is
             * used rather than the arrow keys, which compositors and
             * window managers commonly grab for their own navigation. */
            case SDL_SCANCODE_I:
                p_input->spawn_dir = SCREEN_SPAWN_UP;
                break;
            case SDL_SCANCODE_K:
                p_input->spawn_dir = SCREEN_SPAWN_DOWN;
                break;
            case SDL_SCANCODE_J:
                p_input->spawn_dir = SCREEN_SPAWN_LEFT;
                break;
            case SDL_SCANCODE_L:
                p_input->spawn_dir = SCREEN_SPAWN_RIGHT;
                break;
            case SDL_SCANCODE_X:
                p_input->remove_sel = 1;
                break;
            case SDL_SCANCODE_TAB:
                p_input->select_next = 1;
                break;
            /* Angle the selected panel. */
            case SDL_SCANCODE_COMMA:
                p_input->yaw_step = -1;
                break;
            case SDL_SCANCODE_PERIOD:
                p_input->yaw_step = 1;
                break;
            case SDL_SCANCODE_SEMICOLON:
                p_input->pitch_step = -1;
                break;
            case SDL_SCANCODE_APOSTROPHE:
                p_input->pitch_step = 1;
                break;
            case SDL_SCANCODE_H:
                p_input->toggle_help = 1;
                break;
            default:
                /* Key not bound. */
                break;
        }
    }

cleanup:
    return;
}

/**********************************************************************
 * @brief  Destroy the renderer.
 **********************************************************************/
void screen_render_destroy(screen_render_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    if (NULL != p_ctx->gl_context)
    {
        {
            int panel = 0;

            for (panel = 0; panel < SCREEN_MAX_PANELS; panel++)
            {
                if (0 != p_ctx->textures[panel])
                {
                    glDeleteTextures(1, &p_ctx->textures[panel]);
                }
            }
            if (0 != p_ctx->help_texture)
            {
                glDeleteTextures(1, &p_ctx->help_texture);
            }
        }
        if (0 != p_ctx->quad_vbo)
        {
            glDeleteBuffers(1, &p_ctx->quad_vbo);
        }
        if (0 != p_ctx->line_vbo)
        {
            glDeleteBuffers(1, &p_ctx->line_vbo);
        }
        if (0 != p_ctx->quad_vao)
        {
            glDeleteVertexArrays(1, &p_ctx->quad_vao);
        }
        if (0 != p_ctx->line_vao)
        {
            glDeleteVertexArrays(1, &p_ctx->line_vao);
        }
        if (0 != p_ctx->quad_program)
        {
            glDeleteProgram(p_ctx->quad_program);
        }
        if (0 != p_ctx->line_program)
        {
            glDeleteProgram(p_ctx->line_program);
        }
        SDL_GL_DeleteContext(p_ctx->gl_context);
        p_ctx->gl_context = NULL;
    }

    if (NULL != p_ctx->p_window)
    {
        SDL_DestroyWindow(p_ctx->p_window);
        p_ctx->p_window = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    free(p_ctx);

cleanup:
    return;
}
