/*
Summary: Local feature matching for the located object, ORB-style and
         pure C. The object square is resampled to a fixed canonical
         size, a small image pyramid is built for scale tolerance, FAST
         corners are found on each level and given an orientation from
         the intensity centroid, and each corner gets a 256-bit rotated
         BRIEF descriptor. Two feature sets match when enough descriptor
         pairs agree on one similarity transform (scale, rotation,
         translation), verified by RANSAC. Unlike the global hash this
         survives tilt, rotation and partial views, and cannot be fooled
         by a scene that merely has similar colours.
*/

#ifndef KEYPOINTS_H
#define KEYPOINTS_H

#include "bytes.h"
#include "locate.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define KEYPOINTS_CANON_PX       (256)
#define KEYPOINTS_MAX_KEYPOINTS  (500)
#define KEYPOINTS_DESC_BYTES     (32)
#define KEYPOINTS_MIN_KEYPOINTS  (30)
#define KEYPOINTS_MIN_INLIERS    (10)
#define KEYPOINTS_KEYPOINT_BYTES (6 + KEYPOINTS_DESC_BYTES)

/*
Summary: One keypoint in canonical (256x256) object coordinates.
Inputs:  None.
Outputs: x, y      - position at level 0 scale.
         angle     - orientation, 0..255 for 0..360 degrees.
         level     - pyramid level it was found on.
         desc      - 256-bit rotated BRIEF descriptor.
*/
typedef struct
{
    uint16_t x;
    uint16_t y;
    uint8_t  angle;
    uint8_t  level;
    uint8_t  desc[KEYPOINTS_DESC_BYTES];
} keypoint_t;

/*
Summary: The feature set of one view of an object.
Inputs:  None.
Outputs: count     - keypoints used.
         keypoints - the keypoints.
*/
typedef struct
{
    int        count;
    keypoint_t keypoints[KEYPOINTS_MAX_KEYPOINTS];
} keypoint_set_t;

/*
Summary: Outcome of matching two feature sets.
Inputs:  None.
Outputs: inliers     - correspondences that agree with the best
                       similarity transform.
         candidates  - descriptor matches that passed the ratio test.
         usable      - non-zero when both sets had enough keypoints for
                       the result to mean anything.
*/
typedef struct
{
    int inliers;
    int candidates;
    int usable;
} keypoint_match_t;

#define KEYPOINTS_CONTEXT_PERCENT (250)
#define KEYPOINTS_MIN_CONTEXT_PX  (420)

/*
Summary: Extract features from around one square region of a grayscale
         image. The region is widened to KEYPOINTS_CONTEXT_PERCENT of
         its side (at least KEYPOINTS_MIN_CONTEXT_PX) so the whole
         object is covered even when the locator settled on one busy
         patch of it; background corners simply find no partner.
Inputs:  p_img    - the image.
         p_region - square around the object.
         p_out    - receives the feature set (heap-sized; allocate with
                    calloc, it is about 12 KB).
Outputs: 0 on success, -1 on bad input or allocation failure.
*/
int keypoints_extract(const gray_image_t * p_img, const region_t * p_region,
                     keypoint_set_t * p_out);

/*
Summary: Match two feature sets and verify the geometry.
Inputs:  p_live   - features of the current view.
         p_stored - features of the sealed view.
         p_result - receives the match outcome.
Outputs: None.
*/
void keypoints_match(const keypoint_set_t * p_live,
                    const keypoint_set_t * p_stored,
                    keypoint_match_t * p_result);

/*
Summary: Serialise a feature set into a buffer.
Inputs:  p_set - features.
         p_out - buffer with cap >= 2 + count * KEYPOINTS_KEYPOINT_BYTES.
Outputs: 0 on success with p_out->len set, -1 if too small.
*/
int keypoints_write(const keypoint_set_t * p_set, byte_buf_t * p_out);

/*
Summary: Parse a feature set written by keypoints_write().
Inputs:  in    - the bytes.
         p_out - receives the set.
         p_used - receives how many bytes were consumed.
Outputs: 0 on success, -1 on malformed input.
*/
int keypoints_read(byte_span_t in, keypoint_set_t * p_out, size_t * p_used);

#ifdef __cplusplus
}
#endif

#endif
