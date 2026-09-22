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
        XColor highlight   = palette_rgb(48, 140, 198);   /* #308cc6 qt_fusionPalette 高亮（浅/深同值） */
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
            /* Qt 6.8 qt_fusionPalette 将 Accent 镜像为高亮色（"Unless
               explicitly set, it defaults to Highlight"），浅色组同深色组
               一并补齐，保证深浅切换时 Accent 语义一致。 */
            self->m_colors[groups[g]][XPaletteColorRole_Accent]          = highlight;
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

/**
 * @brief      深色标准调色板填充（对标 Qt 6.8 qt_fusionPalette 深色分支）。
 * @details    颜色基准与折算全部锚定 Qt 6.8 qplatformtheme.cpp 的
 *             qt_fusionPalette()：darkAppearance 时 backGround=(50,50,50)、
 *             windowText=text=(240,240,240)、base=backGround.darker(140)、
 *             light=lighter(150)、mid=darker(130)、midlight=mid.lighter(110)、
 *             dark=darker(150)、shadow=dark.darker(135)、highlight=(48,140,198)、
 *             深色 Link=highlight；QColor::lighter/darker 对 HSV V 通道做
 *             整数折算（darker(f)=V*100/f 向下取整、lighter(f)=V*f/100，
 *             灰色通道经 HSV 往返不变），故衍生灰阶与 Qt 计算逐位一致：
 *             base=35、light=75、mid=38、midlight=41、dark=33、shadow=24。
 *             融合层未覆盖的角色沿用 QPalette 构造默认（setColorGroup）：
 *             ToolTipBase=(255,255,220)、BrightText=白、AlternateBase=
 *             qt_mix_colors(base,button)=(base+button)/2=42、LinkVisited=
 *             magenta、Accent=highlight（"defaults to Highlight"）。
 */
void XPalette_init_dark(XPalette* self)
{
    XColor invalid;
    int g, r;
    if (!self)
        return;

    invalid = XColor_create();

    /* 先整体清为无效颜色，再按角色填充，与浅色路径同一套路。 */
    for (g = 0; g < XPaletteColorGroup_NColorGroups; ++g)
        for (r = 0; r < XPaletteColorRole_NColorRoles; ++r)
            self->m_colors[g][r] = invalid;

    /* ---------- 深色 Active / Inactive / Current ---------- */
    {
        XPaletteColorGroup active = XPaletteColorGroup_Active;
        XPaletteColorGroup inactive = XPaletteColorGroup_Inactive;
        XPaletteColorGroup current = XPaletteColorGroup_Current;

        XColor window      = palette_rgb(50, 50, 50);     /* #323232 backGround */
        XColor base        = palette_rgb(35, 35, 35);     /* backGround.darker(140)=#232323 */
        XColor altBase     = palette_rgb(42, 42, 42);     /* qt_mix_colors(base,button) */
        XColor button      = window;                      /* 7 参构造 Button=backGround */
        XColor text        = palette_rgb(240, 240, 240);  /* #f0f0f0 windowText/text */
        XColor light       = palette_rgb(75, 75, 75);     /* backGround.lighter(150) */
        XColor midlight    = palette_rgb(41, 41, 41);     /* mid.lighter(110)，mid=darker(130)=38 */
        XColor dark        = palette_rgb(33, 33, 33);     /* backGround.darker(150) */
        XColor mid         = palette_rgb(38, 38, 38);     /* backGround.darker(130) */
        XColor shadow      = palette_rgb(24, 24, 24);     /* dark.darker(135) */
        XColor brightText  = palette_rgb(255, 255, 255);  /* QPalette 构造默认 */
        XColor highlight   = palette_rgb(48, 140, 198);   /* #308cc6 */
        XColor hiText      = text;                        /* 深色下 highlightedText=windowText */
        XColor link        = highlight;                   /* 深色下 Link=highlight（深底更可读） */
        XColor linkVisited = palette_rgb(255, 0, 255);    /* QPalette 构造默认 magenta */
        XColor toolTipBase = palette_rgb(255, 255, 220);  /* QPalette 构造默认 */
        XColor placeHolder = palette_rgba(240, 240, 240, 128); /* text 50% alpha */

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
            /* Accent 镜像高亮（qt_fusionPalette 三组统一设置）。 */
            self->m_colors[groups[g]][XPaletteColorRole_Accent]          = highlight;
        }
    }

    /* ---------- 深色禁用组 ---------- */
    {
        XPaletteColorGroup disabled = XPaletteColorGroup_Disabled;
        XColor disWindow = palette_rgb(50, 50, 50);       /* Disabled Base=backGround */
        XColor disText   = palette_rgb(130, 130, 130);    /* 深色分支 disabledText */
        XColor disDark   = palette_rgb(190, 190, 190);    /* QColor(209).darker(110) */
        XColor disShadow = palette_rgb(36, 36, 36);       /* shadow.lighter(150)=24*150/100 */
        XColor disHi     = palette_rgb(145, 145, 145);    /* disabledHighlight */

        /* 7 参构造的 Disabled 组沿用 Active 的 light/mid/midlight 值。 */
        for (r = 0; r < XPaletteColorRole_NColorRoles; ++r)
            self->m_colors[disabled][r] = disWindow;
        self->m_colors[disabled][XPaletteColorRole_Window]     = disWindow;
        self->m_colors[disabled][XPaletteColorRole_Button]     = disWindow;
        self->m_colors[disabled][XPaletteColorRole_Base]       = disWindow;
        self->m_colors[disabled][XPaletteColorRole_AlternateBase] =
            palette_rgb(42, 42, 42);
        self->m_colors[disabled][XPaletteColorRole_WindowText] = disText;
        self->m_colors[disabled][XPaletteColorRole_Text]       = disText;
        self->m_colors[disabled][XPaletteColorRole_ButtonText] = disText;
        self->m_colors[disabled][XPaletteColorRole_PlaceholderText] = disText;
        self->m_colors[disabled][XPaletteColorRole_ToolTipText] = disText;
        self->m_colors[disabled][XPaletteColorRole_Dark]       = disDark;
        self->m_colors[disabled][XPaletteColorRole_Shadow]     = disShadow;
        self->m_colors[disabled][XPaletteColorRole_Light]      = palette_rgb(75, 75, 75);
        self->m_colors[disabled][XPaletteColorRole_Mid]        = palette_rgb(38, 38, 38);
        self->m_colors[disabled][XPaletteColorRole_Midlight]   = palette_rgb(41, 41, 41);
        self->m_colors[disabled][XPaletteColorRole_Highlight]  = disHi;
        self->m_colors[disabled][XPaletteColorRole_HighlightedText] =
            palette_rgb(240, 240, 240);
        self->m_colors[disabled][XPaletteColorRole_Accent]     = disHi;
        self->m_colors[disabled][XPaletteColorRole_Link]       = palette_rgb(48, 140, 198);
        self->m_colors[disabled][XPaletteColorRole_LinkVisited] = palette_rgb(255, 0, 255);
        self->m_colors[disabled][XPaletteColorRole_ToolTipBase] = palette_rgb(255, 255, 220);
        self->m_colors[disabled][XPaletteColorRole_NoRole]     = invalid;
    }
}

XPalette XPalette_create_dark(void)
{
    XPalette palette;
    XPalette_init_dark(&palette);
    return palette;
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
