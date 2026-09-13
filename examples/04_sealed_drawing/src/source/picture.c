#include "picture.h"

#include "hud.h"
#include "locate.h"

#include "jpeg_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define RGBA_BYTES          (4)
#define RGB_BYTES           (3)
#define OPAQUE              (255U)
#define CAPTION_SCALE       (3)
#define CAPTION_PAD_PX      (24)
#define PREVIEW_SCALE       (1)
#define PREVIEW_PAD_PX      (6)
#define BRACKET_LEN_PX      (14)
#define BRACKET_THICK_PX    (2)
#define BRACKET_R           (80U)
#define BRACKET_G           (255U)
#define BRACKET_B           (140U)
#define PERCENT             (100)
#define BAND_DIM_NUM        (3)
#define BAND_DIM_DEN        (10)
#define DIR_MODE            (0755)
#define BMP_HEADER_BYTES    (54)
#define BMP_ROW_ALIGN       (4)
#define BMP_BITS_PER_PIXEL  (24)
#define BMP_PLANES          (1)
#define BYTE_MASK           (0xFFU)
#define BYTE_SHIFT_1        (8)
#define BYTE_SHIFT_2        (16)
#define BYTE_SHIFT_3        (24)

static void put_u32(uint8_t * p_out, uint32_t v)
{
    p_out[0] = (uint8_t)(v & BYTE_MASK);
    p_out[1] = (uint8_t)((v >> BYTE_SHIFT_1) & BYTE_MASK);
    p_out[2] = (uint8_t)((v >> BYTE_SHIFT_2) & BYTE_MASK);
    p_out[3] = (uint8_t)((v >> BYTE_SHIFT_3) & BYTE_MASK);
}

static void put_u16(uint8_t * p_out, uint16_t v)
{
    p_out[0] = (uint8_t)(v & BYTE_MASK);
    p_out[1] = (uint8_t)((v >> BYTE_SHIFT_1) & BYTE_MASK);
}

int picture_slot_begin(const char * p_dir, picture_slot_t * p_slot)
{
    int         result = -1;
    time_t      now    = 0;
    struct tm   local;
    struct stat st;

    if ((NULL == p_dir) || (NULL == p_slot))
    {
        goto cleanup;
    }
    memset(p_slot, 0, sizeof(*p_slot));
    memset(&st, 0, sizeof(st));
    if ((0 != stat(p_dir, &st)) && (0 != mkdir(p_dir, DIR_MODE)))
    {
        (void)fprintf(stderr, "[picture] cannot create '%s'\n", p_dir);
        goto cleanup;
    }
    now = time(NULL);
    if (NULL == localtime_r(&now, &local))
    {
        goto cleanup;
    }
    if (0U == strftime(p_slot->stamp, sizeof(p_slot->stamp),
                       "%Y%m%d-%H%M%S", &local))
    {
        goto cleanup;
    }
    p_slot->p_dir = p_dir;
    result        = 0;

cleanup:

    return result;
}

int picture_save_jpeg(const picture_slot_t * p_slot, byte_span_t jpeg,
                      char * p_path)
{
    int    result = -1;
    FILE * p_file = NULL;

    if ((NULL == p_slot) || (NULL == p_path) || (NULL == jpeg.p_data))
    {
        goto cleanup;
    }
    (void)snprintf(p_path, PICTURE_PATH_CHARS, "%s/%s_capture.jpg",
                   p_slot->p_dir, p_slot->stamp);
    p_file = fopen(p_path, "wb");
    if (NULL == p_file)
    {
        goto cleanup;
    }
    if (fwrite(jpeg.p_data, 1U, jpeg.len, p_file) != jpeg.len)
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_file)
    {
        (void)fclose(p_file);
    }

    return result;
}

static void rgb_to_rgba(const jpeg_rgb_t * p_src, uint8_t * p_dst)
{
    size_t count = (size_t)p_src->width * (size_t)p_src->height;
    size_t i     = 0;

    for (i = 0; i < count; i++)
    {
        memcpy(p_dst + (i * RGBA_BYTES), p_src->p_rgb + (i * RGB_BYTES),
               RGB_BYTES);
        p_dst[(i * RGBA_BYTES) + RGB_BYTES] = (uint8_t)OPAQUE;
    }
}

static void dim_rows(rgba_image_t * p_img, uint32_t first_row)
{
    size_t start = (size_t)first_row * (size_t)p_img->width * RGBA_BYTES;
    size_t end   = (size_t)p_img->height * (size_t)p_img->width *
                   RGBA_BYTES;
    size_t i     = 0;

    for (i = start; i < end; i++)
    {
        if (RGB_BYTES != (i % RGBA_BYTES))
        {
            p_img->p_rgba[i] = (uint8_t)((p_img->p_rgba[i] * BAND_DIM_NUM) /
                                         BAND_DIM_DEN);
        }
    }
}

typedef struct
{
    int      scale;
    uint32_t pad;
} caption_layout_t;

typedef struct
{
    const jpeg_rgb_t *       p_decoded;
    const char *             p_caption;
    const caption_layout_t * p_layout;
} compose_job_t;

static void blit_text(rgba_image_t * p_img, const rgba_image_t * p_text,
                      const uint32_t at[2])
{
    uint32_t top  = at[0];
    uint32_t left = at[1];
    uint32_t rows = p_text->height;
    uint32_t cols = p_text->width;
    uint32_t y    = 0;
    uint32_t x    = 0;

    if ((top + rows) > p_img->height)
    {
        rows = p_img->height - top;
    }
    if ((left + cols) > p_img->width)
    {
        cols = p_img->width - left;
    }
    for (y = 0; y < rows; y++)
    {
        const uint8_t * p_src = p_text->p_rgba +
                                ((size_t)y * p_text->width * RGBA_BYTES);
        uint8_t *       p_dst = p_img->p_rgba +
                                (((size_t)(top + y) * p_img->width) +
                                 left) * RGBA_BYTES;

        for (x = 0; x < cols; x++)
        {
            if (0U != p_src[(x * RGBA_BYTES) + RGB_BYTES])
            {
                memcpy(p_dst + (x * RGBA_BYTES), p_src + (x * RGBA_BYTES),
                       RGBA_BYTES);
            }
        }
    }
}

static int render_caption(const char * p_caption, int scale,
                          rgba_image_t * p_text)
{
    int           result = -1;
    hud_wrapped_t wrapped;
    hud_text_t    text;

    memset(&wrapped, 0, sizeof(wrapped));
    memset(&text, 0, sizeof(text));
    hud_wrap_text(p_caption, &wrapped);
    text.pp_lines   = wrapped.pp_lines;
    text.line_count = wrapped.line_count;
    text.scale      = scale;
    p_text->p_rgba  = hud_render(&text, &p_text->width, &p_text->height);
    if (NULL != p_text->p_rgba)
    {
        result = 0;
    }

    return result;
}

static int compose_from(const compose_job_t * p_job, rgba_image_t * p_out)
{
    const jpeg_rgb_t *       p_decoded = p_job->p_decoded;
    const caption_layout_t * p_layout  = p_job->p_layout;
    int                      result    = -1;
    uint32_t                 band_top  = 0;
    uint32_t                 at[2];
    rgba_image_t             text;

    memset(&text, 0, sizeof(text));
    p_out->width  = (uint32_t)p_decoded->width;
    p_out->height = (uint32_t)p_decoded->height;
    p_out->p_rgba = (uint8_t *)malloc((size_t)p_out->width *
                                      (size_t)p_out->height * RGBA_BYTES);
    if (NULL == p_out->p_rgba)
    {
        goto cleanup;
    }
    rgb_to_rgba(p_decoded, p_out->p_rgba);
    if (0 != render_caption(p_job->p_caption, p_layout->scale, &text))
    {
        goto cleanup;
    }
    if ((text.height + (2U * p_layout->pad)) < p_out->height)
    {
        band_top = p_out->height - text.height - (2U * p_layout->pad);
    }
    dim_rows(p_out, band_top);
    at[0] = band_top + p_layout->pad;
    at[1] = p_layout->pad;
    blit_text(p_out, &text, at);
    result = 0;

cleanup:
    if (NULL != text.p_rgba)
    {
        free(text.p_rgba);
    }

    return result;
}

typedef struct
{
    byte_span_t  jpeg;
    const char * p_caption;
    int          preview;
} compose_request_t;

static void bracket_pixel(rgba_image_t * p_img, int x, int y)
{
    uint8_t * p = NULL;

    if ((x < 0) || (y < 0) || (x >= (int)p_img->width) ||
        (y >= (int)p_img->height))
    {
        goto cleanup;
    }
    p    = p_img->p_rgba + ((((size_t)y * p_img->width) + (size_t)x) *
                            RGBA_BYTES);
    p[0] = (uint8_t)BRACKET_R;
    p[1] = (uint8_t)BRACKET_G;
    p[2] = (uint8_t)BRACKET_B;
    p[3] = (uint8_t)OPAQUE;

cleanup:

    return;
}

static void bracket_corner(rgba_image_t * p_img, const int at[2],
                           const int dir[2])
{
    int i = 0;
    int t = 0;

    for (i = 0; i < BRACKET_LEN_PX; i++)
    {
        for (t = 0; t < BRACKET_THICK_PX; t++)
        {
            bracket_pixel(p_img, at[0] + (i * dir[0]), at[1] + (t * dir[1]));
            bracket_pixel(p_img, at[0] + (t * dir[0]), at[1] + (i * dir[1]));
        }
    }
}

static void draw_search_brackets(rgba_image_t * p_img)
{
    int side = ((int)p_img->width < (int)p_img->height) ? (int)p_img->width
                                                        : (int)p_img->height;
    int x0   = 0;
    int y0   = 0;
    int x1   = 0;
    int y1   = 0;
    int at[2];
    int dir[2];

    side = (side * LOCATE_SEARCH_PERCENT) / PERCENT;
    x0   = ((int)p_img->width - side) / 2;
    y0   = ((int)p_img->height - side) / 2;
    x1   = x0 + side - 1;
    y1   = y0 + side - 1;
    at[0] = x0;
    at[1] = y0;
    dir[0] = 1;
    dir[1] = 1;
    bracket_corner(p_img, at, dir);
    at[0]  = x1;
    dir[0] = -1;
    bracket_corner(p_img, at, dir);
    at[1]  = y1;
    dir[1] = -1;
    bracket_corner(p_img, at, dir);
    at[0]  = x0;
    dir[0] = 1;
    bracket_corner(p_img, at, dir);
}

static int compose_generic(const compose_request_t * p_req,
                           rgba_image_t * p_out)
{
    int              result    = -1;
    byte_span_t      jpeg      = p_req->jpeg;
    const char *     p_caption = p_req->p_caption;
    int              preview   = p_req->preview;
    jpeg_rgb_t       decoded;
    caption_layout_t layout;
    compose_job_t    job;

    memset(&decoded, 0, sizeof(decoded));
    if ((NULL == p_out) || (NULL == p_caption))
    {
        goto cleanup;
    }
    memset(p_out, 0, sizeof(*p_out));
    if (0 != preview)
    {
        layout.scale = PREVIEW_SCALE;
        layout.pad   = PREVIEW_PAD_PX;
        result = jpeg_decode_rgb_quarter(jpeg.p_data, jpeg.len, &decoded);
    }
    else
    {
        layout.scale = CAPTION_SCALE;
        layout.pad   = CAPTION_PAD_PX;
        result = jpeg_decode_rgb(jpeg.p_data, jpeg.len, &decoded);
    }
    if (0 != result)
    {
        result = -1;
        goto cleanup;
    }
    job.p_decoded = &decoded;
    job.p_caption = p_caption;
    job.p_layout  = &layout;
    result        = compose_from(&job, p_out);
    if ((0 == result) && (0 != preview))
    {
        draw_search_brackets(p_out);
    }

cleanup:
    jpeg_rgb_free(&decoded);
    if ((0 != result) && (NULL != p_out) && (NULL != p_out->p_rgba))
    {
        free(p_out->p_rgba);
        p_out->p_rgba = NULL;
    }

    return result;
}

int picture_compose(byte_span_t jpeg, const char * p_caption,
                    rgba_image_t * p_out)
{
    compose_request_t req;

    req.jpeg      = jpeg;
    req.p_caption = p_caption;
    req.preview   = 0;

    return compose_generic(&req, p_out);
}

int picture_preview(byte_span_t jpeg, const char * p_caption,
                    rgba_image_t * p_out)
{
    compose_request_t req;

    req.jpeg      = jpeg;
    req.p_caption = p_caption;
    req.preview   = 1;

    return compose_generic(&req, p_out);
}

static int write_bmp_rows(FILE * p_file, const rgba_image_t * p_img)
{
    int       result   = -1;
    size_t    row_raw  = (size_t)p_img->width * RGB_BYTES;
    size_t    row_pad  = (BMP_ROW_ALIGN - (row_raw % BMP_ROW_ALIGN)) %
                         BMP_ROW_ALIGN;
    uint8_t * p_row    = NULL;
    uint32_t  y        = 0;
    uint32_t  x        = 0;

    p_row = (uint8_t *)calloc(row_raw + row_pad, 1U);
    if (NULL == p_row)
    {
        goto cleanup;
    }
    for (y = p_img->height; y > 0U; y--)
    {
        const uint8_t * p_src = p_img->p_rgba +
                                ((size_t)(y - 1U) * p_img->width *
                                 RGBA_BYTES);

        for (x = 0; x < p_img->width; x++)
        {
            p_row[(x * RGB_BYTES) + 0] = p_src[(x * RGBA_BYTES) + 2];
            p_row[(x * RGB_BYTES) + 1] = p_src[(x * RGBA_BYTES) + 1];
            p_row[(x * RGB_BYTES) + 2] = p_src[(x * RGBA_BYTES) + 0];
        }
        if (fwrite(p_row, 1U, row_raw + row_pad, p_file) !=
            (row_raw + row_pad))
        {
            goto cleanup;
        }
    }
    result = 0;

cleanup:
    if (NULL != p_row)
    {
        free(p_row);
    }

    return result;
}

int picture_save_bmp(const picture_slot_t * p_slot,
                     const rgba_image_t * p_img, char * p_path)
{
    int      result    = -1;
    FILE *   p_file    = NULL;
    size_t   row_bytes = 0;
    uint32_t pixel_bytes = 0;
    uint8_t  header[BMP_HEADER_BYTES];

    if ((NULL == p_slot) || (NULL == p_img) || (NULL == p_path) ||
        (NULL == p_img->p_rgba))
    {
        goto cleanup;
    }
    (void)snprintf(p_path, PICTURE_PATH_CHARS, "%s/%s_overlay.bmp",
                   p_slot->p_dir, p_slot->stamp);
    row_bytes   = ((size_t)p_img->width * RGB_BYTES);
    row_bytes  += (BMP_ROW_ALIGN - (row_bytes % BMP_ROW_ALIGN)) %
                  BMP_ROW_ALIGN;
    pixel_bytes = (uint32_t)(row_bytes * p_img->height);
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    put_u32(header + 2, BMP_HEADER_BYTES + pixel_bytes);
    put_u32(header + 10, BMP_HEADER_BYTES);
    put_u32(header + 14, BMP_HEADER_BYTES - 14);
    put_u32(header + 18, p_img->width);
    put_u32(header + 22, p_img->height);
    put_u16(header + 26, BMP_PLANES);
    put_u16(header + 28, BMP_BITS_PER_PIXEL);
    put_u32(header + 34, pixel_bytes);
    p_file = fopen(p_path, "wb");
    if (NULL == p_file)
    {
        goto cleanup;
    }
    if (fwrite(header, 1U, sizeof(header), p_file) != sizeof(header))
    {
        goto cleanup;
    }
    result = write_bmp_rows(p_file, p_img);

cleanup:
    if (NULL != p_file)
    {
        (void)fclose(p_file);
    }

    return result;
}
