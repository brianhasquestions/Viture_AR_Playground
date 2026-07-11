/**********************************************************************
 * @file    ar_render.c
 * @brief   OpenGL renderer for an optical see-through AR overlay.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Implementation notes:
 *  - OpenGL 3.3 core. On Mesa every core entry point is exported by
 *    libGL directly, so GL_GLEXT_PROTOTYPES is enough and no loader
 *    library (GLEW/glad) is needed.
 *  - The whole scene is static world-space geometry baked into a single
 *    vertex buffer at start-up and drawn with one glDrawArrays call.
 *    Nothing moves in world space: the overlay appears anchored in the
 *    room precisely because only the *view* matrix changes as the head
 *    moves. That is the entire trick behind world-locking.
 *  - Everything is GL_LINES on a black clear colour, because the
 *    combiner is additive (see ar_render.h).
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "ar_render.h"

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each vertex is position (xyz) + colour (rgb). */
#define FLOATS_PER_VERTEX   (6)

/* Ground grid: half-extent in cells, and cell size in metres. */
#define GRID_HALF_CELLS     (8)
#define GRID_CELL_METRES    (0.25F)

/* Height of the grid below the tracking origin, in metres. Roughly */
/* desk height when the origin is at eye level.                     */
#define GRID_Y_METRES       (-0.60F)

/* The world-locked cube: half-size, and how far in front of the    */
/* origin it floats. -Z is forward in the OpenGL convention.        */
#define CUBE_HALF_METRES    (0.15F)
#define CUBE_Z_METRES       (-1.20F)

/* Length of each axis gizmo arm, in metres. */
#define AXIS_METRES         (0.30F)

/* Windowed-mode preview size. */
#define WINDOW_W            (960)
#define WINDOW_H            (540)

/* Substring identifying the glasses among the attached displays. */
#define GLASSES_DISPLAY_TAG "VITURE"

/**********************************************************************
 * @brief  Renderer state.
 **********************************************************************/
struct ar_render_ctx
{
    SDL_Window *  p_window;
    SDL_GLContext gl_context;

    GLuint        program;
    GLuint        vao;
    GLuint        vbo;
    GLint         u_view;
    GLint         u_projection;

    GLsizei       vertex_count;
    int           width;
    int           height;
    int           logged_w;     /* Last drawable size reported, so a */
    int           logged_h;     /* late fullscreen resize is visible. */
};

/* --- Shaders --------------------------------------------------------
 * Deliberately minimal: transform the world-space vertex by view and
 * projection, and pass the colour straight through. No lighting: on an
 * additive display, brightness is the only channel that matters.
 */
static const char * const gp_vertex_shader_src =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec3 a_color;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "out vec3 v_color;\n"
    "void main()\n"
    "{\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_projection * u_view * vec4(a_position, 1.0);\n"
    "}\n";

static const char * const gp_fragment_shader_src =
    "#version 330 core\n"
    "in vec3 v_color;\n"
    "out vec4 frag_color;\n"
    "void main()\n"
    "{\n"
    "    frag_color = vec4(v_color, 1.0);\n"
    "}\n";

/**********************************************************************
 * @brief  Append one line vertex to the geometry buffer.
 *
 * @param[in,out] p_buffer  Vertex buffer being filled.
 * @param[in,out] p_index   Running float offset; advanced by 6.
 **********************************************************************/
static void push_vertex(float * p_buffer, size_t * p_index,
                        float x, float y, float z,
                        float r, float g, float b)
{
    size_t i = 0;

    if ((NULL == p_buffer) || (NULL == p_index))
    {
        goto cleanup;
    }

    i = *p_index;

    p_buffer[i + 0U] = x;
    p_buffer[i + 1U] = y;
    p_buffer[i + 2U] = z;
    p_buffer[i + 3U] = r;
    p_buffer[i + 4U] = g;
    p_buffer[i + 5U] = b;

    *p_index = i + (size_t)FLOATS_PER_VERTEX;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Compile one shader stage and report any error.
 *
 * @return  Shader object, or 0 on failure.
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
            (void)fprintf(stderr, "[render] shader compile failed:\n%s\n",
                          p_log);
            free(p_log);
        }
        glDeleteShader(shader);
        shader = 0;
    }

cleanup:
    return shader;
}

/**********************************************************************
 * @brief  Build the shader program from the two stages above.
 *
 * @return  Program object, or 0 on failure.
 **********************************************************************/
static GLuint build_program(void)
{
    GLuint program  = 0;
    GLuint vertex   = 0;
    GLuint fragment = 0;
    GLint  linked   = GL_FALSE;

    vertex = compile_shader(GL_VERTEX_SHADER, gp_vertex_shader_src);
    if (0 == vertex)
    {
        goto cleanup;
    }

    fragment = compile_shader(GL_FRAGMENT_SHADER,
                              gp_fragment_shader_src);
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
    /* The shaders are already attached (or the program is dead), so   */
    /* they can be released either way.                                */
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
 * @brief  Bake the static world-space scene into a vertex buffer.
 *
 * The scene is authored directly in world coordinates and never
 * changes; head motion is expressed purely through the view matrix.
 *
 * @param[out] p_vertex_count  Receives the number of vertices built.
 *
 * @return  Heap buffer of interleaved position+colour floats, or NULL.
 *          Caller frees.
 **********************************************************************/
static float * build_scene(GLsizei * p_vertex_count)
{
    float *  p_buffer = NULL;
    float *  p_result = NULL;
    size_t   index    = 0;
    size_t   capacity = 0;
    size_t   vertices = 0;
    int      i        = 0;
    float    extent   = 0.0F;

    if (NULL == p_vertex_count)
    {
        goto cleanup;
    }

    /* Grid: one line per row and per column across the full span.     */
    /* Cube: 12 edges. Axes: 3 arms. Two vertices per line.            */
    vertices = (size_t)(((GRID_HALF_CELLS * 2) + 1) * 2 * 2)
             + (size_t)(12 * 2)
             + (size_t)(3 * 2);

    capacity = vertices * (size_t)FLOATS_PER_VERTEX;

    p_buffer = (float *)calloc(capacity, sizeof(float));
    if (NULL == p_buffer)
    {
        goto cleanup;
    }

    extent = (float)GRID_HALF_CELLS * GRID_CELL_METRES;

    /* --- Ground grid, dim cyan: a floor reference the eye can lock   */
    /* onto. Kept dim so it does not overpower the real world.         */
    for (i = -GRID_HALF_CELLS; i <= GRID_HALF_CELLS; i++)
    {
        float offset = (float)i * GRID_CELL_METRES;

        /* Line running along Z. */
        push_vertex(p_buffer, &index, offset, GRID_Y_METRES, -extent,
                    0.0F, 0.30F, 0.35F);
        push_vertex(p_buffer, &index, offset, GRID_Y_METRES, extent,
                    0.0F, 0.30F, 0.35F);

        /* Line running along X. */
        push_vertex(p_buffer, &index, -extent, GRID_Y_METRES, offset,
                    0.0F, 0.30F, 0.35F);
        push_vertex(p_buffer, &index, extent, GRID_Y_METRES, offset,
                    0.0F, 0.30F, 0.35F);
    }

    /* --- World-locked wireframe cube, floating in front of the origin. */
    {
        const float h  = CUBE_HALF_METRES;
        const float cz = CUBE_Z_METRES;

        /* The 8 corners, indexed 0..7 (low Y = 0..3, high Y = 4..7). */
        const float corner[8][3] = {
            { -h, -h, cz - h }, {  h, -h, cz - h },
            {  h, -h, cz + h }, { -h, -h, cz + h },
            { -h,  h, cz - h }, {  h,  h, cz - h },
            {  h,  h, cz + h }, { -h,  h, cz + h }
        };
        /* 12 edges as corner index pairs. */
        const int edge[12][2] = {
            { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },   /* bottom face */
            { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },   /* top face    */
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }    /* uprights    */
        };

        for (i = 0; i < 12; i++)
        {
            const float * p_a = corner[edge[i][0]];
            const float * p_b = corner[edge[i][1]];

            push_vertex(p_buffer, &index, p_a[0], p_a[1], p_a[2],
                        1.0F, 0.55F, 0.0F);
            push_vertex(p_buffer, &index, p_b[0], p_b[1], p_b[2],
                        1.0F, 0.55F, 0.0F);
        }
    }

    /* --- Axis gizmo at the tracking origin: X red, Y green, Z blue. */
    push_vertex(p_buffer, &index, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F);
    push_vertex(p_buffer, &index, AXIS_METRES, 0.0F, 0.0F,
                1.0F, 0.0F, 0.0F);

    push_vertex(p_buffer, &index, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F);
    push_vertex(p_buffer, &index, 0.0F, AXIS_METRES, 0.0F,
                0.0F, 1.0F, 0.0F);

    push_vertex(p_buffer, &index, 0.0F, 0.0F, 0.0F, 0.0F, 0.4F, 1.0F);
    push_vertex(p_buffer, &index, 0.0F, 0.0F, -AXIS_METRES,
                0.0F, 0.4F, 1.0F);

    *p_vertex_count = (GLsizei)(index / (size_t)FLOATS_PER_VERTEX);
    p_result        = p_buffer;
    p_buffer        = NULL;

cleanup:
    if (NULL != p_buffer)
    {
        free(p_buffer);
    }
    return p_result;
}

/**********************************************************************
 * @brief  Locate the display that the VITURE glasses present as.
 **********************************************************************/
int ar_render_find_glasses_display(void)
{
    int          result   = AR_DISPLAY_NOT_FOUND;
    int          count    = 0;
    int          index    = 0;
    const char * p_name   = NULL;

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

        (void)fprintf(stderr, "[render] display %d: %s\n", index,
                      p_name);

        if (NULL != strstr(p_name, GLASSES_DISPLAY_TAG))
        {
            result = index;
        }
    }

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Create a fullscreen GL window on the given display.
 **********************************************************************/
ar_render_ctx_t * ar_render_create(int display_index, int windowed)
{
    ar_render_ctx_t * p_ctx    = NULL;
    ar_render_ctx_t * p_result = NULL;
    float *           p_scene  = NULL;
    uint32_t          flags    = 0U;
    int               pos      = 0;

    p_ctx = (ar_render_ctx_t *)calloc(1U, sizeof(ar_render_ctx_t));
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

    /* Centre the window on the chosen display, then go fullscreen on  */
    /* it. SDL_WINDOWPOS_CENTERED_DISPLAY is what pins us to the       */
    /* glasses rather than the laptop panel.                           */
    pos   = (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display_index);
    flags = SDL_WINDOW_OPENGL;
    if (0 == windowed)
    {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    p_ctx->p_window = SDL_CreateWindow("VITURE AR overlay", pos, pos,
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

    /* Match the 90Hz panel; vsync keeps the overlay tear-free. */
    (void)SDL_GL_SetSwapInterval(1);

    SDL_GL_GetDrawableSize(p_ctx->p_window, &p_ctx->width,
                           &p_ctx->height);
    (void)fprintf(stderr, "[render] window on display %d (%s)\n",
                  display_index,
                  (0 == windowed) ? "fullscreen" : "windowed");

    p_ctx->program = build_program();
    if (0 == p_ctx->program)
    {
        goto cleanup;
    }

    p_ctx->u_view       = glGetUniformLocation(p_ctx->program,
                                               "u_view");
    p_ctx->u_projection = glGetUniformLocation(p_ctx->program,
                                               "u_projection");

    p_scene = build_scene(&p_ctx->vertex_count);
    if (NULL == p_scene)
    {
        goto cleanup;
    }

    glGenVertexArrays(1, &p_ctx->vao);
    glGenBuffers(1, &p_ctx->vbo);

    glBindVertexArray(p_ctx->vao);
    glBindBuffer(GL_ARRAY_BUFFER, p_ctx->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)p_ctx->vertex_count
                              * (size_t)FLOATS_PER_VERTEX
                              * sizeof(float)),
                 p_scene, GL_STATIC_DRAW);

    /* location 0: position (3 floats), location 1: colour (3 floats). */
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          (GLsizei)(FLOATS_PER_VERTEX * sizeof(float)),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          (GLsizei)(FLOATS_PER_VERTEX * sizeof(float)),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glLineWidth(2.0F);

    p_result = p_ctx;
    p_ctx    = NULL;

cleanup:
    if (NULL != p_scene)
    {
        free(p_scene);
    }
    if (NULL != p_ctx)
    {
        /* Partial construction: unwind whatever was built. */
        ar_render_destroy(p_ctx);
    }
    return p_result;
}

/**********************************************************************
 * @brief  Report the current drawable aspect ratio.
 **********************************************************************/
float ar_render_aspect(ar_render_ctx_t * p_ctx)
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
 * @brief  Draw one frame of the world-locked scene and present it.
 **********************************************************************/
void ar_render_frame(ar_render_ctx_t * p_ctx, const float * p_view,
                     const float * p_projection, int draw_scene)
{
    if ((NULL == p_ctx) || (NULL == p_view) || (NULL == p_projection))
    {
        goto cleanup;
    }

    /* Re-query the drawable every frame. Under Wayland the compositor
     * applies fullscreen asynchronously, so the size captured at
     * creation is the pre-fullscreen one (e.g. 960x540 on a 1920x1080
     * panel). Reading it once would leave us rendering into a quarter
     * of the screen with the wrong aspect ratio. This also covers the
     * user resizing a windowed preview. */
    SDL_GL_GetDrawableSize(p_ctx->p_window, &p_ctx->width,
                           &p_ctx->height);

    /* The compositor applies fullscreen a few frames in, so report the
     * drawable whenever it changes rather than once at start-up. */
    if ((p_ctx->width != p_ctx->logged_w) ||
        (p_ctx->height != p_ctx->logged_h))
    {
        (void)fprintf(stderr, "[render] drawable %dx%d\n",
                      p_ctx->width, p_ctx->height);
        p_ctx->logged_w = p_ctx->width;
        p_ctx->logged_h = p_ctx->height;
    }

    glViewport(0, 0, p_ctx->width, p_ctx->height);

    /* Pure black: on an additive combiner this is what the wearer     */
    /* sees straight through. Never clear to anything else.            */
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear((GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    /* While the VIO is still converging the pose is meaningless, so we
     * present a black (fully transparent) frame rather than geometry
     * that would jitter around the room. */
    if (0 != draw_scene)
    {
        glUseProgram(p_ctx->program);
        glUniformMatrix4fv(p_ctx->u_view, 1, GL_FALSE, p_view);
        glUniformMatrix4fv(p_ctx->u_projection, 1, GL_FALSE,
                           p_projection);

        glBindVertexArray(p_ctx->vao);
        glDrawArrays(GL_LINES, 0, p_ctx->vertex_count);
        glBindVertexArray(0);
    }

    SDL_GL_SwapWindow(p_ctx->p_window);

cleanup:
    return;
}

/**********************************************************************
 * @brief  Pump the window event queue.
 **********************************************************************/
void ar_render_poll_events(ar_render_ctx_t * p_ctx, int * p_quit,
                           int * p_recenter)
{
    SDL_Event event = { 0 };

    (void)p_ctx;

    while (0 != SDL_PollEvent(&event))
    {
        if (SDL_QUIT == event.type)
        {
            if (NULL != p_quit)
            {
                *p_quit = 1;
            }
        }
        else if (SDL_KEYDOWN == event.type)
        {
            SDL_Keycode key = event.key.keysym.sym;

            if ((SDLK_ESCAPE == key) || (SDLK_q == key))
            {
                if (NULL != p_quit)
                {
                    *p_quit = 1;
                }
            }
            else if (SDLK_r == key)
            {
                if (NULL != p_recenter)
                {
                    *p_recenter = 1;
                }
            }
            else
            {
                /* Other keys are not bound. */
            }
        }
        else
        {
            /* Event not of interest. */
        }
    }
}

/**********************************************************************
 * @brief  Destroy the renderer and release GL/SDL resources.
 **********************************************************************/
void ar_render_destroy(ar_render_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    if (NULL != p_ctx->gl_context)
    {
        if (0 != p_ctx->vbo)
        {
            glDeleteBuffers(1, &p_ctx->vbo);
        }
        if (0 != p_ctx->vao)
        {
            glDeleteVertexArrays(1, &p_ctx->vao);
        }
        if (0 != p_ctx->program)
        {
            glDeleteProgram(p_ctx->program);
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
