#include"XDataStructConfig.h"
#if !defined(XVARIANTLIST_ITERATOR_H)&& XVariantList_ON
#define XVARIANTLIST_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XVector_iterator.h"
//声明
XContainerTypeDeclare(XVariantList);
//正向迭代器
//typedef struct XVariantList_iterator
//{
//	void* data;
//}XVariantList_iterator;

typedef  XVector_iterator XVariantList_iterator;
XVariantList_iterator XVariantList_begin(XVariantList* list);
XVariantList_iterator XVariantList_end(XVariantList* list);
void XVariantList_iterator_add(XVariantList* list, XVariantList_iterator*it);
bool XVariantList_iterator_equality(XVariantList_iterator* itFirst, XVariantList_iterator* itSecond);
void XVariantList_iterator_for_each(XVariantList* list, XFor_each ForFunction, void* args);
XVariant* XVariantList_iterator_data(XVariantList_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
