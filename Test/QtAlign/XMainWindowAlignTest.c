/**
 * @file       XMainWindowAlignTest.c
 * @brief      XMainWindow × Qt 6.8 QMainWindow 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qmainwindow.h
 *             与 Qt 6.8 源码 src/widgets/widgets/qmainwindowlayout.cpp
 *             （jsdelivr v6.8.0 原文）。关键行为用本机 Qt 6.8.0
 *             （QT_QPA_PLATFORM=offscreen）探针实测：
 *             - 中心控件所有权：setCentralWidget 替换时旧控件被隐藏并删除
 *               （探测脚本 flush deferred-delete 后 QPointer 为空），
 *               setCentralWidget(nullptr) 同样删除旧控件，takeCentralWidget
 *               摘父链且不删除（探针「taken parent=(nil)」）；
 *             - 菜单栏/状态栏惰性且唯一：menuWidget()==menuBar()，替换旧栏
 *               被删除（探针 oldalive=0）；
 *             - 工具栏**重复 addToolBar 是移动**：addToolBar(Left,tb) →
 *               addToolBar(Bottom,tb) → addToolBar(tb) 之后 toolBarArea(tb)
 *               依次为 1 / 8 / 4（恒单条登记）；
 *             - removeToolBar 之后 toolBarArea 归 0（NoToolBarArea）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XMAINWINDOW_ON

#include "XMainWindow.h"
#include "XWidget.h"
#include "XObject.h"
#include "XClass.h"
#include "XContainer.h"
#include "XString.h"
#if XDOCKWIDGET_ON
#include "XDockWidget.h"
#endif
#if XTOOLBAR_ON
#include "XToolBar.h"
#endif

/* ==================== ① 构造默认值 + 惰性菜单栏/状态栏唯一性 ============ */

XQ_TEST(XMainWindow, lazyMenuBarAndStatusBar)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XQ_REQUIRE(mw != NULL);

    /* Qt：menuWidget()/statusBar() 之外没有「已存在栏」的惰性创建——
     * 未调用过前 menuWidget() 为 nullptr（qmainwindow.h:82-89）。 */
    XQ_CHECK(XMainWindow_menuWidget(mw) == NULL);
#if XMENUBAR_ON
    XQ_CHECK(XMainWindow_menuBar(mw) != NULL);
    /* 二次调用返回同一实例（唯一性）。 */
    XQ_CHECK(XMainWindow_menuBar(mw) == XMainWindow_menuWidget(mw));
    XQ_CHECK(XMainWindow_menuBar(mw) == XMainWindow_menuBar(mw));
#else
    /* 裁剪配置（XMENUBAR_ON=0）：惰性创建不可用，恒 NULL。 */
    XQ_CHECK(XMainWindow_menuBar(mw) == NULL);
#endif

#if XSTATUSBAR_ON
    XQ_CHECK(XMainWindow_statusBar(mw) != NULL);
    XQ_CHECK(XMainWindow_statusBar(mw) == XMainWindow_statusBar(mw));
#else
    XQ_CHECK(XMainWindow_statusBar(mw) == NULL);
#endif

    /* 外部栏接管：setMenuWidget 与 setMenuBar 等价（Qt 同）。 */
    {
        XWidget* ext = XWidget_create(NULL, 0);
        XQ_REQUIRE(ext != NULL);
        XMainWindow_setMenuWidget(mw, ext);
        XQ_CHECK(XMainWindow_menuWidget(mw) == ext);
        XQ_CHECK(XWidget_parentWidget(ext) == (XWidget*)mw);
        /* 摘回（NULL 清空位置）：外部控件随替换被删除（Qt 探测
         * oldalive=0），故此后不得再解引用 ext。 */
        XMainWindow_setMenuWidget(mw, NULL);
        XQ_CHECK(XMainWindow_menuWidget(mw) == NULL);
    }

    XClassDelete(mw);
}

/* ==================== ② 中心控件所有权（替换删除 / take 摘父链） ======= */

XQ_TEST(XMainWindow, centralWidgetOwnership)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XWidget* c1;
    XWidget* c2;
    XWidget* taken;
    XQ_REQUIRE(mw != NULL);
    XQ_CHECK(XMainWindow_centralWidget(mw) == NULL);

    c1 = XWidget_create(NULL, 0);
    c2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(c1 != NULL && c2 != NULL);

    XMainWindow_setCentralWidget(mw, c1);
    XQ_CHECK(XMainWindow_centralWidget(mw) == c1);
    XQ_CHECK(XWidget_parentWidget(c1) == (XWidget*)mw);

    /* 替换：旧中心控件被删除（Qt deleteLater 等价物为即时销毁），
     * 新控件接管父链。c1 指针此后为悬垂值，只做指针比较不再解引用。 */
    XMainWindow_setCentralWidget(mw, c2);
    XQ_CHECK(XMainWindow_centralWidget(mw) == c2);
    XQ_CHECK(XWidget_parentWidget(c2) == (XWidget*)mw);

    /* takeCentralWidget：摘父链、区域清空、对象仍归调用方
     * （本机 Qt 6.8 实测 taken->parentWidget()==nullptr）。 */
    taken = XMainWindow_takeCentralWidget(mw);
    XQ_CHECK(taken == c2);
    XQ_CHECK(XWidget_parentWidget(c2) == NULL);
    XQ_CHECK(XMainWindow_centralWidget(mw) == NULL);

    /* 再次 take（已空）返回 NULL，不崩。 */
    XQ_CHECK(XMainWindow_takeCentralWidget(mw) == NULL);

    XClassDelete(c2);
    XClassDelete(mw);
}

/* ==================== ③ 工具栏重复登记 = 移动（Qt 实测 1/8/4） ======= */

XQ_TEST(XMainWindow, toolBarAddMovesInsteadOfDuplicating)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XWidget* tb;
    XQ_REQUIRE(mw != NULL);
    tb = XWidget_create(NULL, 0);
    XQ_REQUIRE(tb != NULL);

    /* 本机 Qt 6.8 实测：Left=1 → Bottom=8 → addToolBar(tb)=Top=4。 */
    XMainWindow_addToolBar(mw, (int)XDockWidgetArea_Left, tb);
    XQ_EQ_I64(XMainWindow_toolBarArea(mw, tb), (int)XDockWidgetArea_Left);
    XQ_CHECK(XWidget_parentWidget(tb) == (XWidget*)mw);

    XMainWindow_addToolBar(mw, (int)XDockWidgetArea_Bottom, tb);
    XQ_EQ_I64(XMainWindow_toolBarArea(mw, tb), (int)XDockWidgetArea_Bottom);

    XMainWindow_addToolBar_2(mw, tb);
    XQ_EQ_I64(XMainWindow_toolBarArea(mw, tb), (int)XDockWidgetArea_Top);

    /* 同区重复添加幂等（仍单条、区域不变）。 */
    XMainWindow_addToolBar(mw, (int)XDockWidgetArea_Top, tb);
    XQ_EQ_I64(XMainWindow_toolBarArea(mw, tb), (int)XDockWidgetArea_Top);

    /* removeToolBar：Qt 探测 remove 后 toolBarArea==0（NoToolBarArea）。 */
    XMainWindow_removeToolBar(mw, tb);
    XQ_EQ_I64(XMainWindow_toolBarArea(mw, tb),
              (int)XDockWidgetArea_NoDockWidgetArea);

    /* tb 仍挂在主窗父链上（removeToolBar 只摘登记并隐藏，不删对象——
     * qmainwindowlayout.cpp:1524-1537），随主窗析构级联释放。 */
    XQ_CHECK(XWidget_parentWidget(tb) == (XWidget*)mw);
    XClassDelete(mw);
}

/* ==================== ④ 停靠面板登记：重复添加视为移动 ================= */

#if XDOCKWIDGET_ON
XQ_TEST(XMainWindow, dockWidgetRegistrationMoves)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XDockWidget* dock;
    XQ_REQUIRE(mw != NULL);
    dock = XDockWidget_create("dock", NULL, 0);
    XQ_REQUIRE(dock != NULL);

    XQ_EQ_I64(XMainWindow_dockWidgetArea(mw, (XWidget*)dock),
              (int)XDockWidgetArea_NoDockWidgetArea);

    XMainWindow_addDockWidget(mw, (int)XDockWidgetArea_Left, (XWidget*)dock);
    XQ_EQ_I64(XMainWindow_dockWidgetArea(mw, (XWidget*)dock),
              (int)XDockWidgetArea_Left);
    XQ_CHECK(XWidget_parentWidget((XWidget*)dock) == (XWidget*)mw);

    /* 重复登记同一面板按 Qt 语义视为移动（不产生第二条登记）。 */
    XMainWindow_addDockWidget(mw, (int)XDockWidgetArea_Right, (XWidget*)dock);
    XQ_EQ_I64(XMainWindow_dockWidgetArea(mw, (XWidget*)dock),
              (int)XDockWidgetArea_Right);

    XMainWindow_removeDockWidget(mw, (XWidget*)dock);
    XQ_EQ_I64(XMainWindow_dockWidgetArea(mw, (XWidget*)dock),
              (int)XDockWidgetArea_NoDockWidgetArea);

    XClassDelete(dock);
    XClassDelete(mw);
}

/* ==================== ⑤ saveState/restoreState 往返 ==================== */

XQ_TEST(XMainWindow, saveStateRoundTrip)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XString* state;
    XQ_REQUIRE(mw != NULL);

    state = XMainWindow_saveState(mw, 0);
    XQ_CHECK_PTR(state);
    if (state) {
        /* 快照非空（Qt 返回非空 QByteArray；本库为 "XMWSTATE:" 前缀串）。 */
        XQ_CHECK(XString_size((const XContainer*)state) > 0);
        XQ_CHECK(XMainWindow_restoreState(mw, state, 0));
        XClassDelete((XClass*)state);
    }
    /* state==NULL 视为重置，直接成功（对齐 Qt 空快照的「无字段可重置」口径）。 */
    XQ_CHECK(XMainWindow_restoreState(mw, NULL, 0));

    XClassDelete(mw);
}
#endif /* XDOCKWIDGET_ON */

/* ==================== ⑥ iconSize 的 QSize 语义 ========================= */

XQ_TEST(XMainWindow, iconSizeQSizeSemantics)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XSize s;
    XQ_REQUIRE(mw != NULL);

    /* 缺省 16x16（Qt iconSize 缺省为样式 PM_ToolBarIconSize；本框架
     * 固定 16，方边视图与 QSize 视图同值）。 */
    s = XMainWindow_iconSize_2(mw);
    XQ_EQ_I64(s.width, 16);
    XQ_EQ_I64(s.height, 16);
    XQ_EQ_I64(XMainWindow_iconSize(mw), 16);

    /* QSize 非方边可表达（旧签名只有单 int）。 */
    XSize_init(&s, 24, 18);
    XMainWindow_setIconSize_2(mw, s);
    s = XMainWindow_iconSize_2(mw);
    XQ_EQ_I64(s.width, 24);
    XQ_EQ_I64(s.height, 18);
    /* 兼容方边视图取宽度（Qt 无 int 版，二维用 iconSize_2）。 */
    XQ_EQ_I64(XMainWindow_iconSize(mw), 24);

    /* 同值幂等；非法尺寸忽略（宽/高任一 <= 0）。 */
    XMainWindow_setIconSize_2(mw, s);
    s = XMainWindow_iconSize_2(mw);
    XQ_EQ_I64(s.width, 24);
    XSize_init(&s, 0, 10);
    XMainWindow_setIconSize_2(mw, s);
    s = XMainWindow_iconSize_2(mw);
    XQ_EQ_I64(s.width, 24);
    XQ_EQ_I64(s.height, 18);

    /* 旧方边 setter 仍工作（宽=高）。 */
    XMainWindow_setIconSize(mw, 20);
    s = XMainWindow_iconSize_2(mw);
    XQ_EQ_I64(s.width, 20);
    XQ_EQ_I64(s.height, 20);

    /* NULL 防御。 */
    s = XMainWindow_iconSize_2(NULL);
    XQ_EQ_I64(s.width, 16);
    XQ_EQ_I64(s.height, 16);
    XMainWindow_setIconSize_2(NULL, s);

    XClassDelete(mw);
}

#if XTOOLBAR_ON
XQ_TEST(XMainWindow, iconSizePropagatesToToolBars)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XToolBar* bar;
    XSize s;
    int w = 0;
    int h = 0;
    XQ_REQUIRE(mw != NULL);
    bar = XToolBar_create(NULL, 0);
    XQ_REQUIRE(bar != NULL);
    XMainWindow_addToolBar(mw, (int)XDockWidgetArea_Top, (XWidget*)bar);
    XSize_init(&s, 32, 24);
    XMainWindow_setIconSize_2(mw, s);
    /* 工具栏侧按宽高两维同步（QSize 口径：非方形不再丢高）。 */
    XToolBar_iconSize_2(bar, &w, &h);
    XQ_EQ_I64(w, 32);
    XQ_EQ_I64(h, 24);
    XQ_EQ_I64(XToolBar_iconSize(bar), 32); /* int 视图取宽 */
    /* 再改一次：两维都跟随（高单独变化也算变化）。 */
    XSize_init(&s, 32, 40);
    XMainWindow_setIconSize_2(mw, s);
    XToolBar_iconSize_2(bar, &w, &h);
    XQ_EQ_I64(w, 32);
    XQ_EQ_I64(h, 40);
    XClassDelete(mw); /* 工具栏随主窗级联释放（登记时已挂父链） */
}
#endif /* XTOOLBAR_ON */

/* ==================== ⑦ addToolBarBreak 缺省区域 ====================== */

XQ_TEST(XMainWindow, addToolBarBreakDefaultArea)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XWidget* tb1;
    XWidget* tb2;
    XQ_REQUIRE(mw != NULL);
    tb1 = XWidget_create(NULL, 0);
    tb2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(tb1 != NULL && tb2 != NULL);

    XMainWindow_addToolBar(mw, (int)XDockWidgetArea_Top, tb1);
    /* Qt 缺省实参 area=TopToolBarArea（qmainwindow.h:114）。 */
    XMainWindow_addToolBarBreak_2(mw);
    XMainWindow_addToolBar(mw, (int)XDockWidgetArea_Top, tb2);

    /* 断行落在 tb1 之后、tb2 之前：toolBarBreak(tb2)=true、tb1=false。 */
    XQ_CHECK(XMainWindow_toolBarBreak(mw, tb2));
    XQ_CHECK(!XMainWindow_toolBarBreak(mw, tb1));

    /* 显式 area 版本与缺省版等价（新窗口同构）。 */
    {
        XMainWindow* mw2 = XMainWindow_create(NULL, 0);
        XWidget* a = XWidget_create(NULL, 0);
        XWidget* b = XWidget_create(NULL, 0);
        XQ_REQUIRE(mw2 != NULL && a != NULL && b != NULL);
        XMainWindow_addToolBar(mw2, (int)XDockWidgetArea_Top, a);
        XMainWindow_addToolBarBreak(mw2, (int)XDockWidgetArea_Top);
        XMainWindow_addToolBar(mw2, (int)XDockWidgetArea_Top, b);
        XQ_CHECK(XMainWindow_toolBarBreak(mw2, b));
        XClassDelete((XClass*)mw2);
    }

    XClassDelete((XClass*)mw);
}

#endif /* XGUI_ON && XWIDGET_ON && XMAINWINDOW_ON */
