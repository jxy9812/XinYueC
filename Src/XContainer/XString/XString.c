#include"XString.h"
#include<stdlib.h>
#include<string.h>
XString* XString_new()
{
	XString* this_string = malloc(sizeof(XString));
	XString_init(this_string);
	return this_string;
}
void XString_init(XString* this_string)
{
	if (ISNULL(this_string, "") )
		return;
	XVector_init(this_string, sizeof(char));
	XString_class_init();
	ObjectVtable(this_string) = XStringVtable;
	XString_clear(this_string);
}

void XString_resize(XString* this_string, size_t len)
{
	XVector_resize(this_string,len);
}

void XString_push_front(XString* this_string, char c)
{
	XVector_push_front(this_string,&c);
}

void XString_push_back(XString* this_string, char c)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, char);
	ObjectVirtualFunc(this_string, EXString_Push_Back, funcPtr)(this_string, c);
}

void XString_append(XString* this_string, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, const char*);
	ObjectVirtualFunc(this_string, EXString_Append, funcPtr)(this_string, string);
}

void XString_assign(XString* this_string, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, const char*);
	ObjectVirtualFunc(this_string, EXString_Assign, funcPtr)(this_string, string);
}

void XString_insert(XString* this_string, const int64_t index, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), "")||ISNULL(string, ""))
		return;
	typedef void (*funcPtr)(XString*, int64_t, const char*);
	ObjectVirtualFunc(this_string, EXString_Insert, funcPtr)(this_string, index, string);
}

void XString_pop_front(XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*);
	ObjectVirtualFunc(this_string, EXString_Pop_Front, funcPtr)(this_string);
}

void XString_pop_back(XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*);
	ObjectVirtualFunc(this_string, EXString_Pop_Back, funcPtr)(this_string);
}

void XString_erase(XString* this_string, void* LpValue)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef void (*funcPtr)(XString*, void*);
	ObjectVirtualFunc(this_string, EXVector_Erase, funcPtr)(this_string, LpValue);
}

bool XString_empty(const XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return true;
	typedef bool(*funcPtr)(XString*);
	return ObjectVirtualFunc(this_string, EXString_Empty, funcPtr)(this_string);
}

size_t XString_size(const XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return 0;
	typedef size_t (*funcPtr)(XString*);
	return ObjectVirtualFunc(this_string, EXString_Size, funcPtr)(this_string);
	//XVector_size(this_string);
}

void XString_swap(XString* this_stringOne, XString* this_stringTwo)
{
	XVector_swap(this_stringOne,this_stringTwo);
}

void XString_free(const XString* this_string)
{
	XVector_free(this_string);
}

void XString_remove(XString* this_string, int64_t index, int64_t n)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef size_t(*funcPtr)(XString*, int64_t, int64_t);
	return ObjectVirtualFunc(this_string, EXVector_Remove, funcPtr)(this_string,index,n);
}

void XString_clear(XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef size_t(*funcPtr)(XString*);
	return ObjectVirtualFunc(this_string, EXVector_Clear, funcPtr)(this_string);
}

char XString_at(const XString* this_string, int64_t index)
{
	return XVector_At(this_string,index,char);
}

const char* XString_data(const XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return NULL;
	typedef const char* (*funcPtr)(const XString*);
	return ObjectVirtualFunc(this_string, EXString_Data, funcPtr)(this_string);
}
