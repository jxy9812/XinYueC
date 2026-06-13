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

/******************************************************************************************
 * 虚函数调用接口
 ******************************************************************************************/

 /**
  * @brief 打开设备（虚函数）
  * @param dev XModbusDevice实例指针（非NULL）
  * @return 成功返回true，失败返回false
  * @note 通过虚函数表调用，由子类实现具体逻辑
  */
bool XModbusDevice_open_base(XModbusDevice* dev);

/**
 * @brief 关闭设备（虚函数）
 * @param dev XModbusDevice实例指针（非NULL）
 * @note 通过虚函数表调用，由子类实现具体逻辑
 */
void XModbusDevice_close_base(XModbusDevice* dev);
#ifdef __cplusplus
}
#endif

#endif // XIODEVICEPRIVATE_H