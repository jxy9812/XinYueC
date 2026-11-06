/**
 * @file       XModbusRtuSerialServer_Protected.h
 * @brief      XModbusRtuSerialServer 受保护接口（串口访问与帧间时序）。
 * @details    仅供子类和内部实现使用，不对外公开；公开 API 见
 *             XModbusRtuSerialServer.h。
 */
#ifndef XMODBUSRTUSERIALSERVER_PROTECTED_H
#define XMODBUSRTUSERIALSERVER_PROTECTED_H

#include "XModbusRtuSerialServer.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

/**
 * @brief 获取关联的串口对象（供子类访问）。
 * @param server XModbusRtuSerialServer实例指针；NULL 返回 NULL。
 * @return 串口对象的借用指针（由 server 创建与释放，禁止调用方释放
 *         或越权改写）；server 为 NULL 或串口尚未创建返回 NULL。
 * @note 借用指针仅在 server 存续期间有效。
 */
XSerialPort* XModbusRtuSerialServer_serialPort(const XModbusRtuSerialServer* server);

/**
 * @brief 获取生效的帧间延迟（微秒；显式值 0 表示按波特率自动计算）。
 * @param server XModbusRtuSerialServer实例指针；NULL 返回 -1。
 * @return 生效帧间延迟（微秒）：显式设置值非 0 时原样返回；为 0 时按
 *         当前波特率自动计算（3.5 字符 × 每字符 11 位，下限 1750us）；
 *         串口不存在时返回下限 1750。
 * @note RTU 帧间静默判定使用本值（单位换算为毫秒空闲窗）。
 */
int XModbusRtuSerialServer_interFrameDelay(const XModbusRtuSerialServer* server);

/**
 * @brief 设置帧间延迟（微秒；0 表示恢复按波特率自动计算）。
 * @param server XModbusRtuSerialServer实例指针；NULL 时不执行任何操作。
 * @param microseconds 帧间延迟（微秒）；0 表示自动计算（3.5 字符传输
 *        时间，下限 1750us）；非 0 值原样登记、不做范围校验。
 * @return 无。
 * @note 仅改登记值，不立即作用于进行中的收包空闲窗（下次取值时生效）。
 */
void XModbusRtuSerialServer_setInterFrameDelay(XModbusRtuSerialServer* server, int microseconds);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSRTUSERIALSERVER_PROTECTED_H
