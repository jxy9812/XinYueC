/**
 * @file       XUpgradePackage.h
 * @brief      .pak 升级包容器常量、结构与解析契约（设计文档 §5）。
 * @details    自定义极简容器，不引入 tar/zip 依赖；嵌入端按条目流式解包直写
 *             Fatfs，顺序读一次性校验，无需随机访问。本头文件只定义磁盘
 *             格式常量、解析后的内存表示和流式解析入口；解包落盘由引擎与
 *             存储后端（XUpgradeStore）完成。.pak 的生成属于服务端打包工具
 *             职责，端上不提供写包容器入口。
 *
 *             磁盘布局（整数一律小端编码）：
 *               偏移   字段
 *               0      magic[4]            "XUPK"
 *               4      u32 format          =1
 *               8      u32 entryCount
 *               12     u32 headerSize      （头+条目表总长，数据区起点）
 *               16     u64 payloadSize     （数据区总字节数）
 *               24     u8  payloadSha256[32]（数据区整段哈希）
 *               56     条目表 entryCount × { u16 nameLen; u8 name[nameLen];
 *                        u8 flags; u64 rawSize; u64 compSize; u64 offset;
 *                        u8 sha256[32] }
 *               …      数据区
 */

#ifndef XUPGRADEPACKAGE_H
#define XUPGRADEPACKAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XUpgrade_config.h"

#if XUPGRADE_ON

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ========================================================================== */
/*                                  格式常量                                   */
/* ========================================================================== */

/** @brief 包文件魔数（文件头前 4 字节的 ASCII "XUPK"）。 */
#define XUPGRADE_PACKAGE_MAGIC "XUPK"
/** @brief 容器格式版本；解析只接受本值。 */
#define XUPGRADE_PACKAGE_FORMAT 1u
/** @brief 定长头大小（字节）：magic(4)+format(4)+entryCount(4)+headerSize(4)+payloadSize(8)+payloadSha256(32)。 */
#define XUPGRADE_PACKAGE_FIXED_HEADER_SIZE 56u
/** @brief 条目 flags：bit0 置位表示条目数据经 zlib deflate 压缩。 */
#define XUPGRADE_PACKAGE_FLAG_ZLIB 0x01u
/** @brief 二进制 SHA-256 摘要长度（字节）。 */
#define XUPGRADE_SHA256_BIN_SIZE 32u
/** @brief 条目相对路径容量上限（UTF-8 字节，含结尾 NUL；Fatfs LFN 上限 255 之内）。 */
#define XUPGRADE_PACKAGE_NAME_MAX 192u

/* ========================================================================== */
/*                                  结构体定义                                 */
/* ========================================================================== */

/**
 * @brief .pak 定长头的解析后表示（主机字节序，非磁盘布局）。
 * @details 磁盘上的多字节整数按小端编码；解析函数负责转换与魔数/格式校验。
 */
typedef struct XUpgradePackageHeader {
    uint32_t m_format;                     /**< 容器格式版本，恒为 XUPGRADE_PACKAGE_FORMAT。 */
    uint32_t m_entryCount;                 /**< 条目表条目数，[1, 65535]。 */
    uint32_t m_headerSize;                 /**< 定长头+条目表总字节数，即数据区起点偏移；不得小于 56。 */
    uint64_t m_payloadSize;                /**< 数据区总字节数；与包文件总长- headerSize 一致才接受。 */
    uint8_t m_payloadSha256[XUPGRADE_SHA256_BIN_SIZE]; /**< 数据区整段 SHA-256（下载校验与解包前复核）。 */
} XUpgradePackageHeader;

/**
 * @brief .pak 条目表条目的解析后表示（主机字节序，非磁盘布局）。
 * @details m_name 为包内相对路径（UTF-8，'/' 分隔），必须通过
 *           XUpgradePackage_checkEntryName 白名单校验后才允许落盘。安全
 *           规则（设计文档 §5）：禁止绝对路径、盘符、".." 段与空段；解包
 *           目标必须落在版本目录内，越界即整包拒绝。
 */
typedef struct XUpgradePackageEntry {
    char m_name[XUPGRADE_PACKAGE_NAME_MAX];            /**< 条目相对路径，NUL 结尾 UTF-8。 */
    uint8_t m_flags;                       /**< 条目标志；bit0=XUPGRADE_PACKAGE_FLAG_ZLIB（zlib 压缩），其余位保留必须为 0。 */
    uint64_t m_rawSize;                    /**< 解压后内容字节数。 */
    uint64_t m_compSize;                   /**< 压缩存储字节数；未压缩条目与 m_rawSize 相等。 */
    uint64_t m_offset;                     /**< 条目数据在数据区内的偏移（相对 m_headerSize 起点）。 */
    uint8_t m_sha256[XUPGRADE_SHA256_BIN_SIZE];        /**< 解压后内容的 SHA-256（逐条目校验）。 */
} XUpgradePackageEntry;

/* ========================================================================== */
/*                                  公共 API                                   */
/* ========================================================================== */

/**
 * @brief 解析 .pak 定长头。
 * @param out 输出结构体；调用方提供存储空间。失败时 out 保持原值不变。
 * @param bytes 包文件起始字节序列；借用，不得为 NULL。
 * @param size bytes 可用字节数；小于 XUPGRADE_PACKAGE_FIXED_HEADER_SIZE 返回
 *             false。
 * @return 解析成功返回 true；长度不足、魔数不为 "XUPK"、format 不为 1 或
 *         headerSize 小于定长头大小返回 false。
 * @note 头解析通过后，调用方继续以 XUpgradePackage_parseEntry 顺序解析
 *       entryCount 个条目，随后从 bytes+headerSize 开始为数据区。
 */
bool XUpgradePackage_parseHeader(XUpgradePackageHeader* out, const uint8_t* bytes, size_t size);

/**
 * @brief 从字节流顺序解析一个条目表条目。
 * @param out 输出结构体；调用方提供存储空间。失败时 out 内容未定义，调用方
 *            不得读取。
 * @param bytes 当前解析位置起始的字节序列；借用，不得为 NULL。
 * @param size bytes 可用字节数。
 * @param consumed 输出本次实际消耗的字节数；不能为 NULL，失败时不写入。
 * @return 解析成功返回 true；长度不足、nameLen 超过
 *         XUPGRADE_PACKAGE_NAME_MAX-1、路径通不过 XUpgradePackage_checkEntryName、
 *         flags 含未知置位或压缩标志与 rawSize/compSize 关系矛盾返回 false。
 * @note 流式契约：调用方循环调用（每轮前进 consumed 字节）直至解析满
 *       entryCount 个条目；嵌入端可边解析边把 m_name 逐条登记到目标清单，
 *       不需要整表驻留内存。
 */
bool XUpgradePackage_parseEntry(XUpgradePackageEntry* out, const uint8_t* bytes, size_t size, size_t* consumed);

/**
 * @brief 校验条目相对路径是否落在白名单内。
 * @param name 待校验路径；NUL 结尾 UTF-8 借用，不得为 NULL。
 * @return 合法返回 true；NULL/空串、以 '/' 或 '\\' 开头、含盘符（如 "C:"）、
 *         含反斜杠、空段、".."/"." 段或含白名单外字符返回 false。
 * @note 白名单字符集 [0-9A-Za-z._-/]；本函数是解包落盘前的强制关卡，防止
 *       路径逃逸（设计文档 §5 安全规则）。
 */
bool XUpgradePackage_checkEntryName(const char* name);

/**
 * @brief 计算数据区在包文件内的起始偏移。
 * @param header 已解析的包头；可为 NULL。
 * @return header 为 NULL 返回 0；否则返回 m_headerSize（数据区起点）。
 * @note 条目 m_offset 一律相对本偏移解释；实现侧还应校验
 *       m_offset+m_compSize 不越过 m_payloadSize。
 */
uint64_t XUpgradePackage_payloadOffset(const XUpgradePackageHeader* header);

#endif /* XUPGRADE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XUPGRADEPACKAGE_H */
