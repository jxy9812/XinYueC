#include "XVariant.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPair.h"
#include "XByteArray.h"
#include "XString.h"
#include "XAlgorithm.h"
#include "XVariantList.h"
#include "XStringList.h"
#include "XHashMap.h"
#include "XMap.h"
#include "XJsonDocument.h"
#include "XJsonArray.h"
#include "XBsonValue.h"
#include "XJsonObject.h"
#include "XBsonDocument.h"
#include "XBsonArray.h"
#include <string.h>
#include <stdlib.h>
static XHashMap*global_typeProperty= NULL;//自定义数据属性哈希映射
//类型属性
typedef struct TypeProperty
{
	XEquality			compare;//等于比较
	XCDataCopyMethod	copyMethod;//数据拷贝方法
	XCDataMoveMethod	moveMethod;//数据移动方法
	XCDataDeinitMethod  deinitMethod;//数据释放方法
	XCDataClearMethod   clearMethod;//数据清除方法
	char*				typeName;//类型名字
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
default:return 0;\
}\
//自定义的类型哈希表初始化
static bool global_typeHash_init()
{
	if (global_typeProperty)
		return true;
	global_typeProperty = XHashMap_Create(int, TypeProperty, int_compare);
	if (global_typeProperty)
		return true;
	return false;
}
static void VXVariant_move(XVariant* var, XVariant* src);
static void VXVariant_copy(XVariant* var, const XVariant* src);
static void VXVariant_deinit(XVariant* var);
XVtable* XVariant_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//	void* table[] = { VXClass_copy,VXClass_move,VXClass_deinit };
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXVariant_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXVariant_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVariant_deinit);
#if SHOWCONTAINERSIZE
	printf("XVariant size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}

XVariant* XVariant_create(void* data, size_t dataSize, int type)
{
	XVariant* var = XMemory_malloc(sizeof(XVariant));
	XVariant_init(var,data,dataSize,type);
	Set_Class_MemoryFree(var, XFree);
	return var;
}

XVariant* XVariant_create_copy(const XVariant* copy)
{
	XJsonArray* var = XVariant_create_null();
	if (var && copy)
		XVariant_copy_base(var, copy);
	return var;
}

XVariant* XVariant_create_move(XVariant* move)
{
	XJsonArray* var = XVariant_create_null();
	if (var && move)
		XVariant_move_base(var, move);
	return var;
}

void XVariant_init(XVariant* var, void* data, size_t dataSize, int type)
{
	if (var == NULL)
		return NULL;
	XClass_init(var);
	XClassGetVtable(var) = XVariant_class_init();
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

XVariant* XVariant_create_null()
{
	return XVariant_create(NULL, 0,XVariantType_NULL);
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

XVariant* XVariant_create_Pair(const XPair* val)
{
	if (val == NULL)
		return NULL;
	return XVariant_create(val, XPair_size(val), XVariantType_Pair);
}

XVariant* XVariant_create_Point(XPoint val)
{
	return XVariant_Create(val, XVariantType_Point);
}

XVariant* XVariant_create_ByteArray(const XByteArray* array)
{
	if (array == NULL)
		return NULL;
	XVariant* var=XVariant_create(NULL,sizeof(XByteArray), XVariantType_ByteArray);
	XByteArray_init(XVariant_DataPtr(var));
	XByteArray_copy_base(XVariant_DataPtr(var),array);
	return var;
}

XVariant* XVariant_create_ByteArray_move(XByteArray* array)
{
	if (array == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XByteArray), XVariantType_ByteArray);
	XByteArray_init(XVariant_DataPtr(var));
	XByteArray_move_base(XVariant_DataPtr(var), array);
	return var;
}

XVariant* XVariant_create_ByteArray_ref(XByteArray* array)
{
	if (array == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_ByteArray);
	if (var == NULL)
		return NULL;
	var->m_data = array;
	var->m_dataSize = sizeof(XByteArray);
	return var;
}

XVariant* XVariant_create_byteArray(const void* data, size_t size)
{
	if (data == NULL||size==0)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XByteArray), XVariantType_ByteArray);
	XByteArray_init(XVariant_DataPtr(var));
	XByteArray_resize_base(XVariant_DataPtr(var),size);
	memcpy(XContainerDataPtr(XVariant_DataPtr(var)),data,size);
	return var;
}

XVariant* XVariant_create_String(XString* str)
{
	if(str==NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XString), XVariantType_String);
	XString_init(XVariant_DataPtr(var));
	XString_copy_base(XVariant_DataPtr(var), str);
	return var;
}

XVariant* XVariant_create_String_move(XString* str)
{
	if (str == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XString), XVariantType_String);
	XString_init(XVariant_DataPtr(var));
	XString_move_base(XVariant_DataPtr(var), str);
	return var;
}

XVariant* XVariant_create_String_ref(XString* str)
{
	if (str == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_String);
	if (var == NULL)
		return NULL;
	var->m_data = str;
	var->m_dataSize = sizeof(XString);
	return var;
}

XVariant* XVariant_create_utf8_str(const char* utf8)
{
	if (utf8 == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XString), XVariantType_String);
	XString_init(XVariant_DataPtr(var));
	XString_assign_utf8(XVariant_DataPtr(var),utf8);
	return var;
}

XVariant* XVariant_create_StringList(const XStringList* list)
{
	if (list == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XStringList), XVariantType_StringList);
	XStringList_init(XVariant_DataPtr(var));
	XStringList_copy_base(XVariant_DataPtr(var), list);
	return var;
}

XVariant* XVariant_create_StringList_move(XStringList* list)
{
	if (list == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XStringList), XVariantType_StringList);
	XStringList_init(XVariant_DataPtr(var));
	XStringList_move_base(XVariant_DataPtr(var), list);
	return var;
}

XVariant* XVariant_create_StringList_ref(XStringList* list)
{
	if (list == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_StringList);
	if (var == NULL)
		return NULL;
	var->m_data = list;
	var->m_dataSize = sizeof(XStringList);
	return var;
}

XVariant* XVariant_create_list(const XVariantList* list)
{
	if (list == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XVariantList), XVariantType_List);
	XVariantList_init(XVariant_DataPtr(var));
	XVariantList_copy_base(XVariant_DataPtr(var), list);
	return var;
}
XVariant* XVariant_create_list_move(XVariantList* list)
{
	if (list == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XVariantList), XVariantType_List);
	XVariantList_init(XVariant_DataPtr(var));
	XVariantList_move_base(XVariant_DataPtr(var), list);
	return var;
}
XVariant* XVariant_create_list_ref(XVariantList* list)
{
	if (list == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_List);
	if (var == NULL)
		return NULL;
	var->m_data = list;
	var->m_dataSize = sizeof(XVariantList);
	return var;
}
//XMap<XString, XVariant>
XVariant* XVariant_create_map(const XVariantMap* map)
{
	if (map == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XVariantMap), XVariantType_Map);
	XMap_init(XVariant_DataPtr(var), ((XMapBase*)map)->m_keyTypeSize,XContainerTypeSize(map), XContainerCompare(map));
	XMap_copy_base(XVariant_DataPtr(var), map);
	return var;
}

XVariant* XVariant_create_map_move(XVariantMap* map)
{
	if (map == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XVariantMap), XVariantType_Map);
	XMap_init(XVariant_DataPtr(var), ((XMapBase*)map)->m_keyTypeSize, XContainerTypeSize(map), XContainerCompare(map));
	XMap_move_base(XVariant_DataPtr(var), map);
	return var;
}

XVariant* XVariant_create_map_ref(XVariantMap* map)
{
	if (map == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_Map);
	if (var == NULL)
		return NULL;
	var->m_data = map;
	var->m_dataSize = sizeof(XVariantMap);
	return var;
}

XVariant* XVariant_create_hash(const XHashMap* hash)
{
	if (hash == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XHashMap), XVariantType_Hash);
	XHashMap_init(XVariant_DataPtr(var), ((XMapBase*)hash)->m_keyTypeSize, XContainerTypeSize(hash), hash->m_hash, XContainerCompare(hash));
	XHashMap_copy_base(XVariant_DataPtr(var), hash);
	return var;
}
XVariant* XVariant_create_hash_move(XHashMap* hash)
{
	if (hash == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XHashMap), XVariantType_Hash);
	XHashMap_init(XVariant_DataPtr(var), ((XMapBase*)hash)->m_keyTypeSize, XContainerTypeSize(hash), hash->m_hash, XContainerCompare(hash));
	XHashMap_move_base(XVariant_DataPtr(var), hash);
	return var;
}
XVariant* XVariant_create_hash_ref(XHashMap* hash)
{
	if (hash == NULL)
		return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_Hash);
	if (var == NULL)
		return NULL;
	var->m_data = hash;
	var->m_dataSize = sizeof(XHashMap);
	return var;
}
XVariant* XVariant_create_JsonDocument(const XJsonDocument* doc)
{
	return XJsonDocument_toVariant(doc);
}
XVariant* XVariant_create_JsonDocument_move(XJsonDocument* doc)
{
	return XJsonDocument_toVariant_move(doc);
}
XVariant* XVariant_create_JsonDocument_ref(XJsonDocument* doc)
{
	return XJsonDocument_toVariant_ref(doc);
}
XVariant* XVariant_create_JsonArray(const XJsonArray* arr)
{
	return XJsonArray_toVariant(arr);
}
XVariant* XVariant_create_JsonArray_move(XJsonArray* arr)
{
	return XJsonArray_toVariant_move(arr);
}
XVariant* XVariant_create_JsonArray_ref(XJsonArray* arr)
{
	return XJsonArray_toVariant_ref(arr);
}
XVariant* XVariant_create_JsonObject(const XJsonObject* obj)
{
	return XJsonObject_toVariant(obj);
}
XVariant* XVariant_create_JsonObject_move(XJsonObject* obj)
{
	return XJsonObject_toVariant_move(obj);
}
XVariant* XVariant_create_JsonObject_ref(XJsonObject* obj)
{
	return XJsonObject_toVariant_ref(obj);
}
XVariant* XVariant_create_JsonValue(const XJsonValue* val)
{
	return XJsonValue_toVariant(val);
}
XVariant* XVariant_create_JsonValue_move(XJsonValue* val)
{
	return XJsonValue_toVariant_move(val);
}
XVariant* XVariant_create_JsonValue_ref(XJsonValue* val)
{
	return XJsonValue_toVariant_ref(val);
}
XVariant* XVariant_create_BsonDocument(const XBsonDocument* doc)
{
	return XBsonDocument_toVariant(doc);
}
XVariant* XVariant_create_BsonDocument_move(XBsonDocument* doc)
{
	return XBsonDocument_toVariant_move(doc);
}
XVariant* XVariant_create_BsonDocument_ref(XBsonDocument* doc)
{
	return XBsonDocument_toVariant_ref(doc);
}
XVariant* XVariant_create_BsonArray(const XBsonArray* arr)
{
	return XBsonArray_toVariant(arr);
}
XVariant* XVariant_create_BsonArray_move(XBsonArray* arr)
{
	return XBsonArray_toVariant_move(arr);
}
XVariant* XVariant_create_BsonArray_ref(XBsonArray* arr)
{
	return XBsonArray_toVariant_ref(arr);
}
XVariant* XVariant_create_BsonValue(const XBsonValue* val)
{
	return XBsonValue_toVariant(val);
}
XVariant* XVariant_create_BsonValue_move(XBsonValue* val)
{
	return XBsonValue_toVariant_move(val);
}
XVariant* XVariant_create_BsonValue_ref(XBsonValue* val)
{
	return XBsonValue_toVariant_ref(val);
}
//转引用
void* XVariant_toRef(const XVariant* var, XVariantType type)
{
	if (var == NULL || var->m_type != type || var->m_dataSize == 0)
		return NULL;
	return XVariant_DataPtr(var);
}
uint8_t XVariant_toUint8(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toUShort(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint8_t);
}

uint8_t* XVariant_toUint8_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Uint8);
}

uint16_t XVariant_toUint16(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toUShort(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint16_t);
}

uint16_t* XVariant_toUint16_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Uint16);
}

uint32_t XVariant_toUint32(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toULong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint32_t);
}

uint32_t* XVariant_toUint32_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Uint32);
}

uint64_t XVariant_toUint64(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toULongLong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint64_t);
}

uint64_t* XVariant_toUint64_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Uint64);
}

int8_t XVariant_toInt8(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toShort(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int8_t);
}

int8_t* XVariant_toInt8_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int8);
}

int16_t XVariant_toInt16(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toShort(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int16_t);
}

int16_t* XVariant_toInt16_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int16);
}

int32_t XVariant_toInt32(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toLong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int32_t);
}

int32_t* XVariant_toInt32_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int32);
}

int64_t XVariant_toInt64(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toLongLong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int64_t);
}

int64_t* XVariant_toInt64_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int64);
}

bool XVariant_toBool(const XVariant* var)
{
	if (var == NULL)
		return 0;
	to_value(var, bool);
}

bool* XVariant_toBool_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Bool);
}

char XVariant_toChar(const XVariant* var)
{
	if (var == NULL)
		return 0;
	to_value(var, char);
}

char* XVariant_toChar_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Char);
}

unsigned char XVariant_toUChar(const XVariant* var)
{
	if (var == NULL)
		return 0;
	to_value(var, unsigned char);
}

unsigned char* XVariant_toUChar_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_UChar);
}

int XVariant_toInt(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toInt(XVariant_DataPtr(var),NULL,10);
	to_value(var, int);
}

int* XVariant_toInt_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int);
}

size_t XVariant_toSize_t(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toULongLong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, size_t);
}

size_t* XVariant_toSize_t_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Size_t);
}

void* XVariant_toPtr(const XVariant* var)
{
	return (void*)XVariant_toSize_t(var);
}

void** XVariant_toPtr_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Ptr);
}

float XVariant_toFloat(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toFloat(XVariant_DataPtr(var), NULL);
	switch (var->m_type) {
	case XVariantType_Uint8: return (float)(*((uint8_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint16: return (float)(*((uint16_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint32: return (float)(*((uint32_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint64: return (float)(*((uint64_t*)(((XVariant*)var)->m_data))); case XVariantType_Int8: return (float)(*((int8_t*)(((XVariant*)var)->m_data))); case XVariantType_Int16: return (float)(*((int16_t*)(((XVariant*)var)->m_data))); case XVariantType_Int32: return (float)(*((int32_t*)(((XVariant*)var)->m_data))); case XVariantType_Int64: return (float)(*((int64_t*)(((XVariant*)var)->m_data))); case XVariantType_Bool: return (float)(*((_Bool*)(((XVariant*)var)->m_data))); case XVariantType_Char: return (float)(*((char*)(((XVariant*)var)->m_data))); case XVariantType_UChar: return (float)(*((unsigned char*)(((XVariant*)var)->m_data))); case XVariantType_Int: return (float)(*((int*)(((XVariant*)var)->m_data))); case XVariantType_Size_t: return (float)(*((size_t*)(((XVariant*)var)->m_data))); case XVariantType_Float: return (float)(*((float*)(((XVariant*)var)->m_data))); case XVariantType_Double: return (float)(*((double*)(((XVariant*)var)->m_data))); default:return 0;
	};
}

float* XVariant_toFloat_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Float);
}

double XVariant_toDouble(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toDouble(XVariant_DataPtr(var), NULL);
	switch (var->m_type) {
	case XVariantType_Uint8: return (double)(*((uint8_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint16: return (double)(*((uint16_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint32: return (double)(*((uint32_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint64: return (double)(*((uint64_t*)(((XVariant*)var)->m_data))); case XVariantType_Int8: return (double)(*((int8_t*)(((XVariant*)var)->m_data))); case XVariantType_Int16: return (double)(*((int16_t*)(((XVariant*)var)->m_data))); case XVariantType_Int32: return (double)(*((int32_t*)(((XVariant*)var)->m_data))); case XVariantType_Int64: return (double)(*((int64_t*)(((XVariant*)var)->m_data))); case XVariantType_Bool: return (double)(*((_Bool*)(((XVariant*)var)->m_data))); case XVariantType_Char: return (double)(*((char*)(((XVariant*)var)->m_data))); case XVariantType_UChar: return (double)(*((unsigned char*)(((XVariant*)var)->m_data))); case XVariantType_Int: return (double)(*((int*)(((XVariant*)var)->m_data))); case XVariantType_Size_t: return (double)(*((size_t*)(((XVariant*)var)->m_data))); case XVariantType_Float: return (double)(*((float*)(((XVariant*)var)->m_data))); case XVariantType_Double: return (double)(*((double*)(((XVariant*)var)->m_data))); default:return 0;
	};
}

double* XVariant_toDouble_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Double);
}

XByteArray* XVariant_toByteArray(const XVariant* var)
{
	return XByteArray_create_copy(XVariant_toByteArray_ref(var));
}

XByteArray* XVariant_toByteArray_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_ByteArray);
}

XString* XVariant_toString(const XVariant* var)
{
	return XString_create_copy(XVariant_toString_ref(var));
}

XString* XVariant_toString_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_String);
}

XStringList* XVariant_toStringList(const XVariant* var)
{
	return XStringList_create_copy(XVariant_toStringList_ref(var));
}

XStringList* XVariant_toStringList_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_StringList);
}

XVariantList* XVariant_toList(const XVariant* var)
{
	return XVariantList_create_copy(XVariant_toList_ref(var));
}

XVariantList* XVariant_toList_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_List);
}

XVariantMap* XVariant_toMap(const XVariant* var)
{
	return XMap_create_copy(XVariant_toMap_ref(var));
}

XVariantMap* XVariant_toMap_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Map);
}

XVariantHashMap* XVariant_toHash(const XVariant* var)
{
	return XHashMap_create_copy(XVariant_toHash_ref(var));
}

XVariantHashMap* XVariant_toHash_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Hash);
}

XJsonDocument* XVariant_toJsonDocument(const XVariant* var)
{
	return XJsonDocument_create_copy(XVariant_toJsonDocument_ref(var));
}

XJsonDocument* XVariant_toJsonDocument_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_JsonDocument);
}

XJsonArray* XVariant_toJsonArray(const XVariant* var)
{
	return XJsonArray_create_copy(XVariant_toJsonArray_ref(var));
}

XJsonArray* XVariant_toJsonArray_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_JsonArray);
}

XJsonObject* XVariant_toJsonObject(const XVariant* var)
{
	return XJsonObject_create_copy(XVariant_toJsonObject_ref(var));
}

XJsonObject* XVariant_toJsonObject_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_JsonObject);
}

XJsonValue* XVariant_toJsonValue(const XVariant* var)
{
	return XJsonValue_create_copy(XVariant_toJsonValue_ref(var));
}

XJsonValue* XVariant_toJsonValue_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_JsonValue);
}

XBsonDocument* XVariant_toBsonDocument(const XVariant* var)
{
	return XBsonDocument_create_copy(XVariant_toBsonDocument_ref(var));
}

XBsonDocument* XVariant_toBsonDocument_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_BsonDocument);
}

XBsonArray* XVariant_toBsonArray(const XVariant* var)
{
	return XBsonArray_create_copy(XVariant_toBsonArray_ref(var));
}

XBsonArray* XVariant_toBsonArray_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_BsonArray);
}

XBsonValue* XVariant_toBsonValue(const XVariant* var)
{
	return XBsonValue_create_copy(XVariant_toBsonValue_ref(var));
}

XBsonValue* XVariant_toBsonValue_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_BsonValue);
}

XPair* XVariant_toPair(const XVariant* var)
{
	return XPair_create_copy(XVariant_toPair_ref(var));
}

XPair* XVariant_toPair_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Pair);
}

XPoint XVariant_toPoint(const XVariant* var)
{
	if (var->m_type != XVariantType_Point)
	{
		XPoint p = { 0 };
		return p;
	}
	return XVariant_Data(var, XPoint);
}

static void setValue(XVariant* var, void* data, size_t size, int type)
{
	if (var == NULL)
		return;
	if (var->m_data && var->m_type != type)
	{
		XVariant_deinit_base(var);
	}
	if (var->m_data == NULL || var->m_dataSize != size)
	{
		if(size>0&& var->m_data==NULL)
		{
			var->m_data = XMemory_malloc(size);
		}
		if (var->m_data == NULL&&size>0)
		{//失败
			var->m_dataSize = 0;
			return;
		}
		var->m_dataSize = size;
	}
	if(XVariant_DataPtr(var)&&data&&size)
		memcpy(XVariant_DataPtr(var), data, size);
	var->m_type = type;
}
void XVariant_setValue(XVariant* var,const XVariant* newVar)
{
	/*if (var == NULL || newVar == NULL||newVar->m_data==NULL||newVar->m_dataSize==0)
		return;*/
	return XVariant_copy_base(var, newVar);
		//setValue(var,NULL,);
}

void XVariant_setValue_null(XVariant* var)
{
	setValue(var, NULL, 0, XVariantType_NULL);
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

void XVariant_setValue_Pair(XVariant* var, const XPair* pair)
{
	setValue(var,pair,XPair_size(pair), XVariantType_Pair);
}

void XVariant_setValue_Point(XVariant* var, XPoint val)
{
	setValue(var,&val,sizeof(XPoint), XVariantType_Point);
}

static void setValue_ByteArray(XVariant* var, XByteArray* array, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || array == NULL)
		return;
	if (var->m_type != XVariantType_ByteArray)
	{
		setValue(var, NULL, sizeof(XByteArray), XVariantType_ByteArray);
		XByteArray_init(XVariant_DataPtr(var));
	}
	dataCreatMethod(XVariant_DataPtr(var), array);
}

void XVariant_setValue_ByteArray(XVariant* var, const XByteArray* array)
{
	setValue_ByteArray(var, array, XByteArray_copy_base);
}
void XVariant_setValue_ByteArray_move(XVariant* var,XByteArray* array)
{
	setValue_ByteArray(var,array, XByteArray_move_base);
}

void XVariant_setValue_ByteArray_ref(XVariant* var, XByteArray* array)
{
	if (var == NULL || array == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = array;
	var->m_dataSize = sizeof(XByteArray);
	var->m_type = XVariantType_ByteArray;
}

void XVariant_setValue_byteArray(XVariant* var, const void* data, size_t size)
{
	if (var == NULL || data == NULL||size==0)
		return;
	if (var->m_type != XVariantType_ByteArray)
	{
		setValue(var, NULL, sizeof(XByteArray), XVariantType_ByteArray);
		XByteArray_init(XVariant_DataPtr(var));
	}
	XByteArray_resize_base(XVariant_DataPtr(var), size);
	memcpy(XContainerDataPtr(XVariant_DataPtr(var)), data, size);
}

static void setValue_String(XVariant* var, const XString* str, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || str == NULL)
		return;
	if (var->m_type != XVariantType_String)
	{
		setValue(var, NULL, sizeof(XString), XVariantType_String);
		XString_init(XVariant_DataPtr(var));
	}
	dataCreatMethod(XVariant_DataPtr(var), str);
}

void XVariant_setValue_String(XVariant* var, const XString* str)
{
	setValue_String(var,str,XString_copy_base);
}

void XVariant_setValue_String_move(XVariant* var, XString* str)
{
	setValue_String(var, str, XString_move_base);
}

void XVariant_setValue_String_ref(XVariant* var, XString* str)
{
	if (var == NULL || str == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = str;
	var->m_dataSize = sizeof(XString);
	var->m_type = XVariantType_String;
}

void XVariant_setValue_utf8_str(XVariant* var, const char* utf8)
{
	if (var == NULL || utf8 == NULL)
		return;
	if (var->m_type != XVariantType_String)
	{
		setValue(var, NULL, sizeof(XString), XVariantType_String);
		XString_init(XVariant_DataPtr(var));
	}
	XString_assign_utf8(XVariant_DataPtr(var), utf8);
}

static void setValue_StringList(XVariant* var, const XStringList* list, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || list == NULL)
		return;
	if (var->m_type != XVariantType_StringList)
	{
		setValue(var, NULL, sizeof(XStringList), XVariantType_StringList);
		XStringList_init(XVariant_DataPtr(var));
	}
	dataCreatMethod(XVariant_DataPtr(var), list);
}

void XVariant_setValue_StringList(XVariant* var, const XStringList* list)
{
	setValue_StringList(var,list, XStringList_copy_base);
}

void XVariant_setValue_StringList_move(XVariant* var, XStringList* list)
{
	setValue_StringList(var, list, XStringList_move_base);
}

void XVariant_setValue_StringList_ref(XVariant* var, XStringList* list)
{
	if (var == NULL || list == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = list;
	var->m_dataSize = sizeof(XStringList);
	var->m_type = XVariantType_StringList;
}

static void setValue_list(XVariant* var, const XVariantList* list, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || list == NULL)
		return;
	if (var->m_type != XVariantType_List)
	{
		setValue(var, NULL, sizeof(XVariantList), XVariantType_List);
		XVariantList_init(XVariant_DataPtr(var));
	}
	dataCreatMethod(XVariant_DataPtr(var), list);
}

void XVariant_setValue_list(XVariant* var, const XVariantList* list)
{
	setValue_list(var,list, XVariantList_copy_base);
}

void XVariant_setValue_list_move(XVariant* var, XVariantList* list)
{
	setValue_list(var, list, XVariantList_move_base);
}

void XVariant_setValue_list_ref(XVariant* var, XVariantList* list)
{
	if (var == NULL || list == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = list;
	var->m_dataSize = sizeof(XVariantList);
	var->m_type = XVariantType_List;
}

static void setValue_map(XVariant* var, const XVariantMap* map, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || map == NULL)
		return;
	if (var->m_type != XVariantType_Map)
	{
		setValue(var, NULL, sizeof(XVariantMap), XVariantType_Map);
		XMap_init(XVariant_DataPtr(var), ((XMapBase*)map)->m_keyTypeSize, XContainerTypeSize(map), XContainerCompare(map));
	}
	dataCreatMethod(XVariant_DataPtr(var), map);
}

void XVariant_setValue_map(XVariant* var, const XVariantMap* map)
{
	setValue_map(var,map, XMap_copy_base);
}

void XVariant_setValue_map_move(XVariant* var, XVariantMap* map)
{
	setValue_map(var, map, XMap_move_base);
}

void XVariant_setValue_map_ref(XVariant* var, XVariantMap* map)
{
	if (var == NULL || map == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = map;
	var->m_dataSize = sizeof(XVariantMap);
	var->m_type = XVariantType_Map;
}

static void setValue_hash(XVariant* var, const XVariantHashMap* hash, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || hash == NULL)
		return;
	if (var->m_type != XVariantType_Hash)
	{
		setValue(var, NULL, sizeof(XVariantHashMap), XVariantType_Hash);
		XHashMap_init(XVariant_DataPtr(var), ((XMapBase*)hash)->m_keyTypeSize, XContainerTypeSize(hash), hash->m_hash, XContainerCompare(hash));
	}
	dataCreatMethod(XVariant_DataPtr(var), hash);
}

void XVariant_setValue_hash(XVariant* var, const XVariantHashMap* hash)
{
	setValue_hash(var,hash, XHashMap_copy_base);
}

void XVariant_setValue_hash_move(XVariant* var, XVariantHashMap* hash)
{
	setValue_hash(var, hash, XHashMap_move_base);
}

void XVariant_setValue_hash_ref(XVariant* var, XVariantHashMap* hash)
{
	if (var == NULL || hash == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = hash;
	var->m_dataSize = sizeof(XVariantHashMap);
	var->m_type = XVariantType_Hash;
}

static void setValue_JsonDocument(XVariant* var, const XJsonDocument* doc, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || doc == NULL)
		return;
	if (var->m_type != XVariantType_JsonDocument)
	{
		setValue(var, NULL, sizeof(XJsonDocument), XVariantType_JsonDocument);
		XJsonDocument_init(XVariant_DataPtr(var));
	}
	dataCreatMethod(XVariant_DataPtr(var), doc);
}

void XVariant_setValue_JsonDocument(XVariant* var, const XJsonDocument* doc)
{
	setValue_JsonDocument(var, doc, XJsonDocument_copy);
}

void XVariant_setValue_JsonDocument_move(XVariant* var, XJsonDocument* doc)
{
	setValue_JsonDocument(var, doc, XJsonDocument_move);
}

void XVariant_setValue_JsonDocument_ref(XVariant* var, XJsonDocument* doc)
{
	if (var == NULL || doc == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = doc;
	var->m_dataSize = sizeof(XJsonDocument);
	var->m_type = XVariantType_JsonDocument;
}

static void setValue_JsonArray(XVariant* var, const XJsonArray* arr, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || arr == NULL)
		return;
	if (var->m_type != XVariantType_JsonArray)
	{
		setValue(var, NULL, sizeof(XJsonArray), XVariantType_JsonArray);
		XJsonArray_init(XVariant_DataPtr(var));
	}
	dataCreatMethod(XVariant_DataPtr(var), arr);
}

void XVariant_setValue_JsonArray(XVariant* var, const XJsonArray* arr)
{
	setValue_JsonArray(var,arr, XJsonArray_copy_base);
}

void XVariant_setValue_JsonArray_move(XVariant* var, XJsonArray* arr)
{
	setValue_JsonArray(var, arr, XJsonArray_move_base);
}

void XVariant_setValue_JsonArray_ref(XVariant* var, XJsonArray* arr)
{
	if (var == NULL || arr == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = arr;
	var->m_dataSize = sizeof(XJsonArray);
	var->m_type = XVariantType_JsonArray;
}

static void setValue_JsonObject(XVariant* var, const XJsonObject* obj, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || obj == NULL)
		return;
	if (var->m_type != XVariantType_JsonObject)
	{
		setValue(var, NULL, sizeof(XJsonObject), XVariantType_JsonObject);
		XJsonObject_init(XVariant_DataPtr(var));
	}
	dataCreatMethod(XVariant_DataPtr(var), obj);
}

void XVariant_setValue_JsonObject(XVariant* var, const XJsonObject* obj)
{
	setValue_JsonObject(var,obj, XJsonObject_copy_base);
}

void XVariant_setValue_JsonObject_move(XVariant* var, XJsonObject* obj)
{
	setValue_JsonObject(var, obj, XJsonObject_move_base);
}

void XVariant_setValue_JsonObject_ref(XVariant* var, XJsonObject* obj)
{
	if (var == NULL || obj == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = obj;
	var->m_dataSize = sizeof(XJsonObject);
	var->m_type = XVariantType_JsonObject;
}

static void setValue_JsonValue(XVariant* var, const XJsonValue* val, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || val == NULL)
		return;
	if (var->m_type != XVariantType_JsonValue)
	{
		setValue(var, NULL, sizeof(XJsonValue), XVariantType_JsonValue);
		XJsonValue_init(XVariant_DataPtr(var), val->type);
	}
	dataCreatMethod(XVariant_DataPtr(var), val);
}

void XVariant_setValue_JsonValue(XVariant* var, const XJsonValue* val)
{
	setValue_JsonValue(var,val, XJsonValue_copy);
}

void XVariant_setValue_JsonValue_move(XVariant* var, XJsonValue* val)
{
	setValue_JsonValue(var, val, XJsonValue_move);
}

void XVariant_setValue_JsonValue_ref(XVariant* var, XJsonValue* val)
{
	if (var == NULL || val == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = val;
	var->m_dataSize = sizeof(XJsonValue);
	var->m_type = XVariantType_JsonValue;
}

static void setValue_BsonDocument(XVariant* var, const XBsonDocument* doc, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || doc == NULL)
		return;
	if (var->m_type != XVariantType_BsonDocument)
	{
		setValue(var, NULL, sizeof(XBsonDocument), XVariantType_BsonDocument);
		XBsonDocument_init(XVariant_DataPtr(var));
	}
	dataCreatMethod(XVariant_DataPtr(var), doc);
}

void XVariant_setValue_BsonDocument(XVariant* var, const XBsonDocument* doc)
{
	setValue_BsonDocument(var,doc, XBsonDocument_copy_base);
}

void XVariant_setValue_BsonDocument_move(XVariant* var, XBsonDocument* doc)
{
	setValue_BsonDocument(var, doc, XBsonDocument_move_base);
}

void XVariant_setValue_BsonDocument_ref(XVariant* var, XBsonDocument* doc)
{
	if (var == NULL || doc == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = doc;
	var->m_dataSize = sizeof(XBsonDocument);
	var->m_type = XVariantType_BsonDocument;
}

static void setValue_BsonArray(XVariant* var, const XBsonArray* arr, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || arr == NULL)
		return;
	if (var->m_type != XVariantType_BsonArray)
	{
		setValue(var, NULL, sizeof(XBsonArray), XVariantType_BsonArray);
		XBsonArray_init(XVariant_DataPtr(var));
	}
	dataCreatMethod(XVariant_DataPtr(var), arr);
}

void XVariant_setValue_BsonArray(XVariant* var, const XBsonArray* arr)
{
	setValue_BsonArray(var,arr, XBsonArray_copy_base);
}

void XVariant_setValue_BsonArray_move(XVariant* var, XBsonArray* arr)
{
	setValue_BsonArray(var, arr, XBsonArray_move_base);
}

void XVariant_setValue_BsonArray_ref(XVariant* var, XBsonArray* arr)
{
	if (var == NULL || arr == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = arr;
	var->m_dataSize = sizeof(XBsonArray);
	var->m_type = XVariantType_BsonArray;
}

static void setValue_BsonValue(XVariant* var, const XBsonValue* val, XCDataCreatMethod dataCreatMethod)
{
	if (var == NULL || val == NULL)
		return;
	if (var->m_type != XVariantType_BsonValue)
	{
		setValue(var, NULL, sizeof(XBsonValue), XVariantType_BsonValue);
		XBsonValue_init(XVariant_DataPtr(var), val->type);
	}
	dataCreatMethod(XVariant_DataPtr(var), val);
}

void XVariant_setValue_BsonValue(XVariant* var, const XBsonValue* val)
{
	setValue_BsonValue(var,val, XBsonValue_copy);
}

void XVariant_setValue_BsonValue_move(XVariant* var, XBsonValue* val)
{
	setValue_BsonValue(var, val, XBsonValue_move);
}

void XVariant_setValue_BsonValue_ref(XVariant* var, XBsonValue* val)
{
	if (var == NULL || val == NULL)
		return;
	XVariant_deinit_base(var);
	var->m_data = val;
	var->m_dataSize = sizeof(XBsonValue);
	var->m_type = XVariantType_BsonValue;
}

void VXVariant_copy(XVariant* var, const XVariant* src)
{
	if (var == NULL || src == NULL)
		return;
	if (var->m_type != src->m_type)
		XVariant_deinit_base(var);//
	if (XVariant_DataPtr(var) == NULL)
	{
		var->m_data = XMemory_calloc(1,src->m_dataSize);
		var->m_dataSize = src->m_dataSize;
		var->m_type = src->m_type;
	}
	switch (src->m_type)
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
		case XVariantType_Point:memcpy(XVariant_DataPtr(var), XVariant_DataPtr(src), src->m_dataSize); break;
		case XVariantType_ByteArray:
		case XVariantType_String:
		case XVariantType_List:
		case XVariantType_Map:
		case XVariantType_JsonObject:
		case XVariantType_JsonArray:
		case XVariantType_BsonArray:
		case XVariantType_BsonDocument:XContainerObject_copy_base(XVariant_DataPtr(var), XVariant_DataPtr(src)); break;
		case XVariantType_JsonDocument:XJsonDocument_copy(XVariant_DataPtr(var), XVariant_DataPtr(src)); break;
		case XVariantType_JsonValue:XJsonValue_copy(XVariant_DataPtr(var), XVariant_DataPtr(src)); break;
		case XVariantType_BsonValue:XBsonValue_copy(XVariant_DataPtr(var), XVariant_DataPtr(src)); break;
		default:
		{
			//其他自定义数据
			if (global_typeProperty != NULL)
			{
				//查找相等比较函数
				TypeProperty* pv = XHashMap_value_base(global_typeProperty, &(var->m_type));
				if (pv && pv->copyMethod)
				{
					pv->copyMethod(XVariant_DataPtr(var), XVariant_DataPtr(src));
				}
				else
				{
					memcpy(XVariant_DataPtr(var), XVariant_DataPtr(src), src->m_dataSize);
				}
			}
			else
			{
				memcpy(XVariant_DataPtr(var), XVariant_DataPtr(src), src->m_dataSize);
			}
		}
	}
}

void VXVariant_move(XVariant* var, XVariant* src)
{
	if (var == NULL || src == NULL)
		return;
	if (var->m_data!=NULL)
		XVariant_deinit_base(var);//
	if (var->m_class.m_vtable == NULL)
		var->m_class = src->m_class;
	if (XVariant_DataPtr(var) == NULL)
	{
		var->m_data = src->m_data;
		src->m_data = NULL;
		var->m_dataSize = src->m_dataSize;
		src->m_dataSize = 0;
		var->m_type = src->m_type;
		src->m_type = XVariantType_NULL;
	}
	
}

void VXVariant_deinit(XVariant* var)
{
	if (var == NULL|| XVariant_DataPtr(var)==NULL)
		return;
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
		case XVariantType_Point:break;
		case XVariantType_ByteArray:
		case XVariantType_String:
		case XVariantType_List:
		case XVariantType_Map:
		case XVariantType_JsonObject:
		case XVariantType_JsonArray:
		case XVariantType_BsonArray:
		case XVariantType_BsonDocument:XContainerObject_deinit_base(XVariant_DataPtr(var)); break;
		case XVariantType_JsonDocument:XJsonDocument_deinit(XVariant_DataPtr(var)); break;
		case XVariantType_JsonValue:XJsonValue_deinit(XVariant_DataPtr(var)); break;
		case XVariantType_BsonValue:XBsonValue_deinit(XVariant_DataPtr(var)); break;
		default:
		{
			//其他自定义数据
			if (global_typeProperty != NULL)
			{
				//查找相等比较函数
				TypeProperty* pv = XHashMap_value_base(global_typeProperty, &(var->m_type));
				if (pv && pv->deinitMethod)
				{
					pv->deinitMethod(XVariant_DataPtr(var));
				}
			}
		}
	}
	XMemory_free(XVariant_DataPtr(var));
	XVariant_DataPtr(var) = NULL;
}

void XVariant_clear(XVariant* var)
{
	if (var == NULL || XVariant_DataPtr(var) == NULL || var->m_dataSize == 0)
		return;
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
		case XVariantType_Point:memset(XVariant_DataPtr(var), 0, var->m_dataSize);break;
		case XVariantType_ByteArray:
		case XVariantType_String:
		case XVariantType_List:
		case XVariantType_Map:
		case XVariantType_JsonObject:
		case XVariantType_JsonArray:
		case XVariantType_BsonArray:
		case XVariantType_BsonDocument:XContainerObject_clear_base(XVariant_DataPtr(var)); break;
		case XVariantType_JsonDocument:XJsonDocument_clear(XVariant_DataPtr(var)); break;
		case XVariantType_JsonValue:XJsonValue_clear(XVariant_DataPtr(var)); break;
		case XVariantType_BsonValue:XBsonValue_clear(XVariant_DataPtr(var)); break;
		default:
		{
			//其他自定义数据
			if (global_typeProperty != NULL)
			{
				//查找相等比较函数
				TypeProperty* pv = XHashMap_value_base(global_typeProperty, &(var->m_type));
				if (pv && pv->clearMethod)
				{
					pv->clearMethod(XVariant_DataPtr(var));
				}
				else
				{
					memset(XVariant_DataPtr(var), 0, var->m_dataSize);
				}
			}
			else
			{
				memset(XVariant_DataPtr(var), 0, var->m_dataSize);
			}
		}
	}
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
	case XVariantType_StringList:return	"XStringList";
	case XVariantType_List:return		"XVariantList";
	case XVariantType_Map:return		"XMap<XString,XVariant>";
	case XVariantType_Hash:return		"XHashMap<XString,XVariant>";
	case XVariantType_JsonDocument:return	"XJsonDocument";
	case XVariantType_JsonArray:return		"XJsonArray";
	case XVariantType_JsonObject:return		"XJsonObject";
	case XVariantType_JsonValue:return		"XJsonValue";
	case XVariantType_BsonArray:return		"XBsonArray";
	case XVariantType_BsonDocument:return	"XBsonDocument";
	case XVariantType_BsonValue:return		"XBsonValue";
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

int32_t XVariant_compare(XVariant* var, XVariant* cmp)
{
	if (var == NULL || cmp == NULL ||
		var->m_type != cmp->m_type ||
		var->m_dataSize != cmp->m_dataSize)
		return false;
	switch (var->m_type)
	{
	case XVariantType_Uint8:return uint8_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Uint16:return uint16_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Uint32:return uint32_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Uint64:return uint64_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Int8:return int8_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Int16:return int16_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Int32:return int32_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Int64:return int64_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Bool:return bool_compare(var->m_data, cmp->m_data);
	case XVariantType_Char:return char_compare(var->m_data, cmp->m_data);
	case XVariantType_UChar:return unsigned_char_compare(var->m_data, cmp->m_data);
	case XVariantType_Int:return int_compare(var->m_data, cmp->m_data);
	case XVariantType_Size_t:return size_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Ptr:return ptr_compare(var->m_data, cmp->m_data);
	case XVariantType_Float:return float_compare(var->m_data, cmp->m_data);
	case XVariantType_Double:return double_compare(var->m_data, cmp->m_data);
	case XVariantType_Pair:return XPair_compare(var->m_data, cmp->m_data);
	case XVariantType_Point:return XPoint_compare(var->m_data, cmp->m_data);
	case XVariantType_ByteArray:return XByteArray_compare(var->m_data, cmp->m_data);
	case XVariantType_String:return XString_compare(var->m_data, cmp->m_data);
	case XVariantType_List:
	{
		return false;//暂未实现
	}
	case XVariantType_Map:
	{
		return false;//暂未实现
	}
	default:
	{
		//其他自定义数据
		if (global_typeProperty == NULL)
			return false;
		//查找相等比较函数
		TypeProperty* pv = XHashMap_value_base(global_typeProperty, &(var->m_type));
		if (pv && pv->compare)
			return (pv->compare)(var->m_data, cmp->m_data);
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

void XVariant_setUserCompare(int type, XCompare compare)
{
	if (type < XVariantType_User || compare == NULL)
		return;
	if (!global_typeHash_init())
		return;
	TypeProperty* pv = XMapBase_value_base(global_typeProperty,&type);
	if (pv == NULL)
	{
		TypeProperty property = { 0 };
		property.compare = compare;
		XHashMap_insert_base(global_typeProperty, &type, &property);
	}
	else
	{
		pv->compare = compare;
	}

}

void XVariant_setUserDataMethod(int type, XCDataCopyMethod copyMethod, XCDataMoveMethod moveMethod, XCDataClearMethod clearMethod, XCDataDeinitMethod deinitMethod)
{
	if (type < XVariantType_User)
		return;
	if (!global_typeHash_init())
		return;
	TypeProperty* pv = XMapBase_value_base(global_typeProperty, &type);
	if (pv == NULL)
	{
		TypeProperty property = { 0 };
		property.copyMethod = copyMethod;
		property.moveMethod = moveMethod;
		property.clearMethod = clearMethod;
		property.deinitMethod = deinitMethod;
		XHashMap_insert_base(global_typeProperty, &type, &property);
	}
	else
	{
		pv->copyMethod = copyMethod;
		pv->moveMethod = moveMethod;
		pv->clearMethod = clearMethod;
		pv->deinitMethod = deinitMethod;
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
