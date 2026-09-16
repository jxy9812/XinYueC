/**
 * @file       XListWidget.h
 * @brief      XListWidget 列表控件（对标 Qt 6.8 QListWidget）。
 * @details    以 m_base 组合继承 XListView；持有内建数据模型桥
 *             （XAbstractItemModel 单列）并通过 XListView 的 model 渲染
 *             呈现条目；提供 addItem/insertItem/item/takeItem/clear 等
 *             条目操作；选择/信号复用基类数据通路。
 * @note       模块总开关 XTABLEWIDGET_ON；XListWidget→XListView。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLISTWIDGET_H
#define XLISTWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XListView.h"
#include "XAbstractItemModel.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XListWidget)
XCLASS_DEFINE_EXTEND_END(XListWidget, XListView)

/** @brief 列表控件对象；m_base 必须是第一个成员（嵌 XListView）。 */
typedef struct XListWidget
{
    XListView m_base;             /**< 基类成员；必须是第一个。 */
    XAbstractItemModel* m_model;  /**< 内建条目模型（对象拥有；单列）。 */
} XListWidget;

/* ==================== 生命周期 ==================== */

XVtable* XListWidget_class_init(void);
/** @brief 初始化列表控件。
 * @param self 目标控件；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @return 无返回值。
 */
void XListWidget_init(XListWidget* self, XWidget* parent,
                      XWidgetFlags flags);
/** @brief 使用指定内存类型创建列表控件。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @return 新建对象；失败 NULL。
 */
XListWidget* XListWidget_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XListWidget_create(parent, flags) \
    XListWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
#define XListWidget_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XListWidget_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 条目（对标 QListWidget） ==================== */

/** @brief 追加条目（XString 主版本；对标 addItem）。
 * @param self 目标控件。
 * @param text 借用 XString*；不能为 NULL。
 * @return 新条目行号；失败 -1。
 */
int XListWidget_addItem(XListWidget* self, const XString* text);
/** @brief 追加条目（UTF-8 兼容重载，转发主版本）。 */
int XListWidget_addItem_2(XListWidget* self, const char* text);
/** @brief 插入条目（XString 主版本；对标 insertItem）。
 * @param self 目标控件。
 * @param row 插入行（0 起，负数最前、超出追加）。
 * @param text 借用 XString*；不能为 NULL。
 * @return 插入行号；失败 -1。
 */
int XListWidget_insertItem(XListWidget* self, int row, const XString* text);
/** @brief 插入条目（UTF-8 兼容重载，转发主版本）。 */
int XListWidget_insertItem_2(XListWidget* self, int row, const char* text);
/** @brief 查询条目数。 @param self 目标控件。 @return 条目数。 */
int XListWidget_count(const XListWidget* self);
/** @brief 读取条目文本（内部借用 XString*；不得释放）。
 * @param self 目标控件。
 * @param row 行号（越界返回 NULL）。
 * @return 借用 XString*。
 */
const XString* XListWidget_item(const XListWidget* self, int row);
/** @brief 读取条目文本（UTF-8 借用）。 */
const char* XListWidget_item_2(const XListWidget* self, int row);
/** @brief 移除条目。
 * @param self 目标控件。
 * @param row 行号。
 * @return 无返回值。
 */
void XListWidget_takeItem(XListWidget* self, int row);
/** @brief 清空全部条目。 @param self 目标控件。 */
void XListWidget_clear(XListWidget* self);
/** @brief 查询当前行。 @param self 目标控件。 @return 行号；无返回 -1。 */
int XListWidget_currentRow(const XListWidget* self);
/** @brief 设置当前行。
 * @param self 目标控件。
 * @param row 行号（-1 清除）。
 * @return 无返回值。
 */
void XListWidget_setCurrentRow(XListWidget* self, int row);
/** @brief 内建条目模型（对标 QListWidget::model）。
 * @param self 目标控件。
 * @return 模型借用指针。
 */
XAbstractItemModel* XListWidget_model(const XListWidget* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XLISTWIDGET_H */
