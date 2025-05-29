#include"XString.h"
#if XString_ON
#include<stdlib.h>
#include<string.h>
#include"XEquality.h"
XString* XString_new(const char* string)
{
	XString* this_string = XMemory_malloc(sizeof(XString));
	XString_init(this_string);
	if(string)
		XString_append(this_string,string);
	return this_string;
}
void XString_init(XString* this_string)
{
	if (ISNULL(this_string, "") )
		return;
	XVector_init(this_string, sizeof(char));
	this_string->m_vector.m_equality = XEquality_char;
	XString_class_init();
	XClassGetVtable(this_string) = XStringVtable;
	XString_clear(this_string);
}

void XString_resize(XString* this_string, size_t len)
{
	XVector_resize_base(this_string,len);
}

void XString_push_front(XString* this_string, char c)
{
	XVector_push_front_base(this_string,&c);
}

void XString_push_back(XString* this_string, char c)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, char);
	XClassGetVirtualFunc(this_string, EXString_Push_Back, funcPtr)(this_string, c);
}

void XString_append(XString* this_string, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, const char*);
	XClassGetVirtualFunc(this_string, EXString_Append, funcPtr)(this_string, string);
}

void XString_assign(XString* this_string, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, const char*);
	XClassGetVirtualFunc(this_string, EXString_Assign, funcPtr)(this_string, string);
}

void XString_insert(XString* this_string, const int64_t index, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), "")||ISNULL(string, ""))
		return;
	typedef void (*funcPtr)(XString*, int64_t, const char*);
	XClassGetVirtualFunc(this_string, EXString_Insert, funcPtr)(this_string, index, string);
}

void XString_pop_front(XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*);
	XClassGetVirtualFunc(this_string, EXString_Pop_Front, funcPtr)(this_string);
}

void XString_pop_back(XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*);
	XClassGetVirtualFunc(this_string, EXString_Pop_Back, funcPtr)(this_string);
}

void XString_erase(XString* this_string, void* LpValue)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, void*);
	XClassGetVirtualFunc(this_string, EXVector_Erase, funcPtr)(this_string, LpValue);
}

bool XString_isEmpty(const XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return true;
	typedef bool(*funcPtr)(XString*);
	return XClassGetVirtualFunc(this_string, EXString_Empty, funcPtr)(this_string);
}

size_t XString_size(const XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return 0;
	typedef size_t (*funcPtr)(XString*);
	return XClassGetVirtualFunc(this_string, EXString_Size, funcPtr)(this_string);
	//XVector_getSize_base(this_string);
}

void XString_swap(XString* this_stringOne, XString* this_stringTwo)
{
	XVector_swap_base(this_stringOne,this_stringTwo);
}

void XString_free(const XString* this_string)
{
	XVector_free_base(this_string);
}

void XString_remove(XString* this_string, int64_t index, int64_t n)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef size_t(*funcPtr)(XString*, int64_t, int64_t);
	return XClassGetVirtualFunc(this_string, EXVector_Remove, funcPtr)(this_string,index,n);
}

void XString_clear(XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return;
	typedef size_t(*funcPtr)(XString*);
	return XClassGetVirtualFunc(this_string, EXContainerObject_Clear, funcPtr)(this_string);
}

char XString_at(const XString* this_string, int64_t index)
{
	return XVector_At_Base(this_string,index,char);
}

const char* XString_data(const XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(XClassGetVtable(this_string), ""))
		return NULL;
	typedef const char* (*funcPtr)(const XString*);
	return XClassGetVirtualFunc(this_string, EXString_Data, funcPtr)(this_string);
}

const char* XString_c_str(const XString* this_string)
{
	return XString_data(this_string);
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
#endif