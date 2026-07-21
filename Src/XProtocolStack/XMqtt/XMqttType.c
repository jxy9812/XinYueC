#include "XMqttType.h"
#include "XMemory.h"
#include <string.h>

// ==================== XMqttStringPair ====================

static void VXMqttStringPair_deinit(XMqttStringPair* pair);
static void VXMqttStringPair_copy(XMqttStringPair* dest, const XMqttStringPair* src);
static void VXMqttStringPair_move(XMqttStringPair* dest, XMqttStringPair* src);

XVtable* XMqttStringPair_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXMqttStringPair_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXMqttStringPair_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMqttStringPair_deinit);

    return XVTABLE_DEFAULT;
}

XMqttStringPair* XMqttStringPair_create(const char* name, const char* value)
{
    XMqttStringPair* pair = (XMqttStringPair*)XMalloc_System(sizeof(XMqttStringPair));
    if (pair) {
        XMqttStringPair_init(pair, name, value);
        Set_Class_MemoryFree(pair, XFree_System);
    }
    return pair;
}

void XMqttStringPair_init(XMqttStringPair* pair, const char* name, const char* value)
{
    if (!pair) return;
    memset(pair, 0, sizeof(XMqttStringPair));
    XClass_init((XClass*)pair);
    XClassGetVtable(pair) = XMqttStringPair_class_init();
    if (name) pair->m_name = XString_create_utf8(name);
    if (value) pair->m_value = XString_create_utf8(value);
}

static void VXMqttStringPair_deinit(XMqttStringPair* pair)
{
    if (!pair) return;
    if (pair->m_name) { XString_delete_base(pair->m_name); pair->m_name = NULL; }
    if (pair->m_value) { XString_delete_base(pair->m_value); pair->m_value = NULL; }
    XClass_Deinit_Parent(XClass, pair);
}

static void VXMqttStringPair_copy(XMqttStringPair* dest, const XMqttStringPair* src)
{
    if (!dest || !src) return;
    if (src->m_name) dest->m_name = XString_create_copy(src->m_name);
    if (src->m_value) dest->m_value = XString_create_copy(src->m_value);
}

static void VXMqttStringPair_move(XMqttStringPair* dest, XMqttStringPair* src)
{
    if (!dest || !src) return;
    dest->m_name = src->m_name; src->m_name = NULL;
    dest->m_value = src->m_value; src->m_value = NULL;
}

const XString* XMqttStringPair_name_const(const XMqttStringPair* pair)
{
    return pair ? pair->m_name : NULL;
}

XString* XMqttStringPair_name(const XMqttStringPair* pair)
{
    if (!pair || !pair->m_name) return NULL;
    return XString_create_copy(pair->m_name);
}

void XMqttStringPair_setName(XMqttStringPair* pair, const char* n)
{
    if (!pair) return;
    if (pair->m_name) { XString_delete_base(pair->m_name); pair->m_name = NULL; }
    if (n) pair->m_name = XString_create_utf8(n);
}

const XString* XMqttStringPair_value_const(const XMqttStringPair* pair)
{
    return pair ? pair->m_value : NULL;
}

XString* XMqttStringPair_value(const XMqttStringPair* pair)
{
    if (!pair || !pair->m_value) return NULL;
    return XString_create_copy(pair->m_value);
}

void XMqttStringPair_setValue(XMqttStringPair* pair, const char* v)
{
    if (!pair) return;
    if (pair->m_value) { XString_delete_base(pair->m_value); pair->m_value = NULL; }
    if (v) pair->m_value = XString_create_utf8(v);
}

bool XMqttStringPair_equal(const XMqttStringPair* a, const XMqttStringPair* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return XString_equals(a->m_name, b->m_name, XChar_CaseSensitive) && XString_equals(a->m_value, b->m_value, XChar_CaseSensitive);
}

// ==================== XMqttUserProperties ====================

XMqttUserProperties* XMqttUserProperties_create(void)
{
    return XVector_create_ex(sizeof(XMqttStringPair), true);
}
