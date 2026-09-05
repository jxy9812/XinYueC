#include "XBsonArray.h"
#include "XJsonArray.h"
#include "XMemory.h"
#include "XVariantTypeOps.h"
#include "XVariantList.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

XVARIANT_TYPE_OPS_DEFINE(XBsonArray, sizeof(XBsonArray), XClass_copy_base,
	XClass_move_base, XBsonArray_clear_base, XBsonArray_deinit_base,
	NULL, "XBsonArray");

/* BSON 数组以 XVector 保存值，序列化时键必须为连续十进制索引。 */
XBsonArray* XBsonArray_create_ex(XMemoryType memory)
{
    XBsonArray* array = (XBsonArray*)XMemory_malloc(sizeof(XBsonArray), memory);
	if (!array) return NULL;
	XBsonArray_init(array);
	Set_Class_Memory(array, memory); Set_Class_IsHeap(array, true);
	return array;
}

XBsonArray* XBsonArray_create_copy(const XBsonArray* other)
{
	if (!other) return NULL;
	XBsonArray* array = XBsonArray_create();
	if (!array) return NULL;
	XCopy(array, other);
	return array;
}

XBsonArray* XBsonArray_create_move(XBsonArray* other)
{
	if (!other) return NULL;
	XBsonArray* array = XBsonArray_create();
	if (!array) return NULL;
	XMove(array, other);
	return array;
}

void XBsonArray_init(XBsonArray* array)
{
	if (!array) return;
	XVector_init((XVector*)array, sizeof(XBsonValue), true);
	XContainerSetDataDeinitMethod(array, XBsonValue_deinit);
	XContainerSetDataCopyMethod(array, XBsonValue_copy);
	XContainerSetDataMoveMethod(array, XBsonValue_move);
}

XJsonArray* XBsonArray_toJsonArray(const XBsonArray* bson_arr)
{
	if (!bson_arr) return NULL;
	XJsonArray* json_arr = XJsonArray_create();
	if (!json_arr) return NULL;
	for (size_t i = 0; i < XBsonArray_size_base(bson_arr); ++i) {
		const XBsonValue* bson_val = XBsonArray_at_base(bson_arr, (int64_t)i);
		XJsonValue* json_val = XBsonValue_to_json(bson_val);
		if (!json_val || !XJsonArray_append_move_base(json_arr, json_val)) {
			XJsonValue_delete(json_val);
			XJsonArray_delete_base(json_arr);
			return NULL;
		}
		XJsonValue_delete(json_val);
	}
	return json_arr;
}

/* JSON 数组按原顺序转换为 BSON 数组。 */
XBsonArray* XBsonArray_fromJsonArray(const XJsonArray* json_arr)
{
	if (!json_arr) return NULL;
	XBsonArray* bson_arr = XBsonArray_create();
	if (!bson_arr) return NULL;
	for (size_t i = 0; i < XJsonArray_size_base(json_arr); ++i) {
		const XJsonValue* json_val = XJsonArray_at_const(json_arr, (int64_t)i);
		XBsonValue* bson_val = XBsonValue_from_json(json_val);
		if (!bson_val || !XBsonArray_append_move_base(bson_arr, bson_val)) {
			XBsonValue_delete(bson_val);
			XBsonArray_delete_base(bson_arr);
			return NULL;
		}
		XBsonValue_delete(bson_val);
	}
	return bson_arr;
}

XByteArray* XBsonArray_toBson(const XBsonArray* array)
{
	return XBsonArray_to_bytes(array);
}

XBsonArray* XBsonArray_fromBson(XByteArray* data)
{
	if (!data || XByteArray_size_base(data) < 5) return NULL;
	XBsonArray* array = XBsonArray_create();
	if (!array) return NULL;
	if (!XBsonArray_from_bytes(array, XContainerDataAddr(data),
		XByteArray_size_base(data))) {
		XBsonArray_delete_base(array);
		return NULL;
	}
	return array;
}

XByteArray* XBsonArray_to_bytes(const XBsonArray* array)
{
	if (!array) return NULL;
	XByteArray* bytes = XByteArray_create();
	if (!bytes || !XByteArray_resize_base(bytes, 4)) {
		XByteArray_delete_base(bytes);
		return NULL;
	}
	for (size_t i = 0; i < XBsonArray_size_base(array); ++i) {
		const XBsonValue* value = XBsonArray_at_base(array, (int64_t)i);
		char key[32];
		int length = snprintf(key, sizeof(key), "%zu", i);
		if (length <= 0 || (size_t)length >= sizeof(key) ||
			!XBsonValue_serialize(value, key, bytes)) {
			XByteArray_delete_base(bytes);
			return NULL;
		}
	}
	if (!XByteArray_push_back_1(bytes, 0x00) ||
		XByteArray_size_base(bytes) > INT32_MAX) {
		XByteArray_delete_base(bytes);
		return NULL;
	}
	uint32_t size = (uint32_t)XByteArray_size_base(bytes);
	XMemory_write_data(XContainerDataAddr(bytes), XBYTE_ORDER_LITTLE_ENDIAN,
		&size, sizeof(size));
	return bytes;
}

/* 数组反序列化必须验证长度、终止符和连续键名。 */
bool XBsonArray_from_bytes(XBsonArray* array, const uint8_t* data, size_t size)
{
	if (!array) return false;
	XBsonArray_clear_base(array);
	if (!data || size < 5 || size > INT32_MAX) return false;
	uint32_t length = 0;
	XMemory_read_data(data, XBYTE_ORDER_LITTLE_ENDIAN, &length, sizeof(length));
	if (length != size || length < 5 || data[length - 1] != 0x00) return false;

	const uint8_t* ptr = data + sizeof(uint32_t);
	const uint8_t* end = data + length - 1;
	size_t expected_index = 0;
	while (ptr < end) {
		XString* key = NULL;
		XBsonValue* value = XBsonValue_deserialize(&ptr, end, &key);
		char expected[32];
		int expected_length = snprintf(expected, sizeof(expected), "%zu", expected_index);
		bool valid_key = value && key && expected_length > 0 &&
			(size_t)expected_length < sizeof(expected) &&
			strcmp(XString_toUtf8(key), expected) == 0;
		if (!valid_key || !XBsonArray_append_move_base(array, value)) {
			XString_delete_base(key);
			XBsonValue_delete(value);
			XBsonArray_clear_base(array);
			return false;
		}
		XString_delete_base(key);
		XBsonValue_delete(value);
		++expected_index;
	}
	if (ptr != end) {
		XBsonArray_clear_base(array);
		return false;
	}
	return true;
}

XVariantList* XBsonArray_toVariantList(const XBsonArray* arr)
{
	if (!arr) return NULL;
	XVariantList* list = XVariantList_create();
	if (!list) return NULL;
	for (size_t i = 0; i < XBsonArray_size_base(arr); ++i) {
		const XBsonValue* value = XBsonArray_at_base(arr, (int64_t)i);
		XVariant* var = XBsonValue_toVariant(value);
		if (!var || !XVariantList_push_back_move_base(list, var)) {
			XVariant_delete_base(var);
			XVariantList_delete_base(list);
			return NULL;
		}
		XVariant_delete_base(var);
	}
	return list;
}

XVariantList* XBsonArray_toVariantList_move(XBsonArray* arr)
{
	if (!arr) return NULL;
	XVariantList* list = XVariantList_create();
	if (!list) return NULL;
	for (size_t i = 0; i < XBsonArray_size_base(arr); ++i) {
		XBsonValue* value = XBsonArray_at_base(arr, (int64_t)i);
		XVariant* var = XBsonValue_toVariant_move(value);
		if (!var || !XVariantList_push_back_move_base(list, var)) {
			XVariant_delete_base(var);
			XVariantList_delete_base(list);
			return NULL;
		}
		XVariant_delete_base(var);
	}
	XBsonArray_clear_base(arr);
	return list;
}

XVariant* XBsonArray_toVariant(const XBsonArray* arr)
{
	if (!arr) return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XBsonArray), XVariantType_BsonArray);
	if (!var || !var->m_data) {
		XVariant_delete_base(var);
		return NULL;
	}
	XBsonArray_init((XBsonArray*)var->m_data);
	XCopy((XBsonArray*)var->m_data, arr);
	return var;
}

XVariant* XBsonArray_toVariant_move(XBsonArray* arr)
{
	if (!arr) return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XBsonArray), XVariantType_BsonArray);
	if (!var || !var->m_data) {
		XVariant_delete_base(var);
		return NULL;
	}
	XBsonArray_init((XBsonArray*)var->m_data);
	XMove((XBsonArray*)var->m_data, arr);
	return var;
}

XVariant* XBsonArray_toVariant_ref(XBsonArray* arr)
{
	if (!arr) return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_BsonArray);
	if (!var) return NULL;
	XVariant_setDataRef(var, arr, sizeof(XBsonArray), XVariantType_BsonArray);
	return var;
}

XBsonArray* XBsonArray_fromVariant(const XVariant* variant)
{
	XBsonArray* source = (XBsonArray*)XVariant_toRef(variant, XVariantType_BsonArray);
	return source ? XBsonArray_create_copy(source) : NULL;
}

XBsonArray* XBsonArray_fromVariant_ref(const XVariant* variant)
{
	return (XBsonArray*)XVariant_toRef(variant, XVariantType_BsonArray);
}

static bool XBsonArray_prepareVariant(XVariant* variant)
{
	if (!variant)
		return false;
	if (variant->m_type != XVariantType_BsonArray ||
		!variant->m_data || variant->m_dataSize != sizeof(XBsonArray)) {
		if (variant->m_data)
			XVariant_deinit_base(variant);
		variant->m_data = XMalloc_System(sizeof(XBsonArray));
		if (!variant->m_data)
			return false;
		variant->m_dataSize = sizeof(XBsonArray);
		XBsonArray_init((XBsonArray*)variant->m_data);
		variant->m_type = XVariantType_BsonArray;
	}
	return true;
}

void XBsonArray_setVariant(XVariant* variant, const XBsonArray* array)
{
	if (array && XBsonArray_prepareVariant(variant))
		XCopy((XBsonArray*)variant->m_data, array);
}

void XBsonArray_setVariant_move(XVariant* variant, XBsonArray* array)
{
	if (array && XBsonArray_prepareVariant(variant))
		XMove((XBsonArray*)variant->m_data, array);
}

void XBsonArray_setVariant_ref(XVariant* variant, XBsonArray* array)
{
	if (!variant || !array)
		return;
	XVariant_setDataRef(variant, array, sizeof(XBsonArray), XVariantType_BsonArray);
}
