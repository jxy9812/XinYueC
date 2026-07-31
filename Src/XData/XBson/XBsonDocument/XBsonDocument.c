#include "XBsonDocument.h"
#include "XBsonArray.h"
#include "XJsonObject.h"
#include "XMemory.h"
#include <limits.h>
#include <string.h>

/* BSON 文档保持元素原始顺序，并允许同名键重复出现。 */
static bool XBsonDocument_keyEquals(const XString* left, const XString* right)
{
	return left && right && XString_compare(left, right) == XCompare_Equality;
}

void XBsonElement_init(XBsonElement* element)
{
	if (!element) return;
	memset(element, 0, sizeof(*element));
	XString_init(&element->m_key);
	XBsonValue_init(&element->m_value, (XBsonType)0);
}

void XBsonElement_deinit(XBsonElement* element)
{
	if (!element) return;
	if (!XClassIsVtableNull(&element->m_key))
		XString_deinit_base(&element->m_key);
	XBsonValue_deinit(&element->m_value);
}

void XBsonElement_copy(XBsonElement* dest, const XBsonElement* src)
{
	if (!dest || !src || dest == src) return;
	if (XClassIsVtableNull(&dest->m_key))
		XString_init(&dest->m_key);
	else
		XString_deinit_base(&dest->m_key);
	XString_copy_base(&dest->m_key, &src->m_key);
	XBsonValue_copy(&dest->m_value, &src->m_value);
}

void XBsonElement_move(XBsonElement* dest, XBsonElement* src)
{
	if (!dest || !src || dest == src) return;
	if (XClassIsVtableNull(&dest->m_key))
		XString_init(&dest->m_key);
	else
		XString_deinit_base(&dest->m_key);
	XString_move_base(&dest->m_key, &src->m_key);
	XBsonValue_move(&dest->m_value, &src->m_value);
}

const XString* XBsonElement_key_const(const XBsonElement* element)
{
	return element ? &element->m_key : NULL;
}

const XBsonValue* XBsonElement_value_const(const XBsonElement* element)
{
	return element ? &element->m_value : NULL;
}

XBsonValue* XBsonElement_value(XBsonElement* element)
{
	return element ? &element->m_value : NULL;
}

XBsonDocument* XBsonDocument_create(void)
{
	XBsonDocument* doc = (XBsonDocument*)XMalloc_System(sizeof(*doc));
	if (!doc) return NULL;
	XBsonDocument_init(doc);
	Set_Class_MemoryFree(doc, XFree_System);
	return doc;
}

XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other)
{
	if (!other) return NULL;
	XBsonDocument* doc = XBsonDocument_create();
	if (!doc) return NULL;
	XBsonDocument_copy_base(doc, other);
	return doc;
}

XBsonDocument* XBsonDocument_create_move(XBsonDocument* other)
{
	if (!other) return NULL;
	XBsonDocument* doc = XBsonDocument_create();
	if (!doc) return NULL;
	XBsonDocument_move_base(doc, other);
	return doc;
}

void XBsonDocument_init(XBsonDocument* doc)
{
	if (!doc) return;
	XVector_init((XVector*)doc, sizeof(XBsonElement), true);
	XContainerSetDataCopyMethod(doc, XBsonElement_copy);
	XContainerSetDataMoveMethod(doc, XBsonElement_move);
	XContainerSetDataDeinitMethod(doc, XBsonElement_deinit);
}

/* 有序元素追加和兼容旧 insert 接口。 */
bool XBsonDocument_append(XBsonDocument* doc, const XString* key,
                          const XBsonValue* value)
{
	const char* key_utf8 = key ? XString_toUtf8(key) : NULL;
	size_t key_length = key ? XString_toUtf8_length(key) : 0;
	if (!doc || !key || !value || !key_utf8 ||
		memchr(key_utf8, 0, key_length) != NULL)
		return false;
	XBsonElement element;
	XBsonElement_init(&element);
	XString_copy_base(&element.m_key, key);
	XBsonValue_copy(&element.m_value, value);
	bool result = XVector_push_back_move_1_base((XVector*)doc, &element);
	XBsonElement_deinit(&element);
	return result;
}

bool XBsonDocument_append_move(XBsonDocument* doc, XString* key,
                               XBsonValue* value)
{
	const char* key_utf8 = key ? XString_toUtf8(key) : NULL;
	size_t key_length = key ? XString_toUtf8_length(key) : 0;
	if (!doc || !key || !value || !key_utf8 ||
		memchr(key_utf8, 0, key_length) != NULL)
		return false;
	XBsonElement element;
	XBsonElement_init(&element);
	XString_move_base(&element.m_key, key);
	XBsonValue_move(&element.m_value, value);
	bool result = XVector_push_back_move_1_base((XVector*)doc, &element);
	XBsonElement_deinit(&element);
	return result;
}

bool XBsonDocument_insert_base(XBsonDocument* doc, const XString* key,
                               const XBsonValue* value)
{
	return XBsonDocument_append(doc, key, value);
}

bool XBsonDocument_insert_move_base(XBsonDocument* doc, XString* key,
                                    XBsonValue* value)
{
	return XBsonDocument_append_move(doc, key, value);
}

static bool XBsonDocument_appendUtf8(XBsonDocument* doc, const char* key,
                                     const XBsonValue* value, bool moveValue)
{
	if (!doc || !key || !value) return false;
	XString_Init_Utf8(name, key);
	bool result = moveValue
		? XBsonDocument_append_move(doc, name, (XBsonValue*)value)
		: XBsonDocument_append(doc, name, value);
	XString_deinit_base(name);
	return result;
}

bool XBsonDocument_insert_keyUtf8_value(XBsonDocument* doc, const char* key,
                                        XBsonValue* value)
{
	return XBsonDocument_appendUtf8(doc, key, value, false);
}

bool XBsonDocument_insert_keyUtf8_value_move(XBsonDocument* doc,
                                             const char* key,
                                             XBsonValue* value)
{
	return XBsonDocument_appendUtf8(doc, key, value, true);
}

bool XBsonDocument_insert_keyUtf8_double(XBsonDocument* doc, const char* key,
                                         double d)
{
	if (!doc || !key) return false;
	XBsonValue_Init(value, XBSON_TYPE_DOUBLE);
	value->data.dbl = d;
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_deinit(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_int32(XBsonDocument* doc, const char* key,
                                        int32_t i)
{
	if (!doc || !key) return false;
	XBsonValue_Init(value, XBSON_TYPE_INT32);
	value->data.int32 = i;
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_deinit(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_int64(XBsonDocument* doc, const char* key,
                                        int64_t i)
{
	if (!doc || !key) return false;
	XBsonValue_Init(value, XBSON_TYPE_INT64);
	value->data.int64 = i;
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_deinit(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_string(XBsonDocument* doc, const char* key,
                                         const XString* strValue)
{
	if (!doc || !key || !strValue) return false;
	XBsonValue* value = XBsonValue_create_string(strValue);
	if (!value) return false;
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_string_move(XBsonDocument* doc,
                                              const char* key,
                                              XString* strValue)
{
	if (!doc || !key || !strValue) return false;
	XBsonValue* value = XBsonValue_create_string(strValue);
	if (!value) return false;
	XString_delete_base(value->data.str);
	value->data.str = XString_create_move(strValue);
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_utf8(XBsonDocument* doc, const char* key,
                                       const char* utf8)
{
	if (!doc || !key || !utf8) return false;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_STRING);
	if (!value) return false;
	XBsonValue_setString_utf8(value, utf8);
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_null(XBsonDocument* doc, const char* key)
{
	if (!doc || !key) return false;
	XBsonValue* value = XBsonValue_create_null();
	if (!value) return false;
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_bool(XBsonDocument* doc, const char* key,
                                       bool b)
{
	if (!doc || !key) return false;
	XBsonValue* value = XBsonValue_create_bool(b);
	if (!value) return false;
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_array(XBsonDocument* doc, const char* key,
                                        const XBsonArray* array)
{
	if (!doc || !key || !array) return false;
	XBsonValue* value = XBsonValue_create_array(array);
	if (!value) return false;
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_array_move(XBsonDocument* doc,
                                             const char* key,
                                             XBsonArray* array)
{
	if (!doc || !key || !array) return false;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_ARRAY);
	if (!value) return false;
	XBsonArray_move_base(value->data.arr, array);
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_document(XBsonDocument* doc, const char* key,
                                           const XBsonDocument* newDoc)
{
	if (!doc || !key || !newDoc) return false;
	XBsonValue* value = XBsonValue_create_document(newDoc);
	if (!value) return false;
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

bool XBsonDocument_insert_keyUtf8_document_move(XBsonDocument* doc,
                                                const char* key,
                                                XBsonDocument* newDoc)
{
	if (!doc || !key || !newDoc) return false;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_DOCUMENT);
	if (!value) return false;
	XBsonDocument_move_base(value->data.doc, newDoc);
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

bool XBsonDocument_insert_value_move(XBsonDocument* doc, const XString* key,
                                     XBsonValue* value)
{
	if (!doc || !key || !value) return false;
	XString* key_copy = XString_create_copy(key);
	if (!key_copy) return false;
	bool result = XBsonDocument_append_move(doc, key_copy, value);
	XString_delete_base(key_copy);
	return result;
}

XBsonElement* XBsonDocument_at_base(XBsonDocument* doc, int64_t index)
{
	return (XBsonElement*)XVector_at_base((const XVector*)doc, index);
}

const XBsonElement* XBsonDocument_at_const(const XBsonDocument* doc,
                                           int64_t index)
{
	return (const XBsonElement*)XVector_at_base((const XVector*)doc, index);
}

int64_t XBsonDocument_indexOf(const XBsonDocument* doc, const XString* key)
{
	if (!doc || !key) return -1;
	for (size_t i = 0; i < XBsonDocument_size_base(doc); ++i) {
		const XBsonElement* element = XBsonDocument_at_const(doc, (int64_t)i);
		if (element && XBsonDocument_keyEquals(&element->m_key, key))
			return (int64_t)i;
	}
	return -1;
}

XBsonValue* XBsonDocument_value_base(XBsonDocument* doc, const XString* key)
{
	int64_t index = XBsonDocument_indexOf(doc, key);
	XBsonElement* element = index >= 0 ? XBsonDocument_at_base(doc, index) : NULL;
	return element ? &element->m_value : NULL;
}

XBsonValue* XBsonDocument_value_keyUtf8(XBsonDocument* doc, const char* key)
{
	if (!doc || !key) return NULL;
	XString_Init_Utf8(name, key);
	XBsonValue* value = XBsonDocument_value_base(doc, name);
	XString_deinit_base(name);
	return value;
}

size_t XBsonDocument_count_keyUtf8(const XBsonDocument* doc, const char* key)
{
	if (!doc || !key) return 0;
	XString_Init_Utf8(name, key);
	size_t count = 0;
	for (size_t i = 0; i < XBsonDocument_size_base(doc); ++i) {
		const XBsonElement* element = XBsonDocument_at_const(doc, (int64_t)i);
		if (element && XBsonDocument_keyEquals(&element->m_key, name)) ++count;
	}
	XString_deinit_base(name);
	return count;
}

bool XBsonDocument_contains_keyUtf8(const XBsonDocument* doc, const char* key)
{
	return XBsonDocument_count_keyUtf8(doc, key) != 0;
}

bool XBsonDocument_removeAt(XBsonDocument* doc, int64_t index)
{
	if (!doc || index < 0 || (size_t)index >= XBsonDocument_size_base(doc))
		return false;
	XVector_removeAt_base((XVector*)doc, index);
	return true;
}

bool XBsonDocument_remove_keyUtf8(XBsonDocument* doc, const char* key)
{
	if (!doc || !key) return false;
	XString_Init_Utf8(name, key);
	int64_t index = XBsonDocument_indexOf(doc, name);
	XString_deinit_base(name);
	return XBsonDocument_removeAt(doc, index);
}

size_t XBsonDocument_removeAll_keyUtf8(XBsonDocument* doc, const char* key)
{
	if (!doc || !key) return 0;
	XString_Init_Utf8(name, key);
	size_t removed = 0;
	for (size_t i = XBsonDocument_size_base(doc); i > 0; --i) {
		const XBsonElement* element = XBsonDocument_at_const(doc, (int64_t)(i - 1));
		if (element && XBsonDocument_keyEquals(&element->m_key, name)) {
			XVector_removeAt_base((XVector*)doc, (int64_t)(i - 1));
			++removed;
		}
	}
	XString_deinit_base(name);
	return removed;
}

static void XBsonDocument_stringCopy(void* dest, const void* src)
{
	XString* d = (XString*)dest;
	if (!d || !src) return;
	if (XClassIsVtableNull(d)) XString_init(d);
	else XString_deinit_base(d);
	XString_copy_base(d, (const XString*)src);
}

static void XBsonDocument_stringMove(void* dest, void* src)
{
	XString* d = (XString*)dest;
	if (!d || !src) return;
	if (XClassIsVtableNull(d)) XString_init(d);
	else XString_deinit_base(d);
	XString_move_base(d, (XString*)src);
}

static void XBsonDocument_stringDeinit(void* data)
{
	if (data) XString_deinit_base((XString*)data);
}

XVector* XBsonDocument_keys_base(const XBsonDocument* doc)
{
	if (!doc) return NULL;
	XVector* keys = XVector_create(sizeof(XString));
	if (!keys) return NULL;
	XContainerSetDataCopyMethod(keys, XBsonDocument_stringCopy);
	XContainerSetDataMoveMethod(keys, XBsonDocument_stringMove);
	XContainerSetDataDeinitMethod(keys, XBsonDocument_stringDeinit);
	for (size_t i = 0; i < XBsonDocument_size_base(doc); ++i) {
		const XBsonElement* element = XBsonDocument_at_const(doc, (int64_t)i);
		if (!element || !XVector_append_1_base(keys, (void*)&element->m_key)) {
			XVector_delete_base(keys);
			return NULL;
		}
	}
	return keys;
}

XJsonObject* XBsonDocument_toJsonObject(const XBsonDocument* bson_obj)
{
	if (!bson_obj) return NULL;
	XJsonObject* json_obj = XJsonObject_create();
	if (!json_obj) return NULL;
	for (size_t i = 0; i < XBsonDocument_size_base(bson_obj); ++i) {
		const XBsonElement* element = XBsonDocument_at_const(bson_obj, (int64_t)i);
		XJsonValue* json_val = element ? XBsonValue_to_json(&element->m_value) : NULL;
		if (!element || !json_val) continue;
		const char* key = XString_toUtf8(&element->m_key);
		if (XJsonObject_contains_keyUtf8(json_obj, key))
			XJsonObject_remove_keyUtf8(json_obj, key);
		if (!XJsonObject_insert_value_move(json_obj, &element->m_key, json_val)) {
			XJsonValue_delete(json_val);
			XJsonObject_delete_base(json_obj);
			return NULL;
		}
		XJsonValue_delete(json_val);
	}
	return json_obj;
}

/* JSON 对象没有重复键，转换到 BSON 时仅生成一个同名元素。 */
XBsonDocument* XBsonDocument_fromJsonObject(const XJsonObject* json_obj)
{
	if (!json_obj) return NULL;
	XBsonDocument* bson_obj = XBsonDocument_create();
	if (!bson_obj) return NULL;
	for_each_iterator(json_obj, XMap, it) {
		XPair* pair = XMap_iterator_data(&it);
		const XString* key = XPair_first(pair);
		const XJsonValue* json_val = XPair_second(pair);
		XBsonValue* bson_val = XBsonValue_from_json(json_val);
		if (!bson_val || !XBsonDocument_append(bson_obj, key, bson_val)) {
			XBsonValue_delete(bson_val);
			XBsonDocument_delete_base(bson_obj);
			return NULL;
		}
		XBsonValue_delete(bson_val);
	}
	return bson_obj;
}

XByteArray* XBsonDocument_toJson(const XBsonDocument* bson_doc,
                                 XJsonDocumentFormat format)
{
	if (!bson_doc) return NULL;
	XJsonObject* json_obj = XBsonDocument_toJsonObject(bson_doc);
	if (!json_obj) return NULL;
	XByteArray* json = XJsonObject_toJson(json_obj, format);
	XJsonObject_delete_base(json_obj);
	return json;
}

XString* XBsonDocument_toJson_string(const XBsonDocument* bson_doc,
                                     XJsonDocumentFormat format)
{
	if (!bson_doc) return NULL;
	XJsonObject* json_obj = XBsonDocument_toJsonObject(bson_doc);
	if (!json_obj) return NULL;
	XString* json = XJsonObject_toString(json_obj, format);
	XJsonObject_delete_base(json_obj);
	return json;
}

XByteArray* XBsonDocument_toBson(const XBsonDocument* doc)
{
	if (!doc) return NULL;
	XByteArray* bytes = XByteArray_create();
	if (!bytes || !XByteArray_resize_base(bytes, 4)) {
		XByteArray_delete_base(bytes);
		return NULL;
	}
	for (size_t i = 0; i < XBsonDocument_size_base(doc); ++i) {
		const XBsonElement* element = XBsonDocument_at_const(doc, (int64_t)i);
		if (!element || !XBsonValue_serialize(&element->m_value,
				XString_toUtf8(&element->m_key), bytes)) {
			XByteArray_delete_base(bytes);
			return NULL;
		}
	}
	if (!XByteArray_push_back_1(bytes, 0x00) ||
		XByteArray_size_base(bytes) > INT32_MAX) {
		XByteArray_delete_base(bytes);
		return NULL;
	}
	uint32_t length = (uint32_t)XByteArray_size_base(bytes);
	XMemory_write_data(XContainerDataAddr(bytes), XBYTE_ORDER_LITTLE_ENDIAN,
		&length, sizeof(length));
	return bytes;
}

/* 解析完整 BSON 文档；失败时目标文档始终保持为空。 */
XBsonDocument* XBsonDocument_fromBson(XByteArray* data)
{
	if (!data || XByteArray_size_base(data) < 5) return NULL;
	XBsonDocument* doc = XBsonDocument_create();
	if (!doc) return NULL;
	if (!XBsonDocument_from_bytes(doc, XContainerDataAddr(data),
		XByteArray_size_base(data))) {
		XBsonDocument_delete_base(doc);
		return NULL;
	}
	return doc;
}

bool XBsonDocument_from_bytes(XBsonDocument* doc, const uint8_t* data,
                              size_t size)
{
	if (!doc) return false;
	XBsonDocument_clear_base(doc);
	if (!data || size < 5 || size > INT32_MAX) return false;
	uint32_t length = 0;
	XMemory_read_data(data, XBYTE_ORDER_LITTLE_ENDIAN, &length, sizeof(length));
	if (length != size || length < 5 || data[length - 1] != 0x00) return false;

	const uint8_t* ptr = data + sizeof(uint32_t);
	const uint8_t* end = data + length - 1;
	while (ptr < end) {
		XString* key = NULL;
		XBsonValue* value = XBsonValue_deserialize(&ptr, end, &key);
		if (!value || !key || !XBsonDocument_append_move(doc, key, value)) {
			XString_delete_base(key);
			XBsonValue_delete(value);
			XBsonDocument_clear_base(doc);
			return false;
		}
		XString_delete_base(key);
		XBsonValue_delete(value);
	}
	if (ptr != end) {
		XBsonDocument_clear_base(doc);
		return false;
	}
	return true;
}

static bool XBsonDocument_insertVariant(XVariantMap* map,
                                        const XString* key, XVariant* value)
{
	if (!map || !key || !value) return false;
	if (XMap_contains(map, key)) XMap_remove_base(map, key);
	return XMap_insert_valueMove_base(map, key, value);
}

XVariantMap* XBsonDocument_toVariantMap(const XBsonDocument* doc)
{
	if (!doc) return NULL;
	XVariantMap* map = XMap_create_XVariantMap();
	if (!map) return NULL;
	for (size_t i = 0; i < XBsonDocument_size_base(doc); ++i) {
		const XBsonElement* element = XBsonDocument_at_const(doc, (int64_t)i);
		XVariant* value = element ? XBsonValue_toVariant(&element->m_value) : NULL;
		if (!element || !value || !XBsonDocument_insertVariant(map,
			&element->m_key, value)) {
			XVariant_delete_base(value);
			XMap_delete_base(map);
			return NULL;
		}
		XVariant_delete_base(value);
	}
	return map;
}

XVariantMap* XBsonDocument_toVariantMap_move(XBsonDocument* doc)
{
	if (!doc) return NULL;
	XVariantMap* map = XMap_create_XVariantMap();
	if (!map) return NULL;
	for (size_t i = 0; i < XBsonDocument_size_base(doc); ++i) {
		XBsonElement* element = XBsonDocument_at_base(doc, (int64_t)i);
		XVariant* value = element ? XBsonValue_toVariant_move(&element->m_value) : NULL;
		if (!element || !value || !XBsonDocument_insertVariant(map,
			&element->m_key, value)) {
			XVariant_delete_base(value);
			XMap_delete_base(map);
			return NULL;
		}
		XVariant_delete_base(value);
	}
	XBsonDocument_clear_base(doc);
	return map;
}

XVariant* XBsonDocument_toVariant(const XBsonDocument* doc)
{
	if (!doc) return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XBsonDocument),
		XVariantType_BsonDocument);
	if (!var || !var->m_data) {
		XVariant_delete_base(var);
		return NULL;
	}
	XBsonDocument_init((XBsonDocument*)var->m_data);
	XBsonDocument_copy_base((XBsonDocument*)var->m_data, doc);
	return var;
}

XVariant* XBsonDocument_toVariant_move(XBsonDocument* doc)
{
	if (!doc) return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XBsonDocument),
		XVariantType_BsonDocument);
	if (!var || !var->m_data) {
		XVariant_delete_base(var);
		return NULL;
	}
	XBsonDocument_init((XBsonDocument*)var->m_data);
	XBsonDocument_move_base((XBsonDocument*)var->m_data, doc);
	return var;
}

XVariant* XBsonDocument_toVariant_ref(XBsonDocument* doc)
{
	if (!doc) return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_BsonDocument);
	if (!var) return NULL;
	var->m_data = doc;
	var->m_dataSize = sizeof(XBsonDocument);
	return var;
}
