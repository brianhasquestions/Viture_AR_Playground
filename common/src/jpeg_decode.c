/**********************************************************************
 * @file    jpeg_decode.c
 * @brief   Minimal baseline-JPEG decoder to grayscale, pure C.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "jpeg_decode.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define JPEG_MAX_COMPONENTS   (4)
#define JPEG_OK               (0)
#define JPEG_ERR              (-1)

/* Zigzag order: coefficient k of the entropy stream maps to this index
 * inside the natural 8x8 block. */
static const uint8_t ZIGZAG[64] =
{
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/**********************************************************************
 * @brief  A Huffman table: fast 8-bit lookup plus canonical fallback.
 **********************************************************************/
typedef struct
{
    uint8_t  bits[17];      /* bits[i] = count of codes of length i.   */
    uint8_t  values[256];   /* Symbols in canonical order.             */
    int      num_values;
    /* Canonical decode tables. */
    int      mincode[17];
    int      maxcode[18];
    int      valptr[17];
    int      built;
} huff_t;

typedef struct
{
    int id;
    int h;              /* Horizontal sampling factor. */
    int v;              /* Vertical sampling factor.   */
    int quant_id;
    int dc_table;
    int ac_table;
    int dc_pred;        /* Running DC predictor.       */
} component_t;

typedef struct
{
    const uint8_t * p;      /* Cursor into entropy-coded data. */
    const uint8_t * p_end;
    uint32_t        bitbuf;
    int             bitcnt;
    int             marker;  /* Non-zero once a marker is hit. */
} bitreader_t;

typedef struct
{
    const uint8_t * data;
    size_t          size;
    size_t          pos;

    int         width;
    int         height;
    int         num_components;
    component_t comp[JPEG_MAX_COMPONENTS];

    uint16_t    quant[4][64];
    huff_t      huff_dc[4];
    huff_t      huff_ac[4];

    int         restart_interval;

    int         max_h;
    int         max_v;
    int         reduced;    /* Non-zero: 2x2 output per block (1/4). */
} jpeg_ctx_t;

/* ---- Standard Annex-K Huffman tables (MJPEG abbreviated-stream
 *      fallback). Only used when a scan references a table the stream
 *      never defined. ------------------------------------------------ */
static const uint8_t STD_DC_LUMA_BITS[17] =
{ 0, 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0 };
static const uint8_t STD_DC_LUMA_VAL[12] =
{ 0,1,2,3,4,5,6,7,8,9,10,11 };

static const uint8_t STD_DC_CHROMA_BITS[17] =
{ 0, 0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0 };
static const uint8_t STD_DC_CHROMA_VAL[12] =
{ 0,1,2,3,4,5,6,7,8,9,10,11 };

static const uint8_t STD_AC_LUMA_BITS[17] =
{ 0, 0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d };
static const uint8_t STD_AC_LUMA_VAL[162] =
{
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

static const uint8_t STD_AC_CHROMA_BITS[17] =
{ 0, 0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77 };
static const uint8_t STD_AC_CHROMA_VAL[162] =
{
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
    0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
    0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
    0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
    0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
    0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

/**********************************************************************
 * @brief  Build the canonical decode tables for a Huffman table.
 **********************************************************************/
static void huff_build(huff_t * p_h)
{
    int  code = 0;
    int  k    = 0;
    int  i    = 0;
    int  total = 0;

    for (i = 1; i <= 16; i++)
    {
        p_h->valptr[i]  = k;
        p_h->mincode[i] = code;
        code += p_h->bits[i];
        p_h->maxcode[i] = (p_h->bits[i] > 0) ? (code - 1) : -1;
        k    += p_h->bits[i];
        code <<= 1;
    }
    p_h->maxcode[17] = 0x7FFFFFFF;
    total = k;
    p_h->num_values = total;
    p_h->built = 1;
}

static void huff_set_standard(huff_t * p_h, const uint8_t * p_bits,
                              const uint8_t * p_val, int nval)
{
    memcpy(p_h->bits, p_bits, 17);
    memcpy(p_h->values, p_val, (size_t)nval);
    huff_build(p_h);
}

/**********************************************************************
 * @brief  Refill the bit buffer, honouring 0xFF00 stuffing and markers.
 **********************************************************************/
static int br_fill(bitreader_t * p_br)
{
    while ((p_br->bitcnt <= 24) && (0 == p_br->marker))
    {
        uint8_t b = 0;

        if (p_br->p >= p_br->p_end)
        {
            p_br->marker = 1;   /* Ran out; treat as end. */
            break;
        }
        b = *p_br->p;
        if (0xFF == b)
        {
            uint8_t b2 = (p_br->p + 1 < p_br->p_end) ? p_br->p[1] : 0xD9;
            if (0x00 == b2)
            {
                p_br->p += 2;   /* Stuffed 0xFF. */
            }
            else
            {
                /* A real marker: stop feeding bits. */
                p_br->marker = b2;
                break;
            }
        }
        else
        {
            p_br->p += 1;
        }
        p_br->bitbuf |= (uint32_t)b << (24 - p_br->bitcnt);
        p_br->bitcnt += 8;
    }
    return 0;
}

static int br_get_bit(bitreader_t * p_br)
{
    int bit = 0;

    if (p_br->bitcnt <= 0)
    {
        br_fill(p_br);
        if (p_br->bitcnt <= 0)
        {
            return 0;
        }
    }
    bit = (int)((p_br->bitbuf >> 31) & 1U);
    p_br->bitbuf <<= 1;
    p_br->bitcnt  -= 1;
    return bit;
}

static int br_get_bits(bitreader_t * p_br, int n)
{
    int v = 0;
    int i = 0;

    for (i = 0; i < n; i++)
    {
        v = (v << 1) | br_get_bit(p_br);
    }
    return v;
}

/**********************************************************************
 * @brief  Decode one Huffman symbol.
 **********************************************************************/
static int huff_decode(bitreader_t * p_br, const huff_t * p_h)
{
    int code = 0;
    int len  = 1;

    for (len = 1; len <= 16; len++)
    {
        code = (code << 1) | br_get_bit(p_br);
        if ((p_h->maxcode[len] >= 0) && (code <= p_h->maxcode[len]))
        {
            int idx = p_h->valptr[len] + (code - p_h->mincode[len]);
            if ((idx >= 0) && (idx < p_h->num_values))
            {
                return p_h->values[idx];
            }
            return -1;
        }
    }
    return -1;
}

/**********************************************************************
 * @brief  Sign-extend an n-bit magnitude as JPEG does.
 **********************************************************************/
static int extend(int v, int n)
{
    if (n == 0)
    {
        return 0;
    }
    if (v < (1 << (n - 1)))
    {
        /* Same as v += ((-1) << n) + 1, but without shifting a negative
         * value (which is undefined behaviour). */
        v -= (1 << n) - 1;
    }
    return v;
}

/**********************************************************************
 * @brief  Separable float inverse DCT of an 8x8 block (in place-ish).
 *
 * @param[in]  in    64 dequantised coefficients in natural order.
 * @param[out] out   64 spatial samples, level-shifted and clamped 0..255.
 **********************************************************************/
static void idct_8x8(const int in[64], uint8_t out[64])
{
    static double c[8][8];
    static int    init = 0;
    double        tmp[64];
    int           x = 0;
    int           y = 0;
    int           u = 0;
    int           v = 0;

    if (0 == init)
    {
        for (u = 0; u < 8; u++)
        {
            for (x = 0; x < 8; x++)
            {
                double cu = (0 == u) ? (1.0 / sqrt(2.0)) : 1.0;
                c[u][x] = cu *
                    cos((((2.0 * x) + 1.0) * u * 3.14159265358979323846) /
                        16.0);
            }
        }
        init = 1;
    }

    /* Rows: 1-D IDCT along x for each row y. */
    for (y = 0; y < 8; y++)
    {
        for (x = 0; x < 8; x++)
        {
            double s = 0.0;
            for (u = 0; u < 8; u++)
            {
                s += c[u][x] * (double)in[(y * 8) + u];
            }
            tmp[(y * 8) + x] = s * 0.5;
        }
    }
    /* Columns: 1-D IDCT along y for each column x. */
    for (x = 0; x < 8; x++)
    {
        for (y = 0; y < 8; y++)
        {
            double s = 0.0;
            for (v = 0; v < 8; v++)
            {
                s += c[v][y] * tmp[(v * 8) + x];
            }
            {
                int val = (int)lround((s * 0.5) + 128.0);
                if (val < 0)   { val = 0; }
                if (val > 255) { val = 255; }
                out[(y * 8) + x] = (uint8_t)val;
            }
        }
    }
}

/**********************************************************************
 * @brief  Reduced IDCT: keep only the 2x2 lowest-frequency coefficients
 *         and emit one sample per 4x4 quadrant (a 2x2 block). Used for
 *         the quarter-resolution preview path, where the full 64-tap
 *         transform would dominate the frame time.
 **********************************************************************/
static void idct_2x2(const int in[64], uint8_t out[4])
{
    /* Mean of cos((2x+1)*pi/16) over x = 0..3 (one half of the block):
     * (cos(pi/16) + cos(3pi/16) + cos(5pi/16) + cos(7pi/16)) / 4.      */
    static const double MEAN_HALF = 0.6407288619;
    double f00  = (double)in[0] / 2.0;
    double f01  = (double)in[1] / sqrt(2.0);
    double f10  = (double)in[8] / sqrt(2.0);
    double f11  = (double)in[9];
    double mean = MEAN_HALF;
    int    qy   = 0;
    int    qx   = 0;

    for (qy = 0; qy < 2; qy++)
    {
        for (qx = 0; qx < 2; qx++)
        {
            double sx  = (0 == qx) ? mean : -mean;
            double sy  = (0 == qy) ? mean : -mean;
            double val = (f00 + (f01 * sx) + (f10 * sy) +
                          (f11 * sx * sy)) / 4.0;
            int    v   = (int)lround(val + 128.0);

            if (v < 0)   { v = 0; }
            if (v > 255) { v = 255; }
            out[(qy * 2) + qx] = (uint8_t)v;
        }
    }
}

/**********************************************************************
 * @brief  Decode one 8x8 block into level-shifted spatial samples.
 **********************************************************************/
static int decode_block(jpeg_ctx_t * p_ctx, bitreader_t * p_br,
                        component_t * p_comp, uint8_t out[64])
{
    int          coef[64];
    int          t    = 0;
    int          k    = 1;
    const huff_t * p_dc = &p_ctx->huff_dc[p_comp->dc_table];
    const huff_t * p_ac = &p_ctx->huff_ac[p_comp->ac_table];
    const uint16_t * p_q = p_ctx->quant[p_comp->quant_id];

    memset(coef, 0, sizeof(coef));

    /* DC coefficient. */
    t = huff_decode(p_br, p_dc);
    if (t < 0)
    {
        return JPEG_ERR;
    }
    {
        int diff = extend(br_get_bits(p_br, t), t);
        p_comp->dc_pred += diff;
        coef[0] = p_comp->dc_pred * (int)p_q[0];
    }

    /* AC coefficients. */
    for (k = 1; k < 64; )
    {
        int rs = huff_decode(p_br, p_ac);
        int r  = 0;
        int s  = 0;

        if (rs < 0)
        {
            return JPEG_ERR;
        }
        r = rs >> 4;
        s = rs & 0x0F;

        if (0 == s)
        {
            if (15 == r)
            {
                k += 16;   /* ZRL: sixteen zeros. */
                continue;
            }
            break;         /* EOB. */
        }
        k += r;
        if (k >= 64)
        {
            break;
        }
        {
            int val = extend(br_get_bits(p_br, s), s);
            coef[ZIGZAG[k]] = val * (int)p_q[ZIGZAG[k]];
        }
        k += 1;
    }

    if (0 != p_ctx->reduced)
    {
        idct_2x2(coef, out);
    }
    else
    {
        idct_8x8(coef, out);
    }
    return JPEG_OK;
}

static uint16_t rd_u16(const uint8_t * p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/**********************************************************************
 * @brief  Parse the segments up to and including SOS.
 **********************************************************************/
static int parse_headers(jpeg_ctx_t * p_ctx)
{
    const uint8_t * d = p_ctx->data;
    size_t          n = p_ctx->size;
    size_t          i = 0;

    if ((n < 2) || (0xFF != d[0]) || (0xD8 != d[1]))
    {
        return JPEG_ERR;   /* No SOI. */
    }
    i = 2;

    while ((i + 4) <= n)
    {
        uint8_t  marker = 0;
        uint16_t seg_len = 0;
        size_t   seg_start = 0;

        if (0xFF != d[i])
        {
            i++;
            continue;
        }
        marker = d[i + 1];
        i += 2;
        if ((0xD9 == marker) || (0x01 == marker) ||
            ((marker >= 0xD0) && (marker <= 0xD7)))
        {
            continue;   /* Standalone markers, no length. */
        }
        if ((i + 2) > n)
        {
            return JPEG_ERR;
        }
        seg_len   = rd_u16(&d[i]);
        seg_start = i + 2;
        if ((seg_len < 2) || ((i + seg_len) > n))
        {
            return JPEG_ERR;
        }

        if (0xDB == marker)                 /* DQT */
        {
            size_t q = seg_start;
            while (q < (i + seg_len))
            {
                int pq = (d[q] >> 4) & 0x0F;
                int tq = d[q] & 0x0F;
                int e  = 0;
                q++;
                if (tq >= 4)
                {
                    return JPEG_ERR;
                }
                for (e = 0; e < 64; e++)
                {
                    if (0 != pq)
                    {
                        p_ctx->quant[tq][e] = rd_u16(&d[q]);
                        q += 2;
                    }
                    else
                    {
                        p_ctx->quant[tq][e] = d[q];
                        q += 1;
                    }
                }
            }
        }
        else if (0xC0 == marker)            /* SOF0 (baseline) */
        {
            int c = 0;
            p_ctx->height = rd_u16(&d[seg_start + 1]);
            p_ctx->width  = rd_u16(&d[seg_start + 3]);
            p_ctx->num_components = d[seg_start + 5];
            if ((p_ctx->num_components < 1) ||
                (p_ctx->num_components > JPEG_MAX_COMPONENTS))
            {
                return JPEG_ERR;
            }
            for (c = 0; c < p_ctx->num_components; c++)
            {
                size_t o = seg_start + 6 + ((size_t)c * 3);
                p_ctx->comp[c].id       = d[o];
                p_ctx->comp[c].h        = (d[o + 1] >> 4) & 0x0F;
                p_ctx->comp[c].v        = d[o + 1] & 0x0F;
                p_ctx->comp[c].quant_id = d[o + 2];
                if (p_ctx->comp[c].h > p_ctx->max_h)
                {
                    p_ctx->max_h = p_ctx->comp[c].h;
                }
                if (p_ctx->comp[c].v > p_ctx->max_v)
                {
                    p_ctx->max_v = p_ctx->comp[c].v;
                }
            }
        }
        else if ((0xC1 <= marker) && (0xCF >= marker) &&
                 (0xC4 != marker) && (0xC8 != marker) && (0xCC != marker))
        {
            /* Any other SOFn = unsupported (progressive, arithmetic..). */
            return JPEG_ERR;
        }
        else if (0xC4 == marker)            /* DHT */
        {
            size_t q = seg_start;
            while (q < (i + seg_len))
            {
                int    tc = (d[q] >> 4) & 0x0F;
                int    th = d[q] & 0x0F;
                huff_t * p_h = NULL;
                int    count = 0;
                int    b     = 0;
                q++;
                if (th >= 4)
                {
                    return JPEG_ERR;
                }
                p_h = (0 == tc) ? &p_ctx->huff_dc[th]
                                : &p_ctx->huff_ac[th];
                p_h->bits[0] = 0;
                for (b = 1; b <= 16; b++)
                {
                    p_h->bits[b] = d[q];
                    count += d[q];
                    q++;
                }
                for (b = 0; b < count; b++)
                {
                    p_h->values[b] = d[q];
                    q++;
                }
                huff_build(p_h);
            }
        }
        else if (0xDD == marker)            /* DRI */
        {
            p_ctx->restart_interval = rd_u16(&d[seg_start]);
        }
        else if (0xDA == marker)            /* SOS */
        {
            int ns = d[seg_start];
            int s  = 0;
            for (s = 0; s < ns; s++)
            {
                int cid = d[seg_start + 1 + (s * 2)];
                int tt  = d[seg_start + 2 + (s * 2)];
                int c   = 0;
                for (c = 0; c < p_ctx->num_components; c++)
                {
                    if (p_ctx->comp[c].id == cid)
                    {
                        p_ctx->comp[c].dc_table = (tt >> 4) & 0x0F;
                        p_ctx->comp[c].ac_table = tt & 0x0F;
                    }
                }
            }
            /* Entropy data starts right after the SOS segment. */
            p_ctx->pos = i + seg_len;
            return JPEG_OK;
        }

        i += seg_len;
    }
    return JPEG_ERR;   /* No SOS found. */
}

/**********************************************************************
 * @brief  Ensure every table a scan references actually exists; fill in
 *         the standard tables where a stream left them undefined.
 **********************************************************************/
static void ensure_tables(jpeg_ctx_t * p_ctx)
{
    int c = 0;

    for (c = 0; c < p_ctx->num_components; c++)
    {
        int dc = p_ctx->comp[c].dc_table;
        int ac = p_ctx->comp[c].ac_table;
        int luma = (0 == c);   /* Component 0 is luma. */

        if (0 == p_ctx->huff_dc[dc].built)
        {
            if (0 != luma)
            {
                huff_set_standard(&p_ctx->huff_dc[dc], STD_DC_LUMA_BITS,
                                  STD_DC_LUMA_VAL, 12);
            }
            else
            {
                huff_set_standard(&p_ctx->huff_dc[dc], STD_DC_CHROMA_BITS,
                                  STD_DC_CHROMA_VAL, 12);
            }
        }
        if (0 == p_ctx->huff_ac[ac].built)
        {
            if (0 != luma)
            {
                huff_set_standard(&p_ctx->huff_ac[ac], STD_AC_LUMA_BITS,
                                  STD_AC_LUMA_VAL, 162);
            }
            else
            {
                huff_set_standard(&p_ctx->huff_ac[ac], STD_AC_CHROMA_BITS,
                                  STD_AC_CHROMA_VAL, 162);
            }
        }
    }
}

/**********************************************************************
 * @brief  One decoded component plane, padded to whole MCUs.
 **********************************************************************/
typedef struct
{
    uint8_t * p_pix;
    int       stride;   /* Bytes per row (padded width).            */
    int       rows;     /* Padded height.                           */
    int       h;        /* Component sampling factors, for lookup.  */
    int       v;
    int       block_dim;/* 8 for full decode, 2 for reduced.       */
} plane_t;

/**********************************************************************
 * @brief  Copy one 8x8 block into its place in a component plane.
 **********************************************************************/
static void store_block(plane_t * p_pl, const uint8_t block[64],
                        int px0, int py0)
{
    int yy  = 0;
    int dim = p_pl->block_dim;

    for (yy = 0; yy < dim; yy++)
    {
        memcpy(p_pl->p_pix + ((size_t)(py0 + yy) * (size_t)p_pl->stride)
                   + (size_t)px0, block + (yy * dim), (size_t)dim);
    }
}

/**********************************************************************
 * @brief  Handle the restart interval bookkeeping after one MCU.
 **********************************************************************/
static void restart_tick(jpeg_ctx_t * p_ctx, bitreader_t * p_br,
                         int * p_restart_ctr, int is_last_mcu)
{
    int rc = 0;

    if (p_ctx->restart_interval <= 0)
    {
        return;
    }
    (*p_restart_ctr)++;
    if ((*p_restart_ctr != p_ctx->restart_interval) || (0 != is_last_mcu))
    {
        return;
    }
    /* Discard any partial bits, consume RSTn, reset DC. */
    p_br->bitbuf = 0;
    p_br->bitcnt = 0;
    if ((p_br->marker >= 0xD0) && (p_br->marker <= 0xD7))
    {
        p_br->marker = 0;
    }
    for (rc = 0; rc < p_ctx->num_components; rc++)
    {
        p_ctx->comp[rc].dc_pred = 0;
    }
    *p_restart_ctr = 0;
}

/**********************************************************************
 * @brief  Decode every block of one component within one MCU.
 **********************************************************************/
static int decode_mcu_component(jpeg_ctx_t * p_ctx, bitreader_t * p_br,
                                int c, plane_t * p_pl, int mcux, int mcuy)
{
    component_t * cp = &p_ctx->comp[c];
    int           bx = 0;
    int           by = 0;

    for (by = 0; by < cp->v; by++)
    {
        for (bx = 0; bx < cp->h; bx++)
        {
            uint8_t block[64];

            if (JPEG_OK != decode_block(p_ctx, p_br, cp, block))
            {
                return JPEG_ERR;
            }
            store_block(p_pl, block,
                        ((mcux * cp->h) + bx) * p_pl->block_dim,
                        ((mcuy * cp->v) + by) * p_pl->block_dim);
        }
    }
    return JPEG_OK;
}

/**********************************************************************
 * @brief  Decode the entropy data into padded per-component planes.
 **********************************************************************/
static int decode_scan(jpeg_ctx_t * p_ctx, plane_t * p_planes)
{
    bitreader_t br;
    int         mcux        = 0;
    int         mcuy        = 0;
    int         mcus_x      = 0;
    int         mcus_y      = 0;
    int         mcu_w       = p_ctx->max_h * 8;
    int         mcu_h       = p_ctx->max_v * 8;
    int         restart_ctr = 0;
    int         c           = 0;

    mcus_x = (p_ctx->width  + mcu_w - 1) / mcu_w;
    mcus_y = (p_ctx->height + mcu_h - 1) / mcu_h;

    memset(&br, 0, sizeof(br));
    br.p     = p_ctx->data + p_ctx->pos;
    br.p_end = p_ctx->data + p_ctx->size;

    for (c = 0; c < p_ctx->num_components; c++)
    {
        p_ctx->comp[c].dc_pred = 0;
    }

    for (mcuy = 0; mcuy < mcus_y; mcuy++)
    {
        for (mcux = 0; mcux < mcus_x; mcux++)
        {
            for (c = 0; c < p_ctx->num_components; c++)
            {
                if (JPEG_OK != decode_mcu_component(p_ctx, &br, c,
                                                    &p_planes[c],
                                                    mcux, mcuy))
                {
                    return JPEG_ERR;
                }
            }
            restart_tick(p_ctx, &br, &restart_ctr,
                         (mcuy == (mcus_y - 1)) && (mcux == (mcus_x - 1)));
        }
    }
    return JPEG_OK;
}

/**********************************************************************
 * @brief  Allocate padded planes for every component.
 **********************************************************************/
static int alloc_planes(const jpeg_ctx_t * p_ctx, plane_t * p_planes)
{
    int mcus_x = (p_ctx->width  + (p_ctx->max_h * 8) - 1) /
                 (p_ctx->max_h * 8);
    int mcus_y = (p_ctx->height + (p_ctx->max_v * 8) - 1) /
                 (p_ctx->max_v * 8);
    int c      = 0;

    for (c = 0; c < p_ctx->num_components; c++)
    {
        plane_t * pl = &p_planes[c];

        pl->h         = p_ctx->comp[c].h;
        pl->v         = p_ctx->comp[c].v;
        pl->block_dim = (0 != p_ctx->reduced) ? 2 : 8;
        pl->stride    = mcus_x * pl->h * pl->block_dim;
        pl->rows      = mcus_y * pl->v * pl->block_dim;
        pl->p_pix  = (uint8_t *)calloc((size_t)pl->stride *
                                       (size_t)pl->rows, 1U);
        if (NULL == pl->p_pix)
        {
            return JPEG_ERR;
        }
    }
    return JPEG_OK;
}

static void free_planes(plane_t * p_planes)
{
    int c = 0;

    for (c = 0; c < JPEG_MAX_COMPONENTS; c++)
    {
        if (NULL != p_planes[c].p_pix)
        {
            free(p_planes[c].p_pix);
            p_planes[c].p_pix = NULL;
        }
    }
}

/**********************************************************************
 * @brief  Parse headers and decode all planes. Caller frees both the
 *         context and the planes.
 **********************************************************************/
typedef struct
{
    const uint8_t * p_data;
    size_t          size;
    int             reduced;
} decode_request_t;

static int decode_planes(const decode_request_t * p_req,
                         jpeg_ctx_t ** pp_ctx, plane_t * p_planes)
{
    jpeg_ctx_t * p_ctx  = NULL;
    int          result = JPEG_ERR;

    if ((NULL == p_req->p_data) || (p_req->size < 4))
    {
        goto cleanup;
    }
    p_ctx = (jpeg_ctx_t *)calloc(1U, sizeof(jpeg_ctx_t));
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    p_ctx->data    = p_req->p_data;
    p_ctx->size    = p_req->size;
    p_ctx->reduced = p_req->reduced;
    if (JPEG_OK != parse_headers(p_ctx))
    {
        goto cleanup;
    }
    if ((p_ctx->width <= 0) || (p_ctx->height <= 0) ||
        (p_ctx->max_h <= 0) || (p_ctx->max_v <= 0) ||
        (p_ctx->num_components <= 0))
    {
        goto cleanup;
    }
    ensure_tables(p_ctx);
    if (JPEG_OK != alloc_planes(p_ctx, p_planes))
    {
        goto cleanup;
    }
    if (JPEG_OK != decode_scan(p_ctx, p_planes))
    {
        goto cleanup;
    }
    *pp_ctx = p_ctx;
    p_ctx   = NULL;
    result  = JPEG_OK;

cleanup:
    if (NULL != p_ctx)
    {
        free(p_ctx);
    }
    return result;
}

int jpeg_decode_gray(const uint8_t * p_data, size_t size,
                     jpeg_image_t * p_out)
{
    jpeg_ctx_t * p_ctx  = NULL;
    uint8_t *    p_gray = NULL;
    int          result = JPEG_ERR;
    int          y      = 0;
    plane_t      planes[JPEG_MAX_COMPONENTS];
    decode_request_t req;

    memset(planes, 0, sizeof(planes));
    if (NULL != p_out)
    {
        memset(p_out, 0, sizeof(*p_out));
    }
    if (NULL == p_out)
    {
        goto cleanup;
    }
    req.p_data  = p_data;
    req.size    = size;
    req.reduced = 0;
    if (JPEG_OK != decode_planes(&req, &p_ctx, planes))
    {
        goto cleanup;
    }
    p_gray = (uint8_t *)malloc((size_t)p_ctx->width *
                               (size_t)p_ctx->height);
    if (NULL == p_gray)
    {
        goto cleanup;
    }
    for (y = 0; y < p_ctx->height; y++)
    {
        memcpy(p_gray + ((size_t)y * (size_t)p_ctx->width),
               planes[0].p_pix + ((size_t)y * (size_t)planes[0].stride),
               (size_t)p_ctx->width);
    }
    p_out->width  = p_ctx->width;
    p_out->height = p_ctx->height;
    p_out->p_gray = p_gray;
    p_gray = NULL;
    result = JPEG_OK;

cleanup:
    free_planes(planes);
    if (NULL != p_gray)
    {
        free(p_gray);
    }
    if (NULL != p_ctx)
    {
        free(p_ctx);
    }
    return result;
}

/**********************************************************************
 * @brief  Clamp an integer to the 0..255 range.
 **********************************************************************/
static uint8_t clamp_u8(int v)
{
    if (v < 0)
    {
        return 0U;
    }
    if (v > 255)
    {
        return 255U;
    }
    return (uint8_t)v;
}

/**********************************************************************
 * @brief  Convert one row from YCbCr planes to packed RGB.
 *
 * Chroma is nearest-neighbour upsampled by the sampling ratio. The
 * conversion uses 16.16 fixed point (JFIF constants 1.402, 0.344136,
 * 0.714136, 1.772).
 **********************************************************************/
static void convert_row(const jpeg_ctx_t * p_ctx, const plane_t * p_planes,
                        int y, uint8_t * p_row)
{
    const uint8_t * p_y  = p_planes[0].p_pix +
                           ((size_t)y * (size_t)p_planes[0].stride);
    int             cy   = (y * p_planes[1].v) / p_ctx->max_v;
    int             cyr  = (y * p_planes[2].v) / p_ctx->max_v;
    const uint8_t * p_cb = p_planes[1].p_pix +
                           ((size_t)cy * (size_t)p_planes[1].stride);
    const uint8_t * p_cr = p_planes[2].p_pix +
                           ((size_t)cyr * (size_t)p_planes[2].stride);
    int             x    = 0;

    for (x = 0; x < p_ctx->width; x++)
    {
        int yy = p_y[x] << 16;
        int cb = (int)p_cb[(x * p_planes[1].h) / p_ctx->max_h] - 128;
        int cr = (int)p_cr[(x * p_planes[2].h) / p_ctx->max_h] - 128;

        p_row[(x * 3) + 0] = clamp_u8((yy + (91881 * cr)) >> 16);
        p_row[(x * 3) + 1] = clamp_u8((yy - (22554 * cb) -
                                       (46802 * cr)) >> 16);
        p_row[(x * 3) + 2] = clamp_u8((yy + (116130 * cb)) >> 16);
    }
}

static int decode_rgb_impl(const decode_request_t * p_req,
                           jpeg_rgb_t * p_out)
{
    jpeg_ctx_t * p_ctx  = NULL;
    uint8_t *    p_rgb  = NULL;
    int          result = JPEG_ERR;
    int          y      = 0;
    int          x      = 0;
    int          out_w  = 0;
    int          out_h  = 0;
    plane_t      planes[JPEG_MAX_COMPONENTS];

    memset(planes, 0, sizeof(planes));
    if (NULL != p_out)
    {
        memset(p_out, 0, sizeof(*p_out));
    }
    if (NULL == p_out)
    {
        goto cleanup;
    }
    if (JPEG_OK != decode_planes(p_req, &p_ctx, planes))
    {
        goto cleanup;
    }
    /* Reduced planes are 1/4 size; so is the output. */
    out_w = (0 != p_req->reduced) ? (p_ctx->width / 4) : p_ctx->width;
    out_h = (0 != p_req->reduced) ? (p_ctx->height / 4) : p_ctx->height;
    if ((out_w <= 0) || (out_h <= 0))
    {
        goto cleanup;
    }
    p_ctx->width  = out_w;
    p_ctx->height = out_h;
    p_rgb = (uint8_t *)malloc((size_t)out_w * (size_t)out_h * 3U);
    if (NULL == p_rgb)
    {
        goto cleanup;
    }
    for (y = 0; y < out_h; y++)
    {
        uint8_t * p_row = p_rgb + ((size_t)y * (size_t)out_w * 3U);

        if (p_ctx->num_components >= 3)
        {
            convert_row(p_ctx, planes, y, p_row);
        }
        else
        {
            const uint8_t * p_y = planes[0].p_pix +
                                  ((size_t)y * (size_t)planes[0].stride);
            for (x = 0; x < out_w; x++)
            {
                memset(p_row + (x * 3), p_y[x], 3U);
            }
        }
    }
    p_out->width  = out_w;
    p_out->height = out_h;
    p_out->p_rgb  = p_rgb;
    p_rgb  = NULL;
    result = JPEG_OK;

cleanup:
    free_planes(planes);
    if (NULL != p_rgb)
    {
        free(p_rgb);
    }
    if (NULL != p_ctx)
    {
        free(p_ctx);
    }
    return result;
}

int jpeg_decode_rgb(const uint8_t * p_data, size_t size,
                    jpeg_rgb_t * p_out)
{
    decode_request_t req;

    req.p_data  = p_data;
    req.size    = size;
    req.reduced = 0;
    return decode_rgb_impl(&req, p_out);
}

int jpeg_decode_rgb_quarter(const uint8_t * p_data, size_t size,
                            jpeg_rgb_t * p_out)
{
    decode_request_t req;

    req.p_data  = p_data;
    req.size    = size;
    req.reduced = 1;
    return decode_rgb_impl(&req, p_out);
}

void jpeg_rgb_free(jpeg_rgb_t * p_img)
{
    if ((NULL != p_img) && (NULL != p_img->p_rgb))
    {
        free(p_img->p_rgb);
        p_img->p_rgb = NULL;
    }
}

void jpeg_image_free(jpeg_image_t * p_img)
{
    if ((NULL != p_img) && (NULL != p_img->p_gray))
    {
        free(p_img->p_gray);
        p_img->p_gray = NULL;
    }
}
