/**
 * @file       XhsfProtocol.h
 * @brief      高速过滤器公共协议约定（服务端/客户端共用，无代码）。
 * @details    文本行协议（'\n' 结尾）：
 *               d <浮点>  提交一个采样        → 应答 r <滤波后值>
 *               a <浮点>  设置平滑系数(0..1]  → 应答 ok
 *               z         复位滤波器           → 应答 ok
 *             常量两端口径一致，改这里即两端同步。
 */
#ifndef XHSFPROTOCOL_H
#define XHSFPROTOCOL_H

#include <stdint.h>

/** @brief 服务端默认监听端口（客户端缺省连接目标一致）。 */
#define XHSF_SERVER_DEFAULT_PORT 6000

/** @brief 服务端最大并发客户端连接数（模板固定槽位，按需调整）。 */
#define XHSF_SERVER_MAX_CLIENTS 8

/** @brief 服务端单连接行缓冲容量（字节；一行协议不得超过该值）。 */
#define XHSF_SERVER_RXBUF_SIZE 512

/** @brief 客户端行缓冲容量（字节）。 */
#define XHSF_CLIENT_RXBUF_SIZE 512

#endif /* XHSFPROTOCOL_H */
