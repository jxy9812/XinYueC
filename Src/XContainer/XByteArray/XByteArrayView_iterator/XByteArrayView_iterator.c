/**
* @file XByteArrayView_iterator.c
* @brief XByteArrayView 正向迭代器实现
* @details 实现 XByteArrayView 的正向迭代器操作函数。
*          迭代器遍历基于视图的 m_data 和 m_size 字段，
*          不涉及容器数据管理，仅提供只读访问。
*/
#include "XByteArrayView_iterator.h"
#if XByteArray_ON
#include "XByteArrayView.h"
#include <stdio.h>

XByteArrayView_iterator XByteArrayView_begin(const XByteArrayView* self)
{
    XByteArrayView_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size == 0)
        return it;
    it.data = self->m_data;
    return it;
}

XByteArrayView_iterator XByteArrayView_end(const XByteArrayView* self)
{
    XByteArrayView_iterator it = { NULL };
    (void)self;
    return it;
}

bool XByteArrayView_iterator_isEnd(const XByteArrayView_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XByteArrayView_iterator_add(const XByteArrayView* self, XByteArrayView_iterator* it)
{
    if (!self || !it || !it->data)
        return;
    /* 如果当前指向最后一个字节，则标记为结束 */
    if (it->data == self->m_data + self->m_size - 1)
    {
        it->data = NULL;
        return;
    }
    it->data = it->data + 1; /* 指向下一个字节 */
}

bool XByteArrayView_iterator_equality(XByteArrayView_iterator* itFirst, XByteArrayView_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XByteArrayView_iterator_for_each(const XByteArrayView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_iterator(self, XByteArrayView, it)
    {
        ForFunction((void*)XByteArrayView_iterator_data(&it), args);
    }
}

const uint8_t* XByteArrayView_iterator_data(XByteArrayView_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
