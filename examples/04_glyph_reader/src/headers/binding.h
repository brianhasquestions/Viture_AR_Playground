/**********************************************************************
 * @file    binding.h
 * @brief   Bind a message to (a device, a glyph): seal and open.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * The message is encrypted under a key derived from BOTH the device's
 * SN hash and the glyph payload:
 *
 *   key_material = sn_hash(32) || payload(3)
 *   keystream    = SHA256(key_material || counter), counter = 0,1,2,...
 *   ciphertext   = plaintext XOR keystream
 *   tag          = SHA256(key_material || plaintext)[0:4]
 *
 * Opening recomputes the key from the live device hash and the decoded
 * glyph; only the intended glasses seeing the intended glyph reproduce
 * the key, so only then does the tag verify and the message appear.
 *
 * This is a teaching construction, not a vetted protocol: a raw SHA-256
 * keystream has no nonce and must never be reused across messages, and
 * the 32-bit tag is short. Do not use it to protect anything real.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef BINDING_H
#define BINDING_H

#include <stddef.h>
#include <stdint.h>

#include "glyph.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define BINDING_SN_HASH_BYTES   (32)
#define BINDING_MAGIC_BYTES     (4)
#define BINDING_TAG_BYTES       (4)
/* magic(4) + msg_len(2) + tag(4) + ciphertext. */
#define BINDING_HEADER_BYTES    (BINDING_MAGIC_BYTES + 2 + BINDING_TAG_BYTES)

/**********************************************************************
 * @brief  Seal a message into a self-describing blob.
 *
 * @param[in]  p_sn_hash  32-byte device SN hash to bind to.
 * @param[in]  p_payload  Glyph payload to bind to.
 * @param[in]  p_msg      Plaintext message.
 * @param[in]  msg_len    Message length in bytes.
 * @param[out] p_blob     Output buffer.
 * @param[in]  blob_cap   Capacity of p_blob.
 * @param[out] p_blob_len Receives the blob length written.
 *
 * @return  0 on success, -1 on bad args or insufficient capacity.
 **********************************************************************/
int binding_seal(const uint8_t * p_sn_hash,
                 const uint8_t p_payload[GLYPH_PAYLOAD_BYTES],
                 const uint8_t * p_msg, size_t msg_len,
                 uint8_t * p_blob, size_t blob_cap, size_t * p_blob_len);

/**********************************************************************
 * @brief  Open a sealed blob with a live device hash and glyph payload.
 *
 * @param[in]  p_sn_hash  32-byte live device SN hash.
 * @param[in]  p_payload  Decoded glyph payload.
 * @param[in]  p_blob     Sealed blob.
 * @param[in]  blob_len   Blob length.
 * @param[out] p_msg      Receives the recovered plaintext.
 * @param[in]  msg_cap    Capacity of p_msg.
 * @param[out] p_msg_len  Receives the plaintext length.
 *
 * @return  0 if the tag verifies (message recovered), -1 otherwise.
 **********************************************************************/
int binding_open(const uint8_t * p_sn_hash,
                 const uint8_t p_payload[GLYPH_PAYLOAD_BYTES],
                 const uint8_t * p_blob, size_t blob_len,
                 uint8_t * p_msg, size_t msg_cap, size_t * p_msg_len);

#ifdef __cplusplus
}
#endif

#endif /* BINDING_H */
