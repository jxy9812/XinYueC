#ifndef XPLCREPLY_H
#define XPLCREPLY_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XString.h"
#include "XVariant.h"
#include "XByteArray.h"
#include "XVector.h"
#include "XPlcDevice.h"   // For XPlcDevice_Error and XPlcDevice_IntermediateError

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XPLC_CORE_ON

/**
 * @file XPlcReply.h
 * @brief PLC 异步回复核心头文件（纯C风格，对齐 Qt6 QModbusReply 接口）
 * @details 表示一个异步 PLC 请求的结果：状态机、错误信息、结果负载与请求回带。
 *          与 XModbusReply 差异：以 m_pduRef 关联 S7 PDU 引用号（超时/乱序定位），
 *          结果为 XVariant 单值 + XByteArray 原始数据块（协议无关）。
 *
 * @par 状态推进
 * send* → Requesting（组帧）→ Waiting（已发送，定时器跑）→ Responding
 * （应答到达/分片中间态）→ Finished 或 Timeout（重试用尽）
 *
 * @par 生命周期惯用法
 * @code
 * XPlcReply* reply = XS7TcpClient_sendRead(client, addr, XS7Value_Word, 1);
 * XObject_setParent(reply, client);
 * XObject_connect_1(reply, XSignal(XPlcReply_finished_signal), reply,
 *     XObject_deleteLater, XConnectionType_Auto);
 * @endcode
 *
 * @note 一个 Reply 可对应多个 PDU（分片读写/块传输序列）：全部完成才置 Finished
 */

 /**
  * @brief PLC 回复状态枚举
  * @details 定义 PLC 请求的完整生命周期状态（与 XModbusReply_State 对位）
  */
typedef enum {
    XPlcReply_State_No_Started,   ///< 未开始
    XPlcReply_State_Requesting,   ///< 请求中（组帧）
    XPlcReply_State_Waiting,      ///< 等待中（已发送，超时定时器运行）
    XPlcReply_State_Responding,   ///< 响应中（应答到达/分片中间态）
    XPlcReply_State_Finished,     ///< 已结束
    XPlcReply_State_Timeout       ///< 已超时，重试用尽导致请求失败
} XPlcReply_State;

/**
 * @brief 回复类型枚举
 */
typedef enum {
    XPlcReply_Raw = 0,       ///< 原始 PDU 回复（rawResult 为完整协议应答 PDU）
    XPlcReply_Common         ///< 结构化数据回复（result 为 XVariant 单值 + rawResult 原始数据块）
} XPlcReply_ReplyType;

XCLASS_DEFINE_BEGING(XPlcReply)
XCLASS_DEFINE_EXTEND_END(XPlcReply, XObject)

 /**
  * @struct XPlcReply
  * @brief PLC 异步回复核心结构体（继承自XObject）
  * @details 封装请求的最终结果：类型、状态、错误、PDU 引用号、
  *          结构化结果、原始负载、请求回带与中间错误列表。
  *
  * @par 成员说明
  * | 成员 | 类型 | 说明 |
  * |------|------|------|
  * | m_class | XObject | 继承自XObject基类，第一位 |
  * | m_state | uint8_t | 回复状态（XPlcReply_State） |
  * | m_type | uint8_t | 回复类型（XPlcReply_ReplyType） |
  * | m_error | uint8_t | 最终错误码（XPlcDevice_Error） |
  * | m_finished | bool | 是否已完成（包含超时/中止） |
  * | m_errorString | XString* | 错误描述字符串 |
  * | m_pduRef | uint16_t | 关联的 S7 PDU 引用号（超时/乱序定位） |
  * | m_result | XVariant* | Common：单值读写结果，对象拥有 |
  * | m_rawResult | XByteArray* | Common：原始数据块；Raw：完整应答 PDU |
  * | m_request | XByteArray* | 请求回带（地址+类型+值），重试/分片复用 |
  * | m_intermediateErrors | XVector* | 中间错误列表（分片部分失败等） |
  */
typedef struct XPlcReply
{
    XObject m_class;                    ///< 继承自XObject基类
    uint8_t    m_state;                 ///< 回复状态（XPlcReply_State）
    uint8_t/*XPlcReply_ReplyType*/ m_type;     ///< 回复类型
    uint8_t /*XPlcDevice_Error*/ m_error;      ///< 最终错误码
    bool m_finished;                    ///< 是否已完成（包含超时/中止）
    XString* m_errorString;             ///< 错误描述字符串

    uint16_t  m_pduRef;                 ///< 关联的 S7 PDU 引用号（超时/乱序定位）
    XVariant* m_result;                 ///< 结构化结果（Common 类型时有效，对象拥有）
    XByteArray* m_rawResult;            ///< 原始负载结果（Common：原始数据块；Raw：完整应答PDU）
    XByteArray* m_request;              ///< 请求回带（重试/分片复用）
    XVector* m_intermediateErrors;      ///< 中间错误列表 (XVector<XPlcDevice_IntermediateError>)
} XPlcReply;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

 /**
  * @brief 初始化XPlcReply的虚函数表
  * @return 初始化完成的虚函数表指针
  * @note 该函数是线程安全的，多次调用返回同一虚表实例
  */
XVtable* XPlcReply_class_init(void);

/**
 * @brief 创建XPlcReply实例
 * @param memory 内存分配类型
 * @param type 回复类型
 * @return 新创建的XPlcReply实例指针，失败返回NULL
 */
XPlcReply* XPlcReply_create_ex(XMemoryType memory, XPlcReply_ReplyType type);

/**
 * @brief 初始化XPlcReply实例
 * @param reply 待初始化的实例指针（非NULL）
 * @param type 回复类型
 * @note 默认状态 No_Started、未完成、无错误、pduRef=0
 */
void XPlcReply_init(XPlcReply* reply, XPlcReply_ReplyType type);

/******************************************************************************************
 * Public API (对齐 QModbusReply)
 ******************************************************************************************/

 // --- 属性查询 ---

 /**
  * @brief 获取回复类型
  * @param reply XPlcReply实例指针
  * @return 回复类型枚举值，reply为NULL时返回XPlcReply_Raw
  */
XPlcReply_ReplyType XPlcReply_type(const XPlcReply* reply);

/**
 * @brief 获取当前回复状态
 * @param reply XPlcReply实例指针
 * @return 当前状态枚举值，reply为NULL时返回XPlcReply_State_No_Started
 */
XPlcReply_State XPlcReply_state(const XPlcReply* reply);

/**
 * @brief 获取关联的 PDU 引用号
 * @param reply XPlcReply实例指针
 * @return pduRef（1..65535），reply为NULL时返回0
 */
uint16_t XPlcReply_pduRef(const XPlcReply* reply);

/**
 * @brief 判断回复是否已完成
 * @param reply XPlcReply实例指针
 * @return 已完成返回true，否则返回false
 */
bool XPlcReply_isFinished(const XPlcReply* reply);

// --- 结果获取 ---

 /**
  * @brief 获取结构化结果（深拷贝）
  * @param reply XPlcReply实例指针
  * @return 结构化结果的深拷贝，调用者负责释放；无结果返回NULL
  */
XVariant* XPlcReply_result(const XPlcReply* reply);

/**
 * @brief 获取结构化结果常量引用
 * @param reply XPlcReply实例指针
 * @return 结构化结果的常量指针，调用者不应释放；无结果返回NULL
 */
const XVariant* XPlcReply_result_const(const XPlcReply* reply);

/**
 * @brief 获取原始负载结果（深拷贝）
 * @param reply XPlcReply实例指针
 * @return 原始负载的深拷贝，调用者负责释放；无结果返回NULL
 */
XByteArray* XPlcReply_rawResult(const XPlcReply* reply);

/**
 * @brief 获取原始负载结果常量引用
 * @param reply XPlcReply实例指针
 * @return 原始负载的常量指针，调用者不应释放；无结果返回NULL
 */
const XByteArray* XPlcReply_rawResult_const(const XPlcReply* reply);

/**
 * @brief 获取请求回带（深拷贝）
 * @param reply XPlcReply实例指针
 * @return 请求回带的深拷贝，调用者负责释放；无请求返回NULL
 */
XByteArray* XPlcReply_request(const XPlcReply* reply);

/**
 * @brief 获取请求回带常量引用
 * @param reply XPlcReply实例指针
 * @return 请求回带的常量指针，调用者不应释放；无请求返回NULL
 */
const XByteArray* XPlcReply_request_const(const XPlcReply* reply);

// --- 错误信息 ---

 /**
  * @brief 获取错误描述字符串（深拷贝）
  * @param reply XPlcReply实例指针
  * @return 错误描述字符串的深拷贝，调用者负责释放；reply为NULL返回NULL
  */
XString* XPlcReply_errorString(const XPlcReply* reply);

/**
 * @brief 获取错误码
 * @param reply XPlcReply实例指针
 * @return 错误码枚举值，reply为NULL时返回XPlcDevice_UnknownError
 */
XPlcDevice_Error XPlcReply_error(const XPlcReply* reply);

// --- 公开结果/状态设置接口（对齐 Qt QModbusReply） ---

 /**
  * @brief 设置结构化结果（深拷贝）
  * @param reply XPlcReply实例指针（非NULL）
  * @param value 结果值（内部复制）
  * @note 对齐 Qt 6.8.3 QModbusReply::setResult
  */
void XPlcReply_setResult(XPlcReply* reply, const XVariant* value);

/**
 * @brief 设置原始负载结果（深拷贝）
 * @param reply XPlcReply实例指针（非NULL）
 * @param data 原始数据（内部复制）
 * @note 对齐 Qt 6.8.3 QModbusReply::setRawResult
 */
void XPlcReply_setRawResult(XPlcReply* reply, const XByteArray* data);

/**
 * @brief 设置完成状态；传入 true 时发出 finished 信号
 * @param reply XPlcReply实例指针（非NULL）
 * @param isFinished 完成标志
 * @note 只更新完成标志，不改变 XPlcReply_state；对齐 Qt 6.8.3
 */
void XPlcReply_setFinished(XPlcReply* reply, bool isFinished);

/**
 * @brief 设置错误及描述，并依次发出 errorOccurred、finished 信号
 * @param reply XPlcReply实例指针（非NULL）
 * @param error 错误码
 * @param errorText 错误描述（可为NULL）
 * @note 传 XPlcDevice_NoError 时仅清空描述文本，不完成回复（内部重试复位用）；
 *       对齐 Qt 6.8.3 QModbusReply::setError
 */
void XPlcReply_setError(XPlcReply* reply, XPlcDevice_Error error, const char* errorText);

// --- 中间错误 ---

 /**
  * @brief 获取中间错误列表（深拷贝）
  * @param reply XPlcReply实例指针
  * @return 中间错误列表的深拷贝，调用者负责释放；无中间错误返回NULL
  */
XVector* XPlcReply_intermediateErrors(const XPlcReply* reply);

/**
 * @brief 添加中间错误并发出 intermediateErrorOccurred 信号
 * @param reply XPlcReply实例指针（非NULL）
 * @param error 中间错误码（如分片部分失败）
 * @note 对齐 Qt 6.8.3 QModbusReply::addIntermediateError
 */
void XPlcReply_addIntermediateError(XPlcReply* reply, XPlcDevice_IntermediateError error);

/******************************************************************************************
 * 信号接口 (对齐 Qt 信号)
 ******************************************************************************************/

 /**
  * @brief 触发 finished 信号
  * @param reply XPlcReply实例指针（非NULL）
  * @return 信号句柄
  */
void* XPlcReply_finished_signal(XPlcReply* reply);

/**
 * @brief 触发 stateChanged 信号
 * @param reply XPlcReply实例指针（非NULL）
 * @param state 新状态
 * @return 信号句柄
 */
void* XPlcReply_stateChanged_signal(XPlcReply* reply, XPlcReply_State state);

/**
 * @brief 触发 errorOccurred 信号
 * @param reply XPlcReply实例指针（非NULL）
 * @param error 错误码
 * @return 信号句柄
 */
void* XPlcReply_errorOccurred_signal(XPlcReply* reply, XPlcDevice_Error error);

/**
 * @brief 触发 intermediateErrorOccurred 信号
 * @param reply XPlcReply实例指针（非NULL）
 * @param error 中间错误码
 * @return 信号句柄
 */
void* XPlcReply_intermediateErrorOccurred_signal(XPlcReply* reply, XPlcDevice_IntermediateError error);

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

#define XPlcReply_deleteLater         XObject_deleteLater
#define XPlcReply_deinitLater         XObject_deinitLater

#endif /* XPLC_CORE_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XPlcReply_create
#define XPlcReply_create(...) XPlcReply_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XPLCREPLY_H
