#include "XBsonValue.h"
#include "XBsonArray.h"
#include "XBsonDocument.h"
#include "XJsonArray.h"
#include "XJsonObject.h"
#include "XMemory.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* BSON 1.1 类型、长度和小端编码辅助函数。 */
static bool XBsonValue_isKnownType(XBsonType type)
{
	switch (type) {
	case XBSON_TYPE_DOUBLE:
	case XBSON_TYPE_STRING:
	case XBSON_TYPE_DOCUMENT:
	case XBSON_TYPE_ARRAY:
	case XBSON_TYPE_BINARY:
	case XBSON_TYPE_UNDEFINED:
	case XBSON_TYPE_OBJECT_ID:
	case XBSON_TYPE_BOOL:
	case XBSON_TYPE_DATETIME:
	case XBSON_TYPE_NULL:
	case XBSON_TYPE_REGEX:
	case XBSON_TYPE_DBPOINTER:
	case XBSON_TYPE_JAVASCRIPT:
	case XBSON_TYPE_SYMBOL:
	case XBSON_TYPE_JAVASCRIPT_SCOPE:
	case XBSON_TYPE_INT32:
	case XBSON_TYPE_TIMESTAMP:
	case XBSON_TYPE_INT64:
	case XBSON_TYPE_DECIMAL128:
	case XBSON_TYPE_MIN_KEY:
	case XBSON_TYPE_MAX_KEY:
		return true;
	default:
		return false;
	}
}

static bool XBsonValue_hasString(const XBsonValue* value)
{
	return value && value->data.str;
}

static bool XBsonValue_appendBytes(XByteArray* output, const void* data,
	                                   size_t size)
{
	return size == 0 || (data && XByteArray_push_back_2(output, data, size));
}

static bool XBsonValue_writeU32(XByteArray* output, uint32_t value)
{
	uint32_t encoded = 0;
	XMemory_write_data(&encoded, XBYTE_ORDER_LITTLE_ENDIAN, &value,
		sizeof(value));
	return XBsonValue_appendBytes(output, &encoded, sizeof(encoded));
}

static bool XBsonValue_writeU64(XByteArray* output, int64_t value)
{
	int64_t encoded = 0;
	XMemory_write_data(&encoded, XBYTE_ORDER_LITTLE_ENDIAN, &value,
		sizeof(value));
	return XBsonValue_appendBytes(output, &encoded, sizeof(encoded));
}

static bool XBsonValue_writeCString(XByteArray* output, const XString* string)
{
	if (!output || !string) return false;
	const char* utf8 = XString_toUtf8(string);
	size_t length = XString_toUtf8_length(string);
	if (!utf8 || memchr(utf8, 0, length) != NULL) return false;
	return XBsonValue_appendBytes(output, utf8, length) &&
		XByteArray_push_back_1(output, 0x00);
}

static bool XBsonValue_writeString(XByteArray* output, const XString* string)
{
	if (!output || !string) return false;
	size_t length = XString_toUtf8_length(string);
	if (length >= INT32_MAX) return false;
	return XBsonValue_writeU32(output, (uint32_t)(length + 1)) &&
		XBsonValue_appendBytes(output, XString_toUtf8(string), length) &&
		XByteArray_push_back_1(output, 0x00);
}

static bool XBsonValue_readU32(const uint8_t** ptr, const uint8_t* end,
	                               uint32_t* result)
{
	if (!ptr || !*ptr || !result || *ptr > end || (size_t)(end - *ptr) < 4)
		return false;
	XMemory_read_data(*ptr, XBYTE_ORDER_LITTLE_ENDIAN, result, sizeof(*result));
	*ptr += 4;
	return true;
}

static bool XBsonValue_readU64(const uint8_t** ptr, const uint8_t* end,
	                               int64_t* result)
{
	if (!ptr || !*ptr || !result || *ptr > end || (size_t)(end - *ptr) < 8)
		return false;
	XMemory_read_data(*ptr, XBYTE_ORDER_LITTLE_ENDIAN, result, sizeof(*result));
	*ptr += 8;
	return true;
}

static bool XBsonValue_readCString(const uint8_t** ptr, const uint8_t* end,
	                                   XString** result)
{
	if (!ptr || !*ptr || !result || *ptr > end) return false;
	const uint8_t* start = *ptr;
	while (*ptr < end && **ptr != 0x00) ++(*ptr);
	if (*ptr >= end) return false;
	*result = XString_create_with_length_utf8((const char*)start,
		(size_t)(*ptr - start));
	if (!*result) return false;
	++(*ptr);
	return true;
}

static bool XBsonValue_readString(const uint8_t** ptr, const uint8_t* end,
	                                  XString* result)
{
	uint32_t length = 0;
	if (!result || !XBsonValue_readU32(ptr, end, &length) || length < 1 ||
		*ptr > end || (size_t)(end - *ptr) < length ||
		(*ptr)[length - 1] != 0x00)
		return false;
	if (!XString_assign_with_length_utf8(result, (const char*)*ptr,
		length - 1))
		return false;
	*ptr += length;
	return true;
}

static bool XBsonValue_readEmbeddedLength(const uint8_t* ptr,
	                                          const uint8_t* end,
	                                          uint32_t* length)
{
	if (!ptr || !length || ptr > end || (size_t)(end - ptr) < 4)
		return false;
	XMemory_read_data(ptr, XBYTE_ORDER_LITTLE_ENDIAN, length, sizeof(*length));
	return *length >= 5 && *length <= (uint32_t)(end - ptr) &&
		ptr[*length - 1] == 0x00;
}

static bool XBsonValue_isSortedRegexOptions(const XString* options)
{
	if (!options) return false;
	const char* text = XString_toUtf8(options);
	size_t length = XString_toUtf8_length(options);
	if (!text) return false;
	for (size_t i = 1; i < length; ++i)
		if ((unsigned char)text[i - 1] > (unsigned char)text[i]) return false;
	return memchr(text, 0, length) == NULL;
}

static char XBsonValue_hexDigit(uint8_t value)
{
	return value < 10 ? (char)('0' + value) : (char)('a' + value - 10);
}

static int XBsonValue_hexValue(char value)
{
	if (value >= '0' && value <= '9') return value - '0';
	if (value >= 'a' && value <= 'f') return value - 'a' + 10;
	if (value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -1;
}

static XString* XBsonValue_toHexString(const uint8_t* data, size_t size)
{
	if (!data && size != 0) return NULL;
	char* text = (char*)XMalloc_System(size * 2 + 1);
	if (!text) return NULL;
	for (size_t i = 0; i < size; ++i) {
		text[i * 2] = XBsonValue_hexDigit(data[i] >> 4);
		text[i * 2 + 1] = XBsonValue_hexDigit(data[i] & 0x0f);
	}
	text[size * 2] = 0;
	XString* result = XString_create_utf8(text);
	XFree_System(text);
	return result;
}

static bool XBsonValue_fromHexString(const XString* text, uint8_t* data,
		                                    size_t size)
{
	if (!text || (!data && size != 0) || XString_toUtf8_length(text) != size * 2)
		return false;
	const char* source = XString_toUtf8(text);
	for (size_t i = 0; i < size; ++i) {
		int high = XBsonValue_hexValue(source[i * 2]);
		int low = XBsonValue_hexValue(source[i * 2 + 1]);
		if (high < 0 || low < 0) return false;
		data[i] = (uint8_t)((high << 4) | low);
	}
	return true;
}

/* BSON 值构造、生命周期和所有权转移。 */
XBsonValue* XBsonValue_create(XBsonType type)
{
	if (!XBsonValue_isKnownType(type)) return NULL;
	XBsonValue* value = (XBsonValue*)XMalloc_System(sizeof(*value));
	if (value) XBsonValue_init(value, type);
	return value;
}

XBsonValue* XBsonValue_create_copy(const XBsonValue* other)
{
	if (!other || !XBsonValue_isKnownType(other->type)) return NULL;
	XBsonValue* value = XBsonValue_create(other->type);
	if (!value) return NULL;
	XBsonValue_copy(value, other);
	return value;
}

XBsonValue* XBsonValue_create_move(XBsonValue* other)
{
	if (!other || !XBsonValue_isKnownType(other->type)) return NULL;
	XBsonValue* value = XBsonValue_create(other->type);
	if (!value) return NULL;
	XBsonValue_move(value, other);
	return value;
}

XBsonValue* XBsonValue_create_null(void) { return XBsonValue_create(XBSON_TYPE_NULL); }
XBsonValue* XBsonValue_create_undefined(void) { return XBsonValue_create(XBSON_TYPE_UNDEFINED); }
XBsonValue* XBsonValue_create_bool(bool input)
{
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_BOOL);
	if (value) XBsonValue_setBool(value, input);
	return value;
}
XBsonValue* XBsonValue_create_double(double input)
{
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_DOUBLE);
	if (value) XBsonValue_setDouble(value, input);
	return value;
}
XBsonValue* XBsonValue_create_int32(int32_t input)
{
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_INT32);
	if (value) XBsonValue_setInt32(value, input);
	return value;
}
XBsonValue* XBsonValue_create_int64(int64_t input)
{
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_INT64);
	if (value) XBsonValue_setInt64(value, input);
	return value;
}

XBsonValue* XBsonValue_create_string(const XString* str)
{
	if (!str) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_STRING);
	if (value) XBsonValue_setString(value, str);
	return value;
}

XBsonValue* XBsonValue_create_symbol(const XString* str)
{
	if (!str) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_SYMBOL);
	if (value) XBsonValue_setSymbol(value, str);
	return value;
}

XBsonValue* XBsonValue_create_document(const XBsonDocument* doc)
{
	if (!doc) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_DOCUMENT);
	if (value) XBsonValue_setDocument(value, doc);
	return value;
}

XBsonValue* XBsonValue_create_array(const XBsonArray* arr)
{
	if (!arr) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_ARRAY);
	if (value) XBsonValue_setArray(value, arr);
	return value;
}

XBsonValue* XBsonValue_create_binary(XBsonBinarySubtype subtype,
                                     const XByteArray* data)
{
	if (!data) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_BINARY);
	if (value) XBsonValue_setBinary(value, subtype, data);
	return value;
}

XBsonValue* XBsonValue_create_object_id(const uint8_t* oid)
{
	if (!oid) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_OBJECT_ID);
	if (value) XBsonValue_setObjectId(value, oid);
	return value;
}

XBsonValue* XBsonValue_create_datetime(int64_t timestamp)
{
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_DATETIME);
	if (value) XBsonValue_setDatetime(value, timestamp);
	return value;
}

XBsonValue* XBsonValue_create_regex(const XString* pattern,
                                    const XString* options)
{
	if (!pattern || !options) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_REGEX);
	if (value) XBsonValue_setRegex(value, pattern, options);
	return value;
}

XBsonValue* XBsonValue_create_dbpointer(const XString* namespace_name,
                                        const uint8_t* oid)
{
	if (!namespace_name || !oid) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_DBPOINTER);
	if (value) XBsonValue_setDbpointer(value, namespace_name, oid);
	return value;
}

XBsonValue* XBsonValue_create_javascript(const XString* code)
{
	if (!code) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_JAVASCRIPT);
	if (value) XBsonValue_setJavascript(value, code);
	return value;
}

XBsonValue* XBsonValue_create_javascript_scope(const XString* code,
                                               const XBsonDocument* scope)
{
	if (!code || !scope) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_JAVASCRIPT_SCOPE);
	if (value) XBsonValue_setJavascript_scope(value, code, scope);
	return value;
}

XBsonValue* XBsonValue_create_timestamp(uint32_t increment, uint32_t timestamp)
{
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_TIMESTAMP);
	if (value) XBsonValue_setTimestamp(value, increment, timestamp);
	return value;
}

XBsonValue* XBsonValue_create_decimal128(const uint8_t* decimal)
{
	if (!decimal) return NULL;
	XBsonValue* value = XBsonValue_create(XBSON_TYPE_DECIMAL128);
	if (value) XBsonValue_setDecimal128(value, decimal);
	return value;
}

XBsonValue* XBsonValue_create_min_key(void) { return XBsonValue_create(XBSON_TYPE_MIN_KEY); }
XBsonValue* XBsonValue_create_max_key(void) { return XBsonValue_create(XBSON_TYPE_MAX_KEY); }

void XBsonValue_init(XBsonValue* value, XBsonType type)
{
	if (!value) return;
	memset(value, 0, sizeof(*value));
	value->type = type;
	switch (type) {
	case XBSON_TYPE_STRING:
	case XBSON_TYPE_JAVASCRIPT:
	case XBSON_TYPE_SYMBOL:
		value->data.str = XString_create();
		break;
	case XBSON_TYPE_DOCUMENT:
		value->data.doc = XBsonDocument_create();
		break;
	case XBSON_TYPE_ARRAY:
		value->data.arr = XBsonArray_create();
		break;
	case XBSON_TYPE_BINARY:
		value->data.binary.data = XByteArray_create();
		value->data.binary.subtype = XBSON_BINARY_GENERIC;
		break;
	case XBSON_TYPE_REGEX:
		value->data.regex.pattern = XString_create();
		value->data.regex.options = XString_create();
		break;
	case XBSON_TYPE_DBPOINTER:
		value->data.dbpointer.m_namespace = XString_create();
		break;
	case XBSON_TYPE_JAVASCRIPT_SCOPE:
		value->data.str = XString_create();
		value->data.doc = XBsonDocument_create();
		break;
	default:
		break;
	}
}

void XBsonValue_deinit(XBsonValue* value)
{
	if (!value) return;
	if (!XBsonValue_isKnownType(value->type)) {
		memset(value, 0, sizeof(*value));
		return;
	}
	switch (value->type) {
	case XBSON_TYPE_STRING:
	case XBSON_TYPE_JAVASCRIPT:
	case XBSON_TYPE_SYMBOL:
		XString_delete_base(value->data.str);
		break;
	case XBSON_TYPE_DOCUMENT:
		XBsonDocument_delete_base(value->data.doc);
		break;
	case XBSON_TYPE_ARRAY:
		XBsonArray_delete_base(value->data.arr);
		break;
	case XBSON_TYPE_BINARY:
		XByteArray_delete_base(value->data.binary.data);
		break;
	case XBSON_TYPE_REGEX:
	XString_delete_base(value->data.regex.pattern);
		XString_delete_base(value->data.regex.options);
		break;
	case XBSON_TYPE_DBPOINTER:
		XString_delete_base(value->data.dbpointer.m_namespace);
		break;
	case XBSON_TYPE_JAVASCRIPT_SCOPE:
		XString_delete_base(value->data.str);
		XBsonDocument_delete_base(value->data.doc);
		break;
	default:
		break;
	}
	memset(value, 0, sizeof(*value));
}

void XBsonValue_delete(XBsonValue* value)
{
	if (value) {
		XBsonValue_deinit(value);
		XFree_System(value);
	}
}

void XBsonValue_clear(XBsonValue* value)
{
	if (!value || !XBsonValue_isKnownType(value->type)) return;
	switch (value->type) {
	case XBSON_TYPE_STRING:
	case XBSON_TYPE_JAVASCRIPT:
	case XBSON_TYPE_SYMBOL:
		XString_clear_base(value->data.str);
		break;
	case XBSON_TYPE_DOCUMENT:
		XBsonDocument_clear_base(value->data.doc);
		break;
	case XBSON_TYPE_ARRAY:
		XBsonArray_clear_base(value->data.arr);
		break;
	case XBSON_TYPE_BINARY:
		XByteArray_clear_base(value->data.binary.data);
		break;
	case XBSON_TYPE_REGEX:
		XString_clear_base(value->data.regex.pattern);
		XString_clear_base(value->data.regex.options);
		break;
	case XBSON_TYPE_DBPOINTER:
		XString_clear_base(value->data.dbpointer.m_namespace);
		memset(value->data.dbpointer.oid, 0, sizeof(value->data.dbpointer.oid));
		break;
	case XBSON_TYPE_JAVASCRIPT_SCOPE:
		XString_clear_base(value->data.str);
		XBsonDocument_clear_base(value->data.doc);
		break;
	case XBSON_TYPE_OBJECT_ID:
		memset(value->data.oid, 0, sizeof(value->data.oid));
		break;
	case XBSON_TYPE_DECIMAL128:
		memset(value->data.decimal, 0, sizeof(value->data.decimal));
		break;
	default:
		memset(&value->data, 0, sizeof(value->data));
		break;
	}
}

void XBsonValue_setNull(XBsonValue* value)
{
	if (!value) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_NULL;
}

void XBsonValue_setUndefined(XBsonValue* value)
{
	if (!value) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_UNDEFINED;
}

#define XBSON_SET_SIMPLE(name, expected_type, member, ctype) \
	void name(XBsonValue* value, ctype input) { \
		if (!value) return; XBsonValue_deinit(value); value->type = expected_type; \
		value->data.member = input; }

XBSON_SET_SIMPLE(XBsonValue_setBool, XBSON_TYPE_BOOL, boolean, bool)
XBSON_SET_SIMPLE(XBsonValue_setDouble, XBSON_TYPE_DOUBLE, dbl, double)
XBSON_SET_SIMPLE(XBsonValue_setInt32, XBSON_TYPE_INT32, int32, int32_t)
XBSON_SET_SIMPLE(XBsonValue_setInt64, XBSON_TYPE_INT64, int64, int64_t)

#undef XBSON_SET_SIMPLE

void XBsonValue_setString(XBsonValue* value, const XString* str)
{
	if (!value || !str) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_STRING;
	value->data.str = XString_create_copy(str);
}

void XBsonValue_setString_move(XBsonValue* value, XString* str)
{
	if (!value || !str) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_STRING;
	value->data.str = XString_create_move(str);
}

void XBsonValue_setString_utf8(XBsonValue* value, const char* utf8)
{
	if (!value || !utf8) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_STRING;
	value->data.str = XString_create_utf8(utf8);
}

void XBsonValue_setSymbol(XBsonValue* value, const XString* str)
{
	if (!value || !str) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_SYMBOL;
	value->data.str = XString_create_copy(str);
}

void XBsonValue_setDocument(XBsonValue* value, const XBsonDocument* doc)
{
	if (!value || !doc) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_DOCUMENT;
	value->data.doc = XBsonDocument_create_copy(doc);
}

void XBsonValue_setDocument_move(XBsonValue* value, XBsonDocument* doc)
{
	if (!value || !doc) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_DOCUMENT;
	value->data.doc = XBsonDocument_create_move(doc);
}

void XBsonValue_setArray(XBsonValue* value, const XBsonArray* arr)
{
	if (!value || !arr) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_ARRAY;
	value->data.arr = XBsonArray_create_copy(arr);
}

void XBsonValue_setArray_move(XBsonValue* value, XBsonArray* arr)
{
	if (!value || !arr) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_ARRAY;
	value->data.arr = XBsonArray_create_move(arr);
}

void XBsonValue_setBinary(XBsonValue* value, XBsonBinarySubtype subtype,
                          const XByteArray* data)
{
	if (!value || !data) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_BINARY;
	value->data.binary.subtype = subtype;
	value->data.binary.data = XByteArray_create_copy(data);
}

void XBsonValue_setBinary_move(XBsonValue* value, XBsonBinarySubtype subtype,
                               XByteArray* data)
{
	if (!value || !data) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_BINARY;
	value->data.binary.subtype = subtype;
	value->data.binary.data = XByteArray_create_move(data);
}

void XBsonValue_setObjectId(XBsonValue* value, const uint8_t* oid)
{
	if (!value || !oid) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_OBJECT_ID;
	memcpy(value->data.oid, oid, sizeof(value->data.oid));
}

void XBsonValue_setDatetime(XBsonValue* value, int64_t timestamp)
{
	if (!value) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_DATETIME;
	value->data.datetime = timestamp;
}

void XBsonValue_setRegex(XBsonValue* value, const XString* pattern,
                         const XString* options)
{
	if (!value || !pattern || !options) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_REGEX;
	value->data.regex.pattern = XString_create_copy(pattern);
	value->data.regex.options = XString_create_copy(options);
}

void XBsonValue_setDbpointer(XBsonValue* value, const XString* namespace_name,
                             const uint8_t* oid)
{
	if (!value || !namespace_name || !oid) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_DBPOINTER;
	value->data.dbpointer.m_namespace = XString_create_copy(namespace_name);
	memcpy(value->data.dbpointer.oid, oid, sizeof(value->data.dbpointer.oid));
}

void XBsonValue_setJavascript(XBsonValue* value, const XString* code)
{
	if (!value || !code) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_JAVASCRIPT;
	value->data.str = XString_create_copy(code);
}

void XBsonValue_setJavascript_scope(XBsonValue* value, const XString* code,
                                    const XBsonDocument* scope)
{
	if (!value || !code || !scope) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_JAVASCRIPT_SCOPE;
	value->data.str = XString_create_copy(code);
	value->data.doc = XBsonDocument_create_copy(scope);
}

void XBsonValue_setTimestamp(XBsonValue* value, uint32_t increment,
                             uint32_t timestamp)
{
	if (!value) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_TIMESTAMP;
	value->data.ts.increment = increment;
	value->data.ts.timestamp = timestamp;
}

void XBsonValue_setDecimal128(XBsonValue* value, const uint8_t* decimal)
{
	if (!value || !decimal) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_DECIMAL128;
	memcpy(value->data.decimal, decimal, sizeof(value->data.decimal));
}

void XBsonValue_setMin_key(XBsonValue* value)
{
	if (!value) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_MIN_KEY;
}

void XBsonValue_setMax_key(XBsonValue* value)
{
	if (!value) return;
	XBsonValue_deinit(value);
	value->type = XBSON_TYPE_MAX_KEY;
}

bool XBsonValue_toBool(const XBsonValue* value, bool defaultValue)
{
	return value && value->type == XBSON_TYPE_BOOL ? value->data.boolean : defaultValue;
}

double XBsonValue_toDouble(const XBsonValue* value, double defaultValue)
{
	return value && value->type == XBSON_TYPE_DOUBLE ? value->data.dbl : defaultValue;
}

int32_t XBsonValue_toInt32(const XBsonValue* value, int32_t defaultValue)
{
	return value && value->type == XBSON_TYPE_INT32 ? value->data.int32 : defaultValue;
}

int64_t XBsonValue_toInt64(const XBsonValue* value, int64_t defaultValue)
{
	if (!value) return defaultValue;
	if (value->type == XBSON_TYPE_INT64) return value->data.int64;
	if (value->type == XBSON_TYPE_INT32) return value->data.int32;
	return defaultValue;
}

const XString* XBsonValue_toString(const XBsonValue* value)
{
	return value && (value->type == XBSON_TYPE_STRING ||
		value->type == XBSON_TYPE_JAVASCRIPT || value->type == XBSON_TYPE_SYMBOL ||
		value->type == XBSON_TYPE_JAVASCRIPT_SCOPE)
		? value->data.str : NULL;
}

const XBsonDocument* XBsonValue_toDocument(const XBsonValue* value)
{
	return value && (value->type == XBSON_TYPE_DOCUMENT ||
		value->type == XBSON_TYPE_JAVASCRIPT_SCOPE) ? value->data.doc : NULL;
}

const XBsonArray* XBsonValue_toArray(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_ARRAY ? value->data.arr : NULL;
}

const XByteArray* XBsonValue_toBinary(const XBsonValue* value,
                                      XBsonBinarySubtype* outSubtype)
{
	if (outSubtype) *outSubtype = XBSON_BINARY_GENERIC;
	if (!value || value->type != XBSON_TYPE_BINARY) return NULL;
	if (outSubtype) *outSubtype = value->data.binary.subtype;
	return value->data.binary.data;
}

const uint8_t* XBsonValue_toObjectId(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_OBJECT_ID ? value->data.oid : NULL;
}

int64_t XBsonValue_toDatetime(const XBsonValue* value, int64_t defaultValue)
{
	return value && value->type == XBSON_TYPE_DATETIME ? value->data.datetime : defaultValue;
}

const XString* XBsonValue_toRegexPattern(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_REGEX ? value->data.regex.pattern : NULL;
}

const XString* XBsonValue_toRegexOptions(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_REGEX ? value->data.regex.options : NULL;
}

const XString* XBsonValue_toDbpointerNamespace(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_DBPOINTER
		? value->data.dbpointer.m_namespace : NULL;
}

const uint8_t* XBsonValue_toDbpointerObjectId(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_DBPOINTER
		? value->data.dbpointer.oid : NULL;
}

const XBsonDocument* XBsonValue_toJavascriptScope(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_JAVASCRIPT_SCOPE ? value->data.doc : NULL;
}

const uint8_t* XBsonValue_toDecimal128(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_DECIMAL128 ? value->data.decimal : NULL;
}

uint32_t XBsonValue_timestampIncrement(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_TIMESTAMP ? value->data.ts.increment : 0;
}

uint32_t XBsonValue_timestamp(const XBsonValue* value)
{
	return value && value->type == XBSON_TYPE_TIMESTAMP ? value->data.ts.timestamp : 0;
}

#define XBSON_IS_TYPE(name, expected_type) \
	bool name(const XBsonValue* value) { return value && value->type == expected_type; }
XBSON_IS_TYPE(XBsonValue_isNull, XBSON_TYPE_NULL)
XBSON_IS_TYPE(XBsonValue_isUndefined, XBSON_TYPE_UNDEFINED)
XBSON_IS_TYPE(XBsonValue_isBool, XBSON_TYPE_BOOL)
XBSON_IS_TYPE(XBsonValue_isDouble, XBSON_TYPE_DOUBLE)
XBSON_IS_TYPE(XBsonValue_isInt32, XBSON_TYPE_INT32)
XBSON_IS_TYPE(XBsonValue_isInt64, XBSON_TYPE_INT64)
XBSON_IS_TYPE(XBsonValue_isString, XBSON_TYPE_STRING)
XBSON_IS_TYPE(XBsonValue_isSymbol, XBSON_TYPE_SYMBOL)
XBSON_IS_TYPE(XBsonValue_isDocument, XBSON_TYPE_DOCUMENT)
XBSON_IS_TYPE(XBsonValue_isArray, XBSON_TYPE_ARRAY)
XBSON_IS_TYPE(XBsonValue_isBinary, XBSON_TYPE_BINARY)
XBSON_IS_TYPE(XBsonValue_isObjectId, XBSON_TYPE_OBJECT_ID)
XBSON_IS_TYPE(XBsonValue_isDatetime, XBSON_TYPE_DATETIME)
XBSON_IS_TYPE(XBsonValue_isRegex, XBSON_TYPE_REGEX)
XBSON_IS_TYPE(XBsonValue_isDbpointer, XBSON_TYPE_DBPOINTER)
XBSON_IS_TYPE(XBsonValue_isJavascript, XBSON_TYPE_JAVASCRIPT)
XBSON_IS_TYPE(XBsonValue_isJavascriptScope, XBSON_TYPE_JAVASCRIPT_SCOPE)
XBSON_IS_TYPE(XBsonValue_isTimestamp, XBSON_TYPE_TIMESTAMP)
XBSON_IS_TYPE(XBsonValue_isDecimal128, XBSON_TYPE_DECIMAL128)
XBSON_IS_TYPE(XBsonValue_isMinKey, XBSON_TYPE_MIN_KEY)
XBSON_IS_TYPE(XBsonValue_isMaxKey, XBSON_TYPE_MAX_KEY)
#undef XBSON_IS_TYPE

void XBsonValue_copy(XBsonValue* dest, const XBsonValue* src)
{
	if (!dest || !src || dest == src || !XBsonValue_isKnownType(src->type)) return;
	XBsonValue_deinit(dest);
	dest->type = src->type;
	switch (src->type) {
	case XBSON_TYPE_DOUBLE:
		dest->data.dbl = src->data.dbl;
		break;
	case XBSON_TYPE_STRING:
	case XBSON_TYPE_JAVASCRIPT:
	case XBSON_TYPE_SYMBOL:
		dest->data.str = XString_create_copy(src->data.str);
		break;
	case XBSON_TYPE_DOCUMENT:
		dest->data.doc = XBsonDocument_create_copy(src->data.doc);
		break;
	case XBSON_TYPE_ARRAY:
		dest->data.arr = XBsonArray_create_copy(src->data.arr);
		break;
	case XBSON_TYPE_BINARY:
		dest->data.binary.subtype = src->data.binary.subtype;
		dest->data.binary.data = XByteArray_create_copy(src->data.binary.data);
		break;
	case XBSON_TYPE_OBJECT_ID:
		memcpy(dest->data.oid, src->data.oid, sizeof(dest->data.oid));
		break;
	case XBSON_TYPE_BOOL: dest->data.boolean = src->data.boolean; break;
	case XBSON_TYPE_DATETIME: dest->data.datetime = src->data.datetime; break;
	case XBSON_TYPE_REGEX:
		dest->data.regex.pattern = XString_create_copy(src->data.regex.pattern);
		dest->data.regex.options = XString_create_copy(src->data.regex.options);
		break;
	case XBSON_TYPE_DBPOINTER:
		dest->data.dbpointer.m_namespace = XString_create_copy(src->data.dbpointer.m_namespace);
		memcpy(dest->data.dbpointer.oid, src->data.dbpointer.oid,
			sizeof(dest->data.dbpointer.oid));
		break;
	case XBSON_TYPE_JAVASCRIPT_SCOPE:
		dest->data.str = XString_create_copy(src->data.str);
		dest->data.doc = XBsonDocument_create_copy(src->data.doc);
		break;
	case XBSON_TYPE_INT32: dest->data.int32 = src->data.int32; break;
	case XBSON_TYPE_TIMESTAMP: dest->data.ts = src->data.ts; break;
	case XBSON_TYPE_INT64: dest->data.int64 = src->data.int64; break;
	case XBSON_TYPE_DECIMAL128:
		memcpy(dest->data.decimal, src->data.decimal, sizeof(dest->data.decimal));
		break;
	default:
		break;
	}
}

void XBsonValue_move(XBsonValue* dest, XBsonValue* src)
{
	if (!dest || !src || dest == src || !XBsonValue_isKnownType(src->type)) return;
	XBsonValue_deinit(dest);
	*dest = *src;
	memset(src, 0, sizeof(*src));
}

XBsonType XBsonValue_type(const XBsonValue* value)
{
	return value ? value->type : (XBsonType)0;
}

bool XBsonValue_is_type(const XBsonValue* value, XBsonType type)
{
	return value && value->type == type;
}

static XJsonValue* XBsonValue_jsonObjectValue(const XJsonObject* object,
                                              const char* key)
{
	if (!object || !key) return NULL;
	XString_Init_Utf8(name, key);
	XJsonValue* value = (XJsonValue*)XJsonObject_value_base(object, name);
	XString_deinit_base(name);
	return value;
}

static XJsonValue* XBsonValue_wrapObject(XJsonObject* object)
{
	if (!object) return NULL;
	XJsonValue* value = XJsonValue_create_object(object);
	XJsonObject_delete_base(object);
	return value;
}

XJsonValue* XBsonValue_to_json(const XBsonValue* bson_val)
{
	if (!bson_val) return NULL;
	switch (bson_val->type) {
	case XBSON_TYPE_DOUBLE: return XJsonValue_create_double(bson_val->data.dbl);
	case XBSON_TYPE_STRING: return XJsonValue_create_string(bson_val->data.str);
	case XBSON_TYPE_BOOL: return XJsonValue_create_bool(bson_val->data.boolean);
	case XBSON_TYPE_INT32: return XJsonValue_create_int(bson_val->data.int32);
	case XBSON_TYPE_INT64: return XJsonValue_create_int(bson_val->data.int64);
	case XBSON_TYPE_NULL: return XJsonValue_create_null();
	case XBSON_TYPE_UNDEFINED: {
		XJsonObject* object = XJsonObject_create();
		if (object) XJsonObject_insert_keyUtf8_bool(object, "$undefined", true);
		return XBsonValue_wrapObject(object);
	}
	case XBSON_TYPE_DOCUMENT: {
		XJsonObject* object = XBsonDocument_toJsonObject(bson_val->data.doc);
		return object ? XBsonValue_wrapObject(object) : NULL;
	}
	case XBSON_TYPE_ARRAY: {
		XJsonArray* array = XBsonArray_toJsonArray(bson_val->data.arr);
		if (!array) return NULL;
		XJsonValue* value = XJsonValue_create_array(array);
		XJsonArray_delete_base(array);
		return value;
	}
	case XBSON_TYPE_BINARY: {
		XJsonObject* object = XJsonObject_create();
		XString* hex = XBsonValue_toHexString(XContainerDataAddr(bson_val->data.binary.data),
			XByteArray_size_base(bson_val->data.binary.data));
		if (!object || !hex ||
			!XJsonObject_insert_keyUtf8_int(object, "$binarySubtype",
				bson_val->data.binary.subtype) ||
			!XJsonObject_insert_keyUtf8_string(object, "$binaryData", hex)) {
			XJsonObject_delete_base(object);
			XString_delete_base(hex);
			return NULL;
		}
		XString_delete_base(hex);
		return XBsonValue_wrapObject(object);
	}
	case XBSON_TYPE_OBJECT_ID: {
		XJsonObject* object = XJsonObject_create();
		XString* hex = XBsonValue_toHexString(bson_val->data.oid, 12);
		if (!object || !hex || !XJsonObject_insert_keyUtf8_string(object, "$oid", hex)) {
			XJsonObject_delete_base(object);
			XString_delete_base(hex);
			return NULL;
		}
		XString_delete_base(hex);
		return XBsonValue_wrapObject(object);
	}
	case XBSON_TYPE_DATETIME: {
		XJsonObject* object = XJsonObject_create();
		if (object) XJsonObject_insert_keyUtf8_int(object, "$date", bson_val->data.datetime);
		return XBsonValue_wrapObject(object);
	}
	case XBSON_TYPE_REGEX: {
		XJsonObject* object = XJsonObject_create();
		if (object && XJsonObject_insert_keyUtf8_string(object, "$regex", bson_val->data.regex.pattern) &&
			XJsonObject_insert_keyUtf8_string(object, "$options", bson_val->data.regex.options))
			return XBsonValue_wrapObject(object);
		XJsonObject_delete_base(object);
		return NULL;
	}
	case XBSON_TYPE_DBPOINTER: {
		XJsonObject* pointer = XJsonObject_create();
		XString* ns = XString_create_copy(bson_val->data.dbpointer.m_namespace);
		XString* oid = XBsonValue_toHexString(bson_val->data.dbpointer.oid, 12);
		XJsonObject* oid_object = XJsonObject_create();
		if (!pointer || !ns || !oid || !oid_object ||
			!XJsonObject_insert_keyUtf8_string(pointer, "$ref", ns) ||
			!XJsonObject_insert_keyUtf8_string(oid_object, "$oid", oid) ||
			!XJsonObject_insert_keyUtf8_object_move(pointer, "$id", oid_object)) {
			XJsonObject_delete_base(pointer);
			XJsonObject_delete_base(oid_object);
			XString_delete_base(ns);
			XString_delete_base(oid);
			return NULL;
		}
		XString_delete_base(ns);
		XString_delete_base(oid);
		/* $id 已由 pointer 持有副本，移动创建后的临时对象仍需释放外壳。 */
		XJsonObject_delete_base(oid_object);
		XJsonObject* wrapper = XJsonObject_create();
		if (!wrapper || !XJsonObject_insert_keyUtf8_object_move(wrapper, "$dbPointer", pointer)) {
			XJsonObject_delete_base(wrapper);
			XJsonObject_delete_base(pointer);
			return NULL;
		}
		XJsonObject_delete_base(pointer);
		return XBsonValue_wrapObject(wrapper);
	}
	case XBSON_TYPE_JAVASCRIPT: {
		XJsonObject* object = XJsonObject_create();
		if (object) XJsonObject_insert_keyUtf8_string(object, "$javascript", bson_val->data.str);
		return XBsonValue_wrapObject(object);
	}
	case XBSON_TYPE_SYMBOL: {
		XJsonObject* object = XJsonObject_create();
		if (object) XJsonObject_insert_keyUtf8_string(object, "$symbol", bson_val->data.str);
		return XBsonValue_wrapObject(object);
	}
	case XBSON_TYPE_JAVASCRIPT_SCOPE: {
		XJsonObject* object = XJsonObject_create();
		XJsonObject* scope = XBsonDocument_toJsonObject(bson_val->data.doc);
		if (!object || !scope ||
			!XJsonObject_insert_keyUtf8_string(object, "$javascript", bson_val->data.str) ||
			!XJsonObject_insert_keyUtf8_object_move(object, "$scope", scope)) {
			XJsonObject_delete_base(object);
			XJsonObject_delete_base(scope);
			return NULL;
		}
		XJsonObject_delete_base(scope);
		return XBsonValue_wrapObject(object);
	}
	case XBSON_TYPE_TIMESTAMP: {
		XJsonObject* object = XJsonObject_create();
		if (object && XJsonObject_insert_keyUtf8_int(object, "$timestamp", bson_val->data.ts.timestamp) &&
			XJsonObject_insert_keyUtf8_int(object, "$increment", bson_val->data.ts.increment))
			return XBsonValue_wrapObject(object);
		XJsonObject_delete_base(object);
		return NULL;
	}
	case XBSON_TYPE_DECIMAL128: {
		XJsonObject* object = XJsonObject_create();
		XString* hex = XBsonValue_toHexString(bson_val->data.decimal, 16);
		if (!object || !hex || !XJsonObject_insert_keyUtf8_string(object, "$decimal128", hex)) {
			XJsonObject_delete_base(object);
			XString_delete_base(hex);
			return NULL;
		}
		XString_delete_base(hex);
		return XBsonValue_wrapObject(object);
	}
	case XBSON_TYPE_MIN_KEY: {
		XJsonObject* object = XJsonObject_create();
		if (object) XJsonObject_insert_keyUtf8_null(object, "$minKey");
		return XBsonValue_wrapObject(object);
	}
	case XBSON_TYPE_MAX_KEY: {
		XJsonObject* object = XJsonObject_create();
		if (object) XJsonObject_insert_keyUtf8_null(object, "$maxKey");
		return XBsonValue_wrapObject(object);
	}
	default:
		return NULL;
	}
}

/* XJson 包装对象解析；包装格式只用于保留 BSON 特有类型。 */
static bool XBsonValue_jsonString(const XJsonValue* value, const XString** result)
{
	if (!value || XJsonValue_type(value) != XJsonValue_String || !result) return false;
	*result = XJsonValue_toString(value);
	return *result != NULL;
}

XBsonValue* XBsonValue_from_json(const XJsonValue* json_val)
{
	if (!json_val) return NULL;
	switch (XJsonValue_type(json_val)) {
	case XJsonValue_Bool: return XBsonValue_create_bool(XJsonValue_toBool(json_val, false));
	case XJsonValue_Double: return XBsonValue_create_double(XJsonValue_toDouble(json_val, 0.0));
	case XJsonValue_Int: {
		int64_t value = XJsonValue_toInt(json_val, 0);
		return value >= INT32_MIN && value <= INT32_MAX
			? XBsonValue_create_int32((int32_t)value) : XBsonValue_create_int64(value);
	}
	case XJsonValue_String: return XBsonValue_create_string(XJsonValue_toString(json_val));
	case XJsonValue_Array: {
		XBsonArray* array = XBsonArray_fromJsonArray(XJsonValue_toArray(json_val));
		if (!array) return NULL;
		XBsonValue* value = XBsonValue_create_array(array);
		XBsonArray_delete_base(array);
		return value;
	}
	case XJsonValue_Null: return XBsonValue_create_null();
	case XJsonValue_Object: {
		XJsonObject* object = XJsonValue_toObject(json_val);
		XJsonValue* special = NULL;
		if (XJsonObject_contains_keyUtf8(object, "$oid")) {
			const XString* text = NULL;
			special = XBsonValue_jsonObjectValue(object, "$oid");
			if (!XBsonValue_jsonString(special, &text) || XString_toUtf8_length(text) != 24)
				return NULL;
			uint8_t oid[12];
			if (!XBsonValue_fromHexString(text, oid, sizeof(oid))) return NULL;
			return XBsonValue_create_object_id(oid);
		}
		if (XJsonObject_contains_keyUtf8(object, "$binarySubtype") &&
			XJsonObject_contains_keyUtf8(object, "$binaryData")) {
			XJsonValue* subtype = XBsonValue_jsonObjectValue(object, "$binarySubtype");
			XJsonValue* binary = XBsonValue_jsonObjectValue(object, "$binaryData");
			const XString* text = NULL;
			int64_t subtype_number = XJsonValue_toInt(subtype, -1);
			if (subtype_number < 0 || subtype_number > 255 ||
				!XBsonValue_jsonString(binary, &text) ||
				XString_toUtf8_length(text) % 2 != 0)
				return NULL;
			size_t size = XString_toUtf8_length(text) / 2;
			XByteArray* data = XByteArray_create();
			if (!data || !XByteArray_resize_base(data, size) ||
				!XBsonValue_fromHexString(text, XContainerDataAddr(data), size)) {
				XByteArray_delete_base(data);
				return NULL;
			}
			XBsonValue* value = XBsonValue_create_binary((XBsonBinarySubtype)subtype_number, data);
			XByteArray_delete_base(data);
			return value;
		}
		if (XJsonObject_contains_keyUtf8(object, "$date")) {
			return XBsonValue_create_datetime(XJsonValue_toInt(
				XBsonValue_jsonObjectValue(object, "$date"), 0));
		}
		if (XJsonObject_contains_keyUtf8(object, "$regex")) {
			const XString* pattern = NULL;
			const XString* options = NULL;
			if (!XBsonValue_jsonString(XBsonValue_jsonObjectValue(object, "$regex"), &pattern) ||
				!XBsonValue_jsonString(XBsonValue_jsonObjectValue(object, "$options"), &options))
				return NULL;
			return XBsonValue_create_regex(pattern, options);
		}
		if (XJsonObject_contains_keyUtf8(object, "$javascript")) {
			const XString* code = NULL;
			if (!XBsonValue_jsonString(XBsonValue_jsonObjectValue(object, "$javascript"), &code))
				return NULL;
			if (XJsonObject_contains_keyUtf8(object, "$scope")) {
				XJsonValue* scope_value = XBsonValue_jsonObjectValue(object, "$scope");
				if (!scope_value || XJsonValue_type(scope_value) != XJsonValue_Object) return NULL;
				XBsonDocument* scope = XBsonDocument_fromJsonObject(XJsonValue_toObject(scope_value));
				if (!scope) return NULL;
				XBsonValue* value = XBsonValue_create_javascript_scope(code, scope);
				XBsonDocument_delete_base(scope);
				return value;
			}
			return XBsonValue_create_javascript(code);
		}
		if (XJsonObject_contains_keyUtf8(object, "$symbol")) {
			const XString* symbol = NULL;
			if (!XBsonValue_jsonString(XBsonValue_jsonObjectValue(object, "$symbol"), &symbol)) return NULL;
			return XBsonValue_create_symbol(symbol);
		}
		if (XJsonObject_contains_keyUtf8(object, "$undefined"))
			return XBsonValue_create_undefined();
		if (XJsonObject_contains_keyUtf8(object, "$timestamp") &&
			XJsonObject_contains_keyUtf8(object, "$increment")) {
			XJsonValue* timestamp_value = XBsonValue_jsonObjectValue(object, "$timestamp");
			XJsonValue* increment_value = XBsonValue_jsonObjectValue(object, "$increment");
			if (!timestamp_value || !increment_value ||
				XJsonValue_type(timestamp_value) != XJsonValue_Int ||
				XJsonValue_type(increment_value) != XJsonValue_Int)
				return NULL;
			int64_t timestamp = XJsonValue_toInt(timestamp_value, -1);
			int64_t increment = XJsonValue_toInt(increment_value, -1);
			if (timestamp < 0 || timestamp > UINT32_MAX ||
				increment < 0 || increment > UINT32_MAX)
				return NULL;
			return XBsonValue_create_timestamp((uint32_t)increment,
				(uint32_t)timestamp);
		}
		if (XJsonObject_contains_keyUtf8(object, "$decimal128")) {
			const XString* text = NULL;
			uint8_t decimal[16];
			if (!XBsonValue_jsonString(XBsonValue_jsonObjectValue(object, "$decimal128"), &text) ||
				!XBsonValue_fromHexString(text, decimal, sizeof(decimal))) return NULL;
			return XBsonValue_create_decimal128(decimal);
		}
		if (XJsonObject_contains_keyUtf8(object, "$dbPointer")) {
			XJsonValue* pointer = XBsonValue_jsonObjectValue(object, "$dbPointer");
			if (!pointer || XJsonValue_type(pointer) != XJsonValue_Object) return NULL;
			XJsonObject* pointer_object = XJsonValue_toObject(pointer);
			const XString* ns = NULL;
			XJsonValue* id = XBsonValue_jsonObjectValue(pointer_object, "$id");
			if (!XBsonValue_jsonString(XBsonValue_jsonObjectValue(pointer_object, "$ref"), &ns) ||
				!id || XJsonValue_type(id) != XJsonValue_Object) return NULL;
			const XString* oid_text = NULL;
			if (!XBsonValue_jsonString(XBsonValue_jsonObjectValue(XJsonValue_toObject(id), "$oid"), &oid_text) ||
				XString_toUtf8_length(oid_text) != 24) return NULL;
			uint8_t oid[12];
			if (!XBsonValue_fromHexString(oid_text, oid, sizeof(oid))) return NULL;
			return XBsonValue_create_dbpointer(ns, oid);
		}
		if (XJsonObject_contains_keyUtf8(object, "$minKey"))
			return XBsonValue_create_min_key();
		if (XJsonObject_contains_keyUtf8(object, "$maxKey"))
			return XBsonValue_create_max_key();
		XBsonDocument* document = XBsonDocument_fromJsonObject(object);
		if (!document) return NULL;
		XBsonValue* value = XBsonValue_create_document(document);
		XBsonDocument_delete_base(document);
		return value;
	}
	default:
		return NULL;
	}
}

bool XBsonValue_serialize(const XBsonValue* value, const char* key,
                          XByteArray* output)
{
	if (!value || !key || !output || !XBsonValue_isKnownType(value->type)) return false;
	XString_Init_Utf8(name, key);
	size_t key_length = XString_toUtf8_length(name);
	const char* key_utf8 = XString_toUtf8(name);
	bool valid_key = key_utf8 && memchr(key_utf8, 0, key_length) == NULL;
	if (!valid_key || !XByteArray_push_back_1(output, (uint8_t)value->type) ||
		!XBsonValue_appendBytes(output, key_utf8, key_length) ||
		!XByteArray_push_back_1(output, 0x00)) {
		XString_deinit_base(name);
		return false;
	}
	bool result = true;
	switch (value->type) {
	case XBSON_TYPE_DOUBLE: {
		double encoded = 0;
		XMemory_write_data(&encoded, XBYTE_ORDER_LITTLE_ENDIAN, &value->data.dbl, sizeof(encoded));
		result = XBsonValue_appendBytes(output, &encoded, sizeof(encoded));
		break;
	}
	case XBSON_TYPE_STRING:
	case XBSON_TYPE_SYMBOL:
	case XBSON_TYPE_JAVASCRIPT:
		result = XBsonValue_writeString(output, value->data.str);
		break;
	case XBSON_TYPE_DOCUMENT: {
		XByteArray* nested = XBsonDocument_toBson(value->data.doc);
		result = nested && XBsonValue_appendBytes(output, XContainerDataAddr(nested),
			XByteArray_size_base(nested));
		XByteArray_delete_base(nested);
		break;
	}
	case XBSON_TYPE_ARRAY: {
		XByteArray* nested = XBsonArray_toBson(value->data.arr);
		result = nested && XBsonValue_appendBytes(output, XContainerDataAddr(nested),
			XByteArray_size_base(nested));
		XByteArray_delete_base(nested);
		break;
	}
	case XBSON_TYPE_BINARY: {
		size_t size = XByteArray_size_base(value->data.binary.data);
		if (size > INT32_MAX - (value->data.binary.subtype == XBSON_BINARY_BINARY_OLD ? 4 : 0)) {
			result = false;
			break;
		}
		uint32_t outer_size = (uint32_t)size;
		if (value->data.binary.subtype == XBSON_BINARY_BINARY_OLD) outer_size += 4;
		result = XBsonValue_writeU32(output, outer_size) &&
			XByteArray_push_back_1(output, (uint8_t)value->data.binary.subtype);
		if (result && value->data.binary.subtype == XBSON_BINARY_BINARY_OLD)
			result = XBsonValue_writeU32(output, (uint32_t)size);
		if (result) result = XBsonValue_appendBytes(output,
			XContainerDataAddr(value->data.binary.data), size);
		break;
	}
	case XBSON_TYPE_UNDEFINED:
		break;
	case XBSON_TYPE_OBJECT_ID:
		result = XBsonValue_appendBytes(output, value->data.oid, 12);
		break;
	case XBSON_TYPE_BOOL:
		result = XByteArray_push_back_1(output, value->data.boolean ? 1 : 0);
		break;
	case XBSON_TYPE_DATETIME:
		result = XBsonValue_writeU64(output, value->data.datetime);
		break;
	case XBSON_TYPE_NULL:
		break;
	case XBSON_TYPE_REGEX:
		result = XBsonValue_writeCString(output, value->data.regex.pattern) &&
			XBsonValue_isSortedRegexOptions(value->data.regex.options) &&
			XBsonValue_writeCString(output, value->data.regex.options);
		break;
	case XBSON_TYPE_DBPOINTER:
		result = XBsonValue_writeString(output, value->data.dbpointer.m_namespace) &&
			XBsonValue_appendBytes(output, value->data.dbpointer.oid, 12);
		break;
	case XBSON_TYPE_JAVASCRIPT_SCOPE: {
		XByteArray* scope = XBsonDocument_toBson(value->data.doc);
		if (!scope || !value->data.str) {
			XByteArray_delete_base(scope);
			result = false;
			break;
		}
		size_t code_length = XString_toUtf8_length(value->data.str);
		size_t total = sizeof(uint32_t) + sizeof(uint32_t) + code_length + 1 +
			XByteArray_size_base(scope);
		result = total <= INT32_MAX && XBsonValue_writeU32(output, (uint32_t)total) &&
			XBsonValue_writeString(output, value->data.str) &&
			XBsonValue_appendBytes(output, XContainerDataAddr(scope), XByteArray_size_base(scope));
		XByteArray_delete_base(scope);
		break;
	}
	case XBSON_TYPE_INT32: {
		int32_t encoded = 0;
		XMemory_write_data(&encoded, XBYTE_ORDER_LITTLE_ENDIAN, &value->data.int32, sizeof(encoded));
		result = XBsonValue_appendBytes(output, &encoded, sizeof(encoded));
		break;
	}
	case XBSON_TYPE_TIMESTAMP: {
		result = XBsonValue_writeU32(output, value->data.ts.increment) &&
			XBsonValue_writeU32(output, value->data.ts.timestamp);
		break;
	}
	case XBSON_TYPE_INT64:
		result = XBsonValue_writeU64(output, value->data.int64);
		break;
	case XBSON_TYPE_DECIMAL128:
		result = XBsonValue_appendBytes(output, value->data.decimal, 16);
		break;
	case XBSON_TYPE_MIN_KEY:
	case XBSON_TYPE_MAX_KEY:
		break;
	default:
		result = false;
		break;
	}
	XString_deinit_base(name);
	return result;
}

/* BSON 元素反序列化：调用方负责继续解析外层文档或数组。 */
XBsonValue* XBsonValue_deserialize(const uint8_t** ptr, const uint8_t* end,
                                   XString** key_out)
{
	if (key_out) *key_out = NULL;
	if (!ptr || !*ptr || !end || *ptr >= end) return NULL;
	uint8_t type_byte = **ptr;
	++(*ptr);
	if (!XBsonValue_isKnownType((XBsonType)type_byte)) return NULL;
	XString* key = NULL;
	if (!XBsonValue_readCString(ptr, end, &key)) return NULL;
	XBsonValue* value = XBsonValue_create((XBsonType)type_byte);
	if (!value) {
		XString_delete_base(key);
		return NULL;
	}
	bool result = true;
	switch (value->type) {
	case XBSON_TYPE_DOUBLE:
		if (*ptr > end || (size_t)(end - *ptr) < 8) result = false;
		else { XMemory_read_data(*ptr, XBYTE_ORDER_LITTLE_ENDIAN, &value->data.dbl, 8); *ptr += 8; }
		break;
	case XBSON_TYPE_STRING:
	case XBSON_TYPE_SYMBOL:
	case XBSON_TYPE_JAVASCRIPT:
		result = XBsonValue_readString(ptr, end, value->data.str);
		break;
	case XBSON_TYPE_DOCUMENT: {
		uint32_t length = 0;
		result = XBsonValue_readEmbeddedLength(*ptr, end, &length) &&
			XBsonDocument_from_bytes(value->data.doc, *ptr, length);
		if (result) *ptr += length;
		break;
	}
	case XBSON_TYPE_ARRAY: {
		uint32_t length = 0;
		result = XBsonValue_readEmbeddedLength(*ptr, end, &length) &&
			XBsonArray_from_bytes(value->data.arr, *ptr, length);
		if (result) *ptr += length;
		break;
	}
	case XBSON_TYPE_BINARY: {
		uint32_t length = 0;
		result = XBsonValue_readU32(ptr, end, &length) && *ptr < end;
		if (!result) break;
		value->data.binary.subtype = (XBsonBinarySubtype)**ptr;
		++(*ptr);
		uint32_t data_length = length;
		if (value->data.binary.subtype == XBSON_BINARY_BINARY_OLD) {
			result = length >= 4 && XBsonValue_readU32(ptr, end, &data_length) &&
				data_length == length - 4;
		}
		if (!result || *ptr > end || (size_t)(end - *ptr) < data_length) {
			result = false;
			break;
		}
		result = XByteArray_resize_base(value->data.binary.data, data_length);
		if (result && data_length != 0)
			memcpy(XContainerDataAddr(value->data.binary.data), *ptr, data_length);
		if (result) *ptr += data_length;
		break;
	}
	case XBSON_TYPE_UNDEFINED:
		break;
	case XBSON_TYPE_OBJECT_ID:
		if ((size_t)(end - *ptr) < 12) result = false;
		else { memcpy(value->data.oid, *ptr, 12); *ptr += 12; }
		break;
	case XBSON_TYPE_BOOL:
		if (*ptr >= end || **ptr > 1) result = false;
		else { value->data.boolean = **ptr != 0; ++(*ptr); }
		break;
	case XBSON_TYPE_DATETIME:
		result = XBsonValue_readU64(ptr, end, &value->data.datetime);
		break;
	case XBSON_TYPE_NULL:
		break;
	case XBSON_TYPE_REGEX: {
		XString* pattern = NULL;
		XString* options = NULL;
		result = XBsonValue_readCString(ptr, end, &pattern) &&
			XBsonValue_readCString(ptr, end, &options) &&
			XBsonValue_isSortedRegexOptions(options);
		if (result) {
			XString_delete_base(value->data.regex.pattern);
			XString_delete_base(value->data.regex.options);
			value->data.regex.pattern = pattern;
			value->data.regex.options = options;
		} else {
			XString_delete_base(pattern);
			XString_delete_base(options);
		}
		break;
	}
	case XBSON_TYPE_DBPOINTER:
		result = XBsonValue_readString(ptr, end, value->data.dbpointer.m_namespace) &&
			(size_t)(end - *ptr) >= 12;
		if (result) { memcpy(value->data.dbpointer.oid, *ptr, 12); *ptr += 12; }
		break;
	case XBSON_TYPE_JAVASCRIPT_SCOPE: {
		const uint8_t* value_start = *ptr;
		uint32_t total = 0;
		result = XBsonValue_readU32(ptr, end, &total) && total >= 14 &&
			value_start <= end && (size_t)(end - value_start) >= total;
		if (!result) break;
		const uint8_t* value_end = value_start + total;
		result = XBsonValue_readString(ptr, value_end, value->data.str);
		uint32_t scope_length = 0;
		if (result) result = XBsonValue_readEmbeddedLength(*ptr, value_end, &scope_length) &&
			XBsonDocument_from_bytes(value->data.doc, *ptr, scope_length);
		if (result) {
			*ptr += scope_length;
			result = *ptr == value_end;
		}
		break;
	}
	case XBSON_TYPE_INT32:
		if ((size_t)(end - *ptr) < 4) result = false;
		else { XMemory_read_data(*ptr, XBYTE_ORDER_LITTLE_ENDIAN, &value->data.int32, 4); *ptr += 4; }
		break;
	case XBSON_TYPE_TIMESTAMP:
		result = XBsonValue_readU32(ptr, end, &value->data.ts.increment) &&
			XBsonValue_readU32(ptr, end, &value->data.ts.timestamp);
		break;
	case XBSON_TYPE_INT64:
		result = XBsonValue_readU64(ptr, end, &value->data.int64);
		break;
	case XBSON_TYPE_DECIMAL128:
		if ((size_t)(end - *ptr) < 16) result = false;
		else { memcpy(value->data.decimal, *ptr, 16); *ptr += 16; }
		break;
	case XBSON_TYPE_MIN_KEY:
	case XBSON_TYPE_MAX_KEY:
		break;
	default:
		result = false;
		break;
	}
	if (!result) {
		XBsonValue_delete(value);
		XString_delete_base(key);
		return NULL;
	}
	if (key_out) *key_out = key;
	else XString_delete_base(key);
	return value;
}

XVariant* XBsonValue_toVariant(const XBsonValue* val)
{
	if (!val) return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XBsonValue), XVariantType_BsonValue);
	if (!var || !var->m_data) {
		XVariant_delete_base(var);
		return NULL;
	}
	XBsonValue_init((XBsonValue*)var->m_data, val->type);
	XBsonValue_copy((XBsonValue*)var->m_data, val);
	return var;
}

XVariant* XBsonValue_toVariant_move(XBsonValue* val)
{
	if (!val) return NULL;
	XVariant* var = XVariant_create(NULL, sizeof(XBsonValue), XVariantType_BsonValue);
	if (!var || !var->m_data) {
		XVariant_delete_base(var);
		return NULL;
	}
	XBsonValue_init((XBsonValue*)var->m_data, val->type);
	XBsonValue_move((XBsonValue*)var->m_data, val);
	return var;
}

XVariant* XBsonValue_toVariant_ref(XBsonValue* val)
{
	if (!val) return NULL;
	XVariant* var = XVariant_create(NULL, 0, XVariantType_BsonValue);
	if (!var) return NULL;
	var->m_data = val;
	var->m_dataSize = sizeof(XBsonValue);
	return var;
}
