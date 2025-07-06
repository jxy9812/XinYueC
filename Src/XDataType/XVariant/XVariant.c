#include "XVariant.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPair.h"
#include <string.h>
typedef struct XVariant
{
	int type;//类型
	size_t dataSize;//数据大小
	void* data;//数据
}XVariant;

#define XVariant_DataPtr(Var)  (&(((XVariant*)Var)->data))
#define XVariant_Data(Var,Type)   (*((Type*)XVariant_DataPtr(Var)))
XVariant* XVariant_create(void* data, size_t dataSize, int type)
{
	XVariant* var = XMemory_malloc(sizeof(XVariant)-sizeof(void*)+dataSize);
	memcpy(XVariant_DataPtr(var),data,dataSize);
	var->type = type;
	var->dataSize = dataSize;
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

XVariant* XVariant_create_XPair(XPair* val)
{
	return XVariant_Create(val, XVariantType_XPair);
}

XVariant* XVariant_create_XPoint(XPoint val)
{
	return XVariant_Create(val, XVariantType_XPoint);
}

XVariant* XVariant_create_XVector(XVector* val)
{
	return XVariant_Create(val, XVariantType_XVector);
}

XVariant* XVariant_create_XString(XString* val)
{
	return XVariant_Create(val, XVariantType_XString);
}

XVariant* XVariant_create_XByteArray(XByteArray* val)
{
	return XVariant_Create(val, XVariantType_XByteArray);
}

XVariant* XVariant_create_XListBase(XListBase* val)
{
	return XVariant_Create(val, XVariantType_XListBase);
}

XVariant* XVariant_create_XMapBase(XMapBase* val)
{
	return XVariant_Create(val, XVariantType_XMapBase);
}

XVariant* XVariant_create_XQueueBase(XQueueBase* val)
{
	return XVariant_Create(val, XVariantType_XQueueBase);
}

XVariant* XVariant_create_XSetBase(XSetBase* val)
{
	return XVariant_Create(val, XVariantType_XSetBase);
}

XVariant* XVariant_create_XStack(XStack* val)
{
	return XVariant_Create(val, XVariantType_XStack);
}

uint8_t XVariant_toUint8(XVariant* var)
{
	if(var->type!= XVariantType_Uint8)
		return 0;
	return XVariant_Data(var,uint8_t);
}

uint16_t XVariant_toUint16(XVariant* var)
{
	if (var->type != XVariantType_Uint16)
		return 0;
	return XVariant_Data(var, uint16_t);
}

uint32_t XVariant_toUint32(XVariant* var)
{
	if (var->type != XVariantType_Uint32)
		return 0;
	return XVariant_Data(var, uint32_t);
}

uint64_t XVariant_toUint64(XVariant* var)
{
	if (var->type != XVariantType_Uint64)
		return 0;
	return XVariant_Data(var, uint64_t);
}

int8_t XVariant_toInt8(XVariant* var)
{
	if (var->type != XVariantType_Int8)
		return 0;
	return XVariant_Data(var, int8_t);
}

int16_t XVariant_toInt16(XVariant* var)
{
	if (var->type != XVariantType_Int16)
		return 0;
	return XVariant_Data(var, int16_t);
}

int32_t XVariant_toInt32(XVariant* var)
{
	if (var->type != XVariantType_Int32)
		return 0;
	return XVariant_Data(var, int32_t);
}

int64_t XVariant_toInt64(XVariant* var)
{
	if (var->type != XVariantType_Int64)
		return 0;
	return XVariant_Data(var, int64_t);
}

bool XVariant_toBool(XVariant* var)
{
	if (var->type != XVariantType_Bool)
		return false;
	return XVariant_Data(var,bool);
}

char XVariant_toChar(XVariant* var)
{
	if (var->type != XVariantType_Char)
		return 0;
	return XVariant_Data(var, char);
}

unsigned char XVariant_toUChar(XVariant* var)
{
	if (var->type != XVariantType_UChar)
		return 0;
	return XVariant_Data(var, unsigned char);
}

int XVariant_toInt(XVariant* var)
{
	if (var->type != XVariantType_Int)
		return 0;
	return XVariant_Data(var, int);
}

size_t XVariant_toSize_t(XVariant* var)
{
	if (var->type != XVariantType_Size_t)
		return 0;
	return XVariant_Data(var, size_t);
}

void* XVariant_toPtr(XVariant* var)
{
	if (var->type != XVariantType_Ptr)
		return 0;
	return XVariant_Data(var, void*);
}

float XVariant_toFloat(XVariant* var)
{
	if (var->type != XVariantType_Float)
		return 0.0f;
	return XVariant_Data(var, float);
}

double XVariant_toDouble(XVariant* var)
{
	if (var->type != XVariantType_Double)
		return 0.0f;
	return XVariant_Data(var, double);
}

XPair* XVariant_toXPair(XVariant* var)
{
	if (var->type != XVariantType_Double)
		return 0;
	return XVariant_Data(var, XPair*);
}

XPoint XVariant_toXPoint(XVariant* var)
{
	if (var->type != XVariantType_XPoint)
	{
		XPoint p = { 0 };
		return p;
	}
	return XVariant_Data(var, XPoint);
}

void XVariant_delete(XVariant* var)
{
	if (var == NULL)
		return;
	if (var->type >= XVariantType_XVector && var->type <= XVariantType_XStack)
	{//当前是类
		XClass_delete_base(XVariant_Data(var,void*));
	}
	else if (var->type== XVariantType_XPair)
	{
		XPair_delete(XVariant_Data(var, void*));
	}

	XMemory_free(var);
}

int XVariant_type(XVariant* var)
{
	return var->type;
}

void* XVariant_data(XVariant* var)
{
	return XVariant_DataPtr(var);
}
