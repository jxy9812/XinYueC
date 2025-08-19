#ifndef XBSONDOCUMENT_H
#define XBSONDOCUMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XBson.h"
#include "XMap.h"
#include "XBsonValue.h"

typedef struct XBsonDocument
{
    XMap members; // 键为 XString, 值为 XBsonValue
} XBsonDocument;

// 构造与析构
XBsonDocument* XBsonDocument_create();
XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other);
XBsonDocument* XBsonDocument_create_move(XBsonDocument* other);
void XBsonDocument_init(XBsonDocument* doc);
bool XBsonDocument_insert_keyUtf8_value(XBsonDocument* doc, const char* key, XBsonValue* value);
bool XBsonDocument_insert_keyUtf8_value_move(XBsonDocument* doc, const char* key, XBsonValue* value);
bool XBsonDocument_insert_keyUtf8_double(XBsonDocument* doc, const char* key, double d);
bool XBsonDocument_insert_keyUtf8_int32(XBsonDocument* doc, const char* key, int32_t i);
bool XBsonDocument_insert_keyUtf8_int64(XBsonDocument* doc, const char* key, int64_t i);
bool XBsonDocument_insert_keyUtf8_string(XBsonDocument* doc, const char* key, const XString* str);
bool XBsonDocument_insert_keyUtf8_string_move(XBsonDocument* doc, const char* key, XString* str);
bool XBsonDocument_insert_keyUtf8_utf8(XBsonDocument* doc, const char* key, const char* utf8);
bool XBsonDocument_insert_keyUtf8_null(XBsonDocument* doc, const char* key);
bool XBsonDocument_insert_keyUtf8_bool(XBsonDocument* doc, const char* key, bool b);
bool XBsonDocument_insert_keyUtf8_array(XBsonDocument* doc, const char* key, const XBsonArray* array);
bool XBsonDocument_insert_keyUtf8_array_move(XBsonDocument* doc, const char* key, XBsonArray* array);
bool XBsonDocument_insert_keyUtf8_document(XBsonDocument* doc, const char* key, const XBsonDocument* newDoc);
bool XBsonDocument_insert_keyUtf8_document_move(XBsonDocument* doc, const char* key, XBsonDocument* newDoc);
bool XBsonDocument_insert_value_move(XBsonDocument* doc, const XString* key, XBsonValue* value);
//删除
bool XBsonDocument_remove_keyUtf8(XBsonDocument* doc, const char* key);
//key XString*  val XBsonValue*
#define XBsonDocument_insert_base				    XMap_insert_base
//key XString*  val XBsonValue*
#define XBsonDocument_insert_move_base		        XMap_insert_move_base
#define XBsonDocument_Insert_Base				    XMap_Insert_Base
#define XBsonDocument_erase_base					XMap_erase_base
#define XBsonDocument_remove_base				    XMap_remove_base
#define XBsonDocument_Remove_Base				    XMap_Remove_Base
#define XBsonDocument_value_base					XMap_value_base
#define XBsonDocument_Value_Base					XMap_Value_Base
#define XBsonDocument_find_base					    XMap_find_base
#define XBsonDocument_contains					    XMap_contains
#define XBsonDocument_keys_base					    XMap_keys_base
#define XBsonDocument_copy_base					    XMap_copy_base	
#define XBsonDocument_move_base					    XMap_move_base	
#define XBsonDocument_deinit_base				    XMap_deinit_base	
#define XBsonDocument_delete_base				    XMap_delete_base	
#define XBsonDocument_clear_base					XMap_clear_base	
#define XBsonDocument_isEmpty_base				    XMap_isEmpty_base	
#define XBsonDocument_size_base					    XMap_size_base	
#define XBsonDocument_capacity_base				    XMap_capacity_base
#define XBsonDocument_swap_base					    XMap_swap_base	
#define XBsonDocument_typeSize_base				    XMap_typeSize_base

// 转换函数
XJsonObject* XBsonDocument_toJsonObject(const XBsonDocument* bson_obj);
XBsonDocument* XBsonDocument_fromJsonObject(const XJsonObject* json_obj);
// 将XBsonDocument转换为JSON（UTF-8）
XByteArray* XBsonDocument_toJson(const XBsonDocument* bson_doc, XJsonDocumentFormat format);
XString* XBsonDocument_toJson_string(const XBsonDocument* bson_doc, XJsonDocumentFormat format);

// 序列化与反序列化
XByteArray* XBsonDocument_toBson(const XBsonDocument* doc);
XBsonDocument* XBsonDocument_fromBson(XByteArray* data);


bool XBsonDocument_from_bytes(XBsonDocument* doc, const uint8_t* data, size_t size);
#ifdef __cplusplus
}
#endif

#endif // XBSONOBJECT_H