#include "XBsonDocument.h"
#include "XMemory.h"
#include "XString.h"
#include "XStack.h"
#include <string.h>

XBsonDocument* XBsonDocument_create() {
    XBsonDocument* object = (XBsonDocument*)XMemory_malloc(sizeof(XBsonDocument));
    if (object) {
        XBsonDocument_init(object);
    }
    return object;
}

XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other) {
    if (!other) return NULL;

    XBsonDocument* object = XBsonDocument_create();
    if (object) {
        XBsonDocument_copy_base(object, other);
    }
    return object;
}

XBsonDocument* XBsonDocument_create_move(XBsonDocument* other) {
    if (!other) return NULL;

    XBsonDocument* object = XBsonDocument_create();
    if (object) {
        XBsonDocument_move_base(object, other);
    }
    return object;
}

void XBsonDocument_init(XBsonDocument* object) {
    if (!object) return;

    XMap_init(object, sizeof(XString), sizeof(XBsonValue),
        XEquality_XString, XLess_XString);

    XMapBaseSetKeyCopyMethod(object, XString_copy_base);
    XMapBaseSetKeyMoveMethod(object, XString_move_base);
    XMapBaseSetKeyDeinitMethod(object, XString_deinit_base);

    XContainerSetDataCopyMethod(object, XJsonValue_copy);
    XContainerSetDataMoveMethod(object, XJsonValue_move);
    XContainerSetDataDeinitMethod(object, XJsonValue_deinit);
}

bool XBsonDocument_insert_keyUtf8_value(XBsonDocument* object, const char* key, XBsonValue* value)
{
    if (object == NULL || key == NULL || value == NULL)
        return false;
    XString_Init_Utf8(str, key);
    bool ret = XMap_insert_keyMove_base(object, str, value);
    XString_deinit_base(str);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_value_move(XBsonDocument* object, const char* key, XBsonValue* value)
{
    if (object == NULL || key == NULL || value == NULL)
        return false;
    XString_Init_Utf8(str, key);
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_double(XBsonDocument* object, const char* key, double d)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_DOUBLE);
    value->data.dbl = d;
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_int32(XBsonDocument* object, const char* key, int32_t i)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_INT32);
    value->data.int32 = i;
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_int64(XBsonDocument* object, const char* key, int64_t i)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_INT64);
    value->data.int64 = i;
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_string(XBsonDocument* object, const char* key, const XString* strValue)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_STRING);
    value->data.str = XString_create_copy(strValue);
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_string_move(XBsonDocument* object, const char* key, XString* strValue)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_STRING);
    value->data.str = XString_create_move(strValue);
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_utf8(XBsonDocument* object, const char* key, const char* utf8)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_STRING);
    value->data.str = XString_create_utf8(utf8);
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_null(XBsonDocument* object, const char* key)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_NULL);
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_bool(XBsonDocument* object, const char* key, bool b)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_BOOLEAN);
    value->data.boolean = b;
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_array(XBsonDocument* object, const char* key, const XBsonArray* array)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_ARRAY);
    value->data.arr = XByteArray_create_copy(array);
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_array_move(XBsonDocument* object, const char* key, XBsonArray* array)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_ARRAY);
    value->data.arr = XByteArray_create_move(array);
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_object(XBsonDocument* object, const char* key, const XBsonDocument* doc)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_DOCUMENT);
    value->data.doc = XBsonDocument_create_copy(doc);
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_insert_keyUtf8_object_move(XBsonDocument* object, const char* key, XBsonDocument* doc)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XBsonValue_Init(value, XBSON_TYPE_DOCUMENT);
    value->data.doc = XBsonDocument_create_move(doc);
    bool ret = XBsonDocument_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XBsonValue_deinit(value);
    return ret;
}

bool XBsonDocument_remove_keyUtf8(XBsonDocument* object, const char* key)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    bool ret = XBsonDocument_remove_base(object, str);
    XString_deinit_base(str);
    return ret;
}

XJsonObject* XBsonDocument_to_json_object(const XBsonDocument* bson_obj) {
    if (!bson_obj) return NULL;

    /*XJsonObject* json_obj = XJsonObject_create();
    if (!json_obj) return NULL;

    XMapIterator it = XMap_begin(&bson_obj->members);
    while (!XMapIterator_isEnd(&it)) {
        const XString* key = *(const XString**)XMapIterator_key(&it);
        const XBsonValue* bson_val = *(const XBsonValue**)XMapIterator_value(&it);

        XJsonValue* json_val = XBsonValue_to_json(bson_val);
        if (json_val) {
            XJsonObject_insert_keyUtf8_value_move(json_obj, XString_toUtf8(key), json_val);
        }

        XMapIterator_next(&it);
    }

    return json_obj;*/
}

void XBsonDocument_from_json_object(XBsonDocument* bson_obj, const XJsonObject* json_obj) {
    if (!bson_obj || !json_obj) return;

   /* XBsonDocument_clear(bson_obj);

    XMapIterator it = XMap_begin(&json_obj->members);
    while (!XMapIterator_isEnd(&it)) {
        const XString* key = *(const XString**)XMapIterator_key(&it);
        const XJsonValue* json_val = *(const XJsonValue**)XMapIterator_value(&it);

        XBsonValue* bson_val = XBsonValue_from_json(json_val);
        if (bson_val) {
            XBsonDocument_insert_move(bson_obj, XString_toUtf8(key), bson_val);
        }

        XMapIterator_next(&it);
    }*/
}

XByteArray* XBsonDocument_to_bytes(const XBsonDocument* object) {
    if (!object) return NULL;

    // 先计算总大小
    XByteArray* temp = XByteArray_create(0);

    //// 写入成员
    //XMapIterator it = XMap_begin(&object->members);
    //while (!XMapIterator_isEnd(&it)) {
    //    const XString* key = *(const XString**)XMapIterator_key(&it);
    //    const XBsonValue* value = *(const XBsonValue**)XMapIterator_value(&it);

    //    XBsonValue_serialize(value, XString_toUtf8(key), temp);
    //    XMapIterator_next(&it);
    //}

    //// 添加终止符
    //XByteArray_push_back_base(temp, 0x00);

    //// 计算总长度并创建最终缓冲区
    //uint32_t total_len = (uint32_t)(XByteArray_size_base(temp) + 4); // 加上长度字段
    //XByteArray* result = XByteArray_create(total_len);

    //uint8_t* write_ptr = XContainerDataPtr(result);
    //bson_write_uint32(&write_ptr, total_len);
    //memcpy(write_ptr, XContainerDataPtr(temp), XByteArray_size_base(temp));

    //XByteArray_delete_base(temp);
    //return result;
}

bool XBsonDocument_from_bytes(XBsonDocument* object, const uint8_t* data, size_t size) {
    if (!object || !data || size < 5) return false; // 最小BSON对象: 4字节长度 + 1字节终止符

    //XBsonDocument_clear(object);

    //const uint8_t* ptr = data;
    //const uint8_t* end = data + size;

    //// 验证长度
    //uint32_t len = bson_read_uint32(&ptr);
    //if (len != size) return false;

    //const uint8_t* content_end = data + len - 1; // 减去终止符

    //// 使用XStack处理递归
    //XStack* stack = XStack_create(sizeof(XBsonDocument*));
    //if (!stack) return false;

    //// 初始状态: 解析当前对象
    //XStack_push_base(stack, &object);

    //while (!XStack_isEmpty_base(stack) && ptr < content_end) {
    //    XBsonDocument* current_obj = *(XBsonDocument**)XStack_top_base(stack);

    //    XString* key = NULL;
    //    XBsonValue* value = XBsonValue_deserialize(&ptr, content_end, &key);
    //    if (!value || !key) {
    //        XBsonValue_delete(value);
    //        XString_delete_base(key);
    //        break;
    //    }

    //    // 添加到当前对象
    //    XBsonDocument_insert_move(current_obj, XString_toUtf8(key), value);
    //    XString_delete_base(key);

    //    // 如果是嵌套文档，压栈处理
    //    if (XBsonValue_is_type(value, XBSON_TYPE_DOCUMENT)) {
    //        XStack_push_base(stack, &value->data.doc->object);
    //    }
    //    else if (ptr < content_end && **ptr == 0x00) {
    //        // 遇到终止符，出栈
    //        XStack_pop_base(stack);
    //        ptr++;
    //    }
    //}

    //XStack_delete_base(stack);

    //// 确保解析到正确的终止符
    //if (ptr != content_end || *ptr != 0x00) {
    //    XBsonDocument_clear(object);
    //    return false;
    //}

    return true;
}