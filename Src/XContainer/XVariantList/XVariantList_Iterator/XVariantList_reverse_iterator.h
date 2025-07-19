#include"XDataStructConfig.h"
#if !defined(XVARIANTLIST_REVERSE_ITERATOR_H)&& XVariantList_ON
#define XVARIANTLIST_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XVector_reverse_iterator.h"
//声明
XContainerTypeDeclare(XVariantList);
//反向迭代器
//typedef struct XVariantList_reverse_iterator
//{
//	void* data;
//}XVariantList_reverse_iterator;

typedef  XVector_reverse_iterator XVariantList_reverse_iterator;
XVariantList_reverse_iterator XVariantList_rbegin(XVariantList* list);
XVariantList_reverse_iterator XVariantList_rend(XVariantList* list);
void XVariantList_reverse_iterator_add(XVariantList* list,XVariantList_reverse_iterator* it);
bool XVariantList_reverse_iterator_equality(XVariantList_reverse_iterator* itFirst, XVariantList_reverse_iterator* itSecond);
void XVariantList_reverse_iterator_for_each(XVariantList* list, XFor_each ForFunction, void* args);
XVariant* XVariantList_reverse_iterator_data(XVariantList_reverse_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H