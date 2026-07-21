#include "XMqttTopicName.h"
#include "XMemory.h"
#include <string.h>

static void VXMqttTopicName_deinit(XMqttTopicName* name);
static void VXMqttTopicName_copy(XMqttTopicName* dest, const XMqttTopicName* src);
static void VXMqttTopicName_move(XMqttTopicName* dest, XMqttTopicName* src);

XVtable* XMqttTopicName_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXMqttTopicName_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXMqttTopicName_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMqttTopicName_deinit);

    return XVTABLE_DEFAULT;
}

XMqttTopicName* XMqttTopicName_create(const char* name)
{
    XMqttTopicName* n = (XMqttTopicName*)XMalloc_System(sizeof(XMqttTopicName));
    if (n) {
        XMqttTopicName_init(n, name);
        Set_Class_MemoryFree(n, XFree_System);
    }
    return n;
}

XMqttTopicName* XMqttTopicName_create_copy(const XMqttTopicName* other)
{
    if (!other) return NULL;
    return XMqttTopicName_create(other->m_name ? XString_toUtf8(other->m_name) : NULL);
}

void XMqttTopicName_init(XMqttTopicName* name, const char* topic)
{
    if (!name) return;
    memset(name, 0, sizeof(XMqttTopicName));
    XClass_init((XClass*)name);
    XClassGetVtable(name) = XMqttTopicName_class_init();
    if (topic) name->m_name = XString_create_utf8(topic);
}

static void VXMqttTopicName_deinit(XMqttTopicName* name)
{
    if (!name) return;
    if (name->m_name) { XString_delete_base(name->m_name); name->m_name = NULL; }
    XClass_Deinit_Parent(XClass, name);
}

static void VXMqttTopicName_copy(XMqttTopicName* dest, const XMqttTopicName* src)
{
    if (!dest || !src) return;
    if (src->m_name) dest->m_name = XString_create_copy(src->m_name);
}

static void VXMqttTopicName_move(XMqttTopicName* dest, XMqttTopicName* src)
{
    if (!dest || !src) return;
    dest->m_name = src->m_name; src->m_name = NULL;
}

const XString* XMqttTopicName_name_const(const XMqttTopicName* name)
{
    return name ? name->m_name : NULL;
}

XString* XMqttTopicName_name(const XMqttTopicName* name)
{
    if (!name || !name->m_name) return NULL;
    return XString_create_copy(name->m_name);
}

void XMqttTopicName_setName(XMqttTopicName* name, const char* topic)
{
    if (!name) return;
    if (name->m_name) { XString_delete_base(name->m_name); name->m_name = NULL; }
    if (topic) name->m_name = XString_create_utf8(topic);
}

bool XMqttTopicName_isValid(const XMqttTopicName* name)
{
    if (!name || !name->m_name) return false;
    const char* s = XString_toUtf8(name->m_name);
    if (!s || s[0] == '\0') return false;
    // 主题名称不能包含通配符
    if (strchr(s, '+') || strchr(s, '#')) return false;
    return true;
}

int XMqttTopicName_levelCount(const XMqttTopicName* name)
{
    if (!name || !name->m_name) return 0;
    const char* s = XString_toUtf8(name->m_name);
    if (!s || s[0] == '\0') return 0;
    int count = 1;
    for (; *s; s++) { if (*s == '/') count++; }
    return count;
}

XVector* XMqttTopicName_levels(const XMqttTopicName* name)
{
    if (!name || !name->m_name) return NULL;
    const char* s = XString_toUtf8(name->m_name);
    if (!s) return NULL;

    XVector* vec = XVector_create_ex(sizeof(XString*), true);
    if (!vec) return NULL;

    char buf[256];
    const char* p = s;
    while (*p) {
        const char* slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len < sizeof(buf)) {
            memcpy(buf, p, len); buf[len] = '\0';
            XString* level = XString_create_utf8(buf);
            if (level) XVector_push_back_1_base(vec, &level);
        }
        if (!slash) break;
        p = slash + 1;
    }
    return vec;
}

bool XMqttTopicName_equal(const XMqttTopicName* a, const XMqttTopicName* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return XString_equals(a->m_name, b->m_name, XChar_CaseSensitive);
}

bool XMqttTopicName_less(const XMqttTopicName* a, const XMqttTopicName* b)
{
    if (!a || !b) return false;
    return XString_compare(a->m_name, b->m_name) < 0;
}

size_t XMqttTopicName_hash(const XMqttTopicName* name, size_t seed)
{
    if (!name || !name->m_name) return seed;
    const char* s = XString_toUtf8(name->m_name);
    size_t h = seed;
    if (s) { for (; *s; s++) { h = h * 31 + (unsigned char)*s; } }
    return h;
}
