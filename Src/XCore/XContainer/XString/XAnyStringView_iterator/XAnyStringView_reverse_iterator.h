/**
* @file XAnyStringView_reverse_iterator.h
* @brief XAnyStringView 反向迭代器头文件
* @details 定义 XAnyStringView 的反向迭代器结构及相关操作函数，
*          用于从后向前遍历视图中的字符数据（以 UTF-8 形式访问）。
*          遵循与 XVector_reverse_iterator 相同的迭代器模式。
*/
#include "CXinYueConfig.h"
#if !defined(XANYSTRINGVIEW_REVERSE_ITERATOR_H) && XString_ON
#define XANYSTRINGVIEW_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XContainer_iterator.h"
#include <stdbool.h>

typedef struct XAnyStringView XAnyStringView;

typedef struct XAnyStringView_reverse_iterator
{
    const char* data;
} XAnyStringView_reverse_iterator;

XAnyStringView_reverse_iterator XAnyStringView_rbegin(const XAnyStringView* self);
XAnyStringView_reverse_iterator XAnyStringView_rend(const XAnyStringView* self);
bool XAnyStringView_reverse_iterator_isRend(const XAnyStringView_reverse_iterator* it);
void XAnyStringView_reverse_iterator_add(const XAnyStringView* self, XAnyStringView_reverse_iterator* it);
bool XAnyStringView_reverse_iterator_equality(XAnyStringView_reverse_iterator* itFirst, XAnyStringView_reverse_iterator* itSecond);
void XAnyStringView_reverse_iterator_for_each(const XAnyStringView* self, XFor_each ForFunction, void* args);
const char* XAnyStringView_reverse_iterator_data(XAnyStringView_reverse_iterator* it);

#ifdef __cplusplus
}
#endif
#endif
