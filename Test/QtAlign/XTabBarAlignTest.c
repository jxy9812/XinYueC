/**
 * @file       XTabBarAlignTest.c
 * @brief      XTabBar × Qt 6.8 QTabBar 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/qtabbar.h
 *               （shape 8 值、addTab/insertTab/removeTab/moveTab、
 *               isTabEnabled/setTabEnabled、isTabVisible/setTabVisible、
 *               tabText/setTabText、tabTextColor、tabIcon、elideMode、
 *               tabToolTip/tabWhatsThis/setTabData、tabRect/tabAt、
 *               currentIndex/count、sizeHint/**minimumSizeHint override**、
 *               drawBase、iconSize、usesScrollButtons、tabsClosable、
 *               setTabButton/tabButton、selectionBehaviorOnRemove、
 *               expanding、isMovable、documentMode、autoHide、
 *               changeCurrentOnDrag、accessibleTabName、setCurrentIndex、
 *               protected virtual tabSizeHint/minimumTabSizeHint/
 *               tabInserted/tabRemoved/tabLayoutChange（:159-163）、
 *               五个信号）；
 *             - Qt 6.8.3 源码 qtabbar.cpp：
 *               insertTab 在当前项之前插入时静默右移当前索引（:937-1007）、
 *               setCurrentIndex 仅接受有效索引（:1400-1436）、
 *               removeTab 的 selectionBehaviorOnRemove 三分支（:1009-1099）、
 *               isTabVisible 越界返回 false（:1136-1140）、
 *               setTabVisible 隐藏当前页时改选可见页（:1149-1166）、
 *               sizeHint 只并入**可见**页签（:1487-1498）、
 *               minimumSizeHint = usesScrollButtons 关时取最小矩形并集 /
 *               开时取 (2×滚动按钮宽+75, sizeHint 高)（:1502-1516）、
 *               moveTab 发 tabMoved 且当前索引变化时补发 currentChanged
 *               （:2007-2097）、setShape 同值早退（:878-886）、
 *               initStyleOption 按 d->elideMode 省略（:202-210）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 探针实测：
 *               新建 tabBar：selectionBehaviorOnRemove=SelectRightTab(1)、
 *               elideMode=ElideNone(3)、iconSize=16×16、
 *               usesScrollButtons=1、expanding=1、autoHide=0、
 *               shape=RoundedNorth(0)；零页签 minimumSizeHint=(123,0)
 *               （=2×24+75 宽、高取 sizeHint 高）；
 *               两个页签后 insertTab(0) 使 currentIndex 0→1（不发信号）；
 *               hide 当前页签后 currentIndex 跳到右邻可见页；全部隐藏时
 *               **不改选也不发 currentChanged(-1)**（cur 保持原值）；
 *               自然宽：空文本 24、"A" 39、"AAAA" 54、长文本 163；带 16px
 *               图标 +14；tabsClosable +24 且 tabButton(RightSide) 返回
 *               CloseButton 子控件（关掉开关后返回空）；
 *               expanding（400 宽，自然 53/163/39）⇒ 119/163/118；
 *               expanding=false ⇒ 自然宽；
 *               访问 0→2→3→2 后 SelectPreviousTab 移除 2 选中原签 3。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABBAR_ON

#include "XTabBar.h"
#include "XAbstractItemView.h" /* XAbstractItemViewTextElideMode 单一编号。 */
#include "XAbstractButton.h"
#include "XPushButton.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h" /* XObject_event_base（事件注入路径）。 */
#include "XVarList.h"
#include "XString.h"
#include "XPainter.h" /* 自然宽口径：文字宽 + 留白。 */
#include "XClass.h"
#include "XCoreApplication.h"
#include "XEvent.h" /* XEVENT_TYPE_DEFERRED_DELETE（deleteLater 拍）。 */
#include "XImage.h"  /* [P3-58/59/60] 离屏像素断言（对齐 XDialAlignTest 口径）。 */
#include "XThread.h"     /* [P3-61] 提示淡出等待。 */
#include "XToolTip.h"    /* [P3-61] 逐页提示弹出（进程级单例）。 */

/** @brief 本库页签高（XTabBar.c 的 XTABBAR_TAB_H）。 */
#define TAB_ALIGN_TAB_H 24
/** @brief 页签自然宽留白（XTabBar.c 的 XTABBAR_TAB_PADDING）。 */
#define TAB_ALIGN_PADDING 24
/** @brief 滚动按钮宽（XTabBar.c 的 XTABBAR_SCROLL_BTN_W）。 */
#define TAB_ALIGN_SCROLL_BTN_W 18

/* ==================== 信号探针 ==================== */

static int tb_currentChanged = 0; /**< currentChanged 次数。 */
static int tb_lastCurrent = -2;   /**< 最近一次 currentChanged 载荷。 */
static int tb_movedCount = 0;     /**< tabMoved 次数。 */
static int tb_closeRequested = 0; /**< tabCloseRequested 次数。 */
static int tb_lastClose = -2;     /**< 最近一次 tabCloseRequested 载荷。 */
static int tb_clicked = 0;        /**< tabBarClicked 次数。 */

static void tb_currentChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, index);
    tb_lastCurrent = index;
    ++tb_currentChanged;
}

static void tb_movedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++tb_movedCount;
}

static void tb_closeRequestedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, index);
    tb_lastClose = index;
    ++tb_closeRequested;
}

static void tb_clickedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++tb_clicked;
}

/** @brief 复位信号探针。 */
static void tb_reset(void)
{
    tb_currentChanged = 0;
    tb_lastCurrent = -2;
    tb_movedCount = 0;
    tb_closeRequested = 0;
    tb_lastClose = -2;
    tb_clicked = 0;
}

/** @brief 挂接全部信号探针（每个用例各自挂接）。 */
static void tb_attach(XTabBar* bar)
{
    if (!bar) return;
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_currentChanged_signal(bar, 0),
                      tb_currentChangedSlot);
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_tabMoved_signal(bar, 0, 0),
                      tb_movedSlot);
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_tabCloseRequested_signal(bar),
                      tb_closeRequestedSlot);
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_tabBarClicked_signal(bar, 0),
                      tb_clickedSlot);
}

/** @brief 页签文字的自然宽（与实现同源：文字宽 + 留白）。 */
static int tb_naturalWidth(const XTabBar* bar, const char* text)
{
    XFont font = XWidget_font((const XWidget*)bar);
    int w = XPainter_textWidth(&font, text ? text : "") + TAB_ALIGN_PADDING;
    XClassDeinit((XClass*)&font);
    return w;
}

/** @brief 向控件直发一次鼠标事件（同 XGroupBoxAlignTest 的注入口径）。 */
static void tb_mouse(XWidget* target, XEventType type, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!target) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, XMouseButton_NoButton,
                     XKeyboardModifier_NoModifier, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

/** @brief 等提示淡出到点（hideText 恒 300ms 延迟隐藏；上限 2s 防呆）。 */
static bool tb_waitToolTipHidden(int maxMs)
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
static bool tb_toolTipTextEquals(const char* expect)
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

/** @brief 两张抓图中矩形区的差异像素数（用于「只改了颜色/版式」断言）。 */
static int tb_countDiff(const XImage* a, const XImage* b, const XRect* r)
{
    int x;
    int y;
    int n = 0;
    if (!a || !b || !r) return 0;
    for (y = r->y; y < r->y + r->height; ++y) {
        for (x = r->x; x < r->x + r->width; ++x) {
            if (XImage_pixel(a, x, y) != XImage_pixel(b, x, y)) ++n;
        }
    }
    return n;
}

/* ==================== ① 缺省值 + 当前索引规则 + 可见性 ============== */

XQ_TEST(XTabBar, defaultsAndCurrentIndexRules)
{
    XTabBar* bar = XTabBar_create(NULL, 0);
    XSize minHint;

    XQ_REQUIRE(bar != NULL);
    XWidget_setGeometry((XWidget*)bar, 0, 0, 400, TAB_ALIGN_TAB_H);

    /* 缺省值（qtabbar_p.h:56-81 + 本机 Qt 6.8 探针）。 */
    XQ_EQ_I64(XTabBar_count(bar), 0);
    XQ_EQ_I64(XTabBar_currentIndex(bar), -1);
    XQ_CHECK(XTabBar_count(bar) == 0); /* [P2-56] isEmpty 已删，改判 count。 */
    XQ_CHECK(XTabBar_drawBase(bar));
    XQ_CHECK(XTabBar_expanding(bar));
    XQ_CHECK(!XTabBar_isMovable(bar));
    XQ_CHECK(!XTabBar_documentMode(bar));
    XQ_CHECK(!XTabBar_autoHide(bar));
    XQ_CHECK(!XTabBar_changeCurrentOnDrag(bar));
    XQ_CHECK(!XTabBar_tabsClosable(bar));
    XQ_EQ_I64(XTabBar_shape(bar), 0);
    XQ_CHECK(XTabBar_usesScrollButtons(bar));
    /* Qt 6.8 探针：elideMode=ElideNone(3)、selBeh=SelectRightTab(1)、
     * iconSize=16（样式 PM_TabBarIconSize）。 */
    XQ_EQ_I64(XTabBar_elideMode(bar),
              XAbstractItemViewTextElideMode_ElideNone);
    XQ_EQ_I64(XTabBar_selectionBehaviorOnRemove(bar), 1);
    XQ_EQ_I64(XTabBar_iconSize(bar), 16);
    /* QSize 面缺省 16×16（两轴同源；int 视图取宽）。 */
    XQ_EQ_I64(XTabBar_iconSize_2(bar).width, 16);
    XQ_EQ_I64(XTabBar_iconSize_2(bar).height, 16);
    XQ_EQ_I64(XTabBar_tabHeight(bar), TAB_ALIGN_TAB_H);
    /* 越界查询：Qt isTabVisible 返回 false（qtabbar.cpp:1136-1140）。 */
    XQ_CHECK(!XTabBar_isTabVisible(bar, 0));
    XQ_CHECK(!XTabBar_isTabEnabled(bar, 0));

    /* minimumSizeHint（qtabbar.cpp:1502-1516）：usesScrollButtons 开 ⇒
     * (2×滚动按钮宽+75, sizeHint 高)；空条 sizeHint 高 0（Qt 探针
     * 零页签 minHint=(123,0)，本库滚动按钮宽 18 ⇒ 111）。 */
    minHint = XWidget_minimumSizeHint_base((XWidget*)bar);
    XQ_EQ_I64(minHint.width, 2 * TAB_ALIGN_SCROLL_BTN_W + 75);
    XQ_EQ_I64(minHint.height, 0);

    tb_reset();
    tb_attach(bar);

    /* 首项插入即成为当前页并发一次 currentChanged（qtabbar.cpp:967-969）。 */
    XQ_EQ_I64(XTabBar_addTab_2(bar, "一"), 0);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 0);
    XQ_EQ_I64(tb_currentChanged, 1);
    XQ_EQ_I64(tb_lastCurrent, 0);

    /* 追加不动当前页。 */
    XQ_EQ_I64(XTabBar_addTab_2(bar, "二"), 1);
    XQ_EQ_I64(XTabBar_count(bar), 2);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 0);
    XQ_EQ_I64(tb_currentChanged, 1);
    XQ_EQ_STR(XTabBar_tabText_2(bar, 1), "二");
    XTabBar_setTabText_2(bar, 1, "二改");
    XQ_EQ_STR(XTabBar_tabText_2(bar, 1), "二改");

    /* 切换/同值/越界/-1：只有真正的变化发信号（Qt 实测 99 与 -1 无操作）。 */
    XTabBar_setCurrentIndex(bar, 1);
    XQ_EQ_I64(tb_currentChanged, 2);
    XQ_EQ_I64(tb_lastCurrent, 1);
    XTabBar_setCurrentIndex(bar, 1);
    XTabBar_setCurrentIndex(bar, 99);
    XTabBar_setCurrentIndex(bar, -1);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 1);
    XQ_EQ_I64(tb_currentChanged, 2);

    /* 在当前项之前插入 ⇒ 当前索引静默右移（qtabbar.cpp:970-971；Qt 探针
     * current 0→1 且无 currentChanged）。 */
    XQ_EQ_I64(XTabBar_insertTab_2(bar, 0, "零"), 0);
    XQ_EQ_I64(XTabBar_count(bar), 3);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 2);
    XQ_EQ_I64(tb_currentChanged, 2);
    /* 末尾插入不动当前项。 */
    XQ_EQ_I64(XTabBar_insertTab_2(bar, XTabBar_count(bar), "尾"), 3);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 2);

    /* 隐藏当前页 ⇒ 改选可见页并发 currentChanged（qtabbar.cpp:1149-1166；
     * Qt 探针：右邻优先，右无则左）。 */
    tb_reset();
    XTabBar_setTabVisible(bar, 2, false);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 3); /* 右邻 */
    XQ_EQ_I64(tb_currentChanged, 1);
    XQ_EQ_I64(tb_lastCurrent, 3);
    XTabBar_setTabVisible(bar, 3, false);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 1); /* 右无 → 左邻 1 */
    /* 全部隐藏：Qt 不改选、也不发 currentChanged(-1)（探针）。 */
    tb_reset();
    XTabBar_setTabVisible(bar, 1, false);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 0); /* 左邻 0 */
    XQ_EQ_I64(tb_currentChanged, 1);
    tb_reset();
    XTabBar_setTabVisible(bar, 0, false);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 0); /* 保持（无可见页可改选） */
    XQ_EQ_I64(tb_currentChanged, 0);
    XQ_CHECK(!XTabBar_isTabVisible(bar, 0));
    XQ_CHECK(XTabBar_tabRect(bar, 0, &(XRect){ 0 })); /* 仍可查询几何 */
    XTabBar_setTabVisible(bar, 0, true);
    XQ_CHECK(XTabBar_isTabVisible(bar, 0));

    /* 移除最后一个页签 ⇒ 空、当前 -1、发 currentChanged(-1)。 */
    while (XTabBar_count(bar) > 0) XTabBar_removeTab(bar, 0);
    XQ_EQ_I64(XTabBar_count(bar), 0);
    XQ_EQ_I64(XTabBar_currentIndex(bar), -1);
    XQ_EQ_I64(tb_lastCurrent, -1);

    XClassDelete((XClass*)bar);
}

/* ==================== ② removeTab 三种选择行为（含 per-tab lastTab） == */

XQ_TEST(XTabBar, removeTabSelectionBehaviors)
{
    XTabBar* bar = XTabBar_create(NULL, 0);

    XQ_REQUIRE(bar != NULL);
    XWidget_setGeometry((XWidget*)bar, 0, 0, 400, TAB_ALIGN_TAB_H);
    XTabBar_addTab_2(bar, "0");
    XTabBar_addTab_2(bar, "1");
    XTabBar_addTab_2(bar, "2");
    XTabBar_addTab_2(bar, "3");
    XQ_EQ_I64(XTabBar_count(bar), 4);

    /* SelectLeftTab：移除当前页（2）⇒ 左邻（原下标 1 的页签）。 */
    XTabBar_setSelectionBehaviorOnRemove(bar, 0);
    XTabBar_setCurrentIndex(bar, 2);
    {
        const char* leftNeighbor = XTabBar_tabText_2(bar, 1);
        XTabBar_removeTab(bar, 2);
        XQ_EQ_I64(XTabBar_currentIndex(bar), 1);
        XQ_EQ_STR(XTabBar_tabText_2(bar, 1), leftNeighbor);
        XQ_EQ_STR(leftNeighbor, "1");
    }

    /* SelectRightTab（缺省）：移除当前页 ⇒ 右邻。 */
    XTabBar_addTab_2(bar, "4");
    XTabBar_setSelectionBehaviorOnRemove(bar, 1);
    XTabBar_setCurrentIndex(bar, 1);
    {
        const char* rightNeighbor = XTabBar_tabText_2(bar, 2);
        XTabBar_removeTab(bar, 1);
        XQ_EQ_I64(XTabBar_currentIndex(bar), 1);
        XQ_EQ_STR(XTabBar_tabText_2(bar, 1), rightNeighbor);
    }

    /* SelectPreviousTab：取该页的 lastTab（上次停留页）而不是「前一个
     * 页签」。Qt 6.8 探针：访问 0→2→3→2 后移除 2 选中原签 3。 */
    {
        XTabBar* prev = XTabBar_create(NULL, 0);
        XQ_REQUIRE(prev != NULL);
        XWidget_setGeometry((XWidget*)prev, 0, 0, 400, TAB_ALIGN_TAB_H);
        XTabBar_addTab_2(prev, "0");
        XTabBar_addTab_2(prev, "1");
        XTabBar_addTab_2(prev, "2");
        XTabBar_addTab_2(prev, "3");
        XTabBar_setSelectionBehaviorOnRemove(prev, 2);
        XTabBar_setCurrentIndex(prev, 0);
        XTabBar_setCurrentIndex(prev, 2);
        XTabBar_setCurrentIndex(prev, 3);
        XTabBar_setCurrentIndex(prev, 2); /* lastTab[2] = 3 */
        XTabBar_removeTab(prev, 2);
        XQ_EQ_I64(XTabBar_currentIndex(prev), 2);
        XQ_EQ_STR(XTabBar_tabText_2(prev, 2), "3");
        XClassDelete((XClass*)prev);
    }

    /* 移除当前页之前的页签 ⇒ 当前索引左移且仍指同一页签。 */
    XTabBar_setCurrentIndex(bar, 2);
    {
        const char* kept = XTabBar_tabText_2(bar, 2);
        XTabBar_removeTab(bar, 0);
        XQ_EQ_I64(XTabBar_currentIndex(bar), 1);
        XQ_EQ_STR(XTabBar_tabText_2(bar, 1), kept);
    }

    XClassDelete((XClass*)bar);
}

/* ==================== ③ moveTab / 自然宽布局 / 最小尺寸 / 角按钮 === */

XQ_TEST(XTabBar, tabSizeHintLayoutAndMinimum)
{
    XTabBar* bar = XTabBar_create(NULL, 0);
    int nat[3];
    int i;
    int sumNatural;
    XRect r0;
    XRect r1;
    XRect r2;
    XSize hint;
    XSize isz;
    XSize s;

    XQ_REQUIRE(bar != NULL);
    XWidget_setGeometry((XWidget*)bar, 0, 0, 400, TAB_ALIGN_TAB_H);
    XTabBar_addTab_2(bar, "Tab-A");
    XTabBar_addTab_2(bar, "Tab-B");
    XTabBar_addTab_2(bar, "Tab-C");

    /* 自然宽 = 文字宽 + 24（Qt Fusion 实测：空文本 24、"A" 39）；高 = 条高。 */
    for (i = 0; i < 3; ++i) {
        XSize s = XTabBar_tabSizeHint_base(bar, i);
        const char* text = XTabBar_tabText_2(bar, i);
        nat[i] = tb_naturalWidth(bar, text);
        XQ_EQ_I64(s.width, nat[i]);
        XQ_EQ_I64(s.height, TAB_ALIGN_TAB_H);
        /* 虚槽分派到基类实现。 */
        XQ_EQ_I64(XTabBar_minimumTabSizeHint_base(bar, i).width, nat[i]);
    }
    sumNatural = nat[0] + nat[1] + nat[2];

    /* sizeHint = 可见页签自然宽之和（与条宽/expanding 无关）。 */
    hint = XWidget_sizeHint_base((XWidget*)bar);
    XQ_EQ_I64(hint.width, sumNatural);
    XQ_EQ_I64(hint.height, TAB_ALIGN_TAB_H);

    /* expanding（缺省开）：多余宽按均分摊平，总宽 == 条宽，且各签不小于
     * 自然宽、宽度差 ≤1（Qt layoutTabs 的迭代均分口径）。 */
    XQ_CHECK(XTabBar_expanding(bar));
    XQ_CHECK(XTabBar_tabRect(bar, 0, &r0));
    XQ_CHECK(XTabBar_tabRect(bar, 1, &r1));
    XQ_CHECK(XTabBar_tabRect(bar, 2, &r2));
    XQ_EQ_I64(r0.x, 0);
    XQ_EQ_I64(r1.x, r0.width);
    XQ_EQ_I64(r2.x, r0.width + r1.width);
    XQ_EQ_I64(r0.width + r1.width + r2.width, 400);
    XQ_CHECK(r0.width >= nat[0] && r1.width >= nat[1] && r2.width >= nat[2]);
    XQ_CHECK(r0.width - r2.width <= 1 && r2.width - r0.width <= 1);

    /* expanding=false ⇒ 自然宽（不拉伸）。 */
    XTabBar_setExpanding(bar, false);
    XQ_CHECK(XTabBar_tabRect(bar, 0, &r0));
    XQ_CHECK(XTabBar_tabRect(bar, 1, &r1));
    XQ_CHECK(XTabBar_tabRect(bar, 2, &r2));
    XQ_EQ_I64(r0.width, nat[0]);
    XQ_EQ_I64(r1.width, nat[1]);
    XQ_EQ_I64(r2.width, nat[2]);
    XQ_EQ_I64(r1.x, nat[0]);
    XQ_EQ_I64(r2.x, nat[0] + nat[1]);
    XTabBar_setExpanding(bar, true);

    /* 隐藏页签不占布局宽（宽 0）也不参与 sizeHint。 */
    XTabBar_setTabVisible(bar, 1, false);
    XQ_EQ_I64(XWidget_sizeHint_base((XWidget*)bar).width, nat[0] + nat[2]);
    XQ_CHECK(XTabBar_tabRect(bar, 0, &r0));
    XQ_CHECK(XTabBar_tabRect(bar, 1, &r1));
    XQ_CHECK(XTabBar_tabRect(bar, 2, &r2));
    XQ_EQ_I64(r1.width, 0);
    XQ_EQ_I64(r2.x, r0.x + r0.width); /* 隐藏签不占位 */
    XQ_EQ_I64(r0.x + r0.width + r2.width, 400);
    XTabBar_setTabVisible(bar, 1, true);

    /* minimumSizeHint：滚动按钮开 ⇒ (2×18+75, 条高)；关 ⇒ 最小宽并集
     * （minTab == tabSizeHint，Fusion 探针一致）。 */
    hint = XWidget_minimumSizeHint_base((XWidget*)bar);
    XQ_EQ_I64(hint.width, 2 * TAB_ALIGN_SCROLL_BTN_W + 75);
    XQ_EQ_I64(hint.height, TAB_ALIGN_TAB_H);
    XTabBar_setUsesScrollButtons(bar, false);
    hint = XWidget_minimumSizeHint_base((XWidget*)bar);
    XQ_EQ_I64(hint.width, sumNatural);
    XQ_EQ_I64(hint.height, TAB_ALIGN_TAB_H);
    XQ_CHECK(!XTabBar_usesScrollButtons(bar));
    XTabBar_setUsesScrollButtons(bar, true);

    /* 溢出滚动：条宽 100 < 自然总宽 ⇒ 单行 + 两端按钮视口（页签 x 从
     * 按钮宽 18 起），scrollOffset 初始 0。 */
    XWidget_setGeometry((XWidget*)bar, 0, 0, 100, TAB_ALIGN_TAB_H);
    XQ_CHECK(XTabBar_isOverflowed(bar));
    XQ_EQ_I64(XTabBar_scrollOffset(bar), 0);
    XQ_CHECK(XTabBar_tabRect(bar, 0, &r0));
    XQ_EQ_I64(r0.x, TAB_ALIGN_SCROLL_BTN_W);
    XQ_EQ_I64(r0.width, nat[0]);
    /* 关闭滚动按钮 ⇒ 不再进入滚动模式，页签从 x=0 起（可越出条宽）。 */
    XTabBar_setUsesScrollButtons(bar, false);
    XQ_CHECK(XTabBar_tabRect(bar, 0, &r0));
    XQ_EQ_I64(r0.x, 0);
    XTabBar_setUsesScrollButtons(bar, true);
    XWidget_setGeometry((XWidget*)bar, 0, 0, 400, TAB_ALIGN_TAB_H);

    /* 图标占位：有图标 ⇒ 自然宽 + 图标**宽** + 间距（Qt 探针 +14 @16px）；
     * 页签**高**只吃图标高（QSize 两轴分开，qtabbar.cpp:1594-1600）。 */
    XTabBar_setTabIcon_2(bar, 0, "icon.png");
    XQ_EQ_I64(XTabBar_iconSize(bar), 16);
    isz = XTabBar_iconSize_2(bar);
    XQ_EQ_I64(isz.width, 16);
    XQ_EQ_I64(isz.height, 16);
    XQ_EQ_I64(XTabBar_tabSizeHint_base(bar, 0).width, nat[0] + 16 + 4);
    XQ_EQ_I64(XTabBar_tabSizeHint_base(bar, 0).height, TAB_ALIGN_TAB_H);
    XTabBar_setIconSize(bar, 24); /* 旧单值入口 = 方边（宽=高） */
    XQ_EQ_I64(XTabBar_tabSizeHint_base(bar, 0).width, nat[0] + 24 + 4);
    XQ_EQ_I64(XTabBar_iconSize_2(bar).height, 24);
    XTabBar_setIconSize(bar, 0); /* 0 = 默认 16（两轴） */
    XQ_EQ_I64(XTabBar_iconSize(bar), 16);
    XQ_EQ_I64(XTabBar_iconSize_2(bar).width, 16);
    /* QSize 面：宽只进自然宽、高只进页签高（30×22 ⇒ 高 max(24,22+8)=30）。 */
    XSize_init(&s, 30, 22);
    XTabBar_setIconSize_2(bar, s);
    isz = XTabBar_iconSize_2(bar);
    XQ_EQ_I64(isz.width, 30);
    XQ_EQ_I64(isz.height, 22);
    XQ_EQ_I64(XTabBar_iconSize(bar), 30); /* int 视图取宽 */
    XQ_EQ_I64(XTabBar_tabSizeHint_base(bar, 0).width, nat[0] + 30 + 4);
    XQ_EQ_I64(XTabBar_tabSizeHint_base(bar, 0).height, 30);
    XQ_EQ_I64(XTabBar_tabHeight(bar), 30);
    XQ_EQ_I64(XTabBar_barHeightHint(bar, 400), 30);
    XQ_CHECK(XTabBar_tabRect(bar, 0, &r0));
    XQ_EQ_I64(r0.height, 30);
    /* 单轴 0 = 该轴取默认 16（旧入口 setIconSize(0) 的复位语义）。 */
    XSize_init(&s, 0, 40);
    XTabBar_setIconSize_2(bar, s);
    isz = XTabBar_iconSize_2(bar);
    XQ_EQ_I64(isz.width, 16);
    XQ_EQ_I64(isz.height, 40);
    XQ_EQ_I64(XTabBar_tabSizeHint_base(bar, 0).width, nat[0] + 16 + 4);
    XQ_EQ_I64(XTabBar_tabHeight(bar), 48); /* 40 + 8 垂直留白 */
    /* 负轴忽略整次调用（旧单值入口的负值忽略语义）。 */
    XSize_init(&s, -1, 10);
    XTabBar_setIconSize_2(bar, s);
    XQ_EQ_I64(XTabBar_iconSize_2(bar).height, 40);
    /* NULL 防御：两轴缺省 16 / 页签高缺省 24。 */
    XQ_EQ_I64(XTabBar_iconSize_2(NULL).width, 16);
    XQ_EQ_I64(XTabBar_iconSize_2(NULL).height, 16);
    XQ_EQ_I64(XTabBar_tabHeight(NULL), TAB_ALIGN_TAB_H);
    XSize_init(&s, 20, 20);
    XTabBar_setIconSize_2(NULL, s);
    XTabBar_setTabIcon(bar, 0, NULL);
    XTabBar_setIconSize(bar, 0);
    XQ_EQ_I64(XTabBar_tabSizeHint_base(bar, 0).width, nat[0]);
    XQ_EQ_I64(XTabBar_tabSizeHint_base(bar, 0).height, TAB_ALIGN_TAB_H);

    XClassDelete((XClass*)bar);
}

/* ==================== ④ moveTab / 页签属性 / 关闭按钮 =============== */

XQ_TEST(XTabBar, moveTabPropertiesAndCloseButton)
{
    XTabBar* bar = XTabBar_create(NULL, 0);
    XAbstractButton* button;
    XAbstractButton* closeButton;
    XString* text;
    XRect rt;

    XQ_REQUIRE(bar != NULL);
    XWidget_setGeometry((XWidget*)bar, 0, 0, 400, TAB_ALIGN_TAB_H);
    XTabBar_addTab_2(bar, "一");
    XTabBar_addTab_2(bar, "二");
    XTabBar_setCurrentIndex(bar, 0);

    /* 启用/文本颜色/提示/帮助/数据/无障碍名往返。 */
    XQ_CHECK(XTabBar_isTabEnabled(bar, 0));
    XTabBar_setTabEnabled(bar, 0, false);
    XQ_CHECK(!XTabBar_isTabEnabled(bar, 0));
    XTabBar_setTabEnabled(bar, 0, true);
    XTabBar_setTabTextColor(bar, 1, 0xFF112233u);
    XQ_EQ_I64(XTabBar_tabTextColor(bar, 1), 0xFF112233u);
    XTabBar_setTabToolTip_2(bar, 1, "提示");
    XQ_EQ_STR(XTabBar_tabToolTip_2(bar, 1), "提示");
    XTabBar_setTabWhatsThis_2(bar, 1, "帮助");
    XQ_EQ_STR(XTabBar_tabWhatsThis_2(bar, 1), "帮助");
    XTabBar_setAccessibleTabName_2(bar, 1, "无障碍");
    XQ_EQ_STR(XTabBar_accessibleTabName_2(bar, 1), "无障碍");
    XTabBar_setTabData_2(bar, 1, "payload");
    XQ_EQ_STR(XTabBar_tabData_2(bar, 1), "payload");
    XTabBar_setTabIcon_2(bar, 0, "icon.png");
    XQ_EQ_STR(XTabBar_tabIcon_2(bar, 0), "icon.png");
    /* 复制返回（XString 主版本）契约。 */
    text = XTabBar_tabText(bar, 0);
    XQ_CHECK_PTR(text);
    if (text) XQ_EQ_STR(XString_toUtf8(text), "一");
    if (text) XClassDelete((XClass*)text);

    /* 自定义角按钮：reparent 归页签条（Qt setTabButton 接管语义），
     * removeTab 时该槽位按钮延迟销毁。 */
    button = (XAbstractButton*)XPushButton_create(NULL, 0);
    XQ_REQUIRE(button != NULL);
    XTabBar_setTabButton(bar, 0, XTabBarButtonPosition_RightSide, button);
    XQ_CHECK(XTabBar_tabButton(bar, 0, XTabBarButtonPosition_RightSide) ==
             button);
    XQ_CHECK(XTabBar_tabButtonRight(bar, 0) == button); /* RightSide 便捷版同槽 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)button) == (XWidget*)bar);
    /* NULL 清除槽位。 */
    XTabBar_setTabButton(bar, 0, XTabBarButtonPosition_LeftSide, NULL);
    XQ_CHECK(XTabBar_tabButton(bar, 0, XTabBarButtonPosition_LeftSide) ==
             NULL);
    /* 清空右角自定义按钮（对象仍挂父链，随条析构回收），后续用例在
     * 干净槽位上验证 tabsClosable 的自动关闭按钮。 */
    XTabBar_setTabButton(bar, 0, XTabBarButtonPosition_RightSide, NULL);
    XQ_CHECK(XTabBar_tabButtonRight(bar, 0) == NULL);

    /* moveTab：换位、发射 tabMoved、当前索引跟随并补发 currentChanged
     * （qtabbar.cpp:2087-2089；本机 Qt 实测同）。加第三签使「不涉及当前页
     * 的换位」可独立断言。 */
    XTabBar_addTab_2(bar, "三");
    tb_reset();
    tb_attach(bar);
    XTabBar_setCurrentIndex(bar, 0);
    XTabBar_moveTab(bar, 0, 1);
    XQ_EQ_I64(tb_movedCount, 1);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 1);
    XQ_EQ_I64(tb_currentChanged, 1);
    XQ_EQ_STR(XTabBar_tabText_2(bar, 1), "一");
    XQ_EQ_STR(XTabBar_tabText_2(bar, 0), "二");
    /* 换位不涉及当前页（当前 = 2）⇒ 只发 tabMoved。 */
    XTabBar_setCurrentIndex(bar, 2);
    tb_reset();
    XTabBar_moveTab(bar, 0, 1);
    XQ_EQ_I64(tb_movedCount, 1);
    XQ_EQ_I64(XTabBar_currentIndex(bar), 2);
    XQ_EQ_I64(tb_currentChanged, 0);

    /* tabsClosable：每页真实 CloseButton 子控件（Qt 探针：tabButton(i,
     * RightSide) 返回 CloseButton、关掉开关后返回空）。 */
    XTabBar_setTabsClosable(bar, true);
    XQ_CHECK(XTabBar_tabsClosable(bar));
    closeButton = XTabBar_tabButtonRight(bar, 0);
    XQ_CHECK_PTR(closeButton);
    XQ_CHECK(closeButton != NULL);
    if (closeButton != NULL) {
        XQ_CHECK(XWidget_parentWidget((XWidget*)closeButton) == (XWidget*)bar);
        /* 关闭按钮落在页签右端（Qt 由样式 SE_TabBarTabRightButton 定位）。 */
        XQ_CHECK(XTabBar_tabRect(bar, 0, &rt));
        {
            XRect bg = XWidget_geometry((XWidget*)closeButton);
            XQ_CHECK(bg.x + bg.width <= rt.x + rt.width);
            XQ_CHECK(bg.x >= rt.x);
        }
        /* 点关闭按钮只发 tabCloseRequested、不改当前页（Qt 同）。 */
        tb_reset();
        {
            int curBefore = XTabBar_currentIndex(bar);
            XAbstractButton_click(closeButton);
            XQ_EQ_I64(tb_closeRequested, 1);
            XQ_EQ_I64(tb_lastClose, 0);
            XQ_EQ_I64(XTabBar_currentIndex(bar), curBefore);
            XQ_EQ_I64(tb_clicked, 0);
        }
    }
    XTabBar_setTabsClosable(bar, false);
    XQ_CHECK(XTabBar_tabButtonRight(bar, 0) == NULL);
    XQ_CHECK(XTabBar_tabButtonRight(bar, 1) == NULL);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);

    /* 移除该页签 ⇒ 槽位清空、按钮走延迟销毁。 */
    XTabBar_removeTab(bar, 0);
    XQ_CHECK(XTabBar_tabButton(bar, 0, XTabBarButtonPosition_RightSide) ==
             NULL);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);

    XClassDelete((XClass*)bar);
}

/* ==================== ⑤ 竖排（P0-01，West/East 形状） ================== */

/** @brief 竖排三链路：tabSizeHint/sizeHint 两轴转置、tabRect 沿 y 累计、
 *         tabAt 沿 y 命中、滚动按钮在上下端。
 * @details 依据 Qt 6.8 qtabbar_p.h:248-253（verticalTabs(shape) = Rounded/
 *          Triangular West/East 四值）、qtabbar.cpp:1599-1602（竖排
 *          tabSizeHint = (截面厚, 文本宽+留白)）、:1351-1355（tabRect 竖排
 *          沿 y 平移 scrollOffset）、:1506-1509（竖排 minimumSizeHint =
 *          (sizeHint().width(), 2×滚动按钮+75)）。XTabBar 无独立
 *          orientation：形状 2/3/6/7 即竖排。 */
XQ_TEST(XTabBar, verticalShapeGeometry)
{
    XTabBar* bar = XTabBar_create(NULL, 0);
    int nat0;
    int nat1;
    int nat2;
    int tabH;
    int scrollMin;
    XRect r0;
    XRect r1;
    XRect r2;
    XSize hint;
    XSize minHint;

    XQ_REQUIRE(bar != NULL);
    /* 本用例专查竖排几何的自然堆叠口径：expanding 的均分摊平由
     * tabSizeHintLayoutAndMinimum（③）覆盖，这里关掉以免页签宽随条宽
     * 重排，保证「沿主轴按自然宽累计」的断言与字体度量解耦。 */
    XTabBar_setExpanding(bar, false);
    XWidget_setGeometry((XWidget*)bar, 0, 0, 400, 400);
    XTabBar_addTab_2(bar, "一");
    XTabBar_addTab_2(bar, "二");
    XTabBar_addTab_2(bar, "三");
    tabH = XTabBar_tabHeight(bar);
    nat0 = tb_naturalWidth(bar, "一");
    nat1 = tb_naturalWidth(bar, "二");
    nat2 = tb_naturalWidth(bar, "三");

    /* 横排基线（形状 0）：沿 x 累计（条宽 400 ≥ 自然总宽，不进溢出）。 */
    XQ_CHECK(XTabBar_tabRect(bar, 0, &r0));
    XQ_CHECK(XTabBar_tabRect(bar, 1, &r1));
    XQ_EQ_I64(r0.x, 0);
    XQ_EQ_I64(r0.y, 0);
    XQ_EQ_I64(r1.x, nat0);
    XQ_EQ_I64(r0.height, tabH);

    /* 形状 2 = RoundedWest（竖排）：tabRect 两轴转置、沿 y 累计。
     * 宽度回到竖条口径 40（截面厚仍是 tabH，宽度只作背景板）。 */
    XWidget_setGeometry((XWidget*)bar, 0, 0, 40, 400);
    XTabBar_setShape(bar, 2);
    XQ_EQ_I64(XTabBar_shape(bar), 2);
    XQ_CHECK(XTabBar_tabRect(bar, 0, &r0));
    XQ_CHECK(XTabBar_tabRect(bar, 1, &r1));
    XQ_CHECK(XTabBar_tabRect(bar, 2, &r2));
    XQ_EQ_I64(r0.x, 0);          /* 截面左缘固定 0。 */
    XQ_EQ_I64(r0.y, 0);          /* 首签贴顶端。 */
    XQ_EQ_I64(r0.width, tabH);   /* 页签宽 = 截面厚（同一 24 基线）。 */
    XQ_EQ_I64(r0.height, nat0);  /* 页签高 = 自然宽（文本沿 y 铺开）。 */
    XQ_EQ_I64(r1.y, nat0);
    XQ_EQ_I64(r1.height, nat1);
    XQ_EQ_I64(r2.y, nat0 + nat1);
    XQ_EQ_I64(r2.height, nat2);

    /* sizeHint 两轴转置；barHeightHint（竖条厚度）仍为页签厚基线。 */
    hint = XWidget_sizeHint_base((XWidget*)bar);
    XQ_EQ_I64(hint.width, tabH);
    XQ_EQ_I64(hint.height, nat0 + nat1 + nat2);
    XQ_EQ_I64(XTabBar_barHeightHint(bar, 400), tabH);

    /* minimumSizeHint 竖排＝(sizeHint 宽, 2×滚动按钮 + 75)。 */
    scrollMin = 2 * TAB_ALIGN_SCROLL_BTN_W + 75;
    minHint = XWidget_minimumSizeHint_base((XWidget*)bar);
    XQ_EQ_I64(minHint.width, hint.width);
    XQ_EQ_I64(minHint.height, scrollMin);
    /* 关滚动按钮 ⇒ 最小矩形并集（竖排沿 y）。 */
    XTabBar_setUsesScrollButtons(bar, false);
    minHint = XWidget_minimumSizeHint_base((XWidget*)bar);
    XQ_EQ_I64(minHint.width, tabH);
    XQ_EQ_I64(minHint.height, nat0 + nat1 + nat2);
    XTabBar_setUsesScrollButtons(bar, true);

    /* tabAt 沿 y 命中；截面（x）越界不命中。[P2-55] tabIndexAt 已删，
     * 改组 XPoint 直调 tabAt（原 tabIndexAt 就是转调同一内部函数）。 */
    {
        XPoint hit;
        XPoint_init(&hit, 5, 0);
        XQ_EQ_I64(XTabBar_tabAt(bar, &hit), 0);
        XPoint_init(&hit, 5, nat0 + 1);
        XQ_EQ_I64(XTabBar_tabAt(bar, &hit), 1);
        XPoint_init(&hit, 5, nat0 + nat1 + 1);
        XQ_EQ_I64(XTabBar_tabAt(bar, &hit), 2);
        XPoint_init(&hit, tabH + 4, nat0 + 1);
        XQ_EQ_I64(XTabBar_tabAt(bar, &hit), -1);
    }

    /* 形状 3 = RoundedEast 同为竖排（方向只影响文本旋转方向）。 */
    XTabBar_setShape(bar, 3);
    XQ_CHECK(XTabBar_tabRect(bar, 1, &r1));
    XQ_EQ_I64(r1.y, nat0);
    XQ_EQ_I64(r1.width, tabH);

    /* 竖排溢出滚动：按钮在上下端（视口起点沿 y 偏移一个按钮宽）。 */
    XWidget_setGeometry((XWidget*)bar, 0, 0, 40, 50);
    XQ_CHECK(XTabBar_isOverflowed(bar));
    XQ_CHECK(XTabBar_tabRect(bar, 0, &r0));
    XQ_EQ_I64(r0.x, 0);
    XQ_EQ_I64(r0.y, TAB_ALIGN_SCROLL_BTN_W);
    XQ_EQ_I64(XTabBar_scrollOffset(bar), 0);

    /* 复位形状 0 ⇒ 回到横排（沿 x 累计）。 */
    XWidget_setGeometry((XWidget*)bar, 0, 0, 400, TAB_ALIGN_TAB_H);
    XTabBar_setShape(bar, 0);
    XQ_CHECK(XTabBar_tabRect(bar, 1, &r1));
    XQ_EQ_I64(r1.x, nat0);
    XQ_EQ_I64(r1.y, 0);
    XQ_EQ_I64(XWidget_sizeHint_base((XWidget*)bar).height, tabH);

    XClassDelete((XClass*)bar);
}

/* ============ ⑥ 文本色 / drawBase / documentMode 的绘制消费 ============
 * P3-58（setTabTextColor 存储即生效）、P3-59（setDrawBase 控制每页基线）、
 * P3-60（documentMode 不画基线）。三条都是纯绘制产出，断言用**离屏抓图
 * 差异**（同 XDialAlignTest 的 XWidget_grab 口径：对同一控件两态各抓一次，
 * 比较矩形区差异像素数），不做具体色值断言（样式层刷色由 B2 交）。 */
XQ_TEST(XTabBar, textColorDrawBaseAndDocumentModePaint)
{
    /* 无边框标志：本用例抓的是**控件自身绘制**（页签 24 行紧贴抓图顶部）。
     * 带装饰的顶层会在抓图合成 28px 标题条（行 0..27）把页签区整个盖住
     * （XDial 的探针点在行 28 以下才侥幸不撞），故按
     * XWindowDecorationAlignTest 的 FramelessWindowHint 口径建窗。 */
    XTabBar* bar = XTabBar_create(
        NULL, (XWidgetFlags)(XWindowType_Window |
                             XWindowType_FramelessWindowHint));
    XImage* base;
    XImage* noBase;
    XImage* docMode;
    XImage* shapeSouth;
    XImage* colored;
    XRect tab1;
    XRect band;
    XRect topRow;
    uint32_t baselinePx;
    uint32_t topPx;

    XQ_REQUIRE(bar != NULL);
    XWidget_setGeometry((XWidget*)bar, 0, 0, 200, 40);
    XTabBar_addTab_2(bar, "aa");
    XTabBar_addTab_2(bar, "bb");
    XTabBar_setCurrentIndex(bar, 0);
    XWidget_show((XWidget*)bar);

    /* 缺省（drawBase=true、documentMode=false、shape=0 North）：抓图基线。 */
    base = XWidget_grab((const XWidget*)bar);
    XQ_REQUIRE(base != NULL);
    XQ_REQUIRE(XTabBar_tabRect(bar, 1, &tab1));

    /* P3-59：drawBase=false ⇒ 每页贴边基线不再画 ⇒ 基线行像素变化。
     * 基线画在页签矩形贴 pane 侧（North 族 = tab.y + 页签高）。 */
    XTabBar_setDrawBase(bar, false);
    XQ_CHECK(!XTabBar_drawBase(bar));
    noBase = XWidget_grab((const XWidget*)bar);
    XQ_REQUIRE(noBase != NULL);
    XRect_init(&band, tab1.x, XTabBar_tabHeight(bar), tab1.width, 1);
    XQ_CHECK_MSG(tb_countDiff(base, noBase, &band) > 0,
                 "drawBase=false 应改变页签基线行像素");

    /* P3-60：documentMode=true 同样不画基线（Qt 文档模式无基底横条）。 */
    XTabBar_setDrawBase(bar, true);
    XQ_CHECK(XTabBar_drawBase(bar));
    XTabBar_setDocumentMode(bar, true);
    XQ_CHECK(XTabBar_documentMode(bar));
    docMode = XWidget_grab((const XWidget*)bar);
    XQ_REQUIRE(docMode != NULL);
    XQ_CHECK_MSG(tb_countDiff(base, docMode, &band) > 0,
                 "documentMode=true 应改变页签基线行像素");
    XTabBar_setDocumentMode(bar, false);

    /* P0-34（自绘回退/基线部分）：形状翻转基线方位 —— North 族基线在
     * 页签底边、South 族在顶边 ⇒ 顶行像素变化（样式分支的 Rounded/
     * Triangular 八值绘制由 B2 在 XCommonStyle 交付）。 */
    baselinePx = XImage_pixel(base, tab1.x + 2, XTabBar_tabHeight(bar));
    topPx = XImage_pixel(base, tab1.x + 2, 0);
    XQ_CHECK_MSG(baselinePx != topPx,
                 "North 族基线在底边、顶行应为亮边：bottom=0x%08x top=0x%08x",
                 baselinePx, topPx);
    XTabBar_setShape(bar, 1); /* RoundedSouth */
    XQ_EQ_I64(XTabBar_shape(bar), 1);
    shapeSouth = XWidget_grab((const XWidget*)bar);
    XQ_REQUIRE(shapeSouth != NULL);
    XRect_init(&topRow, tab1.x, 0, tab1.width, 1);
    XQ_CHECK_MSG(tb_countDiff(base, shapeSouth, &topRow) > 0,
                 "South 族基线应移到顶边（P0-34 形状方位）");
    XTabBar_setShape(bar, 0);

    /* P3-58：普通态页签文本色被消费 ⇒ 第二页标签区像素变化
     * （形状/底不受影响，差异只能来自文本）。 */
    XTabBar_setTabTextColor(bar, 1, 0xFFFF0000u);
    XQ_EQ_I64(XTabBar_tabTextColor(bar, 1), 0xFFFF0000u);
    colored = XWidget_grab((const XWidget*)bar);
    XQ_REQUIRE(colored != NULL);
    XQ_CHECK(XTabBar_tabRect(bar, 1, &tab1));
    XQ_CHECK_MSG(tb_countDiff(base, colored, &tab1) > 0,
                 "setTabTextColor 应在普通态页签的绘制中被消费");

    XClassDelete((XClass*)colored);
    XClassDelete((XClass*)shapeSouth);
    XClassDelete((XClass*)docMode);
    XClassDelete((XClass*)noBase);
    XClassDelete((XClass*)base);
    XClassDelete((XClass*)bar);
}

/* ==================== ⑦ 逐页提示悬停弹出（P3-61） ==================== */

/** @brief 悬停页签弹 tabToolTip、换到无提示页/离开页签条收起。
 * @details XWidget 无 ToolTip 事件派发 ⇒ 提示挂在 MouseMove 的命中变化
 *          上（索引不变不重弹；离开经 LEAVE 收口）。收起走
 *          XToolTip_hideText 的 300ms 淡出，故用轮询等待到点断言。 */
XQ_TEST(XTabBar, tabToolTipHoverPopup)
{
    XTabBar* bar = XTabBar_create(NULL, 0);
    XRect r1;
    XEvent leave;

    XQ_REQUIRE(bar != NULL);
    XWidget_setGeometry((XWidget*)bar, 0, 0, 200, TAB_ALIGN_TAB_H);
    XTabBar_addTab_2(bar, "一");
    XTabBar_addTab_2(bar, "二");
    XTabBar_setTabToolTip_2(bar, 1, "第二页提示");
    XToolTip_hideText();
    XQ_CHECK(tb_waitToolTipHidden(2000));

    /* 悬停命中带提示的页签（tabRect 中心）⇒ 弹出且文本正确。 */
    XQ_REQUIRE(XTabBar_tabRect(bar, 1, &r1));
    tb_mouse((XWidget*)bar, XEVENT_TYPE_MOUSE_MOVE,
             r1.x + r1.width / 2, r1.y + r1.height / 2);
    XQ_CHECK_MSG(XToolTip_isVisible(), "悬停页签应弹出逐页提示");
    XQ_CHECK(tb_toolTipTextEquals("第二页提示"));

    /* 换到无提示页签 ⇒ 收起。 */
    tb_mouse((XWidget*)bar, XEVENT_TYPE_MOUSE_MOVE, 4, 4);
    XQ_CHECK(tb_waitToolTipHidden(2000));

    /* 离开页签条（LEAVE）⇒ 收起。 */
    tb_mouse((XWidget*)bar, XEVENT_TYPE_MOUSE_MOVE,
             r1.x + r1.width / 2, r1.y + r1.height / 2);
    XQ_CHECK(XToolTip_isVisible());
    XEvent_init(&leave, XEVENT_TYPE_LEAVE);
    XObject_event_base((XObject*)bar, &leave);
    XQ_CHECK(tb_waitToolTipHidden(2000));
    XQ_CHECK(XToolTip_text() == NULL);

    XClassDelete((XClass*)bar);
}

/* ==================== ⑧ protected 虚函数钩子（qtabbar.h:159-163） === */

/* 派生类元信息（测试本地类；XCLASS_DEFINE_EXTEND_END 提供 END_SIZE，
 * 供 XVTABLE_INIT_DEFAULT 使用）。 */
XCLASS_DEFINE_BEGING(XQTabBarProbe)
XCLASS_DEFINE_EXTEND_END(XQTabBarProbe, XTabBar)

typedef struct XQTabBarProbe
{
    XTabBar m_base;      /**< 基类；必须是第一个。 */
    int inserted;        /**< tabInserted 次数。 */
    int removed;         /**< tabRemoved 次数。 */
    int layoutChange;    /**< tabLayoutChange 次数。 */
    int sizeHintCalls;   /**< tabSizeHint 次数。 */
} XQTabBarProbe;

static XSize xqTabBarProbe_tabSizeHint(const XTabBar* self, int index)
{
    /* 回落父类槽（XClass_Parent 直调基类默认实现；调 *_base 会经自身
     * 虚表再入本探针槽 → 无限递归）。 */
    XSize s = XClass_Parent(XTabBar, EXTabBar_TabSizeHint,
                            XSize (*)(const XTabBar*, int))(self, index);
    ((XQTabBarProbe*)self)->sizeHintCalls++;
    s.width += 10; /* 子类定制：每签加宽 10。 */
    return s;
}

static XSize xqTabBarProbe_minimumTabSizeHint(const XTabBar* self, int index)
{
    XSize s = XClass_Parent(XTabBar, EXTabBar_MinimumTabSizeHint,
                            XSize (*)(const XTabBar*, int))(self, index);
    s.width += 10;
    return s;
}

static void xqTabBarProbe_tabInserted(XTabBar* self, int index)
{
    ((XQTabBarProbe*)self)->inserted++;
    XTabBar_tabInserted_base(self, index);
}

static void xqTabBarProbe_tabRemoved(XTabBar* self, int index)
{
    ((XQTabBarProbe*)self)->removed++;
    XTabBar_tabRemoved_base(self, index);
}

static void xqTabBarProbe_tabLayoutChange(XTabBar* self)
{
    ((XQTabBarProbe*)self)->layoutChange++;
    XTabBar_tabLayoutChange_base(self);
}

static XVtable* XQTabBarProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQTabBarProbe)
    XVTABLE_INHERIT_XCLASS(XTabBar);
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_TabSizeHint,
                             xqTabBarProbe_tabSizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_MinimumTabSizeHint,
                             xqTabBarProbe_minimumTabSizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_TabInserted,
                             xqTabBarProbe_tabInserted);
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_TabRemoved, xqTabBarProbe_tabRemoved);
    XVTABLE_OVERLOAD_DEFAULT(EXTabBar_TabLayoutChange,
                             xqTabBarProbe_tabLayoutChange);
    return XVTABLE_DEFAULT;
}

static void xqTabBarProbe_init(XQTabBarProbe* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XTabBar_init(&self->m_base, NULL, 0);
    XClassSetVtable(self, XQTabBarProbe);
}

XQ_TEST(XTabBar, protectedVirtualHooks)
{
    XQTabBarProbe probe;
    XSize s;
    int base0;

    xqTabBarProbe_init(&probe);
    XWidget_setGeometry((XWidget*)&probe, 0, 0, 400, TAB_ALIGN_TAB_H);

    /* 重载前的基类自然宽（同一字体/文本口径）。 */
    XTabBar_addTab_2((XTabBar*)&probe, "A");
    base0 = tb_naturalWidth((XTabBar*)&probe, "A");
    XQ_EQ_I64(probe.inserted, 1);
    /* 重载的 tabSizeHint 生效：基类宽 + 10。 */
    s = XTabBar_tabSizeHint_base((XTabBar*)&probe, 0);
    XQ_EQ_I64(s.width, base0 + 10);
    XQ_CHECK(probe.sizeHintCalls > 0);
    XQ_EQ_I64(XTabBar_minimumTabSizeHint_base((XTabBar*)&probe, 0).width,
              base0 + 10);

    /* tabInserted / tabRemoved / tabLayoutChange 回调。 */
    XTabBar_addTab_2((XTabBar*)&probe, "B");
    XQ_EQ_I64(probe.inserted, 2);
    {
        int before = probe.layoutChange;
        XTabBar_setTabText_2((XTabBar*)&probe, 0, "AA");
        XQ_CHECK(probe.layoutChange > before);
    }
    XTabBar_removeTab((XTabBar*)&probe, 0);
    XQ_EQ_I64(probe.removed, 1);

    XClassDeinit((XClass*)&probe);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABBAR_ON */
