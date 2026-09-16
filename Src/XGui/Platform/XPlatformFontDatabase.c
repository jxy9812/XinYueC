/****************************************************************************
 * @file       XPlatformFontDatabase.c
 * @brief      平台无关字体家族快照实现。
 ****************************************************************************/
#include "XPlatformFontDatabase.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#include "XString.h"
#include "XMemory.h"
#include "XFont.h"
#include "XClass.h"

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
    XMemset(self, 0, sizeof(*self));
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
                                     const XString* family)
{
    size_t i, n;
    if (!self || !self->m_families || !family) return false;
    n = XVector_size_base((const XContainer*)self->m_families);
    for (i = 0; i < n; ++i) {
        XString* const* item = (XString* const*)XVector_at_base(
            self->m_families, (int64_t)i);
        if (item && *item && XString_equals(*item, family, XChar_CaseSensitive))
            return true;
    }
    return false;
}

bool XPlatformFontDatabase_hasFamily_2(const XPlatformFontDatabase* self,
                                       const char* family)
{
    XString* tmp = NULL;
    bool ok;
    if (!family) return false;
    tmp = XString_create_utf8(family);
    if (!tmp) return false;
    ok = XPlatformFontDatabase_hasFamily(self, tmp);
    XString_delete_base(tmp);
    return ok;
}
#endif

/* ==================== Task 2.16：默认字体与标准字号 ==================== */

XFont XPlatformFontDatabase_defaultFont(const XPlatformFontDatabase* self)
{
    XFont out;
    (void)self;
    XFont_init(&out);
    return out;
}

XVector* XPlatformFontDatabase_standardSizes(
        const XPlatformFontDatabase* self)
{
    static const int sizes[] = { 6, 7, 8, 9, 10, 11, 12, 14, 16, 18,
                                 20, 22, 24, 26, 28, 32, 36, 40, 44, 48,
                                 54, 60, 66, 72, 80, 88, 96 };
    XVector* list;
    size_t i;
    (void)self;
    list = XVector_create(sizeof(int));
    if (!list) return NULL;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
        XVector_push_back_1_base(list, &sizes[i]);
    return list;
}
