#ifndef XLIST_REVERSE_ITERATOR_H
#define XLIST_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
struct XList;
typedef struct XListNode XListNode;
//反向迭代器
typedef XListNode XList_reverse_iterator;
XList_reverse_iterator* XList_rbegin(struct XList* this_list);
XList_reverse_iterator* XList_rend(struct XList* this_list);
XList_reverse_iterator* XList_reverse_iterator_add(struct XList* this_list, XList_reverse_iterator* it);
void XList_reverse_iterator_for_each(struct XList* this_list, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H