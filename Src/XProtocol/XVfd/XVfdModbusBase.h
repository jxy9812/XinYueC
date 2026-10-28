/**
 * @file       XVfdModbusBase.h
 * @brief      Modbus 语义中间基类公开 API。
 * @details    XVfdDevice 的 Modbus 实现骨架：在链路（XVfdModbusLink）之上提供
 *             轮询读组引擎（品牌注册"地址段+解析钩子"，PollDue 统一轮转提交）、
 *             通用参数通道（品牌 const 描述表 → FC03/06 读写 + 存取约束）、
 *             设备级单飞与动作收口。品牌子类（XVfdAtv61 / XVfdM512）只提供：
 *             点表/轮询组/参数表注册 + 状态机类动作（如 CiA402 启动序列）。
 *             与 XVfdModbusLink 按配对约定组合（设计文档 §7.5）。
 */
#ifndef XVFDMODBUSBASE_H
#define XVFDMODBUSBASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_DEVICE_ON

#include <stdint.h>
#include <stdbool.h>
#include "XVfdDevice.h"
#include "XVfdModbusLink.h"

/** @brief 轮询读组容量上限（品牌可注册的连续读段数） */
#define XVFD_MODBUS_POLL_GROUP_CAP 8

/** @brief 挂起动作 FIFO 深度（轮询在途时连发动作的排队上限） */
#define XVFD_MODBUS_PENDING_CAP 4

XCLASS_DEFINE_BEGING(XVfdModbusBase)
XCLASS_DEFINE_EXTEND_END(XVfdModbusBase, XVfdDevice)

/**
 * @brief 挂起动作（轮询/参数在途时到达的用户动作：动作优先，收口后自动补发）。
 */
typedef struct XVfdModbusPendingAction {
    XVfdOperation m_op;             /**< 动作类型（None=无挂起） */
    bool m_b1;                      /**< 布尔参数（reverse 等） */
    int m_i1;                       /**< 整型参数 1（index/source/rpm/scope/mode） */
    double m_d1;                    /**< 浮点参数 1（hz/accel/value） */
    double m_d2;                    /**< 浮点参数 2（decel） */
} XVfdModbusPendingAction;

/**
 * @brief Modbus 语义中间基类结构体。
 */
typedef struct XVfdModbusBase {
    XVfdDevice m_base;              /**< 基类子对象（第一成员） */
    XVfdModbusLink* m_mblink;       /**< 配对 Modbus 链路（借用；与 m_link 同对象） */
    XVfdOperation m_op;             /**< 当前在途动作（完成信号携带） */
    const XVfdParamDesc* m_params;  /**< 品牌参数描述表（const，借用） */
    int m_paramCount;               /**< 描述表条目数（0=无参数通道） */
    uint16_t m_pendingParamId;      /**< 在途参数读写下标 */
    bool m_paramReadPending;        /**< 在途为参数读 */
    bool m_paramWritePending;       /**< 在途为参数写 */
    XVfdModbusPendingAction m_pending[XVFD_MODBUS_PENDING_CAP]; /**< 挂起动作 FIFO */
    int m_pendingHead;              /**< FIFO 队头下标 */
    int m_pendingCount;             /**< FIFO 内动作数 */
    int m_pollCursor;               /**< 轮询组轮转游标 */
    uint16_t m_groupAddr[XVFD_MODBUS_POLL_GROUP_CAP];   /**< 轮询组起始地址 */
    uint16_t m_groupCount[XVFD_MODBUS_POLL_GROUP_CAP];  /**< 轮询组寄存器数 */
    uint32_t m_groupMask[XVFD_MODBUS_POLL_GROUP_CAP];   /**< 轮询组归属项掩码 */
    void* m_groupParse[XVFD_MODBUS_POLL_GROUP_CAP];     /**< 轮询组解析钩子（Protected 类型） */
    bool m_groupUsed[XVFD_MODBUS_POLL_GROUP_CAP];       /**< 轮询组在用标记 */
} XVfdModbusBase;

/* ==================== 类生命周期 ==================== */

/**
 * @brief 获取 XVfdModbusBase 类虚函数表。
 * @return 指向初始化完成的 XVtable 的指针
 */
XVtable* XVfdModbusBase_class_init(void);

/**
 * @brief 在堆上创建并初始化实例。
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）
 * @return 成功返回新对象（用 XClassDelete/XObject_deleteLater 释放），失败返回 NULL
 * @note 抽象骨架：直接创建仅可用于参数通道验证；业务用品牌子类。
 */
XVfdModbusBase* XVfdModbusBase_create_ex(XMemoryType memory);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param base 实例（非 NULL）
 */
void XVfdModbusBase_init(XVfdModbusBase* base);

/* ==================== 绑定 ==================== */

/**
 * @brief 绑定 Modbus 链路与从站地址（登记轮询计划、启动链路轮询总闸）。
 * @param base 实例（非 NULL）
 * @param link Modbus 链路（借用，不转移所有权）
 * @param unitId 从站地址（1~247；0=广播不登记轮询）
 * @return 成功返回 true
 * @note 轮询计划条目以本设备为 owner；setPollInterval 经基类直达链路。
 */
bool XVfdModbusBase_setLink(XVfdModbusBase* base, XVfdModbusLink* link, int unitId);

/**
 * @brief 解绑链路（注销轮询计划）。
 * @param base 实例（非 NULL）
 */
void XVfdModbusBase_detach(XVfdModbusBase* base);

/**
 * @brief 获取参数描述表条目数。
 * @param base 实例（可 NULL，返回 0）
 * @return 条目数（0=品牌未提供参数通道）
 */
int XVfdModbusBase_paramCount(const XVfdModbusBase* base);

/**
 * @brief 获取参数描述（常量借用，禁释放）。
 * @param base 实例（可 NULL，返回 NULL）
 * @param paramId 参数下标（0 基）
 * @return 描述指针；越界返回 NULL
 */
const XVfdParamDesc* XVfdModbusBase_paramDesc_const(const XVfdModbusBase* base, uint16_t paramId);

#endif /* XVFD_DEVICE_ON */
#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVfdModbusBase_create
#define XVfdModbusBase_create() XVfdModbusBase_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XVFDMODBUSBASE_H */
