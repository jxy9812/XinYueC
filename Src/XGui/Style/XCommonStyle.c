#include "XCommonStyle.h"
#include "XStringUtils.h"
#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include <math.h>

#if XSTYLE_ON

static void xcs_drawSpinArrow(XStyle* self, const XStyleOption* option,
                              XPainter* painter, bool up);
static void xcs_drawSpinSign(XStyle* self, const XStyleOption* option,
                             XPainter* painter, bool plus);
static void xcs_drawBarPanel(XStyle* self, const XStyleOption* option,
                             XPainter* painter);
static void xcs_drawComboBox(XStyle* self, const XStyleOption* option,
                             XPainter* painter, const XWidget* widget);
static void xcs_drawArrow(XStyle* self, const XStyleOption* option,
                          XPainter* painter, int dir);

/* 调色板角色取色（option 内嵌调色板）。 */

/* ==================== Fusion 色彩公式（对标 QColor lighter/darker） ==================== */

/** @brief 变亮（对标 QColor::lighter(factor)：每通道 v+(255-v)*f/100）。 */
static uint32_t xcs_lighter(uint32_t c, int factor)
{
    int r = (c >> 16) & 0xFF;
    int g = (c >> 8) & 0xFF;
    int b = c & 0xFF;
    int a = (c >> 24) & 0xFF;
    r = r + (255 - r) * factor / 100;
    g = g + (255 - g) * factor / 100;
    b = b + (255 - b) * factor / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

/** @brief 变暗（对标 QColor::darker(factor)：每通道 v*f/100）。 */
static uint32_t xcs_darker(uint32_t c, int factor)
{
    int r = ((c >> 16) & 0xFF) * factor / 100;
    int g = ((c >> 8) & 0xFF) * factor / 100;
    int b = (c & 0xFF) * factor / 100;
    int a = (c >> 24) & 0xFF;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

/** @brief 灰度（qGray：0.299R+0.587G+0.114B）。 */
static int xcs_gray(uint32_t c)
{
    return (((c >> 16) & 0xFF) * 299 + ((c >> 8) & 0xFF) * 587 +
            (c & 0xFF) * 114) / 1000;
}

/** @brief 两色按 t/100 混合（对标 mergedColors）。 */
static uint32_t xcs_merged(uint32_t c1, uint32_t c2, int t)
{
    int r = ((c1 >> 16) & 0xFF) +
            ((((c2 >> 16) & 0xFF) - ((c1 >> 16) & 0xFF)) * t) / 100;
    int g = ((c1 >> 8) & 0xFF) +
            ((((c2 >> 8) & 0xFF) - ((c1 >> 8) & 0xFF)) * t) / 100;
    int b = (c1 & 0xFF) +
            (((c2 & 0xFF) - (c1 & 0xFF)) * t) / 100;
    int a = ((c1 >> 24) & 0xFF);
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

/** @brief Fusion buttonColor（button 变亮 + 饱和度 0.75，
 *         对标 QFusionStylePrivate::buttonColor）。 */
static uint32_t xcs_buttonColor(uint32_t button)
{
    int val = xcs_gray(button);
    int lift = 100 + (180 - val) / 6;
    if (lift < 101) lift = 101;
    {
        uint32_t c = xcs_lighter(button, lift);
        /* HSV 饱和度 * 0.75：RGB→HSV→降 S→RGB。 */
        int r = (c >> 16) & 0xFF;
        int g = (c >> 8) & 0xFF;
        int b = c & 0xFF;
        int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        int delta = mx - mn;
        int v = mx;
        int sat = mx ? delta * 100 / mx : 0;
        int hue = 0;
        int nr;
        int ng;
        int nb;
        if (delta) {
            if (mx == r) hue = ((g - b) * 60 / delta + 360) % 360;
            else if (mx == g) hue = ((b - r) * 60 / delta + 120);
            else hue = ((r - g) * 60 / delta + 240);
        }
        sat = sat * 75 / 100;
        {
            /* HSV→RGB（v/sat/hue）。 */
            int region = (hue / 60) % 6;
            int rem = (hue % 60) * 255 / 60;
            int pv = (v * (100 - sat)) / 100;
            int qv = (v * (100 - sat * rem / 25500)) / 100;
            int tv = (v * (100 - sat * (255 - rem) / 25500)) / 100;
            switch (region) {
            case 0: nr = v; ng = tv; nb = pv; break;
            case 1: nr = qv; ng = v; nb = pv; break;
            case 2: nr = pv; ng = v; nb = tv; break;
            case 3: nr = pv; ng = qv; nb = v; break;
            case 4: nr = tv; ng = pv; nb = v; break;
            default: nr = v; ng = pv; nb = qv; break;
            }
        }
        return ((uint32_t)(c & 0xFF000000u)) |
               ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
    }
}

static uint32_t xcs_color(const XStyleOption* option,
                          XPaletteColorRole role)
{
#if XPALETTE_ON
    XColor c = XPalette_color((XPalette*)&option->m_palette,
                              XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)option; (void)role;
    return 0xFF000000u;
#endif
}

static uint32_t xcs_buttonText(const XStyleOption* option)
{
    uint32_t c = xcs_color(option, XPaletteColorRole_ButtonText);
    return c ? c : 0xFF000000u;
}

/** @brief 矩形内绘制单行文本（textlayout 裁剪时回退 drawText）。 */
static void xcs_drawTextInRect(XPainter* painter, const XRect* rect,
                               const char* text, uint32_t color)
{
#if XPAINTER_TEXTLAYOUT_ON
    XPainter_drawTextRect(painter, rect, 0, text, color);
#else
    (void)rect;
    XPainter_drawText(painter, rect->x + 2,
                      rect->y + rect->height - 6, text, color);
#endif
}

/** @brief 绘制普通边框（PE_Frame）。 */
static void xcs_drawFrame(XStyle* self, const XStyleOption* option,
                          XPainter* painter)
{
    uint32_t dark;
    uint32_t light;
    XRect r;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    if (r.width < 2 || r.height < 2) return;
    dark = xcs_color(option, XPaletteColorRole_Dark);
    light = xcs_color(option, XPaletteColorRole_Light);
    if (dark == 0) dark = 0xFF808080u;
    if (light == 0) light = 0xFFE0E0E0u;
    XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, dark);
    XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, dark);
    XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                        r.width, 1}, light);
    XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                        1, r.height}, light);
}

/** @brief 绘制命令按钮面板（PE_PanelButtonCommand：Fusion 渐变入口）。 */
static void xcs_drawPanelButtonCommand(XStyle* self,
                                       const XStyleOption* option,
                                       XPainter* painter)
{
    uint32_t base;
    uint32_t light;
    uint32_t dark;
    XRect r;
    bool sunken;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    if (r.width < 2 || r.height < 2) return;
    base = xcs_color(option, XPaletteColorRole_Button);
    if (base == 0) base = 0xFFCFCFCFu;
    sunken = (option->m_state & XStyleState_Sunken) != 0;
    light = xcs_color(option, XPaletteColorRole_Light);
    dark = xcs_color(option, XPaletteColorRole_Dark);
    if (light == 0) light = 0xFFE0E0E0u;
    if (dark == 0) dark = 0xFF808080u;
    XPainter_fillRect(painter, &r, base);
    if (sunken) {
        /* 凹陷：内缩 1px 的高光上边 + 暗下边。 */
        XPainter_fillRect(painter,
            &(XRect){r.x + 1, r.y + 1, r.width - 2, 1}, dark);
        XPainter_fillRect(painter,
            &(XRect){r.x + 1, r.y + r.height - 2, r.width - 2, 1}, light);
    } else {
        /* 凸起：上/左亮，下/右暗（Fusion 渐变由派生类覆盖）。 */
        XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, light);
        XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, light);
        XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                            r.width, 1}, dark);
        XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                            1, r.height}, dark);
    }
}

/** @brief 绘制复选指示器（PE_IndicatorCheckBox）。 */
static void xcs_drawIndicatorCheckBox(XStyle* self,
                                      const XStyleOption* option,
                                      XPainter* painter)
{
    uint32_t fg;
    uint32_t base;
    XRect r;
    bool on;
    bool noChange;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    fg = xcs_color(option, XPaletteColorRole_Text);
    if (fg == 0) fg = 0xFF000000u;
    base = xcs_color(option, XPaletteColorRole_Base);
    if (base == 0) base = 0xFFFFFFFFu;
    on = (option->m_state & XStyleState_On) != 0;
    noChange = (option->m_state & XStyleState_NoChange) != 0;
    if (r.width < 4 || r.height < 4) return;
    /* 白底 + 灰边框。 */
    XPainter_fillRect(painter, &r, base);
    XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, fg);
    XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, fg);
    XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                        r.width, 1}, fg);
    XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                        1, r.height}, fg);
    if (on || noChange) {
        /* 对勾：drawLine 两段折线（完整复刻原 XCheckBox 实现）；
           部分选中画中横线。 */
        if (noChange) {
            XPainter_drawLine(painter, r.x + 3, r.y + r.height / 2,
                              r.x + r.width - 3, r.y + r.height / 2);
        } else {
            XPainter_drawLine(painter, r.x + 3, r.y + 7,
                              r.x + 6, r.y + 10);
            XPainter_drawLine(painter, r.x + 6, r.y + 10,
                              r.x + 10, r.y + 3);
        }
    }
}

/** @brief 绘制单选指示器（PE_IndicatorRadioButton）。 */
static void xcs_drawIndicatorRadioButton(XStyle* self,
                                         const XStyleOption* option,
                                         XPainter* painter)
{
    uint32_t fg;
    uint32_t base;
    XRect r;
    bool on;
    int cx;
    int cy;
    int rad;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    fg = xcs_color(option, XPaletteColorRole_Text);
    if (fg == 0) fg = 0xFF000000u;
    base = xcs_color(option, XPaletteColorRole_Base);
    if (base == 0) base = 0xFFFFFFFFu;
    on = (option->m_state & XStyleState_On) != 0;
    if (r.width < 4 || r.height < 4) return;
    cx = r.x + r.width / 2;
    cy = r.y + r.height / 2;
    rad = (r.width < r.height ? r.width : r.height) / 2;
    /* 圆底 + 圆边框 + 内点（drawEllipse 完整复刻；SHAPE 裁剪时
       逐行填充回退，与关闭形状时的既有裁剪行为一致）。 */
    XPainter_fillRect(painter, &r, base);
#if XPAINTER_SHAPE_ON
    {
        XRect circle;
        XRect_init(&circle, cx - rad, cy - rad, rad * 2, rad * 2);
        XPainter_drawEllipse(painter, &circle);
    }
    if (on) {
        int rr = rad / 3;
        XRect dot;
        XRect_init(&dot, cx - rr, cy - rr, rr * 2, rr * 2);
        XPainter_drawEllipse(painter, &dot);
        XPainter_fillRect(painter, &dot, fg);
    }
#else
    {
        int i;
        for (i = 0; i < rad; ++i) {
            int hw = (int)(0.8 * (rad - i));
            if (hw < 1) hw = 1;
            XPainter_fillRect(painter,
                &(XRect){cx - hw, cy - rad + i, hw * 2 + 1, 1}, fg);
        }
        if (on) {
            int rr = rad / 3;
            for (i = 0; i < rr * 2 + 1; ++i) {
                int hw = (int)(0.8 * (rr - i));
                if (hw < 1) hw = 1;
                XPainter_fillRect(painter,
                    &(XRect){cx - hw, cy - rr + i, hw * 2 + 1, 1}, fg);
            }
        }
    }
#endif /* XPAINTER_SHAPE_ON */
}

/** @brief 绘制箭头（PE_IndicatorArrow*：drawPolygon 实心三角形）。 */
static void xcs_drawArrow(XStyle* self, const XStyleOption* option,
                          XPainter* painter, int dir)
{
    uint32_t fg;
    XRect r;
    XPoint tri[3];
    int cx;
    int cy;
    int hw;
    int hh;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    fg = xcs_buttonText(option);
    cx = r.x + r.width / 2;
    cy = r.y + r.height / 2;
    hw = r.width / 3;
    if (hw < 2) hw = 2;
    hh = r.height / 3;
    if (hh < 2) hh = 2;
    if (dir == 0) {          /* 下：顶边两点 + 底尖。 */
        tri[0].x = cx - hw; tri[0].y = cy - hh;
        tri[1].x = cx + hw; tri[1].y = cy - hh;
        tri[2].x = cx;      tri[2].y = cy + hh;
    } else if (dir == 1) {   /* 上：底边两点 + 顶尖。 */
        tri[0].x = cx - hw; tri[0].y = cy + hh;
        tri[1].x = cx + hw; tri[1].y = cy + hh;
        tri[2].x = cx;      tri[2].y = cy - hh;
    } else if (dir == 2) {   /* 左：右边两点 + 左尖。 */
        tri[0].x = cx + hw; tri[0].y = cy - hh;
        tri[1].x = cx + hw; tri[1].y = cy + hh;
        tri[2].x = cx - hw; tri[2].y = cy;
    } else {                 /* 右：左边两点 + 右尖。 */
        tri[0].x = cx - hw; tri[0].y = cy - hh;
        tri[1].x = cx - hw; tri[1].y = cy + hh;
        tri[2].x = cx + hw; tri[2].y = cy;
    }
#if XPAINTER_POLYGON_ON
    XPainter_drawPolygon(painter, tri, 3, true);
#else
    /* POLYGON 裁剪时按填充行近似（与关闭形状时的既有裁剪行为一致）。 */
    {
        int i;
        for (i = 0; i < hh; ++i) {
            int w = (i + 1) * hw / hh;
            if (dir == 0)
                XPainter_fillRect(painter, &(XRect){cx - w, cy - hh + i,
                                                    w * 2, 1}, fg);
            else if (dir == 1)
                XPainter_fillRect(painter, &(XRect){cx - w,
                                                    cy + hh - 1 - i,
                                                    w * 2, 1}, fg);
            else if (dir == 2)
                XPainter_fillRect(painter, &(XRect){cx - hh + i, cy - w,
                                                    1, w * 2}, fg);
            else
                XPainter_fillRect(painter, &(XRect){cx + hh - 1 - i,
                                                    cy - w, 1, w * 2}, fg);
        }
    }
#endif /* XPAINTER_POLYGON_ON */
}

/** @brief 绘制焦点框（PE_FrameFocusRect：虚线矩形）。 */
static void xcs_drawFocusRect(XStyle* self, const XStyleOption* option,
                              XPainter* painter)
{
    uint32_t fg;
    XRect r;
    int i;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    fg = xcs_buttonText(option);
    if (r.width < 2 || r.height < 2) return;
    /* 四边虚线（drawLine 逐段，对标 Qt dotted 焦点框）。 */
    for (i = 0; i < r.width; i += 4) {
        int seg = (i + 2 <= r.width) ? 2 : (r.width - i);
        XPainter_drawLine(painter, r.x + i, r.y, r.x + i + seg, r.y);
        XPainter_drawLine(painter, r.x + i, r.y + r.height - 1,
                          r.x + i + seg, r.y + r.height - 1);
    }
    for (i = 0; i < r.height; i += 4) {
        int seg = (i + 2 <= r.height) ? 2 : (r.height - i);
        XPainter_drawLine(painter, r.x, r.y + i, r.x, r.y + i + seg);
        XPainter_drawLine(painter, r.x + r.width - 1, r.y + i,
                          r.x + r.width - 1, r.y + i + seg);
    }
}

/** @brief 绘制进度槽（CE_ProgressBarGroove）。 */
static void xcs_drawProgressGroove(XStyle* self,
                                   const XStyleOption* option,
                                   XPainter* painter)
{
    uint32_t base;
    uint32_t dark;
    uint32_t light;
    XRect r;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    base = xcs_color(option, XPaletteColorRole_Base);
    dark = xcs_color(option, XPaletteColorRole_Dark);
    light = xcs_color(option, XPaletteColorRole_Light);
    if (base == 0) base = 0xFFFFFFFFu;
    if (dark == 0) dark = 0xFF808080u;
    if (light == 0) light = 0xFFE0E0E0u;
    XPainter_fillRect(painter, &r, base);
    /* 凹陷描边：上/左 Dark，下/右 Light（对标原 XProgressBar 实现）。 */
    XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, dark);
    XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, dark);
    XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                        r.width, 1}, light);
    XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                        1, r.height}, light);
}

/** @brief 绘制进度内容（CE_ProgressBarContents：高亮块）。 */
static void xcs_drawProgressContents(XStyle* self,
                                     const XStyleOption* option,
                                     XPainter* painter)
{
    uint32_t hl;
    XRect r;
    double frac;
    int len;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    hl = xcs_color(option, XPaletteColorRole_Highlight);
    if (hl == 0) hl = 0xFF2A82DAu;
    frac = (option->m_progressMax > option->m_progressMin)
        ? (double)(option->m_progressValue - option->m_progressMin)
          / (double)(option->m_progressMax - option->m_progressMin)
        : 0.0;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    if (option->m_horizontal) {
        /* 内缩 1px（对标 QProgressBar 内容与槽的 1px 边距）。 */
        len = (int)((r.width - 2) * frac);
        if (len < 1 && frac > 0.0) len = 1;
        if (len > r.width - 2) len = r.width - 2;
        XPainter_fillRect(painter, &(XRect){r.x + 1, r.y + 1, len,
                                            r.height - 2}, hl);
    } else {
        len = (int)((r.height - 2) * frac);
        if (len < 1 && frac > 0.0) len = 1;
        if (len > r.height - 2) len = r.height - 2;
        XPainter_fillRect(painter,
            &(XRect){r.x + 1, r.y + 1 + (r.height - 2) - len,
                     r.width - 2, len}, hl);
    }
}

/** @brief 绘制按钮控件（CE_PushButton：面板+标签+焦点）。 */
static void xcs_drawPushButton(XStyle* self, const XStyleOption* option,
                               XPainter* painter, const XWidget* widget)
{
    XStyleOption panel;
    XRect content;
    uint32_t textColor;
    (void)widget;
    if (!option || !painter) return;
    panel = *option;
    panel.m_type = XStylePE_PanelButtonCommand;
    xcs_drawPanelButtonCommand(self, &panel, painter);
    if (option->m_text && option->m_text[0]) {
        content = option->m_rect;
        content.x += 4;
        content.width -= 8;
        textColor = option->m_textColor;
        if (textColor == 0) textColor = xcs_buttonText(option);
        xcs_drawTextInRect(painter, &content, option->m_text, textColor);
    }
    if (option->m_state & XStyleState_HasFocus) {
        XStyleOption focus = *option;
        XRect fr = option->m_rect;
        focus.m_type = XStylePE_FrameFocusRect;
        fr.x += 3;
        fr.y += 3;
        fr.width -= 6;
        fr.height -= 6;
        focus.m_rect = fr;
        xcs_drawFocusRect(self, &focus, painter);
    }
}

/** @brief 绘制复选/单选控件（CE_CheckBox/CE_RadioButton：指示器+标签）。 */
static void xcs_drawCheckable(XStyle* self, int ce,
                              const XStyleOption* option,
                              XPainter* painter)
{
    XStyleOption ind;
    XRect indRect;
    XRect labelRect;
    uint32_t textColor;
    (void)ce;
    if (!option || !painter) return;
    int indW = XStyle_pixelMetric(self, XStylePM_IndicatorWidth, option);
    int spacing = XStyle_pixelMetric(self, XStylePM_CheckBoxLabelSpacing,
                                     option);
    ind = *option;
    indRect.x = option->m_rect.x;
    indRect.y = option->m_rect.y +
        (option->m_rect.height - indW) / 2;
    indRect.width = indW;
    indRect.height = indW;
    if (indRect.y < option->m_rect.y) indRect.y = option->m_rect.y;
    if (ce == XStyleCE_CheckBox) {
        ind.m_type = XStylePE_IndicatorCheckBox;
        xcs_drawIndicatorCheckBox(self, &ind, painter);
    } else {
        ind.m_type = XStylePE_IndicatorRadioButton;
        xcs_drawIndicatorRadioButton(self, &ind, painter);
    }
    if (option->m_text && option->m_text[0]) {
        labelRect = option->m_rect;
        labelRect.x += indW + spacing;
        labelRect.width -= indW + spacing;
        textColor = option->m_textColor;
        if (textColor == 0) textColor = xcs_buttonText(option);
        xcs_drawTextInRect(painter, &labelRect, option->m_text, textColor);
    }
}

/** @brief 绘制页签形状（CE_TabBarTabShape）。 */
static void xcs_drawTabShape(XStyle* self, const XStyleOption* option,
                             XPainter* painter)
{
    uint32_t base;
    uint32_t dark;
    uint32_t light;
    XRect r;
    bool selected;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    selected = option->m_tabSelected;
    base = selected
        ? xcs_color(option, XPaletteColorRole_Base)
        : xcs_color(option, XPaletteColorRole_Button);
    dark = xcs_color(option, XPaletteColorRole_Dark);
    light = xcs_color(option, XPaletteColorRole_Light);
    if (base == 0) base = 0xFFCFCFCFu;
    if (dark == 0) dark = 0xFF808080u;
    if (light == 0) light = 0xFFE0E0E0u;
    if (r.width < 2 || r.height < 2) return;
    XPainter_fillRect(painter, &r, base);
    if (selected) {
        /* 选中页签：上/左/右亮边，下边融入容器。 */
        XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, light);
        XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, light);
        XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                            1, r.height}, light);
    } else {
        XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, light);
        XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, dark);
        XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                            1, r.height}, dark);
    }
}

static void VXCommonStyle_drawPrimitive(XStyle* self, int pe,
                                        const XStyleOption* option,
                                        XPainter* painter,
                                        const XWidget* widget)
{
    (void)widget;
    switch (pe) {
    case XStylePE_Frame:
    case XStylePE_FrameButtonBevel:
        xcs_drawFrame(self, option, painter);
        break;
    case XStylePE_PanelButtonCommand:
    case XStylePE_PanelButtonBevel:
    case XStylePE_PanelButtonTool:
        xcs_drawPanelButtonCommand(self, option, painter);
        break;
    case XStylePE_IndicatorCheckBox:
        xcs_drawIndicatorCheckBox(self, option, painter);
        break;
    case XStylePE_IndicatorRadioButton:
        xcs_drawIndicatorRadioButton(self, option, painter);
        break;
    case XStylePE_IndicatorArrowDown:
        xcs_drawArrow(self, option, painter, 0);
        break;
    case XStylePE_IndicatorArrowUp:
        xcs_drawArrow(self, option, painter, 1);
        break;
    case XStylePE_IndicatorArrowLeft:
        xcs_drawArrow(self, option, painter, 2);
        break;
    case XStylePE_IndicatorArrowRight:
        xcs_drawArrow(self, option, painter, 3);
        break;
    case XStylePE_FrameFocusRect:
        xcs_drawFocusRect(self, option, painter);
        break;
    case XStylePE_PanelLineEdit:
        xcs_drawFrame(self, option, painter);
        break;
    case XStylePE_IndicatorSpinUp:
        xcs_drawSpinArrow(self, option, painter, true);
        break;
    case XStylePE_IndicatorSpinDown:
        xcs_drawSpinArrow(self, option, painter, false);
        break;
    case XStylePE_IndicatorSpinPlus:
        xcs_drawSpinSign(self, option, painter, true);
        break;
    case XStylePE_IndicatorSpinMinus:
        xcs_drawSpinSign(self, option, painter, false);
        break;
    case XStylePE_PanelMenuBar:
    case XStylePE_PanelToolBar:
        xcs_drawBarPanel(self, option, painter);
        break;
    default:
        break;
    }
}


/** @brief 绘制页签标签（CE_TabBarTabLabel：居中文本 + 焦点框）。 */
static void xcs_drawTabLabel(XStyle* self, const XStyleOption* option,
                             XPainter* painter, const XWidget* widget)
{
    uint32_t textColor;
    int textW;
    int textH;
    XRect r;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    /* 选中页签用 HighlightedText，未选中用 WindowText（对标
       QCommonStyle 的 foregroundRole + Selected 状态语义）。 */
    if (option->m_tabSelected) {
        textColor = xcs_color(option, XPaletteColorRole_HighlightedText);
        if (textColor == 0) textColor = 0xFFFFFFFFu;
    } else {
        textColor = xcs_color(option, XPaletteColorRole_WindowText);
        if (textColor == 0) textColor = 0xFF000000u;
    }
    if (option->m_text && option->m_text[0]) {
        textW = XPainter_textWidth(XPainter_font(painter), option->m_text);
        textH = XPainter_textHeight(XPainter_font(painter));
        if (textH < 14) textH = 14;
        XPainter_drawText(painter,
            r.x + (r.width - textW) / 2,
            r.y + (r.height - textH) / 2 + textH - 4,
            option->m_text, textColor);
    }
    if (option->m_state & XStyleState_HasFocus) {
        XStyleOption foc = *option;
        foc.m_type = XStylePE_FrameFocusRect;
        foc.m_rect.x = r.x + 2;
        foc.m_rect.y = r.y + 2;
        foc.m_rect.width = r.width - 4;
        foc.m_rect.height = r.height - 4;
        xcs_drawFocusRect(self, &foc, painter);
    }
}


/** @brief 绘制微调上/下按钮箭头（PE_IndicatorSpinUp/Down：折线箭头）。 */
static void xcs_drawSpinArrow(XStyle* self, const XStyleOption* option,
                              XPainter* painter, bool up)
{
    uint32_t fg;
    XRect r;
    int cx;
    int cy;
    int s;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    fg = xcs_buttonText(option);
    cx = r.x + r.width / 2;
    cy = r.y + r.height / 2;
    s = 4;
    if (up) {
        XPainter_drawLine(painter, cx - s, cy + 1, cx + s, cy + 1);
        XPainter_drawLine(painter, cx - s, cy + 1, cx, cy - s);
        XPainter_drawLine(painter, cx + s, cy + 1, cx, cy - s);
    } else {
        XPainter_drawLine(painter, cx - s, cy - 1, cx + s, cy - 1);
        XPainter_drawLine(painter, cx - s, cy - 1, cx, cy + s);
        XPainter_drawLine(painter, cx + s, cy - 1, cx, cy + s);
    }
}

/** @brief 绘制微调加/减号（PE_IndicatorSpinPlus/Minus）。 */
static void xcs_drawSpinSign(XStyle* self, const XStyleOption* option,
                             XPainter* painter, bool plus)
{
    uint32_t fg;
    XRect r;
    int cx;
    int cy;
    int s;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    fg = xcs_buttonText(option);
    cx = r.x + r.width / 2;
    cy = r.y + r.height / 2;
    s = 4;
    XPainter_drawLine(painter, cx - s, cy, cx + s, cy);
    if (plus)
        XPainter_drawLine(painter, cx, cy - s, cx, cy + s);
}


/** @brief 绘制微调框（CC_SpinBox：面板 + 上/下按钮 + 符号）。
 *
 *  完整对齐 QCommonStyle::drawComplexControl(CC_SpinBox)：frame 走
 *  PanelLineEdit；Up/Down 按钮走 PanelButtonBevel，按步进使能置
 *  Disabled 调色板组；按下活动按钮置 Sunken/On，否则 Raised。
 */
static void xcs_drawSpinBox(XStyle* self, const XStyleOption* option,
                            XPainter* painter, const XWidget* widget)
{
    XStyleOption copy = *option;
    XRect r = option->m_rect;
    int buttonW = 16;
    int bh = r.height / 2;
    int bx = r.x + r.width - buttonW;
    bool upEnabled = (option->m_spinStepEnabled & 0x01) != 0;
    bool downEnabled = (option->m_spinStepEnabled & 0x02) != 0;
    if (!option || !painter) return;
    if (r.width <= 2 || r.height <= 2) return;
    /* frame。 */
    if (option->m_spinFrame) {
        XStyleOption frame = *option;
        frame.m_type = XStylePE_PanelLineEdit;
        frame.m_rect = r;
        xcs_drawFrame(self, &frame, painter);
    }
    /* 按钮区背景（与原 XSpinBox 一致：按钮列填充 + 分隔线）。 */
    {
        XRect area;
        XRect_init(&area, bx, r.y, buttonW, r.height);
        XPainter_fillRect(painter, &area,
            xcs_color(option, XPaletteColorRole_Button));
        XPainter_fillRect(painter,
            &(XRect){bx - 1, r.y, 1, r.height},
            xcs_color(option, XPaletteColorRole_Dark));
    }
    /* 上按钮。 */
    if (option->m_spinSymbols != 2 /* NoButtons 不达此处 */) {
        XStyleOption up = copy;
        up.m_rect.x = bx;
        up.m_rect.y = r.y;
        up.m_rect.width = buttonW;
        up.m_rect.height = bh;
        up.m_type = XStylePE_PanelButtonBevel;
        up.m_state = option->m_state & ~(uint32_t)XStyleState_Sunken;
        if (option->m_spinActiveUp &&
            (option->m_state & XStyleState_Sunken)) {
            up.m_state |= XStyleState_On | XStyleState_Sunken;
        } else {
            up.m_state |= XStyleState_Raised;
        }
        if (!upEnabled) up.m_state &= ~(uint32_t)XStyleState_Enabled;
        xcs_drawPanelButtonCommand(self, &up, painter);
        {
            XStyleOption ind = up;
            ind.m_type = option->m_spinSymbols == 1
                ? XStylePE_IndicatorSpinPlus : XStylePE_IndicatorSpinUp;
            xcs_drawSpinArrow(self, &ind, painter, true);
            if (option->m_spinSymbols == 1)
                xcs_drawSpinSign(self, &ind, painter, true);
        }
        /* 下按钮。 */
        {
            XStyleOption down = copy;
            down.m_rect.x = bx;
            down.m_rect.y = r.y + bh;
            down.m_rect.width = buttonW;
            down.m_rect.height = bh;
            down.m_type = XStylePE_PanelButtonBevel;
            down.m_state = option->m_state &
                ~(uint32_t)XStyleState_Sunken;
            if (option->m_spinActiveDown &&
                (option->m_state & XStyleState_Sunken)) {
                down.m_state |= XStyleState_On | XStyleState_Sunken;
            } else {
                down.m_state |= XStyleState_Raised;
            }
            if (!downEnabled) down.m_state &= ~(uint32_t)XStyleState_Enabled;
            xcs_drawPanelButtonCommand(self, &down, painter);
            {
                XStyleOption ind = down;
                ind.m_type = option->m_spinSymbols == 1
                    ? XStylePE_IndicatorSpinMinus
                    : XStylePE_IndicatorSpinDown;
                xcs_drawSpinArrow(self, &ind, painter, false);
                if (option->m_spinSymbols == 1)
                    xcs_drawSpinSign(self, &ind, painter, false);
            }
        }
    }
}


/** @brief 绘制表盘（CC_Dial：完整对标 QCommonStyle::drawComplexControl
 *         CC_Dial + QStyleHelper::drawDial/calcLines/calcArrow）。 */
static void xcs_drawDial(XStyle* self, const XStyleOption* option,
                         XPainter* painter, const XWidget* widget)
{
    uint32_t dark;
    uint32_t light;
    uint32_t button;
    uint32_t windowText;
    uint32_t base;
    XRect r;
    int width;
    int height;
    int radius;
    int bigLine;
    int br2;
    int bcx;
    int bcy;
    XRect br;
    int i;
    int notchCount;
    double angleRad;
    double ca;
    double sa;
    XPoint arrow[3];
    int len;
    int back;
    double aDeg;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    if (r.width <= 8 || r.height <= 8) return;
    dark       = xcs_color(option, XPaletteColorRole_Dark);
    light      = xcs_color(option, XPaletteColorRole_Light);
    button     = xcs_color(option, XPaletteColorRole_Button);
    windowText = xcs_color(option, XPaletteColorRole_WindowText);
    base       = xcs_color(option, XPaletteColorRole_Base);
    if (dark == 0) dark = 0xFF808080u;
    if (light == 0) light = 0xFFE0E0E0u;
    if (button == 0) button = 0xFFCFCFCFu;
    if (windowText == 0) windowText = 0xFF000000u;
    if (base == 0) base = 0xFFFFFFFFu;
    width  = r.width;
    height = r.height;
    radius = (width < height ? width : height) / 2;
    bigLine = radius / 6;
    if (bigLine < 4) bigLine = 4;
    if (bigLine > radius / 2) bigLine = radius / 2;
    br.x = radius / 6 + (width - 2 * radius) / 2 + 1;
    br.y = radius / 6 + (height - 2 * radius) / 2 + 1;
    br.width  = radius * 2 - 2 * (radius / 6) - 2;
    br.height = br.width;
    bcx = br.x + br.width / 2;
    bcy = br.y + br.height / 2;
    br2 = br.width / 2;
    /* 1) 刻度线：区分大线/小线（对标 calcLines：i==0 或 pageStep 倍数
     *    画大线 bigLine，其余画小线 bigLine/2）。 */
    notchCount = option->m_notchSize;
    if (option->m_notchesVisible && notchCount > 0) {
        int smallLine = bigLine / 2;
        int range = option->m_sliderMax - option->m_sliderMin;
        XPainter_setPen(painter, windowText);
        for (i = 0; i <= notchCount; ++i) {
            double a = (240.0 - 300.0 * i / notchCount) *
                       3.14159265358979323846 / 180.0;
            double c = cos(a);
            double sn = -sin(a);
            bool big = (i == 0) ||
                (option->m_pageStep > 0 &&
                 ((notchCount * i) % option->m_pageStep) == 0) ||
                (range > 0 && ((option->m_sliderMin +
                    (option->m_sliderMax - option->m_sliderMin) * i /
                    notchCount) % option->m_pageStep) == 0);
            int inner = big ? (br2 - bigLine)
                            : (br2 - 1 - smallLine);
            int outer = big ? br2 : (br2 - 1);
            XPainter_drawLine(painter,
                              (int)(bcx + inner * c),
                              (int)(bcy + inner * sn),
                              (int)(bcx + outer * c),
                              (int)(bcy + outer * sn));
        }
    }
    /* 2) 表盘圆：Base 色填充（drawEllipse；SHAPE 裁剪退化扫描线圆）。 */
    XPainter_setBrush(painter, base);
    XPainter_setPen(painter, base);
#if XPAINTER_SHAPE_ON
    XPainter_drawEllipse(painter, &br);
#else
    {
        int rc = br.width / 2;
        int dy;
        for (dy = -rc; dy <= rc; ++dy) {
            int dx = (int)(sqrt((double)(rc * rc - dy * dy)));
            if (dx < 1) continue;
            XPainter_drawLine(painter, bcx - dx, bcy + dy,
                              bcx + dx, bcy + dy);
        }
    }
#endif
    /* 3) 凹槽双弧：dark 左上半 / light 右下半。 */
#if XPAINTER_SHAPE_ON
    XPainter_setPen(painter, dark);
    XPainter_drawArc(painter, &br, 60 * 16, 180 * 16);
    XPainter_setPen(painter, light);
    XPainter_drawArc(painter, &br, 240 * 16, 180 * 16);
#endif
    /* 4) 三角指示箭头（对标 calcArrow）。 */
    {
        int range = option->m_sliderMax - option->m_sliderMin;
        double frac = (range > 0)
            ? (double)(option->m_sliderValue - option->m_sliderMin) / range
            : 0.0;
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        angleRad = (240.0 - 300.0 * frac) *
                   3.14159265358979323846 / 180.0;
    }
    ca = cos(angleRad);
    sa = -sin(angleRad);
    len = br2 - bigLine - 5;
    if (len < 5) len = 5;
    back = len / 2;
    arrow[0].x = (int)(bcx + len * ca);
    arrow[0].y = (int)(bcy + len * sa);
    arrow[1].x = (int)(bcx + back * cos(angleRad +
        3.14159265358979323846 * 5.0 / 6.0));
    arrow[1].y = (int)(bcy + back * -sin(angleRad +
        3.14159265358979323846 * 5.0 / 6.0));
    arrow[2].x = (int)(bcx + back * cos(angleRad -
        3.14159265358979323846 * 5.0 / 6.0));
    arrow[2].y = (int)(bcy + back * -sin(angleRad -
        3.14159265358979323846 * 5.0 / 6.0));
    XPainter_setBrush(painter, button);
    XPainter_setPen(painter, dark);
#if XPAINTER_POLYGON_ON
    XPainter_drawConvexPolygon(painter, arrow, 3);
#else
    XPainter_drawLine(painter, arrow[0].x, arrow[0].y,
                      arrow[1].x, arrow[1].y);
    XPainter_drawLine(painter, arrow[1].x, arrow[1].y,
                      arrow[2].x, arrow[2].y);
    XPainter_drawLine(painter, arrow[2].x, arrow[2].y,
                      arrow[0].x, arrow[0].y);
#endif
    /* 5) 箭头描边：按箭头角度分区选 light/dark（对标按 a 区间的
     *    高光/阴影方向）。aDeg ∈ (200°, 360°] 为 XDial 起始 240° 体系。 */
    aDeg = 240.0 - 300.0 *
        ((double)(option->m_sliderValue - option->m_sliderMin) /
         (double)(option->m_sliderMax > option->m_sliderMin
             ? (option->m_sliderMax - option->m_sliderMin) : 1));
    if (aDeg < 0) aDeg += 360.0;
    if (aDeg <= 240.0 + 30.0 || aDeg > 360.0 - 30.0) {
        /* 起止附近：上表面受光。 */
        XPainter_setPen(painter, light);
        XPainter_drawLine(painter, arrow[0].x, arrow[0].y,
                          arrow[2].x, arrow[2].y);
        XPainter_drawLine(painter, arrow[1].x, arrow[1].y,
                          arrow[2].x, arrow[2].y);
        XPainter_setPen(painter, dark);
        XPainter_drawLine(painter, arrow[0].x, arrow[0].y,
                          arrow[1].x, arrow[1].y);
    } else if (aDeg <= 240.0 + 75.0) {
        XPainter_setPen(painter, light);
        XPainter_drawLine(painter, arrow[2].x, arrow[2].y,
                          arrow[0].x, arrow[0].y);
        XPainter_setPen(painter, dark);
        XPainter_drawLine(painter, arrow[1].x, arrow[1].y,
                          arrow[2].x, arrow[2].y);
        XPainter_drawLine(painter, arrow[0].x, arrow[0].y,
                          arrow[1].x, arrow[1].y);
    } else if (aDeg <= 240.0 + 225.0) {
        XPainter_setPen(painter, dark);
        XPainter_drawLine(painter, arrow[2].x, arrow[2].y,
                          arrow[0].x, arrow[0].y);
        XPainter_drawLine(painter, arrow[1].x, arrow[1].y,
                          arrow[2].x, arrow[2].y);
        XPainter_setPen(painter, light);
        XPainter_drawLine(painter, arrow[0].x, arrow[0].y,
                          arrow[1].x, arrow[1].y);
    } else {
        XPainter_setPen(painter, dark);
        XPainter_drawLine(painter, arrow[2].x, arrow[2].y,
                          arrow[0].x, arrow[0].y);
        XPainter_setPen(painter, light);
        XPainter_drawLine(painter, arrow[0].x, arrow[0].y,
                          arrow[1].x, arrow[1].y);
        XPainter_drawLine(painter, arrow[1].x, arrow[1].y,
                          arrow[2].x, arrow[2].y);
    }
    /* 6) 焦点框。 */
    if (option->m_state & XStyleState_HasFocus) {
        XStyleOption foc = *option;
        foc.m_type = XStylePE_FrameFocusRect;
        foc.m_rect = r;
        xcs_drawFocusRect(self, &foc, painter);
    }
}


/** @brief 绘制分组框（CC_GroupBox：镂空边框 + 标题 + 勾选框）。
 *
 *  完整对标 QCommonStyle::drawComplexControl(CC_GroupBox)：标题区
 *  裁剪镂空后画 PE_FrameGroupBox 边框，标题文本居中于左上，
 *  可勾选时指示器走 PE_IndicatorCheckBox。
 */
static void xcs_drawGroupBox(XStyle* self, const XStyleOption* option,
                             XPainter* painter, const XWidget* widget)
{
    XRect r;
    uint32_t dark;
    uint32_t light;
    uint32_t windowText;
    uint32_t base;
    int titleH;
    int textW;
    int textX;
    int midY;
    XRect textRect;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    if (r.width <= 2 || r.height <= 2) return;
    dark       = xcs_color(option, XPaletteColorRole_Dark);
    light      = xcs_color(option, XPaletteColorRole_Light);
    windowText = xcs_color(option, XPaletteColorRole_WindowText);
    base       = xcs_color(option, XPaletteColorRole_Base);
    if (dark == 0) dark = 0xFF808080u;
    if (light == 0) light = 0xFFE0E0E0u;
    if (windowText == 0) windowText = 0xFF000000u;
    if (base == 0) base = 0xFFFFFFFFu;
    titleH = 16;
    /* 标题文本区（勾选框占位在标题左缘）。 */
    textW = option->m_text ? (int)XStrlen(option->m_text) : 0;
    textRect.x = r.x + 6 + (option->m_checkable ? 16 : 0);
    textRect.y = r.y;
    textRect.width = textW;
    textRect.height = titleH;
    midY = r.y + titleH / 2;
    /* 边框：上边线绕开标题区（镂空，对标 clip region 减标题矩形）。 */
    if (option->m_text && option->m_text[0] && !option->m_flat) {
        int gapL = textRect.x - 2;
        int gapR = textRect.x + textRect.width + 2;
        XPainter_fillRect(painter, &(XRect){r.x, midY,
                                            (gapL > r.x) ? gapL - r.x : 0, 1},
                          dark);
        if (gapR < r.x + r.width)
            XPainter_fillRect(painter, &(XRect){gapR, midY,
                                                r.x + r.width - gapR, 1},
                              dark);
        XPainter_fillRect(painter, &(XRect){r.x, midY, 1,
                                            r.y + r.height - midY}, dark);
        XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, midY, 1,
                                            r.y + r.height - midY}, dark);
        XPainter_fillRect(painter, &(XRect){r.x + 1, r.y + r.height - 1,
                                            r.width - 2, 1}, dark);
        XPainter_fillRect(painter, &(XRect){r.x + 1, midY + 1, 1,
                                            r.y + r.height - midY - 1},
                          light);
    } else if (option->m_flat) {
        /* 扁平：标题两侧短边框线。 */
        XPainter_fillRect(painter, &(XRect){r.x, midY, r.width, 1}, dark);
    } else {
        /* 无标题：四边完整凹陷边框。 */
        XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, dark);
        XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, dark);
        XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                            r.width, 1}, light);
        XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                            1, r.height}, light);
    }
    /* 勾选框（可勾选时）：PE_IndicatorCheckBox。 */
    if (option->m_checkable) {
        XStyleOption ind = *option;
        XRect cb;
        XRect_init(&cb, r.x + 6,
                   r.y + (titleH - 13) / 2, 13, 13);
        ind.m_type = XStylePE_IndicatorCheckBox;
        ind.m_rect = cb;
        ind.m_state = (option->m_state & XStyleState_Enabled);
        if (option->m_checked)
            ind.m_state |= XStyleState_On;
        else
            ind.m_state |= XStyleState_Off;
        xcs_drawIndicatorCheckBox(self, &ind, painter);
    }
    /* 标题文本。 */
    if (option->m_text && option->m_text[0]) {
        int textH = XPainter_textHeight(XPainter_font(painter));
        if (textH < 14) textH = 14;
        XPainter_drawText(painter,
            textRect.x, textRect.y + (titleH - textH) / 2 + textH - 4,
            option->m_text, windowText);
    }
}


/** @brief 绘制滑块（CC_Slider：凹槽 + 子页高亮 + 刻度 + 凸起把手）。
 *
 *  几何与视觉完整复刻原 XSlider 实现（凹槽 Base+Dark 描边、
 *  handle 中心前 Highlight 子页、Button 把手 Light/Dark 凸起边）。
 */
static void xcs_drawSlider(XStyle* self, const XStyleOption* option,
                           XPainter* painter, const XWidget* widget)
{
    uint32_t baseC;
    uint32_t dark;
    uint32_t light;
    uint32_t button;
    uint32_t highlight;
    XRect r;
    int handlePos;
    XRect groove;
    XRect handle;
    int range;
    double frac;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    if (r.width <= 2 || r.height <= 2) return;
    baseC     = xcs_color(option, XPaletteColorRole_Base);
    dark      = xcs_color(option, XPaletteColorRole_Dark);
    light     = xcs_color(option, XPaletteColorRole_Light);
    button    = xcs_color(option, XPaletteColorRole_Button);
    highlight = xcs_color(option, XPaletteColorRole_Highlight);
    if (baseC == 0) baseC = 0xFFFFFFFFu;
    if (dark == 0) dark = 0xFF808080u;
    if (light == 0) light = 0xFFE0E0E0u;
    if (button == 0) button = 0xFFCFCFCFu;
    if (highlight == 0) highlight = 0xFF2A82DAu;
    range = option->m_sliderMax - option->m_sliderMin;
    frac = (range > 0)
        ? (double)(option->m_sliderValue - option->m_sliderMin) / range : 0.0;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    if (option->m_horizontal) {
        int avail = r.width - 16;
        handlePos = 8 + (int)(avail * frac);
        groove.x = r.x + 1; groove.y = r.y + (r.height - 5) / 2;
        groove.width = r.width - 2; groove.height = 5;
        handle.width = 16; handle.height = r.height - 2;
        handle.x = handlePos - handle.width / 2;
        handle.y = r.y + 1;
    } else {
        int avail = r.height - 16;
        handlePos = 8 + (int)(avail * (1.0 - frac));
        groove.x = r.x + (r.width - 5) / 2; groove.y = r.y + 1;
        groove.width = 5; groove.height = r.height - 2;
        handle.width = r.width - 2; handle.height = 16;
        handle.x = r.x + 1;
        handle.y = handlePos - handle.height / 2;
    }
    /* 凹槽：Base 底 + Dark 1px 边框。 */
    XPainter_fillRect(painter, &groove, baseC);
    XPainter_fillRect(painter, &(XRect){groove.x, groove.y,
                                        groove.width, 1}, dark);
    XPainter_fillRect(painter, &(XRect){groove.x,
                                        groove.y + groove.height - 1,
                                        groove.width, 1}, dark);
    XPainter_fillRect(painter, &(XRect){groove.x, groove.y, 1,
                                        groove.height}, dark);
    XPainter_fillRect(painter, &(XRect){groove.x + groove.width - 1,
                                        groove.y, 1, groove.height}, dark);
    /* 子页高亮：从凹槽起点到 handle 中心。 */
    if (option->m_horizontal) {
        int wgt = handlePos - groove.x;
        if (wgt > 0) {
            if (wgt > groove.width) wgt = groove.width;
            XPainter_fillRect(painter, &(XRect){groove.x, groove.y,
                                                wgt, groove.height},
                              highlight);
        }
    } else {
        int hgt = (groove.y + groove.height) - handlePos;
        if (hgt > 0) {
            if (hgt > groove.height) hgt = groove.height;
            XPainter_fillRect(painter, &(XRect){groove.x,
                                                groove.y + groove.height -
                                                    hgt,
                                                groove.width, hgt},
                              highlight);
        }
    }
    /* 刻度（tickInterval 0=自动 pageStep→singleStep→1；位置与把手一致）。 */
    if (option->m_sliderTickPosition != 0) {
        int interval = option->m_sliderTickInterval;
        int avail = option->m_horizontal ? r.width - 16 : r.height - 16;
        int v;
        if (interval <= 0)
            interval = option->m_sliderPageStep > 0
                ? option->m_sliderPageStep
                : (option->m_sliderSingleStep > 0
                       ? option->m_sliderSingleStep : 1);
        for (v = option->m_sliderMin; v <= option->m_sliderMax;
             v += interval) {
            double fv = (range > 0)
                ? (double)(v - option->m_sliderMin) / range : 0.0;
            int pos = 8 + (int)(avail *
                (option->m_horizontal ? fv : 1.0 - fv));
            if (option->m_horizontal) {
                if (option->m_sliderTickPosition & 1)
                    XPainter_fillRect(painter,
                        &(XRect){pos - 1, r.y + 1, 3, 3}, dark);
                if (option->m_sliderTickPosition & 2)
                    XPainter_fillRect(painter,
                        &(XRect){pos - 1, r.y + r.height - 4, 3, 3}, dark);
            } else {
                if (option->m_sliderTickPosition & 1)
                    XPainter_fillRect(painter,
                        &(XRect){r.x + 1, pos - 1, 3, 3}, dark);
                if (option->m_sliderTickPosition & 2)
                    XPainter_fillRect(painter,
                        &(XRect){r.x + r.width - 4, pos - 1, 3, 3}, dark);
            }
        }
    }
    /* 把手：Button 底 + Light 顶/左 + Dark 底/右（凸起）。 */
    if (handle.x < r.x) handle.x = r.x;
    if (handle.y < r.y) handle.y = r.y;
    XPainter_fillRect(painter, &handle, button);
    XPainter_fillRect(painter, &(XRect){handle.x, handle.y,
                                        handle.width, 1}, light);
    XPainter_fillRect(painter, &(XRect){handle.x, handle.y, 1,
                                        handle.height}, light);
    XPainter_fillRect(painter, &(XRect){handle.x + handle.width - 1,
                                        handle.y, 1, handle.height}, dark);
    XPainter_fillRect(painter, &(XRect){handle.x,
                                        handle.y + handle.height - 1,
                                        handle.width, 1}, dark);
}


/** @brief 绘制滚动条（CC_ScrollBar：完整对标 QFusionStyle 非 transient 路径）。
 *
 *  groove：buttonColor.darker(107/105/105/107) 四点垂直渐变 +
 *  alphaOutline 顶边线 + subtleEdge(alpha 40) 内框；
 *  slider：gradientStart(lighter 124)→gradientStop(lighter 102) 渐变、
 *  hover highlightedGradient（start.darker(102)/stop.lighter(102)）、
 *  sunken midColor2(merged 40)、alphaOutline 外框 + innerContrastLine
 *  (白 alpha 30) 内框。滑块几何由 min/max/pageStep/value 推算（同
 *  XScrollBar 公式）。
 */
static void xcs_drawScrollBar(XStyle* self, const XStyleOption* option,
                              XPainter* painter, const XWidget* widget)
{
    uint32_t buttonC;
    uint32_t buttonColor;
    uint32_t gradStart;
    uint32_t gradStop;
    uint32_t windowC;
    uint32_t outline;
    uint32_t alphaOutline;
    uint32_t subtleEdge;
    uint32_t contrastLine;
    XRect rect;
    XRect groove;
    XRect slider;
    int contentLen;
    int range;
    int page;
    int handleLen;
    int handlePos;
    int travel;
    int i;
    bool horizontal;
    bool sunken;
    bool hover;
    (void)widget;
    if (!option || !painter) return;
    rect = option->m_rect;
    if (rect.width <= 2 || rect.height <= 2) return;
    horizontal = option->m_horizontal;
    sunken = (option->m_state & XStyleState_Sunken) != 0;
    hover = (option->m_state & XStyleState_MouseOver) != 0;
    buttonC    = xcs_color(option, XPaletteColorRole_Button);
    windowC    = xcs_color(option, XPaletteColorRole_Window);
    if (buttonC == 0) buttonC = 0xFFCFCFCFu;
    if (windowC == 0) windowC = 0xFFCFCFCFu;
    buttonColor = xcs_buttonColor(buttonC);
    gradStart = xcs_lighter(buttonColor, 124);
    gradStop = xcs_lighter(buttonColor, 102);
    outline = xcs_darker(windowC, 140);
    alphaOutline = (outline & 0x00FFFFFFu) | (180u << 24);
    subtleEdge = (outline & 0x00FFFFFFu) | (40u << 24);
    contrastLine = 0x1EFFFFFFu; /* 白 alpha 30。 */
    contentLen = horizontal ? rect.width : rect.height;
    range = option->m_sliderMax - option->m_sliderMin;
    page = option->m_sliderPageStep;
    if (range <= 0) handleLen = contentLen;
    else {
        handleLen = contentLen * page / (range + page);
        if (handleLen < 16) handleLen = 16;
        if (handleLen > contentLen) handleLen = contentLen;
    }
    travel = contentLen - handleLen;
    handlePos = (range > 0 && travel > 0)
        ? (option->m_sliderValue - option->m_sliderMin) * travel / range : 0;
    /* groove 矩形（横条垂直居中 8px，与 XScrollBar 布局一致；
       显示按钮时扣除两端按钮区）。 */
    {
        int btn = (option->m_scrollSubLine || option->m_scrollAddLine)
            ? 16 : 0;
        if (horizontal) {
            XRect_init(&groove, btn, (rect.height - 8) / 2,
                       rect.width - btn * 2, 8);
        } else {
            XRect_init(&groove, (rect.width - 8) / 2, btn,
                       8, rect.height - btn * 2);
        }
    }
    /* 1) groove 渐变（darker 107/105/105/107 四点）。 */
    for (i = 0; i < (horizontal ? groove.height : groove.width); ++i) {
        double t = (double)i / (double)((horizontal ? groove.height
                                        : groove.width) - 1);
        uint32_t c;
        if (t < 0.1)
            c = xcs_merged(xcs_darker(buttonColor, 107),
                           xcs_darker(buttonColor, 105),
                           (int)(t / 0.1 * 100));
        else if (t < 0.9)
            c = xcs_darker(buttonColor, 105);
        else
            c = xcs_merged(xcs_darker(buttonColor, 105),
                           xcs_darker(buttonColor, 107),
                           (int)((t - 0.9) / 0.1 * 100));
        if (horizontal)
            XPainter_fillRect(painter, &(XRect){groove.x, groove.y + i,
                                                groove.width, 1}, c);
        else
            XPainter_fillRect(painter, &(XRect){groove.x + i, groove.y,
                                                1, groove.height}, c);
    }
    /* 2) alphaOutline 顶/左边线。 */
    if (horizontal)
        XPainter_drawLine(painter, groove.x, groove.y,
                          groove.x + groove.width - 1, groove.y);
    else
        XPainter_drawLine(painter, groove.x, groove.y,
                          groove.x, groove.y + groove.height - 1);
    /* 3) subtleEdge 内框（groove adjusted(1,0,-1,-1)）。 */
    XPainter_drawLine(painter, groove.x + 1, groove.y,
                      groove.x + groove.width - 2, groove.y);
    XPainter_drawLine(painter, groove.x + 1,
                      groove.y + groove.height - 1,
                      groove.x + groove.width - 2,
                      groove.y + groove.height - 1);
    /* 4) slider 矩形。 */
    if (horizontal)
        XRect_init(&slider, groove.x + handlePos, groove.y,
                   handleLen, groove.height);
    else
        XRect_init(&slider, groove.x, groove.y + handlePos,
                   groove.width, handleLen);
    /* 5) slider 填充：渐变/悬停/按下。 */
    for (i = 0; i < (horizontal ? slider.height : slider.width); ++i) {
        double t = (double)i / (double)((horizontal ? slider.height
                                        : slider.width) - 1);
        uint32_t c;
        if (sunken)
            c = xcs_merged(gradStart, gradStop, 40);
        else if (hover)
            c = xcs_merged(xcs_darker(gradStart, 102),
                           xcs_lighter(gradStop, 102),
                           (int)(t * 100));
        else
            c = xcs_merged(gradStart, gradStop, (int)(t * 100));
        if (horizontal)
            XPainter_fillRect(painter, &(XRect){slider.x, slider.y + i,
                                                slider.width, 1}, c);
        else
            XPainter_fillRect(painter, &(XRect){slider.x + i, slider.y,
                                                1, slider.height}, c);
    }
    /* 6) alphaOutline 外框。 */
    XPainter_drawLine(painter, slider.x, slider.y,
                      slider.x + slider.width - 1, slider.y);
    XPainter_drawLine(painter, slider.x + slider.width - 1, slider.y,
                      slider.x + slider.width - 1,
                      slider.y + slider.height - 1);
    XPainter_drawLine(painter, slider.x + slider.width - 1,
                      slider.y + slider.height - 1, slider.x,
                      slider.y + slider.height - 1);
    XPainter_drawLine(painter, slider.x, slider.y + slider.height - 1,
                      slider.x, slider.y);
    /* 6b) 两端步进按钮（SC_ScrollBarSubLine/AddLine：gradientStop 填充 +
     *     alphaOutline 边框 + 方向箭头，按下加深）。 */
    if (option->m_scrollSubLine || option->m_scrollAddLine) {
        int bw = 16;
        XRect grow = option->m_rect;
        XRect b1;
        XRect b2;
        uint32_t btnFill = option->m_scrollActiveSub
            ? xcs_merged(gradStart, gradStop, 40) : gradStop;
        if (horizontal) {
            XRect_init(&b1, grow.x, grow.y, bw, grow.height);
            XRect_init(&b2, grow.x + grow.width - bw, grow.y, bw,
                       grow.height);
        } else {
            XRect_init(&b1, grow.x, grow.y, grow.width, bw);
            XRect_init(&b2, grow.x, grow.y + grow.height - bw, grow.width,
                       bw);
        }
        if (option->m_scrollSubLine) {
            XStyleOption arrow = *option;
            XPainter_fillRect(painter, &b1, btnFill);
            XPainter_setPen(painter, alphaOutline);
            XPainter_drawLine(painter, b1.x, b1.y,
                              b1.x + b1.width - 1, b1.y);
            arrow.m_type = horizontal ? XStylePE_IndicatorArrowLeft
                                      : XStylePE_IndicatorArrowUp;
            arrow.m_rect = b1;
            xcs_drawArrow(self, &arrow, painter,
                          horizontal ? 2 : 1);
        }
        if (option->m_scrollAddLine) {
            XStyleOption arrow = *option;
            XPainter_fillRect(painter, &b2, btnFill);
            XPainter_setPen(painter, alphaOutline);
            XPainter_drawLine(painter, b2.x, b2.y,
                              b2.x + b2.width - 1, b2.y);
            arrow.m_type = horizontal ? XStylePE_IndicatorArrowRight
                                      : XStylePE_IndicatorArrowDown;
            arrow.m_rect = b2;
            xcs_drawArrow(self, &arrow, painter,
                          horizontal ? 3 : 0);
        }
    }
    /* 7) innerContrastLine 内框（adjusted(1,1,-1,-1)）。 */
    XPainter_drawLine(painter, slider.x + 1, slider.y + 1,
                      slider.x + slider.width - 2, slider.y + 1);
    XPainter_drawLine(painter, slider.x + slider.width - 2,
                      slider.y + 1, slider.x + slider.width - 2,
                      slider.y + slider.height - 2);
    XPainter_drawLine(painter, slider.x + slider.width - 2,
                      slider.y + slider.height - 2, slider.x + 1,
                      slider.y + slider.height - 2);
    XPainter_drawLine(painter, slider.x + 1, slider.y + slider.height - 2,
                      slider.x + 1, slider.y + 1);
}


/** @brief 绘制工具按钮（CC_ToolButton：AutoRaise 面板 + 图标/文本/箭头）。
 *
 *  完整对标 QCommonStyle::drawComplexControl(CC_ToolButton)：
 *  非按下/AutoRaise 时不画面板（PE_PanelButtonTool 条件分派），
 *  箭头经 PE_IndicatorArrow*，文本/图标居中组合。
 */
static void xcs_drawToolButton(XStyle* self, const XStyleOption* option,
                               XPainter* painter, const XWidget* widget)
{
    XStyleOption bevel = *option;
    bool autoRaise = false;
    bool sunken;
    bool hover;
    if (!option || !painter) return;
    autoRaise = (option->m_state & XStyleState_AutoRaise) != 0;
    sunken = (option->m_state & XStyleState_Sunken) != 0;
    hover = (option->m_state & XStyleState_MouseOver) != 0;
    bevel.m_type = XStylePE_PanelButtonTool;
    bevel.m_rect = option->m_rect;
    if (!autoRaise || sunken || (hover && !sunken)) {
        xcs_drawBarPanel(self, &bevel, painter);
    }
    /* 箭头（SP_ArrowLeft/Right 等价）：居中绘制。 */
    if (option->m_checkState == 1) { /* 复用 checkState 传箭头方向 0-3。 */
        XStyleOption arrow = *option;
        int dir = option->m_progressMin;
        int s = 5;
        XRect ar;
        uint32_t fg = xcs_buttonText(option);
        int cx = option->m_rect.x + option->m_rect.width / 2;
        int cy = option->m_rect.y + option->m_rect.height / 2;
        (void)s;
        XRect_init(&ar, cx - 4, cy - 4, 8, 8);
        arrow.m_type = XStylePE_IndicatorArrowDown;
        arrow.m_rect = ar;
        switch (dir) {
        case 0: arrow.m_type = XStylePE_IndicatorArrowUp; break;
        case 1: arrow.m_type = XStylePE_IndicatorArrowDown; break;
        case 2: arrow.m_type = XStylePE_IndicatorArrowLeft; break;
        default: arrow.m_type = XStylePE_IndicatorArrowRight; break;
        }
        XPainter_setPen(painter, fg);
        xcs_drawArrow(self, &arrow, painter, dir);
    }
}

static void VXCommonStyle_drawComplexControl(XStyle* self, int cc,
                                             const XStyleOption* option,
                                             XPainter* painter,
                                             const XWidget* widget)
{
    switch (cc) {
    case XStyleCC_SpinBox:
        xcs_drawSpinBox(self, option, painter, widget);
        break;
    case XStyleCC_Dial:
        xcs_drawDial(self, option, painter, widget);
        break;
    case XStyleCC_GroupBox:
        xcs_drawGroupBox(self, option, painter, widget);
        break;
    case XStyleCC_Slider:
        xcs_drawSlider(self, option, painter, widget);
        break;
    case XStyleCC_ScrollBar:
        xcs_drawScrollBar(self, option, painter, widget);
        break;
    case XStyleCC_ComboBox:
        xcs_drawComboBox(self, option, painter, widget);
        break;
    case XStyleCC_ToolButton:
        xcs_drawToolButton(self, option, painter, widget);
        break;
    default:
        break;
    }
}


/* ==================== 菜单/工具栏/组合框基元 ==================== */

/** @brief 菜单栏/工具栏面板（PE_PanelMenuBar/PE_PanelToolBar：
 *         Button 色填充 + 底部 1px 暗线，完整对标 qDrawShadePanel
 *         的非 sunken 分支）。 */
static void xcs_drawBarPanel(XStyle* self, const XStyleOption* option,
                             XPainter* painter)
{
    uint32_t button;
    uint32_t dark;
    XRect r;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    button = xcs_color(option, XPaletteColorRole_Button);
    dark = xcs_color(option, XPaletteColorRole_Dark);
    if (button == 0) button = 0xFFCFCFCFu;
    if (dark == 0) dark = 0xFF808080u;
    XPainter_fillRect(painter, &r, button);
    if (r.height > 1)
        XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                            r.width, 1}, dark);
}

/** @brief 菜单栏项（CE_MenuBarItem：Fusion 选中高亮框 + 底部阴影线）。
 *
 *  完整对标 QFusionStyle：选中且按下时 highlight 填充 +
 *  highlight.darker(125) 边框 + HighlightedText 居中文本；未选中时画
 *  mergedColors(window.darker(120), outline.lighter(140), 60) 底部阴影线。
 */
static void xcs_drawMenuBarItem(XStyle* self, const XStyleOption* option,
                                XPainter* painter, const XWidget* widget)
{
    uint32_t windowC;
    uint32_t highlight;
    uint32_t outline;
    uint32_t shadow;
    uint32_t textColor;
    XRect r;
    int textW;
    int textH;
    bool selected;
    bool sunken;
    bool enabled;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    windowC   = xcs_color(option, XPaletteColorRole_Window);
    highlight = xcs_color(option, XPaletteColorRole_Highlight);
    if (windowC == 0) windowC = 0xFFCFCFCFu;
    if (highlight == 0) highlight = 0xFF2A82DAu;
    outline = xcs_darker(windowC, 140);
    selected = (option->m_state & XStyleState_Selected) != 0;
    sunken = (option->m_state & XStyleState_Sunken) != 0;
    enabled = (option->m_state & XStyleState_Enabled) != 0;
    XPainter_fillRect(painter, &r, windowC);
    if (selected && sunken) {
        uint32_t hiOutline = xcs_darker(highlight, 125);
        XPainter_fillRect(painter, &r, highlight);
        XPainter_drawLine(painter, r.x, r.y, r.x + r.width - 1, r.y);
        XPainter_drawLine(painter, r.x + r.width - 1, r.y,
                          r.x + r.width - 1, r.y + r.height - 1);
        XPainter_drawLine(painter, r.x + r.width - 1, r.y + r.height - 1,
                          r.x, r.y + r.height - 1);
        XPainter_drawLine(painter, r.x, r.y + r.height - 1, r.x, r.y);
        (void)hiOutline;
        textColor = enabled
            ? xcs_color(option, XPaletteColorRole_HighlightedText)
            : xcs_color(option, XPaletteColorRole_Text);
        if (textColor == 0) textColor = 0xFFFFFFFFu;
    } else {
        /* 底部阴影线：mergedColors(window.darker(120), outline.lighter(140), 60)。 */
        shadow = xcs_merged(xcs_darker(windowC, 120),
                            xcs_lighter(outline, 140), 60);
        if (r.height > 1)
            XPainter_drawLine(painter, r.x, r.y + r.height - 1,
                              r.x + r.width - 1, r.y + r.height - 1);
        {
            /* 用阴影色重画该线（drawLine 用当前画笔色）。 */
            XPainter_setPen(painter, shadow);
            XPainter_drawLine(painter, r.x, r.y + r.height - 1,
                              r.x + r.width - 1, r.y + r.height - 1);
        }
        textColor = xcs_color(option, XPaletteColorRole_WindowText);
        if (textColor == 0) textColor = 0xFF000000u;
        if (!enabled) textColor = xcs_color(option, XPaletteColorRole_Mid);
    }
    if (option->m_text && option->m_text[0]) {
        textW = XPainter_textWidth(XPainter_font(painter), option->m_text);
        textH = XPainter_textHeight(XPainter_font(painter));
        if (textH < 14) textH = 14;
        XPainter_drawText(painter, r.x + (r.width - textW) / 2,
                          r.y + (r.height - textH) / 2 + textH - 4,
                          option->m_text, textColor);
    }
}

/** @brief 弹出菜单项（CE_MenuItem：选中 highlight 填充 + 文本）。 */
static void xcs_drawMenuItem(XStyle* self, const XStyleOption* option,
                             XPainter* painter, const XWidget* widget)
{
    uint32_t windowC;
    uint32_t highlight;
    uint32_t textColor;
    XRect r;
    bool selected;
    bool enabled;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    windowC   = xcs_color(option, XPaletteColorRole_Window);
    highlight = xcs_color(option, XPaletteColorRole_Highlight);
    if (windowC == 0) windowC = 0xFFEFEFEFu;
    if (highlight == 0) highlight = 0xFF2A82DAu;
    selected = (option->m_state & XStyleState_Selected) != 0;
    enabled = (option->m_state & XStyleState_Enabled) != 0;
    XPainter_fillRect(painter, &r, selected ? highlight : windowC);
    if (selected)
        XPainter_fillRect(painter, &r, highlight);
    textColor = selected
        ? xcs_color(option, XPaletteColorRole_HighlightedText)
        : xcs_color(option, XPaletteColorRole_WindowText);
    if (textColor == 0) textColor = selected ? 0xFFFFFFFFu : 0xFF000000u;
    if (!enabled) textColor = xcs_color(option, XPaletteColorRole_Mid);
    if (option->m_text && option->m_text[0]) {
        int textH = XPainter_textHeight(XPainter_font(painter));
        if (textH < 14) textH = 14;
        XPainter_drawText(painter, r.x + 4,
                          r.y + (r.height - textH) / 2 + textH - 4,
                          option->m_text, textColor);
    }
}

/** @brief 组合框（CC_ComboBox：面板 + 下拉箭头，完整对标
 *         QCommonStyle 的 PE_PanelLineEdit + PE_IndicatorArrowDown 组合）。 */
static void xcs_drawComboBox(XStyle* self, const XStyleOption* option,
                             XPainter* painter, const XWidget* widget)
{
    XRect r;
    XRect arrowR;
    int arrowW = 16;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    if (r.width <= 2 || r.height <= 2) return;
    {
        XStyleOption frame = *option;
        frame.m_type = XStylePE_PanelLineEdit;
        frame.m_rect = r;
        xcs_drawFrame(self, &frame, painter);
    }
    /* 下拉箭头区（右端 16px，Button 面板 + 下箭头）。 */
    if (r.width > arrowW + 4) {
        XStyleOption arrow;
        XRect_init(&arrowR, r.x + r.width - arrowW, r.y,
                   arrowW, r.height);
        arrow = *option;
        arrow.m_type = XStylePE_IndicatorArrowDown;
        arrow.m_rect = arrowR;
        XPainter_fillRect(painter, &arrowR,
                          xcs_color(option, XPaletteColorRole_Button));
        xcs_drawArrow(self, &arrow, painter, 0);
    }
}


/** @brief 绘制停靠窗标题（CE_DockWidgetTitle：highlight 标题条 +
 *         左对齐标题文本 + 底部分隔线）。 */
static void xcs_drawDockTitle(XStyle* self, const XStyleOption* option,
                              XPainter* painter, const XWidget* widget)
{
    uint32_t highlight;
    uint32_t highlightedText;
    uint32_t windowText;
    XRect r;
    int textH;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    highlight = xcs_color(option, XPaletteColorRole_Highlight);
    highlightedText = xcs_color(option, XPaletteColorRole_HighlightedText);
    windowText = xcs_color(option, XPaletteColorRole_WindowText);
    if (highlight == 0) highlight = 0xFF2A82DAu;
    if (highlightedText == 0) highlightedText = 0xFFFFFFFFu;
    if (windowText == 0) windowText = 0xFF000000u;
    XPainter_fillRect(painter, &r, highlight);
    if (option->m_text && option->m_text[0]) {
        textH = XPainter_textHeight(XPainter_font(painter));
        if (textH < 14) textH = 14;
        XPainter_drawText(painter, r.x + 6,
                          r.y + (r.height - textH) / 2 + textH - 4,
                          option->m_text, highlightedText);
    }
    XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height,
                                        r.width, 1}, windowText);
}


/** @brief 绘制分隔条（CE_Splitter：Mid 底 + 中央把手点线）。 */
static void xcs_drawSplitter(XStyle* self, const XStyleOption* option,
                             XPainter* painter, const XWidget* widget)
{
    uint32_t mid;
    uint32_t light;
    XRect r;
    int i;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    mid = xcs_color(option, XPaletteColorRole_Mid);
    light = xcs_color(option, XPaletteColorRole_Light);
    if (mid == 0) mid = 0xFFA0A0A0u;
    if (light == 0) light = 0xFFE0E0E0u;
    XPainter_fillRect(painter, &r, mid);
    if (option->m_horizontal) {
        /* 垂直分隔条（水平布局）：中央竖排 3 点。 */
        for (i = -1; i <= 1; ++i)
            XPainter_fillRect(painter, &(XRect){r.x + r.width / 2 - 1,
                                                r.y + r.height / 2 + i * 4 - 1,
                                                3, 3}, light);
    } else {
        for (i = -1; i <= 1; ++i)
            XPainter_fillRect(painter, &(XRect){r.x + r.width / 2 + i * 4 - 1,
                                                r.y + r.height / 2 - 1,
                                                3, 3}, light);
    }
}

/** @brief 绘制尺寸手柄（CE_SizeGrip：右下角斜点阵）。 */
static void xcs_drawSizeGrip(XStyle* self, const XStyleOption* option,
                             XPainter* painter, const XWidget* widget)
{
    uint32_t mid;
    uint32_t light;
    XRect r;
    int i;
    int j;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    mid = xcs_color(option, XPaletteColorRole_Mid);
    light = xcs_color(option, XPaletteColorRole_Light);
    if (mid == 0) mid = 0xFFA0A0A0u;
    if (light == 0) light = 0xFFE0E0E0u;
    XPainter_fillRect(painter, &r, mid);
    /* 斜点阵（对标 QStyle::drawPrimitive CE_SizeGrip 的 dot matrix）。 */
    for (i = 0; i < 4; ++i)
        for (j = 0; j <= i; ++j) {
            int x = r.x + r.width - 4 - i * 4;
            int y = r.y + r.height - 4 - j * 4;
            XPainter_fillRect(painter, &(XRect){x, y, 2, 2}, light);
        }
}

/** @brief 绘制橡皮筋（CE_RubberBand：半透明蓝填充 + 边框）。 */
static void xcs_drawRubberBand(XStyle* self, const XStyleOption* option,
                               XPainter* painter, const XWidget* widget)
{
    uint32_t highlight;
    uint32_t fill;
    XRect r;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    highlight = xcs_color(option, XPaletteColorRole_Highlight);
    if (highlight == 0) highlight = 0xFF2A82DAu;
    /* 半透明填充（alpha 60）+ 不透明边框（对标
       QStyle::CE_RubberBand 的 alpha blend）。 */
    fill = (highlight & 0x00FFFFFFu) | (60u << 24);
    XPainter_fillRect(painter, &r, fill);
    XPainter_drawLine(painter, r.x, r.y, r.x + r.width - 1, r.y);
    XPainter_drawLine(painter, r.x + r.width - 1, r.y,
                      r.x + r.width - 1, r.y + r.height - 1);
    XPainter_drawLine(painter, r.x + r.width - 1, r.y + r.height - 1,
                      r.x, r.y + r.height - 1);
    XPainter_drawLine(painter, r.x, r.y + r.height - 1, r.x, r.y);
}

/** @brief 绘制工具箱页（CE_ToolBoxTab：highlight 头 + 文本 + 边框）。 */
static void xcs_drawToolBoxTab(XStyle* self, const XStyleOption* option,
                               XPainter* painter, const XWidget* widget)
{
    uint32_t highlight;
    uint32_t highlightedText;
    uint32_t windowText;
    XRect r;
    int textH;
    bool selected;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    highlight = xcs_color(option, XPaletteColorRole_Highlight);
    highlightedText = xcs_color(option, XPaletteColorRole_HighlightedText);
    windowText = xcs_color(option, XPaletteColorRole_WindowText);
    if (highlight == 0) highlight = 0xFF2A82DAu;
    if (highlightedText == 0) highlightedText = 0xFFFFFFFFu;
    if (windowText == 0) windowText = 0xFF000000u;
    selected = option->m_tabSelected;
    XPainter_fillRect(painter, &r, selected ? highlight
                                            : xcs_color(option, XPaletteColorRole_Button));
    if (option->m_text && option->m_text[0]) {
        textH = XPainter_textHeight(XPainter_font(painter));
        if (textH < 14) textH = 14;
        XPainter_drawText(painter, r.x + 6,
                          r.y + (r.height - textH) / 2 + textH - 4,
                          option->m_text, selected ? highlightedText
                                                   : windowText);
    }
    XPainter_drawLine(painter, r.x, r.y + r.height - 1,
                      r.x + r.width - 1, r.y + r.height - 1);
}


/** @brief 绘制表头段（CE_HeaderSection：Button 底 + 边框）。 */
static void xcs_drawHeaderSection(XStyle* self, const XStyleOption* option,
                                  XPainter* painter, const XWidget* widget)
{
    uint32_t button;
    uint32_t mid;
    XRect r;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    button = xcs_color(option, XPaletteColorRole_Button);
    mid = xcs_color(option, XPaletteColorRole_Mid);
    if (button == 0) button = 0xFFCFCFCFu;
    if (mid == 0) mid = 0xFFA0A0A0u;
    XPainter_fillRect(painter, &r, button);
    XPainter_drawLine(painter, r.x, r.y + r.height - 1,
                      r.x + r.width - 1, r.y + r.height - 1);
    XPainter_drawLine(painter, r.x + r.width - 1, r.y,
                      r.x + r.width - 1, r.y + r.height - 1);
    (void)mid;
}

/** @brief 绘制表头标签（CE_HeaderLabel：居中文本）。 */
static void xcs_drawHeaderLabel(XStyle* self, const XStyleOption* option,
                                XPainter* painter, const XWidget* widget)
{
    uint32_t textColor;
    XRect r;
    int textW;
    int textH;
    (void)widget;
    if (!option || !painter) return;
    r = option->m_rect;
    textColor = xcs_color(option, XPaletteColorRole_WindowText);
    if (textColor == 0) textColor = 0xFF000000u;
    if (option->m_text && option->m_text[0]) {
        textW = XPainter_textWidth(XPainter_font(painter), option->m_text);
        textH = XPainter_textHeight(XPainter_font(painter));
        if (textH < 14) textH = 14;
        XPainter_drawText(painter, r.x + (r.width - textW) / 2,
                          r.y + (r.height - textH) / 2 + textH - 4,
                          option->m_text, textColor);
    }
}

static void VXCommonStyle_drawControl(XStyle* self, int ce,
                                      const XStyleOption* option,
                                      XPainter* painter,
                                      const XWidget* widget)
{
    switch (ce) {
    case XStyleCE_PushButton:
    case XStyleCE_PushButtonBevel:
        xcs_drawPushButton(self, option, painter, widget);
        break;
    case XStyleCE_CheckBox:
        xcs_drawCheckable(self, XStyleCE_CheckBox, option, painter);
        break;
    case XStyleCE_RadioButton:
        xcs_drawCheckable(self, XStyleCE_RadioButton, option, painter);
        break;
    case XStyleCE_ProgressBarGroove:
        xcs_drawProgressGroove(self, option, painter);
        break;
    case XStyleCE_ProgressBarContents:
        xcs_drawProgressContents(self, option, painter);
        break;
    case XStyleCE_TabBarTabShape:
        xcs_drawTabShape(self, option, painter);
        break;
    case XStyleCE_TabBarTabLabel:
        xcs_drawTabLabel(self, option, painter, widget);
        break;
    case XStyleCE_MenuBarItem:
        xcs_drawMenuBarItem(self, option, painter, widget);
        break;
    case XStyleCE_MenuItem:
        xcs_drawMenuItem(self, option, painter, widget);
        break;
    case XStyleCE_DockWidgetTitle:
        xcs_drawDockTitle(self, option, painter, widget);
        break;
    case XStyleCE_Splitter:
        xcs_drawSplitter(self, option, painter, widget);
        break;
    case XStyleCE_SizeGrip:
        xcs_drawSizeGrip(self, option, painter, widget);
        break;
    case XStyleCE_RubberBand:
        xcs_drawRubberBand(self, option, painter, widget);
        break;
    case XStyleCE_ToolBoxTab:
        xcs_drawToolBoxTab(self, option, painter, widget);
        break;
    case XStyleCE_HeaderSection:
        xcs_drawHeaderSection(self, option, painter, widget);
        break;
    case XStyleCE_HeaderLabel:
        xcs_drawHeaderLabel(self, option, painter, widget);
        break;
    default:
        break;
    }
}

static int VXCommonStyle_pixelMetric(XStyle* self, int pm,
                                     const XStyleOption* option)
{
    (void)self;
    (void)option;
    switch (pm) {
    case XStylePM_ButtonMargin: return 6;
    case XStylePM_ButtonIconSize: return 16;
    case XStylePM_ButtonShiftHorizontal: return 0;
    case XStylePM_ButtonShiftVertical: return 0;
    case XStylePM_CheckBoxLabelSpacing: return 6;
    case XStylePM_RadioButtonLabelSpacing: return 6;
    case XStylePM_IndicatorWidth: return 13;
    case XStylePM_IndicatorHeight: return 13;
    case XStylePM_DefaultFrameWidth: return 2;
    case XStylePM_ProgressBarChunkWidth: return 9;
    case XStylePM_MenuBarItemSpacing: return 0;
    case XStylePM_ToolBarHandleExtent: return 10;
    case XStylePM_ToolBarSeparatorExtent: return 6;
    case XStylePM_ToolBarItemSpacing: return 1;
    case XStylePM_TabBarTabOverlap: return 0;
    case XStylePM_TabBarBaseHeight: return 2;
    case XStylePM_TabBarTabHSpace: return 12;
    case XStylePM_TabBarTabVSpace: return 6;
    case XStylePM_ScrollBarExtent: return 16;
    case XStylePM_SplitterWidth: return 5;
    case XStylePM_DockWidgetTitleBarButtonMargin: return 4;
    default: return 0;
    }
}

static XSize VXCommonStyle_sizeFromContents(XStyle* self, int ct,
                                            const XStyleOption* option,
                                            XSize contentSize)
{
    XSize result;
    int margin = 0;
    (void)self;
    switch (ct) {
    case 0: /* 按钮 */
        margin = XStyle_pixelMetric((XStyle*)self, XStylePM_ButtonMargin,
                                    option);
        break;
    case 1: /* 复选 */
    case 2: /* 单选 */
        margin = XStyle_pixelMetric((XStyle*)self,
                                    XStylePM_CheckBoxLabelSpacing, option);
        break;
    default:
        break;
    }
    XSize_init(&result, contentSize.width + margin * 2,
               contentSize.height + margin);
    return result;
}

XVtable* XCommonStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCommonStyle)
    XVTABLE_INHERIT_XCLASS(XStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawPrimitive,
                             VXCommonStyle_drawPrimitive);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawControl,
                             VXCommonStyle_drawControl);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_PixelMetric,
                             VXCommonStyle_pixelMetric);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_SizeFromContents,
                             VXCommonStyle_sizeFromContents);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawComplexControl,
                             VXCommonStyle_drawComplexControl);
    return XVTABLE_DEFAULT;
}

void XCommonStyle_init(XCommonStyle* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XStyle_init(&self->m_base);
    XClassSetVtable(self, XCommonStyle);
}

XCommonStyle* XCommonStyle_create_ex(XMemoryType memory)
{
    XCommonStyle* self = (XCommonStyle*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XCommonStyle_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XSTYLE_ON */
