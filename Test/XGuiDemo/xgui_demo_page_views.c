/******************************************************************************
 * @file       xgui_demo_page_views.c
 * @brief      XGuiWindowDemo 扩展页面：条目视图页（对标 Qt 6.8 QListView 族
 *             示例的分页组织）。
 * @details    实现 xgui_demo_pages.h 契约的 demo_page_views_build /
 *             demo_page_views_autotest 两个函数，页面自包含：
 *             - XListWidget：文本条目列表（苹果/香蕉/樱桃/榴莲），点击
 *               选中 → 页面状态 Label 显示当前条目 + status 回调上报；
 *             - XListView：挂接最小自定义 XAbstractItemModel（C 虚表
 *               子类，展示 3x3 网格数据），点击行 → 基类当前索引联动；
 *             - XTreeWidget：两级树（设备→网卡/串口、外设→键盘），点击
 *               节点选中、点击展开指示器切换展开/收起 → 反馈；
 *             - XTableWidget：2 列 3 行小表格，点击单元格 → 反馈当前
 *               行列（对标 QTableWidget::cellClicked）；
 *             - XHeaderView：独立小节。XHeaderView 为 XWidget 直接派生、
 *               仅承载段几何（正式渲染由 XTableView 统一完成，XGui.md
 *               §8.1 声明边界），本页以公开几何 API 自绘段带作可视化。
 *
 *             【绘制偏移补偿（页面侧规避，框架缺陷另立批次修复）】
 *             本库 paintEvent 契约：paintImage 为顶层后备存储（或离屏
 *             重定向目标），绘制前必须按 XWidget_paintOffset 平移到控
 *             件局部原点（XLabel/XFrame/XTableWidget/XWidget 缺省背景
 *             绘制均如此）。XListView（VXListView_paintEvent）与
 *             XTreeWidget（VXTreeWidget_paintEvent）未做该平移——控件
 *             位于窗口原点 (0,0) 时恰好正确，非零偏移时内容直绘到窗
 *             口原点（探针 /tmp 复现：错位白块 + 几何处空白）。XHeader
 *             View 无任何绘制代码（§8.1 几何承载，独立摆放恒不可见）。
 *             本页不改 Src/，以四个 C 虚表子类覆写 PaintEvent 规避：
 *             paintOffset 为零（离屏重定向/恰在原点）时直调基类绘制；
 *             非零时经 XWidget_render 离屏快照（快照闭环内偏移恒零、
 *             基类绘制正确）再按 paintOffset 回贴——对标 Qt 中"控件
 *             绘制恒在自身局部坐标系"的语义，不改变交互与几何。
 *             autotest 经 XObject_event_base 直发鼠标事件（与真实输入
 *             同路径），断言控件状态 getter，输出
 *             "XGuiAutoTest: [PASS]/[FAIL] 中文描述"，返回失败断言数。
 * @note       文件所有权：本文件为页面独立翻译单元，不改契约头、主
 *             文件与 Src/ 任何文件。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "XPrintf.h"
#include "XObject.h"
#include "XEvent.h"
#include "xgui_demo_pages.h"

#if XWIDGET_ON && XLABEL_ON
#include "XLabel.h"
#endif
#if XWIDGET_ON && XTABLEWIDGET_ON
#include "XAbstractItemModel.h"
#include "XAbstractItemView.h"
#include "XListView.h"
#include "XListWidget.h"
#include "XTreeView.h"
#include "XTreeWidget.h"
#include "XTableView.h"
#include "XTableWidget.h"
#include "XHeaderView.h"
#include "XPainter.h"
#include "XWidget_Protected.h" /* XWidget_paintImage/paintOffset 绘制闭环入口。 */
#endif

/* 条目视图族（模型/列表/树/表格/表头）统一由 XWIDGET_ON && XTABLEWIDGET_ON
 * 守卫（各头文件的模块开关口径）；XLabel 段落额外受 XLABEL_ON 守卫，
 * 对齐主文件 xgui_window_demo.c 的分段守卫风格。 */
#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 自定义条目模型（对标
 *                       QAbstractItemModel 子类化） ==================== */

XCLASS_DEFINE_BEGING(DemoViewsGridModel)
XCLASS_DEFINE_EXTEND_END(DemoViewsGridModel, XAbstractItemModel)

/**
 * @brief 最小自定义条目模型：C 虚表子类（m_base 必须是第一个成员）。
 *
 * @note  读 XAbstractItemModel.h 口径：本库模型为内存二维存储模型，
 *        行列数/数据通路是具体实现（rowCount/columnCount/data 经内部
 *        m_cells 承载），未暴露"行列数/数据"虚槽位；可覆写的虚槽为
 *        XClass 族（Deinit/Copy/Move）。故本子类以 XVTABLE_OVERLOAD
 *        覆写 Deinit 槽演示 C 虚表子类定式（XClass_Parent 静态调父
 *        类），3x3 数据经 setDimension/setData 通路注入（对标 Qt 中
 *        重写 rowCount/data 的自定义模型，落地方式随本库模型边界，
 *        信号 dataChanged/rowsInserted 等由基类真实发射）。
 */
typedef struct DemoViewsGridModel
{
    XAbstractItemModel m_base; /**< 基类成员；必须是第一个。 */
} DemoViewsGridModel;

/** @brief 析构：派生无自有资源，静态调父类析构释放基类单元格存储。 */
static void VDemoViewsGridModel_deinit(DemoViewsGridModel* self)
{
    if (!self) return;
    XClass_Parent(XAbstractItemModel, EXClass_Deinit,
                  void (*)(XAbstractItemModel*))(&self->m_base);
}

/** @brief 初始化 DemoViewsGridModel 类虚函数表。 */
XVtable* DemoViewsGridModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoViewsGridModel)
    XVTABLE_INHERIT_XCLASS(XAbstractItemModel);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VDemoViewsGridModel_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化自定义模型并填充 3x3 网格数据（A1..C3 + 行列表头）。 */
static void DemoViewsGridModel_init(DemoViewsGridModel* self)
{
    static const char* const kCells[3][3] = {
        { "A1", "B1", "C1" },
        { "A2", "B2", "C2" },
        { "A3", "B3", "C3" }
    };
    static const char* const kCols[3] = { "列0", "列1", "列2" };
    static const char* const kRows[3] = { "行0", "行1", "行2" };
    int r;
    int c;
    if (!self) return;
    XAbstractItemModel_init(&self->m_base);
    XClassSetVtable(self, DemoViewsGridModel);
    XAbstractItemModel_setDimension(&self->m_base, 3, 3);
    for (r = 0; r < 3; ++r) {
        XAbstractItemModel_setHeaderData_2(&self->m_base, r, 1, kRows[r]);
        for (c = 0; c < 3; ++c) {
            XAbstractItemModel_setData_2(&self->m_base, r, c, kCells[r][c]);
            if (r == 0)
                XAbstractItemModel_setHeaderData_2(&self->m_base, c, 0,
                                                   kCols[c]);
        }
    }
}

/* ==================== 绘制偏移补偿子类（框架缺陷的页面侧规避） ====================
 *
 * 基类 paintEvent 缺 XWidget_paintOffset 平移（见文件头说明），本节
 * 以 C 虚表子类覆写 PaintEvent 规避。判据与路径：
 *   - paintOffset == (0,0)（XWidget_render/grab/保留层等离屏重定向
 *     闭环，或控件恰在绘制目标原点）：基类以局部坐标直绘即为正确，
 *     直接分派基类（内层快照自递归亦经此短路）；
 *   - paintOffset 非零（窗口后备存储直绘）：经 XWidget_render 把本控
 *     件子树离屏快照（闭环内 paintOffset 恒零，基类绘制正确），再按
 *     paintOffset 回贴——等价于"先按局部坐标自绘再平移合成"。
 */

/** @brief paintOffset 安全绘制：basePaint 为最近基类的 PaintEvent 槽。 */
static void views_paintOffsetSafe(XWidget* self, XEvent* event,
                                  void (*basePaint)(XWidget*, XEvent*))
{
    XPoint offset;
    XImage* target;
    XPainter painter;
    XRect dst;
    if (!self || !basePaint) return;
    offset = XWidget_paintOffset(self);
    if (offset.x == 0 && offset.y == 0) {
        basePaint(self, event);
        return;
    }
    target = XWidget_paintImage(self);
    if (!target) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, target)) {
        XPainter_deinit(&painter);
        return;
    }
    XRect_init(&dst, offset.x, offset.y,
               XWidget_width(self), XWidget_height(self));
    /* 离屏快照回贴；快照失败（分配失败等）保持本控件空白，不回退
     * 基类直绘（避免内容错位污染窗口原点一带的邻区像素）。
     * paintTree 以 m_inPaintEvent 门禁拦截"绘制中再派发自身绘制"，
     * XWidget_render 的内层快照需受控放行一次：内层 paintOffset 恒
     * 零、走基类直调短路，不会二次进入快照路径（对标 Qt 中
     * QWidget::render 绕过重入保护的离屏语义）。 */
    {
        uint32_t inPaint = self->m_inPaintEvent;
        self->m_inPaintEvent = 0;
        /* 快照失败（分配失败等）仅本控件本帧缺画、等待下次重绘；不
         * 回退基类直绘，避免内容错位污染窗口原点一带的邻区像素。 */
        XWidget_render(self, &painter, &dst);
        self->m_inPaintEvent = inPaint;
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

XCLASS_DEFINE_BEGING(DemoViewsListWidget)
XCLASS_DEFINE_EXTEND_END(DemoViewsListWidget, XListWidget)

/** @brief XListWidget 子类：绘制偏移补偿（m_base 必须是第一个成员）。 */
typedef struct DemoViewsListWidget
{
    XListWidget m_base; /**< 基类成员；必须是第一个。 */
} DemoViewsListWidget;

/** @brief 绘制：最近基类（XListWidget 表未覆写，解析到 XListView 绘制）。 */
static void VDemoViewsListWidget_paintEvent(XWidget* self, XEvent* event)
{
    views_paintOffsetSafe(self, event,
                          XClass_Parent(XListWidget, EXWidget_PaintEvent,
                                        void (*)(XWidget*, XEvent*)));
}

/** @brief 初始化 DemoViewsListWidget 类虚函数表。 */
XVtable* DemoViewsListWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoViewsListWidget)
    XVTABLE_INHERIT_XCLASS(XListWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VDemoViewsListWidget_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化列表控件子类（几何/条目 API 沿用 XListWidget）。 */
static void DemoViewsListWidget_init(DemoViewsListWidget* self,
                                     XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XListWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, DemoViewsListWidget);
}

XCLASS_DEFINE_BEGING(DemoViewsListView)
XCLASS_DEFINE_EXTEND_END(DemoViewsListView, XListView)

/** @brief XListView 子类：绘制偏移补偿（m_base 必须是第一个成员）。 */
typedef struct DemoViewsListView
{
    XListView m_base; /**< 基类成员；必须是第一个。 */
} DemoViewsListView;

/** @brief 绘制：XListView 基类绘制（缺 paintOffset 平移的框架缺陷点）。 */
static void VDemoViewsListView_paintEvent(XWidget* self, XEvent* event)
{
    views_paintOffsetSafe(self, event,
                          XClass_Parent(XListView, EXWidget_PaintEvent,
                                        void (*)(XWidget*, XEvent*)));
}

/** @brief 初始化 DemoViewsListView 类虚函数表。 */
XVtable* DemoViewsListView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoViewsListView)
    XVTABLE_INHERIT_XCLASS(XListView);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VDemoViewsListView_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化列表视图子类（模型挂接等 API 沿用 XListView）。 */
static void DemoViewsListView_init(DemoViewsListView* self, XWidget* parent,
                                   XWidgetFlags flags)
{
    if (!self) return;
    XListView_init(&self->m_base, parent, flags);
    XClassSetVtable(self, DemoViewsListView);
}

XCLASS_DEFINE_BEGING(DemoViewsTreeWidget)
XCLASS_DEFINE_EXTEND_END(DemoViewsTreeWidget, XTreeWidget)

/** @brief XTreeWidget 子类：绘制偏移补偿（m_base 必须是第一个成员）。 */
typedef struct DemoViewsTreeWidget
{
    XTreeWidget m_base; /**< 基类成员；必须是第一个。 */
} DemoViewsTreeWidget;

/** @brief 绘制：XTreeWidget 基类绘制（缺 paintOffset 平移的框架缺陷点）。 */
static void VDemoViewsTreeWidget_paintEvent(XWidget* self, XEvent* event)
{
    views_paintOffsetSafe(self, event,
                          XClass_Parent(XTreeWidget, EXWidget_PaintEvent,
                                        void (*)(XWidget*, XEvent*)));
}

/** @brief 初始化 DemoViewsTreeWidget 类虚函数表。 */
XVtable* DemoViewsTreeWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoViewsTreeWidget)
    XVTABLE_INHERIT_XCLASS(XTreeWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VDemoViewsTreeWidget_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化树控件子类（条目/展开 API 沿用 XTreeWidget）。 */
static void DemoViewsTreeWidget_init(DemoViewsTreeWidget* self,
                                     XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XTreeWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, DemoViewsTreeWidget);
}

XCLASS_DEFINE_BEGING(DemoViewsHeaderView)
XCLASS_DEFINE_EXTEND_END(DemoViewsHeaderView, XHeaderView)

/** @brief XHeaderView 子类：段带可视化（m_base 必须是第一个成员）。 */
typedef struct DemoViewsHeaderView
{
    XHeaderView m_base; /**< 基类成员；必须是第一个。 */
} DemoViewsHeaderView;

/**
 * @brief 绘制：按公开几何 API（count/sectionSize/sectionPosition）自绘
 *        段带小节，正确的 paintOffset 平移写法（页面级可视化——XHeader
 *        View 本体无绘制代码，正式渲染由 XTableView 统一完成，XGui.md
 *        §8.1 声明边界；独立小节若不自绘则恒不可见）。
 */
static void VDemoViewsHeaderView_paintEvent(XWidget* self, XEvent* event)
{
    DemoViewsHeaderView* hv = (DemoViewsHeaderView*)self;
    XHeaderView* header = &hv->m_base;
    XImage* image;
    XPainter painter;
    XPoint offset;
    XRect band;
    int i;
    int w;
    int h;
    int count;
    (void)event;
    if (!hv) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    offset = XWidget_paintOffset(self);
    w = XWidget_width(self);
    h = XWidget_height(self);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    /* 段带底 + 各段（对标 QHeaderView 水平段带布局）。 */
    XRect_init(&band, 0, 0, w, h);
    XPainter_fillRect(&painter, &band, 0xFFE2E2E2u);
    count = XHeaderView_count(header);
    for (i = 0; i < count; ++i) {
        XRect section;
        int pos = XHeaderView_sectionPosition(header, i);
        int size = XHeaderView_sectionSize(header, i);
        char label[16];
        XRect_init(&section, pos + 1, 1, size - 2 > 0 ? size - 2 : 0,
                   h - 2 > 0 ? h - 2 : 0);
        if (section.width <= 0 || section.height <= 0) continue;
        XPainter_fillRect(&painter, &section, 0xFFFAFAFAu);
        XPainter_setPen(&painter, 0xFFB4B4B4u);
        XPainter_drawRect(&painter, &section);
        snprintf(label, sizeof(label), "段%d", i);
        /* drawText 第 4 参为墨水色（透明=无像素），须显式传不透明色。 */
        XPainter_drawText(&painter, pos + 6, h - 8, label, 0xFF202020u);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 初始化 DemoViewsHeaderView 类虚函数表。 */
XVtable* DemoViewsHeaderView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoViewsHeaderView)
    XVTABLE_INHERIT_XCLASS(XHeaderView);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VDemoViewsHeaderView_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化表头子类（段几何 API 沿用 XHeaderView）。 */
static void DemoViewsHeaderView_init(DemoViewsHeaderView* self,
                                     XWidget* parent, XWidgetFlags flags,
                                     int orientation)
{
    if (!self) return;
    XHeaderView_init(&self->m_base, parent, flags, orientation);
    XClassSetVtable(self, DemoViewsHeaderView);
}

/* ==================== 页面静态自持结构（demo 单实例） ==================== */

/**
 * @brief 条目视图页控件自持表。
 *
 * @note  契约口径：页面根控件堆创建（父子链级联析构，主文件不单独
 *        释放）；内部控件以 static 结构嵌入自持（对标主文件 DemoWin
 *        成员嵌入风格），build 时登记、autotest 时使用。列表/视图/
 *        树/表头使用绘制偏移补偿子类（交互与几何 API 经首个成员
 *        m_base 与基类共用）。自定义模型非 XWidget 不入控件树，以
 *        静态存储自持、视图借用（demo 单实例，once 守卫避免重复
 *        init 泄漏）。
 */
static struct
{
    XWidget* root;              /**< 页面根控件（堆；登记供 autotest 比对）。 */
    DemoPageStatusFn status;    /**< 主窗口状态栏回调（借用）。 */
    void* user;                 /**< 回调 user（主窗口指针，借用）。 */
    bool modelReady;            /**< 自定义模型已初始化守卫。 */
    DemoViewsGridModel model;   /**< 3x3 自定义模型（静态自持）。 */
#if XWIDGET_ON && XLABEL_ON
    XLabel capList;             /**< 小节标题：XListWidget。 */
    XLabel capView;             /**< 小节标题：XListView。 */
    XLabel capTree;             /**< 小节标题：XTreeWidget。 */
    XLabel capHeader;           /**< 小节标题：XHeaderView。 */
    XLabel capTable;            /**< 小节标题：XTableWidget。 */
    XLabel stateLabel;          /**< 页面状态 Label（最近交互反馈）。 */
#endif
    DemoViewsListWidget list;   /**< 条目列表（苹果/香蕉/樱桃/榴莲）。 */
    DemoViewsListView view;     /**< 模型视图（挂自定义 3x3 模型）。 */
    DemoViewsTreeWidget tree;   /**< 两级树（设备→网卡/串口…）。 */
    DemoViewsHeaderView header; /**< 独立表头（几何承载 + 段带可视化）。 */
    XTableWidget table;         /**< 2 列 3 行小表格。 */
} g_views;

/** @brief 交互反馈：写页面状态 Label 并经回调向主窗口状态栏上报。 */
static void views_setState(const char* text)
{
    if (g_views.status) g_views.status(g_views.user, text);
#if XWIDGET_ON && XLABEL_ON
    XLabel_setText_2(&g_views.stateLabel, text);
#else
    (void)text;
#endif
}

/* ==================== 信号槽（void f(XObject*, XVarList*) 定式） ==================== */

/** @brief 列表条目点击：状态行显示当前条目（对标 QListWidget
 *         itemClicked → currentItem 文本联动）。 */
static void views_listClickedSlot(XObject* receiver, XVarList* args)
{
    char buf[96];
    (void)receiver;
    XVarList_args_1(args, int, row);
    if (row < 0) return;
    snprintf(buf, sizeof(buf), "列表点击: 行 %d「%s」", row,
             XListWidget_item_2((XListWidget*)&g_views.list, row));
    views_setState(buf);
}

/** @brief 模型视图点击：显示命中单元格的模型数据（对标 QListView
 *         clicked(index) → model->data(index)）。 */
static void views_viewClickedSlot(XObject* receiver, XVarList* args)
{
    char buf[96];
    (void)receiver;
    XVarList_args_2(args, int, row, int, col);
    if (row < 0 || col < 0) return;
    snprintf(buf, sizeof(buf), "模型视图点击: (%d,%d)「%s」", row, col,
             XAbstractItemModel_data_2(&g_views.model.m_base, row, col));
    views_setState(buf);
}

/** @brief 树节点点击：状态行显示节点文本（对标 QTreeWidget
 *         itemClicked → 当前条目文本）。 */
static void views_treeClickedSlot(XObject* receiver, XVarList* args)
{
    char buf[96];
    XTreeWidgetItem* item;
    (void)receiver;
    XVarList_args_1(args, int, row);
    if (row < 0) return;
    item = XTreeWidget_topLevelItem((XTreeWidget*)&g_views.tree, row);
    snprintf(buf, sizeof(buf), "树点击: 顶层行 %d「%s」", row,
             item ? XTreeWidgetItem_text_2(item) : "");
    views_setState(buf);
}

/** @brief 表格单元格点击：状态行显示当前行列（对标 QTableWidget
 *         cellClicked(row,column)）。 */
static void views_tableClickedSlot(XObject* receiver, XVarList* args)
{
    char buf[96];
    (void)receiver;
    XVarList_args_2(args, int, row, int, col);
    if (row < 0 || col < 0) return;
    snprintf(buf, sizeof(buf), "表格点击: (%d,%d)「%s」", row, col,
             XTableWidget_text(&g_views.table, row, col));
    views_setState(buf);
}

/* ==================== 构建（手工几何风格，内容区 760x480） ==================== */

#if XWIDGET_ON && XLABEL_ON
/** @brief 小节标题标签统一装配（左对齐垂直居中，14px）。 */
static void views_makeCaption(XLabel* label, XWidget* parent,
                              int x, int y, int w, const char* text)
{
    XLabel_init(label, parent, 0);
    XLabel_setText_2(label, text);
    XLabel_setTextPixelSize(label, 14);
    XLabel_setAlignment(label, XAlignment_Left | XAlignment_VCenter);
    XWidget_setGeometry((XWidget*)label, x, y, w, 18);
    XWidget_show((XWidget*)label);
}
#endif

XWidget* demo_page_views_build(XWidget* parent,
                               DemoPageStatusFn status, void* user)
{
    XWidget* root;
    if (!parent) return NULL;
    g_views.status = status;
    g_views.user = user;

    /* 页面根控件：堆创建，父子链级联析构（契约口径）。 */
    root = XWidget_create(parent, 0);
    if (!root) return NULL;
    g_views.root = root;
    XWidget_setGeometry(root, 0, 0, 760, 480);

    /* 自定义模型：静态自持，once 守卫（非控件不入级联析构链）。 */
    if (!g_views.modelReady) {
        DemoViewsGridModel_init(&g_views.model);
        g_views.modelReady = true;
    }

#if XWIDGET_ON && XLABEL_ON
    /* 小节标题（XListWidget 顶部 y=6 一行）。 */
    views_makeCaption(&g_views.capList, root, 8, 6, 200,
                      "XListWidget（点击选择）");
    views_makeCaption(&g_views.capView, root, 224, 6, 196,
                      "XListView＋自定义模型");
    views_makeCaption(&g_views.capTree, root, 436, 6, 160, "XTreeWidget");
    views_makeCaption(&g_views.capHeader, root, 612, 6, 140,
                      "XHeaderView（几何）");
    views_makeCaption(&g_views.capTable, root, 8, 160, 380,
                      "XTableWidget（2 列 3 行）");
#endif

    /* ---- XListWidget：文本条目列表（对标 QListWidget addItem）。 ---- */
    DemoViewsListWidget_init(&g_views.list, root, 0);
    XListWidget_addItem_2((XListWidget*)&g_views.list, "苹果");
    XListWidget_addItem_2((XListWidget*)&g_views.list, "香蕉");
    XListWidget_addItem_2((XListWidget*)&g_views.list, "樱桃");
    XListWidget_addItem_2((XListWidget*)&g_views.list, "榴莲");
    XWidget_setGeometry((XWidget*)&g_views.list, 8, 28, 200, 118);
    XObject_connect_1((XObject*)&g_views.list,
                      (size_t)XListWidget_itemClicked_signal(NULL, 0),
                      (XObject*)&g_views.list, views_listClickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&g_views.list);

    /* ---- XListView：挂自定义 3x3 模型（对标 QListView setModel）。 ---- */
    DemoViewsListView_init(&g_views.view, root, 0);
    XAbstractItemView_setModel((XAbstractItemView*)&g_views.view,
                               &g_views.model.m_base);
    XWidget_setGeometry((XWidget*)&g_views.view, 224, 28, 196, 118);
    XObject_connect_1((XObject*)&g_views.view,
                      (size_t)XAbstractItemView_clicked_signal(NULL, 0, 0),
                      (XObject*)&g_views.view, views_viewClickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&g_views.view);

    /* ---- XTreeWidget：两级树（对标 QTreeWidget.addTopLevelItem）----
     * 顶层行 0「设备」← 网卡/串口；顶层行 1「外设」← 键盘。默认展开
     * （行带 5 行 x 24px），子树随顶层展开态显隐。 */
    DemoViewsTreeWidget_init(&g_views.tree, root, 0);
    {
        XTreeWidgetItem* device = XTreeWidgetItem_create_2("设备", NULL);
        XTreeWidgetItem* outer = XTreeWidgetItem_create_2("外设", NULL);
        XTreeWidgetItem_addChild(device, XTreeWidgetItem_create_2("网卡",
                                                                  device));
        XTreeWidgetItem_addChild(device, XTreeWidgetItem_create_2("串口",
                                                                  device));
        XTreeWidgetItem_addChild(outer, XTreeWidgetItem_create_2("键盘",
                                                                 outer));
        XTreeWidget_addTopLevelItem((XTreeWidget*)&g_views.tree, device);
        XTreeWidget_addTopLevelItem((XTreeWidget*)&g_views.tree, outer);
        /* 勾选指示器直观样例（四期④）：设备=选中、外设=部分选中。 */
        XTreeWidgetItem_setCheckState(device, XItemCheckState_Checked);
        XTreeWidgetItem_setCheckState(outer,
                                      XItemCheckState_PartiallyChecked);
        /* 双列展示（§8.0g17 四期④绘制消费）：列 1 备注 + 表头标签。 */
        XTreeWidget_setColumnCount((XTreeWidget*)&g_views.tree, 2);
        XTreeWidgetItem_setTextAt_2(device, 1, "在线");
        XTreeWidgetItem_setTextAt_2(outer, 1, "就绪");
        {
            static const char* headerLabels[2] = {"名称", "状态"};
            XTreeWidget_setHeaderLabels((XTreeWidget*)&g_views.tree,
                                        headerLabels, 2);
        }
    }
    XWidget_setGeometry((XWidget*)&g_views.tree, 436, 28, 160, 130);
    XObject_connect_1((XObject*)&g_views.tree,
                      (size_t)XTreeWidget_itemClicked_signal(NULL, 0),
                      (XObject*)&g_views.tree, views_treeClickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&g_views.tree);

    /* ---- XHeaderView：独立小节。本体仅段几何承载（无绘制代码，
     * §8.1 声明边界），子类按公开几何 API 自绘段带作可视化；正式
     * 表头渲染由 XTableView 统一完成。 ---- */
    DemoViewsHeaderView_init(&g_views.header, root, 0, 0 /* 0=水平列头 */);
    XHeaderView_setCount((XHeaderView*)&g_views.header, 2);
    XHeaderView_resizeSection((XHeaderView*)&g_views.header, 0, 90);
    XHeaderView_resizeSection((XHeaderView*)&g_views.header, 1, 46);
    XHeaderView_setSectionsClickable((XHeaderView*)&g_views.header, true);
    XWidget_setGeometry((XWidget*)&g_views.header, 612, 28, 140, 26);
    XWidget_show((XWidget*)&g_views.header);

    /* ---- XTableWidget：2 列 3 行小表格（对标 QTableWidget
     * setRowCount/setColumnCount/setText + 表头标签）。 ---- */
    XTableWidget_init(&g_views.table, root, 0);
    XTableWidget_setRowCount(&g_views.table, 3);
    XTableWidget_setColumnCount(&g_views.table, 2);
    {
        static const char* const kColHeaders[2] = { "名称", "数量" };
        XTableWidget_setHorizontalHeaderLabels(&g_views.table, kColHeaders,
                                               2);
    }
    XTableWidget_setText(&g_views.table, 0, 0, "苹果");
    XTableWidget_setText(&g_views.table, 0, 1, "3");
    XTableWidget_setText(&g_views.table, 1, 0, "香蕉");
    XTableWidget_setText(&g_views.table, 1, 1, "5");
    XTableWidget_setText(&g_views.table, 2, 0, "樱桃");
    XTableWidget_setText(&g_views.table, 2, 1, "8");
    XTableView_setColumnWidth((XTableView*)&g_views.table, 0, 130);
    XTableView_setColumnWidth((XTableView*)&g_views.table, 1, 130);
    XWidget_setGeometry((XWidget*)&g_views.table, 8, 182, 380, 124);
    XObject_connect_1((XObject*)&g_views.table,
                      (size_t)XTableWidget_cellClicked_signal(NULL),
                      (XObject*)&g_views.table, views_tableClickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&g_views.table);

#if XWIDGET_ON && XLABEL_ON
    /* ---- 页面状态 Label：显示最近交互（底部一行）。 ---- */
    XLabel_init(&g_views.stateLabel, root, 0);
    XLabel_setText_2(&g_views.stateLabel, "就绪（点击列表/树/表格查看联动）");
    XLabel_setTextPixelSize(&g_views.stateLabel, 14);
    XLabel_setAlignment(&g_views.stateLabel,
                        XAlignment_Left | XAlignment_VCenter);
    XWidget_setGeometry((XWidget*)&g_views.stateLabel, 8, 440, 744, 28);
    XWidget_show((XWidget*)&g_views.stateLabel);
#endif

    XWidget_show(root);
    return root;
}

/* ==================== 自动测试（事件注入 + getter 断言） ==================== */

/** @brief 注入一次鼠标左键按下（与真实输入同路径：XObject_event_base
 *         直发；pos 为控件本地坐标，见 XMouseEvent.h）。 */
static void views_injectClick(XWidget* target, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

int demo_page_views_autotest(XWidget* page)
{
    int failures = 0;

#define VIEWS_EXPECT(cond, what) \
    do { \
        if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
        else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } \
    } while (0)

    if (!page || page != g_views.root) return -1;

    /* 1. XListWidget：点击条目「樱桃」（行 2 槽位中心，行高 24）→
     *    当前行/当前条目/选中态 getter 断言。 */
    {
        XRect r = XListWidget_visualItemRect((XListWidget*)&g_views.list, 2);
        int sel[4];
        int selCount;
        views_injectClick((XWidget*)&g_views.list,
                          r.x + r.width / 2, r.y + r.height / 2);
        VIEWS_EXPECT(XListWidget_currentRow((XListWidget*)&g_views.list) == 2,
                     "列表点击行 2 当前行=2");
        VIEWS_EXPECT(XListWidget_currentItem((XListWidget*)&g_views.list) == 2,
                     "列表点击行 2 当前条目=2");
        VIEWS_EXPECT(strcmp(XListWidget_item_2((XListWidget*)&g_views.list, 2),
                            "樱桃") == 0,
                     "列表行 2 文本=樱桃");
        selCount = XListWidget_selectedItems((XListWidget*)&g_views.list,
                                             sel, 4);
        VIEWS_EXPECT(selCount == 1 && sel[0] == 2,
                     "列表选中集合={行 2}");
    }

    /* 2. 自定义模型 + XListView：模型数据读取断言（rowCount/data 返回
     *    构造值）+ 点击行 1 → 基类当前索引联动。 */
    VIEWS_EXPECT(XAbstractItemModel_rowCount(&g_views.model.m_base) == 3 &&
                 XAbstractItemModel_columnCount(&g_views.model.m_base) == 3,
                 "自定义模型维度 3x3");
    VIEWS_EXPECT(strcmp(XAbstractItemModel_data_2(&g_views.model.m_base,
                                                  1, 1), "B2") == 0,
                 "自定义模型 data(1,1)=B2");
    VIEWS_EXPECT(strcmp(XAbstractItemModel_headerData_2(&g_views.model.m_base,
                                                        2, 0), "列2") == 0,
                 "自定义模型列头(2)=列2");
    {
        XRect r = XListView_visualRect((XListView*)&g_views.view, 1);
        views_injectClick((XWidget*)&g_views.view,
                          r.x + r.width / 2, r.y + r.height / 2);
        VIEWS_EXPECT(XAbstractItemView_currentRow(
                         (XAbstractItemView*)&g_views.view) == 1 &&
                     XAbstractItemView_currentColumn(
                         (XAbstractItemView*)&g_views.view) == 0,
                     "模型视图点击行 1 当前索引=(1,0)");
    }

    /* 3. XTreeWidget：节点点击 → 当前节点断言；展开指示器点击（命中
     *    带 x<10px，绘制位于 x∈[2,6]）→ 展开态经 visualItemRect 行带
     *    几何断言（行 0 子树 3 行：展开时行 1 y=72，折叠后 y=24）。 */
    {
        /* P1 批次起 XTreeWidget 绘制 20px 表头带：行带整体下移 20，
         * 点击坐标按新几何取各行带中心（行 0 中心=20+12）。 */
        views_injectClick((XWidget*)&g_views.tree, 40, 32);
        VIEWS_EXPECT(XTreeWidget_currentItem((XTreeWidget*)&g_views.tree) == 0,
                     "树点击「设备」当前节点=行 0");
        views_injectClick((XWidget*)&g_views.tree, 40, 104);
        VIEWS_EXPECT(XTreeWidget_currentItem((XTreeWidget*)&g_views.tree) == 1,
                     "树点击「外设」当前节点=行 1");
        VIEWS_EXPECT(XTreeWidget_visualItemRect((XTreeWidget*)&g_views.tree,
                                                1).y == 72,
                     "树行 0 展开时行 1 行带 y=72");
        views_injectClick((XWidget*)&g_views.tree, 4, 32);
        VIEWS_EXPECT(XTreeWidget_visualItemRect((XTreeWidget*)&g_views.tree,
                                                1).y == 24,
                     "指示器点击折叠行 0（行 1 y=24）");
        views_injectClick((XWidget*)&g_views.tree, 4, 32);
        VIEWS_EXPECT(XTreeWidget_visualItemRect((XTreeWidget*)&g_views.tree,
                                                1).y == 72,
                     "指示器再点展开行 0（行 1 y=72）");
    }

    /* 4. XTableWidget：维度断言 + 点击单元格 (1,1)（坐标按表头尺寸/
     *    列宽/行高公开字段换算）→ 当前行列 getter 断言。 */
    VIEWS_EXPECT(XTableWidget_rowCount(&g_views.table) == 3 &&
                 XTableWidget_columnCount(&g_views.table) == 2,
                 "表格维度 3 行 2 列");
    {
        XTableWidget* table = &g_views.table;
        int rowH = XTableView_rowHeight((XTableView*)table);
        int x = table->m_headerWidth +
                XTableView_columnWidth((XTableView*)table, 0) +
                XTableView_columnWidth((XTableView*)table, 1) / 2;
        int y = table->m_headerHeight + rowH + rowH / 2;
        views_injectClick((XWidget*)table, x, y);
        VIEWS_EXPECT(XTableWidget_currentRow(table) == 1 &&
                     XTableWidget_currentColumn(table) == 1,
                     "表格点击 (1,1) 当前行列=(1,1)");
        VIEWS_EXPECT(strcmp(XTableWidget_text(table, 1, 1), "5") == 0,
                     "表格单元格 (1,1) 文本=5");
    }

    /* 5. XHeaderView：段几何状态断言（独立小节；sectionClicked 等鼠标
     *    信号为预留句柄无发射点，见 XHeaderView.h @note，故用状态
     *    getter；渲染可视化见 VDemoViewsHeaderView_paintEvent）。 */
    VIEWS_EXPECT(XHeaderView_count((XHeaderView*)&g_views.header) == 2,
                 "表头段数=2");
    VIEWS_EXPECT(XHeaderView_sectionSize((XHeaderView*)&g_views.header,
                                         0) == 90,
                 "表头段 0 尺寸=90");
    VIEWS_EXPECT(XHeaderView_sectionPosition((XHeaderView*)&g_views.header,
                                             1) == 90,
                 "表头段 1 起始位置=90");
    VIEWS_EXPECT(XHeaderView_logicalIndexAt((XHeaderView*)&g_views.header,
                                            100) == 1,
                 "表头位置 100 反查=段 1");

#undef VIEWS_EXPECT
    XPrintf("XGuiAutoTest: [条目视图页] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}

#else /* XWIDGET_ON && XTABLEWIDGET_ON */

/* 条目视图模块整体裁剪时的非空翻译单元哨兵。 */
typedef int xgui_demo_page_views_disabled_sentinel;

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
