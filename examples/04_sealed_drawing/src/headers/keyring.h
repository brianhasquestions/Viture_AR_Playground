/*
Summary: The glasses' asymmetric identity.
         An X25519 key pair is derived deterministically from the
         hardware serial hash (plus an optional PIN), so any host that
         has the glasses plugged in can rebuild the same private key
         and nothing has to be stored. Messages are sealed to the
         public half; only the matching glasses can rebuild the private
         half and open them.

         Derivation:
           seed = HKDF-SHA256(ikm  = sn_hash || pin,
                              salt = KEYRING_SALT,
                              info = KEYRING_INFO)
           priv = seed (X25519 clamps internally), pub = X25519(priv, 9)
*/

#ifndef KEYRING_H
#define KEYRING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define KEYRING_KEY_BYTES       (32)
#define KEYRING_SN_HASH_BYTES   (32)
#define KEYRING_PIN_MAX_CHARS   (256)

/*
Summary: One X25519 identity: private seed and matching public key.
Inputs:  None.
Outputs: None. Wipe with keyring_wipe() when no longer needed.
*/
typedef struct
{
    uint8_t priv[KEYRING_KEY_BYTES];
    uint8_t pub[KEYRING_KEY_BYTES];
} keyring_identity_t;

/*
Summary: Derive the identity key pair for a pair of glasses.
Inputs:  p_sn_hash - KEYRING_SN_HASH_BYTES from glasses_id_read().
         p_pin     - optional NUL-terminated PIN (NULL or "" for none).
         p_out     - receives the key pair.
Outputs: 0 on success, -1 on failure (p_out is wiped on failure).
*/
int keyring_derive(const uint8_t * p_sn_hash, const char * p_pin,
                   keyring_identity_t * p_out);

/*
Summary: Overwrite an identity with zeros so the private key does not
         linger in memory.
Inputs:  p_id - identity to clear; NULL is ignored.
Outputs: None.
*/
void keyring_wipe(keyring_identity_t * p_id);

#ifdef __cplusplus
}
#endif

#endif
