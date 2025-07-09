#include"XString.h"
#if XString_ON
#include<stdlib.h>
#include<string.h>
#include<stdarg.h>
#include"XEquality.h"
#include"XStringList.h"
XString* XString_create(const char* string)
{
	XString* this_string = XMemory_malloc(sizeof(XString));
	XString_init(this_string);
	if(string)
		XString_append_base(this_string,string);
	return this_string;
}
XString* XString_create_fmt(const char* format, ...)
{
	va_list args;
	va_start(args, format);

	// 计算所需缓冲区大小
	int len = vsnprintf(NULL, 0, format, args);
	va_end(args);

	if (len <= 0) return NULL;
	XString* str = XString_create(NULL);
	if (str == NULL)
		return NULL;

	// 分配内存缓冲区
	if(!XString_resize_base(str,len))
	{
		XString_delete_base(str);
		return NULL;
	}
	// 格式化文本
	va_start(args, format);
	vsnprintf(XContainerDataPtr(str), len + 1, format, args);
	va_end(args);
	return str;
}
XString* XString_create_with_length(const char* string, size_t len)
{
	if (string == NULL || len == 0 || *string == 0)
		return NULL;
	XString* this_string = XString_create(NULL);
	XString_resize_base(this_string, len);
	memcpy(XContainerDataPtr(this_string),string,len);
	return this_string;
}
void XString_init(XString* this_string)
{
	if (ISNULL(this_string, "") )
		return;
	XVector_init(this_string, sizeof(char));
	this_string->m_vector.m_equality = XEquality_char;
	XClassGetVtable(this_string) = XString_class_init();
	XString_resize_base(this_string,0);
}

//void XString_push_front_base(XString* this_string, char c)
//{
//	XVector_push_front_base(this_string,&c);
//}
//
//void XString_push_back_base(XString* this_string, char c)
//{
//	//XVector_push_back_base(this_string, &c);
//	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
//		return;
//	typedef void (*funcPtr)(XString*, char);
//	XClassGetVirtualFunc(this_string, EXString_Push_Back, funcPtr)(this_string, c);
//}

void XString_append_base(XString* this_string, const char* str)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, const char*);
	XClassGetVirtualFunc(this_string, EXString_Append, funcPtr)(this_string, str);
}

void XString_append_string(XString* this_string, const XString* string)
{
	if (this_string == NULL || string == NULL)
		return;
	XString_append_base(this_string,XString_c_str(string));
}

void XString_assign_base(XString* this_string, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, const char*);
	XClassGetVirtualFunc(this_string, EXString_Assign, funcPtr)(this_string, string);
}

void XString_insert_base(XString* this_string, const int64_t index, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), "")||ISNULL(string, ""))
		return;
	typedef void (*funcPtr)(XString*, int64_t, const char*);
	XClassGetVirtualFunc(this_string, EXString_Insert, funcPtr)(this_string, index, string);
}
const char* XString_data(const XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return NULL;
	typedef const char* (*funcPtr)(const XString*);
	return XClassGetVirtualFunc(this_string, EXString_Data, funcPtr)(this_string);
}

int64_t XString_find_first_of(const XString* this_string, const char* subStr)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return -1;
	typedef const char* (*funcPtr)(const XString*, const char*);
	return XClassGetVirtualFunc(this_string, EXString_Find_First_Of, funcPtr)(this_string,subStr);
}

int64_t XString_find_last_of(const XString* this_string, const char* subStr)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return -1;
	typedef const char* (*funcPtr)(const XString*, const char*);
	return XClassGetVirtualFunc(this_string, EXString_Find_Last_Of, funcPtr)(this_string, subStr);
}

int64_t XString_find_first_not_of(const XString* this_string, const char* subStr)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return -1;
	typedef const char* (*funcPtr)(const XString*, const char*);
	return XClassGetVirtualFunc(this_string, EXString_Find_First_Not_Of, funcPtr)(this_string, subStr);
}

int64_t XString_find_last_not_of(const XString* this_string, const char* subStr)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return -1;
	typedef const char* (*funcPtr)(const XString*, const char*);
	return XClassGetVirtualFunc(this_string, EXString_Find_Last_Not_Of, funcPtr)(this_string, subStr);
}
XString* XString_to16HexString(const uint8_t* data, size_t dataSize)
{
	if (data == NULL || dataSize == 0)
		return NULL;
	XString* str = XString_create(NULL);
	char buff[10];
	for (size_t i = 0; i < dataSize; i++)
	{ 
		sprintf(buff, "%02X ", data[i]);
		XString_append_base(str, buff);
	}
	XString_pop_back_base(str);
	return str;
}
XStringList* XString_split(XString* this_string, const char* sep)
{
	if(this_string==NULL|| sep==NULL||XString_isEmpty_base(this_string) || *sep == '\0')
		return NULL;
	// 创建结果向量
	XStringList* result = XStringList_create();
	if (result == NULL) {
		return NULL;
	}

	const char* str = XContainerDataPtr(this_string);
	size_t str_len = XContainerSize(this_string);
	size_t sep_len = strlen(sep);

	const char* start = str;
	const char* end;

	// 查找所有分隔符位置并分割字符串
	while ((end = strstr(start, sep)) != NULL) {
		size_t token_len = end - start;
		/*printf("添加\n");*/
		// 创建子字符串
		XString* token = XString_create_with_length(start, token_len);
		if (token == NULL) {
			XStringList_delete_base(result);
			return NULL;
		}

		// 添加到结果向量
		XStringList_push_back_base(result, token);

		// 移动到下一个可能的起始位置
		start = end + sep_len;
	}
	// 处理最后一个分隔符后的剩余部分
	size_t remaining_len = str + str_len - start;
	if (remaining_len > 0 || start == str) {  // 包含空字符串的情况
		XString* token = XString_create_with_length(start, remaining_len);
		if (token == NULL) {
			XStringList_delete_base(result);
			return NULL;
		}

		XStringList_push_back_base(result, token);
	}

	return result;
}
int XString_toInt(XString* this_string)
{
	if (this_string == NULL || XString_isEmpty_base(this_string))
		return 0;
	return atoi(XContainerDataPtr(this_string));
}
long  XString_toLong(XString* this_string,int radix)
{
	if(this_string==NULL||XString_isEmpty_base(this_string))
		return 0;
	return strtol(XContainerDataPtr(this_string),NULL, radix);
}
unsigned long XString_toULong(XString* this_string, int radix)
{
	if (this_string == NULL || XString_isEmpty_base(this_string))
		return 0;
	return strtoul(XContainerDataPtr(this_string), NULL, radix);
}
long long XString_toLongLong(XString* this_string, int radix)
{
	if (this_string == NULL || XString_isEmpty_base(this_string))
		return 0;
	return strtoll(XContainerDataPtr(this_string), NULL, radix);
}
unsigned long long XString_toULongLong(XString* this_string, int radix)
{
	if (this_string == NULL || XString_isEmpty_base(this_string))
		return 0;
	return strtoull(XContainerDataPtr(this_string), NULL, radix);
}
float XString_toFloat(XString* this_string)
{
	if (this_string == NULL || XString_isEmpty_base(this_string))
		return 0;
	return strtof(XContainerDataPtr(this_string), NULL);
}
double XString_toDouble(XString* this_string)
{
	if (this_string == NULL || XString_isEmpty_base(this_string))
		return 0;
	return strtod(XContainerDataPtr(this_string),NULL);
}
#endif