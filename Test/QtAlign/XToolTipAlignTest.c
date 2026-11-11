/**
 * @file       XToolTipAlignTest.c
 * @brief      XToolTip × Qt 6.8 QToolTip 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtooltip.h
 *             与本机 Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/kernel/qtooltip.cpp。
 *             关键语义：
 *             - showText 的空文本分支等价 hideText（qtooltip.cpp:441-446）；
 *             - 已可见且「文本＋关联控件」未变时复用不移动
 *               （qtooltip.cpp:444-457 + QTipLabel::tipChanged）；
 *             - 提示控件是**全局单例**（QTipLabel::instance，qtooltip.cpp:150-151），
 *               首次 showText 惰性建立；
 *             - font/palette 是模块级静态状态，setFont/setPalette 立即
 *               作用于已存在的提示控件（qtooltip.cpp:558-573）；
 *             - msecShowTime 接入到期定时（QTipLabel::restartExpireTimer，
 *               qtooltip.cpp:167-176）；
 *             - hideText 走 QTipLabel::hideTip 的 300ms 延迟隐藏
 *               （qtooltip.cpp:251-255）：淡出期间 isVisible() 仍为 true、
 *               text() 仍可取，到点才隐藏并清空（本机 Qt 6.8 探针：
 *               hideText 后立即可见=1，350ms 后=0；gaps 211）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON

#include "XToolTip.h"
#include "XWidget.h"
#include "XString.h"
#include "XFont.h"
#include "XPalette.h"
#include "XColor.h"
#include "XClass.h"
#include "XThread.h"
#include "XCoreApplication.h"

/** @brief 等待淡出定时器到点（300ms hideTimer；上限 2s 防呆）。
 * @details 定时器挂在主线程时间轮上（XTimeWheelGroup_global_init 把全局
 *          组 moveToThread(mainThread)）⇒ 每步补一拍 processEvents 让
 *          到期回调真正派发，同时给墙钟时间前进。
 * @return 已不可见返回 true。 */
static bool xtip_waitHidden(int maxMs)
{
    int waited = 0;
    while (XToolTip_isVisible() && waited < maxMs) {
        XThread_msleep(10);
        (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
        waited += 10;
    }
    return !XToolTip_isVisible();
}

/** @brief 取 XToolTip_text() 的副本并释放，返回是否与期望串相等。 */
static bool xtip_textEquals(const char* expect)
{
    XString* text = XToolTip_text();
    const char* utf8;
    bool same;
    if (!text)
        return expect == NULL;
    utf8 = XString_toUtf8(text);
    same = (utf8 == NULL) ? (expect == NULL)
                          : (expect != NULL && strcmp(utf8, expect) == 0);
    XClassDelete((XClass*)text);
    return same;
}

/* ==================== ① 显示/隐藏生命周期 + 空文本语义 ==================== */

XQ_TEST(XToolTip, showHideLifecycle)
{
    /* 初始不可见、无文本。 */
    XQ_CHECK(!XToolTip_isVisible());
    XQ_CHECK(XToolTip_text() == NULL);

    /* showText 后可见、文本可回读。 */
    XToolTip_showText_2(10, 20, "提示文本", NULL, NULL, -1);
    XQ_CHECK(XToolTip_isVisible());
    XQ_CHECK(xtip_textEquals("提示文本"));

    /* 同文本同控件：复用分支（不改变可见性/文本）。 */
    XToolTip_showText_2(10, 20, "提示文本", NULL, NULL, -1);
    XQ_CHECK(XToolTip_isVisible());
    XQ_CHECK(xtip_textEquals("提示文本"));

    /* 换文本：可见性保持、文本更新。 */
    XToolTip_showText_2(30, 40, "另一条", NULL, NULL, 500);
    XQ_CHECK(XToolTip_isVisible());
    XQ_CHECK(xtip_textEquals("另一条"));

    /* 空文本 = hideText（qtooltip.cpp:441-446 text.isEmpty() 分支）。
     * hideText 走 300ms 延迟隐藏：淡出窗口内仍可见、文本仍可读
     * （Qt 的 QTipLabel 实例尚未 deleteLater；探针 hideText 后立即可见=1）。 */
    XToolTip_showText_2(30, 40, "", NULL, NULL, -1);
    XQ_CHECK(XToolTip_isVisible());
    XQ_CHECK(xtip_textEquals("另一条"));
    /* 到点（300ms）才真正隐藏并清空。 */
    XQ_CHECK(xtip_waitHidden(2000));
    XQ_CHECK(XToolTip_text() == NULL);

    /* XString 主版本；改文本复用会取消未决淡出。 */
    {
        XString* text = XString_create_utf8("再来");
        XQ_REQUIRE(text != NULL);
        XToolTip_showText(5, 6, text, NULL, NULL, -1);
        XQ_CHECK(XToolTip_isVisible());
        XQ_CHECK(xtip_textEquals("再来"));
        XClassDelete((XClass*)text);
    }
    /* hideText：立即仍可见（300ms 淡出），到点后隐藏且清空；已隐藏时
     * 重复调用无操作（Qt 的 instance 为空/定时器已停分支）。 */
    XToolTip_hideText();
    XQ_CHECK(XToolTip_isVisible());
    XQ_CHECK(xtip_textEquals("再来"));
    XQ_CHECK(xtip_waitHidden(2000));
    XQ_CHECK(XToolTip_text() == NULL);
    XToolTip_hideText();
    XQ_CHECK(!XToolTip_isVisible());
    XQ_CHECK(XToolTip_text() == NULL);
}

/* ==================== ② 字体/调色板静态状态 ==================== */

XQ_TEST(XToolTip, fontAndPaletteState)
{
    XFont font;
    XFont fetched;
    XPalette palette;
    XPalette fetchedPalette;
    uint32_t rgba;

    /* 默认字体可取值且可释放（值语义，调用方负责 XClassDeinit）。 */
    font = XToolTip_font();
    XQ_CHECK(XFont_pointSize(&font) >= 0);
    XClassDeinit(&font);

    /* setFont 深拷贝：改源字体不影响已存状态（qtooltip.cpp:570-573）。 */
    XFont_init(&font);
    XFont_setFamily(&font, "XinYueAlignFamily");
    XFont_setPointSize(&font, 17);
    XToolTip_setFont(&font);
    XFont_setPointSize(&font, 33); /* 改源：已存副本不应跟随 */
    fetched = XToolTip_font();
    XQ_EQ_I64(XFont_pointSize(&fetched), 17);
    XQ_CHECK(XFont_family(&fetched) != NULL);
    if (XFont_family(&fetched))
        XQ_EQ_STR(XFont_family(&fetched), "XinYueAlignFamily");
    XClassDeinit(&fetched);
    XClassDeinit(&font);

    /* setFont(NULL)：显式忽略、状态不变（头文件口径）。 */
    XToolTip_setFont(NULL);
    fetched = XToolTip_font();
    XQ_EQ_I64(XFont_pointSize(&fetched), 17);
    XClassDeinit(&fetched);

    /* 调色板往返：默认值可读；写入后回读一致（qtooltip.cpp:558-563）。 */
    palette = XToolTip_palette();
    XPalette_setColor(&palette, XPaletteColorGroup_Current,
                      XPaletteColorRole_ToolTipBase,
                      XColor_create_rgb(1, 2, 3, 255));
    XToolTip_setPalette(&palette);
    fetchedPalette = XToolTip_palette();
    {
        XColor base = XPalette_color(&fetchedPalette,
                                     XPaletteColorGroup_Current,
                                     XPaletteColorRole_ToolTipBase);
        rgba = XColor_rgba(&base);
    }
    XQ_EQ_I64((rgba >> 16) & 0xFF, 1);
    XQ_EQ_I64((rgba >> 8) & 0xFF, 2);
    XQ_EQ_I64(rgba & 0xFF, 3);

    /* 清理：隐藏提示，避免静态状态影响后续用例。 */
    XToolTip_hideText();
    XQ_CHECK(!XToolTip_isVisible());
}

#endif /* XGUI_ON && XWIDGET_ON */
