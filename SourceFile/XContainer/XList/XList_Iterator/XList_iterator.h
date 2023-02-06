#ifndef  XLIST_ITERATOR_H
#define XLIST_ITERATOR_H
#include"XFunctionCallback.h"
struct XList;
//正向迭代器
typedef struct XList_iterator
{
	char null;
}XList_iterator;
struct XList_iterator* XList_begin(struct XList* this_list);
struct XList_iterator* XList_end(struct XList* this_list);
struct XList_iterator* XList_iterator_add(struct XList* this_list, struct XList_iterator*it);
void XList_iterator_for_each(struct XList* this_list, XFor_each ForFunction, void* args);
#endif // ! ITERATOR_H
