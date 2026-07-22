/**
* @file XAnyStringView.h
* @brief 任意编码字符串视图头文件（非拥有型只读引用，对标 Qt 6.8 QAnyStringView）
* @details XAnyStringView 是一个轻量级的非拥有型（non-owning）字符串只读视图，
*          可以容纳 UTF-8、Latin-1、UTF-16 三种编码的字符串数据。
*          内部使用 tagged union 存储：{数据指针, 编码类型标记 + 长度}。
*          不管理数据生命周期。
*          对标 Qt 6.8 QAnyStringView，提供完整的只读访问、子视图、比较等 API。
* @note XAnyStringView 是值类型（不是 XClass/XContainer 派生类），
*       在栈上分配和传递，不分配堆内存，不涉及虚函数表。
*/
#include "CXinYueConfig.h"
#if !defined(XANYSTRINGVIEW_H) && XString_ON
#define XANYSTRINGVIEW_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================== 前向声明 ============================== */
typedef struct XString XString;              ///< 字符串容器前向声明
typedef struct XStringView XStringView;      ///< UTF-16 字符串视图前向声明
typedef struct XLatin1StringView XLatin1StringView;  ///< Latin-1 字符串视图前向声明
typedef struct XUtf8StringView XUtf8StringView;      ///< UTF-8 字符串视图前向声明
typedef struct XByteArrayView XByteArrayView;        ///< 字节数组视图前向声明
typedef uint16_t XChar;                      ///< 16 位 Unicode 字符

/* ============================== 编码类型标记 ============================== */

/**
* @brief XAnyStringView 编码类型枚举
* @details 标识视图中字符串数据的编码类型。
*          对标 Qt QAnyStringView 的 Tag 枚举。
*/
typedef enum XAnyStringView_Encoding {
    XAnyStringView_Utf8   = 0,  ///< UTF-8 编码
    XAnyStringView_Latin1 = 1,  ///< Latin-1（ISO 8859-1）编码
    XAnyStringView_Utf16  = 2   ///< UTF-16 编码
} XAnyStringView_Encoding;

/* ============================== 内部标记位常量 ============================== */

/**
* @def XAnyStringView_SizeMask
* @brief 从 m_size_and_tag 中提取长度的掩码
* @details 低 (sizeof(size_t)*8 - 2) 位用于存储长度
*/
#define XAnyStringView_SizeMask  ((~(size_t)0) / 4)

/**
* @def XAnyStringView_TypeMask
* @brief 从 m_size_and_tag 中提取编码类型的掩码
* @details 高 2 位用于存储编码类型标记
*/
#define XAnyStringView_TypeMask  (~XAnyStringView_SizeMask)

/**
* @def XAnyStringView_Latin1Flag
* @brief Latin-1 编码标记位值
*/
#define XAnyStringView_Latin1Flag (XAnyStringView_SizeMask + ((size_t)1))

/**
* @def XAnyStringView_Utf16Flag
* @brief UTF-16 编码标记位值
*/
#define XAnyStringView_Utf16Flag  (XAnyStringView_Latin1Flag << 1)

/**
* @brief 任意编码字符串视图结构体（对标 Qt 6.8 QAnyStringView）
* @details 轻量级非拥有型只读视图，使用 tagged union 存储。
*          可以容纳 UTF-8、Latin-1、UTF-16 三种编码的字符串数据。
*          内部包含 {数据指针, 编码类型标记 + 长度} 两个成员。
*          不管理数据生命周期，不分配堆内存，不涉及虚函数表。
*
*          编码类型存储在 m_size_and_tag 的高 2 位：
*          - 00: UTF-8
*          - 01: Latin-1
*          - 10: UTF-16
*
*          null view（m_data == NULL, size() == 0）与 empty view
*          （m_data != NULL, size() == 0）的区别：
*          - null view: 未关联任何数据，isNull() 返回 true
*          - empty view: 关联了数据但长度为 0，isEmpty() 返回 true
*/
typedef struct XAnyStringView
{
    union {
        const void* m_data;          ///< 通用数据指针（NULL 表示 null view）
        const char* m_data_utf8;     ///< UTF-8 数据指针
        const char* m_data_latin1;   ///< Latin-1 数据指针
        const XChar* m_data_utf16;   ///< UTF-16 数据指针
    };
    size_t m_size_and_tag;  ///< 长度（低 N-2 位）和编码类型标记（高 2 位）
} XAnyStringView;

/* ============================== 构造与创建 ============================== */

/**
* @brief 创建默认空视图（null view）
* @details 创建一个空的 XAnyStringView，m_data 为 NULL，size 为 0。
*          等价于 Qt QAnyStringView() 默认构造函数。
* @return XAnyStringView 实例（值类型，栈上返回）
*/
XAnyStringView XAnyStringView_create(void);

/**
* @brief 从 XStringView（UTF-16）创建视图
* @details 创建一个指向 XStringView 数据的只读视图。
*          等价于 Qt QAnyStringView(QStringView v)。
* @param sv XStringView 实例指针（NULL 则创建 null view）
* @return XAnyStringView 实例
*/
XAnyStringView XAnyStringView_create_stringview(const XStringView* sv);

/**
* @brief 从 XLatin1StringView（Latin-1）创建视图
* @details 创建一个指向 XLatin1StringView 数据的只读视图。
*          等价于 Qt QAnyStringView(QLatin1StringView v)。
* @param lv XLatin1StringView 实例指针（NULL 则创建 null view）
* @return XAnyStringView 实例
*/
XAnyStringView XAnyStringView_create_latin1(const XLatin1StringView* lv);

/**
* @brief 从 XUtf8StringView（UTF-8）创建视图
* @details 创建一个指向 XUtf8StringView 数据的只读视图。
*          等价于 Qt QAnyStringView(QUtf8StringView v)。
* @param uv XUtf8StringView 实例指针（NULL 则创建 null view）
* @return XAnyStringView 实例
*/
XAnyStringView XAnyStringView_create_utf8view(const XUtf8StringView* uv);

/**
* @brief 从 XByteArrayView 创建视图
* @details 创建一个指向 XByteArrayView 数据的只读视图（标记为 UTF-8）。
*          等价于 Qt QAnyStringView(QByteArrayView v)。
* @param bav XByteArrayView 实例指针（NULL 则创建 null view）
* @return XAnyStringView 实例
*/
XAnyStringView XAnyStringView_create_bytearrayview(const XByteArrayView* bav);

/**
* @brief 从 XString 创建视图
* @details 创建一个指向 XString 内部 UTF-16 数据的只读视图。
*          等价于 Qt QAnyStringView(const QString& str)。
* @param str XString 实例指针（NULL 则创建 null view）
* @return XAnyStringView 实例
*/
XAnyStringView XAnyStringView_create_string(const XString* str);

/**
* @brief 从 UTF-8 C 字符串创建视图
* @details 自动计算字符串长度（不含终止符 '\0'）。
*          等价于 Qt QAnyStringView(const char* str)。
* @param str UTF-8 C 字符串指针（NULL 则创建 null view）
* @return XAnyStringView 实例
*/
XAnyStringView XAnyStringView_create_cstr(const char* str);

/**
* @brief 从 UTF-8 数据和长度创建视图
* @details 创建一个指向指定 UTF-8 内存区域的只读视图。
*          等价于 Qt QAnyStringView(const char* data, qsizetype len)。
* @param data UTF-8 数据指针（可为 NULL，此时 len 应传 0）
* @param len  字节数
* @return XAnyStringView 实例
*/
XAnyStringView XAnyStringView_create_utf8(const char* data, int64_t len);

/**
* @brief 从 UTF-16 数据和长度创建视图
* @details 创建一个指向指定 UTF-16 内存区域的只读视图。
*          等价于 Qt QAnyStringView(const char16_t* data, qsizetype len)。
* @param data UTF-16 数据指针（可为 NULL，此时 len 应传 0）
* @param len  字符数
* @return XAnyStringView 实例
*/
XAnyStringView XAnyStringView_create_utf16(const XChar* data, int64_t len);

/**
* @brief 从 Latin-1 数据和长度创建视图
* @details 创建一个指向指定 Latin-1 内存区域的只读视图。
*          等价于 Qt QAnyStringView(const char* data, qsizetype len, Latin1Tag)。
* @param data Latin-1 数据指针（可为 NULL，此时 len 应传 0）
* @param len  字符数
* @return XAnyStringView 实例
*/
XAnyStringView XAnyStringView_create_latin1_data(const char* data, int64_t len);

/* ============================== 基本访问 ============================== */

/**
* @brief 获取数据指针
* @details 返回视图指向的原始数据指针。类型为 const void*。
*          等价于 Qt QAnyStringView::data()。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 数据指针（const void* 类型）
*/
const void* XAnyStringView_data(const XAnyStringView* self);

/**
* @brief 获取视图长度（字符数/字节数）
* @details 返回视图中的字符数。UTF-8 和 Latin-1 返回字节数，UTF-16 返回字符数。
*          等价于 Qt QAnyStringView::size() / length()。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 长度
*/
int64_t XAnyStringView_size(const XAnyStringView* self);

/**
* @brief 检查视图是否为空
* @details 判断视图长度是否为 0。
*          等价于 Qt QAnyStringView::empty() / isEmpty()。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 空返回 true，否则返回 false
*/
bool XAnyStringView_empty(const XAnyStringView* self);

/**
* @brief 检查视图是否为 null
* @details 判断视图是否未关联任何数据（m_data == NULL）。
*          等价于 Qt QAnyStringView::isNull()。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return null 返回 true，否则返回 false
*/
bool XAnyStringView_isNull(const XAnyStringView* self);

/**
* @brief 获取视图长度（别名）
* @details 等价于 XAnyStringView_size()。
*          等价于 Qt QAnyStringView::length()。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 长度
*/
int64_t XAnyStringView_length(const XAnyStringView* self);

/**
* @brief 获取编码类型
* @details 返回视图中字符串数据的编码类型。
*          等价于 Qt QAnyStringView 内部 tag() 方法。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 编码类型枚举值
*/
XAnyStringView_Encoding XAnyStringView_encoding(const XAnyStringView* self);

/**
* @brief 检查是否为 UTF-16 编码
* @details 判断视图是否为 UTF-16 编码。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return UTF-16 返回 true，否则返回 false
*/
bool XAnyStringView_isUtf16(const XAnyStringView* self);

/**
* @brief 检查是否为 UTF-8 编码
* @details 判断视图是否为 UTF-8 编码。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return UTF-8 返回 true，否则返回 false
*/
bool XAnyStringView_isUtf8(const XAnyStringView* self);

/**
* @brief 检查是否为 Latin-1 编码
* @details 判断视图是否为 Latin-1 编码。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return Latin-1 返回 true，否则返回 false
*/
bool XAnyStringView_isLatin1(const XAnyStringView* self);

/**
* @brief 获取每个字符/字节的大小
* @details UTF-8 和 Latin-1 返回 1，UTF-16 返回 2。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 字符大小（字节数）
*/
size_t XAnyStringView_charSize(const XAnyStringView* self);

/**
* @brief 获取总字节数
* @details 返回视图占用的总字节数。
*          等价于 Qt QAnyStringView::size_bytes()。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 总字节数
*/
int64_t XAnyStringView_size_bytes(const XAnyStringView* self);

/* ============================== 元素访问 ============================== */

/**
* @brief 获取第一个字符
* @details 返回视图中第一个字符的 XChar 表示。
*          等价于 Qt QAnyStringView::front()。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 第一个字符，空视图返回 0
*/
XChar XAnyStringView_front(const XAnyStringView* self);

/**
* @brief 获取最后一个字符
* @details 返回视图中最后一个字符的 XChar 表示。
*          等价于 Qt QAnyStringView::back()。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 最后一个字符，空视图返回 0
*/
XChar XAnyStringView_back(const XAnyStringView* self);

/* ============================== 子视图操作 ============================== */

/**
* @brief 获取前 n 个字符的子视图
* @details 返回一个新的 XAnyStringView，包含前 n 个字符。
*          等价于 Qt QAnyStringView::first(qsizetype n)。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @param n    字符数（0 <= n <= size）
* @return 子视图
*/
XAnyStringView XAnyStringView_first_n(const XAnyStringView* self, int64_t n);

/**
* @brief 获取后 n 个字符的子视图
* @details 返回一个新的 XAnyStringView，包含后 n 个字符。
*          等价于 Qt QAnyStringView::last(qsizetype n)。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @param n    字符数（0 <= n <= size）
* @return 子视图
*/
XAnyStringView XAnyStringView_last_n(const XAnyStringView* self, int64_t n);

/**
* @brief 获取从 pos 开始到结束的子视图
* @details 返回一个新的 XAnyStringView，包含从 pos 到末尾的字符。
*          等价于 Qt QAnyStringView::sliced(qsizetype pos)。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @param pos  起始位置（0 <= pos <= size）
* @return 子视图
*/
XAnyStringView XAnyStringView_sliced(const XAnyStringView* self, int64_t pos);

/**
* @brief 获取从 pos 开始长度为 n 的子视图
* @details 返回一个新的 XAnyStringView，包含从 pos 开始的 n 个字符。
*          等价于 Qt QAnyStringView::sliced(qsizetype pos, qsizetype n)。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @param pos  起始位置（0 <= pos <= size）
* @param n    字符数（0 <= n <= size - pos）
* @return 子视图
*/
XAnyStringView XAnyStringView_sliced_2(const XAnyStringView* self, int64_t pos, int64_t n);

/**
* @brief 获取去除后 n 个字符的子视图
* @details 返回一个新的 XAnyStringView，去除末尾 n 个字符。
*          等价于 Qt QAnyStringView::chopped(qsizetype n)。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @param n    去除的字符数（0 <= n <= size）
* @return 子视图
*/
XAnyStringView XAnyStringView_chopped(const XAnyStringView* self, int64_t n);

/**
* @brief 获取左侧 n 个字符的子视图（Qt 兼容别名）
* @details 等价于 XAnyStringView_first_n()。
*          等价于 Qt QAnyStringView::left(qsizetype n)。
*/
#define XAnyStringView_left(self, n) XAnyStringView_first_n(&(self), n)

/**
* @brief 获取右侧 n 个字符的子视图（Qt 兼容别名）
* @details 等价于 XAnyStringView_last_n()。
*          等价于 Qt QAnyStringView::right(qsizetype n)。
*/
#define XAnyStringView_right(self, n) XAnyStringView_last_n(&(self), n)

/**
* @brief 获取从 pos 开始长度为 n 的子视图（Qt 兼容别名）
* @details 等价于 XAnyStringView_sliced_2()。
*          等价于 Qt QAnyStringView::mid(qsizetype pos, qsizetype n)。
*/
#define XAnyStringView_mid(self, pos, n) XAnyStringView_sliced_2(&(self), pos, n)

/* ============================== 原地修改 ============================== */

/**
* @brief 原地截断视图到前 n 个字符
* @details 修改当前视图，仅保留前 n 个字符。
*          等价于 Qt QAnyStringView::truncate(qsizetype n)。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @param n    新的字符数（0 <= n <= size）
*/
void XAnyStringView_truncate(XAnyStringView* self, int64_t n);

/**
* @brief 原地去除末尾 n 个字符
* @details 修改当前视图，去除末尾 n 个字符。
*          等价于 Qt QAnyStringView::chop(qsizetype n)。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @param n    去除的字符数（0 <= n <= size）
*/
void XAnyStringView_chop(XAnyStringView* self, int64_t n);

/* ============================== 比较操作 ============================== */

/**
* @brief 比较两个视图是否相等
* @details 比较两个 XAnyStringView 的内容是否完全相同（长度和字符均相等）。
*          等价于 Qt QAnyStringView::equal()。
* @param self  XAnyStringView 实例指针（不可为 NULL）
* @param other 另一个 XAnyStringView 实例指针（不可为 NULL）
* @return 相等返回 true，否则返回 false
*/
bool XAnyStringView_equal(const XAnyStringView* self, const XAnyStringView* other);

/**
* @brief 比较两个视图的字典序
* @details 按字典序比较两个 XAnyStringView 的内容。
*          等价于 Qt QAnyStringView::compare()。
* @param self  XAnyStringView 实例指针（不可为 NULL）
* @param other 另一个 XAnyStringView 实例指针（不可为 NULL）
* @param cs    大小写敏感性（0=不区分，1=区分）
* @return 小于返回负值，等于返回 0，大于返回正值
*/
int XAnyStringView_compare(const XAnyStringView* self, const XAnyStringView* other, int cs);

/* ============================== 编码转换 ============================== */

/**
* @brief 转换为 XString（深拷贝）
* @details 创建一个新的 XString，内容为视图中的数据（统一转换为 UTF-16）。
*          等价于 Qt QAnyStringView::toString()。
* @param self XAnyStringView 实例指针（不可为 NULL）
* @return 新的 XString 指针，失败返回 NULL
*/
XString* XAnyStringView_toString(const XAnyStringView* self);

#ifdef __cplusplus
}
#endif
#include "XAnyStringView_iterator/XAnyStringView_iterator.h"    ///< XAnyStringView 正向迭代器
#include "XAnyStringView_iterator/XAnyStringView_reverse_iterator.h"  ///< XAnyStringView 反向迭代器

#endif // !XANYSTRINGVIEW_H
