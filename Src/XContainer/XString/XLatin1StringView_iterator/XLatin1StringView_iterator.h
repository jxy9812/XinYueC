/**
* @file XLatin1StringView_iterator.h
* @brief XLatin1StringView 正向迭代器头文件
* @details 定义 XLatin1StringView 的正向迭代器结构及相关操作函数，
*          用于从前向后遍历视图中的 Latin-1 字符数据。
*          遵循与 XVector_iterator 相同的迭代器模式。
*/
#include "CXinYueConfig.h"
#if !defined(XLATIN1STRINGVIEW_ITERATOR_H) && XString_ON
#define XLATIN1STRINGVIEW_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XContainer_iterator.h"
#include <stdbool.h>

/**
* @brief 声明 XLatin1StringView 类型
*/
typedef struct XLatin1StringView XLatin1StringView;

/**
* @brief XLatin1StringView 正向迭代器结构体
* @details 用于遍历 XLatin1StringView 中的 Latin-1 字符，支持从前向后的单向遍历。
*          data 指向当前字符位置，NULL 表示结束。
*/
typedef struct XLatin1StringView_iterator
{
    const char* data; ///< 当前指向的字符数据指针
} XLatin1StringView_iterator;

/**
* @brief 获取指向 XLatin1StringView 第一个字符的迭代器
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return 指向第一个字符的迭代器，若视图为空则返回结束迭代器
*/
XLatin1StringView_iterator XLatin1StringView_begin(const XLatin1StringView* self);

/**
* @brief 获取 XLatin1StringView 的结束迭代器（哨兵位置）
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @return 结束迭代器（data 为 NULL）
*/
XLatin1StringView_iterator XLatin1StringView_end(const XLatin1StringView* self);

/**
* @brief 判断迭代器是否已到达末尾
* @param it 迭代器指针
* @return 若迭代器已到达末尾返回 true，否则返回 false
*/
bool XLatin1StringView_iterator_isEnd(const XLatin1StringView_iterator* it);

/**
* @brief 将迭代器移动到下一个字符位置
* @param self XLatin1StringView 实例指针（不可为 NULL）
* @param it 迭代器指针（会被修改）
*/
void XLatin1StringView_iterator_add(const XLatin1StringView* self, XLatin1StringView_iterator* it);

/**
* @brief 判断两个迭代器是否相等
* @param itFirst 第一个迭代器指针
* @param itSecond 第二个迭代器指针
* @return 相等返回 true，不相等返回 false
*/
bool XLatin1StringView_iterator_equality(XLatin1StringView_iterator* itFirst, XLatin1StringView_iterator* itSecond);

/**
* @brief 使用回调函数遍历 XLatin1StringView 中的所有字符
* @param self XLatin1StringView 实例指针
* @param ForFunction 对每个字符调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XLatin1StringView_iterator_for_each(const XLatin1StringView* self, XFor_each ForFunction, void* args);

/**
* @brief 获取迭代器当前指向的字符数据指针
* @param it 迭代器指针
* @return 指向当前字符的 const char* 指针
*/
const char* XLatin1StringView_iterator_data(XLatin1StringView_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // !XLATIN1STRINGVIEW_ITERATOR_H
