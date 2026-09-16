#include "XFusionStyle.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include "XPalette.h"
#include "XColor.h"

#if XSTYLE_ON

/** @brief 颜色线性插值。 */
static uint32_t xfs_lerp(uint32_t a, uint32_t b, double t)
{
    uint32_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    uint32_t aa = (a >> 24) & 0xFF;
    uint32_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    uint32_t ba = (b >> 24) & 0xFF;
    uint32_t r = (uint32_t)(ar + (br - ar) * t);
    uint32_t g = (uint32_t)(ag + (bg - ag) * t);
    uint32_t bl = (uint32_t)(ab + (bb - ab) * t);
    uint32_t al = (uint32_t)(aa + (ba - aa) * t);
    return (al << 24) | (r << 16) | (g << 8) | bl;
}

/* Fusion 主题色（对标 qfusionstyle.cpp FusionStyle 默认调色板）。 */
#define XFS_HIGHLIGHT_DEFAULT 0xFF2A82DAu
#define XFS_BUTTON_BASE       0xFFEFEFEFu
#define XFS_BUTTON_GRAD_TOP   0xFFFFFFFFu
#define XFS_BUTTON_GRAD_BOT   0xFFDCDCDCu
#define XFS_BORDER_DARK       0xFF9E9E9Eu

/** @brief 调色板取色（带 Fusion 默认回退）。 */
static uint32_t xfs_color(const XStyleOption* option,
                          XPaletteColorRole role)
{
#if XPALETTE_ON
    XColor c = XPalette_color((XPalette*)&option->m_palette,
                              XPaletteColorGroup_Current, role);
    uint32_t v = XColor_rgba(&c);
    if (v != 0) return v;
#else
    (void)option; (void)role;
#endif
    switch (role) {
    case XPaletteColorRole_Button: return XFS_BUTTON_BASE;
    case XPaletteColorRole_Light: return XFS_BUTTON_GRAD_TOP;
    case XPaletteColorRole_Midlight: return XFS_BUTTON_GRAD_TOP;
    case XPaletteColorRole_Dark: return XFS_BORDER_DARK;
    case XPaletteColorRole_Mid: return XFS_BORDER_DARK;
    case XPaletteColorRole_Highlight: return XFS_HIGHLIGHT_DEFAULT;
    case XPaletteColorRole_ButtonText: return 0xFF202020u;
    case XPaletteColorRole_WindowText: return 0xFF202020u;
    case XPaletteColorRole_Text: return 0xFF202020u;
    default: return 0xFF000000u;
    }
}

/**
 * @brief Fusion 圆角命令按钮面板（对标 PE_PanelButtonCommand）。
 *
 *        顶部亮渐变 + 底部暗渐变，圆角 2px，悬停高亮边，按下凹陷。
 */
static void xfs_drawPanelButtonCommand(XFusionStyle* self,
                                       const XStyleOption* option,
                                       XPainter* painter)
{
    uint32_t base;
    uint32_t gradTop;
    uint32_t gradBot;
    uint32_t border;
    uint32_t hl;
    XRect r;
    bool sunken;
    bool hover;
    bool disabled;
    bool focused;
    int i;
    int gradH;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    if (r.width < 2 || r.height < 2) return;
    base = xfs_color(option, XPaletteColorRole_Button);
    gradTop = xfs_color(option, XPaletteColorRole_Light);
    gradBot = xfs_color(option, XPaletteColorRole_Midlight);
    border = xfs_color(option, XPaletteColorRole_Dark);
    hl = xfs_color(option, XPaletteColorRole_Highlight);
    sunken = (option->m_state & XStyleState_Sunken) != 0;
    hover = (option->m_state & XStyleState_MouseOver) != 0;
    disabled = (option->m_state & XStyleState_Enabled) == 0;
    focused = (option->m_state & XStyleState_HasFocus) != 0;
    if (disabled) {
        XPainter_fillRect(painter, &r, 0xFFE8E8E8u);
        return;
    }
    /* 渐变填充：逐行插值 gradTop→gradBot。 */
    gradH = r.height > 1 ? r.height : 1;
    if (sunken) {
        /* 按下：整体下移渐变方向并加深。 */
        for (i = 0; i < gradH; ++i) {
            double t = (double)i / (double)(gradH - 1);
            uint32_t c = xfs_lerp(gradBot, gradTop, t);
            XPainter_fillRect(painter,
                &(XRect){r.x, r.y + i, r.width, 1}, c);
        }
    } else {
        for (i = 0; i < gradH; ++i) {
            double t = (double)i / (double)(gradH - 1);
            uint32_t c = xfs_lerp(gradTop, gradBot, t);
            XPainter_fillRect(painter,
                &(XRect){r.x, r.y + i, r.width, 1}, c);
        }
    }
    (void)base;
    /* 圆角边框（XPAINTER_SHAPE_ON 真实圆角 2px；裁剪时矩形近似）。 */
#if XPAINTER_SHAPE_ON
    {
        XRect rr = r;
        XPainter_drawRoundedRect(painter, &rr, 2, 2);
    }
#else
    XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, border);
    XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                        r.width, 1}, border);
    XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, border);
    XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                        1, r.height}, border);
#endif
    /* 悬停：高亮顶边。 */
    if (hover && !sunken) {
        XPainter_fillRect(painter, &(XRect){r.x + 1, r.y + 1,
                                            r.width - 2, 1}, hl);
    }
    /* 焦点：内侧高亮框。 */
    if (focused) {
        XPainter_fillRect(painter,
            &(XRect){r.x + 1, r.y + 1, r.width - 2, 1}, hl);
        XPainter_fillRect(painter,
            &(XRect){r.x + 1, r.y + r.height - 2, r.width - 2, 1}, hl);
        XPainter_fillRect(painter,
            &(XRect){r.x + 1, r.y + 1, 1, r.height - 2}, hl);
        XPainter_fillRect(painter,
            &(XRect){r.x + r.width - 2, r.y + 1, 1, r.height - 2}, hl);
    }
}

/** @brief Fusion 复选指示器（PE_IndicatorCheckBox）。 */
static void xfs_drawIndicatorCheckBox(XFusionStyle* self,
                                      const XStyleOption* option,
                                      XPainter* painter)
{
    uint32_t hl;
    uint32_t fg;
    uint32_t base;
    XRect r;
    bool on;
    bool noChange;
    bool hover;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    if (r.width < 4 || r.height < 4) return;
    hl = xfs_color(option, XPaletteColorRole_Highlight);
    fg = xfs_color(option, XPaletteColorRole_Text);
    base = xfs_color(option, XPaletteColorRole_Base);
    on = (option->m_state & XStyleState_On) != 0;
    noChange = (option->m_state & XStyleState_NoChange) != 0;
    hover = (option->m_state & XStyleState_MouseOver) != 0;
    XPainter_fillRect(painter, &r, base);
    /* 边框：悬停/选中用高亮。 */
    {
        uint32_t bc = (hover || on) ? hl : fg;
        XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, bc);
        XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, bc);
        XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                            r.width, 1}, bc);
        XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                            1, r.height}, bc);
    }
    if (on) {
        /* Fusion 对勾：高亮色 drawLine 折线（完整复刻）。 */
        XPainter_drawLine(painter, r.x + 3, r.y + 7,
                          r.x + 6, r.y + 10);
        XPainter_drawLine(painter, r.x + 6, r.y + 10,
                          r.x + 10, r.y + 3);
    } else if (noChange) {
        XPainter_drawLine(painter, r.x + 3, r.y + r.height / 2,
                          r.x + r.width - 3, r.y + r.height / 2);
    }
}

/** @brief Fusion 单选指示器（PE_IndicatorRadioButton）。 */
static void xfs_drawIndicatorRadioButton(XFusionStyle* self,
                                         const XStyleOption* option,
                                         XPainter* painter)
{
    uint32_t hl;
    uint32_t fg;
    uint32_t base;
    XRect r;
    bool on;
    bool hover;
    int cx;
    int cy;
    int rad;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    if (r.width < 4 || r.height < 4) return;
    hl = xfs_color(option, XPaletteColorRole_Highlight);
    fg = xfs_color(option, XPaletteColorRole_Text);
    base = xfs_color(option, XPaletteColorRole_Base);
    on = (option->m_state & XStyleState_On) != 0;
    hover = (option->m_state & XStyleState_MouseOver) != 0;
    cx = r.x + r.width / 2;
    cy = r.y + r.height / 2;
    rad = (r.width < r.height ? r.width : r.height) / 2;
    XPainter_fillRect(painter, &r, base);
    {
        uint32_t bc = (hover || on) ? hl : fg;
#if XPAINTER_SHAPE_ON
        XRect circle;
        XRect_init(&circle, cx - rad, cy - rad, rad * 2, rad * 2);
        XPainter_drawEllipse(painter, &circle);
#else
        int i;
        for (i = 0; i < rad; ++i) {
            int hw = (int)(0.8 * (rad - i));
            if (hw < 1) hw = 1;
            XPainter_fillRect(painter,
                &(XRect){cx - hw, cy - rad + i, hw * 2 + 1, 1}, bc);
        }
#endif
    }
    if (on) {
        int rr = rad / 3;
#if XPAINTER_SHAPE_ON
        XRect dot;
        XRect_init(&dot, cx - rr, cy - rr, rr * 2, rr * 2);
        XPainter_drawEllipse(painter, &dot);
        XPainter_fillRect(painter, &dot, hl);
#else
        int i;
        for (i = 0; i < rr * 2 + 1; ++i) {
            int hw = (int)(0.8 * (rr - i));
            if (hw < 1) hw = 1;
            XPainter_fillRect(painter,
                &(XRect){cx - hw, cy - rr + i, hw * 2 + 1, 1}, hl);
        }
#endif
    }
}

static void VXFusionStyle_drawPrimitive(XStyle* self, int pe,
                                        const XStyleOption* option,
                                        XPainter* painter,
                                        const XWidget* widget)
{
    XFusionStyle* fs = (XFusionStyle*)self;
    switch (pe) {
    case XStylePE_PanelButtonCommand:
    case XStylePE_PanelButtonBevel:
    case XStylePE_PanelButtonTool:
        xfs_drawPanelButtonCommand(fs, option, painter);
        break;
    case XStylePE_IndicatorCheckBox:
        xfs_drawIndicatorCheckBox(fs, option, painter);
        break;
    case XStylePE_IndicatorRadioButton:
        xfs_drawIndicatorRadioButton(fs, option, painter);
        break;
    default:
        /* 其余走公共实现。 */
        XClass_Parent(XCommonStyle, EXStyle_DrawPrimitive,
                      void(*)(XStyle*, int, const XStyleOption*,
                              XPainter*, const XWidget*))(
            (XStyle*)self, pe, option, painter, widget);
        break;
    }
}

static void VXFusionStyle_drawControl(XStyle* self, int ce,
                                      const XStyleOption* option,
                                      XPainter* painter,
                                      const XWidget* widget)
{
    /* 控件整体走公共实现（其内部基元分派走 Fusion 覆盖）。 */
    XClass_Parent(XCommonStyle, EXStyle_DrawControl,
                  void(*)(XStyle*, int, const XStyleOption*,
                          XPainter*, const XWidget*))(
        (XStyle*)self, ce, option, painter, widget);
}

static void VXFusionStyle_drawComplexControl(XStyle* self, int cc,
                                             const XStyleOption* option,
                                             XPainter* painter,
                                             const XWidget* widget)
{
    /* 复杂控件几何/视觉（Slider/ScrollBar/SpinBox/ComboBox/GroupBox/
     * Dial/ToolButton）由 XCommonStyle 的 Fusion 风味实现承担
     * （xcs_drawScrollBar 等完整对标 QFusionStyle 非 transient 路径），
     * Fusion 层保持与 Qt 相同的覆盖点并回落父类。 */
    XClass_Parent(XCommonStyle, EXStyle_DrawComplexControl,
                  void(*)(XStyle*, int, const XStyleOption*,
                          XPainter*, const XWidget*))(
        (XStyle*)self, cc, option, painter, widget);
}

/* ==================== Fusion 度量（对标 QFusionStyle::pixelMetric） ==================== */

static int VXFusionStyle_pixelMetric(XStyle* self, int metric,
                                     const XStyleOption* option)
{
    switch (metric) {
    case XStylePM_SliderTickmarkOffset: return 4;
    case XStylePM_HeaderMargin: return 2;
    case XStylePM_ToolTipLabelFrameWidth: return 2;
    case XStylePM_ButtonDefaultIndicator: return 0;
    case XStylePM_ButtonShiftHorizontal: return 0;
    case XStylePM_ButtonShiftVertical: return 0;
    case XStylePM_MessageBoxIconSize: return 48;
    case XStylePM_ListViewIconSize: return 24;
    case XStylePM_ScrollBarSliderMin: return 26;
    case XStylePM_TitleBarHeight: return 24;
    case XStylePM_ScrollBarExtent: return 14;
    case XStylePM_SliderThickness: return 15;
    case XStylePM_SliderLength: return 15;
    case XStylePM_DockWidgetTitleMargin: return 1;
    case XStylePM_SpinBoxFrameWidth: return 3;
    case XStylePM_MenuVMargin: return 0;
    case XStylePM_MenuHMargin: return 0;
    case XStylePM_MenuPanelWidth: return 0;
    case XStylePM_MenuBarItemSpacing: return 6;
    case XStylePM_MenuBarVMargin: return 0;
    case XStylePM_MenuBarHMargin: return 0;
    case XStylePM_MenuBarPanelWidth: return 0;
    case XStylePM_ToolBarHandleExtent: return 9;
    case XStylePM_ToolBarItemSpacing: return 1;
    case XStylePM_ToolBarFrameWidth: return 2;
    case XStylePM_ToolBarItemMargin: return 2;
    case XStylePM_SmallIconSize: return 16;
    case XStylePM_ButtonIconSize: return 16;
    case XStylePM_DockWidgetTitleBarButtonMargin: return 2;
    case XStylePM_TitleBarButtonSize: return 19;
    case XStylePM_MaximumDragDistance: return -1;
    case XStylePM_TabCloseIndicatorWidth: return 20;
    case XStylePM_TabCloseIndicatorHeight: return 20;
    case XStylePM_TabBarTabVSpace: return 12;
    case XStylePM_TabBarTabOverlap: return 1;
    case XStylePM_TabBarBaseOverlap: return 2;
    case XStylePM_SubMenuOverlap: return -1;
    case XStylePM_DockWidgetHandleExtent: return 4;
    case XStylePM_SplitterWidth: return 4;
    case XStylePM_IndicatorHeight: return 14;
    case XStylePM_IndicatorWidth: return 14;
    case XStylePM_ExclusiveIndicatorHeight: return 14;
    case XStylePM_ExclusiveIndicatorWidth: return 14;
    case XStylePM_ScrollView_ScrollBarSpacing: return 0;
    case XStylePM_ScrollView_ScrollBarOverlap: return 0;
    case XStylePM_DefaultFrameWidth: return 1;
    default:
        return XClass_Parent(XCommonStyle, EXStyle_PixelMetric,
                             int(*)(XStyle*, int, const XStyleOption*))(
            (XStyle*)self, metric, option);
    }
}

/* ==================== Fusion 标准调色板（对标 qt_fusionPalette；
 *    数值按 Task 2.12 计划常量：Light=#F7F7F7、Midlight=#BFBFBF、
 *    Highlight=#308CC6、Disabled Base=#EFEFEF、Disabled Shadow=#BABABA、
 *    Accent=Highlight）。 ==================== */

#define XFS_PAL_WINDOWTEXT  0xFF000000u
#define XFS_PAL_BACKGROUND  0xFFEFEFEFu
#define XFS_PAL_LIGHT       0xFFF7F7F7u
#define XFS_PAL_MIDLIGHT    0xFFBFBFBFu
#define XFS_PAL_DARK        0xFF9E9E9Eu
#define XFS_PAL_MID         0xFFB7B7B7u
#define XFS_PAL_TEXT        0xFF000000u
#define XFS_PAL_BASE        0xFFFFFFFFu
#define XFS_PAL_HIGHLIGHT   0xFF308CC6u
#define XFS_PAL_HIGHLIGHTED 0xFFFFFFFFu
#define XFS_PAL_DISABLEDTEXT 0xFFBEBEBEu
#define XFS_PAL_DISABLEDBASE 0xFFEFEFEFu
#define XFS_PAL_DISABLEDARK 0xFFBEBEBEu
#define XFS_PAL_DISABLEDSHADOW 0xFFBABABAu
#define XFS_PAL_DISABLEDHIGHLIGHT 0xFF919191u
#define XFS_PAL_SHADOW      0xFF6D6D6Du
#define XFS_PAL_PLACEHOLDER  0x80000000u

static XPalette VXFusionStyle_standardPalette(XStyle* self)
{
    XPalette pal;
    XColor c;
    int g;
    (void)self;
    XMemset(&pal, 0, sizeof(pal));
    XColor_setRgba(&c, XFS_PAL_WINDOWTEXT);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_WindowText, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Text, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_ButtonText, c);
    XColor_setRgba(&c, XFS_PAL_BACKGROUND);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Button, c);
    XColor_setRgba(&c, XFS_PAL_LIGHT);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Light, c);
    XColor_setRgba(&c, XFS_PAL_MIDLIGHT);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Midlight, c);
    XColor_setRgba(&c, XFS_PAL_DARK);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Dark, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Shadow, c);
    XColor_setRgba(&c, XFS_PAL_MID);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Mid, c);
    XColor_setRgba(&c, XFS_PAL_BASE);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Base, c);
    XColor_setRgba(&c, XFS_PAL_HIGHLIGHT);
    for (g = XPaletteColorGroup_Active; g < XPaletteColorGroup_NColorGroups;
         ++g) {
        XPalette_setColor(&pal, (XPaletteColorGroup)g,
                          XPaletteColorRole_Highlight, c);
        XPalette_setColor(&pal, (XPaletteColorGroup)g,
                          XPaletteColorRole_Accent, c);
    }
    XColor_setRgba(&c, XFS_PAL_HIGHLIGHTED);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_HighlightedText, c);
    XColor_setRgba(&c, XFS_PAL_SHADOW);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Shadow, c);
    /* 禁用组。 */
    XColor_setRgba(&c, XFS_PAL_DISABLEDTEXT);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_Text, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_WindowText, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_ButtonText, c);
    XColor_setRgba(&c, XFS_PAL_DISABLEDBASE);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_Base, c);
    XColor_setRgba(&c, XFS_PAL_DISABLEDARK);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_Dark, c);
    XColor_setRgba(&c, XFS_PAL_DISABLEDSHADOW);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_Shadow, c);
    XColor_setRgba(&c, XFS_PAL_DISABLEDHIGHLIGHT);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_Highlight, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_Accent, c);
    XColor_setRgba(&c, XFS_PAL_PLACEHOLDER);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_PlaceholderText, c);
    return pal;
}

/* ==================== Fusion polish（对标 QFusionStyle::polish 子集：
 *    hover 由控件层承担，这里仅回落父类） ==================== */

static void VXFusionStyle_polish(XStyle* self, XWidget* widget)
{
    XClass_Parent(XCommonStyle, EXStyle_Polish,
                  void(*)(XStyle*, XWidget*))((XStyle*)self, widget);
}

XVtable* XFusionStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFusionStyle)
    XVTABLE_INHERIT_XCLASS(XCommonStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawPrimitive,
                             VXFusionStyle_drawPrimitive);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawControl,
                             VXFusionStyle_drawControl);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawComplexControl,
                             VXFusionStyle_drawComplexControl);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_PixelMetric,
                             VXFusionStyle_pixelMetric);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_StandardPalette,
                             VXFusionStyle_standardPalette);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Polish,
                             VXFusionStyle_polish);
    return XVTABLE_DEFAULT;
}

void XFusionStyle_init(XFusionStyle* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XCommonStyle_init(&self->m_base);
    XClassSetVtable(self, XFusionStyle);
}

XFusionStyle* XFusionStyle_create_ex(XMemoryType memory)
{
    XFusionStyle* self = (XFusionStyle*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XFusionStyle_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XFusionStyle_installDefault(void)
{
    XFusionStyle* fs = XFusionStyle_create();
    if (fs) XStyle_setDefaultStyle((XStyle*)fs);
}

#endif /* XSTYLE_ON */
