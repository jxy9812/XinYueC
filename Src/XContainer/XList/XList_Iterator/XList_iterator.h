#include"XDataStructConfig.h"
#if !defined(XLIST_ITERATOR_H)&& XList_ON
#define XLIST_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
typedef struct XList XList;
typedef struct XListNode XListNode;
//正向迭代器
typedef XListNode XList_iterator;
XList_iterator* XList_begin(XList* this_list);
XList_iterator* XList_end(XList* this_list);
XList_iterator* XList_iterator_add(XList* this_list,XList_iterator*it);
void XList_iterator_for_each(XList* this_list, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
