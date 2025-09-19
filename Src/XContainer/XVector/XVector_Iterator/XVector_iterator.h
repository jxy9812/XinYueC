#include"CXinYueConfig.h"
#if !defined(XVECTOR_ITERATOR_H)&& XVector_ON
#define XVECTOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
//声明
XContainerTypeDeclare(XVector);
//正向迭代器
typedef struct XVector_iterator
{
	void* data;
}XVector_iterator;
XVector_iterator XVector_begin(XVector* this_vector);
XVector_iterator XVector_end(XVector* this_vector);
bool XVector_iterator_isEnd(const XVector_iterator* it);
void XVector_iterator_add(XVector* this_vector, XVector_iterator*it);
bool XVector_iterator_equality(XVector_iterator* itFirst, XVector_iterator* itSecond);
void XVector_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args);
void* XVector_iterator_data(XVector_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
