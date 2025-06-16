#include"XDataStructConfig.h"
#if !defined(XSTRINGVECTOR_ITERATOR_H)&& XStringVector_ON
#define XSTRINGVECTOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XVector_iterator.h"
XContainerTypeDeclare(XStringVector);
//正向迭代器
typedef XVector_iterator XStringVector_iterator ;
typedef struct XString XString;
XStringVector_iterator XStringVector_begin(XStringVector* this_XStringVector);
XStringVector_iterator XStringVector_end(XStringVector* this_XStringVector);
void XStringVector_iterator_add(XStringVector* this_XStringVector, XStringVector_iterator* it);
bool XStringVector_iterator_equality(XStringVector_iterator* itFirst, XStringVector_iterator* itSecond);
void XStringVector_iterator_for_each(XStringVector* this_XStringVector, XFor_each ForFunction, void* args);
XString* XStringVector_iterator_data(XStringVector_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
