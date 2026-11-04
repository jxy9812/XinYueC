/**
 * @file       HsfUi.h
 * @brief      高速过滤器客户端主窗组装与启动入口。
 * @details    三段式主窗（仿 XGuiWindowDemo 布局化 chrome）：第一行标题栏
 *             （HsfTitleBar）+ 中间堆栈窗口（HsfStack）+ 底部黑色状态栏
 *             （HsfStatusBar），根容器垂直布局。各部件独立模块（ui/ 目录
 *             各自 .h/.c），本模块只做装配与页序语义（切页/标题/状态同步）。
 *             登录（HsfLogin）→ 主窗装配 → 事件循环的启动编排也在此。
 */
#ifndef HSFUI_H
#define HSFUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>

/**
 * @brief 启动选项（登录口径与 CI 通道；缺省 0/NULL）。
 */
typedef struct HsfUiOptions {
    int autoSeconds;            /**< >0 自动退出秒数（CI/截图验收）；0 常驻。 */
    const char* shotPath;       /**< 离屏截图输出路径（UTF-8；NULL=不截图）。 */
    int shotWidth;              /**< 截图窗口宽（0=缺省基准）。 */
    int shotHeight;             /**< 截图窗口高（0=缺省基准）。 */
    int startPage;              /**< 启动页索引（-1=主页）。 */
    int thenPage;               /**< 出图前切到的目标页（-1=不切）。 */
    const char* loginSpec;      /**< 无头登录 "user:pass@host:port"（CI/联调）；
                                     NULL=登录窗口或跳过。 */
    int noLogin;                /**< 1=跳过登录直进主界面（离线演示）。 */
    int loginShot;              /**< 1=离屏截图目标是登录窗（而非主窗）。 */
} HsfUiOptions;

/**
 * @brief 启动客户端 UI：登录（窗口/无头/跳过）→ 建主窗 → GUI 事件循环。
 * @param opts 启动选项（借用；NULL 按「登录窗口 + 常驻」处理）。
 * @return 事件循环退出码（0=正常；登录失败/取消按失败码）。
 * @note 调用方需已创建 XGuiApplication（main_client.c 负责）。
 * @note 登录走 XLink 内建挑战应答（HsfLogin/HsfLinkClient）；成功后才建
 *       主窗。变频器页在「有连接意图」时自动接远端 provider，离线
 *       （--no-login）回落本地模拟源。
 */
int HsfUi_start(const HsfUiOptions* opts);

#ifdef __cplusplus
}
#endif

#endif /* HSFUI_H */
