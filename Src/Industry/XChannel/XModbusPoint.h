/**
 * @file       XModbusPoint.h
 * @brief      Modbus 数据源点（继承 XIoAnalogPoint；Hub 缓存读写 + 自订阅
 *             信号推送）。
 * @details    [2026-11-01 通道组重构] 数据来源由点子类实现：本类重写
 *             XIoPoint 的 ReadRaw/WriteRaw 虚槽——
 *               ReadRaw：读 Hub 缓存（AI/AO 经标度换算为工程值口径）；
 *               WriteRaw：DO→写 Coils、AO→写 HR（标度逆换算）；DI/AI 只读。
 *             并自订阅 Hub 的 valueChanged/writeCompleted：缓存回填命中本点
 *             时经 xIoPointFeedValue 推送组（推模式）；写收口命中时经
 *             xIoPointWriteDone 转发 writeDone。
 *             四种类型一类通用（区由 XIoPointType + useHolding 决定）：
 *               DigitalInput→DI 区；DigitalOutput→Coils 区；
 *               AnalogInput→IR/HR 区（useHolding）；AnalogOutput→HR 区。
 *             继承 XIoAnalogPoint：AI/AO 标度四件套与死区内联（DI/DO 不
 *             消费，恒 0）。生命周期：调用方 XClassDelete 创建的点。
 */
#ifndef XMODBUSPOINT_H
#define XMODBUSPOINT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XIoAnalogPoint.h"
#include "XModbusHub.h"
#include <stdint.h>
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON

XCLASS_DEFINE_BEGING(XModbusPoint)
XCLASS_DEFINE_ENUM(XModbusPoint, ReadRaw) = XCLASS_VTABLE_GET_SIZE(XIoPoint),
XCLASS_DEFINE_ENUM(XModbusPoint, WriteRaw),
XCLASS_DEFINE_END(XModbusPoint)

/**
 * @brief XModbusPoint Modbus 数据源点（64B + 连接句柄）。
 */
typedef struct XModbusPoint {
    XIoAnalogPoint m_base;      /**< 基点（继承 XIoAnalogPoint：标度/死区内联）。 */
    uint16_t m_address;         /**< Hub 映射区地址（区由点类型+useHolding 决定）。 */
    XModbusHub* m_hub;          /**< 数据源 Hub（借用，不拥有）。 */
    XConnection* m_valueConn;   /**< Hub valueChanged 订阅连接。 */
    XConnection* m_doneConn;    /**< Hub writeCompleted 订阅连接。 */
} XModbusPoint;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XModbusPoint 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XModbusPoint_class_init(void);

/**
 * @brief 栈上初始化 Modbus 点（基段初始化 + 自订阅 Hub 信号）。
 * @param point 目标点；不能为 NULL，可为未初始化内存。
 * @param hub 数据源 Hub（借用，不拥有）；不能为 NULL。
 * @return 无。Hub 信号订阅失败时点呈「无数据」态（readRaw=false）。
 */
void XModbusPoint_init(XModbusPoint* point, XModbusHub* hub);

/* ==================== 工厂（堆创建；释放 XClassDelete） ==================== */

/**
 * @brief 创建离散量输入点（DI 区，只读）。
 * @param hub 数据源 Hub（借用）；不能为 NULL。
 * @param address 统一虚拟地址（Hub 映射 DI 区）；取值 0..65535。
 * @param tag 点名（借用）；可为 NULL。
 * @return 新点；失败返回 NULL。
 */
XModbusPoint* XModbusPoint_createDigitalInput(XModbusHub* hub, int address, const char* tag);

/**
 * @brief 创建离散量输出点（Coils 区，可写）。
 * @param hub 数据源 Hub（借用）；不能为 NULL。
 * @param address 统一虚拟地址（Hub 映射 Coils 区）；取值 0..65535。
 * @param tag 点名（借用）；可为 NULL。
 * @return 新点；失败返回 NULL。
 */
XModbusPoint* XModbusPoint_createDigitalOutput(XModbusHub* hub, int address, const char* tag);

/**
 * @brief 创建寄存器输入点（IR/HR 区，只读）。
 * @param hub 数据源 Hub（借用）；不能为 NULL。
 * @param address 统一虚拟地址；取值 0..65535。
 * @param useHoldingRegisters true=HR 区；false=IR 区。
 * @param scaleNum/scaleDen/offset 标度：eng=raw*Num/Den+Offset。
 * @param deadband 变化死区（工程值）；负值按 0。
 * @param tag 点名（借用）；可为 NULL。
 * @return 新点；失败返回 NULL。
 */
XModbusPoint* XModbusPoint_createAnalogInput(XModbusHub* hub, int address,
                                             bool useHoldingRegisters,
                                             int32_t scaleNum, int32_t scaleDen,
                                             int32_t offset, int32_t deadband,
                                             const char* tag);

/**
 * @brief 创建保持寄存器输出点（HR 区，可写）。
 * @param hub 数据源 Hub（借用）；不能为 NULL。
 * @param address 统一虚拟地址；取值 0..65535。
 * @param scaleNum/scaleDen/offset 标度：raw=(v-Offset)*Den/Num。
 * @param tag 点名（借用）；可为 NULL。
 * @return 新点；失败返回 NULL。
 */
XModbusPoint* XModbusPoint_createAnalogOutput(XModbusHub* hub, int address,
                                              int32_t scaleNum, int32_t scaleDen,
                                              int32_t offset, const char* tag);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMODBUSPOINT_H */
