#ifndef XMODBUSDEVICE_PROTECTED_H
#define XMODBUSDEVICE_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************************
 * 受保护接口（供子类使用）
 ******************************************************************************************/

 /**
  * @brief 设置设备状态（供子类使用）
  * @param dev XModbusDevice实例指针（非NULL）
  * @param newState 新状态
  * @note 状态改变时会发射stateChanged信号
  */
void XModbusDevice_setState(XModbusDevice* dev, XModbusDevice_State newState);

/**
 * @brief 设置设备错误（供子类使用）
 * @param dev XModbusDevice实例指针（非NULL）
 * @param error 错误码
 * @param errorText 错误描述文本（可为NULL，使用默认描述）
 * @note 设置错误时会发射errorOccurred信号
 */
void XModbusDevice_setError(XModbusDevice* dev, XModbusDevice_Error error, const char* errorText);


#ifdef __cplusplus
}
#endif

#endif // XIODEVICEPRIVATE_H