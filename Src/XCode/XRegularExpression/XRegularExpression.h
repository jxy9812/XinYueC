/**
 * @file XRegularExpression.h
 * @brief Qt 6.8 QRegularExpression 对齐实现的公共 API。
 * @details 底层使用 Library/pcre2 的 16-bit PCRE2 API，字符串统一使用 UTF-16。
 */
#ifndef XREGULAREXPRESSION_H
#define XREGULAREXPRESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#if !defined(XREGULAREXPRESSION_API_H) && XRegularExpression_ON
#define XREGULAREXPRESSION_API_H

#include "XClass.h"
#include "XString.h"
#include "XStringView.h"
#include "XAnyStringView.h"
#include "XStringList.h"
#include "XChar.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct XRegularExpressionData XRegularExpressionData;
typedef struct XRegularExpressionMatch XRegularExpressionMatch;
typedef struct XRegularExpressionMatchIterator XRegularExpressionMatchIterator;

/* ============================== 枚举定义 ============================== */

/**
 * @brief 正则表达式编译选项。
 */
typedef enum XRegularExpression_PatternOption {
    XRegularExpression_NoPatternOption            = 0x0000, ///< 不启用任何编译选项
    XRegularExpression_CaseInsensitiveOption     = 0x0001, ///< 忽略英文字母大小写
    XRegularExpression_DotMatchesEverythingOption = 0x0002, ///< 让点号匹配换行符
    XRegularExpression_MultilineOption            = 0x0004, ///< 启用多行模式
    XRegularExpression_ExtendedPatternSyntaxOption = 0x0008, ///< 启用扩展模式语法
    XRegularExpression_InvertedGreedinessOption   = 0x0010, ///< 反转量词的贪婪性
    XRegularExpression_DontCaptureOption          = 0x0020, ///< 默认关闭普通捕获组
    XRegularExpression_UseUnicodePropertiesOption = 0x0040 ///< 使用 Unicode 字符属性
} XRegularExpression_PatternOption;
typedef uint32_t XRegularExpression_PatternOptions;

/**
 * @brief 匹配方式。
 */
typedef enum XRegularExpression_MatchType {
    XRegularExpression_NormalMatch = 0, ///< 只接受完整匹配
    XRegularExpression_PartialPreferCompleteMatch, ///< 优先完整匹配，同时报告部分匹配
    XRegularExpression_PartialPreferFirstMatch, ///< 优先第一个可行匹配
    XRegularExpression_NoMatch ///< 只创建匹配对象，不执行匹配
} XRegularExpression_MatchType;

/**
 * @brief 匹配选项。
 */
typedef enum XRegularExpression_MatchOption {
    XRegularExpression_NoMatchOption = 0x0000, ///< 不启用任何匹配选项
    XRegularExpression_AnchorAtOffsetMatchOption = 0x0001, ///< 要求匹配从 offset 开始
    XRegularExpression_AnchoredMatchOption = XRegularExpression_AnchorAtOffsetMatchOption, ///< 兼容别名
    XRegularExpression_DontCheckSubjectStringMatchOption = 0x0002 ///< 不检查主题 UTF-16 有效性
} XRegularExpression_MatchOption;
typedef uint32_t XRegularExpression_MatchOptions;

/**
 * @brief 通配符转换选项。
 */
typedef enum XRegularExpression_WildcardConversionOption {
    XRegularExpression_DefaultWildcardConversion = 0x0000, ///< 生成完整锚定的路径通配符正则
    XRegularExpression_UnanchoredWildcardConversion = 0x0001, ///< 不自动添加首尾锚点
    XRegularExpression_NonPathWildcardConversion = 0x0002 ///< 按普通通配符处理路径分隔符
} XRegularExpression_WildcardConversionOption;
typedef uint32_t XRegularExpression_WildcardConversionOptions;

/* ============================== 虚函数表定义 ============================== */

XCLASS_DEFINE_BEGING(XRegularExpression)
XCLASS_DEFINE_EXTEND_END(XRegularExpression, XClass)

XCLASS_DEFINE_BEGING(XRegularExpressionMatch)
XCLASS_DEFINE_EXTEND_END(XRegularExpressionMatch, XClass)

XCLASS_DEFINE_BEGING(XRegularExpressionMatchIterator)
XCLASS_DEFINE_EXTEND_END(XRegularExpressionMatchIterator, XClass)

/* ============================== 类定义 ============================== */

/**
 * @brief 正则表达式对象。
 * @details 对应 Qt 6.8 的 QRegularExpression，使用隐式共享数据保存编译结果。
 */
typedef struct XRegularExpression {
    XClass m_class;                         ///< 基类虚函数表，必须位于第一位
    XRegularExpressionData* m_data;        ///< 隐式共享的正则表达式数据
} XRegularExpression;

/**
 * @brief 单次正则匹配结果。
 * @details 保存匹配使用的正则、主题字符串和捕获组偏移。
 */
struct XRegularExpressionMatch {
    XClass m_class;                         ///< 基类虚函数表，必须位于第一位
    XRegularExpression m_regularExpression; ///< 返回结果对应的正则表达式
    XString m_subject;                      ///< 主题字符串副本，保证捕获视图有效
    int64_t* m_capturedOffsets;             ///< 捕获组起止偏移，成对存储
    size_t m_capturedCount;                 ///< 捕获组数量，包含隐式组 0
    XRegularExpression_MatchType m_matchType; ///< 实际匹配类型
    XRegularExpression_MatchOptions m_matchOptions; ///< 实际匹配选项
    bool m_hasMatch;                        ///< 是否完整匹配
    bool m_hasPartialMatch;                 ///< 是否部分匹配
    bool m_isValid;                         ///< 正则和匹配过程是否有效
};

/**
 * @brief 全局匹配迭代器。
 * @details 每次 next() 返回一个新创建的匹配结果，调用者负责释放。
 */
struct XRegularExpressionMatchIterator {
    XClass m_class;                         ///< 基类虚函数表，必须位于第一位
    XRegularExpression m_regularExpression; ///< 迭代使用的正则表达式
    XString m_subject;                      ///< 全局匹配主题字符串副本
    XRegularExpressionMatch* m_next;        ///< 下一个匹配结果
    int64_t m_nextOffset;                   ///< 下一次匹配的起始偏移
    bool m_isValid;                         ///< 迭代器对应正则是否有效
};

/* ============================== 构造与生命周期 ============================== */

/**
 * @brief 初始化 XRegularExpression 类虚函数表。
 * @return 类虚函数表指针，由框架缓存和管理，不得由调用者释放。
 */
XVtable* XRegularExpression_class_init(void);
/**
 * @brief 初始化 XRegularExpressionMatch 类虚函数表。
 * @return 类虚函数表指针，由框架缓存和管理，不得由调用者释放。
 */
XVtable* XRegularExpressionMatch_class_init(void);
/**
 * @brief 初始化 XRegularExpressionMatchIterator 类虚函数表。
 * @return 类虚函数表指针，由框架缓存和管理，不得由调用者释放。
 */
XVtable* XRegularExpressionMatchIterator_class_init(void);

/**
 * @brief 初始化正则表达式对象。
 * @param expression 待初始化的栈对象或已分配对象；不能传入 NULL。
 * @note 初始化前对象不得持有尚未反初始化的资源。
 */
void XRegularExpression_init(XRegularExpression* expression);
/**
 * @brief 初始化匹配结果对象。
 * @param match 待初始化的匹配结果对象；不能传入 NULL。
 */
void XRegularExpressionMatch_init(XRegularExpressionMatch* match);
/**
 * @brief 初始化全局匹配迭代器对象。
 * @param iterator 待初始化的迭代器对象；不能传入 NULL。
 */
void XRegularExpressionMatchIterator_init(XRegularExpressionMatchIterator* iterator);

/**
 * @brief 创建空正则表达式对象。
 * @return 成功返回堆对象指针，调用者必须使用 XRegularExpression_delete_base 释放；失败返回 NULL。
 */
XRegularExpression* XRegularExpression_create(void);
/**
 * @brief 创建正则表达式的共享数据拷贝。
 * @param other 源正则表达式；不能传入 NULL。
 * @return 成功返回新堆对象，调用者负责释放；失败返回 NULL。
 */
XRegularExpression* XRegularExpression_create_copy(const XRegularExpression* other);
/**
 * @brief 创建正则表达式的移动对象。
 * @param other 源对象；成功后源对象进入可反初始化但不再持有正则数据的状态。
 * @return 成功返回新堆对象，调用者负责释放；失败返回 NULL，源对象不变。
 */
XRegularExpression* XRegularExpression_create_move(XRegularExpression* other);
/**
 * @brief 使用 UTF-8 模式创建正则表达式。
 * @param pattern UTF-8 模式字符串；NULL 按空模式处理。
 * @param options 编译选项，可使用多个 XRegularExpression_PatternOption 按位或组合。
 * @return 成功返回新堆对象，调用者负责释放；失败返回 NULL。
 */
XRegularExpression* XRegularExpression_create_utf8(const char* pattern,
                                                     XRegularExpression_PatternOptions options);

/**
 * @brief 创建空匹配结果对象。
 * @return 成功返回堆对象指针，调用者必须使用 XRegularExpressionMatch_delete_base 释放；失败返回 NULL。
 */
XRegularExpressionMatch* XRegularExpressionMatch_create(void);
/**
 * @brief 创建匹配结果的深拷贝。
 * @param other 源匹配结果；不能传入 NULL。
 * @return 成功返回新堆对象，调用者负责释放；失败返回 NULL。
 */
XRegularExpressionMatch* XRegularExpressionMatch_create_copy(const XRegularExpressionMatch* other);
/**
 * @brief 创建匹配结果的移动对象。
 * @param other 源匹配结果；成功后源对象进入可反初始化状态。
 * @return 成功返回新堆对象，调用者负责释放；失败返回 NULL，源对象不变。
 */
XRegularExpressionMatch* XRegularExpressionMatch_create_move(XRegularExpressionMatch* other);

/**
 * @brief 创建空全局匹配迭代器。
 * @return 成功返回堆对象指针，调用者必须使用 XRegularExpressionMatchIterator_delete_base 释放；失败返回 NULL。
 */
XRegularExpressionMatchIterator* XRegularExpressionMatchIterator_create(void);
/**
 * @brief 创建全局匹配迭代器的深拷贝。
 * @param other 源迭代器；不能传入 NULL。
 * @return 成功返回新堆对象，调用者负责释放；失败返回 NULL。
 */
XRegularExpressionMatchIterator* XRegularExpressionMatchIterator_create_copy(const XRegularExpressionMatchIterator* other);
/**
 * @brief 创建全局匹配迭代器的移动对象。
 * @param other 源迭代器；成功后源对象进入可反初始化状态。
 * @return 成功返回新堆对象，调用者负责释放；失败返回 NULL，源对象不变。
 */
XRegularExpressionMatchIterator* XRegularExpressionMatchIterator_create_move(XRegularExpressionMatchIterator* other);

/**
 * @brief 三个正则类的基础生命周期操作宏。
 * @details 宏统一转发到 XClass 虚函数，支持正确调用派生类的 deinit、copy 和 move 重载。
 * @note delete_base 仅用于堆对象；栈对象只调用对应的 deinit_base。
 */
#define XRegularExpression_deinit_base XClass_deinit_base
#define XRegularExpression_delete_base XClass_delete_base
#define XRegularExpression_copy_base XClass_copy_base
#define XRegularExpression_move_base XClass_move_base
#define XRegularExpressionMatch_deinit_base XClass_deinit_base
#define XRegularExpressionMatch_delete_base XClass_delete_base
#define XRegularExpressionMatch_copy_base XClass_copy_base
#define XRegularExpressionMatch_move_base XClass_move_base
#define XRegularExpressionMatchIterator_deinit_base XClass_deinit_base
#define XRegularExpressionMatchIterator_delete_base XClass_delete_base
#define XRegularExpressionMatchIterator_copy_base XClass_copy_base
#define XRegularExpressionMatchIterator_move_base XClass_move_base

/* ============================== QRegularExpression 对齐 API ============================== */

/**
 * @brief 设置 UTF-16 正则模式。
 * @param expression 待修改的正则表达式对象；NULL 时忽略调用。
 * @param pattern UTF-16 模式字符串；NULL 等价于空模式。
 */
void XRegularExpression_setPattern(XRegularExpression* expression, const XString* pattern);
/**
 * @brief 设置 UTF-8 正则模式。
 * @param expression 待修改的正则表达式对象；NULL 时忽略调用。
 * @param pattern UTF-8 模式字符串；NULL 等价于空模式。
 */
void XRegularExpression_setPattern_utf8(XRegularExpression* expression, const char* pattern);
/**
 * @brief 获取正则模式的独立拷贝。
 * @param expression 正则表达式对象。
 * @return 新创建的 XString，调用者负责释放；参数无效时返回 NULL。
 */
XString* XRegularExpression_pattern(const XRegularExpression* expression);
/**
 * @brief 获取正则模式的只读内部引用。
 * @param expression 正则表达式对象。
 * @return 内部 XString 指针，不得释放；对象无效时返回 NULL，指针在对象修改或销毁后失效。
 */
const XString* XRegularExpression_pattern_const(const XRegularExpression* expression);

/**
 * @brief 获取正则表达式编译选项。
 * @param expression 正则表达式对象。
 * @return 当前编译选项组合；参数无效时返回 XRegularExpression_NoPatternOption。
 */
XRegularExpression_PatternOptions XRegularExpression_patternOptions(const XRegularExpression* expression);
/**
 * @brief 设置正则表达式编译选项。
 * @param expression 待修改的正则表达式对象；NULL 时忽略调用。
 * @param options 编译选项组合。
 */
void XRegularExpression_setPatternOptions(XRegularExpression* expression,
                                          XRegularExpression_PatternOptions options);

/**
 * @brief 判断正则模式是否能够成功编译。
 * @param expression 正则表达式对象。
 * @return 合法返回 true，否则返回 false；NULL 返回 false。
 */
bool XRegularExpression_isValid(const XRegularExpression* expression);
/**
 * @brief 获取模式编译错误在 UTF-16 模式中的偏移。
 * @param expression 正则表达式对象。
 * @return 错误偏移；无错误或参数无效时返回 -1。
 */
int64_t XRegularExpression_patternErrorOffset(const XRegularExpression* expression);
/**
 * @brief 获取模式编译错误文本的独立拷贝。
 * @param expression 正则表达式对象。
 * @return 新创建的错误文本，调用者负责释放；参数无效时返回 NULL。
 */
XString* XRegularExpression_errorString(const XRegularExpression* expression);
/**
 * @brief 获取模式编译错误文本的只读内部引用。
 * @param expression 正则表达式对象。
 * @return 内部错误文本指针，不得释放；对象修改或销毁后失效。
 */
const XString* XRegularExpression_errorString_const(const XRegularExpression* expression);
/**
 * @brief 获取捕获组数量，不包含隐式组 0。
 * @param expression 正则表达式对象。
 * @return 捕获组数量；模式无效或参数无效时返回 -1。
 */
int XRegularExpression_captureCount(const XRegularExpression* expression);
/**
 * @brief 获取命名捕获组列表。
 * @param expression 正则表达式对象。
 * @return 按捕获组索引排列的 XStringList，包含索引 0 的空项；调用者负责释放，失败返回 NULL 或空列表。
 */
XStringList* XRegularExpression_namedCaptureGroups(const XRegularExpression* expression);
/**
 * @brief 预编译正则模式并尝试建立 JIT 代码。
 * @param expression 正则表达式对象；NULL 时不执行操作。
 * @note JIT 不可用时仍保留解释执行路径，不影响匹配正确性。
 */
void XRegularExpression_optimize(const XRegularExpression* expression);
/**
 * @brief 比较两个正则表达式的模式和编译选项。
 * @param left 左侧正则表达式。
 * @param right 右侧正则表达式。
 * @return 模式和选项均相同返回 true，否则返回 false；同一 NULL 指针也视为相等。
 */
bool XRegularExpression_equals(const XRegularExpression* left, const XRegularExpression* right);
/**
 * @brief 计算正则表达式的 XHashMap 键哈希值。
 * @param key 指向 XRegularExpression 对象的指针，供 XHashMap 回调使用。
 * @param len 键对象大小；由 XHashMap 传入，函数按对象语义计算并不依赖该值。
 * @return 基于 UTF-16 模式和 PatternOptions 计算的 64 位哈希值；NULL 返回 0。
 * @note 不使用 PCRE2 编译对象地址，因此相同模式和选项的正则对象哈希一致。
 */
uint64_t XRegularExpression_hash(const void* key, size_t len);
/**
 * @brief 比较两个正则表达式，供 XHashMap 的 XCompare 回调使用。
 * @param left 左侧 XRegularExpression 对象指针。
 * @param right 右侧 XRegularExpression 对象指针。
 * @return left 小于 right 返回 -1，相等返回 0，left 大于 right 返回 1。
 * @note 比较字段与 XRegularExpression_equals 一致，为模式字符串和 PatternOptions。
 */
int32_t XRegularExpression_compare(const void* left, const void* right);

/**
 * @brief 使用 UTF-16 字符串执行一次正则匹配。
 * @param expression 正则表达式对象；NULL 或无效模式会生成无效匹配结果。
 * @param subject UTF-16 主题字符串；NULL 按空字符串处理。
 * @param offset 匹配起始 UTF-16 code unit 偏移；负数从主题末尾反向计算。
 * @param matchType 匹配方式。
 * @param matchOptions 匹配选项组合。
 * @return 新创建的匹配结果，调用者负责使用 XRegularExpressionMatch_delete_base 释放；分配失败返回 NULL。
 */
XRegularExpressionMatch* XRegularExpression_match(const XRegularExpression* expression,
                                                    const XString* subject,
                                                    int64_t offset,
                                                    XRegularExpression_MatchType matchType,
                                                    XRegularExpression_MatchOptions matchOptions);
/**
 * @brief 使用非拥有 UTF-16 视图执行一次正则匹配。
 * @param expression 正则表达式对象。
 * @param subjectView UTF-16 主题视图；仅在调用期间需要有效，匹配结果会保存主题副本。
 * @param offset 匹配起始 UTF-16 code unit 偏移；负数从主题末尾反向计算。
 * @param matchType 匹配方式。
 * @param matchOptions 匹配选项组合。
 * @return 新创建的匹配结果，调用者负责释放；分配失败返回 NULL。
 */
XRegularExpressionMatch* XRegularExpression_matchView(const XRegularExpression* expression,
                                                        const XStringView* subjectView,
                                                        int64_t offset,
                                                        XRegularExpression_MatchType matchType,
                                                        XRegularExpression_MatchOptions matchOptions);
/**
 * @brief 使用 UTF-8 字符串执行一次正则匹配。
 * @param expression 正则表达式对象。
 * @param subject UTF-8 主题字符串；NULL 按空字符串处理。
 * @param offset 匹配起始 UTF-16 code unit 偏移，不是 UTF-8 字节偏移。
 * @param matchType 匹配方式。
 * @param matchOptions 匹配选项组合。
 * @return 新创建的匹配结果，调用者负责释放；分配失败返回 NULL。
 */
XRegularExpressionMatch* XRegularExpression_match_utf8(const XRegularExpression* expression,
                                                        const char* subject,
                                                        int64_t offset,
                                                        XRegularExpression_MatchType matchType,
                                                        XRegularExpression_MatchOptions matchOptions);

/**
 * @brief 创建 UTF-16 字符串的全局匹配迭代器。
 * @param expression 正则表达式对象。
 * @param subject UTF-16 主题字符串；NULL 按空字符串处理。
 * @param offset 第一次匹配起始 UTF-16 code unit 偏移。
 * @param matchType 每次匹配使用的匹配方式。
 * @param matchOptions 每次匹配使用的选项组合。
 * @return 新创建的迭代器，调用者负责使用 XRegularExpressionMatchIterator_delete_base 释放；失败返回 NULL。
 */
XRegularExpressionMatchIterator* XRegularExpression_globalMatch(const XRegularExpression* expression,
                                                                  const XString* subject,
                                                                  int64_t offset,
                                                                  XRegularExpression_MatchType matchType,
                                                                  XRegularExpression_MatchOptions matchOptions);
/**
 * @brief 创建 UTF-16 视图的全局匹配迭代器。
 * @param expression 正则表达式对象。
 * @param subjectView 非拥有 UTF-16 主题视图，仅在调用期间需要有效。
 * @param offset 第一次匹配起始 UTF-16 code unit 偏移。
 * @param matchType 每次匹配使用的匹配方式。
 * @param matchOptions 每次匹配使用的选项组合。
 * @return 新创建的迭代器，迭代器内部保存主题副本；失败返回 NULL。
 */
XRegularExpressionMatchIterator* XRegularExpression_globalMatchView(const XRegularExpression* expression,
                                                                      const XStringView* subjectView,
                                                                      int64_t offset,
                                                                      XRegularExpression_MatchType matchType,
                                                                      XRegularExpression_MatchOptions matchOptions);
/**
 * @brief 创建 UTF-8 字符串的全局匹配迭代器。
 * @param expression 正则表达式对象。
 * @param subject UTF-8 主题字符串；NULL 按空字符串处理。
 * @param offset 第一次匹配起始 UTF-16 code unit 偏移，不是字节偏移。
 * @param matchType 每次匹配使用的匹配方式。
 * @param matchOptions 每次匹配使用的选项组合。
 * @return 新创建的迭代器，调用者负责释放；失败返回 NULL。
 */
XRegularExpressionMatchIterator* XRegularExpression_globalMatch_utf8(const XRegularExpression* expression,
                                                                      const char* subject,
                                                                      int64_t offset,
                                                                      XRegularExpression_MatchType matchType,
                                                                      XRegularExpression_MatchOptions matchOptions);

/**
 * @brief 转义正则表达式中的特殊字符。
 * @param string 待转义的 UTF-16 非拥有视图，仅在调用期间需要有效。
 * @return 新创建的转义后 XString，调用者负责释放；参数无效或分配失败时返回 NULL 或空字符串。
 */
XString* XRegularExpression_escape(const XStringView* string);
/**
 * @brief 转义正则表达式中的特殊字符。
 * @param string 待转义的 XString；参数无效时返回 NULL。
 * @return 新创建的转义后 XString，调用者负责释放。
 */
XString* XRegularExpression_escape_2(const XString* string);
/**
 * @brief 将通配符模式转换为正则表达式。
 * @param pattern UTF-16 通配符非拥有视图，仅在调用期间需要有效。
 * @param options 通配符转换选项组合。
 * @return 新创建的正则模式字符串，调用者负责释放；失败返回 NULL。
 */
XString* XRegularExpression_wildcardToRegularExpression(const XStringView* pattern,
                                                         XRegularExpression_WildcardConversionOptions options);
/**
 * @brief 将 XString 通配符模式转换为正则表达式。
 * @param pattern 通配符模式字符串。
 * @param options 通配符转换选项组合。
 * @return 新创建的正则模式字符串，调用者负责释放；参数无效或失败返回 NULL。
 */
XString* XRegularExpression_wildcardToRegularExpression_2(const XString* pattern,
                                                           XRegularExpression_WildcardConversionOptions options);
/**
 * @brief 将通配符模式直接转换为正则表达式对象。
 * @param pattern UTF-16 通配符非拥有视图，仅在调用期间需要有效。
 * @param caseSensitivity 大小写敏感性；不敏感时自动加入忽略大小写选项。
 * @param options 通配符转换选项组合。
 * @return 新创建的正则表达式对象，调用者负责释放；失败返回 NULL。
 */
XRegularExpression* XRegularExpression_fromWildcard(const XStringView* pattern,
                                                     XChar_CaseSensitivity caseSensitivity,
                                                     XRegularExpression_WildcardConversionOptions options);
/**
 * @brief 为模式添加完整匹配锚点。
 * @param expression UTF-16 模式非拥有视图，仅在调用期间需要有效。
 * @return 新创建的 \A(?:expression)\z 模式字符串，调用者负责释放；失败返回 NULL。
 */
XString* XRegularExpression_anchoredPattern(const XStringView* expression);
/**
 * @brief 为 XString 模式添加完整匹配锚点。
 * @param expression 待锚定的模式字符串；NULL 按空模式处理。
 * @return 新创建的锚定模式字符串，调用者负责释放；失败返回 NULL。
 */
XString* XRegularExpression_anchoredPattern_2(const XString* expression);

/* ============================== QRegularExpressionMatch 对齐 API ============================== */

/**
 * @brief 获取匹配结果对应正则表达式的独立拷贝。
 * @param match 匹配结果对象。
 * @return 新创建的正则表达式，调用者负责释放；参数无效时返回 NULL。
 */
XRegularExpression* XRegularExpressionMatch_regularExpression(const XRegularExpressionMatch* match);
/**
 * @brief 获取匹配结果对应正则表达式的只读内部引用。
 * @param match 匹配结果对象。
 * @return 内部正则表达式指针，不得释放；匹配结果销毁后失效。
 */
const XRegularExpression* XRegularExpressionMatch_regularExpression_const(const XRegularExpressionMatch* match);
/**
 * @brief 获取匹配结果使用的匹配方式。
 * @param match 匹配结果对象。
 * @return 匹配方式；参数无效时返回 XRegularExpression_NoMatch。
 */
XRegularExpression_MatchType XRegularExpressionMatch_matchType(const XRegularExpressionMatch* match);
/**
 * @brief 获取匹配结果使用的匹配选项。
 * @param match 匹配结果对象。
 * @return 匹配选项组合；参数无效时返回 XRegularExpression_NoMatchOption。
 */
XRegularExpression_MatchOptions XRegularExpressionMatch_matchOptions(const XRegularExpressionMatch* match);
/**
 * @brief 判断是否存在完整匹配。
 * @param match 匹配结果对象。
 * @return 存在完整匹配返回 true，否则返回 false。
 */
bool XRegularExpressionMatch_hasMatch(const XRegularExpressionMatch* match);
/**
 * @brief 判断是否存在部分匹配。
 * @param match 匹配结果对象。
 * @return 存在部分匹配返回 true，否则返回 false。
 */
bool XRegularExpressionMatch_hasPartialMatch(const XRegularExpressionMatch* match);
/**
 * @brief 判断正则编译和匹配过程是否有效。
 * @param match 匹配结果对象。
 * @return 正则有效且匹配过程完成返回 true；参数无效或编译失败返回 false。
 */
bool XRegularExpressionMatch_isValid(const XRegularExpressionMatch* match);
/**
 * @brief 获取最后一个捕获组索引。
 * @param match 匹配结果对象。
 * @return 最后一个捕获组索引；没有捕获组或参数无效时返回 -1。
 */
int XRegularExpressionMatch_lastCapturedIndex(const XRegularExpressionMatch* match);
/**
 * @brief 判断指定数字捕获组是否成功捕获。
 * @param match 匹配结果对象。
 * @param nth 捕获组索引，0 表示完整匹配文本。
 * @return 捕获组存在且已捕获返回 true，否则返回 false。
 */
bool XRegularExpressionMatch_hasCaptured(const XRegularExpressionMatch* match, int nth);
/**
 * @brief 判断指定命名捕获组是否成功捕获。
 * @param match 匹配结果对象。
 * @param name 捕获组名称，可使用 UTF-8、Latin1 或 UTF-16 视图。
 * @return 捕获组存在且已捕获返回 true，否则返回 false。
 */
bool XRegularExpressionMatch_hasCaptured_2(const XRegularExpressionMatch* match,
                                            const XAnyStringView* name);
/**
 * @brief 获取指定数字捕获组的独立文本。
 * @param match 匹配结果对象。
 * @param nth 捕获组索引，0 表示完整匹配文本。
 * @return 新创建的捕获文本，调用者负责释放；未捕获时返回空 XString，参数无效时返回 NULL。
 */
XString* XRegularExpressionMatch_captured(const XRegularExpressionMatch* match, int nth);
/**
 * @brief 获取指定命名捕获组的独立文本。
 * @param match 匹配结果对象。
 * @param name 捕获组名称，可使用 UTF-8、Latin1 或 UTF-16 视图。
 * @return 新创建的捕获文本，调用者负责释放；未捕获时返回空 XString，参数无效时返回 NULL。
 */
XString* XRegularExpressionMatch_captured_2(const XRegularExpressionMatch* match,
                                             const XAnyStringView* name);
/**
 * @brief 获取指定数字捕获组的非拥有文本视图。
 * @param match 匹配结果对象。
 * @param nth 捕获组索引，0 表示完整匹配文本。
 * @return 指向匹配结果内部主题副本的视图；匹配结果销毁后失效。
 */
XStringView XRegularExpressionMatch_capturedView(const XRegularExpressionMatch* match, int nth);
/**
 * @brief 获取指定命名捕获组的非拥有文本视图。
 * @param match 匹配结果对象。
 * @param name 捕获组名称，可使用 UTF-8、Latin1 或 UTF-16 视图。
 * @return 指向匹配结果内部主题副本的视图；匹配结果销毁后失效。
 */
XStringView XRegularExpressionMatch_capturedView_2(const XRegularExpressionMatch* match,
                                                    const XAnyStringView* name);
/**
 * @brief 获取所有捕获文本列表。
 * @param match 匹配结果对象。
 * @return 新创建的 XStringList，调用者负责释放；列表第 0 项是完整匹配文本，失败返回 NULL。
 */
XStringList* XRegularExpressionMatch_capturedTexts(const XRegularExpressionMatch* match);
/**
 * @brief 获取指定数字捕获组的起始 UTF-16 code unit 偏移。
 * @param match 匹配结果对象。
 * @param nth 捕获组索引。
 * @return 起始偏移；未捕获、索引无效或参数无效时返回 -1。
 */
int64_t XRegularExpressionMatch_capturedStart(const XRegularExpressionMatch* match, int nth);
/**
 * @brief 获取指定命名捕获组的起始 UTF-16 code unit 偏移。
 * @param match 匹配结果对象。
 * @param name 捕获组名称。
 * @return 起始偏移；未捕获、名称无效或参数无效时返回 -1。
 */
int64_t XRegularExpressionMatch_capturedStart_2(const XRegularExpressionMatch* match,
                                                const XAnyStringView* name);
/**
 * @brief 获取指定数字捕获组的 UTF-16 code unit 长度。
 * @param match 匹配结果对象。
 * @param nth 捕获组索引。
 * @return 捕获长度；未捕获、索引无效或参数无效时返回 0。
 */
int64_t XRegularExpressionMatch_capturedLength(const XRegularExpressionMatch* match, int nth);
/**
 * @brief 获取指定命名捕获组的 UTF-16 code unit 长度。
 * @param match 匹配结果对象。
 * @param name 捕获组名称。
 * @return 捕获长度；未捕获、名称无效或参数无效时返回 0。
 */
int64_t XRegularExpressionMatch_capturedLength_2(const XRegularExpressionMatch* match,
                                                 const XAnyStringView* name);
/**
 * @brief 获取指定数字捕获组的结束 UTF-16 code unit 偏移。
 * @param match 匹配结果对象。
 * @param nth 捕获组索引。
 * @return 结束偏移；未捕获、索引无效或参数无效时返回 -1。
 */
int64_t XRegularExpressionMatch_capturedEnd(const XRegularExpressionMatch* match, int nth);
/**
 * @brief 获取指定命名捕获组的结束 UTF-16 code unit 偏移。
 * @param match 匹配结果对象。
 * @param name 捕获组名称。
 * @return 结束偏移；未捕获、名称无效或参数无效时返回 -1。
 */
int64_t XRegularExpressionMatch_capturedEnd_2(const XRegularExpressionMatch* match,
                                               const XAnyStringView* name);

/* ============================== QRegularExpressionMatchIterator 对齐 API ============================== */

/**
 * @brief 判断迭代器对应的正则和主题是否有效。
 * @param iterator 全局匹配迭代器。
 * @return 有效返回 true，否则返回 false；NULL 返回 false。
 */
bool XRegularExpressionMatchIterator_isValid(const XRegularExpressionMatchIterator* iterator);
/**
 * @brief 判断迭代器是否还有未取出的匹配结果。
 * @param iterator 全局匹配迭代器。
 * @return 还有结果返回 true，否则返回 false；NULL 返回 false。
 */
bool XRegularExpressionMatchIterator_hasNext(const XRegularExpressionMatchIterator* iterator);
/**
 * @brief 取出下一个匹配结果并推进迭代器。
 * @param iterator 全局匹配迭代器。
 * @return 新创建的匹配结果，调用者负责释放；没有结果或参数无效时返回 NULL。
 */
XRegularExpressionMatch* XRegularExpressionMatchIterator_next(XRegularExpressionMatchIterator* iterator);
/**
 * @brief 查看下一个匹配结果但不推进迭代器。
 * @param iterator 全局匹配迭代器。
 * @return 新创建的匹配结果拷贝，调用者负责释放；没有结果或参数无效时返回 NULL。
 */
XRegularExpressionMatch* XRegularExpressionMatchIterator_peekNext(const XRegularExpressionMatchIterator* iterator);
/**
 * @brief 获取迭代器对应正则表达式的独立拷贝。
 * @param iterator 全局匹配迭代器。
 * @return 新创建的正则表达式，调用者负责释放；参数无效时返回 NULL。
 */
XRegularExpression* XRegularExpressionMatchIterator_regularExpression(const XRegularExpressionMatchIterator* iterator);
/**
 * @brief 获取迭代器对应正则表达式的只读内部引用。
 * @param iterator 全局匹配迭代器。
 * @return 内部正则表达式指针，不得释放；迭代器销毁后失效。
 */
const XRegularExpression* XRegularExpressionMatchIterator_regularExpression_const(const XRegularExpressionMatchIterator* iterator);
/**
 * @brief 获取迭代器使用的匹配方式。
 * @param iterator 全局匹配迭代器。
 * @return 匹配方式；参数无效或无下一个结果时返回 XRegularExpression_NoMatch。
 */
XRegularExpression_MatchType XRegularExpressionMatchIterator_matchType(const XRegularExpressionMatchIterator* iterator);
/**
 * @brief 获取迭代器使用的匹配选项。
 * @param iterator 全局匹配迭代器。
 * @return 匹配选项组合；参数无效或无下一个结果时返回 XRegularExpression_NoMatchOption。
 */
XRegularExpression_MatchOptions XRegularExpressionMatchIterator_matchOptions(const XRegularExpressionMatchIterator* iterator);

#endif /* XREGULAREXPRESSION_API_H */

#ifdef __cplusplus
}
#endif

#endif /* XREGULAREXPRESSION_H */
