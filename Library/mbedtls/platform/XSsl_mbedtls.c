/**
 * @file XSsl_mbedtls.c -- XSsl mbedTLS backend (XFile I/O)
 */
#include "XSsl_platform.h"
#ifdef XSSL_USE_MBEDTLS
#include "XSsl_mbedtls_p.h"
#include "XMemory.h"
#include "XRandomGenerator.h"
#include "XString.h"
#include "XFileSystem_platform.h"
#include <string.h>
#include <stdio.h>
 /* 1. Memory bridge */
void* xssl_mbedtls_calloc(size_t n, size_t s) {
    if (n && s > (size_t)-1 / n)return NULL;
    return XCalloc_Hybrid(n, s);
}
void xssl_mbedtls_free(void* p) { XFree_Hybrid(p); }

/* 2. Random bridge */
#if defined(MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG)
psa_status_t mbedtls_psa_external_get_random(
    mbedtls_psa_external_random_context_t* ctx,
    uint8_t* out, size_t osz, size_t* olen) {
    (void)ctx;
    if (!out || !olen)return PSA_ERROR_INVALID_ARGUMENT;
    if (!XRandomGenerator_fillSecure(out, osz))return PSA_ERROR_INSUFFICIENT_ENTROPY;
    *olen = osz; return PSA_SUCCESS;
}
#endif

/* 3. Init/deinit */
static bool s_platformInited = false;

bool XSsl_platform_init(void) {
    if (s_platformInited) return true;
    if (mbedtls_platform_set_calloc_free(xssl_mbedtls_calloc, xssl_mbedtls_free) != 0)return 0;
    if (psa_crypto_init() != PSA_SUCCESS) return false;
    s_platformInited = true;
    return true;
}
void XSsl_platform_deinit(void) {
    if (!s_platformInited) return;
    mbedtls_psa_crypto_free();
    s_platformInited = false;
}

/* 4. Queries */
const char* XSsl_backendName(void) { return"mbedtls"; }
bool XSsl_isClassImplemented(XSslImplementedClass c) {
    switch (c) {
    case XSSL_ImplementedClass_Key:case XSSL_ImplementedClass_Certificate:
    case XSSL_ImplementedClass_Socket:case XSSL_ImplementedClass_EllipticCurve:
    case XSSL_ImplementedClass_Dtls:return 1;
    default:return 0;
    }
}
bool XSsl_isFeatureSupported(XSslSupportedFeature f) {
    switch (f) {
    case XSSL_SupportedFeature_CertificateVerification:case XSSL_SupportedFeature_ClientSideAlpn:
    case XSSL_SupportedFeature_ServerSideAlpn:case XSSL_SupportedFeature_Psk:
    case XSSL_SupportedFeature_SessionTicket:case XSSL_SupportedFeature_Alerts:return 1;
    default:return 0;
    }
}
XSslProtocol XSsl_supportedProtocols(void) { return(XSslProtocol)(XSSL_TlsV1_2 | XSSL_TlsV1_3 | XSSL_DtlsV1_2); }
XSslProtocol XSsl_defaultProtocol(void) { return XSSL_SecureProtocols; }

/* 5. Context */
static int xssl_map_proto(XSslProtocol p) {
    switch (p) {
    case XSSL_TlsV1_0:case XSSL_TlsV1_1:case XSSL_TlsV1_2:return MBEDTLS_SSL_VERSION_TLS1_2;
    case XSSL_TlsV1_3:return MBEDTLS_SSL_VERSION_TLS1_3;
    default:return MBEDTLS_SSL_VERSION_TLS1_2;
    }
}
XSslContext* XSsl_contextCreate(XSslProtocol protocol) {
    XSslContext* c = (XSslContext*)XMalloc_System(sizeof(XSslContext));
    if (!c)return NULL; memset(c, 0, sizeof(*c));
    mbedtls_ssl_config_init(&c->conf);
    int e = MBEDTLS_SSL_IS_CLIENT, t = MBEDTLS_SSL_TRANSPORT_STREAM;
    if (protocol == XSSL_DtlsV1_0 || protocol == XSSL_DtlsV1_0OrLater ||
        protocol == XSSL_DtlsV1_2 || protocol == XSSL_DtlsV1_2OrLater)t = MBEDTLS_SSL_TRANSPORT_DATAGRAM;
    if (mbedtls_ssl_config_defaults(&c->conf, e, t, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        mbedtls_ssl_config_free(&c->conf); XFree_System(c); return NULL;
    }
    int v = xssl_map_proto(protocol);
    mbedtls_ssl_conf_min_tls_version(&c->conf, v);
    mbedtls_ssl_conf_max_tls_version(&c->conf, v);
    c->configured = 1; return c;
}
void XSsl_contextDestroy(XSslContext* c) { if (!c)return; mbedtls_ssl_config_free(&c->conf); XFree_System(c); }

/* 6. File read helper (portable XFileSystem API, including FatFs) */
static unsigned char* xssl_read_file_all(const char* path, size_t* olen) {
    if (!path || !olen)return NULL; *olen = 0;
    XString* xn = XString_create_utf8(path);
    if (!xn)return NULL;
    int error = 0;
    XFd fd = XFileSystem_open(xn, XIODevice_ReadOnly, &error);
    XString_delete_base((XClass*)xn);
    if (fd == XFD_INVALID) return NULL;
    XFileStat stat;
    if (!XFileSystem_fstat(fd, &stat) || stat.size <= 0) {
        XFileSystem_close(fd);
        return NULL;
    }
    int64_t fsize = stat.size;
    unsigned char* buf = (unsigned char*)XMalloc_System((size_t)fsize + 1);
    if (!buf) { XFileSystem_close(fd); return NULL; }
    /* Read the known file length directly and tolerate a platform backend
     * returning it in multiple chunks. */
    int64_t got = 0;
    while (got < fsize) {
        int64_t n = XFileSystem_read(fd, (char*)buf + got, fsize - got);
        if (n <= 0) break;
        got += n;
    }
    XFileSystem_close(fd);
    if (got < 0 || (size_t)got != (size_t)fsize) { XFree_System(buf); return NULL; }
    buf[fsize] = '\0'; *olen = (size_t)fsize; return buf;
}

/* 7. Certificates */
XSslCertificate* XSsl_certificateFromPem(const char* d, size_t l) {
    if (!d || !l || !XSsl_platform_init())return NULL;
    XSslCertificate* c = (XSslCertificate*)XMalloc_System(sizeof(XSslCertificate));
    if (!c)return NULL; memset(c, 0, sizeof(*c)); mbedtls_x509_crt_init(&c->crt);
    unsigned char* b = (unsigned char*)XMalloc_System(l + 1);
    if (!b) { XFree_System(c); return NULL; }
    memcpy(b, d, l); b[l] = '\0';
    int r = mbedtls_x509_crt_parse(&c->crt, b, l + 1);
    XFree_System(b);
    if (r < 0) { mbedtls_x509_crt_free(&c->crt); XFree_System(c); return NULL; }return c;
}
XSslCertificate* XSsl_certificateFromDer(const uint8_t* d, size_t l) {
    if (!d || !l || !XSsl_platform_init())return NULL;
    XSslCertificate* c = (XSslCertificate*)XMalloc_System(sizeof(XSslCertificate));
    if (!c)return NULL; memset(c, 0, sizeof(*c)); mbedtls_x509_crt_init(&c->crt);
    int r = mbedtls_x509_crt_parse_der(&c->crt, d, l);
    if (r < 0) { mbedtls_x509_crt_free(&c->crt); XFree_System(c); return NULL; }return c;
}
XSslCertificate* XSsl_certificateLoad(const char* path, XSslEncodingFormat format) {
    if (!XSsl_platform_init()) return NULL;
    size_t len = 0;
    unsigned char* buf = xssl_read_file_all(path, &len);
    if (!buf)return NULL;
    XSslCertificate* c = (XSslCertificate*)XMalloc_System(sizeof(XSslCertificate));
    if (!c) { XFree_System(buf); return NULL; }
    memset(c, 0, sizeof(*c)); mbedtls_x509_crt_init(&c->crt);
    int r = (format == 1) ? mbedtls_x509_crt_parse_der(&c->crt, buf, len) : mbedtls_x509_crt_parse(&c->crt, buf, len + 1);
    XFree_System(buf);
    if (r < 0) { mbedtls_x509_crt_free(&c->crt); XFree_System(c); return NULL; }return c;
}
void XSsl_certificateDestroy(XSslCertificate* c) { if (!c)return; mbedtls_x509_crt_free(&c->crt); XFree_System(c); }

/* 8. Keys */
XSslKey* XSsl_keyFromPem(const char* data, size_t len, XSslKeyAlgorithm algo, XSslKeyType type, const char* pp) {
    if (!data || !len || !XSsl_platform_init())return NULL;
    XSslKey* k = (XSslKey*)XMalloc_System(sizeof(XSslKey));
    if (!k)return NULL; memset(k, 0, sizeof(*k)); mbedtls_pk_init(&k->pk);
    k->algorithm = (XSslKeyAlgorithm)algo; k->type = (XSslKeyType)type;
    unsigned char* b = (unsigned char*)XMalloc_System(len + 1);
    if (!b) { XFree_System(k); return NULL; }
    memcpy(b, data, len); b[len] = '\0';
    const unsigned char* pwd = (const unsigned char*)pp;
    size_t pl = pp ? strlen(pp) : 0;
    int r = (type == 1) ? mbedtls_pk_parse_public_key(&k->pk, b, len + 1) : mbedtls_pk_parse_key(&k->pk, b, len + 1, pwd, pl);
    XFree_System(b);
    if (r) { mbedtls_pk_free(&k->pk); XFree_System(k); return NULL; }return k;
}
XSslKey* XSsl_keyFromDer(const uint8_t* data, size_t len, XSslKeyAlgorithm algo, XSslKeyType type) {
    if (!data || !len || !XSsl_platform_init())return NULL;
    XSslKey* k = (XSslKey*)XMalloc_System(sizeof(XSslKey));
    if (!k)return NULL; memset(k, 0, sizeof(*k)); mbedtls_pk_init(&k->pk);
    k->algorithm = (XSslKeyAlgorithm)algo; k->type = (XSslKeyType)type;
    int r = (type == 1) ? mbedtls_pk_parse_public_key(&k->pk, data, len) : mbedtls_pk_parse_key(&k->pk, data, len, NULL, 0);
    if (r) { mbedtls_pk_free(&k->pk); XFree_System(k); return NULL; }return k;
}
void XSsl_keyDestroy(XSslKey* k) { if (!k)return; mbedtls_pk_free(&k->pk); XFree_System(k); }

bool XSsl_publicKeyEncrypt(const uint8_t* publicKey, size_t publicKeySize,
                           XSslEncodingFormat format, XSslKeyAlgorithm algorithm,
                           const uint8_t* data, size_t dataSize,
                           XByteArray** encrypted)
{
    mbedtls_pk_context pk;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t keyId = MBEDTLS_SVC_KEY_ID_INIT;
    XByteArray* output = NULL;
    unsigned char* keyBuffer = NULL;
    size_t outputSize;
    size_t outputLength = 0;
    bool imported = false;
    bool ok = false;
    int result;
    psa_status_t status;

    if (!publicKey || publicKeySize == 0 || algorithm != XSSL_KeyAlgorithm_Rsa
        || !data || dataSize == 0 || !encrypted || !XSsl_platform_init()) return false;
    *encrypted = NULL;
    mbedtls_pk_init(&pk);
    if (format == XSSL_Pem) {
        keyBuffer = (unsigned char*)XMalloc_System(publicKeySize + 1);
        if (!keyBuffer) goto cleanup;
        memcpy(keyBuffer, publicKey, publicKeySize);
        keyBuffer[publicKeySize] = '\0';
        result = mbedtls_pk_parse_public_key(&pk, keyBuffer, publicKeySize + 1);
    } else {
        result = mbedtls_pk_parse_public_key(&pk, publicKey, publicKeySize);
    }
    if (result != 0 || mbedtls_pk_get_key_type(&pk) != PSA_KEY_TYPE_RSA_PUBLIC_KEY)
        goto cleanup;
    result = mbedtls_pk_get_psa_attributes(&pk, PSA_KEY_USAGE_ENCRYPT, &attributes);
    if (result != 0) goto cleanup;
    result = mbedtls_pk_import_into_psa(&pk, &attributes, &keyId);
    if (result != 0) goto cleanup;
    imported = true;

    outputSize = (mbedtls_pk_get_bitlen(&pk) + 7u) / 8u;
    output = XByteArray_create();
    if (outputSize == 0 || !output
        || !XVector_resize_base((XVector*)output, outputSize))
        goto cleanup;
    status = psa_asymmetric_encrypt(keyId, PSA_ALG_RSA_PKCS1V15_CRYPT,
                                    data, dataSize, NULL, 0,
                                    (uint8_t*)XByteArray_data(output), outputSize,
                                    &outputLength);
    if (status != PSA_SUCCESS
        || !XVector_resize_base((XVector*)output, outputLength))
        goto cleanup;
    *encrypted = output;
    output = NULL;
    ok = true;

cleanup:
    if (imported) psa_destroy_key(keyId);
    psa_reset_key_attributes(&attributes);
    mbedtls_pk_free(&pk);
    if (keyBuffer) XFree_System(keyBuffer);
    if (output) XClass_delete_base((XClass*)output);
    return ok;
}

/* 9. Random */
bool XSsl_randomBytes(uint8_t* b, size_t l) {
    if (!b || !l)return 0; return XRandomGenerator_fillSecure(b, l) ? 1 : 0;
}

#endif /* XSSL_USE_MBEDTLS */
