#ifndef XCANMESSAGEDESCRIPTION_H
#define XCANMESSAGEDESCRIPTION_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XCanCommonDefinitions.h"
#include "XCanSignalDescription.h"
#include "XString.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanMessageDescription.h
 * @brief CAN 消息描述头文件（对齐 Qt6 QCanMessageDescription）
 * @details 定义 CAN 总线消息的完整描述信息，包括唯一 ID、消息名称、
 *          数据长度、发送节点、注释和信号描述列表。
 *          这是一个值类型结构体，不继承 XClass。
 *
 * @par 功能特性
 * - 唯一 ID 标识
 * - 消息名称
 * - 数据长度（DLC）
 * - 发送节点
 * - 注释
 * - 信号描述列表管理
 *
 * @par 使用示例
 * @code
 * XCanMessageDescription msg;
 * XCanMessageDescription_init(&msg);
 * XCanMessageDescription_setUniqueId(&msg, 0x123);
 * XCanMessageDescription_setName(&msg, "EngineData");
 * XCanMessageDescription_setSize(&msg, 8);
 *
 * // 添加信号
 * XCanSignalDescription sig;
 * XCanSignalDescription_init(&sig);
 * XCanSignalDescription_setName(&sig, "EngineSpeed");
 * XCanMessageDescription_addSignalDescription(&msg, &sig);
 * XCanSignalDescription_deinit(&sig);
 *
 * // 使用完毕后清理
 * XCanMessageDescription_deinit(&msg);
 * @endcode
 */

/******************************************************************************************
 * CAN 消息描述结构体
 ******************************************************************************************/

/**
 * @brief CAN 消息描述结构体
 * @details 封装 CAN 总线消息的完整描述信息，包括标识符、名称、
 *          数据长度、发送节点、注释和信号描述列表。
 */
typedef struct XCanMessageDescription {
    XCanBus_UniqueId m_uniqueId;        ///< 唯一 ID
    XString* m_name;                    ///< 消息名称
    uint8_t m_size;                     ///< 数据长度（字节）
    XString* m_transmitter;             ///< 发送节点
    XString* m_comment;                 ///< 注释
    XVector* m_signalDescriptions;        ///< 信号描述列表（XVector<XCanSignalDescription>）
} XCanMessageDescription;

/******************************************************************************************
 * 初始化与清理
 ******************************************************************************************/

/**
 * @brief 初始化消息描述结构体
 * @param msg 待初始化的消息描述指针（不可为 NULL）
 */
void XCanMessageDescription_init(XCanMessageDescription* msg);

/**
 * @brief 销毁消息描述（释放内部资源）
 * @param msg 待销毁的消息描述指针（可为 NULL）
 */
void XCanMessageDescription_deinit(XCanMessageDescription* msg);

/**
 * @brief 复制消息描述（深拷贝）
 * @param dest 目标消息描述指针（不可为 NULL，需已初始化）
 * @param src 源消息描述指针（不可为 NULL）
 */
void XCanMessageDescription_copy(XCanMessageDescription* dest, const XCanMessageDescription* src);

/**
 * @brief 移动消息描述（转移所有权）
 * @param dest 目标消息描述指针（不可为 NULL，需已初始化）
 * @param src 源消息描述指针（不可为 NULL，移动后源对象被清空）
 */
void XCanMessageDescription_move(XCanMessageDescription* dest, XCanMessageDescription* src);

/**
 * @brief 检查消息描述是否有效
 * @param msg 消息描述指针（不可为 NULL）
 * @return 有效返回 true，否则返回 false
 */
bool XCanMessageDescription_isValid(const XCanMessageDescription* msg);

/******************************************************************************************
 * 属性访问（对齐 QCanMessageDescription）
 ******************************************************************************************/

/**
 * @brief 获取唯一 ID
 * @param msg 消息描述指针（不可为 NULL）
 * @return 唯一 ID
 */
XCanBus_UniqueId XCanMessageDescription_uniqueId(const XCanMessageDescription* msg);

/**
 * @brief 设置唯一 ID
 * @param msg 消息描述指针（不可为 NULL）
 * @param id 唯一 ID
 */
void XCanMessageDescription_setUniqueId(XCanMessageDescription* msg, XCanBus_UniqueId id);

/**
 * @brief 获取消息名称
 * @param msg 消息描述指针（不可为 NULL）
 * @return 消息名称字符串的深拷贝，调用者负责释放
 */
XString* XCanMessageDescription_name(const XCanMessageDescription* msg);

/**
 * @brief 设置消息名称
 * @param msg 消息描述指针（不可为 NULL）
 * @param name 消息名称
 */
void XCanMessageDescription_setName(XCanMessageDescription* msg, const char* name);

/**
 * @brief 获取数据长度
 * @param msg 消息描述指针（不可为 NULL）
 * @return 数据长度（字节）
 */
uint8_t XCanMessageDescription_size(const XCanMessageDescription* msg);

/**
 * @brief 设置数据长度
 * @param msg 消息描述指针（不可为 NULL）
 * @param size 数据长度（字节）
 */
void XCanMessageDescription_setSize(XCanMessageDescription* msg, uint8_t size);

/**
 * @brief 获取发送节点
 * @param msg 消息描述指针（不可为 NULL）
 * @return 发送节点字符串的深拷贝，调用者负责释放
 */
XString* XCanMessageDescription_transmitter(const XCanMessageDescription* msg);

/**
 * @brief 设置发送节点
 * @param msg 消息描述指针（不可为 NULL）
 * @param transmitter 发送节点
 */
void XCanMessageDescription_setTransmitter(XCanMessageDescription* msg, const char* transmitter);

/**
 * @brief 获取注释
 * @param msg 消息描述指针（不可为 NULL）
 * @return 注释字符串的深拷贝，调用者负责释放
 */
XString* XCanMessageDescription_comment(const XCanMessageDescription* msg);

/**
 * @brief 设置注释
 * @param msg 消息描述指针（不可为 NULL）
 * @param text 注释文本
 */
void XCanMessageDescription_setComment(XCanMessageDescription* msg, const char* text);

/**
 * @brief 获取所有信号描述列表
 * @param msg 消息描述指针（不可为 NULL）
 * @return 信号描述列表（XVector<XCanSignalDescription>*）的深拷贝，调用者负责释放
 */
XVector* XCanMessageDescription_signalDescriptions(const XCanMessageDescription* msg);

/**
 * @brief 根据信号名称查找信号描述
 * @param msg 消息描述指针（不可为 NULL）
 * @param name 信号名称
 * @param out 输出参数，找到的信号描述（需已初始化）
 * @return 找到返回 true，未找到返回 false
 */
bool XCanMessageDescription_signalDescriptionForName(const XCanMessageDescription* msg,
    const char* name, XCanSignalDescription* out);

/**
 * @brief 清除所有信号描述
 * @param msg 消息描述指针（不可为 NULL）
 */
void XCanMessageDescription_clearSignalDescriptions(XCanMessageDescription* msg);

/**
 * @brief 添加一个信号描述
 * @param msg 消息描述指针（不可为 NULL）
 * @param description 信号描述指针
 */
void XCanMessageDescription_addSignalDescription(XCanMessageDescription* msg,
    const XCanSignalDescription* description);

/**
 * @brief 设置信号描述列表
 * @param msg 消息描述指针（不可为 NULL）
 * @param descriptions 信号描述列表
 */
void XCanMessageDescription_setSignalDescriptions(XCanMessageDescription* msg,
    const XVector* descriptions);

#ifdef __cplusplus
}
#endif

#endif // XCANMESSAGEDESCRIPTION_H
