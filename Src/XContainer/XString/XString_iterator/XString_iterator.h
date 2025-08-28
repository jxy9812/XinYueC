#include"XDataStructConfig.h"
#if !defined(XSTRING_ITERATOR_H)&& XString_ON
#define XSTRING_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XChar.h"
//声明
XContainerTypeDeclare(XString);
//正向迭代器
typedef struct XString_iterator
{
	void* data;
}XString_iterator;
XString_iterator XString_begin(XString* str);
XString_iterator XString_end(XString* str);
bool XString_iterator_isEnd(const XString_iterator* it);
void XString_iterator_add(XString* str, XString_iterator* it);
bool XString_iterator_equality(XString_iterator* itFirst, XString_iterator* itSecond);
void XString_iterator_for_each(XString* str, XFor_each ForFunction, void* args);
XChar* XString_iterator_data(XString_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H