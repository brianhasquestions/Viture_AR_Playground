/**********************************************************************
 * @file    hud.h
 * @brief   Rasterise lines of text into an RGBA image for the overlay.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * There is no font library here: an embedded 8x8 bitmap font (public
 * domain, by Daniel Hepper) is blitted into a heap buffer. White glyphs
 * on a black background - which on the additive combiner means the text
 * glows and the background is fully transparent (see screen_render.h).
 *
 * The result is uploaded as a GL texture and drawn on a quad above the
 * virtual screens to show the command list.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef HUD_H
#define HUD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Rasterise text lines into a newly allocated RGBA buffer.
 *
 * @param[in]  pp_lines     Array of NUL-terminated lines.
 * @param[in]  line_count   Number of lines.
 * @param[in]  scale        Integer pixel scale (>= 1); each font pixel
 *                          becomes scale x scale image pixels.
 * @param[out] p_width      Receives image width in pixels.
 * @param[out] p_height     Receives image height in pixels.
 *
 * @return  Heap RGBA8 buffer (width*height*4 bytes), or NULL on failure.
 *          Caller frees with free().
 **********************************************************************/
uint8_t * hud_render_lines(const char * const * pp_lines,
                           int line_count, int scale,
                           uint32_t * p_width, uint32_t * p_height);

#ifdef __cplusplus
}
#endif

#endif /* HUD_H */
