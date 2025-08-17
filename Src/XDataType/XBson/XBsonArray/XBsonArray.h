#ifndef XBSONARRAY_H
#define XBSONARRAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XBson.h"
#include "XVector.h"
#include "XBsonValue.h"

typedef struct XBsonArray 
{
    XVector elements; // 存储 XBsonValue
} XBsonArray;

// 构造与析构
XBsonArray* XBsonArray_create();
XBsonArray* XBsonArray_create_copy(const XBsonArray* other);
XBsonArray* XBsonArray_create_move(XBsonArray* other);
void XBsonArray_init(XBsonArray* array);
#define XBsonArray_at_base							XVector_at_base
#define XBsonArray_rcopy_base						XVector_rcopy_base
#define XBsonArray_copy_base						XVector_copy_base	
#define XBsonArray_move_base						XVector_move_base	
#define XBsonArray_deinit_base						XVector_deinit_base	
#define XBsonArray_delete_base						XVector_delete_base	
#define XBsonArray_clear_base						XVector_clear_base	
#define XBsonArray_isEmpty_base						XVector_isEmpty_base	
#define XBsonArray_size_base						XVector_size_base	
#define XBsonArray_capacity_base					XVector_capacity_base
#define XBsonArray_swap_base						XVector_swap_base	
#define XBsonArray_typeSize_base					XVector_typeSize_base
#define XBsonArray_count_base						XVector_count_base			
#define XBsonArray_append_base						XVector_append_base			
#define XBsonArray_append_move_base					XVector_append_move_base
#define XBsonArray_prepend_base						XVector_prepend_base	
#define XBsonArray_prepend_move_base				XVector_prepend_move_base
#define XBsonArray_insert                           XVector_insert
#define XBsonArray_insert_move                      XVector_insert_move
#define XBsonArray_removeAt_base                    XVector_removeAt_base
#define XBsonArray_replace                          XVector_replace
#define XBsonArray_replace_move                     XVector_replace_move 
// 转换函数
XJsonArray* XBsonArray_to_json_array(const XBsonArray* bson_arr);
void XBsonArray_from_json_array(XBsonArray* bson_arr, const XJsonArray* json_arr);

// 序列化与反序列化
XByteArray* XBsonArray_to_bytes(const XBsonArray* array);
bool XBsonArray_from_bytes(XBsonArray* array, const uint8_t* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // XBSONARRAY_H