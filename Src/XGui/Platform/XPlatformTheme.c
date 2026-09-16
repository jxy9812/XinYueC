/****************************************************************************
 * @file       XPlatformTheme.c
 * @brief      平台无关主题快照实现。
 ****************************************************************************/
#include "XPlatformTheme.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"

#if XPLATFORMINTEGRATION_ON
struct XPlatformTheme {
    XString* m_name;      /**< 主题名（拥有）。 */
    bool m_dark;          /**< 深色偏好。 */
    int m_colorScheme;    /**< 色彩方案（XPlatformThemeColorScheme）。 */
    XFont m_font;         /**< 平台默认字体快照（值）。 */
};

XPlatformTheme* XPlatformTheme_create_ex(XMemoryType memory, const XString* name)
{
    XPlatformTheme* self = (XPlatformTheme*)XMemory_malloc(sizeof(*self), memory);
    char detected[64] = "embedded";
    bool dark = false;
    const char* effective = NULL;
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    (void)XPlatformThemeDriver_detect(&dark, detected, sizeof(detected));
    if (name && XString_length_base((XContainer*)name) > 0)
        effective = XString_toUtf8(name);
    self->m_name = XString_create_utf8(effective && effective[0]
                                           ? effective : detected);
    self->m_dark = dark;
    self->m_colorScheme = dark ? XPlatformThemeColorScheme_Dark
                               : XPlatformThemeColorScheme_Light;
    XFont_init(&self->m_font);
    return self;
}

XPlatformTheme* XPlatformTheme_create_ex_2(XMemoryType memory, const char* name)
{
    XPlatformTheme* self;
    XString* tmp = NULL;
    if (name && name[0]) {
        tmp = XString_create_utf8(name);
        if (!tmp) return NULL;
    }
    self = XPlatformTheme_create_ex(memory, tmp);
    if (tmp) XString_delete_base(tmp);
    return self;
}

void XPlatformTheme_destroy(XPlatformTheme* self)
{
    if (!self) return;
    if (self->m_name) XString_delete_base(self->m_name);
    XFont_deinit_base(&self->m_font);
    XFree_System(self);
}
const XString* XPlatformTheme_name(const XPlatformTheme* self)
{ return self ? self->m_name : NULL; }
const char* XPlatformTheme_name_2(const XPlatformTheme* self)
{ return self && self->m_name ? XString_toUtf8(self->m_name) : NULL; }
bool XPlatformTheme_isDark(const XPlatformTheme* self)
{ return self && self->m_dark; }
int XPlatformTheme_colorScheme(const XPlatformTheme* self)
{ return self ? self->m_colorScheme : XPlatformThemeColorScheme_Unknown; }
void XPlatformTheme_setColorScheme(XPlatformTheme* self, int scheme)
{ if (self) self->m_colorScheme = scheme; }
XFont XPlatformTheme_font(const XPlatformTheme* self)
{
    XFont out;
    XFont_init(&out);
    if (!self) return out;
    XCopy(&out, &self->m_font);
    return out;
}
void XPlatformTheme_setFont(XPlatformTheme* self, const XFont* font)
{
    if (!self || !font) return;
    XFont_deinit_base(&self->m_font);
    XFont_init(&self->m_font);
    XCopy(&self->m_font, font);
}
int64_t XPlatformTheme_themeHint(const XPlatformTheme* self, int hint)
{
    (void)self; (void)hint;
    return 0;
}
#endif
