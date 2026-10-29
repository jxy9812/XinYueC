/**
 * @file       XVfdPollScheduler.h
 * @brief      总线轮询计划表（传输无关）公开 API——兼容壳。
 * @details    [2026-10-28 公共组件提炼] 语义已原样上移至
 *             Src/XProtocol/XPoll/XPollScheduler.h/.c（XPollScheduler_*），
 *             本头只保留类型别名与函数名宏映射，既有调用点
 *             （XVfdLink.c / XVfdLink_Protected.h / XVfdDevice.h 注释）零
 *             改动。新代码请直接使用 XPollScheduler。
 * @note       [2026-10-29 动态化] XPollScheduler 条目表由固定 32 槽改为
 *             XVector 按需增长，本壳随之不再提供「容量上限」宏
 *             （XVFD_POLLSCHED_MAX_ENTRIES 随 XPOLL_SCHED_MAX_ENTRIES
 *             一并删除）；结构体成员布局亦随之变化（内嵌实例须经
 *             XVfdPollScheduler_init 初始化，零初始化内嵌不再成立）。
 */
#ifndef XVFDPOLLSCHEDULER_H
#define XVFDPOLLSCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_LINK_ON

#include "XPollScheduler.h"

/** @brief 连续失败静默阈值缺省值 */
#define XVFD_POLLSCHED_DEFAULT_MAX_FAILS XPOLL_SCHED_DEFAULT_MAX_FAILS
/** @brief 静默退避时长缺省值（毫秒） */
#define XVFD_POLLSCHED_DEFAULT_MUTE_MS XPOLL_SCHED_DEFAULT_MUTE_MS

/** @brief 轮询计划表（XPollScheduler 别名）。 */
typedef XPollScheduler XVfdPollScheduler;
/** @brief 单条轮询计划条目（XPollEntry 别名）。 */
typedef XPollEntry XVfdPollEntry;
/** @brief 轮询到期回调（XPollDueFunc 别名）。 */
typedef XPollDueFunc XVfdPollDueFunc;

/* 函数名映射（语义零变化；实现统一在 XPollScheduler.c）。 */
#define XVfdPollScheduler_init              XPollScheduler_init
#define XVfdPollScheduler_deinit            XPollScheduler_deinit
#define XVfdPollScheduler_register          XPollScheduler_register
#define XVfdPollScheduler_unregisterOwner   XPollScheduler_unregisterOwner
#define XVfdPollScheduler_setInterval       XPollScheduler_setInterval
#define XVfdPollScheduler_markSent          XPollScheduler_markSent
#define XVfdPollScheduler_reportResult      XPollScheduler_reportResult
#define XVfdPollScheduler_tick              XPollScheduler_tick
#define XVfdPollScheduler_isMuted           XPollScheduler_isMuted
#define XVfdPollScheduler_setBackoff        XPollScheduler_setBackoff

#endif /* XVFD_LINK_ON */
#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVFDPOLLSCHEDULER_H */
