#ifndef XVARIANT_H
#define XVARIANT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
#include"XPoint.h"
#include"XFunctionCallback.h"
#include"XEquality.h"
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
	XVariantType_Pair,//XPair
	XVariantType_Point,//XPoint
	XVariantType_ByteArray,//XByteArray
	XVariantType_String,//XString
	XVariantType_StringList,//XStringList
	XVariantType_List,//XVariantList
	XVariantType_MapBase,//XMapBase<XString, XVariant>
	XVariantType_User,//用户定义类型
}XVariantType;
typedef struct XVariant
{
	int m_type;//类型
	//XEquality m_equality;//相等比较函数
	size_t m_dataSize;//数据大小
	void* m_data;//数据
}XVariant;
/*
* @brief  创建一个XVariant数据
* 当传入data参数时，函数会复制数据内容；若传入NULL，则仅分配内存空间，
* 这种方式可配合XVariant_data函数创建自定义数据类型。
*
* @param  data:XVariant内的数据(将会拷贝) 
* @param  dataSize:数据大小
* @param  type:数据类型
* @retval 返回新的XVariant
*/
XVariant* XVariant_create(void* data, size_t dataSize,int type);
//变量的方式直接创建
#define XVariant_Create(data,type)    XVariant_create(&data,sizeof(data),type)
void XVariant_init(XVariant* var, void* data, size_t dataSize, int type);
#define XVariant_Init(var,data,dataSize,type)  XVariant _##var,*var=&_##var;XVariant_init(var,data,dataSize,type)
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
XVariant* XVariant_create_Pair(const XPair* val);
XVariant* XVariant_create_Point(XPoint val);
XVariant* XVariant_create_ByteArray(const XByteArray* array);
XVariant* XVariant_create_byteArray(const void* data, size_t size);
XVariant* XVariant_create_String(XString* string);
XVariant* XVariant_create_utf8_str(const char* str);
XVariant* XVariant_create_StringList(const XStringList* list);
XVariant* XVariant_create_List(const XVariantList* list);
XVariant* XVariant_create_Map(const XVariantMap* map);//XMap<XString, XVariant>
XVariant* XVariant_create_Hash(const XHashMap* map);//XHashMap<XString, XVariant>

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
XPair* XVariant_toPair(XVariant* var);
XPoint XVariant_toPoint(XVariant* var);
XByteArray* XVariant_toByteArray(XVariant* var);
XString* XVariant_toString(XVariant* var);
XStringList* XVariant_toStringList(XVariant* var);
XVariantList* XVariant_toList(XVariant* var);
XVariantMap* XVariant_toMap(XVariant* var);//XMap<XString, XVariant>
XVariantHashMap* XVariant_toHash(XVariant* var);//XHashMap<XString, XVariant>

void XVariant_setValue(XVariant* var, const XVariant* newVar);
void XVariant_setValue_uint8 (XVariant* var, uint8_t val);
void XVariant_setValue_uint16(XVariant* var, uint16_t val);
void XVariant_setValue_uint32(XVariant* var, uint32_t val);
void XVariant_setValue_uint64(XVariant* var, uint64_t val);
void XVariant_setValue_int8 (XVariant* var, int8_t val);
void XVariant_setValue_int16(XVariant* var, int16_t val);
void XVariant_setValue_int32(XVariant* var, int32_t val);
void XVariant_setValue_int64(XVariant* var, int64_t val);
void XVariant_setValue_bool (XVariant* var, bool val);
void XVariant_setValue_char	(XVariant* var, char val);
void XVariant_setValue_uchar(XVariant* var, unsigned char val);
void XVariant_setValue_int(XVariant* var, int val);
void XVariant_setValue_size_t(XVariant* var, size_t val);
void XVariant_setValue_ptr(XVariant* var, void* val);
void XVariant_setValue_float(XVariant* var, float val);
void XVariant_setValue_double(XVariant* var, double val);
void XVariant_setValue_Pair(XVariant* var, const XPair* pair);
void XVariant_setValue_Point(XVariant* var, XPoint val);
void XVariant_setValue_ByteArray(XVariant* var,const XByteArray* array);
void XVariant_setValue_byteArray(XVariant* var, const void* data, size_t size);
void XVariant_setValue_String(XVariant* var, const XString* string);
void XVariant_setValue_utf8_str(XVariant* var, const char* str);
void XVariant_setValue_StringList(XVariant* var, const XStringList* list);

void XVariant_copy(XVariant* var, const XVariant* src);
void XVariant_move(XVariant* var, XVariant* src);
void XVariant_delete(XVariant* var);
void XVariant_deinit(XVariant* var);
void XVariant_clear(XVariant* var);
void XVariant_swap(XVariant* var,XVariant* other);
int  XVariant_type(XVariant* var);
const char* XVariant_typeName(XVariant* var);
bool XVariant_equality(XVariant* var, XVariant* cmp);
//设置自定义的数据相等比较回调函数
void XVariant_setUserEquality(int type, XEquality equality);
//设置自定义数据的类型名字
void XVariant_setUserTypeName(int type, const char* typeName);
//删除用户定义的类型属性
void XVariant_removeUserTypeProperty(int type);
/*
* @brief  获取XVariant的数据
* @param  var:XVariant指针
* @retval 返回XVariant内部的数据首地址
*/
void* XVariant_data(XVariant* var);
size_t XVariant_dataSize(XVariant* var);
#define XVariant_Value(Var,Type)   (*((Type*)XVariant_data(Var)))
#ifdef __cplusplus
}
#endif
#endif // ! XPOINT_H
