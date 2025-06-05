#include"XStringVector.h"
#if XStringVector_ON
XStringVector* XStringVector_create()
{
	XStringVector* vector=XVector_Create(XString*);
	XContainerSetDataFreeMethod(vector,XContainerDefaultDerivedClassDataFreeMethod);
	return vector;
}
void XStringVector_free(XStringVector* this_stringVector)
{
	XVector_free_base(this_stringVector);
}
void XStringVector_push_front(XStringVector* this_stringVector, XString* string)
{
	XVector_push_front_base(this_stringVector,&string);
}
void XStringVector_push_front_c_str(XStringVector* this_stringVector, const char* str)
{
	XStringVector_push_front(this_stringVector,XString_create(str));
}
void XStringVector_push_back(XStringVector* this_stringVector, XString* string)
{
	XVector_push_back_base(this_stringVector, &string);
}
void XStringVector_push_back_c_str(XStringVector* this_stringVector, const char* str)
{
	XStringVector_push_back(this_stringVector, XString_create(str));
}
void XStringVector_insert(XStringVector* this_stringVector, int64_t index, XString* string)
{
	XVector_insert_base(this_stringVector,index, &string);
}
void XStringVector_insert_c_str(XStringVector* this_stringVector, int64_t index, const char* str)
{
	XStringVector_insert(this_stringVector,index, XString_create(str));
}
XString* XStringVector_at(const XStringVector* this_stringVector, int64_t index)
{
	return XVector_At_Base(this_stringVector,index,XString*);
}

XString* XStringVector_front(const XStringVector* this_stringVector)
{
	return XVector_Front_Base(this_stringVector,XString*);
}

XString* XStringVector_back(const XStringVector* this_stringVector)
{
	return XVector_Back_Base(this_stringVector, XString*);
}

#endif

