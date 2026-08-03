#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XBsonDocument.h"
#include "XBsonArray.h"
#include "XByteArray.h"
#include "XString.h"
#include "XVariantList.h"
#include "XStack.h"
#include "XMemory.h"
#include "XVariantTypeOps.h"
#include "XNumStrConv.h"
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

XVARIANT_TYPE_OPS_DEFINE(XJsonDocument, sizeof(XJsonDocument), XJsonDocument_copy,
	XJsonDocument_move, XJsonDocument_clear, XJsonDocument_deinit,
	NULL, "XJsonDocument");
typedef struct JsonParser
{
    const char* data;
    const char* ptr;
    const char* end;
    XJsonParseError* error;
    size_t depth;
    XStack* stack;
} JsonParser;

/* 保留现有解析栈模型：容器本体仍由 XJsonObject/XJsonArray 持有，栈只记录上下文。 */
typedef enum {
    CONTEXT_OBJECT,
    CONTEXT_ARRAY
} ContextType;

typedef struct {
    ContextType type;
    union {
        XJsonObject* object;
        XJsonArray* array;
    } container;
    XString* currentKey;
} ParseContext;

static void Json_set_error(JsonParser* parser, XJsonParseErrorCode code);
static void Json_skip_whitespace(JsonParser* parser);
static XString* Json_parse_string(JsonParser* parser);
static XJsonValue* Json_parse_value(JsonParser* parser);
static XJsonValue* Json_parse_object(JsonParser* parser);
static XJsonValue* Json_parse_array(JsonParser* parser);
static bool Json_parse_number(JsonParser* parser, XJsonValue** result);
static bool Json_append_codepoint(XByteArray* bytes, uint32_t codepoint);
static bool Json_append_codepoint_string(XString* string, uint32_t codepoint);
static bool Json_flush_string_bytes(XString* string, XByteArray* bytes);

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
    if (!doc)
        return NULL;
    XJsonDocument_init(doc);
    Set_Class_MemoryFree(doc, XFree_System);
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
        XJsonValue_setObject(&doc->root, object);
    }
    return doc;
}

XJsonDocument* XJsonDocument_create_object_move(XJsonObject* object)
{
    if (!object) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc)
    {
        XJsonValue_setObject_move(&doc->root, object);
    }
    return doc;
}

XJsonDocument* XJsonDocument_create_array(XJsonArray* array) {
    if (!array) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc)
    {
        XJsonValue_setArray(&doc->root, array);
    }
    return doc;
}

XJsonDocument* XJsonDocument_create_array_move(XJsonArray* array)
{
    if (!array) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc)
    {
        XJsonValue_setArray_move(&doc->root, array);
    }
    return doc;
}

void XJsonDocument_init(XJsonDocument* document)
{
    if (document == NULL)
        return;
    XJsonValue_init(&document->root, XJsonValue_Invalid);
}

void XJsonDocument_deinit(XJsonDocument* document)
{
    if (!document) return;
    XJsonValue_deinit(&document->root);
}

void XJsonDocument_delete(XJsonDocument* document)
{
    if (!document)
        return;
    XJsonDocument_deinit(document);
    XFree_System(document);
}

void XJsonDocument_clear(XJsonDocument* document)
{
    if (document) {
        XJsonValue_deinit(&document->root);
        XJsonValue_init(&document->root, XJsonValue_Invalid);
    }
}

void XJsonDocument_copy(XJsonDocument* doc, const XJsonDocument* src)
{
    if (doc == NULL || src == NULL)
        return;
    if (doc == src)
        return;
    XJsonValue_copy(&doc->root, &src->root);
}

void XJsonDocument_move(XJsonDocument* doc, XJsonDocument* src)
{
    if (doc == NULL || src == NULL)
        return;
    if (doc == src)
        return;
    XJsonValue_move(&doc->root, &src->root);
    /* A moved-from document is an empty document, matching isEmpty/isNull. */
    src->root.type = XJsonValue_Invalid;
}

XJsonValue* XJsonDocument_root(XJsonDocument* document) 
{
    return document ? &document->root : NULL;
}

const XJsonValue* XJsonDocument_root_const(const XJsonDocument* document) 
{
    return document ? &((XJsonDocument*)document)->root : NULL;
}

void XJsonDocument_setRoot(XJsonDocument* document,const XJsonValue* root) 
{
    if (!document || !root) return;
    XJsonValue_copy(&document->root, root);
}

void XJsonDocument_setRoot_move(XJsonDocument* document, XJsonValue* root)
{
    if (!document || !root) return;
    XJsonValue_move(&document->root, root);
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
    return document && document->root.type == XJsonValue_Invalid;
}

bool XJsonDocument_isEmpty(const XJsonDocument* document)
{
    if (!document )
        return false;
    return document->root.type == XJsonValue_Invalid;
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
    XJsonValue_setArray(&document->root, array);
    return true;
}

bool XJsonDocument_setObject(XJsonDocument* document, const XJsonObject* object)
{
    if (!document || !object)
        return false;
    XJsonValue_setObject(&document->root, object);
    return true;
}

bool XJsonDocument_setArray_move(XJsonDocument* document, XJsonArray* array)
{
    if (!document || !array)
        return false;
    XJsonValue_setArray_move(&document->root, array);
    return true;
}

bool XJsonDocument_setObject_move(XJsonDocument* document, XJsonObject* object)
{
    if (!document || !object)
        return false;
    XJsonValue_setObject_move(&document->root, object);
    return true;
}
XJsonDocument* XJsonDocument_fromString(const XString* json)
{
    return XJsonDocument_fromString_ex(json, NULL);
}

XJsonDocument* XJsonDocument_fromString_ex(const XString* json, XJsonParseError* error)
{
    XByteArray* bytes;
    XJsonDocument* doc;
    if (!json)
        return NULL;
    bytes = XByteArray_create_with_data(XString_toUtf8(json), XString_toUtf8_length(json));
    if (!bytes)
        return NULL;
    doc = XJsonDocument_fromJson_ex(bytes, error);
    XByteArray_delete_base(bytes);
    return doc;
}

XString* XJsonDocument_toString(const XJsonDocument* document, XJsonDocumentFormat format)
{
    if (!document ) return NULL;
    XByteArray* json = XJsonDocument_toJson(document, format);
    XString* str;
    if (!json)
        return NULL;
    str = XString_create();
    if (str)
        XString_append_with_length_utf8(str, XContainerDataAddr(json), XByteArray_size_base(json));
    XByteArray_delete_base(json);
    return str;
}

XJsonDocument* XJsonDocument_fromJson(const XByteArray* json)
{
    return XJsonDocument_fromJson_ex(json, NULL);
}

XJsonDocument* XJsonDocument_fromJson_ex(const XByteArray* json, XJsonParseError* error)
{
    JsonParser parser;
    XStack* stack;
    XJsonValue* root;
    XJsonDocument* document;
    size_t size;
    const unsigned char* data;
    if (error)
        XJsonParseError_init(error);
    if (!json)
        return NULL;
    size = XByteArray_size_base(json);
    if (size > INT32_MAX) {
        if (error) {
            error->offset = 0;
            error->error = XJsonParseError_DocumentTooLarge;
        }
        return NULL;
    }
    data = (const unsigned char*)XByteArray_data((XByteArray*)json);
    if (size && data && data[size - 1] == 0)
        --size;
    if (size == 0 || !data) {
        if (error) {
            error->offset = 0;
            error->error = XJsonParseError_IllegalValue;
        }
        return NULL;
    }
    parser.data = (const char*)data;
    parser.ptr = parser.data;
    parser.end = parser.data + size;
    parser.error = error;
    parser.depth = 0;
    parser.stack = NULL;
    if (size >= 3 && (unsigned char)parser.ptr[0] == 0xef &&
        (unsigned char)parser.ptr[1] == 0xbb && (unsigned char)parser.ptr[2] == 0xbf)
        parser.ptr += 3;
    Json_skip_whitespace(&parser);
    if (parser.ptr >= parser.end || (*parser.ptr != '{' && *parser.ptr != '[')) {
        Json_set_error(&parser, XJsonParseError_IllegalValue);
        return NULL;
    }
    stack = XStack_create(sizeof(ParseContext));
    if (!stack)
        return NULL;
    parser.stack = stack;
    root = Json_parse_value(&parser);
    Json_skip_whitespace(&parser);
    if (!root || parser.ptr != parser.end) {
        if (root)
            XJsonValue_delete(root);
        if (parser.ptr != parser.end)
            Json_set_error(&parser, XJsonParseError_GarbageAtEnd);
        XStack_delete_base(stack);
        return NULL;
    }
    document = XJsonDocument_create();
    if (document)
        XJsonDocument_setRoot_move(document, root);
    XJsonValue_delete(root);
    XStack_delete_base(stack);
    return document;
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

    if (document->root.type == XJsonValue_Invalid) {
        XStack_delete_base(stack);
        return output;
    }

    // 根据根节点类型序列化
    switch (document->root.type) {
    case XJsonValue_Object:
        XJsonObject_toByteArray(document->root.data.object, format, stack, output);
        break;
    case XJsonValue_Array:
        XJsonArray_toByteArray(document->root.data.array, format, stack, output);
        break;
    default:
        XJsonValue_toByteArray(&document->root, format, stack, output);
        break;
    }
    if (format == XJsonDocument_Indented && XContainerSize(output) > 0)
        XByteArray_push_back_1(output, '\n');
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
    XBsonDocument_delete_base(doc);
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
    XBsonArray_delete_base(array);
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
    XVariantMap* map;
    XVariantList* list;
    if (!doc || doc->root.type == XJsonValue_Invalid)
        return XVariant_create_null();
    if (doc->root.type == XJsonValue_Object) {
        map = XJsonObject_toVariantMap(doc->root.data.object);
        if (!map)
            return NULL;
        {
            XVariant* variant = XVariant_create_map_move(map);
            XMap_delete_base((XClass*)map);
            return variant;
        }
    }
    if (doc->root.type == XJsonValue_Array) {
        list = XJsonArray_toVariantList(doc->root.data.array);
        if (!list)
            return NULL;
        {
            XVariant* variant = XVariant_create_list_move(list);
            XClass_delete_base((XClass*)list);
            return variant;
        }
    }
    return XJsonValue_toVariant(&doc->root);
}

XVariant* XJsonDocument_toVariant_move(XJsonDocument* doc)
{
    XVariant* var = XJsonDocument_toVariant(doc);
    if (doc)
        XJsonDocument_clear(doc);
    return var;
}

XJsonDocument* XJsonDocument_fromVariant(const XVariant* variant)
{
    XJsonDocument* document = NULL;
    XJsonArray* array = NULL;
    XJsonObject* object = NULL;
    if (!variant)
        return NULL;
    switch ((XVariantType)variant->m_type) {
    case XVariantType_List:
        array = XJsonArray_fromVariantList(XVariant_toList_ref(variant));
        document = array ? XJsonDocument_create_array_move(array) : NULL;
        if (array) XJsonArray_delete_base(array);
        return document;
    case XVariantType_Map:
        object = XJsonObject_fromVariantMap(XVariant_toMap_ref(variant));
        document = object ? XJsonDocument_create_object_move(object) : NULL;
        if (object) XJsonObject_delete_base(object);
        return document;
    case XVariantType_JsonDocument:
        return XJsonDocument_create_copy(XVariant_toJsonDocument_ref(variant));
    case XVariantType_JsonArray:
        return XJsonDocument_create_array(XVariant_toJsonArray_ref(variant));
    case XVariantType_JsonObject:
        return XJsonDocument_create_object((XJsonObject*)XVariant_toJsonObject_ref(variant));
    case XVariantType_JsonValue: {
        XJsonValue* value = XVariant_toJsonValue(variant);
        if (!value) return NULL;
        document = XJsonDocument_create();
        if (document) XJsonDocument_setRoot_move(document, value);
        XJsonValue_delete(value);
        return document;
    }
    default:
        return NULL;
    }
}

XVariant* XJsonDocument_toVariant_ref(XJsonDocument* doc)
{
    if (doc == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, 0, XVariantType_JsonDocument);
    if (var == NULL)
        return NULL;
	XVariant_setDataRef(var, doc, sizeof(XJsonDocument), XVariantType_JsonDocument);
    return var;
}

XJsonDocument* XJsonDocument_fromVariant_copy(const XVariant* variant)
{
    XJsonDocument* source = (XJsonDocument*)XVariant_toRef(variant, XVariantType_JsonDocument);
    return source ? XJsonDocument_create_copy(source) : NULL;
}

XJsonDocument* XJsonDocument_fromVariant_ref(const XVariant* variant)
{
    return (XJsonDocument*)XVariant_toRef(variant, XVariantType_JsonDocument);
}

static bool XJsonDocument_prepareVariant(XVariant* variant)
{
    if (!variant)
        return false;
    if (variant->m_type != XVariantType_JsonDocument ||
        !variant->m_data || variant->m_dataSize != sizeof(XJsonDocument)) {
        if (variant->m_data)
            XVariant_deinit_base(variant);
        variant->m_data = XMalloc_System(sizeof(XJsonDocument));
        if (!variant->m_data)
            return false;
        variant->m_dataSize = sizeof(XJsonDocument);
        XJsonDocument_init((XJsonDocument*)variant->m_data);
        variant->m_type = XVariantType_JsonDocument;
    }
    return true;
}

void XJsonDocument_setVariant(XVariant* variant, const XJsonDocument* document)
{
    if (document && XJsonDocument_prepareVariant(variant))
        XJsonDocument_copy((XJsonDocument*)variant->m_data, document);
}

void XJsonDocument_setVariant_move(XVariant* variant, XJsonDocument* document)
{
    if (document && XJsonDocument_prepareVariant(variant))
        XJsonDocument_move((XJsonDocument*)variant->m_data, document);
}

void XJsonDocument_setVariant_ref(XVariant* variant, XJsonDocument* document)
{
	if (!variant || !document)
		return;
	XVariant_setDataRef(variant, document, sizeof(XJsonDocument), XVariantType_JsonDocument);
}

void XJson_append_escaped_string_byteArray(const XString* str, XByteArray* output)
{
    const XChar* chars;
    size_t length;
    size_t index;
    if (!str || !output) return;
    chars = XString_constData(str);
    length = XString_length_base(str);
    XByteArray_push_back_2(output, "\"", 1);
    for (index = 0; index < length; ++index) {
        uint32_t codepoint = chars[index];
        if (XChar_isHighSurrogate(chars[index]) && index + 1 < length &&
            XChar_isLowSurrogate(chars[index + 1])) {
            XChar high = chars[index];
            XChar low = chars[index + 1];
            codepoint = XChar_surrogateToUcs4(high, low);
            ++index;
        }
        if (codepoint == '"') XByteArray_push_back_2(output, "\\\"", 2);
        else if (codepoint == '\\') XByteArray_push_back_2(output, "\\\\", 2);
        else if (codepoint == '\b') XByteArray_push_back_2(output, "\\b", 2);
        else if (codepoint == '\f') XByteArray_push_back_2(output, "\\f", 2);
        else if (codepoint == '\n') XByteArray_push_back_2(output, "\\n", 2);
        else if (codepoint == '\r') XByteArray_push_back_2(output, "\\r", 2);
        else if (codepoint == '\t') XByteArray_push_back_2(output, "\\t", 2);
        else if (codepoint < 0x20 || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)codepoint);
            XByteArray_push_back_2(output, escaped, 6);
        } else {
            Json_append_codepoint(output, codepoint);
        }
    }
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
        char buffer[64];
        double num = XJsonValue_toDouble(value, 0.0);
        if (!isfinite(num)) {
            XByteArray_append_utf8(output, "null");
            break;
        }
        snprintf(buffer, sizeof(buffer), "%.17g", num == 0.0 ? 0.0 : num);
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

void XJsonParseError_init(XJsonParseError* error)
{
    if (error) {
        error->offset = -1;
        error->error = XJsonParseError_NoError;
    }
}

const char* XJsonParseError_errorString(const XJsonParseError* error)
{
    if (!error) return "No error";
    switch (error->error) {
    case XJsonParseError_NoError: return "no error occurred";
    case XJsonParseError_UnterminatedObject: return "unterminated object";
    case XJsonParseError_MissingNameSeparator: return "missing name separator";
    case XJsonParseError_UnterminatedArray: return "unterminated array";
    case XJsonParseError_MissingValueSeparator: return "missing value separator";
    case XJsonParseError_IllegalValue: return "illegal value";
    case XJsonParseError_TerminationByNumber: return "terminated by number";
    case XJsonParseError_IllegalNumber: return "illegal number";
    case XJsonParseError_IllegalEscapeSequence: return "illegal escape sequence";
    case XJsonParseError_IllegalUtf8String: return "illegal UTF-8 string";
    case XJsonParseError_UnterminatedString: return "unterminated string";
    case XJsonParseError_MissingObject: return "missing object";
    case XJsonParseError_DeepNesting: return "too deeply nested";
    case XJsonParseError_DocumentTooLarge: return "document too large";
    case XJsonParseError_GarbageAtEnd: return "garbage at end of document";
    default: return "unknown error";
    }
}

static void Json_set_error(JsonParser* parser, XJsonParseErrorCode code)
{
    if (parser && parser->error && parser->error->error == XJsonParseError_NoError) {
        parser->error->offset = (int64_t)(parser->ptr - parser->data);
        parser->error->error = code;
    }
}

static void Json_skip_whitespace(JsonParser* parser)
{
    while (parser && parser->ptr < parser->end &&
           (*parser->ptr == ' ' || *parser->ptr == '\t' ||
            *parser->ptr == '\n' || *parser->ptr == '\r'))
        ++parser->ptr;
}

static int Json_hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool Json_append_codepoint(XByteArray* bytes, uint32_t codepoint)
{
    char encoded[4];
    size_t length;
    if (codepoint <= 0x7f) {
        encoded[0] = (char)codepoint;
        length = 1;
    } else if (codepoint <= 0x7ff) {
        encoded[0] = (char)(0xc0 | (codepoint >> 6));
        encoded[1] = (char)(0x80 | (codepoint & 0x3f));
        length = 2;
    } else if (codepoint <= 0xffff) {
        encoded[0] = (char)(0xe0 | (codepoint >> 12));
        encoded[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[2] = (char)(0x80 | (codepoint & 0x3f));
        length = 3;
    } else if (codepoint <= 0x10ffff) {
        encoded[0] = (char)(0xf0 | (codepoint >> 18));
        encoded[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
        encoded[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[3] = (char)(0x80 | (codepoint & 0x3f));
        length = 4;
    } else {
        return false;
    }
    return XByteArray_push_back_2(bytes, encoded, length);
}

static bool Json_append_codepoint_string(XString* string, uint32_t codepoint)
{
    if (!string || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
        return false;
    if (codepoint <= 0xffff)
        return XString_append_char(string, (XChar)codepoint);
    return XString_append_char(string, (XChar)(0xd800 + ((codepoint - 0x10000) >> 10))) &&
           XString_append_char(string, (XChar)(0xdc00 + ((codepoint - 0x10000) & 0x3ff)));
}

static bool Json_flush_string_bytes(XString* string, XByteArray* bytes)
{
    size_t length;
    if (!string || !bytes)
        return false;
    length = XByteArray_size_base(bytes);
    if (length && !XString_append_with_length_utf8(string, XContainerDataAddr(bytes), length))
        return false;
    XByteArray_clear_base(bytes);
    return true;
}

static XString* Json_parse_string(JsonParser* parser)
{
    XByteArray* bytes;
    XString* string;
    if (!parser || parser->ptr >= parser->end || *parser->ptr != '"') {
        Json_set_error(parser, XJsonParseError_IllegalValue);
        return NULL;
    }
    ++parser->ptr;
    bytes = XByteArray_create_ex(false);
    string = XString_create();
    if (!bytes || !string) {
        if (bytes) XByteArray_delete_base(bytes);
        if (string) XString_delete_base(string);
        return NULL;
    }
    while (parser->ptr < parser->end) {
        unsigned char c = (unsigned char)*parser->ptr++;
        if (c == '"') {
            if (!Json_flush_string_bytes(string, bytes))
                goto string_error;
            XByteArray_delete_base(bytes);
            return string;
        }
        if (c < 0x20) {
            Json_set_error(parser, XJsonParseError_IllegalUtf8String);
            break;
        }
        if (c == '\\') {
            uint32_t codepoint;
            int high;
            if (parser->ptr >= parser->end) {
                Json_set_error(parser, XJsonParseError_UnterminatedString);
                break;
            }
            c = (unsigned char)*parser->ptr++;
            if (!Json_flush_string_bytes(string, bytes))
                goto string_error;
            switch (c) {
            case '"': case '\\': case '/':
                if (!XString_append_char(string, (XChar)c)) goto string_error;
                break;
            case 'b': if (!XString_append_char(string, (XChar)'\b')) goto string_error; break;
            case 'f': if (!XString_append_char(string, (XChar)'\f')) goto string_error; break;
            case 'n': if (!XString_append_char(string, (XChar)'\n')) goto string_error; break;
            case 'r': if (!XString_append_char(string, (XChar)'\r')) goto string_error; break;
            case 't': if (!XString_append_char(string, (XChar)'\t')) goto string_error; break;
            case 'u':
                if (parser->end - parser->ptr < 4) {
                    Json_set_error(parser, XJsonParseError_IllegalEscapeSequence);
                    goto string_error;
                }
                codepoint = 0;
                for (high = 0; high < 4; ++high) {
                    int digit = Json_hex_value(parser->ptr[high]);
                    if (digit < 0) {
                        Json_set_error(parser, XJsonParseError_IllegalEscapeSequence);
                        goto string_error;
                    }
                    codepoint = (codepoint << 4) | (uint32_t)digit;
                }
                parser->ptr += 4;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    uint32_t low = 0;
                    if (parser->end - parser->ptr >= 6 && parser->ptr[0] == '\\' && parser->ptr[1] == 'u') {
                        for (high = 0; high < 4; ++high) {
                            int digit = Json_hex_value(parser->ptr[2 + high]);
                            if (digit < 0) {
                                Json_set_error(parser, XJsonParseError_IllegalEscapeSequence);
                                goto string_error;
                            }
                            low = (low << 4) | (uint32_t)digit;
                        }
                        if (low >= 0xdc00 && low <= 0xdfff) {
                            parser->ptr += 6;
                            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                        }
                    }
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdfff) {
                    if (!XString_append_char(string, (XChar)codepoint)) goto string_error;
                } else if (!Json_append_codepoint_string(string, codepoint)) {
                    goto string_error;
                }
                break;
            default:
                Json_set_error(parser, XJsonParseError_IllegalEscapeSequence);
                goto string_error;
            }
            continue;
        }
        if (c < 0x80) {
            if (!XByteArray_push_back_1(bytes, c)) goto string_error;
        } else {
            size_t length = c < 0xe0 ? 2 : (c < 0xf0 ? 3 : 4);
            uint32_t codepoint = c & (length == 2 ? 0x1f : (length == 3 ? 0x0f : 0x07));
            size_t i;
            if ((length == 2 && c < 0xc2) || (length == 3 && c < 0xe0) ||
                (length == 4 && c > 0xf4) || parser->end - parser->ptr < (ptrdiff_t)(length - 1)) {
                Json_set_error(parser, XJsonParseError_IllegalUtf8String);
                break;
            }
            for (i = 1; i < length; ++i) {
                unsigned char continuation = (unsigned char)parser->ptr[i - 1];
                if ((continuation & 0xc0) != 0x80) {
                    Json_set_error(parser, XJsonParseError_IllegalUtf8String);
                    goto string_error;
                }
                codepoint = (codepoint << 6) | (continuation & 0x3f);
            }
            if ((length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000) ||
                (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) {
                Json_set_error(parser, XJsonParseError_IllegalUtf8String);
                goto string_error;
            }
            if (!Json_flush_string_bytes(string, bytes) ||
                !Json_append_codepoint_string(string, codepoint)) goto string_error;
            parser->ptr += length - 1;
        }
    }
    if (parser->ptr >= parser->end && (!parser->error || parser->error->error == XJsonParseError_NoError))
        Json_set_error(parser, XJsonParseError_UnterminatedString);
string_error:
    XByteArray_delete_base(bytes);
    XString_delete_base(string);
    return NULL;
}

static bool Json_parse_number(JsonParser* parser, XJsonValue** result)
{
    const char* start;
    size_t length;
    char buffer[128];
    bool integer = true;
    errno = 0;
    start = parser->ptr;
    if (parser->ptr < parser->end && *parser->ptr == '-') ++parser->ptr;
    if (parser->ptr >= parser->end || !isdigit((unsigned char)*parser->ptr)) {
        Json_set_error(parser, XJsonParseError_IllegalNumber);
        return false;
    }
    if (*parser->ptr == '0') {
        ++parser->ptr;
        if (parser->ptr < parser->end && isdigit((unsigned char)*parser->ptr)) {
            Json_set_error(parser, XJsonParseError_IllegalNumber);
            return false;
        }
    } else {
        while (parser->ptr < parser->end && isdigit((unsigned char)*parser->ptr)) ++parser->ptr;
    }
    if (parser->ptr < parser->end && *parser->ptr == '.') {
        integer = false;
        ++parser->ptr;
        while (parser->ptr < parser->end && isdigit((unsigned char)*parser->ptr)) ++parser->ptr;
    }
    if (parser->ptr < parser->end && (*parser->ptr == 'e' || *parser->ptr == 'E')) {
        integer = false;
        ++parser->ptr;
        if (parser->ptr < parser->end && (*parser->ptr == '+' || *parser->ptr == '-')) ++parser->ptr;
        if (parser->ptr >= parser->end || !isdigit((unsigned char)*parser->ptr)) {
            Json_set_error(parser, XJsonParseError_IllegalNumber);
            return false;
        }
        while (parser->ptr < parser->end && isdigit((unsigned char)*parser->ptr)) ++parser->ptr;
    }
    length = (size_t)(parser->ptr - start);
    if (!length || length >= sizeof(buffer)) {
        Json_set_error(parser, XJsonParseError_IllegalNumber);
        return false;
    }
    memcpy(buffer, start, length);
    buffer[length] = '\0';
    if (integer) {
        char* endptr;
        long long integer_value = strtoll(buffer, &endptr, 10);
        if (errno == 0 && *endptr == '\0')
            *result = XJsonValue_create_int((int64_t)integer_value);
        else {
            double number = strtod(buffer, &endptr);
            if (errno == ERANGE || !isfinite(number) || *endptr != '\0') {
                Json_set_error(parser, XJsonParseError_IllegalNumber);
                return false;
            }
            *result = XJsonValue_create_double(number);
        }
    } else {
        double number = strtod(buffer, NULL);
        if (errno == ERANGE || !isfinite(number)) {
            Json_set_error(parser, XJsonParseError_IllegalNumber);
            return false;
        }
        *result = XJsonValue_create_double(number);
    }
    return *result != NULL;
}

static XJsonValue* Json_parse_value(JsonParser* parser)
{
    XJsonValue* value = NULL;
    XString* string;
    Json_skip_whitespace(parser);
    if (!parser || parser->ptr >= parser->end) {
        Json_set_error(parser, XJsonParseError_IllegalValue);
        return NULL;
    }
    switch (*parser->ptr) {
    case '{': return Json_parse_object(parser);
    case '[': return Json_parse_array(parser);
    case '"':
        string = Json_parse_string(parser);
        if (!string) return NULL;
        value = XJsonValue_create_null();
        if (value) XJsonValue_setString_move(value, string);
        XString_delete_base(string);
        return value;
    case 't':
        if (parser->end - parser->ptr >= 4 && memcmp(parser->ptr, "true", 4) == 0) {
            parser->ptr += 4;
            return XJsonValue_create_bool(true);
        }
        break;
    case 'f':
        if (parser->end - parser->ptr >= 5 && memcmp(parser->ptr, "false", 5) == 0) {
            parser->ptr += 5;
            return XJsonValue_create_bool(false);
        }
        break;
    case 'n':
        if (parser->end - parser->ptr >= 4 && memcmp(parser->ptr, "null", 4) == 0) {
            parser->ptr += 4;
            return XJsonValue_create_null();
        }
        break;
    default:
        if (*parser->ptr == '-' || isdigit((unsigned char)*parser->ptr)) {
            if (Json_parse_number(parser, &value)) return value;
            return NULL;
        }
        break;
    }
    Json_set_error(parser, XJsonParseError_IllegalValue);
    return NULL;
}

static XJsonValue* Json_parse_object(JsonParser* parser)
{
    XJsonObject* object;
    XJsonValue* value;
    bool value_is_number;
    ParseContext context;
    Json_skip_whitespace(parser);
    if (!parser || parser->ptr >= parser->end || *parser->ptr != '{') return NULL;
    if (++parser->depth > 1024) {
        Json_set_error(parser, XJsonParseError_DeepNesting);
        --parser->depth;
        return NULL;
    }
    ++parser->ptr;
    object = XJsonObject_create();
    if (!object) goto fail_depth;
    context.type = CONTEXT_OBJECT;
    context.container.object = object;
    context.currentKey = NULL;
    XStack_Push_Base(parser->stack, ParseContext, context);
    Json_skip_whitespace(parser);
    if (parser->ptr < parser->end && *parser->ptr == '}') goto object_done;
    if (parser->ptr >= parser->end) {
        Json_set_error(parser, XJsonParseError_UnterminatedObject);
        goto object_error;
    }
    for (;;) {
        XString* key;
        Json_skip_whitespace(parser);
        if (parser->ptr >= parser->end) {
            Json_set_error(parser, XJsonParseError_UnterminatedObject);
            goto object_error;
        }
        if (*parser->ptr == '}') {
            Json_set_error(parser, XJsonParseError_MissingObject);
            goto object_error;
        }
        key = Json_parse_string(parser);
        if (!key) {
            Json_set_error(parser, XJsonParseError_MissingNameSeparator);
            goto object_error;
        }
        ((ParseContext*)XStack_top_base(parser->stack))->currentKey = key;
        Json_skip_whitespace(parser);
        if (parser->ptr >= parser->end || *parser->ptr != ':') {
            Json_set_error(parser, XJsonParseError_MissingNameSeparator);
            goto object_error;
        }
        ++parser->ptr;
        Json_skip_whitespace(parser);
        if (parser->ptr >= parser->end) {
            Json_set_error(parser, XJsonParseError_UnterminatedObject);
            goto object_error;
        }
        if (*parser->ptr == '}') {
            Json_set_error(parser, XJsonParseError_MissingObject);
            goto object_error;
        }
        value = Json_parse_value(parser);
        if (!value) goto object_error;
        value_is_number = value->type == XJsonValue_Int || value->type == XJsonValue_Double;
        key = ((ParseContext*)XStack_top_base(parser->stack))->currentKey;
        if (!XJsonObject_insert_value_move(object, key, value)) {
            XJsonValue_delete(value);
            goto object_error;
        }
        XJsonValue_delete(value);
        XString_delete_base(key);
        ((ParseContext*)XStack_top_base(parser->stack))->currentKey = NULL;
        Json_skip_whitespace(parser);
        if (parser->ptr < parser->end && *parser->ptr == '}') goto object_done;
        if (parser->ptr >= parser->end) {
            Json_set_error(parser, value_is_number ? XJsonParseError_TerminationByNumber : XJsonParseError_IllegalValue);
            goto object_error;
        }
        if (*parser->ptr != ',') {
            Json_set_error(parser, XJsonParseError_MissingValueSeparator);
            goto object_error;
        }
        ++parser->ptr;
        Json_skip_whitespace(parser);
        if (parser->ptr >= parser->end) {
            Json_set_error(parser, XJsonParseError_UnterminatedObject);
            goto object_error;
        }
        if (*parser->ptr == '}') {
            Json_set_error(parser, XJsonParseError_MissingObject);
            goto object_error;
        }
    }
object_done:
    XStack_pop_base(parser->stack);
    --parser->depth;
    ++parser->ptr;
    value = XJsonValue_create_null();
    if (value) XJsonValue_setObject_move(value, object);
    XJsonObject_delete_base(object);
    return value;
object_error:
    if (parser->stack && XStack_size_base(parser->stack) > 0) {
        ParseContext* top = XStack_top_base(parser->stack);
        if (top && top->currentKey) {
            XString_delete_base(top->currentKey);
            top->currentKey = NULL;
        }
        XStack_pop_base(parser->stack);
    }
    XJsonObject_delete_base(object);
fail_depth:
    --parser->depth;
    return NULL;
}

static XJsonValue* Json_parse_array(JsonParser* parser)
{
    XJsonArray* array;
    XJsonValue* value;
    bool value_is_number;
    ParseContext context;
    Json_skip_whitespace(parser);
    if (!parser || parser->ptr >= parser->end || *parser->ptr != '[') return NULL;
    if (++parser->depth > 1024) {
        Json_set_error(parser, XJsonParseError_DeepNesting);
        --parser->depth;
        return NULL;
    }
    ++parser->ptr;
    array = XJsonArray_create();
    if (!array) goto fail_depth;
    context.type = CONTEXT_ARRAY;
    context.container.array = array;
    context.currentKey = NULL;
    XStack_Push_Base(parser->stack, ParseContext, context);
    Json_skip_whitespace(parser);
    if (parser->ptr < parser->end && *parser->ptr == ']') goto array_done;
    if (parser->ptr >= parser->end) {
        Json_set_error(parser, XJsonParseError_UnterminatedArray);
        goto array_error;
    }
    for (;;) {
        Json_skip_whitespace(parser);
        if (parser->ptr >= parser->end) {
            Json_set_error(parser, XJsonParseError_UnterminatedArray);
            goto array_error;
        }
        if (*parser->ptr == ']') {
            Json_set_error(parser, XJsonParseError_MissingObject);
            goto array_error;
        }
        value = Json_parse_value(parser);
        if (!value) goto array_error;
        value_is_number = value->type == XJsonValue_Int || value->type == XJsonValue_Double;
        if (!XJsonArray_append_move_base(array, value)) {
            XJsonValue_delete(value);
            goto array_error;
        }
        XJsonValue_delete(value);
        Json_skip_whitespace(parser);
        if (parser->ptr < parser->end && *parser->ptr == ']') goto array_done;
        if (parser->ptr >= parser->end) {
            Json_set_error(parser, value_is_number ? XJsonParseError_TerminationByNumber : XJsonParseError_IllegalValue);
            goto array_error;
        }
        if (*parser->ptr != ',') {
            Json_set_error(parser, XJsonParseError_MissingValueSeparator);
            goto array_error;
        }
        ++parser->ptr;
        Json_skip_whitespace(parser);
        if (parser->ptr >= parser->end) {
            Json_set_error(parser, XJsonParseError_UnterminatedArray);
            goto array_error;
        }
        if (*parser->ptr == ']') {
            Json_set_error(parser, XJsonParseError_MissingObject);
            goto array_error;
        }
    }
array_done:
    XStack_pop_base(parser->stack);
    --parser->depth;
    ++parser->ptr;
    value = XJsonValue_create_null();
    if (value) XJsonValue_setArray_move(value, array);
    XJsonArray_delete_base(array);
    return value;
array_error:
    if (parser->stack && XStack_size_base(parser->stack) > 0)
        XStack_pop_base(parser->stack);
    XJsonArray_delete_base(array);
fail_depth:
    --parser->depth;
    return NULL;
}

#if 0 /* 旧接口签名保留作迁移参考；实际解析路径使用上面的 ParseContext 实现。 */
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
#endif
