#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XBsonDocument.h"
#include "XBsonArray.h"
#include "XByteArray.h"
#include "XString.h"
#include "XStack.h"
#include "XMemory.h"
#include "XNumStrConv.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
/*                                  XJsonDocument_fromJson                                             */
// 解析上下文：用于栈存储当前解析的容器（对象/数组）及状态
typedef enum 
{
    CONTEXT_OBJECT,
    CONTEXT_ARRAY
} ContextType;

typedef struct 
{
    ContextType type;
    union {
        XJsonObject* object;
        XJsonArray* array;
    } container;
    XString* currentKey; // 仅用于对象解析时存储当前键
} ParseContext;
// 辅助函数：跳过空白字符
static const char* Json_skip_whitespace(const char* ptr, const char* end);
// 辅助函数：解析字符串（处理转义字符）
static XString* Json_parse_string(const char** ptr, const char* end);
// 辅助函数：解析数字
static bool Json_parse_number(const char** ptr, const char* end, double* out_num, int64_t* out_int, bool* is_int);
// 辅助函数：解析关键字（true/false/null）
static XJsonValue* Json_parse_keyword(const char** ptr, const char* end);
// 解析对象
static XJsonValue* Json_parse_object(const char** ptr, const char* end, XStack* stack);
// 解析数组
static XJsonValue* Json_parse_array(const char** ptr, const char* end, XStack* stack);
// 解析值（调度到对应类型的解析函数）
static XJsonValue* Json_parse_value(const char** ptr, const char* end, XStack* stack);

/*                                  XJsonDocument_toJson                                             */                
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
    XJsonDocument* doc = (XJsonDocument*)XMalloc_System(sizeof(XJsonDocument));
    XJsonDocument_init(doc);
    return doc;
}

XJsonDocument* XJsonDocument_create_copy(XJsonDocument* copy)
{
    XJsonDocument* doc = XJsonDocument_create();
    if (doc && copy)
        XJsonDocument_copy(doc, copy);
    return doc;
}

XJsonDocument* XJsonDocument_create_move(XJsonDocument* move)
{
    XJsonDocument* doc = XJsonDocument_create();
    if (doc && move)
        XJsonDocument_move(doc, move);
    return doc;
}

XJsonDocument* XJsonDocument_create_object(XJsonObject* object) 
{
    if (!object) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc) 
    {
        XJsonValue_setObject(doc, object);
    }
    return doc;
}

XJsonDocument* XJsonDocument_create_object_move(XJsonObject* object)
{
    if (!object) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc)
    {
        XJsonValue_setObject_move(doc, object);
    }
    return doc;
}

XJsonDocument* XJsonDocument_create_array(XJsonArray* array) {
    if (!array) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc)
    {
        XJsonValue_setArray(doc, array);
    }
    return doc;
}

XJsonDocument* XJsonDocument_create_array_move(XJsonArray* array)
{
    if (!array) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc)
    {
        XJsonValue_setArray_move(doc, array);
    }
    return doc;
}

void XJsonDocument_init(XJsonDocument* document)
{
    if (document == NULL)
        return;
    XJsonValue_init(document, XJsonValue_Null);
    //document->root = XJsonValue_create_null();
}

void XJsonDocument_deinit(XJsonDocument* document)
{
    if (!document) return;
    XJsonValue_deinit(document);
  /*  if (document->root) 
    {
        XJsonValue_delete(document->root);
        document->root = NULL;
    }*/
}

void XJsonDocument_delete(XJsonDocument* document)
{
    XJsonDocument_deinit(document);

    if(document)
        XFree_System(document);
}

void XJsonDocument_clear(XJsonDocument* document)
{
    XJsonValue_clear(document);
}

void XJsonDocument_copy(XJsonDocument* doc, const XJsonDocument* src)
{
    if (doc == NULL || src == NULL)
        return;
    XJsonValue_copy(doc,src);
}

void XJsonDocument_move(XJsonDocument* doc, XJsonDocument* src)
{
    if (doc == NULL || src == NULL)
        return;
    XJsonValue_move(doc, src);
}

XJsonValue* XJsonDocument_root(XJsonDocument* document) 
{
    return document ? document : NULL;
}

const XJsonValue* XJsonDocument_root_const(const XJsonDocument* document) 
{
    return XJsonDocument_root((XJsonDocument*)document);
}

void XJsonDocument_setRoot(XJsonDocument* document,const XJsonValue* root) 
{
    if (!document || !root) return;
    XJsonValue_copy(document,root);
    /*if (document->root)
    {
        XJsonValue_delete(document->root);
    }
    document->root = root;*/
}

void XJsonDocument_setRoot_move(XJsonDocument* document, XJsonValue* root)
{
    if (!document || !root) return;
    XJsonValue_move(document, root);
}

bool XJsonDocument_isArray(const XJsonDocument* document)
{
    if (!document ) 
        return false;
    return document->root.type == XJsonValue_Array;
}

bool XJsonDocument_isObject(const XJsonDocument* document)
{
    if (!document )
        return false;
    return document->root.type == XJsonValue_Object;
}

bool XJsonDocument_isNull(const XJsonDocument* document)
{
    if (!document )
        return false;
    switch (document->root.type)
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
    if (!document )
        return false;
    switch (document->root.type)
    {
    case XJsonValue_Invalid:
    case XJsonValue_Null: return true;
    case XJsonValue_String:return XString_isEmpty_base(document->root.data.string);
    case XJsonValue_Array: return XJsonArray_isEmpty_base(document->root.data.array);
    case XJsonValue_Object:return XJsonObject_isEmpty_base(document->root.data.object);
    default:
        break;
    };
    return false;
}

XJsonObject* XJsonDocument_object(XJsonDocument* document) 
{
    if (!document ) return NULL;

    if (document->root.type != XJsonValue_Object) 
    {
       /* XJsonObject* obj = XJsonObject_create();
        XJsonValue_setObject(document->root, obj);*/
        return NULL;
    }

    return document->root.data.object;
}

XJsonArray* XJsonDocument_array(XJsonDocument* document) 
{
    if (!document ) return NULL;

    if (document->root.type != XJsonValue_Array) {
     /*   XJsonArray* arr = XJsonArray_create();
        XJsonValue_setArray(document->root, arr);
        return arr;*/
        return NULL;
    }

    return document->root.data.array;
}

bool XJsonDocument_setArray(XJsonDocument* document, const XJsonArray* array)
{
    if (!document || !array) 
        return false;
    XJsonValue_setArray(document, array);
    return true;
}

bool XJsonDocument_setObject(XJsonDocument* document, const XJsonObject* object)
{
    if (!document || !object)
        return false;
    XJsonValue_setObject(document, object);
    return true;
}

bool XJsonDocument_setArray_move(XJsonDocument* document, XJsonArray* array)
{
    if (!document || !array)
        return false;
    XJsonValue_setArray_move(document, array);
    return true;
}

bool XJsonDocument_setObject_move(XJsonDocument* document, XJsonObject* object)
{
    if (!document || !object)
        return false;
    XJsonValue_setObject_move(document, object);
    return true;
}
XJsonDocument* XJsonDocument_fromString(const XString* json)
{
    if (!json || XString_isEmpty_base(json)) return NULL;
    XByteArray* buff = XByteArray_create_ex(false);
    //引用
    XContainerDataPtr(buff)= XString_toUtf8(json);
    XContainerSize(buff)= XString_toUtf8_length(json)+1;
    XContainerCapacity(buff) = XContainerSize(buff) ;
    XJsonDocument* doc = XJsonDocument_fromJson(buff);
    XContainerDataPtr(buff) = NULL;
    XContainerSize(buff) =0;
    XContainerCapacity(buff) = 0;
    XByteArray_delete_base(buff);
    return doc;
}

XString* XJsonDocument_toString(const XJsonDocument* document, XJsonDocumentFormat format)
{
    if (!document ) return NULL;
    XByteArray* json = XJsonDocument_toJson(document, format);
    XString* str = XString_create_utf8(XContainerDataAddr(json));
    XByteArray_delete_base(json);
    return str;
}

XJsonDocument* XJsonDocument_fromJson(const XByteArray* json)
{
    if (!json || XByteArray_isEmpty_base(json)) return NULL;

    const char* data = XContainerDataAddr(json);
    const char* end = data + XByteArray_size_base(json)-1;
    const char* ptr = data;

    // 初始化解析栈
    XStack* stack = XStack_create(sizeof(ParseContext));
    if (!stack) return NULL;

    // 解析根值
    XJsonValue* root = Json_parse_value(&ptr, end, stack);
    if (!root) {
        XStack_delete_base(stack);
        return NULL;
    }

    // 检查是否解析完全
    ptr = Json_skip_whitespace(ptr, end);
    if (ptr != end) {
        XJsonValue_delete(root);
        XStack_delete_base(stack);
        return NULL;
    }

    // 创建文档
    XJsonDocument* doc = XJsonDocument_create();
    if (doc) {
        XJsonDocument_setRoot_move(doc, root);
    }
    XJsonValue_delete(root);

    XStack_delete_base(stack);
    return doc;

}

XByteArray* XJsonDocument_toJson(const XJsonDocument* document, XJsonDocumentFormat format)
{
    if (!document ) return NULL;

    // 创建字节数组存储UTF-8结果
    XByteArray* output = XByteArray_create();
    if (!output) return NULL;

    // 创建栈管理嵌套深度（存储int类型的深度值）
    XStack* stack = XStack_create(sizeof(int));
    if (!stack) {
        XByteArray_delete_base(output);
        return NULL;
    }
    XStack_Push_Base(stack, int,0);

    // 根据根节点类型序列化
    switch (document->root.type) {
    case XJsonValue_Object:
        XJsonObject_toByteArray(document->root.data.object, format, stack, output);
        break;
    case XJsonValue_Array:
        XJsonArray_toByteArray(document->root.data.array, format, stack, output);
        break;
    default:
        XJsonValue_toByteArray(document, format, stack, output);
        break;
    }
    //添加结束符号
    XByteArray_push_back_1(output,0);
    // 清理资源
    XStack_delete_base(stack);
    return output;
}

XJsonDocument* XJsonDocument_fromBson_document(const XByteArray* bson)
{
    if(bson==NULL||XByteArray_isEmpty_base(bson))
        return NULL;
    XBsonDocument* doc= XBsonDocument_fromBson(bson);
    if (doc == NULL)
        return NULL;
    XJsonObject* object= XBsonDocument_toJsonObject(doc);
    if (object == NULL)
    {
        XBsonDocument_delete_base(doc);
        return NULL;
    }
    XJsonDocument* jsonDoc = XJsonDocument_create_object_move(object);
    XJsonObject_delete_base(object);
    return jsonDoc;
}

XJsonDocument* XJsonDocument_fromBson_array(const XByteArray* bson)
{
    if (bson == NULL || XByteArray_isEmpty_base(bson))
        return NULL;
    XBsonArray* array = XBsonArray_fromBson(bson);
    if (array == NULL)
        return NULL;
    XJsonArray* jsonArr = XBsonArray_toJsonArray(array);
    if (jsonArr == NULL)
    {
        XBsonArray_delete_base(array);
        return NULL;
    }
    XJsonDocument* jsonDoc = XJsonDocument_create_array_move(jsonArr);
    XJsonArray_delete_base(jsonArr);
    return jsonDoc;
}

XByteArray* XJsonDocument_toBson(const XJsonDocument* document)
{
    if(!document||XJsonDocument_isEmpty(document))
        return NULL;
    XByteArray* bytes = NULL;
    if (XJsonDocument_isObject(document))
    {
       XBsonDocument* bsonDoc= XBsonDocument_fromJsonObject(XJsonDocument_object(document));
       if (bsonDoc)
       {
           bytes = XBsonDocument_toBson(bsonDoc);
           XBsonDocument_delete_base(bsonDoc);
       }
    }
    else if (XJsonDocument_isArray(document))
    {
        XBsonArray* bsonArr = XBsonArray_fromJsonArray(XJsonDocument_array(document));
        if (bsonArr)
        {
            bytes = XBsonArray_toBson(bsonArr);
            XBsonArray_delete_base(bsonArr);
        }
    }
    return bytes;
}

XVariant* XJsonDocument_toVariant(const XJsonDocument* doc)
{
    if (doc == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, sizeof(XJsonDocument), XVariantType_JsonDocument);
    XJsonDocument_init(var->m_data);
    XJsonDocument_copy(var->m_data, doc);
    return var;
}

XVariant* XJsonDocument_toVariant_move(XJsonDocument* doc)
{
    if (doc == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, sizeof(XJsonDocument), XVariantType_JsonDocument);
    XJsonDocument_init(var->m_data);
    XJsonDocument_move(var->m_data, doc);
    return var;
}

XVariant* XJsonDocument_toVariant_ref(XJsonDocument* doc)
{
    if (doc == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, 0, XVariantType_JsonDocument);
    if (var == NULL)
        return NULL;
    var->m_data = doc;
    var->m_dataSize = sizeof(XJsonDocument);
    return var;
}

void XJson_append_escaped_string_byteArray(const XString* str, XByteArray* output)
{
    if (!str || !output) return;

    // 获取UTF-8数据（假设XString提供UTF-8转换接口）
    const char* utf8_str = XString_toUtf8(str);
    size_t len = XString_toUtf8_length(str);

    // 添加引号
    XByteArray_push_back_2(output, "\"", 1);

    // 转义特殊字符
    for (size_t i = 0; i < len; i++) {
        switch (utf8_str[i]) {
        case '"': XByteArray_push_back_2(output, "\\\"", 2); break;
        case '\\': XByteArray_push_back_2(output, "\\\\", 2); break;
        case '\b': XByteArray_push_back_2(output, "\\b", 2); break;
        case '\f': XByteArray_push_back_2(output, "\\f", 2); break;
        case '\n': XByteArray_push_back_2(output, "\\n", 2); break;
        case '\r': XByteArray_push_back_2(output, "\\r", 2); break;
        case '\t': XByteArray_push_back_2(output, "\\t", 2); break;
        default:
            // 直接添加UTF-8字节
            XByteArray_push_back_2(output, &(utf8_str[i]), 1);
            break;
        }
    }

    // 添加结束引号
    XByteArray_push_back_2(output, "\"", 1);
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
        XByteArray_push_back_2(output, space, sizeof(space) - 1);
    }
}

void XJsonObject_toByteArray(const XJsonObject* object, XJsonDocumentFormat format, XStack* stack, XByteArray* output)
{
    if (!object || !stack || !output || XJsonObject_isEmpty_base(object)) 
    {
        XByteArray_push_back_2(output, "{}", 2);
        return;
    }

    // 获取当前深度并压入新深度（+1）
    int new_depth = XStack_Top_Base(stack,int) + 1;
    XStack_push_base(stack, &new_depth);

    // 写入对象开始符
    XByteArray_push_back_2(output, "{", 1);
    if (format == XJsonDocument_Indented) {
        XByteArray_push_back_2(output, "\n", 1);
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
        XByteArray_push_back_2(output, format == XJsonDocument_Indented ? ": " : ":",
            format == XJsonDocument_Indented ? 2 : 1);

        // 写入值
        XJsonValue_toByteArray(value, format, stack, output);

        // 分隔符（最后一个元素不加）
        if (i != key_count - 1) {
            XByteArray_push_back_2(output, ",", 1);
            if (format == XJsonDocument_Indented) {
                XByteArray_push_back_2(output, "\n", 1);
            }
        }
    }

    // 释放键列表
    XVector_delete_base(keys);

    // 恢复深度
    XStack_pop_base(stack);

    // 写入对象结束符
    if (format == XJsonDocument_Indented) {
        XByteArray_push_back_2(output, "\n", 1);
        XJson_append_indent(format, stack, output);
    }
    XByteArray_push_back_2(output, "}", 1);
}

void XJsonArray_toByteArray(const XJsonArray* array, XJsonDocumentFormat format, XStack* stack, XByteArray* output)
{
    if (!array || !stack || !output || XJsonArray_isEmpty_base(array)) {
        XByteArray_push_back_2(output, "[]", 2);
        return;
    }

    // 获取当前深度并压入新深度（+1）
    int new_depth = XStack_Top_Base(stack, int) + 1;
    XStack_push_base(stack, &new_depth);

    // 写入数组开始符
    XByteArray_push_back_2(output, "[", 1);
    if (format == XJsonDocument_Indented) {
        XByteArray_push_back_2(output, "\n", 1);
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
            XByteArray_push_back_2(output, ",", 1);
            if (format == XJsonDocument_Indented) {
                XByteArray_push_back_2(output, "\n", 1);
            }
        }
    }

    // 恢复深度
    XStack_pop_base(stack);

    // 写入数组结束符
    if (format == XJsonDocument_Indented) {
        XByteArray_push_back_2(output, "\n", 1);
        XJson_append_indent(format, stack, output);
    }
    XByteArray_push_back_2(output, "]", 1);
}

void XJsonValue_toByteArray(const XJsonValue* value, XJsonDocumentFormat format, XStack* stack, XByteArray* output)
{
    if (!value || !output) return;

    switch (value->type) {
    case XJsonValue_Null:
        XByteArray_push_back_2(output, "null", 4);
        break;

    case XJsonValue_Bool:
        if (value->data.boolean) {
            XByteArray_push_back_2(output, "true", 4);
        }
        else {
            XByteArray_push_back_2(output, "false", 5);
        }
        break;
    case XJsonValue_Int: {
        char buf[32]="0";
        //snprintf(buf, sizeof(buf), "%" PRId64, value->data.integer);
        int64_to_str(value->data.integer, buf, sizeof(buf));
        XByteArray_append_utf8(output, buf);
        break;
        
    }
    case XJsonValue_Double: {
        // 转换数字为字符串（UTF-8）
        char buffer[64]="0";
        double num = XJsonValue_toDouble(value, 0.0);
        if (num == (long long)num) 
        {
            //snprintf(buffer, sizeof(buffer), "%lld", (long long)num);
            int64_to_str((long long)num, buffer, sizeof(buffer));
        }
        else 
        {
            //snprintf(buffer, sizeof(buffer), "%g", num);
            double_to_str(num, buffer, sizeof(buffer), -1);
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
        XByteArray_push_back_2(output, "null", 4);
        break;
    }
}

const char* Json_skip_whitespace(const char* ptr, const char* end)
{
    while (ptr < end && (isspace((unsigned char)*ptr))) {
        ptr++;
    }
    return ptr;
}

XString* Json_parse_string(const char** ptr, const char* end)
{
    if (*ptr >= end || **ptr != '"') return NULL;
    (*ptr)++; // 跳过开头引号
    const char* start = *ptr;
    XString* str = XString_create();
    XByteArray* buff = XByteArray_create_ex(false);
    while (*ptr < end && **ptr != '"') {
        if (**ptr == '\\') {
            // 处理转义字符
            (*ptr)++;
            if (*ptr >= end) break;

            char escaped = '\0';
            switch (**ptr) {
            case '"':  escaped = '"'; break;
            case '\\': escaped = '\\'; break;
            case '/':  escaped = '/'; break;
            case 'b':  escaped = '\b'; break;
            case 'f':  escaped = '\f'; break;
            case 'n':  escaped = '\n'; break;
            case 'r':  escaped = '\r'; break;
            case 't':  escaped = '\t'; break;
            default:   // 非法转义，忽略
                (*ptr)++;
                continue;
            }
            if (!XByteArray_isEmpty_base(buff))
            {
                XString_append_with_length_utf8(str, XContainerDataPtr(buff), XContainerSize(buff));
                XByteArray_clear_base(buff);
            }
            XString_append_char(str, XChar_from(escaped));
            (*ptr)++;
        }
        else {
            // 直接添加普通字符（UTF-8兼容）
            XByteArray_push_back_1(buff, **ptr);
            //XString_append_char(str, XChar_from(**ptr));
            (*ptr)++;
        }
    }

    if (*ptr >= end || **ptr != '"') {
        XString_delete_base(str); // 未闭合的字符串
        return NULL;
    }
    (*ptr)++; // 跳过结尾引号
    if (!XByteArray_isEmpty_base(buff))
        XString_append_with_length_utf8(str, XContainerDataPtr(buff), XContainerSize(buff));
    XByteArray_delete_base(buff);
    return str;
}

bool Json_parse_number(const char** ptr, const char* end, double* out_num, int64_t* out_int, bool* is_int)
{
    const char* start = *ptr;
    *is_int = true;  // 默认为整数

    // 跳过符号
    if (*ptr < end && (**ptr == '+' || **ptr == '-')) {
        (*ptr)++;
    }

    // 整数部分
    if (*ptr >= end || !isdigit(**ptr)) {
        return false; // 必须有数字
    }
    while (*ptr < end && isdigit(**ptr)) {
        (*ptr)++;
    }

    // 检查是否有小数部分
    if (*ptr < end && **ptr == '.') {
        *is_int = false;  // 有小数点，不是整数
        (*ptr)++;
        if (*ptr >= end || !isdigit(**ptr)) {
            return false; // 小数点后必须有数字
        }
        while (*ptr < end && isdigit(**ptr)) {
            (*ptr)++;
        }
    }

    // 检查指数部分
    if (*ptr < end && (**ptr == 'e' || **ptr == 'E')) {
        *is_int = false;  // 有指数，不是整数
        (*ptr)++;
        if (*ptr < end && (**ptr == '+' || **ptr == '-')) {
            (*ptr)++;
        }
        if (*ptr >= end || !isdigit(**ptr)) {
            return false; // 指数后必须有数字
        }
        while (*ptr < end && isdigit(**ptr)) {
            (*ptr)++;
        }
    }

    // 转换数值
    char buf[64];
    size_t len = *ptr - start;
    if (len >= sizeof(buf)) return false;
    memcpy(buf, start, len);
    buf[len] = '\0';

    if (*is_int) {
        errno = 0;
        *out_int = strtoll(buf, NULL, 10);
        return errno == 0;
    }
    else {
        *out_num = strtod(buf, NULL);
        return true;
    }
}

XJsonValue* Json_parse_keyword(const char** ptr, const char* end)
{
    if (*ptr + 4 <= end && strncmp(*ptr, "true", 4) == 0) {
        *ptr += 4;
        return XJsonValue_create_bool(true);
    }
    else if (*ptr + 5 <= end && strncmp(*ptr, "false", 5) == 0) {
        *ptr += 5;
        return XJsonValue_create_bool(false);
    }
    else if (*ptr + 4 <= end && strncmp(*ptr, "null", 4) == 0) {
        *ptr += 4;
        return XJsonValue_create_null();
    }
    return NULL;
}

XJsonValue* Json_parse_value(const char** ptr, const char* end, XStack* stack)
{
    *ptr = Json_skip_whitespace(*ptr, end);
    if (*ptr >= end) return NULL;

    switch (**ptr) {
    case '{':
        return Json_parse_object(ptr, end, stack);
    case '[':
        return Json_parse_array(ptr, end, stack);
    case '"':
    {
        XString* str = Json_parse_string(ptr, end);
        if (!str) return NULL;
        XJsonValue* val = XJsonValue_create_null();
        XJsonValue_setString_move(val, str);
        XString_delete_base(str);
        return val;
    }
    case 't':
    case 'f':
    case 'n':
        return Json_parse_keyword(ptr, end);
   /* case '-':
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
    {
        double num;
        if (Json_parse_number(ptr, end, &num)) {
            return XJsonValue_create_double(num);
        }
        return NULL;
    }*/
    default:
        if (isdigit(**ptr) || **ptr == '+' || **ptr == '-') {
            double num;
            int64_t int_val;
            bool is_int;
            if (Json_parse_number(ptr, end, &num, &int_val, &is_int)) {
                if (is_int) {
                    return XJsonValue_create_int(int_val);  // 整数类型
                }
                else {
                    return XJsonValue_create_double(num);  // 浮点类型
                }
            }
            return NULL;
        }
        return NULL;
    }
}

XJsonValue* Json_parse_object(const char** ptr, const char* end, XStack* stack)
{
    if (*ptr >= end || **ptr != '{') return NULL;
    (*ptr)++; // 跳过'{'
    XJsonObject* obj = XJsonObject_create();
    if (!obj) return NULL;

    // 压入对象上下文
    ParseContext ctx = {
        .type = CONTEXT_OBJECT,
        .container.object = obj,
        .currentKey = NULL
    };
    XStack_Push_Base(stack, ParseContext, ctx);

    bool expect_key = true;
    while (*ptr < end) {
        *ptr = Json_skip_whitespace(*ptr, end);
        if (*ptr >= end) break;

        if (**ptr == '}') {
            (*ptr)++; // 跳过'}'
            XStack_pop_base(stack); // 弹出上下文
            XJsonValue* value= XJsonValue_create_null();
            XJsonValue_setObject_move(value, obj); // 转移所有权
            XJsonObject_delete_base(obj);
            return value;
        }

        if (expect_key) {
            // 解析键（必须是字符串）
            XString* key = Json_parse_string(ptr, end);
   /*         XPrintf_2(key);
            printf("\n");*/
            if (!key) goto error;

            *ptr = Json_skip_whitespace(*ptr, end);
            if (*ptr >= end || **ptr != ':') {
                XString_delete_base(key);
                goto error;
            }
            (*ptr)++; // 跳过':'
            *ptr = Json_skip_whitespace(*ptr, end);

            // 更新栈顶上下文的当前键
            ParseContext* top = XStack_top_base(stack);
            if (top->currentKey) XString_delete_base(top->currentKey);
            top->currentKey = key;
            expect_key = false;
        }
        else {
            // 解析值
            XJsonValue* value = Json_parse_value(ptr, end, stack);
            if (!value) goto error;

            // 从栈顶获取当前键并插入对象
            ParseContext* top = XStack_top_base(stack);
            if (!top->currentKey) {
                XJsonValue_delete(value);
                goto error;
            }

            XJsonObject_insert_move_base(obj, top->currentKey, value);
            XJsonValue_delete(value);
            XString_delete_base(top->currentKey);
            top->currentKey = NULL;

            *ptr = Json_skip_whitespace(*ptr, end);
            if (*ptr < end && **ptr == ',') {
                (*ptr)++; // 跳过','
                expect_key = true;
            }
            else {
                expect_key = false;
            }
        }
    }

error:
    XJsonObject_delete_base(obj);
    return NULL;
}

XJsonValue* Json_parse_array(const char** ptr, const char* end, XStack* stack)
{
    if (*ptr >= end || **ptr != '[') return NULL;
    (*ptr)++; // 跳过'['
    XJsonArray* arr = XJsonArray_create();
    if (!arr) return NULL;

    // 压入数组上下文
    ParseContext ctx = {
        .type = CONTEXT_ARRAY,
        .container.array = arr,
        .currentKey = NULL
    };
    XStack_Push_Base(stack, ParseContext, ctx);

    bool expect_element = true;
    while (*ptr < end) {
        *ptr = Json_skip_whitespace(*ptr, end);
        if (*ptr >= end) break;

        if (**ptr == ']') {
            (*ptr)++; // 跳过']'
            XStack_pop_base(stack); // 弹出上下文
            XJsonValue* value = XJsonValue_create_null();
            XJsonValue_setArray_move(value, arr); // 转移所有权
            XJsonArray_delete_base(arr);
            return value;
        }

        if (expect_element) {
            // 解析元素值
            XJsonValue* elem = Json_parse_value(ptr, end, stack);
            if (!elem) goto error;

            XJsonArray_append_move_base(arr, elem);
            XJsonValue_delete(elem);
            expect_element = false;

            *ptr = Json_skip_whitespace(*ptr, end);
            if (*ptr < end && **ptr == ',') {
                (*ptr)++; // 跳过','
                expect_element = true;
            }
        }
        else {
            // 多余的逗号
            goto error;
        }
    }

error:
    XJsonArray_delete_base(arr);
    return NULL;
}
