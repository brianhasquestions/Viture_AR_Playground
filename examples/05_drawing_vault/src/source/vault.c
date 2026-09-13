/**********************************************************************
 * @file    vault.c
 * @brief   Store and recall drawing-bound, device-bound messages.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * On-disk format (all integers big-endian):
 *   magic  "VLT1"                       (4 bytes)
 *   repeated records:
 *     fingerprint                       (PHASH_BYTES)
 *     msg_len                           (2 bytes)
 *     tag                               (4 bytes)
 *     ciphertext                        (msg_len bytes)
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "vault.h"

#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG_BYTES       (4)
#define KM_BYTES        (VAULT_GLASSES_HASH_BYTES + PHASH_BYTES)
static const uint8_t MAGIC[4] = { 'V', 'L', 'T', '1' };

/**********************************************************************
 * @brief  key_material = glasses_hash(32) || fingerprint(8).
 **********************************************************************/
static void key_material(const uint8_t * p_glasses,
                         const uint8_t * p_phash, uint8_t * p_out)
{
    memcpy(p_out, p_glasses, VAULT_GLASSES_HASH_BYTES);
    memcpy(p_out + VAULT_GLASSES_HASH_BYTES, p_phash, PHASH_BYTES);
}

static void keystream_xor(const uint8_t * p_km, uint8_t * p_buf,
                          size_t len)
{
    uint32_t counter = 0;
    size_t   done    = 0;

    while (done < len)
    {
        sha256_ctx_t ctx;
        uint8_t      block[SHA256_DIGEST_BYTES];
        uint8_t      ctr[4];
        size_t       take = 0;
        size_t       i    = 0;

        ctr[0] = (uint8_t)((counter >> 24) & 0xFFU);
        ctr[1] = (uint8_t)((counter >> 16) & 0xFFU);
        ctr[2] = (uint8_t)((counter >> 8) & 0xFFU);
        ctr[3] = (uint8_t)(counter & 0xFFU);

        sha256_init(&ctx);
        sha256_update(&ctx, p_km, KM_BYTES);
        sha256_update(&ctx, ctr, sizeof(ctr));
        sha256_final(&ctx, block);

        take = len - done;
        if (take > SHA256_DIGEST_BYTES)
        {
            take = SHA256_DIGEST_BYTES;
        }
        for (i = 0; i < take; i++)
        {
            p_buf[done + i] ^= block[i];
        }
        done += take;
        counter++;
    }
}

static void compute_tag(const uint8_t * p_km, const uint8_t * p_msg,
                        size_t msg_len, uint8_t p_tag[TAG_BYTES])
{
    sha256_ctx_t ctx;
    uint8_t      digest[SHA256_DIGEST_BYTES];

    sha256_init(&ctx);
    sha256_update(&ctx, p_km, KM_BYTES);
    sha256_update(&ctx, p_msg, msg_len);
    sha256_final(&ctx, digest);
    memcpy(p_tag, digest, TAG_BYTES);
}

static uint16_t rd16(const uint8_t * p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

int vault_enroll(const char * p_path, const uint8_t p_phash[PHASH_BYTES],
                 const uint8_t p_glasses[VAULT_GLASSES_HASH_BYTES],
                 const uint8_t * p_msg, size_t msg_len)
{
    uint8_t   km[KM_BYTES];
    uint8_t   tag[TAG_BYTES];
    uint8_t   hdr[2];
    uint8_t * p_ct    = NULL;
    FILE *    p_f     = NULL;
    long      sz      = 0;
    int       result  = -1;

    if ((NULL == p_path) || (NULL == p_phash) || (NULL == p_glasses) ||
        (NULL == p_msg) || (msg_len > 0xFFFFU))
    {
        return -1;
    }

    key_material(p_glasses, p_phash, km);
    compute_tag(km, p_msg, msg_len, tag);

    p_ct = (uint8_t *)malloc(msg_len);
    if (NULL == p_ct)
    {
        return -1;
    }
    memcpy(p_ct, p_msg, msg_len);
    keystream_xor(km, p_ct, msg_len);

    /* Open for read/write, create if absent; write magic if new. */
    p_f = fopen(p_path, "r+b");
    if (NULL == p_f)
    {
        p_f = fopen(p_path, "w+b");
        if (NULL == p_f)
        {
            goto cleanup;
        }
    }
    (void)fseek(p_f, 0, SEEK_END);
    sz = ftell(p_f);
    if (sz <= 0)
    {
        if (fwrite(MAGIC, 1U, sizeof(MAGIC), p_f) != sizeof(MAGIC))
        {
            goto cleanup;
        }
    }

    hdr[0] = (uint8_t)((msg_len >> 8) & 0xFFU);
    hdr[1] = (uint8_t)(msg_len & 0xFFU);
    if ((fwrite(p_phash, 1U, PHASH_BYTES, p_f) != PHASH_BYTES) ||
        (fwrite(hdr, 1U, sizeof(hdr), p_f) != sizeof(hdr)) ||
        (fwrite(tag, 1U, TAG_BYTES, p_f) != TAG_BYTES) ||
        (fwrite(p_ct, 1U, msg_len, p_f) != msg_len))
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_f)   { (void)fclose(p_f); }
    if (NULL != p_ct)  { free(p_ct); }
    return result;
}

int vault_recall(const char * p_path, const uint8_t p_phash[PHASH_BYTES],
                 const uint8_t p_glasses[VAULT_GLASSES_HASH_BYTES],
                 int max_dist, uint8_t * p_msg, size_t msg_cap,
                 size_t * p_msg_len, int * p_dist)
{
    FILE *    p_f     = NULL;
    uint8_t * p_all   = NULL;
    long      sz      = 0;
    size_t    pos     = 0;
    int       best_d  = 65;
    long      best_off = -1;
    size_t    best_ml = 0;
    int       result  = VAULT_NO_MATCH;

    if ((NULL == p_path) || (NULL == p_phash) || (NULL == p_glasses) ||
        (NULL == p_msg) || (NULL == p_msg_len))
    {
        return VAULT_ERROR;
    }

    p_f = fopen(p_path, "rb");
    if (NULL == p_f)
    {
        return VAULT_NO_MATCH;
    }
    (void)fseek(p_f, 0, SEEK_END);
    sz = ftell(p_f);
    (void)fseek(p_f, 0, SEEK_SET);
    if (sz < (long)sizeof(MAGIC))
    {
        (void)fclose(p_f);
        return VAULT_NO_MATCH;
    }
    p_all = (uint8_t *)malloc((size_t)sz);
    if ((NULL == p_all) ||
        (fread(p_all, 1U, (size_t)sz, p_f) != (size_t)sz))
    {
        (void)fclose(p_f);
        free(p_all);
        return VAULT_ERROR;
    }
    (void)fclose(p_f);

    if (0 != memcmp(p_all, MAGIC, sizeof(MAGIC)))
    {
        free(p_all);
        return VAULT_ERROR;
    }
    pos = sizeof(MAGIC);

    /* Scan records; remember the closest fingerprint within threshold. */
    while ((pos + PHASH_BYTES + 2U + TAG_BYTES) <= (size_t)sz)
    {
        const uint8_t * p_rec_hash = &p_all[pos];
        size_t          ml = rd16(&p_all[pos + PHASH_BYTES]);
        size_t          rec_len = PHASH_BYTES + 2U + TAG_BYTES + ml;
        int             d = 0;

        if ((pos + rec_len) > (size_t)sz)
        {
            break;   /* Truncated record. */
        }
        d = phash_distance(p_phash, p_rec_hash);
        if ((d <= max_dist) && (d < best_d))
        {
            best_d   = d;
            best_off = (long)pos;
            best_ml  = ml;
        }
        pos += rec_len;
    }

    if (best_off < 0)
    {
        free(p_all);
        return VAULT_NO_MATCH;
    }
    if (NULL != p_dist)
    {
        *p_dist = best_d;
    }

    /* Decrypt the best match with the live glasses hash + stored hash. */
    {
        const uint8_t * p_rec  = &p_all[best_off];
        const uint8_t * p_rhash = p_rec;
        const uint8_t * p_tag  = p_rec + PHASH_BYTES + 2;
        const uint8_t * p_ct   = p_rec + PHASH_BYTES + 2 + TAG_BYTES;
        uint8_t         km[KM_BYTES];
        uint8_t         tag_calc[TAG_BYTES];

        if (best_ml > msg_cap)
        {
            result = VAULT_ERROR;
        }
        else
        {
            key_material(p_glasses, p_rhash, km);
            memcpy(p_msg, p_ct, best_ml);
            keystream_xor(km, p_msg, best_ml);
            compute_tag(km, p_msg, best_ml, tag_calc);

            if (0 == memcmp(tag_calc, p_tag, TAG_BYTES))
            {
                *p_msg_len = best_ml;
                result = VAULT_OPENED;
            }
            else
            {
                memset(p_msg, 0, best_ml);
                result = VAULT_LOCKED;
            }
        }
    }

    free(p_all);
    return result;
}
