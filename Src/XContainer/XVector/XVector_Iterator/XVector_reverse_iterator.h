#include"XDataStructConfig.h"
#if !defined(XVECTOR_REVERSE_ITERATOR_H)&& XVector_ON
#define XVECTOR_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
//声明
XContainerTypeDeclare(XVector);
XContainerReverseIteratorDeclare(XVector);
XVector_reverse_iterator* XVector_rbegin(XVector* this_vector);
XVector_reverse_iterator* XVector_rend(XVector* this_vector);
XVector_reverse_iterator* XVector_reverse_iterator_add(XVector* this_vector,XVector_reverse_iterator* it);
void XVector_reverse_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H