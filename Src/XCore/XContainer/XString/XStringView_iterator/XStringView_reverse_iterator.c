/**
* @file XStringView_reverse_iterator.c
* @brief XStringView 反向迭代器实现
* @details 实现 XStringView 的反向迭代器操作函数。
*          迭代器从视图末尾向前遍历，不涉及容器数据管理。
*/
#include "XStringView_reverse_iterator.h"
#if XString_ON
#include "XStringView.h"
#include <stdio.h>

XStringView_reverse_iterator XStringView_rbegin(const XStringView* self)
{
    XStringView_reverse_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size == 0)
        return it;
    it.data = self->m_data + self->m_size - 1;
    return it;
}

XStringView_reverse_iterator XStringView_rend(const XStringView* self)
{
    XStringView_reverse_iterator it = { NULL };
    (void)self;
    return it;
}

bool XStringView_reverse_iterator_isRend(const XStringView_reverse_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XStringView_reverse_iterator_add(const XStringView* self, XStringView_reverse_iterator* it)
{
    if (!self || !it || !it->data)
        return;
    /* 如果当前指向第一个字符，则标记为结束 */
    if (it->data == self->m_data)
    {
        it->data = NULL;
        return;
    }
    it->data = it->data - 1; /* 指向上一个字符 */
}

bool XStringView_reverse_iterator_equality(XStringView_reverse_iterator* itFirst, XStringView_reverse_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XStringView_reverse_iterator_for_each(const XStringView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_reverse_iterator(self, XStringView, it)
    {
        ForFunction((void*)XStringView_reverse_iterator_data(&it), args);
    }
}

const XChar* XStringView_reverse_iterator_data(XStringView_reverse_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
