/** \file
 * @file       xupgrade_pack.c
 * @brief      XUpgrade 服务端打包签名工具（pack / seal / verify / verify-pak / keygen）。
 * @details    设计文档 XUpgrade.md §4/§5 的服务端配套工具：把版本目录打成
 *             .pak、自动补齐 manifest 组件的 size/sha256、按 canonical 规则
 *             签名（ECDSA P-256）并自验、对已发布 manifest 做设备侧同源
 *             验签、对已发布 .pak 做设备侧同源逐步校验（verify-pak）、生成
 *             产品钥对。全部复用库内实现（写包容器原语、canonical
 *             序列化、SHA-256、PSA 造签/验签），零口径漂移；文件遍历走
 *             XDir/XFileInfo 库内可移植 API，工具本体不含平台代码，任何能编
 *             XinYueCS 的宿主平台都能编译（Win32/Linux/macOS/BSD）。
 *
 *             用法：
 *               XUpgradePack pack   <源目录> <输出.pak> [--store]
 *               XUpgradePack seal   <draft.json> <out.json> [--key <64位hex私钥>]
 *                                   [--search-dir <目录>]...
 *               XUpgradePack verify <manifest.json> [--pubkey <130位hex SEC1公钥>]
 *                                   [--key <64位hex私钥>] [--search-dir <目录>]...
 *                                   [--no-files]
 *               XUpgradePack verify-pak <包.pak>
 *               XUpgradePack keygen <私钥输出文件> <公钥输出文件>
 *
 *             路径参数在 Windows 命令行为 GBK 编码，本工具经 XString 的 GBK
 *             入口统一转 UTF-8 后使用；包内条目名受
 *             XUpgradePackage_checkEntryName 白名单约束（ASCII），含白名单外
 *             字符的源文件会被拒绝并指名。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h> /* 本工具为宿主侧发布工程件（Tools 口径），不受 Src 白名单约束 */

#include "XClass.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XString.h"
#include "XFile.h"
#include "XDir.h"
#include "XFileInfo.h"
#include "XByteArrayView.h"
#include "XUpgrade_config.h"
#include "XUpgradeManifest.h"
#include "XUpgradePackage.h"
#include "XUpgrade_test_keys.h"

#if XUPGRADE_ON

/* ========================================================================== */
/*                     库内实现侧原语（extern，e2e 同款先例）                   */
/* ========================================================================== */

/* .pak 写包容器原语：契约头按 §5 设计只挂解析端，写端在 XUpgradePackage.c
 * 内供服务端工具使用（xupgrade_e2e_test.c 已有同款 extern 先例）。 */
extern size_t XUpgradePackage_entryWireSize(const XUpgradePackageEntry* entry);
extern bool XUpgradePackage_writeHeader(uint8_t* out, size_t cap, size_t* used,
                                        const XUpgradePackageHeader* header);
extern bool XUpgradePackage_writeEntry(uint8_t* out, size_t cap, size_t* used,
                                       const XUpgradePackageEntry* entry);
extern bool XUpgradePackage_compress(uint8_t* dest, size_t destCap, size_t* destLen,
                                     const uint8_t* src, size_t srcLen);

/* SHA-256（单段+流式，XCryptographicHash 门面封装）。 */
extern bool XUpgradeVerify_sha256Digest(const uint8_t* data, size_t len, uint8_t* outDigest);
typedef struct XUpgradeVerifySha256 XUpgradeVerifySha256;
extern XUpgradeVerifySha256* XUpgradeVerify_sha256Create(void);
extern void XUpgradeVerify_sha256Delete(XUpgradeVerifySha256* ctx);
extern bool XUpgradeVerify_sha256Begin(XUpgradeVerifySha256* ctx);
extern bool XUpgradeVerify_sha256Update(XUpgradeVerifySha256* ctx, const uint8_t* data, size_t len);
extern bool XUpgradeVerify_sha256End(XUpgradeVerifySha256* ctx, uint8_t* outDigest);

/* ECDSA P-256 造签/验签/公钥派生/密钥生成（PSA 适配层，库内门面）。 */
extern bool XSsl_p256SignHashDer(const uint8_t* privateKeyBe, size_t privateKeyLen,
                                 const uint8_t* sha256Digest, size_t sha256Len,
                                 uint8_t* outDerSignature, size_t cap, size_t* outLen);
extern bool XSsl_p256VerifySha256Der(const uint8_t* publicKeySec1, size_t publicKeyLen,
                                     const uint8_t* sha256Digest, size_t sha256Len,
                                     const uint8_t* derSignature, size_t signatureLen);
extern bool XSsl_p256DerivePublicKey(const uint8_t* privateKeyBe, size_t privateKeyLen,
                                     uint8_t* outPublicKeySec1, size_t cap, size_t* outLen);
extern bool XSsl_p256GenerateKeyPair(uint8_t* outPrivateKeyBe, size_t privCap, size_t* outPrivLen,
                                     uint8_t* outPublicKeySec1, size_t pubCap, size_t* outPubLen);

/* ========================================================================== */
/*                                  常量                                       */
/* ========================================================================== */

/** @brief 单包条目数上限（发布目录的常规规模远低于此；超限请拆分组件）。 */
#define XPACK_ENTRY_MAX 1024u
/** @brief 单文件大小上限（字节），与引擎 XUPGRADE_ENTRY_MAX_RAW 同口径。 */
#define XPACK_FILE_MAX ((uint64_t)512 * 1024 * 1024)
/** @brief 文件流式哈希的读块大小。 */
#define XPACK_CHUNK 65536
/** @brief 搜索目录数量上限。 */
#define XPACK_SEARCH_MAX 8
/** @brief P-256 DER 签名最大长度（与适配层 XSSL_P256_DER_SIG_MAX 同值）。 */
#define XPACK_DER_SIG_MAX 72u
/** @brief 信任公钥 SEC1 未压缩长度（与 XUpgrade.h 的注入格式常量同值）。 */
#define XPACK_PUB_SEC1_SIZE 65u

/* ========================================================================== */
/*                              小工具（hex/base64/串）                        */
/* ========================================================================== */

/* strlen 直接用 <string.h> 标准版：既有 include 原型在先，本地 static
 * 重定义与其冲突（GCC "static declaration follows non-static"）。
 * 调用点均为非 NULL 字面量/缓冲，标准版语义覆盖。 */

static bool xpack_strEq(const char* a, const char* b)
{
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

static bool xpack_hexDigit(char c, uint8_t* outValue)
{
    if (c >= '0' && c <= '9') { *outValue = (uint8_t)(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { *outValue = (uint8_t)(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { *outValue = (uint8_t)(c - 'A' + 10); return true; }
    return false;
}

static bool xpack_hexToBytes(const char* hex, uint8_t* out, size_t cap, size_t* outLen)
{
    size_t n = strlen(hex);
    size_t i;
    if ((n & 1u) != 0u || n / 2u > cap || n == 0u) return false;
    for (i = 0; i < n; i += 2) {
        uint8_t hi;
        uint8_t lo;
        if (!xpack_hexDigit(hex[i], &hi) || !xpack_hexDigit(hex[i + 1], &lo)) return false;
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    *outLen = n / 2;
    return true;
}

static void xpack_bytesToHex(const uint8_t* data, size_t len, char* out, size_t cap)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    if (cap < len * 2u + 1u) return;
    for (i = 0; i < len; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0Fu];
    }
    out[len * 2] = '\0';
}

static const char xpack_b64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t xpack_b64EncodedLen(size_t rawLen)
{
    return ((rawLen + 2u) / 3u) * 4u;
}

static void xpack_b64Encode(const uint8_t* data, size_t len, char* out, size_t cap)
{
    size_t i;
    size_t o = 0;
    if (cap < xpack_b64EncodedLen(len) + 1u) return;
    for (i = 0; i + 2u < len; i += 3u) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
        out[o++] = xpack_b64Chars[(v >> 18) & 0x3Fu];
        out[o++] = xpack_b64Chars[(v >> 12) & 0x3Fu];
        out[o++] = xpack_b64Chars[(v >> 6) & 0x3Fu];
        out[o++] = xpack_b64Chars[v & 0x3Fu];
    }
    if (i < len) {
        uint32_t v = (uint32_t)data[i] << 16;
        int remain = 1;
        if (i + 1u < len) {
            v |= (uint32_t)data[i + 1] << 8;
            remain = 2;
        }
        out[o++] = xpack_b64Chars[(v >> 18) & 0x3Fu];
        out[o++] = xpack_b64Chars[(v >> 12) & 0x3Fu];
        out[o++] = (remain == 2) ? xpack_b64Chars[(v >> 6) & 0x3Fu] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
}

static int xpack_b64Value(char c)
{
    const char* p = xpack_b64Chars;
    int i;
    if (c == '=') return 0;
    for (i = 0; i < 64; ++i) {
        if (p[i] == c) return i;
    }
    return -1;
}

static bool xpack_b64Decode(const char* text, uint8_t* out, size_t cap, size_t* outLen)
{
    size_t n = strlen(text);
    size_t i;
    size_t o = 0;
    if (n == 0u || (n & 3u) != 0u) return false;
    for (i = 0; i < n; i += 4) {
        int v0 = xpack_b64Value(text[i]);
        int v1 = xpack_b64Value(text[i + 1]);
        int v2 = xpack_b64Value(text[i + 2]);
        int v3 = xpack_b64Value(text[i + 3]);
        uint32_t v;
        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
        if (o + 3u > cap) return false;
        v = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
        out[o++] = (uint8_t)(v >> 16);
        if (text[i + 2] != '=') out[o++] = (uint8_t)(v >> 8);
        if (text[i + 3] != '=') out[o++] = (uint8_t)v;
    }
    *outLen = o;
    return true;
}

/** @brief 命令行参数转 UTF-8：Win32 命令行为 ANSI/GBK，经 XString GBK 入口
 *         统一转换；POSIX 参数本就是 UTF-8，直接取用。 */
static bool xpack_argToUtf8(const char* arg, char* out, size_t cap)
{
    XString* s = NULL;
    const char* utf8 = NULL;
    size_t len = 0;
#ifdef _WIN32
    s = XString_create_gbk(arg);
#else
    s = XString_create_utf8(arg);
#endif
    if (!s) return false;
    utf8 = XString_toUtf8(s);
    len = XString_toUtf8_length(s);
    if (!utf8 || len >= cap) {
        XClassDelete(s);
        return false;
    }
    memcpy(out, utf8, len + 1u);
    XClassDelete(s);
    return true;
}

/* ========================================================================== */
/*                              文件读写（整读/流式哈希）                       */
/* ========================================================================== */

static bool xpack_readWholeFile(const char* pathUtf8, uint8_t** outData, size_t* outLen)
{
    XString* path = NULL;
    XFile* file = NULL;
    XByteArrayView view;
    uint8_t* data = NULL;
    XFileInfo info;
    int64_t size;
    (void)view;
    *outData = NULL;
    *outLen = 0;
    path = XString_create_utf8(pathUtf8);
    if (!path) return false;
    XFileInfo_init_2(&info, path);
    size = XFileInfo_size(&info);
    XClassDeinit(&info);
    XClassDelete(path);
    if (size < 0 || (uint64_t)size > XPACK_FILE_MAX) {
        XPrintf("[pack] 文件为空或超过上限：%s\n", pathUtf8);
        return false;
    }
    path = XString_create_utf8(pathUtf8);
    if (!path) return false;
    file = XFile_create_2(path);
    XClassDelete(path);
    if (!file) return false;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        XClassDelete((XClass*)file);
        return false;
    }
    data = (uint8_t*)XMalloc_System((size_t)size + 1u);
    if (!data) {
        XIODevice_close_base((XIODevice*)file);
        XClassDelete((XClass*)file);
        return false;
    }
    {
        int64_t got = XIODevice_readAll_1((XIODevice*)file, (char*)data, size);
        XIODevice_close_base((XIODevice*)file);
        XClassDelete((XClass*)file);
        if (got != size) {
            XFree_System(data);
            return false;
        }
        data[size] = 0;
        *outData = data;
        *outLen = (size_t)size;
    }
    return true;
}

static bool xpack_writeWholeFile(const char* pathUtf8, const uint8_t* data, size_t len)
{
    XString* path = NULL;
    XFile* file = NULL;
    bool ok;
    path = XString_create_utf8(pathUtf8);
    if (!path) return false;
    file = XFile_create_2(path);
    XClassDelete(path);
    if (!file) return false;
    ok = XFile_open_2(file, XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create, 0);
    if (ok) {
        int64_t put = XIODevice_write_1((XIODevice*)file, (const char*)data, (int64_t)len);
        ok = (put == (int64_t)len);
        XIODevice_close_base((XIODevice*)file);
    }
    XClassDelete((XClass*)file);
    return ok;
}

/** @brief 流式计算文件 SHA-256（64KB 块读，支持大镜像文件）。 */
static bool xpack_fileSha256(const char* pathUtf8, uint8_t* outDigest, uint64_t* outSize)
{
    XString* path = NULL;
    XFile* file = NULL;
    XUpgradeVerifySha256* ctx = NULL;
    char* chunk = NULL;
    uint64_t total = 0;
    bool ok = false;
    *outSize = 0;
    path = XString_create_utf8(pathUtf8);
    if (!path) return false;
    file = XFile_create_2(path);
    XClassDelete(path);
    if (!file) return false;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        XClassDelete((XClass*)file);
        return false;
    }
    ctx = XUpgradeVerify_sha256Create();
    chunk = (char*)XMalloc_System(XPACK_CHUNK);
    if (!ctx || !chunk) {
        if (ctx) XUpgradeVerify_sha256Delete(ctx);
        XFree_System(chunk);
        XIODevice_close_base((XIODevice*)file);
        XClassDelete((XClass*)file);
        return false;
    }
    ok = XUpgradeVerify_sha256Begin(ctx);
    while (ok) {
        int64_t got = XIODevice_read_1((XIODevice*)file, chunk, XPACK_CHUNK);
        if (got < 0) { ok = false; break; }
        if (got == 0) break;
        ok = XUpgradeVerify_sha256Update(ctx, (const uint8_t*)chunk, (size_t)got);
        total += (uint64_t)got;
    }
    if (ok) ok = XUpgradeVerify_sha256End(ctx, outDigest);
    XUpgradeVerify_sha256Delete(ctx);
    XFree_System(chunk);
    XIODevice_close_base((XIODevice*)file);
    XClassDelete((XClass*)file);
    if (ok) *outSize = total;
    return ok;
}

/* ========================================================================== */
/*                                目录遍历                                     */
/* ========================================================================== */

/** @brief 递归收集目录下全部文件的相对路径（'/' 分隔，UTF-8，ASCII 白名单
 *         由 checkEntryName 把关）。全部走库内可移植 API，无平台代码。 */
static bool xpack_walkDir(const char* dirUtf8, const char* relPrefix,
                          char names[][XUPGRADE_PACKAGE_NAME_MAX], uint32_t* count)
{
    XString* dirPath = NULL;
    XDir* dir = NULL;
    XStringList* list = NULL;
    XString* joined = NULL;
    const char* utf8 = NULL;
    size_t len = 0;
    size_t i = 0;
    size_t segStart = 0;
    bool ok = true;

    dirPath = XString_create_utf8(dirUtf8);
    if (!dirPath) return false;
    dir = XDir_create_2(dirPath);
    XClassDelete(dirPath);
    if (!dir) return false;
    list = XDir_entryList_1(dir, XDir_Files | XDir_Dirs | XDir_NoDotAndDotDot, XDir_Name);
    XClassDelete((XClass*)dir);
    if (!list) {
        XPrintf("[pack] 目录枚举失败：%s\n", dirUtf8);
        return false;
    }
    joined = XStringList_join_utf8(list, "\n");
    XClassDelete((XClass*)list);
    if (!joined) return false;
    utf8 = XString_toUtf8(joined);
    len = XString_toUtf8_length(joined);
    if (!utf8) {
        XClassDelete(joined);
        return false;
    }
    while (i <= len && ok) {
        char name[XUPGRADE_PACKAGE_NAME_MAX];
        size_t segLen;
        if (i == len || utf8[i] == '\n') {
            segLen = i - segStart;
            if (segLen > 0 && segLen < XUPGRADE_PACKAGE_NAME_MAX) {
                char rel[XUPGRADE_PACKAGE_NAME_MAX];
                memcpy(name, utf8 + segStart, segLen);
                name[segLen] = '\0';
                if (relPrefix[0] != '\0') {
                    if (strlen(relPrefix) + 1u + segLen >= sizeof(rel)) {
                        XPrintf("[pack] 相对路径过长：%s/%s\n", relPrefix, name);
                        ok = false;
                    } else {
                        size_t base = strlen(relPrefix);
                        memcpy(rel, relPrefix, base);
                        rel[base] = '/';
                        memcpy(rel + base + 1u, name, segLen + 1u);
                    }
                } else {
                    memcpy(rel, name, segLen + 1u);
                }
                if (ok) {
                    char childPath[1600];
                    if (*count >= XPACK_ENTRY_MAX) {
                        XPrintf("[pack] 条目数超过上限 %u\n", (unsigned)XPACK_ENTRY_MAX);
                        ok = false;
                    } else if (!XUpgradePackage_checkEntryName(rel)) {
                        XPrintf("[pack] 条目名不在白名单（ASCII [0-9A-Za-z._-/]），拒绝：%s\n", rel);
                        ok = false;
                    } else if (strlen(dirUtf8) + 1u + strlen(name) >= sizeof(childPath)) {
                        XPrintf("[pack] 路径过长：%s/%s\n", dirUtf8, name);
                        ok = false;
                    } else {
                        size_t base = strlen(dirUtf8);
                        memcpy(childPath, dirUtf8, base);
                        childPath[base] = '/';
                        memcpy(childPath + base + 1u, name, strlen(name) + 1u);
                        {
                            XString* cpath = XString_create_utf8(childPath);
                            XFileInfo info;
                            bool isDir = false;
                            if (!cpath) {
                                ok = false;
                            } else {
                                XFileInfo_init_2(&info, cpath);
                                isDir = XFileInfo_isDir(&info);
                                XClassDeinit(&info);
                                XClassDelete(cpath);
                            }
                            if (ok && isDir) {
                                ok = xpack_walkDir(childPath, rel, names, count);
                            } else if (ok) {
                                memcpy(names[*count], rel, strlen(rel) + 1u);
                                ++(*count);
                            }
                        }
                    }
                }
            }
            segStart = i + 1u;
            if (i == len) break;
        }
        ++i;
    }
    XClassDelete(joined);
    return ok;
}

/** @brief 按搜索目录清单定位组件包文件：基准目录（draft/manifest 所在目录）
 *         恒定首选，其后依次尝试 --search-dir。 */
static bool xpack_locateComponent(const char* fileName, const char* baseDir,
                                  const char searchDirs[][1600], uint32_t searchCount,
                                  char* outPath, size_t cap)
{
    uint32_t i;
    for (i = 0; i <= searchCount; ++i) {
        const char* dir = (i == 0) ? baseDir : searchDirs[i - 1];
        size_t dLen = strlen(dir);
        size_t fLen = strlen(fileName);
        if (dLen == 0 || dLen + 1u + fLen >= cap) continue;
        memcpy(outPath, dir, dLen);
        outPath[dLen] = '/';
        memcpy(outPath + dLen + 1u, fileName, fLen + 1u);
        {
            XString* p = XString_create_utf8(outPath);
            bool exists = false;
            if (p) {
                exists = XFile_exists_static(p);
                XClassDelete(p);
            }
            if (exists) return true;
        }
    }
    return false;
}

/* ========================================================================== */
/*                                  pack 子命令                                */
/* ========================================================================== */

static int xpack_cmdPack(const char* srcDir, const char* outPak, bool store)
{
    static char names[XPACK_ENTRY_MAX][XUPGRADE_PACKAGE_NAME_MAX];
    uint32_t count = 0;
    size_t headerSize = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
    size_t payloadLen = 0;
    size_t entryPos;
    size_t dataPos;
    uint64_t running = 0;
    uint8_t* buffer = NULL;
    uint8_t payloadSha[XUPGRADE_SHA256_BIN_SIZE];
    XUpgradePackageHeader header;
    uint32_t i;
    bool ok = true;

    if (!xpack_walkDir(srcDir, "", names, &count)) return 2;
    if (count == 0) {
        XPrintf("[pack] 源目录没有任何文件：%s\n", srcDir);
        return 2;
    }
    XPrintf("[pack] 收集到 %u 个条目\n", (unsigned)count);

    /* 第一遍：逐条目读入、摘要、探针压缩定长 → headerSize/payloadSize。 */
    for (i = 0; ok && i < count; ++i) {
        char path[1600];
        uint8_t* data = NULL;
        size_t len = 0;
        XUpgradePackageEntry probe;
        size_t compCap;
        uint8_t* comp = NULL;
        size_t compLen = 0;
        size_t dLen = strlen(srcDir);
        if (dLen + 1u + strlen(names[i]) >= sizeof(path)) {
            XPrintf("[pack] 路径过长：%s\n", names[i]);
            ok = false;
            break;
        }
        memcpy(path, srcDir, dLen);
        path[dLen] = '/';
        memcpy(path + dLen + 1u, names[i], strlen(names[i]) + 1u);
        if (!xpack_readWholeFile(path, &data, &len)) {
            XPrintf("[pack] 读取失败：%s\n", path);
            ok = false;
            break;
        }
        XMemset(&probe, 0, sizeof(probe));
        memcpy(probe.m_name, names[i], strlen(names[i]) + 1u);
        probe.m_rawSize = (uint64_t)len;
        if (!store) {
            compCap = len * 2u + 1024u;
            comp = (uint8_t*)XMalloc_System(compCap);
            if (!comp) {
                XFree_System(data);
                ok = false;
                break;
            }
            if (XUpgradePackage_compress(comp, compCap, &compLen, data, len) &&
                compLen < len) {
                probe.m_flags = XUPGRADE_PACKAGE_FLAG_ZLIB;
                probe.m_compSize = (uint64_t)compLen;
            } else {
                compLen = len;              /* 不可压缩内容按原样存储 */
                probe.m_flags = 0;
                probe.m_compSize = (uint64_t)len;
            }
            XFree_System(comp);
        } else {
            compLen = len;
            probe.m_flags = 0;
            probe.m_compSize = (uint64_t)len;
        }
        {
            size_t w = XUpgradePackage_entryWireSize(&probe);
            if (w == 0) {
                XPrintf("[pack] 条目序列化被拒：%s\n", names[i]);
                XFree_System(data);
                ok = false;
                break;
            }
            headerSize += w;
            payloadLen += compLen;
        }
        XFree_System(data);
    }
    if (!ok) return 2;

    buffer = (uint8_t*)XMalloc_System(headerSize + payloadLen);
    if (!buffer) return 3;
    entryPos = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
    dataPos = headerSize;

    /* 第二遍：条目表 + 数据区（与第一遍同参，zlib 确定性保证长度一致）。 */
    for (i = 0; ok && i < count; ++i) {
        char path[1600];
        uint8_t* data = NULL;
        size_t len = 0;
        XUpgradePackageEntry entry;
        size_t used = 0;
        size_t dLen = strlen(srcDir);
        memcpy(path, srcDir, dLen);
        path[dLen] = '/';
        memcpy(path + dLen + 1u, names[i], strlen(names[i]) + 1u);
        if (!xpack_readWholeFile(path, &data, &len)) {
            ok = false;
            break;
        }
        XMemset(&entry, 0, sizeof(entry));
        memcpy(entry.m_name, names[i], strlen(names[i]) + 1u);
        entry.m_rawSize = (uint64_t)len;
        entry.m_offset = running;
        if (!XUpgradeVerify_sha256Digest(data, len, entry.m_sha256)) {
            XFree_System(data);
            ok = false;
            break;
        }
        if (!store) {
            size_t compCap = len * 2u + 1024u;
            uint8_t* comp = (uint8_t*)XMalloc_System(compCap);
            size_t compLen = 0;
            if (!comp) {
                XFree_System(data);
                ok = false;
                break;
            }
            if (XUpgradePackage_compress(comp, compCap, &compLen, data, len) &&
                compLen < len) {
                entry.m_flags = XUPGRADE_PACKAGE_FLAG_ZLIB;
                entry.m_compSize = (uint64_t)compLen;
                if (dataPos + compLen > headerSize + payloadLen ||
                    !XUpgradePackage_writeEntry(buffer + entryPos,
                                                headerSize - entryPos, &used, &entry)) {
                    XFree_System(comp);
                    XFree_System(data);
                    ok = false;
                    break;
                }
                entryPos += used;
                memcpy(buffer + dataPos, comp, compLen);
            } else {
                entry.m_flags = 0;
                entry.m_compSize = (uint64_t)len;
                if (dataPos + len > headerSize + payloadLen ||
                    !XUpgradePackage_writeEntry(buffer + entryPos,
                                                headerSize - entryPos, &used, &entry)) {
                    XFree_System(comp);
                    XFree_System(data);
                    ok = false;
                    break;
                }
                entryPos += used;
                memcpy(buffer + dataPos, data, len);
            }
            XFree_System(comp);
        } else {
            entry.m_flags = 0;
            entry.m_compSize = (uint64_t)len;
            if (dataPos + len > headerSize + payloadLen ||
                !XUpgradePackage_writeEntry(buffer + entryPos,
                                            headerSize - entryPos, &used, &entry)) {
                XFree_System(data);
                ok = false;
                break;
            }
            entryPos += used;
            memcpy(buffer + dataPos, data, len);
        }
        dataPos += (size_t)entry.m_compSize;
        running += entry.m_compSize;
        XFree_System(data);
    }
    if (!ok) {
        XFree_System(buffer);
        return 2;
    }

    /* 第三遍：数据区整段哈希写头。 */
    if (!XUpgradeVerify_sha256Digest(buffer + headerSize, payloadLen, payloadSha)) {
        XFree_System(buffer);
        XPrintf("[pack] 数据区摘要失败\n");
        return 2;
    }
    XMemset(&header, 0, sizeof(header));
    header.m_format = XUPGRADE_PACKAGE_FORMAT;
    header.m_entryCount = count;
    header.m_headerSize = (uint32_t)headerSize;
    header.m_payloadSize = (uint64_t)payloadLen;
    XMemcpy(header.m_payloadSha256, payloadSha, XUPGRADE_SHA256_BIN_SIZE);
    if (!XUpgradePackage_writeHeader(buffer, headerSize, &entryPos, &header)) {
        XFree_System(buffer);
        XPrintf("[pack] 包头序列化失败\n");
        return 2;
    }
    {
        uint8_t whole[XUPGRADE_SHA256_BIN_SIZE];
        if (!XUpgradeVerify_sha256Digest(buffer, headerSize + payloadLen, whole)) {
            XFree_System(buffer);
            return 2;
        }
        {
            char hex[XUPGRADE_SHA256_BIN_SIZE * 2u + 1u];
            xpack_bytesToHex(whole, XUPGRADE_SHA256_BIN_SIZE, hex, sizeof(hex));
            if (!xpack_writeWholeFile(outPak, buffer, headerSize + payloadLen)) {
                XPrintf("[pack] 写出失败：%s\n", outPak);
                XFree_System(buffer);
                return 2;
            }
            XPrintf("[pack] 完成：%s（%u 条目，payload %u 字节，整包 sha256=%s）\n",
                    outPak, (unsigned)count, (unsigned)payloadLen, hex);
        }
    }
    XFree_System(buffer);
    return 0;
}

/* ========================================================================== */
/*                            seal / verify 共用部分                           */
/* ========================================================================== */

static bool xpack_resolveTrustKey(bool havePubHex, const char* pubHex,
                                  bool havePrivHex, const char* privHex,
                                  uint8_t* outPub, size_t cap, size_t* outLen)
{
    uint8_t priv[32];
    size_t privLen = 0;
    if (havePubHex) {
        return xpack_hexToBytes(pubHex, outPub, cap, outLen) &&
               *outLen == XPACK_PUB_SEC1_SIZE;
    }
    if (havePrivHex) {
        if (!xpack_hexToBytes(privHex, priv, sizeof(priv), &privLen) ||
            privLen != 32u) {
            return false;
        }
        return XSsl_p256DerivePublicKey(priv, privLen, outPub, cap, outLen);
    }
    /* 缺省：内嵌测试钥（仅内部试点；生产发布必须 --pubkey/--key 指定产品钥）。 */
    return xpack_hexToBytes(XUPGRADE_TEST_KEY_PUB_SEC1_HEX, outPub, cap, outLen) &&
           *outLen == XPACK_PUB_SEC1_SIZE;
}

/** @brief 对已解析 manifest 签名：canonical → SHA-256 → P-256 DER → base64。 */
static bool xpack_signManifest(const XUpgradeManifest* manifest, const uint8_t* privKey,
                               char* outSigB64, size_t sigCap)
{
    uint8_t canonical[XUPGRADE_MANIFEST_CANONICAL_MAX];
    uint32_t canonLen = 0;
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
    uint8_t der[XPACK_DER_SIG_MAX];
    size_t derLen = 0;
    if (!XUpgradeManifest_canonicalBytes(manifest, canonical, sizeof(canonical), &canonLen)) {
        XPrintf("[seal] canonical 序列化失败（manifest 过大？）\n");
        return false;
    }
    if (!XUpgradeVerify_sha256Digest(canonical, canonLen, digest)) return false;
    if (!XSsl_p256SignHashDer(privKey, 32u, digest, XUPGRADE_SHA256_BIN_SIZE,
                              der, sizeof(der), &derLen)) {
        XPrintf("[seal] 签名失败（私钥非法或 PSA 不可用）\n");
        return false;
    }
    if (xpack_b64EncodedLen(derLen) + 1u > sigCap) return false;
    xpack_b64Encode(der, derLen, outSigB64, sigCap);
    return true;
}

/* ========================================================================== */
/*                                  seal 子命令                                */
/* ========================================================================== */

static int xpack_cmdSeal(const char* draftPath, const char* outPath,
                         const char* privHex, bool haveKey,
                         const char searchDirs[][1600], uint32_t searchCount)
{
    uint8_t* draftData = NULL;
    size_t draftLen = 0;
    XUpgradeManifest manifest;
    uint8_t priv[32];
    size_t privLen = 0;
    uint8_t derivedPub[XPACK_PUB_SEC1_SIZE];
    size_t derivedLen = 0;
    uint8_t canonical[XUPGRADE_MANIFEST_CANONICAL_MAX];
    uint32_t canonLen = 0;
    char sigB64[256];
    char finalJson[XUPGRADE_MANIFEST_CANONICAL_MAX + 320];
    size_t finalLen = 0;
    char baseDir[1600];
    uint32_t i;
    size_t k;
    size_t insertPos = 0;
    size_t sigSegLen;

    if (!xpack_readWholeFile(draftPath, &draftData, &draftLen)) {
        XPrintf("[seal] 读取失败：%s\n", draftPath);
        return 2;
    }
    if (!XUpgradeManifest_parse(&manifest, (const char*)draftData, draftLen)) {
        XPrintf("[seal] draft manifest 解析失败（字段缺失/schema/越限）：%s\n", draftPath);
        XFree_System(draftData);
        return 2;
    }
    XFree_System(draftData);
    if (haveKey) {
        if (!xpack_hexToBytes(privHex, priv, sizeof(priv), &privLen) || privLen != 32u) {
            XPrintf("[seal] --key 不是 64 位十六进制私钥\n");
            return 2;
        }
    } else {
        if (!xpack_hexToBytes(XUPGRADE_TEST_KEY_PRIV_HEX, priv, sizeof(priv), &privLen)) {
            return 3;
        }
        XPrintf("[seal] 警告：未指定 --key，使用内嵌测试钥（禁止用于生产发布）\n");
    }
    if (!XSsl_p256DerivePublicKey(priv, privLen, derivedPub, sizeof(derivedPub), &derivedLen)) {
        XPrintf("[seal] 私钥派生公钥失败\n");
        return 2;
    }

    /* 基准目录 = draft 所在目录。 */
    {
        size_t n = strlen(draftPath);
        size_t cut = n;
        for (k = 0; k < n; ++k) {
            if (draftPath[k] == '/' || draftPath[k] == '\\') cut = k;
        }
        if (cut == n) {
            baseDir[0] = '.';
            baseDir[1] = '\0';
        } else {
            if (cut >= sizeof(baseDir)) return 2;
            memcpy(baseDir, draftPath, cut);
            baseDir[cut] = '\0';
        }
    }

    /* 逐组件定位文件并回填 size/sha256。 */
    for (i = 0; i < manifest.m_componentCount; ++i) {
        char path[1600];
        uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
        uint64_t size = 0;
        char hex[XUPGRADE_SHA256_BIN_SIZE * 2u + 1u];
        if (!xpack_locateComponent(manifest.m_components[i].m_file, baseDir,
                                   searchDirs, searchCount, path, sizeof(path))) {
            XPrintf("[seal] 找不到组件包：%s（搜索目录 %u 个）\n",
                    manifest.m_components[i].m_file, searchCount + 1u);
            return 2;
        }
        if (!xpack_fileSha256(path, digest, &size)) {
            XPrintf("[seal] 读取失败：%s\n", path);
            return 2;
        }
        manifest.m_components[i].m_size = size;
        xpack_bytesToHex(digest, XUPGRADE_SHA256_BIN_SIZE, hex, sizeof(hex));
        memcpy(manifest.m_components[i].m_sha256, hex, XUPGRADE_SHA256_HEX_MAX);
        XPrintf("[seal] %-12s %-10s %s  size=%llu sha256=%s\n",
                manifest.m_components[i].m_id, manifest.m_components[i].m_os,
                manifest.m_components[i].m_file, (unsigned long long)size, hex);
    }

    if (!xpack_signManifest(&manifest, priv, sigB64, sizeof(sigB64))) return 2;

    /* 组装最终 manifest = canonical 文本 + 在顶层 version 键前插入 sig。
     * canonical 顶层键序 build<channel<components<…<schema<sig<version，
     * 嵌套组件的 version 全部位于 components 块内（更早），故最后一处
     * "version": 即顶层键。 */
    if (!XUpgradeManifest_canonicalBytes(&manifest, canonical, sizeof(canonical), &canonLen)) {
        XPrintf("[seal] canonical 序列化失败\n");
        return 2;
    }
    {
        const char needle[] = "\"version\":";
        const size_t needleLen = sizeof(needle) - 1u;
        sigSegLen = 0;
        for (k = canonLen; k >= needleLen; --k) {
            if (memcmp(canonical + k - needleLen, needle, needleLen) == 0) {
                insertPos = k - needleLen;
                break;
            }
        }
        if (insertPos == 0 && !(canonLen >= needleLen &&
                                memcmp(canonical + canonLen - needleLen, needle, needleLen) == 0)) {
            XPrintf("[seal] canonical 中未找到顶层 version 键（异常）\n");
            return 3;
        }
        sigSegLen = 7u + strlen(sigB64) + 2u; /* "sig":"…" + 逗号 */
        if (canonLen + sigSegLen + 1u > sizeof(finalJson)) {
            XPrintf("[seal] manifest 过大，超出缓冲\n");
            return 2;
        }
        memcpy(finalJson, canonical, insertPos);
        {
            char seg[300];
            size_t n = 0;
            seg[n++] = '"';
            seg[n++] = 's';
            seg[n++] = 'i';
            seg[n++] = 'g';
            seg[n++] = '"';
            seg[n++] = ':';
            seg[n++] = '"';
            memcpy(seg + n, sigB64, strlen(sigB64));
            n += strlen(sigB64);
            seg[n++] = '"';
            seg[n++] = ',';
            seg[n] = '\0';
            memcpy(finalJson + insertPos, seg, n);
            finalLen = insertPos + n;
        }
        memcpy(finalJson + finalLen, canonical + insertPos, canonLen - insertPos);
        finalLen += canonLen - insertPos;
        finalJson[finalLen] = '\0';
    }

    /* 自验闭环：final 可解析、重解析 canonical 与被签内容逐字节一致、插入的
     * sig 经派生公钥验签通过——设备侧同源三关。（不得用"重签比对"：ECDSA
     * 带随机 k，两次签名必不相同。） */
    {
        XUpgradeManifest check;
        uint8_t checkCanon[XUPGRADE_MANIFEST_CANONICAL_MAX];
        uint32_t checkLen = 0;
        uint8_t sigBin[128];
        size_t sigBinLen = 0;
        uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
        if (!XUpgradeManifest_parse(&check, finalJson, finalLen)) {
            XPrintf("[seal] 自验失败：最终 manifest 解析不过\n");
            return 2;
        }
        if (!XUpgradeManifest_canonicalBytes(&check, checkCanon, sizeof(checkCanon), &checkLen) ||
            checkLen != canonLen || memcmp(checkCanon, canonical, canonLen) != 0) {
            XPrintf("[seal] 自验失败：重解析 canonical 不一致\n");
            return 2;
        }
        if (!xpack_b64Decode(sigB64, sigBin, sizeof(sigBin), &sigBinLen) ||
            !XUpgradeVerify_sha256Digest(checkCanon, checkLen, digest) ||
            !XSsl_p256VerifySha256Der(derivedPub, derivedLen, digest,
                                      XUPGRADE_SHA256_BIN_SIZE, sigBin, sigBinLen)) {
            XPrintf("[seal] 自验失败：插入 sig 验签不过\n");
            return 2;
        }
    }
    if (!xpack_writeWholeFile(outPath, (const uint8_t*)finalJson, finalLen)) {
        XPrintf("[seal] 写出失败：%s\n", outPath);
        return 2;
    }
    {
        char pubHex[131];
        xpack_bytesToHex(derivedPub, derivedLen, pubHex, sizeof(pubHex));
        XPrintf("[seal] 完成：%s（%u 组件；验签公钥 SEC1=%s）\n",
                outPath, (unsigned)manifest.m_componentCount, pubHex);
    }
    return 0;
}

/* ========================================================================== */
/*                               verify-pak 子命令                             */
/* ========================================================================== */

/* zlib uncompress（zlib 已随目标链接；直接声明避开 zlib.h 头路径依赖，
 * 参数表与 zlib 的 uLongf/uLong = unsigned long 口径一致）。 */
extern int uncompress(unsigned char* dest, unsigned long* destLen,
                      const unsigned char* source, unsigned long sourceLen);
#define XPACK_Z_OK 0

/** @brief 对 .pak 做设备侧同源逐步校验（与引擎 staging 校验同构），逐步打印。
 *  @return 0=全过；1=校验失败（打印首个失败步）；2=参数/读文件失败。 */
static int xpack_cmdVerifyPak(const char* pakPath)
{
    extern size_t XUpgradePackage_entryWireSize(const XUpgradePackageEntry* entry);
    uint8_t* data = NULL;
    size_t len = 0;
    XUpgradePackageHeader header;
    XUpgradePackageEntry entry;
    size_t pos = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
    size_t payloadOff;
    uint32_t i;
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
    uint8_t* payload = NULL;

    if (!xpack_readWholeFile(pakPath, &data, &len)) {
        XPrintf("[verify-pak] 读取失败：%s\n", pakPath);
        return 2;
    }
    XPrintf("[verify-pak] 文件=%s 大小=%u\n", pakPath, (unsigned)len);
    if (!XUpgradePackage_parseHeader(&header, data, len)) {
        XPrintf("[verify-pak] FAIL 步1 parseHeader\n");
        XFree_System(data);
        return 1;
    }
    XPrintf("[verify-pak] 步1 parseHeader OK（entryCount=%u headerSize=%u payloadSize=%u）\n",
            (unsigned)header.m_entryCount, (unsigned)header.m_headerSize,
            (unsigned)header.m_payloadSize);
    if ((uint64_t)header.m_headerSize + header.m_payloadSize != (uint64_t)len) {
        XPrintf("[verify-pak] FAIL 步2 headerSize+payloadSize != 文件总长\n");
        XFree_System(data);
        return 1;
    }
    XPrintf("[verify-pak] 步2 总长自洽 OK\n");
    /* 条目表逐条 parseEntry（白名单/flags/长度关系由解析端把关）。 */
    for (i = 0; i < header.m_entryCount; ++i) {
        /* consumed 是"本次消耗"（赋值语义），显式累加。 */
        {
            size_t consumed = 0;
            if (!XUpgradePackage_parseEntry(&entry, data + pos, len - pos, &consumed)) {
                XPrintf("[verify-pak] FAIL 步3 parseEntry 第 %u 条（起始 %u）\n",
                        (unsigned)(i + 1), (unsigned)pos);
                XFree_System(data);
                return 1;
            }
            pos += consumed;
        }
        XPrintf("[verify-pak] 步3 条目%u OK（consumed 后 pos=%u）：%s flags=%u raw=%u comp=%u off=%u\n",
                (unsigned)(i + 1), (unsigned)pos, entry.m_name, (unsigned)entry.m_flags,
                (unsigned)entry.m_rawSize, (unsigned)entry.m_compSize,
                (unsigned)entry.m_offset);
    }
    if (pos != header.m_headerSize) {
        XPrintf("[verify-pak] FAIL 步4 条目表末尾 %u != headerSize %u\n",
                (unsigned)pos, (unsigned)header.m_headerSize);
        XFree_System(data);
        return 1;
    }
    XPrintf("[verify-pak] 步4 表尺寸自洽 OK\n");
    payloadOff = XUpgradePackage_payloadOffset(&header);
    payload = (uint8_t*)XMalloc_System((size_t)header.m_payloadSize + 1u);
    if (!payload) { XFree_System(data); return 2; }
    memcpy(payload, data + payloadOff, (size_t)header.m_payloadSize);
    if (!XUpgradeVerify_sha256Digest(payload, (size_t)header.m_payloadSize, digest) ||
        XMemcmp(digest, header.m_payloadSha256, XUPGRADE_SHA256_BIN_SIZE) != 0) {
        XPrintf("[verify-pak] FAIL 步5 数据区整段哈希不符\n");
        XFree_System(payload);
        XFree_System(data);
        return 1;
    }
    XPrintf("[verify-pak] 步5 数据区整段哈希 OK\n");
    /* 逐条目解压+内容哈希（与引擎第二遍同构）。 */
    for (i = 0; i < header.m_entryCount; ++i) {
        uint8_t* comp = NULL;
        uint8_t* raw = NULL;
        /* 顺序重扫到第 i 条（consumed 赋值语义，显式累加）。 */
        {
            uint32_t k;
            size_t p2 = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
            for (k = 0; k <= i; ++k) {
                size_t consumed = 0;
                if (!XUpgradePackage_parseEntry(&entry, data + p2,
                                                header.m_headerSize - p2, &consumed)) {
                    XPrintf("[verify-pak] FAIL 步6 条目%u 第%u条重扫解析\n",
                            (unsigned)(i + 1), (unsigned)(k + 1));
                    XFree_System(payload);
                    XFree_System(data);
                    return 1;
                }
                if (k < i) p2 += consumed;
            }
        }
        comp = payload + entry.m_offset;
        if (entry.m_flags & XUPGRADE_PACKAGE_FLAG_ZLIB) {
            unsigned long destLen = (unsigned long)entry.m_rawSize;
            raw = (uint8_t*)XMalloc_System((size_t)entry.m_rawSize + 1u);
            if (!raw || uncompress(raw, &destLen, comp,
                                              (unsigned long)entry.m_compSize) != XPACK_Z_OK ||
                destLen != (unsigned long)entry.m_rawSize) {
                XPrintf("[verify-pak] FAIL 步6 条目%u %s zlib 解压失败\n",
                        (unsigned)(i + 1), entry.m_name);
                XFree_System(raw);
                XFree_System(payload);
                XFree_System(data);
                return 1;
            }
        } else {
            raw = comp;                       /* stored：直接引用数据区 */
        }
        if (!XUpgradeVerify_sha256Digest(raw, (size_t)entry.m_rawSize, digest) ||
            XMemcmp(digest, entry.m_sha256, XUPGRADE_SHA256_BIN_SIZE) != 0) {
            XPrintf("[verify-pak] FAIL 步7 条目%u %s 内容哈希不符\n",
                    (unsigned)(i + 1), entry.m_name);
            if (raw != comp) XFree_System(raw);
            XFree_System(payload);
            XFree_System(data);
            return 1;
        }
        XPrintf("[verify-pak] 步6/7 条目%u %s 解压+哈希 OK\n",
                (unsigned)(i + 1), entry.m_name);
        if (raw != comp) XFree_System(raw);
    }
    XFree_System(payload);
    XFree_System(data);
    XPrintf("[verify-pak] PASS：%s 全部 %u 条目校验通过\n", pakPath,
            (unsigned)header.m_entryCount);
    return 0;
}

/* ========================================================================== */
/*                                 verify 子命令                               */
/* ========================================================================== */

static int xpack_cmdVerify(const char* manifestPath, const char* pubHex, bool havePub,
                           const char* privHex, bool haveKey,
                           const char searchDirs[][1600], uint32_t searchCount,
                           bool checkFiles)
{
    uint8_t* data = NULL;
    size_t len = 0;
    XUpgradeManifest manifest;
    uint8_t trustKey[96];
    size_t trustLen = 0;
    uint8_t canonical[XUPGRADE_MANIFEST_CANONICAL_MAX];
    uint32_t canonLen = 0;
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
    uint8_t sig[XUPGRADE_SIGNATURE_MAX];
    size_t sigLen = 0;
    char baseDir[1600];
    uint32_t i;
    size_t k;
    int failures = 0;

    if (!xpack_readWholeFile(manifestPath, &data, &len)) {
        XPrintf("[verify] 读取失败：%s\n", manifestPath);
        return 2;
    }
    if (!XUpgradeManifest_parse(&manifest, (const char*)data, len)) {
        XPrintf("[verify] manifest 解析失败：%s\n", manifestPath);
        XFree_System(data);
        return 2;
    }
    XFree_System(data);
    if (!xpack_resolveTrustKey(havePub, pubHex, haveKey, privHex,
                               trustKey, sizeof(trustKey), &trustLen)) {
        XPrintf("[verify] 信任钥不可用（--pubkey/--key 非法？）\n");
        return 2;
    }
    if (!XUpgradeManifest_canonicalBytes(&manifest, canonical, sizeof(canonical), &canonLen)) {
        XPrintf("[verify] canonical 序列化失败\n");
        return 2;
    }
    if (!XUpgradeVerify_sha256Digest(canonical, canonLen, digest)) return 3;
    if (!xpack_b64Decode(manifest.m_sig, sig, sizeof(sig), &sigLen)) {
        XPrintf("[verify] FAIL：sig 不是合法 base64\n");
        return 1;
    }
    if (!XSsl_p256VerifySha256Der(trustKey, trustLen, digest,
                                  XUPGRADE_SHA256_BIN_SIZE, sig, sigLen)) {
        XPrintf("[verify] FAIL：签名验签不过（manifest 被篡改或信任钥不符）\n");
        return 1;
    }
    XPrintf("[verify] 签名验签 PASS（channel=%s version=%s 组件 %u 个）\n",
            manifest.m_channel, manifest.m_version, (unsigned)manifest.m_componentCount);

    /* 基准目录 = manifest 所在目录。 */
    {
        size_t n = strlen(manifestPath);
        size_t cut = n;
        for (k = 0; k < n; ++k) {
            if (manifestPath[k] == '/' || manifestPath[k] == '\\') cut = k;
        }
        if (cut == n) {
            baseDir[0] = '.';
            baseDir[1] = '\0';
        } else {
            if (cut >= sizeof(baseDir)) return 2;
            memcpy(baseDir, manifestPath, cut);
            baseDir[cut] = '\0';
        }
    }
    if (!checkFiles) return 0;
    for (i = 0; i < manifest.m_componentCount; ++i) {
        char path[1600];
        uint8_t digest2[XUPGRADE_SHA256_BIN_SIZE];
        char hex[XUPGRADE_SHA256_BIN_SIZE * 2u + 1u];
        uint64_t size = 0;
        if (!xpack_locateComponent(manifest.m_components[i].m_file, baseDir,
                                   searchDirs, searchCount, path, sizeof(path))) {
            XPrintf("[verify] 组件 %u：%s 文件未找到（--no-files 可跳过文件核对）\n",
                    (unsigned)(i + 1), manifest.m_components[i].m_file);
            ++failures;
            continue;
        }
        if (!xpack_fileSha256(path, digest2, &size)) {
            XPrintf("[verify] 组件 %u：读取失败 %s\n", (unsigned)(i + 1), path);
            ++failures;
            continue;
        }
        xpack_bytesToHex(digest2, XUPGRADE_SHA256_BIN_SIZE, hex, sizeof(hex));
        if (size != manifest.m_components[i].m_size ||
            !xpack_strEq(hex, manifest.m_components[i].m_sha256)) {
            XPrintf("[verify] 组件 %u：%s size/sha256 与 manifest 不符（实际 %llu/%s）\n",
                    (unsigned)(i + 1), path, (unsigned long long)size, hex);
            ++failures;
        } else {
            XPrintf("[verify] 组件 %u：PASS %s\n", (unsigned)(i + 1),
                    manifest.m_components[i].m_file);
        }
    }
    return (failures == 0) ? 0 : 1;
}

/* ========================================================================== */
/*                                 keygen 子命令                               */
/* ========================================================================== */

static int xpack_cmdKeygen(const char* privOut, const char* pubOut)
{
    uint8_t priv[32];
    size_t privLen = 0;
    uint8_t pub[96];
    size_t pubLen = 0;
    char privHex[65];
    char pubHex[131];
    char line[256];
    size_t n;
    if (!XSsl_p256GenerateKeyPair(priv, sizeof(priv), &privLen, pub, sizeof(pub), &pubLen)) {
        XPrintf("[keygen] 密钥生成失败（PSA/随机数不可用？）\n");
        return 2;
    }
    xpack_bytesToHex(priv, privLen, privHex, sizeof(privHex));
    xpack_bytesToHex(pub, pubLen, pubHex, sizeof(pubHex));
    n = 0;
    memcpy(line, privHex, 64u);
    n = 64u;
    line[n++] = '\n';
    line[n] = '\0';
    if (!xpack_writeWholeFile(privOut, (const uint8_t*)line, n)) {
        XPrintf("[keygen] 私钥写出失败：%s\n", privOut);
        return 2;
    }
    n = 0;
    memcpy(line, pubHex, strlen(pubHex));
    n = strlen(pubHex);
    line[n++] = '\n';
    line[n] = '\0';
    if (!xpack_writeWholeFile(pubOut, (const uint8_t*)line, n)) {
        XPrintf("[keygen] 公钥写出失败：%s\n", pubOut);
        return 2;
    }
    XPrintf("[keygen] 私钥=%s（已写 %s，妥善保管，严禁入库/下发）\n", privHex, privOut);
    XPrintf("[keygen] 公钥=%s（已写 %s；换钥=替换 XUpgrade_trust_pubkey.h 重编译或 XUpgrade_setTrustKey 注入）\n",
            pubHex, pubOut);
    return 0;
}

/* ========================================================================== */
/*                                   入口                                      */
/* ========================================================================== */

static void xpack_usage(void)
{
    XPrintf(
        "XUpgradePack — XUpgrade 服务端打包签名工具\n"
        "用法：\n"
        "  XUpgradePack pack   <源目录> <输出.pak> [--store]\n"
        "  XUpgradePack seal   <draft.json> <out.json> [--key <64hex>] [--search-dir <dir>]...\n"
        "  XUpgradePack verify <manifest.json> [--pubkey <130hex>] [--key <64hex>]\n"
        "                      [--search-dir <dir>]... [--no-files]\n"
        "  XUpgradePack verify-pak <包.pak>（设备侧同源逐步校验）\n"
        "  XUpgradePack keygen <私钥输出文件> <公钥输出文件>\n");
}

int main(int argc, char** argv)
{
    char argBuf[8][1600];
    char keyHex[128];
    char pubHex[264];
    static char searchDirs[XPACK_SEARCH_MAX][1600];
    uint32_t searchCount = 0;
    const char* posArgs[4];
    uint32_t posCount = 0;
    bool store = false;
    bool noFiles = false;
    bool haveKey = false;
    bool havePub = false;
    int i;

    keyHex[0] = '\0';
    pubHex[0] = '\0';
    if (argc < 2) {
        xpack_usage();
        return 2;
    }
    for (i = 2; i < argc; ++i) {
        if (xpack_strEq(argv[i], "--store")) {
            store = true;
        } else if (xpack_strEq(argv[i], "--no-files")) {
            noFiles = true;
        } else if (xpack_strEq(argv[i], "--key") && i + 1 < argc) {
            if (!xpack_argToUtf8(argv[++i], keyHex, sizeof(keyHex))) return 2;
            haveKey = true;
        } else if (xpack_strEq(argv[i], "--pubkey") && i + 1 < argc) {
            if (!xpack_argToUtf8(argv[++i], pubHex, sizeof(pubHex))) return 2;
            havePub = true;
        } else if (xpack_strEq(argv[i], "--search-dir") && i + 1 < argc) {
            if (searchCount >= XPACK_SEARCH_MAX) {
                XPrintf("[error] --search-dir 最多 %u 个\n", (unsigned)XPACK_SEARCH_MAX);
                return 2;
            }
            if (!xpack_argToUtf8(argv[++i], searchDirs[searchCount], 1600u)) return 2;
            ++searchCount;
        } else if (argv[i][0] == '-') {
            XPrintf("[error] 未知选项：%s\n", argv[i]);
            return 2;
        } else {
            if (posCount >= 4u) {
                XPrintf("[error] 位置参数过多\n");
                return 2;
            }
            if (!xpack_argToUtf8(argv[i], argBuf[posCount], 1600u)) return 2;
            posArgs[posCount] = argBuf[posCount];
            ++posCount;
        }
    }
    if (xpack_strEq(argv[1], "pack")) {
        if (posCount != 2u) { xpack_usage(); return 2; }
        return xpack_cmdPack(posArgs[0], posArgs[1], store);
    }
    if (xpack_strEq(argv[1], "seal")) {
        if (posCount != 2u) { xpack_usage(); return 2; }
        return xpack_cmdSeal(posArgs[0], posArgs[1], keyHex, haveKey,
                             searchDirs, searchCount);
    }
    if (xpack_strEq(argv[1], "verify")) {
        if (posCount != 1u) { xpack_usage(); return 2; }
        return xpack_cmdVerify(posArgs[0], pubHex, havePub, keyHex, haveKey,
                               searchDirs, searchCount, !noFiles);
    }
    if (xpack_strEq(argv[1], "verify-pak")) {
        if (posCount != 1u) { xpack_usage(); return 2; }
        return xpack_cmdVerifyPak(posArgs[0]);
    }
    if (xpack_strEq(argv[1], "keygen")) {
        if (posCount != 2u) { xpack_usage(); return 2; }
        return xpack_cmdKeygen(posArgs[0], posArgs[1]);
    }
    xpack_usage();
    return 2;
}

#else /* !XUPGRADE_ON */

int main(void)
{
    XPrintf("XUpgradePack：XUPGRADE_ON=0，模块被裁剪，无可执行功能\n");
    return 3;
}

#endif /* XUPGRADE_ON */
