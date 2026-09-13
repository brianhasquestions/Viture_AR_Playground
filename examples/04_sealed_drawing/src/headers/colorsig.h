/*
Summary: Colour signature of the located object: a coarse hue and
         saturation histogram. A painted object keeps its colours from
         any angle, so this cue is pose-invariant where the shape hash
         is not, and a grey wall or a blue monitor never looks like a
         red-and-gold doll. Twelve hue bins at two saturation levels,
         plus four brightness bins for unsaturated pixels. The grey
         bins are down-weighted to a quarter so that the paint, not the
         wall or the hand around it, defines the signature; each bin is
         its share of that weighted mass scaled to 0..255.
*/

#ifndef COLORSIG_H
#define COLORSIG_H

#include "bytes.h"
#include "locate.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define COLORSIG_HUE_BINS   (12)
#define COLORSIG_SAT_LEVELS (2)
#define COLORSIG_GRAY_BINS  (4)
#define COLORSIG_BINS       ((COLORSIG_HUE_BINS * COLORSIG_SAT_LEVELS) + \
                             COLORSIG_GRAY_BINS)

/*
Summary: Packed RGB image view (from jpeg_decode_rgb or the quarter
         variant).
Inputs:  p_rgb  - width * height * 3 bytes.
         width  - columns.
         height - rows.
Outputs: None.
*/
typedef struct rgb_image
{
    const uint8_t * p_rgb;
    int             width;
    int             height;
    int             pixel_bytes;    /* 3 for RGB, 4 for RGBA. */
} rgb_image_t;

/*
Summary: Saturation (0..100) of one pixel, for callers that weight by
         colourfulness.
Inputs:  p_pixel - first byte of an RGB or RGBA pixel.
Outputs: Saturation in percent.
*/
int colorsig_saturation(const uint8_t * p_pixel);

/*
Summary: Build the signature of one square region of an RGB image.
Inputs:  p_img    - the image.
         p_region - square to sample; clamped to the image.
         p_sig    - receives COLORSIG_BINS bytes.
Outputs: 0 on success, -1 on bad input or an empty region.
*/
int colorsig_compute(const rgb_image_t * p_img, const region_t * p_region,
                     uint8_t * p_sig);

/*
Summary: Histogram intersection of two signatures.
Inputs:  p_a - first signature (COLORSIG_BINS bytes).
         p_b - second signature.
Outputs: Similarity 0..100 (percent of shared mass).
*/
int colorsig_similarity(const uint8_t * p_a, const uint8_t * p_b);

#ifdef __cplusplus
}
#endif

#endif
