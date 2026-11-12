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

/* ==================== 信号探针 ==================== */

static int g_hvMovedHits;   /**< sectionMoved 命中次数。 */
static int g_hvMovedLogical; /**< sectionMoved 的逻辑段号载荷。 */
static int g_hvGeomHits;    /**< geometriesChanged 命中次数。 */

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

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
