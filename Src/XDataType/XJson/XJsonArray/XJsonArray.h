#ifndef XJSONARRAY_H
#define XJSONARRAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XJson.h"

#include "XVector.h"

#if !XVector_ON
#error "XJsonArray requires XVector to be enabled in XDataStructConfig.h"
#endif

typedef struct XJsonArray 
{
    XVector elements; // 存储XJsonValue的向量
} XJsonArray;

// 构造与析构
XJsonArray* XJsonArray_create();
XJsonArray* XJsonArray_create_copy(XJsonArray* copy);
XJsonArray* XJsonArray_create_move(XJsonArray* move);
void XJsonArray_init(XJsonArray*array);
#define XJsonArray_rcopy_base						XVector_rcopy_base
#define XJsonArray_copy_base						XVector_copy_base	
#define XJsonArray_move_base						XVector_move_base	
#define XJsonArray_deinit_base						XVector_deinit_base	
#define XJsonArray_delete_base						XVector_delete_base	
#define XJsonArray_clear_base						XVector_clear_base	
#define XJsonArray_isEmpty_base						XVector_isEmpty_base	
#define XJsonArray_size_base						XVector_size_base	
#define XJsonArray_capacity_base					XVector_capacity_base
#define XJsonArray_swap_base						XVector_swap_base	
#define XJsonArray_typeSize_base					XVector_typeSize_base
#define XJsonArray_count_base						XVector_count_base			
#define XJsonArray_append_base						XVector_append_base			
#define XJsonArray_append_move_base					XVector_append_move_base
#define XJsonArray_prepend_base						XVector_prepend_base	
#define XJsonArray_prepend_move_base				XVector_prepend_move_base
#define XJsonArray_insert                           XVector_insert
#define XJsonArray_insert_move                      XVector_insert_move
#define XJsonArray_removeAt_base                    XVector_removeAt_base
#define XJsonArray_replace                          XVector_replace
#define XJsonArray_replace_move                     XVector_replace_move 
// 元素访问
XJsonValue* XJsonArray_at(XJsonArray* array, int64_t index);
const XJsonValue* XJsonArray_at_const(const XJsonArray* array, int64_t index);

// 转换函数
XString* XJsonArray_toString(const XJsonArray* array, XJsonDocumentFormat format);

XVariantList* XJsonArray_toVariantList(const XJsonArray* array);
XVariantList* XJsonArray_toVariantList_move(XJsonArray* array);
// 与XVariant转换
XVariant* XJsonArray_toVariant(const XJsonArray* array);
XVariant* XJsonArray_toVariant_move(XJsonArray* array);
XVariant* XJsonArray_toVariant_ref(XJsonArray* array);
#ifdef __cplusplus
}
#endif

#endif // XJSONARRAY_H