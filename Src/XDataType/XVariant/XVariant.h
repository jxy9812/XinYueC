#ifndef XVARIANT_H
#define XVARIANT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
#include"XPoint.h"
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
typedef enum
{
	XVariantType_Other,//其他
	XVariantType_Uint8,
	XVariantType_Uint16,
	XVariantType_Uint32,
	XVariantType_Uint64,
	XVariantType_Int8,
	XVariantType_Int16,
	XVariantType_Int32,
	XVariantType_Int64,
	XVariantType_Bool,
	XVariantType_Char,
	XVariantType_UChar,
	XVariantType_Int,
	XVariantType_Size_t,
	XVariantType_Ptr,
	XVariantType_Float,
	XVariantType_Double,
	/*自定义的数据结构*/
	XVariantType_XPair,
	XVariantType_XPoint,
	/* 类*/
	XVariantType_XVector,
	XVariantType_XString,
	XVariantType_XByteArray,
	XVariantType_XListBase,
	XVariantType_XMapBase,
	XVariantType_XQueueBase,
	XVariantType_XSetBase,
	XVariantType_XStack,
}XVariantType;

//普通创建
XVariant* XVariant_create(void* data, size_t dataSize,int type);
//变量的方式直接创建
#define XVariant_Create(data,type)    XVariant_create(&data,sizeof(data),type)
/*					基础数据					*/			
XVariant* XVariant_create_uint8 (uint8_t val);
XVariant* XVariant_create_uint16(uint16_t val);
XVariant* XVariant_create_uint32(uint32_t val);
XVariant* XVariant_create_uint64(uint64_t val);
XVariant* XVariant_create_int8 (int8_t val);
XVariant* XVariant_create_int16(int16_t val);
XVariant* XVariant_create_int32(int32_t val);
XVariant* XVariant_create_int64(int64_t val);
XVariant* XVariant_create_bool(bool val);
XVariant* XVariant_create_char(char val);
XVariant* XVariant_create_uchar(unsigned char val);
XVariant* XVariant_create_int (int val);
XVariant* XVariant_create_size_t(size_t val);
XVariant* XVariant_create_ptr(void* val);
XVariant* XVariant_create_float(float val);
XVariant* XVariant_create_double(double val);
/*				自定义数据										*/		
XVariant* XVariant_create_XPair(XPair* val);
XVariant* XVariant_create_XPoint(XPoint val);
/*				类构造     效率考虑是转移所有权					*/
XVariant* XVariant_create_XVector(XVector* val);
XVariant* XVariant_create_XString(XString* val);
XVariant* XVariant_create_XByteArray(XByteArray* val);
XVariant* XVariant_create_XListBase(XListBase* val);
XVariant* XVariant_create_XMapBase(XMapBase* val);
XVariant* XVariant_create_XQueueBase(XQueueBase* val);
XVariant* XVariant_create_XSetBase(XSetBase* val);
XVariant* XVariant_create_XStack(XStack* val);

uint8_t  XVariant_toUint8 (XVariant* var);
uint16_t XVariant_toUint16(XVariant* var);
uint32_t XVariant_toUint32(XVariant* var);
uint64_t XVariant_toUint64(XVariant* var);
int8_t  XVariant_toInt8 (XVariant* var);
int16_t XVariant_toInt16(XVariant* var);
int32_t XVariant_toInt32(XVariant* var);
int64_t XVariant_toInt64(XVariant* var);
bool XVariant_toBool(XVariant* var);
char XVariant_toChar(XVariant* var);
unsigned char XVariant_toUChar(XVariant* var);
int XVariant_toInt(XVariant* var);
size_t XVariant_toSize_t(XVariant* var);
void* XVariant_toPtr(XVariant* var);
float XVariant_toFloat(XVariant* var);
double XVariant_toDouble(XVariant* var);

XPair* XVariant_toXPair(XVariant* var);
XPoint XVariant_toXPoint(XVariant* var);


void XVariant_delete(XVariant* var);
int  XVariant_type(XVariant* var);
void* XVariant_data(XVariant* var);
#define XVariant_Data(Var,Type)   (*((Type*)XVariant_data(Var)))
#ifdef __cplusplus
}
#endif
#endif // ! XPOINT_H
