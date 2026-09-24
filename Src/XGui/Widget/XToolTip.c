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
#include "XTimer.h"
#include "XWidget_Protected.h"

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
/** @brief 到期自动隐藏定时器（对标 QTipLabel::expireTimer，
 *         qtooltip.cpp:103）：showText 时按 msecShowTime 或默认
 *         「10000 + 40×max(0,字符数−100)」重启（restartExpireTimer，
 *         qtooltip.cpp:165），触发即整块收起（timerEvent→
 *         hideTipImmediately，qtooltip.cpp:275/257）。 */
static XTimer* s_expireTimer = NULL;

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
    /* 对标 Qt：QTipLabel 以 Qt::ToolTip 窗口类型构造（qtooltip.cpp:144
     * `QLabel(w, Qt::ToolTip | Qt::BypassGraphicsProxyWidget)`）。
     * 修复 night #35②（复扫实锤 Depth:32+缓冲全黑）：此前默认 Window
     * 类型不在平台层瞬态弹层族内，XPlatformNativeWindow_create 的
     * 屏幕默认 visual/depth 修复（Popup/ToolTip/Splash 集合，XWindow.h
     * XWindowType_ToolTip）被错过，depth-32 弹层被根合成排除→永不上
     * 屏；显式设 ToolTip 类型后走 override-redirect + 默认 visual，
     * 并落 _NET_WM_WINDOW_TYPE_TOOLTIP。须在建窗前设置（本控件惰性
     * 建窗于首次 showText）。 */
    XWidget_setWindowFlags(s_tooltipWidget,
                           (XWidgetFlags)XWindowType_ToolTip);
    /* 提示底填充（复扫-3 #35 残留）：XLabel 无 autoFillBackground 不画
     * 背景，新建窗 backing store 无内容→上屏纯黑条。开自动填充走
     * Window 角色底色+黑字（对标 QTipLabel 浅底黑字样式口径）。 */
    XWidget_setAutoFillBackground(s_tooltipWidget, true);
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

/** @brief 按 sizeHint 调整提示窗口尺寸（对标 Qt QTipLabel::updateSize
 *         qtooltip.cpp:193：showText 前 resize(sizeHint())——提示窗口
 *         尺寸必须随文本重算）。
 *  @details 修复 night #35：此前 showText 只 move+show、从不 resize，
 *           提示窗口保持创建默认几何（≈30 宽 ×375 高），文本按窄长
 *           窗口逐字换行渲染成竖条。XGui 无 Qt 的 mightBeRichText/
 *           屏幕宽度换行回退（@note），取 sizeHint 直用；宽度 +1 与
 *           Qt 的 extra(1,0) 同口径。 */
static void xtooltip_updateSize(void)
{
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XSize hint;
    if (!s_tooltipWidget) return;
    hint = XLabel_sizeHint((const XLabel*)s_tooltipWidget);
    if (hint.width <= 0 || hint.height <= 0) return;
    XWidget_resize(s_tooltipWidget, hint.width + 1, hint.height);
#endif
}

/** @brief 到期自动隐藏回调（对标 QTipLabel::timerEvent 的 expireTimer
 *         分支，qtooltip.cpp:275-280：hideTimer/expireTimer 任一触发都
 *         hideTipImmediately）。单发定时器触发后框架自停（VXObject_
 *         timerEvent 对 singleShot 的处理），此处只需收起提示。 */
static void xtooltip_expireCb(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    XToolTip_hideText();
}

/** @brief 重启到期定时器（对标 QTipLabel::restartExpireTimer，
 *         qtooltip.cpp:165-173：msecDisplayTime>0 直接采用，否则按
 *         文本长度 10000+40×max(0,len−100) 毫秒；文本越长驻留越久）。
 *         无调度器（无窗口环境）时 XTimer_start_base 内部注册失败，
 *         安全退化为不自动隐藏。 */
static void xtooltip_restartExpire(int msecShowTime)
{
    if (!s_expireTimer) {
        s_expireTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!s_expireTimer) return;
        XTimer_setSingleShot(s_expireTimer, true);
        XTimer_setAutoDelete(s_expireTimer, false);
        XTimer_setTimerCallback(s_expireTimer, xtooltip_expireCb);
    }
    if (msecShowTime > 0) {
        XTimer_setTimeout(s_expireTimer, (size_t)msecShowTime);
    } else {
        size_t chars = s_tooltipText
                           ? XContainer_size_base((const XContainer*)s_tooltipText)
                           : 0;
        size_t extra = chars > 100 ? chars - 100 : 0;
        XTimer_setTimeout(s_expireTimer, 10000 + 40 * extra);
    }
    XTimer_start_base(s_expireTimer);
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
    xtooltip_updateSize();
    XWidget_movePoint(s_tooltipWidget, &(XPoint){x, y});
    XWidget_show(s_tooltipWidget);
    XWidget_raise(s_tooltipWidget);
    /* 独立顶层窗口无宿主帧泵：主动完成首帧绘制上屏（参照 XMenu/
     * XComboBox 弹层口径），悬停唤起的提示必须当帧可见。 */
    XWidget_flushBackingStore(s_tooltipWidget, NULL);
    /* 到期自动隐藏（night #35：msecShowTime 此前仅存储不生效）。 */
    xtooltip_restartExpire(msecShowTime);
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
    /* 到期/隐藏即停表（对标 QTipLabel::timerEvent 触发后双 stop，
     * qtooltip.cpp:277-278；复用路径 restartExpireTimer 亦先 stop）。 */
    if (s_expireTimer)
        XTimer_stop_base(s_expireTimer);
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
