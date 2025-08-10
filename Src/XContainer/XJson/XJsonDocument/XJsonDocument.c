#include "XJsonDocument.h"
#include "XJsonValue.h"
#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XByteArray.h"
#include "XString.h"
#include "XStack.h"
#include "XMemory.h"

// 辅助函数：转义字符串并添加到字节数组（UTF-8）
static void XJson_append_escaped_string_byteArray(const XString* str, XByteArray* output);
// 辅助函数：添加缩进
static void XJson_append_indent(XJsonDocumentFormat format, XStack* stack, XByteArray* output);
// 对象序列化到字节数组
static void XJsonObject_toByteArray(const XJsonObject* object, XJsonDocumentFormat format,
    XStack* stack, XByteArray* output);
// 数组序列化到字节数组
static void XJsonArray_toByteArray(const XJsonArray* array, XJsonDocumentFormat format,
    XStack* stack, XByteArray* output);
// 基本值序列化到字节数组
static void XJsonValue_toByteArray(const XJsonValue* value, XJsonDocumentFormat format,
    XStack* stack, XByteArray* output);


XJsonDocument* XJsonDocument_create(void)
{
    XJsonDocument* doc = (XJsonDocument*)XMemory_malloc(sizeof(XJsonDocument));
    XJsonDocument_init(doc);
    return doc;
}

XJsonDocument* XJsonDocument_create_object(XJsonObject* object) 
{
    if (!object) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc) 
    {
        XJsonValue_setObject(doc->root, object);
    }
    return doc;
}

XJsonDocument* XJsonDocument_create_array(XJsonArray* array) {
    if (!array) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc)
    {
        XJsonValue_setArray(doc->root, array);
    }
    return doc;
}

void XJsonDocument_init(XJsonDocument* document)
{
    if (document == NULL)
        return;
    document->root = XJsonValue_create_null();
}

void XJsonDocument_deinit(XJsonDocument* document)
{
    if (!document) return;

    if (document->root) 
    {
        XJsonValue_delete(document->root);
        document->root = NULL;
    }
}

void XJsonDocument_delete(XJsonDocument* document)
{
    XJsonDocument_deinit(document);
    if(document)
        XMemory_free(document);
}

XJsonValue* XJsonDocument_root(XJsonDocument* document) 
{
    return document ? document->root : NULL;
}

const XJsonValue* XJsonDocument_root_const(const XJsonDocument* document) 
{
    return XJsonDocument_root((XJsonDocument*)document);
}

void XJsonDocument_setRoot(XJsonDocument* document, XJsonValue* root) 
{
    if (!document || !root) return;

    if (document->root)
    {
        XJsonValue_delete(document->root);
    }
    document->root = root;
}

bool XJsonDocument_isArray(const XJsonDocument* document)
{
    if (!document || !document->root) 
        return false;
    return document->root->type == XJsonValue_Array;
}

bool XJsonDocument_isObject(const XJsonDocument* document)
{
    if (!document || !document->root)
        return false;
    return document->root->type == XJsonValue_Object;
}

bool XJsonDocument_isNull(const XJsonDocument* document)
{
    if (!document || !document->root)
        return false;
    switch (document->root->type)
    {
    case XJsonValue_Invalid:
    case XJsonValue_Null: return true;
    default:
        break; 
    };
    return false;
}

bool XJsonDocument_isEmpty(const XJsonDocument* document)
{
    if (!document || !document->root)
        return false;
    switch (document->root->type)
    {
    case XJsonValue_Invalid:
    case XJsonValue_Null: return true;
    case XJsonValue_String:return XString_isEmpty_base(document->root->data.string);
    case XJsonValue_Array: return XJsonArray_isEmpty_base(document->root->data.array);
    case XJsonValue_Object:return XJsonObject_isEmpty_base(document->root->data.object);
    default:
        break;
    };
    return false;
}

XJsonObject* XJsonDocument_object(XJsonDocument* document) 
{
    if (!document || !document->root) return NULL;

    if (document->root->type != XJsonValue_Object) 
    {
       /* XJsonObject* obj = XJsonObject_create();
        XJsonValue_setObject(document->root, obj);*/
        return NULL;
    }

    return document->root->data.object;
}

XJsonArray* XJsonDocument_array(XJsonDocument* document) 
{
    if (!document || !document->root) return NULL;

    if (document->root->type != XJsonValue_Array) {
     /*   XJsonArray* arr = XJsonArray_create();
        XJsonValue_setArray(document->root, arr);
        return arr;*/
        return NULL;
    }

    return document->root->data.array;
}

bool XJsonDocument_setArray(XJsonDocument* document, const XJsonArray* array)
{
    if (!document || array) 
        return false;
    if (document->root)
        XJsonValue_setArray(document->root, array);
    else
        document->root = XJsonValue_create_array(array);
    return true;
}

bool XJsonDocument_setObject(XJsonDocument* document, const XJsonObject* object)
{
    if (!document || object)
        return false;
    if (document->root)
        XJsonValue_setObject(document->root, object);
    else
        document->root = XJsonValue_create_object(object);
    return true;
}

bool XJsonDocument_setArray_move(XJsonDocument* document, XJsonArray* array)
{
    if (!document || array)
        return false;
    if (document->root=NULL)
        document->root = XJsonValue_create_null();
    XJsonValue_setArray_move(document->root, array);
    return true;
}

bool XJsonDocument_setObject_move(XJsonDocument* document, XJsonObject* object)
{
    if (!document || object)
        return false;
    if (document->root = NULL)
        document->root = XJsonValue_create_null();
    XJsonValue_setObject_move(document->root, object);
    return true;
}

XString* XJsonDocument_toString(const XJsonDocument* document, XJsonDocumentFormat format)
{
    if (!document || !document->root) return NULL;
    XByteArray* json = XJsonDocument_toJson(document, format);
    XString* str = XString_create_utf8(XContainerDataPtr(json));
    XByteArray_delete_base(json);
    return str;
}

XByteArray* XJsonDocument_toJson(const XJsonDocument* document, XJsonDocumentFormat format)
{
    if (!document || !document->root) return NULL;

    // 创建字节数组存储UTF-8结果
    XByteArray* output = XByteArray_create(0);
    if (!output) return NULL;

    // 创建栈管理嵌套深度（存储int类型的深度值）
    XStack* stack = XStack_create(sizeof(int));
    if (!stack) {
        XByteArray_delete_base(output);
        return NULL;
    }
    XStack_Push_Base(stack, int,0);

    // 根据根节点类型序列化
    switch (document->root->type) {
    case XJsonValue_Object:
        XJsonObject_toByteArray(document->root->data.object, format, stack, output);
        break;
    case XJsonValue_Array:
        XJsonArray_toByteArray(document->root->data.array, format, stack, output);
        break;
    default:
        XJsonValue_toByteArray(document->root, format, stack, output);
        break;
    }
    //添加结束符号
    XByteArray_push_back_base(output,0);
    // 清理资源
    XStack_delete_base(stack);
    return output;
}

//XJsonDocument* XJsonDocument_fromString(const XString* json) {
//    // 实际实现需要解析JSON字符串
//    // 这里仅作为框架示例
//    XJsonDocument* doc = XJsonDocument_create();
//    if (doc && json) {
//        // 解析逻辑将在这里实现
//    }
//    return doc;
//}
//
//XString* XJsonDocument_toString(const XJsonDocument* document, XJsonDocumentFormat format) {
//    if (!document || !document->root) return NULL;
//
//    switch (document->root->type) {
//    case XJsonValue_Object:
//        return XJsonObject_toString(document->root->data.object);
//    case XJsonValue_Array:
//        return XJsonArray_toString(document->root->data.array);
//    default:
//        return NULL;
//    }
//}
//
//XVariant* XJsonDocument_toVariant(const XJsonDocument* document) {
//    if (!document || !document->root) return NULL;
//    return XJsonValue_toVariant(document->root);
//}
//
//XJsonDocument* XJsonDocument_fromVariant(const XVariant* variant) {
//    if (!variant) return NULL;
//
//    XJsonDocument* doc = XJsonDocument_create();
//    if (doc) {
//        XJsonValue* root = XJsonValue_fromVariant(variant);
//        if (root) {
//            XJsonDocument_setRoot(doc, root);
//        }
//        else {
//            XJsonDocument_delete(doc);
//            return NULL;
//        }
//    }
//
//    return doc;
//}

void XJson_append_escaped_string_byteArray(const XString* str, XByteArray* output)
{
    if (!str || !output) return;

    // 获取UTF-8数据（假设XString提供UTF-8转换接口）
    const char* utf8_str = XString_toUtf8(str);
    size_t len = XString_toUtf8_length(str);

    // 添加引号
    XByteArray_append_array_base(output, "\"", 1);

    // 转义特殊字符
    for (size_t i = 0; i < len; i++) {
        switch (utf8_str[i]) {
        case '\"': XByteArray_append_array_base(output, "\\\"", 2); break;
        case '\\': XByteArray_append_array_base(output, "\\\\", 2); break;
        case '\b': XByteArray_append_array_base(output, "\\b", 2); break;
        case '\f': XByteArray_append_array_base(output, "\\f", 2); break;
        case '\n': XByteArray_append_array_base(output, "\\n", 2); break;
        case '\r': XByteArray_append_array_base(output, "\\r", 2); break;
        case '\t': XByteArray_append_array_base(output, "\\t", 2); break;
        default:
            // 直接添加UTF-8字节
            XByteArray_append_array_base(output, &(utf8_str[i]), 1);
            break;
        }
    }

    // 添加结束引号
    XByteArray_append_array_base(output, "\"", 1);
}

void XJson_append_indent(XJsonDocumentFormat format, XStack* stack, XByteArray* output)
{
    if (format != XJsonDocument_Indented) return;

    // 获取当前深度
    int* depth_ptr =XStack_top_base(stack);
    if (!depth_ptr) 
        return;
    int depth = *depth_ptr;

    // 每个层级添加4个空格（UTF-8编码）
    const char space[] = "    ";
    for (int i = 0; i < depth; i++) {
        XByteArray_append_array_base(output, space, sizeof(space) - 1);
    }
}

void XJsonObject_toByteArray(const XJsonObject* object, XJsonDocumentFormat format, XStack* stack, XByteArray* output)
{
    if (!object || !stack || !output || XJsonObject_isEmpty_base(object)) 
    {
        XByteArray_append_array_base(output, "{}", 2);
        return;
    }

    // 获取当前深度并压入新深度（+1）
    int new_depth = XStack_Top_Base(stack,int) + 1;
    XStack_push_base(stack, &new_depth);

    // 写入对象开始符
    XByteArray_append_array_base(output, "{", 1);
    if (format == XJsonDocument_Indented) {
        XByteArray_append_array_base(output, "\n", 1);
    }

    // 获取键列表
    XVector* keys = XJsonObject_keys_base(object);
    size_t key_count = XVector_size_base(keys);

    for (size_t i = 0; i < key_count; i++) {
        XString* key = (XString*)XVector_at_base(keys, i);
        const XJsonValue* value = XJsonObject_value_base(object, key);
        if (!key || !value) continue;

        // 添加缩进
        XJson_append_indent(format, stack, output);

        // 写入键（转义处理）
        XJson_append_escaped_string_byteArray(key, output);

        // 键值分隔符
        XByteArray_append_array_base(output, format == XJsonDocument_Indented ? ": " : ":",
            format == XJsonDocument_Indented ? 2 : 1);

        // 写入值
        XJsonValue_toByteArray(value, format, stack, output);

        // 分隔符（最后一个元素不加）
        if (i != key_count - 1) {
            XByteArray_append_array_base(output, ",", 1);
            if (format == XJsonDocument_Indented) {
                XByteArray_append_array_base(output, "\n", 1);
            }
        }
    }

    // 释放键列表
    XVector_delete_base(keys);

    // 恢复深度
    XStack_pop_base(stack);

    // 写入对象结束符
    if (format == XJsonDocument_Indented) {
        XByteArray_append_array_base(output, "\n", 1);
        XJson_append_indent(format, stack, output);
    }
    XByteArray_append_array_base(output, "}", 1);
}

void XJsonArray_toByteArray(const XJsonArray* array, XJsonDocumentFormat format, XStack* stack, XByteArray* output)
{
    if (!array || !stack || !output || XJsonArray_isEmpty_base(array)) {
        XByteArray_append_array_base(output, "[]", 2);
        return;
    }

    // 获取当前深度并压入新深度（+1）
    int new_depth = XStack_Top_Base(stack, int) + 1;
    XStack_push_base(stack, &new_depth);

    // 写入数组开始符
    XByteArray_append_array_base(output, "[", 1);
    if (format == XJsonDocument_Indented) {
        XByteArray_append_array_base(output, "\n", 1);
    }

    // 遍历元素
    size_t elem_count = XJsonArray_size_base(array);
    for (size_t i = 0; i < elem_count; i++) {
        const XJsonValue* elem = XJsonArray_at(array, i);
        if (!elem) continue;

        // 添加缩进
        XJson_append_indent(format, stack, output);

        // 写入元素值
        XJsonValue_toByteArray(elem, format, stack, output);

        // 分隔符（最后一个元素不加）
        if (i != elem_count - 1) {
            XByteArray_append_array_base(output, ",", 1);
            if (format == XJsonDocument_Indented) {
                XByteArray_append_array_base(output, "\n", 1);
            }
        }
    }

    // 恢复深度
    XStack_pop_base(stack);

    // 写入数组结束符
    if (format == XJsonDocument_Indented) {
        XByteArray_append_array_base(output, "\n", 1);
        XJson_append_indent(format, stack, output);
    }
    XByteArray_append_array_base(output, "]", 1);
}

void XJsonValue_toByteArray(const XJsonValue* value, XJsonDocumentFormat format, XStack* stack, XByteArray* output)
{
    if (!value || !output) return;

    switch (value->type) {
    case XJsonValue_Null:
        XByteArray_append_array_base(output, "null", 4);
        break;

    case XJsonValue_Bool:
        if (value->data.boolean) {
            XByteArray_append_array_base(output, "true", 4);
        }
        else {
            XByteArray_append_array_base(output, "false", 5);
        }
        break;

    case XJsonValue_Double: {
        // 转换数字为字符串（UTF-8）
        char buffer[64];
        double num = XJsonValue_toDouble(value, 0.0);
        if (num == (long long)num) {
            snprintf(buffer, sizeof(buffer), "%lld", (long long)num);
        }
        else {
            snprintf(buffer, sizeof(buffer), "%g", num);
        }
        XByteArray_append_utf8(output, buffer);
        break;
    }

    case XJsonValue_String:
        XJson_append_escaped_string_byteArray(value->data.string, output);
        break;

    case XJsonValue_Object:
        XJsonObject_toByteArray(value->data.object, format, stack, output);
        break;

    case XJsonValue_Array:
        XJsonArray_toByteArray(value->data.array, format, stack, output);
        break;

    default:
        XByteArray_append_array_base(output, "null", 4);
        break;
    }
}
