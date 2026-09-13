#include "phash.h"

#include "jpeg_decode.h"
#include "locate.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SMALL           (32)
#define LOW             (8)
#define LOW_COUNT       (LOW * LOW)
#define AC_COUNT        (LOW_COUNT - 1)
#define BITS_PER_BYTE   (8)
#define TOP_BIT         (7)
#define PI              (3.14159265358979323846)
#define SUBSAMPLES      (4)
#define PERCENT_FULL    (100)
#define QUARTER         (4)
#define RGB_PIXEL_BYTES (3)
#define DEG_TO_RAD      (PI / 180.0)
#define VARIANT_ANGLES  (5)
#define VARIANT_SCALES  (3)

static double cos_table[LOW][SMALL];
static int    cos_table_ready = 0;

static const double VARIANT_ANGLE[VARIANT_ANGLES] =
{
    -16.0, -8.0, 0.0, 8.0, 16.0
};
static const double VARIANT_SCALE[VARIANT_SCALES] = { 0.85, 1.0, 1.3 };

static void build_cos_table(void)
{
    int u = 0;
    int x = 0;

    for (u = 0; u < LOW; u++)
    {
        for (x = 0; x < SMALL; x++)
        {
            cos_table[u][x] = cos((((2.0 * x) + 1.0) * u * PI) /
                                  (2.0 * SMALL));
        }
    }
    cos_table_ready = 1;
}

typedef struct
{
    double cx;
    double cy;
    double cos_a;
    double sin_a;
    double step;
} sampler_t;

static uint8_t sample_at(const gray_image_t * p_img, const sampler_t * p_s,
                         const double uv[2])
{
    double dx = (uv[0] - ((double)SMALL / 2.0)) * p_s->step;
    double dy = (uv[1] - ((double)SMALL / 2.0)) * p_s->step;
    int    x  = (int)(p_s->cx + (dx * p_s->cos_a) - (dy * p_s->sin_a));
    int    y  = (int)(p_s->cy + (dx * p_s->sin_a) + (dy * p_s->cos_a));

    if (x < 0)
    {
        x = 0;
    }
    if (y < 0)
    {
        y = 0;
    }
    if (x >= p_img->width)
    {
        x = p_img->width - 1;
    }
    if (y >= p_img->height)
    {
        y = p_img->height - 1;
    }

    return p_img->p_gray[((size_t)y * (size_t)p_img->width) + (size_t)x];
}

static double cell_average(const gray_image_t * p_img,
                           const sampler_t * p_s, const int cell[2])
{
    long   sum = 0;
    int    sy  = 0;
    int    sx  = 0;
    double uv[2];

    for (sy = 0; sy < SUBSAMPLES; sy++)
    {
        for (sx = 0; sx < SUBSAMPLES; sx++)
        {
            uv[0] = (double)cell[0] + (((double)sx + 0.5) / SUBSAMPLES);
            uv[1] = (double)cell[1] + (((double)sy + 0.5) / SUBSAMPLES);
            sum  += sample_at(p_img, p_s, uv);
        }
    }

    return (double)sum / (double)(SUBSAMPLES * SUBSAMPLES);
}

static void downscale_view(const gray_image_t * p_img,
                           const phash_view_t * p_view,
                           double out[SMALL][SMALL])
{
    const region_t * p_r = p_view->p_region;
    double           rad = p_view->angle_deg * DEG_TO_RAD;
    sampler_t        s;
    int              cell[2];

    s.cx    = (double)p_r->x + ((double)p_r->side / 2.0);
    s.cy    = (double)p_r->y + ((double)p_r->side / 2.0);
    s.cos_a = cos(rad);
    s.sin_a = sin(rad);
    s.step  = ((double)p_r->side * p_view->scale) / (double)SMALL;
    for (cell[1] = 0; cell[1] < SMALL; cell[1]++)
    {
        for (cell[0] = 0; cell[0] < SMALL; cell[0]++)
        {
            out[cell[1]][cell[0]] = cell_average(p_img, &s, cell);
        }
    }
}

static double dot_row(const double * p_row, const double * p_cos)
{
    double sum = 0.0;
    int    i   = 0;

    for (i = 0; i < SMALL; i++)
    {
        sum += p_row[i] * p_cos[i];
    }

    return sum;
}

static void dct_lowfreq(const double in[SMALL][SMALL],
                        double out[LOW][LOW])
{
    double tmp[SMALL][LOW];
    double column[SMALL];
    int    y = 0;
    int    u = 0;
    int    v = 0;

    if (0 == cos_table_ready)
    {
        build_cos_table();
    }
    for (y = 0; y < SMALL; y++)
    {
        for (v = 0; v < LOW; v++)
        {
            tmp[y][v] = dot_row(in[y], cos_table[v]);
        }
    }
    for (v = 0; v < LOW; v++)
    {
        for (y = 0; y < SMALL; y++)
        {
            column[y] = tmp[y][v];
        }
        for (u = 0; u < LOW; u++)
        {
            out[u][v] = dot_row(column, cos_table[u]);
        }
    }
}

static int cmp_double(const void * p_a, const void * p_b)
{
    double a      = *(const double *)p_a;
    double b      = *(const double *)p_b;
    int    result = 0;

    if (a < b)
    {
        result = -1;
    }
    else if (a > b)
    {
        result = 1;
    }

    return result;
}

int phash_compute(const gray_image_t * p_img, const phash_view_t * p_view,
                  uint8_t * p_hash)
{
    int              result   = -1;
    int              i        = 0;
    double           median   = 0.0;
    const region_t * p_region = NULL;
    double           small_img[SMALL][SMALL];
    double           low[LOW][LOW];
    double           flat[LOW_COUNT];
    double           sorted[AC_COUNT];

    if ((NULL == p_img) || (NULL == p_img->p_gray) || (NULL == p_hash) ||
        (NULL == p_view) || (NULL == p_view->p_region))
    {
        goto cleanup;
    }
    p_region = p_view->p_region;
    if ((p_region->side <= 0) || (p_view->scale <= 0.0) ||
        ((p_region->x + p_region->side) > p_img->width) ||
        ((p_region->y + p_region->side) > p_img->height))
    {
        goto cleanup;
    }
    downscale_view(p_img, p_view, small_img);
    dct_lowfreq((const double (*)[SMALL])small_img, low);

    for (i = 0; i < LOW_COUNT; i++)
    {
        flat[i] = low[i / LOW][i % LOW];
    }
    memcpy(sorted, &flat[1], sizeof(sorted));
    qsort(sorted, (size_t)AC_COUNT, sizeof(double), cmp_double);
    median = (sorted[AC_COUNT / 2] + sorted[(AC_COUNT - 1) / 2]) / 2.0;

    memset(p_hash, 0, PHASH_BYTES);
    for (i = 0; i < LOW_COUNT; i++)
    {
        if (flat[i] > median)
        {
            p_hash[i / BITS_PER_BYTE] |=
                (uint8_t)(1U << (TOP_BIT - (i % BITS_PER_BYTE)));
        }
    }
    result = 0;

cleanup:

    return result;
}

static int hash_variants(const gray_image_t * p_img,
                         const region_t * p_region, phash_set_t * p_set)
{
    int          result = -1;
    int          a      = 0;
    int          z      = 0;
    phash_view_t view;

    view.p_region  = p_region;
    view.angle_deg = 0.0;
    view.scale     = 1.0;
    p_set->count   = 0;
    if (0 != phash_compute(p_img, &view, p_set->primary))
    {
        goto cleanup;
    }
    for (z = 0; z < VARIANT_SCALES; z++)
    {
        for (a = 0; a < VARIANT_ANGLES; a++)
        {
            view.angle_deg = VARIANT_ANGLE[a];
            view.scale     = VARIANT_SCALE[z];
            if (0 != phash_compute(p_img, &view,
                                   p_set->variants[p_set->count]))
            {
                goto cleanup;
            }
            p_set->count++;
        }
    }
    result = 0;

cleanup:

    return result;
}


int phash_set_from_jpeg(byte_span_t jpeg, phash_set_t * p_set)
{
    int          result = -1;
    jpeg_image_t decoded;
    jpeg_rgb_t   small;
    gray_image_t view;
    rgb_image_t  colour;
    region_t     region;
    region_t     scaled;

    memset(&decoded, 0, sizeof(decoded));
    memset(&small, 0, sizeof(small));
    memset(&view, 0, sizeof(view));
    memset(&region, 0, sizeof(region));
    if (NULL == p_set)
    {
        goto cleanup;
    }
    memset(p_set, 0, sizeof(*p_set));
    if (0 != jpeg_decode_gray(jpeg.p_data, jpeg.len, &decoded))
    {
        goto cleanup;
    }
    if (0 != jpeg_decode_rgb_quarter(jpeg.p_data, jpeg.len, &small))
    {
        goto cleanup;
    }
    view.p_gray        = decoded.p_gray;
    view.width         = decoded.width;
    view.height        = decoded.height;
    colour.p_rgb       = small.p_rgb;
    colour.width       = small.width;
    colour.height      = small.height;
    colour.pixel_bytes = RGB_PIXEL_BYTES;
    if (0 != locate_object_colour(&view, &colour, &region))
    {
        goto cleanup;
    }
    if (0 != hash_variants(&view, &region, p_set))
    {
        goto cleanup;
    }
    scaled.x    = region.x / QUARTER;
    scaled.y    = region.y / QUARTER;
    scaled.side = region.side / QUARTER;
    result      = colorsig_compute(&colour, &scaled, p_set->colour);

cleanup:
    jpeg_image_free(&decoded);
    jpeg_rgb_free(&small);

    return result;
}

int phash_from_jpeg(byte_span_t jpeg, uint8_t * p_hash)
{
    int          result = -1;
    phash_set_t * p_set = NULL;

    p_set = (phash_set_t *)calloc(1U, sizeof(*p_set));
    if ((NULL == p_set) || (0 != phash_set_from_jpeg(jpeg, p_set)))
    {
        goto cleanup;
    }
    memcpy(p_hash, p_set->primary, PHASH_BYTES);
    result = 0;

cleanup:
    if (NULL != p_set)
    {
        free(p_set);
    }

    return result;
}

int phash_set_distance(const uint8_t * p_hash, const phash_set_t * p_set)
{
    int best = PHASH_BITS;
    int i    = 0;
    int d    = 0;

    if ((NULL == p_hash) || (NULL == p_set))
    {
        goto cleanup;
    }
    best = phash_distance(p_hash, p_set->primary);
    for (i = 0; i < p_set->count; i++)
    {
        d = phash_distance(p_hash, p_set->variants[i]);
        if (d < best)
        {
            best = d;
        }
    }

cleanup:

    return best;
}

int phash_set_match(const phash_set_t * p_live,
                    const phash_set_t * p_stored)
{
    int distance = PHASH_MATCH_MAX;
    int shape    = 0;
    int colour   = 0;

    if ((NULL == p_live) || (NULL == p_stored))
    {
        goto cleanup;
    }
    shape    = phash_set_distance(p_live->primary, p_stored);
    colour   = colorsig_similarity(p_live->colour, p_stored->colour);
    distance = (shape + (PERCENT_FULL - colour)) / PHASH_MATCH_DIVISOR;

cleanup:

    return distance;
}

int phash_distance(const uint8_t * p_a, const uint8_t * p_b)
{
    int i    = 0;
    int dist = 0;

    for (i = 0; i < PHASH_BYTES; i++)
    {
        uint8_t x = (uint8_t)(p_a[i] ^ p_b[i]);

        while (0U != x)
        {
            x = (uint8_t)(x & (x - 1U));
            dist++;
        }
    }

    return dist;
}
