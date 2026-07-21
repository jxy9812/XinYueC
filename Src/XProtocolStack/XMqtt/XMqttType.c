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
    XString_init(&pair->m_name);
    XString_init(&pair->m_value);
    if (name) XString_assign_utf8(&pair->m_name, name);
    if (value) XString_assign_utf8(&pair->m_value, value);
}

static void VXMqttStringPair_deinit(XMqttStringPair* pair)
{
    if (!pair) return;
    XString_deinit_base(&pair->m_name);
    XString_deinit_base(&pair->m_value);
    XClass_Deinit_Parent(XClass, pair);
}

static void VXMqttStringPair_copy(XMqttStringPair* dest, const XMqttStringPair* src)
{
    if (!dest || !src) return;
    /* 兼容两种调用场景：
     * 1) vtable copy（EXClass_Copy）：dest 已初始化，直接 assign
     * 2) 容器回调（通过 _base 宏）：dest 是零内存，先 init 再 assign */
    if (XClassIsVtableNull(dest))
        XMqttStringPair_init(dest, NULL, NULL);
    XString_assign(&dest->m_name, &src->m_name);
    XString_assign(&dest->m_value, &src->m_value);
}

static void VXMqttStringPair_move(XMqttStringPair* dest, XMqttStringPair* src)
{
    if (!dest || !src) return;
    /* 兼容两种调用场景：
     * 1) vtable move（EXClass_Move）：dest 已初始化，直接 swap
     * 2) 容器回调（通过 _base 宏）：dest 是零内存，先 init 再 swap */
    if (XClassIsVtableNull(dest))
        XMqttStringPair_init(dest, NULL, NULL);
    XString_swap(&dest->m_name, &src->m_name);
    XString_swap(&dest->m_value, &src->m_value);
}

const XString* XMqttStringPair_name_const(const XMqttStringPair* pair)
{
    return pair ? &pair->m_name : NULL;
}

XString* XMqttStringPair_name(const XMqttStringPair* pair)
{
    if (!pair) return NULL;
    return XString_create_copy(&pair->m_name);
}

void XMqttStringPair_setName(XMqttStringPair* pair, const char* n)
{
    if (!pair) return;
    if (n) {
        XString_assign_utf8(&pair->m_name, n);
    } else {
        XString_clear_base(&pair->m_name);
    }
}

const XString* XMqttStringPair_value_const(const XMqttStringPair* pair)
{
    return pair ? &pair->m_value : NULL;
}

XString* XMqttStringPair_value(const XMqttStringPair* pair)
{
    if (!pair) return NULL;
    return XString_create_copy(&pair->m_value);
}

void XMqttStringPair_setValue(XMqttStringPair* pair, const char* v)
{
    if (!pair) return;
    if (v) {
        XString_assign_utf8(&pair->m_value, v);
    } else {
        XString_clear_base(&pair->m_value);
    }
}

bool XMqttStringPair_equal(const XMqttStringPair* a, const XMqttStringPair* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return XString_equals(&a->m_name, &b->m_name, XChar_CaseSensitive) && XString_equals(&a->m_value, &b->m_value, XChar_CaseSensitive);
}

// ==================== XMqttUserProperties ====================

XMqttUserProperties* XMqttUserProperties_create(void)
{
    XMqttUserProperties* props = XVector_create_ex(sizeof(XMqttStringPair), true);
    if (props) {
        /* 直接用 XMqttStringPair 的虚函数 copy/move/deinit 作为容器元素回调 */
        XContainerSetDataCopyMethod(props, (XCDataCopyMethod)XMqttStringPair_copy_base);
        XContainerSetDataMoveMethod(props, (XCDataMoveMethod)XMqttStringPair_move_base);
        XContainerSetDataDeinitMethod(props, (XCDataDeinitMethod)XMqttStringPair_deinit_base);
    }
    return props;
}
