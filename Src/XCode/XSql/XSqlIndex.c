/**
 * @file       XSqlIndex.c
 * @brief      SQL 索引描述类实现。
 */
#include "XSqlIndex.h"

#include <string.h>

static void VXSqlIndex_copy(XSqlIndex* dest, const XSqlIndex* src);
static void VXSqlIndex_move(XSqlIndex* dest, XSqlIndex* src);
static void VXSqlIndex_deinit(XSqlIndex* index);

XVtable* XSqlIndex_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XSqlIndex)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XSqlRecord);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSqlIndex_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSqlIndex_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlIndex_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlIndex_init(XSqlIndex* index)
{
    if (!index) return;
    memset(((unsigned char*)index) + sizeof(XSqlRecord), 0, sizeof(*index) - sizeof(XSqlRecord));
    XSqlRecord_init(&index->m_parent);
    XClassSetVtable(index, XSqlIndex);
}

XSqlIndex* XSqlIndex_create(void)
{
    XSqlIndex* index = (XSqlIndex*)XMalloc_System(sizeof(XSqlIndex));
    if (!index) return NULL;
    memset(index, 0, sizeof(*index));
    XSqlIndex_init(index);
    Set_Class_MemoryFree(index, XFree_System);
    return index;
}

XSqlIndex* XSqlIndex_create_utf8(const char* cursorName, const char* name)
{
    XSqlIndex* index = XSqlIndex_create();
    if (index) {
        XSqlIndex_setCursorName_utf8(index, cursorName);
        XSqlIndex_setName_utf8(index, name);
    }
    return index;
}

XSqlIndex* XSqlIndex_create_2(const XString* cursorName, const XString* name)
{
    XSqlIndex* index = XSqlIndex_create();
    if (index) {
        XSqlIndex_setCursorName(index, cursorName);
        XSqlIndex_setName(index, name);
    }
    return index;
}

static bool xsql_index_reserve(XSqlIndex* index, size_t wanted)
{
    if (wanted <= index->m_sortCapacity) return true;
    size_t capacity = index->m_sortCapacity ? index->m_sortCapacity * 2 : 4;
    while (capacity < wanted) capacity *= 2;
    bool* values = (bool*)XRealloc_System(index->m_descending, capacity * sizeof(bool));
    if (!values) return false;
    index->m_descending = values;
    index->m_sortCapacity = capacity;
    return true;
}

static void VXSqlIndex_deinit(XSqlIndex* index)
{
    if (!index) return;
    if (index->m_cursorName) XString_delete_base(index->m_cursorName);
    if (index->m_name) XString_delete_base(index->m_name);
    if (index->m_descending) XFree_System(index->m_descending);
    index->m_cursorName = NULL;
    index->m_name = NULL;
    index->m_descending = NULL;
    index->m_sortCount = 0;
    index->m_sortCapacity = 0;
    XClass_Deinit_Parent(XSqlRecord, index);
}

static void VXSqlIndex_copy(XSqlIndex* dest, const XSqlIndex* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlIndex_init(dest);
    XClass_Parent(XSqlRecord, EXClass_Copy, void(*)(XSqlRecord*, const XSqlRecord*))(&dest->m_parent, &src->m_parent);
    if (dest->m_cursorName) XString_delete_base(dest->m_cursorName);
    if (dest->m_name) XString_delete_base(dest->m_name);
    if (dest->m_descending) XFree_System(dest->m_descending);
    dest->m_cursorName = src->m_cursorName ? XString_create_copy(src->m_cursorName) : NULL;
    dest->m_name = src->m_name ? XString_create_copy(src->m_name) : NULL;
    dest->m_descending = NULL;
    dest->m_sortCount = src->m_sortCount;
    dest->m_sortCapacity = src->m_sortCount;
    if (dest->m_sortCount) {
        dest->m_descending = (bool*)XMalloc_System(dest->m_sortCount * sizeof(bool));
        if (dest->m_descending) memcpy(dest->m_descending, src->m_descending, dest->m_sortCount * sizeof(bool));
        else dest->m_sortCount = dest->m_sortCapacity = 0;
    }
}

static void VXSqlIndex_move(XSqlIndex* dest, XSqlIndex* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlIndex_init(dest);
    XClass_Parent(XSqlRecord, EXClass_Move, void(*)(XSqlRecord*, XSqlRecord*))(&dest->m_parent, &src->m_parent);
    if (dest->m_cursorName) XString_delete_base(dest->m_cursorName);
    if (dest->m_name) XString_delete_base(dest->m_name);
    if (dest->m_descending) XFree_System(dest->m_descending);
    dest->m_cursorName = src->m_cursorName;
    dest->m_name = src->m_name;
    dest->m_descending = src->m_descending;
    dest->m_sortCount = src->m_sortCount;
    dest->m_sortCapacity = src->m_sortCapacity;
    src->m_cursorName = NULL;
    src->m_name = NULL;
    src->m_descending = NULL;
    src->m_sortCount = src->m_sortCapacity = 0;
}

XSqlIndex* XSqlIndex_create_copy(const XSqlIndex* other)
{
    if (!other) return NULL;
    XSqlIndex* result = XSqlIndex_create();
    if (result) XSqlIndex_copy_base(result, other);
    return result;
}

XSqlIndex* XSqlIndex_create_move(XSqlIndex* other)
{
    if (!other) return NULL;
    XSqlIndex* result = XSqlIndex_create();
    if (result) XSqlIndex_move_base(result, other);
    return result;
}

void XSqlIndex_swap(XSqlIndex* left, XSqlIndex* right)
{
    if (!left || !right || left == right) return;
    XSqlIndex* temp = XSqlIndex_create_move(left);
    if (!temp) return;
    XSqlIndex_move_base(left, right);
    XSqlIndex_move_base(right, temp);
    XSqlIndex_delete_base(temp);
}

void XSqlIndex_setCursorName_utf8(XSqlIndex* index, const char* name)
{
    XString* value = name ? XString_create_utf8(name) : NULL;
    XSqlIndex_setCursorName(index, value);
    if (value) XString_delete_base(value);
}

void XSqlIndex_setCursorName(XSqlIndex* index, const XString* name)
{
    if (!index) return;
    if (index->m_cursorName) XString_delete_base(index->m_cursorName);
    index->m_cursorName = name ? XString_create_copy(name) : NULL;
}

XString* XSqlIndex_cursorName(const XSqlIndex* index) { return index && index->m_cursorName ? XString_create_copy(index->m_cursorName) : XString_create(); }

void XSqlIndex_setName_utf8(XSqlIndex* index, const char* name)
{
    XString* value = name ? XString_create_utf8(name) : NULL;
    XSqlIndex_setName(index, value);
    if (value) XString_delete_base(value);
}

void XSqlIndex_setName(XSqlIndex* index, const XString* name)
{
    if (!index) return;
    if (index->m_name) XString_delete_base(index->m_name);
    index->m_name = name ? XString_create_copy(name) : NULL;
}

XString* XSqlIndex_name(const XSqlIndex* index) { return index && index->m_name ? XString_create_copy(index->m_name) : XString_create(); }

bool XSqlIndex_append_2(XSqlIndex* index, const XSqlField* field, bool descending)
{
    if (!index || !field || !XSqlRecord_append(&index->m_parent, field) || !xsql_index_reserve(index, index->m_sortCount + 1)) return false;
    index->m_descending[index->m_sortCount++] = descending;
    return true;
}

bool XSqlIndex_append(XSqlIndex* index, const XSqlField* field) { return XSqlIndex_append_2(index, field, false); }
bool XSqlIndex_isDescending(const XSqlIndex* index, int fieldIndex) { return index && fieldIndex >= 0 && (size_t)fieldIndex < index->m_sortCount && index->m_descending[fieldIndex]; }
void XSqlIndex_setDescending(XSqlIndex* index, int fieldIndex, bool descending) { if (index && fieldIndex >= 0 && (size_t)fieldIndex < index->m_sortCount) index->m_descending[fieldIndex] = descending; }
