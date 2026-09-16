/**
 * @file       XListView.h
 * @brief      XListView 列表视图（对标 Qt 6.8 QListView）。
 * @details    以 m_base 组合继承 XAbstractItemView；从数据模型
 *             （XAbstractItemModel）按单列垂直列表渲染条目（行高
 *             24px），支持间隔 spacing、选中高亮；选择/信号/命中
 *             全部复用基类数据通路。
 * @note       模块总开关 XTABLEWIDGET_ON；XListView 为 XWidget 派生链
 *             （XListView→XAbstractItemView→XAbstractScrollArea→XFrame）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLISTVIEW_H
#define XLISTVIEW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemView.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XListView)
XCLASS_DEFINE_EXTEND_END(XListView, XAbstractItemView)

/** @brief 列表视图对象；m_base 必须是第一个成员（嵌 XAbstractItemView）。 */
typedef struct XListView
{
    XAbstractItemView m_base;   /**< 基类成员；必须是第一个。 */
    int m_spacing;              /**< 条目间距（像素，默认 0）。 */
    int m_modelColumn;          /**< 渲染列（默认 0）。 */
    int m_rowHeight;            /**< 行高（像素，默认 24）。 */
} XListView;

/* ==================== 生命周期 ==================== */

XVtable* XListView_class_init(void);
/** @brief 初始化列表视图。
 * @param self 目标视图；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @return 无返回值。
 */
void XListView_init(XListView* self, XWidget* parent, XWidgetFlags flags);
/** @brief 使用指定内存类型创建列表视图。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @return 新建对象；失败 NULL。
 */
XListView* XListView_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags);
#define XListView_create(parent, flags) \
    XListView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
#define XListView_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XListView_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 属性（对标 QListView） ==================== */

/** @brief 设置条目间距。
 * @param self 目标视图。
 * @param spacing 间距（像素；>=0）。
 * @return 无返回值。
 */
void XListView_setSpacing(XListView* self, int spacing);
/** @brief 查询条目间距。 @param self 目标视图。 @return 间距。 */
int XListView_spacing(const XListView* self);
/** @brief 设置渲染列（对标 QListView::setModelColumn）。
 * @param self 目标视图。
 * @param column 列号（>=0）。
 * @return 无返回值。
 */
void XListView_setModelColumn(XListView* self, int column);
/** @brief 查询渲染列。 @param self 目标视图。 @return 列号。 */
int XListView_modelColumn(const XListView* self);
/** @brief 设置行高（项目库扩展：统一行高像素）。
 * @param self 目标视图。
 * @param height 行高（像素；>0）。
 * @return 无返回值。
 */
void XListView_setRowHeight(XListView* self, int height);
/** @brief 查询行高。 @param self 目标视图。 @return 行高。 */
int XListView_rowHeight(const XListView* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XLISTVIEW_H */
