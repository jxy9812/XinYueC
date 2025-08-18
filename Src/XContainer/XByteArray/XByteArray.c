#include "XByteArray.h"
#if XByteArray_ON
#include "XString.h"
#include "XEquality.h"
#include <string.h>
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

XByteArray* XByteArray_create_copy(const XByteArray* other)
{
	if (other == NULL)
		return NULL;
	XByteArray* v = XByteArray_create(0);
	XByteArray_copy_base(v, other);
	return v;
}

XByteArray* XByteArray_create_move(XByteArray* other)
{
	if (other == NULL)
		return NULL;
	XByteArray* v = XByteArray_create(0);
	XByteArray_move_base(v, other);
	return v;
}

bool XByteArray_push_front_base(XByteArray* array, const uint8_t byte)
{
	return XVector_push_front_base(array,&byte);
}

bool XByteArray_push_back_base(XByteArray* array, const uint8_t byte)
{
	return XVector_push_back_base(array, &byte);
}

bool XByteArray_insert_base(XByteArray* array, int64_t index, const uint8_t byte)
{
	return XVector_insert(array,index,&byte);
}

bool XByteArray_inserts_base(XByteArray* array, int64_t index, uint8_t byte, size_t n)
{
	return XVector_insert_array_base(array, index, &byte,n);
}

bool XByteArray_append_utf8(XByteArray* array, const char* utf8)
{
	if (array == NULL || utf8 == NULL)
		return false;
	size_t len = strlen(utf8);
	if (len == 0)
		return false;
	return XByteArray_append_array_base(array,utf8,len);
}

uint8_t* XByteArray_find_base(const XByteArray* array, const uint8_t findVal)
{
	return XVector_find_base(array,&findVal);
}
XByteArray* XByteArray_to16HexUtf8(XByteArray* array)
{
	if (array == NULL || XByteArray_isEmpty_base(array))
		return NULL;
	XByteArray* bytes = XByteArray_create(0);
	uint8_t temp[6];
	for (size_t i = 0; i < XByteArray_size_base(array); i++)
	{
		sprintf(temp,"%02X ", XByteArray_At_Base(array, i));
		XByteArray_append_array_base(bytes,temp,3);
	}
	XByteArray_Back_Base(bytes) = 0;
	return bytes;
}
XString* XByteArray_to16HexString(XByteArray* array)
{
	if (array == NULL || XByteArray_isEmpty_base(array))
		return NULL;
	XString* str = XString_create();
	uint8_t temp[6];
	for (size_t i = 0; i < XByteArray_size_base(array); i++)
	{
		sprintf(temp, "%02X ", XByteArray_At_Base(array, i));
		XString_append_with_length_utf8(str, temp,3);
	}
	return str;
}
#endif