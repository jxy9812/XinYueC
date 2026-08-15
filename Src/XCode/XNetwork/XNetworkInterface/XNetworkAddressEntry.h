// XNetworkAddressEntry.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言模拟 Qt6 QNetworkAddressEntry。
// 表示网络接口的一个 IP 地址条目，包含 IP 地址、子网掩码和广播地址。

#ifndef XNETWORKADDRESSENTRY_H
#define XNETWORKADDRESSENTRY_H
#include "XNetwork_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XNETWORK_ON
#if XNETWORK_INTERFACE_ON

#include "XClass.h"
#include "XHostAddress.h"
#include <stdbool.h>
#include <stdint.h>

// =============== 枚举定义 ===============

/**
 * @brief DNS 资格状态。
 *
 * 指示给定主机地址是否有资格在域名系统（DNS）或类似名称解析机制中发布。
 */
typedef enum XNetworkAddressEntry_DnsEligibilityStatus {
    XNetworkAddressEntry_DnsEligibilityUnknown = -1, ///< 无法确定是否应发布
    XNetworkAddressEntry_DnsIneligible = 0,          ///< 不应在 DNS 中发布
    XNetworkAddressEntry_DnsEligible = 1             ///< 有资格在 DNS 中发布
} XNetworkAddressEntry_DnsEligibilityStatus;

// =============== 类定义 ===============

XCLASS_DEFINE_BEGING(XNetworkAddressEntry)
XCLASS_DEFINE_EXTEND_END(XNetworkAddressEntry, XClass);

/**
 * @brief 网络地址条目类。
 *
 * XNetworkAddressEntry 封装了一个网络接口的 IP 地址信息，
 * 包括 IP 地址、子网掩码、广播地址、地址生命周期等。
 */
typedef struct XNetworkAddressEntry {
    XClass m_class;             ///< 基类虚表指针
    XHostAddress ip;            ///< IP 地址
    XHostAddress netmask;       ///< 子网掩码
    XHostAddress broadcast;     ///< 广播地址
    bool broadcastIsValid;      ///< 广播地址是否有效
    
    // ====== 地址生命周期（IPv6 地址常用）======
    int64_t preferredLifetime;  ///< 首选生命周期（毫秒），-1 表示永久/未知
    int64_t validityLifetime;   ///< 有效生命周期（毫秒），-1 表示永久/未知
    bool lifetimeKnown;         ///< 生命周期是否已知
    
    // ====== DNS 资格 ======
    XNetworkAddressEntry_DnsEligibilityStatus dnsEligibilityStatus; ///< DNS 资格状态
    
    // ====== 地址类型标志 ======
    bool isPermanent;           ///< 是否为永久地址
} XNetworkAddressEntry;

// ==================== 构造与析构 ====================

/**
 * @brief 创建一个空的 XNetworkAddressEntry 实例。
 * @return 新分配的实例，需调用 XNetworkAddressEntry_delete_base() 释放。
 */
XNetworkAddressEntry* XNetworkAddressEntry_create_ex(XMemoryType memory);

/**
 * @brief 创建一个指定 IP 地址的 XNetworkAddressEntry 实例。
 * @param ip IP 地址
 * @return 新分配的实例
 */
XNetworkAddressEntry* XNetworkAddressEntry_createWithIp(const XHostAddress* ip);

/**
 * @brief 创建一个完整的 XNetworkAddressEntry 实例。
 * @param ip IP 地址
 * @param netmask 子网掩码
 * @param broadcast 广播地址（可为 NULL）
 * @return 新分配的实例
 */
XNetworkAddressEntry* XNetworkAddressEntry_createFull(const XHostAddress* ip,
                                                       const XHostAddress* netmask,
                                                       const XHostAddress* broadcast);

/**
 * @brief 拷贝构造函数。
 * @param other 源对象
 * @return 新分配的副本
 */
XNetworkAddressEntry* XNetworkAddressEntry_create_copy(const XNetworkAddressEntry* other);

/**
 * @brief 初始化已分配的 XNetworkAddressEntry 结构体。
 * @param entry 指向未初始化的实例
 */
void XNetworkAddressEntry_init(XNetworkAddressEntry* entry);

/**
 * @brief 初始化虚函数表。
 * @return 虚函数表指针
 */
XVtable* XNetworkAddressEntry_class_init(void);

#define XNetworkAddressEntry_delete_base    XClass_delete_base
#define XNetworkAddressEntry_deinit_base    XClass_deinit_base
#define XNetworkAddressEntry_copy_base      XClass_copy_base
#define XNetworkAddressEntry_move_base      XClass_move_base

// ==================== 属性访问器 ====================

/**
 * @brief 获取 IP 地址。
 * @param entry 实例指针
 * @return IP 地址指针（不应被释放）
 */
const XHostAddress* XNetworkAddressEntry_ip(const XNetworkAddressEntry* entry);

/**
 * @brief 设置 IP 地址。
 * @param entry 实例指针
 * @param ip IP 地址
 */
void XNetworkAddressEntry_setIp(XNetworkAddressEntry* entry, const XHostAddress* ip);

/**
 * @brief 获取子网掩码。
 * @param entry 实例指针
 * @return 子网掩码指针（不应被释放）
 */
const XHostAddress* XNetworkAddressEntry_netmask(const XNetworkAddressEntry* entry);

/**
 * @brief 设置子网掩码。
 * @param entry 实例指针
 * @param netmask 子网掩码
 * @note 设置子网掩码会自动更新前缀长度
 */
void XNetworkAddressEntry_setNetmask(XNetworkAddressEntry* entry, const XHostAddress* netmask);

/**
 * @brief 获取广播地址。
 * @param entry 实例指针
 * @return 广播地址指针，如果无效返回 NULL
 */
const XHostAddress* XNetworkAddressEntry_broadcast(const XNetworkAddressEntry* entry);

/**
 * @brief 设置广播地址。
 * @param entry 实例指针
 * @param broadcast 广播地址（可为 NULL）
 */
void XNetworkAddressEntry_setBroadcast(XNetworkAddressEntry* entry, const XHostAddress* broadcast);

/**
 * @brief 判断广播地址是否有效。
 * @param entry 实例指针
 * @return true 如果广播地址有效
 */
bool XNetworkAddressEntry_isBroadcastValid(const XNetworkAddressEntry* entry);

/**
 * @brief 清除广播地址。
 * @param entry 实例指针
 */
void XNetworkAddressEntry_clearBroadcast(XNetworkAddressEntry* entry);

/**
 * @brief 获取前缀长度（子网掩码中连续1的位数）。
 * @param entry 实例指针
 * @return 前缀长度（IPv4: 0-32, IPv6: 0-128），无法确定返回 -1
 * @note 对于 IPv4，典型值为 8、16、24 等；对于 IPv6，典型值为 64、128 等
 */
int XNetworkAddressEntry_prefixLength(const XNetworkAddressEntry* entry);

/**
 * @brief 设置前缀长度。
 * @param entry 实例指针
 * @param length 前缀长度（IPv4: 0-32, IPv6: 0-128）
 * @note 设置前缀长度会自动更新子网掩码
 */
void XNetworkAddressEntry_setPrefixLength(XNetworkAddressEntry* entry, int length);

// ==================== 地址生命周期 ====================

/**
 * @brief 获取首选生命周期。
 * @param entry 实例指针
 * @return 首选生命周期（毫秒），-1 表示永久或未知
 * @note 地址在首选期间可作为新发出数据包的源地址
 */
int64_t XNetworkAddressEntry_preferredLifetime(const XNetworkAddressEntry* entry);

/**
 * @brief 获取有效生命周期。
 * @param entry 实例指针
 * @return 有效生命周期（毫秒），-1 表示永久或未知
 * @note 地址在有效期间可作为本机的有效目标地址
 */
int64_t XNetworkAddressEntry_validityLifetime(const XNetworkAddressEntry* entry);

/**
 * @brief 判断生命周期是否已知。
 * @param entry 实例指针
 * @return true 如果生命周期已知
 */
bool XNetworkAddressEntry_isLifetimeKnown(const XNetworkAddressEntry* entry);

/**
 * @brief 设置地址生命周期。
 * @param entry 实例指针
 * @param preferred 首选生命周期（毫秒），-1 表示永久
 * @param validity 有效生命周期（毫秒），-1 表示永久
 */
void XNetworkAddressEntry_setAddressLifetime(XNetworkAddressEntry* entry, 
                                              int64_t preferred, int64_t validity);

/**
 * @brief 清除地址生命周期。
 * @param entry 实例指针
 * @note 调用后 isLifetimeKnown() 返回 false
 */
void XNetworkAddressEntry_clearAddressLifetime(XNetworkAddressEntry* entry);

// ==================== DNS 资格 ====================

/**
 * @brief 获取 DNS 资格状态。
 * @param entry 实例指针
 * @return DNS 资格状态
 * @note 有资格的地址可在 DNS 或类似名称解析机制中发布
 */
XNetworkAddressEntry_DnsEligibilityStatus XNetworkAddressEntry_dnsEligibility(const XNetworkAddressEntry* entry);

/**
 * @brief 设置 DNS 资格状态。
 * @param entry 实例指针
 * @param status DNS 资格状态
 */
void XNetworkAddressEntry_setDnsEligibility(XNetworkAddressEntry* entry, 
                                             XNetworkAddressEntry_DnsEligibilityStatus status);

// ==================== 地址类型 ====================

/**
 * @brief 判断是否为永久地址。
 * @param entry 实例指针
 * @return true 如果是永久地址
 * @note 永久地址没有过期时间，通常是静态配置的
 */
bool XNetworkAddressEntry_isPermanent(const XNetworkAddressEntry* entry);

/**
 * @brief 判断是否为临时地址。
 * @param entry 实例指针
 * @return true 如果是临时地址
 */
bool XNetworkAddressEntry_isTemporary(const XNetworkAddressEntry* entry);

/**
 * @brief 设置地址是否为永久地址。
 * @param entry 实例指针
 * @param permanent true 表示永久地址
 */
void XNetworkAddressEntry_setPermanent(XNetworkAddressEntry* entry, bool permanent);

// ==================== 比较操作 ====================

/**
 * @brief 判断两个地址条目是否相等。
 * @param entry1 第一个地址条目
 * @param entry2 第二个地址条目
 * @return true 如果相等
 */
bool XNetworkAddressEntry_equals(const XNetworkAddressEntry* entry1, 
                                  const XNetworkAddressEntry* entry2);

/**
 * @brief 判断两个地址条目是否不相等。
 * @param entry1 第一个地址条目
 * @param entry2 第二个地址条目
 * @return true 如果不相等
 */
bool XNetworkAddressEntry_notEquals(const XNetworkAddressEntry* entry1, 
                                     const XNetworkAddressEntry* entry2);

// ==================== 交换操作 ====================

/**
 * @brief 交换两个地址条目的内容。
 * @param entry1 第一个地址条目
 * @param entry2 第二个地址条目
 * @note 此操作非常快且不会失败
 */
void XNetworkAddressEntry_swap(XNetworkAddressEntry* entry1, XNetworkAddressEntry* entry2);

#endif // XNETWORK_INTERFACE_ON
#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XNetworkAddressEntry_create
#define XNetworkAddressEntry_create() XNetworkAddressEntry_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XNETWORKADDRESSENTRY_H
