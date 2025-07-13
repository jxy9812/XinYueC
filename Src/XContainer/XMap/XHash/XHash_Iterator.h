#include"XDataStructConfig.h"
#if !defined(XHASHMAP_ITERATOR_H)&& XHash_ON
#define XHASHMAP_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XFunctionCallback.h"
XContainerTypeDeclare(XHash);
XContainerTypeDeclare(XPair);
typedef struct
{
	size_t index;//当前index;
	void* node;//当前节点
}XHash_iterator;

XHash_iterator XHash_begin(XHash* this_map);
XHash_iterator XHash_end(XHash* this_map);
void XHash_iterator_add(XHash* this_map, XHash_iterator* it);
bool XHash_iterator_equality(XHash_iterator* itFirst, XHash_iterator* itSecond);
void XHash_iterator_for_each(XHash* this_map, XFor_each ForFunction, void* args);
XPair* XHash_iterator_data(XHash_iterator* it);
#ifdef __cplusplus
}
#endif
#endif