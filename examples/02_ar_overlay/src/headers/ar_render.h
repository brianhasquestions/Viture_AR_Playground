/**********************************************************************
 * @file    ar_render.h
 * @brief   OpenGL renderer for an optical see-through AR overlay.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * The VITURE glasses enumerate as an ordinary DisplayPort monitor
 * (1920x1080 @ 90Hz). Rendering AR on them means putting a fullscreen
 * window on that display and drawing world-locked geometry with a view
 * matrix derived from the Carina head pose.
 *
 * Two properties of the optics drive every rendering decision here:
 *
 *  1. The combiner is ADDITIVE. Black pixels emit no light and are
 *     therefore fully transparent to the wearer. There is no alpha
 *     channel to the real world: dark == see-through, bright == solid.
 *     So the scene is cleared to pure black, never to a background
 *     colour.
 *
 *  2. Following from that, geometry is drawn as WIREFRAME lines rather
 *     than filled surfaces. A filled polygon would wash out whatever
 *     is behind it; lines leave the real world visible.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef AR_RENDER_H
#define AR_RENDER_H

#include <stdint.h>

/* Returned by ar_render_find_glasses_display() when no VITURE display */
/* is attached.                                                        */
#define AR_DISPLAY_NOT_FOUND    (-1)

/**********************************************************************
 * @brief  Renderer state (heap allocated). Opaque to callers; the
 *         concrete type lives in ar_render.c so that SDL and GL headers
 *         stay out of the rest of the program.
 **********************************************************************/
typedef struct ar_render_ctx ar_render_ctx_t;

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Locate the display that the VITURE glasses present as.
 *
 * Matches on the SDL display name reported by the compositor (the
 * glasses identify as a "VITURE" panel).
 *
 * @return  SDL display index, or AR_DISPLAY_NOT_FOUND.
 **********************************************************************/
int ar_render_find_glasses_display(void);

/**********************************************************************
 * @brief  Create a fullscreen GL window on the given display.
 *
 * @param[in]  display_index  SDL display index to open on.
 * @param[in]  windowed       Non-zero for a windowed 960x540 preview on
 *                            the current desktop instead of fullscreen
 *                            on the glasses. Useful for development
 *                            without wearing them.
 *
 * @return  New renderer, or NULL on failure. Release with
 *          ar_render_destroy().
 **********************************************************************/
ar_render_ctx_t * ar_render_create(int display_index, int windowed);

/**********************************************************************
 * @brief  Report the current drawable aspect ratio (width / height).
 *
 * Re-queries the window, because under Wayland the drawable is not at
 * its final size until the compositor has applied fullscreen.
 *
 * @param[in]  p_ctx  Renderer.
 *
 * @return  Aspect ratio, or 16.0F/9.0F if p_ctx is NULL.
 **********************************************************************/
float ar_render_aspect(ar_render_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Draw one frame and present it.
 *
 * Clears to black (transparent to the wearer), optionally draws the
 * world-locked scene with the supplied matrices, and swaps buffers.
 *
 * @param[in]  p_ctx        Renderer.
 * @param[in]  p_view       View matrix, 16 floats column-major.
 * @param[in]  p_projection Projection matrix, 16 floats column-major.
 * @param[in]  draw_scene   Non-zero to draw the scene. Pass 0 while the
 *                          head pose is still unstable: feeding an
 *                          unconverged pose into the view matrix makes
 *                          the geometry fly around the room. A black
 *                          frame is fully transparent, so the wearer
 *                          simply sees nothing until tracking locks.
 **********************************************************************/
void ar_render_frame(ar_render_ctx_t * p_ctx, const float * p_view,
                     const float * p_projection, int draw_scene);

/**********************************************************************
 * @brief  Pump the window event queue.
 *
 * @param[in]   p_ctx          Renderer.
 * @param[out]  p_quit         Set to 1 when the user asked to quit
 *                             (Escape, Q, or window close).
 * @param[out]  p_recenter     Set to 1 when the user pressed R.
 **********************************************************************/
void ar_render_poll_events(ar_render_ctx_t * p_ctx, int * p_quit,
                           int * p_recenter);

/**********************************************************************
 * @brief  Destroy the renderer and release GL/SDL resources.
 *
 * Safe to pass NULL.
 **********************************************************************/
void ar_render_destroy(ar_render_ctx_t * p_ctx);

#ifdef __cplusplus
}
#endif

#endif /* AR_RENDER_H */
