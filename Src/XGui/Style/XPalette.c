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

/* ========== 全精度调色板单元桥接（[内存治理 2026-10-11]） ==========
   存储格 16B XColor → 8B XPaletteCell：alpha/R/G/B 各 uint16 原样搬运
   （零量化零损失），非 RGB 色彩空间写入先经 XColor_toRgb 归一（调色板
   语义是「最终显示色」，仅丢弃原色彩空间标签）。XPalette_color/setColor
   与 init 系赋值全部经此两桥，m_colors 勿直访。 */
static XPaletteCell xpal_pack(XColor color)
{
    XPaletteCell cell;
    XColor rgb;
    if (color.m_spec != XColor_Rgb && color.m_spec != XColor_Invalid &&
        color.m_spec != XColor_ExtendedRgb)
    {
        XColor_toRgb(&color, &rgb);
        color = rgb;
    }
    cell.m_alpha = color.m_alpha;
    cell.m_red = color.m_comp1;
    cell.m_green = color.m_comp2;
    cell.m_blue = color.m_comp3;
    return cell;
}

static XColor xpal_unpack(const XPaletteCell* cell)
{
    XColor c;
    if (!cell)
        return XColor_create();
    c.m_spec = XColor_Rgb;
    c.m_alpha = cell->m_alpha;
    c.m_comp1 = cell->m_red;
    c.m_comp2 = cell->m_green;
    c.m_comp3 = cell->m_blue;
    c.m_comp4 = 0;
    return c;
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
            self->m_colors[g][r] = xpal_pack(invalid);

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
            self->m_colors[groups[g]][XPaletteColorRole_WindowText] = xpal_pack(text);
            self->m_colors[groups[g]][XPaletteColorRole_Button] = xpal_pack(button);
            self->m_colors[groups[g]][XPaletteColorRole_Light] = xpal_pack(light);
            self->m_colors[groups[g]][XPaletteColorRole_Midlight] = xpal_pack(midlight);
            self->m_colors[groups[g]][XPaletteColorRole_Dark] = xpal_pack(dark);
            self->m_colors[groups[g]][XPaletteColorRole_Mid] = xpal_pack(mid);
            self->m_colors[groups[g]][XPaletteColorRole_Text] = xpal_pack(text);
            self->m_colors[groups[g]][XPaletteColorRole_BrightText] = xpal_pack(brightText);
            self->m_colors[groups[g]][XPaletteColorRole_ButtonText] = xpal_pack(text);
            self->m_colors[groups[g]][XPaletteColorRole_Base] = xpal_pack(base);
            self->m_colors[groups[g]][XPaletteColorRole_Window] = xpal_pack(window);
            self->m_colors[groups[g]][XPaletteColorRole_Shadow] = xpal_pack(shadow);
            self->m_colors[groups[g]][XPaletteColorRole_Highlight] = xpal_pack(highlight);
            self->m_colors[groups[g]][XPaletteColorRole_HighlightedText] = xpal_pack(hiText);
            self->m_colors[groups[g]][XPaletteColorRole_Link] = xpal_pack(link);
            self->m_colors[groups[g]][XPaletteColorRole_LinkVisited] = xpal_pack(linkVisited);
            self->m_colors[groups[g]][XPaletteColorRole_AlternateBase] = xpal_pack(altBase);
            self->m_colors[groups[g]][XPaletteColorRole_NoRole] = xpal_pack(invalid);
            self->m_colors[groups[g]][XPaletteColorRole_ToolTipBase] = xpal_pack(toolTipBase);
            self->m_colors[groups[g]][XPaletteColorRole_ToolTipText] = xpal_pack(text);
            self->m_colors[groups[g]][XPaletteColorRole_PlaceholderText] = xpal_pack(placeHolder);
            /* Qt 6.8 qt_fusionPalette 将 Accent 镜像为高亮色（"Unless
               explicitly set, it defaults to Highlight"），浅色组同深色组
               一并补齐，保证深浅切换时 Accent 语义一致。 */
            self->m_colors[groups[g]][XPaletteColorRole_Accent] = xpal_pack(highlight);
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
            self->m_colors[disabled][r] = xpal_pack(disWindow);
        self->m_colors[disabled][XPaletteColorRole_Window] = xpal_pack(disWindow);
        self->m_colors[disabled][XPaletteColorRole_Button] = xpal_pack(disWindow);
        self->m_colors[disabled][XPaletteColorRole_Base] = xpal_pack(disBase);
        self->m_colors[disabled][XPaletteColorRole_AlternateBase] = xpal_pack(disBase);
        self->m_colors[disabled][XPaletteColorRole_WindowText] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_Text] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_ButtonText] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_PlaceholderText] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_ToolTipText] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_Dark] = xpal_pack(disDark);
        self->m_colors[disabled][XPaletteColorRole_Mid] = xpal_pack(disMid);
        self->m_colors[disabled][XPaletteColorRole_Midlight] = xpal_pack(disMidlight);
        self->m_colors[disabled][XPaletteColorRole_Light] = xpal_pack(disLight);
        self->m_colors[disabled][XPaletteColorRole_Shadow] = xpal_pack(disShadow);
        self->m_colors[disabled][XPaletteColorRole_Highlight] = xpal_pack(palette_rgb(145, 145, 145));
        self->m_colors[disabled][XPaletteColorRole_HighlightedText] = xpal_pack(disHiText);
        self->m_colors[disabled][XPaletteColorRole_Link] = xpal_pack(disLink);
        self->m_colors[disabled][XPaletteColorRole_LinkVisited] = xpal_pack(disVisited);
        self->m_colors[disabled][XPaletteColorRole_ToolTipBase] = xpal_pack(disToolTipBase);
        self->m_colors[disabled][XPaletteColorRole_ToolTipText] = xpal_pack(disToolTipText);
        self->m_colors[disabled][XPaletteColorRole_PlaceholderText] = xpal_pack(palette_rgba(190, 190, 190, 128));
        self->m_colors[disabled][XPaletteColorRole_NoRole] = xpal_pack(invalid);
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
            self->m_colors[g][r] = xpal_pack(invalid);

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
            self->m_colors[groups[g]][XPaletteColorRole_WindowText] = xpal_pack(text);
            self->m_colors[groups[g]][XPaletteColorRole_Button] = xpal_pack(button);
            self->m_colors[groups[g]][XPaletteColorRole_Light] = xpal_pack(light);
            self->m_colors[groups[g]][XPaletteColorRole_Midlight] = xpal_pack(midlight);
            self->m_colors[groups[g]][XPaletteColorRole_Dark] = xpal_pack(dark);
            self->m_colors[groups[g]][XPaletteColorRole_Mid] = xpal_pack(mid);
            self->m_colors[groups[g]][XPaletteColorRole_Text] = xpal_pack(text);
            self->m_colors[groups[g]][XPaletteColorRole_BrightText] = xpal_pack(brightText);
            self->m_colors[groups[g]][XPaletteColorRole_ButtonText] = xpal_pack(text);
            self->m_colors[groups[g]][XPaletteColorRole_Base] = xpal_pack(base);
            self->m_colors[groups[g]][XPaletteColorRole_Window] = xpal_pack(window);
            self->m_colors[groups[g]][XPaletteColorRole_Shadow] = xpal_pack(shadow);
            self->m_colors[groups[g]][XPaletteColorRole_Highlight] = xpal_pack(highlight);
            self->m_colors[groups[g]][XPaletteColorRole_HighlightedText] = xpal_pack(hiText);
            self->m_colors[groups[g]][XPaletteColorRole_Link] = xpal_pack(link);
            self->m_colors[groups[g]][XPaletteColorRole_LinkVisited] = xpal_pack(linkVisited);
            self->m_colors[groups[g]][XPaletteColorRole_AlternateBase] = xpal_pack(altBase);
            self->m_colors[groups[g]][XPaletteColorRole_NoRole] = xpal_pack(invalid);
            self->m_colors[groups[g]][XPaletteColorRole_ToolTipBase] = xpal_pack(toolTipBase);
            self->m_colors[groups[g]][XPaletteColorRole_ToolTipText] = xpal_pack(text);
            self->m_colors[groups[g]][XPaletteColorRole_PlaceholderText] = xpal_pack(placeHolder);
            /* Accent 镜像高亮（qt_fusionPalette 三组统一设置）。 */
            self->m_colors[groups[g]][XPaletteColorRole_Accent] = xpal_pack(highlight);
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
            self->m_colors[disabled][r] = xpal_pack(disWindow);
        self->m_colors[disabled][XPaletteColorRole_Window] = xpal_pack(disWindow);
        self->m_colors[disabled][XPaletteColorRole_Button] = xpal_pack(disWindow);
        self->m_colors[disabled][XPaletteColorRole_Base] = xpal_pack(disWindow);
        self->m_colors[disabled][XPaletteColorRole_AlternateBase] = xpal_pack(palette_rgb(42, 42, 42));
        self->m_colors[disabled][XPaletteColorRole_WindowText] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_Text] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_ButtonText] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_PlaceholderText] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_ToolTipText] = xpal_pack(disText);
        self->m_colors[disabled][XPaletteColorRole_Dark] = xpal_pack(disDark);
        self->m_colors[disabled][XPaletteColorRole_Shadow] = xpal_pack(disShadow);
        self->m_colors[disabled][XPaletteColorRole_Light] = xpal_pack(palette_rgb(75, 75, 75));
        self->m_colors[disabled][XPaletteColorRole_Mid] = xpal_pack(palette_rgb(38, 38, 38));
        self->m_colors[disabled][XPaletteColorRole_Midlight] = xpal_pack(palette_rgb(41, 41, 41));
        self->m_colors[disabled][XPaletteColorRole_Highlight] = xpal_pack(disHi);
        self->m_colors[disabled][XPaletteColorRole_HighlightedText] = xpal_pack(palette_rgb(240, 240, 240));
        self->m_colors[disabled][XPaletteColorRole_Accent] = xpal_pack(disHi);
        self->m_colors[disabled][XPaletteColorRole_Link] = xpal_pack(palette_rgb(48, 140, 198));
        self->m_colors[disabled][XPaletteColorRole_LinkVisited] = xpal_pack(palette_rgb(255, 0, 255));
        self->m_colors[disabled][XPaletteColorRole_ToolTipBase] = xpal_pack(palette_rgb(255, 255, 220));
        self->m_colors[disabled][XPaletteColorRole_NoRole] = xpal_pack(invalid);
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
    return xpal_unpack(&self->m_colors[g][role]);
}

void XPalette_setColor(XPalette* self, XPaletteColorGroup group, XPaletteColorRole role, XColor color)
{
    XPaletteColorGroup g;
    if (!self || role < 0 || role >= XPaletteColorRole_NColorRoles ||
        role == XPaletteColorRole_NoRole)
        return;
    g = palette_normalize_group(group);
    self->m_colors[g][role] = xpal_pack(color);
}

bool XPalette_isEqual(const XPalette* a, const XPalette* b)
{
    int g, r;
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    for (g = 0; g < XPaletteColorGroup_NColorGroups; ++g)
    {
        for (r = 0; r < XPaletteColorRole_NColorRoles; ++r)
        {
            XColor ca = xpal_unpack(&a->m_colors[g][r]);
            XColor cb = xpal_unpack(&b->m_colors[g][r]);
            if (!XColor_equals(&ca, &cb))
                return false;
        }
    }
    return true;
}

#endif /* XPALETTE_ON */
