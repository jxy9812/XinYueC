/**
 * @file       XVfdLink.h
 * @brief      总线链路基类公开 API。
 * @details    每条物理链路（一条 485 总线 / 一条 TCP 连接 / 一路 CAN）一个实例，
 *             是该链路上唯一的轮询决策者：持有传输无关的轮询计划表
 *             （XVfdPollScheduler）与从站健康模型（连续超时静默退避 +
 *             slaveLost/slaveAlive 事件），并用自带定时器统一驱动排拍
 *             （约束文档「定时器使用规范 2026-10-26 裁定」）。
 *             仲裁原语由具体传输实现提供——XVfdModbusLink·RTU 薄壳复用
 *             XModbusRtuSerialClient 既有队列/帧间延迟/广播回合，TCP/CAN 各有
 *             其语义；设备（XVfdDevice）只依赖本基类做计划登记与健康事件。
 */
#ifndef XVFDLINK_H
#define XVFDLINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_LINK_ON

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XVfdPollScheduler.h"

/** @brief 链路从站健康表容量（同链路从站数上限；线性查找） */
#define XVFD_LINK_SLAVE_CAP 32
/** @brief 排拍周期缺省值（毫秒）：每拍至多派发一条到期轮询 */
#define XVFD_LINK_DEFAULT_TICK_MS 50

/* 虚函数表：继承 XObject，不新增槽位（排拍走自带定时器 + TimerEvent 重载） */
XCLASS_DEFINE_BEGING(XVfdLink)
XCLASS_DEFINE_EXTEND_END(XVfdLink, XObject)

/**
 * @brief 从站健康条目。
 */
typedef struct XVfdLinkSlave {
    int m_unitId;               /**< 从站地址 */
    XObject* m_owner;           /**< 登记设备（借用；与计划表条目一致） */
    bool m_lost;                /**< 是否处于离线（静默）状态 */
    bool m_used;                /**< 条目在用 */
} XVfdLinkSlave;

/**
 * @brief 总线链路基类（XObject 派生；计划表 + 健康模型 + 排拍定时器）。
 */
typedef struct XVfdLink {
    XObject m_class;            /**< 基类（第一成员，由 XClass 管理，禁止手工修改） */
    XVfdPollScheduler m_sched;  /**< 轮询计划表（内嵌；init 时初始化） */
    XVfdLinkSlave m_slaves[XVFD_LINK_SLAVE_CAP]; /**< 从站健康表 */
    XTimerId m_tickTimer;       /**< 排拍定时器（startPolling 拉起） */
    uint32_t m_tickMs;          /**< 排拍周期毫秒（默认 XVFD_LINK_DEFAULT_TICK_MS） */
    bool m_polling;             /**< 轮询总闸 */
} XVfdLink;

/* ==================== 类生命周期 ==================== */

/**
 * @brief 获取 XVfdLink 类虚函数表。
 * @return 指向初始化完成的 XVtable 的指针
 */
XVtable* XVfdLink_class_init(void);

/**
 * @brief 在堆上创建并初始化链路实例。
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）
 * @return 成功返回新对象（用 XClassDelete/XObject_deleteLater 释放），失败返回 NULL
 */
XVfdLink* XVfdLink_create_ex(XMemoryType memory);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param link 链路实例（非 NULL）
 */
void XVfdLink_init(XVfdLink* link);

/* ==================== 轮询计划登记 ==================== */

/**
 * @brief 登记设备的轮询计划（设备不持定时器，节拍由本链路统一驱动）。
 * @param link 链路实例（非 NULL）
 * @param owner 登记设备（XObject 派生；到期回调第一参）
 * @param userData 用户数据（透传）
 * @param dueFunc 到期回调（非 NULL；回调内设备经链路提交轮询读）
 * @param unitId 从站地址（健康事件携带）
 * @param intervalMs 轮询周期毫秒（0=登记但暂停）
 * @return 计划条目下标；表满或参数非法返回 -1
 */
int XVfdLink_registerPoll(XVfdLink* link, XObject* owner, void* userData,
                          XVfdPollDueFunc dueFunc, int unitId, uint32_t intervalMs);

/**
 * @brief 注销某设备的轮询计划（detach 时调用）。
 * @param link 链路实例（非 NULL）
 * @param owner 登记设备
 */
void XVfdLink_unregisterPollOwner(XVfdLink* link, XObject* owner);

/**
 * @brief 修改设备轮询周期（即时生效）。
 * @param link 链路实例（非 NULL）
 * @param owner 登记设备
 * @param intervalMs 周期毫秒（0=暂停）
 * @return 找到并修改返回 true
 */
bool XVfdLink_setPollInterval(XVfdLink* link, XObject* owner, uint32_t intervalMs);

/**
 * @brief 设备成功提交轮询读后回报发送时刻（推进节拍）。
 * @param link 链路实例（非 NULL）
 * @param owner 登记设备
 * @return 找到条目返回 true
 * @note 时刻取内部单调毫秒，调用方无需传。
 */
bool XVfdLink_markPollSent(XVfdLink* link, XObject* owner);

/**
 * @brief 设备回报一次请求结果（健康记账：失败累计→静默退避→slaveLost）。
 * @param link 链路实例（非 NULL）
 * @param owner 登记设备
 * @param unitId 从站地址（健康事件携带）
 * @param ok 是否成功
 * @return 本结果是否导致该从站首次进入静默（true 时已发射 slaveLost）
 * @note 由链路适配器的应答路由调用；信号 slaveLost/slaveAlive 在此发射。
 */
bool XVfdLink_reportResult(XVfdLink* link, XObject* owner, int unitId, bool ok);

/* ==================== 轮询总闸与健康配置 ==================== */

/**
 * @brief 启动轮询（拉起排拍定时器）。
 * @param link 链路实例（非 NULL）
 */
void XVfdLink_startPolling(XVfdLink* link);

/**
 * @brief 停止轮询（停排拍定时器；计划保留）。
 * @param link 链路实例（非 NULL）
 */
void XVfdLink_stopPolling(XVfdLink* link);

/**
 * @brief 查询轮询总闸状态。
 * @param link 链路实例（可 NULL，返回 false）
 * @return 轮询中返回 true
 */
bool XVfdLink_isPolling(const XVfdLink* link);

/**
 * @brief 设置排拍周期毫秒（未启动时生效；建议 20~100）。
 * @param link 链路实例（非 NULL）
 * @param tickMs 排拍周期毫秒
 */
void XVfdLink_setTickMs(XVfdLink* link, uint32_t tickMs);

/**
 * @brief 设置从站静默阈值与退避时长。
 * @param link 链路实例（非 NULL）
 * @param maxFails 连续失败阈值（>=1）
 * @param muteMs 退避时长毫秒（>=1000）
 */
void XVfdLink_setBackoff(XVfdLink* link, uint32_t maxFails, uint32_t muteMs);

/* ==================== 信号 ==================== */

/**
 * @brief 从站离线信号：slaveLost(int unitId)。
 * @param link 链路实例（发射时非 NULL）
 * @param unitId 从站地址（发射时携带）
 * @return 信号标识（内部使用）
 * @note 以 NULL 调用仅取信号标识、不发射；该从站的轮询计划同时进入静默退避，到期探活。
 */
void* XVfdLink_slaveLost_signal(XVfdLink* link, int unitId);

/**
 * @brief 从站恢复信号：slaveAlive(int unitId)。
 * @param link 链路实例（发射时非 NULL）
 * @param unitId 从站地址（发射时携带）
 * @return 信号标识（内部使用）
 * @note 以 NULL 调用仅取信号标识、不发射。
 */
void* XVfdLink_slaveAlive_signal(XVfdLink* link, int unitId);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVfdLink_create
#define XVfdLink_create() XVfdLink_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XVFD_LINK_ON */
#endif /* XVFD_ON */
#endif /* XVFDLINK_H */
