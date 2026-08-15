// XNetworkDatagram.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言模拟 Qt6 QNetworkDatagram，UDP 数据报封装。

#ifndef XNETWORKDATAGRAM_H
#define XNETWORKDATAGRAM_H
#include "XNetwork_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XNETWORK_ON
#if XNETWORK_DATAGRAM_ON

#include "XClass.h"
#include "XHostAddress.h"
#include "XByteArray.h"
#include <stdint.h>
#include <stdbool.h>

// ==================== 虚函数表定义 ====================
XCLASS_DEFINE_BEGING(XNetworkDatagram)
XCLASS_DEFINE_EXTEND_END(XNetworkDatagram, XClass)

// =============== 核心结构体 ===============

/**
 * @brief UDP 数据报封装，包含数据、地址、端口、跳数限制等信息。
 *
 * XNetworkDatagram 是一个值语义类，支持：
 * - 存储数据报数据（XByteArray）
 * - 源地址和目的地址（XHostAddress）
 * - 源端口和目的端口
 * - 跳数限制（TTL/hop limit）
 * - 网络接口索引
 */
typedef struct XNetworkDatagram {
    XClass m_class;                      ///< 基类虚表指针
    
    XByteArray* data;                    ///< 数据报内容
    XHostAddress senderAddress;          ///< 发送者地址
    XHostAddress destinationAddress;     ///< 目标地址
    uint16_t senderPort;                 ///< 发送者端口
    uint16_t destinationPort;            ///< 目标端口
    int hopLimit;                        ///< 跳数限制（TTL），-1 表示未设置
    uint32_t interfaceIndex;             ///< 网络接口索引（IPv6 scope）
} XNetworkDatagram;

// ==================== 构造与析构 ====================

/**
 * @brief 初始化已分配的 XNetworkDatagram 结构体。
 * @param dgram 指向未初始化的 XNetworkDatagram 实例
 */
void XNetworkDatagram_init(XNetworkDatagram* dgram);

/**
 * @brief 创建一个空的 XNetworkDatagram 实例。
 * @return 新分配的实例，需调用 XNetworkDatagram_delete_base() 释放
 */
XNetworkDatagram* XNetworkDatagram_create_ex(XMemoryType memory);

/**
 * @brief 从数据创建数据报。
 * @param data 数据内容（会被复制）
 * @param destinationAddress 目标地址（可为 NULL）
 * @param port 目标端口
 * @return 新创建的实例
 */
XNetworkDatagram* XNetworkDatagram_create_2(const XByteArray* data, const XHostAddress* destinationAddress, uint16_t port);

/**
 * @brief 拷贝构造。
 * @param other 源数据报
 * @return 新创建的副本
 */
XNetworkDatagram* XNetworkDatagram_create_copy(const XNetworkDatagram* other);

/**
 * @brief 释放内部资源。
 * @param dgram 数据报实例
 */
void XNetworkDatagram_deinit(XNetworkDatagram* dgram);

#define XNetworkDatagram_delete_base    XClass_delete_base
#define XNetworkDatagram_deinit_base    XClass_deinit_base
#define XNetworkDatagram_copy_base      XClass_copy_base
#define XNetworkDatagram_move_base      XClass_move_base

/**
 * @brief 初始化虚函数表。
 * @return 虚函数表指针
 */
XVtable* XNetworkDatagram_class_init(void);

// ==================== 清空与有效性 ====================

/**
 * @brief 清空数据报内容和元数据。
 * @param dgram 数据报实例
 */
void XNetworkDatagram_clear(XNetworkDatagram* dgram);

/**
 * @brief 判断数据报是否有效。
 * @param dgram 数据报实例
 * @return true 若至少有一个发送者或接收者地址
 */
bool XNetworkDatagram_isValid(const XNetworkDatagram* dgram);

/**
 * @brief 判断数据报是否为空。
 * @param dgram 数据报实例
 * @return true 若无效
 */
bool XNetworkDatagram_isNull(const XNetworkDatagram* dgram);

// ==================== 数据访问 ====================

/**
 * @brief 获取数据报内容。
 * @param dgram 数据报实例
 * @return 指向 XByteArray 的指针（不应被释放）
 */
XByteArray* XNetworkDatagram_data(const XNetworkDatagram* dgram);

/**
 * @brief 设置数据报内容。
 * @param dgram 数据报实例
 * @param data 数据内容（会被复制）
 */
void XNetworkDatagram_setData(XNetworkDatagram* dgram, const XByteArray* data);

// ==================== 发送者信息 ====================

/**
 * @brief 获取发送者地址。
 * @param dgram 数据报实例
 * @return 指向发送者地址的指针
 */
const XHostAddress* XNetworkDatagram_senderAddress(const XNetworkDatagram* dgram);

/**
 * @brief 获取发送者端口。
 * @param dgram 数据报实例
 * @return 端口号，未设置返回 -1
 */
int XNetworkDatagram_senderPort(const XNetworkDatagram* dgram);

/**
 * @brief 设置发送者地址和端口。
 * @param dgram 数据报实例
 * @param address 地址
 * @param port 端口（0 表示由系统选择）
 */
void XNetworkDatagram_setSender(XNetworkDatagram* dgram, const XHostAddress* address, uint16_t port);

// ==================== 目标信息 ====================

/**
 * @brief 获取目标地址。
 * @param dgram 数据报实例
 * @return 指向目标地址的指针
 */
const XHostAddress* XNetworkDatagram_destinationAddress(const XNetworkDatagram* dgram);

/**
 * @brief 获取目标端口。
 * @param dgram 数据报实例
 * @return 端口号，未设置返回 -1
 */
int XNetworkDatagram_destinationPort(const XNetworkDatagram* dgram);

/**
 * @brief 设置目标地址和端口。
 * @param dgram 数据报实例
 * @param address 目标地址
 * @param port 目标端口
 */
void XNetworkDatagram_setDestination(XNetworkDatagram* dgram, const XHostAddress* address, uint16_t port);

// ==================== 跳数限制 ====================

/**
 * @brief 获取跳数限制（TTL）。
 * @param dgram 数据报实例
 * @return 跳数限制，-1 表示未设置
 */
int XNetworkDatagram_hopLimit(const XNetworkDatagram* dgram);

/**
 * @brief 设置跳数限制（TTL）。
 * @param dgram 数据报实例
 * @param count 跳数（1-255，-1 表示由系统选择）
 */
void XNetworkDatagram_setHopLimit(XNetworkDatagram* dgram, int count);

// ==================== 网络接口 ====================

/**
 * @brief 获取网络接口索引。
 * @param dgram 数据报实例
 * @return 接口索引，0 表示未设置
 */
uint32_t XNetworkDatagram_interfaceIndex(const XNetworkDatagram* dgram);

/**
 * @brief 设置网络接口索引。
 * @param dgram 数据报实例
 * @param index 接口索引
 */
void XNetworkDatagram_setInterfaceIndex(XNetworkDatagram* dgram, uint32_t index);

// ==================== 交换 ====================

/**
 * @brief 交换两个数据报的内容。
 * @param a 数据报 A
 * @param b 数据报 B
 */
void XNetworkDatagram_swap(XNetworkDatagram* a, XNetworkDatagram* b);

// ==================== 回复 ====================

/**
 * @brief 创建回复数据报。
 * @param dgram 原始数据报（接收到的）
 * @param payload 回复数据
 * @return 新创建的回复数据报（需调用者释放）
 */
XNetworkDatagram* XNetworkDatagram_makeReply(const XNetworkDatagram* dgram, const XByteArray* payload);

#endif // XNETWORK_DATAGRAM_ON
#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XNetworkDatagram_create
#define XNetworkDatagram_create(...) XNetworkDatagram_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // XNETWORKDATAGRAM_H
