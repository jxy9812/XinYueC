#include "XMqttTopicFilter.h"
#include "XMemory.h"
#include <string.h>

static void VXMqttTopicFilter_deinit(XMqttTopicFilter* filter);
static void VXMqttTopicFilter_copy(XMqttTopicFilter* dest, const XMqttTopicFilter* src);
static void VXMqttTopicFilter_move(XMqttTopicFilter* dest, XMqttTopicFilter* src);

XVtable* XMqttTopicFilter_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXMqttTopicFilter_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXMqttTopicFilter_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMqttTopicFilter_deinit);
    return XVTABLE_DEFAULT;
}

XMqttTopicFilter* XMqttTopicFilter_create(const char* filter)
{
    XMqttTopicFilter* f = (XMqttTopicFilter*)XMalloc_System(sizeof(XMqttTopicFilter));
    if (f) { XMqttTopicFilter_init(f, filter); Set_Class_MemoryFree(f, XFree_System); }
    return f;
}

XMqttTopicFilter* XMqttTopicFilter_create_copy(const XMqttTopicFilter* other)
{
    if (!other) return NULL;
    return XMqttTopicFilter_create(other->m_filter ? XString_toUtf8(other->m_filter) : NULL);
}

void XMqttTopicFilter_init(XMqttTopicFilter* filter, const char* f)
{
    if (!filter) return;
    memset(filter, 0, sizeof(XMqttTopicFilter));
    XClass_init((XClass*)filter);
    XClassGetVtable(filter) = XMqttTopicFilter_class_init();
    if (f) filter->m_filter = XString_create_utf8(f);
}

static void VXMqttTopicFilter_deinit(XMqttTopicFilter* filter)
{
    if (!filter) return;
    if (filter->m_filter) { XString_delete_base(filter->m_filter); filter->m_filter = NULL; }
    XClass_Deinit_Parent(XClass, filter);
}

static void VXMqttTopicFilter_copy(XMqttTopicFilter* dest, const XMqttTopicFilter* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest))
        XMqttTopicFilter_init(dest, NULL);
    if (src->m_filter) dest->m_filter = XString_create_copy(src->m_filter);
}

static void VXMqttTopicFilter_move(XMqttTopicFilter* dest, XMqttTopicFilter* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest))
        XMqttTopicFilter_init(dest, NULL);
    dest->m_filter = src->m_filter; src->m_filter = NULL;
}

const XString* XMqttTopicFilter_filter_const(const XMqttTopicFilter* filter) { return filter ? filter->m_filter : NULL; }

XString* XMqttTopicFilter_filter(const XMqttTopicFilter* filter)
{
    if (!filter || !filter->m_filter) return NULL;
    return XString_create_copy(filter->m_filter);
}

void XMqttTopicFilter_setFilter(XMqttTopicFilter* filter, const char* f)
{
    if (!filter) return;
    if (filter->m_filter) { XString_delete_base(filter->m_filter); filter->m_filter = NULL; }
    if (f) filter->m_filter = XString_create_utf8(f);
}

XString* XMqttTopicFilter_sharedSubscriptionName(const XMqttTopicFilter* filter)
{
    if (!filter || !filter->m_filter) return NULL;
    const char* s = XString_toUtf8(filter->m_filter);
    if (!s || strncmp(s, "$share/", 7) != 0) return NULL;
    s += 7;
    const char* slash = strchr(s, '/');
    if (!slash) return NULL;
    return XString_create_with_length_utf8(s, (size_t)(slash - s));
}

bool XMqttTopicFilter_isValid(const XMqttTopicFilter* filter)
{
    if (!filter || !filter->m_filter) return false;
    const char* s = XString_toUtf8(filter->m_filter);
    if (!s || s[0] == '\0') return false;
    // '#' 只能在末尾
    const char* h = strchr(s, '#');
    if (h) { if (h[1] != '\0') return false; }
    // '+' 必须在 '/' 之间或末尾
    const char* p = s;
    while ((p = strchr(p, '+')) != NULL) {
        if ((p > s && p[-1] != '/') || (p[1] != '\0' && p[1] != '/')) return false;
        p++;
    }
    return true;
}

bool XMqttTopicFilter_match(const XMqttTopicFilter* filter, const XMqttTopicName* name, XMqttTopicFilter_MatchOption matchOptions)
{
    if (!filter || !name || !filter->m_filter || !name->m_name) return false;
    const char* f = XString_toUtf8(filter->m_filter);
    const char* t = XString_toUtf8(name->m_name);
    if (!f || !t) return false;

    // $ 主题不匹配通配符
    if ((matchOptions & XMqttTopicFilter_WildcardsDontMatchDollarTopicMatchOption) && t[0] == '$') return false;

    // 逐段匹配
    while (*f && *t) {
        if (*f == '+') {
            // 跳过当前层级
            while (*t && *t != '/') t++;
            f++;
            if (*t == '/') t++;
            if (*f == '/') f++;
        } else if (*f == '#') {
            return true; // 匹配剩余所有
        } else if (*f == *t) {
            f++; t++;
        } else {
            return false;
        }
    }
    // 跳过尾部 '/'
    while (*f == '/') f++;
    while (*t == '/') t++;
    return (*f == '\0' && *t == '\0');
}

bool XMqttTopicFilter_equal(const XMqttTopicFilter* a, const XMqttTopicFilter* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return XString_equals(a->m_filter, b->m_filter, XChar_CaseSensitive);
}

bool XMqttTopicFilter_less(const XMqttTopicFilter* a, const XMqttTopicFilter* b)
{
    if (!a || !b) return false;
    return XString_compare(a->m_filter, b->m_filter) < 0;
}

size_t XMqttTopicFilter_hash(const XMqttTopicFilter* filter, size_t seed)
{
    if (!filter || !filter->m_filter) return seed;
    const char* s = XString_toUtf8(filter->m_filter);
    size_t h = seed;
    if (s) { for (; *s; s++) { h = h * 31 + (unsigned char)*s; } }
    return h;
}
