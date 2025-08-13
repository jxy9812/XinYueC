#ifndef XBSONOBJECT_H
#define XBSONOBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XBson.h"
#include "XMap.h"
#include "XBsonValue.h"

typedef struct XBsonObject
{
    XMap members; // 键为 XString*, 值为 XBsonValue*
} XBsonObject;

// 构造与析构
XBsonObject* XBsonObject_create();
XBsonObject* XBsonObject_create_copy(const XBsonObject* other);
XBsonObject* XBsonObject_create_move(XBsonObject* other);
void XBsonObject_init(XBsonObject* object);
//key XString*  val XBsonValue*
#define XBsonObject_insert_base				    XMap_insert_base
//key XString*  val XBsonValue*
#define XBsonObject_insert_move_base		    XMap_insert_move_base
#define XBsonObject_Insert_Base				    XMap_Insert_Base
#define XBsonObject_erase_base					XMap_erase_base
#define XBsonObject_remove_base				    XMap_remove_base
#define XBsonObject_Remove_Base				    XMap_Remove_Base
#define XBsonObject_value_base					XMap_value_base
#define XBsonObject_Value_Base					XMap_Value_Base
#define XBsonObject_find_base					XMap_find_base
#define XBsonObject_contains					XMap_contains
#define XBsonObject_keys_base					XMap_keys_base
#define XBsonObject_copy_base					XMap_copy_base	
#define XBsonObject_move_base					XMap_move_base	
#define XBsonObject_deinit_base				    XMap_deinit_base	
#define XBsonObject_delete_base				    XMap_delete_base	
#define XBsonObject_clear_base					XMap_clear_base	
#define XBsonObject_isEmpty_base				XMap_isEmpty_base	
#define XBsonObject_size_base					XMap_size_base	
#define XBsonObject_capacity_base				XMap_capacity_base
#define XBsonObject_swap_base					XMap_swap_base	
#define XBsonObject_typeSize_base				XMap_typeSize_base

// 转换函数
XJsonObject* XBsonObject_to_json_object(const XBsonObject* bson_obj);
void XBsonObject_from_json_object(XBsonObject* bson_obj, const XJsonObject* json_obj);

// 序列化与反序列化
XByteArray* XBsonObject_to_bytes(const XBsonObject* object);
bool XBsonObject_from_bytes(XBsonObject* object, const uint8_t* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // XBSONOBJECT_H