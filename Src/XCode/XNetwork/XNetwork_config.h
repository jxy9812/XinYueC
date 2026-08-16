/**
 * @file XNetwork_config.h
 * @brief XNetwork 模块网络后端配置文件
 *
 * 通过此配置文件可以选择网络的底层实现：
 *   1. XNETWORK_USE_PLATFORM_API - 平台API模式（Windows IOCP / Linux epoll）
 *   2. XNETWORK_USE_LWIP       - lwIP 协议栈模式（嵌入式设备）
 *
 * 优先级（从高到低）：
 *   XNETWORK_USE_PLATFORM_API > XNETWORK_USE_LWIP
 */

#ifndef XNETWORK_CONFIG_H
#define XNETWORK_CONFIG_H

/* 引入平台自动检测宏（XPLATFORM_* 系列），
 * 保证 lwIP 源码编译时 XPLATFORM_LWIP_NO_SYS_DEFAULT 可用 */
#include "CXinYueConfig.h"

#ifdef __cplusplus
extern "C" {
#endif
/* ========================================================================== */
/*                        模块总开关                                        */
/* ========================================================================== */
/** @brief XNetwork 模块总开关；置 0 时裁剪整个 XNetwork 对外公开 API 及所有子功能。
 * @note 总开关在 CXinYueConfig.h 中统一定义，此处仅兜底默认值。
 *       关闭后若仍有其它模块无条件引用 XNetwork 符号，需同步裁剪对应依赖。 */
#ifndef XNETWORK_ON
#define XNETWORK_ON 1
#endif

#if XNETWORK_ON

/** @brief 抽象套接字模块开关；关闭后 XAbstractSocket / XTcpSocket 相关 API 不编译。 */
#ifndef XNETWORK_ABSTRACT_SOCKET_ON
#define XNETWORK_ABSTRACT_SOCKET_ON 1
#endif

/** @brief TCP 服务器模块开关；关闭后 XTcpServer 相关 API 不编译。 */
#ifndef XNETWORK_TCPSERVER_ON
#define XNETWORK_TCPSERVER_ON 1
#endif

/** @brief UDP 套接字模块开关；关闭后 XUdpSocket 相关 API 不编译。 */
#ifndef XNETWORK_UDPSOCKET_ON
#define XNETWORK_UDPSOCKET_ON 1
#endif

/** @brief 数据报模块开关；关闭后 XNetworkDatagram 相关 API 不编译。 */
#ifndef XNETWORK_DATAGRAM_ON
#define XNETWORK_DATAGRAM_ON 1
#endif

/** @brief 主机信息模块开关；关闭后 XHostInfo 相关 DNS API 不编译。 */
#ifndef XNETWORK_HOSTINFO_ON
#define XNETWORK_HOSTINFO_ON 1
#endif

/** @brief 网络接口模块开关；关闭后 XNetworkInterface 相关 API 不编译。 */
#ifndef XNETWORK_INTERFACE_ON
#define XNETWORK_INTERFACE_ON 1
#endif

/** @brief 代理模块开关；关闭后 XNetworkProxy SOCKS5/HTTP 相关 API 不编译。 */
#ifndef XNETWORK_PROXY_ON
#define XNETWORK_PROXY_ON 1
#endif

/** @brief SSL 模块开关；关闭后 XSslSocket TLS/SSL 相关 API 不编译。 */
#ifndef XNETWORK_SSL_ON
#define XNETWORK_SSL_ON 1
#endif

/* ========================================================================== */
/*                          模式选择                                           */
/* ========================================================================== */

/**
 * @brief 网络后端模式选择
 *
 * 两种模式只能启用一种，优先级：PLATFORM_API > LWIP
 *
 * XNETWORK_USE_PLATFORM_API - 平台API模式
 *                             优点：完整支持所有API，性能最优
 *                             缺点：依赖操作系统
 *                             适用：Windows/Linux/macOS桌面应用
 *
 * XNETWORK_USE_LWIP         - lwIP协议栈模式
 *                             优点：跨平台，无操作系统依赖，适合嵌入式
 *                             缺点：受lwIP功能限制
 *                             适用：STM32等嵌入式设备
 */

/* 未由构建系统或产品配置指定时，桌面构建默认使用平台 API。
 * 保留外部覆盖能力，避免嵌入式构建同时定义两个互斥后端。 */
#if !defined(XNETWORK_USE_PLATFORM_API) && !defined(XNETWORK_USE_LWIP)
#define XNETWORK_USE_PLATFORM_API
#endif

/* ========================================================================== */
/*                        lwIP模式配置                                         */
/* ========================================================================== */

/* lwIP is compiled as a static library for every backend. Keep these
 * compile-time defaults available even when the selected runtime backend is
 * the platform API; only lwIP consumes them. */
#ifndef XNETWORK_LWIP_RECV_BUFFER_SIZE
#define XNETWORK_LWIP_RECV_BUFFER_SIZE  8192
#endif

#ifndef XNETWORK_LWIP_SEND_BUFFER_SIZE
#define XNETWORK_LWIP_SEND_BUFFER_SIZE  8192
#endif

#if defined(XNETWORK_USE_LWIP)

/**
 * @brief lwIP 最大套接字数
 */
#ifndef XNETWORK_LWIP_MAX_SOCKETS
#define XNETWORK_LWIP_MAX_SOCKETS     8
#endif

/**
 * @brief lwIP 每个活跃接收 socket 的缓冲预算（字节）
 *
 * 同时作为 TCP 接收窗口。缓冲首次收到数据时才分配，空闲 socket 不占用
 * 该预算。范围为 TCP_MSS（当前 1460）到 65535；小内存建议 1460/2920，
 * 大内存或高吞吐场景建议 16384/32768。
 */
#ifndef XNETWORK_LWIP_RECV_BUFFER_SIZE
#define XNETWORK_LWIP_RECV_BUFFER_SIZE  8192
#endif

/**
 * @brief lwIP 每个 TCP socket 的发送缓冲预算（字节）
 *
 * 范围为 2 * TCP_MSS（当前 2920）到 65535。值越大，允许同时等待确认的
 * 数据越多，吞吐更高，但动态内存峰值也越大。XFtp 会按该预算调整上传
 * 分块；小预算减少栈和 TLS 临时队列，大预算扩大批量以减少事件开销。
 */
#ifndef XNETWORK_LWIP_SEND_BUFFER_SIZE
#define XNETWORK_LWIP_SEND_BUFFER_SIZE  8192
#endif

/**
 * @brief lwIP TCP 服务器最大监听数
 */
#ifndef XNETWORK_LWIP_MAX_LISTEN_BACKLOG
#define XNETWORK_LWIP_MAX_LISTEN_BACKLOG  4
#endif

/**
 * @brief lwIP DNS 查询超时时间（毫秒）
 */
#ifndef XNETWORK_LWIP_DNS_TIMEOUT_MS
#define XNETWORK_LWIP_DNS_TIMEOUT_MS  5000
#endif

#ifndef XNETWORK_LWIP_TICK_MS
#define XNETWORK_LWIP_TICK_MS  1
#endif
/**
 * @brief lwIP 操作系统模式选择
 *
 * 两种模式：
 *   0 = OS 模式（NO_SYS=0）：使用 XMutex/XSemaphore/XThread 完整 sys_arch，
 *        lwIP 内部可创建线程/信号量/邮箱，支持 LWIP_TCPIP_CORE_LOCKING。
 *        适用：Windows/Linux 桌面、FreeRTOS 等带 OS 的环境。
 *
 *   1 = 裸机模式（NO_SYS=1）：最小 sys_arch，仅保留 sys_arch_protect/sys_now
 *        等必要接口，lwIP 内部不创建线程/信号量/邮箱，所有处理由主循环轮询。
 *        适用：STM32 裸机、无 OS 的嵌入式环境。
 *
 * @note 两种模式都使用 sys_arch_protect（递归互斥锁）保证线程安全，
 *       都通过 XTimeWheelGroup 定时器轮询 sys_check_timeouts()。
 */
/**
 * @brief lwIP IPv6 支持
 *
 * 0 = 关闭 IPv6（默认），仅 IPv4
 * 1 = 开启 IPv6 双栈支持
 */
#ifndef XNETWORK_LWIP_IPV6
#define XNETWORK_LWIP_IPV6  1
#endif

/**
 * @brief lwIP IGMP 多播支持
 *
 * 0 = 关闭 IGMP（默认）
 * 1 = 开启 IGMP 多播组管理
 */
#ifndef XNETWORK_LWIP_IGMP
#define XNETWORK_LWIP_IGMP  1
#endif
#ifndef XNETWORK_LWIP_NO_SYS
/* 对接 CXinYueConfig.h 的平台自动检测：
 *   裸机环境 -> NO_SYS=1（最小 sys_arch，无 tcpip_thread）
 *   有 OS 环境 -> NO_SYS=0（完整 sys_arch，lwIP 内部创建 tcpip_thread）
 * 用户可在此处上方显式 #define XNETWORK_LWIP_NO_SYS 覆盖默认值 */
#define XNETWORK_LWIP_NO_SYS  XPLATFORM_LWIP_NO_SYS_DEFAULT
#endif

#endif /* XNETWORK_USE_LWIP */

/* ========================================================================== */
/*                       平台API模式配置                                       */
/* ========================================================================== */

#if defined(XNETWORK_USE_PLATFORM_API)

/**
 * @brief 读取缓冲区大小
 */
#ifndef XNETWORK_PLATFORM_READ_BUFFER_SIZE
#define XNETWORK_PLATFORM_READ_BUFFER_SIZE   8192
#endif

/**
 * @brief 写入缓冲区大小
 */
#ifndef XNETWORK_PLATFORM_WRITE_BUFFER_SIZE
#define XNETWORK_PLATFORM_WRITE_BUFFER_SIZE  8192
#endif

#endif /* XNETWORK_USE_PLATFORM_API */

/* ========================================================================== */
/*                        自动模式检测                                         */
/* ========================================================================== */

/**
 * @brief 自动选择默认模式
 *
 * 如果用户未指定任何模式，根据平台自动选择：
 *   - Windows/Linux/macOS -> XNETWORK_USE_PLATFORM_API
 *   - 其他平台            -> XNETWORK_USE_LWIP
 */
#if !defined(XNETWORK_USE_PLATFORM_API) && !defined(XNETWORK_USE_LWIP)

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define XNETWORK_USE_PLATFORM_API
#else
#define XNETWORK_USE_LWIP
#endif

#endif

/* ========================================================================== */
/*                        模式验证                                             */
/* ========================================================================== */

/* 检查是否同时启用了多个模式 */
#if defined(XNETWORK_USE_PLATFORM_API) && defined(XNETWORK_USE_LWIP)
#error "XNetwork: 不能同时启用多个网络后端，请只选择一种模式"
#endif

/* 检查是否未启用任何模式 */
#if !defined(XNETWORK_USE_PLATFORM_API) && !defined(XNETWORK_USE_LWIP)
#error "XNetwork: 必须启用至少一个网络后端模式"
#endif

/* ========================================================================== */
/*                        API可用性宏定义                                       */
/* ========================================================================== */

/**
 * @brief API功能可用性宏
 *
 * 用于编译时判断特定API是否可用，便于条件编译
 */

/* 平台初始化 - 两种模式均支持 */
#define XNETWORK_API_ENSURE_INIT       1
#define XNETWORK_API_CLEANUP           1
#define XNETWORK_API_LAST_ERROR        1
#define XNETWORK_API_IS_EAGAIN         1
#define XNETWORK_API_ERROR_STRING      1

/* 私有数据 - 两种模式均支持 */
#define XNETWORK_API_CREATE_PRIVATE    1
#define XNETWORK_API_DELETE_PRIVATE    1
#define XNETWORK_API_SOCKET_FD         1
#define XNETWORK_API_IS_CONNECTED      1

/* 核心操作 - 两种模式均支持 */
#define XNETWORK_API_BIND              1
#define XNETWORK_API_CONNECT           1
#define XNETWORK_API_DISCONNECT        1
#define XNETWORK_API_READ              1
#define XNETWORK_API_WRITE             1
#define XNETWORK_API_HANDLE_EVENT      1
#define XNETWORK_API_SET_DESCRIPTOR    1
#define XNETWORK_API_SET_OPTION        1
#define XNETWORK_API_GET_OPTION        1
#define XNETWORK_API_SET_READ_BUFFER   1

/* 异步读取状态 - 两种模式均支持 */
#define XNETWORK_API_READ_BUFFER       1
#define XNETWORK_API_READ_FINISHED     1
#define XNETWORK_API_WRITE_FINISHED    1
#define XNETWORK_API_CONTINUE_READ     1

/* TCP 服务器 - 两种模式均支持 */
#define XNETWORK_API_SERVER_CREATE     1
#define XNETWORK_API_SERVER_PORT       1
#define XNETWORK_API_SERVER_CLOSE      1
#define XNETWORK_API_GET_ACCEPTED      1
#define XNETWORK_API_CONTINUE_ACCEPT   1

/* DNS 查询 - 两种模式均支持 */
#define XNETWORK_API_LOOKUP_NAME       1
#define XNETWORK_API_LOCAL_HOSTNAME    1

/* ICMP Echo - 平台套接字后端和 lwIP Raw API 均提供 IPv4 能力。 */
#define XNETWORK_API_ICMP_ECHO         1

/* 网络接口枚举 - 仅平台API模式完整支持 */
#if defined(XNETWORK_USE_PLATFORM_API)
#define XNETWORK_API_ENUM_INTERFACES   1
#define XNETWORK_API_GET_IF_ADDRESSES  1
#elif defined(XNETWORK_USE_LWIP)
#define XNETWORK_API_ENUM_INTERFACES   1  /* lwIP: 通过 netif 枚举 */
#define XNETWORK_API_GET_IF_ADDRESSES  1
#endif

/* 多播 - 两种模式均支持 */
#define XNETWORK_API_MULTICAST_GROUP   1
#define XNETWORK_API_MULTICAST_OP      1

/* UDP 特有 - 两种模式均支持 */
#define XNETWORK_API_LAST_SENDER       1
#define XNETWORK_API_SEND_DATAGRAM     1

/* 系统代理与GSSAPI - 仅平台API模式支持 */
#if defined(XNETWORK_USE_PLATFORM_API)
#define XNETWORK_API_SYSTEM_PROXY      1
#define XNETWORK_API_GSSAPI_AUTH       1
#else
#define XNETWORK_API_SYSTEM_PROXY      0
#define XNETWORK_API_GSSAPI_AUTH       0
#endif

#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif

#endif /* XNETWORK_CONFIG_H */
