/**
* @file XAnyStringView_iterator.h
* @brief XAnyStringView 正向迭代器头文件
* @details 定义 XAnyStringView 的正向迭代器结构及相关操作函数，
*          用于从前向后遍历视图中的字符数据（以 UTF-8 形式访问）。
*          遵循与 XVector_iterator 相同的迭代器模式。
*/
#include "CXinYueConfig.h"
#if !defined(XANYSTRINGVIEW_ITERATOR_H) && XString_ON
#define XANYSTRINGVIEW_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XContainer_iterator.h"
#include <stdbool.h>

typedef struct XAnyStringView XAnyStringView;

typedef struct XAnyStringView_iterator
{
    const char* data;
} XAnyStringView_iterator;

XAnyStringView_iterator XAnyStringView_begin(const XAnyStringView* self);
XAnyStringView_iterator XAnyStringView_end(const XAnyStringView* self);
bool XAnyStringView_iterator_isEnd(const XAnyStringView_iterator* it);
void XAnyStringView_iterator_add(const XAnyStringView* self, XAnyStringView_iterator* it);
bool XAnyStringView_iterator_equality(XAnyStringView_iterator* itFirst, XAnyStringView_iterator* itSecond);
void XAnyStringView_iterator_for_each(const XAnyStringView* self, XFor_each ForFunction, void* args);
const char* XAnyStringView_iterator_data(XAnyStringView_iterator* it);

#ifdef __cplusplus
}
#endif
#endif
