#include"XString.h"
//判断是中文
bool XString_isChinese(const char c);
//返回字符串中字符数量，中文算一个
size_t XString_charNumber(const char* str);
//返回对应XVector的索引
size_t XString_XVectorNsel(const struct XString* this_XString, const size_t nSel);

void XString_class_init()
{

}
/*
bool XString_isChinese(const char c)
{
	return (c & 0x8000);//是中文;
}
size_t XString_charNumber(const char* str)
{
	size_t sum = 0;
	for (size_t i = 0; i < strlen(str); i++)
	{
		if (XString_isChinese(str[i]))
			++i;
		++sum;
	}
	return sum;
}

size_t XString_XVectorNsel(const struct XString* this_XString, const size_t nSel)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	if (nSel < 0)
		return -1;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	size_t VnSel = -1;
	for (size_t i = 0; i < XVector_size(v) - 1; i++)
	{
		char c = *((char*)XVector_at(v, i));
		if (XString_isChinese(c))
			++i;
		++VnSel;
		if (VnSel == nSel)
			return i;
	}
	return -1;
}

//删除索引处字符
bool XString_eraseOne(struct XString* this_XString, const int nSel)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return false;
	if (nSel < 0)
		return false;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	size_t VnSel = XString_XVectorNsel(this_XString, nSel);
	if (VnSel == -1)
		return false;

	int offset = 0;
	if (XString_isChinese(*((char*)XVector_at(v, VnSel))))//是中文
		++offset;
	//XVector_erase_int(v, VnSel - offset, VnSel);
	XVector_remove(v, VnSel - offset, offset);
	((struct XString*)this_XString)->_size -= 1;
	return true;
}
//尾删
void XString_pop_back(struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	XString_eraseOne(this_XString, XString_size(this_XString) - 1);
}
//删除索引处开始的n个字符
void XString_erase(struct XString* this_XString, const int nSel, const int n)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	if (nSel < 0 || n <= 0)
		return;
	for (size_t i = 0; i < n; i++)
	{
		if (!XString_eraseOne(this_XString, nSel))
			break;
	}
}
//清空字符串
void XString_clear(struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	int right = XVector_size(v) - 2;
	if (right >= 0)
		//XVector_erase_int(v, 0, right);
		XVector_remove(v, 0, right);
	((struct XString*)this_XString)->_size = 0;
}

//查找函数
int XString_find_first_of(const struct XString* this_XString, const char* find)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(find);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] == find[j])
				return i;
		}
	}
	return -1;
}
int XString_find_last_of(const struct XString* this_XString, const char* find)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(find);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[sLen - i - 1] == find[j])
				return sLen - i - 1;
		}
	}
	return -1;
}
int XString_find_first_not_of(const struct XString* this_XString, const char* find)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(find);
	for (size_t i = 0; i < sLen; i++)
	{
		size_t n = 0;
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] == find[j])
				break;
			n++;
		}
		if (n == fLen)
			return  i;
	}
	return -1;
}
int XString_find_last_not_of(const struct XString* this_XString, const char* find)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(find);
	for (size_t i = 0; i < sLen; i++)
	{
		size_t n = 0;
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[sLen - i - 1] == find[j])
				break;
			n++;
		}
		if (n == fLen)
			return sLen - i - 1;
	}
	return -1;
}

//尾插
void XString_append(struct XString* this_XString, const char* str)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	XString_insert(this_XString, XVector_size(v) - 1, str);
}
// 赋值
void XString_assign(struct XString* this_XString, const char* str)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	XString_clear(this_XString);
	XString_insert(this_XString, 0, str);
}
// 第索引处开始插入字符串
void XString_insert(struct XString* this_XString, const int nSel, const char* str)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	if (str == NULL || nSel < 0)
		return;
	struct XString* string = (struct XString*)this_XString;
	struct XVector* v = string->_data;

	//XVector_insert(v, nSel, str, str + strlen(str) - 1);
	((struct XString*)this_XString)->_size += XString_charNumber(str);
}

//判断函数
bool XString_empty(const struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return string->_size == 0;
}
//返回当前元素大小
int XString_size(const struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return string->_size;
}
////返回当前容器的最大容量
//int XString_capacity(const struct XString* this_XString)
//{
//	if (isObjectNULL(this_XString, "XString_capacity"))
//		return NULL;
//	struct XString* string = (struct XString*)this_XString;
//	return XVector_capacity(string->_data);
//}
//交换
void XString_swap(struct XString* this_XStringOne, struct XString* this_XStringTwo)
{
	if (isNULL(isNULLInfo(this_XStringOne, "")) || isNULL(isNULLInfo(this_XStringTwo, "")))
		return NULL;
	struct XString* stringOne = (struct XString*)this_XStringOne;
	struct XString* stringTwo = (struct XString*)this_XStringTwo;
	XVector_swap(stringOne->_data, stringTwo->_data);
	XSwap(&stringOne->_size, &stringTwo->_size, sizeof(size_t));
}
//释放容器
void XString_free(const struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	XVector_free(string->_data);
	free(this_XString);
}

// 返回索引处字符
char XString_at(const XString* this_XString, int nSel)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return *((char*)XVector_at(string->_data, nSel));
}
// 返回字符串
char* XString_data(const XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return string->_data->object._data;
}
*/
