/** @file XSshClient.c
 * @brief SSH 客户端实现：XSshClient 协议栈。
 */

#include "XSshClient.h"
#include "XSshCrypto.h"

#if XPROTOCOL_ON && XSSH_ON && XSSH_CLIENT_ON

#include "XMemory.h"
#include "XVarList.h"
#include "XRandomGenerator.h"
#include <string.h>

enum {
    XSSH_CLIENT_RX_CAPACITY = 8192,
    XSSH_CLIENT_TX_CAPACITY = 8192,
    XSSH_CLIENT_MAX_PACKET = 4096,
    XSSH_CLIENT_MAX_PAYLOAD = 4096,
    XSSH_CLIENT_HASH_LEN = 32,
    XSSH_CLIENT_MAC_LEN = 32,
    XSSH_CLIENT_POINT_LEN = 65,
    XSSH_CLIENT_CURVE25519_PUBLIC_LEN = 32,
    XSSH_CLIENT_MAX_CIPHER_KEY_LEN = 32,
    XSSH_CLIENT_VERSION_MAX = 256,
    XSSH_CLIENT_PENDING_CAPACITY = 32768,
    XSSH_CLIENT_INITIAL_WINDOW = 32768,
    XSSH_CLIENT_WINDOW_LOW_WATER = 8192,
    XSSH_CLIENT_MAX_PACKET_SIZE = 2048
};

enum {
    SSH_MSG_DISCONNECT = 1,
    SSH_MSG_IGNORE = 2,
    SSH_MSG_UNIMPLEMENTED = 3,
    SSH_MSG_DEBUG = 4,
    SSH_MSG_SERVICE_REQUEST = 5,
    SSH_MSG_SERVICE_ACCEPT = 6,
    SSH_MSG_EXT_INFO = 7,
    SSH_MSG_KEXINIT = 20,
    SSH_MSG_NEWKEYS = 21,
    SSH_MSG_KEX_ECDH_INIT = 30,
    SSH_MSG_KEX_ECDH_REPLY = 31,
    SSH_MSG_USERAUTH_REQUEST = 50,
    SSH_MSG_USERAUTH_FAILURE = 51,
    SSH_MSG_USERAUTH_SUCCESS = 52,
    SSH_MSG_CHANNEL_OPEN = 90,
    SSH_MSG_CHANNEL_OPEN_CONFIRMATION = 91,
    SSH_MSG_CHANNEL_OPEN_FAILURE = 92,
    SSH_MSG_CHANNEL_WINDOW_ADJUST = 93,
    SSH_MSG_CHANNEL_DATA = 94,
    SSH_MSG_CHANNEL_EXTENDED_DATA = 95,
    SSH_MSG_CHANNEL_EOF = 96,
    SSH_MSG_CHANNEL_CLOSE = 97,
    SSH_MSG_CHANNEL_REQUEST = 98,
    SSH_MSG_CHANNEL_SUCCESS = 99,
    SSH_MSG_CHANNEL_FAILURE = 100
};

static const char* const XSSH_CLIENT_VERSION = "SSH-2.0-XinYueC_Client_1.0\r\n";

typedef enum XSshClientState {
    XSshClientState_Version = 0,
    XSshClientState_KexInit,
    XSshClientState_KexEcdh,
    XSshClientState_NewKeys,
    XSshClientState_Service,
    XSshClientState_Userauth,
    XSshClientState_ChannelOpen,
    XSshClientState_ChannelRequest,
    XSshClientState_Ready
} XSshClientState;

typedef struct XSshClientData {
    XSshClientState state;
    bool closed;
    bool authenticated;
    bool channelOpen;
    bool channelEof;
    bool channelClose;
    uint8_t username[XSSH_LOGIN_NAME_SIZE];
    size_t usernameLen;
    uint8_t password[XSSH_LOGIN_PASSWORD_SIZE];
    size_t passwordLen;

    uint8_t serverVersion[XSSH_CLIENT_VERSION_MAX];
    size_t serverVersionLen;
    uint8_t clientKexInit[XSSH_CLIENT_RX_CAPACITY];
    size_t clientKexInitLen;
    uint8_t serverKexInit[XSSH_CLIENT_RX_CAPACITY];
    size_t serverKexInitLen;
    XSshKexAlgorithm kexAlgorithm;
    XSshCipherAlgorithm cipherC2S;
    XSshCipherAlgorithm cipherS2C;

    XCryptographic_Key ecdhKey;
    XCryptographic_Key hostPublicKey;
    uint8_t clientPublicBlob[XSSH_CLIENT_POINT_LEN];
    size_t clientPublicBlobLen;
    uint8_t serverPublicBlob[XSSH_CLIENT_POINT_LEN];
    size_t serverPublicBlobLen;
    uint8_t hostKeyBlob[256];
    size_t hostKeyBlobLen;
    uint8_t sharedSecret[64];
    size_t sharedSecretLen;
    uint8_t exchangeHash[XSSH_CLIENT_HASH_LEN];
    uint8_t sessionId[XSSH_CLIENT_HASH_LEN];

    XCryptographic_Key cipherKeyC2S;
    XCryptographic_Key cipherKeyS2C;
    uint8_t macKeyC2S[XSSH_CLIENT_MAC_LEN];
    uint8_t macKeyS2C[XSSH_CLIENT_MAC_LEN];
    XCryptographic_CipherOperation encOp;
    XCryptographic_CipherOperation decOp;
    bool encActive;
    bool decActive;
    bool sendEncrypted;
    bool recvEncrypted;
    uint32_t sendSeq;
    uint32_t recvSeq;

    uint8_t rxBuf[XSSH_CLIENT_RX_CAPACITY];
    size_t rxLen;
    bool packetLenKnown;
    uint32_t packetLen;
    uint8_t lenPlain[4];
    uint8_t payloadBuf[XSSH_CLIENT_MAX_PAYLOAD];
    uint8_t txOut[XSSH_CLIENT_TX_CAPACITY];
    size_t txOutLen;
    size_t txOutOffset;

    uint32_t localChannel;
    uint32_t remoteChannel;
    uint32_t remoteWindow;
    uint32_t remoteMaxPacket;
    uint32_t localWindow;
    uint32_t localMaxPacket;
    uint8_t channelRequestStep;
    uint8_t pending[XSSH_CLIENT_PENDING_CAPACITY];
    size_t pendingLen;
    size_t pendingOffset;
} XSshClientData;

#define XSSH_CLIENT_DATA(self) ((XSshClientData*)((self)->m_data))

static bool xssh_client_emit_data(XSshClient* self,
                                  const uint8_t* data, size_t size);
static void xssh_client_cleanup_keys(XSshClient* self);

static void xssh_client_write_u32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t xssh_client_get_u32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void xssh_client_write_string(uint8_t* buffer, size_t* offset,
                                     const void* data, size_t length)
{
    xssh_client_write_u32(buffer + *offset, (uint32_t)length);
    *offset += 4;
    if (length) memcpy(buffer + *offset, data, length);
    *offset += length;
}

static bool xssh_client_get_string(const uint8_t* buffer, size_t length,
                                   size_t* offset, const uint8_t** data,
                                   size_t* dataLen)
{
    uint32_t value;
    if (!buffer || !offset || !data || !dataLen || *offset > length ||
        length - *offset < 4) return false;
    value = xssh_client_get_u32(buffer + *offset);
    *offset += 4;
    if ((size_t)value > length - *offset) return false;
    *data = buffer + *offset;
    *dataLen = (size_t)value;
    *offset += (size_t)value;
    return true;
}

static bool xssh_client_const_equal(const uint8_t* a, const uint8_t* b,
                                    size_t length)
{
    uint8_t different = 0;
    size_t i;
    if (!a || !b) return false;
    for (i = 0; i < length; ++i) different |= (uint8_t)(a[i] ^ b[i]);
    return different == 0;
}

static bool xssh_client_send_raw(XSshClient* self,
                                 const uint8_t* data, size_t size)
{
    XSshClientData* state;
    if (!self || !self->m_data || (!data && size)) return false;
    state = XSSH_CLIENT_DATA(self);
    if (size > sizeof(state->txOut) - state->txOutLen) return false;
    if (size) memcpy(state->txOut + state->txOutLen, data, size);
    state->txOutLen += size;
    return true;
}

static bool xssh_client_flush_output(XSshClient* self)
{
    XSshClientData* state;
    if (!self || !self->m_data || !self->m_device) return false;
    state = XSSH_CLIENT_DATA(self);
    while (state->txOutOffset < state->txOutLen) {
        int64_t written = XIODevice_write_1(self->m_device,
            (const char*)state->txOut + state->txOutOffset,
            (int64_t)(state->txOutLen - state->txOutOffset));
        if (written <= 0 || (size_t)written > state->txOutLen - state->txOutOffset)
            return false;
        state->txOutOffset += (size_t)written;
    }
    state->txOutLen = 0;
    state->txOutOffset = 0;
    return true;
}

static bool xssh_client_send_packet(XSshClient* self,
                                    const uint8_t* payload, size_t payloadLen)
{
    XSshClientData* state;
    uint8_t frame[XSSH_CLIENT_MAX_PACKET + 16];
    uint8_t encoded[XSSH_CLIENT_MAX_PACKET + 16];
    uint8_t macInput[XSSH_CLIENT_MAX_PACKET + 20];
    uint8_t mac[XSSH_CLIENT_MAC_LEN];
    size_t total;
    size_t padding;
    if (!self || !self->m_data || !payload || payloadLen > XSSH_CLIENT_MAX_PACKET - 32)
        return false;
    state = XSSH_CLIENT_DATA(self);
    total = 4 + 1 + payloadLen + 4;
    total = (total + 15u) & ~((size_t)15u);
    padding = total - (4 + 1 + payloadLen);
    xssh_client_write_u32(frame, (uint32_t)(payloadLen + 1u + padding));
    frame[4] = (uint8_t)padding;
    memcpy(frame + 5, payload, payloadLen);
    if (!XRandomGenerator_fillSecure(frame + 5 + payloadLen, padding)) return false;
    if (state->sendEncrypted) {
        xssh_client_write_u32(macInput, state->sendSeq);
        memcpy(macInput + 4, frame, total);
        if (!state->encActive || !xssh_hmac_sha256(state->macKeyC2S,
                sizeof(state->macKeyC2S), macInput, 4 + total, mac) ||
            XCryptographic_aesCtrUpdateInto(&state->encOp, (char*)encoded,
                sizeof(encoded), (XByteArrayView){ frame, (int64_t)total }).m_size !=
                (int64_t)total || !xssh_client_send_raw(self, encoded, total) ||
            !xssh_client_send_raw(self, mac, sizeof(mac))) return false;
    } else if (!xssh_client_send_raw(self, frame, total)) {
        return false;
    }
    ++state->sendSeq;
    return true;
}

static bool xssh_client_send_kexinit(XSshClient* self)
{
    XSshClientData* state;
    uint8_t payload[768];
    uint8_t kex[96], host[64], cipher[96], mac[64], cookie[16];
    size_t offset = 0;
    size_t kexLen, hostLen, cipherLen, macLen;
    const uint8_t* none = (const uint8_t*)"none";
    if (!self || !self->m_data ||
        !xssh_build_algorithm_list(xssh_kex_algorithms, kex, sizeof(kex), &kexLen) ||
        !xssh_build_algorithm_list(xssh_hostkey_algorithms, host, sizeof(host), &hostLen) ||
        !xssh_build_algorithm_list(xssh_cipher_algorithms, cipher, sizeof(cipher), &cipherLen) ||
        !xssh_build_algorithm_list(xssh_mac_algorithms, mac, sizeof(mac), &macLen) ||
        !XRandomGenerator_fillSecure(cookie, sizeof(cookie))) return false;
    state = XSSH_CLIENT_DATA(self);
    payload[offset++] = SSH_MSG_KEXINIT;
    memcpy(payload + offset, cookie, sizeof(cookie));
    offset += sizeof(cookie);
    xssh_client_write_string(payload, &offset, kex, kexLen);
    xssh_client_write_string(payload, &offset, host, hostLen);
    xssh_client_write_string(payload, &offset, cipher, cipherLen);
    xssh_client_write_string(payload, &offset, cipher, cipherLen);
    xssh_client_write_string(payload, &offset, mac, macLen);
    xssh_client_write_string(payload, &offset, mac, macLen);
    xssh_client_write_string(payload, &offset, none, 4);
    xssh_client_write_string(payload, &offset, none, 4);
    xssh_client_write_string(payload, &offset, "", 0);
    xssh_client_write_string(payload, &offset, "", 0);
    payload[offset++] = 0;
    xssh_client_write_u32(payload + offset, 0);
    offset += 4;
    if (offset > sizeof(state->clientKexInit)) return false;
    memcpy(state->clientKexInit, payload, offset);
    state->clientKexInitLen = offset;
    return xssh_client_send_packet(self, payload, offset);
}

static bool xssh_client_check_kexinit(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    const uint8_t* list;
    size_t listLen;
    size_t offset = 1 + 16;
    if (!state || state->serverKexInitLen < offset ||
        !xssh_client_get_string(state->serverKexInit, state->serverKexInitLen,
                                &offset, &list, &listLen)) {
        return false;
    }
    /* The client preference is the shared table; select the first entry from it
     * that is present in the server's list. */
    state->kexAlgorithm = XSshKexAlgorithm_None;
    {
        size_t i;
        for (i = 0; xssh_kex_algorithms[i].name; ++i) {
            if (xssh_name_list_contains(list, listLen, xssh_kex_algorithms[i].name)) {
                state->kexAlgorithm = (XSshKexAlgorithm)xssh_kex_algorithms[i].value;
                break;
            }
        }
    }
    if (state->kexAlgorithm == XSshKexAlgorithm_None) return false;
    if (!xssh_client_get_string(state->serverKexInit, state->serverKexInitLen,
                                &offset, &list, &listLen)) return false;
    {
        bool hostKeySupported = false;
        size_t i;
        for (i = 0; xssh_hostkey_algorithms[i].name; ++i) {
            if (xssh_name_list_contains(list, listLen, xssh_hostkey_algorithms[i].name)) {
                hostKeySupported = true;
                break;
            }
        }
        if (!hostKeySupported) return false;
    }
    if (!xssh_client_get_string(state->serverKexInit, state->serverKexInitLen,
                                &offset, &list, &listLen)) return false;
    state->cipherC2S = XSshCipherAlgorithm_None;
    {
        size_t i;
        for (i = 0; xssh_cipher_algorithms[i].name; ++i) {
            if (xssh_name_list_contains(list, listLen, xssh_cipher_algorithms[i].name)) {
                state->cipherC2S = (XSshCipherAlgorithm)xssh_cipher_algorithms[i].value;
                break;
            }
        }
    }
    if (state->cipherC2S == XSshCipherAlgorithm_None) return false;
    if (!xssh_client_get_string(state->serverKexInit, state->serverKexInitLen,
                                &offset, &list, &listLen)) return false;
    state->cipherS2C = XSshCipherAlgorithm_None;
    {
        size_t i;
        for (i = 0; xssh_cipher_algorithms[i].name; ++i) {
            if (xssh_name_list_contains(list, listLen, xssh_cipher_algorithms[i].name)) {
                state->cipherS2C = (XSshCipherAlgorithm)xssh_cipher_algorithms[i].value;
                break;
            }
        }
    }
    if (state->cipherS2C == XSshCipherAlgorithm_None) return false;
    if (!xssh_client_get_string(state->serverKexInit, state->serverKexInitLen,
                                &offset, &list, &listLen) ||
        !xssh_name_list_contains(list, listLen, "hmac-sha2-256")) return false;
    if (!xssh_client_get_string(state->serverKexInit, state->serverKexInitLen,
                                &offset, &list, &listLen) ||
        !xssh_name_list_contains(list, listLen, "hmac-sha2-256")) return false;
    if (!xssh_client_get_string(state->serverKexInit, state->serverKexInitLen,
                                &offset, &list, &listLen) ||
        !xssh_name_list_contains(list, listLen, "none")) return false;
    if (!xssh_client_get_string(state->serverKexInit, state->serverKexInitLen,
                                &offset, &list, &listLen) ||
        !xssh_name_list_contains(list, listLen, "none")) return false;
    return true;
}

static bool xssh_client_store_shared_secret(XSshClient* self,
                                            const uint8_t* shared, size_t sharedLen)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    size_t start = 0;
    size_t length;
    if (!state || !shared || sharedLen == 0 || sharedLen > sizeof(state->sharedSecret))
        return false;
    while (start + 1u < sharedLen && shared[start] == 0) ++start;
    length = sharedLen - start;
    if (shared[start] & 0x80u) {
        if (length + 1u > sizeof(state->sharedSecret)) return false;
        state->sharedSecret[0] = 0;
        memcpy(state->sharedSecret + 1u, shared + start, length);
        ++length;
    } else {
        memcpy(state->sharedSecret, shared + start, length);
    }
    state->sharedSecretLen = length;
    return true;
}

static bool xssh_client_derive_key(XSshClient* self, char letter,
                                   uint8_t* output, size_t outputLen)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t input[4 + 64 + 32 + 1 + 32];
    uint8_t digest[32];
    size_t offset = 0;
    if (!state || !output || outputLen > sizeof(digest) ||
        state->sharedSecretLen > 64) return false;
    xssh_client_write_u32(input + offset, (uint32_t)state->sharedSecretLen);
    offset += 4;
    memcpy(input + offset, state->sharedSecret, state->sharedSecretLen);
    offset += state->sharedSecretLen;
    memcpy(input + offset, state->exchangeHash, 32);
    offset += 32;
    input[offset++] = (uint8_t)letter;
    memcpy(input + offset, state->sessionId, 32);
    offset += 32;
    if (!xssh_hash_sha256(input, offset, digest))
        return false;
    memcpy(output, digest, outputLen);
    return true;
}

static bool xssh_client_setup_keys(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t ivC2S[16], ivS2C[16];
    uint8_t keyC2S[XSSH_CLIENT_MAX_CIPHER_KEY_LEN];
    uint8_t keyS2C[XSSH_CLIENT_MAX_CIPHER_KEY_LEN];
    uint8_t macC2S[32], macS2C[32];
    size_t keyC2SLen = xssh_cipher_key_size(state->cipherC2S);
    size_t keyS2CLen = xssh_cipher_key_size(state->cipherS2C);
    if (!state || keyC2SLen == 0 || keyS2CLen == 0 ||
        !xssh_client_derive_key(self, 'A', ivC2S, sizeof(ivC2S)) ||
        !xssh_client_derive_key(self, 'B', ivS2C, sizeof(ivS2C)) ||
        !xssh_client_derive_key(self, 'C', keyC2S, keyC2SLen) ||
        !xssh_client_derive_key(self, 'D', keyS2C, keyS2CLen) ||
        !xssh_client_derive_key(self, 'E', macC2S, sizeof(macC2S)) ||
        !xssh_client_derive_key(self, 'F', macS2C, sizeof(macS2C))) return false;
    if (!XCryptographic_aesCtrImportKey((XByteArrayView){ keyC2S, (int64_t)keyC2SLen },
                                            &state->cipherKeyC2S) ||
        !XCryptographic_aesCtrImportKey((XByteArrayView){ keyS2C, (int64_t)keyS2CLen },
                                            &state->cipherKeyS2C))
        return false;
    memcpy(state->macKeyC2S, macC2S, sizeof(macC2S));
    memcpy(state->macKeyS2C, macS2C, sizeof(macS2C));
    if (!XCryptographic_aesCtrSetup(&state->encOp, state->cipherKeyC2S, true,
                                        (XByteArrayView){ ivC2S, sizeof(ivC2S) }) ||
        !XCryptographic_aesCtrSetup(&state->decOp, state->cipherKeyS2C, false,
                                        (XByteArrayView){ ivS2C, sizeof(ivS2C) })) return false;
    state->encActive = true;
    state->decActive = true;
    return true;
}

static bool xssh_client_parse_mpint(const uint8_t* data, size_t dataLen,
                                    uint8_t output[32])
{
    size_t start = 0;
    if (!data || !output || dataLen == 0 || dataLen > 33) return false;
    if (dataLen > 1 && data[0] == 0) start = 1;
    if (dataLen - start > 32) return false;
    memset(output, 0, 32);
    memcpy(output + 32 - (dataLen - start), data + start, dataLen - start);
    return true;
}

static bool xssh_client_parse_ecdsa_signature(const uint8_t* data, size_t dataLen,
                                              uint8_t output[64])
{
    size_t offset = 0;
    const uint8_t* value;
    size_t valueLen;
    if (!xssh_client_get_string(data, dataLen, &offset, &value, &valueLen) ||
        !xssh_client_parse_mpint(value, valueLen, output)) return false;
    if (!xssh_client_get_string(data, dataLen, &offset, &value, &valueLen) ||
        !xssh_client_parse_mpint(value, valueLen, output + 32)) return false;
    return offset == dataLen;
}

static bool xssh_client_parse_host_key(XSshClient* self,
                                       const uint8_t* blob, size_t blobLen)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    const uint8_t* algorithm;
    const uint8_t* curve;
    const uint8_t* point;
    size_t algorithmLen, curveLen, pointLen;
    if (!state || !blob || blobLen > sizeof(state->hostKeyBlob)) return false;
    {
        size_t offset = 0;
        if (!xssh_client_get_string(blob, blobLen, &offset, &algorithm, &algorithmLen) ||
            !xssh_client_get_string(blob, blobLen, &offset, &curve, &curveLen) ||
            !xssh_client_get_string(blob, blobLen, &offset, &point, &pointLen) ||
            offset != blobLen || algorithmLen != strlen("ecdsa-sha2-nistp256") ||
            memcmp(algorithm, "ecdsa-sha2-nistp256", algorithmLen) != 0 ||
            curveLen != strlen("nistp256") || memcmp(curve, "nistp256", curveLen) != 0 ||
            pointLen != XSSH_CLIENT_POINT_LEN || point[0] != 0x04) return false;
        memcpy(state->hostKeyBlob, blob, blobLen);
        state->hostKeyBlobLen = blobLen;
        return XCryptographic_ecdsaP256ImportPublicKey(
            (XByteArrayView){ point, (int64_t)pointLen }, &state->hostPublicKey);
    }
}

static bool xssh_client_build_exchange_hash(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t input[XSSH_CLIENT_RX_CAPACITY * 2 + 512];
    size_t offset = 0;
    if (!state || state->serverVersionLen == 0 || state->serverKexInitLen == 0 ||
        state->clientKexInitLen == 0) return false;
    xssh_client_write_string(input, &offset, XSSH_CLIENT_VERSION,
                             strlen(XSSH_CLIENT_VERSION) - 2u);
    xssh_client_write_string(input, &offset, state->serverVersion,
                             state->serverVersionLen >= 2 ? state->serverVersionLen - 2u : state->serverVersionLen);
    xssh_client_write_string(input, &offset, state->clientKexInit,
                             state->clientKexInitLen);
    xssh_client_write_string(input, &offset, state->serverKexInit,
                             state->serverKexInitLen);
    xssh_client_write_string(input, &offset, state->hostKeyBlob,
                             state->hostKeyBlobLen);
    xssh_client_write_string(input, &offset, state->clientPublicBlob,
                             state->clientPublicBlobLen);
    xssh_client_write_string(input, &offset, state->serverPublicBlob,
                             state->serverPublicBlobLen);
    xssh_client_write_string(input, &offset, state->sharedSecret,
                             state->sharedSecretLen);
    if (!xssh_hash_sha256(input, offset, state->exchangeHash)) return false;
    memcpy(state->sessionId, state->exchangeHash, sizeof(state->sessionId));
    return true;
}

static bool xssh_client_verify_host_signature(XSshClient* self,
                                              const uint8_t* signature,
                                              size_t signatureLen)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    const uint8_t* algorithm;
    const uint8_t* signatureBlob;
    size_t algorithmLen, signatureBlobLen;
    uint8_t rawSignature[64];
    uint8_t signatureHash[32];
    size_t offset = 0;
    if (!state || !xssh_client_get_string(signature, signatureLen, &offset,
                                          &algorithm, &algorithmLen) ||
        !xssh_client_get_string(signature, signatureLen, &offset,
                                &signatureBlob, &signatureBlobLen) ||
        offset != signatureLen || algorithmLen != strlen("ecdsa-sha2-nistp256") ||
        memcmp(algorithm, "ecdsa-sha2-nistp256", algorithmLen) != 0 ||
        !xssh_client_parse_ecdsa_signature(signatureBlob, signatureBlobLen, rawSignature))
        return false;
    if (!xssh_hash_sha256(state->exchangeHash, sizeof(state->exchangeHash),
                          signatureHash)) return false;
    return XCryptographic_ecdsaP256VerifyHash(
        state->hostPublicKey,
        (XByteArrayView){ signatureHash, sizeof(signatureHash) },
        (XByteArrayView){ rawSignature, sizeof(rawSignature) });
}

static bool xssh_client_send_newkeys(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t payload = SSH_MSG_NEWKEYS;
    if (!xssh_client_send_packet(self, &payload, 1)) return false;
    state->sendEncrypted = true;
    state->state = XSshClientState_NewKeys;
    return true;
}

static bool xssh_client_send_service_request(XSshClient* self)
{
    uint8_t payload[64];
    size_t offset = 0;
    payload[offset++] = SSH_MSG_SERVICE_REQUEST;
    xssh_client_write_string(payload, &offset, "ssh-userauth", 12);
    return xssh_client_send_packet(self, payload, offset);
}

static bool xssh_client_send_password_request(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t payload[512];
    size_t offset = 0;
    payload[offset++] = SSH_MSG_USERAUTH_REQUEST;
    xssh_client_write_string(payload, &offset, state->username, state->usernameLen);
    xssh_client_write_string(payload, &offset, "ssh-connection", 14);
    xssh_client_write_string(payload, &offset, "password", 8);
    payload[offset++] = 0;
    xssh_client_write_string(payload, &offset, state->password, state->passwordLen);
    return xssh_client_send_packet(self, payload, offset);
}

static bool xssh_client_send_channel_open(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t payload[128];
    size_t offset = 0;
    payload[offset++] = SSH_MSG_CHANNEL_OPEN;
    xssh_client_write_string(payload, &offset, "session", 7);
    xssh_client_write_u32(payload + offset, state->localChannel);
    offset += 4;
    xssh_client_write_u32(payload + offset, XSSH_CLIENT_INITIAL_WINDOW);
    offset += 4;
    xssh_client_write_u32(payload + offset, XSSH_CLIENT_MAX_PACKET_SIZE);
    offset += 4;
    return xssh_client_send_packet(self, payload, offset);
}

static bool xssh_client_send_pty_request(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t payload[160];
    size_t offset = 0;
    payload[offset++] = SSH_MSG_CHANNEL_REQUEST;
    xssh_client_write_u32(payload + offset, state->remoteChannel);
    offset += 4;
    xssh_client_write_string(payload, &offset, "pty-req", 7);
    payload[offset++] = 1;
    xssh_client_write_string(payload, &offset, "xterm", 5);
    xssh_client_write_u32(payload + offset, 80); offset += 4;
    xssh_client_write_u32(payload + offset, 24); offset += 4;
    xssh_client_write_u32(payload + offset, 0); offset += 4;
    xssh_client_write_u32(payload + offset, 0); offset += 4;
    xssh_client_write_string(payload, &offset, "", 0);
    return xssh_client_send_packet(self, payload, offset);
}

static bool xssh_client_send_shell_request(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t payload[64];
    size_t offset = 0;
    payload[offset++] = SSH_MSG_CHANNEL_REQUEST;
    xssh_client_write_u32(payload + offset, state->remoteChannel);
    offset += 4;
    xssh_client_write_string(payload, &offset, "shell", 5);
    payload[offset++] = 1;
    return xssh_client_send_packet(self, payload, offset);
}

static bool xssh_client_send_channel_adjust(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t payload[32];
    size_t offset = 0;
    uint32_t amount = XSSH_CLIENT_INITIAL_WINDOW - state->localWindow;
    if (!amount) return true;
    payload[offset++] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
    xssh_client_write_u32(payload + offset, state->remoteChannel); offset += 4;
    xssh_client_write_u32(payload + offset, amount); offset += 4;
    state->localWindow += amount;
    return xssh_client_send_packet(self, payload, offset);
}

static size_t xssh_client_pending_size(const XSshClientData* state)
{
    return state && state->pendingOffset <= state->pendingLen ?
        state->pendingLen - state->pendingOffset : 0;
}

static bool xssh_client_send_channel_chunk(XSshClient* self,
                                           const uint8_t* data, size_t length)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t payload[XSSH_CLIENT_MAX_PACKET];
    size_t offset = 0;
    if (!state || !data || length == 0 || length > state->remoteWindow ||
        length > state->remoteMaxPacket) return false;
    payload[offset++] = SSH_MSG_CHANNEL_DATA;
    xssh_client_write_u32(payload + offset, state->remoteChannel); offset += 4;
    xssh_client_write_string(payload, &offset, data, length);
    if (!xssh_client_send_packet(self, payload, offset)) return false;
    state->remoteWindow -= (uint32_t)length;
    return true;
}

static bool xssh_client_flush_pending(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    if (!state) return false;
    while (xssh_client_pending_size(state) && state->remoteWindow &&
           !state->closed && state->channelOpen) {
        size_t chunk = xssh_client_pending_size(state);
        if (chunk > state->remoteWindow) chunk = state->remoteWindow;
        if (chunk > state->remoteMaxPacket) chunk = state->remoteMaxPacket;
        if (!xssh_client_send_channel_chunk(self,
                state->pending + state->pendingOffset, chunk)) break;
        state->pendingOffset += chunk;
    }
    if (state->pendingOffset == state->pendingLen)
        state->pendingOffset = state->pendingLen = 0;
    return !state->closed;
}

static XProtocolResult xssh_client_handle_kex_reply(XSshClient* self,
                                                    const uint8_t* payload,
                                                    size_t length)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    size_t offset = 1;
    const uint8_t* hostKey;
    const uint8_t* serverPublic;
    const uint8_t* signature;
    size_t hostKeyLen, serverPublicLen, signatureLen;
    size_t expectedLen;
    uint8_t shared[64];
    size_t sharedLen;
    if (!state || !xssh_client_get_string(payload, length, &offset, &hostKey, &hostKeyLen) ||
        !xssh_client_get_string(payload, length, &offset, &serverPublic, &serverPublicLen) ||
        !xssh_client_get_string(payload, length, &offset, &signature, &signatureLen) ||
        offset != length) return XProtocolResult_Failed;
    expectedLen = state->kexAlgorithm == XSshKexAlgorithm_Curve25519Sha256 ?
        XSSH_CLIENT_CURVE25519_PUBLIC_LEN : XSSH_CLIENT_POINT_LEN;
    if (serverPublicLen != expectedLen ||
        (state->kexAlgorithm == XSshKexAlgorithm_EcdhSha2Nistp256 && serverPublic[0] != 0x04))
        return XProtocolResult_Failed;
    if (!xssh_client_parse_host_key(self, hostKey, hostKeyLen)) return XProtocolResult_Failed;
    XSshClient_setHostKeyAccepted(self, false);
    XSshClient_hostKeyVerificationRequested_signal(self, "ecdsa-sha2-nistp256",
                                                   state->hostKeyBlob,
                                                   state->hostKeyBlobLen);
    if (!self->m_hostKeyAccepted) return XProtocolResult_PermissionDenied;
    memcpy(state->serverPublicBlob, serverPublic, serverPublicLen);
    state->serverPublicBlobLen = serverPublicLen;
    /* ecdhKey was generated before sending SSH_MSG_KEX_ECDH_INIT. */
    sharedLen = (size_t)XCryptographic_ecdhAgreeInto(
        (char*)shared, sizeof(shared), state->ecdhKey,
        (XByteArrayView){ serverPublic, (int64_t)serverPublicLen }).m_size;
    if (sharedLen == 0) return XProtocolResult_Failed;
    if (state->kexAlgorithm == XSshKexAlgorithm_Curve25519Sha256) {
        uint8_t any = 0;
        size_t i;
        for (i = 0; i < sharedLen; ++i) any |= shared[i];
        if (any == 0) return XProtocolResult_Failed;
    }
    if (!xssh_client_store_shared_secret(self, shared, sharedLen) ||
        !xssh_client_build_exchange_hash(self) ||
        !xssh_client_verify_host_signature(self, signature, signatureLen) ||
        !xssh_client_setup_keys(self) || !xssh_client_send_newkeys(self))
        return XProtocolResult_Failed;
    return XProtocolResult_Ok;
}

static XProtocolResult xssh_client_handle_channel(XSshClient* self,
                                                  const uint8_t* payload,
                                                  size_t length)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    size_t offset = 1;
    uint32_t channel;
    const uint8_t* data;
    size_t dataLen;
    if (!state || length < 5) return XProtocolResult_Failed;
    channel = xssh_client_get_u32(payload + offset);
    offset += 4;
    switch (payload[0]) {
    case SSH_MSG_CHANNEL_OPEN_CONFIRMATION:
        if (length < 17 || channel != state->localChannel) return XProtocolResult_Failed;
        state->remoteChannel = xssh_client_get_u32(payload + offset); offset += 4;
        state->remoteWindow = xssh_client_get_u32(payload + offset); offset += 4;
        state->remoteMaxPacket = xssh_client_get_u32(payload + offset); offset += 4;
        if (state->remoteMaxPacket == 0) return XProtocolResult_Failed;
        state->channelOpen = true;
        state->channelRequestStep = 0;
        return xssh_client_send_pty_request(self) ? XProtocolResult_Ok : XProtocolResult_IoError;
    case SSH_MSG_CHANNEL_OPEN_FAILURE:
        return XProtocolResult_Failed;
    case SSH_MSG_CHANNEL_SUCCESS:
        if (channel != state->remoteChannel) return XProtocolResult_Failed;
        if (state->channelRequestStep == 0) {
            state->channelRequestStep = 1;
            return xssh_client_send_shell_request(self) ? XProtocolResult_Ok : XProtocolResult_IoError;
        }
        state->state = XSshClientState_Ready;
        XSshClient_ready_signal(self);
        return XProtocolResult_Ok;
    case SSH_MSG_CHANNEL_FAILURE:
        return XProtocolResult_Failed;
    case SSH_MSG_CHANNEL_WINDOW_ADJUST:
        if (channel != state->localChannel || length < offset + 4) return XProtocolResult_Failed;
        {
            uint32_t amount = xssh_client_get_u32(payload + offset);
            if (UINT32_MAX - state->remoteWindow < amount) return XProtocolResult_ResourceLimit;
            state->remoteWindow += amount;
        }
        return xssh_client_flush_pending(self) ? XProtocolResult_Ok : XProtocolResult_IoError;
    case SSH_MSG_CHANNEL_DATA:
        if (channel != state->localChannel ||
            !xssh_client_get_string(payload, length, &offset, &data, &dataLen) ||
            offset != length || dataLen > state->localWindow) return XProtocolResult_Failed;
        state->localWindow -= (uint32_t)dataLen;
        if (!xssh_client_emit_data(self, data, dataLen) ||
            (state->localWindow < XSSH_CLIENT_WINDOW_LOW_WATER &&
             !xssh_client_send_channel_adjust(self))) return XProtocolResult_IoError;
        return XProtocolResult_Ok;
    case SSH_MSG_CHANNEL_EXTENDED_DATA:
        return XProtocolResult_Ok;
    case SSH_MSG_CHANNEL_EOF:
        if (channel != state->localChannel) return XProtocolResult_Failed;
        state->channelEof = true;
        return XProtocolResult_Ok;
    case SSH_MSG_CHANNEL_CLOSE:
        if (channel != state->localChannel) return XProtocolResult_Failed;
        if (!state->channelClose) {
            uint8_t closePayload[5] = { SSH_MSG_CHANNEL_CLOSE, 0, 0, 0, 0 };
            xssh_client_write_u32(closePayload + 1, state->remoteChannel);
            state->channelClose = true;
            if (!xssh_client_send_packet(self, closePayload, sizeof(closePayload)))
                return XProtocolResult_IoError;
        }
        state->closed = true;
        XSshClient_closed_signal(self);
        return XProtocolResult_Ok;
    default:
        return XProtocolResult_Ok;
    }
}

static XProtocolResult xssh_client_handle_payload(XSshClient* self,
                                                  const uint8_t* payload,
                                                  size_t length)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    uint8_t message;
    if (!state || !payload || length == 0) return XProtocolResult_Failed;
    message = payload[0];
    if (message == SSH_MSG_DISCONNECT) {
        state->closed = true;
        return XProtocolResult_Ok;
    }
    if (message == SSH_MSG_IGNORE || message == SSH_MSG_DEBUG ||
        message == SSH_MSG_UNIMPLEMENTED || message == SSH_MSG_EXT_INFO)
        return XProtocolResult_Ok;
    switch (state->state) {
    case XSshClientState_KexInit:
        if (message != SSH_MSG_KEXINIT || length > sizeof(state->serverKexInit))
            return XProtocolResult_Failed;
        memcpy(state->serverKexInit, payload, length);
        state->serverKexInitLen = length;
        if (!xssh_client_check_kexinit(self)) return XProtocolResult_Failed;
        state->state = XSshClientState_KexEcdh;
        {
            uint8_t request[80];
            size_t offset = 0;
            XCryptographic_EcdhAlgorithm algorithm =
                state->kexAlgorithm == XSshKexAlgorithm_Curve25519Sha256 ?
                XCryptographic_EcdhAlgorithm_X25519 :
                XCryptographic_EcdhAlgorithm_NistP256;
            if (!XCryptographic_ecdhGenerateKey(algorithm, &state->ecdhKey) ||
                (state->clientPublicBlobLen = (size_t)
                 XCryptographic_exportPublicKeyInto(
                     (char*)state->clientPublicBlob,
                     sizeof(state->clientPublicBlob), state->ecdhKey).m_size) == 0)
                return XProtocolResult_Failed;
            request[offset++] = SSH_MSG_KEX_ECDH_INIT;
            xssh_client_write_string(request, &offset, state->clientPublicBlob,
                                     state->clientPublicBlobLen);
            if (!xssh_client_send_packet(self, request, offset)) return XProtocolResult_IoError;
        }
        return XProtocolResult_Ok;
    case XSshClientState_KexEcdh:
        if (message != SSH_MSG_KEX_ECDH_REPLY) return XProtocolResult_Failed;
        return xssh_client_handle_kex_reply(self, payload, length);
    case XSshClientState_NewKeys:
        if (message != SSH_MSG_NEWKEYS) return XProtocolResult_Failed;
        state->recvEncrypted = true;
        state->state = XSshClientState_Service;
        return xssh_client_send_service_request(self) ? XProtocolResult_Ok : XProtocolResult_IoError;
    case XSshClientState_Service:
        if (message != SSH_MSG_SERVICE_ACCEPT) return XProtocolResult_Failed;
        state->state = XSshClientState_Userauth;
        return xssh_client_send_password_request(self) ? XProtocolResult_Ok : XProtocolResult_IoError;
    case XSshClientState_Userauth:
        if (message == SSH_MSG_USERAUTH_SUCCESS) {
            state->authenticated = true;
            memset(state->password, 0, sizeof(state->password));
            state->passwordLen = 0;
            state->state = XSshClientState_ChannelOpen;
            XSshClient_authenticated_signal(self);
            return xssh_client_send_channel_open(self) ? XProtocolResult_Ok : XProtocolResult_IoError;
        }
        return message == SSH_MSG_USERAUTH_FAILURE ? XProtocolResult_PermissionDenied : XProtocolResult_Failed;
    case XSshClientState_ChannelOpen:
    case XSshClientState_ChannelRequest:
    case XSshClientState_Ready:
        return xssh_client_handle_channel(self, payload, length);
    default:
        return XProtocolResult_Failed;
    }
}

static XProtocolResult xssh_client_process_one(XSshClient* self)
{
    XSshClientData* state = XSSH_CLIENT_DATA(self);
    if (state->state == XSshClientState_Version) {
        const uint8_t* newline = (const uint8_t*)memchr(state->rxBuf, '\n', state->rxLen);
        size_t lineLen;
        if (!newline) return state->rxLen > XSSH_CLIENT_VERSION_MAX ?
            XProtocolResult_ResourceLimit : XProtocolResult_Ok;
        lineLen = (size_t)(newline - state->rxBuf) + 1u;
        if (lineLen < 9 || (memcmp(state->rxBuf, "SSH-2.0-", 8) != 0 &&
                            memcmp(state->rxBuf, "SSH-1.99-", 9) != 0))
            return XProtocolResult_Failed;
        memcpy(state->serverVersion, state->rxBuf, lineLen);
        state->serverVersionLen = lineLen;
        memmove(state->rxBuf, state->rxBuf + lineLen, state->rxLen - lineLen);
        state->rxLen -= lineLen;
        state->state = XSshClientState_KexInit;
        if (!xssh_client_send_kexinit(self)) return XProtocolResult_IoError;
        return XProtocolResult_Ok;
    }
    if (state->recvEncrypted) {
        uint8_t plain[XSSH_CLIENT_RX_CAPACITY + 4];
        uint8_t macInput[XSSH_CLIENT_RX_CAPACITY + 8];
        uint8_t calculated[XSSH_CLIENT_MAC_LEN];
        size_t totalCipher, padding, payloadLen;
        if (!state->packetLenKnown) {
            if (state->rxLen < 4) return XProtocolResult_Ok;
            if (XCryptographic_aesCtrUpdateInto(&state->decOp,
                    (char*)state->lenPlain, sizeof(state->lenPlain),
                    (XByteArrayView){ state->rxBuf, 4 }).m_size != 4)
                return XProtocolResult_Failed;
            state->packetLen = xssh_client_get_u32(state->lenPlain);
            if (state->packetLen < 5 || state->packetLen > XSSH_CLIENT_MAX_PACKET ||
                ((state->packetLen + 4u) & 15u) != 0u) return XProtocolResult_Failed;
            state->packetLenKnown = true;
        }
        totalCipher = 4u + state->packetLen;
        if (state->rxLen < totalCipher + XSSH_CLIENT_MAC_LEN) return XProtocolResult_Ok;
        if (XCryptographic_aesCtrUpdateInto(&state->decOp,
                (char*)(plain + 4), sizeof(plain) - 4u,
                (XByteArrayView){ state->rxBuf + 4, (int64_t)(totalCipher - 4u) }).m_size !=
            (int64_t)(totalCipher - 4u)) return XProtocolResult_Failed;
        memcpy(plain, state->lenPlain, 4);
        xssh_client_write_u32(macInput, state->recvSeq);
        memcpy(macInput + 4, plain, totalCipher);
        if (!xssh_hmac_sha256(state->macKeyS2C, sizeof(state->macKeyS2C),
                              macInput, 4 + totalCipher, calculated) ||
            !xssh_client_const_equal(calculated, state->rxBuf + totalCipher,
                                     XSSH_CLIENT_MAC_LEN)) return XProtocolResult_Failed;
        memmove(state->rxBuf, state->rxBuf + totalCipher + XSSH_CLIENT_MAC_LEN,
                state->rxLen - totalCipher - XSSH_CLIENT_MAC_LEN);
        state->rxLen -= totalCipher + XSSH_CLIENT_MAC_LEN;
        state->packetLenKnown = false;
        ++state->recvSeq;
        padding = plain[4];
        if (padding < 4 || padding >= state->packetLen) return XProtocolResult_Failed;
        payloadLen = state->packetLen - padding - 1u;
        if (payloadLen > sizeof(state->payloadBuf)) return XProtocolResult_ResourceLimit;
        memcpy(state->payloadBuf, plain + 5, payloadLen);
        return xssh_client_handle_payload(self, state->payloadBuf, payloadLen);
    }
    if (state->rxLen < 4) return XProtocolResult_Ok;
    {
        uint32_t packetLength = xssh_client_get_u32(state->rxBuf);
        size_t padding;
        size_t payloadLen;
        if (packetLength < 5 || packetLength > XSSH_CLIENT_MAX_PACKET) return XProtocolResult_Failed;
        if (state->rxLen < 4u + packetLength) return XProtocolResult_Ok;
        padding = state->rxBuf[4];
        if (padding < 4 || padding >= packetLength) return XProtocolResult_Failed;
        payloadLen = packetLength - padding - 1u;
        if (payloadLen > sizeof(state->payloadBuf)) return XProtocolResult_ResourceLimit;
        memcpy(state->payloadBuf, state->rxBuf + 5, payloadLen);
        memmove(state->rxBuf, state->rxBuf + 4u + packetLength,
                state->rxLen - 4u - packetLength);
        state->rxLen -= 4u + packetLength;
        ++state->recvSeq;
        return xssh_client_handle_payload(self, state->payloadBuf, payloadLen);
    }
}

static void xssh_client_device_ready_read(XObject* receiver, XVarList* args)
{
    XSshClient* self = (XSshClient*)receiver;
    uint8_t buffer[1024];
    (void)args;
    if (!self || !self->m_device || !self->m_data || XSSH_CLIENT_DATA(self)->closed) return;
    for (;;) {
        int64_t n = XIODevice_read_1(self->m_device, (char*)buffer,
                                     (int64_t)sizeof(buffer));
        if (n <= 0) break;
        if (XSshClient_feedData(self, buffer, (size_t)n) < XProtocolResult_Ok) break;
    }
}

bool XSshClient_setDevice(XSshClient* self, XIODevice* device)
{
    XSshClientData* state;
    if (!self || !device || !self->m_data) return false;
    if (self->m_device == device) return true;
    XSshClient_stop(self);
    self->m_device = device;
    state = XSSH_CLIENT_DATA(self);
    xssh_client_cleanup_keys(self);
    memset(state, 0, sizeof(*state));
    state->state = XSshClientState_Version;
    state->localChannel = 0;
    state->localWindow = XSSH_CLIENT_INITIAL_WINDOW;
    state->localMaxPacket = XSSH_CLIENT_MAX_PACKET_SIZE;
    self->m_hostKeyAccepted = false;
    self->m_readyRead = XObject_connect_1((XObject*)device,
        XSignal(XIODevice_readyRead_signal), (XObject*)self,
        xssh_client_device_ready_read, XConnectionType_Direct);
    return self->m_readyRead != NULL;
}

bool XSshClient_setCredentials(XSshClient* self, const char* user,
                               const char* password)
{
    XSshClientData* state;
    size_t userLen, passwordLen;
    if (!self || !self->m_data || !user || !password ||
        XSSH_CLIENT_DATA(self)->state != XSshClientState_Version) return false;
    userLen = strlen(user);
    passwordLen = strlen(password);
    if (userLen == 0 || userLen >= XSSH_LOGIN_NAME_SIZE ||
        passwordLen >= XSSH_LOGIN_PASSWORD_SIZE) return false;
    state = XSSH_CLIENT_DATA(self);
    memcpy(state->username, user, userLen);
    state->username[userLen] = 0;
    state->usernameLen = userLen;
    memcpy(state->password, password, passwordLen);
    state->password[passwordLen] = 0;
    state->passwordLen = passwordLen;
    return true;
}

bool XSshClient_start(XSshClient* self)
{
    XSshClientData* state;
    if (!self || !self->m_device || !self->m_data) return false;
    state = XSSH_CLIENT_DATA(self);
    if (state->usernameLen == 0 || state->state != XSshClientState_Version) return false;
    state->closed = false;
    if (!xssh_client_send_raw(self, (const uint8_t*)XSSH_CLIENT_VERSION,
                              strlen(XSSH_CLIENT_VERSION)) ||
        !xssh_client_flush_output(self) || !XIODevice_flush(self->m_device)) {
        state->closed = true;
        return false;
    }
    return true;
}

void XSshClient_stop(XSshClient* self)
{
    if (!self) return;
    if (self->m_readyRead) {
        XObject_disconnect_2(self->m_readyRead);
        self->m_readyRead = NULL;
    }
    if (self->m_data && !XSSH_CLIENT_DATA(self)->closed) {
        XSSH_CLIENT_DATA(self)->closed = true;
        XSshClient_closed_signal(self);
    }
}

XProtocolResult XSshClient_feedData(XSshClient* self, const void* data, size_t size)
{
    XSshClientData* state;
    XProtocolResult result = XProtocolResult_Ok;
    if (!self || !self->m_data || (!data && size)) return XProtocolResult_InvalidArgument;
    state = XSSH_CLIENT_DATA(self);
    if (state->closed) return XProtocolResult_Failed;
    if (size > sizeof(state->rxBuf) - state->rxLen) return XProtocolResult_ResourceLimit;
    if (size) memcpy(state->rxBuf + state->rxLen, data, size);
    state->rxLen += size;
    while (!state->closed) {
        size_t before = state->rxLen;
        result = xssh_client_process_one(self);
        if (result < XProtocolResult_Ok) {
            state->closed = true;
            XSshClient_errorOccurred_signal(self, (int)result);
            return result;
        }
        if (before == state->rxLen) break;
    }
    if (!xssh_client_flush_output(self)) return XProtocolResult_IoError;
    return XProtocolResult_Ok;
}

int64_t XSshClient_write(XSshClient* self, const void* data, size_t size)
{
    XSshClientData* state;
    if (!self || !self->m_data || (!data && size) || !XSSH_CLIENT_DATA(self)->channelOpen ||
        XSSH_CLIENT_DATA(self)->state != XSshClientState_Ready) return -1;
    state = XSSH_CLIENT_DATA(self);
    if (state->pendingOffset) {
        size_t pendingSize = xssh_client_pending_size(state);
        if (pendingSize)
            memmove(state->pending, state->pending + state->pendingOffset, pendingSize);
        state->pendingOffset = 0;
        state->pendingLen = pendingSize;
    }
    if (size > sizeof(state->pending) - state->pendingLen) return -1;
    if (size) memcpy(state->pending + state->pendingLen, data, size);
    state->pendingLen += size;
    if (!xssh_client_flush_pending(self) || !xssh_client_flush_output(self) ||
        !XIODevice_flush(self->m_device)) return -1;
    return (int64_t)size;
}

bool XSshClient_closeChannel(XSshClient* self)
{
    XSshClientData* state;
    uint8_t payload[5];
    if (!self || !self->m_data || !XSSH_CLIENT_DATA(self)->channelOpen) return false;
    state = XSSH_CLIENT_DATA(self);
    if (!state->channelEof) {
        payload[0] = SSH_MSG_CHANNEL_EOF;
        xssh_client_write_u32(payload + 1, state->remoteChannel);
        if (!xssh_client_send_packet(self, payload, sizeof(payload))) return false;
        state->channelEof = true;
    }
    if (!state->channelClose) {
        payload[0] = SSH_MSG_CHANNEL_CLOSE;
        xssh_client_write_u32(payload + 1, state->remoteChannel);
        if (!xssh_client_send_packet(self, payload, sizeof(payload))) return false;
        state->channelClose = true;
    }
    if (!xssh_client_flush_output(self) || !XIODevice_flush(self->m_device)) return false;
    XSshClient_stop(self);
    return true;
}

bool XSshClient_flush(XSshClient* self)
{
    if (!self || !self->m_data || XSSH_CLIENT_DATA(self)->closed) return false;
    if (!xssh_client_flush_pending(self) || !xssh_client_flush_output(self)) return false;
    return self->m_device && XIODevice_flush(self->m_device);
}

bool XSshClient_isClosed(const XSshClient* self)
{
    return !self || !self->m_data || XSSH_CLIENT_DATA(self)->closed;
}

bool XSshClient_isAuthenticated(const XSshClient* self)
{
    return self && self->m_data && XSSH_CLIENT_DATA(self)->authenticated;
}

bool XSshClient_isReady(const XSshClient* self)
{
    return self && self->m_data && XSSH_CLIENT_DATA(self)->state == XSshClientState_Ready;
}

void XSshClient_setHostKeyAccepted(XSshClient* self, bool accepted)
{
    if (self) self->m_hostKeyAccepted = accepted;
}

void* XSshClient_hostKeyVerificationRequested_signal(XSshClient* self,
                                                      const char* algorithm,
                                                      const void* keyBlob,
                                                      size_t keyBlobLen)
{
    XEmitSignal(self, XSshClient_hostKeyVerificationRequested_signal,
                XVarList_Create(XVar(const char*, algorithm),
                                XVar(const void*, keyBlob),
                                XVar(size_t, keyBlobLen)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

static bool xssh_client_emit_data(XSshClient* self,
                                  const uint8_t* data, size_t size)
{
    if (!self || (!data && size)) return false;
    if (size) XSshClient_dataReceived_signal(self, data, size);
    return true;
}

void* XSshClient_dataReceived_signal(XSshClient* self,
                                     const void* data, size_t size)
{
    XEmitSignal(self, XSshClient_dataReceived_signal,
                XVarList_Create(XVar(const void*, data), XVar(size_t, size)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSshClient_authenticated_signal(XSshClient* self)
{
    XEmitSignal(self, XSshClient_authenticated_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XSshClient_ready_signal(XSshClient* self)
{
    XEmitSignal(self, XSshClient_ready_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XSshClient_closed_signal(XSshClient* self)
{
    XEmitSignal(self, XSshClient_closed_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XSshClient_errorOccurred_signal(XSshClient* self, int error)
{
    XEmitSignal(self, XSshClient_errorOccurred_signal,
                XVarList_Create(XVar(int, error)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

static void xssh_client_cleanup_keys(XSshClient* self)
{
    XSshClientData* state;
    if (!self || !self->m_data) return;
    state = XSSH_CLIENT_DATA(self);
    if (state->encActive) {
        XCryptographic_aesCtrAbort(&state->encOp);
        state->encActive = false;
    }
    if (state->decActive) {
        XCryptographic_aesCtrAbort(&state->decOp);
        state->decActive = false;
    }
    XCryptographic_destroyKey(&state->ecdhKey);
    XCryptographic_destroyKey(&state->hostPublicKey);
    XCryptographic_destroyKey(&state->cipherKeyC2S);
    XCryptographic_destroyKey(&state->cipherKeyS2C);
    memset(state->macKeyC2S, 0, sizeof(state->macKeyC2S));
    memset(state->macKeyS2C, 0, sizeof(state->macKeyS2C));
}

static void xssh_client_deinit(XSshClient* self)
{
    if (!self) return;
    XSshClient_stop(self);
    xssh_client_cleanup_keys(self);
    if (self->m_data) {
        memset(self->m_data, 0, sizeof(XSshClientData));
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, self);
}

XVtable* XSshClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSshClient)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xssh_client_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XSshClient);
    return XVTABLE_DEFAULT;
}

void XSshClient_init(XSshClient* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init(&self->m_class);
    XClassSetVtable(self, XSshClient);
    self->m_data = XMalloc_System(sizeof(XSshClientData));
    if (self->m_data) {
        memset(self->m_data, 0, sizeof(XSshClientData));
        XSSH_CLIENT_DATA(self)->state = XSshClientState_Version;
        XSSH_CLIENT_DATA(self)->localChannel = 0;
        XSSH_CLIENT_DATA(self)->localWindow = XSSH_CLIENT_INITIAL_WINDOW;
        XSSH_CLIENT_DATA(self)->localMaxPacket = XSSH_CLIENT_MAX_PACKET_SIZE;
    }
}

XSshClient* XSshClient_create(void)
{
    XSshClient* self = (XSshClient*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XSshClient_init(self);
    if (!self->m_data) {
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

#endif /* XPROTOCOL_ON && XSSH_ON && XSSH_CLIENT_ON */
