#include"CXinYueConfig.h"
#if !defined(XBYTEARRAY_ITERATOR_H)&& XByteArray_ON
#define XBYTEARRAY_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XVector_iterator.h"
//声明
XContainerTypeDeclare(XByteArray);
//正向迭代器
//typedef struct XByteArray_iterator
//{
//	void* data;
//}XByteArray_iterator;

typedef  XVector_iterator XByteArray_iterator;
#define XByteArray_begin						XVector_begin
#define XByteArray_end						XVector_end
#define XByteArray_iterator_add				XVector_iterator_add
#define XByteArray_iterator_equality          XVector_iterator_equality
#define XByteArray_iterator_for_each			XVector_iterator_for_each
#define XByteArray_iterator_data				XVector_iterator_data
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
