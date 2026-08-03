#include "XMqttSubscriptionProperties.h"
#include "XMemory.h"
#include <string.h>

// ==================== XMqttSubscriptionProperties ====================

static void VSP_deinit(XMqttSubscriptionProperties* prop);
static void VSP_copy(XMqttSubscriptionProperties* dest, const XMqttSubscriptionProperties* src);
static void VSP_move(XMqttSubscriptionProperties* dest, XMqttSubscriptionProperties* src);

XVtable* XMqttSubscriptionProperties_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XClass)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VSP_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VSP_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VSP_deinit);
    return XVTABLE_DEFAULT;
}

XMqttSubscriptionProperties* XMqttSubscriptionProperties_create(void)
{
    XMqttSubscriptionProperties* p = (XMqttSubscriptionProperties*)XMalloc_System(sizeof(XMqttSubscriptionProperties));
    if (p) { XMqttSubscriptionProperties_init(p); Set_Class_MemoryFree(p, XFree_System); }
    return p;
}

XMqttSubscriptionProperties* XMqttSubscriptionProperties_create_copy(const XMqttSubscriptionProperties* other)
{
    if (!other) return NULL;
    XMqttSubscriptionProperties* p = XMqttSubscriptionProperties_create();
    if (p) XMqttSubscriptionProperties_copy_base(p, other);
    return p;
}

void XMqttSubscriptionProperties_init(XMqttSubscriptionProperties* prop)
{
    if (!prop) return;
    memset(prop, 0, sizeof(XMqttSubscriptionProperties));
    XClass_init((XClass*)prop);
    XClassGetVtable(prop) = XMqttSubscriptionProperties_class_init();
}

static void VSP_deinit(XMqttSubscriptionProperties* prop)
{
    if (!prop) return;
    if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); prop->m_userProperties = NULL; }
    XClass_Deinit_Parent(XClass, prop);
}

static void VSP_copy(XMqttSubscriptionProperties* dest, const XMqttSubscriptionProperties* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest))
        XMqttSubscriptionProperties_init(dest);
    dest->m_subscriptionIdentifier = src->m_subscriptionIdentifier;
    dest->m_noLocal = src->m_noLocal;
    if (src->m_userProperties) dest->m_userProperties = (XMqttUserProperties*)XVector_create_copy((XVector*)src->m_userProperties);
}

static void VSP_move(XMqttSubscriptionProperties* dest, XMqttSubscriptionProperties* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest))
        XMqttSubscriptionProperties_init(dest);
    memcpy(dest, src, sizeof(XMqttSubscriptionProperties));
    memset(src, 0, sizeof(XMqttSubscriptionProperties));
}

const XMqttUserProperties* XMqttSubscriptionProperties_userProperties_const(const XMqttSubscriptionProperties* prop) { return prop ? prop->m_userProperties : NULL; }
XMqttUserProperties* XMqttSubscriptionProperties_userProperties(const XMqttSubscriptionProperties* prop) { if (!prop || !prop->m_userProperties) return NULL; return (XMqttUserProperties*)XVector_create_copy((XVector*)prop->m_userProperties); }
void XMqttSubscriptionProperties_setUserProperties(XMqttSubscriptionProperties* prop, const XMqttUserProperties* user) { if (prop) { if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); } prop->m_userProperties = user ? (XMqttUserProperties*)XVector_create_copy((XVector*)user) : NULL; } }
uint32_t XMqttSubscriptionProperties_subscriptionIdentifier(const XMqttSubscriptionProperties* prop) { return prop ? prop->m_subscriptionIdentifier : 0; }
void XMqttSubscriptionProperties_setSubscriptionIdentifier(XMqttSubscriptionProperties* prop, uint32_t id) { if (prop) prop->m_subscriptionIdentifier = id; }
bool XMqttSubscriptionProperties_noLocal(const XMqttSubscriptionProperties* prop) { return prop ? prop->m_noLocal : false; }
void XMqttSubscriptionProperties_setNoLocal(XMqttSubscriptionProperties* prop, bool noloc) { if (prop) prop->m_noLocal = noloc; }

// ==================== XMqttUnsubscriptionProperties ====================

static void VUSP_deinit(XMqttUnsubscriptionProperties* prop);
static void VUSP_copy(XMqttUnsubscriptionProperties* dest, const XMqttUnsubscriptionProperties* src);
static void VUSP_move(XMqttUnsubscriptionProperties* dest, XMqttUnsubscriptionProperties* src);

XVtable* XMqttUnsubscriptionProperties_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XClass)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VUSP_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VUSP_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VUSP_deinit);
    return XVTABLE_DEFAULT;
}

XMqttUnsubscriptionProperties* XMqttUnsubscriptionProperties_create(void)
{
    XMqttUnsubscriptionProperties* p = (XMqttUnsubscriptionProperties*)XMalloc_System(sizeof(XMqttUnsubscriptionProperties));
    if (p) { XMqttUnsubscriptionProperties_init(p); Set_Class_MemoryFree(p, XFree_System); }
    return p;
}

XMqttUnsubscriptionProperties* XMqttUnsubscriptionProperties_create_copy(const XMqttUnsubscriptionProperties* other)
{
    if (!other) return NULL;
    XMqttUnsubscriptionProperties* p = XMqttUnsubscriptionProperties_create();
    if (p) XMqttUnsubscriptionProperties_copy_base(p, other);
    return p;
}

void XMqttUnsubscriptionProperties_init(XMqttUnsubscriptionProperties* prop)
{
    if (!prop) return;
    memset(prop, 0, sizeof(XMqttUnsubscriptionProperties));
    XClass_init((XClass*)prop);
    XClassGetVtable(prop) = XMqttUnsubscriptionProperties_class_init();
}

static void VUSP_deinit(XMqttUnsubscriptionProperties* prop)
{
    if (!prop) return;
    if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); prop->m_userProperties = NULL; }
    XClass_Deinit_Parent(XClass, prop);
}

static void VUSP_copy(XMqttUnsubscriptionProperties* dest, const XMqttUnsubscriptionProperties* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest))
        XMqttUnsubscriptionProperties_init(dest);
    if (src->m_userProperties) dest->m_userProperties = (XMqttUserProperties*)XVector_create_copy((XVector*)src->m_userProperties);
}

static void VUSP_move(XMqttUnsubscriptionProperties* dest, XMqttUnsubscriptionProperties* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest))
        XMqttUnsubscriptionProperties_init(dest);
    memcpy(dest, src, sizeof(XMqttUnsubscriptionProperties));
    memset(src, 0, sizeof(XMqttUnsubscriptionProperties));
}

const XMqttUserProperties* XMqttUnsubscriptionProperties_userProperties_const(const XMqttUnsubscriptionProperties* prop) { return prop ? prop->m_userProperties : NULL; }
XMqttUserProperties* XMqttUnsubscriptionProperties_userProperties(const XMqttUnsubscriptionProperties* prop) { if (!prop || !prop->m_userProperties) return NULL; return (XMqttUserProperties*)XVector_create_copy((XVector*)prop->m_userProperties); }
void XMqttUnsubscriptionProperties_setUserProperties(XMqttUnsubscriptionProperties* prop, const XMqttUserProperties* user) { if (prop) { if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); } prop->m_userProperties = user ? (XMqttUserProperties*)XVector_create_copy((XVector*)user) : NULL; } }
