/**
 * @file XConsoleShell_XSsh.c
 * @brief XConsoleShell 的 mbedTLS(PSA) 精简 SSH Server 适配实现。
 * @details
 * 实现 SSH 2.0 传输层：版本协商、KEXINIT、ecdh-sha2-nistp256 密钥交换、
 * AES-128-CTR 加密、HMAC-SHA2-256 完整性、password 用户认证和 session 通道。
 * 适配器不创建线程、不监听端口，底层字节传输由调用方 XConsoleShellIo 回调查询；
 * 收到网络字节后由 XConsoleShellSshAdapter_feedData 驱动状态机。
 * 主机密钥首次启动生成并持久化到 XCONSOLE_SHELL_XSSH_HOSTKEY_FILE 文件，
 * 之后跨进程重启保持不变，避免客户端 known_hosts 指纹频繁变化。
 */

#include "XPrintf.h"
#include "XConsoleShell_XSsh.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON

#include "XConsoleShellLogin.h"
#include "XFileSystem.h"
#include "XMemory.h"
#include "XRandomGenerator.h"
#include "XString.h"
#include "psa/crypto.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#ifdef XCONSOLE_SHELL_XSSH_DEBUG
#define XSSH_DBG(...) do { XERROR_PRINTF("[XSSH] " __VA_ARGS__); } while (0)
#else
#define XSSH_DBG(...) do { } while (0)
#endif

/* ------------------------------------------------------------------ */
/* 常量                                                               */
/* ------------------------------------------------------------------ */

enum {
    XSSH_RX_CAPACITY = 4096,
    XSSH_TX_OUT_CAPACITY = 8192,
    XSSH_MAX_PACKET = 2048,
    XSSH_MAX_PAYLOAD = 2048,
    XSSH_HASH_LEN = 32,
    XSSH_MAC_LEN = 32,
    XSSH_POINT_LEN = 65,
    XSSH_CHANNEL_DATA_CHUNK = 1024,
    XSSH_CHANNEL_INITIAL_WINDOW = 32768,
    XSSH_CHANNEL_WINDOW_LOW_WATER = 8192,
    XSSH_CHANNEL_PENDING_CAPACITY = 32768,
    XSSH_VERSION_MAX = 256
};

/* RFC 4254 terminal mode opcode for local character echo. */
#define XSSH_TTY_OP_ECHO 53u

/* SSH 消息类型 */
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
    SSH_MSG_GLOBAL_REQUEST = 80,
    SSH_MSG_REQUEST_FAILURE = 82,
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

static const char* const XSSH_VERSION = "SSH-2.0-XinYueC_1.0\r\n";

/* ------------------------------------------------------------------ */
/* 内部状态与结构                                                      */
/* ------------------------------------------------------------------ */

typedef enum XSshState {
    XSshState_Version = 0,
    XSshState_KexInit,
    XSshState_KexEcdh,
    XSshState_NewKeys,
    XSshState_Service,
    XSshState_Userauth,
    XSshState_Channel
} XSshState;

struct XConsoleShellSshAdapter {
    XConsoleShellIo transport;          /* 底层字节传输（借用） */
    XConsoleShellIo shellIo;            /* Shell 输出回调值副本 */
    XConsoleShell* shell;               /* 目标 Shell（借用） */
    XConsoleShellSession* session;      /* 目标会话（借用） */
    XSshState state;
    bool closed;

    /* 版本与 KEX 数据 */
    uint8_t clientVersion[XSSH_VERSION_MAX];
    size_t clientVersionLen;
    uint8_t clientKexInit[XSSH_RX_CAPACITY];
    size_t clientKexInitLen;
    uint8_t serverKexInit[XSSH_RX_CAPACITY];
    size_t serverKexInitLen;

    /* 主机密钥 */
    mbedtls_svc_key_id_t hostKey;
    bool hostKeyShared;
    uint8_t hostKeyBlob[256];
    size_t hostKeyBlobLen;

    /* 服务端临时 ECDH */
    mbedtls_svc_key_id_t ecdhKey;
    uint8_t clientPublicBlob[256];
    size_t clientPublicBlobLen;
    uint8_t sharedSecret[64];
    size_t sharedSecretLen;

    /* 交换哈希与会话 ID */
    uint8_t exchangeHash[XSSH_HASH_LEN];
    uint8_t sessionId[XSSH_HASH_LEN];

    /* 会话密钥 */
    mbedtls_svc_key_id_t cipherKeyC2S;
    mbedtls_svc_key_id_t cipherKeyS2C;
    mbedtls_svc_key_id_t macKeyC2S;
    mbedtls_svc_key_id_t macKeyS2C;
    psa_cipher_operation_t encOp;
    psa_cipher_operation_t decOp;
    bool encActive;
    bool decActive;
    bool sendEncrypted;
    bool recvEncrypted;
    uint32_t sendSeq;
    uint32_t recvSeq;

    /* 接收缓冲（未加密阶段存明文包，加密阶段存密文包） */
    uint8_t rxBuf[XSSH_RX_CAPACITY];
    size_t rxLen;
    bool packetLenKnown;
    uint32_t packetLen;
    uint8_t lenPlain[4];
    uint8_t payloadBuf[XSSH_MAX_PAYLOAD];

    /* 发送缓冲 */
    uint8_t txOut[XSSH_TX_OUT_CAPACITY];
    size_t txOutLen;
    size_t txOutOffset;

    /* 通道状态 */
    uint32_t remoteChannel;
    uint32_t localChannel;
    uint32_t clientWindow;
    uint32_t clientMaxPacket;
    uint32_t serverWindow;
    uint32_t serverMaxPacket;
    uint32_t ptyColumns;
    uint32_t ptyRows;
    uint32_t ptyPixelWidth;
    uint32_t ptyPixelHeight;
    char ptyTerm[64];
    bool ptyRequested;
    bool inputEcho;
    bool echoPendingCr;
    uint8_t echoEscape; /* 0 普通，1 已收 ESC，2 正在消费 CSI/O 序列。 */
    bool channelOpen;
    bool shellStarted;
    bool authDone;
    uint8_t authAttempts;
    bool remoteEof;
    bool localEof;
    bool remoteClose;
    bool localClose;
    bool promptPendingCr;
    uint8_t channelTxPending[XSSH_CHANNEL_PENDING_CAPACITY];
    size_t channelTxPendingLen;
    size_t channelTxPendingOffset;
};

/* A volatile PSA key is shared by connections during one process lifetime.
 * This keeps the SSH host identity stable across reconnects while avoiding a
 * process-wide private-key byte buffer.  Callers still serialize PSA access
 * according to the library's normal threading contract. */
static mbedtls_svc_key_id_t g_xssh_hostKey;
static uint8_t g_xssh_hostKeyBlob[256];
static size_t g_xssh_hostKeyBlobLen;
static size_t g_xssh_hostKeyRefs;

/* ------------------------------------------------------------------ */
/* 主机密钥持久化                                                      */
/*  磁盘格式：magic(4) || version(4) || keyLen(4) || keyBytes(len)。
 *  keyBytes 为 PSA 导出的 ECC P-256 私钥标量（32 字节，大端）。     */
/* ------------------------------------------------------------------ */

#define XSSH_HOSTKEY_FILE_MAGIC   0x5853484Bu  /* 'XSHK' */
#define XSSH_HOSTKEY_FILE_VERSION 1u
#define XSSH_HOSTKEY_FILE_HEADER  12u
#define XSSH_HOSTKEY_MAX_BYTES    64u

static void xssh_write_u32(uint8_t* p, uint32_t v);
static void xssh_get_u32(const uint8_t* p, uint32_t* v);

static const char* xssh_hostkey_path(void)
{
    return XCONSOLE_SHELL_XSSH_HOSTKEY_FILE;
}

static bool xssh_hostkey_save(const uint8_t* keyBytes, size_t keyLen)
{
    XString* path;
    XFd fd;
    uint8_t header[XSSH_HOSTKEY_FILE_HEADER];
    size_t done;
    int error = 0;
    if (!keyBytes || keyLen == 0 || keyLen > XSSH_HOSTKEY_MAX_BYTES)
        return false;
    path = XString_create_utf8(xssh_hostkey_path());
    if (!path) return false;
    fd = XFileSystem_open(path, XFileSystem_WriteOnly | XFileSystem_Create |
                               XFileSystem_Truncate, &error);
    if (fd == XFD_INVALID) {
        XString_delete_base(path);
        return false;
    }
    xssh_write_u32(header, XSSH_HOSTKEY_FILE_MAGIC);
    xssh_write_u32(header + 4, XSSH_HOSTKEY_FILE_VERSION);
    xssh_write_u32(header + 8, (uint32_t)keyLen);
    done = 0;
    while (done < sizeof(header)) {
        int64_t n = XFileSystem_write(fd, header + done,
                                      (int64_t)(sizeof(header) - done));
        if (n <= 0) {
            XFileSystem_close(fd);
            XString_delete_base(path);
            return false;
        }
        done += (size_t)n;
    }
    done = 0;
    while (done < keyLen) {
        int64_t n = XFileSystem_write(fd, keyBytes + done,
                                      (int64_t)(keyLen - done));
        if (n <= 0) {
            XFileSystem_close(fd);
            XString_delete_base(path);
            return false;
        }
        done += (size_t)n;
    }
    if (!XFileSystem_flush(fd)) {
        XFileSystem_close(fd);
        XString_delete_base(path);
        return false;
    }
    XFileSystem_close(fd);
    /* 主机密钥属敏感材料，尽力限制为仅属主可读写。 */
    (void)XFileSystem_setPermissions(path, XFile_ReadOwner | XFile_WriteOwner);
    XString_delete_base(path);
    return true;
}

static bool xssh_hostkey_load(uint8_t* keyBytes, size_t keyCap, size_t* keyLen)
{
    XString* path;
    XFileStat stat;
    XFd fd;
    uint8_t header[XSSH_HOSTKEY_FILE_HEADER];
    uint32_t magic;
    uint32_t version;
    uint32_t len;
    size_t done;
    int error = 0;
    if (!keyBytes || !keyLen || keyCap < XSSH_HOSTKEY_MAX_BYTES) return false;
    path = XString_create_utf8(xssh_hostkey_path());
    if (!path) return false;
    if (!XFileSystem_stat(path, &stat) || !stat.exists || !stat.isFile ||
        stat.size < (int64_t)XSSH_HOSTKEY_FILE_HEADER ||
        stat.size > (int64_t)(XSSH_HOSTKEY_FILE_HEADER + XSSH_HOSTKEY_MAX_BYTES)) {
        XString_delete_base(path);
        return false;
    }
    fd = XFileSystem_open(path, XFileSystem_ReadOnly, &error);
    XString_delete_base(path);
    if (fd == XFD_INVALID) return false;
    done = 0;
    while (done < sizeof(header)) {
        int64_t n = XFileSystem_read(fd, header + done,
                                     (int64_t)(sizeof(header) - done));
        if (n <= 0) {
            XFileSystem_close(fd);
            return false;
        }
        done += (size_t)n;
    }
    xssh_get_u32(header, &magic);
    xssh_get_u32(header + 4, &version);
    xssh_get_u32(header + 8, &len);
    if (magic != XSSH_HOSTKEY_FILE_MAGIC ||
        version != XSSH_HOSTKEY_FILE_VERSION ||
        len == 0 || len > XSSH_HOSTKEY_MAX_BYTES ||
        (uint64_t)len != (uint64_t)(stat.size - (int64_t)XSSH_HOSTKEY_FILE_HEADER)) {
        XFileSystem_close(fd);
        return false;
    }
    done = 0;
    while (done < len) {
        int64_t n = XFileSystem_read(fd, keyBytes + done, (int64_t)(len - done));
        if (n <= 0) {
            XFileSystem_close(fd);
            return false;
        }
        done += (size_t)n;
    }
    XFileSystem_close(fd);
    *keyLen = len;
    return true;
}

/* ------------------------------------------------------------------ */
/* 基础工具函数                                                       */
/* ------------------------------------------------------------------ */

static void xssh_write_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void xssh_get_u32(const uint8_t* p, uint32_t* v)
{
    *v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool xssh_const_equal(const uint8_t* a, const uint8_t* b, size_t size)
{
    uint8_t diff = 0;
    size_t i;
    if (!a || !b) return false;
    for (i = 0; i < size; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

static void xssh_write_string(uint8_t* buf, size_t* off, const uint8_t* data, size_t len)
{
    xssh_write_u32(buf + *off, (uint32_t)len);
    *off += 4;
    if (len) memcpy(buf + *off, data, len);
    *off += len;
}

static bool xssh_get_string(const uint8_t* buf, size_t len, size_t* off,
                            const uint8_t** out, size_t* outLen)
{
    uint32_t slen;
    if (*off + 4 > len) return false;
    xssh_get_u32(buf + *off, &slen);
    *off += 4;
    if ((size_t)slen > len - *off) return false;
    *out = buf + *off;
    *outLen = (size_t)slen;
    *off += (size_t)slen;
    return true;
}

static bool xssh_name_list_contains(const uint8_t* list, size_t len, const char* name)
{
    size_t nameLen = strlen(name);
    size_t start = 0;
    while (start <= len) {
        size_t i = start;
        while (i < len && list[i] != ',') ++i;
        if (i - start == nameLen && memcmp(list + start, name, nameLen) == 0)
            return true;
        if (i >= len) break;
        start = i + 1;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* 发送路径                                                           */
/* ------------------------------------------------------------------ */

static bool xssh_send_raw(XConsoleShellSshAdapter* adapter,
                          const uint8_t* data, size_t size)
{
    if (!adapter) return false;
    if (size > sizeof(adapter->txOut) - adapter->txOutLen) {
        adapter->closed = true;
        return false;
    }
    if (size) memcpy(adapter->txOut + adapter->txOutLen, data, size);
    adapter->txOutLen += size;
    return true;
}

static bool xssh_flush(XConsoleShellSshAdapter* adapter)
{
    if (!adapter) return false;
    XSSH_DBG("flush start outLen=%u off=%u\n", (unsigned)adapter->txOutLen, (unsigned)adapter->txOutOffset);
    while (adapter->txOutOffset < adapter->txOutLen) {
        int64_t n;
        if (!adapter->transport.write) {
            adapter->closed = true;
            return false;
        }
        n = adapter->transport.write(adapter->transport.userData,
                                     adapter->txOut + adapter->txOutOffset,
                                     adapter->txOutLen - adapter->txOutOffset);
        if (n < 0) {
            adapter->closed = true;
            return false;
        }
        if (n == 0) break;
        adapter->txOutOffset += (size_t)n;
    }
    XSSH_DBG("flush end outLen=%u off=%u\n", (unsigned)adapter->txOutLen, (unsigned)adapter->txOutOffset);
    if (adapter->txOutOffset == adapter->txOutLen) {
        adapter->txOutOffset = adapter->txOutLen = 0;
    } else if (adapter->txOutOffset > 0) {
        memmove(adapter->txOut, adapter->txOut + adapter->txOutOffset,
                adapter->txOutLen - adapter->txOutOffset);
        adapter->txOutLen -= adapter->txOutOffset;
        adapter->txOutOffset = 0;
    }
    return true;
}

static bool xssh_send_packet(XConsoleShellSshAdapter* adapter,
                             const uint8_t* payload, size_t payloadLen)
{
    uint8_t frame[XSSH_MAX_PACKET + 16];
    uint8_t encoded[XSSH_MAX_PACKET + 16];
    uint8_t macInput[XSSH_MAX_PACKET + 16 + 4];
    uint8_t mac[XSSH_MAC_LEN];
    size_t total;
    size_t padLen;
    size_t outLen;
    size_t macLen;

    if (!adapter || payloadLen > XSSH_MAX_PACKET - 32) return false;

    total = 4 + 1 + payloadLen + 4;
    total = (total + 15) & ~((size_t)15);
    padLen = total - (4 + 1 + payloadLen);

    xssh_write_u32(frame, (uint32_t)(payloadLen + 1 + padLen));
    frame[4] = (uint8_t)padLen;
    memcpy(frame + 5, payload, payloadLen);
    if (!XRandomGenerator_fillSecure(frame + 5 + payloadLen, padLen)) {
        adapter->closed = true;
        return false;
    }

    if (adapter->sendEncrypted) {
        if (!adapter->encActive) {
            adapter->closed = true;
            return false;
        }
        /* SSH MAC is computed over the sequence number followed by the
         * unencrypted packet (RFC 4253 section 6.4).  CTR encryption is
         * applied only after the MAC has been computed. */
        xssh_write_u32(macInput, adapter->sendSeq);
        memcpy(macInput + 4, frame, total);
        if (psa_mac_compute(adapter->macKeyS2C, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                            macInput, 4 + total, mac, sizeof(mac), &macLen) != PSA_SUCCESS ||
            macLen != XSSH_MAC_LEN) {
            adapter->closed = true;
            return false;
        }
        if (psa_cipher_update(&adapter->encOp, frame, total, encoded,
                              sizeof(encoded), &outLen) != PSA_SUCCESS ||
            outLen != total) {
            adapter->closed = true;
            return false;
        }
        if (!xssh_send_raw(adapter, encoded, total)) return false;
        if (!xssh_send_raw(adapter, mac, macLen)) return false;
    } else {
        if (!xssh_send_raw(adapter, frame, total)) return false;
    }
    ++adapter->sendSeq;
    return true;
}

/* ------------------------------------------------------------------ */
/* 密钥派生与主机密钥                                                  */
/* ------------------------------------------------------------------ */

static bool xssh_derive_key(XConsoleShellSshAdapter* adapter, char letter,
                            uint8_t* out, size_t outLen)
{
    uint8_t input[4 + 64 + XSSH_HASH_LEN + 1 + XSSH_HASH_LEN];
    uint8_t hash[XSSH_HASH_LEN];
    size_t off = 0;
    size_t hLen;
    if (!adapter || !out || outLen > XSSH_HASH_LEN ||
        adapter->sharedSecretLen > 64)
        return false;
    /* K is an SSH mpint in the key derivation input, including its uint32
     * length prefix.  The exchange hash uses the same representation. */
    xssh_write_u32(input + off, (uint32_t)adapter->sharedSecretLen);
    off += 4;
    memcpy(input + off, adapter->sharedSecret, adapter->sharedSecretLen);
    off += adapter->sharedSecretLen;
    memcpy(input + off, adapter->exchangeHash, XSSH_HASH_LEN);
    off += XSSH_HASH_LEN;
    input[off++] = (uint8_t)letter;
    memcpy(input + off, adapter->sessionId, XSSH_HASH_LEN);
    off += XSSH_HASH_LEN;
    if (psa_hash_compute(PSA_ALG_SHA_256, input, off, hash, sizeof(hash), &hLen) != PSA_SUCCESS ||
        hLen < outLen)
        return false;
    memcpy(out, hash, outLen);
    return true;
}

static bool xssh_setup_keys(XConsoleShellSshAdapter* adapter)
{
    uint8_t ivC2S[16], ivS2C[16], encC2S[16], encS2C[16];
    uint8_t macC2S[XSSH_MAC_LEN], macS2C[XSSH_MAC_LEN];
    psa_key_attributes_t attrs;
    psa_status_t st;

    if (!xssh_derive_key(adapter, 'A', ivC2S, sizeof(ivC2S))) return false;
    if (!xssh_derive_key(adapter, 'B', ivS2C, sizeof(ivS2C))) return false;
    if (!xssh_derive_key(adapter, 'C', encC2S, sizeof(encC2S))) return false;
    if (!xssh_derive_key(adapter, 'D', encS2C, sizeof(encS2C))) return false;
    if (!xssh_derive_key(adapter, 'E', macC2S, sizeof(macC2S))) return false;
    if (!xssh_derive_key(adapter, 'F', macS2C, sizeof(macS2C))) return false;

    attrs = psa_key_attributes_init();
    psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attrs, 128);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attrs, PSA_ALG_CTR);
    st = psa_import_key(&attrs, encC2S, sizeof(encC2S), &adapter->cipherKeyC2S);
    if (st != PSA_SUCCESS) return false;
    st = psa_import_key(&attrs, encS2C, sizeof(encS2C), &adapter->cipherKeyS2C);
    if (st != PSA_SUCCESS) return false;

    attrs = psa_key_attributes_init();
    psa_set_key_type(&attrs, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attrs, 256);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attrs, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    st = psa_import_key(&attrs, macC2S, sizeof(macC2S), &adapter->macKeyC2S);
    if (st != PSA_SUCCESS) return false;
    st = psa_import_key(&attrs, macS2C, sizeof(macS2C), &adapter->macKeyS2C);
    if (st != PSA_SUCCESS) return false;

    adapter->encOp = psa_cipher_operation_init();
    adapter->decOp = psa_cipher_operation_init();
    st = psa_cipher_encrypt_setup(&adapter->encOp, adapter->cipherKeyS2C, PSA_ALG_CTR);
    if (st != PSA_SUCCESS) return false;
    st = psa_cipher_set_iv(&adapter->encOp, ivS2C, sizeof(ivS2C));
    if (st != PSA_SUCCESS) return false;
    adapter->encActive = true;

    st = psa_cipher_decrypt_setup(&adapter->decOp, adapter->cipherKeyC2S, PSA_ALG_CTR);
    if (st != PSA_SUCCESS) return false;
    st = psa_cipher_set_iv(&adapter->decOp, ivC2S, sizeof(ivC2S));
    if (st != PSA_SUCCESS) return false;
    adapter->decActive = true;
    return true;
}

static bool xssh_build_key_blob(const uint8_t* point, size_t pointLen,
                                uint8_t* out, size_t outCap, size_t* outLen)
{
    static const uint8_t kName[] = "ecdsa-sha2-nistp256";
    static const uint8_t kCurve[] = "nistp256";
    size_t off = 0;
    if (!out || pointLen > outCap) return false;
    xssh_write_string(out, &off, kName, sizeof(kName) - 1);
    xssh_write_string(out, &off, kCurve, sizeof(kCurve) - 1);
    xssh_write_string(out, &off, point, pointLen);
    *outLen = off;
    return off <= outCap;
}

static bool xssh_generate_host_key(XConsoleShellSshAdapter* adapter)
{
    psa_key_attributes_t attrs;
    psa_status_t st;
    uint8_t point[XSSH_POINT_LEN];
    size_t pointLen;
    uint8_t privateBytes[XSSH_HOSTKEY_MAX_BYTES];
    size_t privateLen = 0;

    if (!adapter) return false;
    if (g_xssh_hostKey) {
        adapter->hostKey = g_xssh_hostKey;
        adapter->hostKeyShared = true;
        memcpy(adapter->hostKeyBlob, g_xssh_hostKeyBlob,
               g_xssh_hostKeyBlobLen);
        adapter->hostKeyBlobLen = g_xssh_hostKeyBlobLen;
        ++g_xssh_hostKeyRefs;
        return true;
    }

    attrs = psa_key_attributes_init();
    psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attrs, 256);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attrs, PSA_ALG_ECDSA(PSA_ALG_SHA_256));

    /* 优先加载已持久化的主机密钥，保证跨进程重启后主机指纹不变。 */
    if (xssh_hostkey_load(privateBytes, sizeof(privateBytes), &privateLen)) {
        st = psa_import_key(&attrs, privateBytes, privateLen, &adapter->hostKey);
        memset(privateBytes, 0, sizeof(privateBytes));
        if (st == PSA_SUCCESS) {
            st = psa_export_public_key(adapter->hostKey, point, sizeof(point),
                                       &pointLen);
            if (st == PSA_SUCCESS && pointLen == XSSH_POINT_LEN &&
                xssh_build_key_blob(point, pointLen, adapter->hostKeyBlob,
                                    sizeof(adapter->hostKeyBlob),
                                    &adapter->hostKeyBlobLen)) {
                g_xssh_hostKey = adapter->hostKey;
                memcpy(g_xssh_hostKeyBlob, adapter->hostKeyBlob,
                       adapter->hostKeyBlobLen);
                g_xssh_hostKeyBlobLen = adapter->hostKeyBlobLen;
                g_xssh_hostKeyRefs = 1;
                adapter->hostKeyShared = true;
                return true;
            }
            (void)psa_destroy_key(adapter->hostKey);
            adapter->hostKey = 0;
        }
    }

    /* 没有可用持久化密钥：生成新主机密钥并落盘。对 PSA 导出的私钥标量
     * 立即清零，避免在栈上长时间保留明文。 */
    st = psa_generate_key(&attrs, &adapter->hostKey);
    XSSH_DBG("hostkey: generate st=%d\n", (int)st);
    if (st != PSA_SUCCESS) return false;

    st = psa_export_key(adapter->hostKey, privateBytes, sizeof(privateBytes),
                        &privateLen);
    XSSH_DBG("hostkey: export st=%d len=%u\n", (int)st, (unsigned)privateLen);
    if (st != PSA_SUCCESS || privateLen == 0 || privateLen > XSSH_HOSTKEY_MAX_BYTES) {
        (void)psa_destroy_key(adapter->hostKey);
        adapter->hostKey = 0;
        return false;
    }
    if (!xssh_hostkey_save(privateBytes, privateLen))
        XSSH_DBG("hostkey: save failed (host key will not persist)\n");
    memset(privateBytes, 0, sizeof(privateBytes));

    st = psa_export_public_key(adapter->hostKey, point, sizeof(point), &pointLen);
    if (st != PSA_SUCCESS || pointLen != XSSH_POINT_LEN) goto fail;
    if (!xssh_build_key_blob(point, pointLen, adapter->hostKeyBlob,
                             sizeof(adapter->hostKeyBlob), &adapter->hostKeyBlobLen))
        goto fail;

    g_xssh_hostKey = adapter->hostKey;
    memcpy(g_xssh_hostKeyBlob, adapter->hostKeyBlob, adapter->hostKeyBlobLen);
    g_xssh_hostKeyBlobLen = adapter->hostKeyBlobLen;
    g_xssh_hostKeyRefs = 1;
    adapter->hostKeyShared = true;
    return true;

fail:
    if (adapter->hostKeyShared) {
        if (g_xssh_hostKeyRefs) --g_xssh_hostKeyRefs;
        if (g_xssh_hostKeyRefs == 0 && g_xssh_hostKey) {
            (void)psa_destroy_key(g_xssh_hostKey);
            g_xssh_hostKey = 0;
            g_xssh_hostKeyBlobLen = 0;
            memset(g_xssh_hostKeyBlob, 0, sizeof(g_xssh_hostKeyBlob));
        }
    } else {
        (void)psa_destroy_key(adapter->hostKey);
    }
    adapter->hostKey = 0;
    return false;
}

/* ------------------------------------------------------------------ */
/* 消息处理                                                            */
/* ------------------------------------------------------------------ */

static int64_t xssh_shell_write(void* userData, const void* data, size_t size);
static bool xssh_flush_channel_pending(XConsoleShellSshAdapter* adapter);
static void xssh_shell_emit_prompt(XConsoleShellSshAdapter* adapter);
static void xssh_shell_prompt(void* userData, XConsoleShell* shell);

static bool xssh_check_kexinit(XConsoleShellSshAdapter* adapter)
{
    const uint8_t* p = adapter->clientKexInit;
    size_t len = adapter->clientKexInitLen;
    size_t off = 0;
    const uint8_t* list;
    size_t listLen;

    if (len < 1 + 16) { XSSH_DBG("kexinit too short len=%u\n", (unsigned)len); return false; }
    off = 1 + 16;
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 1 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "ecdh-sha2-nistp256")) { XSSH_DBG("kexinit no ecdh list='%.*s'\n", (int)listLen, list); return false; }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 2 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "ecdsa-sha2-nistp256")) { XSSH_DBG("kexinit no ecdsa list='%.*s'\n", (int)listLen, list); return false; }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 3 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "aes128-ctr")) { XSSH_DBG("kexinit no aes c2s list='%.*s'\n", (int)listLen, list); return false; }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 4 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "aes128-ctr")) { XSSH_DBG("kexinit no aes s2c list='%.*s'\n", (int)listLen, list); return false; }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 5 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "hmac-sha2-256")) { XSSH_DBG("kexinit no hmac c2s list='%.*s'\n", (int)listLen, list); return false; }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 6 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "hmac-sha2-256")) { XSSH_DBG("kexinit no hmac s2c list='%.*s'\n", (int)listLen, list); return false; }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 7 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "none")) { XSSH_DBG("kexinit no none c2s list='%.*s'\n", (int)listLen, list); return false; }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 8 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "none")) { XSSH_DBG("kexinit no none s2c list='%.*s'\n", (int)listLen, list); return false; }
    XSSH_DBG("kexinit ok\n");
    return true;
}

static bool xssh_send_kexinit(XConsoleShellSshAdapter* adapter)
{
    uint8_t msg[512];
    size_t off = 0;
    uint8_t cookie[16];
    const uint8_t* kex = (const uint8_t*)"ecdh-sha2-nistp256";
    const uint8_t* host = (const uint8_t*)"ecdsa-sha2-nistp256";
    const uint8_t* cipher = (const uint8_t*)"aes128-ctr";
    const uint8_t* mac = (const uint8_t*)"hmac-sha2-256";
    const uint8_t* none = (const uint8_t*)"none";

    if (!XRandomGenerator_fillSecure(cookie, sizeof(cookie))) return false;
    msg[off++] = SSH_MSG_KEXINIT;
    memcpy(msg + off, cookie, sizeof(cookie));
    off += sizeof(cookie);
    xssh_write_string(msg, &off, kex, strlen((const char*)kex));
    xssh_write_string(msg, &off, host, strlen((const char*)host));
    xssh_write_string(msg, &off, cipher, strlen((const char*)cipher));
    xssh_write_string(msg, &off, cipher, strlen((const char*)cipher));
    xssh_write_string(msg, &off, mac, strlen((const char*)mac));
    xssh_write_string(msg, &off, mac, strlen((const char*)mac));
    xssh_write_string(msg, &off, none, 4);
    xssh_write_string(msg, &off, none, 4);
    xssh_write_string(msg, &off, (const uint8_t*)"", 0);
    xssh_write_string(msg, &off, (const uint8_t*)"", 0);
    msg[off++] = 0; /* first_kex_packet_follows */
    msg[off++] = 0; msg[off++] = 0; msg[off++] = 0; msg[off++] = 0; /* reserved */

    if (off > sizeof(adapter->serverKexInit)) return false;
    memcpy(adapter->serverKexInit, msg, off);
    adapter->serverKexInitLen = off;
    XSSH_DBG("server kexinit payload len=%u\n", (unsigned)off);
    return xssh_send_packet(adapter, msg, off);
}

static XConsoleResult xssh_send_userauth_failure(XConsoleShellSshAdapter* adapter)
{
    uint8_t payload[64];
    size_t off = 0;
    payload[off++] = SSH_MSG_USERAUTH_FAILURE;
    xssh_write_string(payload, &off, (const uint8_t*)"password", 8);
    payload[off++] = 0; /* partial success */
    return xssh_send_packet(adapter, payload, off) ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static XConsoleResult xssh_fail_password_auth(XConsoleShellSshAdapter* adapter)
{
    XConsoleResult result;
    if (!adapter) return XConsoleResult_InvalidArgument;
    result = xssh_send_userauth_failure(adapter);
    if (result != XConsoleResult_Ok) return result;
    if (adapter->authAttempts < UINT8_MAX) ++adapter->authAttempts;
    if (adapter->authAttempts >= XCONSOLE_SHELL_XSSH_MAX_AUTH_ATTEMPTS)
        adapter->closed = true;
    return XConsoleResult_Ok;
}

static bool xssh_der_ecdsa_to_rs(const uint8_t* der, size_t derLen,
                                        uint8_t r[32], uint8_t s[32])
{
    size_t off = 0;
    size_t contentLen;
    size_t contentEnd;
    int which;
    if (!der || derLen < 2 || der[0] != 0x30) return false;
    if (der[1] & 0x80u) {
        size_t n = der[1] & 0x7fu;
        size_t i;
        if (n == 0 || n > 4 || derLen < 2 + n) return false;
        contentLen = 0;
        for (i = 0; i < n; ++i) contentLen = (contentLen << 8) | der[2 + i];
        off = 2 + n;
    } else {
        contentLen = der[1];
        off = 2;
    }
    if (contentLen > derLen - off) return false;
    contentEnd = off + contentLen;
    for (which = 0; which < 2; ++which) {
        uint8_t* dst = (which == 0) ? r : s;
        const uint8_t* ib;
        size_t ilen, ilen2;
        if (off + 2 > contentEnd || der[off] != 0x02) return false;
        ++off;
        ilen = der[off++];
        if (off + ilen > contentEnd || ilen == 0 || ilen > 33) return false;
        ib = der + off;
        ilen2 = ilen;
        if (ib[0] == 0) { ++ib; --ilen2; }
        if (ilen2 > 32) return false;
        memset(dst, 0, 32);
        memcpy(dst + 32 - ilen2, ib, ilen2);
        off += ilen;
    }
    return off == contentEnd;
}

static XConsoleResult xssh_handle_kex_ecdh_init(XConsoleShellSshAdapter* adapter,
                                                const uint8_t* payload, size_t len)
{
    XSSH_DBG("handle kex_ecdh_init len=%u\n", (unsigned)len);
    size_t off = 0;
    const uint8_t* qcString;
    size_t qcStringLen;
    uint8_t serverPub[XSSH_POINT_LEN];
    size_t serverPubLen;
    uint8_t shared[32];
    size_t sharedLen;
    uint8_t hashInput[XSSH_RX_CAPACITY + 64];
    size_t hashOff = 0;
    uint8_t sig[256];
    size_t sigLen;
    uint8_t signatureHash[XSSH_HASH_LEN];
    size_t signatureHashLen;
    uint8_t r[32], sx[32];
    uint8_t sigBlob[300];
    size_t sigBlobLen;
    uint8_t reply[XSSH_MAX_PACKET];
    size_t replyOff = 0;
    psa_key_attributes_t attrs;
    psa_status_t st;
    size_t hLen;

    if (len < 1 + 4) { XSSH_DBG("bad len\n"); return XConsoleResult_Failed; }
    off = 1;
    /* Q_C 在 SSH_MSG_KEX_ECDH_INIT 中是原始未压缩点（0x04 || X || Y），不是 key blob */
    if (!xssh_get_string(payload, len, &off, &qcString, &qcStringLen) ||
        qcStringLen != XSSH_POINT_LEN || qcString[0] != 0x04)
    { XSSH_DBG("get qc point failed len=%u first=%02x\n", (unsigned)qcStringLen, qcString[0]); return XConsoleResult_Failed; }

    memcpy(adapter->clientPublicBlob, qcString, qcStringLen);
    adapter->clientPublicBlobLen = qcStringLen;

    /* 生成服务端临时 ECDH 密钥 */
    attrs = psa_key_attributes_init();
    psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attrs, 256);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attrs, PSA_ALG_ECDH);
    st = psa_generate_key(&attrs, &adapter->ecdhKey);
    if (st != PSA_SUCCESS) { XSSH_DBG("gen ecdh failed st=%d\n", (int)st); return XConsoleResult_Failed; }
    st = psa_export_public_key(adapter->ecdhKey, serverPub, sizeof(serverPub), &serverPubLen);
    if (st != PSA_SUCCESS || serverPubLen != XSSH_POINT_LEN)
    { XSSH_DBG("export pub failed st=%d len=%u\n", (int)st, (unsigned)serverPubLen); return XConsoleResult_Failed; }

    st = psa_raw_key_agreement(PSA_ALG_ECDH, adapter->ecdhKey, qcString, XSSH_POINT_LEN,
                               shared, sizeof(shared), &sharedLen);
    if (st != PSA_SUCCESS || sharedLen == 0)
    { XSSH_DBG("raw agreement failed st=%d len=%u\n", (int)st, (unsigned)sharedLen); return XConsoleResult_Failed; }
    /* 共享密钥 K 以 SSH mpint 编码：先去掉前导 0x00，若最高位仍为 1 再前置 0x00。 */
    {
        size_t _i = 0;
        while (_i + 1 < sharedLen && shared[_i] == 0) ++_i;
        adapter->sharedSecretLen = sharedLen - _i;
        memcpy(adapter->sharedSecret, shared + _i, adapter->sharedSecretLen);
        if (adapter->sharedSecret[0] & 0x80u) {
            memmove(adapter->sharedSecret + 1, adapter->sharedSecret,
                    adapter->sharedSecretLen);
            adapter->sharedSecret[0] = 0;
            adapter->sharedSecretLen += 1;
        }
    }

    /* 交换哈希 H（RFC 4253 §8）：
     *   string V_C || string V_S || string I_C || string I_S
     *   || string K_S || string Q_C || string Q_S || mpint K
     * V_C/V_S 为版本行（不含行尾 CR/LF）；I_C/I_S 内容含消息类型字节；
     * K_S 为 host key blob；Q_C/Q_S 为原始点字符串；K 为 shared secret mpint。
     * 注意 V_C/V_S/I_C/I_S 都必须带 SSH string 的 4 字节长度前缀。 */
    {
        const uint8_t* vc = adapter->clientVersion;
        size_t vcLen = adapter->clientVersionLen;
        const uint8_t* vs = (const uint8_t*)XSSH_VERSION;
        size_t vsLen = strlen(XSSH_VERSION);
        while (vcLen && (vc[vcLen - 1] == '\r' || vc[vcLen - 1] == '\n')) --vcLen;
        while (vsLen && (vs[vsLen - 1] == '\r' || vs[vsLen - 1] == '\n')) --vsLen;
        /* V_C/V_S 按 SSH string 编码（RFC 4253 §8：string V_C、string V_S） */
        xssh_write_u32(hashInput + hashOff, (uint32_t)vcLen);
        hashOff += 4;
        memcpy(hashInput + hashOff, vc, vcLen);
        hashOff += vcLen;
        xssh_write_u32(hashInput + hashOff, (uint32_t)vsLen);
        hashOff += 4;
        memcpy(hashInput + hashOff, vs, vsLen);
        hashOff += vsLen;
    }
    /* I_C/I_S 为 SSH string，内容为“消息类型 + KEXINIT 体”。 */
    xssh_write_u32(hashInput + hashOff, (uint32_t)adapter->clientKexInitLen);
    hashOff += 4;
    memcpy(hashInput + hashOff, adapter->clientKexInit, adapter->clientKexInitLen);
    hashOff += adapter->clientKexInitLen;
    xssh_write_u32(hashInput + hashOff, (uint32_t)adapter->serverKexInitLen);
    hashOff += 4;
    memcpy(hashInput + hashOff, adapter->serverKexInit, adapter->serverKexInitLen);
    hashOff += adapter->serverKexInitLen;
    xssh_write_string(hashInput, &hashOff, adapter->hostKeyBlob, adapter->hostKeyBlobLen);
    xssh_write_string(hashInput, &hashOff, adapter->clientPublicBlob, adapter->clientPublicBlobLen);
    xssh_write_string(hashInput, &hashOff, serverPub, serverPubLen);
    /* K 为 SSH mpint，需带 4 字节长度前缀 */
    xssh_write_u32(hashInput + hashOff, (uint32_t)adapter->sharedSecretLen);
    hashOff += 4;
    memcpy(hashInput + hashOff, adapter->sharedSecret, adapter->sharedSecretLen);
    hashOff += adapter->sharedSecretLen;

    {
        unsigned i;
        XSSH_DBG("hashInputLen=%u\n", (unsigned)hashOff);
        XSSH_DBG("hashInput:");
        for (i = 0; i < hashOff; ++i) XSSH_DBG("%02x", hashInput[i]);
        XSSH_DBG("\n");
        XSSH_DBG("hostKeyBlob:");
        for (i = 0; i < adapter->hostKeyBlobLen; ++i) XSSH_DBG("%02x", adapter->hostKeyBlob[i]);
        XSSH_DBG("\n");
    }
    if (psa_hash_compute(PSA_ALG_SHA_256, hashInput, hashOff,
                         adapter->exchangeHash, sizeof(adapter->exchangeHash), &hLen) != PSA_SUCCESS ||
        hLen != XSSH_HASH_LEN)
    { XSSH_DBG("hash failed hLen=%u\n", (unsigned)hLen); return XConsoleResult_Failed; }
    memcpy(adapter->sessionId, adapter->exchangeHash, XSSH_HASH_LEN);

    if (!xssh_setup_keys(adapter)) { XSSH_DBG("setup keys failed\n"); return XConsoleResult_Failed; }

    /* OpenSSH 的 ECDSA 验证接口按消息模式再计算一次 SHA-256；交换哈希 H
     * 仍作为会话 ID 和密钥派生输入保留原值。 */
    if (psa_hash_compute(PSA_ALG_SHA_256, adapter->exchangeHash,
                         XSSH_HASH_LEN, signatureHash, sizeof(signatureHash),
                         &signatureHashLen) != PSA_SUCCESS ||
        signatureHashLen != XSSH_HASH_LEN)
        return XConsoleResult_Failed;

    /* ecdsa-sha2-nistp256 签名格式（RFC 5656 §3.1.2）：
     *   string "ecdsa-sha2-nistp256"
     *   string ecdsa_signature_blob
     * 其中 ecdsa_signature_blob = mpint r || mpint s */
    st = psa_sign_hash(adapter->hostKey, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                       signatureHash, signatureHashLen, sig, sizeof(sig), &sigLen);
    if (st != PSA_SUCCESS) { XSSH_DBG("sign hash failed st=%d\n", (int)st); return XConsoleResult_Failed; }
    XSSH_DBG("sign sigLen=%u first=%02x %02x %02x %02x\n",
             (unsigned)sigLen, sig[0], sig[1], sig[2], sig[3]);
    {
        /* temporary self-verify against exported host key */
        psa_key_attributes_t vattrs;
        mbedtls_svc_key_id_t vkey = 0;
        psa_status_t vst;
        uint8_t point[XSSH_POINT_LEN];
        size_t point_len = 0;
        vattrs = psa_key_attributes_init();
        psa_set_key_type(&vattrs, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
        psa_set_key_bits(&vattrs, 256);
        psa_set_key_usage_flags(&vattrs, PSA_KEY_USAGE_VERIFY_HASH);
        psa_set_key_algorithm(&vattrs, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
        vst = psa_export_public_key(adapter->hostKey, point, sizeof(point), &point_len);
        if (vst == PSA_SUCCESS) {
            unsigned ii;
            XSSH_DBG("exported point len=%u:", (unsigned)point_len);
            for (ii = 0; ii < point_len; ++ii) XSSH_DBG("%02x", point[ii]);
            XSSH_DBG("\nblob point:");
            {
                const uint8_t* bp = adapter->hostKeyBlob;
                size_t boff = 0, bsl;
                size_t j;
                /* parse blob: alg string, curve string, point string */
                if (boff + 4 <= adapter->hostKeyBlobLen) {
                    bsl = (size_t)((bp[boff]<<24)|(bp[boff+1]<<16)|(bp[boff+2]<<8)|bp[boff+3]); boff += 4;
                    boff += bsl;
                    if (boff + 4 <= adapter->hostKeyBlobLen) {
                        bsl = (size_t)((bp[boff]<<24)|(bp[boff+1]<<16)|(bp[boff+2]<<8)|bp[boff+3]); boff += 4;
                        boff += bsl;
                        if (boff + 4 <= adapter->hostKeyBlobLen) {
                            bsl = (size_t)((bp[boff]<<24)|(bp[boff+1]<<16)|(bp[boff+2]<<8)|bp[boff+3]); boff += 4;
                            for (j = 0; j < bsl; ++j) XSSH_DBG("%02x", bp[boff+j]);
                            XSSH_DBG("\n");
                        }
                    }
                }
            }
        }
        if (vst == PSA_SUCCESS) {
            vst = psa_import_key(&vattrs, point, point_len, &vkey);
            if (vst == PSA_SUCCESS) {
                vst = psa_verify_hash(vkey, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                                      signatureHash, signatureHashLen, sig, sigLen);
                XSSH_DBG("self verify st=%d\n", (int)vst);
                psa_destroy_key(vkey);
            }
        }
    }
    if (sigLen == 64) {
        /* mbedTLS PSA 的 ECDSA 签名直接返回 r||s 各 32 字节 */
        memcpy(r, sig, 32);
        memcpy(sx, sig + 32, 32);
    } else if (!xssh_der_ecdsa_to_rs(sig, sigLen, r, sx)) {
        XSSH_DBG("ECDSA signature parse failed sigLen=%u first=%02x\n",
                 (unsigned)sigLen, sig[0]);
        return XConsoleResult_Failed;
    }
    {
        uint8_t inner[80];
        size_t innerLen = 0;
        size_t rBits = (r[0] & 0x80u) ? 1 : 0;
        size_t sBits = (sx[0] & 0x80u) ? 1 : 0;
        size_t rStored = 32 + rBits;
        size_t sStored = 32 + sBits;
        size_t so = 0;
        if (rStored > 33 || sStored > 33) {
            XSSH_DBG("signature inner too large\n");
            return XConsoleResult_Failed;
        }
        /* mpint r：若最高位为 1 则前置 0x00 */
        xssh_write_u32(inner + innerLen, (uint32_t)rStored); innerLen += 4;
        if (rBits) inner[innerLen++] = 0;
        memcpy(inner + innerLen, r, 32); innerLen += 32;
        /* mpint s */
        xssh_write_u32(inner + innerLen, (uint32_t)sStored); innerLen += 4;
        if (sBits) inner[innerLen++] = 0;
        memcpy(inner + innerLen, sx, 32); innerLen += 32;
        xssh_write_string(sigBlob, &so, (const uint8_t*)"ecdsa-sha2-nistp256", 19);
        xssh_write_string(sigBlob, &so, inner, innerLen);
        sigBlobLen = so;
    }

    /* KEX_ECDH_REPLY：K_S || Q_S(原始点) || signature */
    reply[replyOff++] = SSH_MSG_KEX_ECDH_REPLY;
    xssh_write_string(reply, &replyOff, adapter->hostKeyBlob, adapter->hostKeyBlobLen);
    xssh_write_string(reply, &replyOff, serverPub, serverPubLen);
    xssh_write_string(reply, &replyOff, sigBlob, sigBlobLen);
    {
        unsigned i;
        XSSH_DBG("H:");
        for (i = 0; i < XSSH_HASH_LEN; ++i) XSSH_DBG("%02x", adapter->exchangeHash[i]);
        XSSH_DBG("\n");
        XSSH_DBG("sig:");
        for (i = 0; i < sigBlobLen; ++i) XSSH_DBG("%02x", sigBlob[i]);
        XSSH_DBG("\n");
    }
    XSSH_DBG("sending ECDH_REPLY off=%u\n", (unsigned)replyOff);
    {
        unsigned _i;
        XSSH_DBG("reply:");
        for (_i = 0; _i < replyOff; ++_i) XSSH_DBG("%02x", reply[_i]);
        XSSH_DBG("\n");
    }
    if (!xssh_send_packet(adapter, reply, replyOff)) { XSSH_DBG("send reply failed\n"); return XConsoleResult_Failed; }

    {
        uint8_t nk[1] = { SSH_MSG_NEWKEYS };
        if (!xssh_send_packet(adapter, nk, 1)) return XConsoleResult_Failed;
    }
    adapter->sendEncrypted = true;
    adapter->state = XSshState_NewKeys;
    return XConsoleResult_Ok;
}

static XConsoleResult xssh_handle_service_request(XConsoleShellSshAdapter* adapter,
                                                  const uint8_t* payload, size_t len)
{
    size_t off = 1;
    const uint8_t* name;
    size_t nameLen;
    uint8_t reply[32];
    size_t o = 0;
    if (!xssh_get_string(payload, len, &off, &name, &nameLen)) return XConsoleResult_Failed;
    if (nameLen != 12 || memcmp(name, "ssh-userauth", 12) != 0) return XConsoleResult_Failed;
    reply[o++] = SSH_MSG_SERVICE_ACCEPT;
    xssh_write_string(reply, &o, (const uint8_t*)"ssh-userauth", 12);
    if (!xssh_send_packet(adapter, reply, o)) return XConsoleResult_Failed;
    adapter->state = XSshState_Userauth;
    return XConsoleResult_Ok;
}

static XConsoleResult xssh_handle_userauth_request(XConsoleShellSshAdapter* adapter,
                                                   const uint8_t* payload, size_t len)
{
    size_t off = 1;
    const uint8_t* user;
    const uint8_t* service;
    const uint8_t* method;
    size_t userLen, serviceLen, methodLen;
    if (!xssh_get_string(payload, len, &off, &user, &userLen)) return XConsoleResult_Failed;
    if (!xssh_get_string(payload, len, &off, &service, &serviceLen)) return XConsoleResult_Failed;
    if (!xssh_get_string(payload, len, &off, &method, &methodLen)) return XConsoleResult_Failed;
    if (serviceLen != 14 || memcmp(service, "ssh-connection", 14) != 0)
        return xssh_fail_password_auth(adapter);

    if (methodLen == 4 && memcmp(method, "none", 4) == 0)
        return xssh_send_userauth_failure(adapter);

    if (methodLen == 8 && memcmp(method, "password", 8) == 0) {
        bool change;
        const uint8_t* passwd;
        size_t passwdLen;
        char userBuf[XCONSOLE_SHELL_LOGIN_NAME_SIZE];
        char passBuf[XCONSOLE_SHELL_LOGIN_PASSWORD_SIZE + 1];
        if (off >= len || payload[off] > 1) return XConsoleResult_Failed;
        change = payload[off++] != 0;
        if (change) return xssh_fail_password_auth(adapter);
        if (!xssh_get_string(payload, len, &off, &passwd, &passwdLen) || off != len)
            return XConsoleResult_Failed;
        if (userLen == 0 || userLen >= sizeof(userBuf) ||
            passwdLen > XCONSOLE_SHELL_LOGIN_PASSWORD_SIZE)
            return xssh_fail_password_auth(adapter);
        memcpy(userBuf, user, userLen);
        userBuf[userLen] = 0;
        memcpy(passBuf, passwd, passwdLen);
        passBuf[passwdLen] = 0;
        if (!adapter->shell || !adapter->session) {
            memset(passBuf, 0, sizeof(passBuf));
            return XConsoleResult_Failed;
        }
        if (!XConsoleShellLogin_authenticateSession(adapter->shell, adapter->session,
                                                    userBuf, passBuf)) {
            memset(passBuf, 0, sizeof(passBuf));
            return xssh_fail_password_auth(adapter);
        }
        memset(passBuf, 0, sizeof(passBuf));
        adapter->authDone = true;
        adapter->state = XSshState_Channel;
        {
            uint8_t okMsg[1] = { SSH_MSG_USERAUTH_SUCCESS };
            if (!xssh_send_packet(adapter, okMsg, 1)) return XConsoleResult_Failed;
        }
        return XConsoleResult_Ok;
    }
    return xssh_send_userauth_failure(adapter);
}

static XConsoleResult xssh_send_channel_success(XConsoleShellSshAdapter* adapter,
                                                uint8_t msgType)
{
    uint8_t payload[16];
    size_t off = 0;
    payload[off++] = msgType;
    xssh_write_u32(payload + off, adapter->localChannel);
    off += 4;
    return xssh_send_packet(adapter, payload, off) ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static XConsoleResult xssh_channel_protocol_error(XConsoleShellSshAdapter* adapter)
{
    if (adapter) adapter->closed = true;
    return XConsoleResult_Failed;
}

static bool xssh_send_channel_open_failure(XConsoleShellSshAdapter* adapter,
                                           uint32_t sender, uint32_t reason,
                                           const char* description)
{
    uint8_t payload[160];
    size_t off = 0;
    const char* text = description ? description : "channel open failed";
    size_t textLen = strlen(text);
    if (textLen > sizeof(payload) - 17) textLen = sizeof(payload) - 17;
    payload[off++] = SSH_MSG_CHANNEL_OPEN_FAILURE;
    xssh_write_u32(payload + off, sender); off += 4;
    xssh_write_u32(payload + off, reason); off += 4;
    xssh_write_string(payload, &off, (const uint8_t*)text, textLen);
    xssh_write_string(payload, &off, (const uint8_t*)"", 0);
    return xssh_send_packet(adapter, payload, off);
}

static bool xssh_send_channel_window_adjust(XConsoleShellSshAdapter* adapter,
                                            uint32_t amount)
{
    uint8_t payload[9];
    if (!adapter || !adapter->channelOpen || amount == 0) return false;
    payload[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
    xssh_write_u32(payload + 1, adapter->remoteChannel);
    xssh_write_u32(payload + 5, amount);
    return xssh_send_packet(adapter, payload, sizeof(payload));
}

static bool xssh_send_channel_eof(XConsoleShellSshAdapter* adapter)
{
    uint8_t payload[5];
    if (!adapter || !adapter->channelOpen || adapter->localEof) return true;
    payload[0] = SSH_MSG_CHANNEL_EOF;
    xssh_write_u32(payload + 1, adapter->remoteChannel);
    if (!xssh_send_packet(adapter, payload, sizeof(payload))) return false;
    adapter->localEof = true;
    return true;
}

static bool xssh_send_channel_close(XConsoleShellSshAdapter* adapter)
{
    uint8_t payload[5];
    if (!adapter || !adapter->channelOpen || adapter->localClose) return true;
    payload[0] = SSH_MSG_CHANNEL_CLOSE;
    xssh_write_u32(payload + 1, adapter->remoteChannel);
    if (!xssh_send_packet(adapter, payload, sizeof(payload))) return false;
    adapter->localClose = true;
    return true;
}

static bool xssh_replenish_channel_window(XConsoleShellSshAdapter* adapter)
{
    uint32_t amount;
    if (!adapter || !adapter->channelOpen) return false;
    if (adapter->serverWindow >= XSSH_CHANNEL_WINDOW_LOW_WATER) return true;
    amount = XSSH_CHANNEL_INITIAL_WINDOW - adapter->serverWindow;
    if (!xssh_send_channel_window_adjust(adapter, amount)) return false;
    adapter->serverWindow += amount;
    return true;
}

static XConsoleResult xssh_handle_channel_open(XConsoleShellSshAdapter* adapter,
                                               const uint8_t* payload, size_t len)
{
    size_t off = 1;
    const uint8_t* type;
    size_t typeLen;
    uint32_t sender;
    uint32_t win;
    uint32_t maxPacket;
    uint8_t reply[32];
    size_t o = 0;

    if (!adapter || !payload || len < 1 + 4) return XConsoleResult_Failed;
    if (!xssh_get_string(payload, len, &off, &type, &typeLen))
        return xssh_channel_protocol_error(adapter);
    if (off + 12 != len)
        return xssh_channel_protocol_error(adapter);
    xssh_get_u32(payload + off, &sender); off += 4;
    xssh_get_u32(payload + off, &win); off += 4;
    xssh_get_u32(payload + off, &maxPacket); off += 4;
    if (adapter->channelOpen) {
        if (!xssh_send_channel_open_failure(adapter, sender, 1,
                                            "only one session channel is supported"))
            return XConsoleResult_Failed;
        return XConsoleResult_Ok;
    }
    if (typeLen != 7 || memcmp(type, "session", 7) != 0) {
        if (!xssh_send_channel_open_failure(adapter, sender, 3,
                                            "unsupported channel type"))
            return XConsoleResult_Failed;
        return XConsoleResult_Ok;
    }
    if (maxPacket < 16u) {
        if (!xssh_send_channel_open_failure(adapter, sender, 2,
                                            "invalid maximum packet size"))
            return XConsoleResult_Failed;
        return XConsoleResult_Ok;
    }

    adapter->remoteChannel = sender;
    adapter->clientWindow = win;
    adapter->clientMaxPacket = maxPacket;
    adapter->serverWindow = XSSH_CHANNEL_INITIAL_WINDOW;
    adapter->serverMaxPacket = XSSH_MAX_PACKET;
    adapter->channelOpen = true;

    reply[o++] = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
    xssh_write_u32(reply + o, adapter->remoteChannel); o += 4;
    xssh_write_u32(reply + o, adapter->localChannel); o += 4;
    xssh_write_u32(reply + o, adapter->serverWindow); o += 4;
    xssh_write_u32(reply + o, adapter->serverMaxPacket); o += 4;
    return xssh_send_packet(adapter, reply, o) ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static XConsoleResult xssh_handle_channel_request(XConsoleShellSshAdapter* adapter,
                                                  const uint8_t* payload, size_t len)
{
    size_t off = 1;
    uint32_t recipient;
    const uint8_t* type;
    size_t typeLen;
    bool wantReply;
    uint8_t replyMsg;

    if (!adapter || !payload || len < 1 + 4) return XConsoleResult_Failed;
    if (off + 4 > len) return xssh_channel_protocol_error(adapter);
    xssh_get_u32(payload + off, &recipient); off += 4;
    if (!xssh_get_string(payload, len, &off, &type, &typeLen))
        return xssh_channel_protocol_error(adapter);
    if (off >= len) return xssh_channel_protocol_error(adapter);
    if (payload[off] > 1) return xssh_channel_protocol_error(adapter);
    wantReply = payload[off++] != 0;
    if (!adapter->channelOpen || recipient != adapter->localChannel)
        return xssh_channel_protocol_error(adapter);

    if (typeLen == 5 && memcmp(type, "shell", 5) == 0) {
        if (off != len || adapter->shellStarted) {
            replyMsg = SSH_MSG_CHANNEL_FAILURE;
            if (wantReply && xssh_send_channel_success(adapter, replyMsg) != XConsoleResult_Ok)
                return XConsoleResult_Failed;
            return XConsoleResult_Ok;
        }
        adapter->shellStarted = true;
        if (wantReply && xssh_send_channel_success(adapter, SSH_MSG_CHANNEL_SUCCESS) != XConsoleResult_Ok)
            return XConsoleResult_Failed;
        /* XConsoleShell's synchronous feed path does not invoke its prompt
         * callback, so the initial prompt is sent when the shell request is
         * accepted.  Subsequent prompts are emitted by xssh_shell_prompt. */
        if (adapter->shell) {
            xssh_shell_emit_prompt(adapter);
        }
        return XConsoleResult_Ok;
    }

    if (typeLen == 7 && memcmp(type, "pty-req", 7) == 0) {
        const uint8_t* term;
        const uint8_t* modes;
        size_t termLen;
        size_t modesLen;
        if (!xssh_get_string(payload, len, &off, &term, &termLen) ||
            off + 16 > len || termLen == 0 || termLen >= sizeof(adapter->ptyTerm)) {
            return xssh_channel_protocol_error(adapter);
        }
        memcpy(adapter->ptyTerm, term, termLen);
        adapter->ptyTerm[termLen] = '\0';
        xssh_get_u32(payload + off, &adapter->ptyColumns); off += 4;
        xssh_get_u32(payload + off, &adapter->ptyRows); off += 4;
        xssh_get_u32(payload + off, &adapter->ptyPixelWidth); off += 4;
        xssh_get_u32(payload + off, &adapter->ptyPixelHeight); off += 4;
        if (!xssh_get_string(payload, len, &off, &modes, &modesLen) || off != len)
        {
            return xssh_channel_protocol_error(adapter);
        }
        adapter->inputEcho = true;
        {
            size_t modeOff = 0;
            while (modeOff < modesLen) {
                uint8_t opcode = modes[modeOff++];
                uint32_t value;
                if (opcode == 0) break;
                if (modeOff + 4u > modesLen) return xssh_channel_protocol_error(adapter);
                xssh_get_u32(modes + modeOff, &value);
                modeOff += 4u;
                if (opcode == XSSH_TTY_OP_ECHO) adapter->inputEcho = value != 0;
            }
        }
        adapter->ptyRequested = true;
        replyMsg = SSH_MSG_CHANNEL_SUCCESS;
    } else if (typeLen == 3 && memcmp(type, "env", 3) == 0) {
        const uint8_t* name;
        const uint8_t* value;
        size_t nameLen;
        size_t valueLen;
        if (!xssh_get_string(payload, len, &off, &name, &nameLen) ||
            !xssh_get_string(payload, len, &off, &value, &valueLen) ||
            nameLen == 0 || nameLen > 256 || valueLen > 1024 || off != len)
            return xssh_channel_protocol_error(adapter);
        (void)name;
        (void)value;
        replyMsg = SSH_MSG_CHANNEL_SUCCESS;
    } else if (typeLen == sizeof("window-change") - 1u &&
               memcmp(type, "window-change", sizeof("window-change") - 1u) == 0) {
        if (off + 16 != len) return xssh_channel_protocol_error(adapter);
        xssh_get_u32(payload + off, &adapter->ptyColumns); off += 4;
        xssh_get_u32(payload + off, &adapter->ptyRows); off += 4;
        xssh_get_u32(payload + off, &adapter->ptyPixelWidth); off += 4;
        xssh_get_u32(payload + off, &adapter->ptyPixelHeight); off += 4;
        replyMsg = SSH_MSG_CHANNEL_SUCCESS;
    } else {
        /* Requests not implemented by this adapter (notably exec/subsystem)
         * are rejected without consuming any untrusted request-specific
         * fields.  wantReply still gets the required channel failure. */
        replyMsg = SSH_MSG_CHANNEL_FAILURE;
    }
    if (wantReply && xssh_send_channel_success(adapter, replyMsg) != XConsoleResult_Ok)
        return XConsoleResult_Failed;
    return XConsoleResult_Ok;
}

/* SSH clients put their local terminal in raw/no-echo mode after a PTY is
 * accepted.  The adapter therefore supplies the small amount of terminal
 * echo normally provided by a server-side PTY. */
static bool xssh_echo_channel_input(XConsoleShellSshAdapter* adapter,
                                    const uint8_t* data, size_t size)
{
    uint8_t echo[256];
    size_t echoLen = 0;
    size_t i;
    if (!adapter || (!data && size)) return false;
    if (!adapter->ptyRequested || !adapter->inputEcho) return true;
    for (i = 0; i < size; ++i) {
        uint8_t byte = data[i];
        if (adapter->echoEscape == 1u) {
            if (byte == '[' || byte == 'O') adapter->echoEscape = 2u;
            else adapter->echoEscape = 0u;
            if (adapter->echoEscape == 2u) continue;
        }
        if (adapter->echoEscape == 2u) {
            if (byte >= '@' && byte <= '~') adapter->echoEscape = 0u;
            continue;
        }
        if (byte == 0x1b) {
            adapter->echoEscape = 1u;
            continue;
        }
        if (byte == '\r') {
            static const char crlf[] = "\r\n";
            if (echoLen && !xssh_shell_write(adapter, echo, echoLen)) return false;
            echoLen = 0;
            if (!xssh_shell_write(adapter, crlf, sizeof(crlf) - 1u)) return false;
            adapter->echoPendingCr = true;
            continue;
        }
        if (byte == '\n') {
            if (echoLen && !xssh_shell_write(adapter, echo, echoLen)) return false;
            echoLen = 0;
            if (!adapter->echoPendingCr &&
                !xssh_shell_write(adapter, (const char*)"\n", 1u)) return false;
            adapter->echoPendingCr = false;
            continue;
        }
        adapter->echoPendingCr = false;
        if (byte == '\b' || byte == 0x7f) {
            static const char erase[] = "\b \b";
            /* 远端 PTY 通常关闭本地回显。只有 Shell 能实际删掉输入时才
             * 输出擦除序列，空命令行不能把 `user> ` 提示符擦掉。 */
            if (!XConsoleShell_canBackspace(adapter->shell, adapter->session))
                continue;
            if (echoLen && !xssh_shell_write(adapter, echo, echoLen)) return false;
            echoLen = 0;
            if (!xssh_shell_write(adapter, erase, sizeof(erase) - 1u)) return false;
            continue;
        }
        if (byte == 0x03) {
            static const char interrupt[] = "^C\r\n";
            if (echoLen && !xssh_shell_write(adapter, echo, echoLen)) return false;
            echoLen = 0;
            if (!xssh_shell_write(adapter, interrupt, sizeof(interrupt) - 1u)) return false;
            continue;
        }
        if (byte >= 0x20 && byte != '\t') {
            if (echoLen == sizeof(echo) && !xssh_shell_write(adapter, echo, echoLen))
                return false;
            if (echoLen == sizeof(echo)) echoLen = 0;
            echo[echoLen++] = byte;
        }
    }
    if (echoLen && !xssh_shell_write(adapter, echo, echoLen)) return false;
    return true;
}

static XConsoleResult xssh_handle_channel_data(XConsoleShellSshAdapter* adapter,
                                               const uint8_t* payload, size_t len)
{
    size_t off = 1;
    uint32_t recipient;
    const uint8_t* data;
    size_t dataLen;
    if (!adapter || !payload || len < 1 + 4) return XConsoleResult_Failed;
    if (off + 4 > len) return xssh_channel_protocol_error(adapter);
    xssh_get_u32(payload + off, &recipient); off += 4;
    if (!xssh_get_string(payload, len, &off, &data, &dataLen) || off != len)
        return xssh_channel_protocol_error(adapter);
    if (!adapter->channelOpen || recipient != adapter->localChannel ||
        adapter->remoteEof || dataLen > adapter->serverMaxPacket ||
        dataLen > adapter->serverWindow)
        return xssh_channel_protocol_error(adapter);
    adapter->serverWindow -= (uint32_t)dataLen;
    if (!adapter->shellStarted || !adapter->shell || !adapter->session)
        return xssh_channel_protocol_error(adapter);
    {
        size_t pos = 0;
        XConsoleResult result = XConsoleResult_Ok;
        bool previousCr = adapter->promptPendingCr;
        /* Process one logical input line at a time.  This lets a password
         * command disable echo before a following line in the same TCP packet,
         * while avoiding one SSH output packet per typed character. */
        while (pos < dataLen) {
            size_t end = pos;
            size_t segmentLen;
            uint8_t terminator = 0;
            while (end < dataLen && data[end] != '\r' && data[end] != '\n') ++end;
            if (end < dataLen) terminator = data[end++];
            segmentLen = end - pos;
            /* 回显和 Shell 必须按字节保持同一顺序。若整段先回显再喂入，
             * `abc<退格>` 中的退格会在 Shell 看到 abc 之前被判定为“空行”，
             * 从而无法擦除最后一个字符；TUI 的 ANSI 序列也需要跨字节保持状态。 */
            {
                size_t segmentOffset;
                for (segmentOffset = 0; segmentOffset < segmentLen; ++segmentOffset) {
                    if (!xssh_echo_channel_input(adapter,
                                                 data + pos + segmentOffset, 1u))
                        return XConsoleResult_IoError;
                    result = XConsoleShell_feedDataForSession(
                        adapter->shell, adapter->session,
                        data + pos + segmentOffset, 1u);
                    if (result == XConsoleResult_IoError) break;
                }
            }
            if (result == XConsoleResult_IoError) break;
            if (segmentLen > (terminator ? 1u : 0u)) previousCr = false;
            if (terminator == '\r') {
                if (adapter->session &&
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
                    !adapter->session->suppressPrompt &&
#endif
#if XCONSOLE_SHELL_MULTI_SESSION_ON
                    !adapter->session->m_closeRequested &&
#endif
                    adapter->shellStarted && XConsoleShell_isRunning(adapter->shell))
                    xssh_shell_prompt(adapter, adapter->shell);
                previousCr = true;
            } else if (terminator == '\n') {
                if (!previousCr && adapter->session &&
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
                    !adapter->session->suppressPrompt &&
#endif
#if XCONSOLE_SHELL_MULTI_SESSION_ON
                    !adapter->session->m_closeRequested &&
#endif
                    adapter->shellStarted && XConsoleShell_isRunning(adapter->shell))
                    xssh_shell_prompt(adapter, adapter->shell);
                previousCr = false;
            }
            pos = end;
        }
        adapter->promptPendingCr = previousCr;
#if XCONSOLE_SHELL_MULTI_SESSION_ON
        if (adapter->session && adapter->session->m_closeRequested) {
            if (!xssh_send_channel_eof(adapter) || !xssh_send_channel_close(adapter))
                return XConsoleResult_Failed;
            adapter->closed = true;
            return XConsoleResult_Ok;
        }
#endif
        if (!XConsoleShell_isRunning(adapter->shell)) {
            if (!xssh_send_channel_eof(adapter) || !xssh_send_channel_close(adapter))
                return XConsoleResult_Failed;
            adapter->closed = true;
            return XConsoleResult_Ok;
        }
        if (!xssh_replenish_channel_window(adapter)) return XConsoleResult_Failed;
        /* Shell 命令的返回值（Ok、MoreOutput、PermissionDenied 等）是应用层
           结果，不是传输层错误；只有写入目标的 IoError 才应关闭 SSH 连接。
           因此这里统一归一化为 Ok，IoError 仍继续上抛由调用方关闭。 */
        return result == XConsoleResult_IoError ? XConsoleResult_IoError
                                                : XConsoleResult_Ok;
    }
}

static XConsoleResult xssh_handle_channel_window_adjust(
    XConsoleShellSshAdapter* adapter, const uint8_t* payload, size_t len)
{
    uint32_t recipient;
    uint32_t amount;
    if (!adapter || !payload || len != 9) return XConsoleResult_Failed;
    xssh_get_u32(payload + 1, &recipient);
    xssh_get_u32(payload + 5, &amount);
    if (!adapter->channelOpen || recipient != adapter->localChannel || amount == 0 ||
        UINT32_MAX - adapter->clientWindow < amount)
        return xssh_channel_protocol_error(adapter);
    adapter->clientWindow += amount;
    if (!xssh_flush_channel_pending(adapter)) return XConsoleResult_Failed;
    return XConsoleResult_Ok;
}

static XConsoleResult xssh_handle_channel_eof(XConsoleShellSshAdapter* adapter,
                                              const uint8_t* payload, size_t len)
{
    uint32_t recipient;
    if (!adapter || !payload || len != 5) return XConsoleResult_Failed;
    xssh_get_u32(payload + 1, &recipient);
    if (!adapter->channelOpen || recipient != adapter->localChannel)
        return xssh_channel_protocol_error(adapter);
    adapter->remoteEof = true;
    if (!xssh_send_channel_eof(adapter)) return XConsoleResult_Failed;
    return XConsoleResult_Ok;
}

static XConsoleResult xssh_handle_channel_close(XConsoleShellSshAdapter* adapter,
                                                const uint8_t* payload, size_t len)
{
    uint32_t recipient;
    if (!adapter || !payload || len != 5) return XConsoleResult_Failed;
    xssh_get_u32(payload + 1, &recipient);
    if (!adapter->channelOpen || recipient != adapter->localChannel)
        return xssh_channel_protocol_error(adapter);
    adapter->remoteClose = true;
    if (!xssh_send_channel_close(adapter)) return XConsoleResult_Failed;
    adapter->closed = true;
    return XConsoleResult_Ok;
}

static XConsoleResult xssh_handle_channel(XConsoleShellSshAdapter* adapter,
                                          const uint8_t* payload, size_t len)
{
    uint8_t msg;
    if (len < 1) return XConsoleResult_Failed;
    msg = payload[0];
    switch (msg) {
    case SSH_MSG_GLOBAL_REQUEST:
        /* request-name is a variable-length SSH string; do not assume a
         * fixed offset for want-reply (keepalive names are longer than four
         * bytes).  Unsupported requests are answered with REQUEST_FAILURE. */
        {
            size_t off = 1;
            const uint8_t* name;
            size_t nameLen;
            bool wantReply;
            uint8_t rf[1] = { SSH_MSG_REQUEST_FAILURE };
            if (!xssh_get_string(payload, len, &off, &name, &nameLen) ||
                off >= len || payload[off] > 1)
                return xssh_channel_protocol_error(adapter);
            (void)name;
            (void)nameLen;
            wantReply = payload[off] != 0;
            if (wantReply && !xssh_send_packet(adapter, rf, 1))
                return XConsoleResult_Failed;
            return XConsoleResult_Ok;
        }
    case SSH_MSG_CHANNEL_OPEN:
        return xssh_handle_channel_open(adapter, payload, len);
    case SSH_MSG_CHANNEL_REQUEST:
        return xssh_handle_channel_request(adapter, payload, len);
    case SSH_MSG_CHANNEL_DATA:
        return xssh_handle_channel_data(adapter, payload, len);
    case SSH_MSG_CHANNEL_WINDOW_ADJUST:
        return xssh_handle_channel_window_adjust(adapter, payload, len);
    case SSH_MSG_CHANNEL_EXTENDED_DATA:
        /* The Shell has no separate stderr stream.  Validate the framing and
         * close on an unsupported extended-data type rather than silently
         * injecting it into command input. */
        if (len < 9) return xssh_channel_protocol_error(adapter);
        return xssh_channel_protocol_error(adapter);
    case SSH_MSG_CHANNEL_OPEN_CONFIRMATION:
    case SSH_MSG_CHANNEL_OPEN_FAILURE:
    case SSH_MSG_CHANNEL_SUCCESS:
    case SSH_MSG_CHANNEL_FAILURE:
        return XConsoleResult_Ok;
    case SSH_MSG_CHANNEL_EOF:
        return xssh_handle_channel_eof(adapter, payload, len);
    case SSH_MSG_CHANNEL_CLOSE:
        return xssh_handle_channel_close(adapter, payload, len);
    default:
        return XConsoleResult_Ok;
    }
}

static XConsoleResult xssh_handle_payload(XConsoleShellSshAdapter* adapter,
                                          const uint8_t* payload, size_t len)
{
    uint8_t msg;
    if (len < 1) return XConsoleResult_Failed;
    msg = payload[0];
    switch (msg) {
    case SSH_MSG_DISCONNECT:
        adapter->closed = true;
        return XConsoleResult_Ok;
    case SSH_MSG_IGNORE:
    case SSH_MSG_DEBUG:
    case SSH_MSG_UNIMPLEMENTED:
    case SSH_MSG_EXT_INFO:
        return XConsoleResult_Ok;
    default:
        break;
    }
    switch (adapter->state) {
    case XSshState_KexInit:
        if (msg != SSH_MSG_KEXINIT) return XConsoleResult_Failed;
        if (len > sizeof(adapter->clientKexInit)) return XConsoleResult_Failed;
        memcpy(adapter->clientKexInit, payload, len);
        adapter->clientKexInitLen = len;
        if (!xssh_check_kexinit(adapter)) return XConsoleResult_Failed;
        adapter->state = XSshState_KexEcdh;
        return XConsoleResult_Ok;
    case XSshState_KexEcdh:
        if (msg != SSH_MSG_KEX_ECDH_INIT) return XConsoleResult_Failed;
        return xssh_handle_kex_ecdh_init(adapter, payload, len);
    case XSshState_NewKeys:
        if (msg != SSH_MSG_NEWKEYS) return XConsoleResult_Failed;
        adapter->recvEncrypted = true;
        adapter->state = XSshState_Service;
        return XConsoleResult_Ok;
    case XSshState_Service:
        if (msg != SSH_MSG_SERVICE_REQUEST) return XConsoleResult_Failed;
        return xssh_handle_service_request(adapter, payload, len);
    case XSshState_Userauth:
        if (msg != SSH_MSG_USERAUTH_REQUEST) return XConsoleResult_Failed;
        return xssh_handle_userauth_request(adapter, payload, len);
    case XSshState_Channel:
        return xssh_handle_channel(adapter, payload, len);
    default:
        return XConsoleResult_Failed;
    }
}

/* ------------------------------------------------------------------ */
/* Shell I/O 回调                                                     */
/* ------------------------------------------------------------------ */

static size_t xssh_channel_pending_size(const XConsoleShellSshAdapter* adapter)
{
    if (!adapter || adapter->channelTxPendingOffset > adapter->channelTxPendingLen)
        return 0;
    return adapter->channelTxPendingLen - adapter->channelTxPendingOffset;
}

static void xssh_channel_pending_compact(XConsoleShellSshAdapter* adapter)
{
    size_t pending;
    if (!adapter) return;
    pending = xssh_channel_pending_size(adapter);
    if (pending && adapter->channelTxPendingOffset)
        memmove(adapter->channelTxPending,
                adapter->channelTxPending + adapter->channelTxPendingOffset, pending);
    adapter->channelTxPendingOffset = 0;
    adapter->channelTxPendingLen = pending;
}

/* 向通道待发送缓冲追加一个字节；缓冲满时先尝试发送并压缩，仍放不下返回 false。 */
static bool xssh_channel_pending_append(XConsoleShellSshAdapter* adapter,
                                        uint8_t byte)
{
    if (!adapter) return false;
    if (adapter->channelTxPendingLen >= sizeof(adapter->channelTxPending)) {
        (void)xssh_flush_channel_pending(adapter);
        xssh_channel_pending_compact(adapter);
    }
    if (adapter->channelTxPendingLen >= sizeof(adapter->channelTxPending))
        return false;
    adapter->channelTxPending[adapter->channelTxPendingLen++] = byte;
    return true;
}

static bool xssh_send_channel_data_now(XConsoleShellSshAdapter* adapter,
                                       const uint8_t* data, size_t len)
{
    uint8_t payload[XSSH_MAX_PACKET];
    size_t off = 0;
    if (!adapter || !data || len == 0 || len > XSSH_MAX_PACKET - 32 ||
        !adapter->channelOpen || adapter->localClose || adapter->remoteClose ||
        len > adapter->clientWindow || len > adapter->clientMaxPacket)
        return false;
    /* Flush a previous packet before appending another one.  This keeps the
     * encoded transport buffer bounded even when a command emits many chunks.
     * A non-blocking transport may leave the previous bytes queued; in that
     * case the caller retains the channel data in channelTxPending. */
    if (!xssh_flush(adapter)) return false;
    if (adapter->txOutLen != 0) return false;
    payload[off++] = SSH_MSG_CHANNEL_DATA;
    xssh_write_u32(payload + off, adapter->remoteChannel);
    off += 4;
    xssh_write_string(payload, &off, data, len);
    if (!xssh_send_packet(adapter, payload, off)) return false;
    adapter->clientWindow -= (uint32_t)len;
    (void)xssh_flush(adapter);
    return true;
}

static bool xssh_flush_channel_pending(XConsoleShellSshAdapter* adapter)
{
    if (!adapter) return false;
    xssh_channel_pending_compact(adapter);
    while (xssh_channel_pending_size(adapter) && adapter->clientWindow &&
           !adapter->closed && !adapter->localClose && !adapter->remoteClose) {
        size_t chunk = xssh_channel_pending_size(adapter);
        if (chunk > XSSH_CHANNEL_DATA_CHUNK) chunk = XSSH_CHANNEL_DATA_CHUNK;
        if (chunk > adapter->clientWindow) chunk = adapter->clientWindow;
        if (chunk > adapter->clientMaxPacket) chunk = adapter->clientMaxPacket;
        if (chunk == 0) break;
        if (!xssh_send_channel_data_now(adapter,
                                        adapter->channelTxPending +
                                            adapter->channelTxPendingOffset,
                                        chunk)) {
            if (adapter->closed) return false;
            break;
        }
        adapter->channelTxPendingOffset += chunk;
        if (adapter->channelTxPendingOffset == adapter->channelTxPendingLen)
            adapter->channelTxPendingOffset = adapter->channelTxPendingLen = 0;
    }
    return !adapter->closed;
}

static int64_t xssh_shell_write(void* userData, const void* data, size_t size)
{
    XConsoleShellSshAdapter* adapter = (XConsoleShellSshAdapter*)userData;
    const uint8_t* bytes = (const uint8_t*)data;
    size_t i;
    if (!adapter || adapter->closed || !adapter->channelOpen ||
        !adapter->shellStarted || adapter->localEof || adapter->localClose ||
        adapter->remoteClose ||
        (!data && size))
        return -1;
    if (size == 0) return 0;
    (void)xssh_flush_channel_pending(adapter);
    xssh_channel_pending_compact(adapter);
    /* SSH 客户端的本地终端在会话期间处于 raw 模式，只有换行符不会把光标
     * 移回行首；因此把单独出现的换行规范为回车加换行，避免多行输出逐行
     * 右移。遇到已带回车换行（如输入回显路径）保持原样，不重复插入回车。 */
    for (i = 0; i < size; ++i) {
        uint8_t byte = bytes[i];
        if (byte == '\n' && (i == 0u || bytes[i - 1u] != '\r')) {
            if (!xssh_channel_pending_append(adapter, '\r')) return -1;
        }
        if (!xssh_channel_pending_append(adapter, byte)) return -1;
    }
    if (!xssh_flush_channel_pending(adapter)) return -1;
    return (int64_t)size;
}

static bool xssh_shell_flush(void* userData)
{
    XConsoleShellSshAdapter* adapter = (XConsoleShellSshAdapter*)userData;
    if (!adapter) return false;
    if (!xssh_flush_channel_pending(adapter)) return false;
    if (!xssh_flush(adapter)) return false;
    return !adapter->transport.flush ||
           adapter->transport.flush(adapter->transport.userData);
}

static bool xssh_shell_cancelled(void* userData)
{
    XConsoleShellSshAdapter* adapter = (XConsoleShellSshAdapter*)userData;
    return adapter && adapter->closed;
}

static bool xssh_shell_input_echo(void* userData, bool enabled)
{
    XConsoleShellSshAdapter* adapter = (XConsoleShellSshAdapter*)userData;
    if (!adapter) return false;
    adapter->inputEcho = enabled;
    adapter->echoPendingCr = false;
    adapter->echoEscape = false;
    return true;
}

static bool xssh_shell_terminal_size(void* userData, int* columns, int* rows)
{
    XConsoleShellSshAdapter* adapter = (XConsoleShellSshAdapter*)userData;
    if (!adapter || !columns || !rows || !adapter->ptyRequested ||
        adapter->ptyColumns == 0 || adapter->ptyRows == 0 ||
        adapter->ptyColumns > 0x7fffffffU || adapter->ptyRows > 0x7fffffffU)
        return false;
    *columns = (int)adapter->ptyColumns;
    *rows = (int)adapter->ptyRows;
    return true;
}

/* 发送交互提示符：当前连接会话已登录显示“用户名> ”，未登录或未启用登录
 * 显示默认名。SSH 每个连接绑定独立会话，必须从 adapter->session 读取，不能
 * 读取 Shell 默认会话，否则登录后提示符不会切换到实际登录用户。 */
static void xssh_shell_emit_prompt(XConsoleShellSshAdapter* adapter)
{
    const char* user;
    char prompt[XCONSOLE_SHELL_LOGIN_NAME_SIZE + 2u];
    size_t len;
    if (!adapter || !adapter->shell)
        return;
    user = (adapter->session && adapter->session->authenticated &&
            adapter->session->userName[0])
               ? adapter->session->userName
               : (const char*)XCONSOLE_SHELL_DEFAULT_PROMPT_NAME;
    len = strlen(user);
    if (len >= sizeof(prompt) - 2u)
        len = sizeof(prompt) - 2u;
    memcpy(prompt, user, len);
    prompt[len++] = '>';
    prompt[len++] = ' ';
    (void)xssh_shell_write(adapter, prompt, len);
}

static void xssh_shell_prompt(void* userData, XConsoleShell* shell)
{
    XConsoleShellSshAdapter* adapter = (XConsoleShellSshAdapter*)userData;
    (void)shell;
    if (!adapter || adapter->closed || !adapter->channelOpen ||
        !adapter->shellStarted || adapter->localClose || adapter->remoteClose ||
        !XConsoleShell_isRunning(adapter->shell))
        return;
    xssh_shell_emit_prompt(adapter);
}

#if XCONSOLE_SHELL_LOG_ON
static void xssh_shell_log(void* userData, const XConsoleShellSession* session,
                           const void* data, size_t size)
{
    XConsoleShellSshAdapter* adapter = (XConsoleShellSshAdapter*)userData;
    if (adapter && adapter->transport.log)
        adapter->transport.log(adapter->transport.userData, session, data, size);
}
#endif

#if XCONSOLE_SHELL_AUDIT_ON
static void xssh_shell_audit(void* userData, const XConsoleShellSession* session,
                             const XConsoleCommand* command, int result)
{
    XConsoleShellSshAdapter* adapter = (XConsoleShellSshAdapter*)userData;
    if (adapter && adapter->transport.audit)
        adapter->transport.audit(adapter->transport.userData, session, command, result);
}
#endif

/* ------------------------------------------------------------------ */
/* 输入处理                                                            */
/* ------------------------------------------------------------------ */

static XConsoleResult xssh_process_one(XConsoleShellSshAdapter* adapter)
{
    if (adapter->state == XSshState_Version) {
        const uint8_t* nl;
        size_t lineLen;
        if (adapter->rxLen == 0) return XConsoleResult_Ok;
        nl = (const uint8_t*)memchr(adapter->rxBuf, '\n', adapter->rxLen);
        if (!nl) {
            if (adapter->rxLen > XSSH_VERSION_MAX) return XConsoleResult_Failed;
            return XConsoleResult_Ok;
        }
        lineLen = (size_t)(nl - adapter->rxBuf) + 1;
        if (lineLen < 9 ||
            (memcmp(adapter->rxBuf, "SSH-2.0-", 8) != 0 &&
             memcmp(adapter->rxBuf, "SSH-1.99-", 9) != 0))
            return XConsoleResult_Failed;
        if (lineLen > sizeof(adapter->clientVersion)) return XConsoleResult_Failed;
        memcpy(adapter->clientVersion, adapter->rxBuf, lineLen);
        adapter->clientVersionLen = lineLen;
        memmove(adapter->rxBuf, adapter->rxBuf + lineLen, adapter->rxLen - lineLen);
        adapter->rxLen -= lineLen;
        adapter->state = XSshState_KexInit;
        XSSH_DBG("version parsed len=%u remaining=%u\n", (unsigned)adapter->clientVersionLen, (unsigned)adapter->rxLen);
        return XConsoleResult_Ok;
    }

    if (adapter->recvEncrypted) {
        size_t totalCipher;
        uint8_t macInput[XSSH_RX_CAPACITY + 4];
        uint8_t macCalc[XSSH_MAC_LEN];
        uint8_t plain[XSSH_RX_CAPACITY + 4];
        size_t macLen;
        size_t outLen;
        size_t padLen;
        size_t payloadLen;

        if (!adapter->packetLenKnown) {
            if (adapter->rxLen < 4) return XConsoleResult_Ok;
            if (!adapter->decActive) {
                adapter->closed = true;
                return XConsoleResult_Failed;
            }
            if (psa_cipher_update(&adapter->decOp, adapter->rxBuf, 4,
                                  adapter->lenPlain, sizeof(adapter->lenPlain), &outLen) != PSA_SUCCESS ||
                outLen != 4) {
                adapter->closed = true;
                return XConsoleResult_Failed;
            }
            xssh_get_u32(adapter->lenPlain, &adapter->packetLen);
            /* packet_length includes padding_length, payload, and padding;
             * with AES-CTR the complete packet (including the 4-byte length)
             * must be aligned to the 16-byte cipher block size. */
            if (adapter->packetLen < 5 || adapter->packetLen > XSSH_MAX_PACKET ||
                ((adapter->packetLen + 4u) & 15u) != 0u) {
                adapter->closed = true;
                return XConsoleResult_Failed;
            }
            adapter->packetLenKnown = true;
        }
        totalCipher = 4 + adapter->packetLen;
        if (adapter->rxLen < totalCipher + XSSH_MAC_LEN) return XConsoleResult_Ok;

        if (psa_cipher_update(&adapter->decOp, adapter->rxBuf + 4, totalCipher - 4,
                              plain + 4, totalCipher - 4, &outLen) != PSA_SUCCESS ||
            outLen != totalCipher - 4) {
            adapter->closed = true;
            return XConsoleResult_Failed;
        }
        memcpy(plain, adapter->lenPlain, 4);

        /* The MAC covers the sequence number and the complete plaintext
         * packet, including packet_length, padding_length, payload, and
         * padding.  The encrypted packet must be decrypted before checking
         * the MAC; otherwise CTR ciphertext bytes are authenticated. */
        xssh_write_u32(macInput, adapter->recvSeq);
        memcpy(macInput + 4, plain, totalCipher);
        if (psa_mac_compute(adapter->macKeyC2S, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                            macInput, 4 + totalCipher, macCalc, sizeof(macCalc), &macLen) != PSA_SUCCESS ||
            macLen != XSSH_MAC_LEN ||
            !xssh_const_equal(macCalc, adapter->rxBuf + totalCipher, XSSH_MAC_LEN)) {
            adapter->closed = true;
            return XConsoleResult_Failed;
        }

        memmove(adapter->rxBuf, adapter->rxBuf + totalCipher + XSSH_MAC_LEN,
                adapter->rxLen - (totalCipher + XSSH_MAC_LEN));
        adapter->rxLen -= totalCipher + XSSH_MAC_LEN;
        adapter->packetLenKnown = false;
        ++adapter->recvSeq;

        padLen = plain[4];
        if (padLen < 4 || (size_t)padLen >= adapter->packetLen) {
            adapter->closed = true;
            return XConsoleResult_Failed;
        }
        payloadLen = adapter->packetLen - padLen - 1;
        if (payloadLen > sizeof(adapter->payloadBuf)) {
            adapter->closed = true;
            return XConsoleResult_Failed;
        }
        memcpy(adapter->payloadBuf, plain + 5, payloadLen);
        return xssh_handle_payload(adapter, adapter->payloadBuf, payloadLen);
    } else {
        uint32_t plen;
        size_t padLen;
        size_t payloadLen;
        if (adapter->rxLen < 4) return XConsoleResult_Ok;
        xssh_get_u32(adapter->rxBuf, &plen);
        XSSH_DBG("rx len=%u plen=%u state=%d\n", (unsigned)adapter->rxLen, (unsigned)plen, (int)adapter->state);
        if (plen == 0 || plen > XSSH_MAX_PACKET) return XConsoleResult_Failed;
        if (adapter->rxLen < 4 + plen) return XConsoleResult_Ok;
        padLen = adapter->rxBuf[4];
        if (padLen < 4 || (size_t)padLen >= plen) return XConsoleResult_Failed;
        payloadLen = plen - padLen - 1;
        if (payloadLen > sizeof(adapter->payloadBuf)) return XConsoleResult_Failed;
        memcpy(adapter->payloadBuf, adapter->rxBuf + 5, payloadLen);
        memmove(adapter->rxBuf, adapter->rxBuf + 4 + plen, adapter->rxLen - (4 + plen));
        adapter->rxLen -= 4 + plen;
        ++adapter->recvSeq;
        return xssh_handle_payload(adapter, adapter->payloadBuf, payloadLen);
    }
}

/* ------------------------------------------------------------------ */
/* 公共 API                                                           */
/* ------------------------------------------------------------------ */

XConsoleShellSshAdapter* XConsoleShellSshAdapter_create(const XConsoleShellIo* transport)
{
    XConsoleShellSshAdapter* adapter;
    if (!transport || !transport->write) return NULL;
    if (psa_crypto_init() != PSA_SUCCESS) return NULL;
    adapter = (XConsoleShellSshAdapter*)XCalloc_System(1, sizeof(*adapter));
    if (!adapter) return NULL;
    adapter->transport = *transport;
    adapter->state = XSshState_Version;
    adapter->localChannel = 0;
    adapter->inputEcho = true;
    if (!xssh_generate_host_key(adapter)) goto fail;
    if (!xssh_send_raw(adapter, (const uint8_t*)XSSH_VERSION, strlen(XSSH_VERSION))) goto fail;
    if (!xssh_send_kexinit(adapter)) goto fail;
    if (!xssh_flush(adapter)) goto fail;
    return adapter;
fail:
    XConsoleShellSshAdapter_destroy(adapter);
    return NULL;
}

void XConsoleShellSshAdapter_destroy(XConsoleShellSshAdapter* adapter)
{
    if (!adapter) return;
    if (adapter->encActive) {
        (void)psa_cipher_abort(&adapter->encOp);
        adapter->encActive = false;
    }
    if (adapter->decActive) {
        (void)psa_cipher_abort(&adapter->decOp);
        adapter->decActive = false;
    }
    if (adapter->hostKeyShared) {
        if (g_xssh_hostKeyRefs) --g_xssh_hostKeyRefs;
        if (g_xssh_hostKeyRefs == 0 && g_xssh_hostKey) {
            (void)psa_destroy_key(g_xssh_hostKey);
            g_xssh_hostKey = 0;
            g_xssh_hostKeyBlobLen = 0;
            memset(g_xssh_hostKeyBlob, 0, sizeof(g_xssh_hostKeyBlob));
        }
    } else {
        (void)psa_destroy_key(adapter->hostKey);
    }
    (void)psa_destroy_key(adapter->ecdhKey);
    (void)psa_destroy_key(adapter->cipherKeyC2S);
    (void)psa_destroy_key(adapter->cipherKeyS2C);
    (void)psa_destroy_key(adapter->macKeyC2S);
    (void)psa_destroy_key(adapter->macKeyS2C);
    XFree_System(adapter);
}

bool XConsoleShellSshAdapter_makeIo(XConsoleShellSshAdapter* adapter, XConsoleShellIo* io)
{
    if (!adapter || !io || !adapter->transport.write) return false;
    memset(io, 0, sizeof(*io));
    io->write = xssh_shell_write;
    io->flush = xssh_shell_flush;
    io->cancelled = xssh_shell_cancelled;
    io->inputEcho = xssh_shell_input_echo;
    io->terminalSize = xssh_shell_terminal_size;
    io->prompt = xssh_shell_prompt;
#if XCONSOLE_SHELL_LOG_ON
    io->log = xssh_shell_log;
#endif
#if XCONSOLE_SHELL_AUDIT_ON
    io->audit = xssh_shell_audit;
#endif
    io->userData = adapter;
    return true;
}

void XConsoleShellSshAdapter_setSession(XConsoleShellSshAdapter* adapter,
                                        XConsoleShell* shell,
                                        XConsoleShellSession* session)
{
    if (!adapter) return;
    adapter->shell = shell;
    adapter->session = session;
}

XConsoleResult XConsoleShellSshAdapter_feedData(XConsoleShellSshAdapter* adapter,
                                                XConsoleShell* shell,
                                                XConsoleShellSession* session,
                                                const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    if (!adapter || !shell || (!data && size)) return XConsoleResult_InvalidArgument;
    if (adapter->closed) return XConsoleResult_Failed;
    adapter->shell = shell;
    adapter->session = session;
    if (size > sizeof(adapter->rxBuf) - adapter->rxLen) {
        adapter->closed = true;
        return XConsoleResult_ResourceLimit;
    }
    if (size) memcpy(adapter->rxBuf + adapter->rxLen, bytes, size);
    adapter->rxLen += size;

    while (!adapter->closed) {
        size_t before = adapter->rxLen;
        XConsoleResult r = xssh_process_one(adapter);
        if (r < XConsoleResult_Ok) {
            adapter->closed = true;
            return r;
        }
        if (adapter->rxLen == before) break;
    }
    if (!xssh_flush(adapter)) return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}

bool XConsoleShellSshAdapter_flush(XConsoleShellSshAdapter* adapter)
{
    if (!adapter || adapter->closed) return false;
    if (!xssh_flush(adapter)) return false;
    if (!xssh_flush_channel_pending(adapter)) return false;
    return xssh_flush(adapter);
}

bool XConsoleShellSshAdapter_isClosed(const XConsoleShellSshAdapter* adapter)
{
    return !adapter || adapter->closed;
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON */
