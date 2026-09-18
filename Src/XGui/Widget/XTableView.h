#ifndef XTABLEVIEW_H
#define XTABLEVIEW_H
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
XCLASS_DEFINE_EXTEND_END(XTableView, XAbstractItemView)

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
    int rowSpan;    /**< 跨行数（>=1）。 */
    int columnSpan; /**< 跨列数（>=1）。 */
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
    int* m_colWidths;              /**< 各列宽（像素，下标=列号）。 */
    int m_colCapacity;             /**< 列容量。 */
    int m_rowHeight;               /**< 统一行高（像素，默认 24）。 */
    int m_gridStyle;               /**< 网格风格（XTableViewGridStyle；默认实线）。 */
    int m_gridVisible;             /**< 网格镜像位；恒等于 m_gridStyle != NoGrid。 */
    bool m_wordWrap;               /**< 单元格文本自动换行（对标 wordWrap；默认 false）。 */
    bool m_cornerButton;           /**< 左上角按钮（对标 cornerButtonEnabled；默认 true）。 */
    bool* m_rowHidden;             /**< 行隐藏状态表（平行数组；下标=行号）。 */
    int m_rowHiddenCount;          /**< 行隐藏状态表长度。 */
    bool* m_colHidden;             /**< 列隐藏状态表（平行数组；下标=列号）。 */
    int m_colHiddenCount;          /**< 列隐藏状态表长度。 */
    bool m_sortingEnabled;         /**< 允许排序（对标 sortingEnabled）。 */
    int m_sortColumn;              /**< 最近一次排序列。 */
    int m_sortOrder;               /**< 最近一次排序序：0 升/1 降。 */
    XHeaderView* m_horizontalHeader; /**< 水平表头对象（借用；默认 NULL）。 */
    XHeaderView* m_verticalHeader;   /**< 垂直表头对象（借用；默认 NULL）。 */
    XTableViewSpan* m_spans;       /**< 合并区间表（平行数组；下标=序号）。 */
    int m_spanCount;               /**< 合并区间数。 */
    int m_spanCapacity;            /**< 合并区间容量。 */
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
#define XTableView_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上视图（查表分派析构并释放内存）。 */
#define XTableView_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 尺寸（对标 QTableView） ==================== */

/** @brief 设置列宽。 @param self 目标视图指针。 @param column 列号。 @param width 宽度（像素）。 @return 无返回值。 */
void XTableView_setColumnWidth(XTableView* self, int column, int width);
/** @brief 查询列宽。 @param self 目标视图指针。 @param column 列号。 @return 宽度（像素）；越界返回默认宽。 */
int XTableView_columnWidth(const XTableView* self, int column);
/** @brief 设置统一行高。 @param self 目标视图指针。 @param height 高度（像素）。 @return 无返回值。 */
void XTableView_setRowHeight(XTableView* self, int height);
/** @brief 查询统一行高。 @param self 目标视图指针。 @return 高度（像素）。 */
int XTableView_rowHeight(const XTableView* self);

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
 *       本视图行高为统一承载（与 setRowHeight 同一存储），单行调整
 *       实际作用于全部行，row 仅用于范围校验。
 */
void XTableView_resizeRowToContents(XTableView* self, int row);
/** @brief 按内容调整全部行高（对标 resizeRowsToContents；逐行调用
 *         resizeRowToContents；统一行高承载下等价于一次测算）。
 * @param self 目标视图指针。 @return 无返回值。 */
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
 * @param self 目标视图指针。 @param row 行号。 @param hide true 隐藏。 @return 无返回值。 */
void XTableView_setRowHidden(XTableView* self, int row, bool hide);
/** @brief 查询行隐藏（对标 isRowHidden；越界或未建表返回 false）。
 * @param self 目标视图指针。 @param row 行号。 @return 隐藏返回 true。 */
bool XTableView_isRowHidden(const XTableView* self, int row);
/** @brief 设置列隐藏（对标 setColumnHidden；状态表按需扩容，默认不隐藏）。
 * @param self 目标视图指针。 @param column 列号。 @param hide true 隐藏。 @return 无返回值。 */
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

/* ==================== 跨行/列合并（对标 QTableView::setSpan/rowSpan/columnSpan/clearSpans） ==================== */

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
/**
 * @brief 查询单元格跨行数（对标 rowSpan）。
 * @param self 目标视图指针。 @param row 行号。 @param column 列号。
 * @return 包含 (row,column) 的合并区间跨行数；未合并返回 1。
 * @note 与 Qt 一致：被覆盖格（非原点）同样返回所属区间跨行数；
 *       参数非法或 self 为空返回 1。
 */
int XTableView_rowSpan(const XTableView* self, int row, int column);
/**
 * @brief 查询单元格跨列数（对标 columnSpan）。
 * @param self 目标视图指针。 @param row 行号。 @param column 列号。
 * @return 包含 (row,column) 的合并区间跨列数；未合并返回 1。
 * @note 与 Qt 一致：被覆盖格（非原点）同样返回所属区间跨列数；
 *       参数非法或 self 为空返回 1。
 */
int XTableView_columnSpan(const XTableView* self, int row, int column);
/** @brief 清除全部跨行/跨列合并（对标 clearSpans）。
 * @param self 目标视图指针。 @return 无返回值（有区间时请求一次重绘）。
 * @note 仅清空区间表（容量保留），对标 Qt 语义。 */
void XTableView_clearSpans(XTableView* self);

/* ==================== 表头对象挂接（对标 QTableView::setHorizontalHeader/verticalHeader） ==================== */

/**
 * @brief 挂接水平表头对象（对标 setHorizontalHeader）。
 *
 * @param self 目标视图指针。
 * @param header 表头对象借用指针；可为 NULL 清除挂接；方向非水平
 *               （orientation != 0）的表头忽略。
 * @return 无返回值（挂接状态变化时请求一次重绘）。
 *
 * @note 借用承载：不转移所有权、不析构，生命周期归调用方；copy 共享
 *       指针、move 转移指针。当前绘制仍为内嵌自绘（顶部 XTV_HEADER_H
 *       常量表头区），表头对象仅用于状态查询与挂接承载，不参与本视图
 *       的绘制与几何计算。
 */
void XTableView_setHorizontalHeader(XTableView* self, XHeaderView* header);
/** @brief 查询水平表头对象（对标 horizontalHeader）。
 * @param self 目标视图指针。 @return 借用指针；未挂接或 self 为空返回 NULL。 */
XHeaderView* XTableView_horizontalHeader(const XTableView* self);
/**
 * @brief 挂接垂直表头对象（对标 setVerticalHeader）。
 *
 * @param self 目标视图指针。
 * @param header 表头对象借用指针；可为 NULL 清除挂接；方向非垂直
 *               （orientation != 1）的表头忽略。
 * @return 无返回值（挂接状态变化时请求一次重绘）。
 *
 * @note 借用承载：不转移所有权、不析构，生命周期归调用方；copy 共享
 *       指针、move 转移指针。当前绘制仍为内嵌自绘（无左侧垂直表头区），
 *       表头对象仅用于状态查询与挂接承载，不参与本视图的绘制与几何
 *       计算。
 */
void XTableView_setVerticalHeader(XTableView* self, XHeaderView* header);
/** @brief 查询垂直表头对象（对标 verticalHeader）。
 * @param self 目标视图指针。 @return 借用指针；未挂接或 self 为空返回 NULL。 */
XHeaderView* XTableView_verticalHeader(const XTableView* self);

/* ==================== 排序（对标 QTableView） ==================== */

/** @brief 设置排序开关。 @param self 目标视图指针。 @param enable true 启用。 @return 无返回值。 */
void XTableView_setSortingEnabled(XTableView* self, bool enable);
/** @brief 查询排序开关。 @param self 目标视图指针。 @return 启用返回 true。 */
bool XTableView_isSortingEnabled(const XTableView* self);
/** @brief 按列排序。 @param self 目标视图指针。 @param column 列号。 @param order 0 升序/1 降序。 @return 无返回值。 */
void XTableView_sortByColumn(XTableView* self, int column, int order);

/* ==================== 行/列选择 ==================== */

/** @brief 选择整行。 @param self 目标视图指针。 @param row 行号。 @return 无返回值。 */
void XTableView_selectRow(XTableView* self, int row);
/** @brief 选择整列。 @param self 目标视图指针。 @param column 列号。 @return 无返回值。 */
void XTableView_selectColumn(XTableView* self, int column);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#ifdef __cplusplus
}
#endif
#endif /* XTABLEVIEW_H */
