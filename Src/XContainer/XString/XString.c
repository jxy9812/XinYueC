#include"XString.h"
#if XString_ON
#include<stdlib.h>
#include<string.h>
#include"XEquality.h"
XString* XString_create(const char* string)
{
	XString* this_string = XMemory_malloc(sizeof(XString));
	XString_init(this_string);
	if(string)
		XString_append_base(this_string,string);
	return this_string;
}
void XString_init(XString* this_string)
{
	if (ISNULL(this_string, "") )
		return;
	XVector_init(this_string, sizeof(char));
	this_string->m_vector.m_equality = XEquality_char;
	XClassGetVtable(this_string) = XString_class_init();
	XString_clear_base(this_string);
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

void XString_append_base(XString* this_string, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, const char*);
	XClassGetVirtualFunc(this_string, EXString_Append, funcPtr)(this_string, string);
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
#endif