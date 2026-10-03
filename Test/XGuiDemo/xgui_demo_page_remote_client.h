/* xgui_demo_page_remote_client.h —— XGuiWindowDemo「远程客户端」设置页契约。
 *
 * 与 xgui_demo_page_remote_server.h 对偶：该页以 XGuiClient 承载远程
 * 客户端会话（IP/端口/档位/TLS + 连接状态 + 实时统计 + 模拟触摸）。
 *
 * CLI 预置：--remote-client（启用本页自动连接）+ --remote-host H +
 * --remote-port N（与服务器页共用 --remote-port 词，语义按角色分派）。
 * CLI 只作初始默认值，主控路径是页面 UI（连接/断开按钮）。
 *
 * 生命周期：XGuiClient 父挂页面 build 传入的主窗口顶层（控件树级联
 * 析构兜底）；程序退出经 demo_page_remote_client_shutdown() 显式断链。
 *
 * XGUI_REMOTE_ON=0 裁剪配置下全部函数零操作（契约符号常在, 主文件
 * 可无条件调用）。
 */

#ifndef XGUI_DEMO_PAGE_REMOTE_CLIENT_H
#define XGUI_DEMO_PAGE_REMOTE_CLIENT_H

#include "XGuiConfig.h"
#include "XGuiRemoteProto.h" /* XGUI_REMOTE_ON 开关单一来源。 */

#ifdef __cplusplus
extern "C" {
#endif

#if XGUI_REMOTE_ON

/** @brief --remote-client 参数族的 CLI 初始默认值（指针借用，调用期内有效）。 */
typedef struct DemoRemoteClientCliOptions
{
    bool        enabled;   /**< --remote-client：预置「自动连接」。 */
    const char* host;      /**< --remote-host H（NULL/空=127.0.0.1）。 */
    int         port;      /**< --remote-port N（<=0 用页面默认 46000）。 */
    const char* profile;   /**< --remote-profile performance|resource（NULL=performance）。 */
    bool        tls;       /**< --remote-tls：初始 TLS 开。 */
    const char* password;  /**< --remote-password PW（NULL/空=未设）。 */
    const char* auth;      /**< --remote-auth none|sha256（NULL=none）。 */
} DemoRemoteClientCliOptions;

/** @brief 预置 CLI 初始默认值（须在主窗口构建前调用；NULL 恢复全默认）。 */
void demo_page_remote_client_cliDefaults(
    const DemoRemoteClientCliOptions* opts);

/** @brief 窗口首帧后的 CLI 自动连接钩子（--remote-client 预置时经 UI
 *         同一条连接路径 connect；内部自带终态标记，可每帧重复调用，
 *         完成后零开销）。 */
void demo_page_remote_client_autostart(void);

/** @brief 程序退出清理：断链 + deleteLater（仓库约定；未启用时零操作。
 *         须在主窗口析构前调用）。 */
void demo_page_remote_client_shutdown(void);

#else /* !XGUI_REMOTE_ON：裁剪配置下全部零操作（契约符号常在）。 */

typedef struct DemoRemoteClientCliOptions
{
    bool        enabled;
    const char* host;
    int         port;
    const char* profile;
    bool        tls;
    const char* password;
    const char* auth;
} DemoRemoteClientCliOptions;

#define demo_page_remote_client_cliDefaults(opts) ((void)(opts))
#define demo_page_remote_client_autostart()       ((void)0)
#define demo_page_remote_client_shutdown()        ((void)0)

#endif /* XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_DEMO_PAGE_REMOTE_CLIENT_H */
