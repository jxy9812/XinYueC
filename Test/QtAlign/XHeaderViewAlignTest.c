/**
 * @file       XHeaderViewAlignTest.c
 * @brief      XHeaderView × Qt 6.8 QHeaderView 对齐契约测试。
 * @details    对齐依据：
 *             - Qt 6.8.0 源码（cdn.jsdelivr.net/gh/qt/qtbase@v6.8.0/
 *               src/widgets/itemviews/qheaderview.cpp）：
 *               ① sectionSize 对隐藏段与越界段返回 0（:600-614）；
 *               ② sectionPosition 对无效段返回 -1，位置按视觉序累加
 *                 （:625-643）；
 *               ③ resizeSection 拒绝 <0 与 >1048575，>0 才钳制到
 *                 [min,max]，**size==0 原样存储**（:822-830）；
 *               ④ moveSection/swapSections 的参数是**视觉序号**，尺寸
 *                 按逻辑段存放、移动只改映射（:699-807）；
 *               ⑤ visualIndex/visualIndexAt/logicalIndex/logicalIndexAt
 *                 的映射语义（:551-591、:1024-1060）；
 *               ⑥ sectionsMoved = 发生移动后恒真（:1677-1681）。
 *             - 本机 Qt 6.8.0 实测（QT_QPA_PLATFORM=offscreen）：
 *               独立表头缺省 defaultSectionSize 水平 100 / 垂直 30、
 *               minimumSectionSize 水平 40 / 垂直 21、
 *               sectionsClickable=false、highlightSections=false、
 *               isFirstSectionMovable=true；QTableView/QTableWidget 自建
 *               表头 clickable/highlight=true（qtableview.cpp:577-589）；
 *               resizeSection(0,0) ⇒ sectionSize 0、position(1)=0；
 *               moveSection(0,2) 载荷 (0,0,2) 且尺寸随逻辑段不变；
 *               映射 [1,2,0] 上 swapSections(0,1) 载荷 (1,0,1)/(2,1,0)。
 *
 *             覆盖：缺省矩阵、段尺寸/位置与隐藏段口径、越界调整模式、
 *             视觉序映射与段移动、越界段尺寸、0 尺寸段、状态持久化、
 *             父子所有权。
 * @note       C99；表头全部释放，漏删即运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XHeaderView.h"
#include "XHeaderView_Protected.h" /* K-68/73 合成事件经保护入口注入
                                    * （forwardMove/setStretchViewportLength）。 */
#include "XEvent.h"            /* XMouseEvent / XPoint 构造（双击事件注入）。 */
#include "XEventType.h"        /* XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK。 */
#include "XObject_Protected.h" /* XObject_event_base（事件注入路径）。 */
#include "XVarList.h"          /* XVarList_args_1（信号载荷读取）。 */
#include "XTableView.h"        /* K-73b 裸视图视口长推送回归。 */
#include "XAbstractScrollArea.h" /* K-73b：视口宽事实源读取。 */

/* ==================== 信号探针 ==================== */

static int g_hvMovedHits;   /**< sectionMoved 命中次数。 */
static int g_hvMovedLogical; /**< sectionMoved 的逻辑段号载荷。 */
static int g_hvGeomHits;    /**< geometriesChanged 命中次数。 */
static int g_hvHandleDblHits;    /**< sectionHandleDoubleClicked 命中次数。 */
static int g_hvHandleDblSection; /**< sectionHandleDoubleClicked 的段号载荷。 */
static int g_hvSectionDblHits;   /**< sectionDoubleClicked 命中次数。 */

/** @brief sectionMoved(logical,oldVisual,newVisual) 槽。 */
static void xqHv_movedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_hvMovedHits;
    if (!args) return;
    XVarList_args_3(args, int, logical, int, oldVisual, int, newVisual);
    (void)oldVisual;
    (void)newVisual;
    g_hvMovedLogical = logical;
}

/** @brief geometriesChanged() 槽。 */
static void xqHv_geomSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_hvGeomHits;
}

/** @brief sectionHandleDoubleClicked(int) 槽（单值载荷）。 */
static void xqHv_handleDblSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_hvHandleDblHits;
    if (!args) return;
    XVarList_args_1(args, int, section);
    g_hvHandleDblSection = section;
}

/** @brief sectionDoubleClicked(int) 槽（只计命中次数）。 */
static void xqHv_sectionDblSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_hvSectionDblHits;
}

/** @brief 向表头直发一次鼠标事件（坐标=表头本地坐标；与真实事件同一虚槽）。 */
static void xqHv_mouse(XHeaderView* header, XEventType type, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!header) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)header, (XEvent*)&me);
}

/** @brief 经保护入口向表头转发一次移动事件（K-68 拖拽手势的移动段；
 *         宿主视图表头带转发走的就是这条 XHeaderView_forwardMove 路径）。 */
static void xqHv_move(XHeaderView* header, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!header) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE,
                     XMouseButton_LeftButton, 0, pos);
    XHeaderView_forwardMove(header, (XEvent*)&me);
}

/* ==================== 缺省值矩阵 ==================== */

/** @brief 缺省属性（Qt 6.8.0 实测逐项；固定近似项注明）。 */
XQ_TEST(XHeaderView, defaults)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XHeaderView* vh = XHeaderView_create(NULL, 0, 1);
    XQ_REQUIRE(hh != NULL && vh != NULL);

    XQ_EQ_I64(XHeaderView_orientation(hh), 0);
    XQ_EQ_I64(XHeaderView_orientation(vh), 1);
    XQ_EQ_I64(XHeaderView_count(hh), 0);
    /* Qt 6.8.0 实测：独立表头 defaultSectionSize 水平 100 / 垂直 30，
     * minimumSectionSize 水平 40 / 垂直 21（按方向取实测常量）。 */
    XQ_EQ_I64(XHeaderView_defaultSectionSize(hh), 100);
    XQ_EQ_I64(XHeaderView_minimumSectionSize(hh), 40);
    XQ_EQ_I64(XHeaderView_defaultSectionSize(vh), 30);
    XQ_EQ_I64(XHeaderView_minimumSectionSize(vh), 21);
    XQ_EQ_I64(XHeaderView_maximumSectionSize(hh), 1048575);
    /* resetDefaultSectionSize 按方向复位（Qt 6.8.0 实测 100/30）。 */
    XHeaderView_setDefaultSectionSize(hh, 33);
    XHeaderView_resetDefaultSectionSize(hh);
    XQ_EQ_I64(XHeaderView_defaultSectionSize(hh), 100);
    XHeaderView_setDefaultSectionSize(vh, 33);
    XHeaderView_resetDefaultSectionSize(vh);
    XQ_EQ_I64(XHeaderView_defaultSectionSize(vh), 30);
    /* Qt 6.8.0 实测：**独立** QHeaderView 的 highlightSections/
     * sectionsClickable 缺省均为 false（QHeaderViewPrivate::
     * setDefaultValues 的 highlightSelected/clickableSections 初值；
     * 表格自建表头的 true 由 XTableView_init 显式设置）。 */
    XQ_CHECK(!XHeaderView_highlightSections(hh));
    XQ_CHECK(!XHeaderView_sectionsClickable(hh));
    XQ_CHECK(!XHeaderView_sectionsMovable(hh));
    /* Qt 6.8.0 实测 isFirstSectionMovable 缺省 true（存储标志本身）。 */
    XQ_CHECK(XHeaderView_isFirstSectionMovable(hh));
    XQ_CHECK(!XHeaderView_stretchLastSection(hh));
    XQ_EQ_I64(XHeaderView_stretchSectionCount(hh), 0);
    XQ_EQ_I64(XHeaderView_sectionResizeMode(hh),
              XHeaderViewResizeMode_Interactive);
    XQ_EQ_I64(XHeaderView_defaultAlignment(hh), (int)XAlignment_Center);
    XQ_EQ_I64(XHeaderView_defaultAlignment(vh),
              (int)XAlignment_Left | (int)XAlignment_VCenter);
    XQ_EQ_I64(XHeaderView_sortIndicatorSection(hh), -1);
    XQ_CHECK(!XHeaderView_isSortIndicatorShown(hh));
    XQ_CHECK(!XHeaderView_sectionsMoved(hh));
    XQ_CHECK(!XHeaderView_sectionsHidden(hh));
    XQ_EQ_I64(XHeaderView_length(hh), 0);

    XClassDelete((XClass*)hh);
    XClassDelete((XClass*)vh);
}

/* ==================== 段尺寸/位置与隐藏段口径 ==================== */

/** @brief setCount/resizeSection 钳制；隐藏段 size=0、position=-1、跳过反查。 */
XQ_TEST(XHeaderView, sectionGeometryAndHidden)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);

    XHeaderView_setDefaultSectionSize(hh, 50);
    XHeaderView_setCount(hh, 3);
    XQ_EQ_I64(XHeaderView_count(hh), 3);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 50);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 2), 50);
    XQ_EQ_I64(XHeaderView_length(hh), 150);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 1), 50);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 2), 100);
    /* sectionViewportPosition：offset 恒 0 时与 sectionPosition 同值。 */
    XQ_EQ_I64(XHeaderView_sectionViewportPosition(hh, 2),
              XHeaderView_sectionPosition(hh, 2));

    /* resizeSection 先钳制到 [min,max]（Qt qheaderview.cpp:822-830；
     * 水平头缺省 min=40，Qt 6.8.0 实测 resizeSection(0,10)⇒40）。 */
    XHeaderView_resizeSection(hh, 0, 10);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 40); /* min=40 */
    XHeaderView_setMaximumSectionSize(hh, 100);
    XHeaderView_resizeSection(hh, 2, 500);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 2), 100);

    /* 越界段尺寸为 0（gap 156；Qt :600-614，实测 sectionSize(9)=0）。 */
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 9), 0);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, -1), 0);

    /* resizeSection(0) 原样存储（gap 155；Qt 实测 s=0、下一段位置=0）。 */
    XHeaderView_resizeSection(hh, 0, 0);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 0);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 1), 0);

    /* 隐藏段：size 0（长度按 0 计）、position -1、位置反查跳过。
     * 当前尺寸：段0=0（上面 resizeSection(0,0)）、段1=50、段2=100。 */
    XHeaderView_hideSection(hh, 1);
    XQ_CHECK(XHeaderView_isSectionHidden(hh, 1));
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 0);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 1), -1);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 2), 0); /* 0（段0）+ 0（隐藏段1） */
    XQ_EQ_I64(XHeaderView_length(hh), 100);           /* 0 + 0 + 100 */
    XQ_EQ_I64(XHeaderView_logicalIndexAt(hh, 15), 2); /* 跳过隐藏段 1 */
    XQ_EQ_I64(XHeaderView_logicalIndexAt(hh, 39), 2);

    /* 显示后尺寸恢复（隐藏期尺寸表项保留）。 */
    XHeaderView_showSection(hh, 1);
    XQ_CHECK(!XHeaderView_isSectionHidden(hh, 1));
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 50);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 2), 50);

    XClassDelete((XClass*)hh);
}

/* ==================== 越界调整模式（Qt 返回 Fixed） ==================== */

/** @brief sectionResizeMode 越界返回 Fixed（qheaderview.cpp:1230-1237）。 */
XQ_TEST(XHeaderView, resizeModeOutOfRange)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 3);

    /* 有效段无覆写 ⇒ 回落全局模式 Interactive。 */
    XQ_EQ_I64(XHeaderView_sectionResizeModeAt(hh, 0),
              XHeaderViewResizeMode_Interactive);
    XHeaderView_setSectionResizeModeAt(hh, 1, XHeaderViewResizeMode_Fixed);
    XQ_EQ_I64(XHeaderView_sectionResizeModeAt(hh, 1),
              XHeaderViewResizeMode_Fixed);
    XQ_EQ_I64(XHeaderView_sectionResizeModeAt(hh, 2),
              XHeaderViewResizeMode_Interactive);
    /* 越界与负段：Qt 返回 Fixed（实测 mode5=2、mode-1=2）。 */
    XQ_EQ_I64(XHeaderView_sectionResizeModeAt(hh, 5),
              XHeaderViewResizeMode_Fixed);
    XQ_EQ_I64(XHeaderView_sectionResizeModeAt(hh, -1),
              XHeaderViewResizeMode_Fixed);
    /* -1 哨兵清除覆写（本库扩展口径）。 */
    XHeaderView_setSectionResizeModeAt(hh, 1, -1);
    XQ_EQ_I64(XHeaderView_sectionResizeModeAt(hh, 1),
              XHeaderViewResizeMode_Interactive);

    XClassDelete((XClass*)hh);
}

/* ==================== 段移动与 sectionsMoved（视觉序映射） ==================== */

/** @brief moveSection/swapSections 走视觉序映射：尺寸随逻辑段、载荷为
 *         (逻辑号, 原视觉序, 新视觉序)。 */
XQ_TEST(XHeaderView, moveSectionsAndFlag)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setDefaultSectionSize(hh, 40);
    XHeaderView_setCount(hh, 3);
    XHeaderView_resizeSection(hh, 2, 80);
    XQ_CHECK(!XHeaderView_sectionsMoved(hh));

    g_hvMovedHits = 0;
    g_hvMovedLogical = -9;
    g_hvGeomHits = 0;
    XObject_connect((XObject*)hh, XSignal(XHeaderView_sectionMoved_signal),
                      (XObject*)hh, xqHv_movedSlot, XConnectionType_Direct);
    XObject_connect((XObject*)hh,
                      XSignal(XHeaderView_geometriesChanged_signal),
                      (XObject*)hh, xqHv_geomSlot, XConnectionType_Direct);

    /* 对齐 Qt 6.8（qheaderview.cpp:699-759）：参数是视觉序号；尺寸按
     * 逻辑段存放不变（实测 moveSection(0,2) 后 sectionSize 三值不变），
     * 载荷 (logical, from, to)=(0,0,2)，视觉序变为 [1,2,0]。 */
    XHeaderView_moveSection(hh, 0, 2);
    XQ_CHECK(XHeaderView_sectionsMoved(hh));
    XQ_EQ_I64(g_hvMovedHits, 1);
    XQ_EQ_I64(g_hvMovedLogical, 0); /* 载荷逻辑号 = 被移动段的逻辑号 */
    XQ_CHECK(g_hvGeomHits >= 1);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 40); /* 尺寸随逻辑段不变 */
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 40);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 2), 80);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 0), 2);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 1), 0);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 2), 1);
    XQ_EQ_I64(XHeaderView_logicalIndex(hh, 0), 1);
    XQ_EQ_I64(XHeaderView_logicalIndex(hh, 1), 2);
    XQ_EQ_I64(XHeaderView_logicalIndex(hh, 2), 0);
    /* 视觉序 [1,2,0] ⇒ 位置：段1=0、段2=40、段0=120。 */
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 1), 0);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 2), 40);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 0), 120);
    XQ_EQ_I64(XHeaderView_logicalIndexAt(hh, 50), 2);

    /* 对齐 Qt 6.8（qheaderview.cpp:761-807）：swapSections 也吃视觉序号；
     * 映射 [1,2,0] 上 swap(0,1) 实测载荷 (1,0,1)/(2,1,0)，交换后
     * 视觉序 [2,1,0]。 */
    XHeaderView_swapSections(hh, 0, 1);
    XQ_CHECK(XHeaderView_sectionsMoved(hh));
    XQ_EQ_I64(g_hvMovedHits, 3); /* 交换发两次（双向各一） */
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 0), 2);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 1), 1);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 2), 0);

    /* 段数增减后映射仍是 0..count-1 的排列（追加恒等/缩容保留相对序）。 */
    XHeaderView_setCount(hh, 4);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 3), 3);
    XQ_EQ_I64(XHeaderView_logicalIndex(hh, 3), 3);
    XHeaderView_setCount(hh, 2);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 0), 1);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 1), 0);

    XClassDelete((XClass*)hh);
}

/* ==================== 状态持久化：视觉序映射 ==================== */

#if XByteArray_ON
/** @brief saveState/restoreState 往返视觉序（v2 格式），并兼容 v1。 */
XQ_TEST(XHeaderView, stateRoundTripVisualOrder)
{
    XHeaderView* src = XHeaderView_create(NULL, 0, 0);
    XHeaderView* dst = XHeaderView_create(NULL, 0, 0);
    XByteArray* state;
    XQ_REQUIRE(src != NULL && dst != NULL);
    XHeaderView_setDefaultSectionSize(src, 40);
    XHeaderView_setCount(src, 3);
    XHeaderView_resizeSection(src, 2, 80);
    XHeaderView_moveSection(src, 0, 2); /* 视觉序 [1,2,0] */

    state = XHeaderView_saveState(src);
    XQ_REQUIRE(state != NULL);
    /* 魔数/版本为 v2（追加视觉序映射）。 */
    XQ_CHECK(strncmp((const char*)XByteArray_constData(state), "XHV002", 6) == 0);
    XQ_CHECK(XHeaderView_restoreState(dst, state));
    XQ_EQ_I64(XHeaderView_count(dst), 3);
    XQ_EQ_I64(XHeaderView_sectionSize(dst, 2), 80);
    XQ_EQ_I64(XHeaderView_visualIndex(dst, 0), 2);
    XQ_EQ_I64(XHeaderView_visualIndex(dst, 1), 0);
    XQ_EQ_I64(XHeaderView_visualIndex(dst, 2), 1);
    XQ_CHECK(XHeaderView_sectionsMoved(dst));
    XClassDelete((XClass*)state);
    XClassDelete((XClass*)src);
    XClassDelete((XClass*)dst);
}
#endif /* XByteArray_ON */

/* ==================== 父子所有权 ==================== */

/** @brief 表头随父控件析构。 */
XQ_TEST(XHeaderView, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XHeaderView* hh;
    XQ_REQUIRE(host != NULL);

    hh = XHeaderView_create(host, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 2);
    XQ_CHECK(XWidget_parentWidget((XWidget*)hh) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)hh));
    /* 表头随 host 析构，漏删即判 [LEAK]。 */
    XClassDelete((XClass*)host);
}

/* ==================== stretchSectionCount 口径（P0-77） ==================== */

/** @brief stretchSectionCount 统计「生效模式为 Stretch 的段数」，不含
 *         stretchLastSection。
 *
 *  Qt（qheaderview.cpp:1291-1295）直接返回 `d->stretchSections`：
 *  全局 Stretch 时为 count()（:1178），单段设置按新旧生效模式增减
 *  （:1209/:1213），段数变化时跟随（:1899/:2304）。此前本库返回
 *  `stretchLastSection ? 1 : 0`（把 Qt 口径写反）。 */
XQ_TEST(XHeaderView, stretchSectionCount)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);

    /* 无段：0（无论 stretchLastSection）。 */
    XQ_EQ_I64(XHeaderView_stretchSectionCount(hh), 0);
    XHeaderView_setStretchLastSection(hh, true);
    XQ_EQ_I64(XHeaderView_stretchSectionCount(hh), 0);
    XHeaderView_setStretchLastSection(hh, false);

    XHeaderView_setCount(hh, 3);
    XQ_EQ_I64(XHeaderView_stretchSectionCount(hh), 0); /* Interactive */

    /* 全局 Stretch ⇒ count()。 */
    XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Stretch);
    XQ_EQ_I64(XHeaderView_stretchSectionCount(hh), 3);

    /* 逐段改回 Interactive ⇒ 减一（生效模式由 Stretch 变非 Stretch）。 */
    XHeaderView_setSectionResizeModeAt(hh, 1,
                                       XHeaderViewResizeMode_Interactive);
    XQ_EQ_I64(XHeaderView_stretchSectionCount(hh), 2);

    /* stretchLastSection 不计入 Stretch 段数（Qt 只返回 stretchSections）。 */
    XHeaderView_setStretchLastSection(hh, true);
    XQ_EQ_I64(XHeaderView_stretchSectionCount(hh), 2);
    XHeaderView_setStretchLastSection(hh, false);

    /* 段数变化跟随（新增段跟随全局 Stretch ⇒ 2 + 2）。 */
    XHeaderView_setCount(hh, 5);
    XQ_EQ_I64(XHeaderView_stretchSectionCount(hh), 4);

    /* 全局回 Interactive ⇒ 单段覆写被清（setSectionResizeMode 的覆写
     * 语义）⇒ 0。 */
    XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Interactive);
    XQ_EQ_I64(XHeaderView_stretchSectionCount(hh), 0);

    XClassDelete((XClass*)hh);
}

/* ============ sectionHandleDoubleClicked 发射点（Qt 6.8 口径） ============ */

/** @brief 双击 Interactive 模式的分隔线手柄 ⇒ 发射
 *         sectionHandleDoubleClicked(handle)（qheaderview.cpp:2745-2747）；
 *         双击段内（非手柄）⇒ 发射 sectionDoubleClicked（:2759）。
 *
 *  本用例锁的是「句柄实现真的发射」：VXHeaderView_mouseDoubleClickEvent
 *  的手柄分支早已命中手柄并调用
 *  XHeaderView_sectionHandleDoubleClicked_signal，但该 _signal 此前只
 *  (void)section 空转（无 xhv_emitInt）⇒ 连接方一直接不到。补齐后
 *  xhv_emitInt 走 XObject_emitSignal，载荷为手柄所在段的逻辑号。 */
XQ_TEST(XHeaderView, sectionHandleDoubleClickedEmit)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);

    XHeaderView_setDefaultSectionSize(hh, 50);
    XHeaderView_setCount(hh, 3);
    /* 三段各 50：段0 尾分隔线在 pos-1=49（命中热区 [46,52]）。 */

    g_hvHandleDblHits = 0;
    g_hvHandleDblSection = -9;
    g_hvSectionDblHits = 0;
    XObject_connect((XObject*)hh,
                      XSignal(XHeaderView_sectionHandleDoubleClicked_signal),
                      (XObject*)hh, xqHv_handleDblSlot, XConnectionType_Direct);
    XObject_connect((XObject*)hh,
                      XSignal(XHeaderView_sectionDoubleClicked_signal),
                      (XObject*)hh, xqHv_sectionDblSlot, XConnectionType_Direct);

    /* ① 手柄双击（x=49，段0 尾分隔线）：Qt 发 sectionHandleDoubleClicked(0)、
     *    不发 sectionDoubleClicked。独立表头缺省 sectionsClickable=false，
     *    Qt 双击口径不受 clickable 门控（qheaderview.cpp:2741-2760）。 */
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK, 49, 0);
    XQ_EQ_I64(g_hvHandleDblHits, 1);
    XQ_EQ_I64(g_hvHandleDblSection, 0);
    XQ_EQ_I64(g_hvSectionDblHits, 0);

    /* ② 段内双击（x=20，远离任何分隔线）：Qt 走 else 分支发
     *    sectionDoubleClicked(logicalIndexAt)，不发手柄双击。 */
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK, 20, 0);
    XQ_EQ_I64(g_hvHandleDblHits, 1);    /* 不追加 */
    XQ_EQ_I64(g_hvSectionDblHits, 1);

    /* ③ 手柄非 Interactive（Fixed）：Qt :2746 条件不满足 ⇒ 回落
     *    sectionDoubleClicked（本库 VXHeaderView_mouseDoubleClickEvent
     *    :506-508 补判模式，同口径）。段0 设为 Fixed 后 x=49 不再算手柄语义。 */
    XHeaderView_setSectionResizeModeAt(hh, 0, XHeaderViewResizeMode_Fixed);
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK, 49, 0);
    XQ_EQ_I64(g_hvHandleDblHits, 1);    /* 仍不追加 */
    XQ_EQ_I64(g_hvSectionDblHits, 2);   /* 回落段双击 +1 */

    XClassDelete((XClass*)hh);
}

/* ==================== K-68 段拖拽换位（2026-11-14） ==================== */

/* 对齐依据（Qt 6.8.3 qheaderview.cpp，下机实测 6.8.3 源码）：
 *  - :2518-2528 mousePressEvent 建 MoveSection 手势——门控
 *    sectionsMovable && 非隐藏段 &&（firstSectionMovable || 非首段）；
 *  - :2580-2617 mouseMoveEvent 过 startDragDistance 后按「目标视觉段
 *    中点界」机算 target（visual==0 且逻辑 0 居首且首段不可动不成交）；
 *  - :2673-2682 mouseReleaseEvent 提交 moveSection(visualIndex(section),
 *    visualIndex(target))；已拖动 ⇒ 不走单击链（不发 sectionClicked、
 *    不翻指示器），未拖动 ⇒ 落单击链；
 *  - :699-759 程序化 moveSection **无开关门控**。 */

/** @brief sectionsMovable 拖拽换位与门控口径。 */
XQ_TEST(XHeaderView, sectionsMovableDragMove)
{
    XHeaderView* hh;

    /* ① movable=true（firstSectionMovable 缺省 true）：拖首段 → 次段。 */
    hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 3);            /* 三段各 100（水平缺省） */
    XHeaderView_setSectionsMovable(hh, true);
    XQ_CHECK(XHeaderView_isFirstSectionMovable(hh));
    g_hvMovedHits = 0;
    g_hvMovedLogical = -9;
    XObject_connect((XObject*)hh, XSignal(XHeaderView_sectionMoved_signal),
                    (XObject*)hh, xqHv_movedSlot, XConnectionType_Direct);
    /* 按下段 0（x=50 非手柄）→ 拖过阈值 → 段 1 中点右（x=180）→ 松手：
     * target=段 1 ⇒ 提交 moveSection(0,1)，载荷 (0,0,1)。 */
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 50, 0);
    xqHv_move(hh, 180, 0);
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 180, 0);
    XQ_EQ_I64(g_hvMovedHits, 1);
    XQ_EQ_I64(g_hvMovedLogical, 0);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 0), 1);
    XQ_EQ_I64(XHeaderView_logicalIndex(hh, 1), 0);
    XObject_disconnect((XObject*)hh,
                       XSignal(XHeaderView_sectionMoved_signal),
                       (XObject*)hh, xqHv_movedSlot);
    XClassDelete((XClass*)hh);

    /* ② movable=false（缺省）：同序列不动。 */
    hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 3);
    XQ_CHECK(!XHeaderView_sectionsMovable(hh));
    g_hvMovedHits = 0;
    XObject_connect((XObject*)hh, XSignal(XHeaderView_sectionMoved_signal),
                    (XObject*)hh, xqHv_movedSlot, XConnectionType_Direct);
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 50, 0);
    xqHv_move(hh, 180, 0);
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 180, 0);
    XQ_EQ_I64(g_hvMovedHits, 0);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 0), 0);
    /* 程序化 moveSection 不受开关门控（qheaderview.cpp:699-759 无闸）。 */
    XHeaderView_moveSection(hh, 0, 2);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 0), 2);
    XObject_disconnect((XObject*)hh,
                       XSignal(XHeaderView_sectionMoved_signal),
                       (XObject*)hh, xqHv_movedSlot);
    XClassDelete((XClass*)hh);

    /* ③ firstSectionMovable=false：首段不成手势；次段照常可拖。 */
    hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 3);
    XHeaderView_setSectionsMovable(hh, true);
    XHeaderView_setFirstSectionMovable(hh, false);
    g_hvMovedHits = 0;
    XObject_connect((XObject*)hh, XSignal(XHeaderView_sectionMoved_signal),
                    (XObject*)hh, xqHv_movedSlot, XConnectionType_Direct);
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 50, 0);
    xqHv_move(hh, 180, 0);
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 180, 0);
    XQ_EQ_I64(g_hvMovedHits, 0);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 0), 0);
    /* 拖次段（x=150 → 280）：target=段 2 ⇒ 提交 moveSection(1,2)。 */
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 150, 0);
    xqHv_move(hh, 280, 0);
    xqHv_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 280, 0);
    XQ_EQ_I64(g_hvMovedHits, 1);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 1), 2);
    XObject_disconnect((XObject*)hh,
                       XSignal(XHeaderView_sectionMoved_signal),
                       (XObject*)hh, xqHv_movedSlot);
    XClassDelete((XClass*)hh);
}

/* ==================== K-73 stretchLastSection 引擎（2026-11-14） ==================== */

/* 对齐依据：Qt 6.8.3 qheaderview.cpp:1459-1467（setter 即 setNewLastSection
 * + resizeSections / restoreSizeOnPrevLastSection）、:3468-3516（末段快照
 * 与复原）、:3619-3623（末段 = qMax(stretchSectionLength, lastSectionSize)）。
 * 本机 Qt 6.8.3 offscreen 探针（/tmp/qsprobe，2026-11-14）逐值核对：
 * Interactive 100/150/200/缺省 + stretchLast ⇒ 638 视口末段 188、798 ⇒ 348、
 * 498 ⇒ 夹到快照 100；程序化末段 222 后改宽 col0 ⇒ 回吃余量 238；
 * hide 末列 ⇒ 次末列立即吃满余量。全局 Stretch ⇒ 末段随池等分不特判
 * （引擎跳过，池分配在视图侧 xtw_stretchColumnsToViewport）。 */
XQ_TEST(XHeaderView, stretchLastSectionEngine)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 4);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 150);
    XHeaderView_setSectionSize(hh, 2, 200);
    /* 段 3 维持缺省 100。视口长未推送 ⇒ 引擎不跑（setter 只立快照）。 */
    XHeaderView_setStretchLastSection(hh, true);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 100);

    /* 推送可用长 638 ⇒ 末段 = 638 − 450 = 188。 */
    XHeaderView_setStretchViewportLength(hh, 638);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 188);
    /* 798 ⇒ 348；498 ⇒ qMax(48, 快照 100) = 100（快照托底）。 */
    XHeaderView_setStretchViewportLength(hh, 798);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 348);
    XHeaderView_setStretchViewportLength(hh, 498);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 100);
    /* 程序化把末段抬到 222 ⇒ 快照 222；再推 638 ⇒ qMax(188, 222) = 222。 */
    XHeaderView_resizeSection(hh, 3, 222);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 222);
    XHeaderView_setStretchViewportLength(hh, 638);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 222);
    /* 改宽 col0 到 50 ⇒ 余量 238 ≥ 快照 ⇒ 末段回吃余量 238。 */
    XHeaderView_resizeSection(hh, 0, 50);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 238);
    /* 关闭 ⇒ 旧末段按快照复原 222（restoreSizeOnPrevLastSection）。 */
    XHeaderView_setStretchLastSection(hh, false);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 222);

    /* 隐藏末段 ⇒ 末段身份切换：旧末段按快照复原、新末段（段 2）吃满
     * 余量 638 − 50 − 150 = 438（对标探针 D）。隐藏段尺寸表项仍保留
     * （sectionSize 对隐藏段返回 0，见头文件 @note，故不直接断言段 3）。 */
    XHeaderView_setStretchLastSection(hh, true);
    XHeaderView_setStretchViewportLength(hh, 638);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 238);
    XHeaderView_hideSection(hh, 3);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 2), 438);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 0);
    /* 再显示 ⇒ 末段回到段 3：段 2 复原到成为末段前的快照 200（证明
     * 身份切换时的双向复原都发生了）、段 3 回吃余量 238。 */
    XHeaderView_showSection(hh, 3);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 2), 200);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 238);

    /* 全局 Stretch：末段进拉伸池随其它段均分、引擎不特判（对标探针 A；
     * 表头级 Stretch 以当前可见总长均分：可见 4 段共 638 ⇒ 638/4 = 159
     * 余 2，余数按视觉序前置分摊给首两段（Qt qheaderview.cpp:3624-3626）
     * ⇒ 视觉序 0/1 各 160、2/3 各 159。旧口径把余数整份丢弃、断言「各
     * 159」，§B3/2026-11-15 池口径重写后按 Qt 原文改锁（不改框架）。 */
    XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Stretch);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 160);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 2), 159);
    XHeaderView_setStretchViewportLength(hh, 638);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 2), 159);
    XClassDelete((XClass*)hh);
}

/* ==================== K-73 per-section Stretch 池口径（复核 P1 承接） ==================== */

/* 对齐依据（Qt 6.8.3 qheaderview.cpp:3553-3640 延迟重排路径 + 本机探针
 * P1-1/2/4/5）：末段**强制**视为 Stretch 进池（:3555-3560），其它生效模式
 * 为 Stretch 的可见段同池均分（P1-2：池 {1,3} 各 194）；余数按视觉序逐池
 * 成员 +1（P1-5：池 {1,3} 余量 385 ⇒ 193/192）；余量 <= 0 ⇒ 整体跳过、
 * 池成员保现宽（:3591 `lengthToStretch > 0` 门，P1-4b：视口 298 末段保
 * 448）。全局 Stretch（m_resizeMode==Stretch）的池分配在视图侧分发器，
 * 引擎跳过（stretchLastSectionEngine 末块已锁）。 */
XQ_TEST(XHeaderView, stretchLastSectionPerSectionPool)
{
    XHeaderView* hh;

    /* ① 末段 per-section Stretch ⇒ 池 {末段} 吃满余量（探针 P1-1：
     *    638 − 370 = 268）。 */
    hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 4);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 150);
    XHeaderView_setSectionSize(hh, 2, 120);
    XHeaderView_setSectionResizeModeAt(hh, 3,
                                       XHeaderViewResizeMode_Stretch);
    XHeaderView_setStretchLastSection(hh, true);
    XHeaderView_setStretchViewportLength(hh, 638);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 268);
    /* ② 余量 <= 0（298 − 370 < 0）⇒ 整体跳过、末段保现宽（探针 P1-4b）。
     *    旧实现此处会托底到 qMax(最小段宽, 快照)——与 Qt 不符，已修。 */
    XHeaderView_setStretchViewportLength(hh, 298);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 268);
    XClassDelete((XClass*)hh);

    /* ③ 中段 per-section Stretch + Interactive 末段 ⇒ 池 {1,3} 均分
     *    （探针 P1-2：余量 638 − 250 = 388 ⇒ 各 194）。 */
    hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 4);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 2, 150);
    XHeaderView_setSectionResizeModeAt(hh, 1,
                                       XHeaderViewResizeMode_Stretch);
    XHeaderView_setStretchLastSection(hh, true);
    XHeaderView_setStretchViewportLength(hh, 638);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 194);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 194);

    /* ④ 余数按视觉序逐池成员 +1（探针 P1-5：余量 635 − 250 = 385 ⇒
     *    192 余 1 ⇒ 池前者 +1：col1=193、col3=192）。 */
    XHeaderView_setStretchViewportLength(hh, 635);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 193);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 192);
    XClassDelete((XClass*)hh);
}

/* ==================== K-73b 裸 XTableView 视口长推送（2026-11-14） ==================== */

/* 对齐依据：Qt 6.8.3 QTableView::updateGeometries 以**视口几何**为事实源
 * 摆水平表头（`d->horizontalHeader->setGeometry(vg.left(), horizontalTop,
 * vg.width(), height)`，vg=视口几何，qtableview.cpp:2301,2309）；触发点
 * 是 QAbstractItemView::resizeEvent（qabstractitemview.cpp:2588-2592）。
 * 本库裸 XTableView（无 XTableWidget 宿主）此前无人推视口长 ⇒ 引擎因
 * <0 永不跑；现在视图自己的 resizeEvent 与视口事件覆写（滚动条显隐等
 * 无 resize 的视口几何变化）走 syncViewportLength 同值闸推送。列宽路径
 * （setColumnWidth）**没有**推送点：内容宽变化只翻横条、不改视口宽
 * （XTableView_setColumnWidth 注），其引擎触发由表头侧 resizeSection
 * 尾钩承载——下方「程序化抬末段」步断言的正是该尾钩+快照托底口径。 */
XQ_TEST(XHeaderView, bareTableViewViewportPush)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XHeaderView* hh;
    int avail;
    XQ_REQUIRE(tv != NULL);
    XTableView_setColumnWidth(tv, 0, 100);
    XTableView_setColumnWidth(tv, 1, 150);
    XTableView_setColumnWidth(tv, 2, 200);
    hh = XTableView_horizontalHeader(tv);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setStretchLastSection(hh, true);
    /* 视口长未推送 ⇒ 引擎不跑（末段维持 setColumnWidth 的 200）。 */
    XQ_EQ_I64(XTableView_columnWidth(tv, 2), 200);

    /* resize ⇒ resizeEvent 先落父链几何（视口矩形/滚动条显隐收敛），
     * 再推视口宽 ⇒ 末段 = 视口宽 − 其余两列合计 250。 */
    XWidget_resize((XWidget*)tv, 638, 120);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tv));
    XQ_CHECK(avail > 250 + 200); /* 场景前提：余量足、不触快照托底。 */
    XQ_EQ_I64(XTableView_columnWidth(tv, 2), avail - 250);

    /* 再加宽 ⇒ resizeEvent 路径重推（快照 200 不抬下限，余量回吃）。 */
    XWidget_resize((XWidget*)tv, 800, 120);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tv));
    XQ_EQ_I64(XTableView_columnWidth(tv, 2), avail - 250);

    /* 列宽变化路径：末段程序化抬到 700（> 当前余量 avail−250 ≈ 546）
     * ⇒ resizeSection 尾钩引擎按快照托底 ⇒ 700（setColumnWidth 的
     * sync 推送在此为同值闸 no-op，不得干扰快照口径）。 */
    XTableView_setColumnWidth(tv, 2, 700);
    XQ_EQ_I64(XTableView_columnWidth(tv, 2), 700);
    /* 再加宽窗口 ⇒ 余量反超快照 ⇒ 末段回吃余量（resizeEvent 重推口径：
     * target = qMax(余量, 快照 700)，余量 = avail − 250 > 700）。 */
    XWidget_resize((XWidget*)tv, 1000, 120);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tv));
    XQ_CHECK(avail - 250 > 700);
    XQ_EQ_I64(XTableView_columnWidth(tv, 2), avail - 250);
    XClassDelete((XClass*)tv);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
