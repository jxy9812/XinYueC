#ifndef XTABLEVIEW_H
#define XTABLEVIEW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemView.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

XCLASS_DEFINE_BEGING(XTableView)
XCLASS_DEFINE_EXTEND_END(XTableView, XAbstractItemView)

/**
 * @brief 表格视图（对标 Qt 6.8 QTableView）。
 *
 *        承载列宽/行高、网格显示、排序开关与整行/整列选择等表格层
 *        属性；条目（单元格）模型由派生类（XTableWidget）提供。
 */
typedef struct XTableView
{
    XAbstractItemView m_base;      /**< 基类成员；必须是第一个。 */
    int* m_colWidths;              /**< 各列宽（像素，下标=列号）。 */
    int m_colCapacity;             /**< 列容量。 */
    int m_rowHeight;               /**< 统一行高（像素，默认 24）。 */
    int m_gridVisible;             /**< 显示网格线（默认开；对标 showGrid）。 */
    bool m_sortingEnabled;         /**< 允许排序（对标 sortingEnabled）。 */
    int m_sortColumn;              /**< 最近一次排序列。 */
    int m_sortOrder;               /**< 最近一次排序序：0 升/1 降。 */
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

/* ==================== 网格（对标 QTableView::showGrid） ==================== */

/** @brief 设置网格显示。 @param self 目标视图指针。 @param show true 显示。 @return 无返回值。 */
void XTableView_setShowGrid(XTableView* self, bool show);
/** @brief 查询网格显示。 @param self 目标视图指针。 @return 显示返回 true。 */
bool XTableView_showGrid(const XTableView* self);

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
