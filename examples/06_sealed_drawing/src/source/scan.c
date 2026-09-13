#include "scan.h"

#include "hud.h"

#include "jpeg_decode.h"
#include "colorsig.h"
#include "locate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGBA_BYTES          (4)
#define RGB_BYTES           (3)
#define OPAQUE              (255U)
#define PERCENT             (100)
#define EDGE_KEEP_PERMILLE  (60)
#define PERMILLE            (1000)
#define HIST_BINS           (1024)
#define SWEEP_END           (0.75)
#define BLINK_STEPS         (12.0)
#define SCAN_LINE_HALF_PX   (3)
#define GLOW_ROWS           (60)
#define GLOW_MAX            (110)
#define BOX_LINE_PX         (4)
#define BOX_CORNER_PX       (64)
#define TEXT_SCALE          (3)
#define TEXT_GAP_PX         (12)
#define TEXT_CHARS          (32)
#define BRIGHT_R            (80U)
#define BRIGHT_G            (255U)
#define BRIGHT_B            (140U)
#define DIM_R               (40U)
#define DIM_G               (170U)
#define DIM_B               (90U)
#define LINE_R              (255U)
#define LINE_G              (255U)
#define LINE_B              (255U)
#define OUTSIDE_DIM_NUM     (5)
#define OUTSIDE_DIM_DEN     (10)

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} colour_t;

typedef struct
{
    int      last_row;
    colour_t colour;
} edge_paint_t;

static const colour_t BRIGHT = { BRIGHT_R, BRIGHT_G, BRIGHT_B };
static const colour_t DIM    = { DIM_R, DIM_G, DIM_B };
static const colour_t LINE   = { LINE_R, LINE_G, LINE_B };

static void put_pixel(rgba_image_t * p_img, size_t index, colour_t c)
{
    uint8_t * p = p_img->p_rgba + (index * RGBA_BYTES);

    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = (uint8_t)OPAQUE;
}

static void rgb_to_rgba(const jpeg_rgb_t * p_src, rgba_image_t * p_dst)
{
    size_t count = (size_t)p_src->width * (size_t)p_src->height;
    size_t i     = 0;

    for (i = 0; i < count; i++)
    {
        memcpy(p_dst->p_rgba + (i * RGBA_BYTES),
               p_src->p_rgb + (i * RGB_BYTES), RGB_BYTES);
        p_dst->p_rgba[(i * RGBA_BYTES) + RGB_BYTES] = (uint8_t)OPAQUE;
    }
}

static uint8_t luma_at(const rgba_image_t * p_img, int x, int y)
{
    const uint8_t * p = p_img->p_rgba +
                        ((((size_t)y * p_img->width) + (size_t)x) *
                         RGBA_BYTES);

    return (uint8_t)(((77U * p[0]) + (150U * p[1]) + (29U * p[2])) >> 8);
}

static int sobel_at(const rgba_image_t * p_img, int x, int y)
{
    int gx = 0;
    int gy = 0;

    gx = (luma_at(p_img, x + 1, y - 1) + (2 * luma_at(p_img, x + 1, y)) +
          luma_at(p_img, x + 1, y + 1)) -
         (luma_at(p_img, x - 1, y - 1) + (2 * luma_at(p_img, x - 1, y)) +
          luma_at(p_img, x - 1, y + 1));
    gy = (luma_at(p_img, x - 1, y + 1) + (2 * luma_at(p_img, x, y + 1)) +
          luma_at(p_img, x + 1, y + 1)) -
         (luma_at(p_img, x - 1, y - 1) + (2 * luma_at(p_img, x, y - 1)) +
          luma_at(p_img, x + 1, y - 1));

    return abs(gx) + abs(gy);
}

static void compute_box(const rgba_image_t * p_img, int box[3])
{
    uint8_t *    p_gray = NULL;
    size_t       count  = (size_t)p_img->width * (size_t)p_img->height;
    size_t       i      = 0;
    gray_image_t view;
    rgb_image_t  colour;
    region_t     region;

    box[2]  = 0;
    p_gray  = (uint8_t *)malloc(count);
    if (NULL == p_gray)
    {
        goto cleanup;
    }
    for (i = 0; i < count; i++)
    {
        const uint8_t * p = p_img->p_rgba + (i * RGBA_BYTES);

        p_gray[i] = (uint8_t)(((77U * p[0]) + (150U * p[1]) +
                               (29U * p[2])) >> 8);
    }
    view.p_gray        = p_gray;
    view.width         = (int)p_img->width;
    view.height        = (int)p_img->height;
    colour.p_rgb       = p_img->p_rgba;
    colour.width       = (int)p_img->width;
    colour.height      = (int)p_img->height;
    colour.pixel_bytes = RGBA_BYTES;
    if (0 == locate_object_colour(&view, &colour, &region))
    {
        box[0] = region.x;
        box[1] = region.y;
        box[2] = region.side;
    }

cleanup:
    if (NULL != p_gray)
    {
        free(p_gray);
    }
}

static int magnitude_threshold(const int * p_mag, size_t count)
{
    long   hist[HIST_BINS];
    long   keep = ((long)count * EDGE_KEEP_PERMILLE) / PERMILLE;
    long   seen = 0;
    size_t i    = 0;
    int    bin  = HIST_BINS - 1;

    memset(hist, 0, sizeof(hist));
    for (i = 0; i < count; i++)
    {
        int m = p_mag[i];

        hist[(m >= HIST_BINS) ? (HIST_BINS - 1) : m]++;
    }
    while ((bin > 0) && (seen < keep))
    {
        seen += hist[bin];
        bin--;
    }

    return bin;
}

static void dilate_row(const uint8_t * p_src, uint8_t * p_dst,
                       size_t stride)
{
    size_t x = 0;

    for (x = 1; x < (stride - 1U); x++)
    {
        uint8_t hit = (uint8_t)(p_src[x - 1U] | p_src[x] | p_src[x + 1U] |
                                p_src[x - stride] | p_src[x + stride]);

        p_dst[x] = hit;
    }
}

static int dilate_edges(scan_t * p_scan)
{
    int       result = -1;
    size_t    stride = p_scan->base.width;
    size_t    rows   = p_scan->base.height;
    size_t    y      = 0;
    uint8_t * p_copy = NULL;

    p_copy = (uint8_t *)malloc(stride * rows);
    if (NULL == p_copy)
    {
        goto cleanup;
    }
    memcpy(p_copy, p_scan->p_edge, stride * rows);
    for (y = 1; y < (rows - 1U); y++)
    {
        dilate_row(p_copy + (y * stride), p_scan->p_edge + (y * stride),
                   stride);
    }
    result = 0;

cleanup:
    if (NULL != p_copy)
    {
        free(p_copy);
    }

    return result;
}

static int detect_edges(scan_t * p_scan)
{
    int    result = -1;
    int    side   = p_scan->box[2];
    int    x      = 0;
    int    y      = 0;
    int    thresh = 0;
    int *  p_mag  = NULL;
    size_t count  = (size_t)side * (size_t)side;

    p_mag = (int *)calloc(count, sizeof(int));
    if (NULL == p_mag)
    {
        goto cleanup;
    }
    for (y = 1; y < (side - 1); y++)
    {
        for (x = 1; x < (side - 1); x++)
        {
            p_mag[((size_t)y * (size_t)side) + (size_t)x] =
                sobel_at(&p_scan->base, p_scan->box[0] + x,
                         p_scan->box[1] + y);
        }
    }
    thresh = magnitude_threshold(p_mag, count);
    for (y = 0; y < side; y++)
    {
        for (x = 0; x < side; x++)
        {
            size_t local  = ((size_t)y * (size_t)side) + (size_t)x;
            size_t global = (((size_t)(p_scan->box[1] + y) *
                              p_scan->base.width) +
                             (size_t)(p_scan->box[0] + x));

            p_scan->p_edge[global] = (p_mag[local] > thresh) ? 1U : 0U;
        }
    }
    result = dilate_edges(p_scan);

cleanup:
    if (NULL != p_mag)
    {
        free(p_mag);
    }

    return result;
}

static void dim_outside_box(const scan_t * p_scan, rgba_image_t * p_img)
{
    int x0   = p_scan->box[0];
    int y0   = p_scan->box[1];
    int side = p_scan->box[2];
    int y    = 0;
    int x    = 0;

    for (y = 0; y < (int)p_img->height; y++)
    {
        uint8_t * p_row  = p_img->p_rgba + ((size_t)y * p_img->width *
                                            RGBA_BYTES);
        int       in_row = (y >= y0) && (y < (y0 + side));

        for (x = 0; x < (int)p_img->width; x++)
        {
            uint8_t * p = p_row + ((size_t)x * RGBA_BYTES);

            if ((0 != in_row) && (x >= x0) && (x < (x0 + side)))
            {
                continue;
            }
            p[0] = (uint8_t)((p[0] * OUTSIDE_DIM_NUM) / OUTSIDE_DIM_DEN);
            p[1] = (uint8_t)((p[1] * OUTSIDE_DIM_NUM) / OUTSIDE_DIM_DEN);
            p[2] = (uint8_t)((p[2] * OUTSIDE_DIM_NUM) / OUTSIDE_DIM_DEN);
        }
    }
}

int scan_begin(byte_span_t jpeg, scan_t * p_scan)
{
    int        result = -1;
    size_t     pixels = 0;
    jpeg_rgb_t decoded;

    memset(&decoded, 0, sizeof(decoded));
    if (NULL == p_scan)
    {
        goto cleanup;
    }
    memset(p_scan, 0, sizeof(*p_scan));
    if (0 != jpeg_decode_rgb(jpeg.p_data, jpeg.len, &decoded))
    {
        goto cleanup;
    }
    pixels               = (size_t)decoded.width * (size_t)decoded.height;
    p_scan->base.width   = (uint32_t)decoded.width;
    p_scan->base.height  = (uint32_t)decoded.height;
    p_scan->frame.width  = p_scan->base.width;
    p_scan->frame.height = p_scan->base.height;
    p_scan->base.p_rgba  = (uint8_t *)malloc(pixels * RGBA_BYTES);
    p_scan->frame.p_rgba = (uint8_t *)malloc(pixels * RGBA_BYTES);
    p_scan->p_edge       = (uint8_t *)calloc(pixels, 1U);
    if ((NULL == p_scan->base.p_rgba) || (NULL == p_scan->frame.p_rgba) ||
        (NULL == p_scan->p_edge))
    {
        goto cleanup;
    }
    rgb_to_rgba(&decoded, &p_scan->base);
    compute_box(&p_scan->base, p_scan->box);
    if ((p_scan->box[2] <= 0) || (0 != detect_edges(p_scan)))
    {
        goto cleanup;
    }
    dim_outside_box(p_scan, &p_scan->base);
    result = 0;

cleanup:
    jpeg_rgb_free(&decoded);
    if ((0 != result) && (NULL != p_scan))
    {
        scan_end(p_scan);
    }

    return result;
}

static void draw_corner_arm(rgba_image_t * p_img, const int at[2],
                            const int dir[2])
{
    int i = 0;
    int t = 0;

    for (i = 0; i < BOX_CORNER_PX; i++)
    {
        for (t = 0; t < BOX_LINE_PX; t++)
        {
            int x = at[0] + (i * dir[0]);
            int y = at[1] + (t * dir[1]);

            put_pixel(p_img, ((size_t)y * p_img->width) + (size_t)x, BRIGHT);
            x = at[0] + (t * dir[0]);
            y = at[1] + (i * dir[1]);
            put_pixel(p_img, ((size_t)y * p_img->width) + (size_t)x, BRIGHT);
        }
    }
}

static void draw_box_corners(const scan_t * p_scan, rgba_image_t * p_img)
{
    int x0 = p_scan->box[0];
    int y0 = p_scan->box[1];
    int x1 = x0 + p_scan->box[2] - 1;
    int y1 = y0 + p_scan->box[2] - 1;
    int corners[4][2];
    int dirs[4][2];

    corners[0][0] = x0; corners[0][1] = y0; dirs[0][0] = 1;  dirs[0][1] = 1;
    corners[1][0] = x1; corners[1][1] = y0; dirs[1][0] = -1; dirs[1][1] = 1;
    corners[2][0] = x0; corners[2][1] = y1; dirs[2][0] = 1;  dirs[2][1] = -1;
    corners[3][0] = x1; corners[3][1] = y1; dirs[3][0] = -1; dirs[3][1] = -1;
    draw_corner_arm(p_img, corners[0], dirs[0]);
    draw_corner_arm(p_img, corners[1], dirs[1]);
    draw_corner_arm(p_img, corners[2], dirs[2]);
    draw_corner_arm(p_img, corners[3], dirs[3]);
}

static void paint_edges_until(const scan_t * p_scan, rgba_image_t * p_img,
                              const edge_paint_t * p_paint)
{
    int x0    = p_scan->box[0];
    int side  = p_scan->box[2];
    int y     = 0;
    int x     = 0;
    int y_end = p_scan->box[1] + side;

    if (p_paint->last_row < y_end)
    {
        y_end = p_paint->last_row;
    }
    for (y = p_scan->box[1]; y < y_end; y++)
    {
        size_t row = (size_t)y * p_img->width;

        for (x = x0; x < (x0 + side); x++)
        {
            if (0U != p_scan->p_edge[row + (size_t)x])
            {
                put_pixel(p_img, row + (size_t)x, p_paint->colour);
            }
        }
    }
}

static void tint_row(rgba_image_t * p_img, const int span[3], int amount)
{
    int x = 0;

    for (x = span[1]; x < span[2]; x++)
    {
        uint8_t * p = p_img->p_rgba +
                      ((((size_t)span[0] * p_img->width) + (size_t)x) *
                       RGBA_BYTES);
        int       g = p[1] + amount;

        p[1] = (uint8_t)((g > (int)OPAQUE) ? (int)OPAQUE : g);
    }
}

static void draw_sweep(const scan_t * p_scan, rgba_image_t * p_img, int row)
{
    int span[3];
    int y = 0;
    int t = 0;
    int x = 0;

    span[1] = p_scan->box[0];
    span[2] = p_scan->box[0] + p_scan->box[2];
    for (y = row - GLOW_ROWS; y < row; y++)
    {
        if (y < p_scan->box[1])
        {
            continue;
        }
        span[0] = y;
        tint_row(p_img, span, (GLOW_MAX * (y - (row - GLOW_ROWS))) /
                              GLOW_ROWS);
    }
    for (t = -SCAN_LINE_HALF_PX; t <= SCAN_LINE_HALF_PX; t++)
    {
        y = row + t;
        if ((y < p_scan->box[1]) || (y >= (p_scan->box[1] + p_scan->box[2])))
        {
            continue;
        }
        for (x = span[1]; x < span[2]; x++)
        {
            put_pixel(p_img, ((size_t)y * p_img->width) + (size_t)x, LINE);
        }
    }
}

static void draw_label(const scan_t * p_scan, rgba_image_t * p_img,
                       const char * p_label)
{
    const char * lines[1];
    hud_text_t   text;
    uint8_t *    p_rgba = NULL;
    uint32_t     w      = 0;
    uint32_t     h      = 0;
    uint32_t     x      = 0;
    uint32_t     y      = 0;
    int          top    = 0;

    lines[0]        = p_label;
    text.pp_lines   = lines;
    text.line_count = 1;
    text.scale      = TEXT_SCALE;
    p_rgba          = hud_render(&text, &w, &h);
    if (NULL == p_rgba)
    {
        goto cleanup;
    }
    top = p_scan->box[1] - (int)h - TEXT_GAP_PX;
    if (top < 0)
    {
        top = 0;
    }
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            size_t src = (((size_t)y * w) + x) * RGBA_BYTES;
            size_t dst = (((size_t)(top + (int)y) * p_img->width) +
                          (size_t)p_scan->box[0] + x);

            if (0U != p_rgba[src + RGB_BYTES])
            {
                put_pixel(p_img, dst, BRIGHT);
            }
        }
    }

cleanup:
    if (NULL != p_rgba)
    {
        free(p_rgba);
    }
}

const rgba_image_t * scan_render(scan_t * p_scan, double progress)
{
    const rgba_image_t * p_out = NULL;
    size_t               bytes = 0;
    int                  blink = 0;
    edge_paint_t         paint;
    char                 label[TEXT_CHARS];

    if ((NULL == p_scan) || (NULL == p_scan->frame.p_rgba))
    {
        goto cleanup;
    }
    if (progress < 0.0)
    {
        progress = 0.0;
    }
    if (progress > 1.0)
    {
        progress = 1.0;
    }
    bytes = (size_t)p_scan->base.width * (size_t)p_scan->base.height *
            RGBA_BYTES;
    memcpy(p_scan->frame.p_rgba, p_scan->base.p_rgba, bytes);
    draw_box_corners(p_scan, &p_scan->frame);
    if (progress < SWEEP_END)
    {
        double part = progress / SWEEP_END;

        paint.last_row = p_scan->box[1] +
                         (int)(part * (double)p_scan->box[2]);
        paint.colour   = BRIGHT;
        paint_edges_until(p_scan, &p_scan->frame, &paint);
        draw_sweep(p_scan, &p_scan->frame, paint.last_row);
        (void)snprintf(label, sizeof(label), "SCANNING %3d%%",
                       (int)(part * (double)PERCENT));
    }
    else
    {
        blink          = (int)(((progress - SWEEP_END) / (1.0 - SWEEP_END)) *
                               BLINK_STEPS);
        paint.last_row = p_scan->box[1] + p_scan->box[2];
        paint.colour   = (0 == (blink % 2)) ? BRIGHT : DIM;
        paint_edges_until(p_scan, &p_scan->frame, &paint);
        (void)snprintf(label, sizeof(label), "LOCKED ON");
    }
    draw_label(p_scan, &p_scan->frame, label);
    p_out = &p_scan->frame;

cleanup:

    return p_out;
}

void scan_mark_result(const scan_t * p_scan, rgba_image_t * p_img)
{
    edge_paint_t paint;

    if ((NULL == p_scan) || (NULL == p_img) || (NULL == p_img->p_rgba) ||
        (p_img->width != p_scan->base.width) ||
        (p_img->height != p_scan->base.height))
    {
        goto cleanup;
    }
    paint.last_row = p_scan->box[1] + p_scan->box[2];
    paint.colour   = DIM;
    paint_edges_until(p_scan, p_img, &paint);

cleanup:

    return;
}

void scan_end(scan_t * p_scan)
{
    if (NULL == p_scan)
    {
        goto cleanup;
    }
    if (NULL != p_scan->base.p_rgba)
    {
        free(p_scan->base.p_rgba);
    }
    if (NULL != p_scan->frame.p_rgba)
    {
        free(p_scan->frame.p_rgba);
    }
    if (NULL != p_scan->p_edge)
    {
        free(p_scan->p_edge);
    }
    memset(p_scan, 0, sizeof(*p_scan));

cleanup:

    return;
}
