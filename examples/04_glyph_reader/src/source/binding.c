/**********************************************************************
 * @file    binding.c
 * @brief   Bind a message to (a device, a glyph): seal and open.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "binding.h"

#include "sha256.h"

#include <string.h>

static const uint8_t MAGIC[BINDING_MAGIC_BYTES] = { 'G', 'L', 'B', '1' };

/**********************************************************************
 * @brief  Assemble the key material: sn_hash(32) || payload(3).
 **********************************************************************/
static void key_material(const uint8_t * p_sn_hash,
                         const uint8_t p_payload[GLYPH_PAYLOAD_BYTES],
                         uint8_t * p_out)
{
    memcpy(p_out, p_sn_hash, BINDING_SN_HASH_BYTES);
    memcpy(p_out + BINDING_SN_HASH_BYTES, p_payload, GLYPH_PAYLOAD_BYTES);
}

/**********************************************************************
 * @brief  XOR a SHA-256 counter-mode keystream over a buffer in place.
 **********************************************************************/
static void keystream_xor(const uint8_t * p_km, size_t km_len,
                          uint8_t * p_buf, size_t len)
{
    uint32_t counter = 0;
    size_t   done    = 0;

    while (done < len)
    {
        sha256_ctx_t ctx;
        uint8_t      block[SHA256_DIGEST_BYTES];
        uint8_t      ctr_be[4];
        size_t       take = 0;
        size_t       i    = 0;

        ctr_be[0] = (uint8_t)((counter >> 24) & 0xFFU);
        ctr_be[1] = (uint8_t)((counter >> 16) & 0xFFU);
        ctr_be[2] = (uint8_t)((counter >> 8) & 0xFFU);
        ctr_be[3] = (uint8_t)(counter & 0xFFU);

        sha256_init(&ctx);
        sha256_update(&ctx, p_km, km_len);
        sha256_update(&ctx, ctr_be, sizeof(ctr_be));
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

/**********************************************************************
 * @brief  tag = SHA256(key_material || plaintext)[0:4].
 **********************************************************************/
static void compute_tag(const uint8_t * p_km, size_t km_len,
                        const uint8_t * p_msg, size_t msg_len,
                        uint8_t p_tag[BINDING_TAG_BYTES])
{
    sha256_ctx_t ctx;
    uint8_t      digest[SHA256_DIGEST_BYTES];

    sha256_init(&ctx);
    sha256_update(&ctx, p_km, km_len);
    sha256_update(&ctx, p_msg, msg_len);
    sha256_final(&ctx, digest);
    memcpy(p_tag, digest, BINDING_TAG_BYTES);
}

int binding_seal(const uint8_t * p_sn_hash,
                 const uint8_t p_payload[GLYPH_PAYLOAD_BYTES],
                 const uint8_t * p_msg, size_t msg_len,
                 uint8_t * p_blob, size_t blob_cap, size_t * p_blob_len)
{
    uint8_t km[BINDING_SN_HASH_BYTES + GLYPH_PAYLOAD_BYTES];
    uint8_t tag[BINDING_TAG_BYTES];
    size_t  need = BINDING_HEADER_BYTES + msg_len;

    if ((NULL == p_sn_hash) || (NULL == p_payload) ||
        (NULL == p_msg) || (NULL == p_blob) || (NULL == p_blob_len) ||
        (msg_len > 0xFFFFU) || (need > blob_cap))
    {
        return -1;
    }

    key_material(p_sn_hash, p_payload, km);
    compute_tag(km, sizeof(km), p_msg, msg_len, tag);

    memcpy(p_blob, MAGIC, BINDING_MAGIC_BYTES);
    p_blob[BINDING_MAGIC_BYTES]     = (uint8_t)((msg_len >> 8) & 0xFFU);
    p_blob[BINDING_MAGIC_BYTES + 1] = (uint8_t)(msg_len & 0xFFU);
    memcpy(p_blob + BINDING_MAGIC_BYTES + 2, tag, BINDING_TAG_BYTES);
    memcpy(p_blob + BINDING_HEADER_BYTES, p_msg, msg_len);

    /* Encrypt the ciphertext region in place. */
    keystream_xor(km, sizeof(km), p_blob + BINDING_HEADER_BYTES, msg_len);

    *p_blob_len = need;
    return 0;
}

int binding_open(const uint8_t * p_sn_hash,
                 const uint8_t p_payload[GLYPH_PAYLOAD_BYTES],
                 const uint8_t * p_blob, size_t blob_len,
                 uint8_t * p_msg, size_t msg_cap, size_t * p_msg_len)
{
    uint8_t km[BINDING_SN_HASH_BYTES + GLYPH_PAYLOAD_BYTES];
    uint8_t tag_stored[BINDING_TAG_BYTES];
    uint8_t tag_calc[BINDING_TAG_BYTES];
    size_t  msg_len = 0;

    if ((NULL == p_sn_hash) || (NULL == p_payload) ||
        (NULL == p_blob) || (NULL == p_msg) || (NULL == p_msg_len) ||
        (blob_len < BINDING_HEADER_BYTES))
    {
        return -1;
    }
    if (0 != memcmp(p_blob, MAGIC, BINDING_MAGIC_BYTES))
    {
        return -1;
    }

    msg_len = ((size_t)p_blob[BINDING_MAGIC_BYTES] << 8) |
              (size_t)p_blob[BINDING_MAGIC_BYTES + 1];
    if (((BINDING_HEADER_BYTES + msg_len) > blob_len) ||
        (msg_len > msg_cap))
    {
        return -1;
    }
    memcpy(tag_stored, p_blob + BINDING_MAGIC_BYTES + 2, BINDING_TAG_BYTES);

    key_material(p_sn_hash, p_payload, km);

    /* Decrypt into the caller's buffer. */
    memcpy(p_msg, p_blob + BINDING_HEADER_BYTES, msg_len);
    keystream_xor(km, sizeof(km), p_msg, msg_len);

    /* Verify the key-dependent tag over the recovered plaintext. */
    compute_tag(km, sizeof(km), p_msg, msg_len, tag_calc);
    if (0 != memcmp(tag_calc, tag_stored, BINDING_TAG_BYTES))
    {
        memset(p_msg, 0, msg_len);   /* Wrong device or glyph. */
        return -1;
    }

    *p_msg_len = msg_len;
    return 0;
}
