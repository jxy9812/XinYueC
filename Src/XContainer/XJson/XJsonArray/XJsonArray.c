#include "XJsonArray.h"
#include "XMemory.h"
#include "XVariantList.h"
#include "XJsonValue.h"


XJsonArray* XJsonArray_create(void) 
{
    XJsonArray* array = (XJsonArray*)XMemory_malloc(sizeof(XJsonArray));
    XVector_init(array, sizeof(XJsonValue));
    XContainerSetDataDeinitMethod(array, XJsonValue_deinit);
    XContainerSetDataCopyMethod(array, XJsonValue_copy);
    XContainerSetDataMoveMethod(array, XJsonValue_move);
    return array;
}
// 辅助函数：将XJsonValue序列化为JSON字符串
static void serialize_json_value(const XJsonValue* value, XString* output);

// 辅助函数：转义字符串中的特殊字符
static void escape_string(const XString* str, XString* output) 
{
    if (!str || !output) return;

    for_each_iterator(str,XString,it)
    {
        XChar* p = XString_iterator_data(&it);
        switch (p->code) {
        case '"':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('\"'));
        }
        /*XString_append_utf8(output, "\\\"");*/ break;
        case '\\': 
        {   
            XString_push_back_base(output, XChar_from('\\')); 
            XString_push_back_base(output, XChar_from('\\'));
        } /*XString_append_utf8(output, "\\\\");*/ break;
        case '\b':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('b'));
        }/*XString_append_utf8(output, "\\b"); */ break;
        case '\f':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('f'));
        }/*XString_append_utf8(output, "\\f");*/  break;
        case '\n': 
        { 
            XString_push_back_base(output, XChar_from('\\')); 
            XString_push_back_base(output, XChar_from('n'));
        }/*XString_append_utf8(output, "\\n"); */ break;
        case '\r':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('r'));
        }/* XString_append_utf8(output, "\\r"); */ break;
        case '\t':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('t'));
        }/*XString_append_utf8(output, "\\t");*/  break;
        default:
            // 对于非ASCII字符，保持原样（JSON允许UTF-8编码）
            XString_push_back_base(output, *p);
            break;
        }
    }
}

// 辅助函数：序列化XJsonValue
static void serialize_json_value(const XJsonValue* value, XString* output) 
{
    if (!value || !output) return;

    switch (XJsonValue_type(value)) {
    case XJsonValue_Null:
        XString_append_utf8(output, "null");
        break;

    case XJsonValue_Bool:
        XString_append_utf8(output, XJsonValue_toBool(value, false) ? "true" : "false");
        break;

    case XJsonValue_Double: {
        char buffer[64];
        // 处理整数情况，避免显示为10.0这样的形式
        double num = XJsonValue_toDouble(value, 0.0);
        if (num == (long long)num) {
            snprintf(buffer, sizeof(buffer), "%lld", (long long)num);
        }
        else {
            snprintf(buffer, sizeof(buffer), "%g", num);
        }
        XString_append_utf8(output, buffer);
        break;
    }

    case XJsonValue_String: {
        const XString* str = XJsonValue_toString(value);
        XString_append_utf8(output, "\"");
        if (str) 
        {
            escape_string(str, output);
        }
        XString_append_utf8(output, "\"");
        break;
    }

    case XJsonValue_Array: {
        const XJsonArray* array = XJsonValue_toArray(value);
        XString* arrayStr = XJsonArray_toString(array);
        if (arrayStr) {
            XString_append(output, arrayStr);
            XString_delete_base(arrayStr);
        }
        else {
            XString_append_utf8(output, "[]");
        }
        break;
    }

    case XJsonValue_Object: {
        const XJsonObject* object = XJsonValue_toObject(value);
     /*   XString* objectStr = XJsonObject_toString(object);
        if (objectStr) {
            XString_append_string(output, objectStr);
            XString_delete_base(objectStr);
        }
        else {
            XString_append_utf8(output, "{}");
        }*/
        break;
    }

    default:
        XString_append_utf8(output, "null");
        break;
    }
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
            serialize_json_value(value, output);
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