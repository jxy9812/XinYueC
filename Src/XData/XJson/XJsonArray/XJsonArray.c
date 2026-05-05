#include "XJsonArray.h"
#include "XJsonDocument.h"
#include "XMemory.h"
#include "XVariantList.h"
#include "XJsonValue.h"
#include "XStack.h"

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
    XVector_init(array, sizeof(XJsonValue));
    XContainerSetDataDeinitMethod(array, XJsonValue_deinit);
    XContainerSetDataCopyMethod(array, XJsonValue_copy);
    XContainerSetDataMoveMethod(array, XJsonValue_move);
}

// 数组序列化实现
XString* XJsonArray_toString(const XJsonArray* array, XJsonDocumentFormat format)
{
    XJsonDocument* doc = XJsonDocument_create();
    //引用XJsonArray 
    doc->root.data.array = array;
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
    var->m_data = arr;
    var->m_dataSize = sizeof(XJsonArray);
    return var;
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
    return XVector_at_base(array,index);
}

const XJsonValue* XJsonArray_at_const(const XJsonArray* array, int64_t index)
{
    return XJsonArray_at((XJsonArray*)array, index);
}