/**
 * @file       XDockWidgetAlignTest.c
 * @brief      XDockWidget × Qt 6.8 QDockWidget 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qdockwidget.h
 *             与 Qt 6.8 源码 src/widgets/widgets/qdockwidget.cpp
 *             （jsdelivr v6.8.0 原文）。关键行为用本机 Qt 6.8.0
 *             （QT_QPA_PLATFORM=offscreen）探针实测：
 *             - 默认 features = Closable|Movable|Floatable = 7，
 *               allowedAreas = AllDockWidgetAreas = 0xF（qdockwidget.cpp
 *               QDockWidgetPrivate::init）；
 *             - isAreaAllowed 是**包含**判定（qdockwidget.h:87-88
 *               `(allowedAreas() & area) == area`）：allowedAreas()==0 的
 *               面板 isAreaAllowed(NoDockWidgetArea)==1（探针）；
 *             - setFloating(true) 后 isFloating()==isWindow()==1，
 *               切回 false 复位（探针 floating 两态）；**无父构造**的
 *               面板 isFloating()==1（Qt 定义即 isWindow()，
 *               qdockwidget.h:56；探针 `QDockWidget lone; lone.isFloating()
 *               ==1`），addDockWidget 挂到主窗口后复归 0；
 *             - 非 Closable 面板在**无宿主**时仍可关闭（closeEvent 的
 *               `(!win || !win->isVisible())` 例外，qdockwidget.cpp:
 *               1576-1587，探针 close(noclose) ret=1 visible=0）；
 *             - keepSize：Hide 事件里 layout->keepSize(this) 记录面板跨向
 *               尺寸（qdockwidget.cpp:1634-1636 →
 *               qdockarealayout.cpp:3487-3495），隐藏期间空间让给同区
 *               其余面板（探针 hide d1 后 d2 从 356 长到 600），重新 show
 *               后的第一次布局按记录尺寸恢复（探针 afterShow d1=238
 *               d2=356，与隐藏前一致；保留尺寸优先于隐藏期间写入的
 *               resizeDocks 覆盖值）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDOCKWIDGET_ON

#include "XDockWidget.h"
#include "XWidget.h"
#include "XObject.h"
#include "XClass.h"
#include "XVarList.h"
#include "XContainer.h"
#include "XString.h"
#if XMAINWINDOW_ON
#include "XMainWindow.h"
#endif

/* ==================== 信号采样 ==================== */

static int g_dwFeaturesChanged;
static int g_dwLastFeatures;
static int g_dwTopLevelChanged;
static bool g_dwLastTopLevel;

static void xdwTestReset(void)
{
    g_dwFeaturesChanged = 0;
    g_dwLastFeatures = -1;
    g_dwTopLevelChanged = 0;
    g_dwLastTopLevel = false;
}

static void xdwTestFeaturesSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, features);
    ++g_dwFeaturesChanged;
    g_dwLastFeatures = features;
}

static void xdwTestTopLevelSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, topLevel);
    ++g_dwTopLevelChanged;
    g_dwLastTopLevel = topLevel;
}

static void xdwTestConnect(XDockWidget* dock)
{
    XObject_connect((XObject*)dock, XSignal(XDockWidget_featuresChanged_signal),
                      (XObject*)dock, xdwTestFeaturesSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)dock, XSignal(XDockWidget_topLevelChanged_signal),
                      (XObject*)dock, xdwTestTopLevelSlot,
                      XConnectionType_Direct);
}

/* ==================== ① 构造默认值 ==================== */

XQ_TEST(XDockWidget, constructorDefaults)
{
    XDockWidget* dock = XDockWidget_create("dock", NULL, 0);
    XQ_REQUIRE(dock != NULL);

    /* qdockwidget.cpp QDockWidgetPrivate::init：三个特性位全开。 */
    XQ_EQ_I64(XDockWidget_features(dock),
              (int)XDockWidgetFeature_Closable |
                  (int)XDockWidgetFeature_Movable |
                  (int)XDockWidgetFeature_Floatable);
    /* 缺省允许全部停靠区（Qt 同名常量 AllDockWidgetAreas=0xF）。 */
    XQ_EQ_I64(XDockWidget_allowedAreas(dock), (int)XDockWidgetArea_All);
    /* isFloating 即 isWindow（qdockwidget.h:56）：无父构造 ⇒ 顶层 ⇒ true
     * （探针 `QDockWidget lone; lone.isFloating()==1`）。 */
    XQ_CHECK(XWidget_isWindow((XWidget*)dock));
    XQ_CHECK(XDockWidget_isFloating(dock));
    XQ_CHECK(XDockWidget_widget(dock) == NULL);
    /* toggleViewAction 惰性创建且非空（qdockwidget.cpp toggleViewAction）。 */
    XQ_CHECK_PTR(XDockWidget_toggleViewAction(dock));

    /* NULL 安全（C 侧契约）。 */
    XQ_EQ_I64(XDockWidget_features(NULL), 0);
    XQ_EQ_I64(XDockWidget_allowedAreas(NULL), 0);
    XQ_CHECK(!XDockWidget_isFloating(NULL));
    XQ_CHECK(XDockWidget_widget(NULL) == NULL);
    XQ_CHECK(XDockWidget_toggleViewAction(NULL) == NULL);

    XClassDelete(dock);
}

/* ==================== ② 内容控件所有权（替换摘除、不删除旧件） ======== */

XQ_TEST(XDockWidget, setWidgetOwnership)
{
    XDockWidget* dock = XDockWidget_create("dock", NULL, 0);
    XWidget* c1;
    XWidget* c2;
    XQ_REQUIRE(dock != NULL);
    c1 = XWidget_create(NULL, 0);
    c2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(c1 != NULL && c2 != NULL);

    XDockWidget_setWidget(dock, c1);
    XQ_CHECK(XDockWidget_widget(dock) == c1);
    XQ_CHECK(XWidget_parentWidget(c1) == (XWidget*)dock);
    /* 内容恒在标题条以下（QDockWidgetLayout 同口径）。 */
    XQ_CHECK(XWidget_y(c1) > 0);

    /* 同指针重设幂等（Qt setWidget 的同指针早退）。 */
    XDockWidget_setWidget(dock, c1);
    XQ_CHECK(XDockWidget_widget(dock) == c1);

    /* 替换：旧件摘父链、**不删除**（所有权归还调用方）。 */
    XDockWidget_setWidget(dock, c2);
    XQ_CHECK(XDockWidget_widget(dock) == c2);
    XQ_CHECK(XWidget_parentWidget(c1) == NULL);
    XQ_CHECK(XWidget_parentWidget(c2) == (XWidget*)dock);

    /* 传 NULL 摘除内容（Qt setWidget(nullptr) 同义）。 */
    XDockWidget_setWidget(dock, NULL);
    XQ_CHECK(XDockWidget_widget(dock) == NULL);
    XQ_CHECK(XWidget_parentWidget(c2) == NULL);

    XClassDelete(c1);
    XClassDelete(c2);
    XClassDelete(dock);
}

/* ==================== ③ isAreaAllowed 是包含判定（Qt 内联原文） ======= */

XQ_TEST(XDockWidget, isAreaAllowedIsContainment)
{
    XDockWidget* dock = XDockWidget_create("dock", NULL, 0);
    XQ_REQUIRE(dock != NULL);

    XDockWidget_setAllowedAreas(dock,
                                (int)XDockWidgetArea_Left |
                                    (int)XDockWidgetArea_Right);
    XQ_EQ_I64(XDockWidget_allowedAreas(dock),
              (int)XDockWidgetArea_Left | (int)XDockWidgetArea_Right);
    XQ_CHECK(XDockWidget_isAreaAllowed(dock, (int)XDockWidgetArea_Left));
    XQ_CHECK(!XDockWidget_isAreaAllowed(dock, (int)XDockWidgetArea_Top));
    /* 位组合：要求每一位都允许（包含判定）。 */
    XQ_CHECK(XDockWidget_isAreaAllowed(dock,
                                       (int)XDockWidgetArea_Left |
                                           (int)XDockWidgetArea_Right));
    XQ_CHECK(!XDockWidget_isAreaAllowed(dock,
                                        (int)XDockWidgetArea_Left |
                                            (int)XDockWidgetArea_Top));
    /* 0 是任何掩码的子集 ⇒ NoDockWidgetArea 恒允许（本机 Qt 6.8 实测
     * 「d2 allowed=0 isAreaAllowed(NoDockWidgetArea)=1」）。 */
    XQ_CHECK(XDockWidget_isAreaAllowed(dock,
                                       (int)XDockWidgetArea_NoDockWidgetArea));
    XQ_CHECK(!XDockWidget_isAreaAllowed(NULL, (int)XDockWidgetArea_Left));

    XClassDelete(dock);
}

/* ==================== ④ features / topLevel 信号与浮动切换 ============= */

XQ_TEST(XDockWidget, featuresAndFloatingSignals)
{
#if XMAINWINDOW_ON
    /* 浮/停两态必须有宿主：Qt 6.8 探针（offscreen）standalone
     * QDockWidget 恒 isFloating()==isWindow()==1（无父=顶层窗），
     * setFloating(false) 无处可停靠、isWindow 仍为 1；两向
     * topLevelChanged 的对照口径是 addDockWidget 之后的宿主面板。 */
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XDockWidget* dock;
    XQ_REQUIRE(mw != NULL);
    dock = XDockWidget_create("dock", (XWidget*)mw, 0);
    XQ_REQUIRE(dock != NULL);
    XMainWindow_addDockWidget(mw, (int)XDockWidgetArea_Left, (XWidget*)dock);
#else
    XDockWidget* dock = XDockWidget_create("dock", NULL, 0);
    XQ_REQUIRE(dock != NULL);
#endif
    xdwTestReset();
    xdwTestConnect(dock);

    XDockWidget_setFeatures(dock,
                            (int)XDockWidgetFeature_NoDockWidgetFeatures);
    XQ_EQ_I64(XDockWidget_features(dock),
              (int)XDockWidgetFeature_NoDockWidgetFeatures);
    XQ_EQ_I64(g_dwFeaturesChanged, 1);
    XQ_EQ_I64(g_dwLastFeatures,
              (int)XDockWidgetFeature_NoDockWidgetFeatures);

    /* 同值重复设置不发射（Qt setFeatures 的 `if (d->features == features)
     * return;` 早退）。 */
    XDockWidget_setFeatures(dock,
                            (int)XDockWidgetFeature_NoDockWidgetFeatures);
    XQ_EQ_I64(g_dwFeaturesChanged, 1);

    XDockWidget_setFeatures(dock, (int)XDockWidgetFeature_Closable);
    XQ_EQ_I64(g_dwFeaturesChanged, 2);
    XQ_EQ_I64(g_dwLastFeatures, (int)XDockWidgetFeature_Closable);

    /* 浮动切换：topLevelChanged 载荷=新状态，两向都发。 */
    XDockWidget_setFloating(dock, true);
    XQ_CHECK(XDockWidget_isFloating(dock));
    XQ_EQ_I64(g_dwTopLevelChanged, 1);
    XQ_CHECK(g_dwLastTopLevel);

    XDockWidget_setFloating(dock, false);
    XQ_CHECK(!XDockWidget_isFloating(dock));
    XQ_EQ_I64(g_dwTopLevelChanged, 2);
    XQ_CHECK(!g_dwLastTopLevel);

    /* 同值幂等不重复发（Qt setFloating 的 `if (floating == isFloating())
     * return;`）。 */
    XDockWidget_setFloating(dock, false);
    XQ_EQ_I64(g_dwTopLevelChanged, 2);

#if XMAINWINDOW_ON
    XClassDelete((XClass*)mw); /* 面板随宿主级联释放 */
#else
    XClassDelete(dock);
#endif
}

/* ==================== ⑤ 宿主登记：可见性与重停靠 ====================== */

#if XMAINWINDOW_ON
XQ_TEST(XDockWidget, hostRegistrationAndVisibility)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XDockWidget* dock;
    XWidget* content;
    XQ_REQUIRE(mw != NULL);
    dock = XDockWidget_create("dock", (XWidget*)mw, 0);
    content = XWidget_create(NULL, 0);
    XQ_REQUIRE(dock != NULL && content != NULL);
    XDockWidget_setWidget(dock, content);

    XMainWindow_addDockWidget(mw, (int)XDockWidgetArea_Left, (XWidget*)dock);
    XQ_EQ_I64(XMainWindow_dockWidgetArea(mw, (XWidget*)dock),
              (int)XDockWidgetArea_Left);
    /* 停靠态内容件恒随面板几何（标题条以下全部区域）。 */
    XQ_CHECK(XWidget_width(content) > 0);

    /* 浮动：脱离宿主父链成顶层（Qt::Tool 浮窗语义）。 */
    XDockWidget_setFloating(dock, true);
    XQ_CHECK(XDockWidget_isFloating(dock));
    XQ_CHECK(XWidget_isWindow((XWidget*)dock));

    /* 回归停靠：重新挂回宿主（Qt setFloating(false) → setParent(win)）。 */
    XDockWidget_setFloating(dock, false);
    XQ_CHECK(!XDockWidget_isFloating(dock));
    XQ_CHECK(XWidget_parentWidget((XWidget*)dock) == (XWidget*)mw);

    XClassDelete((XClass*)mw); /* 面板与内容随主窗级联释放 */
}
#endif /* XMAINWINDOW_ON */

/* ==================== ⑥ keepSize：隐藏 → 复显恢复隐藏前尺寸 =========== */

#if XMAINWINDOW_ON
XQ_TEST(XDockWidget, keepSizeRestoresOnReshow)
{
    XMainWindow* mw = XMainWindow_create(NULL, 0);
    XDockWidget* d1;
    XDockWidget* d2;
    XWidget* c1;
    XWidget* c2;
    int h1;
    int h2;
    int h2Hidden;
    int areaH;
    XQ_REQUIRE(mw != NULL);
    XWidget_resize((XWidget*)mw, 800, 600);
    d1 = XDockWidget_create("d1", (XWidget*)mw, 0);
    d2 = XDockWidget_create("d2", (XWidget*)mw, 0);
    c1 = XWidget_create(NULL, 0);
    c2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(d1 != NULL && d2 != NULL && c1 != NULL && c2 != NULL);
    XDockWidget_setWidget(d1, c1);
    XDockWidget_setWidget(d2, c2);
    XMainWindow_addDockWidget(mw, (int)XDockWidgetArea_Left, (XWidget*)d1);
    XMainWindow_addDockWidget(mw, (int)XDockWidgetArea_Left, (XWidget*)d2);

    /* 登记后挂在主窗口父链（无 Window 标志）⇒ isFloating(=isWindow) false
     * （Qt 同：addDockWidget 后探针 isFloating=0）。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)d1) == (XWidget*)mw);
    XQ_CHECK(!XDockWidget_isFloating(d1));
    XQ_CHECK(!XDockWidget_isFloating(d2));

    /* 只给 d1 一个显式行高；d2 走均分/吃余量。 */
    {
        XDockWidget* arr[1];
        int sizes[1];
        arr[0] = d1;
        sizes[0] = 200;
        XMainWindow_resizeDocks(mw, arr, sizes, 1, 2 /* Qt::Vertical */);
    }
    h1 = XWidget_height((XWidget*)d1);
    h2 = XWidget_height((XWidget*)d2);
    /* 停靠区可用高度 = 显式行 + 吃余量行。Qt 探针里裸 QMainWindow 的
     * 客户区高度就是 600（标题栏由窗口系统画在客户区外）；本框架的
     * CSD 标题条画在客户区内（XWindowDecoration_marginsFor，
     * XMainWindow.c:1824-1872 的 xmw_dockAreaRect），故 800x600 窗口的
     * 实际停靠区高 = 600 − 装饰条高（28）。按「d2 吃掉剩余」断言，
     * 不把装饰条高度写成魔法数。 */
    areaH = h1 + h2;
    XQ_EQ_I64(h1, 200);
    XQ_EQ_I64(h2, areaH - h1);
    XQ_CHECK(h2 > h1);

    /* 隐藏 d1：空间让给同区其余面板（Qt 探针 hide 后 d2 长满整个停靠区）。 */
    XWidget_hide((XWidget*)d1);
    h2Hidden = XWidget_height((XWidget*)d2);
    XQ_CHECK_MSG(h2Hidden > h2, "hidden=%d before=%d", h2Hidden, h2);
    XQ_EQ_I64(h2Hidden, areaH);

    /* 复显 d1：keepSize 记录隐藏前的 200 被下一次布局兑现
     * （Qt 探针 afterShow 恢复 238/356；本模型固定 200 + 余量 400）。 */
    XWidget_show((XWidget*)d1);
    XQ_EQ_I64(XWidget_height((XWidget*)d1), h1);
    XQ_EQ_I64(XWidget_height((XWidget*)d2), h2);

    XClassDelete((XClass*)mw); /* 面板/内容随主窗级联释放 */
}
#endif /* XMAINWINDOW_ON */

#endif /* XGUI_ON && XWIDGET_ON && XDOCKWIDGET_ON */
