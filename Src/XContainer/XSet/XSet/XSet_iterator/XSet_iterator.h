#include"XDataStructConfig.h"
#if !defined(XSET_ITERATOR_H)&& XSet_ON
#define XSET_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XFunctionCallback.h"
XContainerTypeDeclare(XSet);
XContainerTypeDeclare(XPair);
typedef struct XSet_iterator
{
	void* node;//当前节点
}XSet_iterator;
XSet_iterator XSet_begin(XSet* this_map);
XSet_iterator XSet_end(XSet* this_map);
bool XSet_iterator_isEnd(const XSet_iterator* it);
void XSet_iterator_add(XSet* this_map, XSet_iterator* it);
bool XSet_iterator_equality(XSet_iterator* itFirst, XSet_iterator* itSecond);
void XSet_iterator_for_each(XSet* this_map, XFor_each ForFunction, void* args);
void* XSet_iterator_data(XSet_iterator* it);
#ifdef __cplusplus
}
#endif
#endif