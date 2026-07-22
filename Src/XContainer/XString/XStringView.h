/**
* @file XStringView.h
* @brief 字符串视图头文件（非拥有型只读引用，对标 Qt 6.8 QStringView）
* @details XStringView 是一个轻量级的非拥有型（non-owning）UTF-16 字符串只读视图，
*          内部仅包含 {数据指针, 长度} 两个成员，不管理数据生命周期。
*          对标 Qt 6.8 QStringView，提供完整的只读访问、子视图、查找、比较、
*          数值转换等 API。
* @note XStringView 是值类型（不是 XClass/XContainer 派生类），
*       在栈上分配和传递，不分配堆内存，不涉及虚函数表。
*/
#include "CXinYueConfig.h"
#if !defined(XSTRINGVIEW_H) && XString_ON
#define XSTRINGVIEW_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================== 前向声明 ============================== */
typedef struct XString XString;              ///< 字符串容器前向声明
typedef uint16_t XChar;                      ///< 16 位 Unicode 字符（与 XChar.h 一致）

/**
* @brief 字符串视图结构体（对标 Qt 6.8 QStringView）
* @details 轻量级非拥有型只读视图，仅包含数据指针和长度。
*          不管理数据生命周期，不分配堆内存，不涉及虚函数表。
*          内部以 UTF-16 编码（XChar/uint16_t 数组）存储。
*
*          null view（m_data == NULL, m_size == 0）与 empty view
*          （m_data != NULL, m_size == 0）的区别：
*          - null view: 未关联任何数据，isNull() 返回 true
*          - empty view: 关联了数据但长度为 0，isEmpty() 返回 true
*/
typedef struct XStringView
{
    const XChar* m_data;  ///< UTF-16 数据指针（NULL 表示 null view）
    int64_t m_size;       ///< 字符数（0 表示空视图）
} XStringView;

/* ============================== 迭代器类型 ============================== */

/**
* @brief XStringView 正向迭代器类型
* @details 实际类型为 const XChar*，指向视图中的 UTF-16 字符数据
*/
/* ============================== 构造与创建 ============================== */

/**
* @brief 创建默认空视图（null view）
* @details 创建一个空的 XStringView，m_data 为 NULL，m_size 为 0。
*          等价于 Qt QStringView() 默认构造函数。
* @return XStringView 实例（值类型，栈上返回）
*/
XStringView XStringView_create(void);

/**
* @brief 从 XChar 数据指针和长度创建视图
* @details 创建一个指向指定 UTF-16 内存区域的只读视图，不拷贝数据。
*          等价于 Qt QStringView(const QChar* data, qsizetype len)。
* @param data XChar 数据指针（可为 NULL，此时 len 应传 0）
* @param len  字符数（为 0 时创建 empty view）
* @return XStringView 实例
*/
XStringView XStringView_create_data(const XChar* data, int64_t len);

/**
* @brief 从指针范围 [first, last) 创建视图
* @details 创建一个指向 [first, last) 范围的只读视图。
*          等价于 Qt QStringView(const QChar* first, const QChar* last)。
* @param first 范围起始指针（包含，可为 NULL）
* @param last  范围结束指针（不包含，需 >= first）
* @return XStringView 实例
*/
XStringView XStringView_create_range(const XChar* first, const XChar* last);

/**
* @brief 从 NULL 终止的 XChar 数组创建视图
* @details 自动计算字符数（不含终止符 \0）。
*          等价于 Qt QStringView(const QChar* data) 从 C 风格 QChar 数组构造。
* @param str XChar 数组指针（NULL 则创建 null view）
* @return XStringView 实例
*/
XStringView XStringView_create_cstr(const XChar* str);

/**
* @brief 从 uint16_t 数据指针和长度创建视图
* @details 创建一个指向指定 UTF-16 内存区域的只读视图。
*          等价于 Qt QStringView(const char16_t* data, qsizetype len)。
* @param data uint16_t 数据指针（可为 NULL）
* @param len  字符数
* @return XStringView 实例
*/
XStringView XStringView_create_utf16(const uint16_t* data, int64_t len);

/**
* @brief 从 XString 创建视图
* @details 创建一个指向 XString 内部 UTF-16 数据的只读视图。
*          等价于 Qt QStringView(const QString&) 从 QString 构造。
* @param str XString 实例指针（NULL 则创建 null view）
* @return XStringView 实例
*/
XStringView XStringView_create_string(const XString* str);

/* ============================== 基本访问 ============================== */

/**
* @brief 获取数据指针
* @details 返回视图指向的 UTF-16 数据指针。若为 null view，返回 NULL。
*          等价于 Qt QStringView::data()。
* @param self XStringView 实例指针（不可为 NULL）
* @return 数据指针（const XChar* 类型）
*/
const XChar* XStringView_data(const XStringView* self);

/**
* @brief 获取常量数据指针（别名）
* @details 等价于 XStringView_data()。
*          等价于 Qt QStringView::constData()。
* @param self XStringView 实例指针（不可为 NULL）
* @return 常量数据指针
*/
const XChar* XStringView_constData(const XStringView* self);

/**
* @brief 获取 UTF-16 数据指针
* @details 返回视图指向的 UTF-16 数据指针（uint16_t 类型）。
*          等价于 Qt QStringView::utf16()。
* @param self XStringView 实例指针（不可为 NULL）
* @return uint16_t 数据指针
*/
const uint16_t* XStringView_utf16(const XStringView* self);

/**
* @brief 获取视图长度（字符数）
* @details 返回视图中的字符数量。
*          等价于 Qt QStringView::size() / length()。
* @param self XStringView 实例指针（不可为 NULL）
* @return 字符数（int64_t 类型）
*/
int64_t XStringView_size(const XStringView* self);

/**
* @brief 获取视图长度（别名）
* @details 等价于 XStringView_size()。
*          等价于 Qt QStringView::length()。
*/
#define XStringView_length(self) XStringView_size(self)

/**
* @brief 判断视图是否为空
* @details 当长度为 0 时返回 true（包括 null view 和 empty view）。
*          等价于 Qt QStringView::empty() / isEmpty()。
* @param self XStringView 实例指针（不可为 NULL）
* @return true 为空，false 为非空
*/
bool XStringView_empty(const XStringView* self);

/**
* @brief 判断视图是否为 null
* @details 当数据指针为 NULL 时返回 true。
*          等价于 Qt QStringView::isNull()。
* @param self XStringView 实例指针（不可为 NULL）
* @return true 为 null view，false 为非 null
*/
bool XStringView_isNull(const XStringView* self);

/**
* @brief 获取最大允许大小
* @details 返回视图允许的最大字符数。
*          等价于 Qt QStringView::maxSize()。
* @return 最大字符数
*/
int64_t XStringView_maxSize(void);

/* ============================== 元素访问 ============================== */

/**
* @brief 获取指定位置的字符
* @details 返回视图中索引为 n 的字符。不进行边界检查。
*          等价于 Qt QStringView::at(qsizetype n)。
* @param self XStringView 实例指针（不可为 NULL）
* @param n 字符索引（从 0 开始）
* @return 对应位置的 XChar 字符
*/
XChar XStringView_at(const XStringView* self, int64_t n);

/**
* @brief 获取第一个字符
* @details 返回视图中的第一个字符。视图不能为空。
*          等价于 Qt QStringView::front() / first()。
* @param self XStringView 实例指针（不可为 NULL）
* @return 第一个 XChar 字符
*/
XChar XStringView_front(const XStringView* self);

/**
* @brief 获取最后一个字符
* @details 返回视图中的最后一个字符。视图不能为空。
*          等价于 Qt QStringView::back() / last()。
* @param self XStringView 实例指针（不可为 NULL）
* @return 最后一个 XChar 字符
*/
XChar XStringView_back(const XStringView* self);

/* ============================== 子视图操作 ============================== */

/**
* @brief 获取前 n 个字符的子视图
* @details 返回包含前 n 个字符的子视图。n 超出范围时截断到 size()。
*          等价于 Qt QStringView::first(qsizetype n)。
* @param self XStringView 实例指针（不可为 NULL）
* @param n 字符数
* @return 子视图 XStringView 实例
*/
XStringView XStringView_first_n(const XStringView* self, int64_t n);

/**
* @brief 获取后 n 个字符的子视图
* @details 返回包含后 n 个字符的子视图。n 超出范围时截断到 size()。
*          等价于 Qt QStringView::last(qsizetype n)。
* @param self XStringView 实例指针（不可为 NULL）
* @param n 字符数
* @return 子视图 XStringView 实例
*/
XStringView XStringView_last_n(const XStringView* self, int64_t n);

/**
* @brief 获取从 pos 开始到末尾的子视图
* @details 返回从 pos 开始到末尾的子视图。
*          等价于 Qt QStringView::sliced(qsizetype pos)。
* @param self XStringView 实例指针（不可为 NULL）
* @param pos 起始位置（必须 <= size()）
* @return 子视图 XStringView 实例
*/
XStringView XStringView_sliced(const XStringView* self, int64_t pos);

/**
* @brief 获取从 pos 开始长度为 n 的子视图
* @details 返回从 pos 开始长度为 n 的子视图。
*          等价于 Qt QStringView::sliced(qsizetype pos, qsizetype n)。
* @param self XStringView 实例指针（不可为 NULL）
* @param pos 起始位置（必须 <= size()）
* @param n   子视图长度（pos + n 必须 <= size()）
* @return 子视图 XStringView 实例
*/
XStringView XStringView_sliced_2(const XStringView* self, int64_t pos, int64_t n);

/**
* @brief 获取从 pos 开始长度为 n 的子视图（别名）
* @details 等价于 XStringView_sliced_2()。
*          等价于 Qt QStringView::mid(qsizetype pos, qsizetype n)。
*/
#define XStringView_mid(self, pos, n) XStringView_sliced_2(self, pos, n)

/**
* @brief 获取去掉末尾 n 个字符的子视图
* @details 返回去掉末尾 n 个字符后的子视图。
*          等价于 Qt QStringView::chopped(qsizetype n)。
* @param self XStringView 实例指针（不可为 NULL）
* @param n 要去掉的末尾字符数（必须 <= size()）
* @return 子视图 XStringView 实例
*/
XStringView XStringView_chopped(const XStringView* self, int64_t n);

/**
* @brief 获取前 n 个字符的子视图（别名）
* @details 等价于 XStringView_first_n()。
*          等价于 Qt QStringView::left(qsizetype n)。
*/
#define XStringView_left(self, n) XStringView_first_n(self, n)

/**
* @brief 获取后 n 个字符的子视图（别名）
* @details 等价于 XStringView_last_n()。
*          等价于 Qt QStringView::right(qsizetype n)。
*/
#define XStringView_right(self, n) XStringView_last_n(self, n)

/* ============================== 原地修改操作 ============================== */

/**
* @brief 原地截断到前 n 个字符
* @details 修改当前视图，仅保留前 n 个字符。
*          等价于 Qt QStringView::truncate(qsizetype n)。
* @param self XStringView 实例指针（不可为 NULL）
* @param n 截断后的字符数（必须 <= size()）
*/
void XStringView_truncate(XStringView* self, int64_t n);

/**
* @brief 原地去掉末尾 n 个字符
* @details 修改当前视图，去掉末尾 n 个字符。
*          等价于 Qt QStringView::chop(qsizetype n)。
* @param self XStringView 实例指针（不可为 NULL）
* @param n 要去掉的末尾字符数（必须 <= size()）
*/
void XStringView_chop(XStringView* self, int64_t n);

/* ============================== 查找操作 ============================== */

/**
* @brief 查找指定字符第一次出现的位置
* @details 从 from 位置开始查找字符 ch 第一次出现的位置。
*          等价于 Qt QStringView::indexOf(QChar ch, qsizetype from)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ch  待查找的 XChar 字符
* @param from 起始查找位置（负数表示从末尾偏移）
* @param cs  大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XStringView_indexOf_char(const XStringView* self, XChar ch, int64_t from, int cs);

/**
* @brief 查找子视图第一次出现的位置
* @details 从 from 位置开始查找子视图 substr 第一次出现的位置。
*          等价于 Qt QStringView::indexOf(QStringView str, qsizetype from)。
* @param self   XStringView 实例指针（不可为 NULL）
* @param substr 待查找的子视图
* @param from   起始查找位置（负数表示从末尾偏移）
* @param cs     大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XStringView_indexOf(const XStringView* self, const XStringView* substr, int64_t from, int cs);

/**
* @brief 查找指定字符最后一次出现的位置
* @details 从末尾向前查找字符 ch 最后一次出现的位置。
*          等价于 Qt QStringView::lastIndexOf(QChar ch, qsizetype from)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ch  待查找的 XChar 字符
* @param from 起始查找位置（负数表示从末尾偏移，-1 表示从末尾开始）
* @param cs  大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XStringView_lastIndexOf_char(const XStringView* self, XChar ch, int64_t from, int cs);

/**
* @brief 查找子视图最后一次出现的位置
* @details 从末尾向前查找子视图 substr 最后一次出现的位置。
*          等价于 Qt QStringView::lastIndexOf(QStringView str, qsizetype from)。
* @param self   XStringView 实例指针（不可为 NULL）
* @param substr 待查找的子视图
* @param from   起始查找位置（-1 表示从末尾开始）
* @param cs     大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XStringView_lastIndexOf(const XStringView* self, const XStringView* substr, int64_t from, int cs);

/**
* @brief 判断是否包含指定字符
* @details 检查视图中是否包含字符 ch。
*          等价于 Qt QStringView::contains(QChar ch)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ch  待查找的 XChar 字符
* @param cs  大小写敏感性
* @return 包含返回 true，否则返回 false
*/
bool XStringView_contains_char(const XStringView* self, XChar ch, int cs);

/**
* @brief 判断是否包含子视图
* @details 检查视图中是否包含子视图 substr。
*          等价于 Qt QStringView::contains(QStringView str)。
* @param self   XStringView 实例指针（不可为 NULL）
* @param substr 待查找的子视图
* @param cs     大小写敏感性
* @return 包含返回 true，否则返回 false
*/
bool XStringView_contains(const XStringView* self, const XStringView* substr, int cs);

/**
* @brief 统计指定字符出现的次数
* @details 统计视图中字符 ch 出现的次数。
*          等价于 Qt QStringView::count(QChar ch)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ch  待统计的 XChar 字符
* @param cs  大小写敏感性
* @return 出现次数
*/
int64_t XStringView_count_char(const XStringView* self, XChar ch, int cs);

/**
* @brief 统计子视图出现的次数
* @details 统计视图中子视图 substr 出现的次数。
*          等价于 Qt QStringView::count(QStringView str)。
* @param self   XStringView 实例指针（不可为 NULL）
* @param substr 待统计的子视图
* @param cs     大小写敏感性
* @return 出现次数
*/
int64_t XStringView_count(const XStringView* self, const XStringView* substr, int cs);

/**
* @brief 判断是否以指定字符开头
* @details 检查视图是否以字符 ch 开头。
*          等价于 Qt QStringView::startsWith(QChar ch)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ch  待检查的 XChar 字符
* @param cs  大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XStringView_startsWith_char(const XStringView* self, XChar ch, int cs);

/**
* @brief 判断是否以指定子视图开头
* @details 检查视图是否以子视图 prefix 开头。
*          等价于 Qt QStringView::startsWith(QStringView str)。
* @param self   XStringView 实例指针（不可为 NULL）
* @param prefix 待检查的前缀子视图
* @param cs     大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XStringView_startsWith(const XStringView* self, const XStringView* prefix, int cs);

/**
* @brief 判断是否以指定字符结尾
* @details 检查视图是否以字符 ch 结尾。
*          等价于 Qt QStringView::endsWith(QChar ch)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ch  待检查的 XChar 字符
* @param cs  大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XStringView_endsWith_char(const XStringView* self, XChar ch, int cs);

/**
* @brief 判断是否以指定子视图结尾
* @details 检查视图是否以子视图 suffix 结尾。
*          等价于 Qt QStringView::endsWith(QStringView str)。
* @param self   XStringView 实例指针（不可为 NULL）
* @param suffix 待检查的后缀子视图
* @param cs     大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XStringView_endsWith(const XStringView* self, const XStringView* suffix, int cs);

/* ============================== 比较操作 ============================== */

/**
* @brief 比较两个视图是否相等
* @details 比较两个 XStringView 的内容是否完全相同（长度和字符均相等）。
*          等价于 Qt QStringView::operator==。
* @param self  XStringView 实例指针（不可为 NULL）
* @param other 另一个 XStringView 实例指针（不可为 NULL）
* @param cs    大小写敏感性
* @return 相等返回 true，否则返回 false
*/
bool XStringView_equal(const XStringView* self, const XStringView* other, int cs);

/**
* @brief 比较两个视图的字典序
* @details 按字典序比较两个 XStringView 的内容。
*          等价于 Qt QStringView::compare(QStringView str)。
* @param self  XStringView 实例指针（不可为 NULL）
* @param other 另一个 XStringView 实例指针（不可为 NULL）
* @param cs    大小写敏感性
* @return 小于返回负值，等于返回 0，大于返回正值
*/
int XStringView_compare(const XStringView* self, const XStringView* other, int cs);

/* ============================== 修剪操作 ============================== */

/**
* @brief 返回去除首尾空白字符后的子视图
* @details 返回一个新的 XStringView，去除首尾的空白字符。
*          等价于 Qt QStringView::trimmed()。
* @param self XStringView 实例指针（不可为 NULL）
* @return 修剪后的 XStringView 实例
*/
XStringView XStringView_trimmed(const XStringView* self);

/* ============================== 编码转换 ============================== */

/**
* @brief 转换为 XString（深拷贝）
* @details 创建一个新的 XString，内容为视图中的 UTF-16 数据。
*          等价于 Qt QStringView::toString()。
* @param self XStringView 实例指针（不可为 NULL）
* @return 新的 XString 指针，失败返回 NULL
*/
XString* XStringView_toString(const XStringView* self);

/* ============================== 数值转换 ============================== */

/**
* @brief 转换为 int 整数
* @details 将视图内容解析为 int 类型整数。
*          等价于 Qt QStringView::toInt(bool* ok, int base)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（int 类型）
*/
/**
* @brief 转换为 short 整数
* @details 将视图内容解析为 short 类型整数。
*          等价于 Qt QStringView::toShort(bool* ok, int base)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（short 类型）
*/
short XStringView_toShort(const XStringView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned short 整数
* @details 将视图内容解析为 unsigned short 类型整数。
*          等价于 Qt QStringView::toUShort(bool* ok, int base)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（unsigned short 类型）
*/
unsigned short XStringView_toUShort(const XStringView* self, bool* ok, int base);

int XStringView_toInt(const XStringView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned int 整数
* @details 将视图内容解析为 unsigned int 类型整数。
*          等价于 Qt QStringView::toUInt(bool* ok, int base)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（unsigned int 类型）
*/
unsigned int XStringView_toUInt(const XStringView* self, bool* ok, int base);

/**
* @brief 转换为 long 整数
* @details 将视图内容解析为 long 类型整数。
*          等价于 Qt QStringView::toLong(bool* ok, int base)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（long 类型）
*/
long XStringView_toLong(const XStringView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned long 整数
* @details 将视图内容解析为 unsigned long 类型整数。
*          等价于 Qt QStringView::toULong(bool* ok, int base)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（unsigned long 类型）
*/
unsigned long XStringView_toULong(const XStringView* self, bool* ok, int base);

/**
* @brief 转换为 long long 整数
* @details 将视图内容解析为 int64_t 类型整数。
*          等价于 Qt QStringView::toLongLong(bool* ok, int base)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（int64_t 类型）
*/
int64_t XStringView_toLongLong(const XStringView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned long long 整数
* @details 将视图内容解析为 uint64_t 类型整数。
*          等价于 Qt QStringView::toULongLong(bool* ok, int base)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（uint64_t 类型）
*/
uint64_t XStringView_toULongLong(const XStringView* self, bool* ok, int base);

/**
* @brief 转换为 float 浮点数
* @details 将视图内容解析为 float 类型浮点数。
*          等价于 Qt QStringView::toFloat(bool* ok)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @return 解析结果（float 类型）
*/
float XStringView_toFloat(const XStringView* self, bool* ok);

/**
* @brief 转换为 double 浮点数
* @details 将视图内容解析为 double 类型浮点数。
*          等价于 Qt QStringView::toDouble(bool* ok)。
* @param self XStringView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @return 解析结果（double 类型）
*/
double XStringView_toDouble(const XStringView* self, bool* ok);

#include "XStringView_iterator/XStringView_iterator.h"    ///< XStringView 正向迭代器
#include "XStringView_iterator/XStringView_reverse_iterator.h"  ///< XStringView 反向迭代器

#ifdef __cplusplus
}
#endif
#endif // !XSTRINGVIEW_H
