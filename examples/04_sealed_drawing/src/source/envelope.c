#include "envelope.h"

#include "kdf.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <limits.h>
#include <string.h>

#define OFF_EPH_PUB         (0)
#define OFF_WRAP_NONCE      (32)
#define OFF_WRAPPED_DEK     (44)
#define OFF_WRAP_TAG        (76)
#define OFF_MSG_NONCE       (92)
#define OFF_MSG_TAG         (104)
#define OFF_CIPHERTEXT      (ENVELOPE_OVERHEAD_BYTES)
#define KEK_SALT_BYTES      (2 * ENVELOPE_KEY_BYTES)

static const char KEK_INFO[] = "XR_Playground/sealed-drawing/kek/v1";

typedef struct
{
    const uint8_t * p_key;
    const uint8_t * p_nonce;
    byte_span_t     aad;
    byte_span_t     in;
    uint8_t *       p_out;
    uint8_t *       p_tag_out;
    const uint8_t * p_tag_in;
} aead_op_t;

typedef struct
{
    const uint8_t * p_eph_pub;
    const uint8_t * p_recipient_pub;
    const uint8_t * p_shared;
} kek_input_t;

static int x25519_generate(uint8_t * p_priv, uint8_t * p_pub)
{
    int        result  = -1;
    size_t     len     = ENVELOPE_KEY_BYTES;
    EVP_PKEY * p_key   = NULL;

    p_key = EVP_PKEY_Q_keygen(NULL, NULL, "X25519");
    if (NULL == p_key)
    {
        goto cleanup;
    }
    if (1 != EVP_PKEY_get_raw_private_key(p_key, p_priv, &len))
    {
        goto cleanup;
    }
    len = ENVELOPE_KEY_BYTES;
    if (1 != EVP_PKEY_get_raw_public_key(p_key, p_pub, &len))
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_key)
    {
        EVP_PKEY_free(p_key);
    }

    return result;
}

static int x25519_shared(const uint8_t * p_priv,
                         const uint8_t * p_peer_pub, uint8_t * p_shared)
{
    int            result = -1;
    size_t         len    = ENVELOPE_KEY_BYTES;
    EVP_PKEY *     p_mine = NULL;
    EVP_PKEY *     p_peer = NULL;
    EVP_PKEY_CTX * p_ctx  = NULL;

    p_mine = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, p_priv,
                                          ENVELOPE_KEY_BYTES);
    p_peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL,
                                         p_peer_pub, ENVELOPE_KEY_BYTES);
    if ((NULL == p_mine) || (NULL == p_peer))
    {
        goto cleanup;
    }
    p_ctx = EVP_PKEY_CTX_new(p_mine, NULL);
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if (1 != EVP_PKEY_derive_init(p_ctx))
    {
        goto cleanup;
    }
    if (1 != EVP_PKEY_derive_set_peer(p_ctx, p_peer))
    {
        goto cleanup;
    }
    if (1 != EVP_PKEY_derive(p_ctx, p_shared, &len))
    {
        goto cleanup;
    }
    if (ENVELOPE_KEY_BYTES != len)
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_ctx)
    {
        EVP_PKEY_CTX_free(p_ctx);
    }
    if (NULL != p_peer)
    {
        EVP_PKEY_free(p_peer);
    }
    if (NULL != p_mine)
    {
        EVP_PKEY_free(p_mine);
    }

    return result;
}

int envelope_public_key(const uint8_t * p_priv, uint8_t * p_pub)
{
    int        result  = -1;
    size_t     pub_len = ENVELOPE_KEY_BYTES;
    EVP_PKEY * p_key   = NULL;

    if ((NULL == p_priv) || (NULL == p_pub))
    {
        goto cleanup;
    }
    p_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, p_priv,
                                         ENVELOPE_KEY_BYTES);
    if (NULL == p_key)
    {
        goto cleanup;
    }
    if (1 != EVP_PKEY_get_raw_public_key(p_key, p_pub, &pub_len))
    {
        goto cleanup;
    }
    if (ENVELOPE_KEY_BYTES != pub_len)
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_key)
    {
        EVP_PKEY_free(p_key);
    }

    return result;
}

static int derive_kek(const kek_input_t * p_in, uint8_t * p_kek)
{
    int         result = -1;
    uint8_t     salt[KEK_SALT_BYTES];
    kdf_input_t kdf;

    memset(&kdf, 0, sizeof(kdf));
    memcpy(salt, p_in->p_eph_pub, ENVELOPE_KEY_BYTES);
    memcpy(salt + ENVELOPE_KEY_BYTES, p_in->p_recipient_pub,
           ENVELOPE_KEY_BYTES);
    kdf.ikm.p_data  = p_in->p_shared;
    kdf.ikm.len     = ENVELOPE_KEY_BYTES;
    kdf.salt.p_data = salt;
    kdf.salt.len    = KEK_SALT_BYTES;
    kdf.info.p_data = (const uint8_t *)KEK_INFO;
    kdf.info.len    = sizeof(KEK_INFO) - 1U;
    result          = kdf_hkdf_sha256(&kdf, p_kek, ENVELOPE_KEY_BYTES);

    return result;
}

static int aead_feed_aad(EVP_CIPHER_CTX * p_ctx, byte_span_t aad,
                         int encrypting)
{
    int result  = 0;
    int out_len = 0;
    int ok      = 1;

    if (0U == aad.len)
    {
        goto cleanup;
    }
    if (0 != encrypting)
    {
        ok = EVP_EncryptUpdate(p_ctx, NULL, &out_len, aad.p_data,
                               (int)aad.len);
    }
    else
    {
        ok = EVP_DecryptUpdate(p_ctx, NULL, &out_len, aad.p_data,
                               (int)aad.len);
    }
    if (1 != ok)
    {
        result = -1;
    }

cleanup:

    return result;
}

static int aead_encrypt(const aead_op_t * p_op)
{
    int              result  = -1;
    int              out_len = 0;
    int              fin_len = 0;
    EVP_CIPHER_CTX * p_ctx   = NULL;

    if ((p_op->in.len > INT_MAX) || (p_op->aad.len > INT_MAX))
    {
        goto cleanup;
    }
    p_ctx = EVP_CIPHER_CTX_new();
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if (1 != EVP_EncryptInit_ex(p_ctx, EVP_aes_256_gcm(), NULL,
                                p_op->p_key, p_op->p_nonce))
    {
        goto cleanup;
    }
    if (0 != aead_feed_aad(p_ctx, p_op->aad, 1))
    {
        goto cleanup;
    }
    if (1 != EVP_EncryptUpdate(p_ctx, p_op->p_out, &out_len,
                               p_op->in.p_data, (int)p_op->in.len))
    {
        goto cleanup;
    }
    if (1 != EVP_EncryptFinal_ex(p_ctx, p_op->p_out + out_len, &fin_len))
    {
        goto cleanup;
    }
    if (1 != EVP_CIPHER_CTX_ctrl(p_ctx, EVP_CTRL_GCM_GET_TAG,
                                 ENVELOPE_TAG_BYTES, p_op->p_tag_out))
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_ctx)
    {
        EVP_CIPHER_CTX_free(p_ctx);
    }

    return result;
}

static int aead_decrypt(const aead_op_t * p_op)
{
    int              result  = ENVELOPE_ERROR;
    int              out_len = 0;
    int              fin_len = 0;
    EVP_CIPHER_CTX * p_ctx   = NULL;

    if ((p_op->in.len > INT_MAX) || (p_op->aad.len > INT_MAX))
    {
        goto cleanup;
    }
    p_ctx = EVP_CIPHER_CTX_new();
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if (1 != EVP_DecryptInit_ex(p_ctx, EVP_aes_256_gcm(), NULL,
                                p_op->p_key, p_op->p_nonce))
    {
        goto cleanup;
    }
    if (0 != aead_feed_aad(p_ctx, p_op->aad, 0))
    {
        goto cleanup;
    }
    if (1 != EVP_DecryptUpdate(p_ctx, p_op->p_out, &out_len,
                               p_op->in.p_data, (int)p_op->in.len))
    {
        goto cleanup;
    }
    if (1 != EVP_CIPHER_CTX_ctrl(p_ctx, EVP_CTRL_GCM_SET_TAG,
                                 ENVELOPE_TAG_BYTES,
                                 (void *)p_op->p_tag_in))
    {
        goto cleanup;
    }
    if (1 != EVP_DecryptFinal_ex(p_ctx, p_op->p_out + out_len, &fin_len))
    {
        OPENSSL_cleanse(p_op->p_out, p_op->in.len);
        result = ENVELOPE_LOCKED;
        goto cleanup;
    }
    result = ENVELOPE_OK;

cleanup:
    if (NULL != p_ctx)
    {
        EVP_CIPHER_CTX_free(p_ctx);
    }

    return result;
}

int envelope_seal(const uint8_t * p_recipient_pub,
                  const envelope_input_t * p_in, byte_buf_t * p_out)
{
    int         result = ENVELOPE_ERROR;
    uint8_t *   p_blob = NULL;
    uint8_t     eph_priv[ENVELOPE_KEY_BYTES];
    uint8_t     shared[ENVELOPE_KEY_BYTES];
    uint8_t     kek[ENVELOPE_KEY_BYTES];
    uint8_t     dek[ENVELOPE_KEY_BYTES];
    kek_input_t kek_in;
    aead_op_t   op;

    memset(eph_priv, 0, sizeof(eph_priv));
    memset(shared, 0, sizeof(shared));
    memset(kek, 0, sizeof(kek));
    memset(dek, 0, sizeof(dek));
    memset(&kek_in, 0, sizeof(kek_in));
    memset(&op, 0, sizeof(op));

    if ((NULL == p_recipient_pub) || (NULL == p_in) || (NULL == p_out) ||
        (NULL == p_out->p_data))
    {
        goto cleanup;
    }
    if ((p_in->payload.len > ENVELOPE_MAX_PAYLOAD_BYTES) ||
        (p_out->cap < (p_in->payload.len + ENVELOPE_OVERHEAD_BYTES)))
    {
        goto cleanup;
    }
    p_blob = p_out->p_data;

    if (0 != x25519_generate(eph_priv, p_blob + OFF_EPH_PUB))
    {
        goto cleanup;
    }
    if (0 != x25519_shared(eph_priv, p_recipient_pub, shared))
    {
        goto cleanup;
    }
    kek_in.p_eph_pub       = p_blob + OFF_EPH_PUB;
    kek_in.p_recipient_pub = p_recipient_pub;
    kek_in.p_shared        = shared;
    if (0 != derive_kek(&kek_in, kek))
    {
        goto cleanup;
    }
    if ((1 != RAND_bytes(dek, ENVELOPE_KEY_BYTES)) ||
        (1 != RAND_bytes(p_blob + OFF_WRAP_NONCE, ENVELOPE_NONCE_BYTES)) ||
        (1 != RAND_bytes(p_blob + OFF_MSG_NONCE, ENVELOPE_NONCE_BYTES)))
    {
        goto cleanup;
    }

    op.p_key     = kek;
    op.p_nonce   = p_blob + OFF_WRAP_NONCE;
    op.aad       = p_in->aad;
    op.in.p_data = dek;
    op.in.len    = ENVELOPE_KEY_BYTES;
    op.p_out     = p_blob + OFF_WRAPPED_DEK;
    op.p_tag_out = p_blob + OFF_WRAP_TAG;
    if (0 != aead_encrypt(&op))
    {
        goto cleanup;
    }

    op.p_key   = dek;
    op.p_nonce = p_blob + OFF_MSG_NONCE;
    op.in      = p_in->payload;
    op.p_out   = p_blob + OFF_CIPHERTEXT;
    op.p_tag_out = p_blob + OFF_MSG_TAG;
    if (0 != aead_encrypt(&op))
    {
        goto cleanup;
    }
    p_out->len = ENVELOPE_OVERHEAD_BYTES + p_in->payload.len;
    result     = ENVELOPE_OK;

cleanup:
    OPENSSL_cleanse(eph_priv, sizeof(eph_priv));
    OPENSSL_cleanse(shared, sizeof(shared));
    OPENSSL_cleanse(kek, sizeof(kek));
    OPENSSL_cleanse(dek, sizeof(dek));

    return result;
}

int envelope_open(const uint8_t * p_recipient_priv,
                  const envelope_input_t * p_in, byte_buf_t * p_out)
{
    int             result   = ENVELOPE_ERROR;
    int             rc       = ENVELOPE_ERROR;
    size_t          ct_len   = 0;
    const uint8_t * p_blob   = NULL;
    uint8_t         shared[ENVELOPE_KEY_BYTES];
    uint8_t         kek[ENVELOPE_KEY_BYTES];
    uint8_t         dek[ENVELOPE_KEY_BYTES];
    uint8_t         my_pub[ENVELOPE_KEY_BYTES];
    kek_input_t     kek_in;
    aead_op_t       op;

    memset(shared, 0, sizeof(shared));
    memset(kek, 0, sizeof(kek));
    memset(dek, 0, sizeof(dek));
    memset(my_pub, 0, sizeof(my_pub));
    memset(&kek_in, 0, sizeof(kek_in));
    memset(&op, 0, sizeof(op));

    if ((NULL == p_recipient_priv) || (NULL == p_in) || (NULL == p_out) ||
        (NULL == p_out->p_data) || (NULL == p_in->payload.p_data))
    {
        goto cleanup;
    }
    if (p_in->payload.len < ENVELOPE_OVERHEAD_BYTES)
    {
        goto cleanup;
    }
    ct_len = p_in->payload.len - ENVELOPE_OVERHEAD_BYTES;
    if ((ct_len > ENVELOPE_MAX_PAYLOAD_BYTES) || (p_out->cap < ct_len))
    {
        goto cleanup;
    }
    p_blob = p_in->payload.p_data;

    if (0 != x25519_shared(p_recipient_priv, p_blob + OFF_EPH_PUB, shared))
    {
        goto cleanup;
    }
    if (0 != envelope_public_key(p_recipient_priv, my_pub))
    {
        goto cleanup;
    }
    kek_in.p_eph_pub       = p_blob + OFF_EPH_PUB;
    kek_in.p_recipient_pub = my_pub;
    kek_in.p_shared        = shared;
    if (0 != derive_kek(&kek_in, kek))
    {
        goto cleanup;
    }

    op.p_key     = kek;
    op.p_nonce   = p_blob + OFF_WRAP_NONCE;
    op.aad       = p_in->aad;
    op.in.p_data = p_blob + OFF_WRAPPED_DEK;
    op.in.len    = ENVELOPE_KEY_BYTES;
    op.p_out     = dek;
    op.p_tag_in  = p_blob + OFF_WRAP_TAG;
    rc = aead_decrypt(&op);
    if (ENVELOPE_OK != rc)
    {
        result = rc;
        goto cleanup;
    }

    op.p_key     = dek;
    op.p_nonce   = p_blob + OFF_MSG_NONCE;
    op.in.p_data = p_blob + OFF_CIPHERTEXT;
    op.in.len    = ct_len;
    op.p_out     = p_out->p_data;
    op.p_tag_in  = p_blob + OFF_MSG_TAG;
    rc = aead_decrypt(&op);
    if (ENVELOPE_OK != rc)
    {
        result = rc;
        goto cleanup;
    }
    p_out->len = ct_len;
    result     = ENVELOPE_OK;

cleanup:
    OPENSSL_cleanse(shared, sizeof(shared));
    OPENSSL_cleanse(kek, sizeof(kek));
    OPENSSL_cleanse(dek, sizeof(dek));

    return result;
}
