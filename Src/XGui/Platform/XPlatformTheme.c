/****************************************************************************
 * @file       XPlatformTheme.c
 * @brief      平台无关主题快照实现。
 ****************************************************************************/
#include "XPlatformTheme.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XPLATFORMINTEGRATION_ON
struct XPlatformTheme { char m_name[64]; bool m_dark; };

XPlatformTheme* XPlatformTheme_create_ex(XMemoryType memory, const char* name)
{
    XPlatformTheme* self = (XPlatformTheme*)XMemory_malloc(sizeof(*self), memory);
    char detected[64] = "embedded";
    bool dark = false;
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    (void)XPlatformThemeDriver_detect(&dark, detected, sizeof(detected));
    XStrncpy(self->m_name, name && name[0] ? name : detected,
            sizeof(self->m_name) - 1);
    self->m_dark = dark;
    return self;
}

void XPlatformTheme_destroy(XPlatformTheme* self) { if (self) XFree_System(self); }
const char* XPlatformTheme_name(const XPlatformTheme* self)
{ return self ? self->m_name : NULL; }
bool XPlatformTheme_isDark(const XPlatformTheme* self)
{ return self && self->m_dark; }
#endif
