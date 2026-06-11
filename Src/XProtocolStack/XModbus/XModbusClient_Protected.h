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
/**
* @brief 辅助函数：创建并初始化 Reply 对象
* @param client 客户端实例指针（非NULL）
* @param request 原始请求PDU
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL
* @note 供子类在实现 sendRawRequest 时调用，用于创建 Reply 对象
*/
XModbusReply* XModbusClient_createReply(XModbusClient* client, const XModbusRequest* request, int serverAddress);
XModbusReply* XModbusClient_createReply_move(XModbusClient* client,XModbusRequest* request, int serverAddress);
XModbusReply* XModbusClient_createReply_ref(XModbusClient* client, XModbusRequest* request, int serverAddress);
#ifdef __cplusplus
}
#endif

#endif // XIODEVICEPRIVATE_H