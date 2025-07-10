#include "XVariant.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPair.h"
#include "XContainerObject.h"
#include "XByteArray.h"
#include "XString.h"
#include "XAlgorithm.h"
#include "XVariantList.h"
#include <string.h>
#include <stdlib.h>

typedef struct XVariant
{
	int m_type;//类型
	//XEquality m_equality;//相等比较函数
	size_t m_dataSize;//数据大小
	void* m_data;//数据
}XVariant;

#define XVariant_DataPtr(Var)    (((XVariant*)Var)->m_data)
//#define XVariant_DataPtr(Var)  (&(((XVariant*)Var)->data))
#define XVariant_Data(Var,Type)   (*((Type*)XVariant_DataPtr(Var)))

#define to_value(var,t) \
switch (var->m_type)\
{\
case XVariantType_Uint8:	return (t)XVariant_Data(var, uint8_t);\
case XVariantType_Uint16:	return (t)XVariant_Data(var, uint16_t);\
case XVariantType_Uint32:	return (t)XVariant_Data(var, uint32_t);\
case XVariantType_Uint64:	return (t)XVariant_Data(var, uint64_t);\
case XVariantType_Int8:		return (t)XVariant_Data(var, int8_t);\
case XVariantType_Int16:	return (t)XVariant_Data(var, int16_t);\
case XVariantType_Int32:	return (t)XVariant_Data(var, int32_t);\
case XVariantType_Int64:	return (t)XVariant_Data(var, int64_t);\
case XVariantType_Bool:		return (t)XVariant_Data(var, bool);\
case XVariantType_Char:		return (t)XVariant_Data(var, char);\
case XVariantType_UChar:	return (t)XVariant_Data(var, unsigned char);\
case XVariantType_Int:		return (t)XVariant_Data(var, int);\
case XVariantType_Size_t:	return (t)XVariant_Data(var, size_t);\
case XVariantType_Ptr:		return (t)XVariant_Data(var, void*);\
case XVariantType_Float:	return (t)XVariant_Data(var, float);\
case XVariantType_Double:	return (t)XVariant_Data(var, double);\
case XVariantType_ByteArray:return (t)XVariant_Data(var, t); \
default:return 0;\
}\

XVariant* XVariant_create(void* data, size_t size, int type)
{
	XVariant* var = XMemory_malloc(sizeof(XVariant));
	if (var == NULL)
		return NULL;
	if(size>0)
	{
		var->m_data = XMemory_malloc(size);
		if (var->m_data == NULL)
		{
			XMemory_free(var);
			return NULL;
		}
		memcpy(XVariant_DataPtr(var), data, size);
	}
	else
	{
		var->m_data = NULL;
	}
	var->m_type = type;
	var->m_dataSize = size;
	return var;
}

XVariant* XVariant_create_uint8(uint8_t val)
{
	return XVariant_Create(val, XVariantType_Uint8);
}

XVariant* XVariant_create_uint16(uint16_t val)
{
	return XVariant_Create(val, XVariantType_Uint16);
}

XVariant* XVariant_create_uint32(uint32_t val)
{
	return XVariant_Create(val, XVariantType_Uint32);
}

XVariant* XVariant_create_uint64(uint64_t val)
{
	return XVariant_Create(val, XVariantType_Uint64);
}

XVariant* XVariant_create_int8(int8_t val)
{
	return XVariant_Create(val, XVariantType_Int8);
}

XVariant* XVariant_create_int16(int16_t val)
{
	return XVariant_Create(val, XVariantType_Int16);
}

XVariant* XVariant_create_int32(int32_t val)
{
	return XVariant_Create(val, XVariantType_Int32);
}

XVariant* XVariant_create_int64(int64_t val)
{
	return XVariant_Create(val, XVariantType_Int64);
}

XVariant* XVariant_create_bool(bool val)
{
	return XVariant_Create(val, XVariantType_Bool);
}

XVariant* XVariant_create_char(char val)
{
	return XVariant_Create(val, XVariantType_Char);
}

XVariant* XVariant_create_uchar(unsigned char val)
{
	return XVariant_Create(val, XVariantType_UChar);
}

XVariant* XVariant_create_int(int val)
{
	return XVariant_Create(val, XVariantType_Int);
}

XVariant* XVariant_create_size_t(size_t val)
{
	return XVariant_Create(val, XVariantType_Size_t);
}

XVariant* XVariant_create_ptr(void* val)
{
	return XVariant_Create(val, XVariantType_Ptr);
}

XVariant* XVariant_create_float(float val)
{
	return XVariant_Create(val, XVariantType_Float);
}

XVariant* XVariant_create_double(double val)
{
	return XVariant_Create(val, XVariantType_Double);
}

XVariant* XVariant_create_XPair(const XPair* val)
{
	if (val == NULL)
		return NULL;
	return XVariant_create(val, XPair_getSize(val), XVariantType_Pair);
}

XVariant* XVariant_create_XPoint(XPoint val)
{
	return XVariant_Create(val, XVariantType_Point);
}

XVariant* XVariant_create_XByteArray(const XByteArray* array)
{
	if (array == NULL)
		return NULL;
	return XVariant_create(XContainerDataPtr(array), XContainerSize(array), XVariantType_ByteArray);
}

XVariant* XVariant_create_byteArray(const void* data, size_t size)
{
	if (data == NULL||size==NULL)
		return NULL;
	return XVariant_create(data,size, XVariantType_ByteArray);
}

XVariant* XVariant_create_XString(XString* string)
{
	if(string==NULL)
		return NULL;
	return XVariant_create(XContainerDataPtr(string), XContainerSize(string), XVariantType_String);
}

XVariant* XVariant_create_str(const char* str)
{
	if (str == NULL)
		return NULL;
	return XVariant_create(str, strlen(str)+1, XVariantType_String);
}

XVariant* XVariant_create_list(const XVariantList* list)
{
	if (list == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL,0, XVariantType_List);
	if (var == NULL)
		return NULL;
	//计算大小
	XVariant* temp = NULL;
	for_each_iterator(list, XVariantList,it)
	{
		temp = XVariantList_iterator_data(&it);
		var->m_dataSize += temp->m_dataSize;
		var->m_dataSize += (sizeof(XVariant)-sizeof(void*));
	}
	//开始申请空间
	var->m_data = XMemory_malloc(var->m_dataSize);
	if (var->m_data == NULL)
	{
		XMemory_free(var);
		return NULL;
	}
	//正式保存数据
	uint8_t* ptr = var->m_data;
	size_t variantSize = sizeof(XVariant) - sizeof(void*);
	for_each_iterator(list, XVariantList, it)
	{
		temp = XVariantList_iterator_data(&it);
		memcpy(ptr,temp, variantSize);
		ptr += variantSize;
		memcpy(ptr,temp->m_data,temp->m_dataSize);
		ptr += temp->m_dataSize;
	}
	return var;
}

uint8_t XVariant_toUint8(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtoul(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint8_t);
}

uint16_t XVariant_toUint16(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtoul(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint16_t);
}

uint32_t XVariant_toUint32(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtoul(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint32_t);
}

uint64_t XVariant_toUint64(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtoull(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint64_t);
}

int8_t XVariant_toInt8(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtol(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int8_t);
}

int16_t XVariant_toInt16(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtol(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int16_t);
}

int32_t XVariant_toInt32(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtol(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int32_t);
}

int64_t XVariant_toInt64(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtoll(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int64_t);
}

bool XVariant_toBool(XVariant* var)
{
	if (var == NULL)
		return 0;
	to_value(var, bool);
}

char XVariant_toChar(XVariant* var)
{
	if (var == NULL)
		return 0;
	to_value(var, char);
}

unsigned char XVariant_toUChar(XVariant* var)
{
	if (var == NULL)
		return 0;
	to_value(var, unsigned char);
}

int XVariant_toInt(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return atoi(XVariant_DataPtr(var));
	to_value(var, int);
	/*switch (var->m_type)
	{
		case XVariantType_Uint8:	return (int)XVariant_Data(var, uint8_t);
		case XVariantType_Uint16:	return (int)XVariant_Data(var, uint16_t);
		case XVariantType_Uint32:	return (int)XVariant_Data(var, uint32_t);
		case XVariantType_Uint64:	return (int)XVariant_Data(var, uint64_t);
		case XVariantType_Int8:		return (int)XVariant_Data(var, int8_t);
		case XVariantType_Int16:	return (int)XVariant_Data(var, int16_t);
		case XVariantType_Int32:	return (int)XVariant_Data(var, int32_t);
		case XVariantType_Int64:	return (int)XVariant_Data(var, int64_t);
		case XVariantType_Bool:		return (int)XVariant_Data(var, bool);
		case XVariantType_Char:		return (int)XVariant_Data(var, char);
		case XVariantType_UChar:	return (int)XVariant_Data(var, unsigned char);
		case XVariantType_Int:		return XVariant_Data(var, int);
		case XVariantType_Size_t:	return (int)XVariant_Data(var, size_t);
		case XVariantType_Ptr:		return (int)XVariant_Data(var, void*);
		case XVariantType_Float:	return (int)XVariant_Data(var, float);
		case XVariantType_Double:	return (int)XVariant_Data(var, double);
		
		default:return 0;
	}*/
}

size_t XVariant_toSize_t(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtoull(XVariant_DataPtr(var), NULL, 10);
	to_value(var, size_t);
}

void* XVariant_toPtr(XVariant* var)
{
	return (void*)XVariant_toSize_t(var);
	/*if (var == NULL)
		return 0;
	switch (var->m_type) {
	case XVariantType_Uint8: return (void*)(*((uint8_t*)(((XVariant*)var)->data))); case XVariantType_Uint16: return (void*)(*((uint16_t*)(((XVariant*)var)->data))); case XVariantType_Uint32: return (void*)(*((uint32_t*)(((XVariant*)var)->data))); case XVariantType_Uint64: return (void*)(*((uint64_t*)(((XVariant*)var)->data))); case XVariantType_Int8: return (void*)(*((int8_t*)(((XVariant*)var)->data))); case XVariantType_Int16: return (void*)(*((int16_t*)(((XVariant*)var)->data))); case XVariantType_Int32: return (void*)(*((int32_t*)(((XVariant*)var)->data))); case XVariantType_Int64: return (void*)(*((int64_t*)(((XVariant*)var)->data))); case XVariantType_Bool: return (void*)(*((_Bool*)(((XVariant*)var)->data))); case XVariantType_Char: return (void*)(*((char*)(((XVariant*)var)->data))); case XVariantType_UChar: return (void*)(*((unsigned char*)(((XVariant*)var)->data))); case XVariantType_Int: return (void*)(*((int*)(((XVariant*)var)->data))); case XVariantType_Size_t: return (void*)(*((size_t*)(((XVariant*)var)->data))); case XVariantType_Ptr: return (void*)(*((void**)(((XVariant*)var)->data))); default:return 0;
	};*/
}

float XVariant_toFloat(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtof(XVariant_DataPtr(var), NULL);
	switch (var->m_type) {
	case XVariantType_Uint8: return (float)(*((uint8_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint16: return (float)(*((uint16_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint32: return (float)(*((uint32_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint64: return (float)(*((uint64_t*)(((XVariant*)var)->m_data))); case XVariantType_Int8: return (float)(*((int8_t*)(((XVariant*)var)->m_data))); case XVariantType_Int16: return (float)(*((int16_t*)(((XVariant*)var)->m_data))); case XVariantType_Int32: return (float)(*((int32_t*)(((XVariant*)var)->m_data))); case XVariantType_Int64: return (float)(*((int64_t*)(((XVariant*)var)->m_data))); case XVariantType_Bool: return (float)(*((_Bool*)(((XVariant*)var)->m_data))); case XVariantType_Char: return (float)(*((char*)(((XVariant*)var)->m_data))); case XVariantType_UChar: return (float)(*((unsigned char*)(((XVariant*)var)->m_data))); case XVariantType_Int: return (float)(*((int*)(((XVariant*)var)->m_data))); case XVariantType_Size_t: return (float)(*((size_t*)(((XVariant*)var)->m_data))); case XVariantType_Float: return (float)(*((float*)(((XVariant*)var)->m_data))); case XVariantType_Double: return (float)(*((double*)(((XVariant*)var)->m_data))); default:return 0;
	};
}

double XVariant_toDouble(XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return strtod(XVariant_DataPtr(var), NULL);
	switch (var->m_type) {
	case XVariantType_Uint8: return (double)(*((uint8_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint16: return (double)(*((uint16_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint32: return (double)(*((uint32_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint64: return (double)(*((uint64_t*)(((XVariant*)var)->m_data))); case XVariantType_Int8: return (double)(*((int8_t*)(((XVariant*)var)->m_data))); case XVariantType_Int16: return (double)(*((int16_t*)(((XVariant*)var)->m_data))); case XVariantType_Int32: return (double)(*((int32_t*)(((XVariant*)var)->m_data))); case XVariantType_Int64: return (double)(*((int64_t*)(((XVariant*)var)->m_data))); case XVariantType_Bool: return (double)(*((_Bool*)(((XVariant*)var)->m_data))); case XVariantType_Char: return (double)(*((char*)(((XVariant*)var)->m_data))); case XVariantType_UChar: return (double)(*((unsigned char*)(((XVariant*)var)->m_data))); case XVariantType_Int: return (double)(*((int*)(((XVariant*)var)->m_data))); case XVariantType_Size_t: return (double)(*((size_t*)(((XVariant*)var)->m_data))); case XVariantType_Float: return (double)(*((float*)(((XVariant*)var)->m_data))); case XVariantType_Double: return (double)(*((double*)(((XVariant*)var)->m_data))); default:return 0;
	};
}

XByteArray* XVariant_toByteArray(XVariant* var)
{
	if (var == NULL||var->m_dataSize==0)
		return NULL;
	XByteArray* array = XByteArray_create(var->m_dataSize);
	if(array!=NULL)
		XByteArray_append_array_base(array, XVariant_DataPtr(var),var->m_dataSize);
	return array;
}

XString* XVariant_toString(XVariant* var)
{
	if (var->m_type != XVariantType_String&&var->m_type != XVariantType_ByteArray)
		return NULL;
	size_t len = var->m_dataSize;
	if (((char*)XVariant_DataPtr(var))[len - 1] == 0)
		len--;
	XString* str = XString_create_with_length(XVariant_DataPtr(var),len);
	return str;
}

XPair* XVariant_toXPair(XVariant* var)
{
	if (var->m_type != XVariantType_Pair)
		return 0;
	XPair* pair = XMemory_malloc(var->m_dataSize);
	if (pair)
		memcpy(pair, XVariant_data(var),var->m_dataSize);
	return pair;
}

XPoint XVariant_toXPoint(XVariant* var)
{
	if (var->m_type != XVariantType_Point)
	{
		XPoint p = { 0 };
		return p;
	}
	return XVariant_Data(var, XPoint);
}

void XVariant_setValue(XVariant* var,const XVariant* newVar)
{
	if (var == NULL || newVar == NULL||newVar->m_data==NULL||newVar->m_dataSize==0)
		return;
	if (var->m_data != NULL)
	{
		XMemory_free(var->m_data);
		var->m_data = NULL;
		var->m_dataSize = 0;
	}
	var->m_data=XMemory_malloc(newVar->m_dataSize);
	if (var->m_data == NULL)
		return;
	memcpy(var->m_data,newVar->m_data,newVar->m_dataSize);
	var->m_dataSize = newVar->m_dataSize;
	var->m_type = newVar->m_type;
}
static void setValue(XVariant* var, void* data, size_t size, int type)
{
	if (var == NULL || data == NULL || size == 0)
		return;
	if (var->m_data == NULL || var->m_dataSize != size)
	{
		if (var->m_data)
			XMemory_free(var->m_data);
		var->m_data = XMemory_malloc(size);
		if (var->m_data == NULL)
		{
			var->m_dataSize = 0;
			return;
		}
		var->m_dataSize = size;
	}
	memcpy(XVariant_DataPtr(var),data,size);
	var->m_type = type;
}
void XVariant_setValue_uint8(XVariant* var, uint8_t val)
{
	setValue(var,&val,sizeof(uint8_t), XVariantType_Uint8);
}

void XVariant_setValue_uint16(XVariant* var, uint16_t val)
{
	setValue(var, &val, sizeof(uint16_t), XVariantType_Uint16);
}

void XVariant_setValue_uint32(XVariant* var, uint32_t val)
{
	setValue(var, &val, sizeof(uint32_t), XVariantType_Uint32);
}

void XVariant_setValue_uint64(XVariant* var, uint64_t val)
{
	setValue(var, &val, sizeof(uint64_t), XVariantType_Uint64);
}

void XVariant_setValue_int8(XVariant* var, int8_t val)
{
	setValue(var, &val, sizeof(int8_t), XVariantType_Int8);
}

void XVariant_setValue_int16(XVariant* var, int16_t val)
{
	setValue(var, &val, sizeof(int16_t), XVariantType_Int16);
}

void XVariant_setValue_int32(XVariant* var, int32_t val)
{
	setValue(var, &val, sizeof(int32_t), XVariantType_Int32);
}

void XVariant_setValue_int64(XVariant* var, int64_t val)
{
	setValue(var, &val, sizeof(int64_t), XVariantType_Int64);
}

void XVariant_setValue_bool(XVariant* var, bool val)
{
	setValue(var, &val, sizeof(bool), XVariantType_Bool);
}

void XVariant_setValue_char(XVariant* var, char val)
{
	setValue(var, &val, sizeof(char), XVariantType_Char);
}

void XVariant_setValue_uchar(XVariant* var, unsigned char val)
{
	setValue(var, &val, sizeof( unsigned char), XVariantType_UChar);
}

void XVariant_setValue_int(XVariant* var, int val)
{
	setValue(var, &val, sizeof(int), XVariantType_Int);
}

void XVariant_setValue_size_t(XVariant* var, size_t val)
{
	setValue(var, &val, sizeof(size_t), XVariantType_Size_t);
}

void XVariant_setValue_ptr(XVariant* var, void* val)
{
	setValue(var, &val, sizeof(void*), XVariantType_Ptr);
}

void XVariant_setValue_float(XVariant* var, float val)
{
	setValue(var, &val, sizeof(float), XVariantType_Float);
}

void XVariant_setValue_double(XVariant* var, double val)
{
	setValue(var, &val, sizeof(double), XVariantType_Double);
}

void XVariant_setValue_XPair(XVariant* var, const XPair* pair)
{
	setValue(var,pair,XPair_getSize(pair), XVariantType_Pair);
}

void XVariant_setValue_XPoint(XVariant* var, XPoint val)
{
	setValue(var,&val,sizeof(XPoint), XVariantType_Point);
}

void XVariant_setValue_XByteArray(XVariant* var, const XByteArray* array)
{
	if(array)
		setValue(var, XContainerDataPtr(array), XContainerSize(array), XVariantType_ByteArray);
}

void XVariant_setValue_byteArray(XVariant* var, const void* data, size_t size)
{
	setValue(var, data, size, XVariantType_ByteArray);
}

void XVariant_setValue_XString(XVariant* var, const XString* string)
{
	if (string)
		setValue(var, XContainerDataPtr(string), XContainerSize(string), XVariantType_String);
}

void XVariant_setValue_str(XVariant* var, const char* str)
{
	setValue(var, str, strlen(str)+1, XVariantType_String);
}

void XVariant_delete(XVariant* var)
{
	if (var == NULL)
		return;
	if (var->m_data)
		XMemory_free(var->m_data);
	XMemory_free(var);
}

void XVariant_clear(XVariant* var)
{
	if (var == NULL)
		return;
	if(var->m_dataSize)
		memset(XVariant_DataPtr(var),0,var->m_dataSize);
}

void XVariant_swap(XVariant* var, XVariant* other)
{
	if (var == NULL || other == NULL)
		return;
	XSwap(var,other,sizeof(XVariant));
}

int XVariant_type(XVariant* var)
{
	return var->m_type;
}

bool XVariant_equality(XVariant* var, XVariant* cmp)
{
	if(var==NULL||cmp==NULL||
		var->m_type!=cmp->m_type||
		var->m_dataSize!=cmp->m_dataSize)
		return false;
	switch (var->m_type)
	{
	case XVariantType_Uint8:	
	case XVariantType_Uint16:	
	case XVariantType_Uint32:	
	case XVariantType_Uint64:	
	case XVariantType_Int8:		
	case XVariantType_Int16:	
	case XVariantType_Int32:	
	case XVariantType_Int64:	
	case XVariantType_Bool:		
	case XVariantType_Char:		
	case XVariantType_UChar:
	case XVariantType_Int:		
	case XVariantType_Size_t:	
	case XVariantType_Ptr:		
	case XVariantType_Float:	
	case XVariantType_Double:
	case XVariantType_Pair:
	case XVariantType_Point://return var->m_equality(var->m_data, cmp->m_data);
	case XVariantType_ByteArray:
	{

	}
	default:return 0; 
	}

}

void* XVariant_data(XVariant* var)
{
	return XVariant_DataPtr(var);
}
