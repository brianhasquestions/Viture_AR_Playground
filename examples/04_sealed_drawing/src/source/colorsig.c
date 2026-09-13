#include "colorsig.h"

#include <string.h>

#define RGB_BYTES       (3)
#define HUE_DEGREES     (360)
#define HUE_SECTOR      (60)
#define SAT_CHROMATIC   (25)
#define SAT_STRONG      (55)
#define VALUE_DARK      (20)
#define PERCENT         (100)
#define BYTE_MAX        (255L)
#define GRAY_WEIGHT_NUM (1)
#define GRAY_WEIGHT_DEN (4)
#define MIN_CHROMA_PPM  (30)
#define PER_MILLE       (1000)

typedef struct
{
    int hue;
    int sat;
    int val;
} hsv_t;

static hsv_t to_hsv(const uint8_t * p)
{
    int   r   = p[0];
    int   g   = p[1];
    int   b   = p[2];
    int   max = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    int   min = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
    int   d   = max - min;
    hsv_t out;

    out.val = (max * PERCENT) / BYTE_MAX;
    out.sat = (0 == max) ? 0 : ((d * PERCENT) / max);
    out.hue = 0;
    if (0 != d)
    {
        if (max == r)
        {
            out.hue = (HUE_SECTOR * (g - b)) / d;
        }
        else if (max == g)
        {
            out.hue = (HUE_SECTOR * 2) + ((HUE_SECTOR * (b - r)) / d);
        }
        else
        {
            out.hue = (HUE_SECTOR * 4) + ((HUE_SECTOR * (r - g)) / d);
        }
        if (out.hue < 0)
        {
            out.hue += HUE_DEGREES;
        }
    }

    return out;
}

static int bin_of(hsv_t c)
{
    int bin = 0;

    if ((c.sat < SAT_CHROMATIC) || (c.val < VALUE_DARK))
    {
        bin = (COLORSIG_HUE_BINS * COLORSIG_SAT_LEVELS) +
              ((c.val * COLORSIG_GRAY_BINS) / (PERCENT + 1));
    }
    else
    {
        int hue_bin = (c.hue * COLORSIG_HUE_BINS) / HUE_DEGREES;
        int level   = (c.sat >= SAT_STRONG) ? 1 : 0;

        bin = (level * COLORSIG_HUE_BINS) + hue_bin;
    }

    return bin;
}

int colorsig_compute(const rgb_image_t * p_img, const region_t * p_region,
                     uint8_t * p_sig)
{
    int  result = -1;
    long counts[COLORSIG_BINS];
    long total        = 0;
    long chroma_total = 0;
    int  x0     = 0;
    int  y0     = 0;
    int  x1     = 0;
    int  y1     = 0;
    int  x      = 0;
    int  y      = 0;
    int  i      = 0;

    memset(counts, 0, sizeof(counts));
    if ((NULL == p_img) || (NULL == p_img->p_rgb) || (NULL == p_region) ||
        (NULL == p_sig))
    {
        goto cleanup;
    }
    x0 = (p_region->x < 0) ? 0 : p_region->x;
    y0 = (p_region->y < 0) ? 0 : p_region->y;
    x1 = p_region->x + p_region->side;
    y1 = p_region->y + p_region->side;
    if (x1 > p_img->width)
    {
        x1 = p_img->width;
    }
    if (y1 > p_img->height)
    {
        y1 = p_img->height;
    }
    for (y = y0; y < y1; y++)
    {
        const uint8_t * p_row = p_img->p_rgb +
                                ((size_t)y * (size_t)p_img->width *
                                 (size_t)p_img->pixel_bytes);

        for (x = x0; x < x1; x++)
        {
            counts[bin_of(to_hsv(p_row + ((size_t)x *
                                          (size_t)p_img->pixel_bytes)))]++;
            total++;
        }
    }
    if (0 == total)
    {
        goto cleanup;
    }
    for (i = 0; i < COLORSIG_BINS; i++)
    {
        long weighted = counts[i];

        if (i >= (COLORSIG_HUE_BINS * COLORSIG_SAT_LEVELS))
        {
            weighted = (weighted * GRAY_WEIGHT_NUM) / GRAY_WEIGHT_DEN;
        }
        chroma_total += weighted;
        counts[i]     = weighted;
    }
    if (chroma_total < ((total * MIN_CHROMA_PPM) / PER_MILLE))
    {
        chroma_total = (total * MIN_CHROMA_PPM) / PER_MILLE;
    }
    for (i = 0; i < COLORSIG_BINS; i++)
    {
        long v = (counts[i] * BYTE_MAX) / chroma_total;

        p_sig[i] = (uint8_t)((v > BYTE_MAX) ? BYTE_MAX : v);
    }
    result = 0;

cleanup:

    return result;
}

int colorsig_saturation(const uint8_t * p_pixel)
{
    hsv_t c = to_hsv(p_pixel);

    return c.sat;
}

int colorsig_similarity(const uint8_t * p_a, const uint8_t * p_b)
{
    long shared = 0;
    int  i      = 0;

    if ((NULL == p_a) || (NULL == p_b))
    {
        goto cleanup;
    }
    for (i = 0; i < COLORSIG_BINS; i++)
    {
        shared += (p_a[i] < p_b[i]) ? p_a[i] : p_b[i];
    }

cleanup:

    return (int)((shared * PERCENT) / BYTE_MAX);
}
