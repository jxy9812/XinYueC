#include"CXinYueConfig.h"
#if !defined(XVARIANTLIST_REVERSE_ITERATOR_H)&& XVariantList_ON
#define XVARIANTLIST_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"
#include"XVector_reverse_iterator.h"
//声明
XContainerTypeDeclare(XVariantList);
//反向迭代器
//typedef struct XVariantList_reverse_iterator
//{
//	void* data;
//}XVariantList_reverse_iterator;

typedef  XVector_reverse_iterator XVariantList_reverse_iterator;
#define XVariantList_rbegin								XVector_rbegin
#define XVariantList_rend								XVector_rend
#define XVariantList_reverse_iterator_isRend			XVector_reverse_iterator_isRend
#define XVariantList_reverse_iterator_add				XVector_reverse_iterator_add
#define XVariantList_reverse_iterator_equality          XVector_reverse_iterator_equality
#define XVariantList_reverse_iterator_for_each			XVector_reverse_iterator_for_each
#define XVariantList_reverse_iterator_data				XVector_reverse_iterator_data
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H