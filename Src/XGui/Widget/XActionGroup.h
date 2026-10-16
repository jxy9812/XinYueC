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
 *             - 状态：enabled/setEnabled（转发到全部成员动作）、
 *               visible/setVisible（转发到全部成员动作，对标 Q_PROPERTY
 *               visible，qactiongroup.cpp:288-300）；
 *             - 信号：triggered(XAction*)/hovered(XAction*)（转发组内
 *               动作的 triggered/hovered，动作加入时连接、移除时断开）。
 *             XActionGroup 为逻辑分组对象，不负责成员动作的销毁
 *             （addAction_2/_3 新建的动作以组为父，随组级联释放）。
 * @note       模块沿用 XGuiConfig.h 既有控件开关模式，在 XWIDGET_ON
 *             且 XACTION_ON 下编译。
 * @note       Exclusive 与 ExclusiveOptional 的差别（"恰好一个"与"至多
 *             一个"）体现在对已选中动作的反选守卫；本库 XAction_setChecked
 *             不感知所属组（Qt 的守卫位于 QAction::activate），故两策略
 *             在组内互斥联动上行为一致，枚举保留语义表达与对齐位。
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
 * @details    仅借用不取得所有权；重复添加忽略；加入时连接动作的
 *             triggered/hovered/destroyed 信号到转发桥，并把组的
 *             enabled/visible 状态套用到该动作（对标
 *             qactiongroup.cpp:126-138 的成员状态同步）。
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
 *             qactiongroup.cpp:247-257）。
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
 * @note       Exclusive 与 ExclusiveOptional 在本库的差别边界见文件头
 *             @note（反选守卫位于 XAction 侧，未感知组）。
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
 * @details    更新分组标志并转发到全部成员动作
 *             （XAction_setEnabled）。
 * @param      self 目标分组对象；可为 NULL。
 * @param      enabled true 启用，false 禁用。
 * @return     无返回值。
 */
void XActionGroup_setEnabled(XActionGroup* self, bool enabled);

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
#endif /* XACTIONGROUP_H */
