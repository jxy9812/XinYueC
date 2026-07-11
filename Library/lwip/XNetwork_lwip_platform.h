/**
 * @file XNetwork_lwip_platform.h
 * @brief lwIP 平台网络实现 - 平台相关额外声明
 *
 * 本文件声明 XNetwork_lwip.c 需要由各平台提供的扩展接口，
 * XNetwork_platform.h 中已声明的标准接口不在此重复。
 *
 * 架构分层：
 *   XNetwork_platform.h                              -> 统一平台抽象接口
 *   Library/lwip/XNetwork_lwip.c                     -> lwIP 适配层（平台无关）
 *        +-- Drive/windows/XNetwork/XNetwork_lwip_win32.c   -> Windows Npcap 虚拟网卡实现
 *        +-- Drive/Posix/XNetwork_lwip_linux.c              -> Linux TAP 虚拟网卡实现
 *        +-- Drive/STM32/XNetwork_lwip_stm32.c              -> STM32 硬件 MAC+PHY 实现
 *
 * 基于 STM32F407+FreeRTOS 成功移植经验，适配 XSync + XMemory 系统。
 * 所有平台实现使用 XMemory 统一内存分配、XTimeWheelGroup 定时器、
 * XRandomGenerator 随机数生成器。
 */
#ifndef XNETWORK_LWIP_PLATFORM_H
#define XNETWORK_LWIP_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "XTypes.h"    /* XFd */

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
struct netif;

/* ================================================================
 * 一、lwIP 网络接口管理
 * ================================================================ */

/**
 * @brief 初始化本平台的 lwIP 虚拟网卡
 * @return 成功返回默认 netif 指针，失败返回 NULL
 *
 * 调用时机：XNetwork_ensureInit() 之后、使用 lwIP 网络之前
 *
 * 平台实现：
 *   - Windows: 创建 loopback (127.0.0.1) 虚拟网卡 + Npcap 适配器虚拟网卡
 *   - Linux:   创建 TAP 适配器虚拟网卡
 *   - 嵌入式:  注册硬件 MAC + PHY 驱动程序
 *
 * @note 实现中应使用 gen_lwip_mac() 生成随机 MAC 地址，避免与真实网卡冲突
 */
struct netif* XNetworkLwip_platform_init(void);

/**
 * @brief 清理本平台的 lwIP 虚拟网卡
 *
 * 调用时机：XNetwork_cleanup() 时调用
 *
 * 平台实现：
 *   - Windows: 关闭所有 Npcap 适配器、释放 DLL
 *   - Linux:   关闭 TAP 适配器
 *   - 嵌入式:  注销硬件网卡驱动
 */
void XNetworkLwip_platform_deinit(void);

/**
 * @brief 获取当前默认 lwIP 网络接口
 * @return 默认 netif 指针，未初始化返回 NULL
 *
 * 用于连接/发送时选择出口网卡。
 */
struct netif* XNetworkLwip_defaultNetif(void);

/**
 * @brief 设置默认 lwIP 网络接口
 * @param netif 要设置为默认的 netif 指针
 *
 * 当 DHCP 获取到有效 IP 后，自动调用此函数设置默认路由。
 */
void XNetworkLwip_setDefaultNetif(struct netif* netif);

/* ================================================================
 * 二、错误码转换
 * ================================================================ */

/**
 * @brief 将 lwIP err_t 错误码转换为类 POSIX errno
 * @param err lwIP 错误码（ERR_OK, ERR_MEM, ERR_TIMEOUT 等）
 * @return 负值错误码（-1=通用错误, -2=超时, -6=WOULDBLOCK 等）
 *
 * 用于 XNetwork_lastError() 和上层错误处理。
 */
int XNetworkLwip_err_to_errno(int err);

/* ================================================================
 * 三、数据包轮询
 * ================================================================ */

/**
 * @brief 轮询平台网卡接收数据包，将数据包喂给 lwIP
 *
 * 由 lwIP 定时器回调每 20ms 调用，在 sys_arch_protect 保护下执行。
 *
 * 平台实现：
 *   - Windows: 轮询所有 Npcap 适配器的 pcap_next_ex
 *   - Linux:   轮询 TAP 适配器的 read()
 *   - 嵌入式:  轮询硬件 MAC 接收描述符
 */
void XNetworkLwip_pollPcap(void);

#ifdef __cplusplus
}
#endif

#endif /* XNETWORK_LWIP_PLATFORM_H */
