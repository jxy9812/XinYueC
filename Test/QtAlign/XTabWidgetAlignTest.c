/**
 * @file       XTabWidgetAlignTest.c
 * @brief      XTabWidget × Qt 6.8 QTabWidget 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtabwidget.h
 *               （addTab/insertTab/removeTab、tabText/tabEnabled/
 *               tabVisible、setCurrentIndex/setCurrentWidget、
 *               tabPosition/tabShape/elideMode/documentMode/
 *               usesScrollButtons/tabBarAutoHide、cornerWidget、
 *               clear、tabBar；sizeHint/minimumSizeHint/heightForWidth/
 *               hasHeightForWidth 重载；protected virtual
 *               tabInserted/tabRemoved）；
 *             - Qt 6.8.3 源码 qtabwidget.cpp：
 *               basicSize（:802-811 横放/竖放两套组合公式）、
 *               sizeHint（:837-878 全部**可见页签**的页 sizeHint 取大 +
 *               页签条 sizeHint（usesScrollButtons 时夹 200）+ 角部件）、
 *               minimumSizeHint（:880-908 页栈最小尺寸 + 页签条最小尺寸）、
 *               heightForWidth（:910-953 竖放先扣条宽，再逐页
 *               heightForWidth 取大）、hasHeightForWidth（:224-236 自身
 *               策略 || 页栈）、setTabAutoHide/isAutoHidden（:163-167）、
 *               insertTab（index 越界由 QStackedLayout::insertWidget 钳到
 *               末尾）、clear（:1403-1413 循环 removeTab(0)）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测：两个页（300x200 与
 *               50x20）切当前页后 sizeHint 恒 304x233（按**全部页**取大，
 *               不随当前页变化）；含换行页时 hasHeightForWidth=true。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON

#include "XTabWidget.h"
#include "XTabWidget_Protected.h"
#include "XTabBar.h"
#include "XStyleOption.h" /* XAbstractItemViewTextElideMode（省略枚举规范位） */
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XObject.h"
#include "XObject_Protected.h" /* XObject_event_base（鼠标事件注入）。 */
#include "XVarList.h"
#include "XString.h"
#include "XClass.h"
#include "XCoreApplication.h"
#include "XEvent.h"   /* XMouseEvent / XEvent */
#include "XThread.h"  /* [P0-35] 提示淡出等待。 */
#include "XToolTip.h" /* [P0-35] 容器转发的逐页提示经页签条弹出。 */

/** @brief 向控件直发一次鼠标移动（同 XGroupBoxAlignTest 的注入口径）。 */
static void xtw_mouseMove(XWidget* target, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!target) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
                     XKeyboardModifier_NoModifier, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

/** @brief 等提示淡出到点（hideText 恒 300ms 延迟隐藏；上限 2s 防呆）。 */
static bool xtw_waitToolTipHidden(int maxMs)
{
    int waited = 0;
    while (XToolTip_isVisible() && waited < maxMs) {
        XThread_msleep(10);
        (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
        waited += 10;
    }
    return !XToolTip_isVisible();
}

/** @brief 取 XToolTip_text() 的副本比对后释放。 */
static bool xtw_toolTipTextEquals(const char* expect)
{
    XString* text = XToolTip_text();
    const char* utf8;
    bool same;
    if (!text) return expect == NULL;
    utf8 = XString_toUtf8(text);
    same = (utf8 == NULL) ? (expect == NULL)
                          : (expect != NULL && strcmp(utf8, expect) == 0);
    XClassDelete((XClass*)text);
    return same;
}

/* ==================== ① 缺省值 + 页增删与索引钳制 ==================== */

XQ_TEST(XTabWidget, defaultsAndTabClamp)
{
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XWidget* page1;
    XWidget* page2;
    XWidget* page3;
    XWidget* page4;

    XQ_REQUIRE(tw != NULL);
    page1 = XWidget_create(NULL, 0);
    page2 = XWidget_create(NULL, 0);
    page3 = XWidget_create(NULL, 0);
    page4 = XWidget_create(NULL, 0);
    XQ_REQUIRE(page1 && page2 && page3 && page4);
    XWidget_setGeometry((XWidget*)tw, 0, 0, 640, 480);

    /* 缺省值：无页、当前 -1、North、非文档模式、ElideNone、不可关闭/拖动。
     * elideMode 不是文档里的 ElideRight：QTabBarPrivate 从
     * styleHint(SH_TabBar_ElideMode) 初始化（qtabbar.cpp:392/2456），
     * Fusion 的 QCommonStyle 返回 ElideNone(3)——本机 Qt 6.8.0
     * offscreen 探针 `QTabWidget tw; tw.elideMode()` 实测 3。 */
    XQ_EQ_I64(XTabWidget_count(tw), 0);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), -1);
    XQ_CHECK(XTabWidget_currentWidget(tw) == NULL);
    XQ_EQ_I64(XTabWidget_tabPosition(tw), 0);
    XQ_CHECK(!XTabWidget_documentMode(tw));
    XQ_EQ_I64(XTabWidget_elideMode(tw),
              XAbstractItemViewTextElideMode_ElideNone);
    XQ_CHECK(!XTabWidget_tabsClosable(tw));
    XQ_CHECK(!XTabWidget_isMovable(tw));
    XQ_CHECK(!XTabWidget_tabBarAutoHide(tw));
    XQ_CHECK_PTR(XTabWidget_tabBar(tw));

    /* addTab 返回索引；首页成为当前页（QTabBar 首项 setCurrentIndex）。 */
    XQ_EQ_I64(XTabWidget_addTab_2(tw, page1, "页一"), 0);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), 0);
    XQ_CHECK(XTabWidget_currentWidget(tw) == page1);
    XQ_EQ_I64(XTabWidget_addTab_2(tw, page2, "页二"), 1);
    XQ_EQ_I64(XTabWidget_indexOf(tw, page2), 1);
    XQ_CHECK(XTabWidget_widget(tw, 1) == page2);

    /* 越界索引一律追加（Qt 经 QStackedLayout::insertWidget 钳到末尾）。 */
    XQ_EQ_I64(XTabWidget_insertTab_2(tw, 99, page3, "页三"), 2);
    XQ_EQ_I64(XTabWidget_insertTab_2(tw, -1, page4, "页四"), 3);
    XQ_EQ_I64(XTabWidget_count(tw), 4);
    XQ_CHECK(XTabWidget_widget(tw, 99) == NULL);
    XQ_EQ_I64(XTabWidget_indexOf(tw, NULL), -1);

    /* 文本往返（setTabText/tabText 转发页签条）。 */
    XQ_EQ_STR(XTabWidget_tabText_2(tw, 1), "页二");
    XTabWidget_setTabText_2(tw, 1, "改名");
    XQ_EQ_STR(XTabWidget_tabText_2(tw, 1), "改名");

    /* 当前页迁移与越界忽略（Qt QTabBar::setCurrentIndex 对 -1/越界无操作，
     * 本机 Qt 6.8 实测 currentChanged 计数 0）。 */
    XTabWidget_setCurrentIndex(tw, 2);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), 2);
    XTabWidget_setCurrentIndex(tw, 99);
    XTabWidget_setCurrentIndex(tw, -1);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), 2);
    XTabWidget_setCurrentWidget(tw, page1);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), 0);

    /* clear 逐页 removeTab，页控件不销毁并归还调用方（Qt 同）。 */
    XTabWidget_clear(tw);
    XQ_EQ_I64(XTabWidget_count(tw), 0);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), -1);
    XQ_CHECK(XWidget_parentWidget(page1) == NULL);
    XQ_CHECK(!XWidget_isVisible(page1));

    XClassDelete((XClass*)page1);
    XClassDelete((XClass*)page2);
    XClassDelete((XClass*)page3);
    XClassDelete((XClass*)page4);
    XClassDelete((XClass*)tw);
    /* 页容器走 deleteLater；冲刷延迟删除让泄漏门看到真实回收。 */
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ② sizeHint/minimumSizeHint 按全部页 + 方位 ======= */

XQ_TEST(XTabWidget, sizeHintAllPagesAndPositions)
{
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XWidget* page1;
    XWidget* page2;
    XSize page1Hint;
    XSize page1Min;
    XSize barHint;
    XSize hint;
    XSize minHint;

    XQ_REQUIRE(tw != NULL);
    page1 = XWidget_create(NULL, 0);
    page2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(page1 && page2);
    XSize_init(&page1Hint, 300, 200);
    XSize_init(&page1Min, 120, 90);
    XWidget_setSizeHint(page1, &page1Hint);
    XWidget_setMinimumSizeHint(page1, &page1Min);
    {
        XSize page2Hint;
        XSize_init(&page2Hint, 500, 100);
        XWidget_setSizeHint(page2, &page2Hint);
    }
    XQ_EQ_I64(XTabWidget_addTab_2(tw, page1, "一"), 0);
    XQ_EQ_I64(XTabWidget_addTab_2(tw, page2, "二"), 1);

    barHint = XWidget_sizeHint_base((XWidget*)XTabWidget_tabBar(tw));
    XQ_CHECK(barHint.width > 0 && barHint.height > 0);

    /* North（横放）：宽 = max(页宽, 条宽)；高 = 页高 + 条高
     * （basicSize 横放分支 qtabwidget.cpp:805-807）。页尺寸取**全部可见
     * 页**的最大值 ⇒ 500（不是当前页 300）。 */
    hint = XWidget_sizeHint_base((XWidget*)tw);
    XQ_EQ_I64(hint.width, 500 > barHint.width ? 500 : barHint.width);
    XQ_CHECK(hint.width >= 500);
    XQ_EQ_I64(hint.height, 200 + barHint.height);

    /* 切当前页不影响首选尺寸（本机 Qt 实测两页切页后 hint 不变）。 */
    XTabWidget_setCurrentIndex(tw, 0);
    XQ_EQ_I64(XWidget_sizeHint_base((XWidget*)tw).width, hint.width);
    XTabWidget_setCurrentIndex(tw, 1);
    XQ_EQ_I64(XWidget_sizeHint_base((XWidget*)tw).width, hint.width);

    /* 隐藏的页签不参与首选尺寸（Qt `if (d->tabs->isTabVisible(i))`）。 */
    XTabWidget_setTabVisible(tw, 1, false);
    XQ_CHECK(XWidget_sizeHint_base((XWidget*)tw).width < hint.width);
    XTabWidget_setTabVisible(tw, 1, true);

    /* minimumSizeHint：页栈「智能最小尺寸」取大后，再经 basicSize 与**页签条
     * 最小提示**组合（qtabwidget.cpp:880-908：`t = d->tabs->minimumSizeHint()`；
     * XTabBar 已重载该虚槽，横向条给「可见页签最小宽之和 × 条高」=
     * qtabbar.cpp:1502-1516）。故高 = 页最小高 + 条最小高，宽取两者较大
     * （gap #189：此前按「条最小提示=0」口径断言，条不参与）。 */
    minHint = XWidget_minimumSizeHint_base((XWidget*)tw);
    {
        XSize barMin = XWidget_minimumSizeHint_base((XWidget*)XTabWidget_tabBar(tw));
        XQ_CHECK(barMin.width > 0 && barMin.height > 0);
        XQ_EQ_I64(minHint.width,
                  page1Min.width > barMin.width ? page1Min.width
                                                : barMin.width);
        XQ_EQ_I64(minHint.height, page1Min.height + barMin.height);
    }

    /* West（竖放）：宽 = 页宽 + max(条宽, 角件宽)；高 = max(页高, 条高)
     * （basicSize 竖放分支）。[P0-01 2026-11-12] setTabPosition(West) 现在
     * 会把页签条形状映射为 RoundedWest(2) —— 页签条**改为竖排**，其
     * sizeHint 两轴转置（宽=页签厚基线、高=自然宽之和，Qt
     * qtabbar.cpp:1599-1602/1506-1509）。故必须重读条提示，不能沿用
     * 切换前（横排）的 barHint。 */
    XTabWidget_setTabPosition(tw, 2);
    XQ_EQ_I64(XTabBar_shape(XTabWidget_tabBar(tw)), 2); /* RoundedWest */
    barHint = XWidget_sizeHint_base((XWidget*)XTabWidget_tabBar(tw));
    XQ_EQ_I64(barHint.width, XTabBar_tabHeight(XTabWidget_tabBar(tw)));
    hint = XWidget_sizeHint_base((XWidget*)tw);
    XQ_EQ_I64(hint.width, 500 + barHint.width);
    XQ_EQ_I64(hint.height, 200);
    XTabWidget_setTabPosition(tw, 0);
    /* 回 North ⇒ 形状回 RoundedNorth，条提示再转置回横排；后续角部件
     * 断言必须用**当前**（横排）条提示。 */
    XQ_EQ_I64(XTabBar_shape(XTabWidget_tabBar(tw)), 0);
    barHint = XWidget_sizeHint_base((XWidget*)XTabWidget_tabBar(tw));

    /* 角部件参与首选尺寸（横放：宽含角件宽，高取角件高与条高较大者）。 */
    {
        XWidget* corner = XWidget_create(NULL, 0);
        XSize cornerHint;
        XQ_REQUIRE(corner != NULL);
        XSize_init(&cornerHint, 40, 30);
        XWidget_setSizeHint(corner, &cornerHint);
        XTabWidget_setCornerWidget(tw, corner, XTABWIDGET_CORNER_TOPRIGHT);
        hint = XWidget_sizeHint_base((XWidget*)tw);
        XQ_CHECK(hint.width >= 500);
        XQ_EQ_I64(hint.height, 200 + (barHint.height > 30 ? barHint.height : 30));
        XTabWidget_setCornerWidget(tw, NULL, XTABWIDGET_CORNER_TOPRIGHT);
        XClassDelete((XClass*)corner); /* 已解除角件登记，随调用方释放 */
    }

    XTabWidget_clear(tw);
    XClassDelete((XClass*)page1);
    XClassDelete((XClass*)page2);
    XClassDelete((XClass*)tw);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ③ heightForWidth 由页传播 ==================== */

/** @brief 测试用 hfw 回调：高 = 宽 / 4。 */
static int xtw_hfwHandler(XWidget* widget, int width, void* userData)
{
    (void)widget;
    (void)userData;
    return width / 4;
}

XQ_TEST(XTabWidget, heightForWidthFromPages)
{
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XWidget* plain;
    XWidget* wrap;
    XSize barHint;
    int expected;

    XQ_REQUIRE(tw != NULL);
    plain = XWidget_create(NULL, 0);
    wrap = XWidget_create(NULL, 0);
    XQ_REQUIRE(plain && wrap);
    XWidget_setHeightForWidthHandler(wrap, xtw_hfwHandler, NULL);
    XQ_CHECK(XWidget_hasHeightForWidth(wrap));

    XQ_EQ_I64(XTabWidget_addTab_2(tw, plain, "普通"), 0);
    /* 无 hfw 页 ⇒ 页签容器自身不报 hfw（Qt hasHeightForWidth = 自身策略
     * || 页栈策略，qtabwidget.cpp:224-236）。 */
    XQ_CHECK(!XWidget_hasHeightForWidth((XWidget*)tw));

    XQ_EQ_I64(XTabWidget_addTab_2(tw, wrap, "换行"), 1);
    XQ_CHECK(XWidget_hasHeightForWidth((XWidget*)tw));

    /* heightForWidth = basicSize 高 = 页高（宽/4）+ 条高（横放）。 */
    barHint = XWidget_sizeHint_base((XWidget*)XTabWidget_tabBar(tw));
    expected = 400 / 4 + barHint.height;
    XQ_EQ_I64(XWidget_heightForWidth((XWidget*)tw, 400), expected);

    /* 移除唯一 hfw 页 ⇒ 不再报 hfw（回调随页集合重挂）。 */
    XTabWidget_removeTab(tw, 1);
    XQ_CHECK(!XWidget_hasHeightForWidth((XWidget*)tw));
    XQ_CHECK(XWidget_heightForWidth((XWidget*)tw, 400) == -1);
    XQ_CHECK(XWidget_parentWidget(wrap) == NULL); /* 页归还调用方 */

    /* 保护虚槽缺省实现可直接调用（空操作；子类经虚表重载）。 */
    XTabWidget_tabInserted(tw, 0);
    XTabWidget_tabRemoved(tw, 0);
    XTabWidget_tabInserted_base(tw, 0);
    XTabWidget_tabRemoved_base(tw, 0);

    XTabWidget_clear(tw);
    XClassDelete((XClass*)wrap);
    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)tw);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ④ 角部件两槽位（gap #185） ======================== */

XQ_TEST(XTabWidget, cornerTwoSlots)
{
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XWidget* a;
    XWidget* b;
    XWidget* c;

    XQ_REQUIRE(tw != NULL);
    a = XWidget_create(NULL, 0);
    b = XWidget_create(NULL, 0);
    c = XWidget_create(NULL, 0);
    XQ_REQUIRE(a && b && c);

    /* Qt 只有两个槽位：`corner & Qt::TopRightCorner` 二选一
     * （qtabwidget.cpp:592-625）⇒ TopLeft 与 BottomLeft 返回同一控件。 */
    XTabWidget_setCornerWidget(tw, a, XTABWIDGET_CORNER_TOPLEFT);
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_TOPLEFT) == a);
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_BOTTOMLEFT) == a);
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_TOPRIGHT) == NULL);

    /* BottomRight 落右槽（与 TopRight 同槽）。 */
    XTabWidget_setCornerWidget(tw, b, XTABWIDGET_CORNER_BOTTOMRIGHT);
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_TOPRIGHT) == b);
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_BOTTOMRIGHT) == b);
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_TOPLEFT) == a);

    /* BottomLeft 覆盖左槽：旧左件只隐藏不销毁（Qt 同）。 */
    XTabWidget_setCornerWidget(tw, c, XTABWIDGET_CORNER_BOTTOMLEFT);
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_TOPLEFT) == c);
    XQ_CHECK(!XWidget_isVisible(a));
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_TOPRIGHT) == b);

    XTabWidget_setCornerWidget(tw, NULL, XTABWIDGET_CORNER_TOPLEFT);
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_TOPLEFT) == NULL);
    XTabWidget_setCornerWidget(tw, NULL, XTABWIDGET_CORNER_TOPRIGHT);
    XQ_CHECK(XTabWidget_cornerWidget(tw, XTABWIDGET_CORNER_TOPRIGHT) == NULL);

    XClassDelete((XClass*)a);
    XClassDelete((XClass*)b);
    XClassDelete((XClass*)c);
    XClassDelete((XClass*)tw);
}

/* ==================== ⑤ tabToolTip 与 tabWhatsThis 独立（gap #188） ==== */

XQ_TEST(XTabWidget, toolTipAndWhatsThisIndependent)
{
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XWidget* page;

    XQ_REQUIRE(tw != NULL);
    page = XWidget_create(NULL, 0);
    XQ_REQUIRE(page != NULL);
    XQ_EQ_I64(XTabWidget_addTab_2(tw, page, "页"), 0);

    /* 对标 QTabWidget::setTabToolTip / setTabWhatsThis（qtabwidget.cpp:
     * 1182-1226）：两套独立存储，互不覆盖。 */
    XTabWidget_setTabToolTip_2(tw, 0, "tip");
    XTabWidget_setTabWhatsThis_2(tw, 0, "what");
    XQ_EQ_STR(XTabWidget_tabToolTip(tw, 0) ? XString_toUtf8(
                  XTabWidget_tabToolTip(tw, 0)) : NULL, "tip");
    XQ_EQ_STR(XTabWidget_tabWhatsThis(tw, 0) ? XString_toUtf8(
                  XTabWidget_tabWhatsThis(tw, 0)) : NULL, "what");

    XTabWidget_setTabWhatsThis_2(tw, 0, "what2");
    XQ_EQ_STR(XTabWidget_tabToolTip(tw, 0) ? XString_toUtf8(
                  XTabWidget_tabToolTip(tw, 0)) : NULL, "tip");
    XQ_EQ_STR(XTabWidget_tabWhatsThis(tw, 0) ? XString_toUtf8(
                  XTabWidget_tabWhatsThis(tw, 0)) : NULL, "what2");

    XTabWidget_setTabToolTip_2(tw, 0, "tip2");
    XQ_EQ_STR(XTabWidget_tabWhatsThis(tw, 0) ? XString_toUtf8(
                  XTabWidget_tabWhatsThis(tw, 0)) : NULL, "what2");

    XTabWidget_clear(tw);
    XClassDelete((XClass*)page);
    XClassDelete((XClass*)tw);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ⑥ setTabBar 替换页签条（gap #186） ================ */

XQ_TEST(XTabWidget, setTabBarReplacement)
{
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XTabBar* bar;
    XTabBar* rejected;
    XWidget* page;

    XQ_REQUIRE(tw != NULL);
    bar = XTabBar_create(NULL, 0);
    rejected = XTabBar_create(NULL, 0);
    page = XWidget_create(NULL, 0);
    XQ_REQUIRE(bar && rejected && page);

    /* 缺省是内嵌条；换入外部条（Qt 要求在任何 addTab 之前调用，
     * qtabwidget.cpp:711-739）。 */
    XQ_CHECK(XTabWidget_tabBar(tw) != bar);
    XQ_EQ_I64(XTabWidget_setTabBar(tw, bar), 0);
    XQ_CHECK(XTabWidget_tabBar(tw) == bar);
    XQ_CHECK(XWidget_parentWidget((XWidget*)bar) == (XWidget*)tw);
    XQ_EQ_I64(XTabWidget_setTabBar(tw, NULL), -1); /* Qt Q_ASSERT(tb) */

    /* 新条承接页签管理（addTab 落到新条上）。 */
    XQ_EQ_I64(XTabWidget_addTab_2(tw, page, "A"), 0);
    XQ_EQ_I64(XTabBar_count(bar), 1);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), 0);
    XQ_EQ_STR(XTabWidget_tabText_2(tw, 0), "A");

    /* 已有页签时拒绝替换（Qt 的未定义区：删旧条会连带删页签而页栈不动，
     * 状态自相矛盾）。 */
    XQ_EQ_I64(XTabWidget_setTabBar(tw, rejected), -1);
    XQ_CHECK(XTabWidget_tabBar(tw) == bar);

    XClassDelete((XClass*)rejected);
    XTabWidget_clear(tw);
    XClassDelete((XClass*)page);
    XClassDelete((XClass*)tw); /* 换入的 bar 是子控件，随容器级联释放。 */
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ⑦ iconSize 的 QSize 面（转发页签条） ============== */

XQ_TEST(XTabWidget, iconSizeQSizeForwarding)
{
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XTabBar* bar;
    XSize s;
    XSize isz;

    XQ_REQUIRE(tw != NULL);
    /* 缺省 16×16（转发内嵌条；Qt QTabWidget::iconSize 即 QTabBar::iconSize
     * 的样式缺省 PM_TabBarIconSize）。 */
    isz = XTabWidget_iconSize_2(tw);
    XQ_EQ_I64(isz.width, 16);
    XQ_EQ_I64(isz.height, 16);
    XQ_EQ_I64(XTabWidget_iconSize(tw), 16);

    /* 旧单值入口 = 方边（宽=高）。 */
    XTabWidget_setIconSize(tw, 24);
    XQ_EQ_I64(XTabWidget_iconSize(tw), 24);
    isz = XTabWidget_iconSize_2(tw);
    XQ_EQ_I64(isz.width, 24);
    XQ_EQ_I64(isz.height, 24);

    /* 二维入口：宽高分别转发（非方形不再丢高）。 */
    XSize_init(&s, 32, 20);
    XTabWidget_setIconSize_2(tw, s);
    isz = XTabWidget_iconSize_2(tw);
    XQ_EQ_I64(isz.width, 32);
    XQ_EQ_I64(isz.height, 20);
    XQ_EQ_I64(XTabWidget_iconSize(tw), 32); /* int 视图取宽 */

    /* 落到真实页签条（同一份存储，容器不另存一份）。 */
    bar = XTabWidget_tabBar(tw);
    XQ_REQUIRE(bar != NULL);
    XQ_EQ_I64(XTabBar_iconSize_2(bar).width, 32);
    XQ_EQ_I64(XTabBar_iconSize_2(bar).height, 20);
    /* 高 20 ⇒ 页签高 20+8=28（> 基线 24 即撑高，Qt 口径
     * qMax(字体高, 图标高) + vframe）。 */
    XQ_EQ_I64(XTabBar_tabHeight(bar), 28);

    /* 高 40 ⇒ 页签高撑到 48（40 + 8 垂直留白），条高提示同值。 */
    XSize_init(&s, 32, 40);
    XTabWidget_setIconSize_2(tw, s);
    XQ_EQ_I64(XTabBar_tabHeight(bar), 48);
    XQ_EQ_I64(XTabBar_barHeightHint(bar, 640), 48);
    XQ_EQ_I64(XTabWidget_iconSize_2(tw).height, 40);

    /* 单值复位 0 = 两轴缺省 16。 */
    XTabWidget_setIconSize(tw, 0);
    XQ_EQ_I64(XTabWidget_iconSize(tw), 16);
    isz = XTabWidget_iconSize_2(tw);
    XQ_EQ_I64(isz.width, 16);
    XQ_EQ_I64(isz.height, 16);

    /* NULL 防御：与旧单值入口的 NULL 口径一致（0）。 */
    isz = XTabWidget_iconSize_2(NULL);
    XQ_EQ_I64(isz.width, 0);
    XQ_EQ_I64(isz.height, 0);
    XTabWidget_setIconSize_2(NULL, s);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑧ 位置 → 页签条形状映射 + 竖条几何（P0-01） ===== */

/** @brief setTabPosition 把页签位置映射成页签条形状并给出竖条几何。
 * @details 依据 Qt 6.8 QTabWidgetPrivate::updateTabBarPosition
 *          （qtabwidget.cpp:960-982）与 _q_tb_tabBarShapeFrom
 *          （qtabwidget.cpp:1420-1432）：North/South/West/East 分别映射
 *          Rounded{North,South,West,East}（Triangular 档同构，家族位
 *          即 `形状 & ~3`）。West/East 的形状让 XTabBar 竖排
 *          （QTabBarPrivate::verticalTabs，qtabbar_p.h:248-253）——
 *          条几何因此是「厚=barHeightHint × 全高」，不是只 24 高的横条。 */
XQ_TEST(XTabWidget, tabPositionDrivesBarShape)
{
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XWidget* page;
    XTabBar* bar;

    XQ_REQUIRE(tw != NULL);
    page = XWidget_create(NULL, 0);
    XQ_REQUIRE(page != NULL);
    XWidget_setGeometry((XWidget*)tw, 0, 0, 640, 480);
    XQ_EQ_I64(XTabWidget_addTab_2(tw, page, "页"), 0);
    bar = XTabWidget_tabBar(tw);
    XQ_REQUIRE(bar != NULL);

    /* 四向 → 圆角族四形状。 */
    XQ_EQ_I64(XTabBar_shape(bar), 0); /* 缺省 North ⇒ RoundedNorth */
    XTabWidget_setTabPosition(tw, 1);
    XQ_EQ_I64(XTabBar_shape(bar), 1); /* South ⇒ RoundedSouth */
    XQ_EQ_I64(XWidget_y((XWidget*)bar) + XWidget_height((XWidget*)bar), 480);
    XTabWidget_setTabPosition(tw, 2);
    XQ_EQ_I64(XTabBar_shape(bar), 2); /* West ⇒ RoundedWest（竖排） */
    XQ_EQ_I64(XWidget_x((XWidget*)bar), 0);
    XQ_EQ_I64(XWidget_width((XWidget*)bar), XTabBar_barHeightHint(bar, 480));
    XQ_EQ_I64(XWidget_height((XWidget*)bar), 480); /* 竖条占全高 */
    XTabWidget_setTabPosition(tw, 3);
    XQ_EQ_I64(XTabBar_shape(bar), 3); /* East ⇒ RoundedEast（竖排） */
    XQ_EQ_I64(XWidget_x((XWidget*)bar) + XWidget_width((XWidget*)bar), 640);
    XQ_EQ_I64(XWidget_height((XWidget*)bar), 480);

    /* 三角族：家族位（形状 & ~3）跟随页签条当前形状 ⇒ 三角档下 West
     * 映射 6=TriangularWest（仍竖排）。 */
    XTabWidget_setTabShape(tw, 5); /* TriangularSouth */
    XQ_EQ_I64(XTabBar_shape(bar), 5);
    XTabWidget_setTabPosition(tw, 2);
    XQ_EQ_I64(XTabBar_shape(bar), 6);
    XTabWidget_setTabPosition(tw, 0);
    XQ_EQ_I64(XTabBar_shape(bar), 4); /* TriangularNorth */
    XTabWidget_setTabShape(tw, 0);
    XQ_EQ_I64(XTabBar_shape(bar), 0);

    /* 竖排下页签几何两轴转置：首签宽=截面厚、高=自然宽量级。 */
    XTabWidget_setTabPosition(tw, 2);
    {
        XRect r0;
        XQ_REQUIRE(XTabBar_tabRect(bar, 0, &r0));
        XQ_EQ_I64(r0.x, 0);
        XQ_EQ_I64(r0.width, XTabBar_tabHeight(bar));
        XQ_CHECK(r0.height > 0);
    }
    XTabWidget_setTabPosition(tw, 0);

    XTabWidget_clear(tw);
    XClassDelete((XClass*)page);
    XClassDelete((XClass*)tw);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ============ ⑨ 容器转发的逐页提示真弹（P0-35：转发 → 页签条消费） ==== */

/** @brief setTabToolTip 经容器转发到页签条后，悬停该页签**真的弹出**。
 * @details P0-35 的两层：容器转发（XTabWidget_setTabToolTip → 页签条的
 *          平行数组）+ 页签条悬停消费（P3-61 的 MouseMove 命中链）。
 *          此前只有第一层，转发后无人消费 ⇒ 悬停永不出现。 */
XQ_TEST(XTabWidget, tabToolTipForwardedAndShownOnHover)
{
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XWidget* page;
    XTabBar* bar;
    XRect r0;

    XQ_REQUIRE(tw != NULL);
    page = XWidget_create(NULL, 0);
    XQ_REQUIRE(page != NULL);
    XWidget_resize(page, 300, 200);
    XWidget_setGeometry((XWidget*)tw, 0, 0, 400, 300);
    XQ_EQ_I64(XTabWidget_addTab_2(tw, page, "唯一页"), 0);
    bar = XTabWidget_tabBar(tw);
    XQ_REQUIRE(bar != NULL);
    /* 容器层可回读（转发到页签条同一份存储）。 */
    XTabWidget_setTabToolTip_2(tw, 0, "容器提示");
    XQ_EQ_STR(XTabWidget_tabToolTip(tw, 0)
                  ? XString_toUtf8(XTabWidget_tabToolTip(tw, 0)) : NULL,
              "容器提示");

    XToolTip_hideText();
    XQ_CHECK(xtw_waitToolTipHidden(2000));

    /* 悬停页签条上的该页签 ⇒ 提示真的弹出（消费端在页签条）。 */
    XQ_REQUIRE(XTabBar_tabRect(bar, 0, &r0));
    xtw_mouseMove((XWidget*)bar, r0.x + r0.width / 2,
                  r0.y + r0.height / 2);
    XQ_CHECK_MSG(XToolTip_isVisible(),
                 "容器转发的逐页提示应在悬停时弹出（P0-35）");
    XQ_CHECK(xtw_toolTipTextEquals("容器提示"));

    XToolTip_hideText();
    XQ_CHECK(xtw_waitToolTipHidden(2000));

    XTabWidget_clear(tw);
    XClassDelete((XClass*)page);
    XClassDelete((XClass*)tw);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON */
