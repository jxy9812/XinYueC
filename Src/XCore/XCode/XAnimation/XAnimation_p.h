/**
 * @file       XAnimation_p.h
 * @brief      XAnimation 模块内部接口（驱动器登记表）。
 * @details     仅供模块内部实现使用，不对外公开；对标
 *              QAnimationTimer::registerAnimation/unregisterAnimation 的
 *              顶层动画登记语义。
 */

#ifndef XANIMATION_P_H
#define XANIMATION_P_H

#include "XAbstractAnimation.h"

/* 顶层动画登记/注销（对标 QAnimationTimer 的动画注册表）。
 * 只有"无所属组或所属组已停止"的动画才是顶层：顶层 Running 动画由全局
 * 驱动器按帧推进；组内动画由将来的组子类（对标 QParallel/QSequential
 * AnimationGroup）在自己的时间推进里驱动。 */
void XAbstractAnimation_registerDriver_internal(XAbstractAnimation* animation);
void XAbstractAnimation_unregisterDriver_internal(XAbstractAnimation* animation);

/* 状态机内部推进入口（true=已推进）——由驱动器定时器回调调用。 */
void XAnimationDriver_advanceAnimation_internal(void);

/* 状态切换核心（对标 QAbstractAnimationPrivate::setState）；组基类在
 * 启停子动画时复用。 */
void XAbstractAnimation_setState_internal(XAbstractAnimation* animation,
                                          XAbstractAnimation_State newState);
/* 组归属维护（对标 QAnimationGroup 持有的子动画回指指针）。 */
void XAbstractAnimation_setGroup_internal(XAbstractAnimation* animation,
                                          XAnimationGroup* group);

#endif // XANIMATION_P_H
