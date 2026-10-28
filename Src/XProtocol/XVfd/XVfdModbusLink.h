/**
 * @file       XVfdModbusLink.h
 * @brief      Modbus 链路适配器公开 API。
 * @details    XVfdLink 的 Modbus 实现：内部持有 XModbusClient（借用）——
 *             RTU 串口（XModbusRtuSerialClient，485 总线主站：单飞/请求队列/
 *             按波特率帧间延迟/广播回合全部复用其既有仲裁）或 Modbus/TCP
 *             （XModbusTcpClient，全双工无仲裁）；RTU-over-TCP 网关场景由
 *             调用方为 RTU 客户端注入外部 IO 后同样适用。
 *             职责：请求提交与应答路由（finished 信号→登记回调）、设备级结果
 *             回报（健康记账交基类）、忙碌判定（计划表顺延依据）。
 *             与 XVfdModbusBase 及品牌子类按 §7.5 配对约定组合。
 */
#ifndef XVFDMODBUSLINK_H
#define XVFDMODBUSLINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_LINK_ON

#include <stdint.h>
#include <stdbool.h>
#include "XVfdLink.h"
#include "XModbusClient.h"
#include "XModbusDataUnit.h"

/** @brief 在途应答登记容量（同链路同时未收口的提交数上限） */
#define XVFD_MODBUS_LINK_PENDING_CAP 32

/**
 * @brief 提交完成回调：应答收口（成功或失败）时由链路路由调用。
 * @param owner 提交时登记的设备对象
 * @param userData 提交时的用户数据
 * @param ok 应答是否成功（error==NoError）
 * @param result 应答数据单元（只读借用，回调返回后失效；失败为 NULL）
 */
typedef void (*XVfdModbusLinkDoneFunc)(XObject* owner, void* userData, bool ok,
                                       const XModbusDataUnit* result);

/**
 * @brief Modbus 链路适配器（XObject 派生，继承 XVfdLink）。
 */
XCLASS_DEFINE_BEGING(XVfdModbusLink)
XCLASS_DEFINE_EXTEND_END(XVfdModbusLink, XVfdLink)

/**
 * @brief 在途登记条目。
 */
typedef struct XVfdModbusPending {
    XModbusReply* m_reply;      /**< 在途应答（路由后 deleteLater） */
    XObject* m_owner;           /**< 提交设备（借用） */
    void* m_userData;           /**< 用户数据（透传） */
    XVfdModbusLinkDoneFunc m_done; /**< 完成回调（非 NULL） */
    int m_unitId;               /**< 从站地址（健康记账） */
    bool m_used;                /**< 条目在用 */
} XVfdModbusPending;

/**
 * @brief Modbus 链路适配器结构体。
 */
typedef struct XVfdModbusLink {
    XVfdLink m_base;            /**< 基类子对象（第一成员） */
    XModbusClient* m_client;    /**< 底层 Modbus 客户端（借用，不转移所有权） */
    XVfdModbusPending m_pending[XVFD_MODBUS_LINK_PENDING_CAP]; /**< 在途登记表 */
    int m_outstanding;          /**< 在途提交数（忙碌判定） */
} XVfdModbusLink;

/* ==================== 类生命周期 ==================== */

/**
 * @brief 获取 XVfdModbusLink 类虚函数表。
 * @return 指向初始化完成的 XVtable 的指针
 */
XVtable* XVfdModbusLink_class_init(void);

/**
 * @brief 在堆上创建并初始化 Modbus 链路实例。
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）
 * @return 成功返回新对象（用 XClassDelete/XObject_deleteLater 释放），失败返回 NULL
 */
XVfdModbusLink* XVfdModbusLink_create_ex(XMemoryType memory);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param link 链路实例（非 NULL）
 */
void XVfdModbusLink_init(XVfdModbusLink* link);

/* ==================== 客户端绑定 ==================== */

/**
 * @brief 绑定底层 Modbus 客户端（借用，不转移所有权；连接管理归调用方）。
 * @param link 链路实例（非 NULL）
 * @param client 已构造的 XModbusRtuSerialClient / XModbusTcpClient
 * @return 成功返回 true
 * @note RTU 场景建议调用方先 setAutoReconnect(true)；串口连接参数由调用方
 *       经 XModbusRtuSerialClient_serialPort 配置。
 */
bool XVfdModbusLink_setClient(XVfdModbusLink* link, XModbusClient* client);

/**
 * @brief 获取底层客户端（借用；未绑定返回 NULL）。
 * @param link 链路实例（可 NULL，返回 NULL）
 * @return 客户端指针
 */
XModbusClient* XVfdModbusLink_client_const(const XVfdModbusLink* link);

/* ==================== 请求提交（全部异步） ==================== */

/**
 * @brief 提交读保持寄存器请求（FC03）。
 * @param link 链路实例（非 NULL）
 * @param owner 发起设备（完成回调第一参；同时用于健康记账）
 * @param unitId 从站地址（1~247）
 * @param address 起始地址（品牌编码规则已换算，库不偏移）
 * @param count 寄存器数（1~125）
 * @param done 完成回调（非 NULL）
 * @param userData 用户数据（透传）
 * @param priority 请求优先级（动作=High / 轮询=Normal；由传输层排序出队）
 * @return 受理返回 true；未绑定/表满/参数非法返回 false
 */
bool XVfdModbusLink_submitRead(XVfdModbusLink* link, XObject* owner, int unitId,
                               uint16_t address, uint16_t count,
                               XVfdModbusLinkDoneFunc done, void* userData,
                               XModbusRequestPriority priority);

/**
 * @brief 提交单寄存器写请求（FC06）。
 * @param link 链路实例（非 NULL）
 * @param owner 发起设备
 * @param unitId 从站地址（1~247；0=广播，无应答不回调）
 * @param address 目标地址
 * @param value 写入值
 * @param done 完成回调（非 NULL）
 * @param userData 用户数据
 * @return 受理返回 true
 */
bool XVfdModbusLink_submitWriteSingle(XVfdModbusLink* link, XObject* owner, int unitId,
                                      uint16_t address, uint16_t value,
                                      XVfdModbusLinkDoneFunc done, void* userData,
                                      XModbusRequestPriority priority);

/**
 * @brief 查询链路是否忙碌（有在途提交；计划表顺延判定依据）。
 * @param link 链路实例（可 NULL，返回 true）
 * @return 忙碌返回 true
 */
bool XVfdModbusLink_isBusy(const XVfdModbusLink* link);

/**
 * @brief 查询在途提交数。
 * @param link 链路实例（可 NULL，返回 0）
 * @return 在途数
 */
int XVfdModbusLink_outstanding(const XVfdModbusLink* link);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVfdModbusLink_create
#define XVfdModbusLink_create() XVfdModbusLink_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XVFD_LINK_ON */
#endif /* XVFD_ON */
#endif /* XVFDMODBUSLINK_H */
