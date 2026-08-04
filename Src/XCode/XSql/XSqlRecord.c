/**
 * @file       XSqlRecord.c
 * @brief      SQL 记录类实现。
 */
#include "XSqlRecord.h"

#include <string.h>

static void VXSqlRecord_copy(XSqlRecord* dest, const XSqlRecord* src);
static void VXSqlRecord_move(XSqlRecord* dest, XSqlRecord* src);
static void VXSqlRecord_deinit(XSqlRecord* record);

XVtable* XSqlRecord_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSqlRecord)
	XCLASS_SET_CLASS_NAME_DEFAULT("XSqlRecord");
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSqlRecord_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSqlRecord_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlRecord_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlRecord_init(XSqlRecord* record)
{
    if (!record) return;
    memset(((unsigned char*)record) + sizeof(XClass), 0, sizeof(*record) - sizeof(XClass));
    XClass_init((XClass*)record);
    XClassSetVtable(record, XSqlRecord);
}

XSqlRecord* XSqlRecord_create(void)
{
    XSqlRecord* record = (XSqlRecord*)XMalloc_System(sizeof(XSqlRecord));
    if (!record) return NULL;
    memset(record, 0, sizeof(*record));
    XSqlRecord_init(record);
    Set_Class_MemoryFree(record, XFree_System);
    return record;
}

static bool xsql_record_reserve(XSqlRecord* record, size_t wanted)
{
    if (!record) return false;
    if (wanted <= record->m_capacity) return true;
    size_t capacity = record->m_capacity ? record->m_capacity * 2 : 4;
    while (capacity < wanted) capacity *= 2;
    XSqlField** fields = (XSqlField**)XRealloc_System(record->m_fields, capacity * sizeof(XSqlField*));
    if (!fields) return false;
    record->m_fields = fields;
    record->m_capacity = capacity;
    return true;
}

static void xsql_record_destroy_field(XSqlField* field)
{
    if (field) XSqlField_delete_base(field);
}

static bool xsql_record_name_equals(const XSqlField* field, const char* name)
{
    if (!field || !name || !field->m_name) return false;
    if (XString_equals_utf8(field->m_name, name, XChar_CaseInsensitive)) return true;
    const char* dot = strchr(name, '.');
    if (!dot || !field->m_tableName) return false;
    size_t tableLength = (size_t)(dot - name);
    size_t fieldLength = strlen(dot + 1);
    const char* table = XString_toUtf8(field->m_tableName);
    const char* fieldName = XString_toUtf8(field->m_name);
    if (!table || !fieldName || strlen(table) != tableLength || strlen(fieldName) != fieldLength)
        return false;
    for (size_t i = 0; i < tableLength; ++i) {
        char a = table[i], b = name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return XString_equals_utf8(field->m_name, dot + 1, XChar_CaseInsensitive);
}

static void VXSqlRecord_deinit(XSqlRecord* record)
{
    if (!record) return;
    for (size_t i = 0; i < record->m_count; ++i) xsql_record_destroy_field(record->m_fields[i]);
    if (record->m_fields) XFree_System(record->m_fields);
    record->m_fields = NULL;
    record->m_count = 0;
    record->m_capacity = 0;
    XClass_Deinit_Parent(XClass, record);
}

static void VXSqlRecord_copy(XSqlRecord* dest, const XSqlRecord* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlRecord_init(dest);
    XSqlRecord_clear(dest);
    for (size_t i = 0; i < src->m_count; ++i) {
        if (!XSqlRecord_append(dest, src->m_fields[i])) {
            XSqlRecord_clear(dest);
            return;
        }
    }
}

static void VXSqlRecord_move(XSqlRecord* dest, XSqlRecord* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlRecord_init(dest);
    XSqlRecord_clear(dest);
    if (dest->m_fields) XFree_System(dest->m_fields);
    dest->m_fields = NULL;
    dest->m_capacity = 0;
    dest->m_fields = src->m_fields;
    dest->m_count = src->m_count;
    dest->m_capacity = src->m_capacity;
    src->m_fields = NULL;
    src->m_count = 0;
    src->m_capacity = 0;
}

XSqlRecord* XSqlRecord_create_copy(const XSqlRecord* other)
{
    if (!other) return NULL;
    XSqlRecord* result = XSqlRecord_create();
    if (result) XSqlRecord_copy_base(result, other);
    return result;
}

XSqlRecord* XSqlRecord_create_move(XSqlRecord* other)
{
    if (!other) return NULL;
    XSqlRecord* result = XSqlRecord_create();
    if (result) XSqlRecord_move_base(result, other);
    return result;
}

void XSqlRecord_swap(XSqlRecord* left, XSqlRecord* right)
{
    if (!left || !right || left == right) return;
    XSqlRecord* temp = XSqlRecord_create_move(left);
    if (!temp) return;
    XSqlRecord_move_base(left, right);
    XSqlRecord_move_base(right, temp);
    XSqlRecord_delete_base(temp);
}

XVariant* XSqlRecord_value(const XSqlRecord* record, int index)
{
    const XSqlField* field = XSqlRecord_field_const(record, index);
    return field ? XSqlField_value(field) : XVariant_create_null();
}

XVariant* XSqlRecord_value_utf8(const XSqlRecord* record, const char* name)
{
    return XSqlRecord_value(record, XSqlRecord_indexOf_utf8(record, name));
}

XVariant* XSqlRecord_value_2(const XSqlRecord* record, const XString* name)
{
    return XSqlRecord_value(record, XSqlRecord_indexOf(record, name));
}

void XSqlRecord_setValue(XSqlRecord* record, int index, const XVariant* value)
{
    XSqlField* field = (XSqlField*)XSqlRecord_field_const(record, index);
    if (field) XSqlField_setValue(field, value);
}

void XSqlRecord_setValue_utf8(XSqlRecord* record, const char* name, const XVariant* value)
{
    XSqlRecord_setValue(record, XSqlRecord_indexOf_utf8(record, name), value);
}

void XSqlRecord_setValue_2(XSqlRecord* record, const XString* name, const XVariant* value)
{
    XSqlRecord_setValue(record, XSqlRecord_indexOf(record, name), value);
}

void XSqlRecord_setNull(XSqlRecord* record, int index)
{
    XSqlField* field = (XSqlField*)XSqlRecord_field_const(record, index);
    if (field) XSqlField_clear(field);
}

void XSqlRecord_setNull_utf8(XSqlRecord* record, const char* name)
{
    XSqlRecord_setNull(record, XSqlRecord_indexOf_utf8(record, name));
}

void XSqlRecord_setNull_2(XSqlRecord* record, const XString* name)
{
    XSqlRecord_setNull(record, XSqlRecord_indexOf(record, name));
}

bool XSqlRecord_isNull(const XSqlRecord* record, int index)
{
    const XSqlField* field = XSqlRecord_field_const(record, index);
    return !field || XSqlField_isNull(field);
}

bool XSqlRecord_isNull_utf8(const XSqlRecord* record, const char* name)
{
    return XSqlRecord_isNull(record, XSqlRecord_indexOf_utf8(record, name));
}

bool XSqlRecord_isNull_2(const XSqlRecord* record, const XString* name)
{
    return XSqlRecord_isNull(record, XSqlRecord_indexOf(record, name));
}

int XSqlRecord_indexOf_utf8(const XSqlRecord* record, const char* name)
{
    if (!record || !name) return -1;
    for (size_t i = 0; i < record->m_count; ++i)
        if (xsql_record_name_equals(record->m_fields[i], name)) return (int)i;
    return -1;
}

int XSqlRecord_indexOf(const XSqlRecord* record, const XString* name)
{
    return XSqlRecord_indexOf_utf8(record, name ? XString_toUtf8(name) : NULL);
}

XString* XSqlRecord_fieldName(const XSqlRecord* record, int index)
{
    const XSqlField* field = XSqlRecord_field_const(record, index);
    return field ? XSqlField_name(field) : XString_create();
}

XSqlField* XSqlRecord_field(const XSqlRecord* record, int index)
{
    const XSqlField* field = XSqlRecord_field_const(record, index);
    return field ? XSqlField_create_copy(field) : XSqlField_create();
}

XSqlField* XSqlRecord_field_utf8(const XSqlRecord* record, const char* name)
{
    return XSqlRecord_field(record, XSqlRecord_indexOf_utf8(record, name));
}

XSqlField* XSqlRecord_field_2(const XSqlRecord* record, const XString* name)
{
    return XSqlRecord_field(record, XSqlRecord_indexOf(record, name));
}

const XSqlField* XSqlRecord_field_const(const XSqlRecord* record, int index)
{
    return record && index >= 0 && (size_t)index < record->m_count ? record->m_fields[index] : NULL;
}

bool XSqlRecord_isGenerated(const XSqlRecord* record, int index)
{
    const XSqlField* field = XSqlRecord_field_const(record, index);
    return field && XSqlField_isGenerated(field);
}

void XSqlRecord_setGenerated(XSqlRecord* record, int index, bool generated)
{
    XSqlField* field = (XSqlField*)XSqlRecord_field_const(record, index);
    if (field) XSqlField_setGenerated(field, generated);
}

void XSqlRecord_setGenerated_utf8(XSqlRecord* record, const char* name, bool generated)
{
    XSqlRecord_setGenerated(record, XSqlRecord_indexOf_utf8(record, name), generated);
}

void XSqlRecord_setGenerated_2(XSqlRecord* record, const XString* name, bool generated)
{
    XSqlRecord_setGenerated(record, XSqlRecord_indexOf(record, name), generated);
}

bool XSqlRecord_append(XSqlRecord* record, const XSqlField* field)
{
    if (!record || !field || !xsql_record_reserve(record, record->m_count + 1)) return false;
    XSqlField* copy = XSqlField_create_copy(field);
    if (!copy) return false;
    record->m_fields[record->m_count++] = copy;
    return true;
}

bool XSqlRecord_replace(XSqlRecord* record, int index, const XSqlField* field)
{
    if (!record || !field || index < 0 || (size_t)index >= record->m_count) return false;
    XSqlField* copy = XSqlField_create_copy(field);
    if (!copy) return false;
    xsql_record_destroy_field(record->m_fields[index]);
    record->m_fields[index] = copy;
    return true;
}

bool XSqlRecord_insert(XSqlRecord* record, int index, const XSqlField* field)
{
    if (!record || !field || index < 0 || (size_t)index > record->m_count || !xsql_record_reserve(record, record->m_count + 1)) return false;
    XSqlField* copy = XSqlField_create_copy(field);
    if (!copy) return false;
    memmove(&record->m_fields[index + 1], &record->m_fields[index], (record->m_count - (size_t)index) * sizeof(XSqlField*));
    record->m_fields[index] = copy;
    ++record->m_count;
    return true;
}

bool XSqlRecord_remove(XSqlRecord* record, int index)
{
    if (!record || index < 0 || (size_t)index >= record->m_count) return false;
    xsql_record_destroy_field(record->m_fields[index]);
    memmove(&record->m_fields[index], &record->m_fields[index + 1], (record->m_count - (size_t)index - 1) * sizeof(XSqlField*));
    --record->m_count;
    return true;
}

bool XSqlRecord_isEmpty(const XSqlRecord* record) { return !record || record->m_count == 0; }
bool XSqlRecord_contains_utf8(const XSqlRecord* record, const char* name) { return XSqlRecord_indexOf_utf8(record, name) >= 0; }
bool XSqlRecord_contains(const XSqlRecord* record, const XString* name) { return XSqlRecord_indexOf(record, name) >= 0; }

void XSqlRecord_clear(XSqlRecord* record)
{
    if (!record) return;
    for (size_t i = 0; i < record->m_count; ++i) xsql_record_destroy_field(record->m_fields[i]);
    record->m_count = 0;
}

void XSqlRecord_clearValues(XSqlRecord* record)
{
    if (!record) return;
    for (size_t i = 0; i < record->m_count; ++i) XSqlField_clear(record->m_fields[i]);
}

int XSqlRecord_count(const XSqlRecord* record) { return record ? (int)record->m_count : 0; }

XSqlRecord* XSqlRecord_keyValues(const XSqlRecord* record, const XSqlRecord* keyFields)
{
    XSqlRecord* result = XSqlRecord_create();
    if (!result || !record || !keyFields) return result;
    for (size_t i = 0; i < keyFields->m_count; ++i) {
        XString* name = XSqlField_name(keyFields->m_fields[i]);
        XSqlField* field = XSqlField_create_copy(keyFields->m_fields[i]);
        XVariant* value = XSqlRecord_value(record,
                                           XSqlRecord_indexOf(record, name));
        if (field && value) {
            XSqlField_setValue(field, value);
            XSqlRecord_append(result, field);
        }
        if (value) XVariant_delete_base(value);
        if (field) XSqlField_delete_base(field);
        if (name) XString_delete_base(name);
    }
    return result;
}

bool XSqlRecord_equals(const XSqlRecord* left, const XSqlRecord* right)
{
    if (left == right) return true;
    if (!left || !right || left->m_count != right->m_count) return false;
    for (size_t i = 0; i < left->m_count; ++i)
        if (!XSqlField_equals(left->m_fields[i], right->m_fields[i])) return false;
    return true;
}
