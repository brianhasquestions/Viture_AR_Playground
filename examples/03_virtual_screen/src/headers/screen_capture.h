/**********************************************************************
 * @file    screen_capture.h
 * @brief   Continuous capture of a Wayland output via wlr-screencopy.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Grabs the contents of one physical display (e.g. the laptop panel
 * "eDP-1") into a shared-memory buffer, so it can be uploaded to a GL
 * texture and drawn on a world-locked quad in the glasses.
 *
 * Protocol: zwlr_screencopy_manager_v1 (wlr-screencopy-unstable-v1),
 * implemented by wlroots-based compositors including Hyprland and Sway.
 * This is the same mechanism `grim` uses. The protocol XML is vendored
 * under protocols/ and compiled by wayland-scanner at build time, so no
 * system wlr-protocols package is required.
 *
 * Capture is asynchronous and pumped from the render loop: one frame is
 * requested at a time, and the next is requested as soon as the last
 * one lands. screen_capture_poll() never blocks the renderer.
 *
 * IMPORTANT: capture the laptop panel, NOT the glasses display. Pointing
 * this at the display it is being rendered on creates a feedback loop
 * (a hall of mirrors).
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef SCREEN_CAPTURE_H
#define SCREEN_CAPTURE_H

#include <stdint.h>

/* Pixel layout of the captured frame, in the order the bytes sit in
 * memory. These map directly onto a GL texture upload format. */
typedef enum
{
    SCREEN_PIXEL_UNKNOWN = 0,
    SCREEN_PIXEL_BGRA,      /* WL_SHM_FORMAT_XRGB8888 / ARGB8888 */
    SCREEN_PIXEL_RGBA       /* WL_SHM_FORMAT_XBGR8888 / ABGR8888 */
} screen_pixel_format_t;

/**********************************************************************
 * @brief  Capture session state (heap allocated). Opaque: the Wayland
 *         headers stay inside screen_capture.c.
 **********************************************************************/
typedef struct screen_capture_ctx screen_capture_ctx_t;

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Connect to the compositor and begin capturing an output.
 *
 * @param[in]  p_output_name  Wayland output to capture, e.g. "eDP-1".
 *                            Pass NULL to take the first output that is
 *                            not the glasses.
 *
 * @return  New session, or NULL on failure (no compositor, protocol
 *          unsupported, or output not found).
 **********************************************************************/
screen_capture_ctx_t * screen_capture_create(const char * p_output_name);

/**********************************************************************
 * @brief  Pump the Wayland event queue and keep frames flowing.
 *
 * Non-blocking. Dispatches any pending events, and requests the next
 * frame once the previous one has landed. Call once per rendered frame.
 *
 * @param[in]  p_ctx  Capture session.
 *
 * @return  1 if a NEW frame became available since the last call (the
 *          caller should re-upload the texture), 0 if not, -1 on error.
 **********************************************************************/
int screen_capture_poll(screen_capture_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Access the most recently captured frame.
 *
 * The returned pointer stays valid until the next screen_capture_poll()
 * that reports a new frame. Do not free it.
 *
 * @param[in]  p_ctx      Capture session.
 * @param[out] p_width    Frame width in pixels.
 * @param[out] p_height   Frame height in pixels.
 * @param[out] p_stride   Row stride in bytes.
 * @param[out] p_format   Pixel layout.
 * @param[out] p_y_invert Non-zero if the compositor delivered the image
 *                        bottom-up, so the texture must be flipped.
 *
 * @return  Pointer to the pixel data, or NULL if no frame yet.
 **********************************************************************/
const uint8_t * screen_capture_pixels(screen_capture_ctx_t * p_ctx,
                                      uint32_t * p_width,
                                      uint32_t * p_height,
                                      uint32_t * p_stride,
                                      screen_pixel_format_t * p_format,
                                      int * p_y_invert);

/**********************************************************************
 * @brief  Name of the output being captured (e.g. "eDP-1").
 *
 * Resolves the actual output even when creation auto-picked one.
 *
 * @param[in]  p_ctx  Capture session.
 *
 * @return  Output name, or NULL. Owned by the session; do not free.
 **********************************************************************/
const char * screen_capture_output_name(screen_capture_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Tear down the capture session and free all resources.
 *         Safe to pass NULL.
 **********************************************************************/
void screen_capture_destroy(screen_capture_ctx_t * p_ctx);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_CAPTURE_H */
