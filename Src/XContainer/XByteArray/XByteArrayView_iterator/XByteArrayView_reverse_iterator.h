/**
* @file XByteArrayView_reverse_iterator.h
* @brief XByteArrayView 反向迭代器头文件
* @details 定义 XByteArrayView 的反向迭代器结构及相关操作函数，
*          用于从后向前遍历视图中的字节数据。
*          遵循与 XVector_reverse_iterator 相同的迭代器模式。
*/
#include "CXinYueConfig.h"
#if !defined(XBYTEARRAYVIEW_REVERSE_ITERATOR_H) && XByteArray_ON
#define XBYTEARRAYVIEW_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XContainer_iterator.h"
#include <stdint.h>
#include <stdbool.h>

/**
* @brief 声明 XByteArrayView 类型
*/
typedef struct XByteArrayView XByteArrayView;

/**
* @brief XByteArrayView 反向迭代器结构体
* @details 用于反向遍历 XByteArrayView 中的字节，支持从后向前的单向遍历。
*          data 指向当前字节位置，NULL 表示结束。
*/
typedef struct XByteArrayView_reverse_iterator
{
    const uint8_t* data; ///< 当前指向的字节数据指针
} XByteArrayView_reverse_iterator;

/**
* @brief 获取指向 XByteArrayView 最后一个字节的反向迭代器
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return 指向最后一个字节的反向迭代器，若视图为空则返回结束迭代器
*/
XByteArrayView_reverse_iterator XByteArrayView_rbegin(const XByteArrayView* self);

/**
* @brief 获取 XByteArrayView 的反向结束迭代器（哨兵位置）
* @param self XByteArrayView 实例指针（不可为 NULL）
* @return 反向结束迭代器（data 为 NULL）
*/
XByteArrayView_reverse_iterator XByteArrayView_rend(const XByteArrayView* self);

/**
* @brief 判断反向迭代器是否已到达开头
* @param it 反向迭代器指针
* @return 若反向迭代器已到达开头返回 true，否则返回 false
*/
bool XByteArrayView_reverse_iterator_isRend(const XByteArrayView_reverse_iterator* it);

/**
* @brief 将反向迭代器移动到前一个字节位置
* @param self XByteArrayView 实例指针（不可为 NULL）
* @param it 反向迭代器指针（会被修改）
*/
void XByteArrayView_reverse_iterator_add(const XByteArrayView* self, XByteArrayView_reverse_iterator* it);

/**
* @brief 判断两个反向迭代器是否相等
* @param itFirst 第一个反向迭代器指针
* @param itSecond 第二个反向迭代器指针
* @return 相等返回 true，不相等返回 false
*/
bool XByteArrayView_reverse_iterator_equality(XByteArrayView_reverse_iterator* itFirst, XByteArrayView_reverse_iterator* itSecond);

/**
* @brief 使用回调函数反向遍历 XByteArrayView 中的所有字节
* @param self XByteArrayView 实例指针
* @param ForFunction 对每个字节调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XByteArrayView_reverse_iterator_for_each(const XByteArrayView* self, XFor_each ForFunction, void* args);

/**
* @brief 获取反向迭代器当前指向的字节数据指针
* @param it 反向迭代器指针
* @return 指向当前字节的 const uint8_t* 指针
*/
const uint8_t* XByteArrayView_reverse_iterator_data(XByteArrayView_reverse_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // !XBYTEARRAYVIEW_REVERSE_ITERATOR_H
