#include"CXinYueConfig.h"
#if !defined(XLISTSLINKEDATOMIC_ITERATOR_H) && XListSLinkedAtomic_ON
#define XLISTSLINKEDATOMIC_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XListBase_iterator.h"
XContainerTypeDeclare(XListSLinkedAtomic);
XContainerTypeDeclare(XListSNodeAtomic);
// 正向迭代器
typedef XListBase_iterator XListSLinkedAtomic_iterator;

XListSLinkedAtomic_iterator XListSLinkedAtomic_begin(XListSLinkedAtomic* this_list);
XListSLinkedAtomic_iterator XListSLinkedAtomic_end(XListSLinkedAtomic* this_list);
bool XListSLinkedAtomic_iterator_isEnd(const XListSLinkedAtomic_iterator* it);
void XListSLinkedAtomic_iterator_add(XListSLinkedAtomic* this_list, XListSLinkedAtomic_iterator* it);
bool XListSLinkedAtomic_iterator_equality(XListSLinkedAtomic_iterator* itFirst, XListSLinkedAtomic_iterator* itSecond);
void XListSLinkedAtomic_iterator_for_each(XListSLinkedAtomic* this_list, XFor_each ForFunction, void* args);
void* XListSLinkedAtomic_iterator_data(XListSLinkedAtomic_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // ! XLISTSLINKEDATOMIC_ITERATOR_H