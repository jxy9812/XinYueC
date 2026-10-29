/**
 * @file       XPollScheduler.h
 * @brief      通用总线轮询计划表（传输无关纯逻辑组件）公开 API。
 * @details    [2026-10-28 公共组件提炼] 语义自 XVfdPollScheduler 原样上移：
 *             每张表登记若干轮询计划条目（owner 或 unit 键 + 周期 + 到期回调），
 *             tick 统一排拍——按「上次发送时刻 + 周期」固定容量条目 + 游标
 *             轮转、错峰触发；条目连续失败达阈值进入静默退避（跳拍），到期
 *             探活恢复。本组件是纯逻辑表（无定时器、无 IO、零系统调用）：
 *             时刻一律由调用方注入 nowMs，驱动方（XVfdLink / XModbusHub 等）
 *             用各自的 XObject 自带定时器拉拍。
 *             另有面向聚合设备的 key 注册扩展（registerKey/reportResultKey/
 *             isMutedKey/isLostKey：owner 为空、按 unitId 寻址），供
 *             (设备, 从站) 粒度的失联退避簿记——同一张表内 owner 登记与
 *             key 登记互不混淆（key 查找只命中 owner 为空的条目）。
 */
#ifndef XPOLLSCHEDULER_H
#define XPOLLSCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XProtocol_config.h"

#if XPOLL_SCHEDULER_ON

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"

/** @brief 计划表容量上限（同一张表登记的条目数上限） */
#define XPOLL_SCHED_MAX_ENTRIES 32
/** @brief 连续失败静默阈值缺省值 */
#define XPOLL_SCHED_DEFAULT_MAX_FAILS 3
/** @brief 静默退避时长缺省值（毫秒） */
#define XPOLL_SCHED_DEFAULT_MUTE_MS 5000

/**
 * @brief 轮询到期回调：由驱动方 tick 经计划表调用，设备在此构建并提交轮询读。
 * @param owner 登记时的设备对象（key 登记的条目为 NULL）
 * @param userData 登记时的用户数据
 */
typedef void (*XPollDueFunc)(XObject* owner, void* userData);

/**
 * @brief 单条轮询计划条目。
 */
typedef struct XPollEntry {
    XObject* m_owner;           /**< 登记设备（借用，不拥有；key 登记=NULL） */
    void* m_userData;           /**< 登记用户数据（透传给到期回调） */
    XPollDueFunc m_due;         /**< 到期回调（NULL=纯簿记条目，tick 跳过） */
    int m_unitId;               /**< 从站地址/登记键（健康事件携带） */
    uint32_t m_intervalMs;      /**< 轮询周期毫秒（0=暂停该条目） */
    uint64_t m_lastSentMs;      /**< 上次成功提交时刻（单调毫秒；0=立即可发） */
    uint32_t m_failCount;       /**< 连续失败计数（成功清零） */
    uint64_t m_muteUntilMs;     /**< 静默退避截止时刻（单调毫秒；0=未静默） */
    bool m_active;              /**< 条目是否在用 */
} XPollEntry;

/**
 * @brief 轮询计划表（纯逻辑组件；由链路/聚合设备等驱动方持有并驱动）。
 */
typedef struct XPollScheduler {
    XPollEntry m_entries[XPOLL_SCHED_MAX_ENTRIES]; /**< 条目数组（固定容量） */
    int m_count;                /**< 在用条目数 */
    int m_cursor;               /**< 轮转游标（错峰） */
    uint32_t m_maxFails;        /**< 连续失败静默阈值 */
    uint32_t m_muteMs;          /**< 静默退避时长毫秒 */
} XPollScheduler;

/* ==================== 生命周期 ==================== */

/**
 * @brief 初始化计划表（与 XPollScheduler_deinit 成对）。
 * @param sched 计划表（非 NULL）
 */
void XPollScheduler_init(XPollScheduler* sched);

/**
 * @brief 反初始化计划表并清空全部条目。
 * @param sched 计划表（非 NULL）
 */
void XPollScheduler_deinit(XPollScheduler* sched);

/* ==================== 登记（owner 键：XVfd 链路语义，原样上移） ==================== */

/**
 * @brief 登记一条轮询计划（同设备重复登记拒绝）。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备（借用；到期回调第一参）
 * @param userData 用户数据（透传）
 * @param dueFunc 到期回调（非 NULL）
 * @param unitId 从站地址（健康事件携带）
 * @param intervalMs 轮询周期毫秒（0=登记但暂停）
 * @return 条目下标（0 基）；表满或参数非法返回 -1
 */
int XPollScheduler_register(XPollScheduler* sched, XObject* owner, void* userData,
                            XPollDueFunc dueFunc, int unitId, uint32_t intervalMs);

/**
 * @brief 注销某设备的全部计划条目。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 */
void XPollScheduler_unregisterOwner(XPollScheduler* sched, XObject* owner);

/**
 * @brief 修改某设备的轮询周期（即时生效）。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 * @param intervalMs 周期毫秒（0=暂停）
 * @return 找到并修改返回 true
 */
bool XPollScheduler_setInterval(XPollScheduler* sched, XObject* owner, uint32_t intervalMs);

/* ==================== 登记（unit 键扩展：XModbusHub (设备,从站) 粒度簿记） ==================== */

/**
 * @brief 按 unit 键登记一条纯簿记条目（due/owner 均空，tick 自动跳过；
 *        同键重复登记拒绝）。供聚合设备做 (设备, 从站) 粒度失联退避，
 *        节拍由驱动方自有定时器负责，不经 tick 派发。
 * @param sched 计划表（非 NULL）
 * @param key 登记键（从站地址；>=0）
 * @param intervalMs 预留周期毫秒（0=不参与 tick 派发）
 * @return 条目下标（0 基）；表满或参数非法返回 -1
 */
int XPollScheduler_registerKey(XPollScheduler* sched, int key, uint32_t intervalMs);

/* ==================== 节拍与健康簿记 ==================== */

/**
 * @brief 设备成功提交轮询读后回报发送时刻（推进该条目的节拍）。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 * @param nowMs 当前单调毫秒
 * @return 找到条目返回 true
 */
bool XPollScheduler_markSent(XPollScheduler* sched, XObject* owner, uint64_t nowMs);

/**
 * @brief 设备回报一次请求结果（成功清失败计数；失败累计并触发静默退避）。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 * @param ok 是否成功
 * @param nowMs 当前单调毫秒
 * @return 是否因本结果进入/维持静默（供驱动方发失联事件）
 */
bool XPollScheduler_reportResult(XPollScheduler* sched, XObject* owner, bool ok, uint64_t nowMs);

/**
 * @brief reportResult 的 unit 键版（语义同上；只命中 owner 为空的条目）。
 * @param sched 计划表（非 NULL）
 * @param key 登记键（registerKey 的 key）
 * @param ok 是否成功
 * @param nowMs 当前单调毫秒
 * @return 是否因本结果进入/维持静默
 */
bool XPollScheduler_reportResultKey(XPollScheduler* sched, int key, bool ok, uint64_t nowMs);

/**
 * @brief 推进一拍：找出下一条到期条目并调用其到期回调（每拍至多一条，错峰）。
 * @param sched 计划表（非 NULL）
 * @param nowMs 当前单调毫秒
 * @return 本拍有派发返回 true；无到期条目返回 false
 * @note 到期回调内设备是否成功提交由 markSent 决定；未提交则下拍重试。
 *       纯簿记条目（due 为空，registerKey 登记）恒被跳过。
 */
bool XPollScheduler_tick(XPollScheduler* sched, uint64_t nowMs);

/**
 * @brief 查询设备当前是否处于静默退避（驱动方健康事件判定用）。
 * @param sched 计划表（非 NULL）
 * @param owner 登记设备
 * @param nowMs 当前单调毫秒
 * @return 静默中返回 true
 */
bool XPollScheduler_isMuted(XPollScheduler* sched, XObject* owner, uint64_t nowMs);

/**
 * @brief isMuted 的 unit 键版（只命中 owner 为空的条目）。
 * @param sched 计划表（非 NULL）
 * @param key 登记键（registerKey 的 key）
 * @param nowMs 当前单调毫秒
 * @return 静默中返回 true
 */
bool XPollScheduler_isMutedKey(XPollScheduler* sched, int key, uint64_t nowMs);

/**
 * @brief isMuted 的 unit 键失联版：静默中 OR 静默到期但尚未收到成功应答
 *        （探活在途）均视为失联。驱动方据此判定 lost→alive 跃迁发恢复事件。
 * @param sched 计划表（非 NULL）
 * @param key 登记键（registerKey 的 key）
 * @return 失联（muteUntil 非零）返回 true
 */
bool XPollScheduler_isLostKey(XPollScheduler* sched, int key);

/**
 * @brief 设置静默阈值与退避时长。
 * @param sched 计划表（非 NULL）
 * @param maxFails 连续失败阈值（>=1）
 * @param muteMs 退避时长毫秒（>=1000）
 */
void XPollScheduler_setBackoff(XPollScheduler* sched, uint32_t maxFails, uint32_t muteMs);

#endif /* XPOLL_SCHEDULER_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPOLLSCHEDULER_H */
