/*
Summary: HKDF-SHA256 (RFC 5869) key derivation via libcrypto's EVP_KDF.
         Every key in this example passes through here, so the salt and
         info strings are the only things that distinguish an identity
         key from a wrapping key.
*/

#ifndef KDF_H
#define KDF_H

#include "bytes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
Summary: The three HKDF inputs, bundled to keep the call short.
Inputs:  ikm  - input keying material (the secret).
         salt - non-secret randomiser or domain string.
         info - context string binding the output to one purpose.
Outputs: None.
*/
typedef struct
{
    byte_span_t ikm;
    byte_span_t salt;
    byte_span_t info;
} kdf_input_t;

/*
Summary: Run HKDF-Extract then HKDF-Expand with SHA-256.
Inputs:  p_in    - the ikm/salt/info triple.
         p_out   - receives out_len derived bytes.
         out_len - bytes wanted (at most 255 * 32).
Outputs: 0 on success, -1 on any libcrypto failure or bad argument.
*/
int kdf_hkdf_sha256(const kdf_input_t * p_in, uint8_t * p_out,
                    size_t out_len);

#ifdef __cplusplus
}
#endif

#endif
