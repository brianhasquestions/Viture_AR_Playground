#include "kdf.h"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>

#define PARAM_COUNT     (5)
#define PARAM_DIGEST    (0)
#define PARAM_KEY       (1)
#define PARAM_SALT      (2)
#define PARAM_INFO      (3)
#define PARAM_END       (4)

int kdf_hkdf_sha256(const kdf_input_t * p_in, uint8_t * p_out,
                    size_t out_len)
{
    int           result = -1;
    EVP_KDF *     p_kdf  = NULL;
    EVP_KDF_CTX * p_ctx  = NULL;
    OSSL_PARAM    params[PARAM_COUNT];

    if ((NULL == p_in) || (NULL == p_out) || (0U == out_len))
    {
        goto cleanup;
    }
    p_kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (NULL == p_kdf)
    {
        goto cleanup;
    }
    p_ctx = EVP_KDF_CTX_new(p_kdf);
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    params[PARAM_DIGEST] = OSSL_PARAM_construct_utf8_string(
        OSSL_KDF_PARAM_DIGEST, (char *)"SHA256", 0U);
    params[PARAM_KEY] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_KEY, (void *)p_in->ikm.p_data, p_in->ikm.len);
    params[PARAM_SALT] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_SALT, (void *)p_in->salt.p_data, p_in->salt.len);
    params[PARAM_INFO] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_INFO, (void *)p_in->info.p_data, p_in->info.len);
    params[PARAM_END] = OSSL_PARAM_construct_end();

    if (1 != EVP_KDF_derive(p_ctx, p_out, out_len, params))
    {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (NULL != p_ctx)
    {
        EVP_KDF_CTX_free(p_ctx);
    }
    if (NULL != p_kdf)
    {
        EVP_KDF_free(p_kdf);
    }

    return result;
}
