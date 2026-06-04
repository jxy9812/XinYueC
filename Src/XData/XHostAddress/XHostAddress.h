// XHostAddress.h
#ifndef XHOSTADDRESS_H
#define XHOSTADDRESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XClass.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 网络层协议类型。
 *
 * 描述地址所属的 IP 协议版本。
 */
typedef enum XHostAddress_NetworkLayerProtocol {
    XHostAddress_UnknownNetworkLayerProtocol = -1, ///< 未知或 null 地址
    XHostAddress_IPv4Protocol = 0,                 ///< IPv4 地址
    XHostAddress_IPv6Protocol = 1,                 ///< IPv6 地址
    XHostAddress_AnyIPProtocol = 2                 ///< 任意 IP（用于 socket bind）
} XHostAddress_NetworkLayerProtocol;

/**
 * @brief 特殊地址构造枚举。
 *
 * 用于快速创建预定义地址（如 localhost、any 等）。
 */
typedef enum XHostAddress_SpecialAddress {
    XHostAddress_NullSpecial,           ///< 空地址（等价于默认构造）
    XHostAddress_AnySpecial,            ///< IPv4 通配地址（0.0.0.0）
    XHostAddress_AnyIPv4Special,        ///< 显式 IPv4 通配
    XHostAddress_AnyIPv6Special,        ///< IPv6 通配地址（::）
    XHostAddress_LocalHostSpecial,      ///< IPv4 回环（127.0.0.1）
    XHostAddress_LocalHostIPv6Special,  ///< IPv6 回环（::1）
    XHostAddress_BroadcastSpecial,      ///< IPv4 广播（255.255.255.255）
    XHostAddress_AnyAllSpecial          ///< 任意地址（Qt 中用于 QAbstractSocket::bind）
} XHostAddress_SpecialAddress;

// ==================== 虚函数表定义 ====================
XCLASS_DEFINE_BEGING(XHostAddress)
XCLASS_DEFINE_EXTEND_END(XHostAddress, XClass)
/**
 * @brief 表示 IPv4 或 IPv6 地址。
 *
 * XHostAddress 是一个值语义类，支持：
 * - 从字符串、整数、字节数组构造
 * - 特殊地址（Any, LocalHost, Broadcast）
 * - IPv6 scope ID（zone index）
 * - 地址分类（loopback, multicast, global 等）
 * - 子网判断
 *
 * @note 不支持 DNS 名称解析（与 Qt 一致）。
 * @note 内部统一使用 IPv6 格式存储 IPv4 地址（::ffff:a.b.c.d）。
 * @note 所有数据成员公开可见，无 PIMPL，全平台二进制兼容。
 */
typedef struct XHostAddress 
{
    XClass m_class;                     ///< 基类虚表指针
    bool isNull;                        ///< 是否为 null 地址（protocol == Unknown）
    XHostAddress_NetworkLayerProtocol protocol; ///< 协议类型
    uint8_t a6[16];                     ///< 统一以 IPv6 格式存储地址
    char scopeId[64];                   ///< IPv6 zone ID（最大 63 字符）
} XHostAddress;

XVtable* XHostAddress_class_init(void);
void XHostAddress_init(XHostAddress* addr);
// ==================== 构造与析构 ====================

/**
 * @brief 创建 null 地址。
 * @return 新分配的 XHostAddress 实例，需调用 XHostAddress_delete() 释放。
 */
XHostAddress* XHostAddress_create(void);
/**
 * @brief 拷贝构造。
 * @param other 源地址
 * @return 新分配的副本。
 */
XHostAddress* XHostAddress_create_copy(const XHostAddress* other);

/**
 * @brief 从字符串构造（仅接受 IP 字面量，不解析主机名）。
 * @param address IPv4 或 IPv6 字符串（如 "192.168.1.1" 或 "::1"）
 * @return 新实例，若无效则返回 null 地址。
 */
XHostAddress* XHostAddress_create_fromString(const char* address);

/**
 * @brief 从 IPv4 地址构造（主机字节序）。
 * @param ip 32 位 IPv4 地址（如 0x7F000001 表示 127.0.0.1）
 * @return 新实例。
 */
XHostAddress* XHostAddress_create_fromIPv4Address(uint32_t ip);

/**
 * @brief 从 IPv6 地址构造（16 字节数组）。
 * @param ip 指向 16 字节 IPv6 地址的指针（网络字节序无关，直接复制）
 * @return 新实例。
 */
XHostAddress* XHostAddress_create_fromIPv6Address(const uint8_t ip[16]);

/**
 * @brief 从特殊地址类型构造。
 * @param special 特殊地址枚举值
 * @return 新实例。
 */
XHostAddress* XHostAddress_create_fromSpecial(XHostAddress_SpecialAddress special);

#define XHostAddress_delete_base    XClass_delete_base
#define XHostAddress_deinit_base    XClass_deinit_base
#define XHostAddress_copy_base      XClass_copy_base
#define XHostAddress_move_base      XClass_move_base

// ==================== 赋值 ====================

/**
 * @brief 从字符串设置地址。
 * @param addr 目标地址
 * @param address IP 字符串
 */
void XHostAddress_setAddress(XHostAddress* addr, const char* address);

/**
 * @brief 从 IPv4 设置（主机字节序）。
 * @param addr 目标地址
 * @param ip 32 位 IPv4 地址
 */
void XHostAddress_setAddressIPv4(XHostAddress* addr, uint32_t ip);

/**
 * @brief 从 IPv6 设置。
 * @param addr 目标地址
 * @param ip 16 字节 IPv6 地址
 */
void XHostAddress_setAddressIPv6(XHostAddress* addr, const uint8_t ip[16]);

/**
 * @brief 设置为特殊地址。
 * @param addr 目标地址
 * @param special 特殊地址类型
 */
void XHostAddress_setAddressSpecial(XHostAddress* addr, XHostAddress_SpecialAddress special);

/**
 * @brief 设置 IPv6 scope ID（如 "eth0"）。
 * @param addr 目标地址（必须为 IPv6）
 * @param id zone ID 字符串（会被复制，最长 63 字符）
 */
void XHostAddress_setScopeId(XHostAddress* addr, const char* id);

// ==================== 查询 ====================

/**
 * @brief 判断是否为 null 地址。
 * @param addr 地址
 * @return true 若地址未初始化或为 Unknown 协议
 */
bool XHostAddress_isNull(const XHostAddress* addr);

/**
 * @brief 获取协议类型。
 * @param addr 地址
 * @return 协议枚举值
 */
XHostAddress_NetworkLayerProtocol XHostAddress_protocol(const XHostAddress* addr);

/**
 * @brief 转为 IPv4（主机字节序），失败返回 0。
 * @param addr 地址
 * @return 32 位 IPv4 地址，若非 IPv4 则返回 0
 */
uint32_t XHostAddress_toIPv4Address(const XHostAddress* addr);

/**
 * @brief 转为 IPv6（输出到 out[16]）。
 * @param addr 地址
 * @param out 输出缓冲区（16 字节）
 */
void XHostAddress_toIPv6Address(const XHostAddress* addr, uint8_t out[16]);

/**
 * @brief 获取 scope ID（若存在）。
 * @param addr 地址
 * @return scope ID 字符串（若无则为空字符串）
 */
const char* XHostAddress_scopeId(const XHostAddress* addr);

/**
 * @brief 转为字符串表示（malloc，需 free）。
 * @param addr 地址
 * @return 动态分配的字符串（如 "192.168.1.1" 或 "::1%eth0"），失败返回 NULL
 */
XString* XHostAddress_toString(const XHostAddress* addr);

/**
 * @brief 是否为回环地址。
 * @param addr 地址
 * @return true 若为 127.0.0.0/8 或 ::1
 */
bool XHostAddress_isLoopback(const XHostAddress* addr);

/**
 * @brief 是否为多播地址。
 * @param addr 地址
 * @return true 若为 224.0.0.0/4 或 ff00::/8
 */
bool XHostAddress_isMulticast(const XHostAddress* addr);

/**
 * @brief 是否为全局可路由地址。
 * @param addr 地址
 * @return true 若非私有、非回环、非链路本地等
 */
bool XHostAddress_isGlobal(const XHostAddress* addr);

/**
 * @brief 是否为链路本地地址。
 * @param addr 地址
 * @return true 若为 169.254.0.0/16 或 fe80::/10
 */
bool XHostAddress_isLinkLocal(const XHostAddress* addr);

/**
 * @brief 是否为站点本地地址（已废弃）。
 * @param addr 地址
 * @return true 若为 fec0::/10（仅 IPv6）
 */
bool XHostAddress_isSiteLocal(const XHostAddress* addr);

/**
 * @brief 是否为唯一本地地址（ULA, fc00::/7）。
 * @param addr 地址
 * @return true 若为 fc00::/7（仅 IPv6）
 */
bool XHostAddress_isUniqueLocal(const XHostAddress* addr);

/**
 * @brief 是否在子网中。
 * @param addr 待检测地址
 * @param subnet 子网基地址
 * @param prefixLength 前缀长度（如 24 表示 /24）
 * @return true 若 addr 属于 subnet/prefixLength
 */
bool XHostAddress_isInSubnet(const XHostAddress* addr, const XHostAddress* subnet, int prefixLength);

/**
 * @brief 解析子网字符串（如 "192.168.1.0/24"）。
 * @param subnet 子网字符串
 * @param host 输出子网地址
 * @param prefixLen 输出前缀长度
 * @return true 若解析成功
 */
bool XHostAddress_parseSubnet(const char* subnet, XHostAddress* host, int* prefixLen);

// ==================== 比较 ====================

/**
 * @brief 相等比较。
 * @param a 地址 A
 * @param b 地址 B
 * @return true 若协议、地址、scopeId 完全相同
 */
bool XHostAddress_operator_equal(const XHostAddress* a, const XHostAddress* b);

/**
 * @brief 排序比较（<0, ==0, >0）。
 * @param a 地址 A
 * @param b 地址 B
 * @return 比较结果（按协议 -> 地址 -> scopeId 字典序）
 */
int XHostAddress_operator_compare(const XHostAddress* a, const XHostAddress* b);

// ==================== 静态常量 ====================

/** @brief 空地址（协议 Unknown） */
extern const XHostAddress XHostAddress_Null;
/** @brief IPv4 通配地址（0.0.0.0） */
extern const XHostAddress XHostAddress_Any;
/** @brief 显式 IPv4 通配 */
extern const XHostAddress XHostAddress_AnyIPv4;
/** @brief IPv6 通配地址（::） */
extern const XHostAddress XHostAddress_AnyIPv6;
/** @brief IPv4 回环（127.0.0.1） */
extern const XHostAddress XHostAddress_LocalHost;
/** @brief IPv6 回环（::1） */
extern const XHostAddress XHostAddress_LocalHostIPv6;
/** @brief IPv4 广播（255.255.255.255） */
extern const XHostAddress XHostAddress_Broadcast;
/** @brief 任意地址（Qt 用于 bind） */
extern const XHostAddress XHostAddress_AnyAll;

// ==================== 辅助函数 ====================

/**
 * @brief 判断字符串是否为合法 IPv4 地址。
 * @param address 字符串
 * @return true 若格式合法（如 "192.168.1.1"）
 */
bool XHostAddress_isIPv4Address(const char* address);

/**
 * @brief 判断字符串是否为合法 IPv6 地址。
 * @param address 字符串（支持 "::1", "fe80::1%eth0" 等）
 * @return true 若格式合法
 */
bool XHostAddress_isIPv6Address(const char* address);

#ifdef __cplusplus
}
#endif
#endif // XHOSTADDRESS_H