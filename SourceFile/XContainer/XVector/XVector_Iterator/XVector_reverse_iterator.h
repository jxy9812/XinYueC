#ifndef XVECTOR_REVERSE_ITERATOR_H
#define XVECTOR_REVERSE_ITERATOR_H
#include"XFunctionCallback.h"
struct XVector;
//反向迭代器
typedef void XVector_reverse_iterator;
struct XVector_reverse_iterator* XVector_rbegin(struct XVector* this_vector);
struct XVector_reverse_iterator* XVector_rend(struct XVector* this_vector);
struct XVector_reverse_iterator* XVector_reverse_iterator_add(struct XVector* this_vector, struct XVector_reverse_iterator* it);
void XVector_reverse_iterator_for_each(struct XVector* this_vector, XFor_each ForFunction, void* args);
#endif // !REVERSE_ITERATOR_H