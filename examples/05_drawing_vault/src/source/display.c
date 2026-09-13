/**********************************************************************
 * @file    display.c
 * @brief   Show a message on the glasses screen (SDL2 2D renderer).
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "display.h"

#include "hud.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Substring identifying the glasses among the video displays. */
#define GLASSES_DISPLAY_TAG   "VITURE"
#define WRAP_COLS             (40)
#define MAX_LINES             (24)
#define HUD_SCALE             (4)

struct display_ctx
{
    SDL_Window *   p_window;
    SDL_Renderer * p_renderer;
    SDL_Texture *  p_texture;
    int            tex_w;
    int            tex_h;
};

/**********************************************************************
 * @brief  Find the VITURE display index, or -1 if none.
 **********************************************************************/
static int find_glasses_display(void)
{
    int result = -1;
    int count  = SDL_GetNumVideoDisplays();
    int i      = 0;

    for (i = 0; i < count; i++)
    {
        const char * p_name = SDL_GetDisplayName(i);
        if ((NULL != p_name) && (NULL != strstr(p_name,
                                                GLASSES_DISPLAY_TAG)))
        {
            result = i;
        }
    }
    return result;
}

display_ctx_t * display_create(int windowed)
{
    display_ctx_t * p_ctx  = NULL;
    display_ctx_t * p_ok   = NULL;
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
    if (display < 0)
    {
        if (0 == windowed)
        {
            (void)fprintf(stderr,
                "[display] no VITURE display found; use --windowed "
                "or connect the video cable.\n");
            goto cleanup;
        }
        display = 0;
    }

    pos   = (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    flags = (0 != windowed) ? 0U
                            : (Uint32)SDL_WINDOW_FULLSCREEN_DESKTOP;

    p_ctx->p_window = SDL_CreateWindow("VITURE drawing vault",
                                       pos, pos, 960, 540, flags);
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
    if (NULL != p_ctx)
    {
        display_destroy(p_ctx);
    }
    return p_ok;
}

/**********************************************************************
 * @brief  Word-wrap p_body into pp_lines starting at index start.
 *
 * @return  Total line count used.
 **********************************************************************/
static int wrap_body(const char * p_body, char lines[][WRAP_COLS + 1],
                     int start, int max_lines)
{
    int  line = start;
    int  col  = 0;

    if (NULL == p_body)
    {
        return line;
    }
    lines[line][0] = '\0';

    while (('\0' != *p_body) && (line < max_lines))
    {
        /* Measure the next word. */
        const char * p_word = p_body;
        int          wlen   = 0;

        while ((p_word[wlen] != '\0') && (p_word[wlen] != ' ') &&
               (p_word[wlen] != '\n'))
        {
            wlen++;
        }

        if ((col > 0) && ((col + 1 + wlen) > WRAP_COLS))
        {
            line++;
            if (line >= max_lines) { break; }
            lines[line][0] = '\0';
            col = 0;
        }
        if (col > 0)
        {
            lines[line][col] = ' ';
            col++;
        }
        {
            int k = 0;
            for (k = 0; (k < wlen) && (col < WRAP_COLS); k++)
            {
                lines[line][col] = p_word[k];
                col++;
            }
            lines[line][col] = '\0';
        }
        p_body += wlen;
        if ('\n' == *p_body)
        {
            line++;
            if (line >= max_lines) { break; }
            lines[line][0] = '\0';
            col = 0;
        }
        while (' ' == *p_body) { p_body++; }
    }
    return line + 1;
}

void display_show_message(display_ctx_t * p_ctx, const char * p_title,
                          const char * p_body)
{
    char        lines[MAX_LINES][WRAP_COLS + 1];
    const char * ptrs[MAX_LINES];
    uint8_t *   p_rgba = NULL;
    uint32_t    w      = 0;
    uint32_t    h      = 0;
    int         count  = 0;
    int         i      = 0;

    if ((NULL == p_ctx) || (NULL == p_ctx->p_renderer))
    {
        return;
    }

    (void)snprintf(lines[0], WRAP_COLS + 1, "%s",
                   (NULL != p_title) ? p_title : "");
    lines[1][0] = '\0';                  /* Blank spacer line. */
    count = wrap_body(p_body, lines, 2, MAX_LINES);
    for (i = 0; i < count; i++)
    {
        ptrs[i] = lines[i];
    }

    p_rgba = hud_render_lines(ptrs, count, HUD_SCALE, &w, &h);
    if (NULL == p_rgba)
    {
        return;
    }

    if (NULL != p_ctx->p_texture)
    {
        SDL_DestroyTexture(p_ctx->p_texture);
        p_ctx->p_texture = NULL;
    }
    p_ctx->p_texture = SDL_CreateTexture(
        p_ctx->p_renderer, SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STATIC, (int)w, (int)h);
    if (NULL != p_ctx->p_texture)
    {
        (void)SDL_UpdateTexture(p_ctx->p_texture, NULL, p_rgba,
                                (int)(w * 4U));
        p_ctx->tex_w = (int)w;
        p_ctx->tex_h = (int)h;
    }
    free(p_rgba);
}

int display_pump(display_ctx_t * p_ctx)
{
    SDL_Event ev;
    int       quit = 0;
    int       win_w = 0;
    int       win_h = 0;

    if ((NULL == p_ctx) || (NULL == p_ctx->p_renderer))
    {
        return 1;
    }

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

    SDL_GetRendererOutputSize(p_ctx->p_renderer, &win_w, &win_h);
    (void)SDL_SetRenderDrawColor(p_ctx->p_renderer, 0, 0, 0, 255);
    (void)SDL_RenderClear(p_ctx->p_renderer);

    if (NULL != p_ctx->p_texture)
    {
        /* Centre the text image, scaled to fit within the window. */
        SDL_Rect dst;
        double   sx = (double)win_w / (double)p_ctx->tex_w;
        double   sy = (double)win_h / (double)p_ctx->tex_h;
        double   s  = (sx < sy) ? sx : sy;
        if (s > 1.0) { s = 1.0; }
        dst.w = (int)((double)p_ctx->tex_w * s);
        dst.h = (int)((double)p_ctx->tex_h * s);
        dst.x = (win_w - dst.w) / 2;
        dst.y = (win_h - dst.h) / 2;
        (void)SDL_RenderCopy(p_ctx->p_renderer, p_ctx->p_texture,
                             NULL, &dst);
    }
    SDL_RenderPresent(p_ctx->p_renderer);
    return quit;
}

void display_destroy(display_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        return;
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
}
