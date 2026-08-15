#ifndef XMODBUSREPLY_H
#define XMODBUSREPLY_H

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XModbusDataUnit.h" // For XModbusDataUnit
#include "XModbusPdu.h"      // For XModbusResponse (which is a typedef of XModbusPdu)
#include "XModbusDevice.h"   // For XModbusDevice_Error and XModbusDevice_IntermediateError
#include "XVector.h"           // For intermediate errors list

#ifdef __cplusplus
extern "C" {
#endif

/**
* @file XModbusReply.h
* @brief Modbus回复核心头文件（纯C风格，严格对齐Qt6 QModbusReply接口）
* @details 该文件实现了Qt6 QModbusReply类的纯C等价封装，继承XObject基类。
* 它用于表示一个异步Modbus请求的结果，包含状态、错误信息和响应数据。
*/

/**
 * @brief Modbus回复状态枚举
 * @details 定义Modbus请求的完整生命周期状态
 */
typedef enum {
    XModbusReply_State_No_Started,   ///< 未开始
    XModbusReply_State_Requesting,   ///< 请求中
    XModbusReply_State_Waiting,      ///< 等待中
    XModbusReply_State_Responding,   ///< 响应中
    XModbusReply_State_Finished,     ///< 已结束
    XModbusReply_State_Timeout       ///< 已超时，正常流程之外，遇到错误导致超时请求失败
} XModbusReply_State;

/**
 * @brief 回复类型枚举（对齐 QModbusReply::ReplyType）
 */
typedef enum {
    XModbusReply_Raw = 0,       ///< 原始 PDU 回复 (XModbusResponse)
    XModbusReply_Common,        ///< 结构化数据回复 (XModbusDataUnit)
    XModbusReply_Broadcast      ///< 广播请求（无回复）
} XModbusReply_ReplyType;

/**
* @struct XModbusReply
* @brief Modbus回复核心结构体（继承自XObject）
* @details 封装了Modbus请求的最终结果，包括类型、从站地址、完成状态、
* 错误信息、结构化结果、原始PDU结果以及中间错误列表。
*/
XCLASS_DEFINE_BEGING(XModbusReply)
XCLASS_DEFINE_EXTEND_END(XModbusReply, XObject)

typedef struct XModbusReply 
{
    XObject m_class;                    ///< 继承自XObject基类
    uint8_t    m_state;                 ///< 回复状态（XModbusReply_State）
    uint8_t/*XModbusReply_ReplyType*/ m_type;     ///< 回复类型
    uint8_t m_serverAddress;               ///< 从站地址
    uint8_t /*XModbusDevice_Error*/ m_error;       ///< 最终错误码
    bool m_finished;                    ///< 是否已完成（包含超时/中止）
    XString* m_errorString;            ///< 错误描述字符串

    XModbusDataUnit* m_result;         ///< 结构化结果（Common 类型时有效）
    XModbusResponse* m_rawResult;      ///< 原始 PDU 结果（Raw 类型时有效）
    XModbusRequest* m_request;          ///< 请求信息
    XVector* m_intermediateErrors;       ///< 中间错误列表 (XList<XModbusDevice_IntermediateError>)
} XModbusReply;

/******************************************************************************************
* 类初始化/实例创建接口
******************************************************************************************/

/**
 * @brief 初始化XModbusReply的虚函数表
 * @return 初始化完成的虚函数表指针
 */
XVtable* XModbusReply_class_init(void);

/**
* @brief 创建XModbusReply实例
* @param type 回复类型
* @param serverAddress 从站地址
* @return 新创建的XModbusReply实例指针，失败返回NULL
*/
XModbusReply* XModbusReply_create_ex(XMemoryType memory,  XModbusReply_ReplyType type, int serverAddress);

/**
* @brief 初始化XModbusReply实例
* @param reply 待初始化的实例指针（非NULL）
* @param type 回复类型
* @param serverAddress 从站地址
*/
void XModbusReply_init(XModbusReply* reply, XModbusReply_ReplyType type, int serverAddress);

/******************************************************************************************
 * Public API (严格对齐 QModbusReply)
 ******************************************************************************************/

 // --- 属性查询 ---

/**
 * @brief 获取回复类型
 * @param reply XModbusReply实例指针
 * @return 回复类型枚举值
 */
XModbusReply_ReplyType XModbusReply_type(const XModbusReply* reply);

/**
 * @brief 获取当前回复状态
 * @param reply XModbusReply实例指针
 * @return 当前状态枚举值
 */
XModbusReply_State XModbusReply_state(const XModbusReply* reply);

/**
 * @brief 获取从站地址
 * @param reply XModbusReply实例指针
 * @return 从站地址（0-247）
 */
int XModbusReply_serverAddress(const XModbusReply* reply);

/**
 * @brief 判断回复是否已完成
 * @param reply XModbusReply实例指针
 * @return 已完成返回true，否则返回false
 */
bool XModbusReply_isFinished(const XModbusReply* reply);

// --- 结果获取 ---

/**
 * @brief 获取结构化结果（深拷贝）
 * @param reply XModbusReply实例指针
 * @return 结构化结果的深拷贝，调用者负责释放；无结果返回NULL
 */
XModbusDataUnit* XModbusReply_result(const XModbusReply* reply);

/**
 * @brief 获取结构化结果常量引用
 * @param reply XModbusReply实例指针
 * @return 结构化结果的常量指针，调用者不应释放
 */
const XModbusDataUnit* XModbusReply_result_const(const XModbusReply* reply);

/**
 * @brief 获取原始PDU结果（深拷贝）
 * @param reply XModbusReply实例指针
 * @return 原始PDU结果的深拷贝，调用者负责释放；无结果返回NULL
 */
XModbusResponse* XModbusReply_rawResult(const XModbusReply* reply);

/**
 * @brief 获取原始PDU结果常量引用
 * @param reply XModbusReply实例指针
 * @return 原始PDU结果的常量指针，调用者不应释放
 */
const XModbusResponse* XModbusReply_rawResult_const(const XModbusReply* reply);

/**
 * @brief 获取原始请求（深拷贝）
 * @param reply XModbusReply实例指针
 * @return 原始请求的深拷贝，调用者负责释放；无请求返回NULL
 */
XModbusRequest* XModbusReply_request(const XModbusReply* reply);

/**
 * @brief 获取原始请求常量引用
 * @param reply XModbusReply实例指针
 * @return 原始请求的常量指针，调用者不应释放
 */
const XModbusRequest* XModbusReply_request_const(const XModbusReply* reply);

// --- 错误信息 ---

/**
 * @brief 获取错误描述字符串（深拷贝）
 * @param reply XModbusReply实例指针
 * @return 错误描述字符串的深拷贝，调用者负责释放；无错误返回NULL
 */
XString* XModbusReply_errorString(const XModbusReply* reply);

/**
 * @brief 获取错误码
 * @param reply XModbusReply实例指针
 * @return 错误码枚举值
 */
XModbusDevice_Error XModbusReply_error(const XModbusReply* reply);

// --- 公开结果/状态设置接口（对齐 Qt QModbusReply） ---

/**
 * @brief 设置结构化结果（深拷贝）
 * @note 对齐 Qt 6.8.3 QModbusReply::setResult。
 */
void XModbusReply_setResult(XModbusReply* reply, const XModbusDataUnit* unit);

/**
 * @brief 设置原始响应 PDU（深拷贝）
 * @note 对齐 Qt 6.8.3 QModbusReply::setRawResult。
 */
void XModbusReply_setRawResult(XModbusReply* reply, const XModbusResponse* response);

/**
 * @brief 设置完成状态；传入 true 时发出 finished 信号
 * @note 只更新完成标志，不改变 XModbusReply_state；对齐 Qt 6.8.3。
 */
void XModbusReply_setFinished(XModbusReply* reply, bool isFinished);

/**
 * @brief 设置错误及描述，并依次发出 errorOccurred、finished 信号
 * @note 对齐 Qt 6.8.3 QModbusReply::setError。
 */
void XModbusReply_setError(XModbusReply* reply, XModbusDevice_Error error, const char* errorText);

// --- 中间错误 ---

/**
 * @brief 获取中间错误列表（深拷贝）
 * @param reply XModbusReply实例指针
 * @return 中间错误列表的深拷贝，调用者负责释放；无中间错误返回空列表
 */
XVector* XModbusReply_intermediateErrors(const XModbusReply* reply);

/**
 * @brief 添加中间错误并发出 intermediateErrorOccurred 信号
 * @note 对齐 Qt 6.8.3 QModbusReply::addIntermediateError。
 */
void XModbusReply_addIntermediateError(XModbusReply* reply, XModbusDevice_IntermediateError error);

/******************************************************************************************
* 信号接口 (严格对齐 Qt 信号)
******************************************************************************************/

/**
 * @brief 触发 finished 信号
 * @param reply XModbusReply实例指针（非NULL）
 * @return 信号句柄
 */
void* XModbusReply_finished_signal(XModbusReply* reply);

/**
 * @brief 触发 stateChanged 信号
 * @param reply XModbusReply实例指针（非NULL）
 * @param state 新状态
 * @return 信号句柄
 */
void* XModbusReply_stateChanged_signal(XModbusReply* reply, XModbusReply_State state);

/**
 * @brief 触发 errorOccurred 信号
 * @param reply XModbusReply实例指针（非NULL）
 * @param error 错误码
 * @return 信号句柄
 */
void* XModbusReply_errorOccurred_signal(XModbusReply* reply, XModbusDevice_Error error);

/**
 * @brief 触发 intermediateErrorOccurred 信号
 * @param reply XModbusReply实例指针（非NULL）
 * @param error 中间错误码
 * @return 信号句柄
 */
void* XModbusReply_intermediateErrorOccurred_signal(XModbusReply* reply, XModbusDevice_IntermediateError error);

#define XModbusReply_deleteLater		XObject_deleteLater
#define XModbusReply_deinitLater		XObject_deinitLater
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusReply_create
#define XModbusReply_create(...) XModbusReply_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // XMODBUSREPLY_H
