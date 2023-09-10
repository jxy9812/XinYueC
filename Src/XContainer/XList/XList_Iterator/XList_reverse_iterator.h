#ifndef XLIST_REVERSE_ITERATOR_H
#define XLIST_REVERSE_ITERATOR_H
#include"XFunctionCallback.h"
struct XList;
//反向迭代器
typedef void XList_reverse_iterator;
XList_reverse_iterator* XList_rbegin(struct XList* this_list);
XList_reverse_iterator* XList_rend(struct XList* this_list);
XList_reverse_iterator* XList_reverse_iterator_add(struct XList* this_list, XList_reverse_iterator* it);
void XList_reverse_iterator_for_each(struct XList* this_list, XFor_each ForFunction, void* args);
#endif // !REVERSE_ITERATOR_H