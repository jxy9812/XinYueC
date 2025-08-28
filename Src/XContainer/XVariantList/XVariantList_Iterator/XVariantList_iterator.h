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
#define XVariantList_begin						XVector_begin
#define XVariantList_end						XVector_end
#define XVariantList_iterator_isEnd				XVector_iterator_isEnd
#define XVariantList_iterator_add				XVector_iterator_add
#define XVariantList_iterator_equality          XVector_iterator_equality
#define XVariantList_iterator_for_each			XVector_iterator_for_each
#define XVariantList_iterator_data				XVector_iterator_data
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
