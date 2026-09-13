/**********************************************************************
 * @file    sha256.c
 * @brief   Self-contained SHA-256 (FIPS 180-4), pure C.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "sha256.h"

#include <string.h>

/* First 32 bits of the fractional parts of the cube roots of the first
 * 64 primes (FIPS 180-4, section 4.2.2). */
static const uint32_t K[64] =
{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static uint32_t rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32U - n));
}

/**********************************************************************
 * @brief  Compress one 64-byte block into the running state.
 **********************************************************************/
static void sha256_block(uint32_t state[8], const uint8_t p_block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int      i = 0;

    for (i = 0; i < 16; i++)
    {
        w[i] = ((uint32_t)p_block[(i * 4) + 0] << 24) |
               ((uint32_t)p_block[(i * 4) + 1] << 16) |
               ((uint32_t)p_block[(i * 4) + 2] << 8) |
               ((uint32_t)p_block[(i * 4) + 3]);
    }
    for (i = 16; i < 64; i++)
    {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                            (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                            (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; i++)
    {
        const uint32_t s1    = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch    = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + s1 + ch + K[i] + w[i];
        const uint32_t s0    = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_init(sha256_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        return;
    }
    p_ctx->state[0] = 0x6a09e667U;
    p_ctx->state[1] = 0xbb67ae85U;
    p_ctx->state[2] = 0x3c6ef372U;
    p_ctx->state[3] = 0xa54ff53aU;
    p_ctx->state[4] = 0x510e527fU;
    p_ctx->state[5] = 0x9b05688cU;
    p_ctx->state[6] = 0x1f83d9abU;
    p_ctx->state[7] = 0x5be0cd19U;
    p_ctx->bit_len    = 0;
    p_ctx->buffer_len = 0;
}

void sha256_update(sha256_ctx_t * p_ctx, const uint8_t * p_data,
                   size_t len)
{
    size_t i = 0;

    if ((NULL == p_ctx) || ((NULL == p_data) && (0U != len)))
    {
        return;
    }

    for (i = 0; i < len; i++)
    {
        p_ctx->buffer[p_ctx->buffer_len] = p_data[i];
        p_ctx->buffer_len++;
        if (SHA256_BLOCK_BYTES == p_ctx->buffer_len)
        {
            sha256_block(p_ctx->state, p_ctx->buffer);
            p_ctx->bit_len   += 512U;
            p_ctx->buffer_len = 0;
        }
    }
}

void sha256_final(sha256_ctx_t * p_ctx, uint8_t * p_digest)
{
    uint64_t total_bits = 0;
    size_t   i          = 0;

    if ((NULL == p_ctx) || (NULL == p_digest))
    {
        return;
    }

    total_bits = p_ctx->bit_len + ((uint64_t)p_ctx->buffer_len * 8U);

    /* Append the 0x80 terminator. */
    p_ctx->buffer[p_ctx->buffer_len] = 0x80U;
    p_ctx->buffer_len++;

    /* If there is no room for the 8-byte length, pad and flush. */
    if (p_ctx->buffer_len > (SHA256_BLOCK_BYTES - 8U))
    {
        while (p_ctx->buffer_len < SHA256_BLOCK_BYTES)
        {
            p_ctx->buffer[p_ctx->buffer_len] = 0x00U;
            p_ctx->buffer_len++;
        }
        sha256_block(p_ctx->state, p_ctx->buffer);
        p_ctx->buffer_len = 0;
    }

    while (p_ctx->buffer_len < (SHA256_BLOCK_BYTES - 8U))
    {
        p_ctx->buffer[p_ctx->buffer_len] = 0x00U;
        p_ctx->buffer_len++;
    }

    /* Big-endian 64-bit total length. */
    for (i = 0; i < 8U; i++)
    {
        const uint32_t shift = (uint32_t)(56U - (i * 8U));
        p_ctx->buffer[56U + i] = (uint8_t)((total_bits >> shift) & 0xFFU);
    }
    sha256_block(p_ctx->state, p_ctx->buffer);

    for (i = 0; i < 8U; i++)
    {
        p_digest[(i * 4) + 0] = (uint8_t)((p_ctx->state[i] >> 24) & 0xFFU);
        p_digest[(i * 4) + 1] = (uint8_t)((p_ctx->state[i] >> 16) & 0xFFU);
        p_digest[(i * 4) + 2] = (uint8_t)((p_ctx->state[i] >> 8) & 0xFFU);
        p_digest[(i * 4) + 3] = (uint8_t)(p_ctx->state[i] & 0xFFU);
    }
}

void sha256(const uint8_t * p_data, size_t len, uint8_t * p_digest)
{
    sha256_ctx_t ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, p_data, len);
    sha256_final(&ctx, p_digest);
}
