#ifndef XCANBUSFRAME_H
#define XCANBUSFRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XByteArray.h"
#include "XString.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanBusFrame.h
 * @brief CAN 总线数据帧头文件（对齐 Qt6 QCanBusFrame）
 * @details 定义 CAN 数据帧的完整结构，包括帧 ID、帧类型、负载数据、时间戳、
 *          扩展帧格式、CAN FD 标志、错误标志等。这是一个值类型结构体，
 *          不继承 XClass，直接通过 memcpy 传递。
 *
 * @par 功能特性
 * - 支持标准帧（11 位 ID）和扩展帧（29 位 ID）
 * - 支持 Classic CAN 和 CAN FD（灵活数据速率）
 * - 支持错误帧、远程帧、数据帧等多种帧类型
 * - 微秒级时间戳
 * - 位速率切换和错误状态指示
 * - 本地回显标志
 *
 * @par 使用示例
 * @code
 * // 创建数据帧
 * XCanBusFrame frame;
 * XCanBusFrame_init(&frame);
 * XCanBusFrame_setFrameId(&frame, 0x7FF);
 * uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
 * XCanBusFrame_setPayload(&frame, data, 4);
 * XCanBusFrame_setFrameType(&frame, XCanBusFrame_DataFrame);
 *
 * // 检查有效性
 * if (XCanBusFrame_isValid(&frame)) {
 *     // 发送帧...
 * }
 *
 * // 清理
 * XCanBusFrame_deinit(&frame);
 * @endcode
 */

/******************************************************************************************
 * 常量定义
 ******************************************************************************************/

/**
 * @brief 标准帧最大 ID 值（11 位）
 */
#define XCANBUSFRAME_STANDARD_ID_MAX 0x7FF

/**
 * @brief 扩展帧最大 ID 值（29 位）
 */
#define XCANBUSFRAME_EXTENDED_ID_MAX 0x1FFFFFFF

/**
 * @brief 标准 CAN 最大负载长度
 */
#define XCANBUSFRAME_MAX_PAYLOAD_CLASSIC 8

/**
 * @brief CAN FD 最大负载长度
 */
#define XCANBUSFRAME_MAX_PAYLOAD_FD 64

/******************************************************************************************
 * 枚举类型定义
 ******************************************************************************************/

/**
 * @brief CAN 帧类型枚举
 * @details 对齐 Qt6 QCanBusFrame::FrameType
 */
typedef enum {
    XCanBusFrame_UnknownFrame = 0x0,        ///< 未知帧类型
    XCanBusFrame_DataFrame = 0x1,           ///< 数据帧
    XCanBusFrame_ErrorFrame = 0x2,          ///< 错误帧
    XCanBusFrame_RemoteRequestFrame = 0x3,  ///< 远程请求帧（RTR）
    XCanBusFrame_InvalidFrame = 0x4         ///< 无效帧
} XCanBusFrame_FrameType;

/**
 * @brief CAN 帧错误标志枚举
 * @details 对齐 Qt6 QCanBusFrame::FrameError，使用位标志组合
 */
typedef enum {
    XCanBusFrame_NoError                    = 0,            ///< 无错误
    XCanBusFrame_TransmissionTimeoutError   = (1 << 0),    ///< 传输超时错误
    XCanBusFrame_LostArbitrationError       = (1 << 1),    ///< 仲裁丢失错误
    XCanBusFrame_ControllerError            = (1 << 2),    ///< 控制器错误
    XCanBusFrame_ProtocolViolationError     = (1 << 3),    ///< 协议违规错误
    XCanBusFrame_TransceiverError           = (1 << 4),    ///< 收发器错误
    XCanBusFrame_MissingAcknowledgmentError = (1 << 5),    ///< 缺少确认错误
    XCanBusFrame_BusOffError                = (1 << 6),    ///< 总线关闭错误
    XCanBusFrame_BusError                   = (1 << 7),    ///< 总线错误
    XCanBusFrame_ControllerRestartError     = (1 << 8),    ///< 控制器重启错误
    XCanBusFrame_UnknownError               = (1 << 9),    ///< 未知错误
    XCanBusFrame_AnyError                   = 0x1FFFFFFFU  ///< 任意错误（29 位掩码）
} XCanBusFrame_FrameError;

/******************************************************************************************
 * 时间戳结构体
 ******************************************************************************************/

/**
 * @brief CAN 帧时间戳结构体
 * @details 提供微秒精度的时间戳信息，对齐 Qt6 QCanBusFrame::TimeStamp
 */
typedef struct XCanBusFrame_TimeStamp {
    int64_t m_secs;     ///< 秒数
    int64_t m_usecs;    ///< 微秒数（0-999999）
} XCanBusFrame_TimeStamp;

/******************************************************************************************
 * CAN 帧结构体
 ******************************************************************************************/

/**
 * @brief CAN 总线数据帧结构体
 * @details 封装 CAN 帧的所有属性，包括帧 ID、帧类型、负载数据、时间戳、
 *          扩展帧格式、CAN FD 标志、位速率切换、错误状态指示、本地回显等。
 *          这是一个值类型结构体，不继承 XClass。
 */
typedef struct XCanBusFrame {
    uint32_t m_canId : 29;          ///< CAN 帧 ID（29 位，错误帧时存储错误码）
    uint8_t m_format : 3;           ///< 帧格式（XCanBusFrame_FrameType，最多 8 种类型）

    uint8_t m_isExtendedFrame : 1;  ///< 是否为扩展帧（29 位 ID）
    uint8_t m_version : 5;          ///< 内部版本号
    uint8_t m_isValidFrameId : 1;   ///< 帧 ID 是否有效
    uint8_t m_isFlexibleDataRate : 1;   ///< 是否为 CAN FD 帧

    uint8_t m_isBitrateSwitch : 1;      ///< 是否启用位速率切换
    uint8_t m_isErrorStateIndicator : 1;///< 是否设置错误状态指示
    uint8_t m_isLocalEcho : 1;          ///< 是否为本地回显帧
    uint8_t m_reserved0 : 5;            ///< 保留位

    uint8_t m_reserved[2];          ///< 保留字节（未来扩展）

    XByteArray* m_load;             ///< 帧负载数据（XByteArray）
    XCanBusFrame_TimeStamp m_stamp; ///< 帧时间戳
} XCanBusFrame;

/******************************************************************************************
 * 创建与初始化
 ******************************************************************************************/

/**
 * @brief 创建 CAN 帧实例（堆分配）
 * @param type 帧类型（默认为 XCanBusFrame_DataFrame）
 * @return 成功返回 XCanBusFrame 指针，失败返回 NULL
 * @note 返回的对象需通过 XCanBusFrame_delete 释放
 */
XCanBusFrame* XCanBusFrame_create(XCanBusFrame_FrameType type);

/**
 * @brief 创建带 ID 和数据的 CAN 帧实例（堆分配）
 * @param identifier 帧 ID（11 位或 29 位）
 * @param data 负载数据指针
 * @param size 负载数据大小
 * @return 成功返回 XCanBusFrame 指针，失败返回 NULL
 * @note 返回的对象需通过 XCanBusFrame_delete 释放
 */
XCanBusFrame* XCanBusFrame_create_with_data(uint32_t identifier, const uint8_t* data, size_t size);

/**
 * @brief 创建 CAN 帧的深拷贝（堆分配）
 * @param other 源帧指针（不可为 NULL）
 * @return 成功返回新的 XCanBusFrame 指针，失败返回 NULL
 */
XCanBusFrame* XCanBusFrame_create_copy(const XCanBusFrame* other);

/**
 * @brief 初始化 CAN 帧结构体（栈上使用）
 * @param frame 待初始化的帧指针（不可为 NULL）
 * @param type 帧类型
 * @note 栈上使用的帧必须调用此函数初始化，使用完毕后调用 XCanBusFrame_deinit
 */
void XCanBusFrame_init(XCanBusFrame* frame, XCanBusFrame_FrameType type);

/**
 * @brief 销毁 CAN 帧（释放内部资源）
 * @param frame 待销毁的帧指针（可为 NULL）
 * @note 释放内部负载数据，但不会释放 frame 本身（栈上对象调用此函数）
 */
void XCanBusFrame_deinit(XCanBusFrame* frame);

/**
 * @brief 删除 CAN 帧（释放堆分配的帧）
 * @param frame 待删除的帧指针（可为 NULL）
 * @note 释放帧本身及其内部资源，仅用于 XCanBusFrame_create 创建的帧
 */
void XCanBusFrame_delete(XCanBusFrame* frame);

/******************************************************************************************
 * 帧属性访问
 ******************************************************************************************/

/**
 * @brief 检查帧是否有效
 * @param frame 帧指针（不可为 NULL）
 * @return 有效返回 true，无效返回 false
 * @details 检查条件：
 * - 帧类型不能为 InvalidFrame
 * - 非扩展帧时，ID 不能超过 11 位
 * - isValidFrameId 必须为 true
 * - 负载长度不能超过最大允许值（CAN FD 64 字节，Classic CAN 8 字节）
 * - CAN FD 帧不能是 RemoteRequestFrame
 */
bool XCanBusFrame_isValid(const XCanBusFrame* frame);

/**
 * @brief 获取帧类型
 * @param frame 帧指针（不可为 NULL）
 * @return 帧类型枚举值
 */
XCanBusFrame_FrameType XCanBusFrame_frameType(const XCanBusFrame* frame);

/**
 * @brief 设置帧类型
 * @param frame 帧指针（不可为 NULL）
 * @param type 新的帧类型
 */
void XCanBusFrame_setFrameType(XCanBusFrame* frame, XCanBusFrame_FrameType type);

/**
 * @brief 检查是否为扩展帧格式（29 位 ID）
 * @param frame 帧指针（不可为 NULL）
 * @return 扩展帧返回 true，标准帧返回 false
 */
bool XCanBusFrame_hasExtendedFrameFormat(const XCanBusFrame* frame);

/**
 * @brief 设置扩展帧格式标志
 * @param frame 帧指针（不可为 NULL）
 * @param isExtended true 设置为扩展帧，false 设置为标准帧
 */
void XCanBusFrame_setExtendedFrameFormat(XCanBusFrame* frame, bool isExtended);

/**
 * @brief 获取帧 ID
 * @param frame 帧指针（不可为 NULL）
 * @return 帧 ID（错误帧返回 0）
 */
uint32_t XCanBusFrame_frameId(const XCanBusFrame* frame);

/**
 * @brief 设置帧 ID
 * @param frame 帧指针（不可为 NULL）
 * @param newFrameId 新的帧 ID（不能超过 0x1FFFFFFF）
 * @note 如果 ID 超过 11 位（0x7FF），自动设置扩展帧格式
 */
void XCanBusFrame_setFrameId(XCanBusFrame* frame, uint32_t newFrameId);

/**
 * @brief 获取帧负载数据（深拷贝）
 * @param frame 帧指针（不可为 NULL）
 * @return 负载数据的深拷贝，调用者负责释放；无数据返回空 XByteArray
 */
XByteArray* XCanBusFrame_payload(const XCanBusFrame* frame);

/**
 * @brief 获取帧负载数据常量引用
 * @param frame 帧指针（不可为 NULL）
 * @return 负载数据的常量指针，调用者不应释放
 */
const XByteArray* XCanBusFrame_payload_const(const XCanBusFrame* frame);

/**
 * @brief 设置帧负载数据
 * @param frame 帧指针（不可为 NULL）
 * @param data 负载数据指针
 * @param size 负载数据大小
 * @note 如果数据超过 8 字节，自动设置 CAN FD 标志
 */
void XCanBusFrame_setPayload(XCanBusFrame* frame, const uint8_t* data, size_t size);

/**
 * @brief 设置帧负载数据（从 XByteArray）
 * @param frame 帧指针（不可为 NULL）
 * @param data 负载数据 XByteArray 指针
 */
void XCanBusFrame_setPayload_from_array(XCanBusFrame* frame, const XByteArray* data);

/**
 * @brief 获取帧时间戳
 * @param frame 帧指针（不可为 NULL）
 * @return 时间戳结构体
 */
XCanBusFrame_TimeStamp XCanBusFrame_timeStamp(const XCanBusFrame* frame);

/**
 * @brief 设置帧时间戳
 * @param frame 帧指针（不可为 NULL）
 * @param ts 新的时间戳
 */
void XCanBusFrame_setTimeStamp(XCanBusFrame* frame, XCanBusFrame_TimeStamp ts);

/******************************************************************************************
 * 错误帧相关
 ******************************************************************************************/

/**
 * @brief 获取错误帧的错误标志
 * @param frame 帧指针（不可为 NULL）
 * @return 错误标志组合（非错误帧返回 NoError）
 */
uint32_t XCanBusFrame_error(const XCanBusFrame* frame);

/**
 * @brief 设置错误帧的错误标志
 * @param frame 帧指针（不可为 NULL）
 * @param err 错误标志组合
 * @note 仅在帧类型为 ErrorFrame 时有效
 */
void XCanBusFrame_setError(XCanBusFrame* frame, uint32_t err);

/******************************************************************************************
 * CAN FD 相关
 ******************************************************************************************/

/**
 * @brief 检查是否为 CAN FD 帧
 * @param frame 帧指针（不可为 NULL）
 * @return CAN FD 返回 true，Classic CAN 返回 false
 */
bool XCanBusFrame_hasFlexibleDataRateFormat(const XCanBusFrame* frame);

/**
 * @brief 设置 CAN FD 格式标志
 * @param frame 帧指针（不可为 NULL）
 * @param isFlexibleData true 设置为 CAN FD，false 设置为 Classic CAN
 * @note 禁用 CAN FD 时，同时清除位速率切换和错误状态指示标志
 */
void XCanBusFrame_setFlexibleDataRateFormat(XCanBusFrame* frame, bool isFlexibleData);

/**
 * @brief 检查是否启用了位速率切换
 * @param frame 帧指针（不可为 NULL）
 * @return 启用返回 true，否则返回 false
 */
bool XCanBusFrame_hasBitrateSwitch(const XCanBusFrame* frame);

/**
 * @brief 设置位速率切换标志
 * @param frame 帧指针（不可为 NULL）
 * @param bitrateSwitch true 启用位速率切换
 * @note 启用位速率切换时，自动设置 CAN FD 标志
 */
void XCanBusFrame_setBitrateSwitch(XCanBusFrame* frame, bool bitrateSwitch);

/**
 * @brief 检查是否设置了错误状态指示
 * @param frame 帧指针（不可为 NULL）
 * @return 已设置返回 true，否则返回 false
 */
bool XCanBusFrame_hasErrorStateIndicator(const XCanBusFrame* frame);

/**
 * @brief 设置错误状态指示标志
 * @param frame 帧指针（不可为 NULL）
 * @param errorStateIndicator true 设置错误状态指示
 * @note 启用错误状态指示时，自动设置 CAN FD 标志
 */
void XCanBusFrame_setErrorStateIndicator(XCanBusFrame* frame, bool errorStateIndicator);

/**
 * @brief 检查是否为本地回显帧
 * @param frame 帧指针（不可为 NULL）
 * @return 本地回显返回 true，否则返回 false
 */
bool XCanBusFrame_hasLocalEcho(const XCanBusFrame* frame);

/**
 * @brief 设置本地回显标志
 * @param frame 帧指针（不可为 NULL）
 * @param localEcho true 设置为本地回显
 */
void XCanBusFrame_setLocalEcho(XCanBusFrame* frame, bool localEcho);

/******************************************************************************************
 * 字符串表示
 ******************************************************************************************/

/**
 * @brief 将 CAN 帧转换为可读字符串
 * @param frame 帧指针（不可为 NULL）
 * @return 字符串表示，调用者负责释放
 * @details 输出格式示例：
 * - 标准帧: "  7FF   [1]  01"
 * - 扩展帧: "1FFFFFFF   [8]  01 23 45 67 89 AB CD EF"
 * - CAN FD: "  400  [10]  01 23 45 67 ... EF 01 23"
 * - 远程帧: "  123   [5]  Remote Request"
 * - 错误帧: "(Error)"
 */
XString* XCanBusFrame_toString(const XCanBusFrame* frame);

/******************************************************************************************
 * 时间戳工具函数
 ******************************************************************************************/

/**
 * @brief 从微秒数创建时间戳
 * @param usec 微秒数
 * @return 归一化的时间戳（微秒数超过 1000000 自动转换为秒）
 */
XCanBusFrame_TimeStamp XCanBusFrame_TimeStamp_fromMicroSeconds(int64_t usec);

/**
 * @brief 获取时间戳的秒数部分
 * @param ts 时间戳
 * @return 秒数
 */
int64_t XCanBusFrame_TimeStamp_seconds(XCanBusFrame_TimeStamp ts);

/**
 * @brief 获取时间戳的微秒数部分
 * @param ts 时间戳
 * @return 微秒数（0-999999）
 */
int64_t XCanBusFrame_TimeStamp_microSeconds(XCanBusFrame_TimeStamp ts);

#ifdef __cplusplus
}
#endif

#endif // XCANBUSFRAME_H
