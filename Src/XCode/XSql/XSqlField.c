/**
 * @file       XSqlField.c
 * @brief      SQL 字段描述类实现。
 */
#include "XSqlField.h"

#include <string.h>

static void VXSqlField_copy(XSqlField* dest, const XSqlField* src);
static void VXSqlField_move(XSqlField* dest, XSqlField* src);
static void VXSqlField_deinit(XSqlField* field);

XVtable* XSqlField_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSqlField))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSqlField_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSqlField_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlField_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlField_init(XSqlField* field)
{
    if (!field) return;
    memset(((unsigned char*)field) + sizeof(XClass), 0, sizeof(*field) - sizeof(XClass));
    XClass_init((XClass*)field);
    XClassSetVtable(field, XSqlField);
    XVariant_init(&field->m_value, NULL, 0, XVariantType_NULL);
    XVariant_init(&field->m_defaultValue, NULL, 0, XVariantType_NULL);
    field->m_metaType = XVariantType_NULL;
    field->m_sqlType = -1;
    field->m_requiredStatus = XSqlFieldRequiredStatus_Unknown;
    field->m_length = -1;
    field->m_precision = -1;
    field->m_generated = true;
}

void XSqlField_init_ex(XSqlField* field, const XString* fieldName,
                       int metaType, const XString* tableName)
{
    XSqlField_init(field);
    if (!field) return;
    XSqlField_setName(field, fieldName);
    XSqlField_setTableName(field, tableName);
    XSqlField_setMetaType(field, metaType);
}

XSqlField* XSqlField_create(void)
{
    XSqlField* field = (XSqlField*)XMalloc_System(sizeof(XSqlField));
    if (!field) return NULL;
    memset(field, 0, sizeof(*field));
    XSqlField_init(field);
    Set_Class_MemoryFree(field, XFree_System);
    return field;
}

XSqlField* XSqlField_create_ex(const XString* fieldName, int metaType,
                               const XString* tableName)
{
    XSqlField* field = XSqlField_create();
    if (field) XSqlField_init_ex(field, fieldName, metaType, tableName);
    return field;
}

static void xsql_field_assign_string(XString** target, const XString* source)
{
    if (*target) {
        XString_delete_base(*target);
        *target = NULL;
    }
    if (source) *target = XString_create_copy(source);
}

static void VXSqlField_deinit(XSqlField* field)
{
    if (!field) return;
    if (field->m_name) XString_delete_base(field->m_name);
    if (field->m_tableName) XString_delete_base(field->m_tableName);
    field->m_name = NULL;
    field->m_tableName = NULL;
    XVariant_deinit_base(&field->m_value);
    XVariant_deinit_base(&field->m_defaultValue);
    XClass_Deinit_Parent(XClass, field);
}

static void VXSqlField_copy(XSqlField* dest, const XSqlField* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlField_init(dest);
    xsql_field_assign_string(&dest->m_name, src->m_name);
    xsql_field_assign_string(&dest->m_tableName, src->m_tableName);
    if (XVariant_isValid(&src->m_value)) XVariant_copy_base(&dest->m_value, &src->m_value);
    else XVariant_setValue_null(&dest->m_value);
    if (XVariant_isValid(&src->m_defaultValue)) XVariant_copy_base(&dest->m_defaultValue, &src->m_defaultValue);
    else XVariant_setValue_null(&dest->m_defaultValue);
    dest->m_metaType = src->m_metaType;
    dest->m_sqlType = src->m_sqlType;
    dest->m_requiredStatus = src->m_requiredStatus;
    dest->m_length = src->m_length;
    dest->m_precision = src->m_precision;
    dest->m_readOnly = src->m_readOnly;
    dest->m_generated = src->m_generated;
    dest->m_autoValue = src->m_autoValue;
}

static void VXSqlField_move(XSqlField* dest, XSqlField* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlField_init(dest);
    if (dest->m_name) XString_delete_base(dest->m_name);
    if (dest->m_tableName) XString_delete_base(dest->m_tableName);
    XVariant_deinit_base(&dest->m_value);
    XVariant_deinit_base(&dest->m_defaultValue);
    dest->m_name = src->m_name;
    dest->m_tableName = src->m_tableName;
    XVariant_move_base(&dest->m_value, &src->m_value);
    XVariant_move_base(&dest->m_defaultValue, &src->m_defaultValue);
    dest->m_metaType = src->m_metaType;
    dest->m_sqlType = src->m_sqlType;
    dest->m_requiredStatus = src->m_requiredStatus;
    dest->m_length = src->m_length;
    dest->m_precision = src->m_precision;
    dest->m_readOnly = src->m_readOnly;
    dest->m_generated = src->m_generated;
    dest->m_autoValue = src->m_autoValue;
    src->m_name = NULL;
    src->m_tableName = NULL;
    XVariant_clear(&src->m_value);
    XVariant_clear(&src->m_defaultValue);
}

XSqlField* XSqlField_create_copy(const XSqlField* other)
{
    if (!other) return NULL;
    XSqlField* field = XSqlField_create();
    if (field) XSqlField_copy_base(field, other);
    return field;
}

XSqlField* XSqlField_create_move(XSqlField* other)
{
    if (!other) return NULL;
    XSqlField* field = XSqlField_create();
    if (field) XSqlField_move_base(field, other);
    return field;
}

void XSqlField_swap(XSqlField* left, XSqlField* right)
{
    if (!left || !right || left == right) return;
    XSqlField* temp = XSqlField_create_move(left);
    if (!temp) return;
    XSqlField_move_base(left, right);
    XSqlField_move_base(right, temp);
    XSqlField_delete_base(temp);
}

void XSqlField_setValue(XSqlField* field, const XVariant* value)
{
    if (!field || field->m_readOnly) return;
    if (value) XVariant_copy_base(&field->m_value, value);
    else XVariant_setValue_null(&field->m_value);
}

XVariant* XSqlField_value(const XSqlField* field) { return field ? XVariant_create_copy(&field->m_value) : XVariant_create_null(); }
const XVariant* XSqlField_value_const(const XSqlField* field) { return field ? &field->m_value : NULL; }
void XSqlField_setName(XSqlField* field, const XString* name) { if (field) xsql_field_assign_string(&field->m_name, name); }
XString* XSqlField_name(const XSqlField* field) { return field && field->m_name ? XString_create_copy(field->m_name) : XString_create(); }
void XSqlField_setTableName(XSqlField* field, const XString* name) { if (field) xsql_field_assign_string(&field->m_tableName, name); }
XString* XSqlField_tableName(const XSqlField* field) { return field && field->m_tableName ? XString_create_copy(field->m_tableName) : XString_create(); }
void XSqlField_clear(XSqlField* field) { if (field && !field->m_readOnly) XVariant_setValue_null(&field->m_value); }
bool XSqlField_isNull(const XSqlField* field) { return !field || !XVariant_isValid(&field->m_value); }
void XSqlField_setReadOnly(XSqlField* field, bool readOnly) { if (field) field->m_readOnly = readOnly; }
bool XSqlField_isReadOnly(const XSqlField* field) { return !field || field->m_readOnly; }
void XSqlField_setAutoValue(XSqlField* field, bool autoValue) { if (field) field->m_autoValue = autoValue; }
bool XSqlField_isAutoValue(const XSqlField* field) { return field && field->m_autoValue; }
void XSqlField_setMetaType(XSqlField* field, int metaType) { if (field) field->m_metaType = metaType; }
int XSqlField_metaType(const XSqlField* field) { return field ? field->m_metaType : XVariantType_NULL; }
int XSqlField_type(const XSqlField* field) { return XSqlField_metaType(field); }
void XSqlField_setType(XSqlField* field, int type) { XSqlField_setMetaType(field, type); }
void XSqlField_setSqlType(XSqlField* field, int sqlType) { if (field) field->m_sqlType = sqlType; }
int XSqlField_typeId(const XSqlField* field) { return field ? field->m_sqlType : -1; }
int XSqlField_typeID(const XSqlField* field) { return XSqlField_typeId(field); }
void XSqlField_setRequiredStatus(XSqlField* field, XSqlFieldRequiredStatus status) { if (field) field->m_requiredStatus = status; }
void XSqlField_setRequired(XSqlField* field, bool required) { if (field) field->m_requiredStatus = required ? XSqlFieldRequiredStatus_Required : XSqlFieldRequiredStatus_Optional; }
XSqlFieldRequiredStatus XSqlField_requiredStatus(const XSqlField* field) { return field ? field->m_requiredStatus : XSqlFieldRequiredStatus_Unknown; }
void XSqlField_setLength(XSqlField* field, int length) { if (field) field->m_length = length; }
int XSqlField_length(const XSqlField* field) { return field ? field->m_length : -1; }
void XSqlField_setPrecision(XSqlField* field, int precision) { if (field) field->m_precision = precision; }
int XSqlField_precision(const XSqlField* field) { return field ? field->m_precision : -1; }
void XSqlField_setDefaultValue(XSqlField* field, const XVariant* value) { if (!field) return; if (value) XVariant_copy_base(&field->m_defaultValue, value); else XVariant_setValue_null(&field->m_defaultValue); }
XVariant* XSqlField_defaultValue(const XSqlField* field) { return field ? XVariant_create_copy(&field->m_defaultValue) : XVariant_create_null(); }
void XSqlField_setGenerated(XSqlField* field, bool generated) { if (field) field->m_generated = generated; }
bool XSqlField_isGenerated(const XSqlField* field) { return field && field->m_generated; }
bool XSqlField_isValid(const XSqlField* field) { return field && field->m_metaType != XVariantType_NULL; }

bool XSqlField_equals(const XSqlField* left, const XSqlField* right)
{
    if (left == right) return true;
    if (!left || !right) return false;
    bool names = (!left->m_name && !right->m_name)
        || (left->m_name && right->m_name && XString_equals(left->m_name, right->m_name, XChar_CaseSensitive));
    bool tables = (!left->m_tableName && !right->m_tableName)
        || (left->m_tableName && right->m_tableName
            && XString_equals(left->m_tableName, right->m_tableName, XChar_CaseSensitive));
    bool values = left->m_value.m_type == right->m_value.m_type
        && left->m_value.m_dataSize == right->m_value.m_dataSize
        && XVariant_compare((XVariant*)&left->m_value, (XVariant*)&right->m_value) == 0;
    bool defaults = left->m_defaultValue.m_type == right->m_defaultValue.m_type
        && left->m_defaultValue.m_dataSize == right->m_defaultValue.m_dataSize
        && XVariant_compare((XVariant*)&left->m_defaultValue,
                            (XVariant*)&right->m_defaultValue) == 0;
    return names && tables && values && defaults
        && left->m_metaType == right->m_metaType
        && left->m_sqlType == right->m_sqlType
        && left->m_requiredStatus == right->m_requiredStatus
        && left->m_length == right->m_length
        && left->m_precision == right->m_precision
        && left->m_readOnly == right->m_readOnly
        && left->m_generated == right->m_generated
        && left->m_autoValue == right->m_autoValue;
}
