/**
 * @file       XUpgradePackage.c
 * @brief      .pak 升级包容器的解析、双哈希校验与打包原语实现（设计文档 §5）。
 * @details    磁盘布局与白名单规则以 XUpgradePackage.h 契约为准：多字节整数
 *             一律小端编码。读侧按"头→条目表→数据区"顺序一次性流式解析，
 *             双哈希校验（条目解压后 SHA-256 + 数据区整段 payloadSha256），
 *             摘要经 XUpgradeVerify.c 的 SHA-256 封装（库内
 *             XCryptographicHash 门面）计算；写侧只
 *             提供序列化原语（entryWireSize/writeHeader/writeEntry）与 zlib
 *             压缩原语（compress/decompress），供测试与服务端打包工具组装
 *             .pak，容器级打包编排不入库。条目数据为 zlib 流（RFC1950 封装，
 *             内层 RFC1951 deflate），对应条目标志 XUPGRADE_PACKAGE_FLAG_ZLIB。
 *             本文件不做文件 I/O：包字节由调用方（引擎经 XFile、测试经内存
 *             缓冲）提供，落盘与目录编排属 XUpgradeEngine/XUpgradeStore。
 */

#if XUPGRADE_ON

#include "XUpgradePackage.h"

#include "xupgrade_shared.h" /* 模块内部共享小工具（xups_*） */
#include "XUpgrade.h"      /* XUpgradeError：容器校验结果统一错误语义 */
#include "XMemory.h"       /* XMalloc_System/XFree_System：统一分配器 */
#include <string.h>        /* memcpy/memset/memcmp：白名单裸内存原语 */
#include "zlib.h"          /* compress2/uncompress：条目级 zlib 压缩（先例 Src/XContainer/XByteArray/XByteArray.c:301） */

/**
 * @brief 单个条目在磁盘上的定长开销（本文件私有常量）：
 *        nameLen(2)+flags(1)+rawSize(8)+compSize(8)+offset(8)+sha256(32)，
 *        另加变长 name。
 */
static const size_t xupgrade_entryFixedSize = 59u;

/* cstrLen 已收敛至 xupgrade_shared.h（xups_cstr_len）。 */

/* ========================================================================== */
/*                          小端编解码辅助（磁盘整数一律小端）                  */
/* ========================================================================== */

/** @brief 读取小端 u16。 */
static uint16_t xupgrade_le16(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/** @brief 读取小端 u32。 */
static uint32_t xupgrade_le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief 读取小端 u64。 */
static uint64_t xupgrade_le64(const uint8_t* p)
{
    return (uint64_t)xupgrade_le32(p) | ((uint64_t)xupgrade_le32(p + 4) << 32);
}

/** @brief 写出小端 u16。 */
static void xupgrade_put16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

/** @brief 写出小端 u32。 */
static void xupgrade_put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/** @brief 写出小端 u64。 */
static void xupgrade_put64(uint8_t* p, uint64_t v)
{
    xupgrade_put32(p, (uint32_t)(v & 0xFFFFFFFFu));
    xupgrade_put32(p + 4, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

/* ========================================================================== */
/*                    契约函数（完整契约文档见 XUpgradePackage.h）               */
/* ========================================================================== */

/**
 * @brief 校验条目相对路径是否落在白名单内（契约见 XUpgradePackage.h:129）。
 * @details 白名单字符集 [0-9A-Za-z._-/]；在此之外拒绝反斜杠、盘符冒号、
 *          非 ASCII 字节与控制字符。空段（开头/结尾/连续 '/'）、"." 与
 *          ".." 段一律拒绝，保证解包目标不可能逃出版本目录。
 */
bool XUpgradePackage_checkEntryName(const char* name)
{
    size_t segStart = 0;
    size_t i = 0;

    if (!name || name[0] == '\0') {
        return false;                       /* 空串与 NULL 拒绝 */
    }
    if (name[0] == '/' || name[0] == '\\') {
        return false;                       /* 绝对路径拒绝 */
    }
    for (;;) {
        char c = name[i];
        if (c == '\0' || c == '/') {
            size_t segLen = i - segStart;   /* 段闭合：检查空段与点段 */
            if (segLen == 0) {
                return false;               /* 连续 '/'、结尾 '/' 产生的空段 */
            }
            if (segLen == 1 && name[segStart] == '.') {
                return false;               /* "." 段 */
            }
            if (segLen == 2 && name[segStart] == '.' && name[segStart + 1] == '.') {
                return false;               /* ".." 段 */
            }
            if (c == '\0') {
                break;
            }
            segStart = i + 1;
        } else if (c == '\\' || c == ':') {
            return false;                   /* 反斜杠与盘符冒号一律拒绝 */
        } else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                     (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-')) {
            return false;                   /* 白名单外字符（含非 ASCII 字节） */
        }
        ++i;
    }
    return true;
}

/**
 * @brief 解析 .pak 定长头（契约见 XUpgradePackage.h:103）。
 * @details 解析全程使用局部变量，全部检查通过后才一次性提交到 out，保证
 *          "失败时 out 保持原值不变"。entryCount 按结构体文档域 [1,65535]
 *          校验（磁盘字段虽为 u32，超出该域按非法容器拒绝）。
 */
bool XUpgradePackage_parseHeader(XUpgradePackageHeader* out, const uint8_t* bytes, size_t size)
{
    XUpgradePackageHeader header;

    if (!out || !bytes || size < XUPGRADE_PACKAGE_FIXED_HEADER_SIZE) {
        return false;                       /* 长度不足 */
    }
    if (memcmp(bytes, XUPGRADE_PACKAGE_MAGIC, 4) != 0) {
        return false;                       /* 魔数不为 "XUPK" */
    }
    memset(&header, 0, sizeof(header));
    header.m_format = xupgrade_le32(bytes + 4);
    if (header.m_format != XUPGRADE_PACKAGE_FORMAT) {
        return false;                       /* 只接受格式版本 1 */
    }
    header.m_entryCount = xupgrade_le32(bytes + 8);
    if (header.m_entryCount < 1u || header.m_entryCount > 65535u) {
        return false;                       /* 条目数越出契约域 */
    }
    header.m_headerSize = xupgrade_le32(bytes + 12);
    if ((size_t)header.m_headerSize < XUPGRADE_PACKAGE_FIXED_HEADER_SIZE) {
        return false;                       /* 数据区起点不得小于定长头 */
    }
    header.m_payloadSize = xupgrade_le64(bytes + 16);
    memcpy(header.m_payloadSha256, bytes + 24, XUPGRADE_SHA256_BIN_SIZE);
    *out = header;
    return true;
}

/**
 * @brief 从字节流顺序解析一个条目表条目（契约见 XUpgradePackage.h:119）。
 * @details 消耗字节数恒为 nameLen+59；nameLen 上限
 *          XUPGRADE_PACKAGE_NAME_MAX-1，name 复制进 out 后保证 NUL 结尾。
 *          flags 含未知置位、未压缩条目 compSize 与 rawSize 不相等、路径
 *          通不过白名单校验均拒绝。m_offset 的数据区越界检查需要包头
 *          payloadSize 上下文，由 XUpgradePackage_verify 或引擎侧执行。
 */
bool XUpgradePackage_parseEntry(XUpgradePackageEntry* out, const uint8_t* bytes, size_t size, size_t* consumed)
{
    XUpgradePackageEntry entry;
    size_t nameLen;

    if (!out || !bytes || !consumed || size < 2u) {
        return false;                       /* nameLen 都放不下 */
    }
    nameLen = (size_t)xupgrade_le16(bytes);
    if (nameLen > (size_t)(XUPGRADE_PACKAGE_NAME_MAX - 1u)) {
        return false;                       /* nameLen 超过容量上限 */
    }
    if (nameLen + xupgrade_entryFixedSize > size) {
        return false;                       /* 条目定长部分越界 */
    }
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.m_name, bytes + 2, nameLen);   /* memset 已清零，NUL 结尾有保证 */
    if (!XUpgradePackage_checkEntryName(entry.m_name)) {
        return false;                       /* 路径逃逸/非法字符 */
    }
    entry.m_flags = bytes[2u + nameLen];
    if ((entry.m_flags & (uint8_t)~XUPGRADE_PACKAGE_FLAG_ZLIB) != 0u) {
        return false;                       /* 保留位必须为 0 */
    }
    entry.m_rawSize = xupgrade_le64(bytes + 3u + nameLen);
    entry.m_compSize = xupgrade_le64(bytes + 11u + nameLen);
    entry.m_offset = xupgrade_le64(bytes + 19u + nameLen);
    memcpy(entry.m_sha256, bytes + 27u + nameLen, XUPGRADE_SHA256_BIN_SIZE);
    if ((entry.m_flags & XUPGRADE_PACKAGE_FLAG_ZLIB) == 0u && entry.m_rawSize != entry.m_compSize) {
        return false;                       /* 未压缩条目两尺寸必须相等 */
    }
    *consumed = nameLen + xupgrade_entryFixedSize;
    *out = entry;
    return true;
}

/**
 * @brief 计算数据区在包文件内的起始偏移（契约见 XUpgradePackage.h:138）。
 */
uint64_t XUpgradePackage_payloadOffset(const XUpgradePackageHeader* header)
{
    if (!header) {
        return 0;
    }
    return (uint64_t)header->m_headerSize;
}

/* ========================================================================== */
/*                     写侧序列化原语（测试/服务端打包工具用）                   */
/* ========================================================================== */

/**
 * @brief 校验条目结构是否可序列化，并返回其磁盘占用字节数。
 * @param entry 待检查条目；可为 NULL。
 * @return 合法返回 nameLen+59；entry 为 NULL、路径通不过白名单校验、name
 *         长度超过 XUPGRADE_PACKAGE_NAME_MAX-1、flags 含未知置位或未压缩
 *         条目 compSize 与 rawSize 不相等返回 0。
 * @note 打包工具先对所有条目累计本函数返回值再加定长头大小，得到
 *       XUpgradePackageHeader.m_headerSize（数据区起点）。
 */
size_t XUpgradePackage_entryWireSize(const XUpgradePackageEntry* entry)
{
    size_t nameLen;

    if (!entry || !XUpgradePackage_checkEntryName(entry->m_name)) {
        return 0;
    }
    nameLen = xups_cstr_len(entry->m_name);
    if (nameLen > (size_t)(XUPGRADE_PACKAGE_NAME_MAX - 1u)) {
        return 0;
    }
    if ((entry->m_flags & (uint8_t)~XUPGRADE_PACKAGE_FLAG_ZLIB) != 0u) {
        return 0;
    }
    if ((entry->m_flags & XUPGRADE_PACKAGE_FLAG_ZLIB) == 0u && entry->m_rawSize != entry->m_compSize) {
        return 0;
    }
    return nameLen + xupgrade_entryFixedSize;
}

/**
 * @brief 将 .pak 定长头序列化为小端磁盘字节。
 * @param out 输出缓冲区；调用方提供存储空间，不能为 NULL。
 * @param cap out 容量（字节）；不得小于 XUPGRADE_PACKAGE_FIXED_HEADER_SIZE。
 * @param used 输出实际写出字节数；不能为 NULL，失败时不写入。
 * @param header 待序列化包头；不能为 NULL。
 * @return 成功返回 true；参数非法、cap 不足、format 不为
 *         XUPGRADE_PACKAGE_FORMAT、entryCount 越出 [1,65535] 或 headerSize
 *         小于定长头大小返回 false。
 * @note 打包顺序：先算好 headerSize/payloadSize/双哈希再写头，写头不修改
 *       入参；字节布局与 XUpgradePackage_parseHeader 的读入口严格互逆。
 */
bool XUpgradePackage_writeHeader(uint8_t* out, size_t cap, size_t* used, const XUpgradePackageHeader* header)
{
    if (!out || !used || !header) {
        return false;
    }
    if (header->m_format != XUPGRADE_PACKAGE_FORMAT) {
        return false;
    }
    if (header->m_entryCount < 1u || header->m_entryCount > 65535u) {
        return false;
    }
    if ((size_t)header->m_headerSize < XUPGRADE_PACKAGE_FIXED_HEADER_SIZE) {
        return false;
    }
    if (cap < XUPGRADE_PACKAGE_FIXED_HEADER_SIZE) {
        return false;
    }
    memcpy(out, XUPGRADE_PACKAGE_MAGIC, 4);
    xupgrade_put32(out + 4, header->m_format);
    xupgrade_put32(out + 8, header->m_entryCount);
    xupgrade_put32(out + 12, header->m_headerSize);
    xupgrade_put64(out + 16, header->m_payloadSize);
    memcpy(out + 24, header->m_payloadSha256, XUPGRADE_SHA256_BIN_SIZE);
    *used = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
    return true;
}

/**
 * @brief 将一个条目表条目序列化为小端磁盘字节。
 * @param out 输出缓冲区；调用方提供存储空间，不能为 NULL。
 * @param cap out 容量（字节）；不足时返回 false。
 * @param used 输出实际写出字节数；不能为 NULL，失败时不写入。
 * @param entry 待序列化条目；不能为 NULL，须先通过
 *        XUpgradePackage_entryWireSize 的合法性检查。
 * @return 成功返回 true；参数非法、cap 不足或条目非法（同
 *         XUpgradePackage_entryWireSize 的拒绝条件）返回 false。
 * @note 条目表必须紧排且恰好终止于 headerSize（数据区起点），读侧
 *       XUpgradePackage_verify 按此强校验；打包时逐条目紧接写出即可。
 */
bool XUpgradePackage_writeEntry(uint8_t* out, size_t cap, size_t* used, const XUpgradePackageEntry* entry)
{
    size_t nameLen;

    if (!out || !used || !entry) {
        return false;
    }
    if (XUpgradePackage_entryWireSize(entry) == 0) {
        return false;
    }
    nameLen = xups_cstr_len(entry->m_name);
    if (cap < nameLen + xupgrade_entryFixedSize) {
        return false;
    }
    xupgrade_put16(out, (uint16_t)nameLen);
    memcpy(out + 2, entry->m_name, nameLen);
    out[2u + nameLen] = entry->m_flags;
    xupgrade_put64(out + 3u + nameLen, entry->m_rawSize);
    xupgrade_put64(out + 11u + nameLen, entry->m_compSize);
    xupgrade_put64(out + 19u + nameLen, entry->m_offset);
    memcpy(out + 27u + nameLen, entry->m_sha256, XUPGRADE_SHA256_BIN_SIZE);
    *used = nameLen + xupgrade_entryFixedSize;
    return true;
}

/* ========================================================================== */
/*                        条目数据 zlib 压缩原语                               */
/* ========================================================================== */

/**
 * @brief 将条目原始内容压缩为 zlib 流（RFC1950 封装 + deflate）。
 * @param dest 输出缓冲区；调用方提供存储空间，不能为 NULL。
 * @param destCap dest 容量（字节）；不得小于 compressBound(srcLen)。
 * @param destLen 输出实际压缩字节数；不能为 NULL，失败时不写入。
 * @param src 条目原始内容；srcLen 为 0 时可为 NULL（按空内容压缩）。
 * @param srcLen 原始内容字节数；不得超过 0xFFFFFFFF（zlib uLong 上限，
 *               Win32 下 unsigned long 为 32 位）。
 * @return 成功返回 true；参数非法、srcLen 超限、destCap 不足或 zlib 内部
 *         失败返回 false。
 * @note 使用 zlib 默认压缩级；输出与 XUPGRADE_PACKAGE_FLAG_ZLIB 条目的磁盘
 *       表示一致，读侧经 XUpgradePackage_decompress 还原。
 */
bool XUpgradePackage_compress(uint8_t* dest, size_t destCap, size_t* destLen, const uint8_t* src, size_t srcLen)
{
    static const uint8_t empty = 0;
    uLongf bound;
    uLongf outLen;
    int rc;

    if (!dest || !destLen || (!src && srcLen != 0)) {
        return false;
    }
    if (srcLen > (size_t)0xFFFFFFFFu) {
        return false;
    }
    if (!src) {
        src = &empty;
    }
    bound = compressBound((uLong)srcLen);
    if ((size_t)bound > destCap) {
        return false;                       /* 输出缓冲区不足（确定性拒绝） */
    }
    outLen = bound;
    rc = compress2((Bytef*)dest, &outLen, (const Bytef*)src, (uLong)srcLen, Z_DEFAULT_COMPRESSION);
    if (rc != Z_OK) {
        return false;
    }
    *destLen = (size_t)outLen;
    return true;
}

/**
 * @brief 将条目 zlib 流还原为原始内容。
 * @param dest 输出缓冲区；调用方提供存储空间，不能为 NULL。
 * @param destCap dest 容量（字节）；不得小于条目声明的 m_rawSize，为 0 直接
 *                返回 false，且不得超过 0xFFFFFFFF（zlib uLong 上限）。
 * @param destLen 输出实际还原字节数；不能为 NULL，失败时不写入。
 * @param src 条目压缩数据；不能为 NULL。
 * @param srcLen 压缩数据字节数；不得超过 0xFFFFFFFF。
 * @return 成功返回 true；参数非法、zlib 流损坏（Z_DATA_ERROR）或 dest 容量
 *         不足（Z_BUF_ERROR）返回 false。
 * @note 调用方（引擎/校验方）必须以条目 m_rawSize 给足 destCap 并核对
 *       还原字节数与其一致；容器哈希（条目 sha256 + payloadSha256）是防
 *       解压炸弹越界的最终关卡。
 */
bool XUpgradePackage_decompress(uint8_t* dest, size_t destCap, size_t* destLen, const uint8_t* src, size_t srcLen)
{
    uLongf outLen;
    int rc;

    if (!dest || !destLen || !src) {
        return false;
    }
    if (destCap == 0 || destCap > (size_t)0xFFFFFFFFu || srcLen > (size_t)0xFFFFFFFFu) {
        return false;
    }
    outLen = (uLongf)destCap;
    rc = uncompress((Bytef*)dest, &outLen, (const Bytef*)src, (uLong)srcLen);
    if (rc != Z_OK) {
        return false;
    }
    *destLen = (size_t)outLen;
    return true;
}

/* ========================================================================== */
/*                    一次性双哈希校验（读侧顺序流式实现）                       */
/* ========================================================================== */

#if XUPGRADE_VERIFY_ON
/**
 * @brief SHA-256 一次性摘要（XUpgradeVerify.c 提供，模块内共享封装，
 *        底层为库内 XCryptographicHash 门面；契约头在 M2 验签阶段统一
 *        收口声明，此处为两侧一致的本地声明）。
 */
extern bool XUpgradeVerify_sha256Digest(const uint8_t* data, size_t len, uint8_t* outDigest);

/** @brief 比较两个二进制 SHA-256 摘要是否一致。 */
static bool xupgrade_digestEqual(const uint8_t* a, const uint8_t* b)
{
    return memcmp(a, b, XUPGRADE_SHA256_BIN_SIZE) == 0;
}
#endif /* XUPGRADE_VERIFY_ON */

/**
 * @brief 一次性校验整段 .pak 字节流（设计文档 §5 读路径的容器级入口）。
 * @param bytes 包文件完整字节序列；借用，不能为 NULL。
 * @param size bytes 字节数；0 返回 XUpgradeError_InvalidArgument。
 * @return 校验通过返回 XUpgradeError_Ok；bytes 为 NULL 或 size 为 0 返回
 *         XUpgradeError_InvalidArgument；魔数/格式/长度/条目表结构/条目
 *         数据区引用越界/双哈希任一不符返回 XUpgradeError_VerifyFailed；
 *         摘要计算或内存分配失败返回 XUpgradeError_Io。
 * @note 校验顺序即磁盘顺序：头→条目表（必须恰好终止于 headerSize）→
 *       数据区整段 payloadSha256→逐条目解压后 sha256。XUPGRADE_VERIFY_ON=0
 *       时（仅内部调试）跳过全部哈希比对，仅做结构与越界校验，调用方不得
 *       将其结果当作完整性结论。压缩条目声明的 rawSize 若超过 compSize 的
 *       deflate 理论最大膨胀比 1032:1（gzip FAQ 上限）即按非法容器拒绝，
 *       分配大小受其钳制。
 */
XUpgradeError XUpgradePackage_verify(const uint8_t* bytes, size_t size)
{
    XUpgradePackageHeader header;
    XUpgradePackageEntry entry;
    size_t consumed = 0;
    size_t pos = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
    size_t tableLimit;
    size_t payloadBase;
#if XUPGRADE_VERIFY_ON
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
#endif /* XUPGRADE_VERIFY_ON */
    uint32_t i;

    if (!bytes || size == 0) {
        return XUpgradeError_InvalidArgument;
    }
    if (!XUpgradePackage_parseHeader(&header, bytes, size)) {
        return XUpgradeError_VerifyFailed;
    }
    if ((size_t)header.m_headerSize > size) {
        return XUpgradeError_VerifyFailed;  /* 头声明越过文件实际长度 */
    }
    payloadBase = (size_t)header.m_headerSize;
    if ((uint64_t)header.m_payloadSize > (uint64_t)(size - payloadBase)) {
        return XUpgradeError_VerifyFailed;  /* 数据区声明越过文件实际长度 */
    }
    tableLimit = payloadBase;
#if XUPGRADE_VERIFY_ON
    /* 数据区整段哈希（下载校验与解包前复核共用本口径） */
    if (!XUpgradeVerify_sha256Digest(bytes + payloadBase, (size_t)header.m_payloadSize, digest)) {
        return XUpgradeError_Io;
    }
    if (!xupgrade_digestEqual(digest, header.m_payloadSha256)) {
        return XUpgradeError_VerifyFailed;
    }
#endif /* XUPGRADE_VERIFY_ON */
    for (i = 0; i < header.m_entryCount; ++i) {
        const uint8_t* comp;
        if (!XUpgradePackage_parseEntry(&entry, bytes + pos, tableLimit - pos, &consumed)) {
            return XUpgradeError_VerifyFailed;
        }
        pos += consumed;                    /* consumed 有界，pos 不会越过 tableLimit */
        if (entry.m_offset > header.m_payloadSize ||
            entry.m_compSize > header.m_payloadSize - entry.m_offset) {
            return XUpgradeError_VerifyFailed;  /* 条目数据区引用越界 */
        }
        comp = bytes + payloadBase + (size_t)entry.m_offset;
#if XUPGRADE_VERIFY_ON
        if ((entry.m_flags & XUPGRADE_PACKAGE_FLAG_ZLIB) != 0u) {
            uint8_t* raw;
            size_t rawLen = 0;
            if (entry.m_rawSize > (uint64_t)SIZE_MAX) {
                return XUpgradeError_VerifyFailed;  /* 目标平台编址不可能 */
            }
            if (entry.m_rawSize > (uint64_t)entry.m_compSize * 1032u + 64u) {
                return XUpgradeError_VerifyFailed;  /* 超过 deflate 物理膨胀上限 */
            }
            if (entry.m_rawSize == 0u) {
                if (!XUpgradeVerify_sha256Digest(NULL, 0, digest)) {
                    return XUpgradeError_Io;
                }
            } else {
                raw = (uint8_t*)XMalloc_System((size_t)entry.m_rawSize);
                if (!raw) {
                    return XUpgradeError_Io;
                }
                if (!XUpgradePackage_decompress(raw, (size_t)entry.m_rawSize, &rawLen,
                                                comp, (size_t)entry.m_compSize) ||
                    rawLen != (size_t)entry.m_rawSize) {
                    XFree_System(raw);
                    return XUpgradeError_VerifyFailed;
                }
                if (!XUpgradeVerify_sha256Digest(raw, rawLen, digest)) {
                    XFree_System(raw);
                    return XUpgradeError_Io;
                }
                XFree_System(raw);
            }
        } else {
            if (!XUpgradeVerify_sha256Digest(comp, (size_t)entry.m_compSize, digest)) {
                return XUpgradeError_Io;
            }
        }
        if (!xupgrade_digestEqual(digest, entry.m_sha256)) {
            return XUpgradeError_VerifyFailed;
        }
#endif /* XUPGRADE_VERIFY_ON */
    }
    if (pos != tableLimit) {
        return XUpgradeError_VerifyFailed;  /* 条目表未恰好终止于数据区起点 */
    }
    return XUpgradeError_Ok;
}

#endif /* XUPGRADE_ON */
