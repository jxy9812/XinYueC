#ifndef  XVECTOR_ITERATOR_H
#define XVECTOR_ITERATOR_H
#include"XFunctionCallback.h"
struct XVector;
////容器for_each(容器循环遍历)回调函数
//typedef void (*XFor_each)(void* LPVal);
//正向迭代器
typedef void XVector_iterator;
XVector_iterator* XVector_begin(struct XVector* this_vector);
XVector_iterator* XVector_end(struct XVector* this_vector);
XVector_iterator* XVector_iterator_add(struct XVector* this_vector, struct XVector_iterator*it);
void XVector_iterator_for_each(struct XVector* this_vector, XFor_each ForFunction, void* args);
#endif // ! ITERATOR_H
