/**********************************************************************
 * @file    phash.h
 * @brief   Perceptual hash of a grayscale frame (drawing fingerprint).
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Frameless recognition: we do not know where in the frame the drawing
 * is, so we take the central square (most of what the wearer is looking
 * at), downscale it, and reduce it to a 64-bit DCT perceptual hash. Two
 * views of the same drawing produce hashes a small Hamming distance
 * apart; unrelated scenes are far apart.
 *
 * This is illumination- and scale-tolerant but NOT rotation- or
 * perspective-invariant: re-view the drawing from roughly the same
 * distance and angle. (That trade-off is the cost of going frameless.)
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef PHASH_H
#define PHASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define PHASH_BYTES     (8)   /* 64-bit hash. */

/**********************************************************************
 * @brief  Compute the 64-bit perceptual hash of a grayscale image.
 *
 * @param[in]  p_gray  Grayscale pixels, row-major.
 * @param[in]  w       Width.
 * @param[in]  h       Height.
 * @param[out] p_hash  Receives PHASH_BYTES bytes (big-endian).
 *
 * @return  0 on success, -1 on bad input.
 **********************************************************************/
int phash_compute(const uint8_t * p_gray, int w, int h,
                  uint8_t p_hash[PHASH_BYTES]);

/**********************************************************************
 * @brief  Hamming distance between two hashes (0..64).
 **********************************************************************/
int phash_distance(const uint8_t a[PHASH_BYTES],
                   const uint8_t b[PHASH_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* PHASH_H */
