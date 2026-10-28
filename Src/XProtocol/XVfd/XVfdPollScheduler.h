/**
 * @file       XVfdPollScheduler.h
 * @brief      总线轮询计划表（传输无关）公开 API。
 * @details    每条链路一个实例：登记各从站设备的轮询计划（owner + 周期 + 到期回调），
 *             tick 统一排拍——按"上次发送时刻 + 周期"全局轮转、错峰触发；从站连续
 *             超时进入静默退避（跳拍），到期探活恢复。本结构是纯逻辑组件（无定时器、
 *             无 IO）：由链路（XVfdLink）持有并驱动，到期回调里由设备经链路提交
 *             轮询读——485 总线的仲裁在链路的传输适配器内完成，与本表解耦。
 */
#ifndef XVFDPOLLSCHEDULER_H
#define XVFDPOLLSCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_LINK_ON

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"

/** @brief 计划表容量上限（同一条链路上登记的设备数上限） */
#define XVFD_POLLSCHED_MAX_ENTRIES 32
/** @brief 连续失败静默阈值缺省值 */
#define XVFD_POLLSCHED_DEFAULT_MAX_FAILS 3
/** @brief 静默退避时长缺省值（毫秒） */
#define XVFD_POLLSCHED_DEFAULT_MUTE_MS 5000

/**
 * @brief 轮询到期回调：由链路 tick 经计划表调用，设备在此构建并提交轮询读。
 * @param owner 登记时的设备对象
 * @param userData 登记时的用户数据
 */
typedef void (*XVfdPollDueFunc)(XObject* owner, void* userData);

/**
 * @brief 单条轮询计划条目。
 */
typedef struct XVfdPollEntry {
    XObject* m_owner;           /**< 登记设备（借用，不拥有） */
    void* m_userData;           /**< 登记用户数据（透传给到期回调） */
    XVfdPollDueFunc m_due;      /**< 到期回调（非 NULL） */
    int m_unitId;               /**< 从站地址（健康事件携带） */
    uint32_t m_intervalMs;      /**< 轮询周期毫秒（0=暂停该条目） */
    uint64_t m_lastSentMs;      /**< 上次成功提交时刻（单调毫秒；0=立即可发） */
    uint32_t m_failCount;       /**< 连续失败计数（成功清零） */
    uint64_t m_muteUntilMs;     /**< 静默退避截止时刻（单调毫秒；0=未静默） */
    bool m_active;              /**< 条目是否在用 */
} XVfdPollEntry;

/**
 * @brief 轮询计划表（纯逻辑组件；由 XVfdLink 持有并驱动）。
 */
typedef struct XVfdPollScheduler {
    XVfdPollEntry m_entries[XVFD_POLLSCHED_MAX_ENTRIES]; /**< 条目数组（固定容量） */
    int m_count;                /**< 在用条目数 */
    int m_cursor;               /**< 轮转游标（错峰） */
    uint32_t m_maxFails;        /**< 连续失败静默阈值 */
    uint32_t m_muteMs;          /**< 静默退避时长毫秒 */
} XVfdPollScheduler;

/**
 * @brief 初始化计划表（与 XVfdPollScheduler_deinit 成对）。
 * @param sched 计划表（非 NULL）
 */
void XVfdPollScheduler_init(XVfdPollScheduler* sched);

/**
 * @brief 反初始化计划表并清空全部条目。
 * @param sched 计划表（非 NULL）
 */
void XVfdPollScheduler_deinit(XVfdPollScheduler* sched);

/**
 * @brief 登记一条轮询计划。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备（借用；到期回调第一参）
 * @param userData 用户数据（透传）
 * @param dueFunc 到期回调（非 NULL）
 * @param unitId 从站地址（健康事件携带）
 * @param intervalMs 轮询周期毫秒（0=登记但暂停）
 * @return 条目下标（0 基）；表满返回 -1
 */
int XVfdPollScheduler_register(XVfdPollScheduler* sched, XObject* owner, void* userData,
                               XVfdPollDueFunc dueFunc, int unitId, uint32_t intervalMs);

/**
 * @brief 注销某设备的全部计划条目。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 */
void XVfdPollScheduler_unregisterOwner(XVfdPollScheduler* sched, XObject* owner);

/**
 * @brief 修改某设备的轮询周期（即时生效）。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 * @param intervalMs 周期毫秒（0=暂停）
 * @return 找到并修改返回 true
 */
bool XVfdPollScheduler_setInterval(XVfdPollScheduler* sched, XObject* owner, uint32_t intervalMs);

/**
 * @brief 设备成功提交轮询读后回报发送时刻（推进该条目的节拍）。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 * @param nowMs 当前单调毫秒
 * @return 找到条目返回 true
 */
bool XVfdPollScheduler_markSent(XVfdPollScheduler* sched, XObject* owner, uint64_t nowMs);

/**
 * @brief 设备回报一次请求结果（成功清失败计数；失败累计并触发静默退避）。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 * @param ok 是否成功
 * @param nowMs 当前单调毫秒
 * @return 是否因本结果进入/维持静默（供链路发 slaveLost 事件）
 */
bool XVfdPollScheduler_reportResult(XVfdPollScheduler* sched, XObject* owner, bool ok, uint64_t nowMs);

/**
 * @brief 推进一拍：找出下一条到期条目并调用其到期回调（每拍至多一条，错峰）。
 * @param sched 计划表（非 NULL）
 * @param nowMs 当前单调毫秒
 * @return 本拍有派发返回 true；无到期条目返回 false
 * @note 到期回调内设备是否成功提交由 markSent 决定；未提交则下拍重试。
 */
bool XVfdPollScheduler_tick(XVfdPollScheduler* sched, uint64_t nowMs);

/**
 * @brief 查询设备当前是否处于静默退避（链路健康事件判定用）。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 * @param nowMs 当前单调毫秒
 * @return 静默中返回 true
 */
bool XVfdPollScheduler_isMuted(XVfdPollScheduler* sched, XObject* owner, uint64_t nowMs);

/**
 * @brief 设置静默阈值与退避时长。
 * @param sched 计划表（非 NULL）
 * @param maxFails 连续失败阈值（>=1）
 * @param muteMs 退避时长毫秒（>=1000）
 */
void XVfdPollScheduler_setBackoff(XVfdPollScheduler* sched, uint32_t maxFails, uint32_t muteMs);

#endif /* XVFD_LINK_ON */
#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVFDPOLLSCHEDULER_H */
