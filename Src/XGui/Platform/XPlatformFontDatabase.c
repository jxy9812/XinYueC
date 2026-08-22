/****************************************************************************
 * @file       XPlatformFontDatabase.c
 * @brief      平台无关字体家族快照实现。
 ****************************************************************************/
#include "XPlatformFontDatabase.h"
#include "XString.h"
#include "XMemory.h"
#include <string.h>

#if XPLATFORMINTEGRATION_ON
struct XPlatformFontDatabase { XVector* m_families; bool m_valid; };

static void font_clear(XPlatformFontDatabase* self)
{
    size_t i, n;
    if (!self || !self->m_families) return;
    n = XVector_size_base((const XContainer*)self->m_families);
    for (i = 0; i < n; ++i) {
        XString** item = (XString**)XVector_at_base(self->m_families, (int64_t)i);
        if (item && *item) XString_delete_base((XClass*)*item);
    }
    XVector_clear_base(self->m_families);
}

XPlatformFontDatabase* XPlatformFontDatabase_create_ex(XMemoryType memory)
{
    XPlatformFontDatabase* self = (XPlatformFontDatabase*)XMemory_malloc(
        sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    self->m_families = XVector_Create(XString*);
    if (!self->m_families) { XFree_System(self); return NULL; }
    self->m_valid = XPlatformFontDatabaseDriver_collect(self->m_families);
    return self;
}

void XPlatformFontDatabase_destroy(XPlatformFontDatabase* self)
{
    if (!self) return;
    font_clear(self);
    if (self->m_families) XVector_delete_base((XClass*)self->m_families);
    XFree_System(self);
}

bool XPlatformFontDatabase_isValid(const XPlatformFontDatabase* self)
{ return self && self->m_valid; }

XVector* XPlatformFontDatabase_families(const XPlatformFontDatabase* self)
{
    XVector* out;
    size_t i, n;
    if (!self || !self->m_families) return NULL;
    out = XVector_Create(XString*);
    if (!out) return NULL;
    n = XVector_size_base((const XContainer*)self->m_families);
    for (i = 0; i < n; ++i) {
        XString* const* item = (XString* const*)XVector_at_base(
            self->m_families, (int64_t)i);
        XString* copy = item && *item ? XString_create_copy(*item) : NULL;
        if (copy) XVector_Push_Back_Base(out, XString*, copy);
    }
    return out;
}

bool XPlatformFontDatabase_hasFamily(const XPlatformFontDatabase* self,
                                     const char* family)
{
    size_t i, n;
    if (!self || !self->m_families || !family) return false;
    n = XVector_size_base((const XContainer*)self->m_families);
    for (i = 0; i < n; ++i) {
        XString* const* item = (XString* const*)XVector_at_base(
            self->m_families, (int64_t)i);
        if (item && *item && strcmp(XString_toUtf8(*item), family) == 0)
            return true;
    }
    return false;
}
#endif
