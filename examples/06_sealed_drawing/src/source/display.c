#include "display.h"

#include "hud.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLASSES_DISPLAY_TAG   "VITURE"
#define HUD_SCALE             (4)
#define TITLE_CHARS           (HUD_WRAP_COLS + 1)
#define WINDOW_W              (960)
#define WINDOW_H              (540)
#define RGBA_BYTES            (4)
#define COLOR_MAX             (255)
#define NO_DISPLAY            (-1)

struct display_ctx
{
    SDL_Window *   p_window;
    SDL_Renderer * p_renderer;
    SDL_Texture *  p_texture;
    int            tex_w;
    int            tex_h;
};

static int find_glasses_display(void)
{
    int result = NO_DISPLAY;
    int count  = SDL_GetNumVideoDisplays();
    int i      = 0;

    for (i = 0; i < count; i++)
    {
        const char * p_name = SDL_GetDisplayName(i);

        if ((NULL != p_name) &&
            (NULL != strstr(p_name, GLASSES_DISPLAY_TAG)))
        {
            result = i;
        }
    }

    return result;
}

display_ctx_t * display_create(int windowed)
{
    display_ctx_t * p_ctx   = NULL;
    display_ctx_t * p_ok    = NULL;
    int             display = 0;
    int             pos     = 0;
    Uint32          flags   = 0;

    if (0 != SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        (void)fprintf(stderr, "[display] SDL init failed: %s\n",
                      SDL_GetError());
        goto cleanup;
    }
    p_ctx = (display_ctx_t *)calloc(1U, sizeof(*p_ctx));
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    display = find_glasses_display();
    if ((NO_DISPLAY == display) && (0 == windowed))
    {
        (void)fprintf(stderr,
                      "[display] no VITURE display found; use --windowed "
                      "or connect the video cable.\n");
        goto cleanup;
    }
    if (NO_DISPLAY == display)
    {
        display = 0;
    }
    pos   = (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    flags = (0 != windowed) ? 0U
                            : (Uint32)SDL_WINDOW_FULLSCREEN_DESKTOP;
    p_ctx->p_window = SDL_CreateWindow("VITURE sealed drawing", pos, pos,
                                       WINDOW_W, WINDOW_H, flags);
    if (NULL == p_ctx->p_window)
    {
        (void)fprintf(stderr, "[display] window failed: %s\n",
                      SDL_GetError());
        goto cleanup;
    }
    p_ctx->p_renderer = SDL_CreateRenderer(
        p_ctx->p_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (NULL == p_ctx->p_renderer)
    {
        (void)fprintf(stderr, "[display] renderer failed: %s\n",
                      SDL_GetError());
        goto cleanup;
    }
    p_ok  = p_ctx;
    p_ctx = NULL;

cleanup:
    display_destroy(p_ctx);

    return p_ok;
}

static void replace_texture(display_ctx_t * p_ctx, const uint8_t * p_rgba,
                            const uint32_t dims[2])
{
    if ((NULL != p_ctx->p_texture) &&
        ((p_ctx->tex_w != (int)dims[0]) || (p_ctx->tex_h != (int)dims[1])))
    {
        SDL_DestroyTexture(p_ctx->p_texture);
        p_ctx->p_texture = NULL;
    }
    if (NULL == p_ctx->p_texture)
    {
        p_ctx->p_texture = SDL_CreateTexture(
            p_ctx->p_renderer, SDL_PIXELFORMAT_ABGR8888,
            SDL_TEXTUREACCESS_STREAMING, (int)dims[0], (int)dims[1]);
    }
    if (NULL != p_ctx->p_texture)
    {
        (void)SDL_UpdateTexture(p_ctx->p_texture, NULL, p_rgba,
                                (int)(dims[0] * RGBA_BYTES));
        p_ctx->tex_w = (int)dims[0];
        p_ctx->tex_h = (int)dims[1];
    }
}

void display_show_message(display_ctx_t * p_ctx, const char * p_title,
                          const char * p_body)
{
    uint32_t        dims[2] = { 0, 0 };
    uint8_t *       p_rgba  = NULL;
    int             i       = 0;
    hud_text_t      text;
    hud_wrapped_t * p_body_lines = NULL;
    const char *    ptrs[HUD_MAX_LINES + 2];
    char            title[TITLE_CHARS];

    memset(&text, 0, sizeof(text));
    if ((NULL == p_ctx) || (NULL == p_ctx->p_renderer))
    {
        goto cleanup;
    }
    p_body_lines = (hud_wrapped_t *)calloc(1U, sizeof(*p_body_lines));
    if (NULL == p_body_lines)
    {
        goto cleanup;
    }
    (void)snprintf(title, sizeof(title), "%s",
                   (NULL != p_title) ? p_title : "");
    hud_wrap_text(p_body, p_body_lines);
    ptrs[0] = title;
    ptrs[1] = "";
    for (i = 0; i < p_body_lines->line_count; i++)
    {
        ptrs[i + 2] = p_body_lines->pp_lines[i];
    }
    text.pp_lines   = ptrs;
    text.line_count = p_body_lines->line_count + 2;
    text.scale      = HUD_SCALE;
    p_rgba          = hud_render(&text, &dims[0], &dims[1]);
    if (NULL == p_rgba)
    {
        goto cleanup;
    }
    replace_texture(p_ctx, p_rgba, dims);

cleanup:
    if (NULL != p_rgba)
    {
        free(p_rgba);
    }
    if (NULL != p_body_lines)
    {
        free(p_body_lines);
    }

    return;
}

void display_show_image(display_ctx_t * p_ctx, const rgba_image_t * p_img)
{
    uint32_t dims[2] = { 0, 0 };

    if ((NULL == p_ctx) || (NULL == p_ctx->p_renderer) ||
        (NULL == p_img) || (NULL == p_img->p_rgba))
    {
        goto cleanup;
    }
    dims[0] = p_img->width;
    dims[1] = p_img->height;
    replace_texture(p_ctx, p_img->p_rgba, dims);

cleanup:

    return;
}

static int poll_quit(void)
{
    SDL_Event ev;
    int       quit = 0;

    while (0 != SDL_PollEvent(&ev))
    {
        if (SDL_QUIT == ev.type)
        {
            quit = 1;
        }
        else if ((SDL_KEYDOWN == ev.type) &&
                 (SDLK_ESCAPE == ev.key.keysym.sym))
        {
            quit = 1;
        }
    }

    return quit;
}

static void draw_centered(display_ctx_t * p_ctx, int win_w, int win_h)
{
    SDL_Rect dst;
    double   sx = (double)win_w / (double)p_ctx->tex_w;
    double   sy = (double)win_h / (double)p_ctx->tex_h;
    double   s  = (sx < sy) ? sx : sy;

    dst.w = (int)((double)p_ctx->tex_w * s);
    dst.h = (int)((double)p_ctx->tex_h * s);
    dst.x = (win_w - dst.w) / 2;
    dst.y = (win_h - dst.h) / 2;
    (void)SDL_RenderCopy(p_ctx->p_renderer, p_ctx->p_texture, NULL, &dst);
}

int display_pump(display_ctx_t * p_ctx)
{
    int quit  = 1;
    int win_w = 0;
    int win_h = 0;

    if ((NULL == p_ctx) || (NULL == p_ctx->p_renderer))
    {
        goto cleanup;
    }
    quit = poll_quit();
    (void)SDL_GetRendererOutputSize(p_ctx->p_renderer, &win_w, &win_h);
    (void)SDL_SetRenderDrawColor(p_ctx->p_renderer, 0, 0, 0, COLOR_MAX);
    (void)SDL_RenderClear(p_ctx->p_renderer);
    if (NULL != p_ctx->p_texture)
    {
        draw_centered(p_ctx, win_w, win_h);
    }
    SDL_RenderPresent(p_ctx->p_renderer);

cleanup:

    return quit;
}

void display_destroy(display_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if (NULL != p_ctx->p_texture)
    {
        SDL_DestroyTexture(p_ctx->p_texture);
    }
    if (NULL != p_ctx->p_renderer)
    {
        SDL_DestroyRenderer(p_ctx->p_renderer);
    }
    if (NULL != p_ctx->p_window)
    {
        SDL_DestroyWindow(p_ctx->p_window);
    }
    free(p_ctx);

cleanup:

    return;
}
