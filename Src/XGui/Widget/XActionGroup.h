/**
 * @file       XActionGroup.h
 * @brief      XActionGroup 动作逻辑分组（对标 Qt 6.8 QActionGroup 核心
 *             公共 API；QActionGroup : QObject，本类 XObject 派生）。
 * @details    功能范围：
 *             - 成员管理：addAction（返回动作本身）/addAction_2（新建
 *               动作并入组，对标 addAction(text)）/addAction_3（新建带
 *               图标动作并入组，对标 addAction(icon,text)）/
 *               removeAction/actions（新建 XVector<XAction*>，调用方
 *               释放）；
 *             - 选中：checkedAction/setCheckedAction（setCheckedAction
 *               触发 triggered 信号；Qt 6.8.3 无此 API，XGui 扩展，
 *               对标 QButtonGroup::setCheckedButton 语义，@note）；
 *             - 互斥：ExclusionPolicy 三态策略（None/Exclusive/
 *               ExclusiveOptional，对标 QActionGroup::ExclusionPolicy）
 *               与 exclusionPolicy/setExclusionPolicy；exclusive/
 *               setExclusive 作为 Exclusive/None 的便捷映射保留（对标
 *               qactiongroup.cpp:247-262）。选中某动作时自动取消其它
 *               动作的选中（XAction_setChecked）；
 *             - 状态：enabled/setEnabled（转发到全部成员动作，组成员侧
 *               按「显式启用位」裁决：组禁用一律生效，组启用时显式设置过
 *               启用态的成员按显式值裁决，对标 qaction.cpp:940-951）、
 *               visible/setVisible（转发到全部成员动作，对标 Q_PROPERTY
 *               visible，qactiongroup.cpp:288-300）；
 *             - 回指：加入/移除/组析构时维护成员动作的 XAction::m_group
 *               （对标 QActionPrivate::group 的 QPointer，qaction_p.h:55）；
 *               移除与组析构都会清空回指，并恢复该动作被组强制修改前的
 *               启用态（XGui 扩展，见 XAction_bindGroup /
 *               XAction_detachGroupQuiet）。
 *             - 信号：triggered(XAction*)/hovered(XAction*)（转发组内
 *               动作的 triggered/hovered，动作加入时连接、移除时断开）。
 *             XActionGroup 为逻辑分组对象，不负责成员动作的销毁
 *             （addAction_2/_3 新建的动作以组为父，随组级联释放）。
 * @note       模块沿用 XGuiConfig.h 既有控件开关模式，在 XWIDGET_ON
 *             且 XACTION_ON 下编译。
 * @note       Exclusive 与 ExclusiveOptional 的差别（"恰好一个"与"至多
 *             一个"）体现在对已选中动作的反选守卫（P0-38 起已实现）：
 *             Qt 的守卫位于 QAction::activate（qaction.cpp:1092-1099），
 *             组策略为 Exclusive 且该动作是组选中项时，activate 不翻转
 *             checked、只发 triggered(true)；ExclusiveOptional 无此守卫
 *             （可反选）。本库把守卫落在 XAction_activate，所需的两个组侧
 *             条件（策略 Exclusive / 组选中位）以缓存位由本组同步给成员
 *             （沿用 m_groupEnabled 的「XCore 不反向依赖 XGui」模式，
 *             见 XAction_setGroupExclusiveByGroup /
 *             XAction_setGroupCheckedByGroup）。
 * @author     XinYueC 团队
 */
#ifndef XACTIONGROUP_H
#define XACTIONGROUP_H

#if XGUI_ON
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

/**
 * @brief      组互斥检查策略（对标 QActionGroup::ExclusionPolicy，
 *             qactiongroup.h:27-32；数值按枚举序对齐）。
 */
typedef enum XActionGroupExclusionPolicy
{
    XActionGroupExclusionPolicy_None = 0,              /**< 各动作独立选中（对标 None）。 */
    XActionGroupExclusionPolicy_Exclusive = 1,         /**< 任意时刻恰好一个选中（默认；对标 Exclusive）。 */
    XActionGroupExclusionPolicy_ExclusiveOptional = 2  /**< 至多一个选中（对标 ExclusiveOptional）。 */
} XActionGroupExclusionPolicy;

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
    XObject     m_base;            /**< 基类成员；必须是第一个。 */
    XVector*    m_actions;         /**< 成员动作借用指针数组（XAction*）。 */
    XVector*    m_bridges;         /**< 与成员顺序一致的内信号桥（XActionGroupBridge*）。 */
    XAction*    m_checkedAction;   /**< 当前选中动作（借用；可为 NULL）。 */
    /* [内存治理] 策略 0..2（XActionGroupExclusionPolicy）+ 两个 bool
       合并 1 字。 */
    uint8_t     m_exclusionPolicy : 2; /**< 互斥策略（默认 Exclusive）。 */
    uint8_t     m_enabled : 1;     /**< 分组启用状态（默认 true）。 */
    uint8_t     m_visible : 1;     /**< 分组可见状态（默认 true，对标 Q_PROPERTY visible）。 */
    uint8_t     m_reserved : 4;    /**< 保留位。 */
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
 *             XClassDelete 释放。
 */
#define XActionGroup_create(parent) \
    XActionGroup_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))
XActionGroup* XActionGroup_create_ex(XMemoryType memory, XObject* parent);


/* ==================== 成员管理（对标 QActionGroup） ==================== */

/**
 * @brief      添加动作到组（对标 QActionGroup::addAction(QAction*)）。
 * @details    仅借用不取得所有权；重复添加忽略（但仍按 Qt 重新套用组的
 *             enabled/visible 状态）；加入时连接动作的
 *             triggered/hovered/destroyed 信号到转发桥，写入动作的组回指
 *             （XAction_bindGroup），并把组的 enabled/visible 状态套用到
 *             该动作（对标 qactiongroup.cpp:126-138 的成员状态同步——
 *             组启用时显式禁用的成员保持禁用，组禁用一律禁用）。
 *             动作已属于其它组时先从旧组 removeAction（qactiongroup.cpp:
 *             139-146），旧组不再保留该成员。
 * @param      self 目标分组对象；可为 NULL。
 * @param      action 成员动作借用指针；可为 NULL（忽略）。
 * @return     加入的动作本身（便于链式书写，对标 Qt 返回 QAction*）；
 *             self/action 为 NULL 或加入失败返回 NULL。
 */
XAction* XActionGroup_addAction(XActionGroup* self, XAction* action);

/**
 * @brief      新建带文本的动作并入组（对标 QActionGroup::addAction(
 *             const QString&)，qactiongroup.cpp:159-162）。
 * @details    动作以组为父对象（对标 new QAction(text, this)），随组
 *             级联释放；加入流程同 addAction。
 * @param      self 目标分组对象；可为 NULL。
 * @param      utf8Text 新动作文本（UTF-8）；可为 NULL 表示空文本。
 * @return     新建动作指针；分配失败返回 NULL。
 */
XAction* XActionGroup_addAction_2(XActionGroup* self, const char* utf8Text);

/**
 * @brief      新建带图标与文本的动作并入组（对标 QActionGroup::
 *             addAction(const QIcon&, const QString&)，
 *             qactiongroup.cpp:173-176）。
 * @details    图标以路径承载（XAction_setIcon 语义）；动作以组为父
 *             对象，随组级联释放；加入流程同 addAction。
 * @param      self 目标分组对象；可为 NULL。
 * @param      iconUtf8 图标路径（UTF-8）；可为 NULL 表示无图标。
 * @param      utf8Text 新动作文本（UTF-8）；可为 NULL 表示空文本。
 * @return     新建动作指针；分配失败返回 NULL。
 */
XAction* XActionGroup_addAction_3(XActionGroup* self, const char* iconUtf8,
                                  const char* utf8Text);

/**
 * @brief      从组移除动作（对标 QActionGroup::removeAction）。
 * @details    断开转发桥连接并删除桥；动作对象本身不销毁。
 *             同时清空动作的组回指（对标 qactiongroup.cpp:193
 *             `action->d_func()->group = nullptr`），并恢复该动作被组
 *             强制修改前的启用态——显式启用值优先，否则恢复默认启用，
 *             仍受 visible 约束（XGui 有意扩展：本机 Qt 6.8 探针实测
 *             Qt 移除后成员保持组写入的禁用态）。
 * @param      self 目标分组对象；可为 NULL。
 * @param      action 成员动作借用指针；可为 NULL（忽略）。
 * @return     无返回值。
 */
void XActionGroup_removeAction(XActionGroup* self, XAction* action);

/**
 * @brief      返回成员动作列表（对标 QActionGroup::actions）。
 * @details    Qt 返回 QList<QAction*>；XGui 返回新建的
 *             XVector<XAction*>（按加入顺序），由调用方拥有，使用后
 *             必须 XClassDelete 释放；无成员返回空数组。
 * @param      self 目标分组对象；可为 NULL。
 * @return     新建的 XVector（元素为 XAction*）；分配失败返回 NULL。
 */
XVector* XActionGroup_actions(const XActionGroup* self);

/* ==================== 选中与状态（对标 QActionGroup） ==================== */

/**
 * @brief      查询当前选中动作（对标 QActionGroup::checkedAction）。
 * @details    返回**缓存**的选中动作（对标 qactiongroup.cpp:296-301
 *             `return d->current.data()`），不是"扫描成员里第一个
 *             isChecked"。缓存的写入时机与 Qt `_q_actionChanged`
 *             （qactiongroup.cpp:25-42）逐条同构：
 *             - 仅当策略非 None 时才维护；
 *             - 成员变选中：若缓存不是它，先把旧缓存动作 setChecked(false)，
 *               再记它（`if (action != d->current) { ... d->current = action; }`）；
 *             - 成员取消选中且它就是缓存：清 NULL。
 *             `addAction` 加入时若动作已选中则直接记缓存（不论策略，
 *             qactiongroup.cpp:136-137 `if (a->isChecked()) d->current = a;`）。
 *             本机 Qt 6.8 实测（QGuiApplication 探针）：非互斥组同时选中
 *             A/B 后 checkedAction() 为 **NULL**（策略 None 不跟踪）；
 *             而 addAction 一个已选中的动作后 checkedAction() 为该动作。
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
 * @brief      查询互斥标志（对标 QActionGroup::isExclusive）。
 * @details    策略非 None（Exclusive 或 ExclusiveOptional）即为互斥
 *             （对标 qactiongroup.cpp:263-270）。
 * @param      self 目标分组对象；可为 NULL。
 * @return     互斥返回 true；self 为 NULL 返回 false。
 */
bool XActionGroup_isExclusive(const XActionGroup* self);

/**
 * @brief      设置互斥标志（对标 QActionGroup::setExclusive）。
 * @details    便捷映射：true→Exclusive，false→None（对标
 *             qactiongroup.cpp:247-257）；策略逐成员下发
 *             （setExclusionPolicy 同路）。
 * @param      self 目标分组对象；可为 NULL。
 * @param      exclusive true 互斥，false 允许多选。
 * @return     无返回值。
 */
void XActionGroup_setExclusive(XActionGroup* self, bool exclusive);

/**
 * @brief      查询互斥检查策略（对标 QActionGroup::exclusionPolicy）。
 * @param      self 目标分组对象；可为 NULL。
 * @return     策略枚举；self 为 NULL 返回 None。
 */
XActionGroupExclusionPolicy XActionGroup_exclusionPolicy(
    const XActionGroup* self);

/**
 * @brief      设置互斥检查策略（对标 QActionGroup::setExclusionPolicy，
 *             qactiongroup.cpp:239-245）。
 * @details    已有成员的选中态不回溯重排（Qt 亦然）；但策略位会逐成员
 *             下发缓存（XAction_setGroupExclusiveByGroup，P0-38 反选守卫
 *             的 Exclusive 位），守卫随后续 activate 即时生效。
 * @note       Exclusive 与 ExclusiveOptional 在本库的差别边界见文件头
 *             @note（反选守卫位于 XAction_activate）。
 * @param      self 目标分组对象；可为 NULL。
 * @param      policy 策略枚举值。
 * @return     无返回值。
 */
void XActionGroup_setExclusionPolicy(XActionGroup* self,
                                     XActionGroupExclusionPolicy policy);

/**
 * @brief      查询分组启用状态（对标 QActionGroup::isEnabled；默认 true）。
 * @param      self 目标分组对象；可为 NULL。
 * @return     启用返回 true；self 为 NULL 返回 false。
 */
bool XActionGroup_isEnabled(const XActionGroup* self);

/**
 * @brief      设置分组启用状态（对标 QActionGroup::setEnabled）。
 * @details    更新分组标志并转发到全部成员动作。Qt 6.8 的组级
 *             setEnabled 无同值早退（qactiongroup.cpp:276-283 无条件
 *             转发）；成员侧按 QAction 的「显式启用位」裁决是否允许被组
 *             覆盖（qaction.cpp:940-951 `if (b && byGroup &&
 *             explicitEnabled) b = explicitEnabledValue;`）——
 *             本库经 XAction_setEnabledByGroup 同口径：
 *             ① 组禁用一律生效（显式启用不豁免）；
 *             ② 组启用时，显式禁用过的成员保持禁用，显式启用过的成员
 *                启用，从未显式设置过的成员按默认启用（受 visible 约束）；
 *             ③ 组路径不改写成员的显式位，组禁用/启用往返不会把成员
 *                永久钉在某个值上。
 * @param      self 目标分组对象；可为 NULL。
 * @param      enabled true 启用，false 禁用。
 * @return     无返回值。
 */
void XActionGroup_setEnabled(XActionGroup* self, bool enabled);

/**
 * @brief      设置分组禁用状态（对标 QActionGroup::setDisabled）。
 * @details    Qt 的 `setDisabled(bool b)` 是 `setEnabled(!b)` 的内联便捷
 *             形态（qactiongroup.h:76），便于信号槽连接；语义完全等价
 *             于取反后调用 XActionGroup_setEnabled。
 * @param      self 目标分组对象；可为 NULL。
 * @param      disabled true 禁用（= setEnabled(false)），false 启用。
 * @return     无返回值。
 */
void XActionGroup_setDisabled(XActionGroup* self, bool disabled);

/**
 * @brief      查询分组可见状态（对标 QActionGroup::isVisible，默认 true）。
 * @param      self 目标分组对象；可为 NULL。
 * @return     可见返回 true；self 为 NULL 返回 false。
 */
bool XActionGroup_isVisible(const XActionGroup* self);

/**
 * @brief      设置分组可见状态（对标 QActionGroup::setVisible，
 *             qactiongroup.cpp:288-300）。
 * @details    更新分组标志并转发到全部成员动作
 *             （XAction_setVisible）；Qt 的 forceInvisible 豁免（控件
 *             内嵌动作）本库无对应概念，全部成员跟随。
 * @param      self 目标分组对象；可为 NULL。
 * @param      visible true 可见，false 隐藏。
 * @return     无返回值。
 */
void XActionGroup_setVisible(XActionGroup* self, bool visible);

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

#endif /* XGUI_ON */

#endif /* XACTIONGROUP_H */
