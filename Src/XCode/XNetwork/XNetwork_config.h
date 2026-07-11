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

#ifdef __cplusplus
extern "C" {
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

/* 取消注释以启用对应模式 */

//#define XNETWORK_USE_PLATFORM_API     /* 平台API模式 */
#define XNETWORK_USE_LWIP           /* lwIP模式 */

/* ========================================================================== */
/*                        lwIP模式配置                                         */
/* ========================================================================== */

#if defined(XNETWORK_USE_LWIP)

/**
 * @brief lwIP 最大套接字数
 */
#ifndef XNETWORK_LWIP_MAX_SOCKETS
#define XNETWORK_LWIP_MAX_SOCKETS     8
#endif

/**
 * @brief lwIP 接收缓冲区大小
 */
#ifndef XNETWORK_LWIP_RECV_BUFFER_SIZE
#define XNETWORK_LWIP_RECV_BUFFER_SIZE  8192
#endif

/**
 * @brief lwIP 发送缓冲区大小
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
#ifndef XNETWORK_LWIP_NO_SYS
#define XNETWORK_LWIP_NO_SYS  1
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

#ifdef __cplusplus
}
#endif

#endif /* XNETWORK_CONFIG_H */