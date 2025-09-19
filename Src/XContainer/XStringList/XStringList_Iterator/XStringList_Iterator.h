#include"CXinYueConfig.h"
#if !defined(XSTRINGVECTOR_ITERATOR_H)&& XStringList_ON
#define XSTRINGVECTOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XVector_iterator.h"
XContainerTypeDeclare(XStringList);
//正向迭代器
typedef XVector_iterator XStringList_iterator ;
typedef struct XString XString;
#define XStringList_begin						XVector_begin
#define XStringList_end							XVector_end
#define XStringList_iterator_isEnd				XVector_iterator_isEnd
#define XStringList_iterator_add				XVector_iterator_add
#define XStringList_iterator_equality			XVector_iterator_equality
#define XStringList_iterator_for_each			XVector_iterator_for_each
#define XStringList_iterator_data				XVector_iterator_data
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
