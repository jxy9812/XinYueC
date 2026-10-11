#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_TOPIC_ON
#include "XMqttTopicName.h"
#include "XMemory.h"
#include "XStringUtils.h" /* strchr/strlen 改经 XStrchr/XStrlen（外部依赖约束：string.h 白名单） */
#include <string.h>

static void VXMqttTopicName_deinit(XMqttTopicName* name);
static void VXMqttTopicName_copy(XMqttTopicName* dest, const XMqttTopicName* src);
static void VXMqttTopicName_move(XMqttTopicName* dest, XMqttTopicName* src);

static void XMqttTopicName_level_deinit(XString** level)
{
    if (level && *level) {
        XClassDelete(*level);
        *level = NULL;
    }
}

/* 库内无 memchr 原语（string.h 白名单仅 memcpy/memset/memmove/memcmp），
   本地扫描检测指定字节，避免越白名单依赖（外部依赖约束）。 */
static bool XMqttTopicName_containsByte(const char* data, size_t length, char c)
{
    for (size_t i = 0; i < length; ++i) {
        if (data[i] == c) return true;
    }
    return false;
}

XVtable* XMqttTopicName_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttTopicName)
    XVTABLE_INHERIT_XCLASS(XClass);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXMqttTopicName_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXMqttTopicName_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMqttTopicName_deinit);

    return XVTABLE_DEFAULT;
}

XMqttTopicName* XMqttTopicName_create_ex(XMemoryType memory, const char* name)
{
    XMqttTopicName* n = (XMqttTopicName*)XMemory_malloc(sizeof(XMqttTopicName), memory);
    if (n) {
        XMqttTopicName_init(n, name);
        Set_Class_Memory(n, memory); Set_Class_IsHeap(n, true);
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
    name->m_name = XString_create_utf8(topic);
}

static void VXMqttTopicName_deinit(XMqttTopicName* name)
{
    if (!name) return;
    if (name->m_name) { XClassDelete(name->m_name); name->m_name = NULL; }
    XClass_Deinit_Parent(XClass, name);
}

static void VXMqttTopicName_copy(XMqttTopicName* dest, const XMqttTopicName* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttTopicName_init(dest, NULL);
    if (dest->m_name) XClassDelete(dest->m_name);
    dest->m_name = src->m_name ? XString_create_copy(src->m_name) : XString_create_utf8(NULL);
}

static void VXMqttTopicName_move(XMqttTopicName* dest, XMqttTopicName* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttTopicName_init(dest, NULL);
    if (dest->m_name) XClassDelete(dest->m_name);
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
    if (name->m_name) { XClassDelete(name->m_name); name->m_name = NULL; }
    name->m_name = XString_create_utf8(topic ? topic : "");
}

bool XMqttTopicName_isValid(const XMqttTopicName* name)
{
    if (!name || !name->m_name) return false;
    const char* s = XString_toUtf8(name->m_name);
    size_t bytes = XString_toUtf8_length(name->m_name);
    const XChar* unicode = XString_unicode(name->m_name);
    size_t chars = XString_length_base(name->m_name);
    if (!s || chars == 0 || chars > UINT16_MAX) return false;
    for (size_t i = 0; i < chars; ++i) {
        if (unicode[i] == 0) return false;
    }
    if (XMqttTopicName_containsByte(s, bytes, '+') ||
        XMqttTopicName_containsByte(s, bytes, '#')) return false;
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

    XVector* vec = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XString*), true);
    if (!vec) return NULL;

    XContainerSetDataDeinitMethod(vec, (XCDataDeinitMethod)XMqttTopicName_level_deinit);
    const char* p = s;
    for (;;) {
        const char* slash = XStrchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : XStrlen(p);
        XString* level = len ? XString_create_with_length_utf8(p, len) :
                               XString_create_utf8("");
        if (!level || !XVector_push_back_1_base(vec, &level)) {
            if (level) XClassDelete(level);
            XClassDelete(vec);
            return NULL;
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

#endif /* XMQTT_TOPIC_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
