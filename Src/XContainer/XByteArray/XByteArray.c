#include "XByteArray.h"
#if XByteArray_ON
#include "XEquality.h"
XByteArray* XByteArray_create(size_t size)
{
	XByteArray* array = XVector_Create(uint8_t);
	if (array == NULL)
		return NULL;
	array->m_parent.m_equality = XEquality_uint8_t;
	if (size!=0)
	{
		XVector_resize_base(array,size);
	}
	return array;
}

void XByteArray_push_front_base(XByteArray* array, const uint8_t byte)
{
	XVector_push_front_base(array,&byte);
}

void XByteArray_push_back_base(XByteArray* array, const uint8_t byte)
{
	XVector_push_back_base(array, &byte);
}

void XByteArray_insert_base(XByteArray* array, int64_t index, const uint8_t byte)
{
	XVector_insert(array,index,&byte);
}

void XByteArray_inserts_base(XByteArray* array, int64_t index, uint8_t byte, size_t n)
{
	XVector_insert_array_base(array, index, &byte,n);
}

uint8_t* XByteArray_find_base(const XByteArray* array, const uint8_t findVal)
{
	return XVector_find_base(array,&findVal);
}
#endif