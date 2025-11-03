#ifndef XVARIANT_H
#define XVARIANT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XClass.h"
#include"XPoint.h"
#include"XCompare.h"
#include"XContainerObject.h"
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
/**
* @brief 定义XVariant支持的数据类型枚举
*/
typedef enum
{
	XVariantType_NULL,            ///< 空类型/其他未定义类型
	XVariantType_Uint8,           ///< 8位无符号整数类型
	XVariantType_Uint16,          ///< 16位无符号整数类型
	XVariantType_Uint32,          ///< 32位无符号整数类型
	XVariantType_Uint64,          ///< 64位无符号整数类型
	XVariantType_Int8,            ///< 8位有符号整数类型
	XVariantType_Int16,           ///< 16位有符号整数类型
	XVariantType_Int32,           ///< 32位有符号整数类型
	XVariantType_Int64,           ///< 64位有符号整数类型
	XVariantType_Bool,            ///< 布尔类型
	XVariantType_Char,            ///< 字符类型
	XVariantType_UChar,           ///< 无符号字符类型
	XVariantType_Int,             ///< 标准int类型
	XVariantType_Size_t,          ///< size_t类型
	XVariantType_Ptr,             ///< 指针类型
	XVariantType_Float,           ///< 单精度浮点类型
	XVariantType_Double,          ///< 双精度浮点类型
	/* 自定义的数据结构 */
	XVariantType_Pair,            ///< XPair结构体类型
	XVariantType_Point,           ///< XPoint结构体类型
	XVariantType_ByteArray,       ///< XByteArray类型（字节数组）
	XVariantType_String,          ///< XString类型（字符串）
	XVariantType_StringList,      ///< XStringList类型（字符串列表）
	XVariantType_List,            ///< XVariantList类型（变体列表）
	XVariantType_Map,             ///< XMap<XString, XVariant>类型（映射）
	XVariantType_Hash,            ///< XHashMap<XString, XVariant>类型（哈希表）
	XVariantType_JsonDocument,    ///< XJsonDocument类型（JSON文档）
	XVariantType_JsonArray,       ///< XJsonArray类型（JSON数组）
	XVariantType_JsonObject,      ///< XJsonObject类型（JSON对象）
	XVariantType_JsonValue,       ///< XJsonValue类型（JSON值）
	XVariantType_BsonArray,       ///< XBsonArray类型（BSON数组）
	XVariantType_BsonDocument,    ///< XBsonDocument类型（BSON文档）
	XVariantType_BsonValue,       ///< XBsonValue类型（BSON值）
	XVariantType_User,            ///< 用户定义类型
}XVariantType;
/**
* @brief 变体数据结构，可存储多种类型的数据
*/
typedef struct XVariant
{
	XClass m_class;           ///< 类信息（继承自XClass）
	int m_type;               ///< 数据类型（XVariantType枚举值）
	size_t m_dataSize;        ///< 数据大小（字节数）
	void* m_data;             ///< 数据指针（指向实际存储的数据）
}XVariant;
/**
* @brief 初始化XVariant类的虚函数表
* @return 指向XVariant类虚函数表的指针
*/
XVtable* XVariant_class_init();
/* 创建与初始化函数 */
/**
* @brief 创建一个XVariant数据
* 当传入data参数时，函数会复制数据内容；若传入NULL，则仅分配内存空间，
* 这种方式可配合XVariant_data函数创建自定义数据类型。
* @param data: XVariant内的数据(将会拷贝)
* @param dataSize: 数据大小
* @param type: 数据类型（XVariantType）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create(void* data, size_t dataSize, int type);
/**
* @brief 创建一个XVariant的副本，复制源对象的数据
* @param copy: 待复制的XVariant对象
* @retval 返回新的XVariant副本，失败返回NULL
*/
XVariant* XVariant_create_copy(const XVariant* copy);
/**
* @brief 创建一个XVariant，转移源对象的数据所有权
* @param move: 待转移数据的XVariant对象（转移后源对象数据失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_move(XVariant* move);
/**
* @brief 通过变量直接创建XVariant的宏
* 自动获取变量地址和大小，简化创建流程
* @param data: 变量数据
* @param type: 数据类型（XVariantType）
* @retval 调用XVariant_create创建的XVariant实例
*/
#define XVariant_Create(data,type)    XVariant_create(&data,sizeof(data),type)
/**
* @brief 初始化XVariant对象
* @param var: 待初始化的XVariant指针
* @param data: 初始化数据（会被拷贝）
* @param dataSize: 数据大小
* @param type: 数据类型（XVariantType）
*/
void XVariant_init(XVariant* var, void* data, size_t dataSize, int type);
/**
* @brief 声明并初始化XVariant对象的宏
* 简化局部变量的声明与初始化流程，自动创建临时变量并初始化
* @param var: 变量名（将作为XVariant指针使用）
* @param data: 初始化数据
* @param dataSize: 数据大小
* @param type: 数据类型（XVariantType）
*/
#define XVariant_Init(var,data,dataSize,type)  XVariant _##var,*var=&_##var;XVariant_init(var,data,dataSize,type)
/* 基础数据类型创建函数 */
/**
* @brief 创建一个空类型的XVariant
* @retval 返回空类型XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_null();
/**
* @brief 创建存储8位无符号整数的XVariant
* @param val: 8位无符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uint8(uint8_t val);
/**
* @brief 创建存储16位无符号整数的XVariant
* @param val: 16位无符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uint16(uint16_t val);
/**
* @brief 创建存储32位无符号整数的XVariant
* @param val: 32位无符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uint32(uint32_t val);
/**
* @brief 创建存储64位无符号整数的XVariant
* @param val: 64位无符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uint64(uint64_t val);
/**
* @brief 创建存储8位有符号整数的XVariant
* @param val: 8位有符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int8(int8_t val);
/**
* @brief 创建存储16位有符号整数的XVariant
* @param val: 16位有符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int16(int16_t val);
/**
* @brief 创建存储32位有符号整数的XVariant
* @param val: 32位有符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int32(int32_t val);
/**
* @brief 创建存储64位有符号整数的XVariant
* @param val: 64位有符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int64(int64_t val);
/**
* @brief 创建存储布尔值的XVariant
* @param val: 布尔数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_bool(bool val);
/**
* @brief 创建存储字符的XVariant
* @param val: 字符数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_char(char val);
/**
* @brief 创建存储无符号字符的XVariant
* @param val: 无符号字符数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uchar(unsigned char val);
/**
* @brief 创建存储int类型的XVariant
* @param val: int数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int(int val);
/**
* @brief 创建存储size_t类型的XVariant
* @param val: size_t数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_size_t(size_t val);
/**
* @brief 创建存储指针的XVariant
* @param val: 指针数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_ptr(void* val);
/**
* @brief 创建存储单精度浮点数的XVariant
* @param val: 单精度浮点数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_float(float val);
/**
* @brief 创建存储双精度浮点数的XVariant
* @param val: 双精度浮点数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_double(double val);
/* 自定义数据类型创建函数 */
/**
* @brief 创建存储XPair结构体的XVariant（复制源数据）
* @param val: 待存储的XPair指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_Pair(const XPair* val);
/**
* @brief 创建存储XPoint的XVariant
* @param val: 待存储的XPoint数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_Point(XPoint val);
/**
* @brief 创建存储XByteArray的XVariant（复制源数据）
* @param array: 待存储的XByteArray指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_ByteArray(const XByteArray* array);
/**
* @brief 创建存储XByteArray的XVariant（转移源数据所有权）
* @param array: 待转移的XByteArray指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_ByteArray_move(XByteArray* array);
/**
* @brief 创建存储XByteArray引用的XVariant（不复制数据，仅引用）
* @param array: 待引用的XByteArray指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_ByteArray_ref(XByteArray* array);
/**
* @brief 从原始数据创建XByteArray类型的XVariant
* @param data: 原始数据指针
* @param size: 数据大小
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_byteArray(const void* data, size_t size);
/**
* @brief 创建存储XString的XVariant（复制源数据）
* @param str: 待存储的XString指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_String(XString* str);
/**
* @brief 创建存储XString的XVariant（转移源数据所有权）
* @param str: 待转移的XString指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_String_move(XString* str);
/**
* @brief 创建存储XString引用的XVariant（不复制数据，仅引用）
* @param str: 待引用的XString指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_String_ref(XString* str);
/**
* @brief 从UTF8字符串创建XString类型的XVariant
* @param utf8: UTF8编码的字符串指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_utf8_str(const char* utf8);
/**
* @brief 创建存储XStringList的XVariant（复制源数据）
* @param list: 待存储的XStringList指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_StringList(const XStringList* list);
/**
* @brief 创建存储XStringList的XVariant（转移源数据所有权）
* @param list: 待转移的XStringList指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_StringList_move(XStringList* list);
/**
* @brief 创建存储XStringList引用的XVariant（不复制数据，仅引用）
* @param list: 待引用的XStringList指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_StringList_ref(XStringList* list);
/**
* @brief 创建存储XVariantList的XVariant（复制源数据）
* @param list: 待存储的XVariantList指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_list(const XVariantList* list);
/**
* @brief 创建存储XVariantList的XVariant（转移源数据所有权）
* @param list: 待转移的XVariantList指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_list_move(XVariantList* list);
/**
* @brief 创建存储XVariantList引用的XVariant（不复制数据，仅引用）
* @param list: 待引用的XVariantList指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_list_ref(XVariantList* list);
/**
* @brief 创建存储XMap<XString, XVariant>的XVariant（复制源数据）
* @param map: 待存储的XVariantMap指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_map(const XVariantMap* map);//XMap<XString, XVariant>
/**
* @brief 创建存储XMap<XString, XVariant>的XVariant（转移源数据所有权）
* @param map: 待转移的XVariantMap指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_map_move(XVariantMap* map);//XMap<XString, XVariant>
/**
* @brief 创建存储XMap<XString, XVariant>引用的XVariant（不复制数据，仅引用）
* @param map: 待引用的XVariantMap指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_map_ref(XVariantMap* map);//XMap<XString, XVariant>
/**
* @brief 创建存储XHashMap<XString, XVariant>的XVariant（复制源数据）
* @param hash: 待存储的XHashMap指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_hash(const XHashMap* hash);//XHashMap<XString, XVariant>
/**
* @brief 创建存储XHashMap<XString, XVariant>的XVariant（转移源数据所有权）
* @param hash: 待转移的XHashMap指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_hash_move(XHashMap* hash);//XHashMap<XString, XVariant>
/**
* @brief 创建存储XHashMap<XString, XVariant>引用的XVariant（不复制数据，仅引用）
* @param hash: 待引用的XHashMap指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_hash_ref(XHashMap* hash);//XHashMap<XString, XVariant>
/**
* @brief 创建存储XJsonDocument的XVariant（复制源数据）
* @param doc: 待存储的XJsonDocument指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonDocument(const XJsonDocument* doc);
/**
* @brief 创建存储XJsonDocument的XVariant（转移源数据所有权）
* @param doc: 待转移的XJsonDocument指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonDocument_move(XJsonDocument* doc);
/**
* @brief 创建存储XJsonDocument引用的XVariant（不复制数据，仅引用）
* @param doc: 待引用的XJsonDocument指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonDocument_ref(XJsonDocument* doc);
/**
* @brief 创建存储XJsonArray的XVariant（复制源数据）
* @param arr: 待存储的XJsonArray指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonArray(const XJsonArray* arr);
/**
* @brief 创建存储XJsonArray的XVariant（转移源数据所有权）
* @param arr: 待转移的XJsonArray指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonArray_move(XJsonArray* arr);
/**
* @brief 创建存储XJsonArray引用的XVariant（不复制数据，仅引用）
* @param arr: 待引用的XJsonArray指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonArray_ref(XJsonArray* arr);
/**
* @brief 创建存储XJsonObject的XVariant（复制源数据）
* @param obj: 待存储的XJsonObject指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonObject(const XJsonObject* obj);
/**
* @brief 创建存储XJsonObject的XVariant（转移源数据所有权）
* @param obj: 待转移的XJsonObject指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonObject_move(XJsonObject* obj);
/**
* @brief 创建存储XJsonObject引用的XVariant（不复制数据，仅引用）
* @param obj: 待引用的XJsonObject指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonObject_ref(XJsonObject* obj);
/**
* @brief 创建存储XJsonValue的XVariant（复制源数据）
* @param val: 待存储的XJsonValue指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonValue(const XJsonValue* val);
/**
* @brief 创建存储XJsonValue的XVariant（转移源数据所有权）
* @param val: 待转移的XJsonValue指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonValue_move(XJsonValue* val);
/**
* @brief 创建存储XJsonValue引用的XVariant（不复制数据，仅引用）
* @param val: 待引用的XJsonValue指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_JsonValue_ref(XJsonValue* val);
/**
* @brief 创建存储XBsonDocument的XVariant（复制源数据）
* @param doc: 待存储的XBsonDocument指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_BsonDocument(const XBsonDocument* doc);
/**
* @brief 创建存储XBsonDocument的XVariant（转移源数据所有权）
* @param doc: 待转移的XBsonDocument指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_BsonDocument_move(XBsonDocument* doc);
/**
* @brief 创建存储XBsonDocument引用的XVariant（不复制数据，仅引用）
* @param doc: 待引用的XBsonDocument指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_BsonDocument_ref(XBsonDocument* doc);
/**
* @brief 创建存储XBsonArray的XVariant（复制源数据）
* @param arr: 待存储的XBsonArray指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_BsonArray(const XBsonArray* arr);
/**
* @brief 创建存储XBsonArray的XVariant（转移源数据所有权）
* @param arr: 待转移的XBsonArray指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_BsonArray_move(XBsonArray* arr);
/**
* @brief 创建存储XBsonArray引用的XVariant（不复制数据，仅引用）
* @param arr: 待引用的XBsonArray指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_BsonArray_ref(XBsonArray* arr);
/**
* @brief 创建存储XBsonValue的XVariant（复制源数据）
* @param val: 待存储的XBsonValue指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_BsonValue(const XBsonValue* val);
/**
* @brief 创建存储XBsonValue的XVariant（转移源数据所有权）
* @param val: 待转移的XBsonValue指针（转移后源对象失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_BsonValue_move(XBsonValue* val);
/**
* @brief 创建存储XBsonValue引用的XVariant（不复制数据，仅引用）
* @param val: 待引用的XBsonValue指针
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_BsonValue_ref(XBsonValue* val);
/* 数据转换函数（获取数据） */
/**
* @brief 获取XVariant中指定类型数据的引用
* @param var: XVariant对象指针
* @param type: 期望的数据类型（XVariantType）
* @retval 指向数据的指针，类型不匹配返回NULL
*/
void* XVariant_toRef(const XVariant* var, XVariantType type);
/**
* @brief 将XVariant中的数据转换为8位无符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的8位无符号整数，类型不匹配返回默认值
*/
uint8_t  XVariant_toUint8(const XVariant* var);
/**
* @brief 获取XVariant中8位无符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向8位无符号整数数据的指针，类型不匹配返回NULL
*/
uint8_t* XVariant_toUint8_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为16位无符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的16位无符号整数，类型不匹配返回默认值
*/
uint16_t XVariant_toUint16(const XVariant* var);
/**
* @brief 获取XVariant中16位无符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向16位无符号整数数据的指针，类型不匹配返回NULL
*/
uint16_t* XVariant_toUint16_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为32位无符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的32位无符号整数，类型不匹配返回默认值
*/
uint32_t XVariant_toUint32(const XVariant* var);
/**
* @brief 获取XVariant中32位无符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向32位无符号整数数据的指针，类型不匹配返回NULL
*/
uint32_t* XVariant_toUint32_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为64位无符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的64位无符号整数，类型不匹配返回默认值
*/
uint64_t XVariant_toUint64(const XVariant* var);
/**
* @brief 获取XVariant中64位无符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向64位无符号整数数据的指针，类型不匹配返回NULL
*/
uint64_t* XVariant_toUint64_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为8位有符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的8位有符号整数，类型不匹配返回默认值
*/
int8_t  XVariant_toInt8(const XVariant* var);
/**
* @brief 获取XVariant中8位有符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向8位有符号整数数据的指针，类型不匹配返回NULL
*/
int8_t* XVariant_toInt8_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为16位有符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的16位有符号整数，类型不匹配返回默认值
*/
int16_t XVariant_toInt16(const XVariant* var);
/**
* @brief 获取XVariant中16位有符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向16位有符号整数数据的指针，类型不匹配返回NULL
*/
int16_t* XVariant_toInt16_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为32位有符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的32位有符号整数，类型不匹配返回默认值
*/
int32_t XVariant_toInt32(const XVariant* var);
/**
* @brief 获取XVariant中32位有符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向32位有符号整数数据的指针，类型不匹配返回NULL
*/
int32_t* XVariant_toInt32_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为64位有符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的64位有符号整数，类型不匹配返回默认值
*/
int64_t XVariant_toInt64(const XVariant* var);
/**
* @brief 获取XVariant中64位有符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向64位有符号整数数据的指针，类型不匹配返回NULL
*/
int64_t* XVariant_toInt64_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为布尔值并返回
* @param var: XVariant对象指针
* @retval 转换后的布尔值，类型不匹配返回默认值
*/
bool XVariant_toBool(const XVariant* var);
/**
* @brief 获取XVariant中布尔数据的指针
* @param var: XVariant对象指针
* @retval 指向布尔数据的指针，类型不匹配返回NULL
*/
bool* XVariant_toBool_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为字符并返回
* @param var: XVariant对象指针
* @retval 转换后的字符，类型不匹配返回默认值
*/
char XVariant_toChar(const XVariant* var);
/**
* @brief 获取XVariant中字符数据的指针
* @param var: XVariant对象指针
* @retval 指向字符数据的指针，类型不匹配返回NULL
*/
char* XVariant_toChar_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为无符号字符并返回
* @param var: XVariant对象指针
* @retval 转换后的无符号字符，类型不匹配返回默认值
*/
unsigned char XVariant_toUChar(const XVariant* var);
/**
* @brief 获取XVariant中无符号字符数据的指针
* @param var: XVariant对象指针
* @retval 指向无符号字符数据的指针，类型不匹配返回NULL
*/
unsigned char* XVariant_toUChar_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为int并返回
* @param var: XVariant对象指针
* @retval 转换后的int值，类型不匹配返回默认值
*/
int XVariant_toInt(const XVariant* var);
/**
* @brief 获取XVariant中int数据的指针
* @param var: XVariant对象指针
* @retval 指向int数据的指针，类型不匹配返回NULL
*/
int* XVariant_toInt_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为size_t并返回
* @param var: XVariant对象指针
* @retval 转换后的size_t值，类型不匹配返回默认值
*/
size_t XVariant_toSize_t(const XVariant* var);
/**
* @brief 获取XVariant中size_t数据的指针
* @param var: XVariant对象指针
* @retval 指向size_t数据的指针，类型不匹配返回NULL
*/
size_t* XVariant_toSize_t_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为指针并返回
* @param var: XVariant对象指针
* @retval 转换后的指针，类型不匹配返回NULL
*/
void* XVariant_toPtr(const XVariant* var);
/**
* @brief 获取XVariant中指针数据的指针
* @param var: XVariant对象指针
* @retval 指向指针数据的二级指针，类型不匹配返回NULL
*/
void** XVariant_toPtr_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为单精度浮点数并返回
* @param var: XVariant对象指针
* @retval 转换后的单精度浮点数，类型不匹配返回默认值
*/
float XVariant_toFloat(const XVariant* var);
/**
* @brief 获取XVariant中单精度浮点数数据的指针
* @param var: XVariant对象指针
* @retval 指向单精度浮点数数据的指针，类型不匹配返回NULL
*/
float* XVariant_toFloat_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为双精度浮点数并返回
* @param var: XVariant对象指针
* @retval 转换后的双精度浮点数，类型不匹配返回默认值
*/
double XVariant_toDouble(const XVariant* var);
/**
* @brief 获取XVariant中双精度浮点数数据的指针
* @param var: XVariant对象指针
* @retval 指向双精度浮点数数据的指针，类型不匹配返回NULL
*/
double* XVariant_toDouble_ref(const XVariant* var);
/**
* @brief 获取XVariant中XPair数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XPair数据的指针，类型不匹配返回NULL
*/
XPair* XVariant_toPair(const XVariant* var);
/**
* @brief 获取XVariant中XPair数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XPair数据的引用指针，类型不匹配返回NULL
*/
XPair* XVariant_toPair_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为XPoint并返回
* @param var: XVariant对象指针
* @retval 转换后的XPoint，类型不匹配返回默认值
*/
XPoint XVariant_toPoint(const XVariant* var);
/**
* @brief 获取XVariant中XByteArray数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XByteArray数据的指针，类型不匹配返回NULL
*/
XByteArray* XVariant_toByteArray(const XVariant* var);
/**
* @brief 获取XVariant中XByteArray数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XByteArray数据的引用指针，类型不匹配返回NULL
*/
XByteArray* XVariant_toByteArray_ref(const XVariant* var);
/**
* @brief 获取XVariant中XString数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XString数据的指针，类型不匹配返回NULL
*/
XString* XVariant_toString(const XVariant* var);
/**
* @brief 获取XVariant中XString数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XString数据的引用指针，类型不匹配返回NULL
*/
XString* XVariant_toString_ref(const XVariant* var);
/**
* @brief 获取XVariant中XStringList数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XStringList数据的指针，类型不匹配返回NULL
*/
XStringList* XVariant_toStringList(const XVariant* var);
/**
* @brief 获取XVariant中XStringList数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XStringList数据的引用指针，类型不匹配返回NULL
*/
XStringList* XVariant_toStringList_ref(const XVariant* var);
/**
* @brief 获取XVariant中XVariantList数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XVariantList数据的指针，类型不匹配返回NULL
*/
XVariantList* XVariant_toList(const XVariant* var);
/**
* @brief 获取XVariant中XVariantList数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XVariantList数据的引用指针，类型不匹配返回NULL
*/
XVariantList* XVariant_toList_ref(const XVariant* var);
/**
* @brief 获取XVariant中XMap<XString, XVariant>数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XVariantMap数据的指针，类型不匹配返回NULL
*/
XVariantMap* XVariant_toMap(const XVariant* var);//XMap<XString, XVariant>
/**
* @brief 获取XVariant中XMap<XString, XVariant>数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XVariantMap数据的引用指针，类型不匹配返回NULL
*/
XVariantMap* XVariant_toMap_ref(const XVariant* var);//XMap<XString, XVariant>
/**
* @brief 获取XVariant中XHashMap<XString, XVariant>数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XVariantHashMap数据的指针，类型不匹配返回NULL
*/
XVariantHashMap* XVariant_toHash(const XVariant* var);//XHashMap<XString, XVariant>
/**
* @brief 获取XVariant中XHashMap<XString, XVariant>数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XVariantHashMap数据的引用指针，类型不匹配返回NULL
*/
XVariantHashMap* XVariant_toHash_ref(const XVariant* var);//XHashMap<XString, XVariant>
/**
* @brief 获取XVariant中XJsonDocument数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XJsonDocument数据的指针，类型不匹配返回NULL
*/
XJsonDocument* XVariant_toJsonDocument(const XVariant* var);
/**
* @brief 获取XVariant中XJsonDocument数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XJsonDocument数据的引用指针，类型不匹配返回NULL
*/
XJsonDocument* XVariant_toJsonDocument_ref(const XVariant* var);
/**
* @brief 获取XVariant中XJsonArray数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XJsonArray数据的指针，类型不匹配返回NULL
*/
XJsonArray* XVariant_toJsonArray(const XVariant* var);
/**
* @brief 获取XVariant中XJsonArray数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XJsonArray数据的引用指针，类型不匹配返回NULL
*/
XJsonArray* XVariant_toJsonArray_ref(const XVariant* var);
/**
* @brief 获取XVariant中XJsonObject数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XJsonObject数据的指针，类型不匹配返回NULL
*/
XJsonObject* XVariant_toJsonObject(const XVariant* var);
/**
* @brief 获取XVariant中XJsonObject数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XJsonObject数据的引用指针，类型不匹配返回NULL
*/
XJsonObject* XVariant_toJsonObject_ref(const XVariant* var);
/**
* @brief 获取XVariant中XJsonValue数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XJsonValue数据的指针，类型不匹配返回NULL
*/
XJsonValue* XVariant_toJsonValue(const XVariant* var);
/**
* @brief 获取XVariant中XJsonValue数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XJsonValue数据的引用指针，类型不匹配返回NULL
*/
XJsonValue* XVariant_toJsonValue_ref(const XVariant* var);
/**
* @brief 获取XVariant中XBsonDocument数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XBsonDocument数据的指针，类型不匹配返回NULL
*/
XBsonDocument* XVariant_toBsonDocument(const XVariant* var);
/**
* @brief 获取XVariant中XBsonDocument数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XBsonDocument数据的引用指针，类型不匹配返回NULL
*/
XBsonDocument* XVariant_toBsonDocument_ref(const XVariant* var);
/**
* @brief 获取XVariant中XBsonArray数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XBsonArray数据的指针，类型不匹配返回NULL
*/
XBsonArray* XVariant_toBsonArray(const XVariant* var);
/**
* @brief 获取XVariant中XBsonArray数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XBsonArray数据的引用指针，类型不匹配返回NULL
*/
XBsonArray* XVariant_toBsonArray_ref(const XVariant* var);
/**
* @brief 获取XVariant中XBsonValue数据的指针（复制数据）
* @param var: XVariant对象指针
* @retval 指向XBsonValue数据的指针，类型不匹配返回NULL
*/
XBsonValue* XVariant_toBsonValue(const XVariant* var);
/**
* @brief 获取XVariant中XBsonValue数据的引用指针
* @param var: XVariant对象指针
* @retval 指向XBsonValue数据的引用指针，类型不匹配返回NULL
*/
XBsonValue* XVariant_toBsonValue_ref(const XVariant* var);
/* 数据设置函数 */
/**
* @brief 将XVariant的值设置为另一个XVariant的值（复制数据）
* @param var: 目标XVariant指针
* @param newVar: 源XVariant指针（提供新值）
*/
void XVariant_setValue(XVariant* var, const XVariant* newVar);
/**
* @brief 将XVariant设置为空类型
* @param var: 目标XVariant指针
*/
void XVariant_setValue_null(XVariant* var);
/**
* @brief 设置XVariant的值为8位无符号整数
* @param var: 目标XVariant指针
* @param val: 8位无符号整数数据
*/
void XVariant_setValue_uint8(XVariant* var, uint8_t val);
/**
* @brief 设置XVariant的值为16位无符号整数
* @param var: 目标XVariant指针
* @param val: 16位无符号整数数据
*/
void XVariant_setValue_uint16(XVariant* var, uint16_t val);
/**
* @brief 设置XVariant的值为32位无符号整数
* @param var: 目标XVariant指针
* @param val: 32位无符号整数数据
*/
void XVariant_setValue_uint32(XVariant* var, uint32_t val);
/**
* @brief 设置XVariant的值为64位无符号整数
* @param var: 目标XVariant指针
* @param val: 64位无符号整数数据
*/
void XVariant_setValue_uint64(XVariant* var, uint64_t val);
/**
* @brief 设置XVariant的值为8位有符号整数
* @param var: 目标XVariant指针
* @param val: 8位有符号整数数据
*/
void XVariant_setValue_int8(XVariant* var, int8_t val);
/**
* @brief 设置XVariant的值为16位有符号整数
* @param var: 目标XVariant指针
* @param val: 16位有符号整数数据
*/
void XVariant_setValue_int16(XVariant* var, int16_t val);
/**
* @brief 设置XVariant的值为32位有符号整数
* @param var: 目标XVariant指针
* @param val: 32位有符号整数数据
*/
void XVariant_setValue_int32(XVariant* var, int32_t val);
/**
* @brief 设置XVariant的值为64位有符号整数
* @param var: 目标XVariant指针
* @param val: 64位有符号整数数据
*/
void XVariant_setValue_int64(XVariant* var, int64_t val);
/**
* @brief 设置XVariant的值为布尔值
* @param var: 目标XVariant指针
* @param val: 布尔数据
*/
void XVariant_setValue_bool(XVariant* var, bool val);
/**
* @brief 设置XVariant的值为字符
* @param var: 目标XVariant指针
* @param val: 字符数据
*/
void XVariant_setValue_char(XVariant* var, char val);
/**
* @brief 设置XVariant的值为无符号字符
* @param var: 目标XVariant指针
* @param val: 无符号字符数据
*/
void XVariant_setValue_uchar(XVariant* var, unsigned char val);
/**
* @brief 设置XVariant的值为int类型
* @param var: 目标XVariant指针
* @param val: int数据
*/
void XVariant_setValue_int(XVariant* var, int val);

#define XVariant_copy_base			XClass_copy_base
#define XVariant_move_base			XClass_move_base
#define XVariant_deinit_base		XClass_deinit_base
#define XVariant_delete_base		XClass_delete_base
#define XVariant_Value(Var,Type)   (*((Type*)XVariant_data(Var)))

#ifdef __cplusplus
}
#endif
#endif // XVARIANT_H