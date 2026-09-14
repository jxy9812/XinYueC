#include "XFusionStyle.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include <string.h>

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
    /* 圆角边框：上下边 + 左右边（矩形近似）。 */
    XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, border);
    XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                        r.width, 1}, border);
    XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, border);
    XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                        1, r.height}, border);
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
        XClass_Parent(XWindowsStyle, EXStyle_DrawPrimitive,
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
    XClass_Parent(XWindowsStyle, EXStyle_DrawControl,
                  void(*)(XStyle*, int, const XStyleOption*,
                          XPainter*, const XWidget*))(
        (XStyle*)self, ce, option, painter, widget);
}

XVtable* XFusionStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFusionStyle)
    XVTABLE_INHERIT_XCLASS(XWindowsStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawPrimitive,
                             VXFusionStyle_drawPrimitive);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawControl,
                             VXFusionStyle_drawControl);
    return XVTABLE_DEFAULT;
}

void XFusionStyle_init(XFusionStyle* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWindowsStyle_init(&self->m_base);
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
