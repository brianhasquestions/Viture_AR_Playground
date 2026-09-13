/**********************************************************************
 * @file    detect.h
 * @brief   Glyph vision layer: grayscale image -> decoded payloads.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Pipeline: Otsu threshold -> connected components on the dark pixels ->
 * keep square-ish blobs (the black finder frame) -> extract the four
 * corners -> homography from the unit marker square to the image ->
 * sample the 8x8 module grid -> hand the grid to glyph_decode().
 *
 * Pure C, no OpenCV. Developed and tested against rendered glyph images
 * so it is verifiable without the headset.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef DETECT_H
#define DETECT_H

#include <stdint.h>

#include "glyph.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define DETECT_MAX_RESULTS   (8)

/**********************************************************************
 * @brief  One decoded glyph and where it sits in the image.
 **********************************************************************/
typedef struct
{
    uint8_t payload[GLYPH_PAYLOAD_BYTES];
    float   cx;     /* Glyph centre, image pixels. */
    float   cy;
} detect_result_t;

/**********************************************************************
 * @brief  Find and decode glyphs in a grayscale image.
 *
 * @param[in]  p_gray  Grayscale pixels, row-major.
 * @param[in]  w       Width.
 * @param[in]  h       Height.
 * @param[out] p_out   Array receiving up to max results.
 * @param[in]  max     Capacity of p_out (<= DETECT_MAX_RESULTS).
 *
 * @return  Number of glyphs decoded (>= 0), or -1 on error.
 **********************************************************************/
int detect_glyphs(const uint8_t * p_gray, int w, int h,
                  detect_result_t * p_out, int max);

/**********************************************************************
 * @brief  Render a module grid to a grayscale image (for the generator
 *         and for offline tests).
 *
 * @param[in]  grid           8x8 module grid (1 = black).
 * @param[in]  module_px      Pixels per module.
 * @param[in]  quiet_modules  White border, in modules, around the grid.
 * @param[out] pp_gray        Receives a heap buffer; caller frees.
 * @param[out] p_w            Receives width.
 * @param[out] p_h            Receives height.
 *
 * @return  0 on success, -1 on failure.
 **********************************************************************/
int glyph_render(const uint8_t grid[GLYPH_MODULES][GLYPH_MODULES],
                 int module_px, int quiet_modules,
                 uint8_t ** pp_gray, int * p_w, int * p_h);

#ifdef __cplusplus
}
#endif

#endif /* DETECT_H */
