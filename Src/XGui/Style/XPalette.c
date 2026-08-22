/******************************************************************************
 * @file       XPalette.c
 * @brief      XPalette 调色板值类型实现（对标 Qt 6.8 QPalette）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPalette.h"

#if XPALETTE_ON

/** @brief 构造不透明 RGB 颜色的小助手（Alpha 恒为 255）。 */
static XColor palette_rgb(int r, int g, int b)
{
    XColor color;
    XColor_init_rgb(&color, r, g, b, 255);
    return color;
}

/** @brief 构造带透明度的 RGB 颜色（用于 Qt PlaceholderText）。 */
static XColor palette_rgba(int r, int g, int b, int a)
{
    XColor color;
    XColor_init_rgb(&color, r, g, b, a);
    return color;
}

/** @brief 组归一化：Current 映射到 Active，越界归到 Active（读取时）。 */
static XPaletteColorGroup palette_normalize_group(XPaletteColorGroup group)
{
    if (group == XPaletteColorGroup_Current)
        return XPaletteColorGroup_Active;
    if (group < 0 || group >= XPaletteColorGroup_NColorGroups)
        return XPaletteColorGroup_Active;
    return group;
}

void XPalette_init_default(XPalette* self)
{
    XColor invalid;
    int g, r;
    if (!self)
        return;

    invalid = XColor_create();

    /* 先整体清为无效颜色，再按角色填充，保证越界单元可辨识。 */
    for (g = 0; g < XPaletteColorGroup_NColorGroups; ++g)
        for (r = 0; r < XPaletteColorRole_NColorRoles; ++r)
            self->m_colors[g][r] = invalid;

    /* ---------- Qt 6.8 Fusion 浅色主题（Active / Inactive / Current） ---------- */
    {
        XPaletteColorGroup active = XPaletteColorGroup_Active;
        XPaletteColorGroup inactive = XPaletteColorGroup_Inactive;
        XPaletteColorGroup current = XPaletteColorGroup_Current;

        /* 基础色板：与 Qt Fusion 的 window/base/button 一致。 */
        XColor window      = palette_rgb(239, 239, 239);   /* #efefef */
        XColor base        = palette_rgb(255, 255, 255);   /* #ffffff */
        XColor altBase     = palette_rgb(247, 247, 247);   /* qt_mix_colors(base,button) */
        XColor button      = palette_rgb(239, 239, 239);   /* #efefef */
        XColor text        = palette_rgb(0, 0, 0);         /* #000000 */
        XColor light       = palette_rgb(255, 255, 255);   /* #ffffff */
        XColor midlight    = palette_rgb(247, 247, 247);   /* qt_mix_colors(button,light) */
        XColor dark        = palette_rgb(159, 159, 159);   /* background.darker(150) */
        XColor mid         = palette_rgb(184, 184, 184);   /* background.darker(130) */
        XColor shadow      = palette_rgb(118, 118, 118);   /* dark.darker(135) */
        XColor brightText  = palette_rgb(255, 255, 255);   /* #ffffff */
        XColor highlight   = palette_rgb(61, 142, 201);    /* #3d8ec9 Fusion 高亮 */
        XColor hiText      = palette_rgb(255, 255, 255);
        XColor link        = palette_rgb(0, 0, 255);       /* #0000ff */
        XColor linkVisited = palette_rgb(255, 0, 255);     /* Qt::magenta */
        XColor toolTipBase = palette_rgb(255, 255, 220);   /* 浅黄工具提示 */
        XColor placeHolder = palette_rgba(0, 0, 0, 128);   /* text with 50% alpha */

        XPaletteColorGroup groups[3] = { active, inactive, current };
        for (g = 0; g < 3; ++g) {
            self->m_colors[groups[g]][XPaletteColorRole_WindowText]      = text;
            self->m_colors[groups[g]][XPaletteColorRole_Button]          = button;
            self->m_colors[groups[g]][XPaletteColorRole_Light]           = light;
            self->m_colors[groups[g]][XPaletteColorRole_Midlight]        = midlight;
            self->m_colors[groups[g]][XPaletteColorRole_Dark]            = dark;
            self->m_colors[groups[g]][XPaletteColorRole_Mid]             = mid;
            self->m_colors[groups[g]][XPaletteColorRole_Text]            = text;
            self->m_colors[groups[g]][XPaletteColorRole_BrightText]      = brightText;
            self->m_colors[groups[g]][XPaletteColorRole_ButtonText]      = text;
            self->m_colors[groups[g]][XPaletteColorRole_Base]            = base;
            self->m_colors[groups[g]][XPaletteColorRole_Window]          = window;
            self->m_colors[groups[g]][XPaletteColorRole_Shadow]          = shadow;
            self->m_colors[groups[g]][XPaletteColorRole_Highlight]       = highlight;
            self->m_colors[groups[g]][XPaletteColorRole_HighlightedText] = hiText;
            self->m_colors[groups[g]][XPaletteColorRole_Link]            = link;
            self->m_colors[groups[g]][XPaletteColorRole_LinkVisited]     = linkVisited;
            self->m_colors[groups[g]][XPaletteColorRole_AlternateBase]   = altBase;
            self->m_colors[groups[g]][XPaletteColorRole_NoRole]          = invalid;
            self->m_colors[groups[g]][XPaletteColorRole_ToolTipBase]     = toolTipBase;
            self->m_colors[groups[g]][XPaletteColorRole_ToolTipText]     = text;
            self->m_colors[groups[g]][XPaletteColorRole_PlaceholderText] = placeHolder;
        }
    }

    /* ---------- 禁用组：文本/前景类角色降为暗灰，底色略灰 ---------- */
    {
        XPaletteColorGroup disabled = XPaletteColorGroup_Disabled;
        XColor disWindow = palette_rgb(239, 239, 239);
        XColor disBase   = palette_rgb(240, 240, 240);
        XColor disText   = palette_rgb(190, 190, 190);   /* Qt Fusion disabledText */
        XColor disDark   = palette_rgb(190, 190, 190);   /* QColor(209).darker(110) */
        XColor disShadow = palette_rgb(177, 177, 177);   /* shadow.lighter(150) */
        XColor disLight  = palette_rgb(255, 255, 255);
        XColor disMidlight = palette_rgb(247, 247, 247);
        XColor disMid    = palette_rgb(184, 184, 184);
        XColor disHiText = palette_rgb(255, 255, 255);
        XColor disLink   = palette_rgb(0, 0, 255);
        XColor disVisited = palette_rgb(255, 0, 255);
        XColor disToolTipBase = palette_rgb(255, 255, 220);
        XColor disToolTipText = palette_rgb(0, 0, 0);

        for (r = 0; r < XPaletteColorRole_NColorRoles; ++r)
            self->m_colors[disabled][r] = disWindow;
        self->m_colors[disabled][XPaletteColorRole_Window]     = disWindow;
        self->m_colors[disabled][XPaletteColorRole_Button]     = disWindow;
        self->m_colors[disabled][XPaletteColorRole_Base]       = disBase;
        self->m_colors[disabled][XPaletteColorRole_AlternateBase] = disBase;
        self->m_colors[disabled][XPaletteColorRole_WindowText] = disText;
        self->m_colors[disabled][XPaletteColorRole_Text]       = disText;
        self->m_colors[disabled][XPaletteColorRole_ButtonText] = disText;
        self->m_colors[disabled][XPaletteColorRole_PlaceholderText] = disText;
        self->m_colors[disabled][XPaletteColorRole_ToolTipText] = disText;
        self->m_colors[disabled][XPaletteColorRole_Dark]       = disDark;
        self->m_colors[disabled][XPaletteColorRole_Mid]        = disMid;
        self->m_colors[disabled][XPaletteColorRole_Midlight]   = disMidlight;
        self->m_colors[disabled][XPaletteColorRole_Light]      = disLight;
        self->m_colors[disabled][XPaletteColorRole_Shadow]     = disShadow;
        self->m_colors[disabled][XPaletteColorRole_Highlight]  = palette_rgb(145, 145, 145);
        self->m_colors[disabled][XPaletteColorRole_HighlightedText] = disHiText;
        self->m_colors[disabled][XPaletteColorRole_Link]       = disLink;
        self->m_colors[disabled][XPaletteColorRole_LinkVisited] = disVisited;
        self->m_colors[disabled][XPaletteColorRole_ToolTipBase] = disToolTipBase;
        self->m_colors[disabled][XPaletteColorRole_ToolTipText] = disToolTipText;
        self->m_colors[disabled][XPaletteColorRole_PlaceholderText] =
            palette_rgba(190, 190, 190, 128);
        self->m_colors[disabled][XPaletteColorRole_NoRole]     = invalid;
    }
}

XPalette XPalette_create(void)
{
    XPalette palette;
    XPalette_init_default(&palette);
    return palette;
}

void XPalette_copy(XPalette* dest, const XPalette* src)
{
    if (!dest || !src || dest == src)
        return;
    *dest = *src;
}

XColor XPalette_color(const XPalette* self, XPaletteColorGroup group, XPaletteColorRole role)
{
    XPaletteColorGroup g;
    XColor invalid = XColor_create();
    if (!self || role < 0 || role >= XPaletteColorRole_NColorRoles)
        return invalid;
    g = palette_normalize_group(group);
    return self->m_colors[g][role];
}

void XPalette_setColor(XPalette* self, XPaletteColorGroup group, XPaletteColorRole role, XColor color)
{
    XPaletteColorGroup g;
    if (!self || role < 0 || role >= XPaletteColorRole_NColorRoles ||
        role == XPaletteColorRole_NoRole)
        return;
    g = palette_normalize_group(group);
    self->m_colors[g][role] = color;
}

bool XPalette_isEqual(const XPalette* a, const XPalette* b)
{
    int g, r;
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    for (g = 0; g < XPaletteColorGroup_NColorGroups; ++g)
        for (r = 0; r < XPaletteColorRole_NColorRoles; ++r)
            if (!XColor_equals(&a->m_colors[g][r], &b->m_colors[g][r]))
                return false;
    return true;
}

#endif /* XPALETTE_ON */
