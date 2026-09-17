/**
 * @file       XTreeView.h
 * @brief      XTreeView 树视图（对标 Qt 6.8 QTreeView）。
 * @details    以 m_base 组合继承 XAbstractItemView；当前模型为扁平表格，
 *             树渲染按第一列绘制（层次缩进预留 indentation）；支持表头
 *             隐藏、缩进宽度、树展开族（expand/collapse 状态以平行数组
 *             按行承载）、列隐藏/列宽状态。树条目由 XTreeWidget 提供扩展。
 * @note       模块总开关 XTABLEWIDGET_ON；XTreeView→XAbstractItemView。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTREEVIEW_H
#define XTREEVIEW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemView.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XTreeView)
XCLASS_DEFINE_EXTEND_END(XTreeView, XAbstractItemView)

/** @brief 树视图对象；m_base 必须是第一个成员（嵌 XAbstractItemView）。 */
typedef struct XTreeView
{
    XAbstractItemView m_base;   /**< 基类成员；必须是第一个。 */
    int m_indentation;          /**< 缩进宽度（像素，默认 20）。 */
    bool m_headerHidden;        /**< 表头隐藏（默认 false）。 */
    int m_rowHeight;            /**< 行高（像素，默认 24）。 */
    bool* m_expanded;           /**< 行展开状态表（平行数组；对象拥有）。 */
    int m_rowStateCount;        /**< 行展开状态表长度（随模型行数同步）。 */
    bool* m_columnHidden;       /**< 列隐藏状态表（平行数组；对象拥有）。 */
    int* m_columnWidths;        /**< 列宽表（平行数组；0=自动铺满）。 */
    int m_columnStateCount;     /**< 列状态表长度。 */
    bool m_expandsOnDoubleClick; /**< 双击切换展开（默认 true）。 */
    bool m_itemsExpandable;      /**< 条目可展开（默认 true）。 */
    bool m_rootIsDecorated;      /**< 绘制展开控件列（默认 true）。 */
    bool m_sortingEnabled;       /**< 排序使能（默认 false）。 */
    bool m_uniformRowHeights;    /**< 等高行（默认 false）。 */
} XTreeView;

/* ==================== 生命周期 ==================== */

XVtable* XTreeView_class_init(void);
/** @brief 初始化树视图。
 * @param self 目标视图；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @return 无返回值。
 */
void XTreeView_init(XTreeView* self, XWidget* parent, XWidgetFlags flags);
/** @brief 使用指定内存类型创建树视图。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @return 新建对象；失败 NULL。
 */
XTreeView* XTreeView_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags);
#define XTreeView_create(parent, flags) \
    XTreeView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
#define XTreeView_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XTreeView_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 属性（对标 QTreeView） ==================== */

/** @brief 设置缩进宽度。
 * @param self 目标视图。
 * @param indentation 缩进（像素；>=0）。
 * @return 无返回值。
 */
void XTreeView_setIndentation(XTreeView* self, int indentation);
/** @brief 查询缩进宽度。 @param self 目标视图。 @return 缩进。 */
int XTreeView_indentation(const XTreeView* self);
/** @brief 设置表头隐藏。
 * @param self 目标视图。
 * @param hidden true 隐藏。
 * @return 无返回值。
 */
void XTreeView_setHeaderHidden(XTreeView* self, bool hidden);
/** @brief 查询表头隐藏。 @param self 目标视图。 @return 隐藏返回 true。 */
bool XTreeView_isHeaderHidden(const XTreeView* self);
/** @brief 设置行高（项目库扩展：统一行高像素）。
 * @param self 目标视图。
 * @param height 行高（像素；>0）。
 * @return 无返回值。
 */
void XTreeView_setRowHeight(XTreeView* self, int height);
/** @brief 查询行高。 @param self 目标视图。 @return 行高。 */
int XTreeView_rowHeight(const XTreeView* self);

/* ==================== 树展开族（对标 QTreeView） ==================== */

/** @brief 展开指定行（对标 expand；状态由折叠变展开时发射 expanded(row)）。
 * @param self 目标视图。
 * @param row 行号（越界忽略）。
 * @return 无返回值。
 */
void XTreeView_expand(XTreeView* self, int row);
/** @brief 收起指定行（对标 collapse；状态由展开变折叠时发射 collapsed(row)）。
 * @param self 目标视图。
 * @param row 行号（越界忽略）。
 * @return 无返回值。
 */
void XTreeView_collapse(XTreeView* self, int row);
/** @brief 查询行展开状态（对标 isExpanded）。
 * @param self 目标视图。
 * @param row 行号。
 * @return 已展开返回 true；越界或未同步返回 false（默认折叠）。
 */
bool XTreeView_isExpanded(const XTreeView* self, int row);
/** @brief 设置行展开状态（对标 setExpanded；变化时发射 expanded/collapsed）。
 * @param self 目标视图。
 * @param row 行号（越界忽略）。
 * @param expand true 展开，false 收起。
 * @return 无返回值。
 */
void XTreeView_setExpanded(XTreeView* self, int row, bool expand);
/** @brief 展开全部行（对标 expandAll；不对单行发射 expanded，对标 Qt）。
 * @param self 目标视图。
 * @return 无返回值。
 */
void XTreeView_expandAll(XTreeView* self);
/** @brief 收起全部行（对标 collapseAll；不对单行发射 collapsed，对标 Qt）。
 * @param self 目标视图。
 * @return 无返回值。
 */
void XTreeView_collapseAll(XTreeView* self);
/** @brief 展开到指定深度（对标 expandToDepth）。
 * @param self 目标视图。
 * @param depth 目标深度（本参数在行模型下不参与计算）。
 * @return 无返回值。
 * @note 当前为扁平行模型、无父子层次，语义简化为全部行置为展开。
 */
void XTreeView_expandToDepth(XTreeView* self, int depth);
/** @brief 设置双击切换展开（对标 setExpandsOnDoubleClick；默认 true）。
 * @param self 目标视图。
 * @param enable true 允许双击切换。
 * @return 无返回值。
 */
void XTreeView_setExpandsOnDoubleClick(XTreeView* self, bool enable);
/** @brief 查询双击切换展开（对标 expandsOnDoubleClick）。
 * @param self 目标视图。
 * @return 允许返回 true。
 */
bool XTreeView_expandsOnDoubleClick(const XTreeView* self);
/** @brief 设置条目可展开（对标 setItemsExpandable；默认 true）。
 * @param self 目标视图。
 * @param enable true 可展开。
 * @return 无返回值。
 * @note 当前扁平行模型无交互展开入口，本接口为状态存取（渲染分支
 *       判定使用），交互接线由派生 XTreeWidget 决定。
 */
void XTreeView_setItemsExpandable(XTreeView* self, bool enable);
/** @brief 查询条目可展开（对标 itemsExpandable）。
 * @param self 目标视图。
 * @return 可展开返回 true。
 */
bool XTreeView_itemsExpandable(const XTreeView* self);
/** @brief 设置绘制根级展开控件列（对标 setRootIsDecorated；默认 true）。
 * @param self 目标视图。
 * @param show true 绘制 +/- 展开控件。
 * @return 无返回值。
 */
void XTreeView_setRootIsDecorated(XTreeView* self, bool show);
/** @brief 查询根级展开控件列（对标 rootIsDecorated）。
 * @param self 目标视图。
 * @return 绘制返回 true。
 */
bool XTreeView_rootIsDecorated(const XTreeView* self);
/** @brief 设置排序使能（对标 setSortingEnabled；默认 false）。
 * @param self 目标视图。
 * @param enable true 使能。
 * @return 无返回值。
 * @note 当前扁平行模型未接表头点击排序，本接口为状态存取。
 */
void XTreeView_setSortingEnabled(XTreeView* self, bool enable);
/** @brief 查询排序使能（对标 isSortingEnabled）。
 * @param self 目标视图。
 * @return 使能返回 true。
 */
bool XTreeView_isSortingEnabled(const XTreeView* self);
/** @brief 设置等高行优化（对标 setUniformRowHeights；默认 false）。
 * @param self 目标视图。
 * @param uniform true 声明全部行等高。
 * @return 无返回值。
 * @note 本库渲染恒为统一行高（m_rowHeight），本接口为状态存取。
 */
void XTreeView_setUniformRowHeights(XTreeView* self, bool uniform);
/** @brief 查询等高行优化（对标 uniformRowHeights）。
 * @param self 目标视图。
 * @return 等高返回 true。
 */
bool XTreeView_uniformRowHeights(const XTreeView* self);

/* ==================== 列状态族（对标 QTreeView/QTreeWidget） ==================== */

/** @brief 设置列隐藏（对标 setColumnHidden）。
 * @param self 目标视图。
 * @param column 列号（<0 忽略）。
 * @param hide true 隐藏。
 * @return 无返回值。
 */
void XTreeView_setColumnHidden(XTreeView* self, int column, bool hide);
/** @brief 查询列隐藏（对标 isColumnHidden）。
 * @param self 目标视图。
 * @param column 列号。
 * @return 隐藏返回 true；越界返回 false。
 */
bool XTreeView_isColumnHidden(const XTreeView* self, int column);
/** @brief 设置列宽（对标 setColumnWidth；0 表示自动铺满）。
 * @param self 目标视图。
 * @param column 列号（<0 忽略）。
 * @param width 宽度（像素；<0 忽略，0 恢复自动）。
 * @return 无返回值。
 */
void XTreeView_setColumnWidth(XTreeView* self, int column, int width);
/** @brief 查询列宽（对标 columnWidth）。
 * @param self 目标视图。
 * @param column 列号。
 * @return 宽度（像素）；未设置或越界返回 0（自动铺满）。
 */
int XTreeView_columnWidth(const XTreeView* self, int column);

/* ==================== 信号（对标 QTreeView） ==================== */

/** @brief expanded(int) 信号（行由折叠变展开时发射；载荷：行号）。 */
void* XTreeView_expanded_signal(XTreeView* self, int row);
/** @brief collapsed(int) 信号（行由展开变折叠时发射；载荷：行号）。 */
void* XTreeView_collapsed_signal(XTreeView* self, int row);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XTREEVIEW_H */
