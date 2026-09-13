/**********************************************************************
 * @file    detect.c
 * @brief   Glyph vision layer: grayscale image -> decoded payloads.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "detect.h"

#include <stdlib.h>
#include <string.h>

/* A blob must occupy at least this fraction of the image to be a
 * candidate frame, and no more than the upper bound. */
#define MIN_AREA_FRAC   (0.0008)
#define MAX_AREA_FRAC   (0.9)

typedef struct
{
    double x;
    double y;
} pt_t;

/**********************************************************************
 * @brief  Otsu's method: pick the global threshold from the histogram.
 *
 * @return  Threshold in 0..255; pixels <= threshold are "dark".
 **********************************************************************/
static int otsu_threshold(const uint8_t * p_gray, int n)
{
    long   hist[256] = { 0 };
    int    i         = 0;
    long   total     = n;
    double sum       = 0.0;
    double sum_b     = 0.0;
    long   w_b       = 0;
    double max_var   = 0.0;
    int    thresh    = 127;

    for (i = 0; i < n; i++)
    {
        hist[p_gray[i]]++;
    }
    for (i = 0; i < 256; i++)
    {
        sum += (double)i * (double)hist[i];
    }
    for (i = 0; i < 256; i++)
    {
        double w_f    = 0.0;
        double m_b    = 0.0;
        double m_f    = 0.0;
        double between = 0.0;

        w_b += hist[i];
        if (0 == w_b)
        {
            continue;
        }
        w_f = (double)(total - w_b);
        if (0.0 == w_f)
        {
            break;
        }
        sum_b += (double)i * (double)hist[i];
        m_b = sum_b / (double)w_b;
        m_f = (sum - sum_b) / w_f;
        between = (double)w_b * w_f * (m_b - m_f) * (m_b - m_f);
        if (between > max_var)
        {
            max_var = between;
            thresh  = i;
        }
    }
    return thresh;
}

/**********************************************************************
 * @brief  Solve an n x n linear system in place (Gaussian elimination
 *         with partial pivoting). a is row-major n x (n+1) augmented.
 *
 * @return  0 on success, -1 if singular.
 **********************************************************************/
static int solve_linear(double * a, double * x, int n)
{
    int i = 0;
    int j = 0;
    int k = 0;

    for (i = 0; i < n; i++)
    {
        int    piv     = i;
        double best     = 0.0;
        double diag     = 0.0;

        for (k = i; k < n; k++)
        {
            double v = a[(k * (n + 1)) + i];
            double av = (v < 0.0) ? -v : v;
            if (av > best)
            {
                best = av;
                piv  = k;
            }
        }
        if (best < 1e-12)
        {
            return -1;
        }
        if (piv != i)
        {
            for (j = 0; j <= n; j++)
            {
                double t = a[(i * (n + 1)) + j];
                a[(i * (n + 1)) + j] = a[(piv * (n + 1)) + j];
                a[(piv * (n + 1)) + j] = t;
            }
        }
        diag = a[(i * (n + 1)) + i];
        for (k = i + 1; k < n; k++)
        {
            double f = a[(k * (n + 1)) + i] / diag;
            for (j = i; j <= n; j++)
            {
                a[(k * (n + 1)) + j] -= f * a[(i * (n + 1)) + j];
            }
        }
    }
    for (i = n - 1; i >= 0; i--)
    {
        double s = a[(i * (n + 1)) + n];
        for (j = i + 1; j < n; j++)
        {
            s -= a[(i * (n + 1)) + j] * x[j];
        }
        x[i] = s / a[(i * (n + 1)) + i];
    }
    return 0;
}

/**********************************************************************
 * @brief  Homography mapping four source points to four destinations.
 *
 * Solves for h (8 unknowns, h33 = 1) via the standard DLT set-up.
 *
 * @return  0 on success, -1 if degenerate.
 **********************************************************************/
static int homography(const pt_t src[4], const pt_t dst[4], double h[9])
{
    double a[8 * 9];
    double x[8];
    int    i = 0;

    memset(a, 0, sizeof(a));
    for (i = 0; i < 4; i++)
    {
        double * r0 = &a[((i * 2) + 0) * 9];
        double * r1 = &a[((i * 2) + 1) * 9];

        r0[0] = src[i].x; r0[1] = src[i].y; r0[2] = 1.0;
        r0[6] = -src[i].x * dst[i].x;
        r0[7] = -src[i].y * dst[i].x;
        r0[8] =  dst[i].x;

        r1[3] = src[i].x; r1[4] = src[i].y; r1[5] = 1.0;
        r1[6] = -src[i].x * dst[i].y;
        r1[7] = -src[i].y * dst[i].y;
        r1[8] =  dst[i].y;
    }
    if (0 != solve_linear(a, x, 8))
    {
        return -1;
    }
    for (i = 0; i < 8; i++)
    {
        h[i] = x[i];
    }
    h[8] = 1.0;
    return 0;
}

static void apply_h(const double h[9], double u, double v,
                    double * p_x, double * p_y)
{
    double w = (h[6] * u) + (h[7] * v) + h[8];
    if (0.0 == w)
    {
        w = 1e-9;
    }
    *p_x = ((h[0] * u) + (h[1] * v) + h[2]) / w;
    *p_y = ((h[3] * u) + (h[4] * v) + h[5]) / w;
}

/**********************************************************************
 * @brief  Sample the dark/light value at (x,y), majority of a 3x3 area.
 *
 * @return  1 if dark, 0 if light or out of bounds.
 **********************************************************************/
static int sample_dark(const uint8_t * p_gray, int w, int h, int thresh,
                       double x, double y)
{
    int dark = 0;
    int lite = 0;
    int dy   = 0;
    int dx   = 0;
    int ix   = (int)(x + 0.5);
    int iy   = (int)(y + 0.5);

    for (dy = -1; dy <= 1; dy++)
    {
        for (dx = -1; dx <= 1; dx++)
        {
            int px = ix + dx;
            int py = iy + dy;
            if ((px < 0) || (py < 0) || (px >= w) || (py >= h))
            {
                continue;
            }
            if (p_gray[(py * w) + px] <= thresh)
            {
                dark++;
            }
            else
            {
                lite++;
            }
        }
    }
    return (dark > lite) ? 1 : 0;
}

/**********************************************************************
 * @brief  Try to read and decode a glyph given its four outer corners.
 *
 * @return  0 and fills p_res on success, -1 otherwise.
 **********************************************************************/
static int decode_at_corners(const uint8_t * p_gray, int w, int h,
                             int thresh, const pt_t corners[4],
                             detect_result_t * p_res)
{
    /* Unit square corners: TL, TR, BR, BL. */
    static const pt_t unit[4] =
    {
        { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 }, { 0.0, 1.0 }
    };
    double  hom[9];
    uint8_t grid[GLYPH_MODULES][GLYPH_MODULES];
    int     r = 0;
    int     c = 0;
    double  cxd = 0.0;
    double  cyd = 0.0;

    if (0 != homography(unit, corners, hom))
    {
        return -1;
    }

    for (r = 0; r < GLYPH_MODULES; r++)
    {
        for (c = 0; c < GLYPH_MODULES; c++)
        {
            double u = ((double)c + 0.5) / (double)GLYPH_MODULES;
            double v = ((double)r + 0.5) / (double)GLYPH_MODULES;
            double px = 0.0;
            double py = 0.0;
            apply_h(hom, u, v, &px, &py);
            grid[r][c] = (uint8_t)sample_dark(p_gray, w, h, thresh,
                                              px, py);
        }
    }

    if (0 != glyph_decode((const uint8_t (*)[GLYPH_MODULES])grid,
                          p_res->payload))
    {
        return -1;
    }

    apply_h(hom, 0.5, 0.5, &cxd, &cyd);
    p_res->cx = (float)cxd;
    p_res->cy = (float)cyd;
    return 0;
}

int detect_glyphs(const uint8_t * p_gray, int w, int h,
                  detect_result_t * p_out, int max)
{
    int       thresh = 0;
    int       n      = 0;
    int       found  = 0;
    uint8_t * p_bin  = NULL;
    int *     p_lbl  = NULL;
    int *     p_stack = NULL;
    long      min_area = 0;
    long      max_area = 0;
    int       y = 0;
    int       x = 0;
    int       next_label = 1;

    if ((NULL == p_gray) || (NULL == p_out) || (w <= 0) || (h <= 0))
    {
        return -1;
    }
    n = w * h;
    min_area = (long)(MIN_AREA_FRAC * (double)n);
    max_area = (long)(MAX_AREA_FRAC * (double)n);

    thresh  = otsu_threshold(p_gray, n);
    p_bin   = (uint8_t *)malloc((size_t)n);
    p_lbl   = (int *)calloc((size_t)n, sizeof(int));
    p_stack = (int *)malloc((size_t)n * sizeof(int));
    if ((NULL == p_bin) || (NULL == p_lbl) || (NULL == p_stack))
    {
        goto cleanup;
    }
    for (y = 0; y < n; y++)
    {
        p_bin[y] = (p_gray[y] <= thresh) ? 1U : 0U;
    }

    /* Flood-fill each dark connected component (4-connectivity). */
    for (y = 0; (y < h) && (found < max); y++)
    {
        for (x = 0; (x < w) && (found < max); x++)
        {
            int   idx = (y * w) + x;
            int   sp  = 0;
            long  area = 0;
            int   minx = x;
            int   maxx = x;
            int   miny = y;
            int   maxy = y;
            /* Extremes for corner extraction. */
            double min_sum = 1e18;
            double max_sum = -1e18;
            double min_dif = 1e18;
            double max_dif = -1e18;
            pt_t   corners[4];

            if ((0 == p_bin[idx]) || (0 != p_lbl[idx]))
            {
                continue;
            }

            p_stack[sp++] = idx;
            p_lbl[idx] = next_label;

            while (sp > 0)
            {
                int   ci = p_stack[--sp];
                int   cx = ci % w;
                int   cy = ci / w;
                double s = (double)cx + (double)cy;
                double d = (double)cx - (double)cy;

                area++;
                if (cx < minx) { minx = cx; }
                if (cx > maxx) { maxx = cx; }
                if (cy < miny) { miny = cy; }
                if (cy > maxy) { maxy = cy; }
                if (s < min_sum) { min_sum = s; corners[0].x = cx; corners[0].y = cy; }
                if (s > max_sum) { max_sum = s; corners[2].x = cx; corners[2].y = cy; }
                if (d < min_dif) { min_dif = d; corners[3].x = cx; corners[3].y = cy; }
                if (d > max_dif) { max_dif = d; corners[1].x = cx; corners[1].y = cy; }

                if ((cx > 0) && (1U == p_bin[ci - 1]) && (0 == p_lbl[ci - 1]))
                { p_lbl[ci - 1] = next_label; p_stack[sp++] = ci - 1; }
                if ((cx < (w - 1)) && (1U == p_bin[ci + 1]) && (0 == p_lbl[ci + 1]))
                { p_lbl[ci + 1] = next_label; p_stack[sp++] = ci + 1; }
                if ((cy > 0) && (1U == p_bin[ci - w]) && (0 == p_lbl[ci - w]))
                { p_lbl[ci - w] = next_label; p_stack[sp++] = ci - w; }
                if ((cy < (h - 1)) && (1U == p_bin[ci + w]) && (0 == p_lbl[ci + w]))
                { p_lbl[ci + w] = next_label; p_stack[sp++] = ci + w; }
            }
            next_label++;

            /* Filter: plausible size and roughly square bounding box. */
            if ((area < min_area) || (area > max_area))
            {
                continue;
            }
            {
                int    bw = (maxx - minx) + 1;
                int    bh = (maxy - miny) + 1;
                double aspect = (double)bw / (double)bh;
                if ((aspect < 0.5) || (aspect > 2.0))
                {
                    continue;
                }
            }

            if (0 == decode_at_corners(p_gray, w, h, thresh, corners,
                                       &p_out[found]))
            {
                found++;
            }
        }
    }

cleanup:
    if (NULL != p_bin)   { free(p_bin); }
    if (NULL != p_lbl)   { free(p_lbl); }
    if (NULL != p_stack) { free(p_stack); }
    return found;
}

int glyph_render(const uint8_t grid[GLYPH_MODULES][GLYPH_MODULES],
                 int module_px, int quiet_modules,
                 uint8_t ** pp_gray, int * p_w, int * p_h)
{
    int       total_modules = GLYPH_MODULES + (2 * quiet_modules);
    int       dim = total_modules * module_px;
    uint8_t * p_img = NULL;
    int       y = 0;
    int       x = 0;

    if ((NULL == pp_gray) || (module_px <= 0) || (quiet_modules < 0))
    {
        return -1;
    }
    p_img = (uint8_t *)malloc((size_t)dim * (size_t)dim);
    if (NULL == p_img)
    {
        return -1;
    }

    for (y = 0; y < dim; y++)
    {
        for (x = 0; x < dim; x++)
        {
            int mod_x = (x / module_px) - quiet_modules;
            int mod_y = (y / module_px) - quiet_modules;
            uint8_t v = 255U;   /* Quiet zone and white modules. */

            if ((mod_x >= 0) && (mod_x < GLYPH_MODULES) &&
                (mod_y >= 0) && (mod_y < GLYPH_MODULES))
            {
                v = (0 != grid[mod_y][mod_x]) ? 0U : 255U;
            }
            p_img[(y * dim) + x] = v;
        }
    }

    *pp_gray = p_img;
    if (NULL != p_w) { *p_w = dim; }
    if (NULL != p_h) { *p_h = dim; }
    return 0;
}
