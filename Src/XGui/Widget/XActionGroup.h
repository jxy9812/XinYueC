/**
 * @file       XActionGroup.h
 * @brief      XActionGroup 动作逻辑分组（对标 Qt 6.8 QActionGroup 核心
 *             公共 API；QActionGroup : QObject，本类 XObject 派生）。
 * @details    功能范围：
 *             - 成员管理：addAction/removeAction/actions（新建
 *               XVector<XAction*>，调用方释放）；
 *             - 选中：checkedAction/setCheckedAction（setCheckedAction
 *               触发 triggered 信号；Qt 6.8.3 无此 API，XGui 扩展，
 *               对标 QButtonGroup::setCheckedButton 语义，@note）；
 *             - 互斥：exclusive/setExclusive（默认 true）——选中某
 *               动作时自动取消其它动作的选中（XAction_setChecked）；
 *             - 状态：enabled/setEnabled（转发到全部成员动作）；
 *             - 信号：triggered(XAction*)/hovered(XAction*)（转发组内
 *               动作的 triggered/hovered，动作加入时连接、移除时断开）。
 *             XActionGroup 为逻辑分组对象，不负责成员动作的销毁。
 * @note       模块沿用 XGuiConfig.h 既有控件开关模式，在 XWIDGET_ON
 *             且 XACTION_ON 下编译。
 * @author     XinYueC 团队
 */
#ifndef XACTIONGROUP_H
#define XACTIONGROUP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XAction.h"
#include "XVector.h"

#if XWIDGET_ON && XACTION_ON

XCLASS_DEFINE_BEGING(XActionGroup)
XCLASS_DEFINE_EXTEND_END(XActionGroup, XObject)

/**
 * @brief      XActionGroup 分组对象；m_base 必须是第一个成员。
 * @details    m_actions 为成员动作借用指针数组；m_bridges 为与成员
 *             顺序一致的内信号桥；m_checkedAction 为当前选中动作
 *             （借用）。
 */
typedef struct XActionGroup
{
    XObject     m_base;          /**< 基类成员；必须是第一个。 */
    XVector*    m_actions;       /**< 成员动作借用指针数组（XAction*）。 */
    XVector*    m_bridges;       /**< 与成员顺序一致的内信号桥（XActionGroupBridge*）。 */
    XAction*    m_checkedAction; /**< 当前选中动作（借用；可为 NULL）。 */
    bool        m_exclusive;     /**< 互斥（默认 true）。 */
    bool        m_enabled;       /**< 分组启用状态（默认 true）。 */
} XActionGroup;

/* ==================== 生命周期 ==================== */

/**
 * @brief      初始化并返回 XActionGroup 类的共享虚函数表。
 * @return     类共享的 XVtable 指针；失败返回 NULL。
 */
XVtable* XActionGroup_class_init(void);

/**
 * @brief      默认初始化嵌入式 XActionGroup 对象。
 * @param      self 待初始化的可写对象存储；不可为 NULL。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     无返回值。
 */
void XActionGroup_init(XActionGroup* self, XObject* parent);

/**
 * @brief      使用默认内存类型创建分组对象（对标 QActionGroup(parent)）。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     新建的已初始化对象指针；失败返回 NULL。成功后必须
 *             XActionGroup_delete_base 释放。
 */
#define XActionGroup_create(parent) \
    XActionGroup_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))
XActionGroup* XActionGroup_create_ex(XMemoryType memory, XObject* parent);

#define XActionGroup_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XActionGroup_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 成员管理（对标 QActionGroup） ==================== */

/**
 * @brief      添加动作到组（对标 QActionGroup::addAction）。
 * @details    仅借用不取得所有权；重复添加忽略；加入时连接动作的
 *             triggered/hovered/destroyed 信号到转发桥。
 * @param      self 目标分组对象；可为 NULL。
 * @param      action 成员动作借用指针；可为 NULL（忽略）。
 * @return     无返回值。
 */
void XActionGroup_addAction(XActionGroup* self, XAction* action);

/**
 * @brief      从组移除动作（对标 QActionGroup::removeAction）。
 * @details    断开转发桥连接并删除桥；动作对象本身不销毁。
 * @param      self 目标分组对象；可为 NULL。
 * @param      action 成员动作借用指针；可为 NULL（忽略）。
 * @return     无返回值。
 */
void XActionGroup_removeAction(XActionGroup* self, XAction* action);

/**
 * @brief      返回成员动作列表（对标 QActionGroup::actions）。
 * @details    Qt 返回 QList<QAction*>；XGui 返回新建的
 *             XVector<XAction*>（按加入顺序），由调用方拥有，使用后
 *             必须 XVector_delete_base 释放；无成员返回空数组。
 * @param      self 目标分组对象；可为 NULL。
 * @return     新建的 XVector（元素为 XAction*）；分配失败返回 NULL。
 */
XVector* XActionGroup_actions(const XActionGroup* self);

/* ==================== 选中与状态（对标 QActionGroup） ==================== */

/**
 * @brief      查询当前选中动作（对标 QActionGroup::checkedAction）。
 * @details    遍历成员返回第一个 XAction_isChecked 为 true 的动作；
 *             与内部缓存保持一致。
 * @param      self 目标分组对象；可为 NULL。
 * @return     选中动作借用指针；无选中或 self 为 NULL 返回 NULL。
 */
XAction* XActionGroup_checkedAction(const XActionGroup* self);

/**
 * @brief      设置选中动作（XGui 扩展；Qt 6.8.3 QActionGroup 无此
 *             公共 API，语义对标 QButtonGroup::setCheckedButton）。
 * @details    动作必须是组成员且可选中；exclusive 时先取消其它成员
 *             选中，再 XAction_setChecked(action,true) 并发射
 *             triggered(action)。
 * @param      self 目标分组对象；可为 NULL。
 * @param      action 目标动作借用指针；可为 NULL（仅清空缓存）。
 * @return     无返回值。
 */
void XActionGroup_setCheckedAction(XActionGroup* self, XAction* action);

/**
 * @brief      查询互斥标志（对标 QActionGroup::isExclusive；默认 true）。
 * @param      self 目标分组对象；可为 NULL。
 * @return     互斥返回 true；self 为 NULL 返回 false。
 */
bool XActionGroup_isExclusive(const XActionGroup* self);

/**
 * @brief      设置互斥标志（对标 QActionGroup::setExclusive）。
 * @param      self 目标分组对象；可为 NULL。
 * @param      exclusive true 互斥，false 允许多选。
 * @return     无返回值。
 */
void XActionGroup_setExclusive(XActionGroup* self, bool exclusive);

/**
 * @brief      查询分组启用状态（对标 QActionGroup::isEnabled；默认 true）。
 * @param      self 目标分组对象；可为 NULL。
 * @return     启用返回 true；self 为 NULL 返回 false。
 */
bool XActionGroup_isEnabled(const XActionGroup* self);

/**
 * @brief      设置分组启用状态（对标 QActionGroup::setEnabled）。
 * @details    更新分组标志并转发到全部成员动作
 *             （XAction_setEnabled）。
 * @param      self 目标分组对象；可为 NULL。
 * @param      enabled true 启用，false 禁用。
 * @return     无返回值。
 */
void XActionGroup_setEnabled(XActionGroup* self, bool enabled);

/* ==================== 信号（对标 QActionGroup signals） ==================== */

/**
 * @brief      动作触发信号（对标 QActionGroup::triggered(QAction*)；
 *             真发射，参数为触发动作借用指针）。
 * @param      self 发射信号的对象；可为 NULL。
 * @param      action 触发动作借用指针。
 * @return     不透明的 triggered 信号标识。
 */
void* XActionGroup_triggered_signal(XActionGroup* self, XAction* action);

/**
 * @brief      动作悬停信号（对标 QActionGroup::hovered(QAction*)；
 *             真发射，参数为悬停动作借用指针）。
 * @param      self 发射信号的对象；可为 NULL。
 * @param      action 悬停动作借用指针。
 * @return     不透明的 hovered 信号标识。
 */
void* XActionGroup_hovered_signal(XActionGroup* self, XAction* action);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XACTION_ON */
#endif /* XACTIONGROUP_H */
