/**********************************************************************
 * @file    xr_args.c
 * @brief   Checked command-line value parsing.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "xr_args.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

/**********************************************************************
 * @brief  Is p_text empty or nothing but whitespace?
 *
 * strtol/strtof skip leading whitespace and would then report "no
 * conversion" the same way they do for real garbage; checking up front
 * keeps the two cases from needing separate handling below.
 **********************************************************************/
static int is_blank(const char * p_text)
{
    int blank = 1;

    while ('\0' != *p_text)
    {
        if (0 == isspace((unsigned char)*p_text))
        {
            blank = 0;
            break;
        }
        p_text++;
    }

    return blank;
}

int xr_args_parse_int(const char * p_text, int * p_out)
{
    int     result = -1;
    long    value  = 0;
    char *  p_end  = NULL;

    if ((NULL == p_text) || (NULL == p_out) || (0 != is_blank(p_text)))
    {
        goto cleanup;
    }

    errno = 0;
    value = strtol(p_text, &p_end, 10);

    /* Reject trailing garbage ("12abc"), overflow, and the long -> int
     * narrowing that would otherwise wrap silently on LP64. */
    if (('\0' != *p_end) || (0 != errno) ||
        (value > (long)INT_MAX) || (value < (long)INT_MIN))
    {
        goto cleanup;
    }

    *p_out = (int)value;
    result = 0;

cleanup:
    return result;
}

int xr_args_parse_float(const char * p_text, float * p_out)
{
    int    result = -1;
    float  value  = 0.0F;
    char * p_end  = NULL;

    if ((NULL == p_text) || (NULL == p_out) || (0 != is_blank(p_text)))
    {
        goto cleanup;
    }

    errno = 0;
    value = strtof(p_text, &p_end);

    /* strtof accepts "inf" and "nan" outright; both would propagate
     * through the projection matrix and blank the display. */
    if (('\0' != *p_end) || (0 != errno) || (0 == isfinite(value)))
    {
        goto cleanup;
    }

    *p_out = value;
    result = 0;

cleanup:
    return result;
}
