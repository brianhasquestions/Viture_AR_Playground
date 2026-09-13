#include "keyring.h"

#include "envelope.h"
#include "kdf.h"

#include <openssl/crypto.h>

#include <stdlib.h>
#include <string.h>

static const char KEYRING_SALT[] = "XR_Playground/sealed-drawing/identity/v1";
static const char KEYRING_INFO[] = "x25519-static-key";

int keyring_derive(const uint8_t * p_sn_hash, const char * p_pin,
                   keyring_identity_t * p_out)
{
    int         result  = -1;
    size_t      pin_len = 0;
    uint8_t *   p_ikm   = NULL;
    kdf_input_t in;

    memset(&in, 0, sizeof(in));
    if ((NULL == p_sn_hash) || (NULL == p_out))
    {
        goto cleanup;
    }
    if (NULL != p_pin)
    {
        pin_len = strnlen(p_pin, KEYRING_PIN_MAX_CHARS);
    }
    p_ikm = (uint8_t *)malloc(KEYRING_SN_HASH_BYTES + pin_len);
    if (NULL == p_ikm)
    {
        goto cleanup;
    }
    memcpy(p_ikm, p_sn_hash, KEYRING_SN_HASH_BYTES);
    if (0U != pin_len)
    {
        memcpy(p_ikm + KEYRING_SN_HASH_BYTES, p_pin, pin_len);
    }
    in.ikm.p_data  = p_ikm;
    in.ikm.len     = KEYRING_SN_HASH_BYTES + pin_len;
    in.salt.p_data = (const uint8_t *)KEYRING_SALT;
    in.salt.len    = sizeof(KEYRING_SALT) - 1U;
    in.info.p_data = (const uint8_t *)KEYRING_INFO;
    in.info.len    = sizeof(KEYRING_INFO) - 1U;

    if (0 != kdf_hkdf_sha256(&in, p_out->priv, KEYRING_KEY_BYTES))
    {
        goto cleanup;
    }
    if (0 != envelope_public_key(p_out->priv, p_out->pub))
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_ikm)
    {
        OPENSSL_cleanse(p_ikm, KEYRING_SN_HASH_BYTES + pin_len);
        free(p_ikm);
    }
    if ((0 != result) && (NULL != p_out))
    {
        keyring_wipe(p_out);
    }

    return result;
}

void keyring_wipe(keyring_identity_t * p_id)
{
    if (NULL != p_id)
    {
        OPENSSL_cleanse(p_id, sizeof(*p_id));
    }
}
