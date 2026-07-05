/**
 * @file XNetwork_lwip_platform.h
 * @brief lwIP 平台网络实现 —— 平台相关额外声明
 *
 * 本文件声明 XNetwork_lwip.c 需要由各平台提供/扩展的接口，
 * XNetwork_platform.h 中已声明的标准接口不在此重复。
 *
 * 架构分层：
 *   XNetwork_platform.h           ← 统一平台抽象接口
 *   Library/lwip/XNetwork_lwip.c  ← lwIP 适配层（平台无关）
 *        │
 *        └── Drive/windows/XNetwork_lwip_win32.c   ← Windows 虚拟网卡实现
 *        └── Drive/Posix/XNetwork_lwip_linux.c     ← Linux TAP 网卡实现
 *        └── ...
 */

#ifndef XNETWORK_LWIP_PLATFORM_H
#define XNETWORK_LWIP_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 前置声明 */
struct netif;

/* ============================================================================
 * 一、lwIP 网络接口管理
 * ============================================================================ */

/**
 * @brief 初始化本平台的 lwIP 虚拟网卡
 * @return 成功返回默认 netif 指针，失败返回 NULL
 * @note 调用时机: XNetwork_ensureInit() 之后、使用 lwIP 网络之前
 * @note 平台实现：
 *       - Windows: 创建 loopback (127.0.0.1) 虚拟网卡 + 驱动线程
 *       - Linux:   创建 TAP adapter 虚拟网卡
 *       - 嵌入式:  注册硬件 MAC + PHY 驱动
 */
struct netif* XNetworkLwip_platform_init(void);

/**
 * @brief 清理本平台的 lwIP 虚拟网卡
 * @note 调用时机: XNetwork_cleanup() 时调用
 */
void XNetworkLwip_platform_deinit(void);

/**
 * @brief 获取当前默认 lwIP 网络接口
 * @return 默认 netif 指针，未初始化返回 NULL
 * @note 用于连接/发送时选择出口网卡
 */
struct netif* XNetworkLwip_defaultNetif(void);

/**
 * @brief 设置默认 lwIP 网络接口
 * @param netif 要设置为默认的 netif 指针
 */
void XNetworkLwip_setDefaultNetif(struct netif* netif);

/* ============================================================================
 * 二、错误码转换
 * ============================================================================ */

/**
 * @brief 将 lwIP err_t 错误码转换为类 POSIX errno
 * @param err lwIP 错误码（ERR_OK, ERR_MEM, ERR_TIMEOUT 等）
 * @return 负值错误码（-1 表示通用错误，-2 表示超时等）
 * @note 用于 XNetwork_lastError() 和上层错误处理
 */
int XNetworkLwip_err_to_errno(int err);

#ifdef __cplusplus
}
#endif

#endif /* XNETWORK_LWIP_PLATFORM_H */