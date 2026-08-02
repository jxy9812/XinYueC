/**
 * @file       XMySqlDriver.c
 * @brief      MySQL/MariaDB XSql 驱动实现。
 * @details    结果集在驱动层转为 XVariant 缓存，客户端协议对象只在执行
 *             期间存在；因此 XSqlResult 的复制、移动和随机定位不依赖
 *             第三方客户端的结果对象生命周期。
 */
#include "XMySqlDriver.h"

#include "XMemory.h"
#include "XSqlDatabase.h"
#include "XSqlField.h"
#include "XSqlIndex.h"
#include "XSqlRecord.h"
#include "XSqlResult.h"
#include "XString.h"
#include "XByteArray.h"
#include "XVariantList.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define XMYSQL_NATIVE_TINY 1u
#define XMYSQL_NATIVE_SHORT 2u
#define XMYSQL_NATIVE_LONG 3u
#define XMYSQL_NATIVE_INT24 9u
#define XMYSQL_NATIVE_BIT 16u
#define XMYSQL_NATIVE_YEAR 13u
#define XMYSQL_NATIVE_TIMESTAMP 7u
#define XMYSQL_NATIVE_DATE 10u
#define XMYSQL_NATIVE_TIME 11u
#define XMYSQL_NATIVE_DATETIME 12u
#define XMYSQL_NATIVE_NEWDATE 14u
#define XMYSQL_NATIVE_TIMESTAMP2 17u
#define XMYSQL_NATIVE_DATETIME2 18u
#define XMYSQL_NATIVE_TIME2 19u
#define XMYSQL_FIELD_NOT_NULL 0x0001u
#define XMYSQL_FIELD_AUTO_INCREMENT 0x0200u

typedef struct XMySqlResult {
    XSqlResult m_parent;
    XVariant*** m_rows;
    size_t m_rowCount;
    size_t m_rowCapacity;
    int m_columnCount;
    XSqlMySqlResult* m_raw;
    bool m_prepared;
} XMySqlResult;

typedef struct XMySqlDriver {
    XSqlDriver m_parent;
    const XSqlMySqlClientApi* m_api;
    XSqlMySqlClient* m_client;
} XMySqlDriver;

XCLASS_DEFINE_BEGING(XMySqlResult)
XCLASS_DEFINE_EXTEND_END(XMySqlResult, XSqlResult)

XCLASS_DEFINE_BEGING(XMySqlDriver)
XCLASS_DEFINE_EXTEND_END(XMySqlDriver, XSqlDriver)

static const XSqlMySqlClientApi* g_xmysql_client_api = NULL;

static XVtable* XMySqlResult_class_init(void);
static XVtable* XMySqlDriver_class_init(void);

static void VXMySqlResult_copy(XMySqlResult* dest, const XMySqlResult* src);
static void VXMySqlResult_move(XMySqlResult* dest, XMySqlResult* src);
static void VXMySqlResult_deinit(XMySqlResult* result);
static XVariant* VXMySqlResult_data(XSqlResult* result, int field);
static bool VXMySqlResult_isNull(XSqlResult* result, int field);
static bool VXMySqlResult_reset(XSqlResult* result, const XString* query);
static bool VXMySqlResult_fetch(XSqlResult* result, int index);
static int VXMySqlResult_size(XSqlResult* result);
static bool VXMySqlResult_prepare(XSqlResult* result, const XString* query);
static bool VXMySqlResult_exec(XSqlResult* result);
static bool VXMySqlResult_nextResult(XSqlResult* result);
static bool VXMySqlResult_execBatch(XSqlResult* result, XSqlBatchExecutionMode mode);
static void VXMySqlResult_detachFromResultSet(XSqlResult* result);
static void* VXMySqlResult_handle(XSqlResult* result);

static void VXMySqlDriver_deinit(XMySqlDriver* driver);
static bool VXMySqlDriver_beginTransaction(XSqlDriver* driver);
static bool VXMySqlDriver_commitTransaction(XSqlDriver* driver);
static bool VXMySqlDriver_rollbackTransaction(XSqlDriver* driver);
static XStringList* VXMySqlDriver_tables(const XSqlDriver* driver, XSqlTableType type);
static XSqlIndex* VXMySqlDriver_primaryIndex(const XSqlDriver* driver, const XString* tableName);
static XSqlRecord* VXMySqlDriver_record(const XSqlDriver* driver, const XString* tableName);
static XString* VXMySqlDriver_formatValue(const XSqlDriver* driver, const XSqlField* field, bool trimStrings);
static XString* VXMySqlDriver_escapeIdentifier(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type);
static void* VXMySqlDriver_handle(const XSqlDriver* driver);
static bool VXMySqlDriver_hasFeature(const XSqlDriver* driver, XSqlDriverFeature feature);
static void VXMySqlDriver_close(XSqlDriver* driver);
static XSqlResult* VXMySqlDriver_createResult(const XSqlDriver* driver);
static bool VXMySqlDriver_open(XSqlDriver* driver, const XString* database,
                               const XString* user, const XString* password,
                               const XString* host, int port, const XString* options);
static bool VXMySqlDriver_isIdentifierEscaped(const XSqlDriver* driver,
                                              const XString* identifier,
                                              XSqlIdentifierType type);
static XString* VXMySqlDriver_stripDelimiters(const XSqlDriver* driver,
                                              const XString* identifier,
                                              XSqlIdentifierType type);
static bool VXMySqlDriver_cancelQuery(XSqlDriver* driver);

static void xmysql_clear_rows(XMySqlResult* result)
{
    size_t row;
    int field;
    if (!result) return;
    for (row = 0; row < result->m_rowCount; ++row) {
        if (!result->m_rows[row]) continue;
        for (field = 0; field < result->m_columnCount; ++field)
            if (result->m_rows[row][field]) XVariant_delete_base(result->m_rows[row][field]);
        XFree_System(result->m_rows[row]);
    }
    if (result->m_rows) XFree_System(result->m_rows);
    result->m_rows = NULL;
    result->m_rowCount = 0;
    result->m_rowCapacity = 0;
}

static bool xmysql_reserve_rows(XMySqlResult* result, size_t wanted)
{
    size_t capacity;
    XVariant*** rows;
    if (!result) return false;
    if (wanted <= result->m_rowCapacity) return true;
    capacity = result->m_rowCapacity ? result->m_rowCapacity * 2 : 8;
    while (capacity < wanted) capacity *= 2;
    rows = (XVariant***)XRealloc_System(result->m_rows, capacity * sizeof(*rows));
    if (!rows) return false;
    result->m_rows = rows;
    result->m_rowCapacity = capacity;
    return true;
}

static void xmysql_release_raw(XMySqlResult* result)
{
    const XMySqlDriver* driver;
    if (!result || !result->m_raw) return;
    driver = (const XMySqlDriver*)result->m_parent.m_driver;
    if (driver && driver->m_api && driver->m_api->resultDestroy)
        driver->m_api->resultDestroy(result->m_raw);
    result->m_raw = NULL;
}

static void xmysql_clear_result_state(XMySqlResult* result)
{
    if (!result) return;
    xmysql_clear_rows(result);
    XSqlRecord_clear(&result->m_parent.m_record);
    XSqlError_deinit_base(&result->m_parent.m_lastError);
    XSqlError_init(&result->m_parent.m_lastError);
    XVariant_setValue_null(&result->m_parent.m_lastInsertId);
    result->m_parent.m_at = XSqlLocation_BeforeFirstRow;
    result->m_parent.m_size = -1;
    result->m_parent.m_numRowsAffected = -1;
    result->m_parent.m_active = false;
    result->m_parent.m_select = false;
    result->m_columnCount = 0;
}

static int xmysql_variant_type(XSqlMySqlValueType type, bool isUnsigned,
                               uint8_t nativeType, XSqlNumericalPrecisionPolicy policy)
{
    (void)policy;
    switch (type) {
    case XSqlMySqlValueType_Integer:
    case XSqlMySqlValueType_UnsignedInteger:
        switch (nativeType) {
        case 1u: return isUnsigned ? XVariantType_UChar : XVariantType_Char;
        case 2u: return isUnsigned ? XVariantType_Uint16 : XVariantType_Int16;
        case 3u:
        case 9u: return isUnsigned ? XVariantType_Uint32 : XVariantType_Int32;
        case 13u: return XVariantType_Int32;
        case 16u:
        case 8u: return isUnsigned ? XVariantType_Uint64 : XVariantType_Int64;
        default: return isUnsigned ? XVariantType_Uint64 : XVariantType_Int64;
        }
    case XSqlMySqlValueType_Real: return XVariantType_Double;
    case XSqlMySqlValueType_ByteArray: return XVariantType_ByteArray;
    case XSqlMySqlValueType_Null: return XVariantType_NULL;
    case XSqlMySqlValueType_DateTime:
        if (nativeType == XMYSQL_NATIVE_DATE || nativeType == XMYSQL_NATIVE_NEWDATE)
            return XVariantType_Date;
        if (nativeType == XMYSQL_NATIVE_TIME || nativeType == XMYSQL_NATIVE_TIME2)
            return XVariantType_String;
        if (nativeType == XMYSQL_NATIVE_TIMESTAMP || nativeType == XMYSQL_NATIVE_DATETIME
            || nativeType == XMYSQL_NATIVE_TIMESTAMP2 || nativeType == XMYSQL_NATIVE_DATETIME2)
            return XVariantType_DateTime;
        return XVariantType_String;
    default: return XVariantType_String;
    }
}

static XVariant* xmysql_value_variant(const XSqlMySqlField* field,
                                      const XSqlMySqlValue* value,
                                      XSqlNumericalPrecisionPolicy policy)
{
    XString* text;
    const char* utf8;
    if (!value || value->m_isNull) return XVariant_create_null();
    if (value->m_type == XSqlMySqlValueType_ByteArray)
        return XVariant_create_byteArray(value->m_data, value->m_size);
    text = XString_create_with_length_utf8((const char*)value->m_data, value->m_size);
    if (!text) return NULL;
    utf8 = XString_toUtf8(text);
    if (field && field->m_nativeType == XMYSQL_NATIVE_BIT) {
        uint64_t number = utf8 ? strtoull(utf8, NULL, 10) : 0;
        XVariant* result = XVariant_create_uint64(number);
        XString_delete_base(text);
        return result;
    }
    if (field && (field->m_nativeType == 0u || field->m_nativeType == 4u
                  || field->m_nativeType == 5u || field->m_nativeType == 246u)
        && policy == XSqlNumericalPrecisionPolicy_HighPrecision) {
        XVariant* result = XVariant_create_String_move(text);
        XString_delete_base(text);
        return result;
    }
    if (field && (field->m_nativeType == XMYSQL_NATIVE_DATE
                  || field->m_nativeType == XMYSQL_NATIVE_NEWDATE)) {
        XDate date = XDate_fromString_iso(utf8);
        XVariant* result = XVariant_create_Date(&date);
        XString_delete_base(text);
        return result;
    }
    if (field && (field->m_nativeType == XMYSQL_NATIVE_TIMESTAMP
                  || field->m_nativeType == XMYSQL_NATIVE_DATETIME
                  || field->m_nativeType == XMYSQL_NATIVE_TIMESTAMP2
                  || field->m_nativeType == XMYSQL_NATIVE_DATETIME2)) {
        XDateTime datetime = XDateTime_fromString_iso(utf8);
        XVariant* result = XVariant_create_DateTime(&datetime);
        XString_delete_base(text);
        return result;
    }
    switch (value->m_type) {
    case XSqlMySqlValueType_Integer:
        {
            long long number = utf8 ? strtoll(utf8, NULL, 10) : 0;
            XVariant* result = field && (field->m_nativeType == XMYSQL_NATIVE_TINY
                                         || field->m_nativeType == XMYSQL_NATIVE_SHORT
                                         || field->m_nativeType == XMYSQL_NATIVE_LONG
                                         || field->m_nativeType == XMYSQL_NATIVE_INT24
                                         || field->m_nativeType == XMYSQL_NATIVE_YEAR)
                ? XVariant_create_int32((int32_t)number)
                : XVariant_create_int64((int64_t)number);
            XString_delete_base(text);
            return result;
        }
    case XSqlMySqlValueType_UnsignedInteger:
        {
            unsigned long long number = utf8 ? strtoull(utf8, NULL, 10) : 0;
            XVariant* result = field && field->m_nativeType == XMYSQL_NATIVE_YEAR
                ? XVariant_create_int32((int32_t)number)
                : field && field->m_nativeType == XMYSQL_NATIVE_BIT
                ? XVariant_create_uint64((uint64_t)number)
                : field && (field->m_nativeType == XMYSQL_NATIVE_TINY
                            || field->m_nativeType == XMYSQL_NATIVE_SHORT
                            || field->m_nativeType == XMYSQL_NATIVE_LONG
                            || field->m_nativeType == XMYSQL_NATIVE_INT24
                            || field->m_nativeType == XMYSQL_NATIVE_YEAR)
                    ? XVariant_create_uint32((uint32_t)number)
                    : XVariant_create_uint64((uint64_t)number);
            XString_delete_base(text);
            return result;
        }
    case XSqlMySqlValueType_Real:
        {
            char* end = NULL;
            double number = utf8 ? strtod(utf8, &end) : 0.0;
            XVariant* result;
            if (!utf8 || !end || end == utf8 || *end != 0) {
                XString_delete_base(text);
                return XVariant_create_null();
            }
            switch (policy) {
            case XSqlNumericalPrecisionPolicy_LowPrecisionInt32:
                result = XVariant_create_int32((int32_t)number);
                break;
            case XSqlNumericalPrecisionPolicy_LowPrecisionInt64:
                result = XVariant_create_int64((int64_t)number);
                break;
            case XSqlNumericalPrecisionPolicy_HighPrecision:
                result = XVariant_create_String_move(text);
                XString_delete_base(text);
                return result;
            case XSqlNumericalPrecisionPolicy_LowPrecisionDouble:
            default:
                result = XVariant_create_double(number);
                break;
            }
            XString_delete_base(text);
            return result;
        }
    default:
        {
            XVariant* result = XVariant_create_String_move(text);
            XString_delete_base(text);
            return result;
        }
    }
}

static bool xmysql_build_record(XMySqlResult* result, XSqlMySqlResult* raw,
                                const XSqlMySqlClientApi* api)
{
    int field;
    if (!result || !raw || !api) return false;
    result->m_columnCount = api->resultColumnCount(raw);
    XSqlRecord_clear(&result->m_parent.m_record);
    for (field = 0; field < result->m_columnCount; ++field) {
        const XSqlMySqlField* info = api->resultField(raw, field);
        XString* name = XString_create_utf8(info && info->m_name ? info->m_name : "");
        XString* table = XString_create_utf8(info && info->m_table ? info->m_table : "");
        XSqlField* sqlField = info
            ? XSqlField_create_ex(name, xmysql_variant_type(info->m_type, info->m_unsigned,
                                                             info->m_nativeType,
                                                             result->m_parent.m_precisionPolicy), table)
            : NULL;
        bool appended = sqlField && XSqlRecord_append(&result->m_parent.m_record, sqlField);
        if (sqlField && info) {
            XSqlField_setLength(sqlField, (int)info->m_length);
            XSqlField_setPrecision(sqlField, (int)info->m_decimals);
            XSqlField_setRequired(sqlField, (info->m_flags & XMYSQL_FIELD_NOT_NULL) != 0);
            XSqlField_setAutoValue(sqlField, (info->m_flags & XMYSQL_FIELD_AUTO_INCREMENT) != 0);
        }
        if (name) XString_delete_base(name);
        if (table) XString_delete_base(table);
        if (sqlField) XSqlField_delete_base(sqlField);
        if (!appended) return false;
    }
    return true;
}

static bool xmysql_cache_rows(XMySqlResult* result, XSqlMySqlResult* raw,
                              const XSqlMySqlClientApi* api)
{
    int row;
    int field;
    int size;
    if (!result || !raw || !api || !api->resultIsSelect(raw)) return true;
    size = api->resultSize(raw);
    if (size < 0) return false;
    for (row = 0; row < size; ++row) {
        XVariant** values;
        if (!api->resultFetch(raw, row) || !xmysql_reserve_rows(result, result->m_rowCount + 1)) return false;
        values = (XVariant**)XCalloc_System((size_t)result->m_columnCount, sizeof(*values));
        if (!values && result->m_columnCount > 0) return false;
        result->m_rows[result->m_rowCount++] = values;
        for (field = 0; field < result->m_columnCount; ++field) {
            const XSqlMySqlField* info = api->resultField(raw, field);
            const XSqlMySqlValue* value = api->resultValue(raw, field);
            values[field] = xmysql_value_variant(info, value, result->m_parent.m_precisionPolicy);
            if (!values[field]) return false;
        }
    }
    return true;
}

static void xmysql_set_result_error(XMySqlResult* result, const XSqlMySqlError* source,
                                    const char* fallback)
{
    XSqlError* error;
    XString* driverText;
    XString* databaseText;
    XString* errorCode;
    const char* driver = source && source->m_driverText && source->m_driverText[0]
        ? source->m_driverText : fallback;
    if (!result) return;
    driverText = XString_create_utf8(driver ? driver : "MySQL operation failed");
    databaseText = XString_create_utf8(source && source->m_databaseText ? source->m_databaseText : "");
    errorCode = XString_create_utf8(source && source->m_errorCode ? source->m_errorCode : "0");
    error = XSqlError_create(driverText, databaseText,
                             source ? source->m_type : XSqlErrorType_UnknownError, errorCode);
    if (driverText) XString_delete_base(driverText);
    if (databaseText) XString_delete_base(databaseText);
    if (errorCode) XString_delete_base(errorCode);
    if (error) {
        XSqlResult_setLastError_base(&result->m_parent, error);
        XSqlError_delete_base(error);
    }
}

static size_t xmysql_utf8_sequence_size(const char* text, size_t remaining)
{
    const unsigned char* bytes = (const unsigned char*)text;
    size_t size;
    size_t index;
    if (!bytes || remaining == 0) return 0;
    if (bytes[0] < 0x80u) return 1;
    if (bytes[0] >= 0xc2u && bytes[0] <= 0xdfu) size = 2;
    else if (bytes[0] >= 0xe0u && bytes[0] <= 0xefu) size = 3;
    else if (bytes[0] >= 0xf0u && bytes[0] <= 0xf4u) size = 4;
    else return 0;
    if (size > remaining) return 0;
    for (index = 1; index < size; ++index)
        if ((bytes[index] & 0xc0u) != 0x80u) return 0;
    return size;
}

static XString* xmysql_date_variant_text(const XVariant* value)
{
    int type;
    if (!value) return NULL;
    type = XVariant_type((XVariant*)value);
    if (type == XVariantType_Date) {
        const XDate* date = XVariant_toDate_ref(value);
        return date && XDate_isValid(date)
            ? XString_create_fmt_utf8("%04d-%02d-%02d", XDate_year(date),
                                      XDate_month(date), XDate_day(date)) : NULL;
    }
    if (type == XVariantType_Time) {
        const XTime* time = XVariant_toTime_ref(value);
        return time && XTime_isValid(time)
            ? XString_create_fmt_utf8("%02d:%02d:%02d.%03d", XTime_hour(time),
                                      XTime_minute(time), XTime_second(time), XTime_msec(time)) : NULL;
    }
    if (type == XVariantType_DateTime) {
        const XDateTime* datetime = XVariant_toDateTime_ref(value);
        const XDate* date = datetime ? &datetime->m_date : NULL;
        const XTime* time = datetime ? &datetime->m_time : NULL;
        return datetime && XDateTime_isValid(datetime)
            ? XString_create_fmt_utf8("%04d-%02d-%02dT%02d:%02d:%02d.%03d",
                                      XDate_year(date), XDate_month(date), XDate_day(date),
                                      XTime_hour(time), XTime_minute(time),
                                      XTime_second(time), XTime_msec(time)) : NULL;
    }
    return NULL;
}

static bool xmysql_append_escaped_text(XString* query, const char* text, size_t size)
{
    size_t index;
    if (!query || (!text && size != 0)) return false;
    if (!XString_append_utf8(query, "'")) return false;
    for (index = 0; index < size; ++index) {
        char escaped[3];
        size_t characterSize;
        if ((unsigned char)text[index] >= 0x80u) {
            characterSize = xmysql_utf8_sequence_size(text + index, size - index);
            if (characterSize == 0
                || !XString_append_with_length_utf8(query, text + index, characterSize))
                return false;
            index += characterSize - 1;
            continue;
        }
        switch ((unsigned char)text[index]) {
        case 0: escaped[0] = '\\'; escaped[1] = '0'; escaped[2] = 0; break;
        case '\n': escaped[0] = '\\'; escaped[1] = 'n'; escaped[2] = 0; break;
        case '\r': escaped[0] = '\\'; escaped[1] = 'r'; escaped[2] = 0; break;
        case '\\': escaped[0] = '\\'; escaped[1] = '\\'; escaped[2] = 0; break;
        case '\'': escaped[0] = '\\'; escaped[1] = '\''; escaped[2] = 0; break;
        case '"': escaped[0] = '\\'; escaped[1] = '"'; escaped[2] = 0; break;
        case 0x1a: escaped[0] = '\\'; escaped[1] = 'Z'; escaped[2] = 0; break;
        default: escaped[0] = text[index]; escaped[1] = 0; escaped[2] = 0; break;
        }
        if (!XString_append_utf8(query, escaped)) return false;
    }
    return XString_append_utf8(query, "'");
}

static bool xmysql_append_escaped_value(XString* query, const XVariant* value,
                                         bool binary)
{
    int type;
    if (!query || !value || !XVariant_isValid(value)) {
        return query && XString_append_utf8(query, "NULL");
    }
    type = XVariant_type((XVariant*)value);
    if (type == XVariantType_NULL) return XString_append_utf8(query, "NULL");
    if (type == XVariantType_Date || type == XVariantType_Time
        || type == XVariantType_DateTime) {
        XString* text = xmysql_date_variant_text(value);
        bool ok = text
            ? xmysql_append_escaped_text(query, XString_toUtf8(text), XString_toUtf8_length(text))
            : XString_append_utf8(query, "NULL");
        if (text) XString_delete_base(text);
        return ok;
    }
    if (type == XVariantType_ByteArray || binary) {
        const uint8_t* data;
        size_t size;
        if (type == XVariantType_ByteArray) {
            const XByteArray* bytes = XVariant_toByteArray_ref(value);
            data = bytes ? XByteArray_data((XByteArray*)bytes) : NULL;
            size = bytes ? XByteArray_size_base(bytes) : 0;
        } else {
            const XString* string = XVariant_toString_const(value);
            data = string ? (const uint8_t*)XString_toUtf8(string) : NULL;
            size = string ? XString_toUtf8_length(string) : 0;
        }
        return xmysql_append_escaped_text(query, (const char*)data, size);
    }
    if (type == XVariantType_Bool)
        return XString_append_utf8(query, XVariant_toBool(value) ? "1" : "0");
    if (type == XVariantType_Double || type == XVariantType_Float) {
        XString* number = XString_create_fmt_utf8("%.17g", XVariant_toDouble(value));
        bool ok = number && XString_append(query, number);
        if (number) XString_delete_base(number);
        return ok;
    }
    if (type == XVariantType_Uint8 || type == XVariantType_Uint16
        || type == XVariantType_Uint32 || type == XVariantType_Uint64
        || type == XVariantType_Size_t) {
        XString* number = XString_create_fmt_utf8("%llu",
                                                   (unsigned long long)XVariant_toUint64(value));
        bool ok = number && XString_append(query, number);
        if (number) XString_delete_base(number);
        return ok;
    }
    if (type == XVariantType_Int8 || type == XVariantType_Int16
        || type == XVariantType_Int32 || type == XVariantType_Int64
        || type == XVariantType_Int) {
        XString* number = XString_create_fmt_utf8("%lld",
                                                   (long long)XVariant_toInt64(value));
        bool ok = number && XString_append(query, number);
        if (number) XString_delete_base(number);
        return ok;
    }
    {
        const XString* string = XVariant_toString_const(value);
        const char* text = string ? XString_toUtf8(string) : "";
        size_t size = string ? XString_toUtf8_length(string) : 0;
        return xmysql_append_escaped_text(query, text, size);
    }
}

static bool xmysql_is_name_char(char value)
{
    return isalnum((unsigned char)value) || value == '_' || value == '$';
}

static int xmysql_find_bound_name(const XSqlResult* result, const char* name, size_t size)
{
    size_t index;
    char buffer[128];
    if (!result || !name || size == 0) return -1;
    if (size >= sizeof(buffer)) return -1;
    memcpy(buffer, name, size);
    buffer[size] = 0;
    for (index = 0; index < result->m_boundCount; ++index) {
        const char* bound = result->m_boundNames[index]
            ? XString_toUtf8(result->m_boundNames[index]) : NULL;
        if (!bound) continue;
        if (strcmp(bound, buffer) == 0) return (int)index;
        if ((bound[0] == ':' || bound[0] == '@' || bound[0] == '$')
            && strcmp(bound + 1, buffer) == 0) return (int)index;
    }
    return -1;
}

static XString* xmysql_build_query(const XSqlResult* result)
{
    const char* text;
    size_t length;
    size_t index;
    size_t positional = 0;
    char quote = 0;
    XString* output;
    if (!result || !result->m_lastQuery) return NULL;
    if (result->m_boundCount == 0) return XString_create_copy(result->m_lastQuery);
    text = XString_toUtf8(result->m_lastQuery);
    length = XString_toUtf8_length(result->m_lastQuery);
    output = XString_create();
    if (!output) return NULL;
    for (index = 0; index < length; ++index) {
        char current = text[index];
        if ((unsigned char)current >= 0x80u) {
            size_t characterSize = xmysql_utf8_sequence_size(text + index, length - index);
            if (characterSize == 0
                || !XString_append_with_length_utf8(output, text + index, characterSize)) goto fail;
            index += characterSize - 1;
            continue;
        }
        if (quote) {
            if (!XString_append_with_length_utf8(output, &current, 1)) goto fail;
            if (current == '\\' && index + 1 < length) {
                if (!XString_append_with_length_utf8(output, text + ++index, 1)) goto fail;
            } else if (current == quote) quote = 0;
            continue;
        }
        if (current == '\'' || current == '"' || current == '`') {
            quote = current;
            if (!XString_append_with_length_utf8(output, &current, 1)) goto fail;
        } else if (current == '?') {
            if (positional < result->m_boundCount
                && !xmysql_append_escaped_value(output, result->m_boundValues[positional],
                                                 (result->m_boundTypes[positional] & XSqlParamType_Binary) != 0)) goto fail;
            if (positional >= result->m_boundCount
                && !XString_append_with_length_utf8(output, &current, 1)) goto fail;
            ++positional;
        } else if ((current == ':' || current == '@' || current == '$')
                   && index + 1 < length && xmysql_is_name_char(text[index + 1])) {
            size_t begin = ++index;
            int bound;
            while (index + 1 < length && xmysql_is_name_char(text[index + 1])) ++index;
            bound = xmysql_find_bound_name(result, text + begin, index - begin + 1);
            if (bound >= 0) {
                if (!xmysql_append_escaped_value(output, result->m_boundValues[bound],
                                                 (result->m_boundTypes[bound] & XSqlParamType_Binary) != 0)) goto fail;
            } else if (!XString_append_with_length_utf8(output, text + begin - 1,
                                                        index - begin + 2)) goto fail;
        } else if (!XString_append_with_length_utf8(output, &current, 1)) {
            goto fail;
        }
    }
    return output;
fail:
    XString_delete_base(output);
    return NULL;
}

static XString* xmysql_build_prepared_query(const XSqlResult* result,
                                            size_t** order, size_t* orderCount)
{
    const char* text;
    size_t length;
    size_t index;
    size_t positional = 0;
    size_t count = 0;
    size_t capacity = 0;
    size_t* positions = NULL;
    char quote = 0;
    XString* output;
    if (order) *order = NULL;
    if (orderCount) *orderCount = 0;
    if (!result || !result->m_lastQuery || !order || !orderCount) return NULL;
    text = XString_toUtf8(result->m_lastQuery);
    length = XString_toUtf8_length(result->m_lastQuery);
    output = XString_create();
    if (!output) return NULL;
    for (index = 0; index < length; ++index) {
        char current = text[index];
        if ((unsigned char)current >= 0x80u) {
            size_t characterSize = xmysql_utf8_sequence_size(text + index, length - index);
            if (characterSize == 0
                || !XString_append_with_length_utf8(output, text + index, characterSize)) goto fail;
            index += characterSize - 1;
            continue;
        }
        if (quote) {
            if (!XString_append_with_length_utf8(output, &current, 1)) goto fail;
            if (current == '\\' && index + 1 < length) {
                if (!XString_append_with_length_utf8(output, text + ++index, 1)) goto fail;
            } else if (current == quote) quote = 0;
            continue;
        }
        if (current == '\'' || current == '"' || current == '`') {
            quote = current;
            if (!XString_append_with_length_utf8(output, &current, 1)) goto fail;
        } else if (current == '?') {
            if (positional >= result->m_boundCount) {
                if (!XString_append_with_length_utf8(output, &current, 1)) goto fail;
            } else {
                if (!XString_append_utf8(output, "?")) goto fail;
                if (count == capacity) {
                    capacity = capacity ? capacity * 2 : 8;
                    positions = (size_t*)XRealloc_System(positions, capacity * sizeof(*positions));
                    if (!positions) goto fail;
                }
                positions[count++] = positional;
            }
            ++positional;
        } else if ((current == ':' || current == '@' || current == '$')
                   && index + 1 < length && xmysql_is_name_char(text[index + 1])) {
            size_t begin = ++index;
            int bound;
            while (index + 1 < length && xmysql_is_name_char(text[index + 1])) ++index;
            bound = xmysql_find_bound_name(result, text + begin, index - begin + 1);
            if (bound < 0) {
                if (!XString_append_with_length_utf8(output, text + begin - 1,
                                                     index - begin + 2)) goto fail;
            } else {
                if (!XString_append_utf8(output, "?")) goto fail;
                if (count == capacity) {
                    capacity = capacity ? capacity * 2 : 8;
                    positions = (size_t*)XRealloc_System(positions, capacity * sizeof(*positions));
                    if (!positions) goto fail;
                }
                positions[count++] = (size_t)bound;
            }
        } else if (!XString_append_with_length_utf8(output, &current, 1)) goto fail;
    }
    *order = positions;
    *orderCount = count;
    return output;
fail:
    if (positions) XFree_System(positions);
    XString_delete_base(output);
    return NULL;
}

static bool xmysql_make_bind(const XVariant* value, XSqlParamType type,
                             XSqlMySqlBind* bind)
{
    int variantType;
    if (!bind) return false;
    memset(bind, 0, sizeof(*bind));
    bind->m_isNull = !value || !XVariant_isValid(value)
        || XVariant_type(value) == XVariantType_NULL;
    if (bind->m_isNull) {
        bind->m_type = XSqlMySqlValueType_Null;
        return true;
    }
    variantType = XVariant_type(value);
    if (variantType == XVariantType_ByteArray) {
        const XByteArray* bytes = XVariant_toByteArray_ref(value);
        bind->m_type = XSqlMySqlValueType_ByteArray;
        bind->m_data = bytes ? XByteArray_data((XByteArray*)bytes) : NULL;
        bind->m_size = bytes ? XByteArray_size_base((XByteArray*)bytes) : 0;
        return (bind->m_data || bind->m_size == 0);
    }
    if (variantType == XVariantType_Bool || variantType == XVariantType_Int8
        || variantType == XVariantType_Int16 || variantType == XVariantType_Int32
        || variantType == XVariantType_Int64 || variantType == XVariantType_Int) {
        bind->m_type = XSqlMySqlValueType_Integer;
        bind->m_data = value->m_data;
        bind->m_size = value->m_dataSize;
        return bind->m_data != NULL;
    }
    if (variantType == XVariantType_Uint8 || variantType == XVariantType_Uint16
        || variantType == XVariantType_Uint32 || variantType == XVariantType_Uint64
        || variantType == XVariantType_Size_t) {
        bind->m_type = XSqlMySqlValueType_UnsignedInteger;
        bind->m_unsigned = true;
        bind->m_data = value->m_data;
        bind->m_size = value->m_dataSize;
        return bind->m_data != NULL;
    }
    if (variantType == XVariantType_Float || variantType == XVariantType_Double) {
        bind->m_type = XSqlMySqlValueType_Real;
        bind->m_data = value->m_data;
        bind->m_size = value->m_dataSize;
        return bind->m_data != NULL;
    }
    if (variantType == XVariantType_Date) {
        bind->m_type = XSqlMySqlValueType_Date;
        bind->m_data = value->m_data;
        bind->m_size = value->m_dataSize;
        return bind->m_data != NULL;
    }
    if (variantType == XVariantType_Time) {
        bind->m_type = XSqlMySqlValueType_Time;
        bind->m_data = value->m_data;
        bind->m_size = value->m_dataSize;
        return bind->m_data != NULL;
    }
    if (variantType == XVariantType_DateTime) {
        bind->m_type = XSqlMySqlValueType_DateTime;
        bind->m_data = value->m_data;
        bind->m_size = value->m_dataSize;
        return bind->m_data != NULL;
    }
    {
        const XString* string = XVariant_toString_const(value);
        bind->m_type = XSqlMySqlValueType_String;
        bind->m_data = string ? XString_toUtf8(string) : NULL;
        bind->m_size = string ? XString_toUtf8_length(string) : 0;
        if ((type & XSqlParamType_Binary) != 0) bind->m_type = XSqlMySqlValueType_ByteArray;
        return bind->m_data != NULL || bind->m_size == 0;
    }
}

static void xmysql_apply_client_error(XMySqlResult* result, XMySqlDriver* driver,
                                      const char* fallback)
{
    const XSqlMySqlError* error = driver && driver->m_api && driver->m_api->lastError
        ? driver->m_api->lastError(driver->m_client) : NULL;
    xmysql_set_result_error(result, error, fallback);
}

static XVtable* XMySqlResult_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XMySqlResult))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XSqlResult);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXMySqlResult_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXMySqlResult_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMySqlResult_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Data, VXMySqlResult_data);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_IsNull, VXMySqlResult_isNull);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Reset, VXMySqlResult_reset);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Fetch, VXMySqlResult_fetch);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Size, VXMySqlResult_size);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Prepare, VXMySqlResult_prepare);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Exec, VXMySqlResult_exec);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_ExecBatch, VXMySqlResult_execBatch);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_NextResult, VXMySqlResult_nextResult);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_DetachFromResultSet, VXMySqlResult_detachFromResultSet);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Handle, VXMySqlResult_handle);
    return XVTABLE_DEFAULT;
}

static XMySqlResult* XMySqlResult_create(const XSqlDriver* driver)
{
    XMySqlResult* result = (XMySqlResult*)XCalloc_System(1, sizeof(*result));
    if (!result) return NULL;
    XSqlResult_init(&result->m_parent, driver);
    if (driver) result->m_parent.m_precisionPolicy = XSqlDriver_numericalPrecisionPolicy(driver);
    XClassSetVtable(result, XMySqlResult);
    Set_Class_MemoryFree(result, XFree_System);
    return result;
}

static void VXMySqlResult_deinit(XMySqlResult* result)
{
    if (!result) return;
    xmysql_release_raw(result);
    xmysql_clear_rows(result);
    XClass_Deinit_Parent(XSqlResult, result);
}

static void VXMySqlResult_copy(XMySqlResult* dest, const XMySqlResult* src)
{
    size_t row;
    int field;
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) {
        XSqlResult_init(&dest->m_parent, src->m_parent.m_driver);
        XClassSetVtable(dest, XMySqlResult);
    }
    xmysql_release_raw(dest);
    xmysql_clear_rows(dest);
    XClass_Parent(XSqlResult, EXClass_Copy, void(*)(XSqlResult*, const XSqlResult*))
        (&dest->m_parent, &src->m_parent);
    dest->m_columnCount = src->m_columnCount;
    dest->m_prepared = src->m_prepared;
    dest->m_raw = NULL;
    for (row = 0; row < src->m_rowCount; ++row) {
        XVariant** values;
        if (!xmysql_reserve_rows(dest, dest->m_rowCount + 1)) return;
        values = (XVariant**)XCalloc_System((size_t)dest->m_columnCount, sizeof(*values));
        if (!values && dest->m_columnCount > 0) return;
        dest->m_rows[dest->m_rowCount++] = values;
        for (field = 0; field < dest->m_columnCount; ++field)
            values[field] = XVariant_create_copy(src->m_rows[row][field]);
    }
}

static void VXMySqlResult_move(XMySqlResult* dest, XMySqlResult* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) {
        XSqlResult_init(&dest->m_parent, src->m_parent.m_driver);
        XClassSetVtable(dest, XMySqlResult);
    }
    xmysql_release_raw(dest);
    xmysql_clear_rows(dest);
    XClass_Parent(XSqlResult, EXClass_Move, void(*)(XSqlResult*, XSqlResult*))
        (&dest->m_parent, &src->m_parent);
    dest->m_rows = src->m_rows;
    dest->m_rowCount = src->m_rowCount;
    dest->m_rowCapacity = src->m_rowCapacity;
    dest->m_columnCount = src->m_columnCount;
    dest->m_prepared = src->m_prepared;
    dest->m_raw = src->m_raw;
    src->m_rows = NULL;
    src->m_rowCount = 0;
    src->m_rowCapacity = 0;
    src->m_columnCount = 0;
    src->m_prepared = false;
    src->m_raw = NULL;
}

static XVariant* VXMySqlResult_data(XSqlResult* base, int field)
{
    XMySqlResult* result = (XMySqlResult*)base;
    if (!result || base->m_at < 0 || (size_t)base->m_at >= result->m_rowCount
        || field < 0 || field >= result->m_columnCount)
        return XVariant_create_null();
    return XVariant_create_copy(result->m_rows[base->m_at][field]);
}

static bool VXMySqlResult_isNull(XSqlResult* base, int field)
{
    XVariant* value = VXMySqlResult_data(base, field);
    bool isNull = !value || !XVariant_isValid(value);
    if (value) XVariant_delete_base(value);
    return isNull;
}

static bool VXMySqlResult_prepare(XSqlResult* base, const XString* query)
{
    XMySqlResult* result = (XMySqlResult*)base;
    XMySqlDriver* driver;
    if (!result || !query) return false;
    xmysql_release_raw(result);
    xmysql_clear_result_state(result);
    XSqlResult_setQuery_base(base, query);
    base->m_bindingSyntax = XString_contains_char(query, XChar_fromLatin1(':'),
                                                  XChar_CaseSensitive)
        ? XSqlBindingSyntax_Named : XSqlBindingSyntax_Positional;
    driver = (XMySqlDriver*)base->m_driver;
    result->m_prepared = driver && driver->m_api && driver->m_api->supportsPreparedQueries
        ? driver->m_api->supportsPreparedQueries(driver->m_client) : true;
    return true;
}

static bool VXMySqlResult_exec(XSqlResult* base)
{
    XMySqlResult* result = (XMySqlResult*)base;
    XMySqlDriver* driver;
    XString* query;
    size_t* order = NULL;
    size_t orderCount = 0;
    XSqlMySqlBind* binds = NULL;
    XSqlMySqlResult* raw = NULL;
    const XSqlMySqlClientApi* api;
    if (!result || !base->m_driver || !base->m_lastQuery) return false;
    driver = (XMySqlDriver*)base->m_driver;
    api = driver->m_api;
    if (!api || !driver->m_client || !api->execute) return false;
    xmysql_release_raw(result);
    xmysql_clear_result_state(result);
    query = NULL;
    if (api->executePrepared && result->m_prepared
        && !XSqlResult_hasOutValues(base)) {
        size_t index;
        query = xmysql_build_prepared_query(base, &order, &orderCount);
        if (query) {
            if (orderCount > 0)
                binds = (XSqlMySqlBind*)XCalloc_System(orderCount, sizeof(*binds));
            if (orderCount == 0 || binds) {
                for (index = 0; index < orderCount; ++index) {
                    if (!xmysql_make_bind(base->m_boundValues[order[index]],
                                          base->m_boundTypes[order[index]], &binds[index])) {
                        XFree_System(binds);
                        binds = NULL;
                        break;
                    }
                }
            }
            if ((orderCount == 0 || binds)
                && api->executePrepared(driver->m_client, XString_toUtf8(query),
                                        XString_toUtf8_length(query), binds,
                                        orderCount, &raw)) {
                /* Prepared execution succeeded. */
            } else {
                if (binds) { XFree_System(binds); binds = NULL; }
                if (query) { XString_delete_base(query); query = NULL; }
                if (order) { XFree_System(order); order = NULL; }
                xmysql_apply_client_error(result, driver, "Unable to execute MySQL prepared query");
                return false;
            }
        }
    }
    if (!raw) {
        if (query) XString_delete_base(query);
        query = xmysql_build_query(base);
        if (!query || !api->execute(driver->m_client, XString_toUtf8(query),
                                   XString_toUtf8_length(query), &raw)) {
            xmysql_apply_client_error(result, driver, "Unable to execute MySQL query");
            if (query) XString_delete_base(query);
            if (order) XFree_System(order);
            if (binds) XFree_System(binds);
            return false;
        }
    }
    if (!xmysql_build_record(result, raw, api) || !xmysql_cache_rows(result, raw, api)) {
        xmysql_set_result_error(result, NULL, "Unable to cache MySQL result");
        api->resultDestroy(raw);
        if (query) XString_delete_base(query);
        if (order) XFree_System(order);
        if (binds) XFree_System(binds);
        return false;
    }
    result->m_raw = raw;
    base->m_select = api->resultIsSelect(raw);
    base->m_size = api->resultSize(raw);
    base->m_numRowsAffected = (int)api->resultRowsAffected(raw);
    {
        uint64_t lastInsertId = api->resultLastInsertId(raw);
        if (lastInsertId) XVariant_setValue_uint64(&base->m_lastInsertId, lastInsertId);
        else XVariant_setValue_null(&base->m_lastInsertId);
    }
    base->m_active = true;
    base->m_at = XSqlLocation_BeforeFirstRow;
    if (base->m_executedQuery) XString_delete_base(base->m_executedQuery);
    base->m_executedQuery = query ? XString_create_copy(query) : XString_create_copy(base->m_lastQuery);
    if (query) XString_delete_base(query);
    if (order) XFree_System(order);
    if (binds) XFree_System(binds);
    return true;
}

static bool VXMySqlResult_nextResult(XSqlResult* base)
{
    XMySqlResult* result = (XMySqlResult*)base;
    XMySqlDriver* driver;
    XSqlMySqlResult* old;
    XSqlMySqlResult* next = NULL;
    const XSqlMySqlClientApi* api;
    if (!result || !result->m_raw || !base->m_driver) return false;
    driver = (XMySqlDriver*)base->m_driver;
    api = driver->m_api;
    if (!api || !api->resultNext || !api->resultDestroy) return false;
    old = result->m_raw;
    if (!api->resultNext(old, &next) || !next) {
        api->resultDestroy(old);
        result->m_raw = NULL;
        xmysql_clear_result_state(result);
        return false;
    }
    result->m_raw = next;
    api->resultDestroy(old);
    xmysql_clear_result_state(result);
    if (!xmysql_build_record(result, next, api) || !xmysql_cache_rows(result, next, api)) {
        xmysql_set_result_error(result, NULL, "Unable to cache next MySQL result");
        xmysql_release_raw(result);
        return false;
    }
    base->m_select = api->resultIsSelect(next);
    base->m_size = api->resultSize(next);
    base->m_numRowsAffected = (int)api->resultRowsAffected(next);
    {
        uint64_t lastInsertId = api->resultLastInsertId(next);
        if (lastInsertId) XVariant_setValue_uint64(&base->m_lastInsertId, lastInsertId);
        else XVariant_setValue_null(&base->m_lastInsertId);
    }
    base->m_active = true;
    base->m_at = XSqlLocation_BeforeFirstRow;
    return true;
}

static bool VXMySqlResult_execBatch(XSqlResult* base, XSqlBatchExecutionMode mode)
{
    XMySqlResult* result = (XMySqlResult*)base;
    XVariant** selected = NULL;
    size_t rowCount = 1;
    size_t row;
    size_t field;
    int totalAffected = 0;
    bool ok = true;
    XVariant** originals = NULL;
    (void)mode;
    if (!result || base->m_boundCount == 0) return false;
    for (field = 0; field < base->m_boundCount; ++field) {
        if (base->m_boundValues[field]
            && XVariant_type(base->m_boundValues[field]) == XVariantType_List) {
            XVariantList* list = XVariant_toList_ref(base->m_boundValues[field]);
            size_t size = list ? XVariantList_size_base(list) : 0;
            if (field == 0) rowCount = size;
            else if (size != rowCount) return false;
        }
    }
    if (rowCount == 0) return false;
    selected = (XVariant**)XCalloc_System(base->m_boundCount, sizeof(*selected));
    originals = (XVariant**)XCalloc_System(base->m_boundCount, sizeof(*originals));
    if (!selected || !originals) {
        if (selected) XFree_System(selected);
        if (originals) XFree_System(originals);
        return false;
    }
    for (field = 0; field < base->m_boundCount; ++field)
        originals[field] = base->m_boundValues[field];
    for (row = 0; row < rowCount && ok; ++row) {
        for (field = 0; field < base->m_boundCount; ++field) {
            XVariant* bound = base->m_boundValues[field];
            XVariant* value = bound;
            if (bound && XVariant_type(bound) == XVariantType_List) {
                XVariantList* list = XVariant_toList_ref(bound);
                value = list ? (XVariant*)XVariantList_at_base(list, (int64_t)row) : NULL;
            }
            selected[field] = value ? XVariant_create_copy(value) : XVariant_create_null();
            if (!selected[field]) { ok = false; break; }
            base->m_boundValues[field] = selected[field];
        }
        if (ok) {
            ok = VXMySqlResult_exec(base);
            if (ok && base->m_numRowsAffected > 0) totalAffected += base->m_numRowsAffected;
        }
        for (field = 0; field < base->m_boundCount; ++field) {
            if (selected[field]) XVariant_delete_base(selected[field]);
            selected[field] = NULL;
            base->m_boundValues[field] = originals[field];
        }
    }
    XFree_System(selected);
    XFree_System(originals);
    if (ok && !base->m_select) base->m_numRowsAffected = totalAffected;
    return ok;
}

static bool VXMySqlResult_reset(XSqlResult* base, const XString* query)
{
    XMySqlResult* result = (XMySqlResult*)base;
    bool ok;
    if (!result || !query) return false;
    ok = VXMySqlResult_prepare(base, query);
    result->m_prepared = false;
    return ok && VXMySqlResult_exec(base);
}

static bool VXMySqlResult_fetch(XSqlResult* base, int index)
{
    XMySqlResult* result = (XMySqlResult*)base;
    if (!result || !base->m_active || !base->m_select) return false;
    if (index < 0) {
        base->m_at = XSqlLocation_BeforeFirstRow;
        return false;
    }
    if ((size_t)index >= result->m_rowCount) {
        if (base) base->m_at = XSqlLocation_AfterLastRow;
        return false;
    }
    if (base->m_forwardOnly && base->m_at >= 0 && index < base->m_at)
        return false;
    base->m_at = index;
    return true;
}

static int VXMySqlResult_size(XSqlResult* base)
{
    return base && base->m_select ? base->m_size : -1;
}

static void VXMySqlResult_detachFromResultSet(XSqlResult* base)
{
    XMySqlResult* result = (XMySqlResult*)base;
    if (!result) return;
    xmysql_release_raw(result);
    xmysql_clear_rows(result);
    XSqlRecord_clear(&base->m_record);
    base->m_active = false;
    base->m_select = false;
    base->m_at = XSqlLocation_BeforeFirstRow;
    base->m_size = -1;
}

static void* VXMySqlResult_handle(XSqlResult* base)
{
    return base ? base->m_handle : NULL;
}

static bool xmysql_exec_simple(XMySqlDriver* driver, const char* sql, XSqlErrorType type,
                               const char* text)
{
    XSqlMySqlResult* raw = NULL;
    const XSqlMySqlClientApi* api;
    if (!driver || !driver->m_client || !driver->m_api) return false;
    api = driver->m_api;
    if (!api->execute(driver->m_client, sql, strlen(sql), &raw)) {
        const XSqlMySqlError* source = api->lastError ? api->lastError(driver->m_client) : NULL;
        XSqlError* error;
        XString* driverText = XString_create_utf8(text);
        XString* databaseText = XString_create_utf8(source && source->m_databaseText ? source->m_databaseText : "");
        XString* errorCode = XString_create_utf8(source && source->m_errorCode ? source->m_errorCode : "0");
        error = XSqlError_create(driverText, databaseText, type, errorCode);
        if (driverText) XString_delete_base(driverText);
        if (databaseText) XString_delete_base(databaseText);
        if (errorCode) XString_delete_base(errorCode);
        if (error) { XSqlDriver_setLastError(&driver->m_parent, error); XSqlError_delete_base(error); }
        return false;
    }
    if (raw) api->resultDestroy(raw);
    return true;
}

static XString* xmysql_quote_identifier(const char* text, size_t size)
{
    XString* result = XString_create();
    size_t index;
    if (!result) return NULL;
    if (!XString_append_utf8(result, "`")) goto fail;
    for (index = 0; index < size; ++index) {
        if (text[index] == '`' && !XString_append_utf8(result, "``")) goto fail;
        if (text[index] != '`' && !XString_append_with_length_utf8(result, text + index, 1)) goto fail;
    }
    if (!XString_append_utf8(result, "`")) goto fail;
    return result;
fail:
    XString_delete_base(result);
    return NULL;
}

static XString* xmysql_quote_dotted_identifier(const char* text)
{
    XString* result = XString_create();
    XString* part;
    const char* cursor;
    const char* separator;
    if (!result) return NULL;
    cursor = text ? text : "";
    for (;;) {
        separator = strchr(cursor, '.');
        part = xmysql_quote_identifier(cursor,
                                       separator ? (size_t)(separator - cursor)
                                                 : strlen(cursor));
        if (!part || !XString_append(result, part)) goto fail;
        XString_delete_base(part);
        part = NULL;
        if (!separator) break;
        if (!XString_append_utf8(result, ".")) goto fail;
        cursor = separator + 1;
    }
    return result;
fail:
    if (part) XString_delete_base(part);
    XString_delete_base(result);
    return NULL;
}

static XString* xmysql_table_sql(const XString* tableName)
{
    return xmysql_quote_dotted_identifier(tableName ? XString_toUtf8(tableName) : "");
}

static XSqlResult* xmysql_exec_metadata(const XSqlDriver* base, const XString* sql)
{
    XSqlResult* result = XSqlDriver_createResult_base(base);
    if (!result || !sql || !XSqlResult_reset_base(result, sql)) {
        if (result) XSqlResult_delete_base(result);
        return NULL;
    }
    return result;
}

static bool xmysql_ascii_contains(const char* text, const char* token)
{
    size_t textLength;
    size_t tokenLength;
    size_t offset;
    size_t index;
    if (!text || !token) return false;
    textLength = strlen(text);
    tokenLength = strlen(token);
    if (tokenLength == 0 || tokenLength > textLength) return false;
    for (offset = 0; offset + tokenLength <= textLength; ++offset) {
        for (index = 0; index < tokenLength; ++index)
            if (tolower((unsigned char)text[offset + index])
                != tolower((unsigned char)token[index])) break;
        if (index == tokenLength) return true;
    }
    return false;
}

static int xmysql_type_from_name(const char* typeName)
{
    bool isUnsigned = xmysql_ascii_contains(typeName, "unsigned");
    if (xmysql_ascii_contains(typeName, "datetime")
        || xmysql_ascii_contains(typeName, "timestamp")) return XVariantType_DateTime;
    if (xmysql_ascii_contains(typeName, "date")) return XVariantType_Date;
    /* MySQL TIME can represent values outside QTime's 24-hour range; Qt
     * therefore exposes it as QString rather than QTime. */
    if (xmysql_ascii_contains(typeName, "time")) return XVariantType_String;
    if (xmysql_ascii_contains(typeName, "blob")
        || xmysql_ascii_contains(typeName, "binary")) return XVariantType_ByteArray;
    if (xmysql_ascii_contains(typeName, "double")
        || xmysql_ascii_contains(typeName, "float")
        || xmysql_ascii_contains(typeName, "decimal")) return XVariantType_Double;
    if (xmysql_ascii_contains(typeName, "tinyint"))
        return isUnsigned ? XVariantType_UChar : XVariantType_Char;
    if (xmysql_ascii_contains(typeName, "smallint"))
        return isUnsigned ? XVariantType_Uint16 : XVariantType_Int16;
    if (xmysql_ascii_contains(typeName, "bigint"))
        return isUnsigned ? XVariantType_Uint64 : XVariantType_Int64;
    if (xmysql_ascii_contains(typeName, "mediumint")
        || xmysql_ascii_contains(typeName, "int"))
        return isUnsigned ? XVariantType_Uint32 : XVariantType_Int32;
    if (xmysql_ascii_contains(typeName, "year")) return XVariantType_Int32;
    return XVariantType_String;
}

static void xmysql_apply_type_shape(XSqlField* field, const char* typeName)
{
    const char* open;
    const char* close;
    const char* comma;
    char* end;
    long length;
    long precision;
    if (!field || !typeName) return;
    open = strchr(typeName, '(');
    close = open ? strchr(open + 1, ')') : NULL;
    if (!open || !close) return;
    length = strtol(open + 1, &end, 10);
    if (end == open + 1 || length < 0 || end > close) return;
    XSqlField_setLength(field, (int)length);
    comma = strchr(open + 1, ',');
    if (comma && comma < close) {
        precision = strtol(comma + 1, &end, 10);
        if (end != comma + 1 && end <= close && precision >= 0)
            XSqlField_setPrecision(field, (int)precision);
    } else if (xmysql_ascii_contains(typeName, "decimal")
               || xmysql_ascii_contains(typeName, "datetime")
               || xmysql_ascii_contains(typeName, "timestamp")
               || xmysql_ascii_contains(typeName, "time")) {
        XSqlField_setPrecision(field, (int)length);
    }
}

static XSqlField* xmysql_field_from_column(XSqlResult* query,
                                           const XString* tableName)
{
    XVariant* nameValue = XSqlResult_data_base(query, 0);
    XVariant* typeValue = XSqlResult_data_base(query, 1);
    XVariant* nullValue = XSqlResult_data_base(query, 2);
    XVariant* defaultValue = XSqlResult_data_base(query, 4);
    XVariant* extraValue = XSqlResult_data_base(query, 5);
    XString* name = nameValue ? XVariant_toString(nameValue) : NULL;
    XString* type = typeValue ? XVariant_toString(typeValue) : NULL;
    XString* nullText = nullValue ? XVariant_toString(nullValue) : NULL;
    XString* extraText = extraValue ? XVariant_toString(extraValue) : NULL;
    XSqlField* field = name
        ? XSqlField_create_ex(name, xmysql_type_from_name(type ? XString_toUtf8(type) : NULL),
                              tableName) : NULL;
    if (field) {
        XSqlField_setRequired(field, nullText
            && XString_equals_utf8(nullText, "NO", XChar_CaseInsensitive));
        if (defaultValue && XVariant_isValid(defaultValue))
            XSqlField_setDefaultValue(field, defaultValue);
        XSqlField_setAutoValue(field, extraText
            && xmysql_ascii_contains(XString_toUtf8(extraText), "auto_increment"));
        xmysql_apply_type_shape(field, type ? XString_toUtf8(type) : NULL);
    }
    if (nameValue) XVariant_delete_base(nameValue);
    if (typeValue) XVariant_delete_base(typeValue);
    if (nullValue) XVariant_delete_base(nullValue);
    if (defaultValue) XVariant_delete_base(defaultValue);
    if (extraValue) XVariant_delete_base(extraValue);
    if (name) XString_delete_base(name);
    if (type) XString_delete_base(type);
    if (nullText) XString_delete_base(nullText);
    if (extraText) XString_delete_base(extraText);
    return field;
}

static void xmysql_append_table_names(XStringList* list, XSqlResult* query,
                                      XSqlTableType type)
{
    if (!list || !query || !XSqlResult_isActive(query)) return;
    while (XSqlResult_fetchNext_base(query)) {
        XVariant* name = XSqlResult_data_base(query, 0);
        XVariant* kind = XSqlResult_data_base(query, 1);
        XString* tableName = name ? XVariant_toString(name) : NULL;
        XString* tableType = kind ? XVariant_toString(kind) : NULL;
        const char* tableTypeText = tableType ? XString_toUtf8(tableType) : "";
        bool isView = strcmp(tableTypeText, "VIEW") == 0;
        if (tableName && ((isView && (type & XSqlTableType_Views))
                          || (!isView && (type & XSqlTableType_Tables))))
            XStringList_push_back_base(list, tableName);
        if (tableName) XString_delete_base(tableName);
        if (tableType) XString_delete_base(tableType);
        if (name) XVariant_delete_base(name);
        if (kind) XVariant_delete_base(kind);
    }
}

static XVtable* XMySqlDriver_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XMySqlDriver))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XSqlDriver);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMySqlDriver_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_BeginTransaction, VXMySqlDriver_beginTransaction);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_CommitTransaction, VXMySqlDriver_commitTransaction);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_RollbackTransaction, VXMySqlDriver_rollbackTransaction);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Tables, VXMySqlDriver_tables);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_PrimaryIndex, VXMySqlDriver_primaryIndex);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Record, VXMySqlDriver_record);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_FormatValue, VXMySqlDriver_formatValue);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_EscapeIdentifier, VXMySqlDriver_escapeIdentifier);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Handle, VXMySqlDriver_handle);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_HasFeature, VXMySqlDriver_hasFeature);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Close, VXMySqlDriver_close);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_CreateResult, VXMySqlDriver_createResult);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Open, VXMySqlDriver_open);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_IsIdentifierEscaped, VXMySqlDriver_isIdentifierEscaped);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_StripDelimiters, VXMySqlDriver_stripDelimiters);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_CancelQuery, VXMySqlDriver_cancelQuery);
    return XVTABLE_DEFAULT;
}

XSqlDriver* XMySqlDriver_create(void)
{
    XMySqlDriver* driver = (XMySqlDriver*)XCalloc_System(1, sizeof(*driver));
    if (!driver) return NULL;
    XSqlDriver_init(&driver->m_parent, XSqlDriverType_MySql, XSqlDbmsType_MySql);
    XClassSetVtable(driver, XMySqlDriver);
    driver->m_api = g_xmysql_client_api ? g_xmysql_client_api : XSqlMySqlClient_defaultApi();
    driver->m_client = driver->m_api && driver->m_api->create ? driver->m_api->create() : NULL;
    if (!driver->m_client) {
        XClass_Deinit_Parent(XSqlDriver, driver);
        XFree_System(driver);
        return NULL;
    }
    Set_Class_MemoryFree(driver, XFree_System);
    return &driver->m_parent;
}

static void VXMySqlDriver_deinit(XMySqlDriver* driver)
{
    if (!driver) return;
    VXMySqlDriver_close(&driver->m_parent);
    if (driver->m_api && driver->m_api->destroy && driver->m_client)
        driver->m_api->destroy(driver->m_client);
    driver->m_client = NULL;
    XClass_Deinit_Parent(XSqlDriver, driver);
}

static bool VXMySqlDriver_open(XSqlDriver* base, const XString* database,
                               const XString* user, const XString* password,
                               const XString* host, int port, const XString* options)
{
    XMySqlDriver* driver = (XMySqlDriver*)base;
    bool ok;
    if (!driver || !driver->m_api || !driver->m_client || !driver->m_api->open) return false;
    if (base->m_open) VXMySqlDriver_close(base);
    ok = driver->m_api->open(driver->m_client,
                             database ? XString_toUtf8(database) : "",
                             user ? XString_toUtf8(user) : "",
                             password ? XString_toUtf8(password) : "",
                             host ? XString_toUtf8(host) : "", port,
                             options ? XString_toUtf8(options) : "");
    XSqlDriver_setOpen(base, ok);
    XSqlDriver_setOpenError(base, !ok);
    if (!ok) {
        const XSqlMySqlError* source = driver->m_api->lastError
            ? driver->m_api->lastError(driver->m_client) : NULL;
        XSqlError* error;
        XString* driverText = XString_create_utf8(source && source->m_driverText ? source->m_driverText : "Unable to open MySQL connection");
        XString* databaseText = XString_create_utf8(source && source->m_databaseText ? source->m_databaseText : "");
        XString* errorCode = XString_create_utf8(source && source->m_errorCode ? source->m_errorCode : "0");
        error = XSqlError_create(driverText, databaseText,
                                 source ? source->m_type : XSqlErrorType_ConnectionError, errorCode);
        if (driverText) XString_delete_base(driverText);
        if (databaseText) XString_delete_base(databaseText);
        if (errorCode) XString_delete_base(errorCode);
        if (error) { XSqlDriver_setLastError(base, error); XSqlError_delete_base(error); }
    }
    return ok;
}

static void VXMySqlDriver_close(XSqlDriver* base)
{
    XMySqlDriver* driver = (XMySqlDriver*)base;
    if (!driver) return;
    if (driver->m_api && driver->m_api->close && driver->m_client)
        driver->m_api->close(driver->m_client);
    XSqlDriver_setOpen(base, false);
}

static bool VXMySqlDriver_beginTransaction(XSqlDriver* base)
{
    return xmysql_exec_simple((XMySqlDriver*)base, "START TRANSACTION",
                               XSqlErrorType_BeginTransactionError, "Unable to begin transaction");
}

static bool VXMySqlDriver_commitTransaction(XSqlDriver* base)
{
    return xmysql_exec_simple((XMySqlDriver*)base, "COMMIT",
                               XSqlErrorType_CommitTransactionError, "Unable to commit transaction");
}

static bool VXMySqlDriver_rollbackTransaction(XSqlDriver* base)
{
    return xmysql_exec_simple((XMySqlDriver*)base, "ROLLBACK",
                               XSqlErrorType_RollbackTransactionError, "Unable to rollback transaction");
}

static XStringList* VXMySqlDriver_tables(const XSqlDriver* base, XSqlTableType type)
{
    XStringList* result = XStringList_create();
    XSqlResult* query;
    XString* sql;
    (void)type;
    if (!result || !base || !base->m_open) return result;
    sql = XString_create_utf8("SHOW FULL TABLES");
    query = xmysql_exec_metadata(base, sql);
    if (query) {
        xmysql_append_table_names(result, query, type);
        XSqlResult_delete_base(query);
    }
    if (sql) XString_delete_base(sql);
    return result;
}

static XSqlIndex* VXMySqlDriver_primaryIndex(const XSqlDriver* base, const XString* tableName)
{
    XSqlIndex* index = XSqlIndex_create();
    XSqlRecord* fields = VXMySqlDriver_record(base, tableName);
    XString* table = xmysql_table_sql(tableName);
    XString* sql;
    XSqlResult* query;
    if (!index || !table) goto done;
    sql = XString_create_fmt_utf8("SHOW INDEX FROM %s WHERE Key_name='PRIMARY'", XString_toUtf8(table));
    query = xmysql_exec_metadata(base, sql);
    if (query) {
        while (XSqlResult_fetchNext_base(query)) {
            XVariant* value = XSqlResult_data_base(query, 4);
            XVariant* cursorValue = XSqlResult_data_base(query, 0);
            XString* name = value ? XVariant_toString(value) : NULL;
            XString* cursorName = cursorValue ? XVariant_toString(cursorValue) : NULL;
            XSqlField* field = name && fields
                ? XSqlRecord_field_utf8(fields, XString_toUtf8(name))
                : name ? XSqlField_create_ex(name, XVariantType_String, tableName) : NULL;
            if (field) {
                XSqlIndex_append(index, field);
                XSqlField_delete_base(field);
            }
            XSqlIndex_setCursorName(index, cursorName);
            XSqlIndex_setName_utf8(index, "PRIMARY");
            if (cursorName) XString_delete_base(cursorName);
            if (cursorValue) XVariant_delete_base(cursorValue);
            if (name) XString_delete_base(name);
            if (value) XVariant_delete_base(value);
        }
        XSqlResult_delete_base(query);
    }
    if (sql) XString_delete_base(sql);
done:
    if (fields) XSqlRecord_delete_base(fields);
    if (table) XString_delete_base(table);
    return index;
}

static XSqlRecord* VXMySqlDriver_record(const XSqlDriver* base, const XString* tableName)
{
    XSqlRecord* result = XSqlRecord_create();
    XString* table = xmysql_table_sql(tableName);
    XString* sql = NULL;
    XSqlResult* query = NULL;
    XSqlRecord* rawRecord = NULL;
    if (!result || !table) goto done;
    /* QMYSQL obtains length/precision from the real result metadata, then
     * performs a second metadata query only to fill default values. */
    sql = XString_create_fmt_utf8("SELECT * FROM %s LIMIT 0", XString_toUtf8(table));
    query = xmysql_exec_metadata(base, sql);
    if (query) {
        rawRecord = XSqlResult_record_base(query);
        if (rawRecord) {
            int index;
            int count = XSqlRecord_count(rawRecord);
            for (index = 0; index < count; ++index) {
                XSqlField* field = XSqlRecord_field(rawRecord, index);
                if (!field || !XSqlRecord_append(result, field)) {
                    if (field) XSqlField_delete_base(field);
                    XSqlRecord_clear(result);
                    break;
                }
                XSqlField_delete_base(field);
            }
        }
        XSqlResult_delete_base(query);
        query = NULL;
    }
    if (sql) { XString_delete_base(sql); sql = NULL; }

    sql = XString_create_fmt_utf8("SHOW COLUMNS FROM %s", XString_toUtf8(table));
    query = xmysql_exec_metadata(base, sql);
    if (query) {
        while (XSqlResult_fetchNext_base(query)) {
            XSqlField* field = xmysql_field_from_column(query, tableName);
            XString* name = field ? XSqlField_name(field) : NULL;
            int index = name ? XSqlRecord_indexOf(result, name) : -1;
            if (rawRecord && XSqlRecord_count(result) > 0 && field && index >= 0) {
                XSqlField* actual = XSqlRecord_field(result, index);
                XVariant* defaultValue = XSqlField_defaultValue(field);
                if (actual) {
                    XSqlField_setDefaultValue(actual, defaultValue);
                    XSqlRecord_replace(result, index, actual);
                    XSqlField_delete_base(actual);
                }
                if (defaultValue) XVariant_delete_base(defaultValue);
            } else if (field && !XSqlRecord_append(result, field)) {
                XSqlField_delete_base(field);
                break;
            }
            if (name) XString_delete_base(name);
            if (field) XSqlField_delete_base(field);
        }
    }
    if (query) { XSqlResult_delete_base(query); query = NULL; }
    if (sql) { XString_delete_base(sql); sql = NULL; }
done:
    if (rawRecord) XSqlRecord_delete_base(rawRecord);
    if (query) XSqlResult_delete_base(query);
    if (sql) XString_delete_base(sql);
    if (table) XString_delete_base(table);
    return result;
}

static XString* VXMySqlDriver_formatValue(const XSqlDriver* base, const XSqlField* field,
                                          bool trimStrings)
{
    XVariant* value;
    XVariant* formattedValue = NULL;
    XString* result;
    bool ok;
    (void)base;
    if (!field) return XString_create_utf8("NULL");
    value = XSqlField_value(field);
    if (value && trimStrings && XVariant_type(value) == XVariantType_String) {
        XString* text = XVariant_toString(value);
        XString* trimmed = text ? XString_trimmed(text) : NULL;
        if (trimmed) formattedValue = XVariant_create_String_move(trimmed);
        if (text) XString_delete_base(text);
    }
    result = XString_create();
    ok = result && xmysql_append_escaped_value(result,
                                                formattedValue ? formattedValue : value,
                                                false);
    if (formattedValue) XVariant_delete_base(formattedValue);
    if (value) XVariant_delete_base(value);
    if (!ok) {
        if (result) XString_delete_base(result);
        return XString_create();
    }
    return result;
}

static XString* VXMySqlDriver_escapeIdentifier(const XSqlDriver* base,
                                               const XString* identifier,
                                               XSqlIdentifierType type)
{
    const char* text;
    XString* result;
    (void)base;
    (void)type;
    if (!identifier || XString_length_base(identifier) == 0
        || VXMySqlDriver_isIdentifierEscaped(base, identifier, type))
        return identifier ? XString_create_copy(identifier) : XString_create();
    text = XString_toUtf8(identifier);
    if (text && (text[0] == '`' || text[strlen(text) - 1] == '`'))
        return XString_create_copy(identifier);
    result = xmysql_quote_dotted_identifier(text ? text : "");
    return result;
}

static void* VXMySqlDriver_handle(const XSqlDriver* base)
{
    const XMySqlDriver* driver = (const XMySqlDriver*)base;
    return driver && driver->m_api && driver->m_api->handle
        ? driver->m_api->handle(driver->m_client) : NULL;
}

static bool VXMySqlDriver_hasFeature(const XSqlDriver* base, XSqlDriverFeature feature)
{
    const XMySqlDriver* driver = (const XMySqlDriver*)base;
    switch (feature) {
    case XSqlDriverFeature_Transactions:
        return driver && base->m_open && driver->m_api && driver->m_client
            && (!driver->m_api->supportsTransactions
                || driver->m_api->supportsTransactions(driver->m_client));
    case XSqlDriverFeature_PreparedQueries:
    case XSqlDriverFeature_PositionalPlaceholders:
        return driver && base->m_open && driver->m_api && driver->m_client
            && (!driver->m_api->supportsPreparedQueries
                || driver->m_api->supportsPreparedQueries(driver->m_client));
    case XSqlDriverFeature_QuerySize:
    case XSqlDriverFeature_Blob:
    case XSqlDriverFeature_Unicode:
    case XSqlDriverFeature_LastInsertId:
    case XSqlDriverFeature_LowPrecisionNumbers:
    case XSqlDriverFeature_MultipleResultSets:
        return true;
    default:
        return false;
    }
}

static XSqlResult* VXMySqlDriver_createResult(const XSqlDriver* base)
{
    XMySqlResult* result = XMySqlResult_create(base);
    return result ? &result->m_parent : NULL;
}

static bool VXMySqlDriver_isIdentifierEscaped(const XSqlDriver* base,
                                              const XString* identifier,
                                              XSqlIdentifierType type)
{
    const char* text;
    size_t length;
    (void)base;
    (void)type;
    if (!identifier) return false;
    text = XString_toUtf8(identifier);
    length = text ? strlen(text) : 0;
    return length > 2 && text[0] == '`' && text[length - 1] == '`';
}

static XString* VXMySqlDriver_stripDelimiters(const XSqlDriver* base,
                                              const XString* identifier,
                                              XSqlIdentifierType type)
{
    const char* text;
    size_t length;
    (void)base;
    (void)type;
    if (!identifier) return XString_create();
    text = XString_toUtf8(identifier);
    length = text ? strlen(text) : 0;
    if (length > 2 && text[0] == '`' && text[length - 1] == '`')
        return XString_create_with_length_utf8(text + 1, length - 2);
    return XString_create_copy(identifier);
}

static bool VXMySqlDriver_cancelQuery(XSqlDriver* base)
{
    XMySqlDriver* driver = (XMySqlDriver*)base;
    return driver && driver->m_api && driver->m_api->cancel
        && driver->m_api->cancel(driver->m_client);
}

bool XMySqlDriver_setClientApi(const XSqlMySqlClientApi* api)
{
    if (!api) {
        g_xmysql_client_api = NULL;
        return true;
    }
    if (!api->create || !api->destroy || !api->open || !api->close || !api->execute
        || !api->executePrepared
        || !api->resultDestroy || !api->resultColumnCount || !api->resultField
        || !api->resultFetch || !api->resultValue || !api->resultSize
        || !api->resultRowsAffected || !api->resultLastInsertId || !api->resultIsSelect
        || !api->lastError || !api->handle || !api->cancel || !api->resultNext) return false;
    g_xmysql_client_api = api;
    return true;
}

const XSqlMySqlClientApi* XMySqlDriver_clientApi(void)
{
    return g_xmysql_client_api ? g_xmysql_client_api : XSqlMySqlClient_defaultApi();
}

bool XMySqlDriver_register(void)
{
    static XSqlDriverCreator* creator;
    if (creator) return true;
    creator = XSqlDriverCreator_create(XMySqlDriver_create);
    if (!creator || !XSqlDatabase_registerSqlDriver_type(XSqlDriverType_MySql,
                                                         &creator->m_parent)) {
        if (creator) XSqlDriverCreator_delete_base(creator);
        creator = NULL;
        return false;
    }
    return true;
}
