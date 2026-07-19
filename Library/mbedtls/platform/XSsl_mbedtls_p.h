/**
 * @file XSsl_mbedtls_p.h
 * @brief XSsl mbedTLS backend - internal header (opaque type definitions).
 *
 * Wraps mbedTLS 4.x types as the opaque XSslContext / XSslCertificate / XSslKey
 * declared in XSsl_platform.h. Platform dependencies are bridged to XinYueC in
 * XSsl_mbedtls.c:
 *   - memory  -> XMemory hybrid allocator (XMalloc_System / XMalloc_MultiPool)
 *   - random  -> XRandomGenerator (XRandomGenerator_fillSecure)
 *   - file    -> XFile
 *   - network -> XAbstractSocket BIO (integrated later, like Qt<->OpenSSL)
 *
 * XSsl_platform_init() must be called once before using cert/key/context ops,
 * because it runs psa_crypto_init() and installs the memory/RNG hooks.
 */
#ifndef XSSL_MBEDTLS_P_H
#define XSSL_MBEDTLS_P_H

#include "XSsl_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(XSSL_USE_MBEDTLS)

#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform.h>
#include <psa/crypto.h>

/* ---- Opaque backend objects (forward-declared in XSsl_platform.h) ---- */

/* SSL context: holds an mbedTLS SSL configuration. The transport role defaults
 * to client; reconfigure with mbedtls_ssl_conf_endpoint() when the socket BIO
 * is wired in. */
struct XSslContext {
    mbedtls_ssl_config conf;   /* SSL configuration (versions, ciphers, ...) */
    int                 configured; /* whether config_defaults succeeded */
};

/* Certificate: an mbedTLS certificate (possibly a chain head). */
struct XSslCertificate {
    mbedtls_x509_crt crt;
};

/* Key: an mbedTLS PK context plus the algorithm/type requested by the caller. */
struct XSslKey {
    mbedtls_pk_context pk;
    XSslKeyAlgorithm   algorithm;
    XSslKeyType        type;
};

/* ---- Memory bridge (installed via mbedtls_platform_set_calloc_free) ---- */
void *xssl_mbedtls_calloc(size_t n, size_t size);
void  xssl_mbedtls_free(void *ptr);

#endif /* XSSL_USE_MBEDTLS */

#ifdef __cplusplus
}
#endif

#endif /* XSSL_MBEDTLS_P_H */