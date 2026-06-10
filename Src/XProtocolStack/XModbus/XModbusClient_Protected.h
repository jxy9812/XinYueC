#ifndef XMODBUSCLIENT_PROTECTED_H
#define XMODBUSCLIENT_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

/**
 * @brief 处理标准Modbus响应（通过虚函数表调用）
 * @note 子类可重载此虚函数实现特定协议处理
 */
bool XModbusClient_processResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

/**
 * @brief 处理私有/自定义Modbus响应（通过虚函数表调用）
 * @note 子类可重载此虚函数实现自定义功能码处理
 */
bool XModbusClient_processPrivateResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

#ifdef __cplusplus
}
#endif

#endif // XIODEVICEPRIVATE_H