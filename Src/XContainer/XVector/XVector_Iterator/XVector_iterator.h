#include"XContainerObject.h"
#if !defined(XVECTOR_ITERATOR_H)&& XVector_ON
#define XVECTOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
typedef struct XVector XVector;
////容器for_each(容器循环遍历)回调函数
//typedef void (*XFor_each)(void* LPVal);
//正向迭代器
typedef void XVector_iterator;
XVector_iterator* XVector_begin(XVector* this_vector);
XVector_iterator* XVector_end(XVector* this_vector);
XVector_iterator* XVector_iterator_add(XVector* this_vector, XVector_iterator*it);
void XVector_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
