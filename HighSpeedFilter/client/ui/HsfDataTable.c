/**
 * @file       HsfDataTable.c
 * @brief      数据域通用表格页骨架：控制工具条 + XTableWidget 铺满。
 * @details    Qt 口径：页根对标 QWidget 容器，布局对标 QVBoxLayout，
 *             表格对标 QTableWidget。顶部控制条（HsfDataBar，时间/通道/
 *             筛选/导出/视图切换）+ 表格（stretch=1 占满）；「图表」视图
 *             为占位空白画布（数据接入后续替换实现）。几何全交布局器
 *             （勿手写坐标；见「XGui 固定宽约束归布局」教训）。
 *             sizePolicy 铺满依赖框架缺省：XAbstractScrollArea_init 已
 *             对标 Qt QAbstractScrollAreaPrivate::init 补
 *             Expanding/Expanding（2026-11-03 根修）。
 *             列宽保持 Qt QHeaderView 缺省 Interactive 口径（Qt 6.8
 *             QTableWidget 缺省不均分列宽）；表头空区观感按既定决策
 *             以底色收束（XTableWidget.c 表头段注释）。
 */
#include "HsfDataTable.h"

#include "HsfTheme.h"
#include "HsfDataBar.h"
#include "XTableWidget.h"
#include "XLabel.h"
#include "XBoxLayout.h"
#include "XObject.h"
#include "XMemory.h"
#include "XSystem.h"             /* XSystem_environment（自测钩子门）。 */
#include "XPainter.h"
#include "XWidget_Protected.h"   /* XWidget_paintImage/paintOffset。 */
#include <string.h>

#if XWIDGET_ON && XTABLEWIDGET_ON && XLAYOUT_ON && XLAYOUT_BOX_ON

/** @brief 图表视图占位画布（paintEvent 画提示文案；数据接入后替换）。 */
typedef struct HsfChartPlaceholder
{
    XWidget m_base;   /**< 画布；必须是第一个成员。 */
    const char* m_text; /**< 占位提示（UTF-8；静态存活）。 */
} HsfChartPlaceholder;

static void VHsfChartPlaceholder_paintEvent(XWidget* self, XEvent* event);

XCLASS_DEFINE_BEGING(HsfChartPlaceholderWidget)
XCLASS_DEFINE_EXTEND_END(HsfChartPlaceholderWidget, XWidget)

/**
 * @brief 图表占位类（XWidget 直派生；只重载 paintEvent——
 *        HsfMonitorVfd 的 HsfVfdBannerWidget 同款三段式）。
 */
XVtable* HsfChartPlaceholder_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfChartPlaceholderWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VHsfChartPlaceholder_paintEvent);
    XCLASS_SHOW_SIZE_DEFAULT(HsfChartPlaceholderWidget);
    return XVTABLE_DEFAULT;
}

/** @brief 表格页对象；m_base 必须是第一个成员。 */
typedef struct HsfDataTablePage
{
    XWidget      m_base;    /**< 页容器；必须是第一个成员。 */
    XBoxLayout   m_layout;  /**< 页内垂直布局。 */
    XLabel       m_title;   /**< 页标题（粗体，居中）。 */
    XWidget*     m_bar;     /**< 控制工具条（时间/筛选/导出/视图切换；借用）。 */
    XTableWidget m_table;   /**< 内容表格（铺满，随窗口拉伸）。 */
    HsfChartPlaceholder m_chart; /**< 图表视图占位（与表格二选一显隐）。 */
    int          m_viewMode; /**< 当前视图：0=表格 1=图表。 */
    int          m_fontBasePx; /**< 建页时表格字体像素（0=未设，不缩放）。 */
    int          m_rowBaseH;   /**< 建页时行高（随字号缩放）。 */
    int          m_rowCount;   /**< 建页时行数（缩放时逐行设高）。 */
} HsfDataTablePage;

/** @brief 工具条动作落地：视图切换显隐两视图（其余动作待数据接入）。 */
static void pageBarAction(void* userData, int action)
{
    HsfDataTablePage* self = (HsfDataTablePage*)userData;
    if (!self) return;
    if (action == HsfDataBarAction_ViewTable ||
        action == HsfDataBarAction_ViewChart) {
        self->m_viewMode = (action == HsfDataBarAction_ViewChart) ? 1 : 0;
        XWidget_setVisible((XWidget*)&self->m_table,
                           self->m_viewMode == 0);
        XWidget_setVisible(&self->m_chart.m_base,
                           self->m_viewMode == 1);
        /* 显隐已对标 Qt 触发父布局失效（XWidget_setVisible 的
         * LayoutRequest 段），此处无需手工重排。 */
    }
    /* Filter/ClearFilter/Export/Reload/GenerateChart：数据接入后接线。 */
}

/** @brief 图表占位画布重绘：窗口底色 + 居中提示文案（灰）。
 *  @note  paintImage 范式（XTableView 同款）：取顶层后备存储 →
 *         begin_image → paintOffset 平移到局部原点。 */
static void VHsfChartPlaceholder_paintEvent(XWidget* self, XEvent* event)
{
    HsfChartPlaceholder* ph = (HsfChartPlaceholder*)self;
    XPainter painter;
    XImage* image;
    XRect r;
    XPoint offset;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XRect_init(&r, 0, 0, XWidget_width(self), XWidget_height(self));
    XPainter_fillRect(&painter, &r, 0xFFF4F6F8u);   /* 页底浅灰（主题窗底） */
    XPainter_drawTextRect(&painter, &r,
                          XPAINTER_TEXT_ALIGN_HCENTER |
                          XPAINTER_TEXT_ALIGN_VCENTER,
                          (ph && ph->m_text) ? ph->m_text : "",
                          0xFF8B98A5u);              /* 次级描边灰 */
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

XWidget* HsfDataTablePage_create(XWidget* parent,
                                 const char* title,
                                 const char* const* headers,
                                 int columnCount, int rowCount)
{
    HsfDataTablePage* self;
    if (!parent) return NULL;
    self = (HsfDataTablePage*)XMemory_malloc(sizeof(HsfDataTablePage),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(HsfDataTablePage));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true); /* 整块随主窗级联释放。 */

    HsfTheme_applyDefaultFont(&self->m_base);

    /* 页标题（粗体居中；样式同 HsfPlaceHolder 标题口径）。 */
    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    {
        XFont f = XWidget_font((XWidget*)&self->m_title);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_title, &f);
        XClassDeinit(&f);
    }
    XLabel_setText_2(&self->m_title, title ? title : "");
    XLabel_setTextPixelSize(&self->m_title, 17);
    XLabel_setAlignment(&self->m_title,
                        XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setFixedHeight((XWidget*)&self->m_title, 26);
    XWidget_show((XWidget*)&self->m_title);

    /* 表格（对标 QTableWidget）：行列 + 表头标签。列宽 Stretch 模式
     * （对标 horizontalHeader()->setSectionResizeMode(
     * QHeaderView::Stretch)）：各列均分内容区宽、随窗口拉伸——表格
     * 几何铺满（布局器）+ 列宽铺满（Stretch 段）合成「贴满窗口」。 */
    XTableWidget_init(&self->m_table, &self->m_base, 0);
    {
        XFont tf = XWidget_font((XWidget*)&self->m_table);
        self->m_fontBasePx = XFont_pixelSize(&tf);
        XClassDeinit(&tf);
    }
    if (columnCount > 0)
        XTableWidget_setColumnCount(&self->m_table, columnCount);
    if (headers && columnCount > 0)
        XTableWidget_setHorizontalHeaderLabels(&self->m_table, headers,
                                               columnCount);
    if (rowCount > 0)
        XTableWidget_setRowCount(&self->m_table, rowCount);
    self->m_rowCount = rowCount;
    self->m_rowBaseH = XTableView_rowHeight(&self->m_table.m_base, 0);
    /* 对齐 Qt：XHeaderView_setSectionResizeMode 本就是
     * horizontalHeader()->setSectionResizeMode 的转发，直接调它。 */
    {
        XHeaderView* hh = XTableView_horizontalHeader(&self->m_table.m_base);
        if (hh) XHeaderView_setSectionResizeMode(hh,
                                                 XHeaderViewResizeMode_Stretch);
    }
    XWidget_show((XWidget*)&self->m_table);

    /* 图表视图占位（自绘提示；与表格同格二选一显隐——显隐已对标 Qt
     * 触发父布局失效，无需手工重排）。 */
    memset(&self->m_chart, 0, sizeof(self->m_chart));
    XWidget_init(&self->m_chart.m_base, &self->m_base, 0);
    XClassGetVtable(&self->m_chart) = HsfChartPlaceholder_class_init();
    self->m_chart.m_text = "图表视图待数据接入"; /* 图表视图待数据接入 */
    HsfTheme_applyDefaultFont(&self->m_chart.m_base);
    /* 控制工具条（时间/通道/筛选/导出/视图切换；动作回 pageBarAction）。 */
    self->m_bar = HsfDataBar_create(&self->m_base);
    if (self->m_bar)
        HsfDataBar_setActionCallback(self->m_bar, pageBarAction, self);

    /* 页内布局：标题行 + 工具条 + 表格（stretch=1 吃满剩余）。 */
    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 24, 24, 24, 24);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);
    if (self->m_bar)
        XBoxLayout_addWidget(&self->m_layout, self->m_bar);
    XBoxLayout_addWidgetEx(&self->m_layout, (XWidget*)&self->m_table, 1, 0);
    XBoxLayout_addWidgetEx(&self->m_layout, &self->m_chart.m_base, 1, 0);
    /* 图表占位初始隐藏（布局 empty 条目按 hiddenWidget 跳过，Qt 同款）。 */
    XWidget_hide(&self->m_chart.m_base);

    /* 兼容自测钩子（env XHSF_DATA_AUTOTEST=chart）：翻钮（用户点「图表」
       的按钮态）+ 直呼动作回调（显隐切换）——两段合起来与真实点击
       等价（真实点击经 barActionSlot 一体完成；钩子无法注入点击，
       直呼须两段齐，漏翻钮会留下「图表态下表格钮仍选中」的假象）。 */
    if (XSystem_environment("XHSF_DATA_AUTOTEST")) {
        if (self->m_bar)
            HsfDataBar_setViewMode(self->m_bar, 1);
        pageBarAction(self, HsfDataBarAction_ViewChart);
    }

    XWidget_show(&self->m_base);
    return &self->m_base;
}

void HsfDataTablePage_applyFont(XWidget* page, int pct)
{
    HsfDataTablePage* self = (HsfDataTablePage*)page;
    XFont src;
    XFont f;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_title, (17 * pct + 50) / 100);
    if (self->m_fontBasePx <= 0) return;
    src = XWidget_font((XWidget*)&self->m_table);
    XFont_init(&f);
    XClassCopy(&f, &src);
    XClassDeinit(&src);
    XFont_setPixelSize(&f, (self->m_fontBasePx * pct + 50) / 100);
    XWidget_setFont((XWidget*)&self->m_table, &f);
    XClassDeinit(&f);
    /* 行高随字号同步（否则放大后文字被行高裁掉）。 */
    if (self->m_rowBaseH > 0) {
        const int rh = (self->m_rowBaseH * pct + 50) / 100;
        int r;
        for (r = 0; r < self->m_rowCount; ++r)
            XTableView_setRowHeight(&self->m_table.m_base, r, rh);
    }
    if (self->m_bar) HsfDataBar_applyFont(self->m_bar, pct);
}

void HsfDataTablePage_adapt(XWidget* page, bool compact)
{
    HsfDataTablePage* self = (HsfDataTablePage*)page;
    if (!self) return;
    /* 竖屏/窄窗：页边距 24→12 腾内容宽（表格页唯一可动几何参数；
     * 表头行高/列宽维持 Qt Interactive 缺省）。工具条同步收说明标签。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 12 : 24, compact ? 12 : 24,
                               compact ? 12 : 24, compact ? 12 : 24);
    if (self->m_bar)
        HsfDataBar_adapt(self->m_bar, compact);
    XLayout_activate((XLayout*)&self->m_layout);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON && XLAYOUT_ON && XLAYOUT_BOX_ON */
