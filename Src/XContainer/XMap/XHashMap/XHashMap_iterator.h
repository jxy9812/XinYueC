#include"XDataStructConfig.h"
#if !defined(XHASHMAP_ITERATOR_H)&& XHashMap_ON
#define XHASHMAP_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XFunctionCallback.h"
XContainerTypeDeclare(XHashMap);
XContainerTypeDeclare(XPair);
typedef struct
{
	size_t index;//当前index;
	void* node;//当前节点
}XHashMap_iterator;

XHashMap_iterator XHashMap_begin(XHashMap* this_map);
XHashMap_iterator XHashMap_end(XHashMap* this_map);
void XHashMap_iterator_add(XHashMap* this_map, XHashMap_iterator* it);
bool XHashMap_iterator_equality(XHashMap_iterator* itFirst, XHashMap_iterator* itSecond);
void XHashMap_iterator_for_each(XHashMap* this_map, XFor_each ForFunction, void* args);
XPair* XHashMap_iterator_data(XHashMap_iterator* it);
#ifdef __cplusplus
}
#endif
#endif