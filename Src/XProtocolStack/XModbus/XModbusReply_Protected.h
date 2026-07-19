#ifndef XMODBUSREPLY_PROTECTED_H
#define XMODBUSREPLY_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

// --- Setter（供设备层调用）---

/**
 * @brief 设置结构化结果（深拷贝）
 * @param reply XModbusReply实例指针（非NULL）
 * @param unit 要设置的数据单元
 * @note 数据会被内部深拷贝
 */
void XModbusReply_setResult(XModbusReply* reply, const XModbusDataUnit* unit);

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
 * @brief 设置原始PDU结果（深拷贝）
 * @param reply XModbusReply实例指针（非NULL）
 * @param response 要设置的响应PDU
 */
void XModbusReply_setRawResult(XModbusReply* reply, const XModbusResponse* response);

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
 * @brief 设置错误码和错误描述
 * @param reply XModbusReply实例指针（非NULL）
 * @param error 错误码
 * @param errorText 错误描述文本（可为NULL，使用默认描述）
 * @note 设置错误时会触发errorOccurred信号
 */
void XModbusReply_setError(XModbusReply* reply, XModbusDevice_Error error, const char* errorText);

/**
 * @brief 添加中间错误到列表
 * @param reply XModbusReply实例指针（非NULL）
 * @param error 中间错误码
 * @note 用于记录请求处理过程中的中间错误
 */
void XModbusReply_addIntermediateError(XModbusReply* reply, XModbusDevice_IntermediateError error);

/**
 * @brief 清除所有中间错误
 * @param reply XModbusReply实例指针（非NULL）
 */
void XModbusReply_clearIntermediateError(XModbusReply* reply);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSREPLY_PROTECTED_H

