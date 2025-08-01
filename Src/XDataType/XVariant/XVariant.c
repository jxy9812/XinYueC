#include "XVariant.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPair.h"
#include "XContainerObject.h"
#include "XByteArray.h"
#include "XString.h"
#include "XAlgorithm.h"
#include "XVariantList.h"
#include "XHashMap.h"
#include "XMap.h"
#include <string.h>
#include <stdlib.h>
static XHashMap*global_typeProperty= NULL;//自定义数据属性哈希映射
//类型属性
typedef struct TypeProperty
{
	XEquality equality;//等于比较
	char* typeName;//类型名字
}TypeProperty;



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
//自定义的类型哈希表初始化
static bool global_typeHash_init()
{
	if (global_typeProperty)
		return true;
	global_typeProperty = XHashMap_Create(int, TypeProperty, XEquality_int,XLess_int);
	if (global_typeProperty)
		return true;
	return false;
}

XVariant* XVariant_create(void* data, size_t dataSize, int type)
{
	XVariant* var = XMemory_malloc(sizeof(XVariant));
	XVariant_init(var,data,dataSize,type);
	return var;
}

void XVariant_init(XVariant* var, void* data, size_t dataSize, int type)
{
	if (var == NULL)
		return NULL;
	if (dataSize > 0)
	{
		var->m_data = XMemory_malloc(dataSize);
		if (var->m_data == NULL)
		{
			XMemory_free(var);
			return NULL;
		}
		if (data != NULL)
			memcpy(XVariant_DataPtr(var), data, dataSize);
	}
	else
	{
		var->m_data = NULL;
	}
	var->m_type = type;
	var->m_dataSize = dataSize;
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
		(var->m_dataSize) += temp->m_dataSize;
		(var->m_dataSize) += (sizeof(XVariant) - sizeof(void*));
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
//XMap<XString, XVariant>
XVariant* XVariant_create_XMap(const XVariantMap* map)
{
	if (map == NULL|| ((XMapBase*)map)->m_KeyEquality != XEquality_XString)
		return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_MapBase);
	if (var == NULL)
		return NULL;
	//计算大小
	XPair* pair = NULL;
	for_each_iterator(map, XMap, it)
	{
		pair = XMap_iterator_data(&it);
		XString* str = XPair_first(pair);
		XVariant* v= XPair_second(pair);
		XPair p = {0};
		p.m_firstTypeSize= XContainerSize(str)+1;//XString大小
		p.m_secondTypeSize = v->m_dataSize + (sizeof(XVariant) - sizeof(void*));
		(var->m_dataSize) += XPair_getSize(&p);
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
	XPair copyPair = { 0 };
	size_t pairTypeSize = (uint8_t*)(&(copyPair.m_first)) - ((uint8_t*)&copyPair);
	size_t variantSize = sizeof(XVariant) - sizeof(void*);
	for_each_iterator(map, XMap, it)
	{
		pair = XMap_iterator_data(&it);
		XString* str = XPair_first(pair);
		XVariant* v = XPair_second(pair);
		copyPair.m_firstTypeSize= XContainerSize(str)+1;//XString大小
		copyPair.m_secondTypeSize = v->m_dataSize + (sizeof(XVariant) - sizeof(void*));
		//拷贝构建XPair结构
		memcpy(ptr, &copyPair, pairTypeSize);
		ptr += pairTypeSize;
		//拷贝XString数据
		memcpy(ptr,XContainerDataPtr(str), XContainerSize(str)+1);
		ptr += (XContainerSize(str)+1);
		//拷贝数据
		memcpy(ptr, v, variantSize);
		ptr += variantSize;
		memcpy(ptr, v->m_data, v->m_dataSize);
		ptr += v->m_dataSize;
	}
	return var;
}

XVariant* XVariant_create_XHash(const XHashMap* hash)
{
	if (hash == NULL || ((XMapBase*)hash)->m_KeyEquality != XEquality_XString)
		return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_MapBase);
	if (var == NULL)
		return NULL;
	//计算大小
	XPair* pair = NULL;
	for_each_iterator(hash, XHashMap, it)
	{
		pair = XHashMap_iterator_data(&it);
		XString* str = XPair_first(pair);
		XVariant* v = XPair_second(pair);
		XPair p = { 0 };
		p.m_firstTypeSize = XContainerSize(str) + 1;//XString大小
		p.m_secondTypeSize = v->m_dataSize + (sizeof(XVariant) - sizeof(void*));
		(var->m_dataSize) += XPair_getSize(&p);
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
	XPair copyPair = { 0 };
	size_t pairTypeSize = (uint8_t*)(&(copyPair.m_first)) - ((uint8_t*)&copyPair);
	size_t variantSize = sizeof(XVariant) - sizeof(void*);
	for_each_iterator(hash, XHashMap, it)
	{
		pair = XHashMap_iterator_data(&it);
		XString* str = XPair_first(pair);
		XVariant* v = XPair_second(pair);
		copyPair.m_firstTypeSize = XContainerSize(str) + 1;//XString大小
		copyPair.m_secondTypeSize = v->m_dataSize + (sizeof(XVariant) - sizeof(void*));
		//拷贝构建XPair结构
		memcpy(ptr, &copyPair, pairTypeSize);
		ptr += pairTypeSize;
		//拷贝XString数据
		memcpy(ptr, XContainerDataPtr(str), XContainerSize(str) + 1);
		ptr += (XContainerSize(str) + 1);
		//拷贝数据
		memcpy(ptr, v, variantSize);
		ptr += variantSize;
		memcpy(ptr, v->m_data, v->m_dataSize);
		ptr += v->m_dataSize;
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
	XString* str = XString_create_with_length_utf8(XVariant_DataPtr(var),len);
	return str;
}

XVariantList* XVariant_toList(XVariant* var)
{
	if (var->m_type != XVariantType_List)
		return NULL;
	XVariantList* list = XVariantList_create();
	if (list == NULL)
		return NULL;
	//正式保存数据
	uint8_t* ptr = var->m_data;
	size_t variantSize = sizeof(XVariant) - sizeof(void*);
	XVariant* temp=ptr;
	XVariant_Init(newVar, NULL, 0, 0);
	while (ptr < ((uint8_t*)var->m_data) + var->m_dataSize)
	{
		memcpy(newVar, temp, variantSize);
		newVar->m_data = XMemory_malloc(temp->m_dataSize);
		ptr += variantSize;
		memcpy(newVar->m_data, ptr, temp->m_dataSize);
		ptr += temp->m_dataSize;
		XVariantList_push_back_move_base(list,newVar);
		temp = ptr;
	}
	XVariant_deinit(newVar);
	return list;
}

XVariantMap* XVariant_toMap(XVariant* var)
{
	if (var->m_type != XVariantType_MapBase)
		return NULL;
	XVariantMap* map = XMap_create_XVariantMap();
	if (map == NULL)
		return NULL;
	//
	size_t variantSize = sizeof(XVariant) - sizeof(void*);
	uint8_t* ptr = var->m_data;
	XPair* temp = ptr;
	XVariant* tv=NULL;
	XString_Init_Utf8(str,NULL);
	XVariant_Init(newVar, NULL, 0, 0);
	while (ptr < ((uint8_t*)var->m_data) + var->m_dataSize)
	{
		//XString* str = XString_create_utf8(XPair_first(temp));
		XString_assign_utf8(str, XPair_first(temp));
		tv = XPair_second(temp);
		//XVariant* newVar = XVariant_create(NULL, 0, 0);

		memcpy(newVar, tv, variantSize);
		newVar->m_data = XMemory_malloc(tv->m_dataSize);
		memcpy(newVar->m_data, ((uint8_t*)tv)+ variantSize, tv->m_dataSize);
		XMapBase_insert_move_base(map,str,newVar);
		ptr += XPair_getSize(temp);
		temp = ptr;
	}
	XString_deinit_base(str);
	XVariant_deinit(newVar);
	return map;
}

XVariantHashMap* XVariant_toHash(XVariant* var)
{
	if (var->m_type != XVariantType_MapBase)
		return NULL;
	XMap* hash = XHashMap_create_XVariantHashMap();
	if (hash == NULL)
		return NULL;
	//
	size_t variantSize = sizeof(XVariant) - sizeof(void*);
	uint8_t* ptr = var->m_data;
	XPair* temp = ptr;
	XVariant* tv = NULL;
	XString_Init_Utf8(str, NULL);
	XVariant_Init(newVar, NULL, 0, 0);
	while (ptr < ((uint8_t*)var->m_data) + var->m_dataSize)
	{
		//XString* str = XString_create_utf8(XPair_first(temp));
		XString_assign_utf8(str, XPair_first(temp));
		tv = XPair_second(temp);
		//XVariant* newVar = XVariant_create(NULL, 0, 0);

		memcpy(newVar, tv, variantSize);
		newVar->m_data = XMemory_malloc(tv->m_dataSize);
		memcpy(newVar->m_data, ((uint8_t*)tv) + variantSize, tv->m_dataSize);
		XMapBase_insert_move_base(hash, str, newVar);
		ptr += XPair_getSize(temp);
		temp = ptr;
	}
	XString_deinit_base(str);
	XVariant_deinit(newVar);
	return hash;
}

XPair* XVariant_toXPair(XVariant* var)
{
	if (var->m_type != XVariantType_Pair)
		return NULL;
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

void XVariant_copy(XVariant* var, const XVariant* src)
{
	if (var == NULL || src == NULL)
		return;
	if (var->m_data)
		XMemory_free(var->m_data);
	memcpy(var,src,sizeof(XVariant));
	var->m_data = XMemory_malloc(var->m_dataSize);
	memcpy(var->m_data,src->m_data, var->m_dataSize);
}

void XVariant_move(XVariant* var, XVariant* src)
{
	if (var == NULL || src == NULL)
		return;
	if (var->m_data)
		XMemory_free(var->m_data);
	memcpy(var, src, sizeof(XVariant));
	src->m_data = NULL;
	src->m_dataSize = 0;
}

void XVariant_delete(XVariant* var)
{
	XVariant_deinit(var);
	XMemory_free(var);
}

void XVariant_deinit(XVariant* var)
{
	if (var == NULL)
		return;
	if (var->m_data)
	{
		XMemory_free(var->m_data);
		var->m_data = NULL;
	}
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

const char* XVariant_typeName(XVariant* var)
{
	if (var == NULL)
		return NULL;
	switch (var->m_type)
	{
	case XVariantType_Uint8: return     "uint8_t";
	case XVariantType_Uint16:return     "uint16_t";
	case XVariantType_Uint32:return     "uint32_t";
	case XVariantType_Uint64:return     "uint64_t";
	case XVariantType_Int8: return      "int8_t";
	case XVariantType_Int16:return      "int16_t";
	case XVariantType_Int32:return      "int32_t";
	case XVariantType_Int64:return      "int64_t";
	case XVariantType_Bool:return       "bool";
	case XVariantType_Char:return       "char";
	case XVariantType_UChar:return	    "unsigned char";
	case XVariantType_Int:return	    "int";
	case XVariantType_Size_t:return	    "size_t";
	case XVariantType_Ptr:return        "void*";
	case XVariantType_Float:return	    "float";
	case XVariantType_Double:return	    "double";
	case XVariantType_Pair:return       "XPair";
	case XVariantType_Point:return      "XPoint";
	case XVariantType_ByteArray:return  "XByteArrat";
	case XVariantType_String:return		"XString";
	case XVariantType_List:return		"XVariantList";
	case XVariantType_MapBase:return    "XMapBase<XString,XVariant>";
	default:
	{
		//其他自定义数据
		if (global_typeProperty == NULL)
			return NULL;
		//查找
		TypeProperty* pv = XHashMap_value_base(global_typeProperty, &(var->m_type));
		if (pv)
			return pv->typeName;
		return NULL;
	}
	}
}

bool XVariant_equality(XVariant* var, XVariant* cmp)
{
	if(var==NULL||cmp==NULL||
		var->m_type!=cmp->m_type||
		var->m_dataSize!=cmp->m_dataSize)
		return false;
	switch (var->m_type)
	{
	case XVariantType_Uint8:return XEquality_uint8_t(var->m_data, cmp->m_data);
	case XVariantType_Uint16:return XEquality_uint16_t(var->m_data, cmp->m_data);
	case XVariantType_Uint32:return XEquality_uint32_t(var->m_data, cmp->m_data);
	case XVariantType_Uint64:return XEquality_uint64_t(var->m_data, cmp->m_data);
	case XVariantType_Int8:return XEquality_int8_t(var->m_data, cmp->m_data);
	case XVariantType_Int16:return XEquality_int16_t(var->m_data, cmp->m_data);
	case XVariantType_Int32:return XEquality_int32_t(var->m_data, cmp->m_data);
	case XVariantType_Int64:return XEquality_int64_t(var->m_data, cmp->m_data);
	case XVariantType_Bool:return XEquality_bool(var->m_data, cmp->m_data);
	case XVariantType_Char:return XEquality_char(var->m_data, cmp->m_data);
	case XVariantType_UChar:return XEquality_unsigned_char(var->m_data, cmp->m_data);
	case XVariantType_Int:return XEquality_int(var->m_data, cmp->m_data);
	case XVariantType_Size_t:return XEquality_size_t(var->m_data, cmp->m_data);
	case XVariantType_Ptr:return XEquality_ptr(var->m_data, cmp->m_data);
	case XVariantType_Float:return XEquality_float(var->m_data, cmp->m_data);
	case XVariantType_Double:return XEquality_double(var->m_data, cmp->m_data);
	case XVariantType_Pair:return XEquality_XPair(var->m_data, cmp->m_data);
	case XVariantType_Point:return XEquality_XPoint(var->m_data, cmp->m_data);
	case XVariantType_ByteArray:
	{
		XByteArray v = { 0 }, c = {0};
		XByteArray* pv = &v, * pc = &c;
		v.m_parent.m_parent.m_data = var->m_data;
		v.m_parent.m_parent.m_capacity = var->m_dataSize;
		v.m_parent.m_parent.m_size = var->m_dataSize;
		c.m_parent.m_parent.m_data = cmp->m_data;
		c.m_parent.m_parent.m_capacity = cmp->m_dataSize;
		c.m_parent.m_parent.m_size = cmp->m_dataSize;
		return XEquality_XByteArray(&pv, &pc);
	}
	case XVariantType_String:
	{
		XString v = { 0 }, c = { 0 };
		XString* pv = &v, * pc = &c;
		XContainerDataPtr(pv) = var->m_data;
		XContainerCapacity (pv) = var->m_dataSize;
		XContainerSize(pv) = var->m_dataSize;
		XContainerDataPtr(pc) = cmp->m_data;
		XContainerCapacity(pc) = cmp->m_dataSize;
		XContainerSize(pc) = cmp->m_dataSize;
		return XEquality_XString(&pv, &pc);
	}
	case XVariantType_List:
	{
		return false;//暂未实现
	}
	case XVariantType_MapBase:
	{
		return false;//暂未实现
	}
	default:
	{
		//其他自定义数据
		if(global_typeProperty==NULL)
			return false;
		//查找相等比较函数
		TypeProperty* pv =XHashMap_value_base(global_typeProperty,&(var->m_type));
		if (pv&&pv->equality)
			return (pv->equality)(var->m_data,cmp->m_data);
		return false;
	}
	}

}

void XVariant_setUserTypeName(int type, const char* typeName)
{
	if (type < XVariantType_User || typeName == NULL)
		return;
	if (!global_typeHash_init())
		return;
	TypeProperty* pv = XMapBase_value_base(global_typeProperty, &type);
	if (pv == NULL)
	{
		size_t len = strlen(typeName) + 1;
		TypeProperty property = { 0 };
		property.typeName =XMemory_malloc(len);
		if (property.typeName == NULL)
			return;
		memcpy(property.typeName,typeName,len);
		XHashMap_insert_base(global_typeProperty, &type, &property);
	}
	else 
	{
		if (pv->typeName)
			XMemory_free(pv->typeName);
		size_t len = strlen(typeName) + 1;
		pv->typeName = XMemory_malloc(len);
		if (pv->typeName == NULL)
			return;
		memcpy(pv->typeName, typeName, len);
	}
}

void XVariant_removeUserTypeProperty(int type)
{
	if (type < XVariantType_User)
		return;
	if (!global_typeHash_init())
		return;
	TypeProperty* pv = XMapBase_value_base(global_typeProperty, &type);
	if (pv == NULL)
		return;
	if (pv->typeName)
		XMemory_free(pv->typeName);
	XMapBase_remove_base(global_typeProperty,&type);
}

void XVariant_setUserEquality(int type, XEquality equality)
{
	if (type < XVariantType_User || equality == NULL)
		return;
	if (!global_typeHash_init())
		return;
	TypeProperty* pv = XMapBase_value_base(global_typeProperty,&type);
	if (pv == NULL)
	{
		TypeProperty property = { 0 };
		property.equality = equality;
		XHashMap_insert_base(global_typeProperty, &type, &property);
	}
	else
	{
		pv->equality = equality;
	}

}


void* XVariant_data(XVariant* var)
{
	if(var)
		return XVariant_DataPtr(var);
	return NULL;
}

size_t XVariant_dataSize(XVariant* var)
{
	if(var)
		return var->m_dataSize;
	return 0;
}
