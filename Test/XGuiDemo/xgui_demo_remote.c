/**
 * @file       xgui_demo_remote.c
 * @brief      XGuiRemote 双进程远程窗口演示（单文件双角色 --server/--client）。
 * @details    人工验收入口（XGuiRemote.md §9.5），对标 xgui_window_demo 的
 *             桌面窗口链路（XWindow + XGuiApplication + XBackingStore 完整
 *             上屏），另加远程会话：
 *               - `--server`：起最小控件树（换色按钮/勾选框/状态栏）+
 *                 XGuiServer 共存镜像（默认端口 46000, 可 --port）; 远端
 *                 点击本窗口控件即时可见并推回远端;
 *               - `--client`：同机另进程连接并在 XGuiClient 远程视图内
 *                 显示远端画面; 本地鼠标/键盘按需求 9 口径转发;
 *               - `--profile performance|resource`：初档; 窗口内「切换
 *                 档位」按钮运行期换档（服务端 setProfileId 广播, 客户端
 *                 requestProfileId 经 PROFILE_SET 协商）;
 *               - `--tls`：客户端经 XSslSocket 加密连接（协议不感知加密）;
 *                 服务端 TLS 档经环境 XGUI_REMOTE_TLS=1 定版（见
 *                 XGuiServer.c xgs_tlsPolicyFromEnv）, 本 demo 在 POSIX 上
 *                 代设该环境变量;
 *               - 本地自测页：参照 xgui_window_demo.c 事件注入样例
 *                 （XWindowSystemInterface_handleMouseEvent_ex 窗口级合成
 *                 press/release + processEvents + 状态断言）, 「本地自测」
 *                 按钮注入一次对「换色」按钮的合成点击并回报结果。
 *             用法示例：
 *               ./XGuiRemoteDemo_Test --server --port 46000
 *               ./XGuiRemoteDemo_Test --client --host 127.0.0.1 --port 46000
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiConfig.h"
#include "XGuiRemoteProto.h" /* XGUI_REMOTE_ON 开关单一来源（#ifndef 定义于
                                本头文件头）——必须先于下方 #if 求值, 否则
                                演示体整段编译出(只剩裁剪占位 main)。 */

#if XGUI_ON && XGUI_REMOTE_ON

#include "XGuiApplication.h"
#include "XGuiRemoteProto.h"
#include "XGuiServer.h"
#include "XGuiClient.h"
#include "XWidget.h"
#include "XObject.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XPushButton.h"
#include "XPalette.h"
#include "XWindowSystemInterface.h"
#include "XWindow.h"
#include "XString.h"
#include "XEvent.h"
#include "XSystem.h"
#include "XAbstractEventDispatcher.h" /* statistics 周期 stdout 输出(poll 回调)。 */
#include "XDateTime.h" /* 单调毫秒时钟(节流)。 */
#include "XPrintf.h"
#include <stdlib.h> /* atoi; setenv: 服务端 TLS 档策略环境代设(demo 进程级)。 */
#include <stdio.h>
#include <string.h>

/* ==================== 演示全局(单窗口单角色, 进程级状态) ==================== */

#define XR_DEMO_DEFAULT_PORT 46000u

typedef enum XrDemoRole {
    XR_DEMO_ROLE_NONE = 0,
    XR_DEMO_ROLE_SERVER = 1,
    XR_DEMO_ROLE_CLIENT = 2
} XrDemoRole;

static XrDemoRole g_role = XR_DEMO_ROLE_NONE;
static uint16_t g_port = XR_DEMO_DEFAULT_PORT;
static const char* g_host = "127.0.0.1";
static bool g_tls = false;
static XGuiRemoteProfileId g_profile = XGUI_REMOTE_PROFILE_PERFORMANCE;
static int64_t g_autoQuitSec = 0;       /* --auto-quit N: N 秒后干净退出
                                          * (联调矩阵 BYE 断链口径; 0=不限)。 */
static int64_t g_autoQuitDeadlineMs = 0; /* 首个 poll tick 定版时刻。 */

/* 服务端侧。 */
static XGuiServer* g_server = NULL;
static XWidget* g_win = NULL;         /* 服务端顶层（被镜像）。 */
static XWidget* g_colorBox = NULL;    /* 「换色」目标(自测点击对象)。 */
static XLabel* g_srvStatus = NULL;
static int g_colorIdx = 0;
static int g_srvClicks = 0;           /* 换色按钮真实点击计数。 */

/* 客户端侧。 */
static XGuiClient* g_client = NULL;
static XLabel* g_cliStatus = NULL;
static XHandle g_statsPoll = NULL;  /* statistics 周期输出 poll 句柄(拥有登记)。 */
static int64_t g_lastStatsMs = 0;   /* 上次输出时刻(2s 节流)。 */

static const int XR_COLOR_COUNT = 4;
static const int XR_COLORS[4][3] = {
    { 0x21, 0x53, 0x9B },
    { 0x2E, 0x9E, 0x44 },
    { 0xD9, 0x77, 0x25 },
    { 0x7B, 0x2D, 0x9E }
};

/* ==================== 工具 ==================== */

static void xr_setWidgetColor(XWidget* w, int r, int g, int b)
{
    XPalette pal;
    XColor c;
    XPalette_init_default(&pal);
    XColor_init_rgb(&c, r, g, b, 255);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                      XPaletteColorRole_Window, c);
    XWidget_setPalette(w, &pal);
    XWidget_setAutoFillBackground(w, true);
}

static const char* xr_profileName(XGuiRemoteProfileId id)
{
    switch (id) {
    case XGUI_REMOTE_PROFILE_PERFORMANCE: return "performance";
    case XGUI_REMOTE_PROFILE_RESOURCE: return "resource";
    case XGUI_REMOTE_PROFILE_AUTO: return "auto(V1=resource)";
    default: return "custom";
    }
}

static XGuiRemoteProfileId xr_nextProfile(XGuiRemoteProfileId id)
{
    return id == XGUI_REMOTE_PROFILE_PERFORMANCE ? XGUI_REMOTE_PROFILE_RESOURCE
                                                 : XGUI_REMOTE_PROFILE_PERFORMANCE;
}

/* ==================== 服务端角色 ==================== */

static void srv_applyColor(void)
{
    if (!g_colorBox) return;
    xr_setWidgetColor(g_colorBox, XR_COLORS[g_colorIdx % XR_COLOR_COUNT][0],
                      XR_COLORS[g_colorIdx % XR_COLOR_COUNT][1],
                      XR_COLORS[g_colorIdx % XR_COLOR_COUNT][2]);
    XWidget_update(g_colorBox);
}

static void srv_onColorClicked(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++g_srvClicks;
    ++g_colorIdx;
    srv_applyColor();
    if (g_srvStatus) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "服务端: 换色 x%d, 当前色 #%02x%02x%02x (远端/本地点击同路)",
                 g_srvClicks, XR_COLORS[g_colorIdx % XR_COLOR_COUNT][0],
                 XR_COLORS[g_colorIdx % XR_COLOR_COUNT][1],
                 XR_COLORS[g_colorIdx % XR_COLOR_COUNT][2]);
        XLabel_setText_2(g_srvStatus, buf);
    }
}

static void srv_onProfileClicked(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    if (!g_server) return;
    g_profile = xr_nextProfile(g_profile);
    XGuiServer_setProfileId(g_server, g_profile); /* 帧边界生效+广播 FB_META。 */
    if (g_srvStatus) {
        char buf[128];
        snprintf(buf, sizeof(buf), "服务端: 档位切换 → %s (已广播 FB_META)",
                 xr_profileName(g_profile));
        XLabel_setText_2(g_srvStatus, buf);
    }
}

static void srv_onSelfTestClicked(XObject* sender, XVarList* args)
{
    XWindow* xwin;
    XPoint tpos;
    XPoint tglobal;
    int clicksBefore = g_srvClicks;
    (void)sender;
    (void)args;
    if (!g_colorBox) return;
    xwin = g_win ? (XWindow*)XWidget_windowHandle(g_win) : NULL;
    if (!xwin) {
        if (g_srvStatus) XLabel_setText_2(g_srvStatus, "自测失败: 无窗口句柄");
        return;
    }
    /* 本地自测（参照 xgui_window_demo.c:1196-1216 注入样例）：窗口级合成
     * 鼠标按下/抬起点「换色」按钮中心 → processEvents 泵完同步链 → 计数
     * 断言。该链路与远程注入(XGuiServer INPUT_POINTER→WSI)同层同路。 */
    tpos.x = XWidget_x(g_colorBox) + XWidget_width(g_colorBox) / 2;
    tpos.y = XWidget_y(g_colorBox) + XWidget_height(g_colorBox) / 2;
    tglobal = tpos;
    XWindowSystemInterface_handleMouseEvent_ex(
        xwin, XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_LeftButton,
        XMouseButton_LeftButton, XKeyboardModifier_NoModifier, tpos, &tglobal,
        0);
    XWindowSystemInterface_handleMouseEvent_ex(
        xwin, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, XMouseButton_LeftButton, 0,
        XKeyboardModifier_NoModifier, tpos, &tglobal, 0);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    if (g_srvStatus) {
        char buf[160];
        snprintf(buf, sizeof(buf), "本地自测: %s(点击计数 %d → %d)",
                 g_srvClicks == clicksBefore + 1 ? "PASS" : "FAIL",
                 clicksBefore, g_srvClicks);
        XLabel_setText_2(g_srvStatus, buf);
    }
}

static void srv_onSession(XObject* sender, XVarList* args)
{
    (void)sender;
    if (g_srvStatus && args) {
        XVarList_args_1(args, int, sid);
        char buf[128];
        snprintf(buf, sizeof(buf), "服务端: 会话 #%d 已接入(流式推送中)", sid);
        XLabel_setText_2(g_srvStatus, buf);
    }
}

static void srv_onSessionEnd(XObject* sender, XVarList* args)
{
    (void)sender;
    if (g_srvStatus && args) {
        XVarList_args_2(args, int, sid, int, reason);
        char buf[160];
        snprintf(buf, sizeof(buf), "服务端: 会话 #%d 断开(reason=%d)", sid,
                 reason);
        XLabel_setText_2(g_srvStatus, buf);
    }
}

static int xr_runServer(void)
{
    XGuiApplication* app;
    XWidget* profileBtn;
    XWidget* selfTestBtn;
    XCheckBox* box;
    uint16_t actualPort;
    int rc = 0;

    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app) {
        XPrintf("XGuiRemoteDemo: XGuiApplication_create_ex 失败\n");
        return 1;
    }

#if !defined(_WIN32)
    if (g_tls) {
        /* 服务端 TLS 策略在 listen 时从环境定版(XGUI_REMOTE_TLS 恰为 "1"
         * 时 accept 后升级), demo 代设(进程级; 库内唯一环境读取口为
         * XSystem_environment, 库不提供写入口)。 */
        setenv("XGUI_REMOTE_TLS", "1", 1);
        XPrintf("XGuiRemoteDemo: 服务端 TLS 档已请求(XGUI_REMOTE_TLS=1)\n");
    }
#else
    if (g_tls)
        XPrintf("XGuiRemoteDemo: 非 UNIX 平台请以环境变量开启服务端 TLS"
                "(XGUI_REMOTE_TLS=1)\n");
#endif

    /* 最小控件树: 换色按钮(远端点击目标) + 勾选框 + 档位/自测按钮 + 状态。 */
    g_win = XWidget_create(NULL, 0);
    if (!g_win) {
        XPrintf("XGuiRemoteDemo: 服务端窗口创建失败\n");
        XClassDelete(app);
        return 1;
    }
    XWidget_resize(g_win, 480, 320);

    /* 换色按钮 = 远端点击目标: 远端 INPUT_POINTER 注入本窗口后由框架命中
     * 分派到本按钮, clicked 槽换色 → present 采集 → 新画面推回远端。 */
    {
        XPushButton* colorBtn = XPushButton_create(g_win, 0);
        XWidget_setGeometry((XWidget*)colorBtn, 24, 24, 200, 120);
        XPushButton_setText_2(colorBtn, "换色(远端点击我)");
        XObject_connect_2((XObject*)colorBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          srv_onColorClicked);
        XWidget_show((XWidget*)colorBtn);
        g_colorBox = (XWidget*)colorBtn; /* 自测与换色同一按钮。 */
    }

    box = XCheckBox_create(g_win, 0);
    XWidget_setGeometry((XWidget*)box, 244, 28, 160, 26);
    XAbstractButton_setText_2((XAbstractButton*)box, "勾选框(远端可点)");
    XWidget_show((XWidget*)box);

    profileBtn = XPushButton_create(g_win, 0);
    XWidget_setGeometry(profileBtn, 24, 168, 132, 30);
    XPushButton_setText_2((XPushButton*)profileBtn, "切换档位");
    XObject_connect_2((XObject*)profileBtn,
                      XSignal(XAbstractButton_clicked_signal),
                      srv_onProfileClicked);
    XWidget_show(profileBtn);

    selfTestBtn = XPushButton_create(g_win, 0);
    XWidget_setGeometry(selfTestBtn, 168, 168, 132, 30);
    XPushButton_setText_2((XPushButton*)selfTestBtn, "本地自测");
    XObject_connect_2((XObject*)selfTestBtn,
                      XSignal(XAbstractButton_clicked_signal),
                      srv_onSelfTestClicked);
    XWidget_show(selfTestBtn);

    g_srvStatus = XLabel_create(g_win, 0);
    XWidget_setGeometry((XWidget*)g_srvStatus, 24, 216, 432, 88);
    XLabel_setText_2(g_srvStatus,
                     "服务端: 等待客户端接入…\n"
                     "提示: 换色/切档/本地自测均可鼠标点按; 远端操作同样生效。");
    XWidget_show((XWidget*)g_srvStatus);

    {
        XString* title = XString_create_utf8("XGuiRemote 演示·服务端");
        XWidget_setWindowTitle(g_win, title);
        XClassDelete((XClass*)title);
    }
    XWidget_show(g_win);

    /* 远程服务: 镜像本窗口 + TCP 监听。 */
    g_server = XGuiServer_create(NULL);
    if (!g_server || !XGuiServer_host(g_server, g_win)) {
        XPrintf("XGuiRemoteDemo: XGuiServer_host 失败(需顶层+后备存储)\n");
        rc = 1;
    }
    else {
        XGuiServer_setProfileId(g_server, g_profile);
        XObject_connect_2((XObject*)g_server,
                          XSignal(XGuiServer_clientConnected_signal),
                          srv_onSession);
        XObject_connect_2((XObject*)g_server,
                          XSignal(XGuiServer_clientDisconnected_signal),
                          srv_onSessionEnd);
        if (!XGuiServer_listen(g_server, g_port)) {
            XPrintf("XGuiRemoteDemo: 监听 %u 端口失败\n", (unsigned)g_port);
            rc = 1;
        }
        else {
            actualPort = XGuiServer_serverPort(g_server);
            XPrintf("XGuiRemoteDemo: 服务端就绪 port=%u profile=%s tls=%s\n",
                    (unsigned)actualPort, xr_profileName(g_profile),
                    g_tls ? "on" : "off");
        }
    }

    if (rc == 0) {
        XGuiApplication_exec();
        XPrintf("XGuiRemoteDemo: 服务端退出\n");
    }

    /* 清理: 先解绑镜像(恢复 present 回调)再销毁窗口, 最后应用单例。 */
    if (g_server) {
        XGuiServer_close(g_server);
        XClassDelete(g_server);
        g_server = NULL;
    }
    if (g_win) {
        XClassDelete(g_win);
        g_win = NULL;
    }
    XClassDelete(app);
    return rc;
}

/* ==================== 客户端角色 ==================== */

static void cli_refreshStatus(const char* note)
{
    if (!g_cliStatus || !g_client) return;
    {
        XGuiRemoteStats st;
        XSize rsz;
        char buf[256];
        XGuiClient_statistics(g_client, &st);
        XGuiClient_remoteSize(g_client, &rsz);
        snprintf(buf, sizeof(buf),
                 "客户端: %s\n状态=%d 远端=%ux%u 档位=%s\n"
                 "帧=%u tile=%u 收=%llu B 发=%llu B rtt=%ums",
                 note, (int)XGuiClient_state(g_client),
                 (unsigned)rsz.width, (unsigned)rsz.height,
                 xr_profileName(g_profile), st.updateCount, st.tileCount,
                 (unsigned long long)st.bytesReceived,
                 (unsigned long long)st.bytesSent, (unsigned)st.rttMs);
        XLabel_setText_2(g_cliStatus, buf);
    }
}

static void cli_onConnected(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    cli_refreshStatus("已连接(流式)");
}

static void cli_onDisconnected(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) {
        cli_refreshStatus("断开, 自动重连中…");
        return;
    }
    {
        XVarList_args_1(args, int, reason);
        char buf[96];
        /* 诊断走 stdout(行缓冲): 联调矩阵采集断链原因(XGuiRemoteByeReason)。 */
        XPrintf("XGuiRemoteDemo: disconnected reason=%d\n", reason);
        snprintf(buf, sizeof(buf), "断开(reason=%d), 自动重连中…", reason);
        cli_refreshStatus(buf);
    }
}

static void cli_onMeta(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    cli_refreshStatus("画面元信息更新");
}

static void cli_onProfileClicked(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    if (!g_client) return;
    g_profile = xr_nextProfile(g_profile);
    XGuiClient_requestProfileId(g_client, g_profile); /* PROFILE_SET 协商。 */
    cli_refreshStatus("已请求切档(待服务端 PROFILE_RESULT/FB_META)");
}

/* statistics 周期 stdout 输出（联调矩阵采集用, 固定 "stats " 前缀便于
 * grep; 2s 节流）。诊断口径走 stdout 行缓冲(启动已 setvbuf), 不依赖
 * 标签控件。 */
static void cli_statsPoll(void* user)
{
    XGuiRemoteStats st;
    XSize rsz;
    int64_t now;
    (void)user;
    if (!g_client) return;
    now = XDateTime_currentMSecsSinceEpoch();
    if (g_autoQuitDeadlineMs == 0 && g_autoQuitSec > 0)
        g_autoQuitDeadlineMs = now + g_autoQuitSec * 1000;
    /* --auto-quit 到点: 干净退出事件循环(main 收尾走 BYE 断链)。 */
    if (g_autoQuitDeadlineMs != 0 && now >= g_autoQuitDeadlineMs) {
        XPrintf("XGuiRemoteDemo: auto-quit 到点, 请求退出\n");
        XGuiApplication_quit();
        return;
    }
    if (g_lastStatsMs != 0 && now - g_lastStatsMs < 2000) return;
    g_lastStatsMs = now;
    XGuiClient_statistics(g_client, &st);
    XGuiClient_remoteSize(g_client, &rsz);
    XPrintf("XGuiRemoteDemo: stats state=%d frames=%u tiles=%u "
            "fps=%u.%02u rx=%llu tx=%llu rtt=%ums remote=%ux%u profile=%s\n",
            (int)XGuiClient_state(g_client), st.updateCount, st.tileCount,
            st.fpsMilli / 100u, st.fpsMilli % 100u,
            (unsigned long long)st.bytesReceived,
            (unsigned long long)st.bytesSent, (unsigned)st.rttMs,
            (unsigned)rsz.width, (unsigned)rsz.height,
            xr_profileName(g_profile));
}

static int xr_runClient(void)
{
    XGuiApplication* app;
    XWidget* win;
    XWidget* profileBtn;
    int rc = 0;

    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app) {
        XPrintf("XGuiRemoteDemo: XGuiApplication_create_ex 失败\n");
        return 1;
    }

    win = XWidget_create(NULL, 0);
    if (!win) {
        XPrintf("XGuiRemoteDemo: 客户端窗口创建失败\n");
        XClassDelete(app);
        return 1;
    }
    /* V1 恒 1:1(§7.2 setFixedSize): 远端典型 800x600, 窗口按「镜像
     * 800x600 + 自有控件一行」足幅——原 560x480 时自有按钮/状态标签
     * (y=376/412)叠在镜像上(E2E 实测遮挡镜像文本)。 */
    XWidget_resize(win, 830, 700);

    g_client = XGuiClient_create(win, 0);
    XWidget_setGeometry((XWidget*)g_client, 8, 8, 544, 360);
    XGuiClient_setAutoReconnect(g_client, true, 1000); /* 演示断线重连。 */

    profileBtn = XPushButton_create(win, 0);
    XWidget_setGeometry(profileBtn, 8, 616, 132, 30);
    XPushButton_setText_2((XPushButton*)profileBtn, "切换档位");
    XObject_connect_2((XObject*)profileBtn,
                      XSignal(XAbstractButton_clicked_signal),
                      cli_onProfileClicked);
    XWidget_show(profileBtn);

    g_cliStatus = XLabel_create(win, 0);
    XWidget_setGeometry((XWidget*)g_cliStatus, 8, 652, 814, 44);
    XLabel_setText_2(g_cliStatus, "客户端: 连接中…");
    XWidget_show((XWidget*)g_cliStatus);

    {
        XString* title = XString_create_utf8("XGuiRemote 演示·客户端");
        XWidget_setWindowTitle(win, title);
        XClassDelete((XClass*)title);
    }
    XWidget_show(win);

    XObject_connect_2((XObject*)g_client,
                      XSignal(XGuiClient_connected_signal), cli_onConnected);
    XObject_connect_2((XObject*)g_client,
                      XSignal(XGuiClient_disconnected_signal),
                      cli_onDisconnected);
    XObject_connect_2((XObject*)g_client,
                      XSignal(XGuiClient_remoteMetaChanged_signal), cli_onMeta);

#if XGUI_REMOTE_TLS_ON
    if (g_tls)
        XGuiClient_connectToHostEncrypted(g_client, g_host, g_port, NULL);
    else
        XGuiClient_connectToHost(g_client, g_host, g_port);
#else
    if (g_tls)
        XPrintf("XGuiRemoteDemo: 本构建未启用 TLS 档, 回退明文\n");
    XGuiClient_connectToHost(g_client, g_host, g_port);
#endif
    XPrintf("XGuiRemoteDemo: 客户端连接 %s:%u tls=%s\n", g_host,
            (unsigned)g_port, g_tls ? "on" : "off");

    /* statistics 周期 stdout 输出登记（2s 节流, 供联调矩阵采集）。 */
    g_statsPoll = XAbstractEventDispatcher_addPollCallback(cli_statsPoll,
                                                           NULL);

    XGuiApplication_exec();
    cli_statsPoll(NULL); /* 退出前补一条最终统计(与节流无关)。 */
    XPrintf("XGuiRemoteDemo: 客户端退出\n");

    if (g_statsPoll) {
        XAbstractEventDispatcher_removePollCallback(g_statsPoll);
        g_statsPoll = NULL;
    }
    XGuiClient_disconnectFromServer(g_client);
    XClassDelete(win); /* 先于 client: 父子同删(client 为其子)。 */
    g_client = NULL;
    XClassDelete(app);
    return rc;
}

/* ==================== 入口 ==================== */

static void xr_printUsage(void)
{
    XPrintf("XGuiRemoteDemo — XGuiServer/XGuiClient 远程窗口双进程演示\n"
            "用法:\n"
            "  XGuiRemoteDemo_Test --server [--port N] "
            "[--profile performance|resource] [--tls]\n"
            "  XGuiRemoteDemo_Test --client [--host H] [--port N] "
            "[--profile performance|resource] [--tls] [--auto-quit N]\n"
            "默认: --server 角色, 端口 %u, performance 档\n",
            (unsigned)XR_DEMO_DEFAULT_PORT);
}

int main(int argc, char** argv)
{
    /* stdout 行缓冲(对齐 xgui_window_demo.c 口径): 非终端重定向时
     * glibc 全缓冲, kill -9 不经 atexit/stdio 清理会整段丢启动日志
     * (E2E 实测: 服务端就绪行全丢)。每个 '\n' 自动 flush。 */
    setvbuf(stdout, NULL, _IOLBF, 1024);
    int i;
    for (i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (strcmp(a, "--server") == 0) {
            g_role = XR_DEMO_ROLE_SERVER;
        }
        else if (strcmp(a, "--client") == 0) {
            g_role = XR_DEMO_ROLE_CLIENT;
        }
        else if (strcmp(a, "--port") == 0 && i + 1 < argc) {
            g_port = (uint16_t)atoi(argv[++i]);
        }
        else if (strcmp(a, "--host") == 0 && i + 1 < argc) {
            g_host = argv[++i];
        }
        else if (strcmp(a, "--profile") == 0 && i + 1 < argc) {
            ++i;
            if (strcmp(argv[i], "resource") == 0)
                g_profile = XGUI_REMOTE_PROFILE_RESOURCE;
            else
                g_profile = XGUI_REMOTE_PROFILE_PERFORMANCE;
        }
        else if (strcmp(a, "--tls") == 0) {
            g_tls = true;
        }
        else if (strcmp(a, "--auto-quit") == 0 && i + 1 < argc) {
            /* N 秒后请求退出事件循环(0/缺省=不限); main 收尾统一走
             * disconnectFromServer→BYE, 供联调矩阵验证干净断链。
             * 时长在首个 poll tick 定版为连接后 N 秒。 */
            g_autoQuitSec = (int64_t)atoi(argv[++i]);
        }
        else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            xr_printUsage();
            return 0;
        }
    }
    if (g_port == 0) g_port = XR_DEMO_DEFAULT_PORT;
    if (g_role == XR_DEMO_ROLE_NONE) {
        xr_printUsage();
        return 1;
    }
    if (g_role == XR_DEMO_ROLE_SERVER) return xr_runServer();
    return xr_runClient();
}

#else /* !XGUI_ON || !XGUI_REMOTE_ON */

#include <stdio.h>
int main(void)
{
    fprintf(stderr, "XGuiRemoteDemo: 本构建未启用 XGui/XGuiRemote, 无演示内容\n");
    return 0;
}

#endif /* XGUI_ON && XGUI_REMOTE_ON */
