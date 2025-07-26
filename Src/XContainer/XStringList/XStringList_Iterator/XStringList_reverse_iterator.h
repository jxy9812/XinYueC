#include"XDataStructConfig.h"
#if !defined(XSTRINGVECTOR_REVERSE_ITERATOR_H)&& XStringList_ON
#define XSTRINGVECTOR_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XVector_reverse_iterator.h"
XContainerTypeDeclare(XStringList);
//反向迭代器
typedef XVector_reverse_iterator XStringList_reverse_iterator;
#define XStringList_rbegin								XVector_rbegin
#define XStringList_rend								XVector_rend
#define XStringList_reverse_iterator_add				XVector_reverse_iterator_add
#define XStringList_reverse_iterator_equality          XVector_reverse_iterator_equality
#define XStringList_reverse_iterator_for_each			XVector_reverse_iterator_for_each
#define XStringList_reverse_iterator_data				XVector_reverse_iterator_data
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H