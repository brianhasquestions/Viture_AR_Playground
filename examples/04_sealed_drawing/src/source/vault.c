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
#define NO_MATCH        (-1)

static const uint8_t MAGIC[MAGIC_BYTES] = { 'S', 'D', 'V', '4' };

typedef struct
{
    size_t start;
    size_t views_off;
    int    view_count;
    size_t blob_off;
    size_t blob_len;
    size_t rec_len;
} record_t;

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

static int write_view(FILE * p_file, const fingerprint_t * p_view)
{
    int        result = -1;
    uint8_t *  p_buf  = NULL;
    byte_buf_t buf;
    uint8_t    len[LEN_BYTES];

    p_buf = (uint8_t *)malloc(FINGERPRINT_MAX_BYTES);
    if (NULL == p_buf)
    {
        goto cleanup;
    }
    buf.p_data = p_buf;
    buf.cap    = FINGERPRINT_MAX_BYTES;
    buf.len    = 0;
    if (0 != fingerprint_write(p_view, &buf))
    {
        goto cleanup;
    }
    write_u32(len, (uint32_t)buf.len);
    if (fwrite(len, 1U, LEN_BYTES, p_file) != LEN_BYTES)
    {
        goto cleanup;
    }
    if (fwrite(buf.p_data, 1U, buf.len, p_file) != buf.len)
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_buf)
    {
        free(p_buf);
    }

    return result;
}

int vault_store(const char * p_path, const vault_views_t * p_views,
                byte_span_t blob)
{
    int     result = -1;
    int     i      = 0;
    FILE *  p_file = NULL;
    long    size   = 0;
    uint8_t count  = 0;
    uint8_t len[LEN_BYTES];

    if ((NULL == p_path) || (NULL == p_views) || (NULL == p_views->p_views) ||
        (p_views->count < 1) || (p_views->count > VAULT_MAX_VIEWS) ||
        (NULL == blob.p_data) || (blob.len > VAULT_MAX_BLOB))
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
    count = (uint8_t)p_views->count;
    if (fwrite(p_views->p_views[0].hashes.primary, 1U, PHASH_BYTES,
               p_file) != PHASH_BYTES)
    {
        goto cleanup;
    }
    if (fwrite(&count, 1U, COUNT_BYTES, p_file) != COUNT_BYTES)
    {
        goto cleanup;
    }
    for (i = 0; i < p_views->count; i++)
    {
        if (0 != write_view(p_file, &p_views->p_views[i]))
        {
            goto cleanup;
        }
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

static int parse_record(const byte_buf_t * p_file, size_t pos,
                        record_t * p_rec)
{
    int    result = -1;
    size_t at     = pos;
    int    v      = 0;

    if ((pos + HEAD_BYTES) > p_file->len)
    {
        goto cleanup;
    }
    p_rec->start      = pos;
    p_rec->view_count = p_file->p_data[pos + PHASH_BYTES];
    at               += HEAD_BYTES;
    p_rec->views_off  = at;
    if ((p_rec->view_count < 1) || (p_rec->view_count > VAULT_MAX_VIEWS))
    {
        goto cleanup;
    }
    for (v = 0; v < p_rec->view_count; v++)
    {
        size_t vlen = 0;

        if ((at + LEN_BYTES) > p_file->len)
        {
            goto cleanup;
        }
        vlen = read_u32(p_file->p_data + at);
        at  += LEN_BYTES;
        if ((vlen > FINGERPRINT_MAX_BYTES) || ((at + vlen) > p_file->len))
        {
            goto cleanup;
        }
        at += vlen;
    }
    if ((at + LEN_BYTES) > p_file->len)
    {
        goto cleanup;
    }
    p_rec->blob_len = read_u32(p_file->p_data + at);
    p_rec->blob_off = at + LEN_BYTES;
    if ((p_rec->blob_len > VAULT_MAX_BLOB) ||
        ((p_rec->blob_off + p_rec->blob_len) > p_file->len))
    {
        goto cleanup;
    }
    p_rec->rec_len = (p_rec->blob_off + p_rec->blob_len) - pos;
    result         = 0;

cleanup:

    return result;
}

typedef struct
{
    int matched;
    int inliers;
    int dist;
} best_view_t;

typedef struct
{
    const byte_buf_t *    p_file;
    const vault_query_t * p_query;
} scan_ctx_t;

static void score_record(const scan_ctx_t * p_ctx, const record_t * p_rec,
                         best_view_t * p_best)
{
    const byte_buf_t *    p_file  = p_ctx->p_file;
    const vault_query_t * p_query = p_ctx->p_query;
    size_t                at      = p_rec->views_off;
    int             v      = 0;
    fingerprint_t * p_view = NULL;

    memset(p_best, 0, sizeof(*p_best));
    p_best->dist = PHASH_MATCH_MAX;
    p_view = (fingerprint_t *)calloc(1U, sizeof(*p_view));
    if (NULL == p_view)
    {
        goto cleanup;
    }
    for (v = 0; v < p_rec->view_count; v++)
    {
        size_t              vlen = read_u32(p_file->p_data + at);
        byte_span_t         bytes;
        fingerprint_score_t sc;

        at         += LEN_BYTES;
        bytes.p_data = p_file->p_data + at;
        bytes.len    = vlen;
        at          += vlen;
        if (0 != fingerprint_read(bytes, p_view))
        {
            continue;
        }
        sc = fingerprint_compare(p_query->p_live, p_view, p_query->max_dist);
        if (sc.inliers > p_best->inliers)
        {
            p_best->inliers = sc.inliers;
        }
        if (sc.hash_score < p_best->dist)
        {
            p_best->dist = sc.hash_score;
        }
        if (0 != sc.matched)
        {
            p_best->matched = 1;
        }
    }

cleanup:
    if (NULL != p_view)
    {
        free(p_view);
    }
}

static int better(const best_view_t * p_a, const best_view_t * p_b)
{
    int result = 0;

    if (p_a->inliers != p_b->inliers)
    {
        result = (p_a->inliers > p_b->inliers) ? 1 : 0;
    }
    else
    {
        result = (p_a->dist < p_b->dist) ? 1 : 0;
    }

    return result;
}

int vault_find(const char * p_path, const vault_query_t * p_query,
               vault_match_t * p_match)
{
    int         result = VAULT_ERROR;
    size_t      pos    = 0;
    int         index  = 0;
    int         best_i = NO_MATCH;
    record_t    best;
    record_t    rec;
    best_view_t best_sc;
    best_view_t sc;
    byte_buf_t  file;
    scan_ctx_t  ctx;

    memset(&file, 0, sizeof(file));
    memset(&best, 0, sizeof(best));
    memset(&best_sc, 0, sizeof(best_sc));
    best_sc.dist = PHASH_MATCH_MAX;
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
        ctx.p_file  = &file;
        ctx.p_query = p_query;
        score_record(&ctx, &rec, &sc);
        if ((0 != sc.matched) && ((NO_MATCH == best_i) ||
                                  (0 != better(&sc, &best_sc))))
        {
            best    = rec;
            best_sc = sc;
            best_i  = index;
        }
        pos += rec.rec_len;
        index++;
    }
    if (NO_MATCH == best_i)
    {
        result = VAULT_NO_MATCH;
        goto cleanup;
    }
    p_match->blob.p_data = (uint8_t *)malloc(best.blob_len);
    if (NULL == p_match->blob.p_data)
    {
        goto cleanup;
    }
    memcpy(p_match->phash, file.p_data + best.start, PHASH_BYTES);
    memcpy(p_match->blob.p_data, file.p_data + best.blob_off, best.blob_len);
    p_match->blob.cap = best.blob_len;
    p_match->blob.len = best.blob_len;
    p_match->dist     = best_sc.dist;
    p_match->inliers  = best_sc.inliers;
    p_match->index    = best_i;
    result            = VAULT_FOUND;

cleanup:
    if (NULL != file.p_data)
    {
        free(file.p_data);
    }

    return result;
}

int vault_list(const char * p_path, vault_entry_t * p_entries, int cap)
{
    int        result = -1;
    int        index  = 0;
    size_t     pos    = 0;
    record_t   rec;
    byte_buf_t file;

    memset(&file, 0, sizeof(file));
    if (NULL == p_path)
    {
        goto cleanup;
    }
    if (0 != fileio_read_all(p_path, &file))
    {
        result = 0;
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
        if ((NULL != p_entries) && (index < cap))
        {
            p_entries[index].index      = index;
            p_entries[index].views      = rec.view_count;
            p_entries[index].blob_bytes = rec.blob_len;
        }
        pos += rec.rec_len;
        index++;
    }
    result = index;

cleanup:
    if (NULL != file.p_data)
    {
        free(file.p_data);
    }

    return result;
}

int vault_forget(const char * p_path, int index)
{
    int        result = -1;
    int        at     = 0;
    size_t     pos    = 0;
    FILE *     p_file = NULL;
    record_t   rec;
    byte_buf_t file;

    memset(&file, 0, sizeof(file));
    if ((NULL == p_path) || (index < 0) ||
        (0 != fileio_read_all(p_path, &file)))
    {
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
        if (at == index)
        {
            break;
        }
        pos += rec.rec_len;
        at++;
    }
    if (pos >= file.len)
    {
        goto cleanup;
    }
    p_file = fopen(p_path, "wb");
    if (NULL == p_file)
    {
        goto cleanup;
    }
    if (fwrite(file.p_data, 1U, pos, p_file) != pos)
    {
        goto cleanup;
    }
    if (fwrite(file.p_data + pos + rec.rec_len, 1U,
               file.len - pos - rec.rec_len, p_file) !=
        (file.len - pos - rec.rec_len))
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_file)
    {
        (void)fclose(p_file);
    }
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
