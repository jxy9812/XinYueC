/**
* @file XLatin1StringView_iterator.c
* @brief XLatin1StringView 正向迭代器实现
* @details 实现 XLatin1StringView 的正向迭代器操作函数。
*          迭代器遍历基于视图的 m_data 和 m_size 字段。
*/
#include "XLatin1StringView_iterator.h"
#if XString_ON
#include "XLatin1StringView.h"
#include <stdio.h>

XLatin1StringView_iterator XLatin1StringView_begin(const XLatin1StringView* self)
{
    XLatin1StringView_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size == 0)
        return it;
    it.data = self->m_data;
    return it;
}

XLatin1StringView_iterator XLatin1StringView_end(const XLatin1StringView* self)
{
    XLatin1StringView_iterator it = { NULL };
    (void)self;
    return it;
}

bool XLatin1StringView_iterator_isEnd(const XLatin1StringView_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XLatin1StringView_iterator_add(const XLatin1StringView* self, XLatin1StringView_iterator* it)
{
    if (!self || !it || !it->data)
        return;
    if (it->data == self->m_data + self->m_size - 1)
    {
        it->data = NULL;
        return;
    }
    it->data = it->data + 1;
}

bool XLatin1StringView_iterator_equality(XLatin1StringView_iterator* itFirst, XLatin1StringView_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XLatin1StringView_iterator_for_each(const XLatin1StringView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_iterator(self, XLatin1StringView, it)
    {
        ForFunction((void*)XLatin1StringView_iterator_data(&it), args);
    }
}

const char* XLatin1StringView_iterator_data(XLatin1StringView_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
