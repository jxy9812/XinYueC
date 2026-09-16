/**
 * @file       XTreeView.h
 * @brief      XTreeView 树视图（对标 Qt 6.8 QTreeView）。
 * @details    以 m_base 组合继承 XAbstractItemView；当前模型为扁平表格，
 *             树渲染按第一列绘制（层次缩进预留 indentation）；支持表头
 *             隐藏、缩进宽度。树条目由 XTreeWidget 提供扩展。
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

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XTREEVIEW_H */
