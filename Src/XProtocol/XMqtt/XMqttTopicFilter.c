#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_TOPIC_ON
#include "XMqttTopicFilter.h"
#include "XMemory.h"
#include <string.h>

static void VXMqttTopicFilter_deinit(XMqttTopicFilter* filter);
static void VXMqttTopicFilter_copy(XMqttTopicFilter* dest, const XMqttTopicFilter* src);
static void VXMqttTopicFilter_move(XMqttTopicFilter* dest, XMqttTopicFilter* src);

XVtable* XMqttTopicFilter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XClass)
	XCLASS_SET_CLASS_NAME_DEFAULT("XMqttTopicFilter");
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
    filter->m_filter = XString_create_utf8(f);
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
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttTopicFilter_init(dest, NULL);
    if (dest->m_filter) XString_delete_base(dest->m_filter);
    dest->m_filter = src->m_filter ? XString_create_copy(src->m_filter) : XString_create_utf8(NULL);
}

static void VXMqttTopicFilter_move(XMqttTopicFilter* dest, XMqttTopicFilter* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttTopicFilter_init(dest, NULL);
    if (dest->m_filter) XString_delete_base(dest->m_filter);
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
    filter->m_filter = XString_create_utf8(f ? f : "");
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
    size_t bytes = XString_toUtf8_length(filter->m_filter);
    const XChar* unicode = XString_unicode(filter->m_filter);
    size_t chars = XString_length_base(filter->m_filter);
    if (!s || chars == 0 || chars > UINT16_MAX) return false;
    for (size_t i = 0; i < chars; ++i) {
        if (unicode[i] == 0) return false;
    }
    if (chars == 1) return true;

    const char* h = strchr(s, '#');
    if (h && (h != s + bytes - 1 || h[-1] != '/')) return false;

    const char* p = s;
    while ((p = strchr(p, '+')) != NULL) {
        if ((p > s && p[-1] != '/') || (p[1] != '\0' && p[1] != '/')) return false;
        p++;
    }

    if (bytes >= 7 && memcmp(s, "$share/", 7) == 0) {
        const char* slash = strchr(s + 7, '/');
        if (!slash || slash == s + 7) return false;
    }
    return true;
}

static bool XMqttTopicFilter_nextLevel(const char* text, size_t length, size_t* pos,
                                       const char** level, size_t* levelLength)
{
    if (*pos > length) return false;
    size_t start = *pos;
    size_t end = start;
    while (end < length && text[end] != '/') ++end;
    *level = text + start;
    *levelLength = end - start;
    *pos = end < length ? end + 1 : length + 1;
    return true;
}

bool XMqttTopicFilter_match(const XMqttTopicFilter* filter, const XMqttTopicName* name, XMqttTopicFilter_MatchOption matchOptions)
{
    if (!filter || !name || !filter->m_filter || !name->m_name ||
        !XMqttTopicFilter_isValid(filter) || !XMqttTopicName_isValid(name)) return false;
    const char* f = XString_toUtf8(filter->m_filter);
    const char* t = XString_toUtf8(name->m_name);
    if (!f || !t) return false;

    size_t flen = XString_toUtf8_length(filter->m_filter);
    size_t tlen = XString_toUtf8_length(name->m_name);
    if (flen == tlen && memcmp(f, t, flen) == 0) return true;

    if ((matchOptions & XMqttTopicFilter_WildcardsDontMatchDollarTopicMatchOption) &&
        t[0] == '$' && (f[0] == '+' || (flen == 1 && f[0] == '#') ||
        (flen == 2 && f[0] == '/' && f[1] == '#'))) return false;

    size_t fp = 0, tp = 0;
    const char *fl, *tl;
    size_t fn, tn;
    while (XMqttTopicFilter_nextLevel(f, flen, &fp, &fl, &fn)) {
        if (fn == 1 && fl[0] == '#') return true;
        if (!XMqttTopicFilter_nextLevel(t, tlen, &tp, &tl, &tn)) return false;
        if (!(fn == 1 && fl[0] == '+') && (fn != tn || memcmp(fl, tl, fn) != 0))
            return false;
    }
    return !XMqttTopicFilter_nextLevel(t, tlen, &tp, &tl, &tn);
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

#endif /* XMQTT_TOPIC_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
