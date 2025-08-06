#ifndef XJSONOBJECT_H
#define XJSONOBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XDataStructConfig.h"
#include "XJsonValue.h"
#include "XMap.h"

#if !XMap_ON
#error "XJsonObject requires XMap to be enabled in XDataStructConfig.h"
#endif

typedef struct XJsonObject 
{
    XMap members; // 键为XString，值为XJsonValue
} XJsonObject;

// 构造与析构
XJsonObject* XJsonObject_create(void);

#define XJsonObject_insert_base				    XMap_insert_base
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
XString* XJsonObject_toString(const XJsonObject* object);
XVariantMap* XJsonObject_toVariantMap(const XJsonObject* object);
XJsonObject* XJsonObject_fromVariantMap(const XVariantMap* map);

#ifdef __cplusplus
}
#endif

#endif // XJSONOBJECT_H