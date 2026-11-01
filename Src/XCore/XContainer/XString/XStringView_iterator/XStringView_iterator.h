/**
* @file XStringView_iterator.h
* @brief XStringView 正向迭代器头文件
* @details 定义 XStringView 的正向迭代器结构及相关操作函数，
*          用于从前向后遍历视图中的字符数据。
*          遵循与 XString_iterator 相同的迭代器模式。
*/
#include "CXinYueConfig.h"
#if !defined(XSTRINGVIEW_ITERATOR_H) && XString_ON
#define XSTRINGVIEW_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XContainer_iterator.h"
#include "XChar.h"
#include <stdbool.h>

/**
* @brief 声明 XStringView 类型
*/
typedef struct XStringView XStringView;

/**
* @brief XStringView 正向迭代器结构体
* @details 用于遍历 XStringView 中的字符，支持从前向后的单向遍历。
*          data 指向当前字符位置，NULL 表示结束。
*/
typedef struct XStringView_iterator
{
    const XChar* data; ///< 当前指向的字符数据指针
} XStringView_iterator;

/**
* @brief 获取指向 XStringView 第一个字符的迭代器
* @param self XStringView 实例指针（不可为 NULL）
* @return 指向第一个字符的迭代器，若视图为空则返回结束迭代器
*/
XStringView_iterator XStringView_begin(const XStringView* self);

/**
* @brief 获取 XStringView 的结束迭代器（哨兵位置）
* @param self XStringView 实例指针（不可为 NULL）
* @return 结束迭代器（data 为 NULL）
*/
XStringView_iterator XStringView_end(const XStringView* self);

/**
* @brief 判断迭代器是否已到达末尾
* @param it 迭代器指针
* @return 若迭代器已到达末尾返回 true，否则返回 false
*/
bool XStringView_iterator_isEnd(const XStringView_iterator* it);

/**
* @brief 将迭代器移动到下一个字符位置
* @param self XStringView 实例指针（不可为 NULL）
* @param it 迭代器指针（会被修改）
*/
void XStringView_iterator_add(const XStringView* self, XStringView_iterator* it);

/**
* @brief 判断两个迭代器是否相等
* @param itFirst 第一个迭代器指针
* @param itSecond 第二个迭代器指针
* @return 相等返回 true，不相等返回 false
*/
bool XStringView_iterator_equality(XStringView_iterator* itFirst, XStringView_iterator* itSecond);

/**
* @brief 使用回调函数遍历 XStringView 中的所有字符
* @param self XStringView 实例指针
* @param ForFunction 对每个字符调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XStringView_iterator_for_each(const XStringView* self, XFor_each ForFunction, void* args);

/**
* @brief 获取迭代器当前指向的字符数据指针
* @param it 迭代器指针
* @return 指向当前字符的 const XChar* 指针
*/
const XChar* XStringView_iterator_data(XStringView_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // !XSTRINGVIEW_ITERATOR_H
