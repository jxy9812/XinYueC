/**
* @file XByteArrayView.h
* @brief 字节数组视图头文件（非拥有型只读引用，对标 Qt 6.8 QByteArrayView）
* @details XByteArrayView 是一个轻量级的非拥有型（non-owning）字节数组只读视图，
*          内部仅包含 {数据指针, 长度} 两个成员，不管理数据生命周期。
*          对标 Qt 6.8 QByteArrayView，提供完整的只读访问、子视图、查找、比较、
*          数值转换等 API。
* @note XByteArrayView 是值类型（不是 XClass/XContainer 派生类），
*       在栈上分配和传递，不分配堆内存，不涉及虚函数表。
*/
#include "CXinYueConfig.h"  ///< 项目配置文件，控制 XByteArrayView 模块是否启用
#if !defined(XBYTEARRAYVIEW_H) && XByteArray_ON
#define XBYTEARRAYVIEW_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>     ///< 标准整数类型定义
#include <stdbool.h>    ///< 布尔类型定义
#include <stddef.h>     ///< 标准类型定义（size_t 等）

/* ============================== 前向声明 ============================== */
typedef struct XByteArray XByteArray;  ///< 字节数组容器前向声明

/**
* @brief 字节数组视图结构体（对标 Qt 6.8 QByteArrayView）
* @details 轻量级非拥有型只读视图，仅包含数据指针和长度。
*          不管理数据生命周期，不分配堆内存，不涉及虚函数表。
*          支持从 XByteArray、原始指针+长度、C 字符串等构造。
*
*          null view（m_data == NULL, m_size == 0）与 empty view
*          （m_data != NULL, m_size == 0）的区别：
*          - null view: 未关联任何数据，isNull() 返回 true
*          - empty view: 关联了数据但长度为 0，isEmpty() 返回 true
*/
typedef struct XByteArrayView
{
    const uint8_t* m_data;  ///< 数据指针（NULL 表示 null view）
    int64_t m_size;         ///< 数据长度（字节数，0 表示空视图）
} XByteArrayView;

/* ============================== 迭代器类型 ============================== */

/**
* @brief XByteArrayView 正向迭代器类型
* @details 实际类型为 const uint8_t*，指向视图中的字节数据
*/
typedef const uint8_t* XByteArrayView_iterator;

/**
* @brief XByteArrayView 常量正向迭代器类型
* @details 与 XByteArrayView_iterator 相同，均为 const uint8_t*
*/
typedef const uint8_t* XByteArrayView_const_iterator;

/* ============================== 构造与创建 ============================== */

/**
* @brief 创建默认空视图（null view）
* @details 创建一个空的 XByteArrayView，m_data 为 NULL，m_size 为 0。
*          等价于 Qt QByteArrayView() 默认构造函数。
* @return XByteArrayView 实例（值类型，栈上返回）
*/
XByteArrayView XByteArrayView_create(void);

/**
* @brief 从数据指针和长度创建视图
* @details 创建一个指向指定内存区域的只读视图，不拷贝数据。
*          等价于 Qt QByteArrayView(const char* data, qsizetype len)。
* @param data 数据指针（可为 NULL，此时 len 应传 0）
* @param len  数据长度（字节数，为 0 时创建 empty view）
* @return XByteArrayView 实例
*/
XByteArrayView XByteArrayView_create_data(const uint8_t* data, int64_t len);

/**
* @brief 从指针范围 [first, last) 创建视图
* @details 创建一个指向 [first, last) 范围的只读视图。
*          等价于 Qt QByteArrayView(const char* first, const char* last)。
* @param first 范围起始指针（包含，可为 NULL）
* @param last  范围结束指针（不包含，需 >= first）
* @return XByteArrayView 实例
*/
XByteArrayView XByteArrayView_create_range(const uint8_t* first, const uint8_t* last);

/**
* @brief 从 NULL 终止的 C 字符串创建视图
* @details 自动计算字符串长度（不含终止符 '\0'）。
*          等价于 Qt QByteArrayView(const char* data) 从 C 字符串构造。
* @param str C 字符串指针（NULL 则创建 null view）
* @return XByteArrayView 实例
*/
XByteArrayView XByteArrayView_create_cstr(const char* str);

/**
* @brief 从 XByteArray 创建视图
* @details 创建一个指向 XByteArray 内部数据的只读视图。
*          等价于 Qt QByteArrayView(const QByteArray&) 从 QByteArray 构造。
* @param ba XByteArray 实例指针（NULL 则创建 null view）
* @return XByteArrayView 实例
*/
XByteArrayView XByteArrayView_create_bytearray(const XByteArray* ba);

/* ============================== 基本访问 ============================== */

/**
* @brief 获取数据指针
* @details 返回视图指向的数据指针。若为 null view，返回 NULL。
*          等价于 Qt QByteArrayView::data()。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return 数据指针（const uint8_t* 类型）
*/
const uint8_t* XByteArrayView_data(const XByteArrayView* self);

/**
* @brief 获取常量数据指针（别名）
* @details 等价于 XByteArrayView_data()，提供与 Qt QByteArrayView::constData() 对应的命名。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return 常量数据指针（const uint8_t* 类型）
*/
#define XByteArrayView_constData(self) XByteArrayView_data(self)

/**
* @brief 获取数据长度（字节数）
* @details 返回视图中的数据字节数。
*          等价于 Qt QByteArrayView::size()。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return 数据长度（int64_t 类型，字节数）
*/
int64_t XByteArrayView_size(const XByteArrayView* self);

/**
* @brief 获取数据长度（字节数，别名）
* @details 等价于 XByteArrayView_size()，提供与 Qt QByteArrayView::length() 对应的命名。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return 数据长度（int64_t 类型，字节数）
*/
#define XByteArrayView_length(self) XByteArrayView_size(self)

/**
* @brief 判断视图是否为空（长度为 0）
* @details 返回 true 表示视图长度为 0（包括 null view 和 empty view）。
*          等价于 Qt QByteArrayView::empty()。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return true 为空，false 为非空
*/
bool XByteArrayView_empty(const XByteArrayView* self);

/**
* @brief 判断视图是否为 null（数据指针为 NULL）
* @details null view 表示未关联任何数据。
*          等价于 Qt QByteArrayView::isNull()。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return true 为 null view，false 为非 null
*/
bool XByteArrayView_isNull(const XByteArrayView* self);

/**
* @brief 判断视图是否为空（长度为 0，别名）
* @details 等价于 XByteArrayView_empty()，提供与 Qt QByteArrayView::isEmpty() 对应的命名。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return true 为空，false 为非空
*/
#define XByteArrayView_isEmpty(self) XByteArrayView_empty(self)

/**
* @brief 获取最大可能的视图大小
* @details 返回视图能表示的最大字节数（受 int64_t 最大值限制）。
*          等价于 Qt QByteArrayView::maxSize()。
* @return 最大大小（int64_t 类型）
*/
int64_t XByteArrayView_maxSize(void);

/**
* @brief 获取最大可能的视图大小（别名）
* @details 等价于 XByteArrayView_maxSize()。
* @return 最大大小（int64_t 类型）
*/
#define XByteArrayView_max_size(self) XByteArrayView_maxSize()

/* ============================== 元素访问 ============================== */

/**
* @brief 获取指定索引处的字节（带边界检查断言）
* @details 返回指定位置的字节值。调用方需确保索引在 [0, size()) 范围内。
*          等价于 Qt QByteArrayView::operator[]。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param n    字节索引（从 0 开始，需 < size()）
* @return 指定位置的字节值（uint8_t 类型）
*/
uint8_t XByteArrayView_at(const XByteArrayView* self, int64_t n);

/**
* @brief 获取指定索引处的字节（运算符风格宏）
* @details 等价于 XByteArrayView_at()，提供类似 operator[] 的语法。
* @param self XByteArrayView 实例指针
* @param n    字节索引
* @return 指定位置的字节值
*/
#define XByteArrayView_operatorIndex(self, n) XByteArrayView_at(self, n)

/**
* @brief 获取第一个字节
* @details 返回视图中的第一个字节。视图不能为空（size() > 0）。
*          等价于 Qt QByteArrayView::front()。
* @param self XByteArrayView 实例指针（不可为 NULL，且 size() > 0）
* @return 第一个字节值（uint8_t 类型）
*/
uint8_t XByteArrayView_front(const XByteArrayView* self);

/**
* @brief 获取最后一个字节
* @details 返回视图中的最后一个字节。视图不能为空（size() > 0）。
*          等价于 Qt QByteArrayView::back()。
* @param self XByteArrayView 实例指针（不可为 NULL，且 size() > 0）
* @return 最后一个字节值（uint8_t 类型）
*/
uint8_t XByteArrayView_back(const XByteArrayView* self);

/**
* @brief 获取第一个字节（别名）
* @details 等价于 XByteArrayView_front()，提供与 Qt QByteArrayView::first() 对应的命名。
* @param self XByteArrayView 实例指针
* @return 第一个字节值
*/
#define XByteArrayView_first(self) XByteArrayView_front(self)

/**
* @brief 获取最后一个字节（别名）
* @details 等价于 XByteArrayView_back()，提供与 Qt QByteArrayView::last() 对应的命名。
* @param self XByteArrayView 实例指针
* @return 最后一个字节值
*/
#define XByteArrayView_last(self) XByteArrayView_back(self)

/* ============================== 子视图操作（返回新视图，不拷贝数据） ============================== */

/**
* @brief 获取前 n 个字节的子视图
* @details 返回包含前 n 个字节的新 XByteArrayView。n 超出范围时截断为 size()。
*          等价于 Qt QByteArrayView::first(qsizetype n)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param n    子视图长度（字节数，0 <= n <= size()，超出时截断）
* @return 子视图 XByteArrayView 实例
*/
XByteArrayView XByteArrayView_first_n(const XByteArrayView* self, int64_t n);

/**
* @brief 获取后 n 个字节的子视图
* @details 返回包含后 n 个字节的新 XByteArrayView。n 超出范围时截断为 size()。
*          等价于 Qt QByteArrayView::last(qsizetype n)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param n    子视图长度（字节数，0 <= n <= size()，超出时截断）
* @return 子视图 XByteArrayView 实例
*/
XByteArrayView XByteArrayView_last_n(const XByteArrayView* self, int64_t n);

/**
* @brief 从指定位置到末尾的子视图
* @details 返回从 pos 到末尾的子视图。
*          等价于 Qt QByteArrayView::sliced(qsizetype pos)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param pos  起始位置（0 <= pos <= size()）
* @return 子视图 XByteArrayView 实例
*/
XByteArrayView XByteArrayView_sliced(const XByteArrayView* self, int64_t pos);

/**
* @brief 从指定位置取指定长度的子视图
* @details 返回从 pos 开始、长度为 n 的子视图。
*          等价于 Qt QByteArrayView::sliced(qsizetype pos, qsizetype n)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param pos  起始位置（0 <= pos <= size()）
* @param n    子视图长度（0 <= n <= size() - pos）
* @return 子视图 XByteArrayView 实例
*/
XByteArrayView XByteArrayView_sliced_n(const XByteArrayView* self, int64_t pos, int64_t n);

/**
* @brief 原地裁剪视图（从 pos 开始）
* @details 修改当前视图，使其从 pos 开始到末尾。
*          等价于 Qt QByteArrayView::slice(qsizetype pos)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param pos  新的起始位置（0 <= pos <= size()）
*/
void XByteArrayView_slice(XByteArrayView* self, int64_t pos);

/**
* @brief 原地裁剪视图（从 pos 开始，取 n 字节）
* @details 修改当前视图，使其从 pos 开始、长度为 n。
*          等价于 Qt QByteArrayView::slice(qsizetype pos, qsizetype n)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param pos  新的起始位置（0 <= pos <= size()）
* @param n    新的长度（0 <= n <= size() - pos）
*/
void XByteArrayView_slice_n(XByteArrayView* self, int64_t pos, int64_t n);

/**
* @brief 获取去掉末尾 n 个字节后的子视图
* @details 返回去掉末尾 n 个字节的新视图。
*          等价于 Qt QByteArrayView::chopped(qsizetype len)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param n    要去掉的末尾字节数（0 <= n <= size()）
* @return 子视图 XByteArrayView 实例
*/
XByteArrayView XByteArrayView_chopped(const XByteArrayView* self, int64_t n);

/**
* @brief 获取前 n 个字节的子视图（别名）
* @details 等价于 XByteArrayView_first_n()，提供与 Qt QByteArrayView::left() 对应的命名。
* @param self XByteArrayView 实例指针
* @param n    子视图长度
* @return 子视图 XByteArrayView 实例
*/
#define XByteArrayView_left(self, n) XByteArrayView_first_n(self, n)

/**
* @brief 获取后 n 个字节的子视图（别名）
* @details 等价于 XByteArrayView_last_n()，提供与 Qt QByteArrayView::right() 对应的命名。
* @param self XByteArrayView 实例指针
* @param n    子视图长度
* @return 子视图 XByteArrayView 实例
*/
#define XByteArrayView_right(self, n) XByteArrayView_last_n(self, n)

/**
* @brief 从指定位置取子视图（n 为 -1 时取到末尾）
* @details 返回从 pos 开始、长度 n 的子视图。n 为 -1 时取到末尾。
*          等价于 Qt QByteArrayView::mid(qsizetype pos, qsizetype n = -1)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param pos  起始位置（0 <= pos <= size()）
* @param n    子视图长度（-1 表示到末尾，0 <= n <= size() - pos）
* @return 子视图 XByteArrayView 实例
*/
XByteArrayView XByteArrayView_mid(const XByteArrayView* self, int64_t pos, int64_t n);

/**
* @brief 原地截断视图到前 n 个字节
* @details 修改当前视图，使其只包含前 n 个字节。
*          等价于 Qt QByteArrayView::truncate(qsizetype n)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param n    新的长度（0 <= n <= size()）
*/
void XByteArrayView_truncate(XByteArrayView* self, int64_t n);

/**
* @brief 原地去掉末尾 n 个字节
* @details 修改当前视图，去掉末尾 n 个字节。
*          等价于 Qt QByteArrayView::chop(qsizetype n)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param n    要去掉的末尾字节数（0 <= n <= size()）
*/
void XByteArrayView_chop(XByteArrayView* self, int64_t n);

/* ============================== 查找操作 ============================== */

/**
* @brief 查找子视图首次出现的位置
* @details 从指定位置开始查找子视图 a 首次出现的位置。
*          等价于 Qt QByteArrayView::indexOf(QByteArrayView a, qsizetype from)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param a    待查找的子视图（不可为 NULL）
* @param from 起始搜索位置（0 <= from <= size()，负数表示从末尾偏移）
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XByteArrayView_indexOf(const XByteArrayView* self, const XByteArrayView* a, int64_t from);

/**
* @brief 查找指定字节首次出现的位置
* @details 从指定位置开始查找字节 ch 首次出现的位置。
*          等价于 Qt QByteArrayView::indexOf(char ch, qsizetype from)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ch   待查找的字节值
* @param from 起始搜索位置（0 <= from <= size()，负数表示从末尾偏移）
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XByteArrayView_indexOf_char(const XByteArrayView* self, uint8_t ch, int64_t from);

/**
* @brief 查找子视图最后出现的位置
* @details 从末尾开始查找子视图 a 最后出现的位置。
*          等价于 Qt QByteArrayView::lastIndexOf(QByteArrayView a)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param a    待查找的子视图（不可为 NULL）
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XByteArrayView_lastIndexOf(const XByteArrayView* self, const XByteArrayView* a);

/**
* @brief 从指定位置向前查找子视图
* @details 从指定位置开始向前查找子视图 a 最后出现的位置。
*          等价于 Qt QByteArrayView::lastIndexOf(QByteArrayView a, qsizetype from)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param a    待查找的子视图（不可为 NULL）
* @param from 起始搜索位置（0 <= from <= size()，-1 表示从末尾开始）
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XByteArrayView_lastIndexOf_from(const XByteArrayView* self, const XByteArrayView* a, int64_t from);

/**
* @brief 查找指定字节最后出现的位置
* @details 从指定位置开始向前查找字节 ch 最后出现的位置。
*          等价于 Qt QByteArrayView::lastIndexOf(char ch, qsizetype from = -1)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ch   待查找的字节值
* @param from 起始搜索位置（-1 表示从末尾开始，0 <= from < size() 表示从该位置向前）
* @return 找到返回位置索引，未找到返回 -1
*/
int64_t XByteArrayView_lastIndexOf_char(const XByteArrayView* self, uint8_t ch, int64_t from);

/**
* @brief 判断是否包含子视图
* @details 检查视图中是否包含子视图 a。
*          等价于 Qt QByteArrayView::contains(QByteArrayView a)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param a    待检查的子视图（不可为 NULL）
* @return true 包含，false 不包含
*/
bool XByteArrayView_contains(const XByteArrayView* self, const XByteArrayView* a);

/**
* @brief 判断是否包含指定字节
* @details 检查视图中是否包含字节 ch。
*          等价于 Qt QByteArrayView::contains(char c)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ch   待检查的字节值
* @return true 包含，false 不包含
*/
bool XByteArrayView_contains_char(const XByteArrayView* self, uint8_t ch);

/**
* @brief 统计子视图出现的次数
* @details 统计子视图 a 在视图中不重叠出现的次数。
*          等价于 Qt QByteArrayView::count(QByteArrayView a)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param a    待统计的子视图（不可为 NULL）
* @return 出现次数（int64_t 类型）
*/
int64_t XByteArrayView_count(const XByteArrayView* self, const XByteArrayView* a);

/**
* @brief 统计指定字节出现的次数
* @details 统计字节 ch 在视图中出现的次数。
*          等价于 Qt QByteArrayView::count(char ch)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ch   待统计的字节值
* @return 出现次数（int64_t 类型）
*/
int64_t XByteArrayView_count_char(const XByteArrayView* self, uint8_t ch);

/**
* @brief 判断是否以指定子视图开头
* @details 检查视图是否以子视图 other 开头。
*          等价于 Qt QByteArrayView::startsWith(QByteArrayView other)。
* @param self  XByteArrayView 实例指针（不可为 NULL）
* @param other 待检查的前缀子视图（不可为 NULL）
* @return true 是，false 否
*/
bool XByteArrayView_startsWith(const XByteArrayView* self, const XByteArrayView* other);

/**
* @brief 判断是否以指定字节开头
* @details 检查视图是否以字节 c 开头。
*          等价于 Qt QByteArrayView::startsWith(char c)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param c    待检查的字节值
* @return true 是，false 否
*/
bool XByteArrayView_startsWith_char(const XByteArrayView* self, uint8_t c);

/**
* @brief 判断是否以指定子视图结尾
* @details 检查视图是否以子视图 other 结尾。
*          等价于 Qt QByteArrayView::endsWith(QByteArrayView other)。
* @param self  XByteArrayView 实例指针（不可为 NULL）
* @param other 待检查的后缀子视图（不可为 NULL）
* @return true 是，false 否
*/
bool XByteArrayView_endsWith(const XByteArrayView* self, const XByteArrayView* other);

/**
* @brief 判断是否以指定字节结尾
* @details 检查视图是否以字节 c 结尾。
*          等价于 Qt QByteArrayView::endsWith(char c)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param c    待检查的字节值
* @return true 是，false 否
*/
bool XByteArrayView_endsWith_char(const XByteArrayView* self, uint8_t c);

/* ============================== 比较操作 ============================== */

/**
* @brief 比较两个视图
* @details 按字节序比较两个视图。cs 为 0 时不区分大小写（ASCII 范围），
*          为 1 时区分大小写。
*          等价于 Qt QByteArrayView::compare(QByteArrayView a, Qt::CaseSensitivity cs)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param a    待比较的视图（不可为 NULL）
* @param cs   大小写敏感性（0=不区分，1=区分）
* @return <0 表示 self < a，0 表示相等，>0 表示 self > a
*/
int32_t XByteArrayView_compare(const XByteArrayView* self, const XByteArrayView* a, int cs);

/**
* @brief 判断两个视图是否相等
* @details 按字节序比较两个视图是否完全相等（大小写敏感）。
* @param lhs 左视图（不可为 NULL）
* @param rhs 右视图（不可为 NULL）
* @return true 相等，false 不相等
*/
bool XByteArrayView_equal(const XByteArrayView* lhs, const XByteArrayView* rhs);

/* ============================== 裁剪操作 ============================== */

/**
* @brief 去除首尾空白字符
* @details 返回去除首尾空白字符后的新视图（不修改原视图）。
*          空白字符包括：' '、'\t'、'\n'、'\r'、'\v'、'\f'。
*          等价于 Qt QByteArrayView::trimmed()。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return 裁剪后的新 XByteArrayView 实例
*/
XByteArrayView XByteArrayView_trimmed(const XByteArrayView* self);

/* ============================== 数值转换 ============================== */

/**
* @brief 转换为 short 整数
* @details 将视图内容解析为 short 类型整数。
*          等价于 Qt QByteArrayView::toShort(bool* ok, int base)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测：0x=16进制，0=8进制，其他=10进制；2..36 有效）
* @return 解析结果（short 类型）
*/
int16_t XByteArrayView_toShort(const XByteArrayView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned short 整数
* @details 将视图内容解析为 unsigned short 类型整数。
*          等价于 Qt QByteArrayView::toUShort(bool* ok, int base)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（uint16_t 类型）
*/
uint16_t XByteArrayView_toUShort(const XByteArrayView* self, bool* ok, int base);

/**
* @brief 转换为 int 整数
* @details 将视图内容解析为 int32_t 类型整数。
*          等价于 Qt QByteArrayView::toInt(bool* ok, int base)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（int32_t 类型）
*/
int32_t XByteArrayView_toInt(const XByteArrayView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned int 整数
* @details 将视图内容解析为 uint32_t 类型整数。
*          等价于 Qt QByteArrayView::toUInt(bool* ok, int base)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（uint32_t 类型）
*/
uint32_t XByteArrayView_toUInt(const XByteArrayView* self, bool* ok, int base);

/**
* @brief 转换为 long 整数
* @details 将视图内容解析为 long 类型整数。
*          等价于 Qt QByteArrayView::toLong(bool* ok, int base)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（long 类型）
*/
long XByteArrayView_toLong(const XByteArrayView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned long 整数
* @details 将视图内容解析为 unsigned long 类型整数。
*          等价于 Qt QByteArrayView::toULong(bool* ok, int base)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（unsigned long 类型）
*/
unsigned long XByteArrayView_toULong(const XByteArrayView* self, bool* ok, int base);

/**
* @brief 转换为 long long 整数
* @details 将视图内容解析为 int64_t 类型整数。
*          等价于 Qt QByteArrayView::toLongLong(bool* ok, int base)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（int64_t 类型）
*/
int64_t XByteArrayView_toLongLong(const XByteArrayView* self, bool* ok, int base);

/**
* @brief 转换为 unsigned long long 整数
* @details 将视图内容解析为 uint64_t 类型整数。
*          等价于 Qt QByteArrayView::toULongLong(bool* ok, int base)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @param base 进制（0 表示自动检测）
* @return 解析结果（uint64_t 类型）
*/
uint64_t XByteArrayView_toULongLong(const XByteArrayView* self, bool* ok, int base);

/**
* @brief 转换为 float 浮点数
* @details 将视图内容解析为 float 类型浮点数。
*          等价于 Qt QByteArrayView::toFloat(bool* ok)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @return 解析结果（float 类型）
*/
float XByteArrayView_toFloat(const XByteArrayView* self, bool* ok);

/**
* @brief 转换为 double 浮点数
* @details 将视图内容解析为 double 类型浮点数。
*          等价于 Qt QByteArrayView::toDouble(bool* ok)。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param ok   可选输出，成功为 true，失败为 false
* @return 解析结果（double 类型）
*/
double XByteArrayView_toDouble(const XByteArrayView* self, bool* ok);

/* ============================== 编码检测 ============================== */

/**
* @brief 检查是否为有效的 UTF-8 编码
* @details 检查视图中的字节序列是否为有效的 UTF-8 编码。
*          等价于 Qt QByteArrayView::isValidUtf8()。
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return true 为有效 UTF-8，false 为无效
*/
bool XByteArrayView_isValidUtf8(const XByteArrayView* self);

/* ============================== 迭代器宏 ============================== */

/**
* @brief 获取指向视图起始位置的迭代器
* @details 返回指向第一个字节的 const 指针。
*          等价于 Qt QByteArrayView::begin()。
* @param self XByteArrayView 实例（值类型，非指针）
* @return 正向迭代器（const uint8_t*）
*/
#define XByteArrayView_begin(self) ((self).m_data)

/**
* @brief 获取指向视图结束位置的迭代器
* @details 返回指向最后一个字节之后位置的 const 指针。
*          等价于 Qt QByteArrayView::end()。
* @param self XByteArrayView 实例（值类型，非指针）
* @return 正向迭代器（const uint8_t*）
*/
#define XByteArrayView_end(self) ((self).m_data + (self).m_size)

/**
* @brief 获取常量起始迭代器
* @details 等价于 XByteArrayView_begin()。
*          等价于 Qt QByteArrayView::cbegin()。
*/
#define XByteArrayView_cbegin(self) XByteArrayView_begin(self)

/**
* @brief 获取常量结束迭代器
* @details 等价于 XByteArrayView_end()。
*          等价于 Qt QByteArrayView::cend()。
*/
#define XByteArrayView_cend(self) XByteArrayView_end(self)

/**
* @brief 获取反向起始迭代器
* @details 返回指向最后一个字节的反向迭代器。
*          等价于 Qt QByteArrayView::rbegin()。
* @param self XByteArrayView 实例（值类型，非指针）
* @return 反向迭代器
*/
#define XByteArrayView_rbegin(self) (&(self).m_data[(self).m_size])

/**
* @brief 获取反向结束迭代器
* @details 返回指向第一个字节之前位置的反向迭代器。
*          等价于 Qt QByteArrayView::rend()。
* @param self XByteArrayView 实例（值类型，非指针）
* @return 反向迭代器
*/
#define XByteArrayView_rend(self) ((self).m_data)

/**
* @brief 获取常量反向起始迭代器
* @details 等价于 XByteArrayView_rbegin()。
*          等价于 Qt QByteArrayView::crbegin()。
*/
#define XByteArrayView_crbegin(self) XByteArrayView_rbegin(self)

/**
* @brief 获取常量反向结束迭代器
* @details 等价于 XByteArrayView_rend()。
*          等价于 Qt QByteArrayView::crend()。
*/
#define XByteArrayView_crend(self) XByteArrayView_rend(self)

#ifdef __cplusplus
}
#endif
#endif // !XBYTEARRAYVIEW_H
