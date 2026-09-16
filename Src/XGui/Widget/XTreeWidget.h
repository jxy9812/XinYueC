/**
 * @file       XTreeWidget.h
 * @brief      XTreeWidget 树控件（对标 Qt 6.8 QTreeWidget）。
 * @details    以 m_base 组合继承 XTreeView；持有树条目
 *             （XTreeWidgetItem：文本 + 子节点数组），自绘递归渲染
 *             （缩进 = 深度 × indentation）；提供 addTopLevelItem/
 *             insertTopLevelItem/topLevelItem/takeTopLevelItem/clear。
 * @note       模块总开关 XTABLEWIDGET_ON；XTreeWidget→XTreeView。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTREEWIDGET_H
#define XTREEWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XTreeView.h"
#include "XString.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 树条目 ==================== */

/** @brief 树条目（对标 QTreeWidgetItem；文本 + 子节点，对象拥有）。 */
typedef struct XTreeWidgetItem
{
    XString* text;                /**< 显示文本（对象拥有）。 */
    struct XTreeWidgetItem** children; /**< 子节点数组（对象拥有）。 */
    int childCount;               /**< 子节点数。 */
    int childCapacity;            /**< 子节点容量。 */
    struct XTreeWidgetItem* parent; /**< 父节点（借用；顶层为 NULL）。 */
} XTreeWidgetItem;

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XTreeWidget)
XCLASS_DEFINE_EXTEND_END(XTreeWidget, XTreeView)

/** @brief 树控件对象；m_base 必须是第一个成员（嵌 XTreeView）。 */
typedef struct XTreeWidget
{
    XTreeView m_base;             /**< 基类成员；必须是第一个。 */
    XTreeWidgetItem** m_topItems; /**< 顶层条目数组（对象拥有）。 */
    int m_topCount;               /**< 顶层条目数。 */
    int m_topCapacity;            /**< 顶层条目容量。 */
} XTreeWidget;

/* ==================== 生命周期 ==================== */

XVtable* XTreeWidget_class_init(void);
/** @brief 初始化树控件。
 * @param self 目标控件；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @return 无返回值。
 */
void XTreeWidget_init(XTreeWidget* self, XWidget* parent,
                      XWidgetFlags flags);
/** @brief 使用指定内存类型创建树控件。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @return 新建对象；失败 NULL。
 */
XTreeWidget* XTreeWidget_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XTreeWidget_create(parent, flags) \
    XTreeWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
#define XTreeWidget_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XTreeWidget_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 条目（对标 QTreeWidget） ==================== */

/** @brief 创建树条目（文本 + 父节点；对标 new QTreeWidgetItem）。
 * @param text 借用 XString*；可为 NULL（空文本）。
 * @param parent 父条目借用指针；NULL=顶层（由 addTopLevelItem 挂接）。
 * @return 新建条目（堆；由树或调用方 delete）。
 */
XTreeWidgetItem* XTreeWidgetItem_create(const XString* text,
                                        XTreeWidgetItem* parent);
/** @brief 创建树条目（UTF-8 兼容重载）。 */
XTreeWidgetItem* XTreeWidgetItem_create_2(const char* text,
                                          XTreeWidgetItem* parent);
/** @brief 释放树条目及其子树。
 * @param item 目标条目；可为 NULL。
 * @return 无返回值。
 */
void XTreeWidgetItem_delete(XTreeWidgetItem* item);
/** @brief 读取条目文本（内部借用 XString*；不得释放）。 */
const XString* XTreeWidgetItem_text(const XTreeWidgetItem* item);
/** @brief 读取条目文本（UTF-8 借用）。 */
const char* XTreeWidgetItem_text_2(const XTreeWidgetItem* item);
/** @brief 设置条目文本（XString 主版本）。
 * @param item 目标条目。
 * @param text 借用 XString*；可为 NULL（清空）。
 * @return 无返回值。
 */
void XTreeWidgetItem_setText(XTreeWidgetItem* item, const XString* text);
/** @brief 设置条目文本（UTF-8 兼容重载，转发主版本）。 */
void XTreeWidgetItem_setText_2(XTreeWidgetItem* item, const char* text);
/** @brief 追加子条目。
 * @param item 目标条目。
 * @param child 子条目（所有权转移给 item）。
 * @return 成功返回 true。
 */
bool XTreeWidgetItem_addChild(XTreeWidgetItem* item,
                              XTreeWidgetItem* child);
/** @brief 查询子条目数。 @param item 目标条目。 @return 子条目数。 */
int XTreeWidgetItem_childCount(const XTreeWidgetItem* item);
/** @brief 读取子条目。 @param item 目标条目。 @param index 序号。 @return 借用指针。 */
XTreeWidgetItem* XTreeWidgetItem_child(const XTreeWidgetItem* item,
                                       int index);

/** @brief 追加顶层条目。
 * @param self 目标控件。
 * @param item 条目（所有权转移给树）。
 * @return 成功返回 true。
 */
bool XTreeWidget_addTopLevelItem(XTreeWidget* self, XTreeWidgetItem* item);
/** @brief 插入顶层条目。
 * @param self 目标控件。
 * @param index 插入位置（0 起）。
 * @param item 条目（所有权转移给树）。
 * @return 成功返回 true。
 */
bool XTreeWidget_insertTopLevelItem(XTreeWidget* self, int index,
                                    XTreeWidgetItem* item);
/** @brief 读取顶层条目。 @param self 目标控件。 @param index 序号。 @return 借用指针。 */
XTreeWidgetItem* XTreeWidget_topLevelItem(const XTreeWidget* self, int index);
/** @brief 查询顶层条目数。 @param self 目标控件。 @return 顶层条目数。 */
int XTreeWidget_topLevelItemCount(const XTreeWidget* self);
/** @brief 移除顶层条目（所有权归还调用方）。
 * @param self 目标控件。
 * @param index 序号。
 * @return 被移除条目；越界返回 NULL。
 */
XTreeWidgetItem* XTreeWidget_takeTopLevelItem(XTreeWidget* self, int index);
/** @brief 清空全部条目。 @param self 目标控件。 */
void XTreeWidget_clear(XTreeWidget* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XTREEWIDGET_H */
