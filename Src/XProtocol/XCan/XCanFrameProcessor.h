#ifndef XCANFRAMEPROCESSOR_H
#define XCANFRAMEPROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XCanCommonDefinitions.h"
#include "XCanBusFrame.h"
#include "XCanMessageDescription.h"
#include "XCanUniqueIdDescription.h"
#include "XString.h"
#include "XMap.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanFrameProcessor.h
 * @brief CAN 帧处理器头文件（对齐 Qt6 QCanFrameProcessor）
 * @details 提供 CAN 帧与信号值之间的双向转换功能。根据消息描述和唯一 ID 描述，
 *          将 QCanBusFrame 解析为信号值映射，或将信号值映射编码为 CAN 帧。
 *
 * @par 功能特性
 * - 帧解析：将 CAN 帧解析为信号值映射
 * - 帧编码：根据信号值映射生成 CAN 帧
 * - 错误和警告信息
 * - 消息描述管理
 *
 * @par 使用示例
 * @code
 * XCanFrameProcessor processor;
 * XCanFrameProcessor_init(&processor);
 *
 * // 设置唯一 ID 描述
 * XCanUniqueIdDescription uidDesc;
 * XCanUniqueIdDescription_init(&uidDesc);
 * XCanUniqueIdDescription_setSource(&uidDesc, XCanBus_FrameId);
 * XCanFrameProcessor_setUniqueIdDescription(&processor, &uidDesc);
 *
 * // 添加消息描述
 * XCanMessageDescription msgDesc;
 * XCanMessageDescription_init(&msgDesc);
 * XCanMessageDescription_setUniqueId(&msgDesc, 0x123);
 * XCanFrameProcessor_addMessageDescription(&processor, &msgDesc);
 *
 * // 解析帧
 * XCanFrameProcessor_ParseResult result;
 * XCanFrameProcessor_parseFrame(&processor, &frame, &result);
 *
 * // 清理
 * XCanFrameProcessor_deinit(&processor);
 * @endcode
 */

/******************************************************************************************
 * 错误枚举
 ******************************************************************************************/

/**
 * @brief 帧处理器错误枚举
 * @details 对齐 Qt6 QCanFrameProcessor::Error
 */
typedef enum {
    XCanFrameProcessor_Error_None = 0,              ///< 无错误
    XCanFrameProcessor_Error_InvalidFrame,          ///< 无效帧
    XCanFrameProcessor_Error_UnsupportedFrameFormat,///< 不支持的帧格式
    XCanFrameProcessor_Error_Decoding,              ///< 解码错误
    XCanFrameProcessor_Error_Encoding               ///< 编码错误
} XCanFrameProcessor_Error;

/******************************************************************************************
 * 解析结果结构体
 ******************************************************************************************/

/**
 * @brief 帧解析结果结构体
 * @details 包含解析后的唯一 ID 和信号值映射。
 *          对齐 Qt6 QCanFrameProcessor::ParseResult。
 */
typedef struct XCanFrameProcessor_ParseResult {
    XCanBus_UniqueId m_uniqueId;        ///< 解析出的唯一 ID
    XMap* m_signalValues;               ///< 信号值映射表（XMap<XString, XVariant>）
} XCanFrameProcessor_ParseResult;

/******************************************************************************************
 * 帧处理器结构体
 ******************************************************************************************/

/**
 * @brief CAN 帧处理器结构体
 * @details 管理消息描述集合和唯一 ID 描述，提供帧与信号值之间的双向转换。
 */
typedef struct XCanFrameProcessor {
    XVector* m_messageDescriptions;           ///< 消息描述列表（XVector<XCanMessageDescription>）
    XCanUniqueIdDescription m_uidDesc;      ///< 唯一 ID 描述
    XCanFrameProcessor_Error m_error;       ///< 当前错误码
    XString* m_errorString;                 ///< 错误描述
    XStringList* m_warnings;                ///< 警告列表
} XCanFrameProcessor;

/******************************************************************************************
 * 初始化与清理
 ******************************************************************************************/

/**
 * @brief 初始化帧处理器
 * @param processor 待初始化的处理器指针（不可为 NULL）
 */
void XCanFrameProcessor_init(XCanFrameProcessor* processor);

/**
 * @brief 销毁帧处理器（释放内部资源）
 * @param processor 待销毁的处理器指针（可为 NULL）
 */
void XCanFrameProcessor_deinit(XCanFrameProcessor* processor);

/******************************************************************************************
 * 核心功能（对齐 QCanFrameProcessor）
 ******************************************************************************************/

/**
 * @brief 根据信号值映射准备 CAN 帧（编码）
 * @param processor 处理器指针（不可为 NULL）
 * @param uniqueId 消息的唯一 ID
 * @param signalValues 信号值映射表（XMap<XString, XVariant>*）
 * @return 编码后的 CAN 帧（堆分配），调用者负责释放；失败返回 NULL
 */
XCanBusFrame* XCanFrameProcessor_prepareFrame(XCanFrameProcessor* processor,
    XCanBus_UniqueId uniqueId, const XMap* signalValues);

/**
 * @brief 解析 CAN 帧为信号值映射
 * @param processor 处理器指针（不可为 NULL）
 * @param frame 待解析的 CAN 帧
 * @param result 输出参数，解析结果（需已初始化）
 * @return 解析成功返回 true，失败返回 false
 */
bool XCanFrameProcessor_parseFrame(XCanFrameProcessor* processor,
    const XCanBusFrame* frame, XCanFrameProcessor_ParseResult* result);

/******************************************************************************************
 * 错误/警告查询
 ******************************************************************************************/

/**
 * @brief 获取当前错误码
 * @param processor 处理器指针（不可为 NULL）
 * @return 错误码
 */
XCanFrameProcessor_Error XCanFrameProcessor_error(const XCanFrameProcessor* processor);

/**
 * @brief 获取错误描述字符串
 * @param processor 处理器指针（不可为 NULL）
 * @return 错误描述字符串的深拷贝，调用者负责释放
 */
XString* XCanFrameProcessor_errorString(const XCanFrameProcessor* processor);

/**
 * @brief 获取警告列表
 * @param processor 处理器指针（不可为 NULL）
 * @return 警告列表（XStringList*）的深拷贝，调用者负责释放
 */
XStringList* XCanFrameProcessor_warnings(const XCanFrameProcessor* processor);

/******************************************************************************************
 * 消息描述管理
 ******************************************************************************************/

/**
 * @brief 获取所有消息描述
 * @param processor 处理器指针（不可为 NULL）
 * @return 消息描述列表（XVector<XCanMessageDescription>*）的深拷贝，调用者负责释放
 */
XVector* XCanFrameProcessor_messageDescriptions(const XCanFrameProcessor* processor);

/**
 * @brief 添加消息描述
 * @param processor 处理器指针（不可为 NULL）
 * @param descriptions 待添加的消息描述列表
 */
void XCanFrameProcessor_addMessageDescriptions(XCanFrameProcessor* processor,
    const XVector* descriptions);

/**
 * @brief 设置消息描述列表
 * @param processor 处理器指针（不可为 NULL）
 * @param descriptions 新的消息描述列表
 */
void XCanFrameProcessor_setMessageDescriptions(XCanFrameProcessor* processor,
    const XVector* descriptions);

/**
 * @brief 清除所有消息描述
 * @param processor 处理器指针（不可为 NULL）
 */
void XCanFrameProcessor_clearMessageDescriptions(XCanFrameProcessor* processor);

/**
 * @brief 获取唯一 ID 描述
 * @param processor 处理器指针（不可为 NULL）
 * @param out 输出参数，唯一 ID 描述（需已初始化）
 */
void XCanFrameProcessor_uniqueIdDescription(const XCanFrameProcessor* processor,
    XCanUniqueIdDescription* out);

/**
 * @brief 设置唯一 ID 描述
 * @param processor 处理器指针（不可为 NULL）
 * @param description 唯一 ID 描述
 */
void XCanFrameProcessor_setUniqueIdDescription(XCanFrameProcessor* processor,
    const XCanUniqueIdDescription* description);

/******************************************************************************************
 * 解析结果辅助函数
 ******************************************************************************************/

/**
 * @brief 初始化解析结果
 * @param result 待初始化的解析结果指针（不可为 NULL）
 */
void XCanFrameProcessor_ParseResult_init(XCanFrameProcessor_ParseResult* result);

/**
 * @brief 销毁解析结果（释放内部资源）
 * @param result 待销毁的解析结果指针（可为 NULL）
 */
void XCanFrameProcessor_ParseResult_deinit(XCanFrameProcessor_ParseResult* result);

#ifdef __cplusplus
}
#endif

#endif // XCANFRAMEPROCESSOR_H
