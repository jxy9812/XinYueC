#ifndef XMODBUSSERVER_PROTECTED_H
#define XMODBUSSERVER_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/
 /**
 * @brief 处理请求（虚函数）
 * @param server XModbusServer实例指针（非NULL）
 * @param request 请求PDU
 * @return 返回响应PDU指针，需要调用者释放
 */
XModbusResponse* XModbusServer_processRequest_base(XModbusServer* server, const XModbusRequest* request);

/**
* @brief 处理私有请求（虚函数）
* @param server XModbusServer实例指针（非NULL）
* @param request 请求PDU
* @return 返回响应PDU指针，需要调用者释放
*/
XModbusResponse* XModbusServer_processPrivateRequest_base(XModbusServer* server, const XModbusRequest* request);

/**
* @brief 写入数据（虚函数基类实现）
* @param server XModbusServer实例指针（非NULL）
* @param unit 要写入的数据单元
* @return 成功返回true，失败返回false
*/
bool XModbusServer_writeData_base(XModbusServer* server, const XModbusDataUnit* unit);

/**
* @brief 读取数据（虚函数基类实现）
* @param server XModbusServer实例指针（非NULL）
* @param unit 用于存储读取数据的XModbusDataUnit指针
* @return 成功返回true，失败返回false
*/
bool XModbusServer_readData_base(XModbusServer* server, XModbusDataUnit* unit);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSSERVER_PROTECTED_H