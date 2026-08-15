/**
 * @file XSshServer.c
 * @brief SSH 服务端协议栈实现（XSshServer）。
 * @details
 * 实现 SSH 2.0 传输层：版本协商、KEXINIT、curve25519-sha256 / ECDH P-256
 * 密钥交换、AES-256/192/128-CTR 加密、HMAC-SHA2-256 完整性、password
 * 用户认证和 session 通道。
 * 适配器不创建线程、不监听端口。底层字节传输由 XIODevice 提供；
 * 与宿主的交互全部通过 XObject 信号槽完成：协议栈向宿主发射
 * bytesReceived/authenticateRequested/isRunningRequested 等请求信号，
 * 宿主在槽中同步处理后通过 setter 回填结果。收到网络字节后由
 * XSshServer_feedData 驱动状态机。
 * 主机密钥首次启动生成并持久化到 XSSH_HOSTKEY_FILE 文件，
 * 之后跨进程重启保持不变，避免客户端 known_hosts 指纹频繁变化。
 */

#include "XSshServer.h"
#include "XSshCrypto.h"

#if XPROTOCOL_ON && XSSH_ON && XSSH_SERVER_ON

#include "XVarList.h"
#include "XFileSystem.h"
#include "XMemory.h"
#include "XRandomGenerator.h"
#include "XString.h"
#include "XCryptographic.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#ifdef XSSH_DEBUG
#define XSSH_DBG(...) do { fprintf(stderr, "[XSSH] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define XSSH_DBG(...) do { } while (0)
#endif

/* ------------------------------------------------------------------ */
/* 常量                                                               */
/* ------------------------------------------------------------------ */

enum {
    XSSH_RX_CAPACITY = XSSH_CFG_RX_CAPACITY,
    XSSH_TX_OUT_CAPACITY = XSSH_CFG_TX_OUT_CAPACITY,
    XSSH_MAX_PACKET = XSSH_CFG_MAX_PACKET,
    XSSH_MAX_PAYLOAD = XSSH_CFG_MAX_PAYLOAD,
    XSSH_HASH_LEN = 32,
    XSSH_MAC_LEN = 32,
    XSSH_POINT_LEN = 65,
    XSSH_CURVE25519_PUBLIC_LEN = 32,
    XSSH_MAX_DH_PUBLIC_LEN = 513,   /* 4096 位 MODP 公钥 + 可能的前导 0 */
    XSSH_MAX_CIPHER_KEY_LEN = 32,
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

typedef struct XSshServerData {
    XSshState state;
    bool closed;

    /* 版本与 KEX 数据 */
    uint8_t clientVersion[XSSH_VERSION_MAX];
    size_t clientVersionLen;
    uint8_t clientKexInit[XSSH_RX_CAPACITY];
    size_t clientKexInitLen;
    uint8_t serverKexInit[XSSH_RX_CAPACITY];
    size_t serverKexInitLen;
    XSshKexAlgorithm kexAlgorithm;
    XSshCipherAlgorithm cipherC2S;
    XSshCipherAlgorithm cipherS2C;

    /* 主机密钥 */
    XCryptographic_Key hostKey;
    bool hostKeyShared;
    uint8_t hostKeyBlob[256];
    size_t hostKeyBlobLen;

    /* 服务端临时 ECDH / 有限域 DH */
    XCryptographic_Key ecdhKey;
    uint8_t ffdhPriv[32];                    /* 自研 FFDH 临时私钥（256 位指数） */
    size_t ffdhPrivLen;
    uint8_t clientPublicBlob[XSSH_MAX_DH_PUBLIC_LEN];
    size_t clientPublicBlobLen;
    uint8_t sharedSecret[XSSH_MAX_DH_PUBLIC_LEN];
    size_t sharedSecretLen;

    /* 交换哈希与会话 ID */
    uint8_t exchangeHash[XSSH_HASH_LEN];
    uint8_t sessionId[XSSH_HASH_LEN];

    /* 会话密钥 */
    XCryptographic_Key cipherKeyC2S;
    XCryptographic_Key cipherKeyS2C;
    uint8_t macKeyC2S[XSSH_MAC_LEN];
    uint8_t macKeyS2C[XSSH_MAC_LEN];
    XCryptographic_CipherOperation encOp;
    XCryptographic_CipherOperation decOp;
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
    bool echoEscape;
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
} XSshServerData;

#define XSSH_DATA(self) ((XSshServerData*)((self)->m_data))

/* 服务端密钥交换算法表：在共享算法表基础上增加 RFC 3526 MODP 有限域 DH，
 * 用于兼容 Xshell 默认的 diffie-hellman-group14/16 算法。客户端（XSshClient）
 * 仍使用共享算法表，因此不会主动选择这些 DH 算法。 */
#if XSSH_FFDH_GROUPS_ON
static const XSshNamedAlgorithm xssh_server_kex_algorithms[] = {
#if XCRYPTOGRAPHIC_X25519_ON && XCRYPTOGRAPHIC_SHA256_ON
    { "curve25519-sha256", XSshKexAlgorithm_Curve25519Sha256 },
#endif
#if XCRYPTOGRAPHIC_ECDH_NISTP256_ON && XCRYPTOGRAPHIC_SHA256_ON
    { "ecdh-sha2-nistp256", XSshKexAlgorithm_EcdhSha2Nistp256 },
#endif
    { "diffie-hellman-group14-sha256", XSshKexAlgorithm_DhGroup14Sha256 },
    { NULL, XSshKexAlgorithm_None }
};
#else
static const XSshNamedAlgorithm xssh_server_kex_algorithms[] = {
#if XCRYPTOGRAPHIC_X25519_ON && XCRYPTOGRAPHIC_SHA256_ON
    { "curve25519-sha256", XSshKexAlgorithm_Curve25519Sha256 },
#endif
#if XCRYPTOGRAPHIC_ECDH_NISTP256_ON && XCRYPTOGRAPHIC_SHA256_ON
    { "ecdh-sha2-nistp256", XSshKexAlgorithm_EcdhSha2Nistp256 },
#endif
    { NULL, XSshKexAlgorithm_None }
};
#endif


/* A volatile PSA key is shared by connections during one process lifetime.
 * This keeps the SSH host identity stable across reconnects while avoiding a
 * process-wide private-key byte buffer.  Callers still serialize PSA access
 * according to the library's normal threading contract. */
static XCryptographic_Key g_xssh_hostKey;
static uint8_t g_xssh_hostKeyBlob[256];
static size_t g_xssh_hostKeyBlobLen;
static size_t g_xssh_hostKeyRefs;

static bool xssh_key_is_valid(const XCryptographic_Key* key)
{
    return key && key->type != XCryptographic_KeyType_None;
}

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
    return XSSH_HOSTKEY_FILE;
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


/* ------------------------------------------------------------------ */
/* 发送路径                                                           */
/* ------------------------------------------------------------------ */

static bool xssh_send_raw(XSshServer* adapter,
                          const uint8_t* data, size_t size)
{
    if (!adapter) return false;
    if (size > sizeof(XSSH_DATA(adapter)->txOut) - XSSH_DATA(adapter)->txOutLen) {
        XSSH_DATA(adapter)->closed = true;
        return false;
    }
    if (size) memcpy(XSSH_DATA(adapter)->txOut + XSSH_DATA(adapter)->txOutLen, data, size);
    XSSH_DATA(adapter)->txOutLen += size;
    return true;
}

static bool xssh_flush(XSshServer* adapter)
{
    if (!adapter) return false;
    XSSH_DBG("flush start outLen=%u off=%u\n", (unsigned)XSSH_DATA(adapter)->txOutLen, (unsigned)XSSH_DATA(adapter)->txOutOffset);
    while (XSSH_DATA(adapter)->txOutOffset < XSSH_DATA(adapter)->txOutLen) {
        int64_t n;
        if (!adapter->m_device) {
            XSSH_DATA(adapter)->closed = true;
            return false;
        }
        n = XIODevice_write_1(adapter->m_device,
                              (const char*)(XSSH_DATA(adapter)->txOut + XSSH_DATA(adapter)->txOutOffset),
                              (int64_t)(XSSH_DATA(adapter)->txOutLen - XSSH_DATA(adapter)->txOutOffset));
        if (n < 0) {
            XSSH_DATA(adapter)->closed = true;
            return false;
        }
        if (n == 0) break;
        XSSH_DATA(adapter)->txOutOffset += (size_t)n;
    }
    XSSH_DBG("flush end outLen=%u off=%u\n", (unsigned)XSSH_DATA(adapter)->txOutLen, (unsigned)XSSH_DATA(adapter)->txOutOffset);
    if (XSSH_DATA(adapter)->txOutOffset == XSSH_DATA(adapter)->txOutLen) {
        XSSH_DATA(adapter)->txOutOffset = XSSH_DATA(adapter)->txOutLen = 0;
    } else if (XSSH_DATA(adapter)->txOutOffset > 0) {
        memmove(XSSH_DATA(adapter)->txOut, XSSH_DATA(adapter)->txOut + XSSH_DATA(adapter)->txOutOffset,
                XSSH_DATA(adapter)->txOutLen - XSSH_DATA(adapter)->txOutOffset);
        XSSH_DATA(adapter)->txOutLen -= XSSH_DATA(adapter)->txOutOffset;
        XSSH_DATA(adapter)->txOutOffset = 0;
    }
    return true;
}

static bool xssh_send_packet(XSshServer* adapter,
                             const uint8_t* payload, size_t payloadLen)
{
    uint8_t frame[XSSH_MAX_PACKET + 16];
    uint8_t encoded[XSSH_MAX_PACKET + 16];
    uint8_t macInput[XSSH_MAX_PACKET + 16 + 4];
    uint8_t mac[XSSH_MAC_LEN];
    size_t total;
    size_t padLen;

    if (!adapter || payloadLen > XSSH_MAX_PACKET - 32) return false;

    total = 4 + 1 + payloadLen + 4;
    total = (total + 15) & ~((size_t)15);
    padLen = total - (4 + 1 + payloadLen);

    xssh_write_u32(frame, (uint32_t)(payloadLen + 1 + padLen));
    frame[4] = (uint8_t)padLen;
    memcpy(frame + 5, payload, payloadLen);
    if (!XRandomGenerator_fillSecure(frame + 5 + payloadLen, padLen)) {
        XSSH_DATA(adapter)->closed = true;
        return false;
    }

    if (XSSH_DATA(adapter)->sendEncrypted) {
        if (!XSSH_DATA(adapter)->encActive) {
            XSSH_DATA(adapter)->closed = true;
            return false;
        }
        /* SSH MAC is computed over the sequence number followed by the
         * unencrypted packet (RFC 4253 section 6.4).  CTR encryption is
         * applied only after the MAC has been computed. */
        xssh_write_u32(macInput, XSSH_DATA(adapter)->sendSeq);
        memcpy(macInput + 4, frame, total);
        if (!xssh_hmac_sha256(XSSH_DATA(adapter)->macKeyS2C,
                              sizeof(XSSH_DATA(adapter)->macKeyS2C),
                              macInput, 4 + total, mac)) {
            XSSH_DATA(adapter)->closed = true;
            return false;
        }
        if (XCryptographic_aesCtrUpdateInto(&XSSH_DATA(adapter)->encOp,
                (char*)encoded, sizeof(encoded),
                XByteArrayView_create_data( frame, (int64_t)total )).m_size != (int64_t)total) {
            XSSH_DATA(adapter)->closed = true;
            return false;
        }
        if (!xssh_send_raw(adapter, encoded, total)) return false;
        if (!xssh_send_raw(adapter, mac, sizeof(mac))) return false;
    } else {
        if (!xssh_send_raw(adapter, frame, total)) return false;
    }
    ++XSSH_DATA(adapter)->sendSeq;
    return true;
}

/* ------------------------------------------------------------------ */
/* 密钥派生与主机密钥                                                  */
/* ------------------------------------------------------------------ */

static bool xssh_derive_key(XSshServer* adapter, char letter,
                            uint8_t* out, size_t outLen)
{
    uint8_t input[4 + XSSH_MAX_DH_PUBLIC_LEN + XSSH_HASH_LEN + 1 + XSSH_HASH_LEN];
    uint8_t hash[XSSH_HASH_LEN];
    size_t off = 0;
    if (!adapter || !out || outLen > XSSH_HASH_LEN ||
        XSSH_DATA(adapter)->sharedSecretLen > XSSH_MAX_DH_PUBLIC_LEN)
        return false;
    /* K is an SSH mpint in the key derivation input, including its uint32
     * length prefix.  The exchange hash uses the same representation. */
    xssh_write_u32(input + off, (uint32_t)XSSH_DATA(adapter)->sharedSecretLen);
    off += 4;
    memcpy(input + off, XSSH_DATA(adapter)->sharedSecret, XSSH_DATA(adapter)->sharedSecretLen);
    off += XSSH_DATA(adapter)->sharedSecretLen;
    memcpy(input + off, XSSH_DATA(adapter)->exchangeHash, XSSH_HASH_LEN);
    off += XSSH_HASH_LEN;
    input[off++] = (uint8_t)letter;
    memcpy(input + off, XSSH_DATA(adapter)->sessionId, XSSH_HASH_LEN);
    off += XSSH_HASH_LEN;
    if (!xssh_hash_sha256(input, off, hash))
        return false;
    memcpy(out, hash, outLen);
    return true;
}

static bool xssh_setup_keys(XSshServer* adapter)
{
    uint8_t ivC2S[16], ivS2C[16];
    uint8_t encC2S[XSSH_MAX_CIPHER_KEY_LEN], encS2C[XSSH_MAX_CIPHER_KEY_LEN];
    uint8_t macC2S[XSSH_MAC_LEN], macS2C[XSSH_MAC_LEN];
    size_t encC2SLen;
    size_t encS2CLen;

    if (!adapter) return false;
    encC2SLen = xssh_cipher_key_size(XSSH_DATA(adapter)->cipherC2S);
    encS2CLen = xssh_cipher_key_size(XSSH_DATA(adapter)->cipherS2C);
    if (encC2SLen == 0 || encS2CLen == 0) return false;

    if (!xssh_derive_key(adapter, 'A', ivC2S, sizeof(ivC2S))) return false;
    if (!xssh_derive_key(adapter, 'B', ivS2C, sizeof(ivS2C))) return false;
    if (!xssh_derive_key(adapter, 'C', encC2S, encC2SLen)) return false;
    if (!xssh_derive_key(adapter, 'D', encS2C, encS2CLen)) return false;
    if (!xssh_derive_key(adapter, 'E', macC2S, sizeof(macC2S))) return false;
    if (!xssh_derive_key(adapter, 'F', macS2C, sizeof(macS2C))) return false;

    if (!XCryptographic_aesCtrImportKey(XByteArrayView_create_data( encC2S, (int64_t)encC2SLen ),
                                        &XSSH_DATA(adapter)->cipherKeyC2S) ||
        !XCryptographic_aesCtrImportKey(XByteArrayView_create_data( encS2C, (int64_t)encS2CLen ),
                                        &XSSH_DATA(adapter)->cipherKeyS2C))
        return false;
    memcpy(XSSH_DATA(adapter)->macKeyC2S, macC2S, sizeof(macC2S));
    memcpy(XSSH_DATA(adapter)->macKeyS2C, macS2C, sizeof(macS2C));

    if (!XCryptographic_aesCtrSetup(&XSSH_DATA(adapter)->encOp,
                                    XSSH_DATA(adapter)->cipherKeyS2C, true,
                                    XByteArrayView_create_data( ivS2C, sizeof(ivS2C) )))
        return false;
    XSSH_DATA(adapter)->encActive = true;

    if (!XCryptographic_aesCtrSetup(&XSSH_DATA(adapter)->decOp,
                                    XSSH_DATA(adapter)->cipherKeyC2S, false,
                                    XByteArrayView_create_data( ivC2S, sizeof(ivC2S) )))
        return false;
    XSSH_DATA(adapter)->decActive = true;
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

static bool xssh_generate_host_key(XSshServer* adapter)
{
    uint8_t point[XSSH_POINT_LEN];
    size_t pointLen;
    uint8_t privateBytes[XSSH_HOSTKEY_MAX_BYTES];
    size_t privateLen = 0;

    if (!adapter) return false;
    if (xssh_key_is_valid(&g_xssh_hostKey)) {
        XSSH_DATA(adapter)->hostKey = g_xssh_hostKey;
        XSSH_DATA(adapter)->hostKeyShared = true;
        memcpy(XSSH_DATA(adapter)->hostKeyBlob, g_xssh_hostKeyBlob,
               g_xssh_hostKeyBlobLen);
        XSSH_DATA(adapter)->hostKeyBlobLen = g_xssh_hostKeyBlobLen;
        ++g_xssh_hostKeyRefs;
        return true;
    }

    /* 优先加载已持久化的主机密钥，保证跨进程重启后主机指纹不变。 */
    if (xssh_hostkey_load(privateBytes, sizeof(privateBytes), &privateLen)) {
        bool imported = XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_NistP256,
            XByteArrayView_create_data( privateBytes, (int64_t)privateLen ),
            &XSSH_DATA(adapter)->hostKey);
        memset(privateBytes, 0, sizeof(privateBytes));
        if (imported) {
            if ((pointLen = (size_t)XCryptographic_exportPublicKeyInto(
                    (char*)point, sizeof(point), XSSH_DATA(adapter)->hostKey).m_size) &&
                pointLen == XSSH_POINT_LEN &&
                xssh_build_key_blob(point, pointLen, XSSH_DATA(adapter)->hostKeyBlob,
                                    sizeof(XSSH_DATA(adapter)->hostKeyBlob),
                                    &XSSH_DATA(adapter)->hostKeyBlobLen)) {
                g_xssh_hostKey = XSSH_DATA(adapter)->hostKey;
                memcpy(g_xssh_hostKeyBlob, XSSH_DATA(adapter)->hostKeyBlob,
                       XSSH_DATA(adapter)->hostKeyBlobLen);
                g_xssh_hostKeyBlobLen = XSSH_DATA(adapter)->hostKeyBlobLen;
                g_xssh_hostKeyRefs = 1;
                XSSH_DATA(adapter)->hostKeyShared = true;
                return true;
            }
            XCryptographic_destroyKey(&XSSH_DATA(adapter)->hostKey);
            XCryptographic_destroyKey(&XSSH_DATA(adapter)->hostKey);
        }
    }

    /* 没有可用持久化密钥：生成新主机密钥并落盘。对 PSA 导出的私钥标量
     * 立即清零，避免在栈上长时间保留明文。 */
    if (!XCryptographic_ecdsaGenerateKey(XCryptographic_EcdsaAlgorithm_NistP256, &XSSH_DATA(adapter)->hostKey)) {
        XSSH_DBG("hostkey: generate failed\n");
        return false;
    }

    privateLen = (size_t)XCryptographic_ecdsaExportPrivateKeyInto(
        (char*)privateBytes, sizeof(privateBytes), XSSH_DATA(adapter)->hostKey).m_size;
    if (privateLen == 0 || privateLen > XSSH_HOSTKEY_MAX_BYTES) {
        XCryptographic_destroyKey(&XSSH_DATA(adapter)->hostKey);
        return false;
    }
    if (!xssh_hostkey_save(privateBytes, privateLen))
        XSSH_DBG("hostkey: save failed (host key will not persist)\n");
    memset(privateBytes, 0, sizeof(privateBytes));

    pointLen = (size_t)XCryptographic_exportPublicKeyInto(
        (char*)point, sizeof(point), XSSH_DATA(adapter)->hostKey).m_size;
    if (pointLen != XSSH_POINT_LEN) goto fail;
    if (!xssh_build_key_blob(point, pointLen, XSSH_DATA(adapter)->hostKeyBlob,
                             sizeof(XSSH_DATA(adapter)->hostKeyBlob), &XSSH_DATA(adapter)->hostKeyBlobLen))
        goto fail;

    g_xssh_hostKey = XSSH_DATA(adapter)->hostKey;
    memcpy(g_xssh_hostKeyBlob, XSSH_DATA(adapter)->hostKeyBlob, XSSH_DATA(adapter)->hostKeyBlobLen);
    g_xssh_hostKeyBlobLen = XSSH_DATA(adapter)->hostKeyBlobLen;
    g_xssh_hostKeyRefs = 1;
    XSSH_DATA(adapter)->hostKeyShared = true;
    return true;

fail:
    if (XSSH_DATA(adapter)->hostKeyShared) {
        if (g_xssh_hostKeyRefs) --g_xssh_hostKeyRefs;
        if (g_xssh_hostKeyRefs == 0 && xssh_key_is_valid(&g_xssh_hostKey)) {
            XCryptographic_destroyKey(&g_xssh_hostKey);
            XCryptographic_destroyKey(&g_xssh_hostKey);
            g_xssh_hostKeyBlobLen = 0;
            memset(g_xssh_hostKeyBlob, 0, sizeof(g_xssh_hostKeyBlob));
        }
    } else {
        XCryptographic_destroyKey(&XSSH_DATA(adapter)->hostKey);
    }
    XCryptographic_destroyKey(&XSSH_DATA(adapter)->hostKey);
    return false;
}

/* ------------------------------------------------------------------ */
/* 消息处理                                                            */
/* ------------------------------------------------------------------ */

static bool xssh_flush_channel_pending(XSshServer* adapter);
static bool xssh_query_authenticate(XSshServer* adapter, const char* user, const char* password);
static bool xssh_query_is_running(XSshServer* adapter);
static bool xssh_query_close_requested(XSshServer* adapter);
static bool xssh_query_suppress_prompt(XSshServer* adapter);
static size_t xssh_query_user_name(XSshServer* adapter, char* buffer, size_t capacity);
static XProtocolResult xssh_feed_bytes(XSshServer* adapter, const void* data, size_t size);
static void xssh_emit_prompt_after_line(XSshServer* adapter);

static bool xssh_check_kexinit(XSshServer* adapter)
{
    const uint8_t* p = XSSH_DATA(adapter)->clientKexInit;
    size_t len = XSSH_DATA(adapter)->clientKexInitLen;
    size_t off = 0;
    const uint8_t* list;
    size_t listLen;
    int selected;

    if (len < 1 + 16) { XSSH_DBG("kexinit too short len=%u\n", (unsigned)len); return false; }
    off = 1 + 16;
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 1 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_select_client_algorithm(list, listLen, xssh_server_kex_algorithms, &selected)) {
        XSSH_DBG("kexinit has no supported key exchange list='%.*s'\n", (int)listLen, list);
        return false;
    }
    XSSH_DATA(adapter)->kexAlgorithm = (XSshKexAlgorithm)selected;
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 2 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_select_client_algorithm(list, listLen, xssh_hostkey_algorithms, &selected)) {
        XSSH_DBG("kexinit has no supported host key list='%.*s'\n", (int)listLen, list);
        return false;
    }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 3 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_select_client_algorithm(list, listLen, xssh_cipher_algorithms, &selected)) {
        XSSH_DBG("kexinit has no supported cipher c2s list='%.*s'\n", (int)listLen, list);
        return false;
    }
    XSSH_DATA(adapter)->cipherC2S = (XSshCipherAlgorithm)selected;
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 4 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_select_client_algorithm(list, listLen, xssh_cipher_algorithms, &selected)) {
        XSSH_DBG("kexinit has no supported cipher s2c list='%.*s'\n", (int)listLen, list);
        return false;
    }
    XSSH_DATA(adapter)->cipherS2C = (XSshCipherAlgorithm)selected;
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 5 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_select_client_algorithm(list, listLen, xssh_mac_algorithms, &selected)) {
        XSSH_DBG("kexinit has no supported MAC c2s list='%.*s'\n", (int)listLen, list);
        return false;
    }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 6 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_select_client_algorithm(list, listLen, xssh_mac_algorithms, &selected)) {
        XSSH_DBG("kexinit has no supported MAC s2c list='%.*s'\n", (int)listLen, list);
        return false;
    }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 7 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "none")) { XSSH_DBG("kexinit no none c2s list='%.*s'\n", (int)listLen, list); return false; }
    if (!xssh_get_string(p, len, &off, &list, &listLen)) { XSSH_DBG("kexinit get string 8 fail off=%u\n", (unsigned)off); return false; }
    if (!xssh_name_list_contains(list, listLen, "none")) { XSSH_DBG("kexinit no none s2c list='%.*s'\n", (int)listLen, list); return false; }
    XSSH_DBG("kexinit ok\n");
    return true;
}

static bool xssh_send_kexinit(XSshServer* adapter)
{
    uint8_t msg[512];
    uint8_t kex[96], host[64], cipher[96], mac[64];
    size_t off = 0;
    size_t kexLen, hostLen, cipherLen, macLen;
    uint8_t cookie[16];
    const uint8_t* none = (const uint8_t*)"none";

    if (!adapter ||
        !xssh_build_algorithm_list(xssh_server_kex_algorithms, kex, sizeof(kex), &kexLen) ||
        !xssh_build_algorithm_list(xssh_hostkey_algorithms, host, sizeof(host), &hostLen) ||
        !xssh_build_algorithm_list(xssh_cipher_algorithms, cipher, sizeof(cipher), &cipherLen) ||
        !xssh_build_algorithm_list(xssh_mac_algorithms, mac, sizeof(mac), &macLen) ||
        !XRandomGenerator_fillSecure(cookie, sizeof(cookie)))
        return false;
    msg[off++] = SSH_MSG_KEXINIT;
    memcpy(msg + off, cookie, sizeof(cookie));
    off += sizeof(cookie);
    xssh_write_string(msg, &off, kex, kexLen);
    xssh_write_string(msg, &off, host, hostLen);
    xssh_write_string(msg, &off, cipher, cipherLen);
    xssh_write_string(msg, &off, cipher, cipherLen);
    xssh_write_string(msg, &off, mac, macLen);
    xssh_write_string(msg, &off, mac, macLen);
    xssh_write_string(msg, &off, none, 4);
    xssh_write_string(msg, &off, none, 4);
    xssh_write_string(msg, &off, (const uint8_t*)"", 0);
    xssh_write_string(msg, &off, (const uint8_t*)"", 0);
    msg[off++] = 0; /* first_kex_packet_follows */
    msg[off++] = 0; msg[off++] = 0; msg[off++] = 0; msg[off++] = 0; /* reserved */

    if (off > sizeof(XSSH_DATA(adapter)->serverKexInit)) return false;
    memcpy(XSSH_DATA(adapter)->serverKexInit, msg, off);
    XSSH_DATA(adapter)->serverKexInitLen = off;
    XSSH_DBG("server kexinit payload len=%u\n", (unsigned)off);
    return xssh_send_packet(adapter, msg, off);
}

static XProtocolResult xssh_send_userauth_failure(XSshServer* adapter)
{
    uint8_t payload[64];
    size_t off = 0;
    payload[off++] = SSH_MSG_USERAUTH_FAILURE;
    xssh_write_string(payload, &off, (const uint8_t*)"password", 8);
    payload[off++] = 0; /* partial success */
    return xssh_send_packet(adapter, payload, off) ? XProtocolResult_Ok : XProtocolResult_Failed;
}

static XProtocolResult xssh_fail_password_auth(XSshServer* adapter)
{
    XProtocolResult result;
    if (!adapter) return XProtocolResult_InvalidArgument;
    result = xssh_send_userauth_failure(adapter);
    if (result != XProtocolResult_Ok) return result;
    if (XSSH_DATA(adapter)->authAttempts < UINT8_MAX) ++XSSH_DATA(adapter)->authAttempts;
    if (XSSH_DATA(adapter)->authAttempts >= XSSH_MAX_AUTH_ATTEMPTS)
        XSSH_DATA(adapter)->closed = true;
    return XProtocolResult_Ok;
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

/* Normalize the raw agreement result before SSH mpint encoding.  The built-in
 * PSA X25519 path used here already returns the value in network byte order;
 * the helper still accepts little-endian providers for portability. */
static bool xssh_store_shared_secret_mpint(XSshServer* adapter,
                                           const uint8_t* shared, size_t sharedLen,
                                           bool sourceIsLittleEndian)
{
    uint8_t encoded[XSSH_MAX_DH_PUBLIC_LEN];
    size_t start = 0;
    size_t length;
    size_t i;
    if (!adapter || !shared || sharedLen == 0 || sharedLen > sizeof(encoded)) return false;
    for (i = 0; i < sharedLen; ++i)
        encoded[i] = sourceIsLittleEndian ? shared[sharedLen - 1u - i] : shared[i];
    while (start + 1u < sharedLen && encoded[start] == 0) ++start;
    length = sharedLen - start;
    if (encoded[start] & 0x80u) {
        if (length >= sizeof(XSSH_DATA(adapter)->sharedSecret)) return false;
        XSSH_DATA(adapter)->sharedSecret[0] = 0;
        memcpy(XSSH_DATA(adapter)->sharedSecret + 1u, encoded + start, length);
        ++length;
    } else {
        if (length > sizeof(XSSH_DATA(adapter)->sharedSecret)) return false;
        memcpy(XSSH_DATA(adapter)->sharedSecret, encoded + start, length);
    }
    XSSH_DATA(adapter)->sharedSecretLen = length;
    return true;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* 有限域 DH 帮助函数（RFC 3526 group14 = ffdhe2048，自研 2048 位大数） */
/* ------------------------------------------------------------------ */

#if XSSH_FFDH_GROUPS_ON

#define XSSH_BN_LIMBS 64u
#define XSSH_BN_BITS  2048u
#define XSSH_BN_BYTES 256u

typedef struct XSshBn {
    uint32_t d[XSSH_BN_LIMBS];
} XSshBn;

/* RFC 3526 group14 素数 p（2048 位 MODP 群），小端 limb 表示 */
static const uint32_t xssh_dh14_prime[XSSH_BN_LIMBS] = {
    0xffffffffu, 0xffffffffu, 0x8aacaa68u, 0x15728e5au,
    0x98fa0510u, 0x15d22618u, 0xea956ae5u, 0x3995497cu,
    0x95581718u, 0xde2bcbf6u, 0x6f4c52c9u, 0xb5c55df0u,
    0xec07a28fu, 0x9b2783a2u, 0x180e8603u, 0xe39e772cu,
    0x2e36ce3bu, 0x32905e46u, 0xca18217cu, 0xf1746c08u,
    0x4abc9804u, 0x670c354eu, 0x7096966du, 0x9ed52907u,
    0x208552bbu, 0x1c62f356u, 0xdca3ad96u, 0x83655d23u,
    0xfd24cf5fu, 0x69163fa8u, 0x1c55d39au, 0x98da4836u,
    0xa163bf05u, 0xc2007cb8u, 0xece45b3du, 0x49286651u,
    0x7c4b1fe6u, 0xae9f2411u, 0x5a899fa5u, 0xee386bfbu,
    0xf406b7edu, 0x0bff5cb6u, 0xa637ed6bu, 0xf44c42e9u,
    0x625e7ec6u, 0xe485b576u, 0x6d51c245u, 0x4fe1356du,
    0xf25f1437u, 0x302b0a6du, 0xcd3a431bu, 0xef9519b3u,
    0x8e3404ddu, 0x514a0879u, 0x3b139b22u, 0x020bbea6u,
    0x8a67cc74u, 0x29024e08u, 0x80dc1cd1u, 0xc4c6628bu,
    0x2168c234u, 0xc90fdaa2u, 0xffffffffu, 0xffffffffu,
};

/* R^2 mod p，R = 2^(32*64)，用于 Montgomery 形式转换 */
static const uint32_t xssh_dh14_r2[XSSH_BN_LIMBS] = {
    0x125fb664u, 0x477122ceu, 0x9b38d313u, 0xb03548fbu,
    0x6fd412c1u, 0x4c2153ffu, 0x873f9bc6u, 0x2a092b50u,
    0xfcb7f5f9u, 0xbbc71629u, 0x36bd84e7u, 0x4bec06e1u,
    0x6b020cb1u, 0x27ba725au, 0xed939eebu, 0xf8115426u,
    0x8a0e30d9u, 0x4bc1b187u, 0x258633ffu, 0x5620820eu,
    0x785a3071u, 0x074ed6abu, 0x81f1cb61u, 0xf228105fu,
    0x4e2e6f7fu, 0x570e436fu, 0xd7450bd9u, 0x5ca52ff7u,
    0x75f10a7eu, 0x552272d2u, 0x739c7978u, 0xac2b7925u,
    0x325b54d0u, 0xa2f88257u, 0xe8d72bd5u, 0xbc821c9du,
    0x866d2986u, 0xdbd442b3u, 0x70c4b2ceu, 0x9478951bu,
    0x94910c76u, 0x5d998fb3u, 0x7e300867u, 0xf273b293u,
    0x38569f92u, 0x8c106bbeu, 0x14e992c5u, 0xf83c92cbu,
    0xed6880ddu, 0xd85d6e7eu, 0xbe06a1dfu, 0xeb5b276fu,
    0xfa11e105u, 0x2a492090u, 0x19ea00beu, 0x63bdd96du,
    0x0a1698abu, 0x27238297u, 0x9240c974u, 0x8a3a686cu,
    0x66613000u, 0x3ed85703u, 0x628b3197u, 0x0cd37a33u,
};

/* (-p^{-1}) mod 2^32；group14 素数低 32 位为 0xffffffff，故等于 1 */
#define XSSH_DH14_NINV 1u

static void xssh_bn_zero(XSshBn* a)
{
    if (a) memset(a, 0, sizeof(*a));
}

static void xssh_bn_copy(XSshBn* dst, const XSshBn* src)
{
    if (dst && src) memcpy(dst, src, sizeof(*dst));
}

static void xssh_bn_set_u32(XSshBn* a, uint32_t v)
{
    if (!a) return;
    xssh_bn_zero(a);
    a->d[0] = v;
}

static int xssh_bn_cmp(const XSshBn* a, const XSshBn* b)
{
    int i;
    if (!a || !b) return 0;
    for (i = (int)XSSH_BN_LIMBS - 1; i >= 0; --i) {
        if (a->d[i] > b->d[i]) return 1;
        if (a->d[i] < b->d[i]) return -1;
    }
    return 0;
}

static bool xssh_bn_is_zero(const XSshBn* a)
{
    uint32_t v = 0;
    size_t i;
    if (!a) return true;
    for (i = 0; i < XSSH_BN_LIMBS; ++i) v |= a->d[i];
    return v == 0;
}

static bool xssh_bn_bit(const XSshBn* a, size_t bit)
{
    if (!a || bit >= XSSH_BN_BITS) return false;
    return ((a->d[bit / 32u] >> (bit % 32u)) & 1u) != 0;
}

/* 大端字节转 2048 位定点数（len <= 256） */
static void xssh_bn_from_be(const uint8_t* bytes, size_t len, XSshBn* out)
{
    size_t i;
    xssh_bn_zero(out);
    if (!bytes || !out || len > XSSH_BN_BYTES) return;
    for (i = 0; i < len; ++i) {
        size_t bufIndex = XSSH_BN_BYTES - len + i;
        size_t limb = (XSSH_BN_BYTES - 1u - bufIndex) / 4u;
        unsigned shift = (unsigned)((XSSH_BN_BYTES - 1u - bufIndex) % 4u) * 8u;
        out->d[limb] |= ((uint32_t)bytes[i]) << shift;
    }
}

/* 2048 位定点数转大端字节（固定 256 字节） */
static size_t xssh_bn_to_be(const XSshBn* in, uint8_t* out, size_t outCap)
{
    size_t i;
    if (!in || !out || outCap < XSSH_BN_BYTES) return 0;
    for (i = 0; i < XSSH_BN_BYTES; ++i) {
        size_t limb = (XSSH_BN_BYTES - 1u - i) / 4u;
        unsigned shift = (unsigned)((XSSH_BN_BYTES - 1u - i) % 4u) * 8u;
        out[i] = (uint8_t)(in->d[limb] >> shift);
    }
    return XSSH_BN_BYTES;
}

static uint32_t xssh_bn_add_raw(XSshBn* out, const XSshBn* a, const XSshBn* b)
{
    uint64_t carry = 0;
    size_t i;
    if (!out || !a || !b) return 0;
    for (i = 0; i < XSSH_BN_LIMBS; ++i) {
        uint64_t sum = (uint64_t)a->d[i] + b->d[i] + carry;
        out->d[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (uint32_t)carry;
}

static void xssh_bn_sub_raw(XSshBn* out, const XSshBn* a, const XSshBn* b)
{
    uint64_t borrow = 0;
    size_t i;
    if (!out || !a || !b) return;
    for (i = 0; i < XSSH_BN_LIMBS; ++i) {
        uint32_t ai = a->d[i];
        uint64_t bi = (uint64_t)b->d[i] + borrow;
        out->d[i] = (uint32_t)((uint64_t)ai - bi);
        borrow = ((uint64_t)ai < bi) ? 1u : 0u;
    }
}

static void xssh_bn_add_mod(XSshBn* out, const XSshBn* a, const XSshBn* b,
                            const XSshBn* mod)
{
    XSshBn temp;
    uint32_t carry;
    if (!out || !a || !b || !mod) return;
    carry = xssh_bn_add_raw(&temp, a, b);
    if (carry) {
        XSshBn correction, zero;
        xssh_bn_zero(&zero);
        xssh_bn_sub_raw(&correction, &zero, mod);
        (void)xssh_bn_add_raw(&temp, &temp, &correction);
    } else if (xssh_bn_cmp(&temp, mod) >= 0) {
        xssh_bn_sub_raw(&temp, &temp, mod);
    }
    *out = temp;
}

static void xssh_bn_sub_mod(XSshBn* out, const XSshBn* a, const XSshBn* b,
                            const XSshBn* mod)
{
    if (!out || !a || !b || !mod) return;
    if (xssh_bn_cmp(a, b) >= 0) {
        xssh_bn_sub_raw(out, a, b);
    } else {
        XSshBn temp;
        xssh_bn_sub_raw(&temp, mod, b);
        xssh_bn_add_raw(out, a, &temp);
    }
}

/* Montgomery 乘法：out = a*b*R^{-1} mod m */
static void xssh_bn_mont_mul(XSshBn* out, const XSshBn* a, const XSshBn* b,
                             const XSshBn* mod, uint32_t ninv)
{
    uint64_t T[2u * XSSH_BN_LIMBS + 4u];
    size_t i, j;
    if (!out || !a || !b || !mod) return;
    memset(T, 0, sizeof(T));
    /* T = a*b */
    for (i = 0; i < XSSH_BN_LIMBS; ++i) {
        uint64_t carry = 0;
        uint64_t ai = a->d[i];
        for (j = 0; j < XSSH_BN_LIMBS; ++j) {
            uint64_t cur = T[i + j] + ai * (uint64_t)b->d[j] + carry;
            T[i + j] = cur & 0xffffffffu;
            carry = cur >> 32;
        }
        {
            size_t k = i + XSSH_BN_LIMBS;
            while (carry) {
                uint64_t cur = T[k] + carry;
                T[k] = cur & 0xffffffffu;
                carry = cur >> 32;
                ++k;
            }
        }
    }
    /* Montgomery 归约 */
    for (i = 0; i < XSSH_BN_LIMBS; ++i) {
        uint32_t u = (uint32_t)((T[i] * ninv) & 0xffffffffu);
        uint64_t carry = 0;
        for (j = 0; j < XSSH_BN_LIMBS; ++j) {
            uint64_t cur = T[i + j] + u * (uint64_t)mod->d[j] + carry;
            T[i + j] = cur & 0xffffffffu;
            carry = cur >> 32;
        }
        {
            size_t k = i + XSSH_BN_LIMBS;
            while (carry) {
                uint64_t cur = T[k] + carry;
                T[k] = cur & 0xffffffffu;
                carry = cur >> 32;
                ++k;
            }
        }
    }
    /* 结果 = T >> (32*64)，第 65 个 limb（bit 2048）走补充归约 */
    for (i = 0; i < XSSH_BN_LIMBS; ++i)
        out->d[i] = (uint32_t)T[i + XSSH_BN_LIMBS];
    if (T[2u * XSSH_BN_LIMBS] != 0u) {
        XSshBn correction, zero, temp;
        xssh_bn_zero(&zero);
        xssh_bn_sub_raw(&correction, &zero, mod);
        (void)xssh_bn_add_raw(&temp, out, &correction);
        *out = temp;
    }
    if (xssh_bn_cmp(out, mod) >= 0)
        xssh_bn_sub_raw(out, out, mod);
}

/* Montgomery 模幂：out = base^exponent mod mod */
static void xssh_bn_pow_mod(XSshBn* out, const XSshBn* base,
                            const XSshBn* exponent, const XSshBn* mod,
                            const XSshBn* r2)
{
    XSshBn one, baseMont, result, tmp;
    size_t i;
    if (!out || !base || !exponent || !mod || !r2) return;
    xssh_bn_set_u32(&one, 1u);
    xssh_bn_mont_mul(&baseMont, base, r2, mod, XSSH_DH14_NINV);
    xssh_bn_mont_mul(&result, &one, r2, mod, XSSH_DH14_NINV);
    for (i = XSSH_BN_BITS; i-- > 0;) {
        xssh_bn_mont_mul(&tmp, &result, &result, mod, XSSH_DH14_NINV);
        result = tmp;
        if (xssh_bn_bit(exponent, i)) {
            xssh_bn_mont_mul(&tmp, &result, &baseMont, mod, XSSH_DH14_NINV);
            result = tmp;
        }
    }
    xssh_bn_mont_mul(out, &result, &one, mod, XSSH_DH14_NINV);
}

/* 2048 位定点数转 SSH mpint 字节（若最高位为 1 则前置 0x00） */
static size_t xssh_bn_to_mpint(const XSshBn* in, uint8_t* out, size_t outCap)
{
    uint8_t be[XSSH_BN_BYTES];
    size_t start = 0;
    size_t len;
    if (!in || !out || outCap < 1u) return 0;
    if (xssh_bn_to_be(in, be, sizeof(be)) != sizeof(be)) return 0;
    while (start + 1u < sizeof(be) && be[start] == 0) ++start;
    len = sizeof(be) - start;
    if ((be[start] & 0x80u) != 0) {
        if (outCap < len + 1u) return 0;
        out[0] = 0;
        memcpy(out + 1, be + start, len);
        return len + 1u;
    }
    if (outCap < len) return 0;
    memcpy(out, be + start, len);
    return len;
}

/* 生成服务端 DH 私钥并计算公钥 f = g^x mod p（g=2），以 mpint 形式返回 */
static bool xssh_ffdh_generate_key(XSshServer* adapter,
                                   uint8_t* serverPublicMpint,
                                   size_t serverPublicCap,
                                   size_t* serverPublicLen)
{
    uint8_t priv[32];
    XSshBn privBn, base, f;
    size_t fMpintLen;
    if (!adapter || !serverPublicMpint || !serverPublicLen) return false;
    if (!XRandomGenerator_fillSecure(priv, sizeof(priv))) return false;
    priv[0] |= 0x80u;               /* 保证 256 位私钥与 2048 位群安全强度匹配 */
    xssh_bn_from_be(priv, sizeof(priv), &privBn);
    memcpy(XSSH_DATA(adapter)->ffdhPriv, priv, sizeof(priv));
    XSSH_DATA(adapter)->ffdhPrivLen = sizeof(priv);
    xssh_bn_set_u32(&base, 2u);     /* g = 2 */
    xssh_bn_pow_mod(&f, &base, &privBn,
                    (const XSshBn*)xssh_dh14_prime,
                    (const XSshBn*)xssh_dh14_r2);
    fMpintLen = xssh_bn_to_mpint(&f, serverPublicMpint, serverPublicCap);
    if (fMpintLen == 0) return false;
    *serverPublicLen = fMpintLen;
    return true;
}

/* 计算共享秘密 K = e^x mod p，以 256 字节大端输出 */
static bool xssh_ffdh_agree(XSshServer* adapter,
                            const uint8_t* clientEMpint,
                            size_t clientEMpintLen,
                            uint8_t* shared, size_t sharedCapacity,
                            size_t* sharedLen)
{
    XSshBn e, priv, K;
    const XSshBn* mod = (const XSshBn*)xssh_dh14_prime;
    size_t eLen = clientEMpintLen;
    if (!adapter || !clientEMpint || !shared || !sharedLen ||
        clientEMpintLen == 0 || clientEMpintLen > XSSH_MAX_DH_PUBLIC_LEN ||
        XSSH_DATA(adapter)->ffdhPrivLen == 0)
        return false;
    /* mpint 可能带前导 0x00（RFC 4251 §5 符号位约定），先剥离再转定点数。 */
    while (eLen > 0 && clientEMpint[0] == 0u) { ++clientEMpint; --eLen; }
    if (eLen == 0 || eLen > XSSH_BN_BYTES)
        return false;
    xssh_bn_from_be(clientEMpint, eLen, &e);
    if (xssh_bn_is_zero(&e) || xssh_bn_cmp(&e, mod) >= 0) return false;
    xssh_bn_from_be(XSSH_DATA(adapter)->ffdhPriv,
                    XSSH_DATA(adapter)->ffdhPrivLen, &priv);
    xssh_bn_pow_mod(&K, &e, &priv, mod, (const XSshBn*)xssh_dh14_r2);
    if (sharedCapacity < XSSH_BN_BYTES) return false;
    if (xssh_bn_to_be(&K, shared, sharedCapacity) != XSSH_BN_BYTES) return false;
    *sharedLen = XSSH_BN_BYTES;
    return true;
}

/* 清理临时私钥缓存 */
static void xssh_ffdh_destroy_key(XSshServer* adapter)
{
    if (!adapter) return;
    memset(XSSH_DATA(adapter)->ffdhPriv, 0, sizeof(XSSH_DATA(adapter)->ffdhPriv));
    XSSH_DATA(adapter)->ffdhPrivLen = 0;
}

#endif /* XSSH_FFDH_GROUPS_ON */


static XProtocolResult xssh_handle_kex_ecdh_init(XSshServer* adapter,
                                                const uint8_t* payload, size_t len)
{
    XSSH_DBG("handle kex_ecdh_init len=%u\n", (unsigned)len);
    size_t off = 0;
    const uint8_t* qcString;
    size_t qcStringLen;
    uint8_t serverPub[XSSH_MAX_DH_PUBLIC_LEN];
    size_t serverPubLen;
    uint8_t shared[XSSH_MAX_DH_PUBLIC_LEN];
    size_t sharedLen;
    bool isDhGroup14 = false;
    uint8_t hashInput[4u * XSSH_MAX_PACKET + 4u * XSSH_MAX_DH_PUBLIC_LEN + 128];
    size_t hashOff = 0;
    uint8_t sig[256];
    size_t sigLen;
    uint8_t signatureHash[XSSH_HASH_LEN];
    uint8_t r[32], sx[32];
    uint8_t sigBlob[300];
    size_t sigBlobLen;
    uint8_t reply[XSSH_MAX_PACKET];
    size_t replyOff = 0;
    size_t expectedPublicLen;
    XCryptographic_EcdhAlgorithm ecdhAlgorithm;
    bool sharedIsLittleEndian;

    if (len < 1 + 4) { XSSH_DBG("bad len\n"); return XProtocolResult_Failed; }
    off = 1;
    switch (XSSH_DATA(adapter)->kexAlgorithm) {
    case XSshKexAlgorithm_Curve25519Sha256:
#if XCRYPTOGRAPHIC_X25519_ON && XCRYPTOGRAPHIC_SHA256_ON
        expectedPublicLen = XSSH_CURVE25519_PUBLIC_LEN;
        ecdhAlgorithm = XCryptographic_EcdhAlgorithm_X25519;
        sharedIsLittleEndian = false;
        break;
#else
        return XProtocolResult_Failed;
#endif
    case XSshKexAlgorithm_EcdhSha2Nistp256:
#if XCRYPTOGRAPHIC_ECDH_NISTP256_ON && XCRYPTOGRAPHIC_SHA256_ON
        expectedPublicLen = XSSH_POINT_LEN;
        ecdhAlgorithm = XCryptographic_EcdhAlgorithm_NistP256;
        sharedIsLittleEndian = false;
        break;
#else
        return XProtocolResult_Failed;
#endif
    case XSshKexAlgorithm_DhGroup14Sha256:
#if XSSH_FFDH_GROUPS_ON && XCRYPTOGRAPHIC_SHA256_ON
        expectedPublicLen = 0;                 /* 长度由包内 mpint 决定 */
        ecdhAlgorithm = XCryptographic_EcdhAlgorithm_None;
        sharedIsLittleEndian = false;
        isDhGroup14 = true;
        break;
#else
        return XProtocolResult_Failed;
#endif
    default:
        return XProtocolResult_Failed;
    }

    /* Q_C 是所选密钥交换算法定义的原始公钥字符串，不是 SSH key blob。
     * DH group14 的 Q_C 为 mpint e（含可能的前导 0），长度不固定。 */
    if (!xssh_get_string(payload, len, &off, &qcString, &qcStringLen) ||
        off != len ||
        (!isDhGroup14 && qcStringLen != expectedPublicLen) ||
        (isDhGroup14 && (qcStringLen == 0 || qcStringLen > XSSH_MAX_DH_PUBLIC_LEN)) ||
        (XSSH_DATA(adapter)->kexAlgorithm == XSshKexAlgorithm_EcdhSha2Nistp256 &&
         qcString[0] != 0x04)) {
        XSSH_DBG("get client public key failed len=%u\n", (unsigned)qcStringLen);
        return XProtocolResult_Failed;
    }

    memcpy(XSSH_DATA(adapter)->clientPublicBlob, qcString, qcStringLen);
    XSSH_DATA(adapter)->clientPublicBlobLen = qcStringLen;

    /* 生成服务端临时密钥并执行 ECDH/X25519 或有限域 DH 原始密钥协商。 */
    if (isDhGroup14) {
#if XSSH_FFDH_GROUPS_ON
        if (!xssh_ffdh_generate_key(adapter, serverPub, sizeof(serverPub),
                                    &serverPubLen)) {
            XSSH_DBG("ffdh generate key failed\n");
            xssh_ffdh_destroy_key(adapter);
            return XProtocolResult_Failed;
        }
        sharedLen = 0;
        if (!xssh_ffdh_agree(adapter, qcString, qcStringLen,
                             shared, sizeof(shared), &sharedLen)) {
            XSSH_DBG("ffdh raw agreement failed\n");
            xssh_ffdh_destroy_key(adapter);
            return XProtocolResult_Failed;
        }
        xssh_ffdh_destroy_key(adapter);
#else
        return XProtocolResult_Failed;
#endif
    } else {
        if (!XCryptographic_ecdhGenerateKey(ecdhAlgorithm, &XSSH_DATA(adapter)->ecdhKey)) {
            XSSH_DBG("generate key exchange key failed\n");
            return XProtocolResult_Failed;
        }
        serverPubLen = (size_t)XCryptographic_exportPublicKeyInto(
            (char*)serverPub, sizeof(serverPub), XSSH_DATA(adapter)->ecdhKey).m_size;
        if (serverPubLen != expectedPublicLen) {
            XSSH_DBG("export public key failed len=%u\n", (unsigned)serverPubLen);
            return XProtocolResult_Failed;
        }

        sharedLen = (size_t)XCryptographic_ecdhAgreeInto(
            (char*)shared, sizeof(shared), XSSH_DATA(adapter)->ecdhKey,
            XByteArrayView_create_data( qcString, (int64_t)qcStringLen )).m_size;
        if (sharedLen == 0) {
            XSSH_DBG("raw key agreement failed len=%u\n", (unsigned)sharedLen);
            return XProtocolResult_Failed;
        }
        if (XSSH_DATA(adapter)->kexAlgorithm == XSshKexAlgorithm_Curve25519Sha256) {
            uint8_t any = 0;
            size_t i;
            for (i = 0; i < sharedLen; ++i) any |= shared[i];
            if (any == 0) return XProtocolResult_Failed;
        }
    }
    if (!xssh_store_shared_secret_mpint(adapter, shared, sharedLen, sharedIsLittleEndian))
        return XProtocolResult_Failed;

    /* 交换哈希 H（RFC 4253 §8）：
     *   string V_C || string V_S || string I_C || string I_S
     *   || string K_S || string Q_C || string Q_S || mpint K
     * V_C/V_S 为版本行（不含行尾 CR/LF）；I_C/I_S 内容含消息类型字节；
     * K_S 为 host key blob；Q_C/Q_S 为原始公钥字符串；K 为 shared secret mpint。
     * 注意 V_C/V_S/I_C/I_S 都必须带 SSH string 的 4 字节长度前缀。 */
    {
        const uint8_t* vc = XSSH_DATA(adapter)->clientVersion;
        size_t vcLen = XSSH_DATA(adapter)->clientVersionLen;
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
    xssh_write_u32(hashInput + hashOff, (uint32_t)XSSH_DATA(adapter)->clientKexInitLen);
    hashOff += 4;
    memcpy(hashInput + hashOff, XSSH_DATA(adapter)->clientKexInit, XSSH_DATA(adapter)->clientKexInitLen);
    hashOff += XSSH_DATA(adapter)->clientKexInitLen;
    xssh_write_u32(hashInput + hashOff, (uint32_t)XSSH_DATA(adapter)->serverKexInitLen);
    hashOff += 4;
    memcpy(hashInput + hashOff, XSSH_DATA(adapter)->serverKexInit, XSSH_DATA(adapter)->serverKexInitLen);
    hashOff += XSSH_DATA(adapter)->serverKexInitLen;
    xssh_write_string(hashInput, &hashOff, XSSH_DATA(adapter)->hostKeyBlob, XSSH_DATA(adapter)->hostKeyBlobLen);
    xssh_write_string(hashInput, &hashOff, XSSH_DATA(adapter)->clientPublicBlob, XSSH_DATA(adapter)->clientPublicBlobLen);
    xssh_write_string(hashInput, &hashOff, serverPub, serverPubLen);
    /* K 为 SSH mpint，需带 4 字节长度前缀 */
    xssh_write_u32(hashInput + hashOff, (uint32_t)XSSH_DATA(adapter)->sharedSecretLen);
    hashOff += 4;
    memcpy(hashInput + hashOff, XSSH_DATA(adapter)->sharedSecret, XSSH_DATA(adapter)->sharedSecretLen);
    hashOff += XSSH_DATA(adapter)->sharedSecretLen;

    {
        unsigned i;
        XSSH_DBG("hashInputLen=%u\n", (unsigned)hashOff);
        XSSH_DBG("hashInput:");
        for (i = 0; i < hashOff; ++i) XSSH_DBG("%02x", hashInput[i]);
        XSSH_DBG("\n");
        XSSH_DBG("hostKeyBlob:");
        for (i = 0; i < XSSH_DATA(adapter)->hostKeyBlobLen; ++i) XSSH_DBG("%02x", XSSH_DATA(adapter)->hostKeyBlob[i]);
        XSSH_DBG("\n");
    }
    if (!xssh_hash_sha256(hashInput, hashOff, XSSH_DATA(adapter)->exchangeHash))
    { XSSH_DBG("hash failed\n"); return XProtocolResult_Failed; }
    memcpy(XSSH_DATA(adapter)->sessionId, XSSH_DATA(adapter)->exchangeHash, XSSH_HASH_LEN);

    if (!xssh_setup_keys(adapter)) { XSSH_DBG("setup keys failed\n"); return XProtocolResult_Failed; }

    /* OpenSSH 的 ECDSA 验证接口按消息模式再计算一次 SHA-256；交换哈希 H
     * 仍作为会话 ID 和密钥派生输入保留原值。 */
    if (!xssh_hash_sha256(XSSH_DATA(adapter)->exchangeHash,
                          XSSH_HASH_LEN, signatureHash))
        return XProtocolResult_Failed;

    /* ecdsa-sha2-nistp256 签名格式（RFC 5656 §3.1.2）：
     *   string "ecdsa-sha2-nistp256"
     *   string ecdsa_signature_blob
     * 其中 ecdsa_signature_blob = mpint r || mpint s */
    sigLen = (size_t)XCryptographic_ecdsaSignHashInto(
        (char*)sig, sizeof(sig), XSSH_DATA(adapter)->hostKey,
        XByteArrayView_create_data( signatureHash, XSSH_HASH_LEN ), false).m_size;
    if (sigLen == 0) {
        XSSH_DBG("sign hash failed\n");
        return XProtocolResult_Failed;
    }
    XSSH_DBG("sign sigLen=%u first=%02x %02x %02x %02x\n",
             (unsigned)sigLen, sig[0], sig[1], sig[2], sig[3]);
    if (sigLen == 64) {
        /* mbedTLS PSA 的 ECDSA 签名直接返回 r||s 各 32 字节 */
        memcpy(r, sig, 32);
        memcpy(sx, sig + 32, 32);
    } else if (!xssh_der_ecdsa_to_rs(sig, sigLen, r, sx)) {
        XSSH_DBG("ECDSA signature parse failed sigLen=%u first=%02x\n",
                 (unsigned)sigLen, sig[0]);
        return XProtocolResult_Failed;
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
            return XProtocolResult_Failed;
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
    xssh_write_string(reply, &replyOff, XSSH_DATA(adapter)->hostKeyBlob, XSSH_DATA(adapter)->hostKeyBlobLen);
    xssh_write_string(reply, &replyOff, serverPub, serverPubLen);
    xssh_write_string(reply, &replyOff, sigBlob, sigBlobLen);
    {
        unsigned i;
        XSSH_DBG("H:");
        for (i = 0; i < XSSH_HASH_LEN; ++i) XSSH_DBG("%02x", XSSH_DATA(adapter)->exchangeHash[i]);
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
    if (!xssh_send_packet(adapter, reply, replyOff)) { XSSH_DBG("send reply failed\n"); return XProtocolResult_Failed; }

    {
        uint8_t nk[1] = { SSH_MSG_NEWKEYS };
        if (!xssh_send_packet(adapter, nk, 1)) return XProtocolResult_Failed;
    }
    XSSH_DATA(adapter)->sendEncrypted = true;
    XSSH_DATA(adapter)->state = XSshState_NewKeys;
    return XProtocolResult_Ok;
}

static XProtocolResult xssh_handle_service_request(XSshServer* adapter,
                                                  const uint8_t* payload, size_t len)
{
    size_t off = 1;
    const uint8_t* name;
    size_t nameLen;
    uint8_t reply[32];
    size_t o = 0;
    if (!xssh_get_string(payload, len, &off, &name, &nameLen)) return XProtocolResult_Failed;
    if (nameLen != 12 || memcmp(name, "ssh-userauth", 12) != 0) return XProtocolResult_Failed;
    reply[o++] = SSH_MSG_SERVICE_ACCEPT;
    xssh_write_string(reply, &o, (const uint8_t*)"ssh-userauth", 12);
    if (!xssh_send_packet(adapter, reply, o)) return XProtocolResult_Failed;
    XSSH_DATA(adapter)->state = XSshState_Userauth;
    return XProtocolResult_Ok;
}

static XProtocolResult xssh_handle_userauth_request(XSshServer* adapter,
                                                   const uint8_t* payload, size_t len)
{
    size_t off = 1;
    const uint8_t* user;
    const uint8_t* service;
    const uint8_t* method;
    size_t userLen, serviceLen, methodLen;
    if (!xssh_get_string(payload, len, &off, &user, &userLen)) return XProtocolResult_Failed;
    if (!xssh_get_string(payload, len, &off, &service, &serviceLen)) return XProtocolResult_Failed;
    if (!xssh_get_string(payload, len, &off, &method, &methodLen)) return XProtocolResult_Failed;
    if (serviceLen != 14 || memcmp(service, "ssh-connection", 14) != 0)
        return xssh_fail_password_auth(adapter);

    if (methodLen == 4 && memcmp(method, "none", 4) == 0)
        return xssh_send_userauth_failure(adapter);

    if (methodLen == 8 && memcmp(method, "password", 8) == 0) {
        bool change;
        const uint8_t* passwd;
        size_t passwdLen;
        char userBuf[XSSH_LOGIN_NAME_SIZE];
        char passBuf[XSSH_LOGIN_PASSWORD_SIZE + 1];
        if (off >= len || payload[off] > 1) return XProtocolResult_Failed;
        change = payload[off++] != 0;
        if (change) return xssh_fail_password_auth(adapter);
        if (!xssh_get_string(payload, len, &off, &passwd, &passwdLen) || off != len)
            return XProtocolResult_Failed;
        if (userLen == 0 || userLen >= sizeof(userBuf) ||
            passwdLen > XSSH_LOGIN_PASSWORD_SIZE)
            return xssh_fail_password_auth(adapter);
        memcpy(userBuf, user, userLen);
        userBuf[userLen] = 0;
        memcpy(passBuf, passwd, passwdLen);
        passBuf[passwdLen] = 0;
        if (!adapter->m_data) {
            memset(passBuf, 0, sizeof(passBuf));
            return XProtocolResult_Failed;
        }
        if (!xssh_query_authenticate(adapter, userBuf, passBuf)) {
            memset(passBuf, 0, sizeof(passBuf));
            return xssh_fail_password_auth(adapter);
        }
        memset(passBuf, 0, sizeof(passBuf));
        XSSH_DATA(adapter)->authDone = true;
        XSSH_DATA(adapter)->state = XSshState_Channel;
        {
            uint8_t okMsg[1] = { SSH_MSG_USERAUTH_SUCCESS };
            if (!xssh_send_packet(adapter, okMsg, 1)) return XProtocolResult_Failed;
        }
        return XProtocolResult_Ok;
    }
    return xssh_send_userauth_failure(adapter);
}

static XProtocolResult xssh_send_channel_success(XSshServer* adapter,
                                                uint8_t msgType)
{
    uint8_t payload[16];
    size_t off = 0;
    payload[off++] = msgType;
    xssh_write_u32(payload + off, XSSH_DATA(adapter)->localChannel);
    off += 4;
    return xssh_send_packet(adapter, payload, off) ? XProtocolResult_Ok : XProtocolResult_Failed;
}

static XProtocolResult xssh_channel_protocol_error(XSshServer* adapter)
{
    if (adapter) XSSH_DATA(adapter)->closed = true;
    return XProtocolResult_Failed;
}

static bool xssh_send_channel_open_failure(XSshServer* adapter,
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

static bool xssh_send_channel_window_adjust(XSshServer* adapter,
                                            uint32_t amount)
{
    uint8_t payload[9];
    if (!adapter || !XSSH_DATA(adapter)->channelOpen || amount == 0) return false;
    payload[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
    xssh_write_u32(payload + 1, XSSH_DATA(adapter)->remoteChannel);
    xssh_write_u32(payload + 5, amount);
    return xssh_send_packet(adapter, payload, sizeof(payload));
}

static bool xssh_send_channel_eof(XSshServer* adapter)
{
    uint8_t payload[5];
    if (!adapter || !XSSH_DATA(adapter)->channelOpen || XSSH_DATA(adapter)->localEof) return true;
    payload[0] = SSH_MSG_CHANNEL_EOF;
    xssh_write_u32(payload + 1, XSSH_DATA(adapter)->remoteChannel);
    if (!xssh_send_packet(adapter, payload, sizeof(payload))) return false;
    XSSH_DATA(adapter)->localEof = true;
    return true;
}

static bool xssh_send_channel_close(XSshServer* adapter)
{
    uint8_t payload[5];
    if (!adapter || !XSSH_DATA(adapter)->channelOpen || XSSH_DATA(adapter)->localClose) return true;
    payload[0] = SSH_MSG_CHANNEL_CLOSE;
    xssh_write_u32(payload + 1, XSSH_DATA(adapter)->remoteChannel);
    if (!xssh_send_packet(adapter, payload, sizeof(payload))) return false;
    XSSH_DATA(adapter)->localClose = true;
    return true;
}

static bool xssh_replenish_channel_window(XSshServer* adapter)
{
    uint32_t amount;
    if (!adapter || !XSSH_DATA(adapter)->channelOpen) return false;
    if (XSSH_DATA(adapter)->serverWindow >= XSSH_CHANNEL_WINDOW_LOW_WATER) return true;
    amount = XSSH_CHANNEL_INITIAL_WINDOW - XSSH_DATA(adapter)->serverWindow;
    if (!xssh_send_channel_window_adjust(adapter, amount)) return false;
    XSSH_DATA(adapter)->serverWindow += amount;
    return true;
}

static XProtocolResult xssh_handle_channel_open(XSshServer* adapter,
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

    if (!adapter || !payload || len < 1 + 4) return XProtocolResult_Failed;
    if (!xssh_get_string(payload, len, &off, &type, &typeLen))
        return xssh_channel_protocol_error(adapter);
    if (off + 12 != len)
        return xssh_channel_protocol_error(adapter);
    xssh_get_u32(payload + off, &sender); off += 4;
    xssh_get_u32(payload + off, &win); off += 4;
    xssh_get_u32(payload + off, &maxPacket); off += 4;
    if (XSSH_DATA(adapter)->channelOpen) {
        if (!xssh_send_channel_open_failure(adapter, sender, 1,
                                            "only one session channel is supported"))
            return XProtocolResult_Failed;
        return XProtocolResult_Ok;
    }
    if (typeLen != 7 || memcmp(type, "session", 7) != 0) {
        if (!xssh_send_channel_open_failure(adapter, sender, 3,
                                            "unsupported channel type"))
            return XProtocolResult_Failed;
        return XProtocolResult_Ok;
    }
    if (maxPacket < 16u) {
        if (!xssh_send_channel_open_failure(adapter, sender, 2,
                                            "invalid maximum packet size"))
            return XProtocolResult_Failed;
        return XProtocolResult_Ok;
    }

    XSSH_DATA(adapter)->remoteChannel = sender;
    XSSH_DATA(adapter)->clientWindow = win;
    XSSH_DATA(adapter)->clientMaxPacket = maxPacket;
    XSSH_DATA(adapter)->serverWindow = XSSH_CHANNEL_INITIAL_WINDOW;
    XSSH_DATA(adapter)->serverMaxPacket = XSSH_MAX_PACKET;
    XSSH_DATA(adapter)->channelOpen = true;

    reply[o++] = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
    xssh_write_u32(reply + o, XSSH_DATA(adapter)->remoteChannel); o += 4;
    xssh_write_u32(reply + o, XSSH_DATA(adapter)->localChannel); o += 4;
    xssh_write_u32(reply + o, XSSH_DATA(adapter)->serverWindow); o += 4;
    xssh_write_u32(reply + o, XSSH_DATA(adapter)->serverMaxPacket); o += 4;
    return xssh_send_packet(adapter, reply, o) ? XProtocolResult_Ok : XProtocolResult_Failed;
}

static XProtocolResult xssh_handle_channel_request(XSshServer* adapter,
                                                  const uint8_t* payload, size_t len)
{
    size_t off = 1;
    uint32_t recipient;
    const uint8_t* type;
    size_t typeLen;
    bool wantReply;
    uint8_t replyMsg;

    if (!adapter || !payload || len < 1 + 4) return XProtocolResult_Failed;
    if (off + 4 > len) return xssh_channel_protocol_error(adapter);
    xssh_get_u32(payload + off, &recipient); off += 4;
    if (!xssh_get_string(payload, len, &off, &type, &typeLen))
        return xssh_channel_protocol_error(adapter);
    if (off >= len) return xssh_channel_protocol_error(adapter);
    if (payload[off] > 1) return xssh_channel_protocol_error(adapter);
    wantReply = payload[off++] != 0;
    if (!XSSH_DATA(adapter)->channelOpen || recipient != XSSH_DATA(adapter)->localChannel)
        return xssh_channel_protocol_error(adapter);

    if (typeLen == 5 && memcmp(type, "shell", 5) == 0) {
        if (off != len || XSSH_DATA(adapter)->shellStarted) {
            replyMsg = SSH_MSG_CHANNEL_FAILURE;
            if (wantReply && xssh_send_channel_success(adapter, replyMsg) != XProtocolResult_Ok)
                return XProtocolResult_Failed;
            return XProtocolResult_Ok;
        }
        XSSH_DATA(adapter)->shellStarted = true;
        if (wantReply && xssh_send_channel_success(adapter, SSH_MSG_CHANNEL_SUCCESS) != XProtocolResult_Ok)
            return XProtocolResult_Failed;
        /* Shell 的同步 feed 路径不会触发 prompt 回调，因此在接受
         * shell 请求时补发一次初始提示符；后续由 xssh_emit_prompt_after_line 负责。 */
        (void)XSshServer_emitPrompt(adapter);
        return XProtocolResult_Ok;
    }

    if (typeLen == 7 && memcmp(type, "pty-req", 7) == 0) {
        const uint8_t* term;
        const uint8_t* modes;
        size_t termLen;
        size_t modesLen;
        if (!xssh_get_string(payload, len, &off, &term, &termLen) ||
            off + 16 > len || termLen == 0 || termLen >= sizeof(XSSH_DATA(adapter)->ptyTerm)) {
            return xssh_channel_protocol_error(adapter);
        }
        memcpy(XSSH_DATA(adapter)->ptyTerm, term, termLen);
        XSSH_DATA(adapter)->ptyTerm[termLen] = '\0';
        xssh_get_u32(payload + off, &XSSH_DATA(adapter)->ptyColumns); off += 4;
        xssh_get_u32(payload + off, &XSSH_DATA(adapter)->ptyRows); off += 4;
        xssh_get_u32(payload + off, &XSSH_DATA(adapter)->ptyPixelWidth); off += 4;
        xssh_get_u32(payload + off, &XSSH_DATA(adapter)->ptyPixelHeight); off += 4;
        if (!xssh_get_string(payload, len, &off, &modes, &modesLen) || off != len)
        {
            return xssh_channel_protocol_error(adapter);
        }
        XSSH_DATA(adapter)->inputEcho = true;
        {
            size_t modeOff = 0;
            while (modeOff < modesLen) {
                uint8_t opcode = modes[modeOff++];
                uint32_t value;
                if (opcode == 0) break;
                if (modeOff + 4u > modesLen) return xssh_channel_protocol_error(adapter);
                xssh_get_u32(modes + modeOff, &value);
                modeOff += 4u;
                if (opcode == XSSH_TTY_OP_ECHO) XSSH_DATA(adapter)->inputEcho = value != 0;
            }
        }
        XSSH_DATA(adapter)->ptyRequested = true;
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
    } else if (typeLen == 12 && memcmp(type, "window-change", 12) == 0) {
        if (off + 16 != len) return xssh_channel_protocol_error(adapter);
        xssh_get_u32(payload + off, &XSSH_DATA(adapter)->ptyColumns); off += 4;
        xssh_get_u32(payload + off, &XSSH_DATA(adapter)->ptyRows); off += 4;
        xssh_get_u32(payload + off, &XSSH_DATA(adapter)->ptyPixelWidth); off += 4;
        xssh_get_u32(payload + off, &XSSH_DATA(adapter)->ptyPixelHeight); off += 4;
        replyMsg = SSH_MSG_CHANNEL_SUCCESS;
    } else {
        /* Requests not implemented by this adapter (notably exec/subsystem)
         * are rejected without consuming any untrusted request-specific
         * fields.  wantReply still gets the required channel failure. */
        replyMsg = SSH_MSG_CHANNEL_FAILURE;
    }
    if (wantReply && xssh_send_channel_success(adapter, replyMsg) != XProtocolResult_Ok)
        return XProtocolResult_Failed;
    return XProtocolResult_Ok;
}

/* SSH clients put their local terminal in raw/no-echo mode after a PTY is
 * accepted.  The adapter therefore supplies the small amount of terminal
 * echo normally provided by a server-side PTY. */
static bool xssh_echo_channel_input(XSshServer* adapter,
                                    const uint8_t* data, size_t size)
{
    uint8_t echo[256];
    size_t echoLen = 0;
    size_t i;
    if (!adapter || (!data && size)) return false;
    if (!XSSH_DATA(adapter)->ptyRequested || !XSSH_DATA(adapter)->inputEcho) return true;
    for (i = 0; i < size; ++i) {
        uint8_t byte = data[i];
        if (XSSH_DATA(adapter)->echoEscape) {
            if (byte >= '@' && byte <= '~') XSSH_DATA(adapter)->echoEscape = false;
            continue;
        }
        if (byte == 0x1b) {
            XSSH_DATA(adapter)->echoEscape = true;
            continue;
        }
        if (byte == '\r') {
            static const char crlf[] = "\r\n";
            if (echoLen && !XSshServer_write(adapter, echo, echoLen)) return false;
            echoLen = 0;
            if (!XSshServer_write(adapter, crlf, sizeof(crlf) - 1u)) return false;
            XSSH_DATA(adapter)->echoPendingCr = true;
            continue;
        }
        if (byte == '\n') {
            if (echoLen && !XSshServer_write(adapter, echo, echoLen)) return false;
            echoLen = 0;
            if (!XSSH_DATA(adapter)->echoPendingCr &&
                !XSshServer_write(adapter, (const char*)"\n", 1u)) return false;
            XSSH_DATA(adapter)->echoPendingCr = false;
            continue;
        }
        XSSH_DATA(adapter)->echoPendingCr = false;
        if (byte == '\b' || byte == 0x7f) {
            static const char erase[] = "\b \b";
            if (echoLen && !XSshServer_write(adapter, echo, echoLen)) return false;
            echoLen = 0;
            if (!XSshServer_write(adapter, erase, sizeof(erase) - 1u)) return false;
            continue;
        }
        if (byte == 0x03) {
            static const char interrupt[] = "^C\r\n";
            if (echoLen && !XSshServer_write(adapter, echo, echoLen)) return false;
            echoLen = 0;
            if (!XSshServer_write(adapter, interrupt, sizeof(interrupt) - 1u)) return false;
            continue;
        }
        if (byte >= 0x20 || byte == '\t') {
            if (echoLen == sizeof(echo) && !XSshServer_write(adapter, echo, echoLen))
                return false;
            if (echoLen == sizeof(echo)) echoLen = 0;
            echo[echoLen++] = byte;
        }
    }
    if (echoLen && !XSshServer_write(adapter, echo, echoLen)) return false;
    return true;
}

static XProtocolResult xssh_handle_channel_data(XSshServer* adapter,
                                               const uint8_t* payload, size_t len)
{
    size_t off = 1;
    uint32_t recipient;
    const uint8_t* data;
    size_t dataLen;
    if (!adapter || !payload || len < 1 + 4) return XProtocolResult_Failed;
    if (off + 4 > len) return xssh_channel_protocol_error(adapter);
    xssh_get_u32(payload + off, &recipient); off += 4;
    if (!xssh_get_string(payload, len, &off, &data, &dataLen) || off != len)
        return xssh_channel_protocol_error(adapter);
    if (!XSSH_DATA(adapter)->channelOpen || recipient != XSSH_DATA(adapter)->localChannel ||
        XSSH_DATA(adapter)->remoteEof || dataLen > XSSH_DATA(adapter)->serverMaxPacket ||
        dataLen > XSSH_DATA(adapter)->serverWindow)
        return xssh_channel_protocol_error(adapter);
    XSSH_DATA(adapter)->serverWindow -= (uint32_t)dataLen;
    if (!XSSH_DATA(adapter)->shellStarted)
        return xssh_channel_protocol_error(adapter);
    {
        size_t pos = 0;
        XProtocolResult result = XProtocolResult_Ok;
        bool previousCr = XSSH_DATA(adapter)->promptPendingCr;
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
            if (!xssh_echo_channel_input(adapter, data + pos, segmentLen))
                return XProtocolResult_IoError;
            result = xssh_feed_bytes(adapter, data + pos, segmentLen);
            if (result == XProtocolResult_IoError) break;
            if (segmentLen > (terminator ? 1u : 0u)) previousCr = false;
            if (terminator == '\r') {
                if (!xssh_query_suppress_prompt(adapter))
                    xssh_emit_prompt_after_line(adapter);
                previousCr = true;
            } else if (terminator == '\n') {
                if (!previousCr && !xssh_query_suppress_prompt(adapter))
                    xssh_emit_prompt_after_line(adapter);
                previousCr = false;
            }
            pos = end;
        }
        XSSH_DATA(adapter)->promptPendingCr = previousCr;
        if (xssh_query_close_requested(adapter)) {
            if (!xssh_send_channel_eof(adapter) || !xssh_send_channel_close(adapter))
                return XProtocolResult_Failed;
            XSSH_DATA(adapter)->closed = true;
            return XProtocolResult_Ok;
        }
        if (!xssh_query_is_running(adapter)) {
            if (!xssh_send_channel_eof(adapter) || !xssh_send_channel_close(adapter))
                return XProtocolResult_Failed;
            XSSH_DATA(adapter)->closed = true;
            return XProtocolResult_Ok;
        }
        if (!xssh_replenish_channel_window(adapter)) return XProtocolResult_Failed;
        /* Shell 命令的返回值（Ok、MoreOutput、PermissionDenied 等）是应用层
           结果，不是传输层错误；只有写入目标的 IoError 才应关闭 SSH 连接。
           因此这里统一归一化为 Ok，IoError 仍继续上抛由调用方关闭。 */
        return result == XProtocolResult_IoError ? XProtocolResult_IoError
                                                : XProtocolResult_Ok;
    }
}

static XProtocolResult xssh_handle_channel_window_adjust(
    XSshServer* adapter, const uint8_t* payload, size_t len)
{
    uint32_t recipient;
    uint32_t amount;
    if (!adapter || !payload || len != 9) return XProtocolResult_Failed;
    xssh_get_u32(payload + 1, &recipient);
    xssh_get_u32(payload + 5, &amount);
    if (!XSSH_DATA(adapter)->channelOpen || recipient != XSSH_DATA(adapter)->localChannel || amount == 0 ||
        UINT32_MAX - XSSH_DATA(adapter)->clientWindow < amount)
        return xssh_channel_protocol_error(adapter);
    XSSH_DATA(adapter)->clientWindow += amount;
    if (!xssh_flush_channel_pending(adapter)) return XProtocolResult_Failed;
    return XProtocolResult_Ok;
}

static XProtocolResult xssh_handle_channel_eof(XSshServer* adapter,
                                              const uint8_t* payload, size_t len)
{
    uint32_t recipient;
    if (!adapter || !payload || len != 5) return XProtocolResult_Failed;
    xssh_get_u32(payload + 1, &recipient);
    if (!XSSH_DATA(adapter)->channelOpen || recipient != XSSH_DATA(adapter)->localChannel)
        return xssh_channel_protocol_error(adapter);
    XSSH_DATA(adapter)->remoteEof = true;
    if (!xssh_send_channel_eof(adapter)) return XProtocolResult_Failed;
    return XProtocolResult_Ok;
}

static XProtocolResult xssh_handle_channel_close(XSshServer* adapter,
                                                const uint8_t* payload, size_t len)
{
    uint32_t recipient;
    if (!adapter || !payload || len != 5) return XProtocolResult_Failed;
    xssh_get_u32(payload + 1, &recipient);
    if (!XSSH_DATA(adapter)->channelOpen || recipient != XSSH_DATA(adapter)->localChannel)
        return xssh_channel_protocol_error(adapter);
    XSSH_DATA(adapter)->remoteClose = true;
    if (!xssh_send_channel_close(adapter)) return XProtocolResult_Failed;
    XSSH_DATA(adapter)->closed = true;
    return XProtocolResult_Ok;
}

static XProtocolResult xssh_handle_channel(XSshServer* adapter,
                                          const uint8_t* payload, size_t len)
{
    uint8_t msg;
    if (len < 1) return XProtocolResult_Failed;
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
                return XProtocolResult_Failed;
            return XProtocolResult_Ok;
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
        return XProtocolResult_Ok;
    case SSH_MSG_CHANNEL_EOF:
        return xssh_handle_channel_eof(adapter, payload, len);
    case SSH_MSG_CHANNEL_CLOSE:
        return xssh_handle_channel_close(adapter, payload, len);
    default:
        return XProtocolResult_Ok;
    }
}

static XProtocolResult xssh_handle_payload(XSshServer* adapter,
                                          const uint8_t* payload, size_t len)
{
    uint8_t msg;
    if (len < 1) return XProtocolResult_Failed;
    msg = payload[0];
    switch (msg) {
    case SSH_MSG_DISCONNECT:
        XSSH_DATA(adapter)->closed = true;
        return XProtocolResult_Ok;
    case SSH_MSG_IGNORE:
    case SSH_MSG_DEBUG:
    case SSH_MSG_UNIMPLEMENTED:
    case SSH_MSG_EXT_INFO:
        return XProtocolResult_Ok;
    default:
        break;
    }
    switch (XSSH_DATA(adapter)->state) {
    case XSshState_KexInit:
        if (msg != SSH_MSG_KEXINIT) return XProtocolResult_Failed;
        if (len > sizeof(XSSH_DATA(adapter)->clientKexInit)) return XProtocolResult_Failed;
        memcpy(XSSH_DATA(adapter)->clientKexInit, payload, len);
        XSSH_DATA(adapter)->clientKexInitLen = len;
        if (!xssh_check_kexinit(adapter)) return XProtocolResult_Failed;
        XSSH_DATA(adapter)->state = XSshState_KexEcdh;
        return XProtocolResult_Ok;
    case XSshState_KexEcdh:
        if (msg != SSH_MSG_KEX_ECDH_INIT) return XProtocolResult_Failed;
        return xssh_handle_kex_ecdh_init(adapter, payload, len);
    case XSshState_NewKeys:
        if (msg != SSH_MSG_NEWKEYS) return XProtocolResult_Failed;
        XSSH_DATA(adapter)->recvEncrypted = true;
        XSSH_DATA(adapter)->state = XSshState_Service;
        return XProtocolResult_Ok;
    case XSshState_Service:
        if (msg != SSH_MSG_SERVICE_REQUEST) return XProtocolResult_Failed;
        return xssh_handle_service_request(adapter, payload, len);
    case XSshState_Userauth:
        if (msg != SSH_MSG_USERAUTH_REQUEST) return XProtocolResult_Failed;
        return xssh_handle_userauth_request(adapter, payload, len);
    case XSshState_Channel:
        return xssh_handle_channel(adapter, payload, len);
    default:
        return XProtocolResult_Failed;
    }
}

/* ------------------------------------------------------------------ */
/* Shell I/O 回调                                                     */
/* ------------------------------------------------------------------ */

static size_t xssh_channel_pending_size(const XSshServer* adapter)
{
    if (!adapter || XSSH_DATA(adapter)->channelTxPendingOffset > XSSH_DATA(adapter)->channelTxPendingLen)
        return 0;
    return XSSH_DATA(adapter)->channelTxPendingLen - XSSH_DATA(adapter)->channelTxPendingOffset;
}

static void xssh_channel_pending_compact(XSshServer* adapter)
{
    size_t pending;
    if (!adapter) return;
    pending = xssh_channel_pending_size(adapter);
    if (pending && XSSH_DATA(adapter)->channelTxPendingOffset)
        memmove(XSSH_DATA(adapter)->channelTxPending,
                XSSH_DATA(adapter)->channelTxPending + XSSH_DATA(adapter)->channelTxPendingOffset, pending);
    XSSH_DATA(adapter)->channelTxPendingOffset = 0;
    XSSH_DATA(adapter)->channelTxPendingLen = pending;
}

/* 向通道待发送缓冲追加一个字节；缓冲满时先尝试发送并压缩，仍放不下返回 false。 */
static bool xssh_channel_pending_append(XSshServer* adapter,
                                        uint8_t byte)
{
    if (!adapter) return false;
    if (XSSH_DATA(adapter)->channelTxPendingLen >= sizeof(XSSH_DATA(adapter)->channelTxPending)) {
        (void)xssh_flush_channel_pending(adapter);
        xssh_channel_pending_compact(adapter);
    }
    if (XSSH_DATA(adapter)->channelTxPendingLen >= sizeof(XSSH_DATA(adapter)->channelTxPending))
        return false;
    XSSH_DATA(adapter)->channelTxPending[XSSH_DATA(adapter)->channelTxPendingLen++] = byte;
    return true;
}

static bool xssh_send_channel_data_now(XSshServer* adapter,
                                       const uint8_t* data, size_t len)
{
    uint8_t payload[XSSH_MAX_PACKET];
    size_t off = 0;
    if (!adapter || !data || len == 0 || len > XSSH_MAX_PACKET - 32 ||
        !XSSH_DATA(adapter)->channelOpen || XSSH_DATA(adapter)->localClose || XSSH_DATA(adapter)->remoteClose ||
        len > XSSH_DATA(adapter)->clientWindow || len > XSSH_DATA(adapter)->clientMaxPacket)
        return false;
    /* Flush a previous packet before appending another one.  This keeps the
     * encoded transport buffer bounded even when a command emits many chunks.
     * A non-blocking transport may leave the previous bytes queued; in that
     * case the caller retains the channel data in channelTxPending. */
    if (!xssh_flush(adapter)) return false;
    if (XSSH_DATA(adapter)->txOutLen != 0) return false;
    payload[off++] = SSH_MSG_CHANNEL_DATA;
    xssh_write_u32(payload + off, XSSH_DATA(adapter)->remoteChannel);
    off += 4;
    xssh_write_string(payload, &off, data, len);
    if (!xssh_send_packet(adapter, payload, off)) return false;
    XSSH_DATA(adapter)->clientWindow -= (uint32_t)len;
    (void)xssh_flush(adapter);
    return true;
}

static bool xssh_flush_channel_pending(XSshServer* adapter)
{
    if (!adapter) return false;
    xssh_channel_pending_compact(adapter);
    while (xssh_channel_pending_size(adapter) && XSSH_DATA(adapter)->clientWindow &&
           !XSSH_DATA(adapter)->closed && !XSSH_DATA(adapter)->localClose && !XSSH_DATA(adapter)->remoteClose) {
        size_t chunk = xssh_channel_pending_size(adapter);
        if (chunk > XSSH_CHANNEL_DATA_CHUNK) chunk = XSSH_CHANNEL_DATA_CHUNK;
        if (chunk > XSSH_DATA(adapter)->clientWindow) chunk = XSSH_DATA(adapter)->clientWindow;
        if (chunk > XSSH_DATA(adapter)->clientMaxPacket) chunk = XSSH_DATA(adapter)->clientMaxPacket;
        if (chunk == 0) break;
        if (!xssh_send_channel_data_now(adapter,
                                        XSSH_DATA(adapter)->channelTxPending +
                                            XSSH_DATA(adapter)->channelTxPendingOffset,
                                        chunk)) {
            if (XSSH_DATA(adapter)->closed) return false;
            break;
        }
        XSSH_DATA(adapter)->channelTxPendingOffset += chunk;
        if (XSSH_DATA(adapter)->channelTxPendingOffset == XSSH_DATA(adapter)->channelTxPendingLen)
            XSSH_DATA(adapter)->channelTxPendingOffset = XSSH_DATA(adapter)->channelTxPendingLen = 0;
    }
    return !XSSH_DATA(adapter)->closed;
}

/* ==================== 公共输出/输入接口 ==================== */

/* 发送 SSH 会话输出：Shell 输出经加密通道写回客户端。 */
int64_t XSshServer_write(XSshServer* adapter, const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    size_t i;
    if (!adapter || XSSH_DATA(adapter)->closed || !XSSH_DATA(adapter)->channelOpen ||
        !XSSH_DATA(adapter)->shellStarted || XSSH_DATA(adapter)->localEof || XSSH_DATA(adapter)->localClose ||
        XSSH_DATA(adapter)->remoteClose ||
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

bool XSshServer_setInputEcho(XSshServer* adapter, bool enabled)
{
    if (!adapter) return false;
    XSSH_DATA(adapter)->inputEcho = enabled;
    XSSH_DATA(adapter)->echoPendingCr = false;
    XSSH_DATA(adapter)->echoEscape = false;
    return true;
}

/* 发送交互提示符：当前连接会话已登录显示“用户名>”，未登录或未启用登录
 * 显示默认名。通过 userNameRequested 信号向宿主查询当前连接绑定的用户。 */
bool XSshServer_emitPrompt(XSshServer* adapter)
{
    char prompt[XSSH_LOGIN_NAME_SIZE + 2u];
    char name[XSSH_LOGIN_NAME_SIZE];
    size_t len;
    if (!adapter)
        return false;
    len = xssh_query_user_name(adapter, name, sizeof(name));
    if (len == 0 || len >= sizeof(name)) {
        const char* def = (const char*)XSSH_DEFAULT_PROMPT_NAME;
        len = strlen(def);
        if (len >= sizeof(name)) len = sizeof(name) - 1u;
        memcpy(name, def, len);
    }
    if (len >= sizeof(prompt) - 2u)
        len = sizeof(prompt) - 2u;
    memcpy(prompt, name, len);
    prompt[len++] = '>';
    prompt[len++] = ' ';
    return XSshServer_write(adapter, prompt, len) == (int64_t)len;
}

/* 行结束后检查是否需要发送提示符。 */
static void xssh_emit_prompt_after_line(XSshServer* adapter)
{
    if (!adapter || !adapter->m_data || XSSH_DATA(adapter)->closed ||
        !XSSH_DATA(adapter)->channelOpen || !XSSH_DATA(adapter)->shellStarted ||
        XSSH_DATA(adapter)->localClose || XSSH_DATA(adapter)->remoteClose ||
        !xssh_query_is_running(adapter))
        return;
    (void)XSshServer_emitPrompt(adapter);
}

/* ==================== 主机查询（信号 + setter 回填） ==================== */

static bool xssh_query_authenticate(XSshServer* adapter,
                                    const char* user, const char* password)
{
    if (!adapter) return false;
    adapter->m_authResult = false;
    XSshServer_authenticateRequested_signal(adapter, user, password);
    return adapter->m_authResult;
}

static bool xssh_query_is_running(XSshServer* adapter)
{
    if (!adapter) return false;
    adapter->m_isRunningResult = false;
    XSshServer_isRunningRequested_signal(adapter);
    return adapter->m_isRunningResult;
}

static bool xssh_query_close_requested(XSshServer* adapter)
{
    if (!adapter) return false;
    adapter->m_closeRequestedResult = false;
    XSshServer_closeRequested_signal(adapter);
    return adapter->m_closeRequestedResult;
}

static bool xssh_query_suppress_prompt(XSshServer* adapter)
{
    if (!adapter) return false;
    adapter->m_suppressPromptResult = false;
    XSshServer_suppressPromptRequested_signal(adapter);
    return adapter->m_suppressPromptResult;
}

static size_t xssh_query_user_name(XSshServer* adapter,
                                   char* buffer, size_t capacity)
{
    size_t n;
    if (!adapter || !buffer || !capacity) return 0;
    adapter->m_userNameResultLen = 0;
    adapter->m_userNameResult[0] = '\0';
    XSshServer_userNameRequested_signal(
        adapter, adapter->m_userNameResult, sizeof(adapter->m_userNameResult));
    n = adapter->m_userNameResultLen;
    if (n >= capacity) n = capacity - 1u;
    if (n) memcpy(buffer, adapter->m_userNameResult, n);
    buffer[n] = '\0';
    return n;
}

/* 投递解密后的通道字节给宿主 Shell，并读取同步回填结果。 */
static XProtocolResult xssh_feed_bytes(XSshServer* adapter,
                                       const void* data, size_t size)
{
    if (!adapter) return XProtocolResult_InvalidArgument;
    adapter->m_bytesReceivedResult = XProtocolResult_Ok;
    XSshServer_bytesReceived_signal(adapter, data, size);
    return (XProtocolResult)adapter->m_bytesReceivedResult;
}


/* ------------------------------------------------------------------ */
/* 输入处理                                                            */
/* ------------------------------------------------------------------ */

static XProtocolResult xssh_process_one(XSshServer* adapter)
{
    if (XSSH_DATA(adapter)->state == XSshState_Version) {
        const uint8_t* nl;
        size_t lineLen;
        if (XSSH_DATA(adapter)->rxLen == 0) return XProtocolResult_Ok;
        nl = (const uint8_t*)memchr(XSSH_DATA(adapter)->rxBuf, '\n', XSSH_DATA(adapter)->rxLen);
        if (!nl) {
            if (XSSH_DATA(adapter)->rxLen > XSSH_VERSION_MAX) return XProtocolResult_Failed;
            return XProtocolResult_Ok;
        }
        lineLen = (size_t)(nl - XSSH_DATA(adapter)->rxBuf) + 1;
        if (lineLen < 9 ||
            (memcmp(XSSH_DATA(adapter)->rxBuf, "SSH-2.0-", 8) != 0 &&
             memcmp(XSSH_DATA(adapter)->rxBuf, "SSH-1.99-", 9) != 0))
            return XProtocolResult_Failed;
        if (lineLen > sizeof(XSSH_DATA(adapter)->clientVersion)) return XProtocolResult_Failed;
        memcpy(XSSH_DATA(adapter)->clientVersion, XSSH_DATA(adapter)->rxBuf, lineLen);
        XSSH_DATA(adapter)->clientVersionLen = lineLen;
        memmove(XSSH_DATA(adapter)->rxBuf, XSSH_DATA(adapter)->rxBuf + lineLen, XSSH_DATA(adapter)->rxLen - lineLen);
        XSSH_DATA(adapter)->rxLen -= lineLen;
        XSSH_DATA(adapter)->state = XSshState_KexInit;
        XSSH_DBG("version parsed len=%u remaining=%u\n", (unsigned)XSSH_DATA(adapter)->clientVersionLen, (unsigned)XSSH_DATA(adapter)->rxLen);
        return XProtocolResult_Ok;
    }

    if (XSSH_DATA(adapter)->recvEncrypted) {
        size_t totalCipher;
        uint8_t macInput[XSSH_RX_CAPACITY + 4];
        uint8_t macCalc[XSSH_MAC_LEN];
        uint8_t plain[XSSH_RX_CAPACITY + 4];
        size_t padLen;
        size_t payloadLen;

        if (!XSSH_DATA(adapter)->packetLenKnown) {
            if (XSSH_DATA(adapter)->rxLen < 4) return XProtocolResult_Ok;
            if (!XSSH_DATA(adapter)->decActive) {
                XSSH_DATA(adapter)->closed = true;
                return XProtocolResult_Failed;
            }
            if (XCryptographic_aesCtrUpdateInto(&XSSH_DATA(adapter)->decOp,
                    (char*)XSSH_DATA(adapter)->lenPlain,
                    sizeof(XSSH_DATA(adapter)->lenPlain),
                    XByteArrayView_create_data( XSSH_DATA(adapter)->rxBuf, 4 )).m_size != 4) {
                XSSH_DATA(adapter)->closed = true;
                return XProtocolResult_Failed;
            }
            xssh_get_u32(XSSH_DATA(adapter)->lenPlain, &XSSH_DATA(adapter)->packetLen);
            /* packet_length includes padding_length, payload, and padding;
             * with AES-CTR the complete packet (including the 4-byte length)
             * must be aligned to the 16-byte cipher block size. */
            if (XSSH_DATA(adapter)->packetLen < 5 || XSSH_DATA(adapter)->packetLen > XSSH_MAX_PACKET ||
                ((XSSH_DATA(adapter)->packetLen + 4u) & 15u) != 0u) {
                XSSH_DATA(adapter)->closed = true;
                return XProtocolResult_Failed;
            }
            XSSH_DATA(adapter)->packetLenKnown = true;
        }
        totalCipher = 4 + XSSH_DATA(adapter)->packetLen;
        if (XSSH_DATA(adapter)->rxLen < totalCipher + XSSH_MAC_LEN) return XProtocolResult_Ok;

        if (XCryptographic_aesCtrUpdateInto(&XSSH_DATA(adapter)->decOp,
                (char*)(plain + 4), totalCipher - 4,
                XByteArrayView_create_data( XSSH_DATA(adapter)->rxBuf + 4,
                                  (int64_t)(totalCipher - 4) )).m_size !=
            (int64_t)(totalCipher - 4)) {
            XSSH_DATA(adapter)->closed = true;
            return XProtocolResult_Failed;
        }
        memcpy(plain, XSSH_DATA(adapter)->lenPlain, 4);

        /* The MAC covers the sequence number and the complete plaintext
         * packet, including packet_length, padding_length, payload, and
         * padding.  The encrypted packet must be decrypted before checking
         * the MAC; otherwise CTR ciphertext bytes are authenticated. */
        xssh_write_u32(macInput, XSSH_DATA(adapter)->recvSeq);
        memcpy(macInput + 4, plain, totalCipher);
        if (!xssh_hmac_sha256(XSSH_DATA(adapter)->macKeyC2S,
                              sizeof(XSSH_DATA(adapter)->macKeyC2S),
                              macInput, 4 + totalCipher, macCalc) ||
            !xssh_const_equal(macCalc, XSSH_DATA(adapter)->rxBuf + totalCipher, XSSH_MAC_LEN)) {
            XSSH_DATA(adapter)->closed = true;
            return XProtocolResult_Failed;
        }

        memmove(XSSH_DATA(adapter)->rxBuf, XSSH_DATA(adapter)->rxBuf + totalCipher + XSSH_MAC_LEN,
                XSSH_DATA(adapter)->rxLen - (totalCipher + XSSH_MAC_LEN));
        XSSH_DATA(adapter)->rxLen -= totalCipher + XSSH_MAC_LEN;
        XSSH_DATA(adapter)->packetLenKnown = false;
        ++XSSH_DATA(adapter)->recvSeq;

        padLen = plain[4];
        if (padLen < 4 || (size_t)padLen >= XSSH_DATA(adapter)->packetLen) {
            XSSH_DATA(adapter)->closed = true;
            return XProtocolResult_Failed;
        }
        payloadLen = XSSH_DATA(adapter)->packetLen - padLen - 1;
        if (payloadLen > sizeof(XSSH_DATA(adapter)->payloadBuf)) {
            XSSH_DATA(adapter)->closed = true;
            return XProtocolResult_Failed;
        }
        memcpy(XSSH_DATA(adapter)->payloadBuf, plain + 5, payloadLen);
        return xssh_handle_payload(adapter, XSSH_DATA(adapter)->payloadBuf, payloadLen);
    } else {
        uint32_t plen;
        size_t padLen;
        size_t payloadLen;
        if (XSSH_DATA(adapter)->rxLen < 4) return XProtocolResult_Ok;
        xssh_get_u32(XSSH_DATA(adapter)->rxBuf, &plen);
        XSSH_DBG("rx len=%u plen=%u state=%d\n", (unsigned)XSSH_DATA(adapter)->rxLen, (unsigned)plen, (int)XSSH_DATA(adapter)->state);
        if (plen == 0 || plen > XSSH_MAX_PACKET) return XProtocolResult_Failed;
        if (XSSH_DATA(adapter)->rxLen < 4 + plen) return XProtocolResult_Ok;
        padLen = XSSH_DATA(adapter)->rxBuf[4];
        if (padLen < 4 || (size_t)padLen >= plen) return XProtocolResult_Failed;
        payloadLen = plen - padLen - 1;
        if (payloadLen > sizeof(XSSH_DATA(adapter)->payloadBuf)) return XProtocolResult_Failed;
        memcpy(XSSH_DATA(adapter)->payloadBuf, XSSH_DATA(adapter)->rxBuf + 5, payloadLen);
        memmove(XSSH_DATA(adapter)->rxBuf, XSSH_DATA(adapter)->rxBuf + 4 + plen, XSSH_DATA(adapter)->rxLen - (4 + plen));
        XSSH_DATA(adapter)->rxLen -= 4 + plen;
        ++XSSH_DATA(adapter)->recvSeq;
        return xssh_handle_payload(adapter, XSSH_DATA(adapter)->payloadBuf, payloadLen);
    }
}

/* ------------------------------------------------------------------ */
/* 公共 API                                                           */
/* ------------------------------------------------------------------ */

XSshServer* XSshServer_create_ex(XMemoryType memory)
{
    XSshServer* self = (XSshServer*)XMemory_malloc(sizeof(XSshServer), memory);
    if (!self) return NULL;
    XSshServer_init(self);
    if (!self->m_data) {
        XMemory_method(memory)->free(self);
        return NULL;
    }
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

static void vxssh_server_cleanup_keys(XSshServer* adapter)
{
    if (!adapter || !adapter->m_data) return;
    if (XSSH_DATA(adapter)->encActive) {
        XCryptographic_aesCtrAbort(&XSSH_DATA(adapter)->encOp);
        XSSH_DATA(adapter)->encActive = false;
    }
    if (XSSH_DATA(adapter)->decActive) {
        XCryptographic_aesCtrAbort(&XSSH_DATA(adapter)->decOp);
        XSSH_DATA(adapter)->decActive = false;
    }
    if (XSSH_DATA(adapter)->hostKeyShared) {
        if (g_xssh_hostKeyRefs) --g_xssh_hostKeyRefs;
        if (g_xssh_hostKeyRefs == 0 && xssh_key_is_valid(&g_xssh_hostKey)) {
            XCryptographic_destroyKey(&g_xssh_hostKey);
            g_xssh_hostKeyBlobLen = 0;
            memset(g_xssh_hostKeyBlob, 0, sizeof(g_xssh_hostKeyBlob));
        }
    } else {
        XCryptographic_destroyKey(&XSSH_DATA(adapter)->hostKey);
    }
    XCryptographic_destroyKey(&XSSH_DATA(adapter)->ecdhKey);
    XCryptographic_destroyKey(&XSSH_DATA(adapter)->cipherKeyC2S);
    XCryptographic_destroyKey(&XSSH_DATA(adapter)->cipherKeyS2C);
    memset(XSSH_DATA(adapter)->macKeyC2S, 0, sizeof(XSSH_DATA(adapter)->macKeyC2S));
    memset(XSSH_DATA(adapter)->macKeyS2C, 0, sizeof(XSSH_DATA(adapter)->macKeyS2C));
}

static void xssh_device_ready_read(XObject* receiver, XVarList* args)
{
    XSshServer* self = (XSshServer*)receiver;
    uint8_t buf[1024];
    (void)args;
    if (!self || !self->m_device || !self->m_data) return;
    for (;;) {
        int64_t n = XIODevice_read_1(self->m_device, (char*)buf, (int64_t)sizeof(buf));
        if (n <= 0) break;
        (void)XSshServer_feedData(self, buf, (size_t)n);
        if (XSSH_DATA(self)->closed) break;
    }
}

bool XSshServer_setDevice(XSshServer* self, XIODevice* device)
{
    if (!self || !device) return false;
    if (self->m_device == device) return true;
    XSshServer_stop(self);
    self->m_device = device;
    if (self->m_data) {
        memset(self->m_data, 0, sizeof(XSshServerData));
        XSSH_DATA(self)->state = XSshState_Version;
        XSSH_DATA(self)->localChannel = 0;
        XSSH_DATA(self)->inputEcho = true;
    }
    self->m_readyRead = XObject_connect_1((XObject*)device,
        XSignal(XIODevice_readyRead_signal), (XObject*)self,
        xssh_device_ready_read, XConnectionType_Direct);
    return true;
}

void XSshServer_setHostContext(XSshServer* self, void* context)
{
    if (!self) return;
    self->m_hostContext = context;
}

bool XSshServer_start(XSshServer* self)
{
    if (!self || !self->m_device || !self->m_data) return false;
    if (!xssh_generate_host_key(self)) return false;
    if (!xssh_send_raw(self, (const uint8_t*)XSSH_VERSION, strlen(XSSH_VERSION))) return false;
    if (!xssh_send_kexinit(self)) return false;
    if (!xssh_flush(self)) return false;
    return true;
}

void XSshServer_stop(XSshServer* self)
{
    if (!self) return;
    if (self->m_readyRead) {
        XObject_disconnect_2(self->m_readyRead);
        self->m_readyRead = NULL;
    }
    if (self->m_data && !XSSH_DATA(self)->closed) {
        XSSH_DATA(self)->closed = true;
        XSshServer_closed_signal(self);
    }
}

XProtocolResult XSshServer_feedData(XSshServer* adapter,
                                    const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    if (!adapter || !adapter->m_data || (!data && size))
        return XProtocolResult_InvalidArgument;
    if (XSSH_DATA(adapter)->closed) return XProtocolResult_Failed;
    if (size > sizeof(XSSH_DATA(adapter)->rxBuf) - XSSH_DATA(adapter)->rxLen) {
        XSSH_DATA(adapter)->closed = true;
        return XProtocolResult_ResourceLimit;
    }
    if (size) memcpy(XSSH_DATA(adapter)->rxBuf + XSSH_DATA(adapter)->rxLen, bytes, size);
    XSSH_DATA(adapter)->rxLen += size;

    while (!XSSH_DATA(adapter)->closed) {
        size_t before = XSSH_DATA(adapter)->rxLen;
        XProtocolResult r = xssh_process_one(adapter);
        if (r < XProtocolResult_Ok) {
            XSSH_DATA(adapter)->closed = true;
            XSshServer_errorOccurred_signal(adapter, (int)r);
            return r;
        }
        if (XSSH_DATA(adapter)->rxLen == before) break;
    }
    if (!xssh_flush(adapter)) return XProtocolResult_IoError;
    return XProtocolResult_Ok;
}

bool XSshServer_flush(XSshServer* adapter)
{
    if (!adapter || !adapter->m_data || XSSH_DATA(adapter)->closed) return false;
    if (!xssh_flush(adapter)) return false;
    if (!xssh_flush_channel_pending(adapter)) return false;
    if (!xssh_flush(adapter)) return false;
    return adapter->m_device && XIODevice_flush(adapter->m_device);
}

bool XSshServer_isClosed(const XSshServer* adapter)
{
    return !adapter || !adapter->m_data || XSSH_DATA(adapter)->closed;
}

/* ==================== 信号（协议栈 -> 宿主） ==================== */

void* XSshServer_bytesReceived_signal(XSshServer* self,
                                      const void* data, size_t size)
{
    XEmitSignal(self, XSshServer_bytesReceived_signal,
                XVarList_Create(XVar(const void*, data), XVar(size_t, size)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSshServer_authenticateRequested_signal(XSshServer* self,
                                              const char* user,
                                              const char* password)
{
    XEmitSignal(self, XSshServer_authenticateRequested_signal,
                XVarList_Create(XVar(const char*, user),
                                XVar(const char*, password)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSshServer_isRunningRequested_signal(XSshServer* self)
{
    XEmitSignal(self, XSshServer_isRunningRequested_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSshServer_closeRequested_signal(XSshServer* self)
{
    XEmitSignal(self, XSshServer_closeRequested_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSshServer_suppressPromptRequested_signal(XSshServer* self)
{
    XEmitSignal(self, XSshServer_suppressPromptRequested_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSshServer_userNameRequested_signal(XSshServer* self,
                                          char* buffer, size_t capacity)
{
    XEmitSignal(self, XSshServer_userNameRequested_signal,
                XVarList_Create(XVar(char*, buffer), XVar(size_t, capacity)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSshServer_closed_signal(XSshServer* self)
{
    XEmitSignal(self, XSshServer_closed_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSshServer_errorOccurred_signal(XSshServer* self, int error)
{
    XEmitSignal(self, XSshServer_errorOccurred_signal,
                XVarList_Create(XVar(int, error)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/* ==================== setter 回填（宿主 -> 协议栈） ==================== */

void XSshServer_setBytesReceivedResult(XSshServer* self, int result)
{
    if (self) self->m_bytesReceivedResult = result;
}

void XSshServer_setAuthenticateResult(XSshServer* self, bool result)
{
    if (self) self->m_authResult = result;
}

void XSshServer_setIsRunningResult(XSshServer* self, bool result)
{
    if (self) self->m_isRunningResult = result;
}

void XSshServer_setCloseRequestedResult(XSshServer* self, bool result)
{
    if (self) self->m_closeRequestedResult = result;
}

void XSshServer_setSuppressPromptResult(XSshServer* self, bool result)
{
    if (self) self->m_suppressPromptResult = result;
}

void XSshServer_setUserNameResult(XSshServer* self,
                                  const char* name, size_t len)
{
    if (!self) return;
    self->m_userNameResultLen = 0;
    self->m_userNameResult[0] = '\0';
    if (!name || len == 0) return;
    if (len >= sizeof(self->m_userNameResult))
        len = sizeof(self->m_userNameResult) - 1u;
    memcpy(self->m_userNameResult, name, len);
    self->m_userNameResult[len] = '\0';
    self->m_userNameResultLen = len;
}

static void VXSshServer_deinit(XSshServer* self)
{
    if (!self) return;
    XSshServer_stop(self);
    vxssh_server_cleanup_keys(self);
    if (self->m_data) {
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, self);
}

XVtable* XSshServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSshServer)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSshServer_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XSshServer);
    return XVTABLE_DEFAULT;
}

void XSshServer_init(XSshServer* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init(&self->m_class);
    XClassSetVtable(self, XSshServer);
    self->m_data = XCalloc_System(1, sizeof(XSshServerData));
    if (!self->m_data) return;
    XSSH_DATA(self)->state = XSshState_Version;
    XSSH_DATA(self)->localChannel = 0;
    XSSH_DATA(self)->inputEcho = true;
    self->m_bytesReceivedResult = XProtocolResult_Ok;
}

#endif /* XPROTOCOL_ON && XSSH_ON && XSSH_SERVER_ON */
