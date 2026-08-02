/**
 * @file       XSqlError.c
 * @brief      SQL 错误信息类实现。
 */
#include "XSqlError.h"

#include <string.h>

static void VXSqlError_copy(XSqlError* dest, const XSqlError* src);
static void VXSqlError_move(XSqlError* dest, XSqlError* src);
static void VXSqlError_deinit(XSqlError* error);

XVtable* XSqlError_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSqlError))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSqlError_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSqlError_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlError_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlError_init(XSqlError* error)
{
    if (!error) return;
    memset(((unsigned char*)error) + sizeof(XClass), 0, sizeof(*error) - sizeof(XClass));
    XClass_init((XClass*)error);
    XClassSetVtable(error, XSqlError);
    error->m_type = XSqlErrorType_NoError;
}

static void xsql_error_assign_string(XString** target, const XString* source)
{
    if (*target) {
        XString_delete_base(*target);
        *target = NULL;
    }
    if (source) *target = XString_create_copy(source);
}

static void VXSqlError_deinit(XSqlError* error)
{
    if (!error) return;
    if (error->m_driverText) XString_delete_base(error->m_driverText);
    if (error->m_databaseText) XString_delete_base(error->m_databaseText);
    if (error->m_errorCode) XString_delete_base(error->m_errorCode);
    error->m_driverText = NULL;
    error->m_databaseText = NULL;
    error->m_errorCode = NULL;
    XClass_Deinit_Parent(XClass, error);
}

static void VXSqlError_copy(XSqlError* dest, const XSqlError* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlError_init(dest);
    xsql_error_assign_string(&dest->m_driverText, src->m_driverText);
    xsql_error_assign_string(&dest->m_databaseText, src->m_databaseText);
    xsql_error_assign_string(&dest->m_errorCode, src->m_errorCode);
    dest->m_type = src->m_type;
}

static void VXSqlError_move(XSqlError* dest, XSqlError* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlError_init(dest);
    if (dest->m_driverText) XString_delete_base(dest->m_driverText);
    if (dest->m_databaseText) XString_delete_base(dest->m_databaseText);
    if (dest->m_errorCode) XString_delete_base(dest->m_errorCode);
    dest->m_driverText = src->m_driverText;
    dest->m_databaseText = src->m_databaseText;
    dest->m_errorCode = src->m_errorCode;
    dest->m_type = src->m_type;
    src->m_driverText = NULL;
    src->m_databaseText = NULL;
    src->m_errorCode = NULL;
    src->m_type = XSqlErrorType_NoError;
}

XSqlError* XSqlError_create(const XString* driverText,
                            const XString* databaseText,
                            XSqlErrorType type,
                            const XString* errorCode)
{
    XSqlError* error = (XSqlError*)XMalloc_System(sizeof(XSqlError));
    if (!error) return NULL;
    memset(error, 0, sizeof(*error));
    XSqlError_init(error);
    XSqlError_setDriverText(error, driverText);
    XSqlError_setDatabaseText(error, databaseText);
    XSqlError_setNativeErrorCode(error, errorCode);
    error->m_type = type;
    Set_Class_MemoryFree(error, XFree_System);
    return error;
}

XSqlError* XSqlError_create_utf8(const char* driverText, const char* databaseText,
                                 XSqlErrorType type, const char* errorCode)
{
    XString* driver = driverText ? XString_create_utf8(driverText) : NULL;
    XString* database = databaseText ? XString_create_utf8(databaseText) : NULL;
    XString* code = errorCode ? XString_create_utf8(errorCode) : NULL;
    XSqlError* result = XSqlError_create(driver, database, type, code);
    if (driver) XString_delete_base(driver);
    if (database) XString_delete_base(database);
    if (code) XString_delete_base(code);
    return result;
}

XSqlError* XSqlError_create_copy(const XSqlError* other)
{
    if (!other) return NULL;
    XSqlError* result = XSqlError_create(NULL, NULL, XSqlErrorType_NoError, NULL);
    if (result) XSqlError_copy_base(result, other);
    return result;
}

XSqlError* XSqlError_create_move(XSqlError* other)
{
    if (!other) return NULL;
    XSqlError* result = XSqlError_create(NULL, NULL, XSqlErrorType_NoError, NULL);
    if (result) XSqlError_move_base(result, other);
    return result;
}

void XSqlError_swap(XSqlError* left, XSqlError* right)
{
    if (!left || !right || left == right) return;
    XSqlError* temp = XSqlError_create_move(left);
    if (!temp) return;
    XSqlError_move_base(left, right);
    XSqlError_move_base(right, temp);
    XSqlError_delete_base(temp);
}

void XSqlError_setDriverText(XSqlError* error, const XString* text) { if (error) xsql_error_assign_string(&error->m_driverText, text); }
void XSqlError_setDatabaseText(XSqlError* error, const XString* text) { if (error) xsql_error_assign_string(&error->m_databaseText, text); }
void XSqlError_setNativeErrorCode(XSqlError* error, const XString* code) { if (error) xsql_error_assign_string(&error->m_errorCode, code); }
void XSqlError_setType(XSqlError* error, XSqlErrorType type) { if (error) error->m_type = type; }
XString* XSqlError_driverText(const XSqlError* error) { return error && error->m_driverText ? XString_create_copy(error->m_driverText) : XString_create(); }
XString* XSqlError_databaseText(const XSqlError* error) { return error && error->m_databaseText ? XString_create_copy(error->m_databaseText) : XString_create(); }
XString* XSqlError_nativeErrorCode(const XSqlError* error) { return error && error->m_errorCode ? XString_create_copy(error->m_errorCode) : XString_create(); }
XSqlErrorType XSqlError_type(const XSqlError* error) { return error ? error->m_type : XSqlErrorType_UnknownError; }

XString* XSqlError_text(const XSqlError* error)
{
    XString* result = XString_create();
    if (!result || !error) return result;
    if (error->m_driverText) XString_append(result, error->m_driverText);
    if (error->m_driverText && error->m_databaseText) XString_append_utf8(result, ": ");
    if (error->m_databaseText) XString_append(result, error->m_databaseText);
    return result;
}

bool XSqlError_isValid(const XSqlError* error) { return error && error->m_type != XSqlErrorType_NoError; }

bool XSqlError_equals(const XSqlError* left, const XSqlError* right)
{
    if (left == right) return true;
    if (!left || !right || left->m_type != right->m_type) return false;
    return (!left->m_errorCode && !right->m_errorCode)
        || (left->m_errorCode && right->m_errorCode
            && XString_equals(left->m_errorCode, right->m_errorCode, XChar_CaseSensitive));
}
