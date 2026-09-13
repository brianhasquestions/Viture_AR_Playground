/**********************************************************************
 * @file    display.h
 * @brief   Show a message on the glasses screen (SDL2 2D renderer).
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * A fullscreen window on the VITURE display, onto which we blit text
 * rasterised by hud_render_lines(). No OpenGL: the SDL renderer copies
 * the RGBA text image straight to the screen, which is all a flat 2D
 * message needs.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct display_ctx display_ctx_t;

/**********************************************************************
 * @brief  Open a fullscreen window on the glasses (or windowed preview).
 *
 * @param[in]  windowed  Non-zero for a desktop-window preview instead of
 *                       fullscreen on the VITURE display.
 *
 * @return  Context, or NULL on failure. Release with display_destroy().
 **********************************************************************/
display_ctx_t * display_create(int windowed);

/**********************************************************************
 * @brief  Replace what is shown: a bold title and a body message.
 *
 * The body is word-wrapped. Pass NULL body to show just the title.
 **********************************************************************/
void display_show_message(display_ctx_t * p_ctx, const char * p_title,
                          const char * p_body);

/**********************************************************************
 * @brief  Process window events and repaint.
 *
 * @return  1 if the user asked to quit (Esc or window close), else 0.
 **********************************************************************/
int display_pump(display_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Destroy the window and release resources. Safe on NULL.
 **********************************************************************/
void display_destroy(display_ctx_t * p_ctx);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
