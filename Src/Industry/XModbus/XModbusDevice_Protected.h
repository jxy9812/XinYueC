/**
 * @file       XModbusDevice_Protected.h
 * @brief      XModbusDevice 受保护接口（状态迁移、置错、open/close 虚分派）。
 * @details    仅供子类和内部实现使用，不对外公开；公开 API 见
 *             XModbusDevice.h。
 */
#ifndef XMODBUSDEVICE_PROTECTED_H
#define XMODBUSDEVICE_PROTECTED_H

#include "XModbusDevice.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON
/******************************************************************************************
 * 受保护接口（供子类使用）
 ******************************************************************************************/

 /**
  * @brief 设置设备状态并在变化时广播 stateChanged（状态机驱动入口）。
  * @param dev XModbusDevice实例指针；NULL 或状态无变化时不执行任何操作。
  * @param newState 新状态（XModbusDevice_State）。
  * @return 无。
  * @note 仅供子类 open/close 实现等内部路径调用；本函数只改状态并发
  *       信号，不校验迁移顺序（状态机合法性由子类保证）。
  */
void XModbusDevice_setState(XModbusDevice* dev, XModbusDevice_State newState);

/**
 * @brief 设置设备错误码与描述，并发出 errorOccurred 信号（子类置错入口）。
 * @param dev XModbusDevice实例指针；NULL 时不执行任何操作。
 * @param error 错误码（XModbusDevice_Error；任意取值都会照常登记并发
 *        信号——注意与 XModbusReply_setError 的 NoError 复位路径不同）。
 * @param errorText 错误描述文本（XString 借用：函数内深拷贝存储，调用方
 *        返回后即可释放；NULL 时按错误码自动生成默认文案）。
 * @return 无。
 * @note 旧描述立即释放替换；最新一次置错覆盖此前错误信息。
 */
void XModbusDevice_setError(XModbusDevice* dev, XModbusDevice_Error error, const XString* errorText);
/**
 * @brief setError 的 UTF-8 const char* 兼容重载（行为与主版本一致）。
 * @param errorText 错误描述文本（UTF-8，借用：函数内存副本，调用方
 *        返回后即可释放；NULL 时按错误码自动生成默认文案）。
 * @return 无。其余语义同 XModbusDevice_setError 主版本。
 */
void XModbusDevice_setError_2(XModbusDevice* dev, XModbusDevice_Error error, const char* errorText);

/******************************************************************************************
 * 虚函数调用接口
 ******************************************************************************************/

 /**
  * @brief 打开设备——open 虚槽的虚分派入口。
  * @param dev XModbusDevice实例指针；NULL 或虚表无 open 实现返回 false。
  * @return 子类 open 实现返回 true 即 true；参数非法或 open 未重载
  *         （抽象基类）返回 false。
  * @note 与 connectDevice 等价（同一分派路径）；状态迁移与置错由子类
  *       open 实现负责。
  */
bool XModbusDevice_open_base(XModbusDevice* dev);

/**
 * @brief 关闭设备——close 虚槽的虚分派入口。
 * @param dev XModbusDevice实例指针；NULL 或虚表无 close 实现时
 *        不执行任何操作。
 * @return 无。
 * @note 状态回 UnconnectedState 由子类 close 实现负责。
 */
void XModbusDevice_close_base(XModbusDevice* dev);

#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XMODBUSDEVICE_PROTECTED_H
