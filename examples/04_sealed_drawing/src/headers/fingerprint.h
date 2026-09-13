/*
Summary: Everything that identifies one view of an object, and how two
         views are compared. A fingerprint carries the perceptual hash
         set and colour signature (phash.h) and the local keypoints
         (keypoints.h). Keypoints decide the match whenever both views
         have enough of them; the hash and colour score is the fallback
         for plain, texture-poor drawings.
*/

#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include "bytes.h"
#include "keypoints.h"
#include "phash.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define FINGERPRINT_MAX_BYTES   (4096 + (KEYPOINTS_MAX_KEYPOINTS * \
                                         KEYPOINTS_KEYPOINT_BYTES))

/*
Summary: One view's fingerprint. About 20 KB; allocate on the heap.
Inputs:  None.
Outputs: hashes - perceptual hash set and colour signature.
         points - local keypoints.
*/
typedef struct
{
    phash_set_t    hashes;
    keypoint_set_t points;
} fingerprint_t;

/*
Summary: How a live view compared with a stored one.
Inputs:  None.
Outputs: inliers    - geometrically consistent keypoint matches.
         hash_score - phash_set_match() score (lower is closer).
         by_points  - non-zero when keypoints decided the outcome.
         matched    - non-zero when the views are the same object.
*/
typedef struct
{
    int inliers;
    int hash_score;
    int by_points;
    int matched;
} fingerprint_score_t;

/*
Summary: Decode a frame, locate the object and fingerprint it.
Inputs:  jpeg  - the frame.
         p_out - receives the fingerprint.
Outputs: 0 on success, -1 if the frame does not decode.
*/
int fingerprint_from_jpeg(byte_span_t jpeg, fingerprint_t * p_out);

/*
Summary: Compare a live fingerprint with a stored one.
Inputs:  p_live   - current view.
         p_stored - sealed view.
         max_dist - hash-score threshold used when keypoints cannot
                    decide.
Outputs: The score; read its matched field for the verdict.
*/
fingerprint_score_t fingerprint_compare(const fingerprint_t * p_live,
                                        const fingerprint_t * p_stored,
                                        int max_dist);

/*
Summary: Serialise a fingerprint.
Inputs:  p_fp  - fingerprint.
         p_out - buffer with cap >= FINGERPRINT_MAX_BYTES.
Outputs: 0 on success with p_out->len set, -1 if too small.
*/
int fingerprint_write(const fingerprint_t * p_fp, byte_buf_t * p_out);

/*
Summary: Parse a fingerprint written by fingerprint_write().
Inputs:  in    - the bytes.
         p_out - receives the fingerprint.
Outputs: 0 on success, -1 on malformed input.
*/
int fingerprint_read(byte_span_t in, fingerprint_t * p_out);

#ifdef __cplusplus
}
#endif

#endif
