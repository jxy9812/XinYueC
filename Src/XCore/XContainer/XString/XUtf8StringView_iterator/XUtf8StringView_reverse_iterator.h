/**
* @file XUtf8StringView_reverse_iterator.h
* @brief XUtf8StringView 反向迭代器头文件
* @details 定义 XUtf8StringView 的反向迭代器结构及相关操作函数，
*          用于从后向前遍历视图中的 UTF-8 字符数据。
*          遵循与 XVector_reverse_iterator 相同的迭代器模式。
*/
#include "CXinYueConfig.h"
#if !defined(XUTF8STRINGVIEW_REVERSE_ITERATOR_H) && XString_ON
#define XUTF8STRINGVIEW_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XContainer_iterator.h"
#include <stdbool.h>

typedef struct XUtf8StringView XUtf8StringView;

typedef struct XUtf8StringView_reverse_iterator
{
    const char* data;
} XUtf8StringView_reverse_iterator;

XUtf8StringView_reverse_iterator XUtf8StringView_rbegin(const XUtf8StringView* self);
XUtf8StringView_reverse_iterator XUtf8StringView_rend(const XUtf8StringView* self);
bool XUtf8StringView_reverse_iterator_isRend(const XUtf8StringView_reverse_iterator* it);
void XUtf8StringView_reverse_iterator_add(const XUtf8StringView* self, XUtf8StringView_reverse_iterator* it);
bool XUtf8StringView_reverse_iterator_equality(XUtf8StringView_reverse_iterator* itFirst, XUtf8StringView_reverse_iterator* itSecond);
void XUtf8StringView_reverse_iterator_for_each(const XUtf8StringView* self, XFor_each ForFunction, void* args);
const char* XUtf8StringView_reverse_iterator_data(XUtf8StringView_reverse_iterator* it);

#ifdef __cplusplus
}
#endif
#endif
