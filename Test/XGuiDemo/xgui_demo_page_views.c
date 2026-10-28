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
 *             【页内摆位布局容器化（2026-10-17）】页内排布原为 build/
 *             adapt 手写 setGeometry 坐标，现改为根控件挂盒式布局器自管
 *             （根纵向 = 小节标题行 + 视图行 + 小表格段 + 底部弹性 +
 *             状态行贴底，行/列嵌套对标原坐标间距）。主文件挂到本页根
 *             的大表格（m_tableWidget，自选项卡页打散归位）经根控件
 *             CHILD_ADDED 事件收编进小表格行（贴其右侧、宽随根，列宽
 *             随可视宽定版），对应主文件 demo_layout_content 手写摆位段
 *             删除；demo_page_views_adapt 退化为空实现（契约调用点保留）。
 *
 *             【绘制偏移补偿（夜三起默认直绘，旧快照路径降为回退开关）】
 *             本库 paintEvent 契约：paintImage 为顶层后备存储（或离屏
 *             重定向目标），绘制前必须按 XWidget_paintOffset 平移到控
 *             件局部原点（XLabel/XFrame/XTableWidget/XWidget 缺省背景
 *             绘制均如此）。历史上 XListView/XTreeWidget 未做该平移，
 *             本页曾以 C 虚表子类覆写 PaintEvent、非零偏移时经
 *             XWidget_render 离屏快照规避。该框架缺陷已在 Src/ 修复
 *             （XListView.c:339 / XTreeView.c:1065 / XTreeWidget.c:1818
 *             / XTableView.c:1083 均按 paintOffset 平移后绘制），子类
 *             现默认直接分派基类（基类自平移，GPU/SW 与其他页同路径，
 *             page5 GPU 增量 168.9fps→直绘基线的放大器根除）；旧快照
 *             路径仅当 XGUI_DEMO_VIEWS_SNAPSHOT=1 才启用（逐位回退，
 *             供 A/B 对照——GPU 直通下它会经 painterGpuSessionAcquire
 *             触发每控件 FBO 现绘+readback+重上传，见
 *             views_paintOffsetSafe 注）。XHeaderView 无任何绘制代码
 *             （§8.1 几何承载）；本页以子类按公开几何 API 自绘段带
 *             （自带平移，不受本开关影响）。
 *             autotest 经 XObject_event_base 直发鼠标事件（与真实输入
 *             同路径），断言控件状态 getter，输出
 *             "XGuiAutoTest: [PASS]/[FAIL] 中文描述"，返回失败断言数。
 *
 *             【宽/紧凑宽度分档（2026-10-25）】真机 400 逻辑宽竖屏截图
 *             体检：宽档四列横排在窄根下自第二列起整体裁出视口——大表
 *             格被 380 定宽小表格挤成零宽（内容不可见且无滚动可达）、
 *             小节标题按列宽截断、表头段带只剩段 0；行高 24 亦低于手机
 *             40px 触控目标。根宽 <560 转紧凑档：五小节「标题+视图」
 *             成对纵排全宽单列（小表格行转纵向，小表格按「表头+3 行」
 *             定高全展、大表格吃余高），可点击行高统一提到 40；≥560
 *             逐位还原宽档装配（桌面 1000x700 现状不变）。分档统一走
 *             views_applyWidthTier（幂等），根 resize 与 adapt 契约双
 *             入口共用。
 * @note       文件所有权：本文件为页面独立翻译单元，不改契约头、主
 *             文件与 Src/ 任何文件。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include "XObject_Protected.h"
#include <string.h>

#include "XPrintf.h"
#include "XObject.h"
#include "XEvent.h"
#include "XSystem.h"
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
#if XWIDGET_ON && XTABLEWIDGET_ON && XLAYOUT_ON
#include "XBoxLayout.h" /* 页内摆位容器化：根/行/列盒式布局器（2026-10-17）。 */
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
 * 历史上基类 paintEvent 缺 XWidget_paintOffset 平移（文件头"绘制偏移
 * 补偿"节），本节曾以离屏快照规避；现基类四视图 paintEvent 均已自行
 * 平移，默认恒直接分派基类（含 paintOffset 非零场景）。旧快照路径仅
 * 供 XGUI_DEMO_VIEWS_SNAPSHOT=1 回退对照，逻辑原样保留：
 *   - paintOffset == (0,0)（XWidget_render/grab/保留层等离屏重定向
 *     闭环，或控件恰在绘制目标原点）：基类以局部坐标直绘即为正确，
 *     直接分派基类（内层快照自递归亦经此短路）；
 *   - paintOffset 非零且开关开（旧口径）：经 XWidget_render 把本控
 *     件子树离屏快照（闭环内 paintOffset 恒零，基类绘制正确），再按
 *     paintOffset 回贴——等价于"先按局部坐标自绘再平移合成"。
 */

/** @brief 旧离屏快照规避路径回退开关（夜三）：默认关；
 *         XGUI_DEMO_VIEWS_SNAPSHOT=1 才启用（一次探测缓存、GUI 主线程
 *         单写，同 Src/XGui/Charts/XChartView.c xcv_dirtyCullEnabled
 *         纪律）。=1 逐位还原旧行为（含 GPU 离屏会话放大器），供
 *         A/B 对照；=0 走基类自平移直绘（本帧起 GPU/SW 同路径）。 */
static int g_viewsSnapshotGate = -1;

static bool views_snapshotFallbackEnabled(void)
{
    if (g_viewsSnapshotGate < 0) {
        const char* env = XSystem_environment("XGUI_DEMO_VIEWS_SNAPSHOT");
        g_viewsSnapshotGate =
            (env && env[0] == '1' && env[1] == '\0') ? 1 : 0;
    }
    return g_viewsSnapshotGate > 0;
}

/** @brief paintOffset 安全绘制：basePaint 为最近基类的 PaintEvent 槽。
 *
 * @note  夜三归因更新：文件头所述"基类 paintEvent 缺 XWidget_paintOffset
 *        平移"的框架缺陷已在 Src/ 修复（XListView.c/XTreeView.c/
 *        XTreeWidget.c/XTableView.c 的 paintEvent 均先取 paintOffset
 *        平移再绘制，XListView.c:339 同型），本函数默认直接分派基类
 *        （基类自平移，语义=Qt"控件绘制恒在自身局部坐标系"）。旧离屏
 *        快照路径降级为回退开关 XGUI_DEMO_VIEWS_SNAPSHOT=1 才启用：
 *        该路径在 GPU 直通下是数量级放大器——每帧每控件 XWidget_render
 *        在控件尺寸离屏图上 begin_image，尺寸≠窗口会话缓冲即触发
 *        XPainter.c painterGpuSessionAcquire 离屏 GPU 会话（FBO 现绘
 *        +readback+drawImage 整幅重上传）×3~4 控件，实测 page5 增量
 *        168.9fps（5.92ms/帧）vs 直绘基类 8108fps（SW 0.123ms/帧），
 *        剪掉离屏会话后与其他页同路径（GPU 直通嵌套帧，零 readback）。
 *        开关仅作 A/B 回退（=1 逐位还原旧行为），默认关零开销。 */
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
    /* 基类平移已修复：非零偏移也直绘（基类 paintEvent 自行平移）。 */
    if (!views_snapshotFallbackEnabled()) {
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
#if XLAYOUT_ON
    XBoxLayout rootBox;         /**< 页面根纵向布局器（页内摆位自管）。 */
    XBoxLayout capRow;          /**< 小节标题行（水平，列构型同视图行）。 */
    XBoxLayout viewsRow;        /**< 视图行（水平：列表/视图/树/表头）。 */
    XBoxLayout midSection;      /**< 小表格段（纵向：标题 + 小表格行）。 */
    XBoxLayout fileRow;         /**< 小表格行（水平：小表格+大表格余宽）。 */
    /* 紧凑档（根宽 <560 竖屏口径）四个小节盒：「标题+视图」成对纵排
     * 全宽单列，替代宽档四列横排（宽档下窄根自第二列起裁出视口）。
     * 盒惰性初始化一次，跨档翻转复用（子控件随档在行盒/小节盒间迁移）。 */
    XBoxLayout secList;         /**< 紧凑档小节盒：XListWidget 标题+视图。 */
    XBoxLayout secView;         /**< 紧凑档小节盒：XListView 标题+视图。 */
    XBoxLayout secTree;         /**< 紧凑档小节盒：XTreeWidget 标题+视图。 */
    XBoxLayout secHeader;       /**< 紧凑档小节盒：XHeaderView 标题+段带。 */
    XTableWidget* bigTable;     /**< 主文件后挂大表格（m_tableWidget 借用）。 */
    bool bigTableAdopted;       /**< 大表格已收编进 fileRow 守卫。 */
    bool buildDone;             /**< build 装配完成守卫（其后新增子=大表格）。 */
    bool secBoxesReady;         /**< 紧凑档小节盒已初始化守卫（惰性一次）。 */
    bool compact;               /**< 当前宽度档：true=紧凑纵排 false=宽档横排。 */
#endif
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

/* ==================== 页面根控件子类（大表格收编载体，2026-10-17） ====================
 *
 * 主文件 DemoWin_create 在本页 build 之后才把大表格（m_tableWidget）
 * 以本页根为父挂入（自选项卡页打散归位），build 装配段已收工、adapt
 * 契约又不携带新增子控件——故以根控件子类的事件入口拦截 CHILD_ADDED，
 * 把 build 完成后新增的子控件（即大表格）收编进小表格行（贴其右侧、
 * stretch 吃余宽），列宽随根宽定版（对标原 demo_layout_content「条目
 * 视图页补充大表格」段的摆位/列宽逻辑，该段已自主文件删除）。事件拦
 * 截写法与 XGroupBox.c VXGroupBox_event 同型（本框架子对象事件经事件
 * 入口分派而不走 EXObject_ChildEvent 槽位）。
 */

XCLASS_DEFINE_BEGING(DemoViewsRootWidget)
XCLASS_DEFINE_EXTEND_END(DemoViewsRootWidget, XWidget)

/** @brief 页面根控件子类（m_base 必须是第一个成员）。 */
typedef struct DemoViewsRootWidget
{
    XWidget m_base; /**< 基类成员；必须是第一个。 */
} DemoViewsRootWidget;

#if XLAYOUT_ON
/* ==================== 宽/紧凑宽度分档（真机竖屏口径，2026-10-25） ====================
 *
 * 真机 400 逻辑宽竖屏截图体检（见文件头「宽/紧凑宽度分档」节）：宽档
 * 四列横排在窄根下自第二列起整体裁出视口，行高 24 低于手机触控目标。
 * 按根宽分双档：≥560 维持宽档（桌面现状逐位不变），<560 转紧凑档纵排
 * 单列 + 行高 40。分档判定挂根宽（页面根几何由堆叠布局分配，根 resize
 * 与 adapt 契约两路都携带新鲜宽度），统一入口 views_applyWidthTier。
 */

/** @brief 宽/紧凑分档阈值：宽档四列横排（3 伸缩列 + 176 定宽树列 +
 *         间距边距）需约 ≥560 根宽标题才不截断；手机横屏（~800）落
 *         宽档、竖屏（400）落紧凑档。 */
#define VIEWS_COMPACT_MAX_W 560
/** @brief 紧凑档统一行高：手机可点击行触控目标下限（40 逻辑像素）。 */
#define VIEWS_COMPACT_ROW_H 40
/** @brief 宽档行高还原值 = 各视图库默认（XListView/XTreeView/XTableView
 *         缺省同为 24），桌面现状不动。 */
#define VIEWS_WIDE_ROW_H 24
/** @brief 宽档树列/小表格定宽与表头段带高（build 宽档装配原值，翻档
 *         逐位还原用）。 */
#define VIEWS_WIDE_TREE_W 176
#define VIEWS_WIDE_TABLE_W 380
#define VIEWS_WIDE_HEADER_H 26
/** @brief 紧凑档表头段带高：段可点击（sectionClicked），随触控目标一并
 *         提到 40。 */
#define VIEWS_COMPACT_HEADER_H 40

/** @brief 大表格列宽随可视宽定版（逐式对标原 demo_layout_content 段：
 *         可视宽≥372 四列定宽恰满；≥200 保「名称+类型」主列比例，其余
 *         列横滚可达；<200 不动（原窄根换行铺位已由布局器静态摆位取
 *         代，此处行为与原段一致=维持上次列宽）。 */
static void views_bigTableRefit(void)
{
    XTableWidget* table = g_views.bigTable;
    int view;
    if (!g_views.bigTableAdopted || !table) return;
    /* 大表格宽按档换算：宽档 = 根宽 − 左右缘 16 − 小表格 380 − 段距 16
     * （fileRow 摆位式，与原段 tx=404/tw=rootW-412 逐像素同源）；紧凑档
     * 小表格行转纵向，大表格满列宽 = 根宽 − 左右缘 16。可视宽再让纵条
     * 带 12。 */
    if (g_views.compact)
        view = XWidget_width(g_views.root) - 16 - 12;
    else
        view = XWidget_width(g_views.root) - 412 - 12;
    if (view >= 372) {
        XTableView_setColumnWidth((XTableView*)table, 0, 128);
        XTableView_setColumnWidth((XTableView*)table, 1, 72);
        XTableView_setColumnWidth((XTableView*)table, 2, 60);
        XTableView_setColumnWidth((XTableView*)table, 3, 112);
    } else if (view >= 200) {
        int c0 = view * 62 / 100;
        XTableView_setColumnWidth((XTableView*)table, 0, c0);
        XTableView_setColumnWidth((XTableView*)table, 1, view - c0);
        XTableView_setColumnWidth((XTableView*)table, 2, 60);
        XTableView_setColumnWidth((XTableView*)table, 3, 112);
    }
}

/** @brief 可点击行度量随档定版（幂等，每拍重申无害）：列表/模型视图/
 *         树/双表格行高宽档还原库默认 24（桌面现状不动）、紧凑档提到
 *         40 手机触控目标；表头段带同为可点击元素随档 26/40。 */
static void views_applyRowMetrics(bool compact)
{
    int rowH = compact ? VIEWS_COMPACT_ROW_H : VIEWS_WIDE_ROW_H;
    XListView_setRowHeight((XListView*)&g_views.list, rowH);
    XListView_setRowHeight((XListView*)&g_views.view, rowH);
    XTreeView_setRowHeight((XTreeView*)&g_views.tree, rowH);
    XTableView_setRowHeight((XTableView*)&g_views.table, rowH);
    if (g_views.bigTableAdopted && g_views.bigTable)
        XTableView_setRowHeight((XTableView*)g_views.bigTable, rowH);
    XWidget_setFixedHeight((XWidget*)&g_views.header,
                           compact ? VIEWS_COMPACT_HEADER_H
                                   : VIEWS_WIDE_HEADER_H);
}

/** @brief 切紧凑档装配：五小节「标题+视图」成对纵排全宽单列。
 * @details 子控件从宽档 capRow/viewsRow 摘出挂入四个小节盒（盒体惰性
 *          初始化一次、跨档复用），宽档两行盒整体从页根让位（addLayout
 *          挂入的借用条目，取出不释放；stretch 平行数组随取出/插入同步
 *          迁移，见 VXBoxLayout_takeAt）；小表格行转纵向——树列/小表格
 *          解除宽档定宽改随列全宽（窄根下 380 定宽会把行内兄弟挤成零
 *          宽，大表格不可见的根因），小表格高按「表头+3 行」定版（3 行
 *          数据全展不出内滚条），大表格吃余高。仅在档位翻转时调用，
 *          子控件归属由档位守卫唯一确定（摘挂顺序不可倒置）。 */
static void views_layoutCompact(void)
{
    if (!g_views.secBoxesReady) {
        XBoxLayout_init(&g_views.secList, XBoxLayoutDirection_TopToBottom);
        XBoxLayout_init(&g_views.secView, XBoxLayoutDirection_TopToBottom);
        XBoxLayout_init(&g_views.secTree, XBoxLayoutDirection_TopToBottom);
        XBoxLayout_init(&g_views.secHeader, XBoxLayoutDirection_TopToBottom);
        XLayout_setSpacing((XLayout*)&g_views.secList, 4);
        XLayout_setSpacing((XLayout*)&g_views.secView, 4);
        XLayout_setSpacing((XLayout*)&g_views.secTree, 4);
        XLayout_setSpacing((XLayout*)&g_views.secHeader, 4);
        g_views.secBoxesReady = true;
    }
    /* 先摘后挂：此刻四个标题/视图必挂宽档行盒（档位守卫保证）。 */
    XLayout_removeWidget((XLayout*)&g_views.capRow,
                         (XWidget*)&g_views.capList);
    XLayout_removeWidget((XLayout*)&g_views.capRow,
                         (XWidget*)&g_views.capView);
    XLayout_removeWidget((XLayout*)&g_views.capRow,
                         (XWidget*)&g_views.capTree);
    XLayout_removeWidget((XLayout*)&g_views.capRow,
                         (XWidget*)&g_views.capHeader);
    XLayout_removeWidget((XLayout*)&g_views.viewsRow,
                         (XWidget*)&g_views.list);
    XLayout_removeWidget((XLayout*)&g_views.viewsRow,
                         (XWidget*)&g_views.view);
    XLayout_removeWidget((XLayout*)&g_views.viewsRow,
                         (XWidget*)&g_views.tree);
    XLayout_removeWidget((XLayout*)&g_views.viewsRow,
                         (XWidget*)&g_views.header);
    XBoxLayout_addWidget(&g_views.secList, (XWidget*)&g_views.capList);
    XBoxLayout_addWidgetEx(&g_views.secList, (XWidget*)&g_views.list, 1, 0);
    XBoxLayout_addWidget(&g_views.secView, (XWidget*)&g_views.capView);
    XBoxLayout_addWidgetEx(&g_views.secView, (XWidget*)&g_views.view, 1, 0);
    XBoxLayout_addWidget(&g_views.secTree, (XWidget*)&g_views.capTree);
    XBoxLayout_addWidgetEx(&g_views.secTree, (XWidget*)&g_views.tree, 1, 0);
    XBoxLayout_addWidget(&g_views.secHeader, (XWidget*)&g_views.capHeader);
    XBoxLayout_addWidget(&g_views.secHeader, (XWidget*)&g_views.header);
    /* 宽档两行盒让位，四小节插回页根头部（midSection/状态行保持原位）。 */
    XLayout_removeItem((XLayout*)&g_views.rootBox,
                       (XLayoutItem*)&g_views.capRow);
    XLayout_removeItem((XLayout*)&g_views.rootBox,
                       (XLayoutItem*)&g_views.viewsRow);
    XBoxLayout_insertLayout(&g_views.rootBox, 0, (XLayout*)&g_views.secList,
                            1);
    XBoxLayout_insertLayout(&g_views.rootBox, 1, (XLayout*)&g_views.secView,
                            1);
    XBoxLayout_insertLayout(&g_views.rootBox, 2, (XLayout*)&g_views.secTree,
                            1);
    XBoxLayout_insertLayout(&g_views.rootBox, 3, (XLayout*)&g_views.secHeader,
                            0);
    XBoxLayout_setDirection(&g_views.fileRow,
                            XBoxLayoutDirection_TopToBottom);
    XWidget_setMinimumWidth((XWidget*)&g_views.tree, 0);
    XWidget_setMaximumWidth((XWidget*)&g_views.tree, XWIDGET_MAX_SIZE);
    XWidget_setMinimumWidth((XWidget*)&g_views.capTree, 0);
    XWidget_setMaximumWidth((XWidget*)&g_views.capTree, XWIDGET_MAX_SIZE);
    XWidget_setMinimumWidth((XWidget*)&g_views.table, 0);
    XWidget_setMaximumWidth((XWidget*)&g_views.table, XWIDGET_MAX_SIZE);
    XWidget_setFixedHeight((XWidget*)&g_views.table,
                           g_views.table.m_headerHeight +
                           XTableView_rowHeight(
                               (XTableView*)&g_views.table) * 3);
    /* [最小高保障 2026-10-27 视觉走查 P1-3] 五视图紧凑档总高超出视口时
     * 布局把余高压薄，树只剩 ~1.5 行且被水平滚动条叠压（真机截图实证）。
     * 视图最小高=表头档+3 行（紧凑行高常量与 applyRowMetrics 同源）：
     * 内容超出视口的底部截断交由后续页级滚动专项（scroll 壳已验证可
     * 行，双档装配交互待收尾），本处先保可读。 */
    XWidget_setMinimumHeight((XWidget*)&g_views.list,
                             VIEWS_COMPACT_HEADER_H +
                             VIEWS_COMPACT_ROW_H * 3);
    XWidget_setMinimumHeight((XWidget*)&g_views.view,
                             VIEWS_COMPACT_HEADER_H +
                             VIEWS_COMPACT_ROW_H * 3);
    XWidget_setMinimumHeight((XWidget*)&g_views.tree,
                             VIEWS_COMPACT_HEADER_H +
                             VIEWS_COMPACT_ROW_H * 3);
    XWidget_setMinimumHeight((XWidget*)&g_views.header,
                             VIEWS_COMPACT_HEADER_H +
                             VIEWS_COMPACT_ROW_H * 2);
}

/** @brief 切宽档装配：四小节摘回 capRow/viewsRow 行盒，逐位还原 build
 *         宽档装配的伸展/对齐/定宽，小表格行转回横向。
 * @details 对齐位与伸展挂在布局的条目包装上，removeWidget 连同包装一并
 *          销毁，故重挂时须逐一重申（stretch 数组虽随取出/插入同步，
 *          条目是新建的，显式传参最稳）。 */
static void views_layoutWide(void)
{
    XLayout_removeWidget((XLayout*)&g_views.secList,
                         (XWidget*)&g_views.capList);
    XLayout_removeWidget((XLayout*)&g_views.secList, (XWidget*)&g_views.list);
    XLayout_removeWidget((XLayout*)&g_views.secView,
                         (XWidget*)&g_views.capView);
    XLayout_removeWidget((XLayout*)&g_views.secView, (XWidget*)&g_views.view);
    XLayout_removeWidget((XLayout*)&g_views.secTree,
                         (XWidget*)&g_views.capTree);
    XLayout_removeWidget((XLayout*)&g_views.secTree, (XWidget*)&g_views.tree);
    XLayout_removeWidget((XLayout*)&g_views.secHeader,
                         (XWidget*)&g_views.capHeader);
    XLayout_removeWidget((XLayout*)&g_views.secHeader,
                         (XWidget*)&g_views.header);
    XLayout_removeItem((XLayout*)&g_views.rootBox,
                       (XLayoutItem*)&g_views.secList);
    XLayout_removeItem((XLayout*)&g_views.rootBox,
                       (XLayoutItem*)&g_views.secView);
    XLayout_removeItem((XLayout*)&g_views.rootBox,
                       (XLayoutItem*)&g_views.secTree);
    XLayout_removeItem((XLayout*)&g_views.rootBox,
                       (XLayoutItem*)&g_views.secHeader);
    XBoxLayout_insertLayout(&g_views.rootBox, 0, (XLayout*)&g_views.capRow,
                            0);
    XBoxLayout_insertLayout(&g_views.rootBox, 1, (XLayout*)&g_views.viewsRow,
                            1);
    /* 标题行：列表/视图/表头伸缩 27、树列定宽 176（build 宽档原口径）。 */
    XBoxLayout_addWidgetEx(&g_views.capRow, (XWidget*)&g_views.capList, 27, 0);
    XBoxLayout_addWidgetEx(&g_views.capRow, (XWidget*)&g_views.capView, 27, 0);
    XBoxLayout_addWidgetEx(&g_views.capRow, (XWidget*)&g_views.capTree, 0, 0);
    XWidget_setFixedWidth((XWidget*)&g_views.capTree, VIEWS_WIDE_TREE_W);
    XBoxLayout_addWidgetEx(&g_views.capRow, (XWidget*)&g_views.capHeader, 27,
                           0);
    /* 视图行：同构四列，表头 h26 顶对齐（build 宽档原口径）。 */
    XBoxLayout_addWidgetEx(&g_views.viewsRow, (XWidget*)&g_views.list, 27, 0);
    XBoxLayout_addWidgetEx(&g_views.viewsRow, (XWidget*)&g_views.view, 27, 0);
    XBoxLayout_addWidgetEx(&g_views.viewsRow, (XWidget*)&g_views.tree, 0, 0);
    XWidget_setFixedWidth((XWidget*)&g_views.tree, VIEWS_WIDE_TREE_W);
    XBoxLayout_addWidgetEx(&g_views.viewsRow, (XWidget*)&g_views.header, 27,
                           XAlignment_Top);
    /* 小表格行转回横向：小表格定宽 380、高交还根布局弹性分配（撑满改造
     * 口径：撤固定高随段双向撑满）。 */
    XBoxLayout_setDirection(&g_views.fileRow,
                            XBoxLayoutDirection_LeftToRight);
    XWidget_setFixedWidth((XWidget*)&g_views.table, VIEWS_WIDE_TABLE_W);
    XWidget_setMinimumHeight((XWidget*)&g_views.table, 0);
    XWidget_setMaximumHeight((XWidget*)&g_views.table, XWIDGET_MAX_SIZE);
    XBoxLayout_setStretchFactorLayout(&g_views.rootBox,
                                      (XLayout*)&g_views.midSection, 1);
}

/** @brief 宽/紧凑分档统一入口（幂等）：根宽过阈值翻档重排，行度量与
 *         大表格列宽每拍重申。
 * @details 根 resizeEvent 与 adapt 契约双入口共用（重复无害）。翻档改
 *          的多是定尺/条目归属，setFixed 不失效布局、摘挂也只标脏——
 *          须显式 invalidate+activate 让新装配本拍落几何（根 resizeEvent
 *          派发于根布局 activate 之前，此时根宽已是新值，activate 以新
 *          宽解算；同 Modbus 从站页定尺纪律）。 */
static void views_applyWidthTier(void)
{
    bool compact;
    if (!g_views.root || !g_views.buildDone) return;
    compact = XWidget_width(g_views.root) > 0 &&
              XWidget_width(g_views.root) < VIEWS_COMPACT_MAX_W;
    views_applyRowMetrics(compact);
    if (compact != g_views.compact) {
        g_views.compact = compact;
        if (compact)
            views_layoutCompact();
        else
            views_layoutWide();
        XLayout_invalidate((XLayout*)&g_views.rootBox);
        XLayout_activate((XLayout*)&g_views.rootBox);
    }
    views_bigTableRefit();
}

/** @brief 收编主文件后挂的本页子控件（大表格）：插到小表格之后，stretch
 *         吃行内余宽（宽随页面根自适应）；[撑满改造 2026-10-17] 撤
 *         h124 固定高——原定高防内容 sizeHint(~186) 顶高行挤偏小表格，
 *         现行高由根布局 stretch 分配（小表格段 1 份弹性高），sizeHint
 *         不再驱动行高，两表随行撑满（固定高反而被居中留缝）。
 *         收编时点晚于 build，页根可能已是紧凑档：走分档统一入口补齐
 *         行高/列宽与行向，不再只刷列宽。 */
static void views_adoptBigTable(XObject* child)
{
    if (g_views.bigTableAdopted || !child || !child->m_is_widget) return;
    g_views.bigTable = (XTableWidget*)child;
    XBoxLayout_insertWidgetEx(&g_views.fileRow, 1, (XWidget*)child, 1, 0);
    g_views.bigTableAdopted = true;
    views_applyWidthTier();
}
#endif /* XLAYOUT_ON */

/** @brief 根控件事件入口：build 装配完成后新增的子控件即主文件大表格。 */
static bool VDemoViewsRootWidget_event(XWidget* self, XEvent* event)
{
    bool result;
    result = XClass_Parent(XWidget, EXObject_Event,
                           bool (*)(XObject*, XEvent*))((XObject*)self,
                                                        event);
#if XLAYOUT_ON
    if (g_views.buildDone && event &&
        XEvent_type(event) == XEVENT_TYPE_CHILD_ADDED)
        views_adoptBigTable(XChildEvent_child((const XChildEvent*)event));
#endif
    return result;
}

/** @brief 根控件尺寸变化：重定大表格列宽（本槽派发于根布局 activate
 *         之前，故列宽按根宽预测式换算——与 fileRow 摆位式同源）；并按
 *         新根宽过档（真机竖屏口径：宽/紧凑分档见 views_applyWidthTier
 *         节注）——堆叠几何随 contentHost resize 自动重激活的路径不经过
 *         adapt 契约漏斗，窄根翻档须在此兜住。 */
static void VDemoViewsRootWidget_resizeEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void (*)(XWidget*, XEvent*))((XWidget*)self, event);
#if XLAYOUT_ON
    views_applyWidthTier();
#endif
}

/** @brief 初始化 DemoViewsRootWidget 类虚函数表。 */
XVtable* DemoViewsRootWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoViewsRootWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VDemoViewsRootWidget_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VDemoViewsRootWidget_resizeEvent);
    return XVTABLE_DEFAULT;
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
    XTreeWidgetItem* shown;
    (void)receiver;
    /* 信号第二参为真实命中条目（含子节点，对标 QTreeWidget::itemClicked
     * 载荷）；旧载荷/空载荷时回退顶层行条目。 */
    XVarList_args_2(args, int, row, XTreeWidgetItem*, item);
    if (row < 0) return;
    shown = item ? item : XTreeWidget_topLevelItem((XTreeWidget*)&g_views.tree, row);
    snprintf(buf, sizeof(buf), "树点击: 行 %d「%s」", row,
             shown ? XTreeWidgetItem_text_2(shown) : "");
    views_setState(buf);
}

/** @brief 表头段点击：状态行反馈（对标 QHeaderView::sectionClicked）。 */
static void views_headerClickedSlot(XObject* receiver, XVarList* args)
{
    char buf[64];
    (void)receiver;
    XVarList_args_1(args, int, section);
    snprintf(buf, sizeof(buf), "表头段点击: %d", section);
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

/* ==================== 构建（布局容器化装配，2026-10-17） ==================== */

#if XWIDGET_ON && XLABEL_ON
/** @brief 小节标题标签统一装配（左对齐垂直居中，14px；固定高 18，宽随
 *         行布局器伸缩——原 x/y/w 手写坐标由 build 装配段的列构型取代）。 */
static void views_makeCaption(XLabel* label, XWidget* parent,
                              const char* text)
{
    XLabel_init(label, parent, 0);
    XLabel_setText_2(label, text);
    XLabel_setTextPixelSize(label, 14);
    XLabel_setAlignment(label, XAlignment_Left | XAlignment_VCenter);
    XWidget_setFixedHeight((XWidget*)label, 18);
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

    /* 页面根控件：子类堆创建，父子链级联析构（契约口径）——子类事件
       入口收编主文件后挂的大表格（见 DemoViewsRootWidget 节注）。
       页面几何由堆叠布局分配、页内由根布局器重排，不再手写 setGeometry。 */
    root = (XWidget*)XMemory_malloc(sizeof(DemoViewsRootWidget),
                                    XCLASS_DEFAULT_MEMORY_TYPE);
    if (!root) return NULL;
    XMemset(root, 0, sizeof(DemoViewsRootWidget));
    XWidget_init(root, parent, 0);
    XClassSetVtable(root, DemoViewsRootWidget);
    Set_Class_Memory(root, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(root, true);
    g_views.root = root;

    /* 自定义模型：静态自持，once 守卫（非控件不入级联析构链）。 */
    if (!g_views.modelReady) {
        DemoViewsGridModel_init(&g_views.model);
        g_views.modelReady = true;
    }

#if XWIDGET_ON && XLABEL_ON
    /* 小节标题（固定高 18 一行；宽度随列布局器伸缩）。 */
    views_makeCaption(&g_views.capList, root, "XListWidget（点击选择）");
    views_makeCaption(&g_views.capView, root, "XListView＋自定义模型");
    views_makeCaption(&g_views.capTree, root, "XTreeWidget");
    views_makeCaption(&g_views.capHeader, root, "XHeaderView（几何）");
    views_makeCaption(&g_views.capTable, root, "XTableWidget（2 列 3 行）");
#endif

    /* ---- XListWidget：文本条目列表（对标 QListWidget addItem）。 ---- */
    DemoViewsListWidget_init(&g_views.list, root, 0);
    XListWidget_addItem_2((XListWidget*)&g_views.list, "苹果");
    XListWidget_addItem_2((XListWidget*)&g_views.list, "香蕉");
    XListWidget_addItem_2((XListWidget*)&g_views.list, "樱桃");
    XListWidget_addItem_2((XListWidget*)&g_views.list, "榴莲");
    XObject_connect_1((XObject*)&g_views.list,
                      (size_t)XListWidget_itemClicked_signal(NULL, 0),
                      (XObject*)&g_views.list, views_listClickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&g_views.list);

    /* ---- XListView：挂自定义 3x3 模型（对标 QListView setModel）。 ---- */
    DemoViewsListView_init(&g_views.view, root, 0);
    XAbstractItemView_setModel((XAbstractItemView*)&g_views.view,
                               &g_views.model.m_base);
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
        /* 双列展示（§8.0g17 四期④绘制消费）：列 1 备注 + 表头标签；
         * 列 0 固定宽（名称列省略根修，见下方 setColumnWidth 注）。 */
        XTreeWidget_setColumnCount((XTreeWidget*)&g_views.tree, 2);
        XTreeWidgetItem_setTextAt_2(device, 1, "在线");
        XTreeWidgetItem_setTextAt_2(outer, 1, "就绪");
        {
            static const char* headerLabels[2] = {"名称", "状态"};
            XTreeWidget_setHeaderLabels((XTreeWidget*)&g_views.tree,
                                        headerLabels, 2);
        }
    }
    /* 列 0 固定宽（名称列省略根修）：缺省两列全 0=自动均摊，原 adapt
     * 树列定宽 176（=80+84+12，名称列 80 + 状态列让开纵向滚动条带，
     * 目验二轮挂项「在线/就绪」出全字的下限，宽窄根两档 adapt 均落此
     * 下限）——容器化后以 setFixedWidth(176) 等价定版。 */
    XTreeView_setColumnWidth((XTreeView*)&g_views.tree, 0, 80);
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
    /* 段点击联动状态行（库侧 sectionClicked 已真实发射，demo 补消费）。 */
    XObject_connect_1((XObject*)&g_views.header,
                      (size_t)XHeaderView_sectionClicked_signal(
                          (XHeaderView*)&g_views.header, 0),
                      (XObject*)&g_views.header, views_headerClickedSlot,
                      XConnectionType_Direct);
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
    XWidget_show((XWidget*)&g_views.stateLabel);
#endif

#if XLAYOUT_ON
    /* ---- 根纵向布局器装配（间距/边距对标改造前手写坐标）：
     * 根 = 小节标题行 + 视图行 + 小表格段 + 底部弹性 + 状态行贴底。
     * 原坐标：标题行 y6 h18、视图行 y28 h118~130、小表格段 y160 起
     * （标题 h18 + 段距 4 + 表格 h124）、状态行贴底 h28、x8 两缘。 ---- */
    /* 小节标题行：四列构型与视图行同构（列表/视图/表头伸缩 27、树列
     * 定宽 176 对齐），段距 12 对标原 x1→x2→x3→x4 步进 12。 */
    XBoxLayout_init(&g_views.capRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addWidgetEx(&g_views.capRow, (XWidget*)&g_views.capList,
                           27, 0);
    XBoxLayout_addWidgetEx(&g_views.capRow, (XWidget*)&g_views.capView,
                           27, 0);
    XBoxLayout_addWidgetEx(&g_views.capRow, (XWidget*)&g_views.capTree,
                           0, 0);
    XWidget_setFixedWidth((XWidget*)&g_views.capTree, 176);
    XBoxLayout_addWidgetEx(&g_views.capRow, (XWidget*)&g_views.capHeader,
                           27, 0);
    XLayout_setSpacing((XLayout*)&g_views.capRow, 12);
    /* 视图行：列表/视图/树随行高撑满（横向伸缩 27/树定宽 176；
     * [撑满改造 2026-10-17] 撤 h118/h130 固定高与 strut——下方余高
     * 原被根布局空白段吞掉=页面下半大片留白，现视图行与小表格段
     * 各吃 1 份弹性高，页面纵向全利用）；表头 h26 保持紧凑顶对齐。 */
    XBoxLayout_init(&g_views.viewsRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addWidgetEx(&g_views.viewsRow, (XWidget*)&g_views.list,
                           27, 0);
    XBoxLayout_addWidgetEx(&g_views.viewsRow, (XWidget*)&g_views.view,
                           27, 0);
    XBoxLayout_addWidgetEx(&g_views.viewsRow, (XWidget*)&g_views.tree,
                           0, 0);
    XWidget_setFixedWidth((XWidget*)&g_views.tree, 176);
    XBoxLayout_addWidgetEx(&g_views.viewsRow, (XWidget*)&g_views.header,
                           27, XAlignment_Top);
    XWidget_setFixedHeight((XWidget*)&g_views.header, 26);
    XLayout_setSpacing((XLayout*)&g_views.viewsRow, 12);
    /* 小表格段：小表格定宽 380、高随段撑满（[撑满改造] 撤 h124 固定高）
     * + 主文件后挂大表格收编进行内余宽双向撑满（段距 16 对标原大表格
     * x=404=8+380+16）。 */
    XBoxLayout_init(&g_views.fileRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addWidget(&g_views.fileRow, (XWidget*)&g_views.table);
    XWidget_setFixedWidth((XWidget*)&g_views.table, 380);
    XLayout_setSpacing((XLayout*)&g_views.fileRow, 16);
    XBoxLayout_init(&g_views.midSection, XBoxLayoutDirection_TopToBottom);
    XBoxLayout_addWidget(&g_views.midSection, (XWidget*)&g_views.capTable);
    XBoxLayout_addLayout(&g_views.midSection, (XLayout*)&g_views.fileRow);
    XLayout_setSpacing((XLayout*)&g_views.midSection, 4);
    /* 页根：段距 4、边距 8,6,8,4；[撑满改造 2026-10-17] 余高不再留
     * 空白段——视图行与小表格段各吃 1 份弹性高（1:1）纵向撑满页面，
     * 状态行固定高贴底（对标原 y=rootH-32 h28）。 */
    XBoxLayout_init(&g_views.rootBox, XBoxLayoutDirection_TopToBottom);
    XBoxLayout_addLayout(&g_views.rootBox, (XLayout*)&g_views.capRow);
    XBoxLayout_addLayout(&g_views.rootBox, (XLayout*)&g_views.viewsRow);
    XBoxLayout_addLayout(&g_views.rootBox, (XLayout*)&g_views.midSection);
    XBoxLayout_setStretchFactorLayout(&g_views.rootBox,
                                      (XLayout*)&g_views.viewsRow, 1);
    XBoxLayout_setStretchFactorLayout(&g_views.rootBox,
                                      (XLayout*)&g_views.midSection, 1);
    XBoxLayout_addWidget(&g_views.rootBox, (XWidget*)&g_views.stateLabel);
    XWidget_setFixedHeight((XWidget*)&g_views.stateLabel, 28);
    XLayout_setContentsMargins((XLayout*)&g_views.rootBox, 8, 6, 8, 4);
    XLayout_setSpacing((XLayout*)&g_views.rootBox, 4);
    XWidget_setLayout(root, (XLayout*)&g_views.rootBox);
    g_views.buildDone = true;
#endif /* XLAYOUT_ON */

    XWidget_show(root);
    return root;
}

/** @brief 自适应重排（xgui_demo_pages.h 契约）：宽/紧凑宽度分档重排。
 * @details 页内摆位 2026-10-17 起由根控件挂盒式布局器自管（装配见
 *          demo_page_views_build），几何随堆叠布局分配的根尺寸自动重排，
 *          手写坐标不再；2026-10-25 起本契约恢复职责——主文件
 *          demo_layout_content 在切页/resize/startup/停靠全路径携带
 *          新鲜根宽调用进来，这里按根宽分宽/紧凑双档重排（幂等统一
 *          入口 views_applyWidthTier；根 resizeEvent 为另一兜底入口）。
 *          大表格摆位/列宽逻辑在本文件（DemoViewsRootWidget 节）。
 *          page 与登记根不符时亦无操作。 */
void demo_page_views_adapt(XWidget* page)
{
#if XLAYOUT_ON
    if (page == g_views.root) views_applyWidthTier();
#else
    (void)page;
#endif
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
