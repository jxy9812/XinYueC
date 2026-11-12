/**
 * @file       XWindowDecorationAlignTest.c
 * @brief      XWindowDecoration 内部一致性契约测试。
 * @details    本机 Qt 6.8 **无对应类**（窗口装饰在 Qt 侧由 QStyle 的
 *             CC_TitleBar/PE_FrameWindow 与窗口管理器共同承担，不存在
 *             QWindowDecoration），故本文件只做本模块的一致性对齐——
 *             全部断言都取「与运行模式无关」的规则（不依赖是否 CSD、
 *             是否有 fbdev 显示驱动）：
 *             - 只有**顶层**控件可被装饰（xwd_activeForImpl
 *               `if (!top->m_isWindow) return false;`，等值于 Qt 里
 *               QWidget::isWindow() 才有 WM 帧）；
 *             - **瞬态类型永不装饰**（Popup/ToolTip/SplashScreen/Desktop，
 *               XWindowDecoration.c:2018-2026）；
 *             - **FramelessWindowHint 不装饰**（同文件 :2027，等值 Qt 的
 *               Qt::FramelessWindowHint 交应用自绘）；
 *             - 所有入口对 NULL 零开销安全（返回 false/零边距/无动作）。
 *             未装饰时的保留边距恒为零（marginsFor 的 activeFor 闸）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON

#include "XWindowDecoration.h"
#include "XTitleBar.h"
#include "XWidget.h"
#include "XObject.h"
#include "XClass.h"
#include "XGeometry.h"

/* ==================== ① NULL 入口全部安全 ==================== */

XQ_TEST(XWindowDecoration, nullEntriesAreSafe)
{
    XMargins m = XWindowDecoration_marginsFor(NULL);

    XQ_CHECK(!XWindowDecoration_activeFor(NULL));
    XQ_CHECK(!XWindowDecoration_activeForFlags(NULL, 0));
    XQ_EQ_I64(m.top, 0);
    XQ_EQ_I64(m.left, 0);
    XQ_EQ_I64(m.right, 0);
    XQ_EQ_I64(m.bottom, 0);
    XQ_CHECK(!XWindowDecoration_isShaded(NULL));
    XQ_CHECK(!XWindowDecoration_handlePointer(NULL, NULL));
    XQ_CHECK(!XWindowDecoration_notifyWindowState(NULL));
    /* 无动作入口：NULL 不崩（装饰模块在控件生命周期钩子里裸调）。 */
    XWindowDecoration_handleLeave(NULL);
    XWindowDecoration_syncWindow(NULL);
    XWindowDecoration_syncBarGeometry(NULL);
    /* [P2-49] XWindowDecoration_draw 已删（纯空壳，函数体只 (void) 三参
     * 短路；条带绘制在 XTitleBar::paintEvent 内自绘）⇒ 该 NULL 冒烟行
     * 一并删除，骨架入口覆盖由上面三个无动作入口保持。 */
    XWindowDecoration_notifyWindowDestroyed(NULL);
    XWindowDecoration_notifyActivation(NULL);
    XWindowDecoration_notifyAppearanceChanged(NULL);

    /* 未开始任何手势、未发生快照搬步。 */
    XQ_CHECK(!XWindowDecoration_gestureActive());
    XQ_CHECK(XWindowDecoration_dragSnapshotStepCount() >= 0);
}

/* ==================== ② 非顶层控件不参与装饰 ==================== */

XQ_TEST(XWindowDecoration, childWidgetIsNeverDecorated)
{
    XWidget* top = XWidget_create(NULL, (XWidgetFlags)XWindowType_Window);
    XWidget* child;
    XMargins m;
    XQ_REQUIRE(top != NULL);
    child = XWidget_create(top, 0);
    XQ_REQUIRE(child != NULL);
    XQ_CHECK(!XWidget_isWindow(child));

    /* 规则：只有顶层窗口才有装饰（非顶层恒 false，与 CSD 模式无关）。 */
    XQ_CHECK(!XWindowDecoration_activeFor(child));
    m = XWindowDecoration_marginsFor(child);
    XQ_EQ_I64(m.top, 0);
    XQ_EQ_I64(m.left, 0);
    XQ_EQ_I64(m.right, 0);
    XQ_EQ_I64(m.bottom, 0);
    XQ_CHECK(!XWindowDecoration_isShaded(child));
    /* 同步钩子对非顶层应为零开销空操作。 */
    XWindowDecoration_syncWindow(child);
    XWindowDecoration_syncBarGeometry(child);
    XQ_CHECK(!XWindowDecoration_notifyWindowState(child));

    XClassDelete((XClass*)child);
    XClassDelete((XClass*)top);
}

/* ==================== ③ 瞬态类型 / 无边框位恒不装饰 ==================== */

XQ_TEST(XWindowDecoration, transientAndFramelessAreExcluded)
{
    XWidget* popup = XWidget_create(NULL, (XWidgetFlags)XWindowType_Popup);
    XWidget* tooltip = XWidget_create(NULL, (XWidgetFlags)XWindowType_ToolTip);
    XWidget* splash = XWidget_create(NULL, (XWidgetFlags)XWindowType_SplashScreen);
    XWidget* frameless = XWidget_create(
        NULL, (XWidgetFlags)(XWindowType_Window |
                             XWindowType_FramelessWindowHint));
    XQ_REQUIRE(popup != NULL && tooltip != NULL && splash != NULL &&
               frameless != NULL);

    /* 瞬态类型永不装饰（XWindowDecoration.c:2018-2026 白名单之外）。 */
    XQ_CHECK(!XWindowDecoration_activeFor(popup));
    XQ_CHECK(!XWindowDecoration_activeFor(tooltip));
    XQ_CHECK(!XWindowDecoration_activeFor(splash));
    /* FramelessWindowHint：应用自绘，框架不再加条（同文件 :2027）。 */
    XQ_CHECK(!XWindowDecoration_activeFor(frameless));
    /* 按 flags 预测的变体同一判定内核（setWindowFlags 动态路径专用）。 */
    XQ_CHECK(!XWindowDecoration_activeForFlags(
        popup, (XWindowFlags)XWindowType_Popup));

    XClassDelete((XClass*)popup);
    XClassDelete((XClass*)tooltip);
    XClassDelete((XClass*)splash);
    XClassDelete((XClass*)frameless);
}

/* ==================== ④ 未装饰窗口的条带几何同步恒零边距 ============== */

XQ_TEST(XWindowDecoration, undecoratedWindowKeepsZeroMargins)
{
    XWidget* win = XWidget_create(NULL, (XWidgetFlags)XWindowType_Window);
    XMargins m;
    XRect panel;
    XRect oldG;
    XRect newG;
    XQ_REQUIRE(win != NULL);
    XWidget_resize(win, 400, 300);

    if (!XWindowDecoration_activeFor(win)) {
        /* 无显示/交 WM 模式：保留边距恒零（marginsFor 的 activeFor 闸）。 */
        m = XWindowDecoration_marginsFor(win);
        XQ_EQ_I64(m.top, 0);
        XQ_EQ_I64(m.left, 0);
        XQ_EQ_I64(m.right, 0);
        XQ_EQ_I64(m.bottom, 0);
    }
    /* 条带重钉与外观通知对未承载条的窗口为空操作（不创建状态/不崩）。 */
    XWindowDecoration_syncBarGeometry(win);
    XWindowDecoration_notifyAppearanceChanged(win);

    /* 让位条带归位：空参数与未装饰窗口均安全（fbdev 专用语义，其它平台
     * 下 blit/fill 为 no-op）。 */
    XRect_init(&oldG, 0, 0, 100, 80);
    XRect_init(&newG, 20, 10, 100, 80);
    XWindowDecoration_restoreExposeStrips(&oldG, &newG, NULL);
    XWindowDecoration_restoreExposeStripsEx(&oldG, &newG, NULL, false);
    (void)XWindowDecoration_fbdevPanelRect(&panel); /* 非 fbdev 下恒 false */

    XClassDelete((XClass*)win);
}

#endif /* XGUI_ON && XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON */
