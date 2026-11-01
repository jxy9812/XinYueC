/**
 * @file       XhsfClient.h
 * @brief      高速过滤器客户端（模板）：XGuiApplication 窗口 + TCP 上报。
 * @details    窗口：数值输入框 + 发送按钮 + 滤波结果/连接状态标签。
 *             协议同服务端（d/a/z 文本行）。整窗生命周期由本模块接管。
 */
#ifndef XHSFCLIENT_H
#define XHSFCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

/**
 * @brief 启动客户端：建窗、连接服务端并进入 GUI 事件循环（阻塞至退出）。
 * @param host 服务端主机名/IP（UTF-8；如 "127.0.0.1"）。
 * @param port 服务端端口。
 * @return 事件循环退出码（0=正常）。
 * @note 调用方需已创建 XGuiApplication（main_client.c 负责）。
 */
int XhsfClient_start(const char* host, unsigned port);

#endif /* XHSFCLIENT_H */
