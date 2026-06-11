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
typedef struct XModbusReply {
    XObject m_class;                    ///< 继承自XObject基类

    XModbusReply_ReplyType m_type;     ///< 回复类型
    int m_serverAddress;               ///< 从站地址
    bool m_isFinished;                 ///< 是否已完成

    XModbusDevice_Error m_error;       ///< 最终错误码
    XString* m_errorString;            ///< 错误描述字符串

    XModbusDataUnit* m_result;         ///< 结构化结果（Common 类型时有效）
    XModbusResponse* m_rawResult;      ///< 原始 PDU 结果（Raw 类型时有效）
    XModbusRequest* m_request;          //请求 信息
    XVector* m_intermediateErrors;       ///< 中间错误列表 (XList<XModbusDevice_IntermediateError>)
} XModbusReply;

/******************************************************************************************
* 类初始化/实例创建接口
******************************************************************************************/

/**
 * @brief 初始化XModbusReply的虚函数表
 */
XVtable* XModbusReply_class_init(void);

/**
* @brief 创建XModbusReply实例
* @param type 回复类型
* @param serverAddress 从站地址
* @return 新创建的XModbusReply实例指针
*/
XModbusReply* XModbusReply_create(XModbusReply_ReplyType type, int serverAddress);

/**
* @brief 初始化XModbusReply实例
* @param reply 待初始化的实例指针
* @param type 回复类型
* @param serverAddress 从站地址
*/
void XModbusReply_init(XModbusReply* reply, XModbusReply_ReplyType type, int serverAddress);

/******************************************************************************************
 * Public API (严格对齐 QModbusReply)
 ******************************************************************************************/

 // --- 属性查询 ---
XModbusReply_ReplyType XModbusReply_type(const XModbusReply* reply);
int XModbusReply_serverAddress(const XModbusReply* reply);
bool XModbusReply_isFinished(const XModbusReply* reply);

// --- 结果获取 ---
XModbusDataUnit* XModbusReply_result(const XModbusReply* reply);        // 返回拷贝
XModbusResponse* XModbusReply_rawResult(const XModbusReply* reply);     // 返回拷贝

// --- 错误信息 ---
XString* XModbusReply_errorString(const XModbusReply* reply);           // 返回拷贝
XModbusDevice_Error XModbusReply_error(const XModbusReply* reply);



// --- 中间错误 ---
XVector* XModbusReply_intermediateErrors(const XModbusReply* reply); // 返回拷贝列表
void XModbusReply_addIntermediateError(XModbusReply* reply, XModbusDevice_IntermediateError error);

/******************************************************************************************
* 信号接口 (严格对齐 Qt 信号)
******************************************************************************************/

/**
 * @brief 触发 finished 信号
 */
void* XModbusReply_finished_signal(XModbusReply* reply);

/**
* @brief 触发 errorOccurred 信号
*/
void* XModbusReply_errorOccurred_signal(XModbusReply* reply, XModbusDevice_Error error);

/**
* @brief 触发 intermediateErrorOccurred 信号
*/
void* XModbusReply_intermediateErrorOccurred_signal(XModbusReply* reply, XModbusDevice_IntermediateError error);
#define XModbusReply_deleteLater		XObject_deleteLater
#define XModbusReply_deinitLater		XObject_deinitLater
#ifdef __cplusplus
}
#endif

#endif // XMODBUSREPLY_H