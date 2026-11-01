#ifndef XPLCREPLY_PROTECTED_H
#define XPLCREPLY_PROTECTED_H

#include "XPlcReply.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XPLC_CORE_ON

/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

 // 额外所有权语义和状态控制接口，公开 Qt 对齐接口位于 XPlcReply.h。

 /**
  * @brief 设置结构化结果（移动语义）
  * @param reply XPlcReply实例指针（非NULL）
  * @param value 结果值（移动，函数内接管所有权）
  */
void XPlcReply_setResult_move(XPlcReply* reply, XVariant* value);

/**
 * @brief 设置结构化结果（引用语义）
 * @param reply XPlcReply实例指针（非NULL）
 * @param value 结果值（引用，函数内接管所有权，失败时不释放）
 */
void XPlcReply_setResult_ref(XPlcReply* reply, XVariant* value);

/**
 * @brief 设置原始负载结果（移动语义）
 * @param reply XPlcReply实例指针（非NULL）
 * @param data 原始数据（移动，函数内接管所有权）
 */
void XPlcReply_setRawResult_move(XPlcReply* reply, XByteArray* data);

/**
 * @brief 设置原始负载结果（引用语义）
 * @param reply XPlcReply实例指针（非NULL）
 * @param data 原始数据（引用，函数内接管所有权，失败时不释放）
 */
void XPlcReply_setRawResult_ref(XPlcReply* reply, XByteArray* data);

/**
 * @brief 设置回复状态
 * @param reply XPlcReply实例指针（非NULL）
 * @param state 新的状态值
 * @note 设置状态时会触发 stateChanged 信号；置 Finished/Timeout 时
 *       自动置完成标志并发 finished 信号（仅首次）
 */
void XPlcReply_setState(XPlcReply* reply, XPlcReply_State state);

/**
 * @brief 设置关联的 PDU 引用号
 * @param reply XPlcReply实例指针（非NULL）
 * @param pduRef S7 PDU 引用号（1..65535）
 * @note 由子类在登记 pending 时写入，用于超时/乱序定位
 */
void XPlcReply_setPduRef(XPlcReply* reply, uint16_t pduRef);

/**
 * @brief 清除所有中间错误
 * @param reply XPlcReply实例指针（非NULL）
 */
void XPlcReply_clearIntermediateError(XPlcReply* reply);

#endif /* XPLC_CORE_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XPLCREPLY_PROTECTED_H
