/****************************************************************************
 * @file       XPlatformTheme.c
 * @brief      平台无关主题快照实现。
 ****************************************************************************/
#include "XPlatformTheme.h"
#include "XStringUtils.h"
#include "XSystem.h"

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
    if (tmp) XClassDelete(tmp);
    return self;
}

void XPlatformTheme_destroy(XPlatformTheme* self)
{
    if (!self) return;
    if (self->m_name) XClassDelete(self->m_name);
    XClassDeinit(&self->m_font);
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
    XClassCopy(&out, &self->m_font);
    return out;
}
void XPlatformTheme_setFont(XPlatformTheme* self, const XFont* font)
{
    if (!self || !font) return;
    XClassDeinit(&self->m_font);
    XFont_init(&self->m_font);
    XClassCopy(&self->m_font, font);
}
int64_t XPlatformTheme_themeHint(const XPlatformTheme* self, int hint)
{
    (void)self; (void)hint;
    return 0;
}
#endif /* XPLATFORMINTEGRATION_ON */

/* ==================== 平台窗口装饰策略（进程级） ====================
 *  不受 XPLATFORMINTEGRATION_ON 门控（与声明侧 XPlatformTheme.h 同步）：
 *  纯进程级策略查询，实现只依赖 XGuiConfig 宏与 XSystem_environment，
 *  装饰判定调用点（XWindowDecoration_activeFor）不受平台集成宏守卫。 */

/** @brief 运行时装饰策略覆盖（进程级；Auto=未覆盖，解析规则见
 *  XPlatformThemeDecoration_effectiveMode）。 */
static XPlatformThemeDecorationMode g_xptDecorationMode =
    XPlatformThemeDecoration_Auto;

/** @brief XGUI_CSD 环境变量解析（读一次缓存，原 XWindowDecoration 的
 *  xwd_forceMode 逻辑迁入）：1 强制框架自绘/0 强制交 WM/其余自动。
 *  返回值约定：-2 未读取（仅初值）、-1 自动（未设置或取值非法）、
 *  1 强制开、0 强制关。 */
static int xpt_envDecorationForce(void)
{
    static int cached = -2; /* -2=未读取，-1=自动。 */
    if (cached == -2) {
        const char* env = XSystem_environment("XGUI_CSD");
        cached = -1;
        if (env && env[0] && !env[1]) {
            if (env[0] == '1') cached = 1;
            else if (env[0] == '0') cached = 0;
        }
    }
    return cached;
}

void XPlatformThemeDecoration_setMode(XPlatformThemeDecorationMode mode)
{
    g_xptDecorationMode = mode;
}

XPlatformThemeDecorationMode XPlatformThemeDecoration_mode(void)
{
    return g_xptDecorationMode;
}

XPlatformThemeDecorationMode XPlatformThemeDecoration_effectiveMode(void)
{
    int force;
    /* 优先级（见契约头）：运行时 setMode > XGUI_CSD > XGUI_CSD_DEFAULT
     * > Auto（回落值，调用方按 fbdev 显示驱动探测裁量）。 */
    if (g_xptDecorationMode != XPlatformThemeDecoration_Auto)
        return g_xptDecorationMode;
    force = xpt_envDecorationForce();
    if (force == 1) return XPlatformThemeDecoration_Framework;
    if (force == 0) return XPlatformThemeDecoration_System;
#if XGUI_CSD_DEFAULT
    return XPlatformThemeDecoration_Framework; /* 编译默认：桌面强制 CSD。 */
#else
    return XPlatformThemeDecoration_Auto;
#endif
}
