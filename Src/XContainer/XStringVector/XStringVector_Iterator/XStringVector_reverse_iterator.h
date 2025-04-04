#include"XDataStructConfig.h"
#if !defined(XSTRINGVECTOR_REVERSE_ITERATOR_H)&& XStringVector_ON
#define XSTRINGVECTOR_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
XContainerTypeDeclare(XStringVector);
//反向迭代器
XContainerReverseIteratorDeclare(XStringVector);

XContainer_rbegin(XStringVector);
XContainer_rend(XStringVector);
XContainer_reverse_iterator_add(XStringVector);
//typedef void (*XFor_each)(XString* string,void* args);
XContainer_reverse_iterator_for_each(XStringVector);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H