#ifndef XMODBUSREPLY_PROTECTED_H
#define XMODBUSREPLY_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

 // --- Setter（供设备层调用）---
void XModbusReply_setResult(XModbusReply* reply, const XModbusDataUnit* unit);
void XModbusReply_setResult_move(XModbusReply* reply, const XModbusDataUnit* unit);
void XModbusReply_setResult_ref(XModbusReply* reply, const XModbusDataUnit* unit);
void XModbusReply_setRawResult(XModbusReply* reply, const XModbusResponse* response);
void XModbusReply_setRawResult_move(XModbusReply* reply, const XModbusResponse* response);
void XModbusReply_setRawResult_ref(XModbusReply* reply, const XModbusResponse* response);
void XModbusReply_setFinished(XModbusReply* reply, bool finished);
void XModbusReply_setError(XModbusReply* reply, XModbusDevice_Error error, const char* errorText);

#ifdef __cplusplus
}
#endif

#endif // XIODEVICEPRIVATE_H