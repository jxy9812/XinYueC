/* xgui_demo_page_remote_server.h —— XGuiWindowDemo「远程窗口」设置页扩展契约。
 *
 * 页面 build / autotest 声明见 xgui_demo_pages.h（页面族统一契约）；
 * 本头只承载该页特有的两件事：
 *   1. CLI 初始默认值预置（--remote-server 与同名参数族）——CLI 只作
 *      初始默认值，主控路径是 UI（页面开关/编辑框），见 XGuiRemote.md；
 *   2. 程序退出时的干净停止（unhost + close + deleteLater，仓库约定）。
 *
 * 说明：页面内部以 XGuiServer 承载远程服务（在主窗口控件树上 host
 * 顶层镜像）；XGUI_REMOTE_ON=0 裁剪配置下 build 返回 NULL，主文件
 * 跳过注册，本头的函数仍可安全调用（零操作）。
 */

#ifndef XGUI_DEMO_PAGE_REMOTE_SERVER_H
#define XGUI_DEMO_PAGE_REMOTE_SERVER_H

#include "XGuiConfig.h"
#include "XGuiRemoteProto.h" /* XGUI_REMOTE_ON 开关单一来源。 */

#ifdef __cplusplus
extern "C" {
#endif

#if XGUI_REMOTE_ON

/** @brief --remote-server 参数族的 CLI 初始默认值（指针借用，调用期内有效）。
 *  @note  仅作页面初始状态预置；是否真正启动监听由 UI 开关与
 *         enabled 预置共同决定（enabled=true 时页面在窗口首帧后
 *         自动走 UI 同一条启动路径）。 */
typedef struct DemoRemoteServerCliOptions
{
    bool        enabled;   /**< --remote-server：预置「启用远程服务器」开。 */
    int         port;      /**< --remote-port N：初始端口（<=0 用页面默认 46000）。 */
    const char* profile;   /**< --remote-profile performance|resource|latency（NULL=performance）。 */
    bool        tls;       /**< --remote-tls：初始 TLS 开。 */
    const char* cert;      /**< --remote-cert PATH：初始证书路径（NULL/空=未设）。 */
    const char* key;       /**< --remote-key PATH：初始私钥路径（NULL/空=未设）。 */
    const char* password;  /**< --remote-password PW：初始认证口令（NULL/空=未设）。 */
    const char* auth;      /**< --remote-auth none|sha256（NULL=none）。 */
} DemoRemoteServerCliOptions;

/** @brief 预置 CLI 初始默认值（须在主窗口构建前调用；NULL 恢复全默认）。 */
void demo_page_remote_server_cliDefaults(
    const DemoRemoteServerCliOptions* opts);

/** @brief 窗口首帧后的 CLI 自动启动钩子（--remote-server 预置时经 UI
 *         同一条启动路径 host+listen；内部自带重试与终态标记，可每帧
 *         重复调用，完成后零开销）。 */
void demo_page_remote_server_autostart(void);

/** @brief 程序退出清理：unhost + close + deleteLater（仓库约定；
 *         未启用远程服务时零操作。须在主窗口析构前调用）。 */
void demo_page_remote_server_shutdown(void);

/** @brief 主窗尺寸变化联动：服务地址行宽度按当前根宽自适应
 *         （2026-10-04 多 IP 修——窗口足够宽时 2+ IP 一行放下全部,
 *         窄窗 wordWrap 折行兜底; 幂等可频繁调）。 */
void demo_page_remote_server_adaptWidth(void);

#else /* !XGUI_REMOTE_ON：裁剪配置下全部零操作（契约符号常在）。 */

typedef struct DemoRemoteServerCliOptions
{
    bool        enabled;
    int         port;
    const char* profile;
    bool        tls;
    const char* cert;
    const char* key;
    const char* password;
    const char* auth;
} DemoRemoteServerCliOptions;

#define demo_page_remote_server_cliDefaults(opts) ((void)(opts))
#define demo_page_remote_server_autostart()       ((void)0)
#define demo_page_remote_server_shutdown()        ((void)0)
#define demo_page_remote_server_adaptWidth()      ((void)0)

#endif /* XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_DEMO_PAGE_REMOTE_SERVER_H */
