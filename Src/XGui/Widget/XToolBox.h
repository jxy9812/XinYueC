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
 *             页面控件所有权：addItem 后控件装进**本条目专属的 XScrollArea**
 *             （对标 qtoolbox.cpp:322-336 每页一只 QScrollArea ⇒ 页的
 *             parentWidget() 是滚动区 viewport，页内容超高时页内滚动），
 *             滚动区以 toolbox 为父、随容器析构；removeItem 先把页从滚动区
 *             取回再回收容器，页仍以 toolbox 为父（对标 Qt 的
 *             `w->setParent(this)`，控件不销毁）。
 * @note       模块总开关 XTOOLBOX_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XFRAME_ON。
 * @author     XinYueC 团队
 */
#ifndef XTOOLBOX_H
#define XTOOLBOX_H

#if XGUI_ON
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
/* [Qt 对齐 2026-11-11] 页增删虚槽（对标 QToolBox::itemInserted/itemRemoved，
 * qtoolbox.h:63-64 的 protected virtual）。追加在父类槽之后。 */
XCLASS_DEFINE_ENUM(XToolBox, ItemInserted) = XCLASS_VTABLE_GET_SIZE(XFrame),
XCLASS_DEFINE_ENUM(XToolBox, ItemRemoved),
XCLASS_DEFINE_END(XToolBox)

/**
 * @brief      XToolBox 控件对象；m_base 必须是第一个成员。
 */
typedef struct XToolBox
{
    XFrame m_base;         /**< 基类成员；必须是第一个。 */
    XVector* m_items;      /**< 页面条目（XToolBoxItem*，拥有）。 */
    /* [内存治理 2026-10-11] 窄整型：页索引有 -1 哨兵（无页），
       用 int16_t 保留符号并消除指针后的 4B 洞。 */
    int16_t m_currentIndex;    /**< 当前页索引（无页 -1）。 */
} XToolBox;

/* ==================== 生命周期 ==================== */

XVtable* XToolBox_class_init(void);
/** @brief X工具盒init（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XToolBox_init(XToolBox* self, XWidget* parent, XWidgetFlags flags);
#define XToolBox_create(parent, flags) XToolBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XToolBox* XToolBox_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags);

/* ==================== 页面管理（对标 QToolBox public API） ==================== */

/** @brief 追加页面并返回索引（对标 addItem）。 */
int XToolBox_addItem(XToolBox* self, XWidget* widget, const char* utf8Text);
/** @brief 在 index 处插入页面，返回实际索引（对标 insertItem）。 */
/**
 * @brief      插入工具箱条目。
 */
int XToolBox_insertItem(XToolBox* self, int index, XWidget* widget,
                        const char* utf8Text);
/** @brief 移除条目（控件不销毁，对标 removeItem）。
 * @details 控件出表并隐藏，但**仍以本工具箱为父**（Qt 显式
 *          `w->setParent(this)`，随容器析构级联释放；不是交还调用方）。
 *          页表清空时发 currentChanged(-1)；移出的是当前页时新当前页
 *          固定落在索引 0 并随之发 currentChanged(0)（Qt 口径）。
 *          条目出表后调 itemRemoved(原索引)。
 */

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
/** @brief 设置条目图标路径（对标 QToolBox::setItemIcon；图标以路径
 *         字符串表示，XString 主版本）。
 * @param self 目标工具箱。
 * @param index 条目索引。
 * @param path 图标路径借用指针；NULL 清除图标。
 * @return 无返回值。
 */
void XToolBox_setItemIcon(XToolBox* self, int index, const XString* path);
/** @brief 使用 UTF-8 路径设置条目图标（UTF-8 兼容重载）。
 * @param self 目标工具箱。
 * @param index 条目索引。
 * @param utf8 图标路径（UTF-8）；NULL 清除图标。
 * @return 无返回值。
 */
void XToolBox_setItemIcon_2(XToolBox* self, int index, const char* utf8);
/** @brief 查询条目图标路径（对标 QToolBox::itemIcon）。
 * @param self 目标工具箱。
 * @param index 条目索引。
 * @return 借用指针；未设置或越界返回 NULL。
 */
const XString* XToolBox_itemIcon(const XToolBox* self, int index);
/** @brief 设置条目提示（对标 QToolBox::setItemToolTip；XString 主版本）。
 * @details 仅存储状态；提示展示层尚未接入。
 * @param self 目标工具箱；传入 NULL 时函数不执行任何操作。
 * @param index 条目索引；越界时忽略。
 * @param tip 提示文本借用指针；NULL 清除提示。
 * @return 无返回值。
 */
void XToolBox_setItemToolTip(XToolBox* self, int index, const XString* tip);
/** @brief 设置条目提示（UTF-8 兼容重载，转发 XString 主版本）。
 * @param self 目标工具箱；传入 NULL 时函数不执行任何操作。
 * @param index 条目索引；越界时忽略。
 * @param utf8 提示文本（UTF-8）；NULL 清除提示。
 * @return 无返回值。
 */
void XToolBox_setItemToolTip_2(XToolBox* self, int index, const char* utf8);
/** @brief 查询条目提示（对标 QToolBox::itemToolTip）。
 * @param self 目标工具箱；传入 NULL 或越界时返回 NULL。
 * @param index 条目索引。
 * @return 借用内部 XString 指针；未设置返回 NULL；禁止释放或修改。
 */
const XString* XToolBox_itemToolTip(const XToolBox* self, int index);

/* ==================== 当前页槽 ==================== */

int XToolBox_currentIndex(const XToolBox* self);
/**
 * @brief      获取当前控件。
 */
XWidget* XToolBox_currentWidget(const XToolBox* self);
/** @brief X工具盒set当前索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 无返回值。
 */
void XToolBox_setCurrentIndex(XToolBox* self, int index);
/** @brief X工具盒set当前控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param widget 子控件指针。
 * @return 无返回值。
 */
void XToolBox_setCurrentWidget(XToolBox* self, XWidget* widget);

/* ==================== 信号 ==================== */

/**
 * @brief      当前项变化信号（真发射）。
 */
void* XToolBox_currentChanged_signal(XToolBox* self, int index);

/* ==================== 保护虚槽（对标 Qt protected virtual） ==================== */

/** @brief 页插入钩子（对标 QToolBox::itemInserted，qtoolbox.h:63）。
 * @details insertItem/addItem 在条目登记、当前页与信号处理完成后调用；
 *          虚槽 EXToolBox_ItemInserted，子类可重载。缺省空操作。
 * @param self 目标工具箱。
 * @param index 新页索引。
 * @return 无返回值。
 */
void XToolBox_itemInserted(XToolBox* self, int index);
/** @brief 页移除钩子（对标 QToolBox::itemRemoved，qtoolbox.h:64）。
 * @details removeItem 在条目出表后调用；虚槽 EXToolBox_ItemRemoved。
 * @param self 目标工具箱。
 * @param index 被移除页的原索引。
 * @return 无返回值。
 */
void XToolBox_itemRemoved(XToolBox* self, int index);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON */

#endif /* XGUI_ON */

#endif /* XTOOLBOX_H */
