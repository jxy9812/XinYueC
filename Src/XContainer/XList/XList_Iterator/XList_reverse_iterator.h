#include"XDataStructConfig.h"
#if !defined(XLIST_REVERSE_ITERATOR_H)&& XList_ON
#define XLIST_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
typedef struct XList XList;
typedef struct XListNode XListNode;
//反向迭代器
typedef XListNode XList_reverse_iterator;
XList_reverse_iterator* XList_rbegin(XList* this_list);
XList_reverse_iterator* XList_rend(XList* this_list);
XList_reverse_iterator* XList_reverse_iterator_add(XList* this_list, XList_reverse_iterator* it);
void XList_reverse_iterator_for_each(XList* this_list, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H