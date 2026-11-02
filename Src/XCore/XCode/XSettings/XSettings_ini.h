/**
 * @file       XSettings_ini.h
 * @brief      XSettings 的 INI 算法内部接口（仅 XSettings*.c 使用）。
 * @details    对齐 Qt 6.8.3 qsettings.cpp 的 INI 键/值转义、类型前缀、
 *             节解析与文件写出算法；字节缓冲与 TLV 承载工具。不属于公开
 *             API，外部模块禁止包含本头文件。
 * @author     XinYueC 团队
 */
#ifndef XSETTINGS_INI_H
#define XSETTINGS_INI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
#include "XVariant.h"
#include "XGeometry.h"
#include "XDateTime.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ==================== 字节缓冲 ==================== */

/** @brief 可增长字节缓冲（XMemory 分配；禁 stdio/string.h 的合规承载）。 */
typedef struct XSettingsByteBuf
{
    char* data;   /**< 缓冲区指针；可为 NULL（空缓冲）。 */
    size_t len;   /**< 有效字节数。 */
    size_t cap;   /**< 已分配容量。 */
} XSettingsByteBuf;

/** @brief 初始化为空缓冲。 */
void xbb_init(XSettingsByteBuf* buf);
/** @brief 释放缓冲内存并复位。 */
void xbb_free(XSettingsByteBuf* buf);
/** @brief 追加 n 字节；失败（OOM）返回 false，缓冲保持原状。 */
bool xbb_append(XSettingsByteBuf* buf, const char* bytes, size_t n);
/** @brief 追加单字节。 */
bool xbb_append_char(XSettingsByteBuf* buf, char ch);
/** @brief 追加 NUL 结尾字符串。 */
bool xbb_append_str(XSettingsByteBuf* buf, const char* str);
/** @brief 追加 XString 的 UTF-8 序列化字节。 */
bool xbb_append_string(XSettingsByteBuf* buf, const XString* str);
/** @brief 缓冲内容按 UTF-8 解码为新建 XString（空缓冲产出空串）。 */
XString* xbb_toString(const XSettingsByteBuf* buf);

/* ==================== INI 键转义（输出恒为 ASCII 字节） ==================== */

/**
 * @brief 键转义（对标 iniEscapedKey）。
 * @details '/'→'\\'；[A-Za-z0-9_.-] 原样；≤0xFF 码元 → %HH 大写；其余
 *          码元 → %UXXXX 大写（代理对拆成两段，与 Qt 一致）。
 */
bool xsettingsEscapedKey(const XString* key, XSettingsByteBuf* out);

/**
 * @brief 键解转义（对标 iniUnescapedKey）。
 * @param bytes 原始 UTF-8 字节（键段）。
 * @return 新建 XString；'\\'→'/'，%HH/%UHHHH 解码，无效序列容错输出
 *         字面 '%'。调用方释放。
 */
XString* xsettingsUnescapedKey(const char* bytes, size_t len);

/* ==================== INI 值转义 ==================== */

/**
 * @brief 值转义（对标 iniEscapedString）。
 * @details \0\a\b\f\n\r\t\v\"\\ 转义；控制字符（及原始字节前缀内容的
 *          ≥0x7F 码元）→ \xHH 小写十六进制；其余 useCodec 时按 UTF-8
 *          输出；含 ';' ',' '=' 或首尾空格时整体加双引号；\0/\xHH 后随
 *          十六进制数字时后者强制再转义。
 */
bool xsettingsEscapedString(const XString* str, XSettingsByteBuf* out);

/**
 * @brief 值列表转义（对标 iniEscapedStringList）：空表 → "@Invalid()"，
 *        否则逐元素独立转义后以 ", " 连接。
 */
bool xsettingsEscapedStringList(const XStringList* list, XSettingsByteBuf* out);

/**
 * @brief 值解转义状态机（对标 iniUnescapedStringList）。
 * @param bytes 值原始字节（已按行扫描截出，含引号/转义/逗号）。
 * @param isStringListOut 输出：出现过引号外逗号（列表语义）。
 * @return 新建 XStringList（元素为 XString；单值时表长为 1 或 0），调用
 *         方释放；OOM 返回 NULL。
 */
XStringList* xsettingsUnescapedStringList(const char* bytes, size_t len, bool* isStringListOut);

/* ==================== 类型前缀：变体 ↔ 文本 ==================== */

/**
 * @brief 变体 → INI 文本（对标 variantToString）。
 * @details null→@Invalid()；ByteArray→@ByteArray(...)；String 含 NUL→
 *          @String(...)、以 @ 开头→前置 '@'；Bool→true/false；整型→十
 *          进制；浮点→最短往返 'g'；Point/Size/Rect→@Point/@Size/@Rect；
 *          DateTime→@DateTime(内部 TLV base64)；List→", " 连接逐元素；
 *          Map/Hash 等→@Variant(内部 TLV base64)。调用方释放。
 */
XString* xsettingsVariantToString(const XVariant* variant);

/**
 * @brief INI 文本 → 变体（对标 stringToVariant）。
 * @details 仅当 s 以 '@' 开头时尝试前缀解析（@ByteArray/@String/@Variant/
 *          @DateTime/@Rect/@Size/@Point/@Invalid/@ @@ 规则）；不匹配回退
 *          普通字符串。调用方释放；OOM 返回 NULL。
 */
XVariant* xsettingsStringToVariant(const XString* s);

/**
 * @brief 字符串列表 → 变体列表降级判定（对标 stringListToVariantList）。
 * @details 任一元素以 '@' 开头且（长度<2 或第二字符 != '@'）→ 整表降级
 *          为 List 变体（逐元素 xsettingsStringToVariant）；否则 '@@' 剥
 *          一层 '@' 后合成 StringList 变体。调用方释放；OOM 返回 NULL。
 */
XVariant* xsettingsStringListToVariantList(const XStringList* list);

/**
 * @brief 最短往返 'g' 数字文本（对标 QString::number(x,'g',Shortest)）；
 *        指数归一为符号 + 至少两位数字。调用方释放。
 */
XString* xsettingsNumberShortestDouble(double value);

/* ==================== INI 行扫描与节解析 ==================== */

/**
 * @brief 逻辑行扫描（对标 readIniLine）。
 * @details 处理：行首空白跳过；引号内跨物理行；'\' 行续接；行首 ';'
 *          整行注释透明吞掉；引号外 ';' 截断（';' 留给下次调用）；首个
 *          引号外 '=' 记为分隔符；空行透明。
 * @param data 全文件字节。
 * @param size 全文件字节数。
 * @param dataPosInOut 输入/输出扫描游标（读到哪从哪继续）。
 * @param lineStartOut 输出：行内容起始（已跳过行首空白）。
 * @param lineLenOut 输出：行内容长度。
 * @param equalsPosOut 输出：行内 '=' 的绝对字节下标；无 '=' 输出 -1。
 * @return 扫到逻辑行返回 true；数据耗尽返回 false。
 */
bool xsettingsReadIniLine(const uint8_t* data, size_t size, size_t* dataPosInOut,
                          size_t* lineStartOut, size_t* lineLenOut, int64_t* equalsPosOut);

/**
 * @brief 按行切块并产出节原始块（对标 readIniFile + unparsedIniSections）。
 * @details 跳过 UTF-8 BOM；'[' 开头的行解析节头（缺 ']' 时 *okOut=false）；
 *          [General]/[GENERAL] → 根节 ""；[%General] → "General/"；其余
 *          iniUnescapedKey 解码 + 尾 '/'。节块内各行以 '\n' 连接。
 * @param sectionsOut 输出映射容器指针（XMap：XString* 节名 → XByteArray*
 *        块；map 深拷贝持有键值，调用方创建空 map 传入；解析失败时已写入
 *        的条目由调用方随 map 释放）。
 * @param okOut 输出：节头语法是否全部合法（缺 ']' → false）。
 * @return true 全程 OOM 之外的成功路径；false OOM。
 */
bool xsettingsReadIniSections(const uint8_t* data, size_t size, XMap* sectionsOut, bool* okOut);

/* ==================== INI 文件写出 ==================== */

/**
 * @brief 按合并键表产出 INI 文件字节（对标 writeIniFile，统一非 Windows
 *        语义：'\n' 换行、节按转义名字典序、[General] 首节、键按转义
 *        剩余段字典序；键 %HH 大写、值 \xHH 小写；无 BOM UTF-8）。
 * @param keys 键名数组（借用地主持有的 XString* 指针，n 个）。
 * @param values 变体数组（与 keys 一一对应；NULL 元素 = 无效变体）。
 * @param n 键值对个数。
 * @param out 输出缓冲。
 * @return true 成功；false OOM/写值失败。
 */
bool xsettingsWriteIniFile(XString** keys, XVariant** values, size_t n, XSettingsByteBuf* out);

#ifdef __cplusplus
}
#endif

#endif /* XSETTINGS_INI_H */
