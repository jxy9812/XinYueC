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
 * @brief 获取关联的串口对象（供子类访问）
 * @param server XModbusRtuSerialServer实例指针
 * @return 串口对象指针，server为NULL时返回NULL
 * @note 返回的串口对象由XModbusRtuSerialServer管理，不需要手动释放
 */
XSerialPort* XModbusRtuSerialServer_serialPort(const XModbusRtuSerialServer* server);

/**
 * @brief 获取当前帧间延迟值（微秒），如果设置为0则返回自动计算值
 * @param server XModbusRtuSerialServer实例指针
 * @return 帧间延迟（微秒）
 */
int XModbusRtuSerialServer_interFrameDelay(const XModbusRtuSerialServer* server);

/**
 * @brief 设置帧间延迟（微秒）
 * @param server XModbusRtuSerialServer实例指针（非NULL）
 * @param microseconds 帧间延迟（微秒），传入0表示自动计算
 */
void XModbusRtuSerialServer_setInterFrameDelay(XModbusRtuSerialServer* server, int microseconds);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSRTUSERIALSERVER_PROTECTED_H
