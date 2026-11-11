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
#include "XObject.h"
#include "XClass.h"
#include "XVarList.h"
#include "XContainer.h"
#include "XVector.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"

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
    XObject_connect_1((XObject*)area, XSignal(XMdiArea_subWindowActivated_signal),
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

#endif /* XGUI_ON && XWIDGET_ON && XMDIAREA_ON */
