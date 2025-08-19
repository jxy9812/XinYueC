#include "XBsonValue.h"
#include "XBsonArray.h"
#include "XBsonDocument.h"
#include "XMemory.h"
#include "XJsonObject.h"
#include "XJsonArray.h"
#include <string.h>
#include <stdio.h>

// 字节序转换函数
static uint32_t bson_read_uint32(const uint8_t** ptr) {
    uint32_t val = *(const uint32_t*)*ptr;
    *ptr += 4;
    return val;
}

static uint64_t bson_read_uint64(const uint8_t** ptr) {
    uint64_t val = *(const uint64_t*)*ptr;
    *ptr += 8;
    return val;
}

static void bson_write_uint32(uint8_t** ptr, uint32_t val) {
    *(uint32_t*)*ptr = val;
    *ptr += 4;
}

static void bson_write_uint64(uint8_t** ptr, uint64_t val) {
    *(uint64_t*)*ptr = val;
    *ptr += 8;
}

XBsonValue* XBsonValue_create(XBsonType type) {
    XBsonValue* value = (XBsonValue*)XMemory_malloc(sizeof(XBsonValue));
    if (value) {
        XBsonValue_init(value, type);
    }
    return value;
}

XBsonValue* XBsonValue_create_copy(const XBsonValue* other) {
    if (!other) return NULL;

    XBsonValue* value = XBsonValue_create(other->type);
    if (value) {
        XBsonValue_copy(value, other);
    }
    return value;
}

XBsonValue* XBsonValue_create_move(XBsonValue* other) {
    if (!other) return NULL;

    XBsonValue* value = XBsonValue_create(other->type);
    if (value) {
        XBsonValue_move(value, other);
    }
    return value;
}

XBsonValue* XBsonValue_create_null(void)
{
    XBsonValue* value = XBsonValue_create(XBSON_TYPE_NULL);
    //XBsonValue_setNull(value);
    return value;
}

XBsonValue* XBsonValue_create_bool(bool value)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setBool(val,value);
    return val;
}

XBsonValue* XBsonValue_create_double(double value)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setDouble(val, value);
    return val;
}

XBsonValue* XBsonValue_create_int32(int32_t value)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setInt32(val, value);
    return val;
}

XBsonValue* XBsonValue_create_int64(int64_t value)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setInt64(val, value);
    return val;
}

XBsonValue* XBsonValue_create_string(const XString* str)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setString(val, str);
    return val;
}

XBsonValue* XBsonValue_create_document(const XBsonDocument* doc)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setDocument(val, doc);
    return val;
}

XBsonValue* XBsonValue_create_array(const XBsonArray* arr)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setArray(val, arr);
    return val;
}

XBsonValue* XBsonValue_create_binary(XBsonBinarySubtype subtype, const XByteArray* data)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setBinary(val,subtype,data);
    return val;
}

XBsonValue* XBsonValue_create_object_id(const uint8_t* oid)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setObjectId(val, oid);
    return val;
}

XBsonValue* XBsonValue_create_datetime(int64_t timestamp)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setDatetime(val, timestamp);
    return val;
}

XBsonValue* XBsonValue_create_regex(const XString* pattern, const XString* options)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setRegex(val, pattern,options);
    return val;
}

XBsonValue* XBsonValue_create_javascript(const XString* code)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setJavascript(val, code);
    return val;
}

XBsonValue* XBsonValue_create_javascript_scope(const XString* code, const XBsonDocument* scope)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setJavascript_scope(val, code, scope);
    return val;
}

XBsonValue* XBsonValue_create_timestamp(uint32_t increment, uint32_t timestamp)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setTimestamp(val, increment,timestamp);
    return val;
}

XBsonValue* XBsonValue_create_decimal128(const uint8_t* decimal)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_NULL);
    XBsonValue_setDecimal128(val, decimal);
    return val;
}

XBsonValue* XBsonValue_create_min_key(void)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_MIN_KEY);
    return val;
}

XBsonValue* XBsonValue_create_max_key(void)
{
    XBsonValue* val = XBsonValue_create(XBSON_TYPE_MAX_KEY);
    return val;
}

void XBsonValue_init(XBsonValue* value, XBsonType type) {
    if (!value) return;

    value->type = type;
    memset(&value->data, 0, sizeof(value->data));

    switch (type) {
    case XBSON_TYPE_STRING:
    case XBSON_TYPE_JAVASCRIPT:
        value->data.str = XString_create();
        break;
    case XBSON_TYPE_DOCUMENT:
    case XBSON_TYPE_JAVASCRIPT_SCOPE:
        value->data.doc = XBsonDocument_create();
        break;
    case XBSON_TYPE_ARRAY:
        value->data.arr = XBsonArray_create();
        break;
    case XBSON_TYPE_BINARY:
        value->data.binary.data = XByteArray_create(0);
        value->data.binary.subtype = XBSON_BINARY_GENERIC;
        break;
    case XBSON_TYPE_REGEX:
        value->data.regex.pattern = XString_create();
        value->data.regex.options = XString_create();
        break;
    default:
        break;
    }
}

void XBsonValue_setNull(XBsonValue* value)
{
    if (!value) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_NULL;
}

void XBsonValue_setBool(XBsonValue* value, bool b)
{
    if (!value) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_BOOLEAN;
    value->data.boolean = b;
}

void XBsonValue_setDouble(XBsonValue* value, double d)
{
    if (!value) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_DOUBLE;
    value->data.dbl = d;
}

void XBsonValue_setInt32(XBsonValue* value, int32_t i)
{
    if (!value) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_INT32;
    value->data.int32 = i;
}

void XBsonValue_setInt64(XBsonValue* value, int64_t i)
{
    if (!value) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_INT64;
    value->data.int64 = i;
}

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
    value->data.str = XString_create_move(str); //将数据转移
}

void XBsonValue_setString_utf8(XBsonValue* value, const char* utf8)
{
    if (!value || !utf8) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_STRING;
    value->data.str = XString_create_utf8(utf8);
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
    value->data.doc = XBsonDocument_create_move(doc); // 
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

void XBsonValue_setBinary(XBsonValue* value, XBsonBinarySubtype subtype, const XByteArray* data)
{
    if (!value || !data) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_BINARY;
    value->data.binary.subtype = subtype;
    value->data.binary.data = XByteArray_create_copy(data);
}

void XBsonValue_setBinary_move(XBsonValue* value, XBsonBinarySubtype subtype, XByteArray* data)
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
    memcpy(value->data.oid, oid, 12);
}

void XBsonValue_setDatetime(XBsonValue* value, int64_t timestamp)
{
    if (!value) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_DATETIME;
    value->data.datetime = timestamp;
}

void XBsonValue_setRegex(XBsonValue* value, const XString* pattern, const XString* options)
{
    if (!value || !pattern || !options) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_REGEX;
    value->data.regex.pattern = XString_create_copy(pattern);
    value->data.regex.options = XString_create_copy(options);
}

void XBsonValue_setJavascript(XBsonValue* value, const XString* code)
{
    if (!value || !code) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_JAVASCRIPT;
    value->data.str = XString_create_copy(code);
}

void XBsonValue_setJavascript_scope(XBsonValue* value, const XString* code, const XBsonDocument* scope)
{
    if (!value || !code) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_JAVASCRIPT_SCOPE;
    value->data.str = XString_create_copy(code);
    value->data.doc = XBsonDocument_create_copy(scope);
}

void XBsonValue_setTimestamp(XBsonValue* value, uint32_t increment, uint32_t timestamp)
{
    if (!value) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_TIMESTAMP;
    value->data.ts.increment = increment;
    value->data.ts.timestamp = timestamp;
}

void XBsonValue_setDecimal128(XBsonValue* value, const uint8_t* decimal)
{
    if (!value) return;
    XBsonValue_deinit(value);
    value->type = XBSON_TYPE_DECIMAL128;
    memcpy(value->data.decimal, decimal, 16);
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
    return (value && value->type == XBSON_TYPE_BOOLEAN) ? value->data.boolean : defaultValue;
}

double XBsonValue_toDouble(const XBsonValue* value, double defaultValue)
{
    return (value && value->type == XBSON_TYPE_DOUBLE) ? value->data.dbl : defaultValue;
}

int32_t XBsonValue_toInt32(const XBsonValue* value, int32_t defaultValue)
{
    return (value && value->type == XBSON_TYPE_INT32) ? value->data.int32 : defaultValue;
}

int64_t XBsonValue_toInt64(const XBsonValue* value, int64_t defaultValue)
{
    if (value && value->type == XBSON_TYPE_INT64) {
        return value->data.int64;
    }
    else if (value && value->type == XBSON_TYPE_INT32) {
        return (int64_t)value->data.int32;
    }
    return defaultValue;
}

const XString* XBsonValue_toString(const XBsonValue* value)
{
    return (value && (value->type == XBSON_TYPE_STRING || value->type == XBSON_TYPE_JAVASCRIPT))
        ? value->data.str : NULL;
}

const XBsonDocument* XBsonValue_toDocument(const XBsonValue* value)
{
    return (value && (value->type == XBSON_TYPE_DOCUMENT || value->type == XBSON_TYPE_JAVASCRIPT_SCOPE))
        ? value->data.doc : NULL;
}

const XBsonArray* XBsonValue_toArray(const XBsonValue* value)
{
    return (value && value->type == XBSON_TYPE_ARRAY) ? value->data.arr : NULL;
}

const XByteArray* XBsonValue_toBinary(const XBsonValue* value, XBsonBinarySubtype* outSubtype)
{
    if (value && value->type == XBSON_TYPE_BINARY) {
        if (outSubtype) *outSubtype = value->data.binary.subtype;
        return value->data.binary.data;
    }
    if (outSubtype) *outSubtype = XBSON_BINARY_GENERIC;
    return NULL;
}

const uint8_t* XBsonValue_toObjectId(const XBsonValue* value)
{
    return (value && value->type == XBSON_TYPE_OBJECT_ID) ? value->data.oid : NULL;
}

int64_t XBsonValue_toDatetime(const XBsonValue* value, int64_t defaultValue)
{
    return (value && value->type == XBSON_TYPE_DATETIME) ? value->data.datetime : defaultValue;
}

const XString* XBsonValue_toRegexPattern(const XBsonValue* value)
{
    return (value && value->type == XBSON_TYPE_REGEX) ? value->data.regex.pattern : NULL;
}

const XString* XBsonValue_toRegexOptions(const XBsonValue* value)
{
    return (value && value->type == XBSON_TYPE_REGEX) ? value->data.regex.options : NULL;
}

bool XBsonValue_isNull(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_NULL;
}

bool XBsonValue_isBool(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_BOOLEAN;
}

bool XBsonValue_isDouble(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_DOUBLE;
}

bool XBsonValue_isInt32(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_INT32;
}

bool XBsonValue_isInt64(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_INT64;
}

bool XBsonValue_isString(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_STRING;
}

bool XBsonValue_isDocument(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_DOCUMENT;
}

bool XBsonValue_isArray(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_ARRAY;
}

bool XBsonValue_isBinary(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_BINARY;
}

bool XBsonValue_isObjectId(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_OBJECT_ID;
}

bool XBsonValue_isDatetime(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_DATETIME;
}

bool XBsonValue_isRegex(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_REGEX;
}

bool XBsonValue_isJavascript(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_JAVASCRIPT;
}

bool XBsonValue_isJavascriptScope(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_JAVASCRIPT_SCOPE;
}

bool XBsonValue_isTimestamp(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_TIMESTAMP;
}

bool XBsonValue_isDecimal128(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_DECIMAL128;
}

bool XBsonValue_isMinKey(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_MIN_KEY;
}

bool XBsonValue_isMaxKey(const XBsonValue* value)
{
    return XBsonValue_type(value) == XBSON_TYPE_MAX_KEY;
}

void XBsonValue_deinit(XBsonValue* value) {
    if (!value) return;

    switch (value->type) {
    case XBSON_TYPE_STRING:
    case XBSON_TYPE_JAVASCRIPT:
        XString_delete_base(value->data.str);
        break;
    case XBSON_TYPE_DOCUMENT:
    case XBSON_TYPE_JAVASCRIPT_SCOPE:
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
    default:
        break;
    }

    memset(value, 0, sizeof(XBsonValue));
    value->type = (XBsonType)0;
}

void XBsonValue_delete(XBsonValue* value) {
    if (value) {
        XBsonValue_deinit(value);
        XMemory_free(value);
    }
}

void XBsonValue_copy(XBsonValue* dest, const XBsonValue* src) {
    if (!dest || !src || dest == src) return;

    XBsonValue_deinit(dest);
    dest->type = src->type;

    switch (src->type) {
    case XBSON_TYPE_DOUBLE:
        dest->data.dbl = src->data.dbl;
        break;
    case XBSON_TYPE_STRING:
    case XBSON_TYPE_JAVASCRIPT:
        dest->data.str = XString_create_copy(src->data.str);
        break;
    case XBSON_TYPE_DOCUMENT:
    case XBSON_TYPE_JAVASCRIPT_SCOPE:
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
        memcpy(dest->data.oid, src->data.oid, 12);
        break;
    case XBSON_TYPE_BOOLEAN:
        dest->data.boolean = src->data.boolean;
        break;
    case XBSON_TYPE_DATETIME:
        dest->data.datetime = src->data.datetime;
        break;
    case XBSON_TYPE_NULL:
        break;
    case XBSON_TYPE_REGEX:
        dest->data.regex.pattern = XString_create_copy(src->data.regex.pattern);
        dest->data.regex.options = XString_create_copy(src->data.regex.options);
        break;
    case XBSON_TYPE_INT32:
        dest->data.int32 = src->data.int32;
        break;
    case XBSON_TYPE_TIMESTAMP:
        dest->data.ts = src->data.ts;
        break;
    case XBSON_TYPE_INT64:
        dest->data.int64 = src->data.int64;
        break;
    case XBSON_TYPE_DECIMAL128:
        memcpy(dest->data.decimal, src->data.decimal, 16);
        break;
    case XBSON_TYPE_MIN_KEY:
    case XBSON_TYPE_MAX_KEY:
        break;
    }
}

void XBsonValue_move(XBsonValue* dest, XBsonValue* src) {
    if (!dest || !src || dest == src) return;

    XBsonValue_deinit(dest);
    *dest = *src;
    memset(src, 0, sizeof(XBsonValue));
    src->type = (XBsonType)0;
}

XBsonType XBsonValue_type(const XBsonValue* value) {
    return value ? value->type : (XBsonType)0;
}

bool XBsonValue_is_type(const XBsonValue* value, XBsonType type) {
    return XBsonValue_type(value) == type;
}

XJsonValue* XBsonValue_to_json(const XBsonValue* bson_val) {
    if (!bson_val) return NULL;

    XJsonValue* json_val = XJsonValue_create_null();
    if (!json_val) return NULL;

    switch (bson_val->type) {
    case XBSON_TYPE_DOUBLE:
        XJsonValue_setDouble(json_val, bson_val->data.dbl);
        break;
    case XBSON_TYPE_STRING:
        XJsonValue_setString(json_val, bson_val->data.str);
        break;
    case XBSON_TYPE_DOCUMENT: {
        XJsonObject* json_obj = XBsonDocument_toJsonObject(bson_val->data.doc);
        XJsonValue_setObject_move(json_val, json_obj);
        XJsonObject_delete_base(json_obj);
        break;
    }
    case XBSON_TYPE_ARRAY: {
        XJsonArray* json_arr = XBsonArray_toJsonArray(bson_val->data.arr);
        XJsonValue_setArray_move(json_val, json_arr);
        XJsonArray_delete_base(json_arr);
        break;
    }
    case XBSON_TYPE_BINARY: {
        XJsonObject* bin_obj = XJsonObject_create();
        XJsonObject_insert_keyUtf8_int(bin_obj, "$binarySubtype", bson_val->data.binary.subtype);

        // 二进制数据转为十六进制字符串
        char* hex_str = (char*)XMemory_malloc(XByteArray_size_base(bson_val->data.binary.data) * 2 + 1);
        for (size_t i = 0; i < XByteArray_size_base(bson_val->data.binary.data); i++) {
            sprintf(hex_str + i * 2, "%02x", XByteArray_At_Base(bson_val->data.binary.data, i));
        }
        XJsonObject_insert_keyUtf8_utf8(bin_obj, "$binaryData", hex_str);
        XMemory_free(hex_str);

        XJsonValue_setObject_move(json_val, bin_obj);
        XJsonObject_delete_base(bin_obj);
        break;
    }
    case XBSON_TYPE_OBJECT_ID: {
        XJsonObject* oid_obj = XJsonObject_create();
        char oid_str[25] = { 0 };
        for (int i = 0; i < 12; i++) {
            sprintf(oid_str + i * 2, "%02x", bson_val->data.oid[i]);
        }
        XJsonObject_insert_keyUtf8_utf8(oid_obj, "$oid", oid_str);
        XJsonValue_setObject_move(json_val, oid_obj);
        XJsonObject_delete_base(oid_obj);
        break;
    }
    case XBSON_TYPE_BOOLEAN:
        XJsonValue_setBool(json_val, bson_val->data.boolean);
        break;
    case XBSON_TYPE_DATETIME: {
        XJsonObject* date_obj = XJsonObject_create();
        XJsonObject_insert_keyUtf8_int(date_obj, "$date", bson_val->data.datetime);
        XJsonValue_setObject_move(json_val, date_obj);
        XJsonObject_delete_base(date_obj);
        break;
    }
    case XBSON_TYPE_NULL:
        XJsonValue_setNull(json_val);
        break;
    case XBSON_TYPE_REGEX: {
        XJsonObject* regex_obj = XJsonObject_create();
        XJsonObject_insert_keyUtf8_string(regex_obj, "$regex", bson_val->data.regex.pattern);
        XJsonObject_insert_keyUtf8_string(regex_obj, "$options", bson_val->data.regex.options);
        XJsonValue_setObject_move(json_val, regex_obj);
        XJsonObject_delete_base(regex_obj);
        break;
    }
    case XBSON_TYPE_JAVASCRIPT: {
        XJsonObject* js_obj = XJsonObject_create();
        XJsonObject_insert_keyUtf8_string(js_obj, "$javascript", bson_val->data.str);
        XJsonValue_setObject_move(json_val, js_obj);
        XJsonObject_delete_base(js_obj);
        break;
    }
    case XBSON_TYPE_JAVASCRIPT_SCOPE: {
        XJsonObject* js_obj = XJsonObject_create();
        XString* str= XBsonDocument_toJson_string(bson_val->data.doc, XJsonDocument_Compact);
        XJsonObject_insert_keyUtf8_string(js_obj, "$javascript", str);
        XJsonValue_setObject_move(json_val, js_obj);
        XJsonObject_delete_base(js_obj);
        XString_delete_base(str);
        break;
    }
    case XBSON_TYPE_INT32:
        XJsonValue_setInt(json_val, bson_val->data.int32);
        break;
    case XBSON_TYPE_TIMESTAMP: {
        XJsonObject* ts_obj = XJsonObject_create();
        XJsonObject_insert_keyUtf8_int(ts_obj, "$timestamp", bson_val->data.ts.timestamp);
        XJsonObject_insert_keyUtf8_int(ts_obj, "$increment", bson_val->data.ts.increment);
        XJsonValue_setObject_move(json_val, ts_obj);
        XJsonObject_delete_base(ts_obj);
        break;
    }
    case XBSON_TYPE_INT64:
        XJsonValue_setInt(json_val, bson_val->data.int64);
        break;
    case XBSON_TYPE_DECIMAL128: {
        XJsonObject* dec_obj = XJsonObject_create();
        char dec_str[35] = { 0 }; // 十进制字符串表示
        // 简化实现，实际应转换为合适的字符串表示
        XJsonObject_insert_keyUtf8_utf8(dec_obj, "$decimal128", dec_str);
        XJsonValue_setObject_move(json_val, dec_obj);
        XJsonObject_delete_base(dec_obj);
        break;
    }
    case XBSON_TYPE_MIN_KEY: {
        XJsonObject* min_obj = XJsonObject_create();
        XJsonObject_insert_keyUtf8_null(min_obj, "$minKey");
        XJsonValue_setObject_move(json_val, min_obj);
        XJsonObject_delete_base(min_obj);
        break;
    }
    case XBSON_TYPE_MAX_KEY: {
        XJsonObject* max_obj = XJsonObject_create();
        XJsonObject_insert_keyUtf8_null(max_obj, "$maxKey");
        XJsonValue_setObject_move(json_val, max_obj);
        XJsonObject_delete_base(max_obj);
        break;
    }
    }

    return json_val;
}

XBsonValue* XBsonValue_from_json(const XJsonValue* json_val) 
{
    if (!json_val) return NULL;

    XBsonValue* bson_val = NULL;

    switch (XJsonValue_type(json_val)) 
    {
    case XJsonValue_Bool:
        bson_val = XBsonValue_create(XBSON_TYPE_BOOLEAN);
        bson_val->data.boolean = XJsonValue_toBool(json_val, false);
        break;
    case XJsonValue_Double:
        bson_val = XBsonValue_create(XBSON_TYPE_DOUBLE);
        bson_val->data.dbl = XJsonValue_toDouble(json_val, 0.0);
        break;
    case XJsonValue_Int: {
        int64_t val = XJsonValue_toInt(json_val, 0);
        if (val >= INT32_MIN && val <= INT32_MAX) {
            bson_val = XBsonValue_create(XBSON_TYPE_INT32);
            bson_val->data.int32 = (int32_t)val;
        }
        else {
            bson_val = XBsonValue_create(XBSON_TYPE_INT64);
            bson_val->data.int64 = val;
        }
        break;
    }
    case XJsonValue_String:
        bson_val = XBsonValue_create(XBSON_TYPE_STRING);
        XString_copy_base(bson_val->data.str, XJsonValue_toString(json_val));
        break;
    case XJsonValue_Array: {
        bson_val = XBsonValue_create(XBSON_TYPE_ARRAY);
        if (bson_val->data.arr)
            XBsonArray_delete_base(bson_val->data.arr);
        bson_val->data.arr=XBsonArray_fromJsonArray(XJsonValue_toArray(json_val));
        break;
    }
    case XJsonValue_Object: {
        XJsonObject* json_obj = XJsonValue_toObject(json_val);
        // 检查是否为特殊BSON类型的JSON表示
        if (XJsonObject_contains(json_obj, "$oid")) {
            bson_val = XBsonValue_create(XBSON_TYPE_OBJECT_ID);
            const XString* oid_str = XJsonValue_toString(XJsonObject_value_base(json_obj, "$oid"));
            // 解析十六进制字符串为12字节OID
            const char* oid_utf8 = XString_toUtf8(oid_str);
            for (int i = 0; i < 12; i++) {
                sscanf(oid_utf8 + i * 2, "%02hhx", &bson_val->data.oid[i]);
            }
        }
        else if (XJsonObject_contains(json_obj, "$binarySubtype") &&
            XJsonObject_contains(json_obj, "$binaryData")) {
            bson_val = XBsonValue_create(XBSON_TYPE_BINARY);
            bson_val->data.binary.subtype =
                (XBsonBinarySubtype)XJsonValue_toInt(XJsonObject_value_base(json_obj, "$binarySubtype"), 0);
            // 解析十六进制字符串为二进制数据
            const XString* bin_str = XJsonValue_toString(XJsonObject_value_base(json_obj, "$binaryData"));
            const char* bin_utf8 = XString_toUtf8(bin_str);
            size_t len = XString_length_base(bin_str) / 2;
            XByteArray_resize_base(bson_val->data.binary.data, len);
            for (size_t i = 0; i < len; i++) {
                sscanf(bin_utf8 + i * 2, "%02hhx", XByteArray_At_Base(bson_val->data.binary.data, i));
            }
        }
        else {
            bson_val = XBsonValue_create(XBSON_TYPE_DOCUMENT);
            if (bson_val->data.doc)
                XBsonDocument_delete_base(bson_val->data.doc);
            bson_val->data.doc=XBsonDocument_fromJsonObject(json_obj);
        }
        break;
    }
    case XJsonValue_Null:
        bson_val = XBsonValue_create(XBSON_TYPE_NULL);
        break;
    default:
        bson_val = XBsonValue_create(XBSON_TYPE_NULL);
        break;
    }

    return bson_val;
}

bool XBsonValue_serialize(const XBsonValue* value, const char* key, XByteArray* output) 
{
    if (!value || !output) return false;

    size_t start_pos = XByteArray_size_base(output);

    // 写入类型
    uint8_t type_byte = (uint8_t)value->type;
    XByteArray_push_back_base(output, type_byte);

    // 写入键
    if (key) {
        XByteArray_append_utf8(output, key);
        XByteArray_push_back_base(output, 0x00); // 键终止符
    }

    // 写入值
    //uint8_t* write_ptr = XByteArray_at_base(output, XByteArray_size_base(output));

    switch (value->type) {
    case XBSON_TYPE_DOUBLE: {
        //double d = value->data.dbl;
        double d=0.0; XMemory_write_data(&d, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, &(value->data.dbl), sizeof(double));;
        XByteArray_append_array_base(output,&d,sizeof(double));
        break;
    }
    case XBSON_TYPE_STRING: {
        uint32_t len = (uint32_t)XString_toUtf8_length(XBsonValue_toString(value)) + 1; // 包含终止符
        XMemory_Write_Var(&len, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, temp, uint32_t);
        XByteArray_append_array_base(output, &temp, sizeof(uint32_t));
        XByteArray_append_array_base(output, XString_toUtf8( XBsonValue_toString(value)), len - 1);
        XByteArray_push_back_base(output, 0x00);// 字符串终止符

       /* XPrint_utf8(key);
        printf("\t");
        XPrint(XBsonValue_toString(value));
        printf("\n");*/
        //bson_write_uint32(&write_ptr, len);
        //memcpy(write_ptr, XString_toUtf8(value->data.str), len - 1);
        //write_ptr += len - 1;
        //*write_ptr++ = 0x00; // 字符串终止符
        break;
    }
    case XBSON_TYPE_DOCUMENT: {
        XByteArray* doc_data = XBsonDocument_toBson(value->data.doc);
        XByteArray_append_array_base(output, XContainerDataPtr(doc_data), XByteArray_size_base(doc_data));
       /* memcpy(write_ptr, XContainerDataPtr(doc_data), XByteArray_size_base(doc_data));
        write_ptr += XByteArray_size_base(doc_data);*/
        XByteArray_delete_base(doc_data);
        break;
    }
    case XBSON_TYPE_ARRAY: {
        XByteArray* arr_data = XBsonArray_to_bytes(value->data.arr);
        XByteArray_append_array_base(output, XContainerDataPtr(arr_data), XByteArray_size_base(arr_data));
        /*memcpy(write_ptr, XContainerDataPtr(arr_data), XByteArray_size_base(arr_data));
        write_ptr += XByteArray_size_base(arr_data);*/
        XByteArray_delete_base(arr_data);
        break;
    }
    case XBSON_TYPE_BINARY: {
        uint32_t len = (uint32_t)XByteArray_size_base(value->data.binary.data);
        XMemory_Write_Var(&len, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, temp, uint32_t);
        XByteArray_append_array_base(output, &temp, sizeof(uint32_t));
        XByteArray_push_back_base(output, (uint8_t)value->data.binary.subtype);
        XByteArray_append_array_base(output, XContainerDataPtr(value->data.binary.data), len);
        //bson_write_uint32(&write_ptr, len);
        //*write_ptr++ = (uint8_t)value->data.binary.subtype;
        //memcpy(write_ptr, XContainerDataPtr(value->data.binary.data), len);
        //write_ptr += len;
        break;
    }
    case XBSON_TYPE_OBJECT_ID: 
    {
        XByteArray_append_array_base(output, value->data.oid, 12);
       /* memcpy(write_ptr, value->data.oid, 12);
        write_ptr += 12;*/
        break;
    }
    case XBSON_TYPE_BOOLEAN: 
    {
        XByteArray_push_back_base(output, value->data.boolean ? 0x01 : 0x00);
        //*write_ptr++ = value->data.boolean ? 0x01 : 0x00;
        break;
    }
    case XBSON_TYPE_DATETIME: 
    {
        XMemory_Write_Var(&(value->data.datetime), XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, temp, uint64_t);
        XByteArray_append_array_base(output, &temp, sizeof(uint64_t));
        //bson_write_uint64(&write_ptr, value->data.datetime);
        break;
    }
    case XBSON_TYPE_NULL:
        // 无数据
        break;
    case XBSON_TYPE_REGEX: {
        const char* pattern = XString_toUtf8(value->data.regex.pattern);
        size_t pattern_len = XString_toUtf8_length(value->data.regex.pattern);
        XByteArray_append_array_base(output, pattern, pattern_len);
        XByteArray_push_back_base(output,0x00);
        //memcpy(write_ptr, pattern, pattern_len);
        //write_ptr += pattern_len;
        //*write_ptr++ = 0x00;

        const char* options = XString_toUtf8(value->data.regex.options);
        size_t options_len = XString_toUtf8_length(value->data.regex.options);
        XByteArray_append_array_base(output, options, options_len);
        XByteArray_push_back_base(output, 0x00);
        /*memcpy(write_ptr, options, options_len);
        write_ptr += options_len;
        *write_ptr++ = 0x00;*/
        break;
    }
    case XBSON_TYPE_JAVASCRIPT: {
        uint32_t len = (uint32_t)XString_toUtf8_length(value->data.str) + 1;
        XMemory_Write_Var(&len, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, temp, uint32_t);
        XByteArray_append_array_base(output, &temp,sizeof(uint32_t));
        XByteArray_append_array_base(output, XString_toUtf8(value->data.str), len - 1);
        XByteArray_push_back_base(output,0x00);
        //bson_write_uint32(&write_ptr, len);
        //memcpy(write_ptr, XString_toUtf8(value->data.str), len - 1);
        //write_ptr += len - 1;
        //*write_ptr++ = 0x00;
        break;
    }
    case XBSON_TYPE_JAVASCRIPT_SCOPE: {
        // 先写入JavaScript代码
        uint32_t code_len = (uint32_t)XString_toUtf8_length(value->data.str) + 1;
        XMemory_Write_Var(&code_len, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, temp, uint32_t);
        XByteArray_append_array_base(output, &temp, sizeof(uint32_t));
        XByteArray_append_array_base(output, XString_toUtf8(value->data.str), code_len - 1);
        XByteArray_push_back_base(output,0x00);
       /* bson_write_uint32(&write_ptr, code_len);
        memcpy(write_ptr, XString_toUtf8(value->data.str), code_len - 1);
        write_ptr += code_len - 1;
        *write_ptr++ = 0x00;*/

        // 再写入作用域文档
        XByteArray* scope_data = XBsonDocument_toBson(value->data.doc);
        XByteArray_append_array_base(output, XContainerDataPtr(scope_data), XByteArray_size_base(scope_data));
      /*  memcpy(write_ptr, XContainerDataPtr(scope_data), XByteArray_size_base(scope_data));
        write_ptr += XByteArray_size_base(scope_data);*/
        XByteArray_delete_base(scope_data);
        break;
    }
    case XBSON_TYPE_INT32: 
    {
        XMemory_Write_Var(&(value->data.int32), XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, temp, int32_t);
        XByteArray_append_array_base(output, &temp, sizeof(int32_t));
        //bson_write_uint32(&write_ptr, (uint32_t)value->data.int32);
        break;
    }
    case XBSON_TYPE_TIMESTAMP: 
    {
        XMemory_Write_Var(&(value->data.ts.increment), XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, increment, uint32_t);
        XByteArray_append_array_base(output, &increment, sizeof(uint32_t));
        XMemory_Write_Var(&(value->data.ts.timestamp), XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, timestamp, uint32_t);
        XByteArray_append_array_base(output, &timestamp, sizeof(uint32_t));
      /*  bson_write_uint32(&write_ptr, value->data.ts.increment);
        bson_write_uint32(&write_ptr, value->data.ts.timestamp);*/
        break;
    }
    case XBSON_TYPE_INT64: 
    {
        XMemory_Write_Var(&(value->data.int64), XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, temp, int64_t);
        XByteArray_append_array_base(output, &temp, sizeof(int64_t));
        //bson_write_uint64(&write_ptr, (uint64_t)value->data.int64);
        break;
    }
    case XBSON_TYPE_DECIMAL128: {
        XByteArray_append_array_base(output, &(value->data.decimal), 16);
        //memcpy(write_ptr, value->data.decimal, 16);
        //write_ptr += 16;
        break;
    }
    case XBSON_TYPE_MIN_KEY:
    case XBSON_TYPE_MAX_KEY:
        // 无数据
        break;
    default:
        return false;
    }

    // 调整输出缓冲区大小
    //XByteArray_resize_base(output, write_ptr - (uint8_t*)XContainerDataPtr(output));
    return true;
}

XBsonValue* XBsonValue_deserialize(const uint8_t** ptr, const uint8_t* end, XString** key_out) 
{
    if (!ptr || !*ptr || *ptr >= end) return NULL;

    // 读取类型
    XBsonType type = (XBsonType) **ptr;
    (*ptr)++;

    // 读取键
    XString* key = NULL;
    if (type != XBSON_TYPE_MIN_KEY && type != XBSON_TYPE_MAX_KEY) {
        const char* key_start = (const char*)*ptr;
        while (*ptr < end && **ptr != 0x00) {
            (*ptr)++;
        }
        if (*ptr >= end) return NULL;

        key = XString_create_with_length_utf8(key_start, *ptr - (const uint8_t*)key_start);
      /*  XPrint(key);
        printf("\n");*/
        (*ptr)++; // 跳过终止符
    }

    if (key_out) 
    {
        *key_out = key;
    }
    else if (key) 
    {
        XString_delete_base(key);
    }

    // 创建值对象
    XBsonValue* value = XBsonValue_create(type);
    if (!value) return NULL;

    // 解析值
    switch (type)
    {
    case XBSON_TYPE_DOUBLE:
        if (*ptr + 8 > end) goto error;
        XMemory_read_data(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, &(value->data.dbl), sizeof(double));
        *ptr += 8;
        break;
    case XBSON_TYPE_STRING: {
        if (*ptr + 4 > end) goto error;
        //uint32_t len = *((uint32_t*)ptr);
        XMemory_Read_Var(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN,len,uint32_t);
        //XMemory_read_data(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, &(value->data.dbl), sizeof(double));
        (*ptr) += sizeof(uint32_t);
        if (*ptr + len > end) goto error;
        XString_assign_with_length_utf8(value->data.str, (const char*)*ptr, len - 1);
        (*ptr)+= len; // 包含终止符
        break;
    }
    case XBSON_TYPE_DOCUMENT: {
        if (*ptr + 4 > end) goto error;
        //uint32_t len = *((uint32_t*)ptr);
        XMemory_Read_Var(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, len, uint32_t);
        //(*ptr) += sizeof(uint32_t);
        const uint8_t* doc_end = *ptr + len - 1; // 减去长度字段本身
        if (doc_end > end) goto error;
        XBsonDocument_from_bytes(value->data.doc, *ptr, len);
        *ptr = doc_end + 1; // 跳过终止符
        break;
    }
    case XBSON_TYPE_ARRAY: {
        if (*ptr + 4 > end) goto error;
        //uint32_t len = *((uint32_t*)ptr);
        XMemory_Read_Var(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, len, uint32_t);
        //(*ptr) += sizeof(uint32_t);
        const uint8_t* arr_end = *ptr + len - 1;
        if (arr_end > end) goto error;

        XBsonArray_from_bytes(value->data.arr, *ptr, len);
        *ptr = arr_end + 1;
        break;
    }
    case XBSON_TYPE_BINARY: {
        if (*ptr + 5 > end) goto error; // 4字节长度 + 1字节子类型
        //uint32_t len = *((uint32_t*)ptr);
        XMemory_Read_Var(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, len, uint32_t);
        (*ptr) += sizeof(uint32_t);
        value->data.binary.subtype = (XBsonBinarySubtype) * *ptr;
        (*ptr)++;

        if (*ptr + len > end) goto error;
        XByteArray_resize_base(value->data.binary.data, len);
        memcpy(XContainerDataPtr(value->data.binary.data), *ptr, len);
        (*ptr) += len;
        break;
    }
    case XBSON_TYPE_OBJECT_ID:
        if (*ptr + 12 > end) goto error;
        memcpy(value->data.oid, *ptr, 12);
        (*ptr) += 12;
        break;
    case XBSON_TYPE_BOOLEAN:
        if (*ptr + 1 > end) goto error;
        value->data.boolean = **ptr != 0x00;
        (*ptr)++;
        break;
    case XBSON_TYPE_DATETIME:
        if (*ptr + 8 > end) goto error;
        //value->data.datetime = *((uint64_t*)ptr);
        XMemory_read_data(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, &(value->data.datetime), sizeof(uint64_t));
        (*ptr) += sizeof(uint64_t);
        break;
    case XBSON_TYPE_NULL:
        // 无数据
        break;
    case XBSON_TYPE_REGEX: {
        // 读取模式
        const char* pattern_start = (const char*)*ptr;
        while (*ptr < end && **ptr != 0x00) (*ptr)++;
        if (*ptr >= end) goto error;
        XString_assign_with_length_utf8(value->data.regex.pattern, pattern_start, *ptr - (const uint8_t*)pattern_start);
        (*ptr)++;

        // 读取选项
        const char* options_start = (const char*)*ptr;
        while (*ptr < end && **ptr != 0x00) (*ptr)++;
        if (*ptr >= end) goto error;
        XString_assign_with_length_utf8(value->data.regex.options, options_start, *ptr - (const uint8_t*)options_start);
        (*ptr)++;
        break;
    }
    case XBSON_TYPE_JAVASCRIPT: {
        if (*ptr + 4 > end) goto error;
        //uint32_t len = *((uint32_t*)ptr);
        XMemory_Read_Var(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, len, uint32_t);
        (*ptr) += sizeof(uint32_t);
        if (*ptr + len > end) goto error;
        XString_assign_with_length_utf8(value->data.str, (const char*)*ptr, len - 1);
        *ptr += len;
        break;
    }
    case XBSON_TYPE_JAVASCRIPT_SCOPE: {
        if (*ptr + 4 > end) goto error;
        //uint32_t code_len = *((uint32_t*)ptr);
        XMemory_Read_Var(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, code_len, uint32_t);
        (*ptr) += sizeof(uint32_t);
        if (*ptr + code_len > end) goto error;
        XString_assign_with_length_utf8(value->data.str, (const char*)*ptr, code_len - 1);
        *ptr += code_len;

        // 解析作用域文档
        if (*ptr + 4 > end) goto error;
        //uint32_t scope_len = *((uint32_t*)ptr);
        XMemory_Read_Var(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, scope_len, uint32_t);
        (*ptr) += sizeof(uint32_t);
        const uint8_t* scope_end = *ptr + scope_len - 1;
        if (scope_end > end) goto error;

        XBsonDocument_from_bytes(value->data.doc, *ptr, scope_len);
        *ptr = scope_end + 1;
        break;
    }
    case XBSON_TYPE_INT32:
        if (*ptr + 4 > end) goto error;
        XMemory_read_data(*ptr,XMEMORY_BYTE_ORDER_LITTLE_ENDIAN,&(value->data.int32), sizeof(int32_t));
        (*ptr) += sizeof(int32_t);
        break;
    case XBSON_TYPE_TIMESTAMP:
        if (*ptr + 8 > end) goto error;
        XMemory_read_data(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, &(value->data.ts.increment), sizeof(uint32_t));
        //value->data.ts.increment = *((uint32_t*)ptr);
        (*ptr) += sizeof(uint32_t);
        XMemory_read_data(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, &(value->data.ts.timestamp), sizeof(uint32_t));
        //value->data.ts.timestamp = *((uint32_t*)ptr);
        (*ptr) += sizeof(uint32_t);
        break;
    case XBSON_TYPE_INT64:
        if (*ptr + 8 > end) goto error;
        XMemory_read_data(*ptr, XMEMORY_BYTE_ORDER_LITTLE_ENDIAN, &(value->data.int64), sizeof(int64_t));
        //value->data.int64 = *((int64_t*)ptr);
        (*ptr) += sizeof(int64_t);
        break;
    case XBSON_TYPE_DECIMAL128:
        if (*ptr + 16 > end) goto error;
        memcpy(value->data.decimal, *ptr, 16);
        *ptr += 16;
        break;
    case XBSON_TYPE_MIN_KEY:
    case XBSON_TYPE_MAX_KEY:
        // 无数据
        break;
    default:
        goto error;
    }

    return value;

error:
    XBsonValue_delete(value);
    if (key_out && *key_out) {
        XString_delete_base(*key_out);
        *key_out = NULL;
    }
    return NULL;
}