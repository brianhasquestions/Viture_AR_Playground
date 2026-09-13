/*
Summary: Perceptual hash of a camera frame: the object fingerprint.
         The square around the located object (see locate.h) is
         box-downscaled to 32x32,
         the top-left 8x8 low-frequency DCT block is taken, and each
         coefficient is thresholded at the block median to give 64 bits.
         Two views of the same drawing land a small Hamming distance
         apart; unrelated scenes land far apart. Tolerant of brightness,
         noise, scale and position in the view, but not rotation or
         strong perspective: look at the object from roughly the same
         angle it was sealed from.
*/

#ifndef PHASH_H
#define PHASH_H

#include "bytes.h"
#include "colorsig.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PHASH_BYTES     (8)
#define PHASH_BITS      (64)
#define PHASH_MAX_VARIANTS  (32)

struct region;

/*
Summary: How to look at a region before hashing it: rotated about its
         centre and zoomed. A view with angle 0 and scale 1 is the
         plain crop. Sealing stores hashes of several views so a later
         look at the object from a slightly different tilt or distance
         still matches.
Inputs:  p_region  - square around the object (a region_t from locate.h).
         angle_deg - rotation applied to the sampling grid.
         scale     - 1.0 samples the whole region; 0.85 samples the
                     central 85% (the object seen a little closer).
Outputs: None.
*/
typedef struct
{
    const struct region * p_region;
    double                angle_deg;
    double                scale;
} phash_view_t;

/*
Summary: A primary fingerprint plus its stored variants.
Inputs:  None.
Outputs: primary  - hash of the plain view; used as the sealing aad.
         variants - hashes of the rotated/zoomed views.
         count    - number of variants.
         colour   - colour signature of the same region (colorsig.h).
*/
typedef struct
{
    uint8_t primary[PHASH_BYTES];
    uint8_t variants[PHASH_MAX_VARIANTS][PHASH_BYTES];
    int     count;
    uint8_t colour[COLORSIG_BINS];
} phash_set_t;

/*
Summary: Compute the 64-bit perceptual hash of one view of an image.
Inputs:  p_img  - the image.
         p_view - region, rotation and zoom to sample.
         p_hash - receives PHASH_BYTES bytes, big-endian bit order.
Outputs: 0 on success, -1 on bad input or a region outside the image.
*/
int phash_compute(const gray_image_t * p_img, const phash_view_t * p_view,
                  uint8_t * p_hash);

/*
Summary: Decode a baseline JPEG frame, locate the object, hash the
         plain view plus the rotated and zoomed variants, and take the
         colour signature.
Inputs:  jpeg  - the encoded frame (one MJPEG frame from the camera).
         p_set - receives the primary hash and variants.
Outputs: 0 on success, -1 if the frame does not decode.
*/
int phash_set_from_jpeg(byte_span_t jpeg, phash_set_t * p_set);

/*
Summary: Decode a frame, locate the object, and hash the plain view.
Inputs:  jpeg   - the encoded frame.
         p_hash - receives PHASH_BYTES bytes.
Outputs: 0 on success, -1 if the frame does not decode.
*/
int phash_from_jpeg(byte_span_t jpeg, uint8_t * p_hash);

/*
Summary: Smallest Hamming distance from a hash to any hash in a set
         (primary and variants).
Inputs:  p_hash - live fingerprint.
         p_set  - stored set.
Outputs: Distance 0..PHASH_BITS.
*/
int phash_set_distance(const uint8_t * p_hash, const phash_set_t * p_set);

#define PHASH_MATCH_DIVISOR (3)
#define PHASH_MATCH_MAX     ((PHASH_BITS + 100) / PHASH_MATCH_DIVISOR)

/*
Summary: Effective distance between a live fingerprint and a stored set:
         (smallest shape distance + colour dissimilarity in percent) / 3.
         Shape alone barely separates a painted object seen from two
         angles from an unrelated scene; its colours do, so the two
         cues carry similar weight. Same object: about 10-17. Unrelated
         scene: about 26 and up.
Inputs:  p_live   - live set (primary hash and colour are used).
         p_stored - stored set.
Outputs: Effective distance 0..PHASH_MATCH_MAX.
*/
int phash_set_match(const phash_set_t * p_live,
                    const phash_set_t * p_stored);

/*
Summary: Hamming distance between two fingerprints.
Inputs:  p_a - first fingerprint (PHASH_BYTES).
         p_b - second fingerprint (PHASH_BYTES).
Outputs: Number of differing bits, 0..PHASH_BITS.
*/
int phash_distance(const uint8_t * p_a, const uint8_t * p_b);

#ifdef __cplusplus
}
#endif

#endif
