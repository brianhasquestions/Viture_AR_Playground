#include "keypoints.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CANON               (KEYPOINTS_CANON_PX)
#define LEVELS              (6)
#define LEVEL_SCALE         (1.25)
#define FAST_THRESHOLD_HI   (20)
#define FAST_THRESHOLD_LO   (8)
#define FAST_THRESHOLD_STEP (4)
#define FAST_WANT_CORNERS   (120)
#define FAST_ARC            (9)
#define RING_PX             (16)
#define BORDER_PX           (22)
#define PATCH_HALF          (15)
#define PAIR_COUNT          (256)
#define PAIR_RANGE          (13)
#define PER_LEVEL_CAP       (150)
#define LCG_SEED            (0x5EEDBEEFU)
#define LCG_MUL             (1664525U)
#define LCG_INC             (1013904223U)
#define ANGLE_STEPS         (256)
#define DEG_FULL            (360.0)
#define PI                  (3.14159265358979323846)
#define MAX_HAMMING         (80)
#define RATIO_NUM           (17)
#define RATIO_DEN           (20)
#define RANSAC_ITERATIONS   (250)
#define INLIER_TOL_PX       (7.0)
#define MIN_SCALE           (0.5)
#define MAX_SCALE           (2.0)
#define BITS_PER_BYTE       (8)
#define U16_MAX_VALUE       (65535)
#define HEADER_BYTES        (2)
#define PERCENT             (100)

typedef struct
{
    uint8_t * p_pix;
    int       side;
    double    scale;
} level_t;

typedef struct
{
    int x;
    int y;
    int score;
} corner_t;

typedef struct
{
    int8_t x0;
    int8_t y0;
    int8_t x1;
    int8_t y1;
} pair_t;

static pair_t g_pairs[PAIR_COUNT];
static int    g_pairs_ready = 0;
static const int RING_DX[RING_PX] =
{
    0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1
};
static const int RING_DY[RING_PX] =
{
    -3, -3, -2, -1, 0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3
};

static void build_pairs(void)
{
    uint32_t state = LCG_SEED;
    int      i     = 0;

    for (i = 0; i < PAIR_COUNT; i++)
    {
        int8_t v[4];
        int    k = 0;

        for (k = 0; k < 4; k++)
        {
            state = (state * LCG_MUL) + LCG_INC;
            v[k]  = (int8_t)((int)((state >> 16) % ((2 * PAIR_RANGE) + 1)) -
                             PAIR_RANGE);
        }
        g_pairs[i].x0 = v[0];
        g_pairs[i].y0 = v[1];
        g_pairs[i].x1 = v[2];
        g_pairs[i].y1 = v[3];
    }
    g_pairs_ready = 1;
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
    {
        v = lo;
    }
    if (v > hi)
    {
        v = hi;
    }

    return v;
}

static int gray_at(const gray_image_t * p_img, int x, int y)
{
    x = clamp_int(x, 0, p_img->width - 1);
    y = clamp_int(y, 0, p_img->height - 1);

    return p_img->p_gray[((size_t)y * (size_t)p_img->width) + (size_t)x];
}

static uint8_t sample_region(const gray_image_t * p_img,
                             const region_t * p_r, const double uv[2])
{
    double fx = (double)p_r->x + (uv[0] * (double)p_r->side) - 0.5;
    double fy = (double)p_r->y + (uv[1] * (double)p_r->side) - 0.5;
    int    x0 = (int)floor(fx);
    int    y0 = (int)floor(fy);
    double ax = fx - (double)x0;
    double ay = fy - (double)y0;
    double v  = ((1.0 - ax) * (1.0 - ay) * gray_at(p_img, x0, y0)) +
                (ax * (1.0 - ay) * gray_at(p_img, x0 + 1, y0)) +
                ((1.0 - ax) * ay * gray_at(p_img, x0, y0 + 1)) +
                (ax * ay * gray_at(p_img, x0 + 1, y0 + 1));

    return (uint8_t)(v + 0.5);
}

typedef struct
{
    const gray_image_t * p_img;
    const region_t *     p_r;
    int                  sub;
} canon_sampler_t;

static uint8_t canon_pixel(const canon_sampler_t * p_cs, int x, int y)
{
    long   sum = 0;
    int    sx  = 0;
    int    sy  = 0;
    int    sub = p_cs->sub;
    double uv[2];

    for (sy = 0; sy < sub; sy++)
    {
        for (sx = 0; sx < sub; sx++)
        {
            uv[0] = ((double)x + (((double)sx + 0.5) / sub)) / CANON;
            uv[1] = ((double)y + (((double)sy + 0.5) / sub)) / CANON;
            sum  += sample_region(p_cs->p_img, p_cs->p_r, uv);
        }
    }

    return (uint8_t)(sum / (sub * sub));
}

static int build_canonical(const gray_image_t * p_img,
                           const region_t * p_r, level_t * p_level)
{
    int             result = -1;
    int             x      = 0;
    int             y      = 0;
    canon_sampler_t cs;

    cs.p_img = p_img;
    cs.p_r   = p_r;
    cs.sub   = (p_r->side > CANON) ? (p_r->side / CANON) : 1;
    p_level->side  = CANON;
    p_level->scale = 1.0;
    p_level->p_pix = (uint8_t *)malloc((size_t)CANON * (size_t)CANON);
    if (NULL == p_level->p_pix)
    {
        goto cleanup;
    }
    for (y = 0; y < CANON; y++)
    {
        for (x = 0; x < CANON; x++)
        {
            p_level->p_pix[((size_t)y * CANON) + (size_t)x] =
                canon_pixel(&cs, x, y);
        }
    }
    result = 0;

cleanup:

    return result;
}

static uint8_t box_average(const level_t * p_from, const int box[4])
{
    long sum = 0;
    long n   = 0;
    int  sx  = 0;
    int  sy  = 0;

    for (sy = box[1]; (sy < box[3]) && (sy < p_from->side); sy++)
    {
        for (sx = box[0]; (sx < box[2]) && (sx < p_from->side); sx++)
        {
            sum += p_from->p_pix[((size_t)sy * p_from->side) + sx];
            n++;
        }
    }
    sum = (n > 0) ? (sum / n) : 0;

    return (uint8_t)sum;
}

static int build_downscaled(const level_t * p_from, level_t * p_to)
{
    int    result = -1;
    int    x      = 0;
    int    y      = 0;
    double f      = (double)p_from->side / (double)p_to->side;
    int    box[4];

    p_to->p_pix = (uint8_t *)malloc((size_t)p_to->side * (size_t)p_to->side);
    if (NULL == p_to->p_pix)
    {
        goto cleanup;
    }
    for (y = 0; y < p_to->side; y++)
    {
        for (x = 0; x < p_to->side; x++)
        {
            box[0] = (int)(x * f);
            box[1] = (int)(y * f);
            box[2] = (int)((x + 1) * f);
            box[3] = (int)((y + 1) * f);
            if (box[2] <= box[0]) { box[2] = box[0] + 1; }
            if (box[3] <= box[1]) { box[3] = box[1] + 1; }
            p_to->p_pix[((size_t)y * p_to->side) + (size_t)x] =
                box_average(p_from, box);
        }
    }
    result = 0;

cleanup:

    return result;
}

static int pix(const level_t * p_l, int x, int y)
{
    return p_l->p_pix[((size_t)y * (size_t)p_l->side) + (size_t)x];
}

typedef struct
{
    int x;
    int y;
    int threshold;
} probe_t;

static int fast_score(const level_t * p_l, const probe_t * p_probe)
{
    int x       = p_probe->x;
    int y       = p_probe->y;
    int threshold = p_probe->threshold;
    int centre  = pix(p_l, x, y);
    int i       = 0;
    int run_br  = 0;
    int run_dk  = 0;
    int best_br = 0;
    int best_dk = 0;
    int score   = 0;

    for (i = 0; i < (RING_PX + FAST_ARC); i++)
    {
        int k = i % RING_PX;
        int v = pix(p_l, x + RING_DX[k], y + RING_DY[k]);

        if (v > (centre + threshold))
        {
            run_br++;
            run_dk = 0;
        }
        else if (v < (centre - threshold))
        {
            run_dk++;
            run_br = 0;
        }
        else
        {
            run_br = 0;
            run_dk = 0;
        }
        if (run_br > best_br) { best_br = run_br; }
        if (run_dk > best_dk) { best_dk = run_dk; }
    }
    if ((best_br < FAST_ARC) && (best_dk < FAST_ARC))
    {
        goto cleanup;
    }
    for (i = 0; i < RING_PX; i++)
    {
        score += abs(pix(p_l, x + RING_DX[i], y + RING_DY[i]) - centre);
    }

cleanup:

    return score;
}

static int cmp_corner(const void * p_a, const void * p_b)
{
    const corner_t * a = (const corner_t *)p_a;
    const corner_t * b = (const corner_t *)p_b;

    return b->score - a->score;
}

static void score_level(const level_t * p_l, int threshold, int * p_scores)
{
    int     side = p_l->side;
    probe_t probe;

    probe.threshold = threshold;
    for (probe.y = BORDER_PX; probe.y < (side - BORDER_PX); probe.y++)
    {
        for (probe.x = BORDER_PX; probe.x < (side - BORDER_PX); probe.x++)
        {
            p_scores[((size_t)probe.y * side) + probe.x] =
                fast_score(p_l, &probe);
        }
    }
}

static int detect_level(const level_t * p_l, corner_t * p_out, int cap)
{
    int * p_scores  = NULL;
    int   count     = 0;
    int   x         = 0;
    int   y         = 0;
    int   side      = p_l->side;
    int   threshold = FAST_THRESHOLD_HI;
    int   passing   = 0;

    p_scores = (int *)calloc((size_t)side * (size_t)side, sizeof(int));
    if (NULL == p_scores)
    {
        goto cleanup;
    }
    while (threshold >= FAST_THRESHOLD_LO)
    {
        size_t i = 0;

        score_level(p_l, threshold, p_scores);
        passing = 0;
        for (i = 0; i < ((size_t)side * (size_t)side); i++)
        {
            passing += (p_scores[i] > 0) ? 1 : 0;
        }
        if (passing >= FAST_WANT_CORNERS)
        {
            break;
        }
        threshold -= FAST_THRESHOLD_STEP;
    }
    for (y = BORDER_PX; y < (side - BORDER_PX); y++)
    {
        for (x = BORDER_PX; x < (side - BORDER_PX); x++)
        {
            int s = p_scores[((size_t)y * side) + x];

            if ((s <= 0) ||
                (s < p_scores[((size_t)(y - 1) * side) + x]) ||
                (s < p_scores[((size_t)(y + 1) * side) + x]) ||
                (s < p_scores[((size_t)y * side) + x - 1]) ||
                (s <= p_scores[((size_t)y * side) + x + 1]))
            {
                continue;
            }
            if (count < cap)
            {
                p_out[count].x     = x;
                p_out[count].y     = y;
                p_out[count].score = s;
                count++;
            }
            else if (s > p_out[cap - 1].score)
            {
                p_out[cap - 1].x     = x;
                p_out[cap - 1].y     = y;
                p_out[cap - 1].score = s;
                qsort(p_out, (size_t)cap, sizeof(corner_t), cmp_corner);
            }
        }
    }
    qsort(p_out, (size_t)count, sizeof(corner_t), cmp_corner);

cleanup:
    if (NULL != p_scores)
    {
        free(p_scores);
    }

    return count;
}

static uint8_t orientation(const level_t * p_l, int cx, int cy)
{
    long   m01 = 0;
    long   m10 = 0;
    int    dx  = 0;
    int    dy  = 0;
    double a   = 0.0;

    for (dy = -PATCH_HALF; dy <= PATCH_HALF; dy++)
    {
        for (dx = -PATCH_HALF; dx <= PATCH_HALF; dx++)
        {
            int v = pix(p_l, cx + dx, cy + dy);

            m10 += (long)dx * v;
            m01 += (long)dy * v;
        }
    }
    a = atan2((double)m01, (double)m10);
    if (a < 0.0)
    {
        a += 2.0 * PI;
    }

    return (uint8_t)((int)((a / (2.0 * PI)) * ANGLE_STEPS) % ANGLE_STEPS);
}

static int blurred(const level_t * p_l, int x, int y)
{
    int sum = 0;
    int dx  = 0;
    int dy  = 0;

    for (dy = -2; dy <= 2; dy++)
    {
        for (dx = -2; dx <= 2; dx++)
        {
            sum += pix(p_l, x + dx, y + dy);
        }
    }

    return sum / 25;
}

static void describe(const level_t * p_l, const corner_t * p_c,
                     keypoint_t * p_kp)
{
    double a  = ((double)p_kp->angle / ANGLE_STEPS) * 2.0 * PI;
    double ca = cos(a);
    double sa = sin(a);
    int    i  = 0;

    memset(p_kp->desc, 0, KEYPOINTS_DESC_BYTES);
    for (i = 0; i < PAIR_COUNT; i++)
    {
        const pair_t * p = &g_pairs[i];
        int x0 = p_c->x + (int)lround((p->x0 * ca) - (p->y0 * sa));
        int y0 = p_c->y + (int)lround((p->x0 * sa) + (p->y0 * ca));
        int x1 = p_c->x + (int)lround((p->x1 * ca) - (p->y1 * sa));
        int y1 = p_c->y + (int)lround((p->x1 * sa) + (p->y1 * ca));

        if (blurred(p_l, x0, y0) < blurred(p_l, x1, y1))
        {
            p_kp->desc[i / BITS_PER_BYTE] |=
                (uint8_t)(1U << (i % BITS_PER_BYTE));
        }
    }
}

static void free_levels(level_t * p_levels)
{
    int i = 0;

    for (i = 0; i < LEVELS; i++)
    {
        if (NULL != p_levels[i].p_pix)
        {
            free(p_levels[i].p_pix);
            p_levels[i].p_pix = NULL;
        }
    }
}

static int extract_level(const level_t * p_l, int level,
                         keypoint_set_t * p_out)
{
    int        result    = -1;
    int        found     = 0;
    int        i         = 0;
    corner_t * p_corners = NULL;

    p_corners = (corner_t *)calloc(PER_LEVEL_CAP, sizeof(corner_t));
    if (NULL == p_corners)
    {
        goto cleanup;
    }
    found = detect_level(p_l, p_corners, PER_LEVEL_CAP);
    for (i = 0; (i < found) && (p_out->count < KEYPOINTS_MAX_KEYPOINTS); i++)
    {
        keypoint_t * p_kp = &p_out->keypoints[p_out->count];
        double       sx   = (double)p_corners[i].x * p_l->scale;
        double       sy   = (double)p_corners[i].y * p_l->scale;

        p_kp->x     = (uint16_t)((sx > U16_MAX_VALUE) ? U16_MAX_VALUE : sx);
        p_kp->y     = (uint16_t)((sy > U16_MAX_VALUE) ? U16_MAX_VALUE : sy);
        p_kp->level = (uint8_t)level;
        p_kp->angle = orientation(p_l, p_corners[i].x, p_corners[i].y);
        describe(p_l, &p_corners[i], p_kp);
        p_out->count++;
    }
    result = 0;

cleanup:
    if (NULL != p_corners)
    {
        free(p_corners);
    }

    return result;
}

static void widen_region(const gray_image_t * p_img, const region_t * p_in,
                         region_t * p_out)
{
    int side = (p_in->side * KEYPOINTS_CONTEXT_PERCENT) / PERCENT;
    int max  = (p_img->width < p_img->height) ? p_img->width
                                              : p_img->height;
    int cx   = p_in->x + (p_in->side / 2);
    int cy   = p_in->y + (p_in->side / 2);

    if (side < KEYPOINTS_MIN_CONTEXT_PX)
    {
        side = KEYPOINTS_MIN_CONTEXT_PX;
    }
    if (side > max)
    {
        side = max;
    }
    p_out->side = side;
    p_out->x    = clamp_int(cx - (side / 2), 0, p_img->width - side);
    p_out->y    = clamp_int(cy - (side / 2), 0, p_img->height - side);
}

int keypoints_extract(const gray_image_t * p_img, const region_t * p_region,
                     keypoint_set_t * p_out)
{
    int      result = -1;
    int      level  = 0;
    region_t wide;
    level_t  levels[LEVELS];

    memset(levels, 0, sizeof(levels));
    if ((NULL == p_img) || (NULL == p_img->p_gray) || (NULL == p_region) ||
        (NULL == p_out) || (p_region->side <= 0))
    {
        goto cleanup;
    }
    if (0 == g_pairs_ready)
    {
        build_pairs();
    }
    p_out->count = 0;
    widen_region(p_img, p_region, &wide);
    if (0 != build_canonical(p_img, &wide, &levels[0]))
    {
        goto cleanup;
    }
    for (level = 1; level < LEVELS; level++)
    {
        levels[level].scale = levels[level - 1].scale * LEVEL_SCALE;
        levels[level].side  = (int)((double)CANON / levels[level].scale);
        if (0 != build_downscaled(&levels[level - 1], &levels[level]))
        {
            goto cleanup;
        }
    }
    for (level = 0; level < LEVELS; level++)
    {
        if (0 != extract_level(&levels[level], level, p_out))
        {
            goto cleanup;
        }
    }
    result = 0;

cleanup:
    free_levels(levels);

    return result;
}

static int hamming(const uint8_t * p_a, const uint8_t * p_b)
{
    int i = 0;
    int d = 0;

    for (i = 0; i < KEYPOINTS_DESC_BYTES; i++)
    {
        uint8_t x = (uint8_t)(p_a[i] ^ p_b[i]);

        while (0U != x)
        {
            x = (uint8_t)(x & (x - 1U));
            d++;
        }
    }

    return d;
}

typedef struct
{
    int live;
    int stored;
} pair_index_t;

static int ratio_matches(const keypoint_set_t * p_live,
                         const keypoint_set_t * p_stored,
                         pair_index_t * p_pairs)
{
    int count = 0;
    int i     = 0;
    int j     = 0;

    for (i = 0; i < p_live->count; i++)
    {
        int best   = MAX_HAMMING + 1;
        int second = MAX_HAMMING + 1;
        int best_j = -1;

        for (j = 0; j < p_stored->count; j++)
        {
            int d = hamming(p_live->keypoints[i].desc,
                            p_stored->keypoints[j].desc);

            if (d < best)
            {
                second = best;
                best   = d;
                best_j = j;
            }
            else if (d < second)
            {
                second = d;
            }
        }
        if ((best_j >= 0) && (best <= MAX_HAMMING) &&
            ((best * RATIO_DEN) < (second * RATIO_NUM)))
        {
            p_pairs[count].live   = i;
            p_pairs[count].stored = best_j;
            count++;
        }
    }

    return count;
}

typedef struct
{
    double a;
    double b;
    double tx;
    double ty;
} similarity_t;

typedef struct
{
    const keypoint_set_t * p_live;
    const keypoint_set_t * p_stored;
    const pair_index_t *  p_pairs;
    int                   count;
} ransac_input_t;

typedef struct
{
    const keypoint_t * p_live;
    const keypoint_t * p_stored;
} keypoint_pair_t;

static int fit_similarity(const keypoint_pair_t * p_a,
                          const keypoint_pair_t * p_b, similarity_t * p_t)
{
    const keypoint_t * p_l0    = p_a->p_live;
    const keypoint_t * p_s0    = p_a->p_stored;
    const keypoint_t * p_l1    = p_b->p_live;
    const keypoint_t * p_s1    = p_b->p_stored;
    int                result = -1;
    double dxs    = (double)p_s1->x - (double)p_s0->x;
    double dys    = (double)p_s1->y - (double)p_s0->y;
    double dxl    = (double)p_l1->x - (double)p_l0->x;
    double dyl    = (double)p_l1->y - (double)p_l0->y;
    double den    = (dxs * dxs) + (dys * dys);
    double scale  = 0.0;

    if (den < 1.0)
    {
        goto cleanup;
    }
    p_t->a  = ((dxl * dxs) + (dyl * dys)) / den;
    p_t->b  = ((dyl * dxs) - (dxl * dys)) / den;
    scale   = sqrt((p_t->a * p_t->a) + (p_t->b * p_t->b));
    if ((scale < MIN_SCALE) || (scale > MAX_SCALE))
    {
        goto cleanup;
    }
    p_t->tx = (double)p_l0->x - ((p_t->a * p_s0->x) - (p_t->b * p_s0->y));
    p_t->ty = (double)p_l0->y - ((p_t->b * p_s0->x) + (p_t->a * p_s0->y));
    result  = 0;

cleanup:

    return result;
}

static int count_inliers(const ransac_input_t * p_in,
                         const similarity_t * p_t)
{
    int inliers = 0;
    int i       = 0;

    for (i = 0; i < p_in->count; i++)
    {
        const keypoint_t * l = &p_in->p_live->keypoints[
            p_in->p_pairs[i].live];
        const keypoint_t * s = &p_in->p_stored->keypoints[
            p_in->p_pairs[i].stored];
        double px = (p_t->a * s->x) - (p_t->b * s->y) + p_t->tx;
        double py = (p_t->b * s->x) + (p_t->a * s->y) + p_t->ty;
        double ex = px - (double)l->x;
        double ey = py - (double)l->y;

        if (((ex * ex) + (ey * ey)) <= (INLIER_TOL_PX * INLIER_TOL_PX))
        {
            inliers++;
        }
    }

    return inliers;
}


static int fit_pair(const ransac_input_t * p_in, const int idx[2],
                    similarity_t * p_t)
{
    int                i0 = idx[0];
    int                i1 = idx[1];
    const keypoint_t * l0 = &p_in->p_live->keypoints[p_in->p_pairs[i0].live];
    const keypoint_t * s0 = &p_in->p_stored->keypoints[
        p_in->p_pairs[i0].stored];
    const keypoint_t * l1 = &p_in->p_live->keypoints[p_in->p_pairs[i1].live];
    const keypoint_t * s1 = &p_in->p_stored->keypoints[
        p_in->p_pairs[i1].stored];
    keypoint_pair_t    a;
    keypoint_pair_t    b;

    a.p_live   = l0;
    a.p_stored = s0;
    b.p_live   = l1;
    b.p_stored = s1;

    return fit_similarity(&a, &b, p_t);
}

static int ransac(const ransac_input_t * p_in)
{
    uint32_t     state = LCG_SEED;
    int          best  = 0;
    int          iter  = 0;
    similarity_t t;

    for (iter = 0; iter < RANSAC_ITERATIONS; iter++)
    {
        int idx[2];
        int n = 0;

        state  = (state * LCG_MUL) + LCG_INC;
        idx[0] = (int)((state >> 8) % (uint32_t)p_in->count);
        state  = (state * LCG_MUL) + LCG_INC;
        idx[1] = (int)((state >> 8) % (uint32_t)p_in->count);
        if (idx[0] == idx[1])
        {
            continue;
        }
        if (0 != fit_pair(p_in, idx, &t))
        {
            continue;
        }
        n = count_inliers(p_in, &t);
        if (n > best)
        {
            best = n;
        }
    }

    return best;
}

void keypoints_match(const keypoint_set_t * p_live,
                    const keypoint_set_t * p_stored,
                    keypoint_match_t * p_result)
{
    pair_index_t * p_pairs = NULL;
    ransac_input_t in;

    if (NULL == p_result)
    {
        goto cleanup;
    }
    memset(p_result, 0, sizeof(*p_result));
    if ((NULL == p_live) || (NULL == p_stored))
    {
        goto cleanup;
    }
    p_result->usable = ((p_live->count >= KEYPOINTS_MIN_KEYPOINTS) &&
                        (p_stored->count >= KEYPOINTS_MIN_KEYPOINTS)) ? 1 : 0;
    p_pairs = (pair_index_t *)calloc(KEYPOINTS_MAX_KEYPOINTS,
                                     sizeof(pair_index_t));
    if (NULL == p_pairs)
    {
        goto cleanup;
    }
    p_result->candidates = ratio_matches(p_live, p_stored, p_pairs);
    if (p_result->candidates < 2)
    {
        goto cleanup;
    }
    in.p_live   = p_live;
    in.p_stored = p_stored;
    in.p_pairs  = p_pairs;
    in.count    = p_result->candidates;
    p_result->inliers = ransac(&in);

cleanup:
    if (NULL != p_pairs)
    {
        free(p_pairs);
    }
}

int keypoints_write(const keypoint_set_t * p_set, byte_buf_t * p_out)
{
    int       result = -1;
    int       i      = 0;
    size_t    need   = 0;
    uint8_t * p      = NULL;

    if ((NULL == p_set) || (NULL == p_out) || (NULL == p_out->p_data) ||
        (p_set->count < 0) || (p_set->count > KEYPOINTS_MAX_KEYPOINTS))
    {
        goto cleanup;
    }
    need = HEADER_BYTES + ((size_t)p_set->count * KEYPOINTS_KEYPOINT_BYTES);
    if (p_out->cap < need)
    {
        goto cleanup;
    }
    p    = p_out->p_data;
    p[0] = (uint8_t)(p_set->count >> BITS_PER_BYTE);
    p[1] = (uint8_t)(p_set->count & 0xFFU);
    p   += HEADER_BYTES;
    for (i = 0; i < p_set->count; i++)
    {
        const keypoint_t * k = &p_set->keypoints[i];

        p[0] = (uint8_t)(k->x >> BITS_PER_BYTE);
        p[1] = (uint8_t)(k->x & 0xFFU);
        p[2] = (uint8_t)(k->y >> BITS_PER_BYTE);
        p[3] = (uint8_t)(k->y & 0xFFU);
        p[4] = k->angle;
        p[5] = k->level;
        memcpy(p + 6, k->desc, KEYPOINTS_DESC_BYTES);
        p += KEYPOINTS_KEYPOINT_BYTES;
    }
    p_out->len = need;
    result     = 0;

cleanup:

    return result;
}

int keypoints_read(byte_span_t in, keypoint_set_t * p_out, size_t * p_used)
{
    int             result = -1;
    int             i      = 0;
    int             count  = 0;
    const uint8_t * p      = in.p_data;

    if ((NULL == p) || (NULL == p_out) || (NULL == p_used) ||
        (in.len < HEADER_BYTES))
    {
        goto cleanup;
    }
    count = ((int)p[0] << BITS_PER_BYTE) | (int)p[1];
    if ((count > KEYPOINTS_MAX_KEYPOINTS) ||
        (in.len < (HEADER_BYTES + ((size_t)count * KEYPOINTS_KEYPOINT_BYTES))))
    {
        goto cleanup;
    }
    p += HEADER_BYTES;
    for (i = 0; i < count; i++)
    {
        keypoint_t * k = &p_out->keypoints[i];

        k->x     = (uint16_t)(((uint16_t)p[0] << BITS_PER_BYTE) | p[1]);
        k->y     = (uint16_t)(((uint16_t)p[2] << BITS_PER_BYTE) | p[3]);
        k->angle = p[4];
        k->level = p[5];
        memcpy(k->desc, p + 6, KEYPOINTS_DESC_BYTES);
        p += KEYPOINTS_KEYPOINT_BYTES;
    }
    p_out->count = count;
    *p_used      = HEADER_BYTES + ((size_t)count * KEYPOINTS_KEYPOINT_BYTES);
    result       = 0;

cleanup:

    return result;
}
