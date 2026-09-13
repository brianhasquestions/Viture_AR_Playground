#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>

int fileio_read_all(const char * p_path, byte_buf_t * p_out)
{
    int       result = -1;
    FILE *    p_file = NULL;
    uint8_t * p_buf  = NULL;
    long      size   = 0;

    if ((NULL == p_path) || (NULL == p_out))
    {
        goto cleanup;
    }
    p_file = fopen(p_path, "rb");
    if (NULL == p_file)
    {
        goto cleanup;
    }
    if (0 != fseek(p_file, 0L, SEEK_END))
    {
        goto cleanup;
    }
    size = ftell(p_file);
    if ((size <= 0L) || (0 != fseek(p_file, 0L, SEEK_SET)))
    {
        goto cleanup;
    }
    p_buf = (uint8_t *)malloc((size_t)size);
    if (NULL == p_buf)
    {
        goto cleanup;
    }
    if (fread(p_buf, 1U, (size_t)size, p_file) != (size_t)size)
    {
        goto cleanup;
    }
    p_out->p_data = p_buf;
    p_out->cap    = (size_t)size;
    p_out->len    = (size_t)size;
    p_buf         = NULL;
    result        = 0;

cleanup:
    if (NULL != p_file)
    {
        (void)fclose(p_file);
    }
    if (NULL != p_buf)
    {
        free(p_buf);
    }

    return result;
}
