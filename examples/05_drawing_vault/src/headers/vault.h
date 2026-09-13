/**********************************************************************
 * @file    vault.h
 * @brief   Store and recall drawing-bound, device-bound messages.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * A vault file holds records: { fingerprint(phash), sealed message }.
 *
 *   enroll: append a record whose message is encrypted under
 *           key = SHA256(glasses_hash || fingerprint).
 *   recall: fingerprint the current view, pick the record whose stored
 *           fingerprint is closest (Hamming) within a threshold, then
 *           decrypt it with the live glasses hash. The drawing selects
 *           the record; the glasses hash is the gate that opens it.
 *
 * The stored fingerprint (exact) is what feeds the key, so a fuzzy live
 * match still decrypts correctly. Teaching-grade crypto (raw SHA-256
 * keystream, 32-bit tag); do not protect anything real with it.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef VAULT_H
#define VAULT_H

#include <stddef.h>
#include <stdint.h>

#include "phash.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define VAULT_GLASSES_HASH_BYTES    (32)

/* Recall outcomes. */
#define VAULT_OPENED         (0)   /* Matched and decrypted.          */
#define VAULT_LOCKED         (1)   /* Matched, but wrong glasses.     */
#define VAULT_NO_MATCH       (2)   /* No drawing close enough.        */
#define VAULT_ERROR          (-1)

/**********************************************************************
 * @brief  Enroll a message against a drawing fingerprint and device.
 *
 * @param[in]  p_path        Vault file (created if absent).
 * @param[in]  p_phash       Drawing fingerprint (PHASH_BYTES).
 * @param[in]  p_glasses     Device glasses hash (32 bytes).
 * @param[in]  p_msg         Message plaintext.
 * @param[in]  msg_len       Message length (<= 65535).
 *
 * @return  0 on success, -1 on failure.
 **********************************************************************/
int vault_enroll(const char * p_path, const uint8_t p_phash[PHASH_BYTES],
                 const uint8_t p_glasses[VAULT_GLASSES_HASH_BYTES],
                 const uint8_t * p_msg, size_t msg_len);

/**********************************************************************
 * @brief  Recall the message for the drawing currently in view.
 *
 * @param[in]  p_path        Vault file.
 * @param[in]  p_phash       Fingerprint of the current view.
 * @param[in]  p_glasses     Live device glasses hash.
 * @param[in]  max_dist      Max Hamming distance to accept as a match.
 * @param[out] p_msg         Receives the recovered plaintext.
 * @param[in]  msg_cap       Capacity of p_msg.
 * @param[out] p_msg_len     Receives plaintext length (on VAULT_OPENED).
 * @param[out] p_dist        Receives the matched distance (may be NULL).
 *
 * @return  VAULT_OPENED, VAULT_LOCKED, VAULT_NO_MATCH, or VAULT_ERROR.
 **********************************************************************/
int vault_recall(const char * p_path, const uint8_t p_phash[PHASH_BYTES],
                 const uint8_t p_glasses[VAULT_GLASSES_HASH_BYTES],
                 int max_dist, uint8_t * p_msg, size_t msg_cap,
                 size_t * p_msg_len, int * p_dist);

#ifdef __cplusplus
}
#endif

#endif /* VAULT_H */
