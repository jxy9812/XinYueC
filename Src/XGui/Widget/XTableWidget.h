/**
 * @file       XTableWidget.h
 * @brief      XTableWidget 表格控件（对标 Qt 6.8 QTableWidget/QTableView）。
 * @details    单元格字符串数据模型（对标 QTableWidgetItem 的文本/选中/
 *             前后景色/三态勾选子集）+ 表头标签 + 网格渲染 + 单元格
 *             选择/键盘导航/滚轮滚动 + cellClicked 等信号。继承
 *             XAbstractScrollArea：视口滚动经内部滚动条联动，滚动条
 *             策略/scrollContentsBy 语义与基类一致。
 * @note       模块总开关 XTABLEWIDGET_ON 定义于 XGuiConfig.h；=0 时
 *             裁剪整个 XTableWidget 公共 API。依赖 XWIDGET_ON、
 *             XABSTRACTSCROLLAREA_ON、XPALETTE_ON、XPAINTER_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTABLEWIDGET_H
#define XTABLEWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XAbstractScrollArea.h"
#include "XGuiConfig.h"

#if XTABLEWIDGET_ON

XCLASS_DEFINE_BEGING(XTableWidget)
XCLASS_DEFINE_EXTEND_END(XTableWidget, XAbstractScrollArea)

/**
 * @brief 单元格（对标 QTableWidgetItem 的文本/外观子集）。
 *
 *        直接内嵌存储于表格行数组；text 超长截断，前景/背景色为
 *        0 时回退调色板默认色。
 */
typedef struct XTableWidgetItem
{
    char     text[128];       /**< 显示文本（UTF-8，超长截断）。 */
    bool     selected;        /**< 是否选中。 */
    uint32_t foreground;      /**< 前景色 ARGB；0=使用调色板默认。 */
    uint32_t background;      /**< 背景色 ARGB；0=使用调色板默认。 */
    int      checkState;      /**< 勾选态：0 未选/1 部分/2 选中。 */
} XTableWidgetItem;

/**
 * @brief 表格控件（对标 QTableWidget）。
 *
 *        行×列二维单元格存储 + 行高/列宽 + 表头标签 + 当前单元格；
 *        继承 XAbstractScrollArea 后滚动条随内容自动按需显示。
 */
typedef struct XTableWidget
{
    XAbstractScrollArea m_base;   /**< 基类成员；必须是第一个。 */
    XTableWidgetItem** m_cells;   /**< 行指针数组（行×列单元格）。 */
    int m_rows;                   /**< 当前行数。 */
    int m_columns;                /**< 当前列数。 */
    int m_rowCapacity;            /**< 行指针数组容量。 */
    int m_colCapacity;            /**< 列容量（每行单元格数）。 */
    int* m_colWidths;             /**< 各列宽（像素，下标=列号）。 */
    char (*m_hHeaders)[64];       /**< 水平表头标签（下标=列号）。 */
    char (*m_vHeaders)[64];       /**< 垂直表头标签（下标=行号）。 */
    int m_currentRow;             /**< 当前单元格行；-1=无。 */
    int m_currentColumn;          /**< 当前单元格列；-1=无。 */
    int m_rowHeight;              /**< 统一行高（像素，默认 24）。 */
    int m_headerHeight;           /**< 水平表头高度（像素）。 */
    int m_headerWidth;            /**< 垂直表头宽度（像素）。 */
    bool m_sortingEnabled;        /**< 允许排序（对标 sortingEnabled）。 */
    bool m_gridVisible;           /**< 显示网格线（默认开）。 */
    int m_sortColumn;             /**< 最近一次排序列。 */
    int m_sortOrder;              /**< 最近一次排序序：0 升/1 降。 */
} XTableWidget;

XVtable* XTableWidget_class_init(void);

/**
 * @brief 初始化表格控件（栈对象/嵌入成员使用）。
 *
 *        初始 0 行 0 列、无当前单元格、行高 24、表头 24/40、网格
 *        可见；内部滚动条策略 AsNeeded。
 *
 * @param self   目标表格控件指针，不能为空。
 * @param parent 父控件；可为 NULL（顶层控件）。
 * @param flags  窗口标志位组合。
 * @return 无返回值。
 */
void XTableWidget_init(XTableWidget* self, XWidget* parent, XWidgetFlags flags);

/**
 * @brief 在堆上创建表格控件（对标 QTableWidget(parent) 构造）。
 *
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 等）。
 * @param parent 父控件；可为 NULL。
 * @param flags  窗口标志位组合。
 * @return 创建成功的表格控件指针；分配失败返回 NULL。
 */
XTableWidget* XTableWidget_create_ex(XMemoryType memory, XWidget* parent,
                                     XWidgetFlags flags);

/**
 * @brief 删除基类入口：经 XAbstractScrollArea 虚表析构（宏复用）。
 *
 * @param self 目标表格控件指针。
 * @return 无返回值。
 */
#define XTableWidget_delete_base(self) XAbstractScrollArea_delete_base((XAbstractScrollArea*)(self))

/* ===== 尺寸（对标 QTableWidget 行列 API） ===== */

/**
 * @brief 设置行数（对标 QTableWidget::setRowCount）。
 *
 *        扩大时新增空行；缩小时裁掉尾部行，当前行越界时收敛。
 *
 * @param self 目标表格控件指针。
 * @param rows 新行数；负值忽略。
 * @return 无返回值。
 */
void XTableWidget_setRowCount(XTableWidget* self, int rows);

/**
 * @brief 查询行数（对标 QTableWidget::rowCount）。
 *
 * @param self 目标表格控件指针。
 * @return 当前行数；self 为 NULL 返回 0。
 */
int XTableWidget_rowCount(const XTableWidget* self);

/**
 * @brief 设置列数（对标 QTableWidget::setColumnCount）。
 *
 *        扩大时每行补零单元格；缩小时当前列越界时收敛。
 *
 * @param self    目标表格控件指针。
 * @param columns 新列数；负值忽略。
 * @return 无返回值。
 */
void XTableWidget_setColumnCount(XTableWidget* self, int columns);

/**
 * @brief 查询列数（对标 QTableWidget::columnCount）。
 *
 * @param self 目标表格控件指针。
 * @return 当前列数；self 为 NULL 返回 0。
 */
int XTableWidget_columnCount(const XTableWidget* self);

/**
 * @brief 在 row 位置插入一行（原有行依次后移）。
 *
 * @param self 目标表格控件指针。
 * @param row  插入位置（0=最前；>行数时等价 append）。
 * @return 无返回值。
 */
void XTableWidget_insertRow(XTableWidget* self, int row);

/**
 * @brief 在 column 位置插入一列（预留列宽数组）。
 *
 * @param self   目标表格控件指针。
 * @param column 插入位置。
 * @return 无返回值。
 */
void XTableWidget_insertColumn(XTableWidget* self, int column);

/**
 * @brief 移除 row 指定行（后续行前移）。
 *
 * @param self 目标表格控件指针。
 * @param row  待移除行号。
 * @return 无返回值。
 */
void XTableWidget_removeRow(XTableWidget* self, int row);

/**
 * @brief 移除 column 指定列（列数减一）。
 *
 * @param self   目标表格控件指针。
 * @param column 待移除列号。
 * @return 无返回值。
 */
void XTableWidget_removeColumn(XTableWidget* self, int column);

/* ===== 单元格 ===== */

/**
 * @brief 写入单元格（拷贝 item 全字段；对标 setItem）。
 *
 * @param self   目标表格控件指针。
 * @param row    目标行号。
 * @param column 目标列号。
 * @param item   单元格数据指针；NULL 忽略。
 * @return 无返回值。
 */
void XTableWidget_setItem(XTableWidget* self, int row, int column,
                          const XTableWidgetItem* item);

/**
 * @brief 读取单元格（对标 item）。
 *
 * @param self   目标表格控件指针。
 * @param row    行号。
 * @param column 列号。
 * @return 单元格只读指针；越界/self 为 NULL 返回 NULL。
 */
const XTableWidgetItem* XTableWidget_item(const XTableWidget* self,
                                          int row, int column);

/**
 * @brief 设置单元格文本（快捷接口）。
 *
 * @param self   目标表格控件指针。
 * @param row    行号。
 * @param column 列号。
 * @param utf8   UTF-8 文本；NULL 忽略；超长截断。
 * @return 无返回值。
 */
void XTableWidget_setText(XTableWidget* self, int row, int column,
                          const char* utf8);

/**
 * @brief 读取单元格文本。
 *
 * @param self   目标表格控件指针。
 * @param row    行号。
 * @param column 列号。
 * @return 单元格文本（UTF-8）；越界返回空串。
 */
const char* XTableWidget_text(const XTableWidget* self, int row, int column);

/**
 * @brief 设置当前单元格并滚动到可见（对标 setCurrentCell）。
 *
 *        发射 currentCellChanged 信号并请求重绘。
 *
 * @param self   目标表格控件指针。
 * @param row    当前行号。
 * @param column 当前列号。
 * @return 无返回值。
 */
void XTableWidget_setCurrentCell(XTableWidget* self, int row, int column);

/**
 * @brief 查询当前单元格行号（对标 currentRow）。
 *
 * @param self 目标表格控件指针。
 * @return 当前行号；无当前单元格返回 -1。
 */
int XTableWidget_currentRow(const XTableWidget* self);

/**
 * @brief 查询当前单元格列号（对标 currentColumn）。
 *
 * @param self 目标表格控件指针。
 * @return 当前列号；无当前单元格返回 -1。
 */
int XTableWidget_currentColumn(const XTableWidget* self);

/* ===== 表头 ===== */

/**
 * @brief 批量设置水平表头标签（对标 setHorizontalHeaderLabels）。
 *
 * @param self   目标表格控件指针。
 * @param labels 标签字符串数组（UTF-8）。
 * @param count  标签个数。
 * @return 无返回值。
 */
void XTableWidget_setHorizontalHeaderLabels(XTableWidget* self,
                                            const char* const* labels, int count);

/**
 * @brief 批量设置垂直表头标签（对标 setVerticalHeaderLabels）。
 *
 * @param self   目标表格控件指针。
 * @param labels 标签字符串数组（UTF-8）。
 * @param count  标签个数。
 * @return 无返回值。
 */
void XTableWidget_setVerticalHeaderLabels(XTableWidget* self,
                                          const char* const* labels, int count);

/**
 * @brief 读取水平表头标签（对标 horizontalHeaderItem）。
 *
 * @param self   目标表格控件指针。
 * @param column 列号。
 * @return 列表头文本；越界/未设置返回空串。
 */
const char* XTableWidget_horizontalHeaderItem(const XTableWidget* self, int column);

/**
 * @brief 读取垂直表头标签（对标 verticalHeaderItem）。
 *
 * @param self 目标表格控件指针。
 * @param row  行号。
 * @return 行表头文本；越界/未设置返回空串。
 */
const char* XTableWidget_verticalHeaderItem(const XTableWidget* self, int row);

/**
 * @brief 设置列宽（最小 20px；对标 QHeaderView::resizeSection）。
 *
 * @param self   目标表格控件指针。
 * @param column 列号。
 * @param width  列宽（像素）。
 * @return 无返回值。
 */
void XTableWidget_setColumnWidth(XTableWidget* self, int column, int width);

/**
 * @brief 查询列宽。
 *
 * @param self   目标表格控件指针。
 * @param column 列号。
 * @return 列宽（像素）；越界返回默认宽 90。
 */
int XTableWidget_columnWidth(const XTableWidget* self, int column);

/* ===== 行为 ===== */

/**
 * @brief 启用/禁用排序（对标 setSortingEnabled）。
 *
 * @param self   目标表格控件指针。
 * @param enable true 允许排序。
 * @return 无返回值。
 */
void XTableWidget_setSortingEnabled(XTableWidget* self, bool enable);

/**
 * @brief 查询是否允许排序（对标 isSortingEnabled）。
 *
 * @param self 目标表格控件指针。
 * @return 允许排序返回 true。
 */
bool XTableWidget_isSortingEnabled(const XTableWidget* self);

/**
 * @brief 按 column 列文本排序（冒泡；对标 sortItems）。
 *
 * @param self   目标表格控件指针。
 * @param column 排序列号。
 * @param order  0 升序/1 降序。
 * @return 无返回值。
 */
void XTableWidget_sortItems(XTableWidget* self, int column, int order);

/**
 * @brief 设置网格线可见性（对标 setGridVisible）。
 *
 * @param self    目标表格控件指针。
 * @param visible true 显示网格线。
 * @return 无返回值。
 */
void XTableWidget_setGridVisible(XTableWidget* self, bool visible);

/**
 * @brief 查询网格线是否可见（对标 isGridVisible）。
 *
 * @param self 目标表格控件指针。
 * @return 网格线可见返回 true。
 */
bool XTableWidget_isGridVisible(const XTableWidget* self);

/**
 * @brief 设置统一行高（最小 16px）。
 *
 * @param self   目标表格控件指针。
 * @param height 行高（像素）。
 * @return 无返回值。
 */
void XTableWidget_setRowHeight(XTableWidget* self, int height);

/**
 * @brief 查询行高。
 *
 * @param self 目标表格控件指针。
 * @return 行高（像素）；self 为 NULL 返回默认 24。
 */
int XTableWidget_rowHeight(const XTableWidget* self);

/**
 * @brief 清空表格（行列数归零；对标 clear）。
 *
 * @param self 目标表格控件指针。
 * @return 无返回值。
 */
void XTableWidget_clear(XTableWidget* self);

/**
 * @brief 清空全部单元格内容（保留行列数；对标 clearContents）。
 *
 * @param self 目标表格控件指针。
 * @return 无返回值。
 */
void XTableWidget_clearContents(XTableWidget* self);

/**
 * @brief 滚动到指定单元格使其可见（对标 scrollToItem）。
 *
 *        按行号换算垂直滚动条值。
 *
 * @param self   目标表格控件指针。
 * @param row    目标行号。
 * @param column 目标列号（当前版本仅参与签名，预留）。
 * @return 无返回值。
 */
void XTableWidget_scrollToItem(XTableWidget* self, int row, int column);

/* ==================== 信号（仅返回自身地址；发射经 emitSignal） ==================== */

/**
 * @brief cellClicked 信号地址（参数：row, column）。
 *
 * @param self 目标表格控件指针。
 * @return 信号槽地址。
 */
void* XTableWidget_cellClicked_signal(XTableWidget* self);

/**
 * @brief cellDoubleClicked 信号地址（参数：row, column）。
 *
 * @param self 目标表格控件指针。
 * @return 信号槽地址。
 */
void* XTableWidget_cellDoubleClicked_signal(XTableWidget* self);

/**
 * @brief currentCellChanged 信号地址（参数：row, column）。
 *
 * @param self 目标表格控件指针。
 * @return 信号槽地址。
 */
void* XTableWidget_currentCellChanged_signal(XTableWidget* self);

/**
 * @brief itemChanged 信号地址（单元格内容变化时发射）。
 *
 * @param self 目标表格控件指针。
 * @return 信号槽地址。
 */
void* XTableWidget_itemChanged_signal(XTableWidget* self);

#endif /* XTABLEWIDGET_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTABLEWIDGET_H */