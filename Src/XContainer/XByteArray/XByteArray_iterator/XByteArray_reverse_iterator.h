#include"CXinYueConfig.h"
#if !defined(XBYTEARRAY_REVERSE_ITERATOR_H)&& XByteArray_ON
#define XBYTEARRAY_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"
#include"XVector_reverse_iterator.h"
//声明
XContainerTypeDeclare(XByteArray);
//反向迭代器
//typedef struct XByteArray_reverse_iterator
//{
//	void* data;
//}XByteArray_reverse_iterator;

typedef  XVector_reverse_iterator XByteArray_reverse_iterator;
#define XByteArray_rbegin								XVector_rbegin
#define XByteArray_rend									XVector_rend
#define XByteArray_reverse_iterator_add					XVector_reverse_iterator_add
#define XByteArray_reverse_iterator_equality			XVector_reverse_iterator_equality
#define XByteArray_reverse_iterator_for_each			XVector_reverse_iterator_for_each
#define XByteArray_reverse_iterator_data				XVector_reverse_iterator_data
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H