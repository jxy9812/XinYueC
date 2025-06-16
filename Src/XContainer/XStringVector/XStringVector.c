#include"XStringVector.h"
#if XStringVector_ON
#include<string.h>
XStringVector* XStringVector_create()
{
	XStringVector* vector=XMemory_malloc(sizeof(XStringVector));
	StringVector_init(vector);
	
	return vector;
}
void StringVector_init(XStringVector* this_stringVector)
{
	if (this_stringVector == NULL)
		return;
	XVector_init(this_stringVector,sizeof(XString*));
	XClassGetVtable(this_stringVector) = XStringVector_class_init();
	XContainerSetDataDeleteMethod(this_stringVector, XContainerDefaultDerivedClassDataDeleteMethod);
}
void XStringVector_push_front_base(XStringVector* this_stringVector, XString* string)
{
	XVector_push_front_base(this_stringVector,string);
}
void XStringVector_push_front_c_str(XStringVector* this_stringVector, const char* str)
{
	XStringVector_push_front_base(this_stringVector,XString_create(str));
}
void XStringVector_push_back_base(XStringVector* this_stringVector, XString* string)
{
	XVector_push_back_base(this_stringVector, string);
}
void XStringVector_push_back_c_str(XStringVector* this_stringVector, const char* str)
{
	XStringVector_push_back_base(this_stringVector, XString_create(str));
}
void XStringVector_insert_base(XStringVector* this_stringVector, int64_t index, XString* string)
{
	XVector_insert_base(this_stringVector,index, string);
}
void XStringVector_insert_c_str(XStringVector* this_stringVector, int64_t index, const char* str)
{
	XStringVector_insert_base(this_stringVector,index, XString_create(str));
}
XString* XStringVector_at_base(const XStringVector* this_stringVector, int64_t index)
{
	return XVector_at_base(this_stringVector,index);
}

XString* XStringVector_front_base(const XStringVector* this_stringVector)
{
	return XVector_front_base(this_stringVector);
}

XString* XStringVector_back_base(const XStringVector* this_stringVector)
{
	return XVector_back_base(this_stringVector);
}

XString* XStringVector_join(const XStringVector* this_stringVector, const char* separator)
{
	if(this_stringVector==NULL|| separator==NULL)
		return NULL;
	size_t len = strlen(separator);
	if (len == 0 || XString_isEmpty_base(this_stringVector))
		return NULL;
	XString* str = XString_create(NULL);
	for_each_iterator(this_stringVector, XStringVector, it)
	{
		XString* s = XStringVector_iterator_data(&it);
		if (!XString_isEmpty_base(s))
		{
			XString_append_base(str, XString_c_str(s));
			XString_append_base(str, separator);
		}
	}
	XContainerSize(str) -= len;
	((char*)XContainerDataPtr(str))[XContainerSize(str)]=0;
	return str;
}

#endif

