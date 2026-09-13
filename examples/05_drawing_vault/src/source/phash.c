/**********************************************************************
 * @file    phash.c
 * @brief   Perceptual hash of a grayscale frame (drawing fingerprint).
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Method (classic DCT pHash):
 *   1. crop the central square, box-downscale to 32x32;
 *   2. take the top-left 8x8 low-frequency DCT coefficients;
 *   3. threshold each (except DC) at the median -> 64 bits.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "phash.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SMALL       (32)   /* Downscaled working size.        */
#define LOW         (8)    /* Low-frequency block kept.       */

/**********************************************************************
 * @brief  Box-average the central square of the image to SMALL x SMALL.
 **********************************************************************/
static void downscale_center(const uint8_t * p_gray, int w, int h,
                             double out[SMALL][SMALL])
{
    int side = (w < h) ? w : h;
    int ox   = (w - side) / 2;
    int oy   = (h - side) / 2;
    int by   = 0;
    int bx   = 0;

    for (by = 0; by < SMALL; by++)
    {
        for (bx = 0; bx < SMALL; bx++)
        {
            /* Source rectangle for this destination cell. */
            int x0 = ox + (int)(((long)bx * side) / SMALL);
            int x1 = ox + (int)(((long)(bx + 1) * side) / SMALL);
            int y0 = oy + (int)(((long)by * side) / SMALL);
            int y1 = oy + (int)(((long)(by + 1) * side) / SMALL);
            long sum = 0;
            long cnt = 0;
            int  yy  = 0;
            int  xx  = 0;

            if (x1 <= x0) { x1 = x0 + 1; }
            if (y1 <= y0) { y1 = y0 + 1; }
            for (yy = y0; (yy < y1) && (yy < h); yy++)
            {
                for (xx = x0; (xx < x1) && (xx < w); xx++)
                {
                    sum += p_gray[(yy * w) + xx];
                    cnt++;
                }
            }
            out[by][bx] = (cnt > 0) ? ((double)sum / (double)cnt) : 0.0;
        }
    }
}

/**********************************************************************
 * @brief  Compute the LOW x LOW top-left DCT-II coefficients of a
 *         SMALL x SMALL block.
 **********************************************************************/
static void dct_lowfreq(const double in[SMALL][SMALL],
                        double out[LOW][LOW])
{
    static double cos_t[LOW][SMALL];
    static int    init = 0;
    int           u = 0;
    int           v = 0;
    int           x = 0;
    int           y = 0;

    if (0 == init)
    {
        for (u = 0; u < LOW; u++)
        {
            for (x = 0; x < SMALL; x++)
            {
                cos_t[u][x] = cos(((2.0 * x + 1.0) * u *
                                   3.14159265358979323846) /
                                  (2.0 * SMALL));
            }
        }
        init = 1;
    }

    for (u = 0; u < LOW; u++)
    {
        for (v = 0; v < LOW; v++)
        {
            double s = 0.0;
            for (y = 0; y < SMALL; y++)
            {
                double row = 0.0;
                for (x = 0; x < SMALL; x++)
                {
                    row += in[y][x] * cos_t[v][x];
                }
                s += row * cos_t[u][y];
            }
            out[u][v] = s;
        }
    }
}

static int cmp_double(const void * a, const void * b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) { return -1; }
    if (da > db) { return 1; }
    return 0;
}

int phash_compute(const uint8_t * p_gray, int w, int h,
                  uint8_t p_hash[PHASH_BYTES])
{
    double small_img[SMALL][SMALL];
    double low[LOW][LOW];
    double vals[LOW * LOW];
    double sorted[(LOW * LOW) - 1];
    double median = 0.0;
    int    i      = 0;
    int    n      = 0;
    int    bit    = 0;

    if ((NULL == p_gray) || (w <= 0) || (h <= 0) || (NULL == p_hash))
    {
        return -1;
    }

    downscale_center(p_gray, w, h, small_img);
    dct_lowfreq((const double (*)[SMALL])small_img, low);

    /* Flatten, dropping the DC term (low[0][0]) for the median. */
    for (i = 0; i < (LOW * LOW); i++)
    {
        vals[i] = low[i / LOW][i % LOW];
    }
    for (i = 1; i < (LOW * LOW); i++)
    {
        sorted[i - 1] = vals[i];
    }
    n = (LOW * LOW) - 1;
    qsort(sorted, (size_t)n, sizeof(double), cmp_double);
    median = (sorted[n / 2] + sorted[(n - 1) / 2]) / 2.0;

    memset(p_hash, 0, PHASH_BYTES);
    for (bit = 0; bit < (LOW * LOW); bit++)
    {
        if (vals[bit] > median)
        {
            p_hash[bit / 8] |= (uint8_t)(1U << (7 - (bit % 8)));
        }
    }
    return 0;
}

int phash_distance(const uint8_t a[PHASH_BYTES],
                   const uint8_t b[PHASH_BYTES])
{
    int i    = 0;
    int dist = 0;

    for (i = 0; i < PHASH_BYTES; i++)
    {
        uint8_t x = (uint8_t)(a[i] ^ b[i]);
        while (0U != x)
        {
            dist += (int)(x & 1U);
            x = (uint8_t)(x >> 1);
        }
    }
    return dist;
}
