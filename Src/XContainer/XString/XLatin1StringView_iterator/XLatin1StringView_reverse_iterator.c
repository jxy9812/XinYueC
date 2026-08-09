/**
* @file XLatin1StringView_reverse_iterator.c
* @brief XLatin1StringView 反向迭代器实现
*/
#include "XLatin1StringView_reverse_iterator.h"
#if XString_ON
#include "XLatin1StringView.h"
#include <stdio.h>

XLatin1StringView_reverse_iterator XLatin1StringView_rbegin(const XLatin1StringView* self)
{
    XLatin1StringView_reverse_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size == 0)
        return it;
    it.data = self->m_data + self->m_size - 1;
    return it;
}

XLatin1StringView_reverse_iterator XLatin1StringView_rend(const XLatin1StringView* self)
{
    XLatin1StringView_reverse_iterator it = { NULL };
    (void)self;
    return it;
}

bool XLatin1StringView_reverse_iterator_isRend(const XLatin1StringView_reverse_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XLatin1StringView_reverse_iterator_add(const XLatin1StringView* self, XLatin1StringView_reverse_iterator* it)
{
    if (!self || !it || !it->data)
        return;
    if (it->data == self->m_data)
    {
        it->data = NULL;
        return;
    }
    it->data = it->data - 1;
}

bool XLatin1StringView_reverse_iterator_equality(XLatin1StringView_reverse_iterator* itFirst, XLatin1StringView_reverse_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XLatin1StringView_reverse_iterator_for_each(const XLatin1StringView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_reverse_iterator(self, XLatin1StringView, it)
    {
        ForFunction((void*)XLatin1StringView_reverse_iterator_data(&it), args);
    }
}

const char* XLatin1StringView_reverse_iterator_data(XLatin1StringView_reverse_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
