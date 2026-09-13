/**********************************************************************
 * @file    jpeg_decode.h
 * @brief   Minimal baseline-JPEG decoder to grayscale, pure C.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * The glasses camera only emits MJPEG, and each MJPEG frame is a
 * self-contained baseline (SOF0) JPEG. Glyph detection needs only
 * luminance, so jpeg_decode_gray() hands back only the Y plane.
 * jpeg_decode_rgb() keeps the chroma planes too and converts to RGB, for
 * samples that show the captured picture.
 *
 * Scope: baseline sequential DCT, 8-bit, Huffman coding, with restart
 * markers and the standard Annex-K Huffman tables as a fallback for
 * abbreviated MJPEG streams. NOT supported: progressive, arithmetic
 * coding, 12-bit, or exotic sampling factors beyond 1x1 / 2x1 / 2x2.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef JPEG_DECODE_H
#define JPEG_DECODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  A decoded 8-bit grayscale image.
 **********************************************************************/
typedef struct
{
    int       width;
    int       height;
    uint8_t * p_gray;   /* width*height bytes, row-major. Heap-owned. */
} jpeg_image_t;

/**********************************************************************
 * @brief  Decode a baseline JPEG to grayscale.
 *
 * @param[in]  p_data  JPEG byte stream (one MJPEG frame).
 * @param[in]  size    Length of p_data in bytes.
 * @param[out] p_out   On success, receives the image; caller must call
 *                     jpeg_image_free(). Zeroed on failure.
 *
 * @return  0 on success, negative on any parse/decode error.
 **********************************************************************/
int jpeg_decode_gray(const uint8_t * p_data, size_t size,
                     jpeg_image_t * p_out);

/**********************************************************************
 * @brief  Release the pixels owned by a jpeg_image_t. Safe on NULL/zero.
 **********************************************************************/
void jpeg_image_free(jpeg_image_t * p_img);

/**********************************************************************
 * @brief  A decoded 8-bit packed RGB image.
 **********************************************************************/
typedef struct
{
    int       width;
    int       height;
    uint8_t * p_rgb;    /* width*height*3 bytes, row-major. Heap-owned. */
} jpeg_rgb_t;

/**********************************************************************
 * @brief  Decode a baseline JPEG to packed RGB (YCbCr -> RGB, chroma
 *         nearest-neighbour upsampled). Grayscale streams are
 *         replicated into all three channels.
 *
 * @param[in]  p_data  JPEG byte stream.
 * @param[in]  size    Length of p_data in bytes.
 * @param[out] p_out   On success, receives the image; caller must call
 *                     jpeg_rgb_free(). Zeroed on failure.
 *
 * @return  0 on success, negative on any parse/decode error.
 **********************************************************************/
int jpeg_decode_rgb(const uint8_t * p_data, size_t size,
                    jpeg_rgb_t * p_out);

/**********************************************************************
 * @brief  Decode a baseline JPEG to packed RGB at one quarter of the
 *         width and height. Only the 2x2 lowest-frequency coefficients
 *         of each block are transformed, so it is several times faster
 *         than jpeg_decode_rgb(); meant for live previews.
 *
 * @param[in]  p_data  JPEG byte stream.
 * @param[in]  size    Length of p_data in bytes.
 * @param[out] p_out   On success, receives the (width/4 x height/4)
 *                     image; caller must call jpeg_rgb_free().
 *
 * @return  0 on success, negative on any parse/decode error.
 **********************************************************************/
int jpeg_decode_rgb_quarter(const uint8_t * p_data, size_t size,
                            jpeg_rgb_t * p_out);

/**********************************************************************
 * @brief  Release the pixels owned by a jpeg_rgb_t. Safe on NULL/zero.
 **********************************************************************/
void jpeg_rgb_free(jpeg_rgb_t * p_img);

#ifdef __cplusplus
}
#endif

#endif /* JPEG_DECODE_H */
