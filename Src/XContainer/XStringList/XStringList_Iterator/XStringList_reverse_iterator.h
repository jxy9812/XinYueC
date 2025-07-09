#include"XDataStructConfig.h"
#if !defined(XSTRINGVECTOR_REVERSE_ITERATOR_H)&& XStringList_ON
#define XSTRINGVECTOR_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XVector_reverse_iterator.h"
XContainerTypeDeclare(XStringList);
//反向迭代器
typedef XVector_reverse_iterator XStringList_reverse_iterator;
XStringList_reverse_iterator XStringList_rbegin(XStringList* this_XStringList);
XStringList_reverse_iterator XStringList_rend(XStringList* this_XStringList);
void XStringList_reverse_iterator_add(XStringList* this_XStringList, XStringList_reverse_iterator* it);
bool XStringList_reverse_iterator_equality(XStringList_reverse_iterator* itFirst, XStringList_reverse_iterator* itSecond);
void XStringList_reverse_iterator_for_each(XStringList* this_XStringList, XFor_each ForFunction, void* args);
void* XStringList_reverse_iterator_data(XStringList_reverse_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H