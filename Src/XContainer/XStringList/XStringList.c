#include"XStringList.h"
#if XStringList_ON
#include<string.h>
XStringList* XStringList_create()
{
	XStringList* vector=XMemory_malloc(sizeof(XStringList));
	XStringList_init(vector);
	
	return vector;
}
void XStringList_init(XStringList* this_stringVector)
{
	if (this_stringVector == NULL)
		return;
	XVector_init(this_stringVector,sizeof(XString*));
	XClassGetVtable(this_stringVector) = XStringList_class_init();
	XContainerSetDataDeleteMethod(this_stringVector, XContainerDefaultDerivedClassDataDeleteMethod);
}
void XStringList_push_front_base(XStringList* this_stringVector, XString* string)
{
	XVector_push_front_base(this_stringVector,string);
}
void XStringList_push_front_c_str(XStringList* this_stringVector, const char* str)
{
	XStringList_push_front_base(this_stringVector,XString_create(str));
}
void XStringList_push_back_base(XStringList* this_stringVector, XString* string)
{
	XVector_push_back_base(this_stringVector, string);
}
void XStringList_push_back_c_str(XStringList* this_stringVector, const char* str)
{
	XStringList_push_back_base(this_stringVector, XString_create(str));
}
void XStringList_insert_base(XStringList* this_stringVector, int64_t index, XString* string)
{
	XVector_insert_base(this_stringVector,index, string);
}
void XStringList_insert_c_str(XStringList* this_stringVector, int64_t index, const char* str)
{
	XStringList_insert_base(this_stringVector,index, XString_create(str));
}
XString* XStringList_at_base(const XStringList* this_stringVector, int64_t index)
{
	return XVector_at_base(this_stringVector,index);
}

XString* XStringList_front_base(const XStringList* this_stringVector)
{
	return XVector_front_base(this_stringVector);
}

XString* XStringList_back_base(const XStringList* this_stringVector)
{
	return XVector_back_base(this_stringVector);
}

XString* XStringList_join(const XStringList* this_stringVector, const char* separator)
{
	if(this_stringVector==NULL|| separator==NULL)
		return NULL;
	size_t len = strlen(separator);
	if (len == 0 || XString_isEmpty_base(this_stringVector))
		return NULL;
	XString* str = XString_create(NULL);
	for_each_iterator(this_stringVector, XStringList, it)
	{
		XString* s = XStringList_iterator_data(&it);
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

