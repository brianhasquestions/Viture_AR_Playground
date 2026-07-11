/**********************************************************************
 * @file    screen_render.h
 * @brief   Renders world-locked virtual screens in the glasses.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Same world-locking principle as 02_ar_overlay: each quad is static in
 * world space and only the view matrix moves, so the screens appear
 * nailed to a spot in the room. The difference is that the quads are
 * textured with live desktop pixels instead of being wireframe.
 *
 * Panels live in numbered SLOTS. A slot is acquired when a screen is
 * added and released when it is removed, so panels can come and go at
 * runtime without disturbing the others.
 *
 * The additive-combiner rules from 02 still apply, with a twist: black
 * desktop pixels are invisible (they emit no light), so a dark terminal
 * on a dark wall will be hard to see. Bright content reads best.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef SCREEN_RENDER_H
#define SCREEN_RENDER_H

#include <stdint.h>

#include "screen_capture.h"

/* Returned when no VITURE display is attached. */
#define SCREEN_DISPLAY_NOT_FOUND    (-1)

/* Maximum number of virtual screens that can be shown at once. */
#define SCREEN_MAX_PANELS           (6)

/* Modifier keys that must be held for a control to register. The
 * compositor sees keys first: Hyprland binds most Super and Ctrl+Super
 * combinations itself, so those never reach us. Ctrl+Alt is left alone
 * by default. Configurable so no one gets stuck fighting their keybinds. */
#define SCREEN_MOD_CTRL             (0x1U)
#define SCREEN_MOD_ALT              (0x2U)
#define SCREEN_MOD_SHIFT            (0x4U)
#define SCREEN_MOD_SUPER            (0x8U)

/* Spawn directions, reported by screen_render_poll(). */
#define SCREEN_SPAWN_NONE           (0)
#define SCREEN_SPAWN_UP             (1)
#define SCREEN_SPAWN_DOWN           (2)
#define SCREEN_SPAWN_LEFT           (3)
#define SCREEN_SPAWN_RIGHT          (4)

/**********************************************************************
 * @brief  One tick of user input, filled by screen_render_poll().
 *
 * Every field is an edge (a single key press), not a held state, so a
 * value is consumed once and cleared on the next poll.
 **********************************************************************/
typedef struct
{
    int quit;         /* Q: quit.                                     */
    int recenter;     /* R: re-anchor everything in front of you.     */
    int scale_step;   /* -/=: shrink (-1) or grow (+1) all panels.    */
    int dist_step;    /* [/]: nearer (-1) or further (+1) all panels. */
    int spawn_dir;    /* SCREEN_SPAWN_*: spawn relative to selection.  */
    int remove_sel;   /* X: remove the selected panel.                */
    int select_next;  /* Tab: select the next panel.                  */
    int yaw_step;     /* ,/.: angle selected panel left (-1)/right(+1)*/
    int pitch_step;   /* ;/': tilt selected panel down (-1)/up (+1).  */
    int toggle_help;  /* H: show/hide the on-screen command list.     */
} screen_input_t;

/**********************************************************************
 * @brief  Renderer state (heap allocated). Opaque.
 **********************************************************************/
typedef struct screen_render_ctx screen_render_ctx_t;

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Locate the display the VITURE glasses present as.
 *
 * @return  SDL display index, or SCREEN_DISPLAY_NOT_FOUND.
 **********************************************************************/
int screen_render_find_glasses_display(void);

/**********************************************************************
 * @brief  Create a GL window on the given display.
 *
 * @param[in]  display_index  SDL display index.
 * @param[in]  windowed       Non-zero for a desktop-window preview.
 *
 * @return  New renderer, or NULL. Release with screen_render_destroy().
 **********************************************************************/
screen_render_ctx_t * screen_render_create(int display_index,
                                           int windowed);

/**********************************************************************
 * @brief  Current drawable aspect ratio (re-queried each call).
 **********************************************************************/
float screen_render_aspect(screen_render_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Acquire a free panel slot and its GL texture.
 *
 * @param[in]  p_ctx  Renderer.
 *
 * @return  Slot index in [0, SCREEN_MAX_PANELS), or -1 if all are in
 *          use. Release with screen_render_release_panel().
 **********************************************************************/
int screen_render_acquire_panel(screen_render_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Release a panel slot so it stops drawing and can be reused.
 *
 * @param[in]  p_ctx  Renderer.
 * @param[in]  panel  Slot from screen_render_acquire_panel().
 **********************************************************************/
void screen_render_release_panel(screen_render_ctx_t * p_ctx,
                                 int panel);

/**********************************************************************
 * @brief  Upload a captured desktop frame into a panel's GL texture.
 *
 * @param[in]  p_ctx      Renderer.
 * @param[in]  panel      Slot from screen_render_acquire_panel().
 * @param[in]  p_pixels   Pixel data from screen_capture_pixels().
 * @param[in]  width      Frame width.
 * @param[in]  height     Frame height.
 * @param[in]  stride     Row stride in bytes.
 * @param[in]  format     Pixel layout.
 * @param[in]  y_invert   Non-zero if the image is bottom-up.
 **********************************************************************/
void screen_render_upload(screen_render_ctx_t * p_ctx, int panel,
                          const uint8_t * p_pixels, uint32_t width,
                          uint32_t height, uint32_t stride,
                          screen_pixel_format_t format, int y_invert);

/**********************************************************************
 * @brief  Upload the help-overlay image and show or hide it.
 *
 * Pass p_pixels non-NULL to (re)upload the RGBA help texture. Pass NULL
 * to only change visibility.
 *
 * @param[in]  p_ctx     Renderer.
 * @param[in]  p_pixels  RGBA8 help image, or NULL to keep the current.
 * @param[in]  width     Image width.
 * @param[in]  height    Image height.
 * @param[in]  visible   Non-zero to draw the help overlay.
 **********************************************************************/
void screen_render_set_help(screen_render_ctx_t * p_ctx,
                            const uint8_t * p_pixels, uint32_t width,
                            uint32_t height, int visible);

/**********************************************************************
 * @brief  Draw all active panels and present the frame.
 *
 * @param[in]  p_ctx         Renderer.
 * @param[in]  p_view        View matrix (16 floats, column-major).
 * @param[in]  p_projection  Projection matrix (16 floats).
 * @param[in]  p_models      SCREEN_MAX_PANELS model matrices back to
 *                           back (16 floats each), indexed by slot.
 *                           Only acquired slots are drawn.
 * @param[in]  selected      Slot to highlight, or -1 for none.
 * @param[in]  p_help_model  Model matrix for the help overlay quad, or
 *                           NULL to skip it. Drawn only when visible.
 * @param[in]  draw_scene    0 while head tracking is still converging;
 *                           presents a black (invisible) frame instead
 *                           of geometry that would fly around.
 **********************************************************************/
void screen_render_frame(screen_render_ctx_t * p_ctx,
                         const float * p_view,
                         const float * p_projection,
                         const float * p_models, int selected,
                         const float * p_help_model, int draw_scene);

/**********************************************************************
 * @brief  Set which modifiers must be held for controls to register.
 *
 * @param[in]  p_ctx  Renderer.
 * @param[in]  mask   OR of SCREEN_MOD_*. All named modifiers must be
 *                    held simultaneously.
 **********************************************************************/
void screen_render_set_modifiers(screen_render_ctx_t * p_ctx,
                                 uint32_t mask);

/**********************************************************************
 * @brief  Pump the window event queue into an input tick.
 *
 * @param[in]   p_ctx   Renderer.
 * @param[out]  p_input Zeroed and filled with this tick's edges.
 **********************************************************************/
void screen_render_poll(screen_render_ctx_t * p_ctx,
                        screen_input_t * p_input);

/**********************************************************************
 * @brief  Destroy the renderer. Safe to pass NULL.
 **********************************************************************/
void screen_render_destroy(screen_render_ctx_t * p_ctx);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_RENDER_H */
