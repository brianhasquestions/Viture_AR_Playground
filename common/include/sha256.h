/**********************************************************************
 * @file    sha256.h
 * @brief   Self-contained SHA-256 (FIPS 180-4), pure C.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * No external dependency. Used here to derive the message-decryption key
 * from the glyph payload and the device SN hash, and to checksum the
 * recovered plaintext. Not hardened against side channels; it is a
 * textbook implementation for an offline demo.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SHA256_DIGEST_BYTES     (32)
#define SHA256_BLOCK_BYTES      (64)

/**********************************************************************
 * @brief  Streaming SHA-256 state.
 **********************************************************************/
typedef struct
{
    uint32_t state[8];
    uint64_t bit_len;                       /* Total message bits.     */
    uint8_t  buffer[SHA256_BLOCK_BYTES];    /* Partial block.          */
    size_t   buffer_len;                    /* Bytes held in buffer.   */
} sha256_ctx_t;

/**********************************************************************
 * @brief  Begin a new digest.
 **********************************************************************/
void sha256_init(sha256_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Absorb len bytes of message.
 **********************************************************************/
void sha256_update(sha256_ctx_t * p_ctx, const uint8_t * p_data,
                   size_t len);

/**********************************************************************
 * @brief  Finish and emit the 32-byte digest.
 *
 * @param[in,out] p_ctx    Context; consumed (do not reuse without init).
 * @param[out]    p_digest Buffer of at least SHA256_DIGEST_BYTES.
 **********************************************************************/
void sha256_final(sha256_ctx_t * p_ctx, uint8_t * p_digest);

/**********************************************************************
 * @brief  One-shot convenience: digest a single buffer.
 **********************************************************************/
void sha256(const uint8_t * p_data, size_t len, uint8_t * p_digest);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
