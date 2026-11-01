/**
* @file XLatin1StringView_reverse_iterator.h
* @brief XLatin1StringView 反向迭代器头文件
* @details 定义 XLatin1StringView 的反向迭代器结构及相关操作函数，
*          用于从后向前遍历视图中的 Latin-1 字符数据。
*          遵循与 XVector_reverse_iterator 相同的迭代器模式。
*/
#include "CXinYueConfig.h"
#if !defined(XLATIN1STRINGVIEW_REVERSE_ITERATOR_H) && XString_ON
#define XLATIN1STRINGVIEW_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XContainer_iterator.h"
#include <stdbool.h>

typedef struct XLatin1StringView XLatin1StringView;

typedef struct XLatin1StringView_reverse_iterator
{
    const char* data;
} XLatin1StringView_reverse_iterator;

XLatin1StringView_reverse_iterator XLatin1StringView_rbegin(const XLatin1StringView* self);
XLatin1StringView_reverse_iterator XLatin1StringView_rend(const XLatin1StringView* self);
bool XLatin1StringView_reverse_iterator_isRend(const XLatin1StringView_reverse_iterator* it);
void XLatin1StringView_reverse_iterator_add(const XLatin1StringView* self, XLatin1StringView_reverse_iterator* it);
bool XLatin1StringView_reverse_iterator_equality(XLatin1StringView_reverse_iterator* itFirst, XLatin1StringView_reverse_iterator* itSecond);
void XLatin1StringView_reverse_iterator_for_each(const XLatin1StringView* self, XFor_each ForFunction, void* args);
const char* XLatin1StringView_reverse_iterator_data(XLatin1StringView_reverse_iterator* it);

#ifdef __cplusplus
}
#endif
#endif
