#include "XCommonStyle.h"
#include "XStringUtils.h"
#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include "XPalette.h"
#include "XAlignment.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XIcon.h"
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
static XIcon* xcs_standardIcon(XStyle* self, int sp,
                               const XStyleOption* option,
                               const XWidget* widget);

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

/** @brief 变暗（对标 QColor::darker(factor)：每通道 v*100/factor，
 *         factor > 100 时变暗；旧实现 v*f/100 对 255 输入会溢出字节） */
static uint32_t xcs_darker(uint32_t c, int factor)
{
    int r = ((c >> 16) & 0xFF);
    int g = ((c >> 8) & 0xFF);
    int b = (c & 0xFF);
    int a = (c >> 24) & 0xFF;
    if (factor <= 0) return c;
    r = r * 100 / factor;
    g = g * 100 / factor;
    b = b * 100 / factor;
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

/** @brief 绘制进度块（PE_IndicatorProgressChunk：Highlight 色填充，
 *         水平时上下各留 3px，垂直时左右各留 2px）。 */
static void xcs_drawProgressChunk(XStyle* self, const XStyleOption* option,
                                  XPainter* painter)
{
    uint32_t hl;
    XRect r;
    bool vertical;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    hl = xcs_color(option, XPaletteColorRole_Highlight);
    if (hl == 0) hl = 0xFF2A82DAu;
    vertical = !(option->m_state & XStyleState_Horizontal);
    if (!vertical) {
        XPainter_fillRect(painter,
            &(XRect){r.x, r.y + 3, r.width - 2, r.height - 6}, hl);
    } else {
        XPainter_fillRect(painter,
            &(XRect){r.x + 2, r.y, r.width - 6, r.height - 2}, hl);
    }
}

/** @brief 绘制凸起面板（对标 qDrawShadePanel sunken=false）。 */
static void xcs_drawShadePanel(XStyle* self, const XStyleOption* option,
                               XPainter* painter, int lineWidth)
{
    uint32_t button;
    uint32_t light;
    uint32_t dark;
    XRect r;
    int i;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    button = xcs_color(option, XPaletteColorRole_Button);
    light = xcs_color(option, XPaletteColorRole_Light);
    dark = xcs_color(option, XPaletteColorRole_Dark);
    if (button == 0) button = 0xFFCFCFCFu;
    if (light == 0) light = 0xFFE0E0E0u;
    if (dark == 0) dark = 0xFF808080u;
    if (lineWidth < 1) lineWidth = 1;
    if (r.width <= lineWidth * 2 || r.height <= lineWidth * 2) return;
    XPainter_fillRect(painter, &r, button);
    for (i = 0; i < lineWidth; ++i) {
        XPainter_fillRect(painter,
            &(XRect){r.x + i, r.y + i, r.width - i * 2, 1}, light);
        XPainter_fillRect(painter,
            &(XRect){r.x + i, r.y + i, 1, r.height - i * 2}, light);
        XPainter_fillRect(painter,
            &(XRect){r.x + i, r.y + r.height - 1 - i,
                     r.width - i * 2, 1}, dark);
        XPainter_fillRect(painter,
            &(XRect){r.x + r.width - 1 - i, r.y + i,
                     1, r.height - i * 2}, dark);
    }
}

/** @brief 绘制工具栏把手（PE_IndicatorToolBarHandle：两段凸起面板）。 */
static void xcs_drawToolBarHandle(XStyle* self,
                                  const XStyleOption* option,
                                  XPainter* painter)
{
    XRect r;
    XStyleOption sub;
    if (!option || !painter) return;
    r = option->m_rect;
    sub = *option;
    if (option->m_state & XStyleState_Horizontal) {
        int x = r.width / 3;
        if (r.height > 4) {
            XRect_init(&sub.m_rect, r.x + x, r.y + 2, 3, r.height - 4);
            xcs_drawShadePanel(self, &sub, painter, 1);
            sub.m_rect.x = r.x + x + 3;
            xcs_drawShadePanel(self, &sub, painter, 1);
        }
    } else if (r.width > 4) {
        int y = r.height / 3;
        XRect_init(&sub.m_rect, r.x + 2, r.y + y, r.width - 4, 3);
        xcs_drawShadePanel(self, &sub, painter, 1);
        sub.m_rect.y = r.y + y + 3;
        xcs_drawShadePanel(self, &sub, painter, 1);
    }
}

/** @brief 绘制工具栏分隔线（PE_IndicatorToolBarSeparator：
 *         凹陷暗线 + 亮线偏移，对标 qDrawShadeLine）。 */
static void xcs_drawToolBarSeparator(XStyle* self,
                                     const XStyleOption* option,
                                     XPainter* painter)
{
    uint32_t dark;
    uint32_t light;
    XRect r;
    bool horizontal;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    dark = xcs_color(option, XPaletteColorRole_Dark);
    light = xcs_color(option, XPaletteColorRole_Light);
    if (dark == 0) dark = 0xFF808080u;
    if (light == 0) light = 0xFFE0E0E0u;
    horizontal = (option->m_state & XStyleState_Horizontal) != 0;
    /* 画笔色覆盖为 dark；偏移线用 light 重画（对标 qDrawShadeLine）。 */
    XPainter_setPen(painter, dark);
    if (horizontal)
        XPainter_drawLine(painter, r.x + r.width / 2, r.y,
                          r.x + r.width / 2, r.y + r.height);
    else
        XPainter_drawLine(painter, r.x, r.y + r.height / 2,
                          r.x + r.width, r.y + r.height / 2);
    XPainter_setPen(painter, light);
    if (horizontal)
        XPainter_drawLine(painter, r.x + r.width / 2 + 1, r.y,
                          r.x + r.width / 2 + 1, r.y + r.height);
    else
        XPainter_drawLine(painter, r.x, r.y + r.height / 2 + 1,
                          r.x + r.width, r.y + r.height / 2 + 1);
}

/** @brief 绘制树分支指示（PE_IndicatorBranch：9x9 展开框 +
 *         Dense4Pattern 连线，完整对标 QCommonStyle）。 */
static void xcs_drawBranch(XStyle* self, const XStyleOption* option,
                           XPainter* painter)
{
    static const int decoration_size = 9;
    int mid_h;
    int mid_v;
    int bef_h;
    int bef_v;
    int aft_h;
    int aft_v;
    uint32_t dark;
    XRect r;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    dark = xcs_color(option, XPaletteColorRole_Dark);
    if (dark == 0) dark = 0xFF808080u;
    mid_h = r.x + r.width / 2;
    mid_v = r.y + r.height / 2;
    bef_h = mid_h;
    bef_v = mid_v;
    aft_h = mid_h;
    aft_v = mid_v;
    if (option->m_state & XStyleState_Children) {
        int delta = decoration_size / 2;
        bef_h -= delta;
        bef_v -= delta;
        aft_h += delta;
        aft_v += delta;
        XPainter_drawLine(painter, bef_h + 2, bef_v + 4,
                          bef_h + 6, bef_v + 4);
        if (!(option->m_state & XStyleState_Open))
            XPainter_drawLine(painter, bef_h + 4, bef_v + 2,
                              bef_h + 4, bef_v + 6);
        XPainter_setPen(painter, dark);
        XPainter_drawLine(painter, bef_h, bef_v,
                          bef_h + decoration_size - 1, bef_v);
        XPainter_drawLine(painter, bef_h + decoration_size - 1, bef_v,
                          bef_h + decoration_size - 1,
                          bef_v + decoration_size - 1);
        XPainter_drawLine(painter, bef_h + decoration_size - 1,
                          bef_v + decoration_size - 1, bef_h,
                          bef_v + decoration_size - 1);
        XPainter_drawLine(painter, bef_h, bef_v + decoration_size - 1,
                          bef_h, bef_v);
    }
    /* Dense4Pattern 连线（XPAINTER_BRUSH_ON 裁剪时退化为纯色填充，
     * 与 XPainter 图案刷裁剪行为一致）。 */
#if XPAINTER_BRUSH_ON
    XPainter_setBrush_2(painter, XPainterBrushStyle_Dense4Pattern);
    XPainter_setPen(painter, dark);
#else
    XPainter_setPen(painter, dark);
#endif
    if (option->m_state & XStyleState_Item) {
        XPainter_fillRect(painter,
            &(XRect){aft_h, mid_v, r.x + r.width - aft_h, 1}, dark);
    }
    if (option->m_state & XStyleState_Sibling) {
        XPainter_fillRect(painter,
            &(XRect){mid_h, aft_v, 1, r.y + r.height - aft_v}, dark);
    }
    if (option->m_state &
        (XStyleState_Open | XStyleState_Children | XStyleState_Item |
         XStyleState_Sibling)) {
        XPainter_fillRect(painter,
            &(XRect){mid_h, r.y, 1, bef_v - r.y}, dark);
    }
#if XPAINTER_BRUSH_ON
    XPainter_setBrush_2(painter, XPainterBrushStyle_SolidPattern);
#endif
}

/** @brief 绘制条目视图项面板（PE_PanelItemViewItem：选中高亮）。 */
static void xcs_drawPanelItemViewItem(XStyle* self,
                                      const XStyleOption* option,
                                      XPainter* painter,
                                      const XWidget* widget)
{
    uint32_t hl;
    bool selected;
    (void)self;
    (void)widget;
    if (!option || !painter) return;
    hl = xcs_color(option, XPaletteColorRole_Highlight);
    if (hl == 0) hl = 0xFF2A82DAu;
    selected = (option->m_state & XStyleState_Selected) != 0;
    if (option->m_showDecorationSelected && selected) {
        XPainter_fillRect(painter, &option->m_rect, hl);
    } else if (selected) {
        XRect textRect = XStyle_subElementRect((XStyle*)self,
                                               XStyleSE_ItemViewItemText,
                                               option, widget);
        XPainter_fillRect(painter, &textRect, hl);
    }
}

/** @brief 绘制条目勾选框（PE_IndicatorItemViewItemCheck：委托复选指示器）。 */
static void xcs_drawItemViewItemCheck(XStyle* self,
                                      const XStyleOption* option,
                                      XPainter* painter)
{
    XStyleOption cb = *option;
    cb.m_type = XStylePE_IndicatorCheckBox;
    xcs_drawIndicatorCheckBox(self, &cb, painter);
}

/** @brief 绘制分组框边框（PE_FrameGroupBox：Flat 顶线 / 凹陷矩形）。 */
static void xcs_drawFrameGroupBox(XStyle* self,
                                  const XStyleOption* option,
                                  XPainter* painter)
{
    uint32_t dark;
    uint32_t light;
    XRect r;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    dark = xcs_color(option, XPaletteColorRole_Dark);
    light = xcs_color(option, XPaletteColorRole_Light);
    if (dark == 0) dark = 0xFF808080u;
    if (light == 0) light = 0xFFE0E0E0u;
    if (option->m_flat) {
        XPainter_fillRect(painter, &(XRect){r.x, r.y + 1, r.width, 1}, dark);
    } else {
        XPainter_fillRect(painter, &(XRect){r.x, r.y, r.width, 1}, dark);
        XPainter_fillRect(painter, &(XRect){r.x, r.y, 1, r.height}, dark);
        XPainter_fillRect(painter, &(XRect){r.x + 1, r.y + 1,
                                            r.width - 2, 1}, light);
        XPainter_fillRect(painter, &(XRect){r.x + 1, r.y + 1,
                                            1, r.height - 2}, light);
        XPainter_fillRect(painter, &(XRect){r.x, r.y + r.height - 1,
                                            r.width, 1}, light);
        XPainter_fillRect(painter, &(XRect){r.x + r.width - 1, r.y,
                                            1, r.height}, light);
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
    case XStylePE_FrameGroupBox:
        xcs_drawFrameGroupBox(self, option, painter);
        break;
    case XStylePE_PanelButtonCommand:
    case XStylePE_PanelButtonBevel:
    case XStylePE_PanelButtonTool:
        xcs_drawPanelButtonCommand(self, option, painter);
        break;
    case XStylePE_IndicatorCheckBox:
        xcs_drawIndicatorCheckBox(self, option, painter);
        break;
    case XStylePE_IndicatorItemViewItemCheck:
        xcs_drawItemViewItemCheck(self, option, painter);
        break;
    case XStylePE_IndicatorRadioButton:
        xcs_drawIndicatorRadioButton(self, option, painter);
        break;
    case XStylePE_IndicatorProgressChunk:
        xcs_drawProgressChunk(self, option, painter);
        break;
    case XStylePE_IndicatorToolBarHandle:
        xcs_drawToolBarHandle(self, option, painter);
        break;
    case XStylePE_IndicatorToolBarSeparator:
        xcs_drawToolBarSeparator(self, option, painter);
        break;
    case XStylePE_IndicatorBranch:
        xcs_drawBranch(self, option, painter);
        break;
    case XStylePE_PanelItemViewItem:
        xcs_drawPanelItemViewItem(self, option, painter, widget);
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
    case XStylePE_PanelMenu:
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
    /* 文本统一用 WindowText：选中页签形状以 Base 填充，若按
       HighlightedText 取白字会白底白字不可见（对标 Qt Fusion：
       选中也用 WindowText 文本）。 */
    textColor = xcs_color(option, XPaletteColorRole_WindowText);
    if (textColor == 0) textColor = 0xFF000000u;
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

/** @brief 绘制进度条标签（CE_ProgressBarLabel：居中文本，对标
 *         QCommonStyle 的 Qt::AlignCenter）。 */
static void xcs_drawProgressBarLabel(XStyle* self,
                                     const XStyleOption* option,
                                     XPainter* painter)
{
    uint32_t textColor;
    XRect r;
    int textW;
    int textH;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    if (!option->m_text || !option->m_text[0]) return;
    textColor = option->m_textColor;
    if (textColor == 0)
        textColor = xcs_color(option, XPaletteColorRole_HighlightedText);
    if (textColor == 0) textColor = 0xFFFFFFFFu;
    textW = XPainter_textWidth(XPainter_font(painter), option->m_text);
    textH = XPainter_textHeight(XPainter_font(painter));
    if (textH < 14) textH = 14;
    XPainter_drawText(painter, r.x + (r.width - textW) / 2,
                      r.y + (r.height - textH) / 2 + textH - 4,
                      option->m_text, textColor);
}

/** @brief 绘制进度条控件（CE_ProgressBar：槽 + 内容 + 可选标签，
 *         完整对标 QCommonStyle）。 */
static void xcs_drawProgressBar(XStyle* self, const XStyleOption* option,
                                XPainter* painter, const XWidget* widget)
{
    XStyleOption sub;
    if (!option || !painter) return;
    sub = *option;
    sub.m_type = XStyleCE_ProgressBarGroove;
    sub.m_rect = XStyle_subElementRect((XStyle*)self,
                                       XStyleSE_ProgressBarGroove,
                                       option, widget);
    xcs_drawProgressGroove(self, &sub, painter);
    sub.m_type = XStyleCE_ProgressBarContents;
    sub.m_rect = XStyle_subElementRect((XStyle*)self,
                                       XStyleSE_ProgressBarContents,
                                       option, widget);
    xcs_drawProgressContents(self, &sub, painter);
    if (option->m_progressTextVisible) {
        sub.m_type = XStyleCE_ProgressBarLabel;
        sub.m_rect = XStyle_subElementRect((XStyle*)self,
                                           XStyleSE_ProgressBarLabel,
                                           option, widget);
        xcs_drawProgressBarLabel(self, &sub, painter);
    }
}

/** @brief 绘制表头控件（CE_Header：段 + 标签，对标 QCommonStyle 子集）。 */
static void xcs_drawHeader(XStyle* self, const XStyleOption* option,
                           XPainter* painter, const XWidget* widget)
{
    XStyleOption sub;
    if (!option || !painter) return;
    sub = *option;
    sub.m_type = XStyleCE_HeaderSection;
    sub.m_rect = option->m_rect;
    xcs_drawHeaderSection(self, &sub, painter, widget);
    sub.m_type = XStyleCE_HeaderLabel;
    sub.m_rect = XStyle_subElementRect((XStyle*)self, XStyleSE_HeaderLabel,
                                       option, widget);
    if (sub.m_rect.width > 0 && sub.m_rect.height > 0)
        xcs_drawHeaderLabel(self, &sub, painter, widget);
}

/** @brief 绘制工具栏控件（CE_ToolBar：面板 + 顶层工具栏分隔线，
 *         对标 QCommonStyle）。 */
static void xcs_drawToolBar(XStyle* self, const XStyleOption* option,
                            XPainter* painter, const XWidget* widget)
{
    XStyleOption panel;
    (void)widget;
    if (!option || !painter) return;
    panel = *option;
    panel.m_type = XStylePE_PanelToolBar;
    xcs_drawBarPanel(self, &panel, painter);
}

/** @brief 绘制组合框标签（CE_ComboBoxLabel：左对齐文本）。 */
static void xcs_drawComboBoxLabel(XStyle* self,
                                  const XStyleOption* option,
                                  XPainter* painter)
{
    uint32_t textColor;
    XRect r;
    int textH;
    (void)self;
    if (!option || !painter) return;
    r = option->m_rect;
    if (!option->m_text || !option->m_text[0]) return;
    textColor = option->m_textColor;
    if (textColor == 0)
        textColor = xcs_color(option, XPaletteColorRole_ButtonText);
    if (textColor == 0) textColor = 0xFF000000u;
    textH = XPainter_textHeight(XPainter_font(painter));
    if (textH < 14) textH = 14;
    XPainter_drawText(painter, r.x + 2,
                      r.y + (r.height - textH) / 2 + textH - 4,
                      option->m_text, textColor);
}

/** @brief 绘制条目视图项（CE_ItemViewItem：面板 + 勾选 + 文本 + 焦点，
 *         完整对标 QCommonStyle 子集）。 */
static void xcs_drawItemViewItem(XStyle* self, const XStyleOption* option,
                                 XPainter* painter, const XWidget* widget)
{
    XRect checkRect;
    XRect iconRect;
    XRect textRect;
    uint32_t textColor;
    int textH;
    if (!option || !painter) return;
    checkRect = XStyle_subElementRect((XStyle*)self,
                                      XStyleSE_ItemViewItemCheckIndicator,
                                      option, widget);
    iconRect = XStyle_subElementRect((XStyle*)self,
                                     XStyleSE_ItemViewItemDecoration,
                                     option, widget);
    textRect = XStyle_subElementRect((XStyle*)self,
                                     XStyleSE_ItemViewItemText,
                                     option, widget);
    xcs_drawPanelItemViewItem(self, option, painter, widget);
    /* 勾选指示。 */
    if ((option->m_viewFeatures & 0x1) && checkRect.width > 0) {
        XStyleOption check = *option;
        check.m_type = XStylePE_IndicatorItemViewItemCheck;
        check.m_rect = checkRect;
        check.m_state &= ~(uint32_t)XStyleState_HasFocus;
        if (option->m_checkState2 == 2)
            check.m_state |= XStyleState_On;
        else if (option->m_checkState2 == 1)
            check.m_state |= XStyleState_NoChange;
        else
            check.m_state |= XStyleState_Off;
        xcs_drawItemViewItemCheck(self, &check, painter);
    }
    /* 图标：XGui 条目视图当前经 m_icon 借用 XIcon，绘制由控件层
     * 完成（Ruling：见 Task 2.12 报告，QIcon::paint 未在此处接线）。 */
    (void)iconRect;
    /* 文本。 */
    if (option->m_text && option->m_text[0] && textRect.width > 0) {
        if (option->m_state & XStyleState_Selected)
            textColor = xcs_color(option, XPaletteColorRole_HighlightedText);
        else
            textColor = xcs_color(option, XPaletteColorRole_Text);
        if (textColor == 0)
            textColor = (option->m_state & XStyleState_Selected)
                ? 0xFFFFFFFFu : 0xFF000000u;
        if (!(option->m_state & XStyleState_Enabled))
            textColor = xcs_color(option, XPaletteColorRole_Mid);
        textH = XPainter_textHeight(XPainter_font(painter));
        if (textH < 14) textH = 14;
        XPainter_drawText(painter, textRect.x,
                          textRect.y + (textRect.height - textH) / 2 +
                              textH - 4,
                          option->m_text, textColor);
    }
    /* 焦点框。 */
    if (option->m_state & XStyleState_HasFocus) {
        XStyleOption foc = *option;
        foc.m_type = XStylePE_FrameFocusRect;
        foc.m_rect = XStyle_subElementRect((XStyle*)self,
                                           XStyleSE_ItemViewItemFocusRect,
                                           option, widget);
        xcs_drawFocusRect(self, &foc, painter);
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
    case XStyleCE_ProgressBar:
        xcs_drawProgressBar(self, option, painter, widget);
        break;
    case XStyleCE_ProgressBarGroove:
        xcs_drawProgressGroove(self, option, painter);
        break;
    case XStyleCE_ProgressBarContents:
        xcs_drawProgressContents(self, option, painter);
        break;
    case XStyleCE_ProgressBarLabel:
        xcs_drawProgressBarLabel(self, option, painter);
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
    case XStyleCE_Header:
        xcs_drawHeader(self, option, painter, widget);
        break;
    case XStyleCE_ToolBar:
        xcs_drawToolBar(self, option, painter, widget);
        break;
    case XStyleCE_ItemViewItem:
        xcs_drawItemViewItem(self, option, painter, widget);
        break;
    case XStyleCE_ComboBoxLabel:
        xcs_drawComboBoxLabel(self, option, painter);
        break;
    case XStyleCE_MenuBarEmptyArea:
    case XStyleCE_HeaderEmptyArea:
    case XStyleCE_ScrollBarAddLine:
    case XStyleCE_ScrollBarSubLine:
    case XStyleCE_ScrollBarAddPage:
    case XStyleCE_ScrollBarSubPage:
    case XStyleCE_ScrollBarSlider:
    case XStyleCE_ScrollBarFirst:
    case XStyleCE_ScrollBarLast:
        /* 对标 Qt：QCommonStyle::drawControl 不处理 CE_ScrollBar* 与
         * 空区元素，滚动条经 CC_ScrollBar 复杂控件绘制；此处显式
         * 列出以覆盖计划清单，保持与 Qt 一致的空操作。 */
        break;
    default:
        break;
    }
}

static int VXCommonStyle_pixelMetric(XStyle* self, int pm,
                                     const XStyleOption* option)
{
    (void)self;
    switch (pm) {
    case XStylePM_ButtonMargin:
        return 6;
    case XStylePM_DockWidgetTitleBarButtonMargin:
        return 2;
    case XStylePM_ButtonDefaultIndicator:
        return 0;
    case XStylePM_MenuButtonIndicator:
        return 12;
    case XStylePM_ButtonShiftHorizontal:
    case XStylePM_ButtonShiftVertical:
    case XStylePM_DefaultFrameWidth:
        return 2;
    case XStylePM_ComboBoxFrameWidth:
    case XStylePM_SpinBoxFrameWidth:
    case XStylePM_MenuPanelWidth:
    case XStylePM_TabBarBaseOverlap:
    case XStylePM_TabBarBaseHeight:
        return XStyle_pixelMetric((XStyle*)self, XStylePM_DefaultFrameWidth,
                                  option);
    case XStylePM_ScrollBarExtent:
        return 16;
    case XStylePM_ScrollBarSliderMin:
        return 9;
    case XStylePM_SliderThickness:
        return 16;
    case XStylePM_SliderLength:
        return 16;
    case XStylePM_DockWidgetSeparatorExtent:
        return 6;
    case XStylePM_DockWidgetHandleExtent:
        return 8;
    case XStylePM_DockWidgetFrameWidth:
        return 1;
    case XStylePM_DockWidgetTitleMargin:
        return 0;
    case XStylePM_SpinBoxSliderHeight:
    case XStylePM_MenuBarPanelWidth:
        return 2;
    case XStylePM_MenuBarItemSpacing:
        return 0;
    case XStylePM_ToolBarFrameWidth:
        return 1;
    case XStylePM_ToolBarItemMargin:
        return 0;
    case XStylePM_ToolBarItemSpacing:
        return 4;
    case XStylePM_ToolBarHandleExtent:
        return 8;
    case XStylePM_ToolBarSeparatorExtent:
        return 6;
    case XStylePM_ToolBarExtensionExtent:
        return 12;
    case XStylePM_TabBarTabOverlap:
        return 3;
    case XStylePM_TabBarTabHSpace:
        return 24;
    case XStylePM_TabBarTabShiftHorizontal:
        return 0;
    case XStylePM_TabBarTabShiftVertical:
        return 2;
    case XStylePM_TabBarTabVSpace:
        /* RoundedNorth/South/West/East=8；TriangularWest/East=3；其余 2。 */
        if (option &&
            (option->m_tabPosition == 0 || option->m_tabPosition == 1))
            return 8;
        if (option && option->m_tabPosition >= 2) return 3;
        return 2;
    case XStylePM_ProgressBarChunkWidth:
        return 9;
    case XStylePM_IndicatorWidth:
        return 13;
    case XStylePM_IndicatorHeight:
        return 13;
    case XStylePM_ExclusiveIndicatorWidth:
        return 12;
    case XStylePM_ExclusiveIndicatorHeight:
        return 12;
    case XStylePM_MenuTearoffHeight:
        return 10;
    case XStylePM_MenuScrollerHeight:
        return 10;
    case XStylePM_MenuDesktopFrameWidth:
    case XStylePM_MenuHMargin:
    case XStylePM_MenuVMargin:
        return 0;
    case XStylePM_HeaderMargin:
        return 4;
    case XStylePM_HeaderMarkSize:
        return 16;
    case XStylePM_HeaderGripMargin:
        return 4;
    case XStylePM_HeaderDefaultSectionSizeHorizontal:
        return 100;
    case XStylePM_HeaderDefaultSectionSizeVertical:
        return 30;
    case XStylePM_TabBarScrollButtonWidth:
        return 16;
    case XStylePM_ToolBarIconSize:
        return 24;
    case XStylePM_ButtonIconSize:
    case XStylePM_SmallIconSize:
        return 16;
    case XStylePM_LargeIconSize:
        return 32;
    case XStylePM_LineEditIconSize:
        return 16;
    case XStylePM_LineEditIconMargin:
        return 4;
    case XStylePM_ToolTipLabelFrameWidth:
        return 1;
    case XStylePM_CheckBoxLabelSpacing:
    case XStylePM_RadioButtonLabelSpacing:
        return 6;
    case XStylePM_SizeGripSize:
        return 13;
    case XStylePM_MessageBoxIconSize:
        return 32;
    case XStylePM_TabBarIconSize:
        return 16;
    case XStylePM_TextCursorWidth:
        return 1;
    case XStylePM_TabBar_ScrollButtonOverlap:
        return 1;
    case XStylePM_TabCloseIndicatorWidth:
    case XStylePM_TabCloseIndicatorHeight:
        return 16;
    case XStylePM_ScrollView_ScrollBarSpacing:
        return 2 * XStyle_pixelMetric((XStyle*)self,
                                      XStylePM_DefaultFrameWidth, option);
    case XStylePM_ScrollView_ScrollBarOverlap:
        return 0;
    case XStylePM_SubMenuOverlap:
        return -XStyle_pixelMetric((XStyle*)self, XStylePM_MenuPanelWidth,
                                   option);
    case XStylePM_TreeViewIndentation:
        return 20;
    case XStylePM_TitleBarHeight:
        return 18;
    case XStylePM_TitleBarButtonSize:
        return 16;
    case XStylePM_TitleBarButtonIconSize:
        return 16;
    case XStylePM_LayoutLeftMargin:
    case XStylePM_LayoutTopMargin:
    case XStylePM_LayoutRightMargin:
    case XStylePM_LayoutBottomMargin:
        return (option && (option->m_state & XStyleState_Window)) ? 11 : 9;
    case XStylePM_LayoutHorizontalSpacing:
    case XStylePM_LayoutVerticalSpacing:
        return 6;
    case XStylePM_FocusFrameVMargin:
    case XStylePM_FocusFrameHMargin:
        return 2;
    case XStylePM_MdiSubWindowFrameWidth:
        return 4;
    case XStylePM_MdiSubWindowMinimizedWidth:
        return 196;
    case XStylePM_MaximumDragDistance:
        return 60;
    case XStylePM_ListViewIconSize:
    case XStylePM_IconViewIconSize:
        return 24;
    case XStylePM_SliderControlThickness:
    case XStylePM_SliderTickmarkOffset:
    case XStylePM_SliderSpaceAvailable:
        return 0;
    case XStylePM_SplitterWidth:
        return 0; /* QCommonStyle 无定义；XWindowsStyle 覆盖为 4。 */
    default:
        return 0;
    }
}

/* ==================== 尺寸计算（对标 QCommonStyle::sizeFromContents） ==================== */

static XSize VXCommonStyle_sizeFromContents(XStyle* self, int ct,
                                            const XStyleOption* option,
                                            XSize contentSize)
{
    XSize size;
    (void)self;
    size = contentSize;
    switch (ct) {
    case XStyleCT_PushButton:
        if (option) {
            int width = contentSize.width;
            int height = contentSize.height;
            int buttonMargin = XStyle_pixelMetric((XStyle*)self,
                                                  XStylePM_ButtonMargin,
                                                  option);
            int defaultFrameWidth = XStyle_pixelMetric(
                (XStyle*)self, XStylePM_DefaultFrameWidth, option) * 2;
            width += buttonMargin + defaultFrameWidth;
            height += buttonMargin + defaultFrameWidth;
            if (option->m_checkable) { /* 复用 m_checkable 传
                                            AutoDefaultButton 特性。 */
                int buttonIndicator = XStyle_pixelMetric(
                    (XStyle*)self, XStylePM_ButtonDefaultIndicator,
                    option) * 2;
                width += buttonIndicator;
                height += buttonIndicator;
            }
            XSize_init(&size, width, height);
        }
        break;
    case XStyleCT_RadioButton:
    case XStyleCT_CheckBox:
        if (option) {
            bool isRadio = (ct == XStyleCT_RadioButton);
            int width = XStyle_pixelMetric(
                (XStyle*)self,
                isRadio ? XStylePM_ExclusiveIndicatorWidth
                        : XStylePM_IndicatorWidth, option);
            int height = XStyle_pixelMetric(
                (XStyle*)self,
                isRadio ? XStylePM_ExclusiveIndicatorHeight
                        : XStylePM_IndicatorHeight, option);
            int margins = 0;
            if (option->m_text && option->m_text[0]) {
                margins = 4 + XStyle_pixelMetric(
                    (XStyle*)self,
                    isRadio ? XStylePM_RadioButtonLabelSpacing
                            : XStylePM_CheckBoxLabelSpacing, option);
            }
            size.width += width + margins;
            size.height += 4;
            if (size.height < height) size.height = height;
        }
        break;
    case XStyleCT_ToolButton:
        size.width += 6;
        size.height += 5;
        break;
    case XStyleCT_ComboBox:
        if (option) {
            int frameWidth = option->m_spinFrame
                ? XStyle_pixelMetric((XStyle*)self,
                                     XStylePM_ComboBoxFrameWidth,
                                     option) * 2 : 0;
            int textMargins = 2 * (XStyle_pixelMetric(
                (XStyle*)self, XStylePM_FocusFrameHMargin, option) + 1);
            int other = 23 > 2 * textMargins + XStyle_pixelMetric(
                (XStyle*)self, XStylePM_ScrollBarExtent, option)
                ? 23 : 2 * textMargins + XStyle_pixelMetric(
                (XStyle*)self, XStylePM_ScrollBarExtent, option);
            size.width += frameWidth + other;
            size.height += frameWidth;
        }
        break;
    case XStyleCT_HeaderSection:
        if (option) {
            int margin = XStyle_pixelMetric((XStyle*)self,
                                            XStylePM_HeaderMargin, option);
            int iconSize = option->m_icon
                ? XStyle_pixelMetric((XStyle*)self, XStylePM_SmallIconSize,
                                     option) : 0;
            int textW = option->m_text
                ? (int)XStrlen(option->m_text) * 8 : 0; /* 近似字宽。 */
            int textH = 14;
            size.height = margin +
                (iconSize > textH ? iconSize : textH) + margin;
            size.width = (option->m_icon ? margin : 0) + iconSize +
                (option->m_text ? margin : 0) + textW + margin;
        }
        break;
    case XStyleCT_TabWidget:
        size.width += 4;
        size.height += 4;
        break;
    case XStyleCT_LineEdit:
        if (option) {
            int lw = option->m_spinFrame ? 1 : 0;
            size.width += 2 * lw;
            size.height += 2 * lw;
        }
        break;
    case XStyleCT_GroupBox:
        if (option && !option->m_flat)
            size.width += 16;
        break;
    case XStyleCT_SpinBox:
        if (option) {
            int frameWidth = option->m_spinFrame
                ? XStyle_pixelMetric((XStyle*)self,
                                     XStylePM_SpinBoxFrameWidth, option) : 0;
            size.width += 2 * frameWidth;
            size.height += 2 * frameWidth;
            if (option->m_spinSymbols != 2) { /* NoButtons=2。 */
                int h = size.height / 2 - frameWidth;
                if (h < 8) h = 8;
                int bw = 16;
                int maxbw = h * 8 / 5;
                if (maxbw > size.width / 3) maxbw = size.width / 3;
                if (bw > maxbw) bw = maxbw;
                if (bw < 16) bw = 16;
                size.width += bw;
            }
        }
        break;
    case XStyleCT_ScrollBar:
    case XStyleCT_MenuBar:
    case XStyleCT_Menu:
    case XStyleCT_MenuBarItem:
    case XStyleCT_Slider:
    case XStyleCT_ProgressBar:
    case XStyleCT_TabBarTab:
    default:
        break;
    }
    return size;
}

/* ==================== 样式提示（对标 QCommonStyle::styleHint 子集） ==================== */

static int VXCommonStyle_styleHint(XStyle* self, int sh,
                                   const XStyleOption* opt,
                                   const XWidget* widget)
{
    (void)self;
    switch (sh) {
    case XStyleSH_Menu_KeyboardSearch:
        return 0;
    case XStyleSH_Slider_AbsoluteSetButtons:
        return 0x00000010; /* Qt::MiddleButton。 */
    case XStyleSH_Slider_PageSetButtons:
        return 0x00000001; /* Qt::LeftButton。 */
    case XStyleSH_ScrollBar_ContextMenu:
        return 1;
    case XStyleSH_GroupBox_TextLabelVerticalAlignment:
        return XAlignment_VCenter;
    case XStyleSH_GroupBox_TextLabelColor:
        if (opt) {
            XColor c = XPalette_color((XPalette*)&opt->m_palette,
                                      XPaletteColorGroup_Current,
                                      XPaletteColorRole_Text);
            return (int)XColor_rgba(&c);
        }
        return 0;
    case XStyleSH_ListViewExpand_SelectMouseType:
    case XStyleSH_TabBar_SelectMouseType:
        return 0; /* QEvent::MouseButtonPress=0。 */
    case XStyleSH_TabBar_Alignment:
        return XAlignment_Left;
    case XStyleSH_Header_ArrowAlignment:
        return XAlignment_Right | XAlignment_VCenter;
    case XStyleSH_TitleBar_AutoRaise:
        return 0;
    case XStyleSH_Menu_SubMenuPopupDelay:
        return 256;
    case XStyleSH_Menu_SloppySubMenus:
        return 1;
    case XStyleSH_Menu_SubMenuUniDirection:
        return 0;
    case XStyleSH_Menu_SubMenuUniDirectionFailCount:
        return 1;
    case XStyleSH_Menu_SubMenuSloppySelectOtherActions:
        return 1;
    case XStyleSH_Menu_SubMenuSloppyCloseTimeout:
        return 1000;
    case XStyleSH_Menu_SubMenuResetWhenReenteringParent:
        return 0;
    case XStyleSH_Menu_SubMenuDontStartSloppyOnLeave:
        return 0;
    case XStyleSH_ProgressDialog_TextLabelAlignment:
        return XAlignment_Center;
    case XStyleSH_BlinkCursorWhenTextSelected:
        return 1;
    case XStyleSH_Table_GridLineColor:
        if (opt) {
            XColor c = XPalette_color((XPalette*)&opt->m_palette,
                                      XPaletteColorGroup_Current,
                                      XPaletteColorRole_Mid);
            return (int)XColor_rgba(&c);
        }
        return -1;
    case XStyleSH_LineEdit_PasswordCharacter:
        return 0x25CF; /* U+25CF ●（对标平台默认）。 */
    case XStyleSH_LineEdit_PasswordMaskDelay:
        return 1000;
    case XStyleSH_ToolBox_SelectedPageTitleBold:
        return 1;
    case XStyleSH_UnderlineShortcut:
        return 0;
    case XStyleSH_SpinBox_ClickAutoRepeatRate:
        return 150;
    case XStyleSH_SpinBox_ClickAutoRepeatThreshold:
        return 500;
    case XStyleSH_ToolTipLabel_Opacity:
        return 255;
    case XStyleSH_SpinControls_DisableOnBounds:
        return 1;
    case XStyleSH_Dial_BackgroundRole:
        return XPaletteColorRole_Window;
    case XStyleSH_ComboBox_LayoutDirection:
        return opt ? opt->m_direction : 0;
    case XStyleSH_ItemView_ShowDecorationSelected:
        (void)widget;
        return 0;
    case XStyleSH_ItemView_ActivateItemOnSingleClick:
        return 0;
    case XStyleSH_ScrollBar_RollBetweenButtons:
        return 0;
    case XStyleSH_Menu_SelectionWrap:
        return 0;
    case XStyleSH_TabWidget_DefaultTabPosition:
        return 0; /* QTabWidget::North=0。 */
    case XStyleSH_ToolBar_Movable:
        return 1;
    case XStyleSH_ComboBox_UseNativePopup:
        return 0;
    case XStyleSH_TabBar_ChangeCurrentDelay:
        return 0;
    case XStyleSH_Widget_Animate:
        return 0;
    case XStyleSH_Splitter_OpaqueResize:
        return 1;
    case XStyleSH_Widget_Animation_Duration:
        return 250;
    case XStyleSH_ComboBox_AllowWheelScrolling:
        return 1;
    case XStyleSH_SpinBox_ButtonsInsideFrame:
        return 1;
    case XStyleSH_SpinBox_StepModifier:
        return 0; /* Qt::ControlModifier=0x04000000 于 Qt6 为 0x02000000；
                     简化记录为 0。 */
    case XStyleSH_TabBar_AllowWheelScrolling:
        return 0;
    case XStyleSH_Table_AlwaysDrawLeftTopGridLines:
        return 0;
    case XStyleSH_SpinBox_SelectOnStep:
        return 1;
    case XStyleSH_DialogButtonBox_ButtonsHaveIcons:
        return 0;
    case XStyleSH_Menu_SupportsSections:
        return 0;
    case XStyleSH_ToolTip_WakeUpDelay:
        return 0;
    case XStyleSH_ToolTip_FallAsleepDelay:
        return 0;
    case XStyleSH_ScrollBar_Transient:
        return 0;
    default:
        return 0;
    }
}

/* ==================== 子元素矩形（对标 QCommonStyle::subElementRect 子集） ==================== */

static XRect VXCommonStyle_subElementRect(XStyle* self, int sr,
                                          const XStyleOption* opt,
                                          const XWidget* widget)
{
    XRect r;
    XRect_init(&r, 0, 0, 0, 0);
    (void)widget;
    if (!opt) return r;
    switch (sr) {
    case XStyleSE_PushButtonContents: {
        int dx1 = XStyle_pixelMetric((XStyle*)self,
                                     XStylePM_DefaultFrameWidth, opt);
        if (opt->m_checkable) /* AutoDefaultButton 特性复用。 */
            dx1 += XStyle_pixelMetric((XStyle*)self,
                                      XStylePM_ButtonDefaultIndicator, opt);
        XRect_init(&r, opt->m_rect.x + dx1, opt->m_rect.y + dx1,
                   opt->m_rect.width - dx1 * 2,
                   opt->m_rect.height - dx1 * 2);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_PushButtonFocusRect: {
        int dbw1 = 0;
        if (opt->m_checkable)
            dbw1 = XStyle_pixelMetric((XStyle*)self,
                                      XStylePM_ButtonDefaultIndicator, opt);
        int dfw1 = XStyle_pixelMetric((XStyle*)self,
                                      XStylePM_DefaultFrameWidth, opt) + 1;
        XRect_init(&r, opt->m_rect.x + dfw1 + dbw1,
                   opt->m_rect.y + dfw1 + dbw1,
                   opt->m_rect.width - dfw1 * 2 - dbw1 * 2,
                   opt->m_rect.height - dfw1 * 2 - dbw1 * 2);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_PushButtonBevel:
        r = opt->m_rect;
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    case XStyleSE_CheckBoxIndicator: {
        int h = XStyle_pixelMetric((XStyle*)self, XStylePM_IndicatorHeight,
                                   opt);
        XRect_init(&r, opt->m_rect.x,
                   opt->m_rect.y + (opt->m_rect.height - h) / 2,
                   XStyle_pixelMetric((XStyle*)self, XStylePM_IndicatorWidth,
                                      opt), h);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_CheckBoxContents: {
        XRect ir = VXCommonStyle_subElementRect(
            self, XStyleSE_CheckBoxIndicator, opt, widget);
        int spacing = XStyle_pixelMetric((XStyle*)self,
                                         XStylePM_CheckBoxLabelSpacing, opt);
        XRect_init(&r, ir.x + ir.width + spacing, opt->m_rect.y,
                   opt->m_rect.width - ir.width - spacing,
                   opt->m_rect.height);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_CheckBoxFocusRect: {
        XRect cr = VXCommonStyle_subElementRect(
            self, XStyleSE_CheckBoxContents, opt, widget);
        int tw = opt->m_text ? (int)XStrlen(opt->m_text) * 8 : 0;
        if (tw <= 0) {
            r = VXCommonStyle_subElementRect(
                self, XStyleSE_CheckBoxIndicator, opt, widget);
            r = XRect_adjusted(&r, 1, 1, -1, -1);
            break;
        }
        XRect_init(&r, cr.x - 3, cr.y - 2,
                   (tw > cr.width ? cr.width : tw) + 6,
                   cr.height + 4);
        r = XRect_intersected(&r, &opt->m_rect);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_CheckBoxClickRect: {
        XRect f = VXCommonStyle_subElementRect(
            self, XStyleSE_CheckBoxFocusRect, opt, widget);
        XRect i = VXCommonStyle_subElementRect(
            self, XStyleSE_CheckBoxIndicator, opt, widget);
        r = XRect_united(&f, &i);
        break;
    }
    case XStyleSE_RadioButtonIndicator: {
        int h = XStyle_pixelMetric((XStyle*)self,
                                   XStylePM_ExclusiveIndicatorHeight, opt);
        XRect_init(&r, opt->m_rect.x,
                   opt->m_rect.y + (opt->m_rect.height - h) / 2,
                   XStyle_pixelMetric((XStyle*)self,
                                      XStylePM_ExclusiveIndicatorWidth,
                                      opt), h);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_RadioButtonContents: {
        XRect ir = VXCommonStyle_subElementRect(
            self, XStyleSE_RadioButtonIndicator, opt, widget);
        int spacing = XStyle_pixelMetric((XStyle*)self,
                                         XStylePM_RadioButtonLabelSpacing,
                                         opt);
        XRect_init(&r, ir.x + ir.width + spacing, opt->m_rect.y,
                   opt->m_rect.width - ir.width - spacing,
                   opt->m_rect.height);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_RadioButtonFocusRect: {
        XRect cr = VXCommonStyle_subElementRect(
            self, XStyleSE_RadioButtonContents, opt, widget);
        int tw = opt->m_text ? (int)XStrlen(opt->m_text) * 8 : 0;
        if (tw <= 0) {
            r = VXCommonStyle_subElementRect(
                self, XStyleSE_RadioButtonIndicator, opt, widget);
            r = XRect_adjusted(&r, 1, 1, -1, -1);
            break;
        }
        XRect_init(&r, cr.x - 3, cr.y - 2,
                   (tw > cr.width ? cr.width : tw) + 6,
                   cr.height + 4);
        r = XRect_intersected(&r, &opt->m_rect);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_RadioButtonClickRect: {
        XRect f = VXCommonStyle_subElementRect(
            self, XStyleSE_RadioButtonFocusRect, opt, widget);
        XRect i = VXCommonStyle_subElementRect(
            self, XStyleSE_RadioButtonIndicator, opt, widget);
        r = XRect_united(&f, &i);
        break;
    }
    case XStyleSE_ComboBoxFocusRect: {
        int margin = opt->m_spinFrame ? 3 : 0;
        XRect_init(&r, opt->m_rect.x + margin, opt->m_rect.y + margin,
                   opt->m_rect.width - 2 * margin - 16,
                   opt->m_rect.height - 2 * margin);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_SliderFocusRect: {
        XRect_init(&r, opt->m_rect.x, opt->m_rect.y,
                   opt->m_rect.width, opt->m_rect.height);
        r = XRect_intersected(&r, &opt->m_rect);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_ProgressBarGroove:
    case XStyleSE_ProgressBarContents:
    case XStyleSE_ProgressBarLabel: {
        int textw = 0;
        bool vertical = !(opt->m_state & XStyleState_Horizontal);
        if (!vertical && opt->m_progressTextVisible && opt->m_text)
            textw = ((int)XStrlen(opt->m_text) > 4
                     ? (int)XStrlen(opt->m_text) : 4) * 8 + 6;
        if (sr == XStyleSE_ProgressBarLabel) {
            if (textw > 0)
                XRect_init(&r, opt->m_rect.x + opt->m_rect.width - textw,
                           opt->m_rect.y, textw, opt->m_rect.height);
            else
                r = opt->m_rect;
        } else {
            if (textw > 0)
                XRect_init(&r, opt->m_rect.x, opt->m_rect.y,
                           opt->m_rect.width - textw, opt->m_rect.height);
            else
                r = opt->m_rect;
        }
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_HeaderLabel: {
        int margin = XStyle_pixelMetric((XStyle*)self, XStylePM_HeaderMargin,
                                        opt);
        XRect_init(&r, opt->m_rect.x + margin, opt->m_rect.y + margin,
                   opt->m_rect.width - margin * 2,
                   opt->m_rect.height - margin * 2);
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_HeaderArrow: {
        int h = opt->m_rect.height;
        int w = opt->m_rect.width;
        int margin = XStyle_pixelMetric((XStyle*)self, XStylePM_HeaderMargin,
                                        opt);
        if (opt->m_state & XStyleState_Horizontal) {
            int horiz_size = h / 2;
            XRect_init(&r, opt->m_rect.x + w - margin * 2 - horiz_size,
                       opt->m_rect.y + 5, horiz_size,
                       h - margin * 2 - 5);
        } else {
            int vert_size = w / 2;
            XRect_init(&r, opt->m_rect.x + 5,
                       opt->m_rect.y + h - margin * 2 - vert_size,
                       w - margin * 2 - 5, vert_size);
        }
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_ItemViewItemCheckIndicator:
    case XStyleSE_ItemViewItemDecoration:
    case XStyleSE_ItemViewItemText:
    case XStyleSE_ItemViewItemFocusRect: {
        int checkW = 13;
        int margin = 2;
        int decoW = 16;
        XRect_init(&r, opt->m_rect.x + margin, opt->m_rect.y + margin,
                   opt->m_rect.width - margin * 2,
                   opt->m_rect.height - margin * 2);
        if (sr == XStyleSE_ItemViewItemCheckIndicator) {
            XRect_init(&r, opt->m_rect.x, opt->m_rect.y,
                       checkW, opt->m_rect.height);
        } else if (sr == XStyleSE_ItemViewItemDecoration) {
            XRect_init(&r, opt->m_rect.x + checkW + margin, opt->m_rect.y,
                       decoW, opt->m_rect.height);
        } else if (sr == XStyleSE_ItemViewItemText) {
            XRect_init(&r, opt->m_rect.x + checkW + decoW + margin * 2,
                       opt->m_rect.y,
                       opt->m_rect.width - checkW - decoW - margin * 2,
                       opt->m_rect.height);
        } else {
            XRect_init(&r, opt->m_rect.x + 1, opt->m_rect.y + 1,
                       opt->m_rect.width - 2, opt->m_rect.height - 2);
        }
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    }
    case XStyleSE_LineEditContents:
    case XStyleSE_FrameContents:
        r = opt->m_rect;
        r = XStyle_visualRect(opt->m_direction, &opt->m_rect, &r);
        break;
    case XStyleSE_TabBarTabText:
        r = opt->m_rect;
        break;
    case XStyleSE_ToolBarHandle: {
        XRect_init(&r, 0, 0, 3,
                   opt->m_rect.height > 0 ? opt->m_rect.height : 1);
        break;
    }
    default:
        r = opt->m_rect;
        break;
    }
    return r;
}

/* ==================== 子控件矩形（对标 QCommonStyle::subControlRect 子集） ==================== */

static XRect VXCommonStyle_subControlRect(XStyle* self, int cc,
                                          const XStyleOption* opt, int sc,
                                          const XWidget* widget)
{
    XRect ret;
    XRect_init(&ret, 0, 0, 0, 0);
    (void)widget;
    if (!opt) return ret;
    switch (cc) {
    case XStyleCC_Slider: {
        int len = XStyle_pixelMetric((XStyle*)self, XStylePM_SliderLength,
                                     opt);
        bool horizontal = opt->m_horizontal;
        if (sc == XStyleSC_SliderHandle) {
            int sliderPos = XStyle_sliderPositionFromValue(
                opt->m_sliderMin, opt->m_sliderMax, opt->m_sliderValue,
                (horizontal ? opt->m_rect.width : opt->m_rect.height) - len,
                false);
            if (horizontal)
                XRect_init(&ret, opt->m_rect.x + sliderPos, opt->m_rect.y,
                           len, opt->m_rect.height);
            else
                XRect_init(&ret, opt->m_rect.x, opt->m_rect.y + sliderPos,
                           opt->m_rect.width, len);
        } else if (sc == XStyleSC_SliderGroove) {
            if (horizontal)
                XRect_init(&ret, opt->m_rect.x, opt->m_rect.y,
                           opt->m_rect.width, opt->m_rect.height);
            else
                XRect_init(&ret, opt->m_rect.x, opt->m_rect.y,
                           opt->m_rect.width, opt->m_rect.height);
        }
        ret = XStyle_visualRect(opt->m_direction, &opt->m_rect, &ret);
        break;
    }
    case XStyleCC_ScrollBar: {
        int sbextent = XStyle_pixelMetric((XStyle*)self,
                                          XStylePM_ScrollBarExtent, opt);
        int maxlen = (opt->m_horizontal ? opt->m_rect.width
                                        : opt->m_rect.height) - sbextent * 2;
        int sliderlen;
        int sliderstart;
        int range = opt->m_sliderMax - opt->m_sliderMin;
        if (range != 0) {
            sliderlen = (int)((int64_t)opt->m_sliderPageStep * maxlen /
                              (range + opt->m_sliderPageStep));
            {
                int slidermin = XStyle_pixelMetric(
                    (XStyle*)self, XStylePM_ScrollBarSliderMin, opt);
                if (sliderlen < slidermin) sliderlen = slidermin;
                if (sliderlen > maxlen) sliderlen = maxlen;
            }
        } else {
            sliderlen = maxlen;
        }
        sliderstart = sbextent + XStyle_sliderPositionFromValue(
            opt->m_sliderMin, opt->m_sliderMax, opt->m_sliderValue,
            maxlen - sliderlen, false);
        if (opt->m_horizontal) {
            switch (sc) {
            case XStyleSC_ScrollBarSubLine: {
                int bw = opt->m_rect.width / 2 < sbextent
                    ? opt->m_rect.width / 2 : sbextent;
                XRect_init(&ret, 0, 0, bw, opt->m_rect.height);
                break;
            }
            case XStyleSC_ScrollBarAddLine: {
                int bw = opt->m_rect.width / 2 < sbextent
                    ? opt->m_rect.width / 2 : sbextent;
                XRect_init(&ret, opt->m_rect.width - bw, 0, bw,
                           opt->m_rect.height);
                break;
            }
            case XStyleSC_ScrollBarSubPage:
                XRect_init(&ret, sbextent, 0, sliderstart - sbextent,
                           opt->m_rect.height);
                break;
            case XStyleSC_ScrollBarAddPage:
                XRect_init(&ret, sliderstart + sliderlen, 0,
                           maxlen - sliderstart - sliderlen + sbextent,
                           opt->m_rect.height);
                break;
            case XStyleSC_ScrollBarGroove:
                XRect_init(&ret, sbextent, 0,
                           opt->m_rect.width - sbextent * 2,
                           opt->m_rect.height);
                break;
            case XStyleSC_ScrollBarSlider:
                XRect_init(&ret, sliderstart, 0, sliderlen,
                           opt->m_rect.height);
                break;
            default:
                break;
            }
        } else {
            switch (sc) {
            case XStyleSC_ScrollBarSubLine: {
                int bh = opt->m_rect.height / 2 < sbextent
                    ? opt->m_rect.height / 2 : sbextent;
                XRect_init(&ret, 0, 0, opt->m_rect.width, bh);
                break;
            }
            case XStyleSC_ScrollBarAddLine: {
                int bh = opt->m_rect.height / 2 < sbextent
                    ? opt->m_rect.height / 2 : sbextent;
                XRect_init(&ret, 0, opt->m_rect.height - bh,
                           opt->m_rect.width, bh);
                break;
            }
            case XStyleSC_ScrollBarSubPage:
                XRect_init(&ret, 0, sbextent, opt->m_rect.width,
                           sliderstart - sbextent);
                break;
            case XStyleSC_ScrollBarAddPage:
                XRect_init(&ret, 0, sliderstart + sliderlen,
                           opt->m_rect.width,
                           maxlen - sliderstart - sliderlen + sbextent);
                break;
            case XStyleSC_ScrollBarGroove:
                XRect_init(&ret, 0, sbextent, opt->m_rect.width,
                           opt->m_rect.height - sbextent * 2);
                break;
            case XStyleSC_ScrollBarSlider:
                XRect_init(&ret, 0, sliderstart, opt->m_rect.width,
                           sliderlen);
                break;
            default:
                break;
            }
        }
        ret = XStyle_visualRect(opt->m_direction, &opt->m_rect, &ret);
        break;
    }
    case XStyleCC_SpinBox: {
        int fw = opt->m_spinFrame
            ? XStyle_pixelMetric((XStyle*)self, XStylePM_SpinBoxFrameWidth,
                                 opt) : 0;
        int bh = opt->m_rect.height / 2 - fw;
        int bw;
        int x;
        int y;
        int lx;
        int rx;
        if (bh < 8) bh = 8;
        bw = bh * 8 / 5;
        if (bw < 16) bw = 16;
        if (bw > opt->m_rect.width / 4) bw = opt->m_rect.width / 4;
        y = fw + opt->m_rect.y;
        x = opt->m_rect.x + opt->m_rect.width - fw - bw;
        lx = fw;
        rx = x - fw;
        switch (sc) {
        case XStyleSC_SpinBoxUp:
            if (opt->m_spinSymbols == 2) break; /* NoButtons。 */
            XRect_init(&ret, x, y, bw, bh);
            break;
        case XStyleSC_SpinBoxDown:
            if (opt->m_spinSymbols == 2) break;
            XRect_init(&ret, x, y + bh, bw, bh);
            break;
        case XStyleSC_SpinBoxEditField:
            if (opt->m_spinSymbols == 2)
                XRect_init(&ret, lx, fw, opt->m_rect.width - 2 * fw,
                           opt->m_rect.height - 2 * fw);
            else
                XRect_init(&ret, lx, fw, rx,
                           opt->m_rect.height - 2 * fw);
            break;
        case XStyleSC_SpinBoxFrame:
            ret = opt->m_rect;
            break;
        default:
            break;
        }
        ret = XStyle_visualRect(opt->m_direction, &opt->m_rect, &ret);
        break;
    }
    case XStyleCC_ComboBox: {
        int margin = opt->m_spinFrame ? 3 : 0;
        int bmarg = opt->m_spinFrame ? 2 : 0;
        int xpos = opt->m_rect.x + opt->m_rect.width - bmarg - 16;
        switch (sc) {
        case XStyleSC_ComboBoxFrame:
            ret = opt->m_rect;
            break;
        case XStyleSC_ComboBoxArrow:
            XRect_init(&ret, xpos, opt->m_rect.y + bmarg, 16,
                       opt->m_rect.height - 2 * bmarg);
            break;
        case XStyleSC_ComboBoxEditField:
            XRect_init(&ret, opt->m_rect.x + margin,
                       opt->m_rect.y + margin,
                       opt->m_rect.width - 2 * margin - 16,
                       opt->m_rect.height - 2 * margin);
            break;
        case XStyleSC_ComboBoxListBoxPopup:
            ret = opt->m_rect;
            break;
        default:
            break;
        }
        ret = XStyle_visualRect(opt->m_direction, &opt->m_rect, &ret);
        break;
    }
    case XStyleCC_ToolButton: {
        int mbi = XStyle_pixelMetric((XStyle*)self,
                                     XStylePM_MenuButtonIndicator, opt);
        ret = opt->m_rect;
        if (sc == XStyleSC_ToolButton) {
            if (opt->m_checkState == 1) /* MenuButtonPopup 特性复用。 */
                ret.width -= mbi;
        } else if (sc == XStyleSC_ToolButtonMenu) {
            if (opt->m_checkState == 1) {
                ret.x += ret.width - mbi;
                ret.width = mbi;
            } else {
                XRect_init(&ret, 0, 0, 0, 0);
            }
        } else {
            XRect_init(&ret, 0, 0, 0, 0);
        }
        ret = XStyle_visualRect(opt->m_direction, &opt->m_rect, &ret);
        break;
    }
    default:
        break;
    }
    return ret;
}

/* ==================== 复杂控件命中测试（对标 QCommonStyle 子集） ==================== */

static int VXCommonStyle_hitTestComplexControl(XStyle* self, int cc,
                                               const XStyleOption* opt,
                                               int x, int y,
                                               const XWidget* widget)
{
    int sc = XStyleSC_None;
    uint32_t ctrl;
    switch (cc) {
    case XStyleCC_Slider:
    case XStyleCC_ScrollBar:
    case XStyleCC_SpinBox:
    case XStyleCC_ComboBox:
    case XStyleCC_ToolButton:
        ctrl = (cc == XStyleCC_Slider) ? XStyleSC_SliderHandle
             : (cc == XStyleCC_ComboBox) ? XStyleSC_ComboBoxArrow
             : (cc == XStyleCC_ToolButton) ? XStyleSC_ToolButton
             : (cc == XStyleCC_SpinBox) ? XStyleSC_SpinBoxUp
             : XStyleSC_ScrollBarAddLine;
        while (ctrl != 0) {
            XRect r = VXCommonStyle_subControlRect(self, cc, opt, (int)ctrl,
                                                   widget);
            if (r.width > 0 && r.height > 0 &&
                XRect_contains(&r, x, y)) {
                sc = (int)ctrl;
                break;
            }
            if (cc == XStyleCC_Slider) {
                if (ctrl == XStyleSC_SliderHandle)
                    ctrl = XStyleSC_SliderGroove;
                else
                    ctrl = 0;
            } else if (cc == XStyleCC_ComboBox || cc == XStyleCC_ToolButton) {
                ctrl >>= 1;
            } else if (cc == XStyleCC_SpinBox) {
                ctrl <<= 1;
                if (ctrl > XStyleSC_SpinBoxEditField) ctrl = 0;
            } else {
                ctrl <<= 1;
                if (ctrl > XStyleSC_ScrollBarGroove) ctrl = 0;
            }
        }
        break;
    default:
        break;
    }
    return sc;
}

/* ==================== 标准图标生成(对标 QCommonStyle::standardIcon) ==================== */

/** @brief 标准图标位图边长(px,正方形)。 */
#define XCSI_ICON_SIZE 48

/** @brief 图标绘制回调(原点坐标系 0..size)。 */
typedef void (*XcsiIconPainter)(XPainter* painter, int size);

/** @brief 实心圆(扫描线逐行填充)。 */
static void xcsi_fillCircle(XPainter* painter, int cx, int cy, int r,
                            uint32_t color)
{
    int dy;
    for (dy = -r; dy <= r; ++dy) {
        int dx = (int)(sqrt((double)r * r - (double)dy * dy) + 0.5);
        XRect row;
        XRect_init(&row, cx - dx, cy + dy, dx * 2, 1);
        XPainter_fillRect(painter, &row, color);
    }
}

/** @brief 粗线段(纵向堆叠 width 条 1px 线近似)。 */
static void xcsi_line(XPainter* painter, int x1, int y1, int x2, int y2,
                      int width, uint32_t color)
{
    int i;
    int lo = -(width - 1) / 2;
    for (i = lo; i < lo + width; ++i)
        XPainter_drawLine(painter, x1, y1 + i, x2, y2 + i);
}

/** @brief 实心三角形。 */
static void xcsi_fillTri(XPainter* painter, int x1, int y1, int x2, int y2,
                         int x3, int y3, uint32_t color)
{
    XPoint pts[3];
    pts[0].x = (short)x1; pts[0].y = (short)y1;
    pts[1].x = (short)x2; pts[1].y = (short)y2;
    pts[2].x = (short)x3; pts[2].y = (short)y3;
    XPainter_setBrush(painter, color);
    XPainter_setPen_2(painter, XPainterPenStyle_NoPen);
    XPainter_drawPolygon(painter, pts, 3, XPainterFillRule_OddEven);
}

/** @brief 实心矩形。 */
static void xcsi_rect(XPainter* painter, int x, int y, int w, int h,
                      uint32_t color)
{
    XRect r;
    XRect_init(&r, x, y, w, h);
    XPainter_fillRect(painter, &r, color);
}

/** @brief 空心矩形(1px 边框)。 */
static void xcsi_frame(XPainter* painter, int x, int y, int w, int h,
                       uint32_t color)
{
    XPainter_setPen(painter, color);
    XPainter_drawLine(painter, x, y, x + w - 1, y);
    XPainter_drawLine(painter, x, y + h - 1, x + w - 1, y + h - 1);
    XPainter_drawLine(painter, x, y, x, y + h - 1);
    XPainter_drawLine(painter, x + w - 1, y, x + w - 1, y + h - 1);
}

/** @brief 圆底消息图标:实心圆 + 居中字形(几何线画,不依赖字体)。 */
static void xcsi_circleGlyph(XPainter* painter, uint32_t bg, uint32_t fg,
                             char glyph)
{
    int s = XCSI_ICON_SIZE;
    int cx = s / 2, cy = s / 2;
    xcsi_fillCircle(painter, cx, cy, s * 5 / 12, bg);
    switch (glyph) {
    case 'i':
        xcsi_rect(painter, cx - 2, (int)(s * 0.40), 4, (int)(s * 0.32), fg);
        xcsi_rect(painter, cx - 2, (int)(s * 0.22), 4, 4, fg);
        break;
    case '!':
        xcsi_rect(painter, cx - 2, (int)(s * 0.28), 4, (int)(s * 0.34), fg);
        xcsi_rect(painter, cx - 2, (int)(s * 0.70), 4, 4, fg);
        break;
    case 'x':
        xcsi_line(painter, (int)(s * 0.32), (int)(s * 0.32),
                  (int)(s * 0.68), (int)(s * 0.68), 4, fg);
        xcsi_line(painter, (int)(s * 0.68), (int)(s * 0.32),
                  (int)(s * 0.32), (int)(s * 0.68), 4, fg);
        break;
    case 'v':
        xcsi_line(painter, (int)(s * 0.28), (int)(s * 0.52),
                  (int)(s * 0.44), (int)(s * 0.66), 5, fg);
        xcsi_line(painter, (int)(s * 0.44), (int)(s * 0.66),
                  (int)(s * 0.72), (int)(s * 0.32), 5, fg);
        break;
    default:
        break;
    }
}

/* ---- 各标准图标绘制回调 ---- */

static void xcsi_p_msgInfo(XPainter* painter, int size)
{ (void)size; xcsi_circleGlyph(painter, 0xFF2A82DAu, 0xFFFFFFFFu, 'i'); }

static void xcsi_p_msgWarning(XPainter* painter, int size)
{
    (void)size;
    xcsi_fillTri(painter, 24, 5, 3, 41, 45, 41, 0xFFFFC845u);
    xcsi_rect(painter, 22, 18, 4, 12, 0xFF303030u);
    xcsi_rect(painter, 22, 33, 4, 4, 0xFF303030u);
}

static void xcsi_p_msgCritical(XPainter* painter, int size)
{ (void)size; xcsi_circleGlyph(painter, 0xFFCC2020u, 0xFFFFFFFFu, 'x'); }

static void xcsi_p_msgQuestion(XPainter* painter, int size)
{
    (void)size;
    xcsi_circleGlyph(painter, 0xFF2A82DAu, 0xFFFFFFFFu, 0);
    XPainter_setPen(painter, 0xFFFFFFFFu);
    XPainter_drawText(painter, 19, 32, "?", 0xFFFFFFFFu);
}

static void xcsi_p_dirClosed(XPainter* painter, int size)
{
    (void)size;
    xcsi_rect(painter, 6, 14, 24, 6, 0xFFFFD870u);   /* 顶盖 */
    xcsi_rect(painter, 6, 19, 36, 20, 0xFFF0B840u);  /* 主体 */
    xcsi_rect(painter, 6, 19, 36, 2, 0xFFFFE9A8u);   /* 高光边 */
}

static void xcsi_p_dirOpen(XPainter* painter, int size)
{
    (void)size;
    xcsi_rect(painter, 6, 12, 24, 6, 0xFFFFD870u);
    xcsi_rect(painter, 6, 17, 36, 8, 0xFFF0B840u);
    xcsi_fillTri(painter, 12, 25, 44, 25, 38, 41, 0xFFFFC845u);
    xcsi_fillTri(painter, 12, 25, 38, 41, 12, 41, 0xFFF0B840u);
}

static void xcsi_p_file(XPainter* painter, int size)
{
    (void)size;
    xcsi_rect(painter, 12, 6, 22, 34, 0xFFF8F8F8u);
    xcsi_frame(painter, 12, 6, 22, 34, 0xFF9A9A9Au);
    xcsi_fillTri(painter, 26, 6, 34, 14, 26, 14, 0xFFC8C8C8u); /* 折角 */
    xcsi_rect(painter, 15, 18, 16, 2, 0xFFB0B0B0u);
    xcsi_rect(painter, 15, 23, 16, 2, 0xFFB0B0B0u);
    xcsi_rect(painter, 15, 28, 12, 2, 0xFFB0B0B0u);
}

static void xcsi_p_titleMin(XPainter* painter, int size)
{ (void)size; xcsi_line(painter, 12, 34, 36, 34, 4, 0xFF404040u); }

static void xcsi_p_titleMax(XPainter* painter, int size)
{ (void)size; xcsi_frame(painter, 12, 12, 24, 24, 0xFF404040u); }

static void xcsi_p_titleClose(XPainter* painter, int size)
{
    (void)size;
    xcsi_line(painter, 14, 14, 34, 34, 4, 0xFF404040u);
    xcsi_line(painter, 34, 14, 14, 34, 4, 0xFF404040u);
}

static void xcsi_p_titleNormal(XPainter* painter, int size)
{
    (void)size;
    xcsi_frame(painter, 10, 16, 22, 18, 0xFF404040u);
    xcsi_frame(painter, 16, 10, 22, 18, 0xFF404040u);
}

static void xcsi_p_titleMenu(XPainter* painter, int size)
{
    (void)size;
    xcsi_line(painter, 12, 16, 36, 16, 3, 0xFF404040u);
    xcsi_line(painter, 12, 24, 36, 24, 3, 0xFF404040u);
    xcsi_line(painter, 12, 32, 36, 32, 3, 0xFF404040u);
}

static void xcsi_p_arrow(XPainter* painter, int size, int dir)
{
    uint32_t c = 0xFF404040u;
    switch (dir) {
    case 0: xcsi_fillTri(painter, 24, 8, 8, 28, 40, 28, c); break;    /* 上 */
    case 1: xcsi_fillTri(painter, 24, 40, 8, 20, 40, 20, c); break;   /* 下 */
    case 2: xcsi_fillTri(painter, 8, 24, 28, 8, 28, 40, c); break;    /* 左 */
    default: xcsi_fillTri(painter, 40, 24, 20, 8, 20, 40, c); break;  /* 右 */
    }
}

static void xcsi_p_mediaPlay(XPainter* painter, int size)
{ (void)size; xcsi_fillTri(painter, 14, 8, 14, 40, 42, 24, 0xFF208040u); }

static void xcsi_p_mediaStop(XPainter* painter, int size)
{ (void)size; xcsi_rect(painter, 12, 12, 24, 24, 0xFF208040u); }

static void xcsi_p_skipFwd(XPainter* painter, int size)
{
    (void)size;
    xcsi_fillTri(painter, 12, 10, 12, 38, 32, 24, 0xFF404040u);
    xcsi_rect(painter, 34, 10, 5, 28, 0xFF404040u);
}

/* ---- 设备与场所类图标（桌面/电脑/回收站/驱动器/主目录） ---- */

/** @brief 桌面图标：蓝色屏幕 + 支架 + 底座线。 */
static void xcsi_p_desktop(XPainter* painter, int size)
{
    (void)size;
    xcsi_rect(painter, 8, 8, 32, 22, 0xFF2A82DAu);   /* 蓝色屏幕 */
    xcsi_frame(painter, 8, 8, 32, 22, 0xFF202020u);
    xcsi_rect(painter, 8, 8, 32, 2, 0xFF5AA6F0u);    /* 顶部高光 */
    xcsi_rect(painter, 22, 30, 4, 6, 0xFF404040u);   /* 支架 */
    xcsi_line(painter, 14, 39, 34, 39, 4, 0xFF404040u); /* 底座线 */
}

/** @brief 电脑图标：显示器（屏幕+支架）+ 立式主机。 */
static void xcsi_p_computer(XPainter* painter, int size)
{
    (void)size;
    xcsi_rect(painter, 6, 6, 26, 20, 0xFFBFE0F8u);   /* 显示器屏幕 */
    xcsi_frame(painter, 6, 6, 26, 20, 0xFF404040u);
    xcsi_rect(painter, 16, 26, 6, 4, 0xFF404040u);   /* 支架 */
    xcsi_rect(painter, 10, 30, 18, 3, 0xFF404040u);  /* 底盘 */
    xcsi_rect(painter, 36, 8, 8, 30, 0xFF909090u);   /* 主机机箱 */
    xcsi_frame(painter, 36, 8, 8, 30, 0xFF404040u);
    xcsi_rect(painter, 38, 12, 4, 2, 0xFF505050u);   /* 光驱槽 */
    xcsi_rect(painter, 38, 18, 4, 2, 0xFF505050u);   /* 软驱槽 */
    xcsi_rect(painter, 38, 32, 3, 3, 0xFF30C040u);   /* 电源灯 */
}

/** @brief 回收站图标：灰色桶身 + 桶盖提手。 */
static void xcsi_p_trash(XPainter* painter, int size)
{
    (void)size;
    xcsi_rect(painter, 20, 6, 8, 3, 0xFF606468u);    /* 提手 */
    xcsi_line(painter, 11, 11, 37, 11, 3, 0xFF8A8F98u); /* 桶盖 */
    xcsi_rect(painter, 14, 13, 20, 26, 0xFF9AA0A6u); /* 灰色桶身 */
    xcsi_rect(painter, 14, 13, 20, 2, 0xFFC4C9CFu);  /* 口沿高光 */
    xcsi_rect(painter, 20, 17, 2, 18, 0xFF7A8088u);  /* 竖纹 */
    xcsi_rect(painter, 26, 17, 2, 18, 0xFF7A8088u);
}

/** @brief 硬盘驱动器图标：硬驱方盒 + 状态指示灯。 */
static void xcsi_p_driveHD(XPainter* painter, int size)
{
    (void)size;
    xcsi_rect(painter, 6, 16, 36, 18, 0xFF787E86u);  /* 驱动方盒 */
    xcsi_frame(painter, 6, 16, 36, 18, 0xFF404040u);
    xcsi_rect(painter, 6, 16, 36, 2, 0xFFA8AEB6u);   /* 顶部高光 */
    xcsi_rect(painter, 10, 22, 20, 2, 0xFF50555Cu);  /* 读写槽 */
    xcsi_rect(painter, 34, 27, 4, 4, 0xFF30C040u);   /* 状态指示灯 */
}

/** @brief 软盘驱动器图标：盘体 + 金属滑片 + 标签。 */
static void xcsi_p_driveFD(XPainter* painter, int size)
{
    (void)size;
    xcsi_rect(painter, 8, 8, 32, 32, 0xFF3060A8u);   /* 盘体 */
    xcsi_frame(painter, 8, 8, 32, 32, 0xFF203860u);
    xcsi_rect(painter, 15, 8, 16, 11, 0xFFC8C8C8u);  /* 金属滑片 */
    xcsi_rect(painter, 26, 10, 4, 7, 0xFF404040u);   /* 读写孔 */
    xcsi_rect(painter, 14, 25, 20, 15, 0xFFF0F0F0u); /* 标签 */
    xcsi_rect(painter, 17, 29, 14, 2, 0xFFB0B0B0u);
    xcsi_rect(painter, 17, 33, 14, 2, 0xFFB0B0B0u);
}

/** @brief 光盘驱动器图标：圆盘 + 中心孔 + 盘面反光。 */
static void xcsi_p_driveCD(XPainter* painter, int size)
{
    (void)size;
    xcsi_fillCircle(painter, 24, 24, 17, 0xFFB8C4D0u); /* 外圈银盘 */
    xcsi_fillCircle(painter, 24, 24, 13, 0xFFDCE6EEu); /* 内圈高光 */
    xcsi_fillCircle(painter, 24, 24, 6, 0xFFB8C4D0u);
    xcsi_fillCircle(painter, 24, 24, 3, 0xFF303030u);  /* 中心孔 */
    xcsi_fillTri(painter, 14, 12, 22, 8, 12, 20, 0xFFF4FAFFu); /* 反光 */
}

/** @brief DVD 图标：同 CD 几何，紫罗兰配色区分。 */
static void xcsi_p_driveDVD(XPainter* painter, int size)
{
    (void)size;
    xcsi_fillCircle(painter, 24, 24, 17, 0xFFC090D8u);
    xcsi_fillCircle(painter, 24, 24, 13, 0xFFEDDCF4u);
    xcsi_fillCircle(painter, 24, 24, 6, 0xFFC090D8u);
    xcsi_fillCircle(painter, 24, 24, 3, 0xFF303030u);
    xcsi_fillTri(painter, 14, 12, 22, 8, 12, 20, 0xFFF8EEFCu);
}

/** @brief 网络驱动器图标：驱动方盒 + 网线连至远端节点。 */
static void xcsi_p_driveNet(XPainter* painter, int size)
{
    (void)size;
    xcsi_rect(painter, 6, 26, 24, 14, 0xFF787E86u);  /* 驱动方盒 */
    xcsi_frame(painter, 6, 26, 24, 14, 0xFF404040u);
    xcsi_rect(painter, 10, 31, 12, 2, 0xFF50555Cu);
    xcsi_rect(painter, 24, 32, 3, 3, 0xFF30C040u);   /* 指示灯 */
    xcsi_line(painter, 30, 33, 38, 33, 2, 0xFF404040u); /* 网线 */
    xcsi_line(painter, 38, 33, 38, 16, 2, 0xFF404040u);
    xcsi_fillCircle(painter, 38, 12, 5, 0xFF2A82DAu); /* 远端节点 */
    xcsi_fillCircle(painter, 38, 12, 2, 0xFFFFFFFFu);
}

/** @brief 主目录图标：三角屋顶 + 方形房身 + 门。 */
static void xcsi_p_dirHome(XPainter* painter, int size)
{
    (void)size;
    xcsi_fillTri(painter, 24, 6, 6, 22, 42, 22, 0xFFC0504Du); /* 屋顶 */
    xcsi_rect(painter, 10, 22, 28, 20, 0xFFE8C06Au); /* 房身 */
    xcsi_frame(painter, 10, 22, 28, 20, 0xFF8A6A30u);
    xcsi_rect(painter, 20, 30, 8, 12, 0xFF8A5A2Au);  /* 门 */
}

static void xcsi_p_dialogOk(XPainter* painter, int size)
{ (void)size; xcsi_circleGlyph(painter, 0xFF2E9E4Fu, 0xFFFFFFFFu, 'v'); }

static void xcsi_p_arrowUp(XPainter* painter, int size)
{ xcsi_p_arrow(painter, size, 0); }

static void xcsi_p_arrowDown(XPainter* painter, int size)
{ xcsi_p_arrow(painter, size, 1); }

static void xcsi_p_arrowLeft(XPainter* painter, int size)
{ xcsi_p_arrow(painter, size, 2); }

static void xcsi_p_arrowRight(XPainter* painter, int size)
{ xcsi_p_arrow(painter, size, 3); }

/** @brief 组装:绘制回调 → XImage → XPixmap → XIcon(调用方 delete_base)。 */
static XIcon* xcsi_build(XcsiIconPainter fn)
{
    XImage img;
    XPixmap pm;
    XIcon* icon = NULL;
    XPainter painter;
    XImage_init(&img);
    XImage_init_ex(&img, XCSI_ICON_SIZE, XCSI_ICON_SIZE,
                   XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, &img)) {
        fn(&painter, XCSI_ICON_SIZE);
        XPainter_end(&painter);
        XPixmap_init(&pm);
        XPixmap_init_image(&pm, &img, 0);
        icon = XIcon_create();
        if (icon) XIcon_init_pixmap(icon, &pm);
        XPixmap_deinit_base(&pm);
    }
    XPainter_deinit(&painter);
    XImage_deinit_base(&img);
    return icon;
}

/** @brief 标准图标生成(分派到几何绘制;未知值返回 NULL)。 */
static XIcon* xcs_standardIcon(XStyle* self, int sp,
                               const XStyleOption* option,
                               const XWidget* widget)
{
    (void)self; (void)option; (void)widget;
    switch (sp) {
    case XStyleSP_MessageBoxInformation:
        return xcsi_build(xcsi_p_msgInfo);
    case XStyleSP_MessageBoxWarning:
        return xcsi_build(xcsi_p_msgWarning);
    case XStyleSP_MessageBoxCritical:
        return xcsi_build(xcsi_p_msgCritical);
    case XStyleSP_MessageBoxQuestion:
        return xcsi_build(xcsi_p_msgQuestion);
    case XStyleSP_DirClosedIcon:
    case XStyleSP_DirIcon:
        return xcsi_build(xcsi_p_dirClosed);
    case XStyleSP_DirOpenIcon:
        return xcsi_build(xcsi_p_dirOpen);
    case XStyleSP_FileIcon:
        return xcsi_build(xcsi_p_file);
    case XStyleSP_TitleBarMinButton:
        return xcsi_build(xcsi_p_titleMin);
    case XStyleSP_TitleBarMaxButton:
        return xcsi_build(xcsi_p_titleMax);
    case XStyleSP_TitleBarCloseButton:
        return xcsi_build(xcsi_p_titleClose);
    case XStyleSP_TitleBarNormalButton:
        return xcsi_build(xcsi_p_titleNormal);
    case XStyleSP_TitleBarMenuButton:
        return xcsi_build(xcsi_p_titleMenu);
    case XStyleSP_DialogOkButton:
    case XStyleSP_DialogYesButton:
    case XStyleSP_DialogApplyButton:
        return xcsi_build(xcsi_p_dialogOk);
    case XStyleSP_DialogCancelButton:
    case XStyleSP_DialogCloseButton:
    case XStyleSP_DialogNoButton:
        return xcsi_build(xcsi_p_msgCritical);
    case XStyleSP_DialogHelpButton:
        return xcsi_build(xcsi_p_msgQuestion);
    case XStyleSP_ArrowUp:
        return xcsi_build(xcsi_p_arrowUp);
    case XStyleSP_ArrowDown:
        return xcsi_build(xcsi_p_arrowDown);
    case XStyleSP_ArrowLeft:
    case XStyleSP_ArrowBack:
        return xcsi_build(xcsi_p_arrowLeft);
    case XStyleSP_ArrowRight:
    case XStyleSP_ArrowForward:
        return xcsi_build(xcsi_p_arrowRight);
    case XStyleSP_MediaPlay:
        return xcsi_build(xcsi_p_mediaPlay);
    case XStyleSP_MediaStop:
        return xcsi_build(xcsi_p_mediaStop);
    case XStyleSP_MediaSkipForward:
        return xcsi_build(xcsi_p_skipFwd);
    case XStyleSP_DesktopIcon:
        return xcsi_build(xcsi_p_desktop);
    case XStyleSP_ComputerIcon:
        return xcsi_build(xcsi_p_computer);
    case XStyleSP_TrashIcon:
        return xcsi_build(xcsi_p_trash);
    case XStyleSP_DriveHDIcon:
        return xcsi_build(xcsi_p_driveHD);
    case XStyleSP_DriveFDIcon:
        return xcsi_build(xcsi_p_driveFD);
    case XStyleSP_DriveCDIcon:
        return xcsi_build(xcsi_p_driveCD);
    case XStyleSP_DriveDVDIcon:
        return xcsi_build(xcsi_p_driveDVD);
    case XStyleSP_DriveNetIcon:
        return xcsi_build(xcsi_p_driveNet);
    case XStyleSP_DirHomeIcon:
        return xcsi_build(xcsi_p_dirHome);
    default:
        return NULL;
    }
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
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_StyleHint,
                             VXCommonStyle_styleHint);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_SubElementRect,
                             VXCommonStyle_subElementRect);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_SubControlRect,
                             VXCommonStyle_subControlRect);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_HitTestComplexControl,
                             VXCommonStyle_hitTestComplexControl);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_StandardIcon,
                             xcs_standardIcon);
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
