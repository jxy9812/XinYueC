/**
 * @file       XToolTip.c
 * @brief      工具提示静态 API 类实现（对标 Qt 6.8 QToolTip 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部为模块级静态状态，
 *             全局提示控件惰性创建/复用。无窗口环境安全退化（仅存储
 *             状态不显示，见函数级注释）。
 * @author     XinYueC 团队
 */

#include "XToolTip.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#if XWIDGET_ON
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
#include "XLabel.h"
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/* ==================== 模块级静态状态 ==================== */

/** @brief 当前提示文本（对象拥有；NULL 表示无文本）。 */
static XString* s_tooltipText = NULL;
/** @brief 当前可见标志。 */
static bool s_tooltipVisible = false;
/** @brief 当前提示位置（屏幕坐标）。 */
static int s_tooltipX = 0;
static int s_tooltipY = 0;
/** @brief 关联控件（借用；不拥有）。 */
static XWidget* s_tooltipRelatedWidget = NULL;
/** @brief 显示时长记录（本实现不启动定时器）。 */
static int s_tooltipMsecShowTime = -1;
/** @brief 全局提示控件（对象拥有；惰性创建）。 */
static XWidget* s_tooltipWidget = NULL;
/** @brief 已设置的字体与标志（未设置时返回默认字体）。 */
static XFont s_tooltipFont;
static bool s_tooltipFontSet = false;
#if XPALETTE_ON
/** @brief 已设置的调色板与标志（未设置时返回默认调色板）。 */
static XPalette s_tooltipPalette;
static bool s_tooltipPaletteSet = false;
#endif

/** @brief 确保全局提示控件存在（XLabel 优先；XLABEL_ON=0 时退化为
 *         普通顶层 XWidget，仅存储不显示文本）。 */
static XWidget* xtooltip_ensureWidget(void)
{
    if (s_tooltipWidget) return s_tooltipWidget;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    {
        XLabel* label = XLabel_create(NULL, 0);
        s_tooltipWidget = (XWidget*)label;
    }
#else
    s_tooltipWidget = XWidget_create(NULL, 0);
#endif
    return s_tooltipWidget;
}

/** @brief 应用已存储字体/调色板到提示控件（若已创建）。 */
static void xtooltip_applyAppearance(void)
{
    if (!s_tooltipWidget) return;
    if (s_tooltipFontSet)
        XWidget_setFont(s_tooltipWidget, &s_tooltipFont);
#if XPALETTE_ON
    if (s_tooltipPaletteSet)
        XWidget_setPalette(s_tooltipWidget, &s_tooltipPalette);
#endif
}

/** @brief 释放当前文本并保存新文本（深拷贝）。 */
static void xtooltip_setText(const XString* text)
{
    if (s_tooltipText) {
        XString_delete_base((XClass*)s_tooltipText);
        s_tooltipText = NULL;
    }
    if (text)
        s_tooltipText = XString_create_copy(text);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    if (s_tooltipWidget)
        XLabel_setText((XLabel*)s_tooltipWidget, text);
#endif
}

/* ==================== 静态 API（对标 QToolTip） ==================== */

void XToolTip_showText(int x, int y, const XString* text, XWidget* widget,
                       const XRect* rect, int msecShowTime)
{
    (void)rect;
    if (!text) {
        /* 空文本等价 hideText（对标 QToolTip::hideText 的实现方式）。 */
        XToolTip_hideText();
        return;
    }
    xtooltip_ensureWidget();
    if (!s_tooltipWidget) {
        /* 无窗口环境：仅存储状态，安全退化不显示。 */
        s_tooltipX = x;
        s_tooltipY = y;
        s_tooltipRelatedWidget = widget;
        s_tooltipMsecShowTime = msecShowTime;
        xtooltip_setText(text);
        s_tooltipVisible = true;
        return;
    }
    s_tooltipX = x;
    s_tooltipY = y;
    s_tooltipRelatedWidget = widget;
    s_tooltipMsecShowTime = msecShowTime;
    xtooltip_setText(text);
    xtooltip_applyAppearance();
    XWidget_movePoint(s_tooltipWidget, &(XPoint){x, y});
    XWidget_show(s_tooltipWidget);
    XWidget_raise(s_tooltipWidget);
    s_tooltipVisible = true;
}

void XToolTip_showText_2(int x, int y, const char* utf8, XWidget* widget,
                         const XRect* rect, int msecShowTime)
{
    XString* text;
    if (!utf8) {
        XToolTip_hideText();
        return;
    }
    text = XString_create_utf8(utf8);
    if (!text) return;
    XToolTip_showText(x, y, text, widget, rect, msecShowTime);
    XString_delete_base((XClass*)text);
}

void XToolTip_hideText(void)
{
    if (s_tooltipWidget) {
        XWidget_hide(s_tooltipWidget);
    }
    if (s_tooltipText) {
        XString_delete_base((XClass*)s_tooltipText);
        s_tooltipText = NULL;
    }
    s_tooltipVisible = false;
    s_tooltipRelatedWidget = NULL;
    s_tooltipMsecShowTime = -1;
}

bool XToolTip_isVisible(void)
{
    return s_tooltipVisible;
}

XString* XToolTip_text(void)
{
    if (!s_tooltipText) return NULL;
    return XString_create_copy(s_tooltipText);
}

XFont XToolTip_font(void)
{
    XFont out;
    XFont_init(&out);
    if (s_tooltipFontSet)
        XCopy(&out, &s_tooltipFont);
    return out;
}

void XToolTip_setFont(const XFont* font)
{
    XFont temp;
    if (!font) return;
    XFont_init(&temp);
    XCopy(&temp, font);
    if (s_tooltipFontSet)
        XMove(&s_tooltipFont, &temp);
    else {
        XFont_init(&s_tooltipFont);
        XMove(&s_tooltipFont, &temp);
        s_tooltipFontSet = true;
    }
    if (s_tooltipWidget)
        XWidget_setFont(s_tooltipWidget, &s_tooltipFont);
}

XPalette XToolTip_palette(void)
{
#if XPALETTE_ON
    if (!s_tooltipPaletteSet)
        return XPalette_create();
    return s_tooltipPalette;
#else
    XPalette out;
    out.m_disabled = 0;
    return out;
#endif
}

void XToolTip_setPalette(const XPalette* palette)
{
#if XPALETTE_ON
    if (!palette) return;
    if (!s_tooltipPaletteSet) {
        XPalette_init_default(&s_tooltipPalette);
        s_tooltipPaletteSet = true;
    }
    XPalette_copy(&s_tooltipPalette, palette);
    if (s_tooltipWidget)
        XWidget_setPalette(s_tooltipWidget, &s_tooltipPalette);
#else
    (void)palette;
#endif
}

#endif /* XWIDGET_ON */
