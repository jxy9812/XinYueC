/**
 * @file       XSqlRelation.c
 * @brief      SQL 外键关系描述类实现。
 */
#include "XSqlRelation.h"

#include <string.h>

static void VXSqlRelation_copy(XSqlRelation* dest, const XSqlRelation* src);
static void VXSqlRelation_move(XSqlRelation* dest, XSqlRelation* src);
static void VXSqlRelation_deinit(XSqlRelation* relation);

XVtable* XSqlRelation_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSqlRelation))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSqlRelation_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSqlRelation_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlRelation_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlRelation_init(XSqlRelation* relation)
{
    if (!relation) return;
    memset(((unsigned char*)relation) + sizeof(XClass), 0, sizeof(*relation) - sizeof(XClass));
    XClass_init(&relation->m_class);
    XClassSetVtable(relation, XSqlRelation);
}

void XSqlRelation_init_utf8(XSqlRelation* relation, const char* tableName,
                            const char* indexColumn, const char* displayColumn)
{
    XSqlRelation_init(relation);
    if (!relation) return;
    XSqlRelation_setTableName_utf8(relation, tableName);
    XSqlRelation_setIndexColumn_utf8(relation, indexColumn);
    XSqlRelation_setDisplayColumn_utf8(relation, displayColumn);
}

void XSqlRelation_init_2(XSqlRelation* relation, const XString* tableName,
                         const XString* indexColumn, const XString* displayColumn)
{
    XSqlRelation_init(relation);
    if (!relation) return;
    XSqlRelation_setTableName(relation, tableName);
    XSqlRelation_setIndexColumn(relation, indexColumn);
    XSqlRelation_setDisplayColumn(relation, displayColumn);
}

XSqlRelation* XSqlRelation_create(void)
{
    XSqlRelation* relation = (XSqlRelation*)XMalloc_System(sizeof(XSqlRelation));
    if (!relation) return NULL;
    memset(relation, 0, sizeof(*relation));
    XSqlRelation_init(relation);
    Set_Class_MemoryFree(relation, XFree_System);
    return relation;
}

XSqlRelation* XSqlRelation_create_utf8(const char* tableName, const char* indexColumn,
                                       const char* displayColumn)
{
    XSqlRelation* relation = XSqlRelation_create();
    if (relation) {
        XSqlRelation_setTableName_utf8(relation, tableName);
        XSqlRelation_setIndexColumn_utf8(relation, indexColumn);
        XSqlRelation_setDisplayColumn_utf8(relation, displayColumn);
    }
    return relation;
}

XSqlRelation* XSqlRelation_create_2(const XString* tableName, const XString* indexColumn,
                                    const XString* displayColumn)
{
    XSqlRelation* relation = XSqlRelation_create();
    if (relation) {
        XSqlRelation_setTableName(relation, tableName);
        XSqlRelation_setIndexColumn(relation, indexColumn);
        XSqlRelation_setDisplayColumn(relation, displayColumn);
    }
    return relation;
}

static void VXSqlRelation_deinit(XSqlRelation* relation)
{
    if (!relation) return;
    if (relation->m_tableName) XString_delete_base(relation->m_tableName);
    if (relation->m_indexColumn) XString_delete_base(relation->m_indexColumn);
    if (relation->m_displayColumn) XString_delete_base(relation->m_displayColumn);
    relation->m_tableName = NULL;
    relation->m_indexColumn = NULL;
    relation->m_displayColumn = NULL;
    XClass_Deinit_Parent(XClass, relation);
}

static void VXSqlRelation_copy(XSqlRelation* dest, const XSqlRelation* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlRelation_init(dest);
    XSqlRelation_setTableName_utf8(dest, src->m_tableName ? XString_toUtf8(src->m_tableName) : NULL);
    XSqlRelation_setIndexColumn_utf8(dest, src->m_indexColumn ? XString_toUtf8(src->m_indexColumn) : NULL);
    XSqlRelation_setDisplayColumn_utf8(dest, src->m_displayColumn ? XString_toUtf8(src->m_displayColumn) : NULL);
}

static void VXSqlRelation_move(XSqlRelation* dest, XSqlRelation* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlRelation_init(dest);
    if (dest->m_tableName) XString_delete_base(dest->m_tableName);
    if (dest->m_indexColumn) XString_delete_base(dest->m_indexColumn);
    if (dest->m_displayColumn) XString_delete_base(dest->m_displayColumn);
    dest->m_tableName = src->m_tableName; dest->m_indexColumn = src->m_indexColumn; dest->m_displayColumn = src->m_displayColumn;
    src->m_tableName = NULL; src->m_indexColumn = NULL; src->m_displayColumn = NULL;
}
XSqlRelation* XSqlRelation_create_copy(const XSqlRelation* other) { if (!other) return NULL; XSqlRelation* result = XSqlRelation_create(); if (result) XSqlRelation_copy_base(result, other); return result; }
XSqlRelation* XSqlRelation_create_move(XSqlRelation* other) { if (!other) return NULL; XSqlRelation* result = XSqlRelation_create(); if (result) XSqlRelation_move_base(result, other); return result; }
void XSqlRelation_swap(XSqlRelation* left, XSqlRelation* right) { if (!left || !right || left == right) return; XSqlRelation* tmp = XSqlRelation_create_move(left); XSqlRelation_move_base(left, right); XSqlRelation_move_base(right, tmp); XSqlRelation_delete_base(tmp); }
void XSqlRelation_setTableName(XSqlRelation* relation, const XString* tableName) { if (!relation) return; if (relation->m_tableName) XString_delete_base(relation->m_tableName); relation->m_tableName = tableName ? XString_create_copy(tableName) : NULL; }
void XSqlRelation_setIndexColumn(XSqlRelation* relation, const XString* indexColumn) { if (!relation) return; if (relation->m_indexColumn) XString_delete_base(relation->m_indexColumn); relation->m_indexColumn = indexColumn ? XString_create_copy(indexColumn) : NULL; }
void XSqlRelation_setDisplayColumn(XSqlRelation* relation, const XString* displayColumn) { if (!relation) return; if (relation->m_displayColumn) XString_delete_base(relation->m_displayColumn); relation->m_displayColumn = displayColumn ? XString_create_copy(displayColumn) : NULL; }
void XSqlRelation_setTableName_utf8(XSqlRelation* relation, const char* tableName) { XString* value = tableName ? XString_create_utf8(tableName) : NULL; XSqlRelation_setTableName(relation, value); if (value) XString_delete_base(value); }
void XSqlRelation_setIndexColumn_utf8(XSqlRelation* relation, const char* indexColumn) { XString* value = indexColumn ? XString_create_utf8(indexColumn) : NULL; XSqlRelation_setIndexColumn(relation, value); if (value) XString_delete_base(value); }
void XSqlRelation_setDisplayColumn_utf8(XSqlRelation* relation, const char* displayColumn) { XString* value = displayColumn ? XString_create_utf8(displayColumn) : NULL; XSqlRelation_setDisplayColumn(relation, value); if (value) XString_delete_base(value); }
XString* XSqlRelation_tableName(const XSqlRelation* relation) { return relation && relation->m_tableName ? XString_create_copy(relation->m_tableName) : XString_create(); }
XString* XSqlRelation_indexColumn(const XSqlRelation* relation) { return relation && relation->m_indexColumn ? XString_create_copy(relation->m_indexColumn) : XString_create(); }
XString* XSqlRelation_displayColumn(const XSqlRelation* relation) { return relation && relation->m_displayColumn ? XString_create_copy(relation->m_displayColumn) : XString_create(); }
bool XSqlRelation_isValid(const XSqlRelation* relation) { return relation && relation->m_tableName && relation->m_indexColumn && relation->m_displayColumn && XString_length_base(relation->m_tableName) && XString_length_base(relation->m_indexColumn) && XString_length_base(relation->m_displayColumn); }
