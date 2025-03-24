#ifndef  XLIST_ITERATOR_H
#define XLIST_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
struct XList;
typedef struct XListNode XListNode;
//正向迭代器
typedef XListNode XList_iterator;
XList_iterator* XList_begin(struct XList* this_list);
XList_iterator* XList_end(struct XList* this_list);
XList_iterator* XList_iterator_add(struct XList* this_list,XList_iterator*it);
void XList_iterator_for_each(struct XList* this_list, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
