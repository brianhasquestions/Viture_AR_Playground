/**********************************************************************
 * @file    glyph.c
 * @brief   Glyph code layer: payload <-> 8x8 module grid.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "glyph.h"

#include <string.h>

#define INNER_LO    (1)
#define INNER_HI    (6)   /* Interior modules are rows/cols 1..6. */

uint8_t glyph_crc8(const uint8_t * p_data, int len)
{
    uint8_t crc = 0x00U;
    int     i   = 0;
    int     b   = 0;

    for (i = 0; i < len; i++)
    {
        crc ^= p_data[i];
        for (b = 0; b < 8; b++)
        {
            if (0U != (crc & 0x80U))
            {
                crc = (uint8_t)((crc << 1) ^ 0x07U);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/**********************************************************************
 * @brief  Is module (r,c) one of the four orientation-corner cells?
 **********************************************************************/
static int is_corner(int r, int c)
{
    return (((r == INNER_LO) || (r == INNER_HI)) &&
            ((c == INNER_LO) || (c == INNER_HI))) ? 1 : 0;
}

/**********************************************************************
 * @brief  Walk the 32 data modules in row-major order, invoking cb.
 *
 * The four orientation corners are skipped, so exactly 32 cells are
 * visited: bits 0..23 are payload, 24..31 are the CRC.
 **********************************************************************/
static void for_each_data_cell(void * p_user,
                               void (*cb)(void *, int idx, int r, int c))
{
    int idx = 0;
    int r   = 0;
    int c   = 0;

    for (r = INNER_LO; r <= INNER_HI; r++)
    {
        for (c = INNER_LO; c <= INNER_HI; c++)
        {
            if (0 != is_corner(r, c))
            {
                continue;
            }
            cb(p_user, idx, r, c);
            idx++;
        }
    }
}

/* --- encode --------------------------------------------------------- */

typedef struct
{
    uint8_t bits[32];   /* 24 payload + 8 CRC, MSB-first per byte. */
    uint8_t (*grid)[GLYPH_MODULES];
} enc_t;

static void enc_cell(void * p_user, int idx, int r, int c)
{
    enc_t * e = (enc_t *)p_user;
    e->grid[r][c] = e->bits[idx];
}

void glyph_encode(const uint8_t * p_payload,
                  uint8_t grid[GLYPH_MODULES][GLYPH_MODULES])
{
    enc_t   e;
    uint8_t full[GLYPH_PAYLOAD_BYTES + 1];
    int     i = 0;
    int     r = 0;
    int     c = 0;

    memset(grid, 0, (size_t)GLYPH_MODULES * GLYPH_MODULES);

    /* Border ring: black. */
    for (i = 0; i < GLYPH_MODULES; i++)
    {
        grid[0][i] = 1;
        grid[GLYPH_MODULES - 1][i] = 1;
        grid[i][0] = 1;
        grid[i][GLYPH_MODULES - 1] = 1;
    }

    /* Orientation corners: top-left black, the other three white. */
    grid[INNER_LO][INNER_LO] = 1;
    grid[INNER_LO][INNER_HI] = 0;
    grid[INNER_HI][INNER_LO] = 0;
    grid[INNER_HI][INNER_HI] = 0;

    /* Payload followed by its CRC, expanded MSB-first into 32 bits. */
    memcpy(full, p_payload, GLYPH_PAYLOAD_BYTES);
    full[GLYPH_PAYLOAD_BYTES] = glyph_crc8(p_payload, GLYPH_PAYLOAD_BYTES);
    for (i = 0; i < 32; i++)
    {
        int byte = i / 8;
        int bit  = 7 - (i % 8);
        e.bits[i] = (uint8_t)((full[byte] >> bit) & 1U);
    }
    e.grid = grid;
    for_each_data_cell(&e, enc_cell);

    (void)r; (void)c;
}

/* --- decode --------------------------------------------------------- */

typedef struct
{
    const uint8_t (*grid)[GLYPH_MODULES];
    uint8_t bits[32];
} dec_t;

static void dec_cell(void * p_user, int idx, int r, int c)
{
    dec_t * d = (dec_t *)p_user;
    d->bits[idx] = d->grid[r][c];
}

/**********************************************************************
 * @brief  Rotate an 8x8 grid 90 degrees clockwise.
 **********************************************************************/
static void rotate_cw(const uint8_t in[GLYPH_MODULES][GLYPH_MODULES],
                      uint8_t out[GLYPH_MODULES][GLYPH_MODULES])
{
    int r = 0;
    int c = 0;

    for (r = 0; r < GLYPH_MODULES; r++)
    {
        for (c = 0; c < GLYPH_MODULES; c++)
        {
            out[c][GLYPH_MODULES - 1 - r] = in[r][c];
        }
    }
}

/**********************************************************************
 * @brief  Check the border ring and orientation corners of one grid.
 **********************************************************************/
static int layout_ok(const uint8_t grid[GLYPH_MODULES][GLYPH_MODULES])
{
    int i = 0;

    for (i = 0; i < GLYPH_MODULES; i++)
    {
        if ((0 == grid[0][i]) || (0 == grid[GLYPH_MODULES - 1][i]) ||
            (0 == grid[i][0]) || (0 == grid[i][GLYPH_MODULES - 1]))
        {
            return 0;   /* Border must be fully black. */
        }
    }
    if ((1 != grid[INNER_LO][INNER_LO]) ||
        (0 != grid[INNER_LO][INNER_HI]) ||
        (0 != grid[INNER_HI][INNER_LO]) ||
        (0 != grid[INNER_HI][INNER_HI]))
    {
        return 0;       /* Orientation L must match. */
    }
    return 1;
}

static int try_decode_oriented(
        const uint8_t grid[GLYPH_MODULES][GLYPH_MODULES],
        uint8_t * p_payload)
{
    dec_t   d;
    uint8_t full[GLYPH_PAYLOAD_BYTES + 1];
    int     i = 0;

    if (0 == layout_ok(grid))
    {
        return -1;
    }

    d.grid = grid;
    for_each_data_cell(&d, dec_cell);

    memset(full, 0, sizeof(full));
    for (i = 0; i < 32; i++)
    {
        int byte = i / 8;
        int bit  = 7 - (i % 8);
        if (0 != d.bits[i])
        {
            full[byte] |= (uint8_t)(1U << bit);
        }
    }

    if (glyph_crc8(full, GLYPH_PAYLOAD_BYTES) != full[GLYPH_PAYLOAD_BYTES])
    {
        return -1;      /* CRC mismatch. */
    }

    memcpy(p_payload, full, GLYPH_PAYLOAD_BYTES);
    return 0;
}

int glyph_decode(const uint8_t grid[GLYPH_MODULES][GLYPH_MODULES],
                 uint8_t * p_payload)
{
    uint8_t cur[GLYPH_MODULES][GLYPH_MODULES];
    uint8_t nxt[GLYPH_MODULES][GLYPH_MODULES];
    int     rot = 0;

    if ((NULL == grid) || (NULL == p_payload))
    {
        return -1;
    }

    memcpy(cur, grid, sizeof(cur));

    for (rot = 0; rot < 4; rot++)
    {
        if (0 == try_decode_oriented(
                     (const uint8_t (*)[GLYPH_MODULES])cur, p_payload))
        {
            return 0;
        }
        rotate_cw((const uint8_t (*)[GLYPH_MODULES])cur, nxt);
        memcpy(cur, nxt, sizeof(cur));
    }
    return -1;
}
