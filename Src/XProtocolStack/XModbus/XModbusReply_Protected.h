#ifndef XMODBUSREPLY_PROTECTED_H
#define XMODBUSREPLY_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

// 额外所有权语义和状态控制接口，公开 Qt 对齐接口位于 XModbusReply.h。

/**
 * @brief 设置结构化结果（移动语义）
 * @param reply XModbusReply实例指针（非NULL）
 * @param unit 要设置的数据单元（移动，函数内释放原指针）
 */
void XModbusReply_setResult_move(XModbusReply* reply, const XModbusDataUnit* unit);

/**
 * @brief 设置结构化结果（引用语义）
 * @param reply XModbusReply实例指针（非NULL）
 * @param unit 要设置的数据单元（引用，函数内不释放）
 */
void XModbusReply_setResult_ref(XModbusReply* reply, const XModbusDataUnit* unit);

/**
 * @brief 设置原始PDU结果（移动语义）
 * @param reply XModbusReply实例指针（非NULL）
 * @param response 要设置的响应PDU（移动，函数内释放原指针）
 */
void XModbusReply_setRawResult_move(XModbusReply* reply, const XModbusResponse* response);

/**
 * @brief 设置原始PDU结果（引用语义）
 * @param reply XModbusReply实例指针（非NULL）
 * @param response 要设置的响应PDU（引用，函数内不释放）
 */
void XModbusReply_setRawResult_ref(XModbusReply* reply, const XModbusResponse* response);

/**
 * @brief 设置回复状态
 * @param reply XModbusReply实例指针（非NULL）
 * @param state 新的状态值
 * @note 设置状态时会触发stateChanged信号
 */
void XModbusReply_setState(XModbusReply* reply, XModbusReply_State state);

/**
 * @brief 清除所有中间错误
 * @param reply XModbusReply实例指针（非NULL）
 */
void XModbusReply_clearIntermediateError(XModbusReply* reply);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSREPLY_PROTECTED_H

