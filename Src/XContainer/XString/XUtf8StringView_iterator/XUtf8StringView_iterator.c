/**
* @file XUtf8StringView_iterator.c
* @brief XUtf8StringView 正向迭代器实现
*/
#include "XUtf8StringView_iterator.h"
#if XString_ON
#include "XUtf8StringView.h"
#include <stdio.h>

XUtf8StringView_iterator XUtf8StringView_begin(const XUtf8StringView* self)
{
    XUtf8StringView_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size == 0)
        return it;
    it.data = self->m_data;
    return it;
}

XUtf8StringView_iterator XUtf8StringView_end(const XUtf8StringView* self)
{
    XUtf8StringView_iterator it = { NULL };
    (void)self;
    return it;
}

bool XUtf8StringView_iterator_isEnd(const XUtf8StringView_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XUtf8StringView_iterator_add(const XUtf8StringView* self, XUtf8StringView_iterator* it)
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

bool XUtf8StringView_iterator_equality(XUtf8StringView_iterator* itFirst, XUtf8StringView_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XUtf8StringView_iterator_for_each(const XUtf8StringView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_iterator(self, XUtf8StringView, it)
    {
        ForFunction((void*)XUtf8StringView_iterator_data(&it), args);
    }
}

const char* XUtf8StringView_iterator_data(XUtf8StringView_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
