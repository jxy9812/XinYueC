/**
 * @file       XKeySequence.h
 * @brief      XKeyCombination / XKeySequence 键序列值类型（对标 Qt 6.8
 *             QKeyCombination / QKeySequence）。
 * @details    XGui 的快捷键序列承载：一个 `XKeySequence` 最多 4 组
 *             `XKeyCombination`（修饰位 + 非修饰键），与 Qt
 *             `QKeySequencePrivate::MaxKeyCount == 4` 同上限
 *             （qkeysequence.h:141-148 的 `QKeySequence(int k1, int k2,
 *             int k3, int k4)` 与 qkeysequence.cpp:831 的
 *             `static_assert(MaxKeyCount == 4)`）。
 *
 *             本类型此前内嵌在 XKeySequenceEdit.h 里（只有捕获控件在用），
 *             导致 XShortcut 只能承载 `XKey` 裸键码、无法表达 Ctrl/Alt/
 *             Shift/Meta 组合。现上移为独立公共值类型，**单一事实源**：
 *             XShortcut（快捷键对象）、XKeySequenceEdit（捕获控件）、
 *             XAbstractButton / XLabel（助记符与快捷键文本）共用同一份
 *             定义与同一套 Qt 文本编解码（`toString`/`fromString`）。
 *
 *             语义取证（Qt 6.8.3）：
 *             - `QKeySequence::matches`（qkeysequence.cpp:1335-1354）：
 *               `userN > seqN ⇒ NoMatch`；等长 ⇒ ExactMatch 否则 PartialMatch；
 *               逐组比较，任一组不等 ⇒ NoMatch；
 *             - `QKeySequencePrivate::encodeString`（qkeysequence.cpp:
 *               1200-1265）：修饰位输出顺序 **Meta → Ctrl → Alt → Shift →
 *               Num**，再拼键名；
 *             - `QKeySequencePrivate::keyName`（qkeysequence.cpp:1268-1317）：
 *               `key && key < Qt::Key_Escape && key != Qt::Key_Space` ⇒
 *               单字符大写；F1..F35 ⇒ "F%1"；其余查 keyname 表；
 *             - `QKeySequencePrivate::decodeString`（qkeysequence.cpp:
 *               1040-1200）：整串小写、按 '+' 切段，除末段外每段必须是修饰
 *               名；末段按「单字符 / fN / keyname 表」解析；
 *             - `QKeySequence::mnemonic`（qkeysequence.cpp:935-967）：
 *               取首个 `&x`（`&&` 为字面量）的 x，转大写，配 **Alt** 修饰。
 *
 * @note       本类型是**纯值对象**：无堆资源、无所有权、无 init/deinit，
 *             按值拷贝即完整副本。`XKeySequence` 的 `combos` 数组定长
 *             4，`count` 为有效组数（0 = 空序列）。
 * @warning    `XKeySequence_fromString` / `XKeySequence_toString` 一律以
 *             UTF-8 C 字符串为出入口（与框架其它文本 API 同口径）；
 *             键名匹配按 ASCII 大小写不敏感（Qt 的 `toLower()` 同义）。
 * @author     XinYueC 团队
 */
#ifndef XKEYSEQUENCE_H
#define XKEYSEQUENCE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XEvent.h" /* XKeyboardModifiers / XKey（修饰位与键码枚举）。 */

#if XWIDGET_ON

/** @brief 单条键序列的最大组合数（对标 QKeySequencePrivate::MaxKeyCount = 4）。 */
#define XKEYSEQUENCE_MAX_LENGTH 4

/**
 * @brief 单组快捷键组合（对标 QKeyCombination）。
 * @details `modifiers` 是 XKeyboardModifiers 位掩码（与
 *          `XKeyEvent::m_modifiers` 同域，取
 *          XKeyboardModifier_ControlModifier 等），`key` 是 XKey 键码。
 *          空组合为 `{ NoModifier, 0 }`（key==0 即 QKeyCombination 的
 *          `fromCombined(0)`）。
 */
typedef struct XKeyCombination
{
    XKeyboardModifiers modifiers; /**< 修饰键位掩码（XKeyboardModifiers 域）。 */
    int key;                     /**< 非修饰键键码（XKey 枚举或 ASCII 码位）。 */
} XKeyCombination;

/**
 * @brief 快捷键序列（最多 4 组组合的有序集合，对标 QKeySequence）。
 * @details `count` 为有效组数；`combos[count..3]` 恒为零组合。
 */
typedef struct XKeySequence
{
    XKeyCombination combos[XKEYSEQUENCE_MAX_LENGTH]; /**< 组合数组（定长 4）。 */
    int count; /**< 有效组合数（0 = 空序列）。 */
} XKeySequence;

/** @brief 序列文本格式（对标 QKeySequence::SequenceFormat，数值一致）。 */
typedef enum XKeySequenceFormat
{
    XKeySequenceFormat_NativeText = 0,   /**< 本地文本（Qt::NativeText）。 */
    XKeySequenceFormat_PortableText = 1  /**< 可移植文本（Qt::PortableText）。 */
} XKeySequenceFormat;

/** @brief 序列匹配结果（对标 QKeySequence::SequenceMatch，数值一致）。 */
typedef enum XKeySequenceMatch
{
    XKeySequenceMatch_NoMatch = 0,   /**< 无匹配（Qt::NoMatch）。 */
    XKeySequenceMatch_PartialMatch,  /**< 前缀匹配（Qt::PartialMatch）。 */
    XKeySequenceMatch_ExactMatch     /**< 完全匹配（Qt::ExactMatch）。 */
} XKeySequenceMatch;

/* ==================== 组合（XKeyCombination） ==================== */

/**
 * @brief      构造空组合（对标 QKeyCombination() / fromCombined(0)）。
 * @return     空组合 `{ NoModifier, 0 }`。
 */
XKeyCombination XKeyCombination_empty(void);

/**
 * @brief      由修饰位与键码构造组合（对标 QKeyCombination(modifiers, key)）。
 * @param      modifiers 修饰位掩码（XKeyboardModifiers 域）。
 * @param      key 非修饰键键码。
 * @return     组合值。
 */
XKeyCombination XKeyCombination_make(XKeyboardModifiers modifiers, int key);

/**
 * @brief      是否为空组合（key==0 且无修饰位）。
 * @param      self 组合借用指针；可为 NULL（NULL 视为空）。
 * @return     true 为空。
 */
bool XKeyCombination_isEmpty(const XKeyCombination* self);

/**
 * @brief      两个组合是否相等（对标 QKeyCombination::operator==）。
 * @param      a 左值借用指针；可为 NULL。
 * @param      b 右值借用指针；可为 NULL。
 * @return     true 相等（NULL 与 NULL 视为相等）。
 */
bool XKeyCombination_isEqual(const XKeyCombination* a,
                             const XKeyCombination* b);

/* ==================== 序列（XKeySequence） ==================== */

/**
 * @brief      构造空序列（对标 QKeySequence()）。
 * @return     空序列值（count==0，组合全零）。
 */
XKeySequence XKeySequence_empty(void);

/**
 * @brief      由单组组合构造序列（对标 QKeySequence(QKeyCombination)）。
 * @param      combination 首组组合（按值传入）。
 * @return     单组序列；入参为空组合时返回空序列。
 */
XKeySequence XKeySequence_fromCombination(XKeyCombination combination);

/**
 * @brief      由键码 + 修饰位构造单组序列（快捷路径）。
 * @param      modifiers 修饰位掩码。
 * @param      key 非修饰键键码。
 * @return     单组序列；key==0 时返回空序列。
 */
XKeySequence XKeySequence_fromKey(XKeyboardModifiers modifiers, int key);

/**
 * @brief      有效组合数（对标 QKeySequence::count）。
 * @param      self 序列借用指针；可为 NULL。
 * @return     组合数；NULL 返回 0。
 */
int XKeySequence_count(const XKeySequence* self);

/**
 * @brief      是否为空序列（对标 QKeySequence::isEmpty）。
 * @param      self 序列借用指针；可为 NULL。
 * @return     true 空/NULL。
 */
bool XKeySequence_isEmpty(const XKeySequence* self);

/**
 * @brief      按索引取组合（对标 QKeySequence::operator[]）。
 * @param      self 序列借用指针；可为 NULL。
 * @param      index 索引（0 起）。
 * @return     组合值；越界/NULL 返回空组合。
 */
XKeyCombination XKeySequence_at(const XKeySequence* self, int index);

/**
 * @brief      首组组合的键码（对标 `QKeySequence::operator[](0).key()`）。
 * @param      self 序列借用指针；可为 NULL。
 * @return     键码；空序列/NULL 返回 0。
 */
int XKeySequence_firstKey(const XKeySequence* self);

/**
 * @brief      两个序列是否逐组相等（对标 QKeySequence::operator==）。
 * @param      a 左值借用指针；可为 NULL。
 * @param      b 右值借用指针；可为 NULL。
 * @return     true 相等（NULL 与 NULL 视为相等；组数也须相等）。
 */
bool XKeySequence_isEqual(const XKeySequence* a, const XKeySequence* b);

/**
 * @brief      序列匹配（对标 QKeySequence::matches，qkeysequence.cpp:
 *             1335-1354）。
 * @details    语义逐条对齐 Qt：
 *             ① `self->count > seq->count` ⇒ NoMatch（被查序列比输入短）；
 *             ② 组数相等 ⇒ 候选 ExactMatch，否则候选 PartialMatch；
 *             ③ 逐组比较，任一组不等 ⇒ NoMatch；
 *             ④ 全等 ⇒ 返回②的候选结果。
 * @param      self 序列（「已登记」的一侧）借用指针；可为 NULL。
 * @param      seq 被匹配的序列（「输入」的一侧）借用指针；可为 NULL。
 * @return     匹配结果枚举；NULL 入参按空序列处理。
 */
XKeySequenceMatch XKeySequence_matches(const XKeySequence* self,
                                       const XKeySequence* seq);

/**
 * @brief      序列转文本（对标 QKeySequence::toString）。
 * @details    修饰位输出顺序 Meta → Ctrl → Alt → Shift → Num（Num 为
 *             XKeyboardModifier_KeypadModifier，对标 qkeysequence.cpp:
 *             1236-1257 的 encodeString），随后是键名（keyName 口径见
 *             文件头）；多组之间以 ", " 分隔（Qt 的 addKey 以 '+' 连接组内、
 *             `toString` 在多组之间用 ", "）。
 * @param      self 序列借用指针；可为 NULL 或空（输出空串）。
 * @param      format 文本格式（NativeText/PortableText；本库两者输出相同
 *             ——无翻译表，见 @note）。
 * @param      out 输出缓冲；为 NULL 或 cap==0 时只计算所需长度。
 * @param      cap 输出缓冲字节数（含终止符）。
 * @return     实际写入的字节数（不含终止符）；缓冲不足时截断并返回可写
 *             下的最大字节数。
 * @note       Qt 的 NativeText 走 `QCoreApplication::translate("QShortcut",
 *             ...)` 取本地化修饰名/键名。本库无翻译表且 XGui 的文本一律
 *             UTF-8 直出，故两种格式当前输出相同（可移植英文名）。
 */
int XKeySequence_toString(const XKeySequence* self, XKeySequenceFormat format,
                          char* out, size_t cap);

/**
 * @brief      文本解析为序列（对标 `QKeySequence(const QString&)` /
 *             `QKeySequencePrivate::decodeString`）。
 * @details    解析规则（逐条对齐 qkeysequence.cpp:1040-1200）：
 *             ① 以 ',' 切成最多 4 段（Qt 的 `assign` 同样按 ',' 切分，
 *                故 "Ctrl+," 无法表达——与 Qt 一致）；
 *             ② 每段内按 '+' 切分：除末段外每段必须是修饰名
 *                （ctrl/shift/alt/meta/num，ASCII 大小写不敏感）；
 *             ③ 末段：单字符 ⇒ 大写的 Unicode 码位；`f1`..`f35` ⇒
 *                XKey_F1..F35；否则查键名表（space/esc/tab/backtab/
 *                backspace/return/enter/ins/del/…，大小写不敏感）；
 *             ④ 任一段非法 ⇒ 该段跳过；无任何合法段 ⇒ 返回 false 且
 *                `out` 置空序列。
 *             "Ctrl++"（加号键本身）按 Qt 的 `lastIndexOf('+', size-2)`
 *             口径可解析。
 * @param      utf8 以 '\0' 结尾的 UTF-8 文本；可为 NULL（视作空）。
 * @param      out 输出序列；不可为 NULL（失败时置空序列）。
 * @return     true 至少解析出一组组合；false 文本为空/非法。
 */
bool XKeySequence_fromString(const char* utf8, XKeySequence* out);

/**
 * @brief      取文本里的助记符序列（对标 QKeySequence::mnemonic，
 *             qkeysequence.cpp:935-967）。
 * @details    扫描 `&x`（`&&` 表示字面 `&`，跳过），取首个可打印字符
 *             转大写并配 **Alt** 修饰位；无 `&x` 返回空序列。
 * @param      textUtf8 以 '\0' 结尾的 UTF-8 文本；可为 NULL。
 * @return     助记符序列（Alt + 大写码位）；无则空序列。
 */
XKeySequence XKeySequence_mnemonic(const char* textUtf8);

/* ==================== 键名（供 XShortcut/控件复用） ==================== */

/**
 * @brief      键码转可读键名（对标 QKeySequencePrivate::keyName）。
 * @details    `key && key < XKey_Escape && key != XKey_Space` ⇒ 单字符
 *             大写；XKey_F1..XKey_F35 ⇒ "F1".."F35"；其余查键名表；
 *             未知键返回 "?"。
 * @param      key 键码（XKey 枚举或 ASCII 码位）。
 * @param      out 输出缓冲；可为 NULL（只返回长度）。
 * @param      cap 输出缓冲字节数（含终止符）。
 * @return     写入字节数（不含终止符）。
 * @note       结果只写调用方缓冲，**不返回静态缓冲**（避免并发重入问题）。
 */
int XKeySequence_keyName(int key, char* out, size_t cap);

/**
 * @brief      解析单个键名（`keyName` 的逆运算，供文本解析复用）。
 * @param      nameUtf8 键名（ASCII 大小写不敏感）；可为 NULL。
 * @param      length 键名字节数。
 * @return     键码；无法解析返回 0（XKey_None）。
 */
int XKeySequence_keyFromName(const char* nameUtf8, size_t length);

#endif /* XWIDGET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XKEYSEQUENCE_H */
