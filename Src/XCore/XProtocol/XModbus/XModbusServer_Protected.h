/**
 * @file       XModbusServer_Protected.h
 * @brief      XModbusServer 受保护接口（请求处理与数据读写虚分派）。
 * @details    仅供子类和内部实现使用，不对外公开；公开 API 见
 *             XModbusServer.h。
 */
#ifndef XMODBUSSERVER_PROTECTED_H
#define XMODBUSSERVER_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/
 /**
 * @brief 处理请求——ProcessRequest 虚槽的分派入口（标准功能码应答）。
 * @param server XModbusServer实例指针；NULL 或 request 为 NULL
 *        返回 NULL。
 * @param request 请求PDU（借用，不取得所有权）。
 * @return 新响应PDU（正常应答或 Modbus 异常应答，调用方 XClassDelete
 *         释放）；参数非法返回 NULL。
 * @note 默认实现覆盖 FC01-08/11/12/15/16/17/22/23/24/43：数据读写经
 *       readData/writeData 虚槽完成（网络读成功时发 dataRead 信号）；
 *       其余功能码转发给 processPrivateRequest_base。子类可重载。
 */
XModbusResponse* XModbusServer_processRequest_base(XModbusServer* server, const XModbusRequest* request);

/**
* @brief 处理私有/自定义功能码请求——ProcessPrivateRequest 虚槽的分派入口。
* @param server XModbusServer实例指针；NULL 或 request 为 NULL 返回 NULL。
* @param request 请求PDU（借用，不取得所有权）。
* @return 新响应PDU（调用方 XClassDelete 释放）；基类默认实现返回
*         IllegalFunction 异常应答（参数合法时不会为 NULL）。
* @note 标准功能码之外的功能码由 processRequest_base 转发至此；
*       子类重载实现私有协议。
*/
XModbusResponse* XModbusServer_processPrivateRequest_base(XModbusServer* server, const XModbusRequest* request);

/**
* @brief 写入数据——WriteData 虚槽的分派入口（服务端数据落表）。
* @param server XModbusServer实例指针；NULL 或 unit 为 NULL 返回 false。
* @param unit 待写数据单元（借用：按其类型/起始地址/值写入数据映射表，
*        不取得所有权）。
* @return 写入成功 true；server/unit 为 NULL、映射表缺失、类型未登记
*         或地址越界返回 false（失败时映射表保持不变）。
* @note 默认实现写入成功后发 dataWritten 信号；本入口为数据表读写，
*       不含通信。
*/
bool XModbusServer_writeData_base(XModbusServer* server, const XModbusDataUnit* unit);

/**
* @brief 读取数据——ReadData 虚槽的分派入口（服务端数据取表）。
* @param server XModbusServer实例指针；NULL 或 unit 为 NULL 返回 false。
* @param unit 目标数据单元（输入输出：按其类型/起始地址/数量从映射表
*        读出并回填值，不取得所有权）。
* @return 读取成功 true；server/unit 为 NULL、映射表缺失、类型未登记
*         或地址越界返回 false（各分支先校验后回填，失败时 unit 保持
*         原内容）。
* @note 本入口不发 dataRead 信号——信号由网络请求处理路径
*       （processRequest 默认实现）在读成功后发出；本地直接调用
*       （如 data1/data2）不触发信号。
*/
bool XModbusServer_readData_base(XModbusServer* server, XModbusDataUnit* unit);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSSERVER_PROTECTED_H