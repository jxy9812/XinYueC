/**
 * @file XProcessEnvironment.c
 * @brief XProcessEnvironment 的公开 API 实现。
 * @details
 * 本实现只负责 name=value 集合的生命周期、查找、复制和修改；当前系统环境
 * 的枚举委托给 XProcess Drive 后端，避免公共层依赖平台环境变量接口。
 */

#include "XProcessConfig.h"

#if XProcess_ON
#include "XProcessEnvironment.h"

#if XPROCESS_ENVIRONMENT_ON

#include "XMemory.h"
#include <string.h>

/* 当前系统环境由 Drive/Posix 或 Drive/windows 提供。 */
XProcessEnvironment* XProcessEnvironment_platform_systemEnvironment(void);

static bool xpe_valid_name(const char* name)
{
    return name && name[0] != '\0' && strchr(name, '=') == NULL;
}

/* Windows 环境变量名大小写不敏感；POSIX 保持标准的大小写敏感语义。 */
static bool xpe_name_equal(const char* left, const char* right, size_t length)
{
    size_t i;
    if (!left || !right) return false;
    for (i = 0; i < length; ++i) {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
#ifdef _WIN32
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
#endif
        if (a != b) return false;
    }
    return true;
}

static int64_t xpe_find_index(const XProcessEnvironment* self, const char* name)
{
    size_t i;
    if (!self || !self->m_entries || !xpe_valid_name(name)) return -1;
    for (i = 0; i < XStringList_size_base(self->m_entries); ++i) {
        const XString* item = XStringList_at_base(self->m_entries, i);
        const char* text = item ? XString_toUtf8(item) : NULL;
        const char* equal = text ? strchr(text, '=') : NULL;
        size_t nameLen = equal ? (size_t)(equal - text) : 0;
        if (equal && nameLen == strlen(name) && xpe_name_equal(text, name, nameLen))
            return (int64_t)i;
    }
    return -1;
}

static bool xpe_ensure_entries(XProcessEnvironment* self)
{
    if (!self) return false;
    if (!self->m_entries) self->m_entries = XStringList_create();
    if (!self->m_entries) return false;
    self->m_inherit = false;
    return true;
}

void XProcessEnvironment_init(XProcessEnvironment* self)
{
    if (!self) return;
    self->m_entries = XStringList_create();
    self->m_inherit = false;
}

void XProcessEnvironment_initInherit(XProcessEnvironment* self)
{
    if (!self) return;
    self->m_entries = NULL;
    self->m_inherit = true;
}

XProcessEnvironment* XProcessEnvironment_create(void)
{
    XProcessEnvironment* self = (XProcessEnvironment*)XCalloc_System(1, sizeof(*self));
    if (!self) return NULL;
    XProcessEnvironment_init(self);
    return self;
}

XProcessEnvironment* XProcessEnvironment_createInherit(void)
{
    XProcessEnvironment* self = (XProcessEnvironment*)XCalloc_System(1, sizeof(*self));
    if (!self) return NULL;
    XProcessEnvironment_initInherit(self);
    return self;
}

XProcessEnvironment* XProcessEnvironment_createCopy(const XProcessEnvironment* other)
{
    XProcessEnvironment* self;
    if (!other) return NULL;
    self = other->m_inherit ? XProcessEnvironment_createInherit()
                            : XProcessEnvironment_create();
    if (!self) return NULL;
    if (!other->m_inherit && other->m_entries) {
        XStringList* copy = XStringList_create_copy(other->m_entries);
        if (!copy) {
            XProcessEnvironment_delete(self);
            return NULL;
        }
        XStringList_delete_base(self->m_entries);
        self->m_entries = copy;
    }
    return self;
}

void XProcessEnvironment_deinit(XProcessEnvironment* self)
{
    if (!self) return;
    if (self->m_entries) {
        XStringList_delete_base(self->m_entries);
        self->m_entries = NULL;
    }
    self->m_inherit = false;
}

void XProcessEnvironment_delete(XProcessEnvironment* self)
{
    if (!self) return;
    XProcessEnvironment_deinit(self);
    XFree_System(self);
}

bool XProcessEnvironment_isEmpty(const XProcessEnvironment* self)
{
    return !self || !self->m_entries || XStringList_size_base(self->m_entries) == 0;
}

bool XProcessEnvironment_inheritsFromParent(const XProcessEnvironment* self)
{
    return self && self->m_inherit;
}

void XProcessEnvironment_clear(XProcessEnvironment* self)
{
    if (!self) return;
    if (self->m_entries) XStringList_clear_base(self->m_entries);
    /* 对齐 QProcessEnvironment：继承标记的空对象清空后仍继承父环境。 */
}

void XProcessEnvironment_swap(XProcessEnvironment* self,
                              XProcessEnvironment* other)
{
    XProcessEnvironment temporary;
    if (!self || !other || self == other) return;
    temporary = *self;
    *self = *other;
    *other = temporary;
}

bool XProcessEnvironment_equals(const XProcessEnvironment* self,
                                const XProcessEnvironment* other)
{
    size_t i;
    size_t count;
    if (!self || !other || self->m_inherit != other->m_inherit) return false;
    if (self->m_inherit) return true;
    count = self->m_entries ? XStringList_size_base(self->m_entries) : 0;
    if (count != (other->m_entries ? XStringList_size_base(other->m_entries) : 0))
        return false;
    for (i = 0; i < count; ++i) {
        const XString* item = XStringList_at_base(self->m_entries, i);
        const char* text = item ? XString_toUtf8(item) : NULL;
        const char* equal = text ? strchr(text, '=') : NULL;
        XString* name;
        XString* value;
        int64_t index;
        if (!equal) return false;
        name = XString_create_with_length_utf8(text, (size_t)(equal - text));
        if (!name) return false;
        index = xpe_find_index(other, XString_toUtf8(name));
        if (index < 0) {
            XString_delete_base(name);
            return false;
        }
        value = XProcessEnvironment_value_utf8(other, XString_toUtf8(name), NULL);
        XString_delete_base(name);
        if (!value) return false;
        if (strcmp(XString_toUtf8(value), equal + 1) != 0) {
            XString_delete_base(value);
            return false;
        }
        XString_delete_base(value);
    }
    return true;
}

bool XProcessEnvironment_contains_utf8(const XProcessEnvironment* self, const char* name)
{
    return xpe_find_index(self, name) >= 0;
}

bool XProcessEnvironment_contains(const XProcessEnvironment* self, const XString* name)
{
    return name && XProcessEnvironment_contains_utf8(self, XString_toUtf8(name));
}

bool XProcessEnvironment_insert_utf8(XProcessEnvironment* self,
                                     const char* name, const char* value)
{
    XString* entry;
    int64_t index;
    size_t before;
    if (!self || !xpe_valid_name(name)) return false;
    if (!xpe_ensure_entries(self)) return false;
    entry = XString_create_fmt_utf8("%s=%s", name, value ? value : "");
    if (!entry) return false;
    index = xpe_find_index(self, name);
    if (index >= 0) XStringList_remove_base(self->m_entries, index, 1);
    before = XStringList_size_base(self->m_entries);
    XStringList_push_back_base(self->m_entries, entry);
    XString_delete_base(entry);
    return XStringList_size_base(self->m_entries) == before + 1;
}

bool XProcessEnvironment_insert(XProcessEnvironment* self,
                                const XString* name, const XString* value)
{
    return name && XProcessEnvironment_insert_utf8(self, XString_toUtf8(name),
                                                    value ? XString_toUtf8(value) : "");
}

bool XProcessEnvironment_remove_utf8(XProcessEnvironment* self, const char* name)
{
    int64_t index = xpe_find_index(self, name);
    if (index < 0) return false;
    XStringList_remove_base(self->m_entries, index, 1);
    return true;
}

bool XProcessEnvironment_remove(XProcessEnvironment* self, const XString* name)
{
    return name && XProcessEnvironment_remove_utf8(self, XString_toUtf8(name));
}

XString* XProcessEnvironment_value_utf8(const XProcessEnvironment* self,
                                         const char* name,
                                         const char* defaultValue)
{
    int64_t index = xpe_find_index(self, name);
    const XString* item;
    const char* text;
    const char* equal;
    if (index < 0) return XString_create_utf8(defaultValue ? defaultValue : "");
    item = XStringList_at_base(self->m_entries, index);
    text = item ? XString_toUtf8(item) : NULL;
    equal = text ? strchr(text, '=') : NULL;
    return XString_create_utf8(equal ? equal + 1 : (defaultValue ? defaultValue : ""));
}

XString* XProcessEnvironment_value(const XProcessEnvironment* self,
                                   const XString* name,
                                   const XString* defaultValue)
{
    return name ? XProcessEnvironment_value_utf8(self, XString_toUtf8(name),
                                                  defaultValue ? XString_toUtf8(defaultValue) : "")
                : XString_create_utf8(defaultValue ? XString_toUtf8(defaultValue) : "");
}

XStringList* XProcessEnvironment_toStringList(const XProcessEnvironment* self)
{
    if (!self || self->m_inherit || !self->m_entries) return XStringList_create();
    return XStringList_create_copy(self->m_entries);
}

XStringList* XProcessEnvironment_keys(const XProcessEnvironment* self)
{
    XStringList* result = XStringList_create();
    size_t i;
    if (!result || !self || self->m_inherit || !self->m_entries) return result;
    for (i = 0; i < XStringList_size_base(self->m_entries); ++i) {
        const XString* item = XStringList_at_base(self->m_entries, i);
        const char* text = item ? XString_toUtf8(item) : NULL;
        const char* equal = text ? strchr(text, '=') : NULL;
        if (equal) {
            XString* key = XString_create_with_length_utf8(text, (size_t)(equal - text));
            if (!key) {
                XStringList_delete_base(result);
                return NULL;
            }
            XStringList_push_back_base(result, key);
            XString_delete_base(key);
        }
    }
    return result;
}

bool XProcessEnvironment_insertEnvironment(XProcessEnvironment* self,
                                            const XProcessEnvironment* other)
{
    size_t i;
    if (!self || !other) return false;
    if (other->m_inherit || !other->m_entries) return true;
    if (!xpe_ensure_entries(self)) return false;
    for (i = 0; i < XStringList_size_base(other->m_entries); ++i) {
        const XString* item = XStringList_at_base(other->m_entries, i);
        const char* text = item ? XString_toUtf8(item) : NULL;
        const char* equal = text ? strchr(text, '=') : NULL;
        XString* name;
        bool inserted;
        if (!equal || equal == text) return false;
        /* insert_utf8 要求名称独立且以 NUL 结尾，不能直接传入 name=value。 */
        name = XString_create_with_length_utf8(text, (size_t)(equal - text));
        if (!name) return false;
        inserted = XProcessEnvironment_insert_utf8(self, XString_toUtf8(name), equal + 1);
        XString_delete_base(name);
        if (!inserted) return false;
    }
    return true;
}

XProcessEnvironment* XProcessEnvironment_systemEnvironment(void)
{
    return XProcessEnvironment_platform_systemEnvironment();
}

#else

/* 环境功能关闭时保留 ABI 类型和空实现，确保 XProcess 仍可独立编译。 */
#include "XMemory.h"

void XProcessEnvironment_init(XProcessEnvironment* self)
{
    if (self) { self->m_entries = NULL; self->m_inherit = false; }
}

void XProcessEnvironment_initInherit(XProcessEnvironment* self)
{
    if (self) { self->m_entries = NULL; self->m_inherit = true; }
}

XProcessEnvironment* XProcessEnvironment_create(void)
{
    XProcessEnvironment* self = (XProcessEnvironment*)XCalloc_System(sizeof(*self), 1);
    if (self) XProcessEnvironment_init(self);
    return self;
}

XProcessEnvironment* XProcessEnvironment_createInherit(void)
{
    XProcessEnvironment* self = (XProcessEnvironment*)XCalloc_System(sizeof(*self), 1);
    if (self) XProcessEnvironment_initInherit(self);
    return self;
}

XProcessEnvironment* XProcessEnvironment_createCopy(const XProcessEnvironment* other)
{
    XProcessEnvironment* self;
    if (!other) return NULL;
    self = other->m_inherit ? XProcessEnvironment_createInherit()
                            : XProcessEnvironment_create();
    return self;
}

void XProcessEnvironment_deinit(XProcessEnvironment* self)
{
    if (self) { self->m_entries = NULL; self->m_inherit = false; }
}

void XProcessEnvironment_delete(XProcessEnvironment* self)
{
    if (self) { XProcessEnvironment_deinit(self); XFree_System(self); }
}

bool XProcessEnvironment_isEmpty(const XProcessEnvironment* self)
{
    return !self || !self->m_entries;
}

bool XProcessEnvironment_inheritsFromParent(const XProcessEnvironment* self)
{
    return self && self->m_inherit;
}

void XProcessEnvironment_clear(XProcessEnvironment* self)
{
    (void)self;
}

void XProcessEnvironment_swap(XProcessEnvironment* self,
                              XProcessEnvironment* other)
{
    XProcessEnvironment temporary;
    if (!self || !other || self == other) return;
    temporary = *self;
    *self = *other;
    *other = temporary;
}

bool XProcessEnvironment_equals(const XProcessEnvironment* self,
                                const XProcessEnvironment* other)
{
    return self && other && self->m_inherit == other->m_inherit &&
           self->m_entries == other->m_entries;
}

bool XProcessEnvironment_contains_utf8(const XProcessEnvironment* self, const char* name)
{
    (void)self; (void)name; return false;
}

bool XProcessEnvironment_contains(const XProcessEnvironment* self, const XString* name)
{
    (void)self; (void)name; return false;
}

bool XProcessEnvironment_insert_utf8(XProcessEnvironment* self,
                                     const char* name, const char* value)
{
    (void)self; (void)name; (void)value; return false;
}

bool XProcessEnvironment_insert(XProcessEnvironment* self,
                                const XString* name, const XString* value)
{
    (void)self; (void)name; (void)value; return false;
}

bool XProcessEnvironment_remove_utf8(XProcessEnvironment* self, const char* name)
{
    (void)self; (void)name; return false;
}

bool XProcessEnvironment_remove(XProcessEnvironment* self, const XString* name)
{
    (void)self; (void)name; return false;
}

XString* XProcessEnvironment_value_utf8(const XProcessEnvironment* self,
                                        const char* name, const char* defaultValue)
{
    (void)self; (void)name;
    return XString_create_utf8(defaultValue ? defaultValue : "");
}

XString* XProcessEnvironment_value(const XProcessEnvironment* self,
                                   const XString* name, const XString* defaultValue)
{
    (void)self; (void)name;
    return XString_create_copy(defaultValue);
}

XStringList* XProcessEnvironment_toStringList(const XProcessEnvironment* self)
{
    (void)self; return XStringList_create();
}

XStringList* XProcessEnvironment_keys(const XProcessEnvironment* self)
{
    (void)self; return XStringList_create();
}

bool XProcessEnvironment_insertEnvironment(XProcessEnvironment* self,
                                            const XProcessEnvironment* other)
{
    (void)self; (void)other; return false;
}

XProcessEnvironment* XProcessEnvironment_systemEnvironment(void)
{
    return XProcessEnvironment_create();
}

#endif /* XPROCESS_ENVIRONMENT_ON */
#endif /* XProcess_ON */
