/**
* @file XUtf8StringView.h
* @brief UTF-8 字符串视图头文件（非拥有型只读引用，对标 Qt 6.8 QUtf8StringView）
* @details XUtf8StringView 是一个轻量级的非拥有型（non-owning）UTF-8 字符串只读视图，
*          内部仅包含 {数据指针, 长度} 两个成员，不管理数据生命周期。
*          对标 Qt 6.8 QUtf8StringView，提供完整的只读访问、子视图、查找、比较、
*          数值转换等 API。
* @note XUtf8StringView 是值类型（不是 XClass/XContainer 派生类），
*       在栈上分配和传递，不分配堆内存，不涉及虚函数表。
*/
#include "CXinYueConfig.h"
#if !defined(XUTF8STRINGVIEW_H) && XString_ON
#define XUTF8STRINGVIEW_H
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
typedef struct XByteArrayView XByteArrayView;        ///< 字节数组视图前向声明
typedef uint16_t XChar;                      ///< 16 位 Unicode 字符

/**
* @brief UTF-8 字符串视图结构体（对标 Qt 6.8 QUtf8StringView）
* @details 轻量级非拥有型只读视图，仅包含数据指针和长度。
*          不管理数据生命周期，不分配堆内存，不涉及虚函数表。
*          内部以 UTF-8 编码存储，每个字符占 1~4 字节。
*
*          null view（m_data == NULL, m_size == 0）与 empty view
*          （m_data != NULL, m_size == 0）的区别：
*          - null view: 未关联任何数据，isNull() 返回 true
*          - empty view: 关联了数据但长度为 0，isEmpty() 返回 true
*/
typedef struct XUtf8StringView
{
    const char* m_data;  ///< UTF-8 数据指针（NULL 表示 null view）
    int64_t m_size;      ///< 字节数（0 表示空视图）
} XUtf8StringView;

/* ============================== 迭代器类型 ============================== */

/**
* @brief XUtf8StringView 正向迭代器类型
* @details 实际类型为 const char*，指向视图中的 UTF-8 字节数据
*/
/* ============================== 构造与创建 ============================== */

/**
* @brief 创建默认空视图（null view）
* @details 创建一个空的 XUtf8StringView，m_data 为 NULL，m_size 为 0。
*          等价于 Qt QUtf8StringView() 默认构造函数。
* @return XUtf8StringView 实例（值类型，栈上返回）
*/
XUtf8StringView XUtf8StringView_create(void);

/**
* @brief 从 NULL 终止的 C 字符串创建视图
* @details 自动计算字符串长度（不含终止符 '\0'）。
*          等价于 Qt QUtf8StringView(const char* str)。
* @param str UTF-8 C 字符串指针（NULL 则创建 null view）
* @return XUtf8StringView 实例
*/
XUtf8StringView XUtf8StringView_create_cstr(const char* str);

/**
* @brief 从数据指针和长度创建视图
* @details 创建一个指向指定 UTF-8 内存区域的只读视图，不拷贝数据。
*          等价于 Qt QUtf8StringView(const char* data, qsizetype len)。
* @param data 数据指针（可为 NULL，此时 len 应传 0）
* @param len  字节数（为 0 时创建 empty view）
* @return XUtf8StringView 实例
*/
XUtf8StringView XUtf8StringView_create_data(const char* data, int64_t len);

/**
* @brief 从指针范围 [first, last) 创建视图
* @details 创建一个指向 [first, last) 范围的只读视图。
*          等价于 Qt QUtf8StringView(const char* f, const char* l)。
* @param first 范围起始指针（包含，可为 NULL）
* @param last  范围结束指针（不包含，需 >= first）
* @return XUtf8StringView 实例
*/
XUtf8StringView XUtf8StringView_create_range(const char* first, const char* last);

/**
* @brief 从 XByteArrayView 创建视图
* @details 创建一个指向 XByteArrayView 数据的只读视图。
*          等价于 Qt QUtf8StringView(QByteArrayView str)。
* @param bav XByteArrayView 实例指针（NULL 则创建 null view）
* @return XUtf8StringView 实例
*/
XUtf8StringView XUtf8StringView_create_bytearrayview(const XByteArrayView* bav);

/**
* @brief 从 XStringView 创建视图
* @details 从 UTF-16 视图创建 UTF-8 视图（需运行时转换，暂存于内部缓冲区）。
*          注意：此函数会分配临时内存，与值类型语义略有不同。
*          等价于 Qt QUtf8StringView(QStringView str)。
* @param sv XStringView 实例指针（NULL 则创建 null view）
* @return XUtf8StringView 实例
*/
XUtf8StringView XUtf8StringView_create_stringview(const XStringView* sv);

/* ============================== 基本访问 ============================== */

/**
* @brief 获取数据指针
* @details 返回视图指向的 UTF-8 数据指针。若为 null view，返回 NULL。
*          等价于 Qt QUtf8StringView::data()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return 数据指针（const char* 类型）
*/
const char* XUtf8StringView_data(const XUtf8StringView* self);

/**
* @brief 获取 UTF-8 数据指针（别名）
* @details 等价于 XUtf8StringView_data()。
*          等价于 Qt QUtf8StringView::utf8()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return UTF-8 数据指针（const char* 类型）
*/
const char* XUtf8StringView_utf8(const XUtf8StringView* self);

/**
* @brief 获取常量数据指针（别名）
* @details 等价于 XUtf8StringView_data()。
*          等价于 Qt QUtf8StringView::constData()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return 数据指针（const char* 类型）
*/
const char* XUtf8StringView_constData(const XUtf8StringView* self);

/**
* @brief 获取视图长度（字节数）
* @details 返回视图中的 UTF-8 字节数。
*          等价于 Qt QUtf8StringView::size() / length()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return 字节数
*/
int64_t XUtf8StringView_size(const XUtf8StringView* self);

/**
* @brief 检查视图是否为空
* @details 判断视图长度是否为 0。
*          等价于 Qt QUtf8StringView::empty() / isEmpty()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return 空返回 true，否则返回 false
*/
bool XUtf8StringView_empty(const XUtf8StringView* self);

/**
* @brief 检查视图是否为 null
* @details 判断视图是否未关联任何数据（m_data == NULL）。
*          等价于 Qt QUtf8StringView::isNull()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return null 返回 true，否则返回 false
*/
bool XUtf8StringView_isNull(const XUtf8StringView* self);

/**
* @brief 获取视图长度（别名）
* @details 等价于 XUtf8StringView_size()。
*          等价于 Qt QUtf8StringView::length()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return 字节数
*/
int64_t XUtf8StringView_length(const XUtf8StringView* self);

/* ============================== 元素访问 ============================== */

/**
* @brief 获取指定位置的字节
* @details 返回视图中第 n 个字节的值。不进行边界检查。
*          等价于 Qt QUtf8StringView::operator[](qsizetype n)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param n    索引位置（0 <= n < size）
* @return 第 n 个字节的值，越界返回 0
*/
char XUtf8StringView_at(const XUtf8StringView* self, int64_t n);

/**
* @brief 获取第一个字节
* @details 返回视图中第一个字节的值。视图不能为空。
*          等价于 Qt QUtf8StringView::front()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return 第一个字节的值，空视图返回 0
*/
char XUtf8StringView_front(const XUtf8StringView* self);

/**
* @brief 获取最后一个字节
* @details 返回视图中最后一个字节的值。视图不能为空。
*          等价于 Qt QUtf8StringView::back()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return 最后一个字节的值，空视图返回 0
*/
char XUtf8StringView_back(const XUtf8StringView* self);

/* ============================== 子视图操作 ============================== */

/**
* @brief 获取前 n 个字节的子视图
* @details 返回一个新的 XUtf8StringView，包含前 n 个字节。
*          等价于 Qt QUtf8StringView::first(qsizetype n)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param n    字节数（0 <= n <= size）
* @return 子视图
*/
XUtf8StringView XUtf8StringView_first_n(const XUtf8StringView* self, int64_t n);

/**
* @brief 获取后 n 个字节的子视图
* @details 返回一个新的 XUtf8StringView，包含后 n 个字节。
*          等价于 Qt QUtf8StringView::last(qsizetype n)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param n    字节数（0 <= n <= size）
* @return 子视图
*/
XUtf8StringView XUtf8StringView_last_n(const XUtf8StringView* self, int64_t n);

/**
* @brief 获取从 pos 开始到结束的子视图
* @details 返回一个新的 XUtf8StringView，包含从 pos 到末尾的字节。
*          等价于 Qt QUtf8StringView::sliced(qsizetype pos)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param pos  起始位置（0 <= pos <= size）
* @return 子视图
*/
XUtf8StringView XUtf8StringView_sliced(const XUtf8StringView* self, int64_t pos);

/**
* @brief 获取从 pos 开始长度为 n 的子视图
* @details 返回一个新的 XUtf8StringView，包含从 pos 开始的 n 个字节。
*          等价于 Qt QUtf8StringView::sliced(qsizetype pos, qsizetype n)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param pos  起始位置（0 <= pos <= size）
* @param n    字节数（0 <= n <= size - pos）
* @return 子视图
*/
XUtf8StringView XUtf8StringView_sliced_2(const XUtf8StringView* self, int64_t pos, int64_t n);

/**
* @brief 获取去除后 n 个字节的子视图
* @details 返回一个新的 XUtf8StringView，去除末尾 n 个字节。
*          等价于 Qt QUtf8StringView::chopped(qsizetype n)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param n    去除的字节数（0 <= n <= size）
* @return 子视图
*/
XUtf8StringView XUtf8StringView_chopped(const XUtf8StringView* self, int64_t n);

/**
* @brief 获取左侧 n 个字节的子视图（Qt 兼容别名）
* @details 等价于 XUtf8StringView_first_n()。
*          等价于 Qt QUtf8StringView::left(qsizetype n)。
*/
#define XUtf8StringView_left(self, n) XUtf8StringView_first_n(&(self), n)

/**
* @brief 获取右侧 n 个字节的子视图（Qt 兼容别名）
* @details 等价于 XUtf8StringView_last_n()。
*          等价于 Qt QUtf8StringView::right(qsizetype n)。
*/
#define XUtf8StringView_right(self, n) XUtf8StringView_last_n(&(self), n)

/**
* @brief 获取从 pos 开始长度为 n 的子视图（Qt 兼容别名）
* @details 等价于 XUtf8StringView_sliced_2()。
*          等价于 Qt QUtf8StringView::mid(qsizetype pos, qsizetype n)。
*/
#define XUtf8StringView_mid(self, pos, n) XUtf8StringView_sliced_2(&(self), pos, n)

/* ============================== 原地修改 ============================== */

/**
* @brief 原地截断视图到前 n 个字节
* @details 修改当前视图，仅保留前 n 个字节。
*          等价于 Qt QUtf8StringView::truncate(qsizetype n)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param n    新的字节数（0 <= n <= size）
*/
void XUtf8StringView_truncate(XUtf8StringView* self, int64_t n);

/**
* @brief 原地去除末尾 n 个字节
* @details 修改当前视图，去除末尾 n 个字节。
*          等价于 Qt QUtf8StringView::chop(qsizetype n)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param n    去除的字节数（0 <= n <= size）
*/
void XUtf8StringView_chop(XUtf8StringView* self, int64_t n);

/* ============================== 查找操作 ============================== */

/**
* @brief 查找字符第一次出现的位置
* @details 从起始位置开始查找指定字符。
*          等价于 Qt QUtf8StringView::indexOf(QChar ch, qsizetype from)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ch  待查找的字符（ASCII 范围）
* @param from 起始搜索位置（0 <= from < size）
* @param cs  大小写敏感性（0=不区分，1=区分）
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XUtf8StringView_indexOf_char(const XUtf8StringView* self, char ch, int64_t from, int cs);

/**
* @brief 查找子视图第一次出现的位置
* @details 从起始位置开始查找指定子视图。
*          等价于 Qt QUtf8StringView::indexOf(QStringView str, qsizetype from)。
* @param self  XUtf8StringView 实例指针（不可为 NULL）
* @param sub  待查找的子视图
* @param from 起始搜索位置
* @param cs   大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XUtf8StringView_indexOf(const XUtf8StringView* self, const XUtf8StringView* sub, int64_t from, int cs);

/**
* @brief 查找字符最后一次出现的位置
* @details 从末尾开始向前查找指定字符。
*          等价于 Qt QUtf8StringView::lastIndexOf(QChar ch, qsizetype from)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ch  待查找的字符（ASCII 范围）
* @param from 起始搜索位置（-1 表示从末尾开始）
* @param cs  大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XUtf8StringView_lastIndexOf_char(const XUtf8StringView* self, char ch, int64_t from, int cs);

/**
* @brief 查找子视图最后一次出现的位置
* @details 从末尾开始向前查找指定子视图。
*          等价于 Qt QUtf8StringView::lastIndexOf(QStringView str, qsizetype from)。
* @param self  XUtf8StringView 实例指针（不可为 NULL）
* @param sub  待查找的子视图
* @param from 起始搜索位置（-1 表示从末尾开始）
* @param cs   大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XUtf8StringView_lastIndexOf(const XUtf8StringView* self, const XUtf8StringView* sub, int64_t from, int cs);

/**
* @brief 检查是否包含指定字符
* @details 判断视图中是否包含指定字符。
*          等价于 Qt QUtf8StringView::contains(QChar ch)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ch  待检查的字符（ASCII 范围）
* @param cs  大小写敏感性
* @return 包含返回 true，否则返回 false
*/
bool XUtf8StringView_contains_char(const XUtf8StringView* self, char ch, int cs);

/**
* @brief 检查是否包含指定子视图
* @details 判断视图中是否包含指定子视图。
*          等价于 Qt QUtf8StringView::contains(QStringView str)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param sub 待检查的子视图
* @param cs  大小写敏感性
* @return 包含返回 true，否则返回 false
*/
bool XUtf8StringView_contains(const XUtf8StringView* self, const XUtf8StringView* sub, int cs);

/**
* @brief 统计指定字符出现的次数
* @details 统计视图中指定字符出现的次数。
*          等价于 Qt QUtf8StringView::count(QChar ch)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ch  待统计的字符（ASCII 范围）
* @param cs  大小写敏感性
* @return 出现次数
*/
int64_t XUtf8StringView_count_char(const XUtf8StringView* self, char ch, int cs);

/**
* @brief 统计指定子视图出现的次数
* @details 统计视图中指定子视图出现的次数。
*          等价于 Qt QUtf8StringView::count(QStringView str)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param sub 待统计的子视图
* @param cs  大小写敏感性
* @return 出现次数
*/
int64_t XUtf8StringView_count(const XUtf8StringView* self, const XUtf8StringView* sub, int cs);

/**
* @brief 检查是否以指定字符开头
* @details 判断视图是否以指定字符开头。
*          等价于 Qt QUtf8StringView::startsWith(QChar ch)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ch  待检查的字符（ASCII 范围）
* @param cs  大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XUtf8StringView_startsWith_char(const XUtf8StringView* self, char ch, int cs);

/**
* @brief 检查是否以指定子视图开头
* @details 判断视图是否以指定子视图开头。
*          等价于 Qt QUtf8StringView::startsWith(QStringView str)。
* @param self   XUtf8StringView 实例指针（不可为 NULL）
* @param prefix 前缀子视图
* @param cs     大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XUtf8StringView_startsWith(const XUtf8StringView* self, const XUtf8StringView* prefix, int cs);

/**
* @brief 检查是否以指定字符结尾
* @details 判断视图是否以指定字符结尾。
*          等价于 Qt QUtf8StringView::endsWith(QChar ch)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ch  待检查的字符（ASCII 范围）
* @param cs  大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XUtf8StringView_endsWith_char(const XUtf8StringView* self, char ch, int cs);

/**
* @brief 检查是否以指定子视图结尾
* @details 判断视图是否以指定子视图结尾。
*          等价于 Qt QUtf8StringView::endsWith(QStringView str)。
* @param self   XUtf8StringView 实例指针（不可为 NULL）
* @param suffix 后缀子视图
* @param cs     大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XUtf8StringView_endsWith(const XUtf8StringView* self, const XUtf8StringView* suffix, int cs);

/* ============================== 比较操作 ============================== */

/**
* @brief 比较两个视图是否相等
* @details 比较两个 XUtf8StringView 的内容是否完全相同（长度和字节均相等）。
*          等价于 Qt QUtf8StringView::operator==。
* @param self  XUtf8StringView 实例指针（不可为 NULL）
* @param other 另一个 XUtf8StringView 实例指针（不可为 NULL）
* @param cs    大小写敏感性（0=不区分，1=区分）
* @return 相等返回 true，否则返回 false
*/
bool XUtf8StringView_equal(const XUtf8StringView* self, const XUtf8StringView* other, int cs);

/**
* @brief 比较两个视图的字典序
* @details 按字典序比较两个 XUtf8StringView 的内容。
*          等价于 Qt QUtf8StringView::compare(QUtf8StringView str)。
* @param self  XUtf8StringView 实例指针（不可为 NULL）
* @param other 另一个 XUtf8StringView 实例指针（不可为 NULL）
* @param cs    大小写敏感性
* @return 小于返回负值，等于返回 0，大于返回正值
*/
int XUtf8StringView_compare(const XUtf8StringView* self, const XUtf8StringView* other, int cs);

/* ============================== 修剪操作 ============================== */

/**
* @brief 返回去除首尾空白字符后的子视图
* @details 返回一个新的 XUtf8StringView，去除首尾的空白字符。
*          等价于 Qt QUtf8StringView::trimmed()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return 修剪后的 XUtf8StringView 实例
*/
XUtf8StringView XUtf8StringView_trimmed(const XUtf8StringView* self);

/* ============================== 编码转换 ============================== */

/**
* @brief 转换为 XString（深拷贝）
* @details 创建一个新的 XString，内容为视图中的 UTF-8 数据。
*          等价于 Qt QUtf8StringView::toString()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return 新的 XString 指针，失败返回 NULL
*/
XString* XUtf8StringView_toString(const XUtf8StringView* self);

/* ============================== 编码检测 ============================== */

/**
* @brief 检查是否为有效的 UTF-8 编码
* @details 检查视图中的字节序列是否为有效的 UTF-8 编码。
*          等价于 Qt QUtf8StringView::isValidUtf8()。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @return true 为有效 UTF-8，false 为无效
*/
bool XUtf8StringView_isValidUtf8(const XUtf8StringView* self);

/* ============================== 数值转换 ============================== */

/**
* @brief 转换为 short 整数
* @details 将视图内容解析为 short 类型整数。
*          等价于 Qt QUtf8StringView::toShort(bool* ok, int base)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（short 类型）
*/
short XUtf8StringView_toShort(const XUtf8StringView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned short 整数
* @details 将视图内容解析为 unsigned short 类型整数。
*          等价于 Qt QUtf8StringView::toUShort(bool* ok, int base)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（unsigned short 类型）
*/
unsigned short XUtf8StringView_toUShort(const XUtf8StringView* self, bool* ok, int base);

/**
* @brief 转换为 int 整数
* @details 将视图内容解析为 int 类型整数。
*          等价于 Qt QUtf8StringView::toInt(bool* ok, int base)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（int 类型）
*/
int XUtf8StringView_toInt(const XUtf8StringView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned int 整数
* @details 将视图内容解析为 unsigned int 类型整数。
*          等价于 Qt QUtf8StringView::toUInt(bool* ok, int base)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（unsigned int 类型）
*/
unsigned int XUtf8StringView_toUInt(const XUtf8StringView* self, bool* ok, int base);

/**
* @brief 转换为 long 整数
* @details 将视图内容解析为 long 类型整数。
*          等价于 Qt QUtf8StringView::toLong(bool* ok, int base)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（long 类型）
*/
long XUtf8StringView_toLong(const XUtf8StringView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned long 整数
* @details 将视图内容解析为 unsigned long 类型整数。
*          等价于 Qt QUtf8StringView::toULong(bool* ok, int base)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（unsigned long 类型）
*/
unsigned long XUtf8StringView_toULong(const XUtf8StringView* self, bool* ok, int base);

/**
* @brief 转换为 long long 整数
* @details 将视图内容解析为 int64_t 类型整数。
*          等价于 Qt QUtf8StringView::toLongLong(bool* ok, int base)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（int64_t 类型）
*/
int64_t XUtf8StringView_toLongLong(const XUtf8StringView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned long long 整数
* @details 将视图内容解析为 uint64_t 类型整数。
*          等价于 Qt QUtf8StringView::toULongLong(bool* ok, int base)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（uint64_t 类型）
*/
uint64_t XUtf8StringView_toULongLong(const XUtf8StringView* self, bool* ok, int base);

/**
* @brief 转换为 float 浮点数
* @details 将视图内容解析为 float 类型浮点数。
*          等价于 Qt QUtf8StringView::toFloat(bool* ok)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @return 解析结果（float 类型）
*/
float XUtf8StringView_toFloat(const XUtf8StringView* self, bool* ok);

/**
* @brief 转换为 double 浮点数
* @details 将视图内容解析为 double 类型浮点数。
*          等价于 Qt QUtf8StringView::toDouble(bool* ok)。
* @param self XUtf8StringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @return 解析结果（double 类型）
*/
double XUtf8StringView_toDouble(const XUtf8StringView* self, bool* ok);

#include "XUtf8StringView_iterator.h"    ///< XUtf8StringView 正向迭代器
#include "XUtf8StringView_reverse_iterator.h"  ///< XUtf8StringView 反向迭代器

#ifdef __cplusplus
}
#endif
#endif // !XUTF8STRINGVIEW_H
