/**
* @file XLatin1StringView.h
* @brief Latin-1 字符串视图头文件（非拥有型只读引用，对标 Qt 6.8 QLatin1StringView）
* @details XLatin1StringView 是一个轻量级的非拥有型（non-owning）Latin-1 字符串只读视图，
*          内部仅包含 {数据指针, 长度} 两个成员，不管理数据生命周期。
*          对标 Qt 6.8 QLatin1StringView，提供完整的只读访问、子视图、查找、比较等 API。
* @note XLatin1StringView 是值类型（不是 XClass/XContainer 派生类），
*       在栈上分配和传递，不分配堆内存，不涉及虚函数表。
*/
#include "CXinYueConfig.h"
#if !defined(XLATIN1STRINGVIEW_H) && XString_ON
#define XLATIN1STRINGVIEW_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================== 前向声明 ============================== */
typedef struct XByteArrayView XByteArrayView;  ///< 字节数组视图前向声明
typedef struct XStringView XStringView;        ///< 字符串视图前向声明
typedef struct XString XString;                ///< 字符串容器前向声明
typedef uint16_t XChar;                        ///< 16 位 Unicode 字符

/**
* @brief Latin-1 字符串视图结构体（对标 Qt 6.8 QLatin1StringView）
* @details 轻量级非拥有型只读视图，仅包含数据指针和长度。
*          不管理数据生命周期，不分配堆内存，不涉及虚函数表。
*          内部以 Latin-1（ISO 8859-1）编码存储，每个字符占 1 字节。
*
*          null view（m_data == NULL, m_size == 0）与 empty view
*          （m_data != NULL, m_size == 0）的区别：
*          - null view: 未关联任何数据，isNull() 返回 true
*          - empty view: 关联了数据但长度为 0，isEmpty() 返回 true
*/
typedef struct XLatin1StringView
{
    const char* m_data;  ///< Latin-1 数据指针（NULL 表示 null view）
    int64_t m_size;      ///< 字符数（0 表示空视图）
} XLatin1StringView;

/* ============================== 迭代器类型 ============================== */

/**
* @brief XLatin1StringView 正向迭代器类型
* @details 实际类型为 const char*，指向视图中的 Latin-1 字符数据
*/
/* ============================== 构造与创建 ============================== */

/**
* @brief 创建默认空视图（null view）
* @details 创建一个空的 XLatin1StringView，m_data 为 NULL，m_size 为 0。
*          等价于 Qt QLatin1StringView() 默认构造函数。
* @return XLatin1StringView 实例（值类型，栈上返回）
*/
XLatin1StringView XLatin1StringView_create(void);

/**
* @brief 从 NULL 终止的 C 字符串创建视图
* @details 自动计算字符串长度（不含终止符 '\0'）。
*          等价于 Qt QLatin1StringView(const char* s)。
* @param str C 字符串指针（NULL 则创建 null view）
* @return XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_create_cstr(const char* str);

/**
* @brief 从数据指针和长度创建视图
* @details 创建一个指向指定 Latin-1 内存区域的只读视图，不拷贝数据。
*          等价于 Qt QLatin1StringView(const char* s, qsizetype sz)。
* @param data 数据指针（可为 NULL，此时 len 应传 0）
* @param len  字符数（为 0 时创建 empty view）
* @return XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_create_data(const char* data, int64_t len);

/**
* @brief 从指针范围 [first, last) 创建视图
* @details 创建一个指向 [first, last) 范围的只读视图。
*          等价于 Qt QLatin1StringView(const char* f, const char* l)。
* @param first 范围起始指针（包含，可为 NULL）
* @param last  范围结束指针（不包含，需 >= first）
* @return XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_create_range(const char* first, const char* last);

/**
* @brief 从 XByteArrayView 创建视图
* @details 创建一个指向 XByteArrayView 数据的只读视图。
*          等价于 Qt QLatin1StringView(QByteArrayView s)。
* @param bav XByteArrayView 实例指针（NULL 则创建 null view）
* @return XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_create_bytearrayview(const XByteArrayView* bav);

/* ============================== 基本访问 ============================== */

/**
* @brief 获取 Latin-1 数据指针
* @details 返回视图指向的原始 Latin-1 数据指针。
*          等价于 Qt QLatin1StringView::latin1()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return Latin-1 数据指针（const char* 类型）
*/
const char* XLatin1StringView_latin1(const XLatin1StringView* self);

/**
* @brief 获取数据指针
* @details 返回视图指向的数据指针。若为 null view，返回 NULL。
*          等价于 Qt QLatin1StringView::data()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return 数据指针（const char* 类型）
*/
const char* XLatin1StringView_data(const XLatin1StringView* self);

/**
* @brief 获取常量数据指针
* @details 等价于 XLatin1StringView_data()。
*          等价于 Qt QLatin1StringView::constData()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return 常量数据指针
*/
const char* XLatin1StringView_constData(const XLatin1StringView* self);

/**
* @brief 获取视图长度（字符数）
* @details 返回视图中的字符数量。
*          等价于 Qt QLatin1StringView::size() / length()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return 字符数（int64_t 类型）
*/
int64_t XLatin1StringView_size(const XLatin1StringView* self);

/**
* @brief 获取视图长度（别名）
* @details 等价于 XLatin1StringView_size()。
*          等价于 Qt QLatin1StringView::length()。
*/
#define XLatin1StringView_length(self) XLatin1StringView_size(self)

/**
* @brief 判断视图是否为空
* @details 当长度为 0 时返回 true（包括 null view 和 empty view）。
*          等价于 Qt QLatin1StringView::empty() / isEmpty()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return true 为空，false 为非空
*/
bool XLatin1StringView_empty(const XLatin1StringView* self);

/**
* @brief 判断视图是否为 null
* @details 当数据指针为 NULL 时返回 true。
*          等价于 Qt QLatin1StringView::isNull()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return true 为 null view，false 为非 null
*/
bool XLatin1StringView_isNull(const XLatin1StringView* self);

/* ============================== 元素访问 ============================== */

/**
* @brief 获取指定位置的字符
* @details 返回视图中索引为 n 的字符。不进行边界检查。
*          等价于 Qt QLatin1StringView::at(qsizetype i)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param n 字符索引（从 0 开始）
* @return 对应位置的字符（char 类型）
*/
char XLatin1StringView_at(const XLatin1StringView* self, int64_t n);

/**
* @brief 获取第一个字符
* @details 返回视图中的第一个字符。视图不能为空。
*          等价于 Qt QLatin1StringView::front() / first()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return 第一个字符（char 类型）
*/
char XLatin1StringView_front(const XLatin1StringView* self);

/**
* @brief 获取最后一个字符
* @details 返回视图中的最后一个字符。视图不能为空。
*          等价于 Qt QLatin1StringView::back() / last()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return 最后一个字符（char 类型）
*/
char XLatin1StringView_back(const XLatin1StringView* self);

/* ============================== 子视图操作 ============================== */

/**
* @brief 获取前 n 个字符的子视图
* @details 返回包含前 n 个字符的子视图。n 超出范围时截断到 size()。
*          等价于 Qt QLatin1StringView::first(qsizetype n)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param n 字符数
* @return 子视图 XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_first_n(const XLatin1StringView* self, int64_t n);

/**
* @brief 获取后 n 个字符的子视图
* @details 返回包含后 n 个字符的子视图。n 超出范围时截断到 size()。
*          等价于 Qt QLatin1StringView::last(qsizetype n)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param n 字符数
* @return 子视图 XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_last_n(const XLatin1StringView* self, int64_t n);

/**
* @brief 获取从 pos 开始到末尾的子视图
* @details 返回从 pos 开始到末尾的子视图。
*          等价于 Qt QLatin1StringView::sliced(qsizetype pos)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param pos 起始位置（必须 <= size()）
* @return 子视图 XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_sliced(const XLatin1StringView* self, int64_t pos);

/**
* @brief 获取从 pos 开始长度为 n 的子视图
* @details 返回从 pos 开始长度为 n 的子视图。
*          等价于 Qt QLatin1StringView::sliced(qsizetype pos, qsizetype n)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param pos 起始位置（必须 <= size()）
* @param n   子视图长度（pos + n 必须 <= size()）
* @return 子视图 XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_sliced_2(const XLatin1StringView* self, int64_t pos, int64_t n);

/**
* @brief 获取从 pos 开始长度为 n 的子视图（别名）
* @details 等价于 XLatin1StringView_sliced_2()。
*          等价于 Qt QLatin1StringView::mid(qsizetype pos, qsizetype n)。
*/
#define XLatin1StringView_mid(self, pos, n) XLatin1StringView_sliced_2(self, pos, n)

/**
* @brief 获取去掉末尾 n 个字符的子视图
* @details 返回去掉末尾 n 个字符后的子视图。
*          等价于 Qt QLatin1StringView::chopped(qsizetype n)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param n 要去掉的末尾字符数（必须 <= size()）
* @return 子视图 XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_chopped(const XLatin1StringView* self, int64_t n);

/**
* @brief 获取前 n 个字符的子视图（别名）
* @details 等价于 XLatin1StringView_first_n()。
*          等价于 Qt QLatin1StringView::left(qsizetype n)。
*/
#define XLatin1StringView_left(self, n) XLatin1StringView_first_n(self, n)

/**
* @brief 获取后 n 个字符的子视图（别名）
* @details 等价于 XLatin1StringView_last_n()。
*          等价于 Qt QLatin1StringView::right(qsizetype n)。
*/
#define XLatin1StringView_right(self, n) XLatin1StringView_last_n(self, n)

/* ============================== 原地修改操作 ============================== */

/**
* @brief 原地截断到前 n 个字符
* @details 修改当前视图，仅保留前 n 个字符。
*          等价于 Qt QLatin1StringView::truncate(qsizetype n)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param n 截断后的字符数（必须 <= size()）
*/
void XLatin1StringView_truncate(XLatin1StringView* self, int64_t n);

/**
* @brief 原地去掉末尾 n 个字符
* @details 修改当前视图，去掉末尾 n 个字符。
*          等价于 Qt QLatin1StringView::chop(qsizetype n)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param n 要去掉的末尾字符数（必须 <= size()）
*/
void XLatin1StringView_chop(XLatin1StringView* self, int64_t n);

/* ============================== 查找操作 ============================== */

/**
* @brief 查找指定字符第一次出现的位置
* @details 从 from 位置开始查找字符 ch 第一次出现的位置。
*          等价于 Qt QLatin1StringView::indexOf(QChar ch, qsizetype from)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param ch  待查找的字符（char 类型）
* @param from 起始查找位置（负数表示从末尾偏移）
* @param cs  大小写敏感性（0=不区分，1=区分）
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XLatin1StringView_indexOf_char(const XLatin1StringView* self, char ch, int64_t from, int cs);

/**
* @brief 查找子视图第一次出现的位置
* @details 从 from 位置开始查找子视图 substr 第一次出现的位置。
*          等价于 Qt QLatin1StringView::indexOf(QLatin1StringView str, qsizetype from)。
* @param self   XLatin1StringView 实例指针（不可为 NULL）
* @param substr 待查找的子视图
* @param from   起始查找位置（负数表示从末尾偏移）
* @param cs     大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XLatin1StringView_indexOf(const XLatin1StringView* self, const XLatin1StringView* substr, int64_t from, int cs);

/**
* @brief 查找指定字符最后一次出现的位置
* @details 从末尾向前查找字符 ch 最后一次出现的位置。
*          等价于 Qt QLatin1StringView::lastIndexOf(QChar ch, qsizetype from)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param ch  待查找的字符（char 类型）
* @param from 起始查找位置（-1 表示从末尾开始）
* @param cs  大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XLatin1StringView_lastIndexOf_char(const XLatin1StringView* self, char ch, int64_t from, int cs);

/**
* @brief 查找子视图最后一次出现的位置
* @details 从末尾向前查找子视图 substr 最后一次出现的位置。
*          等价于 Qt QLatin1StringView::lastIndexOf(QLatin1StringView str, qsizetype from)。
* @param self   XLatin1StringView 实例指针（不可为 NULL）
* @param substr 待查找的子视图
* @param from   起始查找位置（-1 表示从末尾开始）
* @param cs     大小写敏感性
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XLatin1StringView_lastIndexOf(const XLatin1StringView* self, const XLatin1StringView* substr, int64_t from, int cs);

/**
* @brief 判断是否包含指定字符
* @details 检查视图中是否包含字符 ch。
*          等价于 Qt QLatin1StringView::contains(QChar ch)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param ch  待查找的字符（char 类型）
* @param cs  大小写敏感性
* @return 包含返回 true，否则返回 false
*/
bool XLatin1StringView_contains_char(const XLatin1StringView* self, char ch, int cs);

/**
* @brief 判断是否包含子视图
* @details 检查视图中是否包含子视图 substr。
*          等价于 Qt QLatin1StringView::contains(QLatin1StringView str)。
* @param self   XLatin1StringView 实例指针（不可为 NULL）
* @param substr 待查找的子视图
* @param cs     大小写敏感性
* @return 包含返回 true，否则返回 false
*/
bool XLatin1StringView_contains(const XLatin1StringView* self, const XLatin1StringView* substr, int cs);

/**
* @brief 统计指定字符出现的次数
* @details 统计视图中字符 ch 出现的次数。
*          等价于 Qt QLatin1StringView::count(QChar ch)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param ch  待统计的字符（char 类型）
* @param cs  大小写敏感性
* @return 出现次数
*/
int64_t XLatin1StringView_count_char(const XLatin1StringView* self, char ch, int cs);

/**
* @brief 统计子视图出现的次数
* @details 统计视图中子视图 substr 出现的次数。
*          等价于 Qt QLatin1StringView::count(QLatin1StringView str)。
* @param self   XLatin1StringView 实例指针（不可为 NULL）
* @param substr 待统计的子视图
* @param cs     大小写敏感性
* @return 出现次数
*/
int64_t XLatin1StringView_count(const XLatin1StringView* self, const XLatin1StringView* substr, int cs);

/**
* @brief 判断是否以指定字符开头
* @details 检查视图是否以字符 ch 开头。
*          等价于 Qt QLatin1StringView::startsWith(QChar ch)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param ch  待检查的字符（char 类型）
* @param cs  大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XLatin1StringView_startsWith_char(const XLatin1StringView* self, char ch, int cs);

/**
* @brief 判断是否以指定子视图开头
* @details 检查视图是否以子视图 prefix 开头。
*          等价于 Qt QLatin1StringView::startsWith(QLatin1StringView str)。
* @param self   XLatin1StringView 实例指针（不可为 NULL）
* @param prefix 待检查的前缀子视图
* @param cs     大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XLatin1StringView_startsWith(const XLatin1StringView* self, const XLatin1StringView* prefix, int cs);

/**
* @brief 判断是否以指定字符结尾
* @details 检查视图是否以字符 ch 结尾。
*          等价于 Qt QLatin1StringView::endsWith(QChar ch)。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param ch  待检查的字符（char 类型）
* @param cs  大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XLatin1StringView_endsWith_char(const XLatin1StringView* self, char ch, int cs);

/**
* @brief 判断是否以指定子视图结尾
* @details 检查视图是否以子视图 suffix 结尾。
*          等价于 Qt QLatin1StringView::endsWith(QLatin1StringView str)。
* @param self   XLatin1StringView 实例指针（不可为 NULL）
* @param suffix 待检查的后缀子视图
* @param cs     大小写敏感性
* @return 是返回 true，否则返回 false
*/
bool XLatin1StringView_endsWith(const XLatin1StringView* self, const XLatin1StringView* suffix, int cs);

/* ============================== 比较操作 ============================== */

/**
* @brief 比较两个视图是否相等
* @details 比较两个 XLatin1StringView 的内容是否完全相同（长度和字符均相等）。
*          等价于 Qt QLatin1StringView::operator==。
* @param self  XLatin1StringView 实例指针（不可为 NULL）
* @param other 另一个 XLatin1StringView 实例指针（不可为 NULL）
* @param cs    大小写敏感性（0=不区分，1=区分）
* @return 相等返回 true，否则返回 false
*/
bool XLatin1StringView_equal(const XLatin1StringView* self, const XLatin1StringView* other, int cs);

/**
* @brief 比较两个视图的字典序
* @details 按字典序比较两个 XLatin1StringView 的内容。
*          等价于 Qt QLatin1StringView::compare(QLatin1StringView str)。
* @param self  XLatin1StringView 实例指针（不可为 NULL）
* @param other 另一个 XLatin1StringView 实例指针（不可为 NULL）
* @param cs    大小写敏感性
* @return 小于返回负值，等于返回 0，大于返回正值
*/
int XLatin1StringView_compare(const XLatin1StringView* self, const XLatin1StringView* other, int cs);

/* ============================== 修剪操作 ============================== */

/**
* @brief 返回去除首尾空白字符后的子视图
* @details 返回一个新的 XLatin1StringView，去除首尾的空白字符。
*          等价于 Qt QLatin1StringView::trimmed()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return 修剪后的 XLatin1StringView 实例
*/
XLatin1StringView XLatin1StringView_trimmed(const XLatin1StringView* self);

/* ============================== 编码转换 ============================== */

/**
* @brief 转换为 XString（深拷贝）
* @details 创建一个新的 XString，内容为视图中的 Latin-1 数据（转换为 UTF-16）。
*          等价于 Qt QLatin1StringView::toString()。
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return 新的 XString 指针，失败返回 NULL
*/
XString* XLatin1StringView_toString(const XLatin1StringView* self);

#include "XLatin1StringView_iterator/XLatin1StringView_iterator.h"    ///< XLatin1StringView 正向迭代器
#include "XLatin1StringView_iterator/XLatin1StringView_reverse_iterator.h"  ///< XLatin1StringView 反向迭代器

#ifdef __cplusplus
}
#endif
#endif // !XLATIN1STRINGVIEW_H#endif // !XLATIN1STRINGVIEW_H
