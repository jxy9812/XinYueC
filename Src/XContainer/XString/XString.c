#include"XString.h"
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
	ObjectVirtualFunc(this_string, EXString_append, funcPtr)(this_string, string);
}

bool XString_empty(const XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef bool(*funcPtr)(XString*);
	return ObjectVirtualFunc(this_string, EXString_Empty, funcPtr)(this_string);
}

size_t XString_size(const XString* this_string)
{
	if (ISNULL(this_string, "") || ISNULL(ObjectVtable(this_string), ""))
		return;
	typedef size_t (*funcPtr)(XString*);
	return ObjectVirtualFunc(this_string, EXString_Size, funcPtr)(this_string);
	//XVector_size(this_string);
}
