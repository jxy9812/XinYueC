/**
 * @file       XMdiAreaAlignTest.c
 * @brief      XMdiArea / XMdiSubWindow × Qt 6.8 QMdiArea / QMdiSubWindow
 *             对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qmdiarea.h、
 *             qmdisubwindow.h 与 Qt 6.8 源码 src/widgets/widgets/qmdiarea.cpp
 *             （jsdelivr v6.8.0 原文）。关键行为用本机 Qt 6.8.0
 *             （QT_QPA_PLATFORM=offscreen）探针实测 + 源码对照：
 *             - addSubWindow(普通控件) 新建 QMdiSubWindow 并置
 *               WA_DeleteOnClose（qmdiarea.cpp:1962），返回该子窗；
 *             - addSubWindow(已是 QMdiSubWindow) **直接接管**、不新建外壳、
 *               不动内容件；重复加入只告警并原样返回
 *               （qmdiarea.cpp:1949-1957，探针 count=1 ret==sw）；
 *             - removeSubWindow(**子窗口**) 只 setParent(nullptr) + 移出
 *               列表，**不删除对象**（qmdiarea.cpp:1984-1999，探针
 *               alive=1 parent=(nil) count=0）；
 *             - closeAllSubWindows 逐窗 close()：忽略关闭事件的保持打开
 *               （Qt 文档），内部创建者（WA_DeleteOnClose）随后被删除；
 *               活动窗关闭后发 subWindowActivated(nullptr)
 *               （探针 closeAll 后 sig=1、count=0、alive=0）；
 *             - removeSubWindow(**内容控件**) 只 `child->setWidget(nullptr)`
 *               ——子窗口**保留**在 subWindowList（qmdiarea.cpp:2004-2016
 *               与官方文档；探针 count 仍 3、child->widget()==nullptr、
 *               content->parentWidget()==nullptr、子窗仍可见）；
 *             - subWindowList(WindowOrder)（qmdiarea.cpp:1344-1391；探针）：
 *               CreationOrder=[a,b,c]；StackingOrder 随 raise 变化
 *               （activate(a) 后 [b,c,a]、raise(a) 后 [c,b,a]）；
 *               ActivationHistoryOrder 由旧到新（激活 a 后 [b,c,a]、
 *               再激活 b 后 [c,a,b]）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XMDIAREA_ON

#include "XMdiArea.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XObject.h"
#include "XClass.h"
#include "XVarList.h"
#include "XContainer.h"
#include "XVector.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XStringUtils.h" /* XStrcmp（页签条类名指纹）。 */
#include "XBrush.h"       /* P3-54 背景画刷断言。 */
#if XTABBAR_ON
#include "XTabBar.h"
#endif
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif
#if XSCREEN_ON
#include "XScreen.h"
#endif

/* ==================== 信号采样 ==================== */

static int g_maActivated;
static XMdiSubWindow* g_maLastActivated;

static void xmaTestReset(void)
{
    g_maActivated = 0;
    g_maLastActivated = NULL;
}

static void xmaTestActivatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XMdiSubWindow*, window);
    ++g_maActivated;
    g_maLastActivated = (XMdiSubWindow*)window;
}

static void xmaTestConnect(XMdiArea* area)
{
    XObject_connect((XObject*)area, XSignal(XMdiArea_subWindowActivated_signal),
                      (XObject*)area, xmaTestActivatedSlot,
                      XConnectionType_Direct);
}

/* ==================== ① 子窗加入与激活 ==================== */

XQ_TEST(XMdiArea, addSubWindowActivates)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XWidget* c1;
    XWidget* c2;
    XMdiSubWindow* sw1;
    XMdiSubWindow* sw2;
    XQ_REQUIRE(area != NULL);
    c1 = XWidget_create(NULL, 0);
    c2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(c1 != NULL && c2 != NULL);

    xmaTestReset();
    xmaTestConnect(area);

    sw1 = XMdiArea_addSubWindow(area, c1);
    XQ_CHECK_PTR(sw1);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 1);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == sw1);
    XQ_CHECK(XMdiArea_currentSubWindow(area) == sw1);
    XQ_CHECK(XMdiSubWindow_widget(sw1) == c1);
    /* 归属反查（Qt QMdiSubWindow::mdiArea）。 */
    XQ_CHECK(XMdiSubWindow_mdiArea(sw1) == area);
    XQ_EQ_I64(g_maActivated, 1);
    XQ_CHECK(g_maLastActivated == sw1);

    sw2 = XMdiArea_addSubWindow(area, c2);
    XQ_CHECK_PTR(sw2);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 2);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == sw2);
    XQ_CHECK(sw1 != sw2);
    /* subWindowList 借用指针：长度随登记变化。 */
    {
        const XVector* list = XMdiArea_subWindowList(area);
        XQ_CHECK_PTR(list);
        if (list) XQ_EQ_I64((long long)XVector_size_base(
                                (const XContainer*)list), 2);
    }

    /* 显式激活切换：aboutToActivate/activated 由 setActiveSubWindow 驱动。 */
    XMdiArea_setActiveSubWindow(area, sw1);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == sw1);
    XQ_EQ_I64(g_maActivated, 3);
    XQ_CHECK(g_maLastActivated == sw1);

    /* setActiveSubWindow(NULL) → 失活并发 subWindowActivated(NULL)。 */
    XMdiArea_setActiveSubWindow(area, NULL);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == NULL);
    XQ_EQ_I64(g_maActivated, 4);
    XQ_CHECK(g_maLastActivated == NULL);

    XClassDelete((XClass*)area); /* 子窗与内容随之级联释放 */
}

/* ==================== ② 接管既有子窗口（Qt qobject_cast 分支） ========= */

XQ_TEST(XMdiArea, addExistingSubWindowIsAdopted)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* sw;
    XWidget* content;
    XMdiSubWindow* ret;
    XQ_REQUIRE(area != NULL);
    sw = XMdiSubWindow_create(NULL, 0);
    content = XWidget_create(NULL, 0);
    XQ_REQUIRE(sw != NULL && content != NULL);
    XMdiSubWindow_setWidget(sw, content);
    XMdiSubWindow_setWindowTitle_2(sw, "adopted");

    /* Qt：传入已是子窗口 ⇒ 直接接管（返回同一对象、不新建外壳）。 */
    ret = XMdiArea_addSubWindow(area, (XWidget*)sw);
    XQ_CHECK(ret == sw);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 1);
    /* 内容件不受接管流程影响（Qt 不调 setWidget）。 */
    XQ_CHECK(XMdiSubWindow_widget(sw) == content);
    XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw), "adopted");
    XQ_CHECK(XMdiSubWindow_mdiArea(sw) == area);

    /* 重复加入：原样返回、不产生第二条（qmdiarea.cpp:1951-1954）。 */
    XQ_CHECK(XMdiArea_addSubWindow(area, (XWidget*)sw) == sw);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 1);

    /* 接管来的子窗**不带** DeleteOnClose：closeAllSubWindows 只隐藏它、
     * 仍留在 subWindowList 里（本机 Qt 6.8 实测：用户自建
     * QMdiSubWindow 加入后 close 返回 1、对象存活、count 不变）。 */
    XMdiArea_closeAllSubWindows(area);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 1);
    XQ_CHECK(XWidget_isHidden((XWidget*)sw));
    XQ_CHECK(XMdiSubWindow_widget(sw) == content);
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);

    XClassDelete((XClass*)area);
}

/* ==================== ③ removeSubWindow(子窗口)：摘链不删除 ============ */

XQ_TEST(XMdiArea, removeSubWindowDetachesWithoutDeleting)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* sw;
    XWidget* content;
    XQ_REQUIRE(area != NULL);
    content = XWidget_create(NULL, 0);
    XQ_REQUIRE(content != NULL);
    sw = XMdiArea_addSubWindow(area, content);
    XQ_REQUIRE(sw != NULL);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 1);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == sw);

    /* Qt：removeSubWindow(QMdiSubWindow*) → setParent(nullptr) + 移出列表，
     * **不删除对象**；随后判空/解引用均安全（探针 alive=1 parent=(nil)）。 */
    XMdiArea_removeSubWindow(area, (XWidget*)sw);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 0);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)sw) == NULL);
    XQ_CHECK(XMdiSubWindow_mdiArea(sw) == NULL);
    XQ_CHECK(XMdiSubWindow_widget(sw) == content); /* 内容件随之保留 */

    /* 对象仍归调用方：必须自行释放（Qt 文档「never actually deleted」）。 */
    XClassDelete((XClass*)sw);

    /* 未登记的子窗口：Qt 告警后返回，不动作。 */
    sw = XMdiSubWindow_create(NULL, 0);
    XQ_REQUIRE(sw != NULL);
    XMdiArea_removeSubWindow(area, (XWidget*)sw);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 0);
    XClassDelete((XClass*)sw);

    XClassDelete((XClass*)area);
}

/* ==================== ④ closeAllSubWindows：走关闭事件 + 活动窗收口 ===== */

XQ_TEST(XMdiArea, closeAllSubWindowsUsesCloseEvent)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* sw1;
    XMdiSubWindow* sw2;
    XQ_REQUIRE(area != NULL);
    sw1 = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    sw2 = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    XQ_REQUIRE(sw1 != NULL && sw2 != NULL);

    xmaTestReset();
    xmaTestConnect(area);

    XMdiArea_closeAllSubWindows(area);
    /* 内部创建的子窗带 DeleteOnClose：列表清空（Qt WA_DeleteOnClose 同）；
     * 关闭事件被接受后对象走延迟删除——先冲刷，避免延迟事件悬垂。 */
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 0);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == NULL);
    /* 活动窗被关闭 → resetActiveWindow 发 subWindowActivated(nullptr)。 */
    XQ_EQ_I64(g_maActivated, 1);
    XQ_CHECK(g_maLastActivated == NULL);
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);

    /* 关闭被拒绝（closeEvent ignore）的子窗保持登记：借一个自定义子窗
     * 无法在本用例内派生，故以「空区域重复 closeAll 幂等」验证无副作用
     * 的分支。 */
    XMdiArea_closeAllSubWindows(area);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 0);
    XQ_EQ_I64(g_maActivated, 1);

    XClassDelete((XClass*)area);
}

/* ==================== ⑤ 视图模式 / 区域选项默认值 ====================== */

XQ_TEST(XMdiArea, viewModeAndOptions)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XQ_REQUIRE(area != NULL);

    XQ_EQ_I64(XMdiArea_viewMode(area), XMdiAreaViewMode_SubWindowView);
    XQ_EQ_I64(XMdiArea_activationOrder(area),
              XMdiAreaWindowOrder_CreationOrder);
    XQ_CHECK(!XMdiArea_documentMode(area));
    XQ_CHECK(!XMdiArea_tabsMovable(area));
    XQ_CHECK(!XMdiArea_tabsClosable(area));
    XQ_CHECK(!XMdiArea_testOption(
        area, XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation));

    XMdiArea_setViewMode(area, XMdiAreaViewMode_TabbedView);
    XQ_EQ_I64(XMdiArea_viewMode(area), XMdiAreaViewMode_TabbedView);
    XMdiArea_setDocumentMode(area, true);
    XQ_CHECK(XMdiArea_documentMode(area));
    XMdiArea_setTabsClosable(area, true);
    XQ_CHECK(XMdiArea_tabsClosable(area));
    XMdiArea_setOption(area,
                       XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation,
                       true);
    XQ_CHECK(XMdiArea_testOption(
        area, XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation));
    XMdiArea_setOption(area,
                       XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation,
                       false);
    XQ_CHECK(!XMdiArea_testOption(
        area, XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation));

    XClassDelete((XClass*)area);
}

/* ==================== ⑥ removeSubWindow(内容控件)：子窗保留 ============ */

XQ_TEST(XMdiArea, removeSubWindowOfContentKeepsSubWindow)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XWidget* content;
    XMdiSubWindow* sw;
    XQ_REQUIRE(area != NULL);
    content = XWidget_create(NULL, 0);
    XQ_REQUIRE(content != NULL);
    sw = XMdiArea_addSubWindow(area, content);
    XQ_REQUIRE(sw != NULL);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 1);

    /* Qt 6.8（qmdiarea.cpp:2004-2016 + 官方文档）：传入内容控件只
     * `child->setWidget(nullptr)` —— 子窗**保留**在 subWindowList、
     * 内容件摘链交还调用方、活动窗不变。本机探针 count 仍 3、
     * child->widget()==nullptr、content->parentWidget()==nullptr、
     * 子窗仍可见。 */
    XMdiArea_removeSubWindow(area, content);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 1);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == sw);
    XQ_CHECK(XMdiSubWindow_widget(sw) == NULL);
    XQ_CHECK(XWidget_parentWidget(content) == NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)sw) != NULL);
    XQ_CHECK(!XWidget_isHidden((XWidget*)sw));
    {
        const XVector* list = XMdiArea_subWindowList(area);
        XQ_CHECK(list && XVector_size_base((const XContainer*)list) == 1);
    }

    /* 未知内容控件：不动作（Qt 告警后返回）。 */
    {
        XWidget* stray = XWidget_create(NULL, 0);
        XQ_REQUIRE(stray != NULL);
        XMdiArea_removeSubWindow(area, stray);
        XQ_EQ_I64(XMdiArea_subWindowCount(area), 1);
        XClassDelete((XClass*)stray);
    }

    XClassDelete((XClass*)content); /* 内容件归调用方 */
    XClassDelete((XClass*)area);    /* 子窗随区域级联释放 */
}

/* ==================== ⑦ subWindowList(WindowOrder) ==================== */

/** @brief 子窗口在列表中的下标；不在列表返回 -1。 */
static int xmaListIndex(const XVector* list, const XMdiSubWindow* sw)
{
    int64_t i;
    int64_t n;
    if (!list || !sw) return -1;
    n = XVector_size_base((const XContainer*)list);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot = (XMdiSubWindow**)XVector_at_base(list, i);
        if (slot && *slot == sw) return (int)i;
    }
    return -1;
}

XQ_TEST(XMdiArea, subWindowListOrders)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* a;
    XMdiSubWindow* b;
    XMdiSubWindow* c;
    XVector* list;
    XQ_REQUIRE(area != NULL);
    a = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    b = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    c = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    XQ_REQUIRE(a != NULL && b != NULL && c != NULL);

    /* CreationOrder（缺省）：登记顺序 a,b,c（探针同）。 */
    list = XMdiArea_subWindowList_2(area, XMdiAreaWindowOrder_CreationOrder);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_I64((long long)XVector_size_base((const XContainer*)list), 3);
        XQ_EQ_I64(xmaListIndex(list, a), 0);
        XQ_EQ_I64(xmaListIndex(list, b), 1);
        XQ_EQ_I64(xmaListIndex(list, c), 2);
        XClassDelete((XClass*)list);
    }
    /* 无参 subWindowList 是 CreationOrder 的借用版（同序）。 */
    {
        const XVector* borrow = XMdiArea_subWindowList(area);
        XQ_EQ_I64(xmaListIndex(borrow, a), 0);
        XQ_EQ_I64(xmaListIndex(borrow, c), 2);
    }

    /* StackingOrder：叠加次序（探针 add a,b,c 后 [a,b,c]）。 */
    list = XMdiArea_subWindowList_2(area, XMdiAreaWindowOrder_StackingOrder);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_I64((long long)XVector_size_base((const XContainer*)list), 3);
        XQ_EQ_I64(xmaListIndex(list, a), 0);
        XQ_EQ_I64(xmaListIndex(list, b), 1);
        XQ_EQ_I64(xmaListIndex(list, c), 2);
        XClassDelete((XClass*)list);
    }

    /* ActivationHistoryOrder：由旧到新（探针 add a,b,c → [a,b,c]）。 */
    list = XMdiArea_subWindowList_2(area,
                                    XMdiAreaWindowOrder_ActivationHistoryOrder);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_I64(xmaListIndex(list, a), 0);
        XQ_EQ_I64(xmaListIndex(list, b), 1);
        XQ_EQ_I64(xmaListIndex(list, c), 2);
        XClassDelete((XClass*)list);
    }

    /* 激活 a：置顶后 StackingOrder = [b,c,a]；激活史 = [b,c,a]
     * （探针 activate-a stacking/activation 均为 b c a）。 */
    XMdiArea_setActiveSubWindow(area, a);
    list = XMdiArea_subWindowList_2(area, XMdiAreaWindowOrder_StackingOrder);
    if (list) {
        XQ_EQ_I64(xmaListIndex(list, b), 0);
        XQ_EQ_I64(xmaListIndex(list, c), 1);
        XQ_EQ_I64(xmaListIndex(list, a), 2);
        XClassDelete((XClass*)list);
    } else {
        XQ_CHECK_PTR(list);
    }
    list = XMdiArea_subWindowList_2(area,
                                    XMdiAreaWindowOrder_ActivationHistoryOrder);
    if (list) {
        XQ_EQ_I64(xmaListIndex(list, b), 0);
        XQ_EQ_I64(xmaListIndex(list, c), 1);
        XQ_EQ_I64(xmaListIndex(list, a), 2);
        XClassDelete((XClass*)list);
    } else {
        XQ_CHECK_PTR(list);
    }

    /* 再激活 b：激活史 = [c,a,b]（探针 activate-b activation 同）。 */
    XMdiArea_setActiveSubWindow(area, b);
    list = XMdiArea_subWindowList_2(area,
                                    XMdiAreaWindowOrder_ActivationHistoryOrder);
    if (list) {
        XQ_EQ_I64(xmaListIndex(list, c), 0);
        XQ_EQ_I64(xmaListIndex(list, a), 1);
        XQ_EQ_I64(xmaListIndex(list, b), 2);
        XClassDelete((XClass*)list);
    } else {
        XQ_CHECK_PTR(list);
    }

    /* raise(a)：StackingOrder = [c,b,a]（探针 raise-a 同）。 */
    XWidget_raise((XWidget*)a);
    list = XMdiArea_subWindowList_2(area, XMdiAreaWindowOrder_StackingOrder);
    if (list) {
        XQ_EQ_I64(xmaListIndex(list, c), 0);
        XQ_EQ_I64(xmaListIndex(list, b), 1);
        XQ_EQ_I64(xmaListIndex(list, a), 2);
        XClassDelete((XClass*)list);
    } else {
        XQ_CHECK_PTR(list);
    }

    XClassDelete((XClass*)area);
}

/* ==================== ⑧ activateNext/Previous 按激活顺序选序 =========== */

XQ_TEST(XMdiArea, activationOrderDrivesNextPrevious)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* a;
    XMdiSubWindow* b;
    XMdiSubWindow* c;
    XQ_REQUIRE(area != NULL);
    a = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    b = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    c = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    XQ_REQUIRE(a != NULL && b != NULL && c != NULL);

    /* 无活动窗：Qt 取列表末项为当前再步进
     * （探针 no-active next→a、no-active prev→b）。 */
    XMdiArea_setActiveSubWindow(area, NULL);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == NULL);
    XMdiArea_activateNextSubWindow(area);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == a);
    XMdiArea_setActiveSubWindow(area, NULL);
    XMdiArea_activatePreviousSubWindow(area);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == b);

    /* 有活动窗（creation 顺序）：b 的前一个是 a、后一个是 c。 */
    XMdiArea_setActiveSubWindow(area, b);
    XMdiArea_activatePreviousSubWindow(area);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == a);
    XMdiArea_activateNextSubWindow(area);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == b);

    XClassDelete((XClass*)area);
}

/* ==================== ⑨ AreaOption 位值与 Qt 一致（P0-51） ============== */
/* Qt 6.8 qmdiarea.h:36-38：AreaOption 只有
 * DontMaximizeSubWindowOnActivation = 0x1；X 原先本位 0x2、另有一个
 * 独占 0x1 的 AllowTabbedView（Qt 无此选项——页签是 ViewMode）。 */

XQ_TEST(XMdiArea, areaOptionBitsMatchQt)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XQ_REQUIRE(area != NULL);

    XQ_EQ_I64(XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation, 0x1);
    /* 缺省无该位（Qt 构造 options=nullptr 位集为空）。 */
    XQ_CHECK(!XMdiArea_testOption(
        area, XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation));
    /* 按 Qt 位值 0x1 设置必须生效（旧实现会落到 AllowTabbedView 位）。 */
    XMdiArea_setOption(area,
                       XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation,
                       true);
    XQ_CHECK(XMdiArea_testOption(
        area, XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation));
    XMdiArea_setOption(area,
                       XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation,
                       false);
    XQ_CHECK(!XMdiArea_testOption(
        area, XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation));
    XQ_CHECK(!XMdiArea_testOption(NULL,
                                  XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation));
    XMdiArea_setOption(NULL, 0x1, true);

    XClassDelete((XClass*)area);
}

/* ==================== ⑩ addSubWindow_2 的 flags 形参（P0-52） =========== */
/* Qt qmdiarea.cpp:1944-1975：普通控件路径 `new QMdiSubWindow(viewport(),
 * windowFlags)`；已子窗路径 `setParent(viewport(), windowFlags ?
 * windowFlags : child->windowFlags())` 后做类型位规范化。 */

XQ_TEST(XMdiArea, addSubWindowFlagsOverload)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XWidget* c1;
    XWidget* c2;
    XMdiSubWindow* plain;
    XMdiSubWindow* framed;
    XQ_REQUIRE(area != NULL);
    c1 = XWidget_create(NULL, 0);
    c2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(c1 != NULL && c2 != NULL);

    /* flags=0：与既有入口等价（无 Frameless 位、类型位为 SubWindow）。 */
    plain = XMdiArea_addSubWindow_2(area, c1, 0);
    XQ_REQUIRE(plain != NULL);
    XQ_CHECK((XWidget_windowFlags((XWidget*)plain) &
              (XWidgetFlags)XWindowType_FramelessWindowHint) == 0);
    XQ_EQ_I64((long long)(XWidget_windowFlags((XWidget*)plain) &
                          XWindowType_TypeMask),
              (long long)XWindowType_SubWindow);
    XQ_CHECK(!XWidget_isWindow((XWidget*)plain));

    /* 显式 FramelessWindowHint：透传给新建外壳（Qt 同款）。 */
    framed = XMdiArea_addSubWindow_2(
        area, c2, (XWidgetFlags)XWindowType_FramelessWindowHint);
    XQ_REQUIRE(framed != NULL);
    XQ_CHECK((XWidget_windowFlags((XWidget*)framed) &
              (XWidgetFlags)XWindowType_FramelessWindowHint) != 0);
    XQ_EQ_I64((long long)(XWidget_windowFlags((XWidget*)framed) &
                          XWindowType_TypeMask),
              (long long)XWindowType_SubWindow);
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 2);

    XClassDelete((XClass*)area);
}

/* ==================== ⑪ sizeHint / minimumSizeHint（P3-51/52） ========== */
/* Qt qmdiarea.cpp:1710-1734 sizeHint：主屏 virtualSize×2/(3×(嵌套+1))，
 * 再逐子窗 expandedTo(child->sizeHint())；
 * qmdiarea.cpp:1736-1750 minimumSizeHint：PM 度量 + 基类并集，滚动条
 * 关闭时再并子窗 minimumSizeHint。 */

XQ_TEST(XMdiArea, sizeHintFollowsScreenAndChildren)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XWidget* content;
    XMdiSubWindow* sw;
    XSize hint;
    XSize childHint;
    XSize contentHint;
    int64_t expectW;
    int64_t expectH;
    XQ_REQUIRE(area != NULL);

    /* 空区域：无子窗，提示取主屏公式基值（本测试宿主无主屏 ⇒ (0,0)）。 */
    hint = XWidget_sizeHint_base((const XWidget*)area);
    XQ_CHECK(hint.width >= 0 && hint.height >= 0);

    /* 子窗内容给一个确定的首选尺寸，区域提示必须不窄于子窗提示。 */
    content = XWidget_create(NULL, 0);
    XQ_REQUIRE(content != NULL);
    XSize_init(&contentHint, 300, 200);
    XWidget_setSizeHint(content, &contentHint);
    sw = XMdiArea_addSubWindow(area, content);
    XQ_REQUIRE(sw != NULL);
    childHint = XWidget_sizeHint_base((const XWidget*)sw);
    XQ_CHECK(childHint.width >= 300);
    XQ_CHECK(childHint.height >= 200);
    hint = XWidget_sizeHint_base((const XWidget*)area);
    XQ_CHECK_MSG(hint.width >= childHint.width,
                 "area=%d child=%d", hint.width, childHint.width);
    XQ_CHECK_MSG(hint.height >= childHint.height,
                 "area=%d child=%d", hint.height, childHint.height);

    /* 主屏存在时按 Qt 公式给出基值（本机宿主注册了主屏才断言公式；
     * 无主屏宿主断言 (0,0) 基线）。 */
    {
        XScreen* screen = NULL;
#if XGUIAPPLICATION_ON
        screen = XGuiApplication_primaryScreen();
#endif
        if (screen) {
#if XSCREEN_ON
            XSize virtualSize = XScreen_virtualSize(screen);
            expectW = (int64_t)virtualSize.width * 2 / 3;
            expectH = (int64_t)virtualSize.height * 2 / 3;
            XQ_CHECK_MSG((int64_t)hint.width ==
                             (expectW > childHint.width
                                  ? expectW : childHint.width),
                         "w=%d expect=%lld", hint.width,
                         (long long)(expectW > childHint.width
                                         ? expectW : childHint.width));
            XQ_CHECK_MSG((int64_t)hint.height ==
                             (expectH > childHint.height
                                  ? expectH : childHint.height),
                         "h=%d expect=%lld", hint.height,
                         (long long)(expectH > childHint.height
                                         ? expectH : childHint.height));
#endif
        } else {
            /* 无主屏：基值 (0,0) ⇒ 恰为子窗提示的最大值。 */
            XQ_EQ_I64(hint.width, childHint.width);
            XQ_EQ_I64(hint.height, childHint.height);
        }
    }

    /* 虚表槽已挂：经 XWidget_sizeHint 分派到本类实现（非基类）。 */
    {
        XSize viaBase = XWidget_sizeHint_base((const XWidget*)area);
        XQ_EQ_I64(viaBase.width, hint.width);
        XQ_EQ_I64(viaBase.height, hint.height);
    }

    /* 空指针安全：对外唯一入口是 XWidget 虚分派，NULL 按其口径返回
     * 无效尺寸 (-1,-1)（《api命名风格》*_base 契约收敛后本类不再有
     * 独立公开函数）。 */
    XQ_CHECK(XWidget_sizeHint_base(NULL).width == -1);

    XClassDelete((XClass*)area);
}

XQ_TEST(XMdiArea, minimumSizeHintIncludesChildrenWhenScrollBarsOff)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XWidget* content;
    XMdiSubWindow* sw;
    XSize onHint;
    XSize offHint;
    XQ_REQUIRE(area != NULL);

    content = XWidget_create(NULL, 0);
    XQ_REQUIRE(content != NULL);
    /* 内容件最小提示用 setMinimumSizeHint（XWidget_minimumSizeHint 的
     * 存储位；setMinimumSize 只钳几何、不改提示值）。 */
    {
        XSize contentMin;
        XSize_init(&contentMin, 320, 240);
        XWidget_setMinimumSizeHint(content, &contentMin);
    }
    sw = XMdiArea_addSubWindow(area, content);
    XQ_REQUIRE(sw != NULL);

    /* 缺省策略 AsNeeded ⇒ scrollBarsEnabled=true ⇒ 不并子窗提示。 */
    onHint = XWidget_minimumSizeHint_base((const XWidget*)area);
    /* 关闭两条滚动条 ⇒ 逐子窗并入 minimumSizeHint（标题带 + 内容最小）。 */
    XAbstractScrollArea_setVerticalScrollBarPolicy(
        (XAbstractScrollArea*)area, XScrollBarPolicy_AlwaysOff);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)area, XScrollBarPolicy_AlwaysOff);
    offHint = XWidget_minimumSizeHint_base((const XWidget*)area);
    XQ_CHECK_MSG(offHint.width >= 320,
                 "offW=%d onW=%d", offHint.width, onHint.width);
    XQ_CHECK_MSG(offHint.height >= 240,
                 "offH=%d onH=%d", offHint.height, onHint.height);
    XQ_CHECK(offHint.width >= onHint.width);
    XQ_CHECK(offHint.height >= onHint.height);

    /* 虚表槽已挂（经 XWidget_minimumSizeHint 分派）。 */
    {
        XSize viaBase = XWidget_minimumSizeHint_base((const XWidget*)area);
        XQ_EQ_I64(viaBase.width, offHint.width);
        XQ_EQ_I64(viaBase.height, offHint.height);
    }
    /* 空指针安全同上：XWidget_minimumSizeHint_base(NULL) = (-1,-1)。 */
    XQ_CHECK(XWidget_minimumSizeHint_base(NULL).width == -1);

    XClassDelete((XClass*)area);
}

/* ==================== ⑫ Tabbed 视图页签条（P3-53） ===================== */
/* Qt qmdiarea.cpp:1523-1597：TabbedView 建单一条 QTabBar（页签=子窗，
 * currentChanged → 激活、tabCloseRequested → 关闭该子窗、tabMoved →
 * 子窗表平移），仅活动子窗可见；SubWindowView 撤条恢复平铺。 */

#if XTABBAR_ON
static XTabBar* xmaTestFindTabBar(XWidget* parent)
{
    const XVector* children;
    int64_t i;
    int64_t n;
    if (!parent) return NULL;
    children = XObject_children((XObject*)parent);
    if (!children) return NULL;
    n = XVector_size_base((const XContainer*)children);
    for (i = 0; i < n; ++i) {
        XObject* obj = *(XObject**)XVector_at_base(children, i);
        XVtable* vt;
        if (!obj) continue;
        vt = XClassGetVtable((XClass*)obj);
        if (vt && XVTABLE_GET_NAME(vt) &&
            XStrcmp(XVTABLE_GET_NAME(vt), "XTabBar") == 0)
            return (XTabBar*)obj;
    }
    return NULL;
}

XQ_TEST(XMdiArea, tabbedViewBuildsTabBarAndSyncs)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* sw1;
    XMdiSubWindow* sw2;
    XTabBar* bar;
    XRect before;
    XRect after;
    XQ_REQUIRE(area != NULL);
    XWidget_setGeometry((XWidget*)area, 0, 0, 640, 480);
    sw1 = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    sw2 = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    XQ_REQUIRE(sw1 != NULL && sw2 != NULL);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == sw2);
    before = XWidget_geometry((XWidget*)sw2);

    /* 建条前的属性：装配时下发（Qt setViewMode 建条四连发）。 */
    XMdiArea_setTabsClosable(area, true);
    XMdiArea_setTabsMovable(area, true);
    XMdiArea_setDocumentMode(area, true);
    XMdiArea_setTabShape(area, 4);
    XMdiArea_setViewMode(area, XMdiAreaViewMode_TabbedView);
    XQ_EQ_I64(XMdiArea_viewMode(area), XMdiAreaViewMode_TabbedView);

    bar = xmaTestFindTabBar((XWidget*)area);
    XQ_CHECK_PTR(bar);
    if (!bar) {
        XClassDelete((XClass*)area);
        return;
    }
    XQ_EQ_I64(XTabBar_count(bar), 2);
    XQ_CHECK(XTabBar_tabsClosable(bar));
    XQ_CHECK(XTabBar_isMovable(bar));
    XQ_CHECK(XTabBar_documentMode(bar));
    XQ_EQ_I64(XTabBar_shape(bar), 4);
    /* 页签下标 = 子窗 CreationOrder 下标；当前页=活动窗（sw2 → 1）。 */
    XQ_EQ_I64(XTabBar_currentIndex(bar), 1);
    /* 仅活动子窗可见（Qt 页签视图一页一窗）。判据用显式隐藏位：区域
     * 本身未 show 时 isVisible 恒假（父链不可见），isHidden 才是本
     * 机制维护的状态。 */
    XQ_CHECK(!XWidget_isHidden((XWidget*)sw2));
    XQ_CHECK(XWidget_isHidden((XWidget*)sw1));
    XQ_CHECK(!XWidget_isHidden((XWidget*)bar));
    /* 页签条让位：视口上边距 > 0，页签条位于 contentsRect 顶部条带。 */
    {
        XMargins m = XAbstractScrollArea_viewportMargins(
            (const XAbstractScrollArea*)area);
        XQ_CHECK(m.top > 0);
        XQ_CHECK(XWidget_height((XWidget*)bar) == m.top);
    }

    /* 切页签：激活对应子窗（currentChanged 槽）。 */
    XTabBar_setCurrentIndex(bar, 0);
    XQ_CHECK(XMdiArea_activeSubWindow(area) == sw1);
    XQ_CHECK(!XWidget_isHidden((XWidget*)sw1));
    XQ_CHECK(XWidget_isHidden((XWidget*)sw2));

    /* 反方向：setActiveSubWindow 驱动页签下标。 */
    XMdiArea_setActiveSubWindow(area, sw2);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 1);

    /* tabMoved：子窗表同序平移，页签↔子窗映射保持（模拟页签条拖拽
     * 发射的 tabMoved(from,to)，qmdiarea.cpp:766-785）。 */
    {
        const XVector* list = XMdiArea_subWindowList(area);
        XQ_REQUIRE(list && XVector_size_base((const XContainer*)list) == 2);
        XQ_CHECK(XVector_At_Base(list, 0, XMdiSubWindow*) == sw1);
        {
            int fromIdx = 0;
            int toIdx = 1;
            XVarList* args = XVarList_Create(XVar(int, fromIdx),
                                             XVar(int, toIdx));
            XQ_REQUIRE(args != NULL);
            XObject_emitSignal((XObject*)bar,
                               (size_t)XTabBar_tabMoved_signal(bar, 0, 1),
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
        XQ_CHECK(XVector_At_Base(list, 0, XMdiSubWindow*) == sw2);
        XQ_CHECK(XVector_At_Base(list, 1, XMdiSubWindow*) == sw1);
        {
            int fromIdx = 1;
            int toIdx = 0;
            XVarList* args = XVarList_Create(XVar(int, fromIdx),
                                             XVar(int, toIdx));
            XQ_REQUIRE(args != NULL);
            XObject_emitSignal((XObject*)bar,
                               (size_t)XTabBar_tabMoved_signal(bar, 1, 0),
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }

    /* 关闭页签：tabCloseRequested(idx) → 关闭该子窗（qmdiarea.cpp:756-764）。
     * 关 0 号（列表已还原为 [sw1, sw2]，即 sw1），余下活动窗 sw2 存活。 */
    {
        int closeIdx = 0;
        XVarList* args = XVarList_Create(XVar(int, closeIdx));
        XQ_REQUIRE(args != NULL);
        XObject_emitSignal((XObject*)bar,
                           (size_t)XTabBar_tabCloseRequested_signal(bar),
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        XQ_EQ_I64(XMdiArea_subWindowCount(area), 1);
        XQ_EQ_I64(XTabBar_count(bar), 1);
        (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
    }

    /* 切回子窗视图：撤条 + 还边距 + 余窗恢复可见 + 几何回放。 */
    XMdiArea_setViewMode(area, XMdiAreaViewMode_SubWindowView);
    XQ_EQ_I64(XMdiArea_viewMode(area), XMdiAreaViewMode_SubWindowView);
    XQ_CHECK(XWidget_isHidden((XWidget*)bar));
    {
        XMargins m = XAbstractScrollArea_viewportMargins(
            (const XAbstractScrollArea*)area);
        XQ_EQ_I64(m.top, 0);
    }
    XQ_CHECK(!XWidget_isHidden((XWidget*)sw2));
    after = XWidget_geometry((XWidget*)sw2);
    XQ_CHECK_MSG(after.width == before.width && after.height == before.height,
                 "before=%dx%d after=%dx%d", before.width, before.height,
                 after.width, after.height);

    XClassDelete((XClass*)area);
}
#endif /* XTABBAR_ON */

/* ==================== ⑬ 背景色/画刷（P3-54） =========================== */
/* Qt setBackground(const QBrush&) + paintEvent `fillRect(exposedRect,
 * d->background)`（qmdiarea.cpp:2669-2675）。X：便捷 ARGB 视图 +
 * XBrush 值句柄；NoBrush/0 = 调色板 Base 缺省。 */

XQ_TEST(XMdiArea, backgroundBrushAndColor)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    const XBrush* brush;
    XBrush solid;
    XColor color;
    XQ_REQUIRE(area != NULL);

    XQ_EQ_I64(XMdiArea_background(area), 0);
    brush = XMdiArea_background_2(area);
    XQ_CHECK_PTR(brush);
    XQ_EQ_I64(XBrush_style(brush), XBrushStyle_NoBrush);

    /* 便捷色：非 0 即纯色刷（单一事实源）。 */
    XMdiArea_setBackground(area, 0xFF102030u);
    XQ_EQ_I64(XMdiArea_background(area), 0xFF102030u);
    brush = XMdiArea_background_2(area);
    XQ_REQUIRE(brush != NULL);
    XQ_EQ_I64(XBrush_style(brush), XBrushStyle_SolidPattern);
    color = XBrush_color(brush);
    XQ_EQ_I64(XColor_rgba(&color), 0xFF102030u);

    /* 0 = 清除（NoBrush）。 */
    XMdiArea_setBackground(area, 0);
    XQ_EQ_I64(XMdiArea_background(area), 0);
    XQ_EQ_I64(XBrush_style(XMdiArea_background_2(area)), XBrushStyle_NoBrush);

    /* 画刷入口：纯色刷同步便捷色；NULL/NoBrush 清除。 */
    color = XColor_create_rgb(0x11, 0x22, 0x33, 0xFF);
    solid = XBrush_create_color(color, XBrushStyle_SolidPattern);
    XMdiArea_setBackground_2(area, &solid);
    XQ_EQ_I64(XMdiArea_background(area), 0xFF112233u);
    XQ_EQ_I64(XBrush_style(XMdiArea_background_2(area)),
              XBrushStyle_SolidPattern);
    XMdiArea_setBackground_2(area, NULL);
    XQ_EQ_I64(XMdiArea_background(area), 0);
    XQ_EQ_I64(XBrush_style(XMdiArea_background_2(area)), XBrushStyle_NoBrush);
    XQ_CHECK(XMdiArea_background_2(NULL) == NULL);
    XQ_EQ_I64(XMdiArea_background(NULL), 0);
    XMdiArea_setBackground(NULL, 0);
    XMdiArea_setBackground_2(NULL, NULL);

    /* 绘制烟测：设色后同步重绘不崩（paintEvent 走刷/色/Base 三态）。 */
    XWidget_show((XWidget*)area);
    XMdiArea_setBackground(area, 0xFF405060u);
    XWidget_repaint((XWidget*)area);
    XMdiArea_setBackground(area, 0);
    XWidget_repaint((XWidget*)area);

    XClassDelete((XClass*)area);
}

#endif /* XGUI_ON && XWIDGET_ON && XMDIAREA_ON */
