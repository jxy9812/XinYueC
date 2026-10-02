#ifndef XPLCDEVICE_PROTECTED_H
#define XPLCDEVICE_PROTECTED_H

#include "XPlcDevice.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XPLC_CORE_ON

/******************************************************************************************
 * 受保护接口（供子类使用，不对外公开）
 ******************************************************************************************/

 /**
  * @brief 设置设备状态（供子类使用）
  * @param dev XPlcDevice实例指针（非NULL）
  * @param newState 新状态
  * @note 状态改变时会发射 stateChanged 信号；含握手完成 → ConnectedState 的推进
  */
void XPlcDevice_setState(XPlcDevice* dev, XPlcDevice_State newState);

/**
 * @brief 设置设备错误（供子类使用）
 * @param dev XPlcDevice实例指针（非NULL）
 * @param error 错误码
 * @param errorText 错误描述文本（可为NULL，使用默认描述）
 * @note 设置错误时会发射 errorOccurred 信号；S7 细节错误（error class/code、
 *       数据区 0x05/0x06/0x0A）由子类写入 errorText 并置 XPlcDevice_ProtocolError
 */
void XPlcDevice_setError(XPlcDevice* dev, XPlcDevice_Error error, const char* errorText);

/******************************************************************************************
 * 虚函数调用接口
 ******************************************************************************************/

 /**
  * @brief 打开设备（虚函数，查表分派）
  * @param dev XPlcDevice实例指针（非NULL）
  * @return 成功返回true，失败返回false
  * @note 通过虚函数表调用，由子类实现具体逻辑；基类默认槽为空，返回false
  */
bool XPlcDevice_open_base(XPlcDevice* dev);

/**
 * @brief 关闭设备（虚函数，查表分派）
 * @param dev XPlcDevice实例指针（非NULL）
 * @note 通过虚函数表调用，由子类实现具体逻辑
 */
void XPlcDevice_close_base(XPlcDevice* dev);

#endif /* XPLC_CORE_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XPLCDEVICE_PROTECTED_H
