#include "fingerprint.h"

#include "colorsig.h"
#include "locate.h"

#include "jpeg_decode.h"

#include <stdlib.h>
#include <string.h>

#define QUARTER             (4)
#define RGB_PIXEL_BYTES     (3)
#define COUNT_BYTES         (1)
#define HASH_HEAD_BYTES     (PHASH_BYTES + COUNT_BYTES)

typedef struct
{
    const gray_image_t * p_gray;
    const rgb_image_t *  p_colour;
} images_t;

static int hash_and_colour(const images_t * p_im, fingerprint_t * p_out,
                           region_t * p_region)
{
    const gray_image_t * p_gray   = p_im->p_gray;
    const rgb_image_t *  p_colour = p_im->p_colour;
    int                  result   = -1;
    region_t             scaled;

    if (0 != locate_object_colour(p_gray, p_colour, p_region))
    {
        goto cleanup;
    }
    if (0 != phash_set_hash_region(p_gray, p_region, &p_out->hashes))
    {
        goto cleanup;
    }
    scaled.x    = p_region->x / QUARTER;
    scaled.y    = p_region->y / QUARTER;
    scaled.side = p_region->side / QUARTER;
    result      = colorsig_compute(p_colour, &scaled,
                                   p_out->hashes.colour);

cleanup:

    return result;
}

int fingerprint_from_jpeg(byte_span_t jpeg, fingerprint_t * p_out)
{
    int          result = -1;
    jpeg_image_t decoded;
    jpeg_rgb_t   small;
    gray_image_t gray;
    rgb_image_t  colour;
    region_t     region;
    images_t     im;

    memset(&decoded, 0, sizeof(decoded));
    memset(&small, 0, sizeof(small));
    if (NULL == p_out)
    {
        goto cleanup;
    }
    memset(p_out, 0, sizeof(*p_out));
    if (0 != jpeg_decode_gray(jpeg.p_data, jpeg.len, &decoded))
    {
        goto cleanup;
    }
    if (0 != jpeg_decode_rgb_quarter(jpeg.p_data, jpeg.len, &small))
    {
        goto cleanup;
    }
    gray.p_gray        = decoded.p_gray;
    gray.width         = decoded.width;
    gray.height        = decoded.height;
    colour.p_rgb       = small.p_rgb;
    colour.width       = small.width;
    colour.height      = small.height;
    colour.pixel_bytes = RGB_PIXEL_BYTES;
    im.p_gray   = &gray;
    im.p_colour = &colour;
    if (0 != hash_and_colour(&im, p_out, &region))
    {
        goto cleanup;
    }
    result = keypoints_extract(&gray, &region, &p_out->points);

cleanup:
    jpeg_image_free(&decoded);
    jpeg_rgb_free(&small);

    return result;
}

fingerprint_score_t fingerprint_compare(const fingerprint_t * p_live,
                                        const fingerprint_t * p_stored,
                                        int max_dist)
{
    fingerprint_score_t score;
    keypoint_match_t    km;

    memset(&score, 0, sizeof(score));
    memset(&km, 0, sizeof(km));
    if ((NULL == p_live) || (NULL == p_stored))
    {
        goto cleanup;
    }
    score.hash_score = phash_set_match(&p_live->hashes, &p_stored->hashes);
    keypoints_match(&p_live->points, &p_stored->points, &km);
    score.inliers   = km.inliers;
    score.by_points = km.usable;
    if (0 != km.usable)
    {
        score.matched = (km.inliers >= KEYPOINTS_MIN_INLIERS) ? 1 : 0;
    }
    else
    {
        score.matched = (score.hash_score <= max_dist) ? 1 : 0;
    }

cleanup:

    return score;
}

int fingerprint_write(const fingerprint_t * p_fp, byte_buf_t * p_out)
{
    int        result = -1;
    size_t     pos    = 0;
    uint8_t *  p      = NULL;
    byte_buf_t rest;
    size_t     hash_bytes = 0;

    if ((NULL == p_fp) || (NULL == p_out) || (NULL == p_out->p_data) ||
        (p_fp->hashes.count < 0) ||
        (p_fp->hashes.count > PHASH_MAX_VARIANTS))
    {
        goto cleanup;
    }
    hash_bytes = HASH_HEAD_BYTES +
                 ((size_t)p_fp->hashes.count * PHASH_BYTES) + COLORSIG_BINS;
    if (p_out->cap < hash_bytes)
    {
        goto cleanup;
    }
    p = p_out->p_data;
    memcpy(p, p_fp->hashes.primary, PHASH_BYTES);
    pos     = PHASH_BYTES;
    p[pos]  = (uint8_t)p_fp->hashes.count;
    pos    += COUNT_BYTES;
    memcpy(p + pos, p_fp->hashes.variants,
           (size_t)p_fp->hashes.count * PHASH_BYTES);
    pos += (size_t)p_fp->hashes.count * PHASH_BYTES;
    memcpy(p + pos, p_fp->hashes.colour, COLORSIG_BINS);
    pos += COLORSIG_BINS;
    rest.p_data = p + pos;
    rest.cap    = p_out->cap - pos;
    rest.len    = 0;
    if (0 != keypoints_write(&p_fp->points, &rest))
    {
        goto cleanup;
    }
    p_out->len = pos + rest.len;
    result     = 0;

cleanup:

    return result;
}

int fingerprint_read(byte_span_t in, fingerprint_t * p_out)
{
    int         result = -1;
    size_t      pos    = 0;
    size_t      used   = 0;
    size_t      count  = 0;
    byte_span_t rest;

    if ((NULL == in.p_data) || (NULL == p_out) || (in.len < HASH_HEAD_BYTES))
    {
        goto cleanup;
    }
    memset(p_out, 0, sizeof(*p_out));
    memcpy(p_out->hashes.primary, in.p_data, PHASH_BYTES);
    pos   = PHASH_BYTES;
    count = in.p_data[pos];
    pos  += COUNT_BYTES;
    if ((count > PHASH_MAX_VARIANTS) ||
        (in.len < (pos + (count * PHASH_BYTES) + COLORSIG_BINS)))
    {
        goto cleanup;
    }
    memcpy(p_out->hashes.variants, in.p_data + pos, count * PHASH_BYTES);
    p_out->hashes.count = (int)count;
    pos += count * PHASH_BYTES;
    memcpy(p_out->hashes.colour, in.p_data + pos, COLORSIG_BINS);
    pos += COLORSIG_BINS;
    rest.p_data = in.p_data + pos;
    rest.len    = in.len - pos;
    result      = keypoints_read(rest, &p_out->points, &used);

cleanup:

    return result;
}
