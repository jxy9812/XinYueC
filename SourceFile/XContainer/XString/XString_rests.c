#include"XString.h"
#include"XString_head.h"
#include"XVector.h"
#include"XVector_head.h"
#include"XContainerObject.h"
#include"XAlgorithm.h"
#include<stdlib.h>
//判断函数
bool XString_empty(const struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XSTRING* string = (struct XSTRING*)this_XString;
	return string->_size==0;
}
//返回当前元素大小
int XString_size(const struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XSTRING* string = (struct XSTRING*)this_XString;
	return string->_size;
}
////返回当前容器的最大容量
//int XString_capacity(const struct XString* this_XString)
//{
//	if (isObjectNULL(this_XString, "XString_capacity"))
//		return NULL;
//	struct XSTRING* string = (struct XSTRING*)this_XString;
//	return XVector_capacity(string->_data);
//}
//交换
void XString_swap(struct XString* this_XStringOne, struct XString* this_XStringTwo)
{
	if (isNULL(isNULLInfo(this_XStringOne, ""))|| isNULL(isNULLInfo(this_XStringTwo, "")))
		return NULL;
	struct XSTRING* stringOne = (struct XSTRING*)this_XStringOne;
	struct XSTRING* stringTwo = (struct XSTRING*)this_XStringTwo;
	XVector_swap(stringOne->_data, stringTwo->_data);
	swap(&stringOne->_size, &stringTwo->_size, sizeof(size_t));
}
//释放容器
void XString_free(const struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XSTRING* string = (struct XSTRING*)this_XString;
	XVector_free(string->_data);
	free(this_XString);
}