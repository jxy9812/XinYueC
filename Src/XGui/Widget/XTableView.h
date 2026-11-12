#ifndef XTABLEVIEW_H
#define XTABLEVIEW_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemView.h"
#include "XHeaderView.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

XCLASS_DEFINE_BEGING(XTableView)
/* [P0-33 2026-11-12] 排序本体虚槽（对标 QTableView::sortByColumn 内的
 * model->sort 步骤）：缺省实现排模型（VXTableView_sortByColumn），
 * XTableWidget 覆写为条目矩阵排序（XTableWidget_sortItems——条目矩阵
 * 与内建模型是两份存储，只排模型会让 m_cells 与模型错位）。
 * @note 本类**新增**了槽位 ⇒ 收尾必须用 XCLASS_DEFINE_END（END_SIZE 取
 *       末枚举 +1）；XCLASS_DEFINE_EXTEND_END 只适用于「不新增槽位」的
 *       派生类（它把 END_SIZE 直接等于父类槽数，会让新槽落在表容量之外）。 */
XCLASS_DEFINE_ENUM(XTableView, SortByColumn) =
    XCLASS_VTABLE_GET_SIZE(XAbstractItemView),
XCLASS_DEFINE_END(XTableView)

/**
 * @brief 网格线风格（对标 Qt::PenStyle 简化；数值对齐 XPainterPenStyle）。
 */
typedef enum XTableViewGridStyle
{
    XTABLEVIEW_GRID_NO = 0,        /**< 无网格（等价 showGrid(false)）。 */
    XTABLEVIEW_GRID_SOLID = 1,     /**< 实线网格（默认，对标 Qt::SolidLine）。 */
    XTABLEVIEW_GRID_DASH = 2       /**< 虚线网格（对标 Qt::DashLine）。 */
} XTableViewGridStyle;

/**
 * @brief 单元格合并区间（对标 QTableView::setSpan 的 Span 存储）。
 */
typedef struct XTableViewSpan
{
    int row;        /**< 原点行号。 */
    int column;     /**< 原点列号。 */
    int16_t rowSpan;    /**< 跨行数（>=1；单元格量级 ≤32767）。 */
    int16_t columnSpan; /**< 跨列数（>=1；列量级 ≤32767）。 */
} XTableViewSpan;

/**
 * @brief 表格视图（对标 Qt 6.8 QTableView）。
 *
 *        承载列宽/行高、网格显示、排序开关与整行/整列选择等表格层
 *        属性；条目（单元格）模型由派生类（XTableWidget）提供。
 *        平铺模型下隐藏行/列采用平行状态表（参照 XTreeView 列状态
 *        数组模式），在访问入口、copy/move、deinit 全路径同步。
 */
typedef struct XTableView
{
    XAbstractItemView m_base;      /**< 基类成员；必须是第一个。 */
    /* 指针组（8B 对齐）。 */
    int* m_colWidths;              /**< 各列宽（像素，下标=列号）；对象拥有。 */
    int16_t* m_rowHeights;         /**< 逐行行高覆写表（像素；0=未覆写回落缺省）；对象拥有。 */
    bool* m_rowHidden;             /**< 行隐藏状态表（平行数组；下标=行号）。 */
    bool* m_colHidden;             /**< 列隐藏状态表（平行数组；下标=列号）。 */
    XHeaderView* m_horizontalHeader; /**< 水平表头对象（视图内建拥有；setHorizontalHeader
                                      *   接管新表头并回收旧内建表头，对齐 Qt）。 */
    XHeaderView* m_verticalHeader;   /**< 垂直表头对象（所有权口径同上）。 */
    XTableViewSpan* m_spans;       /**< 合并区间表（平行数组；下标=序号）。 */
    /* [内存治理 2026-10-11] 窄整型：容量/计数与排序索引收 int16_t
       （sortColumn 有 -1 哨兵，保留符号）；行高 px 收 uint16_t；
       网格风格 0..2 收 uint8_t；gridVisible 是 0/1 镜像位并入位域。 */
    int16_t m_colCapacity;         /**< 列宽表容量。 */
    int16_t m_rowHeightCount;      /**< 逐行行高表长度。 */
    int16_t m_rowHeightCapacity;   /**< 逐行行高表容量。 */
    int16_t m_rowHiddenCount;      /**< 行隐藏状态表长度。 */
    int16_t m_colHiddenCount;      /**< 列隐藏状态表长度。 */
    int16_t m_sortColumn;          /**< 最近一次排序列（-1=未排序）。 */
    int16_t m_sortOrder;           /**< 最近一次排序序：0 升/1 降。 */
    int16_t m_spanCount;           /**< 合并区间数。 */
    int16_t m_spanCapacity;        /**< 合并区间容量。 */
    uint16_t m_rowHeight;          /**< 统一行高（像素，默认 24）。 */
    /* [内存治理 2026-10-11] 标志位字：3 个开关 + 网格镜像位合并 1 字。 */
    uint32_t m_wordWrap : 1;       /**< 单元格文本自动换行（对标 wordWrap；缺省 **true**，
                                    *   Qt 6.8 QTableViewPrivate::wrapItemText 初值 true）。 */
    uint32_t m_cornerButton : 1;   /**< 左上角按钮（对标 cornerButtonEnabled；默认 true）。 */
    uint32_t m_sortingEnabled : 1; /**< 允许排序（对标 sortingEnabled）。 */
    uint32_t m_gridVisible : 1;    /**< 网格镜像位；恒等于 m_gridStyle != NoGrid。 */
    uint32_t m_tableViewFlagsUnused : 28; /**< 保留位。 */
    uint8_t m_gridStyle;           /**< 网格风格（XTableViewGridStyle 0..2；默认实线）。 */
} XTableView;

XVtable* XTableView_class_init(void);

/**
 * @brief 初始化嵌入式表格视图。
 *
 * @param self 目标视图指针，不能为空。
 * @param parent 父控件（可空）。
 * @param flags 控件标志位。
 * @return 无返回值。
 */
void XTableView_init(XTableView* self, XWidget* parent, XWidgetFlags flags);

/**
 * @brief 堆上创建表格视图。
 *
 * @param memory 内存类型。
 * @param parent 父控件（可空）。
 * @param flags 控件标志位。
 * @return 视图指针；分配失败返回 NULL。
 */
XTableView* XTableView_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags);
#define XTableView_create(parent, flags) \
    XTableView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))

/** @brief 析构入口（查表分派父类析构）。 */

/** @brief 删除堆上视图（查表分派析构并释放内存）。 */

/* ==================== 尺寸（对标 QTableView） ==================== */

/** @brief 设置列宽。 @param self 目标视图指针。 @param column 列号。 @param width 宽度（像素）。 @return 无返回值。 */
void XTableView_setColumnWidth(XTableView* self, int column, int width);
/** @brief 查询列宽。 @param self 目标视图指针。 @param column 列号。 @return 宽度（像素）；越界返回默认宽。 */
int XTableView_columnWidth(const XTableView* self, int column);
/** @brief 设置指定行的行高（对齐 QTableView::setRowHeight）。
 * @param self 目标视图指针；为 NULL 无操作。
 * @param row 行号；< 0 无操作。
 * @param height 行高（像素）；< 0 无操作。>0 时按表头最小区间尺寸钳制
 *        （Qt 6.8.0 实测 setRowHeight(0,10)⇒21）；**0 在本库按"未覆写"
 *        哨兵处理**（逐行覆写表以 0 表示无覆写，与 Qt 允许 0 高行不同，
 *        见 XTableView_rowHeight 注释）。
 * @return 无返回值。
 * @warning 只影响 row 这一行；其余行继续使用缺省行高。 */
void XTableView_setRowHeight(XTableView* self, int row, int height);
/** @brief 查询指定行的行高（对齐 QTableView::rowHeight）。
 * @param self 目标视图指针；可为 NULL。
 * @param row 行号。
 * @return 行高（像素）；**隐藏行返回 0**（Qt QTableView::rowHeight =
 *         verticalHeader->sectionSize，隐藏段为 0；本机 Qt 6.8.0 实测），
 *         未覆写时回落缺省行高。
 */
int XTableView_rowHeight(const XTableView* self, int row);

/* ==================== 按内容调整（对标 QTableView::resizeColumn(s/Row(s))ToContents） ==================== */

/**
 * @brief 按内容调整指定列宽（对标 resizeColumnToContents）。
 *
 * @param self 目标视图指针。
 * @param column 列号（无模型或越界忽略）。
 * @return 无返回值。
 *
 * @note 简化测算：取该列全部可见行单元格文本的 XPainter_textWidth
 *       最大值加左右边距（各 4 像素）作为新列宽；不含表头文本、
 *       自动换行与最小区间尺寸钳制（隐藏行不参与测算，对标 Qt）。
 */
void XTableView_resizeColumnToContents(XTableView* self, int column);
/** @brief 按内容调整全部列宽（对标 resizeColumnsToContents；逐列调用
 *         resizeColumnToContents）。 @param self 目标视图指针。 @return 无返回值。 */
void XTableView_resizeColumnsToContents(XTableView* self);
/**
 * @brief 按内容调整行高（对标 resizeRowToContents）。
 *
 * @param self 目标视图指针。
 * @param row 行号（无模型或越界忽略）。
 * @return 无返回值。
 *
 * @note 简化测算：行高 = XPainter_textHeight + 上下边距（各 2 像素）。
 *       只影响 row 这一行（逐行覆写），其余行不变。
 */
void XTableView_resizeRowToContents(XTableView* self, int row);
/** @brief 按内容调整全部行高（对齐 QTableView::resizeRowsToContents）。
 * @param self 目标视图指针；为 NULL 无操作。 @return 无返回值。
 * @note 逐行调用 XTableView_resizeRowToContents。 */
void XTableView_resizeRowsToContents(XTableView* self);

/* ==================== 网格（对标 QTableView::showGrid/gridStyle） ==================== */

/** @brief 设置网格显示（等价 gridStyle != NoGrid 的开关形式，两者联动：
 *         开时若无风格则回落实线，关时风格置 NoGrid）。
 * @param self 目标视图指针。 @param show true 显示。 @return 无返回值。 */
void XTableView_setShowGrid(XTableView* self, bool show);
/** @brief 查询网格显示（等价 gridStyle() != NoGrid）。 @param self 目标视图指针。 @return 显示返回 true。 */
bool XTableView_showGrid(const XTableView* self);
/** @brief 设置网格线风格（对标 setGridStyle；仅接受 XTableViewGridStyle 枚举值）。
 * @param self 目标视图指针。 @param style 网格风格。 @return 无返回值。 */
void XTableView_setGridStyle(XTableView* self, int style);
/** @brief 查询网格线风格（对标 gridStyle）。 @param self 目标视图指针。 @return 网格风格（XTableViewGridStyle）。 */
int XTableView_gridStyle(const XTableView* self);

/* ==================== 文本与角按钮（对标 QTableView） ==================== */

/** @brief 设置单元格文本自动换行开关（对标 setWordWrap；平铺绘制当前
 *         恒为单行文本，标志位持久化供派生类使用）。
 * @param self 目标视图指针。 @param wrap true 自动换行。 @return 无返回值。 */
void XTableView_setWordWrap(XTableView* self, bool wrap);
/** @brief 查询自动换行开关（对标 wordWrap）。 @param self 目标视图指针。 @return 启用返回 true。 */
bool XTableView_wordWrap(const XTableView* self);
/** @brief 设置左上角按钮启用开关（对标 setCornerButtonEnabled）。
 * @param self 目标视图指针。 @param enable true 启用。 @return 无返回值。 */
void XTableView_setCornerButtonEnabled(XTableView* self, bool enable);
/** @brief 查询左上角按钮启用开关（对标 isCornerButtonEnabled）。
 * @param self 目标视图指针。 @return 启用返回 true。 */
bool XTableView_isCornerButtonEnabled(const XTableView* self);

/* ==================== 位置反查（对标 QTableView::rowAt/columnAt） ==================== */

/** @brief 位置反查行号（对标 rowAt；y 为控件坐标，自动扣除表头区，
 *         隐藏行高度按 0 跳过；无命中返回 -1）。
 * @param self 目标视图指针。 @param y 纵向坐标（像素）。 @return 行号；越界/无模型返回 -1。 */
int XTableView_rowAt(const XTableView* self, int y);
/** @brief 位置反查列号（对标 columnAt；x 为控件坐标，隐藏列宽度按 0
 *         跳过；无命中返回 -1）。
 * @param self 目标视图指针。 @param x 横向坐标（像素）。 @return 列号；越界/无模型返回 -1。 */
int XTableView_columnAt(const XTableView* self, int x);

/**
 * @brief 视图坐标 → 单元格（对齐 `QTableView::indexAt`）。
 *
 * @param self   目标视图；为 NULL 时两个输出均置 -1。
 * @param x      横向坐标（像素，视口坐标）。
 * @param y      纵向坐标（像素，视口坐标）。
 * @param outRow 行号输出；可为 NULL。未命中写 -1。
 * @param outCol 列号输出；可为 NULL。未命中写 -1。
 * @return 命中返回 true；未命中返回 false。
 * @note   对齐 Qt：命中落在合并区间的被覆盖格时输出吸附到区间原点。
 */
bool XTableView_indexAt(const XTableView* self, int x, int y,
                        int* outRow, int* outCol);

/* ==================== 行/列视口位置（对标 rowViewportPosition/columnViewportPosition） ==================== */

/**
 * @brief 查询行的视口起点纵坐标（对标 rowViewportPosition）。
 *
 * @param self 目标视图指针。
 * @param row 行号。
 * @return 纵坐标（像素）= 顶部表头区高（内嵌自绘表头）+ 该行之前各
 *         可见行高累计；隐藏行占高 0 跳过。无模型、越界或 self 为空
 *         返回 -1。
 *
 * @note 与 rowAt 同一坐标系（控件坐标，当前无滚动偏移），二者互逆：
 *       rowAt(rowViewportPosition(row)) == row。目标行为隐藏行时返回
 *       其前序可见行的累计终点（同下一可见行起点）。
 */
int XTableView_rowViewportPosition(const XTableView* self, int row);
/**
 * @brief 查询列的视口起点横坐标（对标 columnViewportPosition）。
 *
 * @param self 目标视图指针。
 * @param column 列号。
 * @return 横坐标（像素）= 左侧起点 0（本实现无左侧垂直表头区）+ 该列
 *         之前各可见列宽累计；隐藏列占宽 0 跳过。无模型、越界或
 *         self 为空返回 -1。
 *
 * @note 与 columnAt 同一坐标系（控件坐标，当前无滚动偏移），二者互逆：
 *       columnAt(columnViewportPosition(column)) == column。目标列为
 *       隐藏列时返回其前序可见列的累计终点（同下一可见列起点）。
 */
int XTableView_columnViewportPosition(const XTableView* self, int column);

/* ==================== 滚动与偏移（对标 QTableView::scrollTo/horizontalOffset/verticalOffset） ==================== */

/**
 * @brief 滚动使指定单元格可见（对标 QAbstractItemView::scrollTo 的
 *        QTableView 公共面；EnsureVisible 语义）。
 *
 * @param self 目标视图指针。
 * @param row 行号。
 * @param column 列号。
 * @return 无返回值。self 为空或坐标非法为无操作。
 * @note 转发基类 XAbstractItemView_scrollTo（Qt QTableView::scrollTo
 *       → 基类同签名的公共面形态；Hint 重载经 scrollToHint 提供）。
 */
void XTableView_scrollTo(XTableView* self, int row, int column, int hint);
/**
 * @brief 查询水平滚动偏移（对标 QTableView::horizontalOffset = 水平
 *        滚动条值）。
 *
 * @param self 目标视图指针。
 * @return 水平滚动条当前值；无滚动条或 self 为空返回 0。
 */
int XTableView_horizontalOffset(const XTableView* self);
/**
 * @brief 查询垂直滚动偏移（对标 QTableView::verticalOffset = 垂直
 *        滚动条值）。
 *
 * @param self 目标视图指针。
 * @return 垂直滚动条当前值；无滚动条或 self 为空返回 0。
 */
int XTableView_verticalOffset(const XTableView* self);

/* ==================== 视觉矩形（对标 QTableView::visualRect） ==================== */

/**
 * @brief 查询单元格（行 row、列 column）的视觉矩形（对标 visualRect）。
 *
 * @param self 目标视图指针。
 * @param row 行号。
 * @param column 列号。
 * @return 控件坐标下的单元格矩形：x = columnViewportPosition(column)、
 *         y = rowViewportPosition(row)、宽 = columnWidth(column)、
 *         高 = rowHeight(row)；self 为空、无模型、行/列越界或行/列
 *         隐藏返回空矩形 {0,0,0,0}。
 *
 * @note 与 rowAt/columnAt 同一坐标系（控件坐标，当前无滚动偏移；
 *       y 向含顶部内嵌表头区高度、x 向自 0 起）；隐藏行/列占高/宽 0
 *       跳过，隐藏单元格不可见故返回空矩形（对标 Qt 不可见条目返回
 *       空矩形）。
 */
XRect XTableView_visualRect(const XTableView* self, int row, int column);

/* ==================== 行/列隐藏（对标 QTableView） ==================== */

/** @brief 设置行隐藏（对标 setRowHidden；状态表按需扩容，默认不隐藏）。
 * @param self 目标视图指针。 @param row 行号。 @param hide true 隐藏。 @return 无返回值。
 * @note 与 Qt 一致同步转发 verticalHeader->setSectionHidden（垂直表头段
 *       存在时；qtableview.cpp:2680-2686），隐藏行段尺寸按 0 计。 */
void XTableView_setRowHidden(XTableView* self, int row, bool hide);
/** @brief 查询行隐藏（对标 isRowHidden；越界或未建表返回 false）。
 * @param self 目标视图指针。 @param row 行号。 @return 隐藏返回 true。 */
bool XTableView_isRowHidden(const XTableView* self, int row);
/** @brief 设置列隐藏（对标 setColumnHidden；状态表按需扩容，默认不隐藏）。
 * @param self 目标视图指针。 @param column 列号。 @param hide true 隐藏。 @return 无返回值。
 * @note 与 Qt 一致同步转发 horizontalHeader->setSectionHidden（水平表头段
 *       存在时；qtableview.cpp:2705-2711）⇒ columnWidth/表头段位置按隐藏列
 *       计 0（Qt QHeaderView::sectionSize 对隐藏段返回 0）。 */
void XTableView_setColumnHidden(XTableView* self, int column, bool hide);
/** @brief 查询列隐藏（对标 isColumnHidden；越界或未建表返回 false）。
 * @param self 目标视图指针。 @param column 列号。 @return 隐藏返回 true。 */
bool XTableView_isColumnHidden(const XTableView* self, int column);
/** @brief 隐藏行便捷接口（对标 hideRow；等价 setRowHidden(row, true)）。
 * @param self 目标视图指针。 @param row 行号。 @return 无返回值。 */
void XTableView_hideRow(XTableView* self, int row);
/** @brief 显示行便捷接口（对标 showRow；等价 setRowHidden(row, false)）。
 * @param self 目标视图指针。 @param row 行号。 @return 无返回值。 */
void XTableView_showRow(XTableView* self, int row);
/** @brief 隐藏列便捷接口（对标 hideColumn；等价 setColumnHidden(column, true)）。
 * @param self 目标视图指针。 @param column 列号。 @return 无返回值。 */
void XTableView_hideColumn(XTableView* self, int column);
/** @brief 显示列便捷接口（对标 showColumn；等价 setColumnHidden(column, false)）。
 * @param self 目标视图指针。 @param column 列号。 @return 无返回值。 */
void XTableView_showColumn(XTableView* self, int column);

/* ==================== 跨行/列合并（对标 QTableView::setSpan/spanAt/clearSpans） ==================== */

/**
 * @brief 设置单元格合并区间（对标 setSpan）。
 * @param self 目标视图指针。 @param row 原点行号。 @param column 原点列号。
 * @param rowSpan 跨行数。 @param columnSpan 跨列数。
 * @return 无返回值（区间变化时请求一次重绘）。
 * @note 对标 Qt 语义：rowSpan/columnSpan 均 <=1 时移除该原点已有区间，
 *       任一 <=0 忽略；同原点重复设置替换旧值；区间可超出当前模型
 *       范围（绘制按可见范围收敛，对标 Qt 区间与模型尺寸解耦）。
 */
void XTableView_setSpan(XTableView* self, int row, int column,
                        int rowSpan, int columnSpan);
/** @brief 查询单元格所属合并区间的跨行数（对齐 `QTableView::rowSpan`）。
 * @param self 目标视图；可为 NULL。
 * @param row 行号。
 * @param column 列号。
 * @return 跨行数；未合并、参数越界或 self 为 NULL 返回 1。 */
int XTableView_rowSpan(const XTableView* self, int row, int column);

/** @brief 查询单元格所属合并区间的跨列数（对齐 `QTableView::columnSpan`）。
 * @param self 目标视图；可为 NULL。
 * @param row 行号。
 * @param column 列号。
 * @return 跨列数；未合并、参数越界或 self 为 NULL 返回 1。 */
int XTableView_columnSpan(const XTableView* self, int row, int column);

/** @brief 清除全部跨行/跨列合并（对标 clearSpans）。
 * @param self 目标视图指针。 @return 无返回值（有区间时请求一次重绘）。
 * @note 仅清空区间表（容量保留），对标 Qt 语义。 */
void XTableView_clearSpans(XTableView* self);

/* ==================== 合并区间随行列增删迁移（P0-32） ====================
 * [为什么要有] Qt 在 QTableView::setModel 里把模型的 rowsInserted/
 * columnsInserted/rowsRemoved/columnsRemoved 接到 span 维护槽
 * （qtableview.cpp:1267-1280 → QTableViewPrivate::updateSpanInsertedRows
 * 等 → QSpanCollection::updateInsertedRows/…，qtableview.cpp:185-437）；
 * 本库此前 span 只在 setSpan/clearSpans 增删 ⇒ 行/列增删后合并区间
 * 不迁移、合并单元格错位（XTableWidget 的 m_cells 平移与合并区间脱节）。
 * 本库便利类按调用点迁移（XTableWidget 的增删入口显式调用），维护
 * 模型信号接线收敛为这两个入口。 */

/**
 * @brief 行/列插入后迁移合并区间（对标 QSpanCollection::updateInsertedRows/
 *        updateInsertedColumns，qtableview.cpp:185-249）。
 *
 * @param self 目标视图指针；为 NULL 或无区间为空操作。
 * @param rows true=行轴迁移（行插入），false=列轴迁移（列插入）。
 * @param start 插入起始行/列号（>=0）。
 * @param end 插入区间末行/列号（>=start；delta = end-start+1）。
 * @return 无返回值。
 * @note 迁移规则（Qt 同款）：区间整体在插入位之前（底边 < start）不动；
 *       跨越/命中插入位的区间底边推进 delta（原点在插入位之前的区间
 *       被「拉长」覆盖新行/列）；原点在插入位及之后的区间整体平移
 *       delta。纯状态迁移，不触发重绘（调用方自行刷新）。
 */
void XTableView_shiftSpansForInsert(XTableView* self, bool rows, int start,
                                    int end);

/**
 * @brief 行/列删除后迁移合并区间（对标 QSpanCollection::updateRemovedRows/
 *        updateRemovedColumns，qtableview.cpp:310-437）。
 *
 * @param self 目标视图指针；为 NULL 或无区间为空操作。
 * @param rows true=行轴迁移（行删除），false=列轴迁移（列删除）。
 * @param start 删除起始行/列号（>=0）。
 * @param end 删除区间末行/列号（>=start；delta = end-start+1）。
 * @return 无返回值。
 * @note 迁移规则（Qt 同款）：区间整体在删除位之前不动；整个落在删除
 *       区间内的区间删除；起点在删除位之前、终点落在区间内的区间截尾
 *       （底边 = start-1）；横跨删除区间的区间跳过删除段（跨度减
 *       delta）；起点在删除位之后的区间按 delta 上移/左移。区间缩到
 *       1×1 时一并删除（Qt 同）。纯状态迁移，不触发重绘。
 */
void XTableView_shiftSpansForDelete(XTableView* self, bool rows, int start,
                                    int end);

/* ==================== 表头对象挂接（对标 QTableView::setHorizontalHeader/verticalHeader） ==================== */

/**
 * @brief 挂接水平表头对象（对标 setHorizontalHeader）。
 *
 * @param self 目标视图指针。
 * @param header 表头对象；方向必须为水平（0），否则忽略（本库的安全扩展；
 *               Qt 不校验方向）。
 * @return 无返回值。
 *
 * @note **Qt 所有权语义（gap 166，2026-11-11 对齐）**：非 NULL 新表头
 *       `setParent(this)` 成为本视图子控件、`setFirstSectionMovable(true)`，
 *       并把 sectionResized/sectionMoved/sectionCountChanged/
 *       geometriesChanged 四条信号重接到视图（qtableview.cpp:1366-1399）；
 *       被替换的旧表头**以本视图为父**时删除（视图拥有的内建表头随替换
 *       回收）。传 NULL 是本库扩展（Qt 头指针不可为空、无对应分支）：
 *       只解挂不析构，调用方仍可自行释放。
 * @note 表头对象承载段尺寸/排序/隐藏状态，视图绘制仍为内嵌自绘
 *       （顶部 XTV_HEADER_H 常量表头区）；段尺寸以表头为事实源、视图
 *       数组为镜像（sectionResized 回写）。
 */
void XTableView_setHorizontalHeader(XTableView* self, XHeaderView* header);
/** @brief 查询水平表头对象（对标 horizontalHeader）。
 * @param self 目标视图指针。 @return 借用指针；未挂接或 self 为空返回 NULL。 */
XHeaderView* XTableView_horizontalHeader(const XTableView* self);
/**
 * @brief 挂接垂直表头对象（对标 setVerticalHeader）。
 *
 * @param self 目标视图指针。
 * @param header 表头对象；方向必须为垂直（1），否则忽略。
 * @return 无返回值。
 *
 * @note 所有权/接线口径同 setHorizontalHeader（qtableview.cpp:1405-1438）：
 *       新表头 setParent(this)，旧表头（父为本视图时）删除，四条几何信号
 *       重接（sectionResized 回写逐行行高覆写表）。传 NULL 为解挂扩展。
 */
void XTableView_setVerticalHeader(XTableView* self, XHeaderView* header);
/** @brief 查询垂直表头对象（对标 verticalHeader）。
 * @param self 目标视图指针。 @return 借用指针；未挂接或 self 为空返回 NULL。 */
XHeaderView* XTableView_verticalHeader(const XTableView* self);

/* ==================== 排序（对标 QTableView） ==================== */

/**
 * @brief 设置排序开关（对标 setSortingEnabled，Qt 四步，qtableview.cpp:
 *        2738-2758）。
 *
 * @param self 目标视图指针。 @param enable true 启用。
 * @return 无返回值。
 * @note Qt 四步：① `horizontalHeader()->setSortIndicatorShown(enable)`；
 *       ② 断开既有动态表头接线；③ enable 时**先**按当前指示段调
 *       sortByColumn 立即排序（须在接线/置位之前，否则不会对模型排序，
 *       Qt 源注释原文），再接 `sortIndicatorChanged → 排序虚槽`；
 *       ④ disable 时接 `sectionPressed/sectionEntered → selectColumn`
 *       （点表头选列，qtableview.cpp:2751-2756）；最后写 sortingEnabled。
 */
void XTableView_setSortingEnabled(XTableView* self, bool enable);
/** @brief 查询排序开关。 @param self 目标视图指针。 @return 启用返回 true。 */
bool XTableView_isSortingEnabled(const XTableView* self);
/**
 * @brief 按列排序（对标 sortByColumn，qtableview.cpp:3375-3384）。
 *
 * @param self 目标视图指针。 @param column 列号（-1 恢复自然序；<-1 忽略）。
 * @param order 0 升序/1 降序。
 * @return 无返回值。
 * @note 先 `header->setSortIndicator(column, order)`；排序未使能或指示段
 *       与入参同段同序（setSortIndicator 同值早退不会发
 *       sortIndicatorChanged）时**立即**经 EXTableView_SortByColumn 虚槽
 *       排序——缺省排模型，XTableWidget 覆写为条目矩阵排序
 *       （XTableWidget_sortItems，两份存储必须一起排）。
 */
void XTableView_sortByColumn(XTableView* self, int column, int order);

/* ==================== 行/列选择（对标 QTableView::selectRow/selectColumn） ==================== */

/**
 * @brief 选择整行（对标 selectRow；qtableview.cpp:3247-3251 经
 *        d->selectRow(row,true) 以 Rows 命令展开）。
 *
 * @param self 目标视图指针。 @param row 行号（<0 忽略）。
 * @return 无返回值。
 * @note 保留「写入当前格」一步（当前列随行定格），再以
 *       ClearAndSelect|Rows 命令经选择模型展开整行（P0-74：此前只写
 *       当前格，与 Qt 的整行语义不符）。
 */
void XTableView_selectRow(XTableView* self, int row);
/**
 * @brief 选择整列（对标 selectColumn；qtableview.cpp:3259-3263 以
 *        Columns 命令展开）。
 *
 * @param self 目标视图指针。 @param column 列号（<0 忽略）。
 * @return 无返回值。
 * @note 保留「写入当前格」一步，再以 ClearAndSelect|Columns 命令展开
 *       整列（P0-75，与 P0-74 同面）。
 */
void XTableView_selectColumn(XTableView* self, int column);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XTABLEVIEW_H */
