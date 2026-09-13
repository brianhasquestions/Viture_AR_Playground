#include "vault.h"

#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC_BYTES     (4)
#define LEN_BYTES       (4)
#define COUNT_BYTES     (1)
#define HEAD_BYTES      (PHASH_BYTES + COUNT_BYTES)
#define BYTE_SHIFT_3    (24)
#define BYTE_SHIFT_2    (16)
#define BYTE_SHIFT_1    (8)
#define BYTE_MASK       (0xFFU)
#define NO_MATCH_DIST   (PHASH_BITS + 1)

static const uint8_t MAGIC[MAGIC_BYTES] = { 'S', 'D', 'V', '3' };

static void write_u32(uint8_t * p_out, uint32_t value)
{
    p_out[0] = (uint8_t)((value >> BYTE_SHIFT_3) & BYTE_MASK);
    p_out[1] = (uint8_t)((value >> BYTE_SHIFT_2) & BYTE_MASK);
    p_out[2] = (uint8_t)((value >> BYTE_SHIFT_1) & BYTE_MASK);
    p_out[3] = (uint8_t)(value & BYTE_MASK);
}

static uint32_t read_u32(const uint8_t * p_in)
{
    uint32_t value = ((uint32_t)p_in[0] << BYTE_SHIFT_3) |
                     ((uint32_t)p_in[1] << BYTE_SHIFT_2) |
                     ((uint32_t)p_in[2] << BYTE_SHIFT_1) |
                     (uint32_t)p_in[3];

    return value;
}

static int write_set(FILE * p_file, const phash_set_t * p_set)
{
    int     result = -1;
    uint8_t count  = (uint8_t)p_set->count;

    if ((p_set->count < 0) || (p_set->count > PHASH_MAX_VARIANTS))
    {
        goto cleanup;
    }
    if (fwrite(p_set->primary, 1U, PHASH_BYTES, p_file) != PHASH_BYTES)
    {
        goto cleanup;
    }
    if (fwrite(&count, 1U, COUNT_BYTES, p_file) != COUNT_BYTES)
    {
        goto cleanup;
    }
    if (fwrite(p_set->variants, PHASH_BYTES, count, p_file) != count)
    {
        goto cleanup;
    }
    if (fwrite(p_set->colour, 1U, COLORSIG_BINS, p_file) != COLORSIG_BINS)
    {
        goto cleanup;
    }
    result = 0;

cleanup:

    return result;
}

int vault_store(const char * p_path, const phash_set_t * p_set,
                byte_span_t blob)
{
    int     result = -1;
    FILE *  p_file = NULL;
    long    size   = 0;
    uint8_t len[LEN_BYTES];

    if ((NULL == p_path) || (NULL == p_set) || (NULL == blob.p_data) ||
        (blob.len > VAULT_MAX_BLOB))
    {
        goto cleanup;
    }
    p_file = fopen(p_path, "ab");
    if (NULL == p_file)
    {
        goto cleanup;
    }
    if (0 != fseek(p_file, 0L, SEEK_END))
    {
        goto cleanup;
    }
    size = ftell(p_file);
    if ((0L == size) &&
        (fwrite(MAGIC, 1U, MAGIC_BYTES, p_file) != MAGIC_BYTES))
    {
        goto cleanup;
    }
    if (0 != write_set(p_file, p_set))
    {
        goto cleanup;
    }
    write_u32(len, (uint32_t)blob.len);
    if (fwrite(len, 1U, LEN_BYTES, p_file) != LEN_BYTES)
    {
        goto cleanup;
    }
    if (fwrite(blob.p_data, 1U, blob.len, p_file) != blob.len)
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

typedef struct
{
    const uint8_t * p_primary;
    size_t          blob_off;
    size_t          blob_len;
    size_t          rec_len;
    int             dist;
} record_t;

static int parse_record(const byte_buf_t * p_file, size_t pos,
                        record_t * p_rec)
{
    int             result = -1;
    const uint8_t * p_at   = p_file->p_data + pos;
    size_t          count  = 0;
    size_t          set_len = 0;

    if ((pos + HEAD_BYTES) > p_file->len)
    {
        goto cleanup;
    }
    count   = p_at[PHASH_BYTES];
    set_len = HEAD_BYTES + (count * PHASH_BYTES) + COLORSIG_BINS;
    if ((count > PHASH_MAX_VARIANTS) ||
        ((pos + set_len + LEN_BYTES) > p_file->len))
    {
        goto cleanup;
    }
    p_rec->p_primary = p_at;
    p_rec->blob_len  = read_u32(p_at + set_len);
    p_rec->blob_off  = pos + set_len + LEN_BYTES;
    p_rec->rec_len   = set_len + LEN_BYTES + p_rec->blob_len;
    if ((p_rec->blob_len > VAULT_MAX_BLOB) ||
        ((pos + p_rec->rec_len) > p_file->len))
    {
        goto cleanup;
    }
    result = 0;

cleanup:

    return result;
}

static int record_distance(const record_t * p_rec,
                           const phash_set_t * p_live)
{
    phash_set_t set;
    size_t      count = p_rec->p_primary[PHASH_BYTES];

    memset(&set, 0, sizeof(set));
    memcpy(set.primary, p_rec->p_primary, PHASH_BYTES);
    memcpy(set.variants, p_rec->p_primary + HEAD_BYTES, count * PHASH_BYTES);
    memcpy(set.colour, p_rec->p_primary + HEAD_BYTES + (count * PHASH_BYTES),
           COLORSIG_BINS);
    set.count = (int)count;

    return phash_set_match(p_live, &set);
}

int vault_find(const char * p_path, const vault_query_t * p_query,
               vault_match_t * p_match)
{
    int        result = VAULT_ERROR;
    size_t     pos    = 0;
    record_t   best;
    record_t   rec;
    byte_buf_t file;

    memset(&file, 0, sizeof(file));
    memset(&best, 0, sizeof(best));
    best.dist = NO_MATCH_DIST;
    if ((NULL == p_path) || (NULL == p_query) || (NULL == p_match) ||
        (NULL == p_query->p_live))
    {
        goto cleanup;
    }
    memset(p_match, 0, sizeof(*p_match));
    if (0 != fileio_read_all(p_path, &file))
    {
        result = VAULT_NO_MATCH;
        goto cleanup;
    }
    if ((file.len < MAGIC_BYTES) ||
        (0 != memcmp(file.p_data, MAGIC, MAGIC_BYTES)))
    {
        goto cleanup;
    }
    pos = MAGIC_BYTES;
    while (pos < file.len)
    {
        if (0 != parse_record(&file, pos, &rec))
        {
            goto cleanup;
        }
        rec.dist = record_distance(&rec, p_query->p_live);
        if ((rec.dist <= p_query->max_dist) && (rec.dist < best.dist))
        {
            best = rec;
        }
        pos += rec.rec_len;
    }
    if (NO_MATCH_DIST == best.dist)
    {
        result = VAULT_NO_MATCH;
        goto cleanup;
    }
    p_match->blob.p_data = (uint8_t *)malloc(best.blob_len);
    if (NULL == p_match->blob.p_data)
    {
        goto cleanup;
    }
    memcpy(p_match->phash, best.p_primary, PHASH_BYTES);
    memcpy(p_match->blob.p_data, file.p_data + best.blob_off, best.blob_len);
    p_match->blob.cap = best.blob_len;
    p_match->blob.len = best.blob_len;
    p_match->dist     = best.dist;
    result            = VAULT_FOUND;

cleanup:
    if (NULL != file.p_data)
    {
        free(file.p_data);
    }

    return result;
}

void vault_match_free(vault_match_t * p_match)
{
    if ((NULL != p_match) && (NULL != p_match->blob.p_data))
    {
        free(p_match->blob.p_data);
        memset(&p_match->blob, 0, sizeof(p_match->blob));
    }
}
