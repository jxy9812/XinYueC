/**
 * @file       XAnimationGroup.h
 * @brief      动画组基类公开 API。
 * @details    对齐 Qt 6.8 QAnimationGroup（QtCore 动画模块）；实现只依赖
 *             XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @note       与 Qt 一致：组基类只负责子动画容器管理，时长与时间级联由
 *             并行/顺序组子类（对标 QParallelAnimationGroup/
 *             QSequentialAnimationGroup，后续批次）实现。
 */
#ifndef XANIMATIONGROUP_H
#define XANIMATIONGROUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XAbstractAnimation.h"
#include "XVector.h"

XCLASS_DEFINE_BEGING(XAnimationGroup)
XCLASS_DEFINE_EXTEND_END(XAnimationGroup, XAbstractAnimation);

/**
 * @brief 动画容器基类，对应 Qt 6.8 的 QAnimationGroup。
 */
typedef struct XAnimationGroup {
    XAbstractAnimation m_class; ///< 继承 XAbstractAnimation，必须位于结构体第一位。
    XVector* m_animations;      ///< 子动画列表，元素类型为 XAbstractAnimation*。
} XAnimationGroup;

/**
 * @brief 初始化 XAnimationGroup 虚函数表。
 * @return XAnimationGroup 的共享虚函数表。
 */
XVtable* XAnimationGroup_class_init(void);
/**
 * @brief 创建无父对象的动画组（对标 QAnimationGroup(QObject*)）。
 * @param memory 内存类型。
 * @return 新动画组；内存分配失败返回 NULL。
 * @note 组基类的 duration 恒为 0（Qt 为纯虚）；业务请使用后续批次的
 *       并行/顺序组派生类。
 */
XAnimationGroup* XAnimationGroup_create_ex(XMemoryType memory);
/**
 * @brief 初始化动画组基类部分（栈对象或派生类构造场景）。
 * @param group 调用者提供的未初始化存储。
 */
void XAnimationGroup_init(XAnimationGroup* group);

/**
 * @brief 把动画加入组并转移父权（对标 QAnimationGroup::addAnimation）。
 * @param group 目标组。
 * @param animation 待加入动画；animation 为 NULL 时被拒绝。
 * @return 加入成功返回 true。
 * @note 对标 Qt insertAnimation：已属于其他组的动画先从旧组移出再插入
 *       （自动搬运，不拒绝）；组取得 animation 的所有权。
 */
bool XAnimationGroup_addAnimation(XAnimationGroup* group, XAbstractAnimation* animation);
/**
 * @brief 把动画移出组（对标 QAnimationGroup::removeAnimation）。
 * @param group 当前持有该动画的组。
 * @param animation 待移除动画；所有权转交调用者。
 * @note 组被取空时对标 animationRemoved：组自停并把当前时间归零。
 */
void XAnimationGroup_removeAnimation(XAnimationGroup* group, XAbstractAnimation* animation);
/**
 * @brief 获取子动画数量（对标 QAnimationGroup::animationCount）。
 * @param group 目标组。
 * @return 子动画数量；group 为 NULL 时返回 0。
 */
int XAnimationGroup_animationCount(const XAnimationGroup* group);
/**
 * @brief 按下标获取子动画（对标 QAnimationGroup::animationAt）。
 * @param group 目标组。
 * @param index 下标（0 起）。
 * @return 子动画；越界或 group 为 NULL 时返回 NULL。
 * @warning 借用语义：返回值归组所有，调用者不得释放。
 */
XAbstractAnimation* XAnimationGroup_animationAt(const XAnimationGroup* group, int index);
/**
 * @brief 查找动画下标（对标 QAnimationGroup::indexOfAnimation）。
 * @param group 目标组。
 * @param animation 待查找动画。
 * @return 下标；未找到或参数为 NULL 时返回 -1。
 */
int XAnimationGroup_indexOfAnimation(const XAnimationGroup* group,
                                     const XAbstractAnimation* animation);
/**
 * @brief 移除指定下标的动画并交还所有权（对标 QAnimationGroup::takeAnimation）。
 * @param group 目标组。
 * @param index 下标（0 起）。
 * @return 被移除的动画；越界或 group 为 NULL 时返回 NULL。移除后归调用者管理。
 */
XAbstractAnimation* XAnimationGroup_takeAnimation(XAnimationGroup* group, int index);
/**
 * @brief 清空并销毁全部子动画（对标 QAnimationGroup::clear）。
 * @param group 目标组。
 * @note 与 Qt 一致：子动画随父对象所有权约定由组释放。
 */
void XAnimationGroup_clear(XAnimationGroup* group);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XAnimationGroup_create
#define XAnimationGroup_create() \
	XAnimationGroup_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XANIMATIONGROUP_H
