#include "XJsonDocument.h"
#include "XJsonValue.h"
#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XByteArray.h"
#include "XString.h"
#include "XStack.h"
#include "XMemory.h"
#include <ctype.h>
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
static const char* skip_whitespace(const char* ptr, const char* end);
// 辅助函数：解析字符串（处理转义字符）
static XString* parse_string(const char** ptr, const char* end);
// 辅助函数：解析数字
static bool parse_number(const char** ptr, const char* end, double* out);
// 辅助函数：解析关键字（true/false/null）
static XJsonValue* parse_keyword(const char** ptr, const char* end);
// 解析对象
static XJsonValue* parse_object(const char** ptr, const char* end, XStack* stack);
// 解析数组
static XJsonValue* parse_array(const char** ptr, const char* end, XStack* stack);
// 解析值（调度到对应类型的解析函数）
static XJsonValue* parse_value(const char** ptr, const char* end, XStack* stack);

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

XJsonDocument* XJsonDocument_fromJson(const XByteArray* json)
{
    if (!json || XByteArray_isEmpty_base(json)) return NULL;

    const char* data = XContainerDataPtr(json);
    const char* end = data + XByteArray_size_base(json)-1;
    const char* ptr = data;

    // 初始化解析栈
    XStack* stack = XStack_create(sizeof(ParseContext));
    if (!stack) return NULL;

    // 解析根值
    XJsonValue* root = parse_value(&ptr, end, stack);
    if (!root) {
        XStack_delete_base(stack);
        return NULL;
    }

    // 检查是否解析完全
    ptr = skip_whitespace(ptr, end);
    if (ptr != end) {
        XJsonValue_delete(root);
        XStack_delete_base(stack);
        return NULL;
    }

    // 创建文档
    XJsonDocument* doc = XJsonDocument_create();
    if (doc) {
        XJsonDocument_setRoot(doc, root);
    }
    else {
        XJsonValue_delete(root);
    }

    XStack_delete_base(stack);
    return doc;

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

const char* skip_whitespace(const char* ptr, const char* end)
{
    while (ptr < end && (isspace((unsigned char)*ptr))) {
        ptr++;
    }
    return ptr;
}

XString* parse_string(const char** ptr, const char* end)
{
    if (*ptr >= end || **ptr != '"') return NULL;
    (*ptr)++; // 跳过开头引号
    const char* start = *ptr;
    XString* str = XString_create(NULL);
    XByteArray* buff = XByteArray_create(0);
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
            XByteArray_push_back_base(buff, **ptr);
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

bool parse_number(const char** ptr, const char* end, double* out)
{
    if (!ptr || !*ptr || !end || !out || *ptr >= end) {
        return false;
    }

    const char* start = *ptr;
    char* endptr;

    // 让strtod处理转换，同时获取有效数字的结束位置
    errno = 0;
    *out = strtod(start, &endptr);

    // 检查是否有有效数字被解析
    if (endptr == start) {
        return false;
    }

    // 检查是否超出输入范围
    if (endptr > end) {
        return false;
    }

    // 检查转换错误（溢出等）
    if (errno != 0) {
        return false;
    }

    // 验证解析出的数字是否符合JSON规范
    const char* current = start;
    const char* num_end = endptr;

    // 1. 检查符号
    if (*current == '+' || *current == '-') {
        current++;
        if (current >= num_end) { // 不能只有符号
            return false;
        }
    }

    // 2. 检查整数部分或小数部分的起始
    bool has_digits = false;
    if (isdigit((unsigned char)*current)) {
        has_digits = true;
        // 检查前导零（仅允许单独的0）
        if (*current == '0') {
            current++;
            if (current < num_end && isdigit((unsigned char)*current)) {
                return false; // 不允许"0123"这样的前导零
            }
        }
        else {
            // 跳过其他数字
            while (current < num_end && isdigit((unsigned char)*current)) {
                current++;
            }
        }
    }

    // 3. 检查小数部分
    if (current < num_end && *current == '.') {
        current++;
        // 小数点后必须有数字
        if (current >= num_end || !isdigit((unsigned char)*current)) {
            return false;
        }
        has_digits = true;
        // 跳过小数部分数字
        while (current < num_end && isdigit((unsigned char)*current)) {
            current++;
        }
    }

    // 必须有数字部分
    if (!has_digits) {
        return false;
    }

    // 4. 检查指数部分
    if (current < num_end && (*current == 'e' || *current == 'E')) {
        current++;
        // 指数符号（可选）
        if (current < num_end && (*current == '+' || *current == '-')) {
            current++;
        }
        // 指数后必须有数字
        if (current >= num_end || !isdigit((unsigned char)*current)) {
            return false;
        }
        // 跳过指数部分数字
        while (current < num_end && isdigit((unsigned char)*current)) {
            current++;
        }
    }

    // 确保所有字符都被验证（没有多余字符）
    if (current != num_end) {
        return false;
    }

    // 更新指针位置
    *ptr = endptr;
    return true;
}

XJsonValue* parse_keyword(const char** ptr, const char* end)
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

XJsonValue* parse_value(const char** ptr, const char* end, XStack* stack)
{
    *ptr = skip_whitespace(*ptr, end);
    if (*ptr >= end) return NULL;

    switch (**ptr) {
    case '{':
        return parse_object(ptr, end, stack);
    case '[':
        return parse_array(ptr, end, stack);
    case '"':
    {
        XString* str = parse_string(ptr, end);
        if (!str) return NULL;
        XJsonValue* val = XJsonValue_create_null();
        XJsonValue_setString_move(val, str);
        return val;
    }
    case 't':
    case 'f':
    case 'n':
        return parse_keyword(ptr, end);
    case '-':
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
    {
        double num;
        if (parse_number(ptr, end, &num)) {
            return XJsonValue_create_double(num);
        }
        return NULL;
    }
    default:
        return NULL;
    }
}

XJsonValue* parse_object(const char** ptr, const char* end, XStack* stack)
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
        *ptr = skip_whitespace(*ptr, end);
        if (*ptr >= end) break;

        if (**ptr == '}') {
            (*ptr)++; // 跳过'}'
            XStack_pop_base(stack); // 弹出上下文
            XJsonValue* value= XJsonValue_create_null();
            XJsonValue_setObject_move(value, obj); // 转移所有权
            //XJsonObject_delete_base(obj);
            return value;
        }

        if (expect_key) {
            // 解析键（必须是字符串）
            XString* key = parse_string(ptr, end);
   /*         XPrint(key);
            printf("\n");*/
            if (!key) goto error;

            *ptr = skip_whitespace(*ptr, end);
            if (*ptr >= end || **ptr != ':') {
                XString_delete_base(key);
                goto error;
            }
            (*ptr)++; // 跳过':'
            *ptr = skip_whitespace(*ptr, end);

            // 更新栈顶上下文的当前键
            ParseContext* top = XStack_top_base(stack);
            if (top->currentKey) XString_delete_base(top->currentKey);
            top->currentKey = key;
            expect_key = false;
        }
        else {
            // 解析值
            XJsonValue* value = parse_value(ptr, end, stack);
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

            *ptr = skip_whitespace(*ptr, end);
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

XJsonValue* parse_array(const char** ptr, const char* end, XStack* stack)
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
        *ptr = skip_whitespace(*ptr, end);
        if (*ptr >= end) break;

        if (**ptr == ']') {
            (*ptr)++; // 跳过']'
            XStack_pop_base(stack); // 弹出上下文
            XJsonValue* value = XJsonValue_create_null();
            XJsonValue_setArray_move(value, arr); // 转移所有权
            return value;
        }

        if (expect_element) {
            // 解析元素值
            XJsonValue* elem = parse_value(ptr, end, stack);
            if (!elem) goto error;

            XJsonArray_append_move_base(arr, elem);
            XJsonValue_delete(elem);
            expect_element = false;

            *ptr = skip_whitespace(*ptr, end);
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
