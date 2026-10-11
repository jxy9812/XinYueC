/*******************************************************************************
 * @file       xupgrade_shared.h
 * @brief      XUpgrade 模块内部共享小工具（跨 TU 收敛的静态内联实现）。
 * @details    2026-10-08 评审收敛：str_eq/cstrLen/trim/version_valid/
 *             derive_root/setErr/validName/buildPath/extractVersion/sha256
 *             单段封装此前在 engine/gray/stub/manifest/verify/package/
 *             storedir/ab 各 TU 逐字重复约 200+ 行，本头以 static inline
 *             单一定义收口。实现与被收敛的各副本逐字等价（语义无变化）。
 *             仅模块内部消费，不放公共契约头；mem* 原语白名单口径不变。
 * @author     XinYueC 团队
 *******************************************************************************/
#ifndef XUPGRADE_SHARED_H
#define XUPGRADE_SHARED_H

#include "XUpgrade.h"         /* XUPGRADE_VERSION_MAX/XUPGRADE_DETAIL_MAX */
#include "XUpgradePackage.h"  /* XUPGRADE_SHA256_BIN_SIZE */
#include "XMemory.h"          /* XMemcpy */
#include "XString.h"
#include "XByteArrayView.h"   /* sha256: 视图读取 */
#include "XCryptographicHash.h"
#include "XSystem.h"          /* deriveRoot: XSystem_executableFilePath */
#include <string.h>           /* 仅 memcpy（mem* 白名单） */

/** @brief 比较 NUL 结尾串是否相等（string.h 比较函数被禁，自实现）。 */
static inline int xups_str_eq(const char* a, const char* b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/** @brief 复制 NUL 结尾串到定长缓冲，超长截断并保证 NUL。
 *  @return 放得下返回 true；发生截断返回 false（内容仍有效）。 */
static inline int xups_str_copy(char* dst, size_t cap, const char* src)
{
    size_t i;
    if (!dst || cap == 0) return 1;
    if (!src) { dst[0] = '\0'; return 1; }
    for (i = 0; i + 1 < cap && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
    return src[i] == '\0';
}

/** @brief 取 NUL 结尾串长度（strlen 替代）；NULL 返回 0。 */
static inline size_t xups_cstr_len(const char* s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n] != '\0') ++n;
    return n;
}

/** @brief 去除缓冲首尾空白（含 \r\n），原地收拢。 */
static inline void xups_trim(char* buf)
{
    size_t begin = 0;
    size_t end;
    if (!buf) return;
    while (buf[begin] == ' ' || buf[begin] == '\t' ||
           buf[begin] == '\r' || buf[begin] == '\n') ++begin;
    end = begin;
    while (buf[end] != '\0') ++end;
    while (end > begin && (buf[end - 1] == ' ' || buf[end - 1] == '\t' ||
                           buf[end - 1] == '\r' || buf[end - 1] == '\n')) --end;
    if (begin > 0) {
        size_t i;
        for (i = 0; i < end - begin; ++i) buf[i] = buf[begin + i];
        buf[end - begin] = '\0';
    } else {
        buf[end] = '\0';
    }
}

/** @brief 版本串白名单校验：[0-9A-Za-z._-]，非空。 */
static inline int xups_version_valid(const char* v)
{
    size_t i;
    if (!v || v[0] == '\0') return 0;
    for (i = 0; v[i] != '\0'; ++i) {
        char c = v[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

/** @brief 由可执行文件路径推导安装根（= exe 所在目录，§2；与
 *         XUpgradeIdentity rootDir 同口径）。 */
static inline int xups_derive_root(char* root, size_t cap)
{
    char exe[1024];
    size_t i;
    size_t lastSlash = 0;
    int hasSlash = 0;
    if (!root || cap == 0) return 0;
    root[0] = '\0';
    if (!XSystem_executableFilePath(exe, sizeof(exe))) return 0;
    for (i = 0; exe[i] != '\0'; ++i) {
        if (exe[i] == '/' || exe[i] == '\\') {
            lastSlash = i;
            hasSlash = 1;
        }
    }
    if (!hasSlash || lastSlash == 0) return 0;
    exe[lastSlash] = '\0';
    return xups_str_copy(root, cap, exe);
}

/** @brief 契约错误说明输出槽：失败路径写静态借用文本（成功时调用方传 NULL）。 */
static inline void xups_set_err(const char** errText, const char* text)
{
    if (errText) *errText = text;
}

/** @brief 校验组件 id/版本串是否落在路径安全白名单 [0-9A-Za-z._-] 内
 *         （非空、非 '.' 开头、ASCII、不超 maxBytes；路径拼装前强制关卡）。 */
static inline int xups_valid_name(const char* name, size_t maxBytes)
{
    size_t i;
    if (!name || !name[0]) return 0;
    if (name[0] == '.') return 0;
    for (i = 0; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (i + 1 >= maxBytes) return 0;
        if (c >= 0x80) return 0;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || c == '.' || c == '_' || c == '-') {
            continue;
        }
        return 0;
    }
    return 1;
}

/** @brief 以 base 为前缀拼路径（'/' 分隔，跳过 NULL 段，至多 4 段）。
 *  @return 新路径字符串（堆上 XString，调用方 XClassDelete 释放）；失败 NULL。 */
static inline XString* xups_build_path(const XString* base, const char* part1,
                                       const char* part2, const char* part3,
                                       const char* part4)
{
    XString* path = XString_create_utf8(base ? XString_toUtf8(base) : "");
    if (!path) return NULL;
    if ((part1 && (!XString_append_utf8(path, "/") || !XString_append_utf8(path, part1)))
        || (part2 && (!XString_append_utf8(path, "/") || !XString_append_utf8(path, part2)))
        || (part3 && (!XString_append_utf8(path, "/") || !XString_append_utf8(path, part3)))
        || (part4 && (!XString_append_utf8(path, "/") || !XString_append_utf8(path, part4)))) {
        XClassDelete(path);
        return NULL;
    }
    return path;
}

/** @brief 从指针/标记文件内容提取合法版本串（去首尾空白+白名单复核）。 */
static inline int xups_extract_version(const unsigned char* data, size_t size,
                                       char* out, unsigned int cap)
{
    size_t begin = 0;
    size_t end = size;
    size_t length;
    size_t i;
    if (!data || !out || cap == 0) return 0;
    while (begin < end && (data[begin] == ' ' || data[begin] == '\t'
                           || data[begin] == '\r' || data[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (data[end - 1] == ' ' || data[end - 1] == '\t'
                           || data[end - 1] == '\r' || data[end - 1] == '\n')) {
        --end;
    }
    length = end - begin;
    if (length == 0 || length + 1 > (size_t)cap || length + 1 > XUPGRADE_VERSION_MAX) return 0;
    for (i = begin; i < end; ++i) {
        unsigned char c = data[i];
        if (c >= 0x80) return 0;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
              || c == '.' || c == '_' || c == '-')) {
            return 0;
        }
    }
    memcpy(out, &data[begin], length);
    out[length] = '\0';
    return 1;
}

/** @brief 单段内存 SHA-256（out 容量 XUPGRADE_SHA256_BIN_SIZE）。 */
static inline int xups_sha256_bytes(const unsigned char* data, size_t len,
                                    unsigned char out[XUPGRADE_SHA256_BIN_SIZE])
{
    XCryptographicHash hash;
    XByteArrayView view;
    if (!out || (!data && len > 0)) return 0;
    XCryptographicHash_init(&hash, XCryptographicHash_Sha256);
    view = XByteArrayView_create_data(data, (int64_t)len);
    XCryptographicHash_addData_2(&hash, view);
    view = XCryptographicHash_resultView(&hash);
    if (XByteArrayView_size(&view) != XUPGRADE_SHA256_BIN_SIZE) {
        XCryptographicHash_deinit(&hash);
        return 0;
    }
    memcpy(out, XByteArrayView_data(&view), XUPGRADE_SHA256_BIN_SIZE);
    XCryptographicHash_deinit(&hash);
    return 1;
}

#endif /* XUPGRADE_SHARED_H */
