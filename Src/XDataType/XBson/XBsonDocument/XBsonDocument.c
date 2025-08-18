#include "XBsonDocument.h"
#include "XBsonArray.h"
#include "XJsonObject.h"
#include "XMemory.h"
#include "XString.h"
#include "XStack.h"
#include <string.h>

XBsonDocument* XBsonDocument_create() {
    XBsonDocument* doc = (XBsonDocument*)XMemory_malloc(sizeof(XBsonDocument));
    if (doc) {
        XBsonDocument_init(doc);
    }
    return doc;
}

XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other) {
    if (!other) return NULL;

    XBsonDocument* doc = XBsonDocument_create();
    if (doc) {
        XBsonDocument_copy_base(doc, other);
    }
    return doc;
}

XBsonDocument* XBsonDocument_create_move(XBsonDocument* other) {
    if (!other) return NULL;

    XBsonDocument* doc = XBsonDocument_create();
    if (doc) {
        XBsonDocument_move_base(doc, other);
    }
    return doc;
}

void XBsonDocument_init(XBsonDocument* doc) {
    if (!doc) return;

    XMap_init(doc, sizeof(XString), sizeof(XBsonValue),
        XEquality_XString, XLess_XString);

    XMapBaseSetKeyCopyMethod(doc, XString_copy_base);
    XMapBaseSetKeyMoveMethod(doc, XString_move_base);
    XMapBaseSetKeyDeinitMethod(doc, XString_deinit_base);

    XContainerSetDataCopyMethod(doc, XBsonValue_copy);
    XContainerSetDataMoveMethod(doc, XBsonValue_move);
    XContainerSetDataDeinitMethod(doc, XBsonValue_deinit);
}

bool XBsonDocument_insert_keyUtf8_value(XBsonDocument* doc, const char* key, XBsonValue* value)
{
    if (doc == NULL || key == NULL || value == NULL)
        return false;
    XString_Init_Utf8(str, key);
    bool ret = XMap_insert_keyMove_base(doc, str, value);
    XString_deinit_base(str);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_value_move(XBsonDocument* doc, const char* key, XBsonValue* value)
{
    if (doc == NULL || key == NULL || value == NULL)
        return false;
    XString_Init_Utf8(str, key);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_double(XBsonDocument* doc, const char* key, double d)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_DOUBLE);
    value->data.dbl = d;
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_int32(XBsonDocument* doc, const char* key, int32_t i)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_INT32);
    value->data.int32 = i;
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_int64(XBsonDocument* doc, const char* key, int64_t i)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_INT64);
    value->data.int64 = i;
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_string(XBsonDocument* doc, const char* key, const XString* strValue)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_STRING);
    if (value->data.str)
        XString_copy_base(value->data.str, strValue);
    else
        value->data.str = XString_create_copy(strValue);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_string_move(XBsonDocument* doc, const char* key, XString* strValue)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_STRING);
    if (value->data.str)
        XString_move_base(value->data.str,strValue);
    else
        value->data.str = XString_create_move(strValue);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_utf8(XBsonDocument* doc, const char* key, const char* utf8)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_STRING);
    if (value->data.str)
        XString_assign_utf8(value->data.str,utf8);
    else
        value->data.str = XString_create_utf8(utf8);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_null(XBsonDocument* doc, const char* key)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_NULL);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_bool(XBsonDocument* doc, const char* key, bool b)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_BOOLEAN);
    value->data.boolean = b;
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_array(XBsonDocument* doc, const char* key, const XBsonArray* array)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_ARRAY);
    if (value->data.arr)
        XBsonArray_copy_base(value->data.arr,array);
    else
        value->data.arr = XBsonArray_create_copy(array);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_array_move(XBsonDocument* doc, const char* key, XBsonArray* array)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_ARRAY);
    if (value->data.arr)
        XBsonArray_move_base(value->data.arr, array);
    else
        value->data.arr = XBsonArray_create_move(array);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_document(XBsonDocument* doc, const char* key, const XBsonDocument* newDoc)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_DOCUMENT);
    if (value->data.doc)
        XBsonDocument_copy_base(value->data.doc, newDoc);
    else
        value->data.doc = XBsonDocument_create_copy(newDoc);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_document_move(XBsonDocument* doc, const char* key, XBsonDocument* newDoc)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_DOCUMENT);
    if (value->data.doc)
        XBsonDocument_move_base(value->data.doc, newDoc);
    else
        value->data.doc = XBsonDocument_create_move(newDoc);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_value_move(XBsonDocument* doc, const XString* key, XBsonValue* value)
{
    if (doc == NULL || key == NULL || value == NULL)
        return false;
    XString_Init_Utf8(str, NULL);
    XString_assign(str,key);
    bool ret = XBsonDocument_insert_move_base(doc, str, value);
    XString_deinit_base(str);
    return ret;
}

bool XBsonDocument_remove_keyUtf8(XBsonDocument* doc, const char* key)
{
    if (doc == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    bool ret = XBsonDocument_remove_base(doc, str);
    XString_deinit_base(str);
    return ret;
}

XJsonObject* XBsonDocument_to_json_object(const XBsonDocument* bson_obj) 
{
    if (!bson_obj) return NULL;

    XJsonObject* json_obj = XJsonObject_create();
    if (!json_obj) return NULL;

    /*XMapIterator it = XMap_begin(&bson_obj->members);
    while (!XMapIterator_isEnd(&it)) {*/
    for_each_iterator(bson_obj,XMap,it)
    {
        XPair* pair = XMap_iterator_data(&it);
        const XString* key = XPair_first(pair);
        const XBsonValue* bson_val = XPair_second(pair);

        XJsonValue* json_val = XBsonValue_to_json(bson_val);
        if (json_val) {
            XJsonObject_insert_value_move(json_obj, key, json_val);
            XJsonValue_delete(json_val);
        }

    }

    return json_obj;
}

void XBsonDocument_from_json_object(XBsonDocument* bson_obj, const XJsonObject* json_obj) {
    if (!bson_obj || !json_obj) return;

    XBsonDocument_clear_base(bson_obj);

   /* XMapIterator it = XMap_begin(&json_obj->members);
    while (!XMapIterator_isEnd(&it)) {*/
    for_each_iterator(json_obj, XMap, it)
    {
        XPair* pair = XMap_iterator_data(&it);
        const XString* key = XPair_first(pair);
        const XJsonValue* json_val = XPair_second(pair);

        XBsonValue* bson_val = XBsonValue_from_json(json_val);
        if (bson_val)
        {
            XBsonDocument_insert_value_move(bson_obj, key, bson_val);
            XBsonValue_delete(bson_val);
        }
    }
}

XByteArray* XBsonDocument_toJson(const XBsonDocument* bson_doc, XJsonDocumentFormat format)
{
    if (!bson_doc) return NULL;

    // 1. 将BSON文档转换为JSON对象
    XJsonObject* json_obj = XBsonDocument_to_json_object(bson_doc);
    if (!json_obj) return NULL;

    // 2. 将JSON对象转换为JSON字符串
    XByteArray* json = XJsonObject_toJson(json_obj, format);

    // 3. 释放中间JSON对象（XJsonObject_toString不会接管所有权）
    XJsonObject_delete_base(json_obj);

    return json;
}

XString* XBsonDocument_toJson_string(const XBsonDocument* bson_doc, XJsonDocumentFormat format)
{
    if (!bson_doc) return NULL;

    // 1. 将BSON文档转换为JSON对象
    XJsonObject* json_obj = XBsonDocument_to_json_object(bson_doc);
    if (!json_obj) return NULL;

    // 2. 将JSON对象转换为JSON字符串
    XString* json = XJsonObject_toString(json_obj, format);

    // 3. 释放中间JSON对象（XJsonObject_toString不会接管所有权）
    XJsonObject_delete_base(json_obj);

    return json;
}

XByteArray* XBsonDocument_to_Bson(const XBsonDocument* doc) 
{
    if (!doc) return NULL;

    // 先计算总大小
    XByteArray* temp = XByteArray_create(0);

    // 写入成员
    for_each_iterator(doc,XMap,it)
    {
        XPair* pair= XMap_iterator_data(&it);
        const XString* key = XPair_first(pair);
        const XBsonValue* value =XPair_second(pair);

        XBsonValue_serialize(value, XString_toUtf8(key), temp);
    }

    // 添加终止符
    XByteArray_push_back_base(temp, 0x00);

    // 计算总长度并创建最终缓冲区
    uint32_t total_len = (uint32_t)(XByteArray_size_base(temp) + 4); // 加上长度字段
    XByteArray* result = XByteArray_create(total_len);

    uint8_t* write_ptr = XContainerDataPtr(result);
    //bson_write_uint32(&write_ptr, total_len);
    *((uint32_t*)write_ptr) = total_len;
    write_ptr += sizeof(uint32_t);
    memcpy(write_ptr, XContainerDataPtr(temp), XByteArray_size_base(temp));

    XByteArray_delete_base(temp);
    return result;
}

XBsonDocument* XBsonDocument_from_Bson(XByteArray* data) {
    if (!data || XContainerSize(data) < 5) return NULL; // 最小BSON对象: 4字节长度 + 1字节终止符

    XBsonDocument* doc = XBsonDocument_create();
    if (!XBsonDocument_from_bytes(doc, XContainerDataPtr(data), XContainerSize(data)))
    {
        XBsonDocument_delete_base(doc);
        return NULL;
    }
    return doc;
}

bool XBsonDocument_from_bytes(XBsonDocument* doc, const uint8_t* data, size_t size)
{
    if (!doc || !data || size < 5) return false; // 最小BSON对象: 4字节长度 + 1字节终止符

    XBsonDocument_clear_base(doc);

    const uint8_t* ptr = data;
    const uint8_t* end = data + size;

    // 验证长度
    uint32_t len = *((uint32_t*)ptr);
    ptr += sizeof(uint32_t);
    if (len != size) return false;

    const uint8_t* content_end = data + len - 1; // 减去终止符

    // 使用XStack处理递归
    XStack* stack = XStack_create(sizeof(XBsonDocument*));
    if (!stack) return false;

    // 初始状态: 解析当前对象
    XStack_push_base(stack, &doc);

    while (!XStack_isEmpty_base(stack) && ptr < content_end) {
        XBsonDocument* current_obj = *(XBsonDocument**)XStack_top_base(stack);

        XString* key = NULL;
        XBsonValue* value = XBsonValue_deserialize(&ptr, content_end, &key);
        if (!value || !key) {
            XBsonValue_delete(value);
            XString_delete_base(key);
            break;
        }

        // 添加到当前对象
        XBsonDocument_insert_move_base(current_obj, key, value);
        XString_delete_base(key);
        XBsonValue_delete(value);

        // 如果是嵌套文档，压栈处理
        if (XBsonValue_is_type(value, XBSON_TYPE_DOCUMENT)) {
            XStack_push_base(stack, &(value->data.doc));
        }
        else if (ptr < content_end && *ptr == 0x00) {
            // 遇到终止符，出栈
            XStack_pop_base(stack);
            ptr++;
        }
    }

    XStack_delete_base(stack);

    // 确保解析到正确的终止符
    if (ptr != content_end || *ptr != 0x00) {
        XBsonDocument_clear_base(doc);
        return false;
    }

    return true;
}