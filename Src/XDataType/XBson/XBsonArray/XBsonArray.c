#include "XBsonArray.h"
#include "XMemory.h"
#include "XStack.h"
#include <string.h>

XBsonArray* XBsonArray_create() 
{
    XBsonArray* array = (XBsonArray*)XMemory_malloc(sizeof(XBsonArray));
    if (array) {
        XBsonArray_init(array);
    }
    return array;
}

XBsonArray* XBsonArray_create_copy(const XBsonArray* other)
{
    if (!other) return NULL;

    XBsonArray* array = XBsonArray_create();
    if (array) {
        XBsonArray_copy_base(array, other);
    }
    return array;
}

XBsonArray* XBsonArray_create_move(XBsonArray* other) 
{
    if (!other) return NULL;

    XBsonArray* array = XBsonArray_create();
    if (array) {
        XBsonArray_move_base(array, other);
    }
    return array;
}

void XBsonArray_init(XBsonArray* array) 
{
    if (!array) return;

    XVector_init(array, sizeof(XBsonValue));
    XContainerSetDataDeinitMethod(array, XBsonValue_deinit);
    XContainerSetDataCopyMethod(array, XBsonValue_copy);
    XContainerSetDataMoveMethod(array, XBsonValue_move);
}

XJsonArray* XBsonArray_to_json_array(const XBsonArray* bson_arr)
{
    if (!bson_arr) return NULL;

  /*  XJsonArray* json_arr = XJsonArray_create();
    if (!json_arr) return NULL;

    for (size_t i = 0; i < XBsonArray_size(bson_arr); i++) {
        const XBsonValue* bson_val = XBsonArray_at_const(bson_arr, i);
        XJsonValue* json_val = XBsonValue_to_json(bson_val);
        if (json_val) {
            XJsonArray_append_base(json_arr, json_val);
        }
    }

    return json_arr;*/
}

void XBsonArray_from_json_array(XBsonArray* bson_arr, const XJsonArray* json_arr) 
{
    if (!bson_arr || !json_arr) return;

    /*XBsonArray_clear(bson_arr);

    for (size_t i = 0; i < XJsonArray_size_base(json_arr); i++) {
        const XJsonValue* json_val = XJsonArray_at_const(json_arr, i);
        XBsonValue* bson_val = XBsonValue_from_json(json_val);
        if (bson_val) {
            XBsonArray_append_move(bson_arr, bson_val);
        }
    }*/
}

XByteArray* XBsonArray_to_bytes(const XBsonArray* array) 
{
    if (!array) return NULL;

    // 先计算总大小
    XByteArray* temp = XByteArray_create(0);

    // 写入元素
    for (size_t i = 0; i < XBsonArray_size_base(array); i++) {
        const XBsonValue* value = XBsonArray_at_base(array, i);
        char key[32];
        sprintf(key, "%zu", i); // 数组元素键为索引字符串
        XBsonValue_serialize(value, key, temp);
    }

    // 添加终止符
    XByteArray_push_back_base(temp, 0x00);

    // 计算总长度并创建最终缓冲区
    uint32_t total_len = (uint32_t)(XByteArray_size_base(temp) + 4); // 加上长度字段
    XByteArray* result = XByteArray_create(total_len);

    uint8_t* write_ptr = XContainerDataPtr(result);
    *((uint32_t*)write_ptr) = total_len;
    //bson_write_uint32(&write_ptr, total_len);
    memcpy(write_ptr+sizeof(uint32_t), XContainerDataPtr(temp), XByteArray_size_base(temp));

    XByteArray_delete_base(temp);
    return result;
}

bool XBsonArray_from_bytes(XBsonArray* array, const uint8_t* data, size_t size) {
    if (!array || !data || size < 5) return false; // 最小BSON数组: 4字节长度 + 1字节终止符

    //XBsonArray_clear(array);

    //const uint8_t* ptr = data;
    //const uint8_t* end = data + size;

    //// 验证长度
    //uint32_t len = bson_read_uint32(&ptr);
    //if (len != size) return false;

    //const uint8_t* content_end = data + len - 1; // 减去终止符

    //// 使用XStack处理递归
    //XStack* stack = XStack_create(sizeof(size_t));
    //if (!stack) return false;

    //// 初始状态: 解析当前数组
    //size_t current_index = 0;
    //XStack_push_base(stack, &current_index);

    //while (!XStack_isEmpty_base(stack) && ptr < content_end) {
    //    current_index = *(size_t*)XStack_top_base(stack);

    //    XString* key = NULL;
    //    XBsonValue* value = XBsonValue_deserialize(&ptr, content_end, &key);
    //    if (!value || !key) {
    //        XBsonValue_delete(value);
    //        XString_delete_base(key);
    //        break;
    //    }

    //    // 验证键是否为当前索引
    //    char expected_key[32];
    //    sprintf(expected_key, "%zu", current_index);
    //    if (strcmp(XString_toUtf8(key), expected_key) != 0) {
    //        XBsonValue_delete(value);
    //        XString_delete_base(key);
    //        break;
    //    }

    //    XString_delete_base(key);

    //    // 添加到数组
    //    XBsonArray_append_move(array, value);

    //    // 更新索引
    //    current_index++;
    //    *(size_t*)XStack_top_base(stack) = current_index;

    //    // 如果是嵌套文档或数组，压栈处理
    //    if (XBsonValue_is_type(value, XBSON_TYPE_DOCUMENT) ||
    //        XBsonValue_is_type(value, XBSON_TYPE_ARRAY)) {
    //        size_t new_index = 0;
    //        XStack_push_base(stack, &new_index);
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
    //    XBsonArray_clear(array);
    //    return false;
    //}

    return true;
}