#include"XDataStructConfig.h"
#if !defined(XSET_REVERSE_ITERATOR_H)&& XSet_ON
#define XSET_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XFunctionCallback.h"
XContainerTypeDeclare(XSet);
XContainerTypeDeclare(XPair);
typedef struct XSet_reverse_iterator
{
	void* node;//当前节点
}XSet_reverse_iterator;
XSet_reverse_iterator XSet_rbegin(XSet* this_map);
XSet_reverse_iterator XSet_rend(XSet* this_map);
bool XSet_reverse_iterator_isRend(const XSet_reverse_iterator* it);
XSet_reverse_iterator* XSet_reverse_iterator_add(XSet* this_map, XSet_reverse_iterator* it);
bool XSet_reverse_iterator_equality(XSet_reverse_iterator* itFirst, XSet_reverse_iterator* itSecond);
void XSet_reverse_iterator_for_each(XSet* this_map, XFor_each ForFunction, void* args);
void* XSet_reverse_iterator_data(XSet_reverse_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // !XMAP_REVERSE_ITERATOR_H
