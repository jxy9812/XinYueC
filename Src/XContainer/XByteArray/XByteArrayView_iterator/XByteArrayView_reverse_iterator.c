/**
* @file XByteArrayView_reverse_iterator.c
* @brief XByteArrayView 反向迭代器实现
* @details 实现 XByteArrayView 的反向迭代器操作函数。
*          迭代器从视图末尾向前遍历，不涉及容器数据管理。
*/
#include "XByteArrayView_reverse_iterator.h"
#if XByteArray_ON
#include "XByteArrayView.h"
#include <stdio.h>

XByteArrayView_reverse_iterator XByteArrayView_rbegin(const XByteArrayView* self)
{
    XByteArrayView_reverse_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size == 0)
        return it;
    it.data = self->m_data + self->m_size - 1;
    return it;
}

XByteArrayView_reverse_iterator XByteArrayView_rend(const XByteArrayView* self)
{
    XByteArrayView_reverse_iterator it = { NULL };
    (void)self;
    return it;
}

bool XByteArrayView_reverse_iterator_isRend(const XByteArrayView_reverse_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XByteArrayView_reverse_iterator_add(const XByteArrayView* self, XByteArrayView_reverse_iterator* it)
{
    if (!self || !it || !it->data)
        return;
    /* 如果当前指向第一个字节，则标记为结束 */
    if (it->data == self->m_data)
    {
        it->data = NULL;
        return;
    }
    it->data = it->data - 1; /* 指向上一个字节 */
}

bool XByteArrayView_reverse_iterator_equality(XByteArrayView_reverse_iterator* itFirst, XByteArrayView_reverse_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XByteArrayView_reverse_iterator_for_each(const XByteArrayView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_reverse_iterator(self, XByteArrayView, it)
    {
        ForFunction((void*)XByteArrayView_reverse_iterator_data(&it), args);
    }
}

const uint8_t* XByteArrayView_reverse_iterator_data(XByteArrayView_reverse_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
