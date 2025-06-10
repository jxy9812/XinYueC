#include"XDataStructConfig.h"
#if !defined(XSTRINGVECTOR_REVERSE_ITERATOR_H)&& XStringVector_ON
#define XSTRINGVECTOR_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XVector_reverse_iterator.h"
XContainerTypeDeclare(XStringVector);
//反向迭代器
typedef XVector_reverse_iterator XStringVector_reverse_iterator;
XStringVector_reverse_iterator XStringVector_rbegin(XStringVector* this_XStringVector);
XStringVector_reverse_iterator XStringVector_rend(XStringVector* this_XStringVector);
void XStringVector_reverse_iterator_add(XStringVector* this_XStringVector, XStringVector_reverse_iterator* it);
bool XStringVector_reverse_iterator_equality(XStringVector_reverse_iterator* itFirst, XStringVector_reverse_iterator* itSecond);
void XStringVector_reverse_iterator_for_each(XStringVector* this_XStringVector, XFor_each ForFunction, void* args);
void* XStringVector_reverse_iterator_data(XStringVector_reverse_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H