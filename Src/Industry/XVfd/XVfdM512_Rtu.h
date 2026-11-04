/**
 * @file       XVfdM512_Rtu.h
 * @brief      511M/512M/513M 系列专用的 **Modbus-RTU 客户端**
 *             （继承 XModbusRtuSerialClient，只补本厂的非标准错误帧）。
 * @details    [2026-11-03 真机实测] 本系列从站的**错误应答帧是非标准的**：
 *
 *             本厂错误帧（8 字节）：
 *               [站号][原功能码][80][01][00][错误码][CRC16][CRC16]
 *               —— 功能码**不置 0x80**（标准 Modbus 异常帧是 5 字节
 *                  [站号][fc|0x80][错误码][CRC16]）。
 *               第 6 字节才是错误码：
 *                 0x02 非法功能码 / 0x04 非法数据值（写只读区）
 *                 0x05 非法数据地址（越界 / 跨组 / 单点地址连读）
 *
 *             为什么框架的通用客户端处理不了：它按标准规则算帧长——
 *             功能码没置 0x80 ⇒ 走读应答分支 ⇒ 字节数字段取 0x80=128 ⇒
 *             期望 133 字节 ⇒ 8 字节的帧**永远留缓冲** ⇒ 请求超时；
 *             而缓冲只在**重试**时才清 ⇒ 重试次数为 0 时整条 RTU 连接死掉。
 *
 *             为什么不用改框架：厂商特例不该进通用类。本类**继承**
 *             XModbusRtuSerialClient 并只重写一件事：**帧间超时到期时，
 *             先把缓冲里的本厂错误帧就地归一化成标准 5 字节异常帧**，
 *             再交回父类走标准分支 —— 于是错误语义、reply 收口、
 *             发送队列续泵全部复用框架自己的实现，框架侧零改动。
 *
 *             用法：与 XModbusRtuSerialClient 完全一致（连接参数表 +
 *             connectDevice），只是类型换成本类。
 */
#ifndef XVFDM512_RTU_H
#define XVFDM512_RTU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XVfd_config.h"

#if XVFD_ON && XVFD_M512_ON

#include <stdint.h>
#include <stdbool.h>

#include "XClass.h"
#include "XMemory.h"
#include "XModbusRtuSerialClient.h"

/**
 * @brief 511M/512M/513M 专用 RTU 客户端（继承 XModbusRtuSerialClient）。
 */
XCLASS_DEFINE_BEGING(XVfdM512RtuClient)
XCLASS_DEFINE_EXTEND_END(XVfdM512RtuClient, XModbusRtuSerialClient)

typedef struct XVfdM512RtuClient {
    XModbusRtuSerialClient m_base;   /**< 基类子对象（第一成员）。 */
} XVfdM512RtuClient;

/**
 * @brief 取 XVfdM512RtuClient 类虚函数表。
 * @return 初始化完成的虚表指针。
 */
XVtable* XVfdM512RtuClient_class_init(void);

/**
 * @brief 在堆上创建 XVfdM512RtuClient 实例。
 * @param memory 内存类型。
 * @return 成功返回实例（用 XClassDelete 释放），失败返回 NULL。
 */
XVfdM512RtuClient* XVfdM512RtuClient_create_ex(XMemoryType memory);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param dev 实例（非 NULL）。
 */
void XVfdM512RtuClient_init(XVfdM512RtuClient* dev);

/**
 * @brief 本厂非标准错误码 → 标准 Modbus 异常码的映射。
 * @param vendorCode 本厂错误码（帧第 6 字节）。
 * @return 标准异常码（XModbusPdu_ExceptionCode）。
 * @details 本厂编号与标准编号不同：
 *   本厂 0x02 非法功能码 → 标准 0x01；本厂 0x04 非法数据值 → 标准 0x03；
 *   本厂 0x05 非法数据地址 → 标准 0x02；其余 → 标准 0x04 从站设备故障。
 */
uint8_t XVfdM512Rtu_mapErrorCode(uint8_t vendorCode);

#endif /* XVFD_ON && XVFD_M512_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVfdM512RtuClient_create
#define XVfdM512RtuClient_create() XVfdM512RtuClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XVFDM512_RTU_H */
