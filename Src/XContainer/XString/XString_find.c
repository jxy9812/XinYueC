#include"XString.h"
#include"XString_head.h"
#include"XContainerObject.h"
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