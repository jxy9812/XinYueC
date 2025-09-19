#ifndef XJSONOBJECT_H
#define XJSONOBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XJson.h"
#include "XJsonValue.h"
#include "XMap.h"

#if !XMap_ON
#error "XJsonObject requires XMap to be enabled in CXinYueConfig.h"
#endif

typedef struct XJsonObject 
{
    XMap members; // 键为XString，值为XJsonValue
} XJsonObject;

// 构造与析构
XJsonObject* XJsonObject_create(void);
XJsonObject* XJsonObject_create_copy(XJsonObject* copy);
XJsonObject* XJsonObject_create_move(XJsonObject* move);
void XJsonObject_init(XJsonObject* object);
//插入
bool XJsonObject_insert_keyUtf8_value(XJsonObject* object, const char* key, XJsonValue* value);
bool XJsonObject_insert_keyUtf8_value_move(XJsonObject* object, const char* key, XJsonValue* value);
bool XJsonObject_insert_keyUtf8_double(XJsonObject* object, const char* key, double d);
bool XJsonObject_insert_keyUtf8_int(XJsonObject* object, const char* key, int64_t i);
bool XJsonObject_insert_keyUtf8_string(XJsonObject* object, const char* key,const XString* str);
bool XJsonObject_insert_keyUtf8_string_move(XJsonObject* object, const char* key, XString* str);
bool XJsonObject_insert_keyUtf8_utf8(XJsonObject* object, const char* key,const char*utf8);
bool XJsonObject_insert_keyUtf8_null(XJsonObject* object, const char* key);
bool XJsonObject_insert_keyUtf8_bool(XJsonObject* object, const char* key,bool b);
bool XJsonObject_insert_keyUtf8_array(XJsonObject* object, const char* key,const XJsonArray* array);
bool XJsonObject_insert_keyUtf8_array_move(XJsonObject* object, const char* key, XJsonArray* array);
bool XJsonObject_insert_keyUtf8_object(XJsonObject* object, const char* key, const XJsonObject* value);
bool XJsonObject_insert_keyUtf8_object_move(XJsonObject* object, const char* key, XJsonObject* value);

bool XJsonObject_insert_value_move(XJsonObject* object, const XString* key, XJsonValue* value);
//删除
bool XJsonObject_remove_keyUtf8(XJsonObject* object, const char* key);
//key XString*  val XJsonValue*
#define XJsonObject_insert_base				    XMap_insert_base
//key XString*  val XJsonValue*
#define XJsonObject_insert_move_base		    XMap_insert_move_base
#define XJsonObject_Insert_Base				    XMap_Insert_Base
#define XJsonObject_erase_base					XMap_erase_base
#define XJsonObject_remove_base				    XMap_remove_base
#define XJsonObject_Remove_Base				    XMap_Remove_Base
#define XJsonObject_value_base					XMap_value_base
#define XJsonObject_Value_Base					XMap_Value_Base
#define XJsonObject_find_base					XMap_find_base
#define XJsonObject_contains					XMap_contains
#define XJsonObject_keys_base					XMap_keys_base
#define XJsonObject_copy_base					XMap_copy_base	
#define XJsonObject_move_base					XMap_move_base	
#define XJsonObject_deinit_base				    XMap_deinit_base	
#define XJsonObject_delete_base				    XMap_delete_base	
#define XJsonObject_clear_base					XMap_clear_base	
#define XJsonObject_isEmpty_base				XMap_isEmpty_base	
#define XJsonObject_size_base					XMap_size_base	
#define XJsonObject_capacity_base				XMap_capacity_base
#define XJsonObject_swap_base					XMap_swap_base	
#define XJsonObject_typeSize_base				XMap_typeSize_base

// 转换函数
XString* XJsonObject_toString(const XJsonObject* object, XJsonDocumentFormat format);
//内部utf8编码适合传输
XByteArray* XJsonObject_toJson(const XJsonObject* object, XJsonDocumentFormat format);

XVariantMap* XJsonObject_toVariantMap(const XJsonObject* object);
XVariantMap* XJsonObject_toVariantMap_move(XJsonObject* object);
// 与XVariant转换
XVariant* XJsonObject_toVariant(const XJsonObject* object);
XVariant* XJsonObject_toVariant_move(XJsonObject* object);
XVariant* XJsonObject_toVariant_ref(XJsonObject* object);
#ifdef __cplusplus
}
#endif

#endif // XJSONOBJECT_H