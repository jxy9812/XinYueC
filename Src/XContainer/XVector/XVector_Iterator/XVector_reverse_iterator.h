#include"CXinYueConfig.h"
#if !defined(XVECTOR_REVERSE_ITERATOR_H)&& XVector_ON
#define XVECTOR_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
//声明
XContainerTypeDeclare(XVector);
//正向迭代器
typedef struct XVector_reverse_iterator
{
	void* data;
}XVector_reverse_iterator;
XVector_reverse_iterator XVector_rbegin(XVector* this_vector);
XVector_reverse_iterator XVector_rend(XVector* this_vector);
bool XVector_reverse_iterator_isRend(const XVector_reverse_iterator* it);
void XVector_reverse_iterator_add(XVector* this_vector,XVector_reverse_iterator* it);
bool XVector_reverse_iterator_equality(XVector_reverse_iterator* itFirst, XVector_reverse_iterator* itSecond);
void XVector_reverse_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args);
void* XVector_reverse_iterator_data(XVector_reverse_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H