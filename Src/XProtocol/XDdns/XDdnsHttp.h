/** @file       XDdnsHttp.h
 * @brief      XDdns 轻量 HTTP GET 传输公开 API（裸 XTcpSocket 直发）。
 * @details    对标"最小 HTTP/1.0 客户端"：connectToHost → GET → 读到对端
 *             关闭 → 拆状态行与应答体。为何不用 XNetworkAccessManager
 *             见 XDdnsHttp.c 文件头（XNM 同步 GET 对真实互联网地址存在
 *             崩溃/超时问题，2026-10-16 实测；待根因修复后可切换回去）。
 *             HTTPS 预留：XDdnsHttp_get 的 URL 支持 https scheme 时需
 *             经 XSslSocket 同构实现（XDDNS_SSL_ON 扩展位）。
 */
#ifndef XDDNSHTTP_H
#define XDDNSHTTP_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入本协议配置，确保 XDDNS_ON / XDDNS_CLIENT_ON 已定义
   （必须早于下方条件编译判定，否则直接 include 本头文件时开关未定义、
   整段声明会被跳过） */
#include "CXinYueConfig.h"
#include "XDdns_config.h"

#if XDDNS_ON && XDDNS_CLIENT_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief      同步 HTTP GET：URL → 应答体（截断至 cap-1）。
 * @param      urlUtf8 完整 URL（http scheme；借用）。
 * @param      timeoutMs 连接/收发总超时（毫秒）。
 * @param      bodyOut 应答体输出缓冲区（调用方提供存储空间），NUL 结尾。
 * @param      cap 输出缓冲区容量。
 * @param      statusCodeOut HTTP 状态码输出（调用方提供存储空间）；可 NULL。
 * @return     HTTP 2xx 且成功读到应答体返回 true；连接/超时/非 2xx/应答
 *             超长返回 false（bodyOut 保持 "空串或已收到的部分"）。
 * @note       阻塞口径：内部 waitForReadyRead 轮询 + 总 deadline，供
 *             GUI 线程短耗时调用（探测/签名 API 均 <2KB 应答）。
 * @warning    仅支持 http scheme；https 需 XSslSocket 扩展（预留）。
 */
bool XDdnsHttp_get(const char* urlUtf8, uint32_t timeoutMs,
                   char* bodyOut, size_t cap, int* statusCodeOut);

#endif /* XDDNS_ON && XDDNS_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDDNSHTTP_H */
