/**
* @file XStringView_iterator.c
* @brief XStringView 正向迭代器实现
* @details 实现 XStringView 的正向迭代器操作函数。
*          迭代器遍历基于视图的 m_data 和 m_size 字段，
*          不涉及容器数据管理，仅提供只读访问。
*/
#include "XStringView_iterator.h"
#if XString_ON
#include "XStringView.h"
#include <stdio.h>

XStringView_iterator XStringView_begin(const XStringView* self)
{
    XStringView_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size == 0)
        return it;
    it.data = self->m_data;
    return it;
}

XStringView_iterator XStringView_end(const XStringView* self)
{
    XStringView_iterator it = { NULL };
    (void)self;
    return it;
}

bool XStringView_iterator_isEnd(const XStringView_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XStringView_iterator_add(const XStringView* self, XStringView_iterator* it)
{
    if (!self || !it || !it->data)
        return;
    /* 如果当前指向最后一个字符，则标记为结束 */
    if (it->data == self->m_data + self->m_size - 1)
    {
        it->data = NULL;
        return;
    }
    it->data = it->data + 1; /* 指向下一个字符 */
}

bool XStringView_iterator_equality(XStringView_iterator* itFirst, XStringView_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XStringView_iterator_for_each(const XStringView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_iterator(self, XStringView, it)
    {
        ForFunction((void*)XStringView_iterator_data(&it), args);
    }
}

const XChar* XStringView_iterator_data(XStringView_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
