/*
Summary: Stacked asymmetric + symmetric sealing of one message.

         seal(recipient_pub, aad, msg):
           1. ephemeral X25519 key pair (eph_priv, eph_pub)
           2. shared = X25519(eph_priv, recipient_pub)
           3. kek    = HKDF-SHA256(shared, salt = eph_pub || recipient_pub,
                                   info = ENVELOPE_KEK_INFO)
           4. dek    = 32 random bytes
           5. wrapped = AES-256-GCM(kek, wrap_nonce, dek, aad)
           6. ct      = AES-256-GCM(dek, msg_nonce,  msg, aad)

         Blob layout (ENVELOPE_OVERHEAD_BYTES + msg length):
           eph_pub[32] wrap_nonce[12] wrapped_dek[32] wrap_tag[16]
           msg_nonce[12] msg_tag[16] ciphertext[N]

         open() reverses it with the recipient private key. Any wrong
         key, wrong aad (the drawing fingerprint), or modified byte fails
         a GCM tag check and yields ENVELOPE_LOCKED with no plaintext.

         The X25519 layer is what lets a sender seal a message with only
         the recipient's public key; the DEK layer means the message key
         is never derived from anything, only wrapped.
*/

#ifndef ENVELOPE_H
#define ENVELOPE_H

#include "bytes.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ENVELOPE_KEY_BYTES          (32)
#define ENVELOPE_NONCE_BYTES        (12)
#define ENVELOPE_TAG_BYTES          (16)
#define ENVELOPE_OVERHEAD_BYTES     (120)
#define ENVELOPE_MAX_PAYLOAD_BYTES  (1048576)

#define ENVELOPE_OK         (0)
#define ENVELOPE_ERROR      (-1)
#define ENVELOPE_LOCKED     (-2)

/*
Summary: What goes into a seal or open call besides the key.
Inputs:  aad     - additional authenticated data; bound to the tags but
                   not encrypted (here: the drawing fingerprint).
         payload - plaintext to seal, or the blob to open.
Outputs: None.
*/
typedef struct
{
    byte_span_t aad;
    byte_span_t payload;
} envelope_input_t;

/*
Summary: Compute the X25519 public key for a private key.
Inputs:  p_priv - ENVELOPE_KEY_BYTES private key.
         p_pub  - receives ENVELOPE_KEY_BYTES.
Outputs: 0 on success, -1 on failure.
*/
int envelope_public_key(const uint8_t * p_priv, uint8_t * p_pub);

/*
Summary: Seal a message to a recipient public key.
Inputs:  p_recipient_pub - ENVELOPE_KEY_BYTES X25519 public key.
         p_in            - aad and the plaintext payload.
         p_out           - buffer with cap >= payload.len +
                           ENVELOPE_OVERHEAD_BYTES.
Outputs: ENVELOPE_OK with p_out->len set to the blob size, or
         ENVELOPE_ERROR on bad arguments or libcrypto failure.
*/
int envelope_seal(const uint8_t * p_recipient_pub,
                  const envelope_input_t * p_in, byte_buf_t * p_out);

/*
Summary: Open a blob with the recipient private key.
Inputs:  p_recipient_priv - ENVELOPE_KEY_BYTES X25519 private key.
         p_in             - the same aad used to seal, and the blob as
                            payload.
         p_out            - buffer with cap >= blob length -
                            ENVELOPE_OVERHEAD_BYTES.
Outputs: ENVELOPE_OK with p_out->len set to the plaintext size,
         ENVELOPE_LOCKED if any authentication tag fails (p_out is
         zeroed), or ENVELOPE_ERROR on bad arguments or library failure.
*/
int envelope_open(const uint8_t * p_recipient_priv,
                  const envelope_input_t * p_in, byte_buf_t * p_out);

#ifdef __cplusplus
}
#endif

#endif
