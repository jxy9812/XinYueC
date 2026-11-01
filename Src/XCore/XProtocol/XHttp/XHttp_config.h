/** @file XHttp_config.h
 * @brief HTTP 协议子功能配置文件
 *
 * 通过此配置文件可以裁剪 XHttp 协议内部的各个子功能：
 *   1. XHTTP_CORE_ON    - HTTP/1 核心（XHttpHeaders / XHttpReply / XHttpRequest /
 *                         XHttpAuthenticator / XHttpMultipart / XHstsPolicy）
 *   2. XHTTP_HTTP2_ON   - HTTP/2 支持（XHttp2Client / XHttp2Connection / XHttp2Frame /
 *                         XHttp2Headers / XHttp2Configuration）
 *   3. XHTTP_SERVER_ON  - HTTP 服务器（XHttpServer / XHttpServerRouter / ...）
 *   4. XHTTP_ACCESS_ON  - 访问管理（XNetworkAccessManager / XNetworkCache /
 *                         XNetworkCookie / XRestAccessManager / XServerChan）
 *
 * 协议总开关 XHTTP_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 * 子功能默认随核心开启而启用。
 */

#ifndef XHTTP_CONFIG_H
#define XHTTP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XHTTP_ON
#define XHTTP_ON XPROTOCOL_ON
#endif

#if XHTTP_ON

/* ========================================================================== */
/*                        子功能开关                                        */
/* ========================================================================== */

/** @brief HTTP/1 核心（XHttpHeaders / XHttpReply / XHttpRequest / XHttpAuthenticator / XHttpMultipart / XHstsPolicy） */
#ifndef XHTTP_CORE_ON
#define XHTTP_CORE_ON 1
#endif

/** @brief HTTP/2 支持（XHttp2Client / XHttp2Connection / XHttp2Frame / XHttp2Headers / XHttp2Configuration） */
#ifndef XHTTP_HTTP2_ON
#define XHTTP_HTTP2_ON XHTTP_CORE_ON
#endif

/** @brief HTTP 服务器（XHttpServer / XHttpServerRouter / XHttpServerRouterRule / XHttpServerWebSocketUpgradeResponse） */
#ifndef XHTTP_SERVER_ON
#define XHTTP_SERVER_ON XHTTP_CORE_ON
#endif

/** @brief 访问管理（XNetworkAccessManager / XNetworkCache / XNetworkCookie / XRestAccessManager / XServerChan） */
#ifndef XHTTP_ACCESS_ON
#define XHTTP_ACCESS_ON XHTTP_CORE_ON
#endif

#endif /* XHTTP_ON */

#ifdef __cplusplus
}
#endif

#endif /* XHTTP_CONFIG_H */