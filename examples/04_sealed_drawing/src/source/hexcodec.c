#include "hexcodec.h"

#include <stdio.h>

#define HEX_CHARS_PER_BYTE  (2)
#define NIBBLE_BITS         (4)
#define NIBBLE_MASK         (0x0FU)
#define DECIMAL_BASE        (10)
#define BAD_NIBBLE          (-1)

static const char HEX_DIGITS[] = "0123456789abcdef";

static int hex_nibble(char c)
{
    int value = BAD_NIBBLE;

    if ((c >= '0') && (c <= '9'))
    {
        value = c - '0';
    }
    else if ((c >= 'a') && (c <= 'f'))
    {
        value = (c - 'a') + DECIMAL_BASE;
    }
    else if ((c >= 'A') && (c <= 'F'))
    {
        value = (c - 'A') + DECIMAL_BASE;
    }

    return value;
}

int hex_encode(byte_span_t in, char * p_out, size_t out_cap)
{
    int    result = -1;
    size_t i      = 0;
    size_t needed = 0;

    if ((NULL == p_out) || ((NULL == in.p_data) && (0U != in.len)))
    {
        goto cleanup;
    }
    needed = (in.len * HEX_CHARS_PER_BYTE) + 1U;
    if (out_cap < needed)
    {
        goto cleanup;
    }
    for (i = 0; i < in.len; i++)
    {
        uint8_t byte = in.p_data[i];

        p_out[i * HEX_CHARS_PER_BYTE] =
            HEX_DIGITS[(byte >> NIBBLE_BITS) & NIBBLE_MASK];
        p_out[(i * HEX_CHARS_PER_BYTE) + 1U] =
            HEX_DIGITS[byte & NIBBLE_MASK];
    }
    p_out[in.len * HEX_CHARS_PER_BYTE] = '\0';
    result = 0;

cleanup:

    return result;
}

int hex_decode(const char * p_hex, byte_buf_t * p_out)
{
    int          result = -1;
    size_t       count  = 0;
    const char * p_cur  = NULL;

    if ((NULL == p_hex) || (NULL == p_out) || (NULL == p_out->p_data))
    {
        goto cleanup;
    }
    p_cur = p_hex;
    while ('\0' != p_cur[0])
    {
        int hi = 0;
        int lo = 0;

        if (('\0' == p_cur[1]) || (count >= p_out->cap))
        {
            goto cleanup;
        }
        hi = hex_nibble(p_cur[0]);
        lo = hex_nibble(p_cur[1]);
        if ((BAD_NIBBLE == hi) || (BAD_NIBBLE == lo))
        {
            goto cleanup;
        }
        p_out->p_data[count] = (uint8_t)((hi << NIBBLE_BITS) | lo);
        count++;
        p_cur += HEX_CHARS_PER_BYTE;
    }
    p_out->len = count;
    result     = 0;

cleanup:

    return result;
}
