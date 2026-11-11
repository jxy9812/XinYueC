/**
 * @file       XToolTip.c
 * @brief      工具提示静态 API 类实现（对标 Qt 6.8 QToolTip 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部为模块级静态状态，
 *             全局提示控件惰性创建/复用。无窗口环境安全退化（仅存储
 *             状态不显示，见函数级注释）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XToolTip.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XTimer.h"
#include "XWidget_Protected.h"
#include "XGuiApplication.h" /* 锚定控件所在屏（XGuiApplication_screenAt）。 */
#include "XScreen.h"         /* 提示落位钳制（XScreen_availableGeometry）。 */

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
/** @brief 淡出隐藏定时器（对标 QTipLabel::hideTimer：hideTip 启动
 *         300ms 后真正隐藏，qtooltip.cpp:251-255）。 */
static XTimer* s_hideTimer = NULL;
/** @brief 淡出进行中标志（等价 QTimer::isActive 守卫：重复 hideText
 *         不重置剩余时长，qtooltip.cpp:253 `if (!hideTimer.isActive())`）。 */
static bool s_tooltipHiding = false;

/** @brief 立即隐藏（对标 QTipLabel::hideTipImmediately，
 *         qtooltip.cpp:257-262：close + deleteLater；本实现收起窗口并
 *         清空模块状态）。 */
static void xtooltip_hideImmediately(void)
{
    if (s_hideTimer)
        XTimer_stop_base(s_hideTimer);
    s_tooltipHiding = false;
    /* 到期/隐藏即停表（对标 QTipLabel::timerEvent 触发后双 stop，
     * qtooltip.cpp:277-278；复用路径 restartExpireTimer 亦先 stop）。 */
    if (s_expireTimer)
        XTimer_stop_base(s_expireTimer);
    if (s_tooltipWidget) {
        XWidget_hide(s_tooltipWidget);
    }
    if (s_tooltipText) {
        XClassDelete((XClass*)s_tooltipText);
        s_tooltipText = NULL;
    }
    s_tooltipVisible = false;
    s_tooltipRelatedWidget = NULL;
    s_tooltipMsecShowTime = -1;
}

/** @brief 淡出定时器回调：300ms 到点后真正隐藏。 */
static void xtooltip_hideCb(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    xtooltip_hideImmediately();
}

/** @brief 启动淡出隐藏定时（对标 QTipLabel::hideTip：
 *         `if (!hideTimer.isActive()) hideTimer.start(300, this);`）。
 * @note  已在进行中的淡出不重置剩余时长（Qt 同）；调用方用
 *        s_tooltipVisible 闸保证只在可见态进入一次。 */
static void xtooltip_startHideTimer(void)
{
    if (s_tooltipHiding)
        return; /* Qt：hideTimer 已活动则不重启（不延长淡出）。 */
    if (!s_hideTimer) {
        s_hideTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!s_hideTimer) {
            /* 无定时器设施（无调度器环境）：退化为立即隐藏，保证状态
             * 不悬挂（Qt 在无事件循环时同样收不到定时器）。 */
            xtooltip_hideImmediately();
            return;
        }
        XTimer_setSingleShot(s_hideTimer, true);
        XTimer_setAutoDelete(s_hideTimer, false);
        XTimer_setTimerCallback(s_hideTimer, xtooltip_hideCb);
    }
    s_tooltipHiding = true;
    XTimer_setTimeout(s_hideTimer, 300);
    XTimer_start_base(s_hideTimer);
}

/** @brief 取消未决的淡出隐藏（重新显示被复用/改文本时调用，对标
 *         QTipLabel::restartExpireTimer 的 `hideTimer.stop()`）。 */
static void xtooltip_stopHideTimer(void)
{
    if (s_hideTimer)
        XTimer_stop_base(s_hideTimer);
    s_tooltipHiding = false;
}

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
        XClassDelete((XClass*)s_tooltipText);
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
    /* 到期是"立即收起"而非淡出（对标 QTipLabel::timerEvent 对
     * hideTimer/expireTimer 任一触发都调 hideTipImmediately，
     * qtooltip.cpp:275-280）。 */
    xtooltip_hideImmediately();
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
    int px = x;
    int py = y;
    if (!text || XContainer_size_base((const XContainer*)text) == 0) {
        /* 空文本等价 hideText（对标 QToolTip::showText 的
         * text.isEmpty() 分支，qtooltip.cpp:439-446：空文本隐藏当前
         * 提示并返回；NULL 与零长度 XString 同路径）。 */
        XToolTip_hideText();
        return;
    }
    /* 对标 Qt 复用分支（qtooltip.cpp:444-455）+ QTipLabel::tipChanged
     * （qtooltip.cpp:402-411）：提示已可见且文本相同、关联控件相同
     * （rect 本实现未建模，等价空 rect → tipChanged 返 false）时直接
     * 返回不移动不重摆——文档口径「text 与当前提示相同则提示不移动」
     * （qtooltip.cpp:437-439）。 */
    if (s_tooltipVisible && s_tooltipText &&
        XString_equals(s_tooltipText, text, XChar_CaseSensitive) &&
        s_tooltipRelatedWidget == widget)
        return;
    xtooltip_ensureWidget();
    if (!s_tooltipWidget) {
        /* 无窗口环境：仅存储状态，安全退化不显示。 */
        xtooltip_stopHideTimer(); /* 取消可能的未决淡出（Qt reuseTip 同） */
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
    /* 改文本/换锚点复用提示：取消未决淡出（对标 QTipLabel::reuseTip →
     * restartExpireTimer 的 `hideTimer.stop()`，qtooltip.cpp:167/178）。 */
    xtooltip_stopHideTimer();
    xtooltip_setText(text);
    xtooltip_applyAppearance();
    xtooltip_updateSize();
    /* 锚定落位（设计口径 2026-11-07，偏离 Qt 的鼠标点/显式点落位）：
     * 调用方给出悬停控件全局区域（rect）时，提示悬于控件上方——底边
     * 对齐控件上缘（GAP 2px），不再覆盖被悬停的控件本身；上方放不下
     * （越出屏幕工作区上缘）降级到控件下方，下放再越出下缘则按工作区
     * 下缘回收；水平与控件左缘对齐并钳制在工作区内。无 rect 保持 Qt
     * 原语义：按传入坐标落位（QToolTip::showText 显式 pos，API 测试
     * 路径与无宿主退化路径不受影响）。 */
    if (rect) {
        XPoint anchor;
        XRect avail;
        XScreen* screen;
        int tipW = XWidget_width(s_tooltipWidget);
        int tipH = XWidget_height(s_tooltipWidget);
        const int gap = 2;
        anchor.x = rect->x;
        anchor.y = rect->y;
        avail.x = 0; avail.y = 0;
        avail.width = 0; avail.height = 0;
        screen = XGuiApplication_screenAt(&anchor);
        if (screen)
            avail = XScreen_availableGeometry(screen);
        px = rect->x;
        py = rect->y - tipH - gap;
        if (avail.height > 0 && py < avail.y)
            py = rect->y + rect->height + gap;  /* 上方放不下→控件下方 */
        if (avail.height > 0 && py + tipH > avail.y + avail.height)
            py = avail.y + avail.height - tipH; /* 下方越缘→工作区下缘回收 */
        if (avail.width > 0) {
            if (px + tipW > avail.x + avail.width)
                px = avail.x + avail.width - tipW;
            if (px < avail.x) px = avail.x;
        } else if (px < 0) {
            px = 0;
        }
    }
    XWidget_move_2(s_tooltipWidget, &(XPoint){px, py});
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
    XClassDelete((XClass*)text);
}

void XToolTip_hideText(void)
{
    /* 对标 QToolTip::hideText（qtooltip.cpp:251-255）：
     *   `if (!QTipLabel::instance) return; QTipLabel::instance->hideTip();`
     *   → `if (!hideTimer.isActive()) hideTimer.start(300, this);`
     * ——隐藏**延迟 300ms**（淡出动画时长）。淡出期间 isVisible() 仍为
     * true、text() 仍可取（实例尚未 deleteLater）；300ms 到点才真正收起
     * 并清状态。本机 Qt 6.8 探针：hideText 后立即 isVisible()==1，
     * 350ms 后为 0。gaps 211 的旧实现为立即 XWidget_hide。 */
    if (!s_tooltipVisible)
        return; /* 不可见/无提示：Qt 的 instance 为空分支，无操作。 */
    xtooltip_startHideTimer();
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
        XClassCopy(&out, &s_tooltipFont);
    return out;
}

void XToolTip_setFont(const XFont* font)
{
    XFont temp;
    /* NULL 处理口径（头文件同文）：忽略并保持原状态。Qt 的
     * setFont(const QFont&) 为引用入参无 NULL 概念（qtooltip.cpp:570-573
     * 无条件赋值，默认构造 QFont 即回落应用字体）；本实现的 NULL 扩展
     * 语义取「显式忽略」分支——不做隐藏的复位副作用，调用方以默认
     * 构造 XFont 传参即可表达 Qt 的回落语义。 */
    if (!font) return;
    XFont_init(&temp);
    XClassCopy(&temp, font);
    if (s_tooltipFontSet)
        XClassMove(&s_tooltipFont, &temp);
    else {
        XFont_init(&s_tooltipFont);
        XClassMove(&s_tooltipFont, &temp);
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
    /* NULL 处理口径（头文件同文）：忽略并保持原状态。Qt 的
     * setPalette(const QPalette&) 为引用入参无 NULL 概念
     * （qtooltip.cpp:558-563 无条件赋值，默认构造 QPalette 即回落
     * 默认配色）；本实现的 NULL 扩展语义取「显式忽略」分支——调用方
     * 以 XPalette_create() 默认值传参即可表达 Qt 的回落语义。 */
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

#endif /* XGUI_ON */
