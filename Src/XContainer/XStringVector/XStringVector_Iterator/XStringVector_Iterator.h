#include"XDataStructConfig.h"
#if !defined(XSTRINGVECTOR_ITERATOR_H)&& XStringVector_ON
#define XSTRINGVECTOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
XContainerTypeDeclare(XStringVector);
//正向迭代器
XContainerIteratorDeclare(XStringVector);

XContainer_begin(XStringVector);
XContainer_end(XStringVector);
XContainer_iterator_add(XStringVector);
//typedef void (*XFor_each)(XString* string,void* args);
XContainer_iterator_for_each(XStringVector);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
