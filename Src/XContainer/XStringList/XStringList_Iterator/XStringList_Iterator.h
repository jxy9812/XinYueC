#include"XDataStructConfig.h"
#if !defined(XSTRINGVECTOR_ITERATOR_H)&& XStringList_ON
#define XSTRINGVECTOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XVector_iterator.h"
XContainerTypeDeclare(XStringList);
//正向迭代器
typedef XVector_iterator XStringList_iterator ;
typedef struct XString XString;
XStringList_iterator XStringList_begin(XStringList* this_XStringList);
XStringList_iterator XStringList_end(XStringList* this_XStringList);
void XStringList_iterator_add(XStringList* this_XStringList, XStringList_iterator* it);
bool XStringList_iterator_equality(XStringList_iterator* itFirst, XStringList_iterator* itSecond);
void XStringList_iterator_for_each(XStringList* this_XStringList, XFor_each ForFunction, void* args);
XString* XStringList_iterator_data(XStringList_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
