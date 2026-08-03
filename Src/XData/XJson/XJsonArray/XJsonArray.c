#include "XJsonArray.h"
#include "XJsonDocument.h"
#include "XMemory.h"
#include "XVariantTypeOps.h"
#include "XVariantList.h"
#include "XStringList.h"
#include "XJsonValue.h"
#include "XStack.h"

int32_t XJsonArray_compare(const XJsonArray* lhs, const XJsonArray* rhs)
{
    return XJsonArray_equals(lhs, rhs)
        ? XCompare_Equality : XCompare_Other;
}

XVARIANT_TYPE_OPS_DEFINE(XJsonArray, sizeof(XJsonArray), XJsonArray_copy_base,
	XJsonArray_move_base, XJsonArray_clear_base, XJsonArray_deinit_base,
	XJsonArray_compare, "XJsonArray");

XJsonArray* XJsonArray_create()
{
    XJsonArray* array = (XJsonArray*)XMalloc_System(sizeof(XJsonArray));
    if (array == NULL)
        return NULL;
    XJsonArray_init(array);
    Set_Class_MemoryFree(array, XFree_System);
    return array;
}

XJsonArray* XJsonArray_create_copy(XJsonArray* copy)
{
    XJsonArray* array = XJsonArray_create();
    if (array&& copy)
        XJsonArray_copy_base(array,copy);
    return array;
}

XJsonArray* XJsonArray_create_move(XJsonArray* move)
{
    XJsonArray* array = XJsonArray_create();
    if (array && move)
        XJsonArray_move_base(array, move);
    return array;
}

void XJsonArray_init(XJsonArray* array)
{
    if (array==NULL)
        return;
    XVector_init(array, sizeof(XJsonValue),true);
    XContainerSetDataDeinitMethod(array, XJsonValue_deinit);
    XContainerSetDataCopyMethod(array, XJsonValue_copy);
    XContainerSetDataMoveMethod(array, XJsonValue_move);
}

// 数组序列化实现
XString* XJsonArray_toString(const XJsonArray* array, XJsonDocumentFormat format)
{
    XJsonDocument* doc = XJsonDocument_create();
    if (!doc)
        return NULL;
    //引用XJsonArray 
    doc->root.data.array = (XJsonArray*)array;
    doc->root.type = XJsonValue_Array;
    XString* str = XJsonDocument_toString(doc, format);
    //恢复防止释放 XJsonArray
    doc->root.data.array = NULL;
    doc->root.type = XJsonValue_Invalid;
    XJsonDocument_delete(doc);
    return str;
}
XVariantList* XJsonArray_toVariantList(const XJsonArray* arr)
{
    if (arr == NULL)
        return NULL;
    XVariantList* list = XVariantList_create();
    if (list == NULL)
        return NULL;
    XJsonValue* value = NULL;
    XVariant* var = NULL;
    for_each_iterator(arr, XVector, it)
    {
        value = XVector_iterator_data(&it);
        var=XJsonValue_toVariant(value);
        XVariantList_push_back_move_base(list,var);
        XVariant_delete_base(var);
    }
    return list;
}
XVariantList* XJsonArray_toVariantList_move(XJsonArray* arr)
{
    if (arr == NULL)
        return NULL;
    XVariantList* list = XVariantList_create();
    if (list == NULL)
        return NULL;
    XVector_detach((XVector*)arr);
    if (!XVector_isDetached((const XVector*)arr)) {
        XVariantList_delete_base(list);
        return NULL;
    }
    XJsonValue* value = NULL;
    XVariant* var = NULL;
    for_each_iterator(arr, XVector, it)
    {
        value = XVector_iterator_data(&it);
        var = XJsonValue_toVariant_move(value);
        XVariantList_push_back_move_base(list, var);
        XVariant_delete_base(var);
    }
    return list;
}
XVariant* XJsonArray_toVariant(const XJsonArray* arr)
{
    if (arr == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, sizeof(XJsonArray), XVariantType_JsonArray);
    XJsonArray_init(var->m_data);
    XJsonArray_copy_base(var->m_data, arr);
    return var;
}
XVariant* XJsonArray_toVariant_move(XJsonArray* arr)
{
    if (arr == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, sizeof(XJsonArray), XVariantType_JsonArray);
    XJsonArray_init(var->m_data);
    XJsonArray_move_base(var->m_data, arr);
    return var;
}
XVariant* XJsonArray_toVariant_ref(XJsonArray* arr)
{
    if (arr == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, 0, XVariantType_JsonArray);
    if (var == NULL)
        return NULL;
	XVariant_setDataRef(var, arr, sizeof(XJsonArray), XVariantType_JsonArray);
    return var;
}

XJsonArray* XJsonArray_fromVariant(const XVariant* variant)
{
    XJsonArray* source = (XJsonArray*)XVariant_toRef(variant, XVariantType_JsonArray);
    return source ? XJsonArray_create_copy(source) : NULL;
}

XJsonArray* XJsonArray_fromVariant_ref(const XVariant* variant)
{
    return (XJsonArray*)XVariant_toRef(variant, XVariantType_JsonArray);
}

static bool XJsonArray_prepareVariant(XVariant* variant)
{
    if (!variant)
        return false;
    if (variant->m_type != XVariantType_JsonArray ||
        !variant->m_data || variant->m_dataSize != sizeof(XJsonArray)) {
        if (variant->m_data)
            XVariant_deinit_base(variant);
        variant->m_data = XMalloc_System(sizeof(XJsonArray));
        if (!variant->m_data)
            return false;
        variant->m_dataSize = sizeof(XJsonArray);
        XJsonArray_init((XJsonArray*)variant->m_data);
        variant->m_type = XVariantType_JsonArray;
    }
    return true;
}

void XJsonArray_setVariant(XVariant* variant, const XJsonArray* array)
{
    if (array && XJsonArray_prepareVariant(variant))
        XJsonArray_copy_base((XJsonArray*)variant->m_data, array);
}

void XJsonArray_setVariant_move(XVariant* variant, XJsonArray* array)
{
    if (array && XJsonArray_prepareVariant(variant))
        XJsonArray_move_base((XJsonArray*)variant->m_data, array);
}

void XJsonArray_setVariant_ref(XVariant* variant, XJsonArray* array)
{
	if (!variant || !array)
		return;
	XVariant_setDataRef(variant, array, sizeof(XJsonArray), XVariantType_JsonArray);
}
//XVariantList* XJsonArray_toVariantList(const XJsonArray* array) {
//    if (!array) return NULL;
//
//    XVariantList* list = XVariantList_create();
//    if (!list) return NULL;
//
//    int size = XJsonArray_size(array);
//    for (int i = 0; i < size; i++) {
//        const XJsonValue* jsonVal = XJsonArray_at_const(array, i);
//        XVariant* var = XJsonValue_toVariant(jsonVal);
//        if (var) {
//            XVariantList_push_back_base(list, var);
//        }
//    }
//
//    return list;
//}
//
//XJsonArray* XJsonArray_fromVariantList(const XVariantList* list) {
//    if (!list) return NULL;
//
//    XJsonArray* array = XJsonArray_create();
//    if (!array) return NULL;
//
//    int size = XVariantList_size_base(list);
//    for (int i = 0; i < size; i++) {
//        const XVariant* var = XVariantList_at_base(list, i);
//        XJsonValue* jsonVal = XJsonValue_fromVariant(var);
//        if (jsonVal) {
//            XJsonArray_append(array, jsonVal);
//        }
//    }
//
//    return array;
//}

XJsonValue* XJsonArray_at(XJsonArray* array, int64_t index)
{
    size_t size;
    if (!array)
        return NULL;
    size = XJsonArray_size_base(array);
    if (index < 0)
        index += (int64_t)size;
    if (index < 0 || (size_t)index >= size)
        return NULL;
    return XVector_at_base(array, index);
}

const XJsonValue* XJsonArray_at_const(const XJsonArray* array, int64_t index)
{
    return XJsonArray_at((XJsonArray*)array, index);
}

XJsonValue* XJsonArray_first(const XJsonArray* array)
{
    return (array && XJsonArray_size_base(array) > 0) ?
        XJsonValue_create_copy(XJsonArray_at_const(array, 0)) : XJsonValue_create_undefined();
}

XJsonValue* XJsonArray_last(const XJsonArray* array)
{
    size_t size = array ? XJsonArray_size_base(array) : 0;
    return size ? XJsonValue_create_copy(XJsonArray_at_const(array, (int64_t)size - 1)) :
        XJsonValue_create_undefined();
}

XJsonValue* XJsonArray_takeAt(XJsonArray* array, int64_t index)
{
    XJsonValue* value;
    size_t size;
    if (!array)
        return XJsonValue_create_undefined();
    size = XJsonArray_size_base(array);
    if (index < 0)
        index += (int64_t)size;
    if (index < 0 || (size_t)index >= size)
        return XJsonValue_create_undefined();
    value = XJsonValue_create_copy(XJsonArray_at(array, index));
    if (value)
        XJsonArray_removeAt_base(array, index);
    return value;
}

bool XJsonArray_contains(const XJsonArray* array, const XJsonValue* value)
{
    size_t index;
    if (!array || !value)
        return false;
    for (index = 0; index < XJsonArray_size_base(array); ++index) {
        if (XJsonValue_equals(XJsonArray_at_const(array, (int64_t)index), value))
            return true;
    }
    return false;
}

bool XJsonArray_equals(const XJsonArray* left, const XJsonArray* right)
{
    size_t index;
    size_t size;
    if (left == right)
        return true;
    if (!left || !right)
        return false;
    size = XJsonArray_size_base(left);
    if (size != XJsonArray_size_base(right))
        return false;
    for (index = 0; index < size; ++index) {
        if (!XJsonValue_equals(XJsonArray_at_const(left, (int64_t)index),
                               XJsonArray_at_const(right, (int64_t)index)))
            return false;
    }
    return true;
}

XJsonArray* XJsonArray_fromStringList(const XStringList* list)
{
    XJsonArray* array;
    size_t index;
    if (!list)
        return NULL;
    array = XJsonArray_create();
    if (!array)
        return NULL;
    for (index = 0; index < XStringList_size_base(list); ++index) {
        XString* string = XStringList_at_base((XStringList*)list, (int64_t)index);
        XJsonValue* value = string ? XJsonValue_create_string(string) : NULL;
        if (!value || !XJsonArray_append_move_base(array, value)) {
            if (value) XJsonValue_delete(value);
            XJsonArray_delete_base(array);
            return NULL;
        }
        XJsonValue_delete(value);
    }
    return array;
}

XJsonArray* XJsonArray_fromVariantList(const XVariantList* list)
{
    XJsonArray* array;
    size_t index;
    if (!list)
        return NULL;
    array = XJsonArray_create();
    if (!array)
        return NULL;
    for (index = 0; index < XVariantList_size_base(list); ++index) {
        XVariant* variant = XVariantList_at_base((XVariantList*)list, (int64_t)index);
        XJsonValue* value = XJsonValue_fromVariant(variant);
        if (!value || !XJsonArray_append_move_base(array, value)) {
            if (value) XJsonValue_delete(value);
            XJsonArray_delete_base(array);
            return NULL;
        }
        XJsonValue_delete(value);
    }
    return array;
}
