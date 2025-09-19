#include"CXinYueConfig.h"
#if !defined(XString_REVERSE_ITERATOR_H)&& XString_ON
#define XString_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XChar.h"
//声明
XContainerTypeDeclare(XString);
//正向迭代器
typedef struct XString_reverse_iterator
{
	void* data;
}XString_reverse_iterator;
XString_reverse_iterator XString_rbegin(XString* str);
XString_reverse_iterator XString_rend(XString* str);
bool XString_reverse_iterator_isRend(const XString_reverse_iterator* it);
void XString_reverse_iterator_add(XString* str, XString_reverse_iterator* it);
bool XString_reverse_iterator_equality(XString_reverse_iterator* itFirst, XString_reverse_iterator* itSecond);
void XString_reverse_iterator_for_each(XString* str, XFor_each ForFunction, void* args);
XChar* XString_reverse_iterator_data(XString_reverse_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H