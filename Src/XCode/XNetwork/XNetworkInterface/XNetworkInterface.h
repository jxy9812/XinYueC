// XNetworkInterface.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言模拟 Qt6 QNetworkInterface。
// 提供主机网络接口的枚举和信息查询功能。

#ifndef XNETWORKINTERFACE_H
#define XNETWORKINTERFACE_H
#include "XNetwork_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XNETWORK_ON
#if XNETWORK_INTERFACE_ON

#include "XClass.h"
#include "XHostAddress.h"
#include "XNetworkAddressEntry.h"
#include "XString.h"
#include "XVector.h"
#include <stdbool.h>
#include <stdint.h>

typedef void* XDeviceNetworkInterfaceIterator;

/* 网络接口枚举器（前向声明） */
struct XNetworkInterface;
XDeviceNetworkInterfaceIterator XDeviceNetwork_enumInterfacesBegin(void);
struct XNetworkInterface* XDeviceNetwork_enumInterfacesNext(XDeviceNetworkInterfaceIterator iter);
void XDeviceNetwork_enumInterfacesEnd(XDeviceNetworkInterfaceIterator iter);


// =============== 枚举定义 ===============

/**
 * @brief 网络接口标志。
 *
 * 描述网络接口的状态和能力。
 */
typedef enum XNetworkInterface_InterfaceFlag {
    XNetworkInterface_IsUp          = 0x01,  ///< 接口已启用（管理状态）
    XNetworkInterface_IsRunning     = 0x02,  ///< 接口正在运行（物理连接）
    XNetworkInterface_CanBroadcast  = 0x04,  ///< 接口支持广播
    XNetworkInterface_IsLoopBack    = 0x08,  ///< 接口是回环接口
    XNetworkInterface_IsPointToPoint = 0x10, ///< 接口是点对点接口
    XNetworkInterface_CanMulticast  = 0x20   ///< 接口支持多播
} XNetworkInterface_InterfaceFlag;

/**
 * @brief 网络接口标志位掩码类型。
 */
typedef int XNetworkInterface_InterfaceFlags;

/**
 * @brief 网络接口硬件类型。
 *
 * 描述网络接口的物理层类型。
 */
typedef enum XNetworkInterface_InterfaceType {
    XNetworkInterface_Unknown      = 0,   ///< 未知类型
    XNetworkInterface_Loopback     = 1,   ///< 回环接口
    XNetworkInterface_Virtual      = 2,   ///< 虚拟接口（如隧道）
    XNetworkInterface_Ethernet     = 3,   ///< 以太网接口
    XNetworkInterface_Slip         = 4,   ///< 串行线路接口
    XNetworkInterface_CanBus       = 5,   ///< CAN 总线接口
    XNetworkInterface_Ppp          = 6,   ///< 点对点协议接口
    XNetworkInterface_Fddi         = 7,   ///< 光纤分布式数据接口
    XNetworkInterface_Wifi         = 8,   ///< IEEE 802.11 Wi-Fi 接口
    XNetworkInterface_Phonet       = 9,   ///< Linux Phonet 接口
    XNetworkInterface_Ieee802154   = 10,  ///< IEEE 802.15.4 个人区域网接口
    XNetworkInterface_SixLoWPAN    = 11,  ///< 6LoWPAN 接口
    XNetworkInterface_Ieee80216    = 12,  ///< IEEE 802.16 WiMAX 接口
    XNetworkInterface_Ieee1394     = 13   ///< IEEE 1394 FireWire 接口
} XNetworkInterface_InterfaceType;

// =============== 类定义 ===============

XCLASS_DEFINE_BEGING(XNetworkInterface)
XCLASS_DEFINE_EXTEND_END(XNetworkInterface, XClass);

/**
 * @brief 网络接口类。
 *
 * XNetworkInterface 表示主机上的一个网络接口。
 * 每个网络接口可能包含零个或多个 IP 地址，
 * 每个地址可选地关联子网掩码和/或广播地址。
 */
typedef struct XNetworkInterface {
    XClass m_class;                         ///< 基类虚表指针
    XString* name;                          ///< 接口名称（如 "eth0"、"en0"、"lo"）
    XString* humanReadableName;             ///< 人类可读名称（Windows 上为 "本地连接" 等）
    XString* hardwareAddress;               ///< 硬件地址（MAC 地址，格式 "00:11:22:33:44:55"）
    int index;                              ///< 接口索引（系统分配的唯一标识符）
    int mtu;                                ///< 最大传输单元（字节）
    XNetworkInterface_InterfaceFlags flags; ///< 接口标志
    XNetworkInterface_InterfaceType type;   ///< 接口硬件类型
    XVector* addressEntries;                ///< IP 地址条目列表（XNetworkAddressEntry）
    bool isValid;                           ///< 是否包含有效信息
} XNetworkInterface;

// ==================== 构造与析构 ====================

/**
 * @brief 创建一个空的 XNetworkInterface 实例。
 * @return 新分配的实例，需调用 XClassDelete() 释放。
 */
XNetworkInterface* XNetworkInterface_create_ex(XMemoryType memory);

/**
 * @brief 拷贝构造函数。
 * @param other 源对象
 * @return 新分配的副本
 */
XNetworkInterface* XNetworkInterface_create_copy(const XNetworkInterface* other);

XNetworkInterface* XNetworkInterface_create_move(const XNetworkInterface* other);
/**
 * @brief 初始化已分配的 XNetworkInterface 结构体。
 * @param iface 指向未初始化的实例
 */
void XNetworkInterface_init(XNetworkInterface* iface);

/**
 * @brief 初始化虚函数表。
 * @return 虚函数表指针
 */
XVtable* XNetworkInterface_class_init(void);


// ==================== 属性访问器 ====================

/**
 * @brief 获取接口名称。
 * @param iface 实例指针
 * @return 接口名称字符串（如 "eth0"、"en0"、"lo"）
 * @note Unix 上为 "eth0"、"lo" 等；Windows 上为内部 ID
 */
XString* XNetworkInterface_name(const XNetworkInterface* iface);
const XString* XNetworkInterface_name_const(const XNetworkInterface* iface);

/**
 * @brief 获取人类可读名称。
 * @param iface 实例指针
 * @return 人类可读名称（Windows 上如 "本地连接"）
 * @note Unix 上通常与 name() 相同
 */
XString* XNetworkInterface_humanReadableName(const XNetworkInterface* iface);
const XString* XNetworkInterface_humanReadableName_const(const XNetworkInterface* iface);

/**
 * @brief 获取硬件地址（MAC 地址）。
 * @param iface 实例指针
 * @return 硬件地址字符串（格式 "00:11:22:33:44:55"）
 * @note 以太网接口返回 MAC 地址，其他类型可能不同
 */
XString* XNetworkInterface_hardwareAddress(const XNetworkInterface* iface);
const XString* XNetworkInterface_hardwareAddress_const(const XNetworkInterface* iface);

/**
 * @brief 获取接口索引。
 * @param iface 实例指针
 * @return 接口索引（系统唯一标识符），未知返回 0
 * @note 此索引与 IPv6 地址的 scope ID 字段匹配
 */
int XNetworkInterface_index(const XNetworkInterface* iface);

/**
 * @brief 获取最大传输单元（MTU）。
 * @param iface 实例指针
 * @return MTU 字节数，未知返回 0
 * @note MTU 是无需分片可发送的最大数据包大小
 */
int XNetworkInterface_maximumTransmissionUnit(const XNetworkInterface* iface);

/**
 * @brief 获取接口标志。
 * @param iface 实例指针
 * @return 接口标志位掩码
 */
XNetworkInterface_InterfaceFlags XNetworkInterface_flags(const XNetworkInterface* iface);

/**
 * @brief 获取接口硬件类型。
 * @param iface 实例指针
 * @return 接口类型枚举值
 */
XNetworkInterface_InterfaceType XNetworkInterface_type(const XNetworkInterface* iface);

/**
 * @brief 获取 IP 地址条目列表。
 * @param iface 实例指针
 * @return XNetworkAddressEntry 指针数组
 * @note 每个条目包含 IP 地址、子网掩码和广播地址
 */
XVector* XNetworkInterface_addressEntries(const XNetworkInterface* iface);

/**
 * @brief 判断接口是否有效。
 * @param iface 实例指针
 * @return true 如果包含有效的网络接口信息
 */
bool XNetworkInterface_isValid(const XNetworkInterface* iface);

// ==================== 静态工具函数 ====================

/**
 * @brief 获取所有活动接口的 IP 地址列表。
 * @return XHostAddress 数组（需调用者释放）
 * @note 等效于遍历所有 IsUp 状态接口的地址
 */
XVector* XNetworkInterface_allAddresses(void);

/**
 * @brief 获取所有网络接口列表。
 * @return XNetworkInterface 数组（需调用者释放数组）
 * @note 失败时返回空列表
 */
XVector* XNetworkInterface_allInterfaces(void);

/**
 * @brief 根据名称获取网络接口。
 * @param name 接口名称（如 "eth0"）或索引字符串（如 "1"）
 * @return 接口对象（需调用者释放），不存在则返回无效接口
 */
XNetworkInterface* XNetworkInterface_interfaceFromName(const XString* name);

/**
 * @brief 根据索引获取网络接口。
 * @param index 接口索引
 * @return 接口对象（需调用者释放），不存在则返回无效接口
 */
XNetworkInterface* XNetworkInterface_interfaceFromIndex(int index);

/**
 * @brief 根据接口名称获取索引。
 * @param name 接口名称
 * @return 接口索引，不存在返回 0
 */
int XNetworkInterface_interfaceIndexFromName(const XString* name);

/**
 * @brief 根据接口索引获取名称。
 * @param index 接口索引
 * @return 接口名称字符串（需调用者释放），不存在返回 NULL
 */
XString* XNetworkInterface_interfaceNameFromIndex(int index);

// ==================== 辅助函数 ====================

/**
 * @brief 判断接口是否已启用。
 * @param iface 实例指针
 * @return true 如果接口已启用
 */
bool XNetworkInterface_isUp(const XNetworkInterface* iface);

/**
 * @brief 判断接口是否正在运行。
 * @param iface 实例指针
 * @return true 如果接口正在运行
 */
bool XNetworkInterface_isRunning(const XNetworkInterface* iface);

/**
 * @brief 判断接口是否支持广播。
 * @param iface 实例指针
 * @return true 如果接口支持广播
 */
bool XNetworkInterface_canBroadcast(const XNetworkInterface* iface);

/**
 * @brief 判断接口是否为回环接口。
 * @param iface 实例指针
 * @return true 如果是回环接口
 */
bool XNetworkInterface_isLoopBack(const XNetworkInterface* iface);

/**
 * @brief 判断接口是否为点对点接口。
 * @param iface 实例指针
 * @return true 如果是点对点接口
 */
bool XNetworkInterface_isPointToPoint(const XNetworkInterface* iface);

/**
 * @brief 判断接口是否支持多播。
 * @param iface 实例指针
 * @return true 如果接口支持多播
 */
bool XNetworkInterface_canMulticast(const XNetworkInterface* iface);

// ==================== 网卡配置（DHCP / 静态 IP） ====================
// 对标「系统网络设置」场景：按接口索引查询与切换 DHCP/静态配置。
// 能力来自平台后端（见 XDeviceNetwork.h 网卡配置节）：Windows 经
// IP Helper + netsh（需管理员权限）；其余平台当前不支持，下面的
// configSupported 恒 false、查询/设置返回 false。
// 地址字段复用值语义的 XHostAddress（isNull()=无此项），连接名为 XString。

XCLASS_DEFINE_BEGING(XNetworkInterfaceConfig)
XCLASS_DEFINE_EXTEND_END(XNetworkInterfaceConfig, XClass)

/**
 * @brief 网卡 IPv4 配置快照。
 *
 * 查询结果对象：地址为值语义 XHostAddress（isNull()=无该项），
 * 连接名为 XString*（无该项为 NULL）。创建/析构走 XClass 体系
 * （XNetworkInterfaceConfig_create / XClassDelete）。
 */
typedef struct XNetworkInterfaceConfig {
    XClass m_class;             ///< 基类（必须位于第一成员）
    XString* m_friendlyName;    ///< 连接名（如 "以太网"；netsh name= 口径）；NULL=无
    XHostAddress m_ipv4Address; ///< 首个 IPv4 单播地址；isNull()=无
    XHostAddress m_ipv4Netmask; ///< 该地址的子网掩码；isNull()=无
    XHostAddress m_ipv4Gateway; ///< 首个 IPv4 默认网关；isNull()=无
    XHostAddress m_ipv6Address; ///< 首个 IPv6 单播地址（预填用）；isNull()=无
    XHostAddress m_ipv6Gateway; ///< 首个 IPv6 默认网关；isNull()=无
    XHostAddress m_dhcpServer;  ///< DHCP 服务器地址；isNull()=无
    XHostAddress m_dnsPrimary;  ///< 首选 DNS；isNull()=无
    XHostAddress m_dnsSecondary;///< 备用 DNS；isNull()=无
    int m_ifIndex;              ///< 接口索引（查询入参回显）
    int m_ipv6PrefixLength;     ///< m_ipv6Address 的前缀长度（0..128）；无 IPv6 为 0
    bool m_dhcpEnabled;         ///< true=DHCP 自动获取，false=静态配置
    bool m_operUp;              ///< 接口管理状态 Up
} XNetworkInterfaceConfig;

/** @brief 初始化已分配的配置快照对象（地址置 null、连接名置 NULL）。 */
void XNetworkInterfaceConfig_init(XNetworkInterfaceConfig* config);

/** @brief 创建配置快照对象；调用方 XClassDelete 释放。 */
XNetworkInterfaceConfig* XNetworkInterfaceConfig_create_ex(XMemoryType memory);

/** @brief 初始化虚函数表（deinit/copy/move 全套值语义）。 */
XVtable* XNetworkInterfaceConfig_class_init(void);

/**
 * @brief 查询平台是否具备网卡配置能力。
 * @return 具备返回 true（当前仅 Windows 后端）；其余平台 false。
 */
bool XNetworkInterface_configSupported(void);

/**
 * @brief 按接口索引查询当前 IPv4 配置快照（填充调用方创建的对象）。
 * @param ifIndex 接口索引（与 XNetworkInterface_index 同源）
 * @param outConfig 输出对象；须已经 init/create，不能为 NULL。
 *                  成功时整对象被新结果覆盖；失败时内容不变。
 * @return 成功返回 true；索引不存在或平台不支持返回 false
 */
bool XNetworkInterface_queryConfig(int ifIndex,
                                   XNetworkInterfaceConfig* outConfig);

/**
 * @brief 将网卡切回 DHCP 自动获取（地址与 DNS 一并交还自动管理）。
 * @param ifIndex 接口索引
 * @return 平台受理返回 true；不支持/找不到接口/权限不足返回 false
 */
bool XNetworkInterface_setDhcpMode(int ifIndex);

/**
 * @brief 将网卡切换为静态 IPv4 配置并应用。
 * @param ifIndex 接口索引
 * @param ipv4Address 静态 IP（点分十进制字面量），非空
 * @param ipv4Netmask 子网掩码（点分十进制字面量），非空
 * @param ipv4Gateway 默认网关；NULL/空=不设网关
 * @param dnsPrimary 首选 DNS；NULL/空=清空 DNS
 * @param dnsSecondary 备用 DNS；仅 dnsPrimary 非空时生效
 * @return 平台受理返回 true；参数非法/不支持/权限不足返回 false
 * @note 应用会短暂中断该网卡连接；Windows 需以管理员身份运行。
 */
bool XNetworkInterface_setStaticMode(int ifIndex, const char* ipv4Address,
                                     const char* ipv4Netmask,
                                     const char* ipv4Gateway,
                                     const char* dnsPrimary,
                                     const char* dnsSecondary);

/**
 * @brief 为网卡追加一个静态 IPv6 地址（可带网关）。
 * @param ifIndex 接口索引
 * @param ipv6Address IPv6 字面量（不含 %zone），非空
 * @param prefixLength 前缀长度（0..128）
 * @param ipv6Gateway IPv6 默认网关；NULL/空=不设（lwIP 后端由 RA 管理，
 *                    该参数被忽略）
 * @return 平台受理返回 true；参数非法/不支持/权限不足返回 false
 * @note Windows=netsh interface ipv6（需管理员权限）；不影响既有
 *       SLAAC/临时地址；地址已存在时按失败上报。
 */
bool XNetworkInterface_setStaticIpv6(int ifIndex, const char* ipv6Address,
                                     int prefixLength,
                                     const char* ipv6Gateway);

// ==================== 交换操作 ====================

/**
 * @brief 交换两个网络接口的内容。
 * @param iface1 第一个网络接口
 * @param iface2 第二个网络接口
 * @note 此操作非常快且不会失败
 */
void XNetworkInterface_swap(XNetworkInterface* iface1, XNetworkInterface* iface2);

#endif // XNETWORK_INTERFACE_ON
#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XNetworkInterface_create
#define XNetworkInterface_create() XNetworkInterface_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XNetworkInterfaceConfig_create
#define XNetworkInterfaceConfig_create() XNetworkInterfaceConfig_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XNETWORKINTERFACE_H
