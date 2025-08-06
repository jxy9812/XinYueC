#include "XJsonArray.h"
#include "XMemory.h"
#include "XVariantList.h"
#include "XJsonValue.h"
// 辅助函数：序列化XJsonValue
void XJson_serialize_json_value(const XJsonValue* value, XString* output);

XJsonArray* XJsonArray_create(void) 
{
    XJsonArray* array = (XJsonArray*)XMemory_malloc(sizeof(XJsonArray));
    XVector_init(array, sizeof(XJsonValue));
    XContainerSetDataDeinitMethod(array, XJsonValue_deinit);
    XContainerSetDataCopyMethod(array, XJsonValue_copy);
    XContainerSetDataMoveMethod(array, XJsonValue_move);
    return array;
}

// 数组序列化实现
XString* XJsonArray_toString(const XJsonArray* array) {
    if (!array) return NULL;

    // 创建输出字符串
    XString* output = XString_create_utf8("");
    if (!output) return NULL;

    // 开始数组
    XString_append_utf8(output, "[");

    int size = XJsonArray_size_base(array);
    for (int i = 0; i < size; i++) {
        const XJsonValue* value = XJsonArray_at_const(array, i);
        if (value) {
            XJson_serialize_json_value(value, output);
        }
        else {
            XString_append_utf8(output, "null");
        }

        // 添加逗号分隔（最后一个元素除外）
        if (i != size - 1) {
            XString_append_utf8(output, ", ");
        }
    }

    // 结束数组
    XString_append_utf8(output, "]");

    return output;
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