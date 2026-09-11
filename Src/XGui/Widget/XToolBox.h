/**
 * @file       XToolBox.h
 * @brief      XToolBox 工具箱控件（对标 Qt 6.8 QToolBox 全部公共 API）。
 * @details    功能范围：
 *             - 页面管理：addItem(widget, text)/insertItem/removeItem/
 *               count/widget/indexOf；removeItem 仅移除条目（控件不销毁，
 *               保持为 toolbox 子控件与 Qt 语义一致）；
 *             - 条目属性：setItemText/itemText、setItemEnabled/
 *               isItemEnabled（禁用条目不可选）；
 *             - 当前页：currentIndex/currentWidget/setCurrentIndex/
 *               setCurrentWidget；
 *             - 信号：currentChanged(int index)；
 *             - 绘制：每页一个页头条（当前页高亮）+ 当前页控件区域
 *               （简化绘制，页头不可点击展开——单页展开模式）。
 *             页面控件所有权：addItem 后控件 reparent 为 toolbox 子控件
 *             （随容器析构）；removeItem 保持父子关系（对标 Qt）。
 * @note       模块总开关 XTOOLBOX_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XFRAME_ON。
 * @author     XinYueC 团队
 */
#ifndef XTOOLBOX_H
#define XTOOLBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XFrame.h"

#if XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON

XCLASS_DEFINE_BEGING(XToolBox)
XCLASS_DEFINE_EXTEND_END(XToolBox, XFrame)

/**
 * @brief      XToolBox 控件对象；m_base 必须是第一个成员。
 */
typedef struct XToolBox
{
    XFrame m_base;         /**< 基类成员；必须是第一个。 */
    XVector* m_items;      /**< 页面条目（XToolBoxItem*，拥有）。 */
    int m_currentIndex;    /**< 当前页索引（无页 -1）。 */
} XToolBox;

/* ==================== 生命周期 ==================== */

XVtable* XToolBox_class_init(void);
void XToolBox_init(XToolBox* self, XWidget* parent, XWidgetFlags flags);
#define XToolBox_create(parent, flags) XToolBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XToolBox* XToolBox_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags);
#define XToolBox_deinit_base(self) XFrame_deinit_base((XFrame*)(self))
#define XToolBox_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 页面管理（对标 QToolBox public API） ==================== */

/** @brief 追加页面并返回索引（对标 addItem）。 */
int XToolBox_addItem(XToolBox* self, XWidget* widget, const char* utf8Text);
/** @brief 在 index 处插入页面，返回实际索引（对标 insertItem）。 */
/**
 * @brief      插入工具箱条目。
 */
int XToolBox_insertItem(XToolBox* self, int index, XWidget* widget,
                        const char* utf8Text);
/** @brief 移除条目（控件不销毁，对标 removeItem）。 */
/**
 * @brief      移除工具箱条目。
 */
void XToolBox_removeItem(XToolBox* self, int index);
/** @brief 查询页面数量。 */
int XToolBox_count(const XToolBox* self);
/** @brief 查询指定索引的页面控件；越界返回 NULL。 */
/**
 * @brief      获取内容控件（对标 Qt 同名方法）。
 */
XWidget* XToolBox_widget(const XToolBox* self, int index);
/** @brief 查询页面控件索引；未找到返回 -1。 */
int XToolBox_indexOf(const XToolBox* self, const XWidget* widget);

/* ==================== 条目属性 ==================== */

/** @brief 设置页头文本（对标 setItemText）。 */
/**
 * @brief      设置条目文本。
 */
void XToolBox_setItemText(XToolBox* self, int index, const char* utf8);
/** @brief 查询页头文本（对标 itemText）。 */
/**
 * @brief      获取条目文本。
 */
const char* XToolBox_itemText(const XToolBox* self, int index);
/** @brief 设置条目启用（禁用条目不可选；对标 setItemEnabled）。 */
/**
 * @brief      设置条目可用。
 */
void XToolBox_setItemEnabled(XToolBox* self, int index, bool enabled);
/** @brief 查询条目启用（对标 isItemEnabled）。 */
bool XToolBox_isItemEnabled(const XToolBox* self, int index);

/* ==================== 当前页槽 ==================== */

int XToolBox_currentIndex(const XToolBox* self);
/**
 * @brief      获取当前控件。
 */
XWidget* XToolBox_currentWidget(const XToolBox* self);
void XToolBox_setCurrentIndex(XToolBox* self, int index);
void XToolBox_setCurrentWidget(XToolBox* self, XWidget* widget);

/* ==================== 信号 ==================== */

/**
 * @brief      当前项变化信号（真发射）。
 */
void* XToolBox_currentChanged_signal(XToolBox* self, int index);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON */

#ifdef __cplusplus
}
#endif
const char* XToolBox_itemToolTip(const XToolBox* self, int index);
void XToolBox_setItemIcon(XToolBox* self, int index, const char* icon);
void XToolBox_setItemToolTip(XToolBox* self, int index, const char* tip);
#endif /* XTOOLBOX_H */
