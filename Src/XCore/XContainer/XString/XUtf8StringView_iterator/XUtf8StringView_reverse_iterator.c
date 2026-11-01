/**
* @file XUtf8StringView_reverse_iterator.c
* @brief XUtf8StringView 反向迭代器实现
*/
#include "XUtf8StringView_reverse_iterator.h"
#if XString_ON
#include "XUtf8StringView.h"
#include <stdio.h>

XUtf8StringView_reverse_iterator XUtf8StringView_rbegin(const XUtf8StringView* self)
{
    XUtf8StringView_reverse_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size == 0)
        return it;
    it.data = self->m_data + self->m_size - 1;
    return it;
}

XUtf8StringView_reverse_iterator XUtf8StringView_rend(const XUtf8StringView* self)
{
    XUtf8StringView_reverse_iterator it = { NULL };
    (void)self;
    return it;
}

bool XUtf8StringView_reverse_iterator_isRend(const XUtf8StringView_reverse_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XUtf8StringView_reverse_iterator_add(const XUtf8StringView* self, XUtf8StringView_reverse_iterator* it)
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

bool XUtf8StringView_reverse_iterator_equality(XUtf8StringView_reverse_iterator* itFirst, XUtf8StringView_reverse_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XUtf8StringView_reverse_iterator_for_each(const XUtf8StringView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_reverse_iterator(self, XUtf8StringView, it)
    {
        ForFunction((void*)XUtf8StringView_reverse_iterator_data(&it), args);
    }
}

const char* XUtf8StringView_reverse_iterator_data(XUtf8StringView_reverse_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
