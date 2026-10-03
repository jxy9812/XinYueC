/******************************************************************************
 * @file       xgui_demo_page_remote_server.c
 * @brief      XGuiWindowDemo「远程窗口」设置页（XGuiServer 远程服务开关与
 *             全量配置的 UI 主控页）。
 * @details    契约见 xgui_demo_pages.h（demo_page_remote_server_build /
 *             demo_page_remote_server_autotest）与
 *             xgui_demo_page_remote_server.h（CLI 预置/autostart/shutdown）。
 *
 *             页面内容（内容区约 760x480）：
 *              - 远程服务开关（启动/停止，勾选框）；
 *              - 端口（编辑框）/ 档位 performance|resource（下拉）/ TLS 开关
 *                / 证书路径 / 私钥路径 / 认证方式与口令（全量 UI 可改）；
 *              - 「应用配置」按钮 + 待生效提示行（显式生效时机反馈）；
 *              - 会话状态区（监听中 :端口 / 会话数 / 最近事件 / 最近错误）；
 *              - RemotePing 计数按钮（联调断言辅助，见下）。
 *
 *             生效时机（显式，严禁静默不生效）：
 *              - 档位 = 运行期热切换：XGuiServer_setProfileId 对既有会话
 *                帧边界生效并广播 FB_META（XGuiRemote.md §5/§6.4），会话
 *                不断；未监听时仅更新服务端默认档（同样即时生效）。
 *              - 端口 / TLS / 证书路径 / 私钥路径 / 认证 / 口令 = 监听期
 *                配置：改动先暂存（待生效提示行可见），点「应用配置」时
 *                重启监听（close→listen）后生效；启动监听时按当前界面值
 *                定版。每次应用都给出「已应用」或失败原因的状态反馈。
 *
 *             生命周期：XGuiServer 实例父挂主窗口顶层（控件树/对象树上，
 *             级联析构兜底）；host 在启动监听时惰性执行（需窗口后备存储
 *             就绪）；程序退出经 demo_page_remote_server_shutdown()
 *             unhost + close + deleteLater（XObject 析构会清挂起
 *             deferred-delete 事件，两序皆安全，见 XObject.c:583）。
 *
 *             RemotePing 按钮 = 联调断言辅助：本地点击与远程注入点击走
 *             同一条 clicked 信号路径，计数 +1 并重绘计数标签——E2E 矩阵
 *             以「远程注入后计数增长 + 像素变化」作双向闭环断言。
 *
 *             autotest 口径：全程非阻塞（直呼槽/经 setChecked 触发真实
 *             toggled 信号），监听演练一律用端口 0（系统分配，不占固定
 *             端口），结束恢复「未监听 + 默认端口显示」。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h> /* atoi */
#include "XGuiConfig.h"
#include "XGuiRemoteProto.h" /* XGUI_REMOTE_ON 开关单一来源——必须先于下方 #if。 */
#include "XPrintf.h"
#include "XObject.h"
#include "XWidget.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XLineEdit.h"
#include "XComboBox.h"
#include "XPushButton.h"
#include "XGuiServer.h"
#include "XWidget.h" /* XWidget_windowHandle/XWidget_setWindowTitle_2。 */
#include "XWindow.h" /* XWindow_title/XWindow_setTitle_2(标题跟随 FB_META)。 */
#include "XString.h" /* XString/XString_create_utf8/XString_compare。 */
#include "XNetworkInterface.h" /* 本机 IP 枚举(服务地址显示; XDeviceNetwork
                                * getifaddrs 封装的仓内现有 API)。 */
#include "xgui_demo_pages.h"
#include "xgui_demo_page_remote_server.h"

/*
 * 控件段宏守卫：本页依赖 XGuiRemote 与 PushButton/Label/CheckBox/
 * LineEdit/ComboBox。任一模块被裁剪时降级为契约桩（build 返回 NULL、
 * autotest 返回 -1），保证契约符号始终存在、主文件可无条件链接。
 */
#if XGUI_REMOTE_ON && XWIDGET_ON && XPUSHBUTTON_ON && \
    XLABEL_ON && XCHECKBOX_ON && XLINEEDIT_ON && XCOMBOBOX_ON

/* ==================== 页面几何常量（内容区约 760x480） ====================
 * 2026-10-03 目验收口：800x600 + 左停靠导航面板(176)时页面根仅 600 宽，
 * 全部控件以「列右缘 = rootW-8 = 592」反推收窄，800x600 与更窄窗口不溢出。 */

#define RS_PAGE_COL2_X   480  /**< 右列原点 x（状态/待生效提示）。 */

/* ==================== 服务地址行（本机 IP 枚举; 2026-10-02 增强） ======== */

/** @brief 枚举本机非回环 IPv4 填入 buf（逗号分隔）。
 *  @details 走仓内现有 XNetworkInterface API（Drive/Posix 层
 *           getifaddrs 封装, 先例 XNetworkInterfaceTest）。最多取
 *           maxAddrs 个; 一个都没有时 buf 收 "none"。 */
static void rs_collectLocalIPv4(char* buf, int cap, int maxAddrs)
{
    XVector* addrs = XNetworkInterface_allAddresses();
    int count = 0;
    if (!buf || cap <= 0) return;
    buf[0] = '\0';
    if (addrs) {
        size_t i;
        size_t n = XVector_size_base(addrs);
        for (i = 0; i < n && count < maxAddrs; ++i) {
            XHostAddress* a = (XHostAddress*)XVector_at_base(addrs, (int64_t)i);
            XString* s;
            if (!a || a->isNull || a->protocol != XHostAddress_IPv4Protocol)
                continue;
            if (XHostAddress_isLoopback(a)) continue; /* 非回环口径。 */
            s = XHostAddress_toString(a);
            if (!s) continue;
            if (buf[0]) strncat(buf, ",", (size_t)cap - strlen(buf) - 1);
            strncat(buf, XString_toUtf8(s), (size_t)cap - strlen(buf) - 1);
            XClassDelete(s);
            ++count;
        }
        XClassDelete(addrs);
    }
    if (count == 0)
        snprintf(buf, (size_t)cap, "none");
}

/* ==================== CLI 初始默认值（进程级，build 前预置） ============ */

/** @brief CLI 预置暂存（指针借用；未预置时全零=等价默认）。 */
static DemoRemoteServerCliOptions s_rsCli;

void demo_page_remote_server_cliDefaults(const DemoRemoteServerCliOptions* opts)
{
    memset(&s_rsCli, 0, sizeof(s_rsCli));
    if (opts) s_rsCli = *opts;
}

/* ==================== 页面内部控件登记（demo 单实例 static 自持） ======== */

/**
 * @brief 远程窗口设置页内部登记表。
 * @details 控件经 *_create 堆创建挂在页面根下（父子链级联析构），本表
 *          只存借用指针；XGuiServer 父挂主窗口顶层（非页面根），退出
 *          经 demo_page_remote_server_shutdown() 显式收尾。
 */
static struct
{
    XWidget*         m_root;        /**< 页面根控件（契约返回值；堆对象）。 */
    DemoPageStatusFn m_status;      /**< 主窗口状态栏反馈回调（借用）。 */
    void*            m_user;        /**< 回调上下文（主窗口指针，借用）。 */
    char             m_lastStatus[160]; /**< 最近一次反馈文本（自测断言用）。 */
    bool             m_ready;       /**< build 已完成且控件指针有效。 */
    bool             m_updating;    /**< 程序性 setChecked/预置期间抑制联动。 */

    /* ---- 配置区控件 ---- */
    XCheckBox* m_srvToggle;   /**< 远程服务开关（启动/停止）。 */
    XLineEdit* m_portEdit;    /**< 监听端口（可编辑）。 */
    XComboBox* m_profileCombo;/**< 档位 performance|resource。 */
    XCheckBox* m_tlsCheck;    /**< TLS 开关。 */
    XLineEdit* m_certEdit;    /**< 证书路径。 */
    XLineEdit* m_keyEdit;     /**< 私钥路径。 */
    XComboBox* m_authCombo;   /**< 认证：0=无 1=SHA256 挑战应答。 */
    XLineEdit* m_passwordEdit;/**< 认证口令（密码回显）。 */
    XPushButton* m_applyBtn;  /**< 应用配置（监听中=重启监听生效）。 */

    /* ---- 反馈区控件 ---- */
    XLabel*    m_stateLabel;  /**< 单行服务状态（监听中 :端口 / 已停止）。 */
    XLabel*    m_addrLabel;   /**< 服务地址行（服务地址: <本机IP>:<端口>;
                               *   2026-10-02 增强）。 */
    XLabel*    m_pendingLabel;/**< 待生效提示行。 */
    XLabel*    m_sessionLabel;/**< 会话状态多行（监听/会话数/事件/错误）。 */

    /* ---- 输入回显（联调断言辅助; 2026-10-02 增强） ---- */
    XLineEdit* m_echoEdit;    /**< 输入回显框: 每次内容变化同步窗口标题,
                               *   客户端窗口标题随 FB_META 跟随供断言。 */
    char       m_baseTitle[96];/**< 标题基串(回显前缀, 防空回显清题)。 */

    /* ---- 联调断言辅助 ---- */
    XPushButton* m_pingBtn;   /**< RemotePing 按钮（本地点击与远程注入同路）。 */
    XLabel*      m_pingLabel; /**< RemotePing 计数显示。 */
    int          m_pingCount; /**< RemotePing 计数。 */

    /* ---- 服务与运行态 ---- */
    XGuiServer* m_server;     /**< 服务对象（父挂主窗口顶层; 惰性 host）。 */
    XWidget*    m_topLevel;   /**< 主窗口顶层（借用; build 时的 parent）。 */
    int         m_sessions;   /**< 最近一次信号刷新的会话数。 */
    char        m_lastEvent[96];  /**< 最近事件文本。 */
    char        m_lastError[96];  /**< 最近错误文本。 */
} s_rs;

/* ==================== 内部辅助 ==================== */

/** @brief 向主窗口状态栏反馈并登记最近文本（autotest 断言用）。 */
static void rs_report(const char* text)
{
    if (!text) return;
    snprintf(s_rs.m_lastStatus, sizeof(s_rs.m_lastStatus), "%s", text);
    if (s_rs.m_status)
        s_rs.m_status(s_rs.m_user, text);
}

/** @brief 读取端口编辑框数值（非法/空 → 0；0=listen 由系统分配）。 */
static int rs_portFromEdit(void)
{
    if (!s_rs.m_portEdit) return 0;
    return atoi(XLineEdit_text(s_rs.m_portEdit));
}

/** @brief 档位组合框当前值 → 档位 id。 */
static XGuiRemoteProfileId rs_profileFromCombo(void)
{
    int idx = s_rs.m_profileCombo ? XComboBox_currentIndex(s_rs.m_profileCombo) : 0;
    return idx == 1 ? XGUI_REMOTE_PROFILE_RESOURCE
                    : XGUI_REMOTE_PROFILE_PERFORMANCE;
}

static const char* rs_profileName(XGuiRemoteProfileId id)
{
    return id == XGUI_REMOTE_PROFILE_RESOURCE ? "resource" : "performance";
}

/** @brief 认证组合框当前值 → 认证方法。 */
static XGuiRemoteAuthMethod rs_authFromCombo(void)
{
    int idx = s_rs.m_authCombo ? XComboBox_currentIndex(s_rs.m_authCombo) : 0;
    return idx == 1 ? XGUI_REMOTE_AUTH_SHA256_CHALLENGE
                    : XGUI_REMOTE_AUTH_NONE;
}

static const char* rs_authName(XGuiRemoteAuthMethod m)
{
    return m == XGUI_REMOTE_AUTH_SHA256_CHALLENGE ? "sha256" : "none";
}

/** @brief 会话状态多行区整体刷新（监听/会话数/事件/错误/待生效）。 */
static void rs_refreshSessionLabel(void)
{
    char buf[512];
    XGuiServer* srv = s_rs.m_server;
    if (!s_rs.m_sessionLabel) return;
    if (!srv) {
        snprintf(buf, sizeof(buf),
                 "服务状态: 未创建\n会话数: 0\n最近事件: %s\n最近错误: %s",
                 s_rs.m_lastEvent, s_rs.m_lastError);
    }
    else if (XGuiServer_isListening(srv)) {
        snprintf(buf, sizeof(buf),
                 "服务状态: 监听中 :%u\n会话数: %d\n"
                 "TLS: %s | 证书: %s\n认证: %s | 口令: %s\n"
                 "档位: %s\n最近事件: %s\n最近错误: %s",
                 (unsigned)XGuiServer_serverPort(srv),
                 XGuiServer_sessionCount(srv),
                 XGuiServer_tlsEnabled(srv) ? "开" : "关",
                 XGuiServer_tlsCertFile(srv) ? XGuiServer_tlsCertFile(srv)
                                             : "(未设)",
                 rs_authName(XGuiServer_authMethod(srv)),
                 XGuiServer_hasPassword(srv) ? "已设" : "未设",
                 rs_profileName(XGuiServer_profileId(srv)),
                 s_rs.m_lastEvent, s_rs.m_lastError);
    }
    else {
        snprintf(buf, sizeof(buf),
                 "服务状态: 已停止\n会话数: %d\n最近事件: %s\n最近错误: %s",
                 XGuiServer_sessionCount(srv),
                 s_rs.m_lastEvent, s_rs.m_lastError);
    }
    XLabel_setText_2(s_rs.m_sessionLabel, buf);
}

/** @brief 待生效提示行刷新（监听中且配置有改动 → 显式提示）。 */
static void rs_refreshPendingLabel(void)
{
    char buf[160];
    if (!s_rs.m_pendingLabel) return;
    if (s_rs.m_server && XGuiServer_isListening(s_rs.m_server)) {
        int stagedPort = rs_portFromEdit();
        bool pending = false;
        /* 逐项与已定版策略比对（端口/TLS/证书/认证/口令）——口令原文
           不留存，仅以「已设口令且编辑框非空」口径近似。 */
        if (stagedPort != (int)XGuiServer_serverPort(s_rs.m_server))
            pending = true;
        if ((s_rs.m_tlsCheck &&
             XAbstractButton_isChecked((const XAbstractButton*)s_rs.m_tlsCheck)) !=
            XGuiServer_tlsEnabled(s_rs.m_server))
            pending = true;
        {
            const char* cert = s_rs.m_certEdit ? XLineEdit_text(s_rs.m_certEdit) : "";
            const char* key = s_rs.m_keyEdit ? XLineEdit_text(s_rs.m_keyEdit) : "";
            const char* actCert = XGuiServer_tlsCertFile(s_rs.m_server);
            const char* actKey = XGuiServer_tlsKeyFile(s_rs.m_server);
            if (strcmp(cert, actCert ? actCert : "") != 0 ||
                strcmp(key, actKey ? actKey : "") != 0)
                pending = true;
        }
        if (rs_authFromCombo() != XGuiServer_authMethod(s_rs.m_server))
            pending = true;
        if (s_rs.m_passwordEdit && XLineEdit_text(s_rs.m_passwordEdit)[0] &&
            !XGuiServer_hasPassword(s_rs.m_server))
            pending = true;
        if (pending)
            snprintf(buf, sizeof(buf),
                     "\xE2\x97\x8F 配置已修改, 待「应用配置」重启监听后生效");
        else
            buf[0] = '\0';
    }
    else {
        /* 未监听：改动将在下次启动时直接生效，无需提示。 */
        buf[0] = '\0';
    }
    XLabel_setText_2(s_rs.m_pendingLabel, buf);
}

/** @brief 单行服务状态刷新。 */
static void rs_refreshStateLabel(void)
{
    char buf[96];
    if (!s_rs.m_stateLabel) return;
    if (s_rs.m_server && XGuiServer_isListening(s_rs.m_server))
        snprintf(buf, sizeof(buf), "监听中 :%u",
                 (unsigned)XGuiServer_serverPort(s_rs.m_server));
    else
        snprintf(buf, sizeof(buf), "已停止");
    XLabel_setText_2(s_rs.m_stateLabel, buf);
}

/** @brief 服务地址行刷新（2026-10-02 增强: 「服务地址: <本机IP>:<端口>」）。
 *  @details 本机 IP 走仓内现有 XNetworkInterface API（getifaddrs 封装）;
 *           多地址时全部列出供选择; 端口或地址变更时重调（端口取编辑
 *           框现值, 监听中显示系统分配实际端口）。 */
static void rs_refreshAddrLabel(void)
{
    char addrs[256];
    char buf[384];
    int port;
    if (!s_rs.m_addrLabel) return;
    port = (s_rs.m_server && XGuiServer_isListening(s_rs.m_server))
               ? (int)XGuiServer_serverPort(s_rs.m_server)
               : rs_portFromEdit();
    rs_collectLocalIPv4(addrs, (int)sizeof(addrs), 4);
    snprintf(buf, sizeof(buf), "服务地址: %s:%d", addrs, port);
    XLabel_setText_2(s_rs.m_addrLabel, buf);
}

/** @brief 输入回显 → 窗口标题同步（2026-10-02 增强: 联调断言辅助）。
 *  @details 每次内容变化把「远程镜像: <回显>」写入主窗口标题
 *           （XWindow_setTitle_2 → 平台窗 → 客户端 FB_META 标题跟随,
 *           供键盘往返用字符串断言）; 空回显恢复基串标题。 */
static void rs_syncEchoToTitle(void)
{
    char buf[128];
    const char* echo;
    if (!s_rs.m_topLevel || !s_rs.m_echoEdit) return;
    echo = XLineEdit_text(s_rs.m_echoEdit);
    snprintf(buf, sizeof(buf), "%s%s", s_rs.m_baseTitle, echo);
    XWidget_setWindowTitle_2(s_rs.m_topLevel, buf);
}

/** @brief 把界面当前配置应用到服务对象（监听期配置; 口令原文不留存）。 */
static bool rs_applyConfigToServer(void)
{
    XGuiServer* srv = s_rs.m_server;
    const char* cert;
    const char* key;
    if (!srv) return false;
    /* 认证: 先口令后方法（§6.8: SHA256 未设口令 listen 拒绝）。 */
    if (s_rs.m_passwordEdit && XLineEdit_text(s_rs.m_passwordEdit)[0]) {
        if (!XGuiServer_setPassword(srv, XLineEdit_text(s_rs.m_passwordEdit))) {
            rs_report("口令设置失败");
            return false;
        }
    }
    else {
        XGuiServer_clearPassword(srv);
    }
    XGuiServer_setAuthMethod(srv, rs_authFromCombo());
    /* TLS 策略与证书路径（证书成对试装载校验, 失败不应用）。 */
    XGuiServer_setTlsEnabled(srv, s_rs.m_tlsCheck &&
        XAbstractButton_isChecked((const XAbstractButton*)s_rs.m_tlsCheck));
    cert = s_rs.m_certEdit ? XLineEdit_text(s_rs.m_certEdit) : "";
    key = s_rs.m_keyEdit ? XLineEdit_text(s_rs.m_keyEdit) : "";
    if (!XGuiServer_setTlsCertificateFiles(srv, cert, key)) {
        if (cert[0] || key[0]) {
            rs_report("证书/私钥装载失败, 未应用(检查 PEM 路径)");
            return false;
        }
    }
    return true;
}

/** @brief 启动监听（host 惰性绑定 + 配置定版 + listen）。 */
static bool rs_startListening(void)
{
    XGuiServer* srv = s_rs.m_server;
    int port;
    char buf[160];
    if (!srv || !s_rs.m_topLevel) return false;
    if (XGuiServer_isListening(srv)) return true;
    if (!XGuiServer_hostedWidget(srv)) {
        /* 首次启动: 窗口后备存储已就绪(交互期/首帧后)方可绑定镜像。 */
        if (!XGuiServer_host(srv, s_rs.m_topLevel)) {
            rs_report("镜像绑定失败(需顶层+后备存储)");
            return false;
        }
    }
    if (!rs_applyConfigToServer()) return false;
    port = rs_portFromEdit();
    if (port < 0 || port > 65535) {
        rs_report("端口非法(0=系统分配, 1..65535)");
        return false;
    }
    if (!XGuiServer_listen(srv, (uint16_t)port)) {
        snprintf(buf, sizeof(buf), "监听失败(port=%d%s%s)", port,
                 rs_authFromCombo() == XGUI_REMOTE_AUTH_SHA256_CHALLENGE &&
                 !XGuiServer_hasPassword(srv)
                     ? ", SHA256 认证需先设口令" : "",
                 port != 0 ? ", 端口可能被占用" : "");
        rs_report(buf);
        XPrintf("XGuiWindowDemo: remote-server listen FAILED port=%d\n",
                port);
        return false;
    }
    snprintf(buf, sizeof(buf), "已启动: 监听 :%u (tls=%s 认证=%s) 已应用",
             (unsigned)XGuiServer_serverPort(srv),
             XGuiServer_tlsEnabled(srv) ? "on" : "off",
             rs_authName(XGuiServer_authMethod(srv)));
    rs_report(buf);
    /* 诊断走 stdout(演示 main 已置行缓冲): 供联调矩阵等待就绪。 */
    XPrintf("XGuiWindowDemo: remote-server listening port=%u tls=%s "
            "auth=%s\n", (unsigned)XGuiServer_serverPort(srv),
            XGuiServer_tlsEnabled(srv) ? "on" : "off",
            rs_authName(XGuiServer_authMethod(srv)));
    return true;
}

/** @brief 停止监听（发 BYE(SERVER_SHUTDOWN) 断全部会话）。 */
static void rs_stopListening(void)
{
    if (!s_rs.m_server || !XGuiServer_isListening(s_rs.m_server)) return;
    XGuiServer_close(s_rs.m_server);
    rs_report("已停止监听(会话已断开)");
}

/** @brief 重启监听以使暂存配置生效（close → listen, 显式反馈）。 */
static void rs_restartListening(void)
{
    char buf[160];
    uint16_t oldPort;
    if (!s_rs.m_server || !XGuiServer_isListening(s_rs.m_server)) return;
    oldPort = XGuiServer_serverPort(s_rs.m_server);
    XGuiServer_close(s_rs.m_server);
    if (rs_startListening()) {
        snprintf(buf, sizeof(buf), "已重启监听: 新配置已应用(原 :%u)",
                 (unsigned)oldPort);
        rs_report(buf);
    }
    else {
        /* 重启失败如实呈现: 已停止, 无静默降级。 */
        rs_report("重启监听失败, 服务已停止(修正配置后重新启用)");
        if (s_rs.m_srvToggle && !s_rs.m_updating) {
            s_rs.m_updating = true;
            XAbstractButton_setChecked((XAbstractButton*)s_rs.m_srvToggle,
                                       false);
            s_rs.m_updating = false;
        }
    }
}

/* ==================== 槽 ==================== */

/** @brief 服务开关 toggled：启动/停止（启动失败回弹开关）。 */
static void rs_srvToggleSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (!s_rs.m_ready || !s_rs.m_srvToggle || s_rs.m_updating) return;
    if (XAbstractButton_isChecked((const XAbstractButton*)s_rs.m_srvToggle)) {
        if (!rs_startListening()) {
            /* 启动失败: 回弹开关（抑制联动防递归）。 */
            s_rs.m_updating = true;
            XAbstractButton_setChecked((XAbstractButton*)s_rs.m_srvToggle,
                                       false);
            s_rs.m_updating = false;
        }
    }
    else {
        rs_stopListening();
    }
    rs_refreshStateLabel();
    rs_refreshAddrLabel(); /* 端口定版变化(系统分配↔编辑框值)→ 服务地址刷新。 */
    rs_refreshPendingLabel();
    rs_refreshSessionLabel();
}

/** @brief 档位 activated：运行期热切换（会话不断, 广播 FB_META）。 */
static void rs_profileSlot(XObject* sender, XVarList* args)
{
    char buf[128];
    (void)sender; (void)args;
    if (!s_rs.m_ready || !s_rs.m_server) return;
    XGuiServer_setProfileId(s_rs.m_server, rs_profileFromCombo());
    snprintf(buf, sizeof(buf), "档位已热切换 → %s (会话不断, 已广播 FB_META)",
             rs_profileName(XGuiServer_profileId(s_rs.m_server)));
    rs_report(buf);
    rs_refreshSessionLabel();
}

/** @brief 「应用配置」clicked：监听中=重启监听生效; 未监听=预检暂存。 */
static void rs_applySlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (!s_rs.m_ready) return;
    if (s_rs.m_server && XGuiServer_isListening(s_rs.m_server)) {
        rs_restartListening();
    }
    else {
        /* 未监听: 仅预检（证书试装载等）, 启动时按界面值定版。 */
        if (s_rs.m_server && rs_applyConfigToServer())
            rs_report("配置已预检通过, 启动监听后生效");
        rs_refreshPendingLabel();
    }
    rs_refreshStateLabel();
    rs_refreshAddrLabel(); /* 重启监听端口变化 → 服务地址刷新。 */
    rs_refreshSessionLabel();
}

/** @brief 配置控件变动（textEdited/toggled）：刷新待生效提示 + 服务地址。 */
static void rs_configChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (!s_rs.m_ready) return;
    rs_refreshPendingLabel();
    rs_refreshAddrLabel(); /* 端口编辑即刷新服务地址显示(任务口径)。 */
}

/** @brief 输入回显 textChanged：内容同步到主窗口标题（FB_META 跟随,
 *         供客户端窗口标题字符串断言; 2026-10-02 增强）。 */
static void rs_echoChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (!s_rs.m_ready) return;
    rs_syncEchoToTitle();
}

/** @brief RemotePing clicked：联调断言辅助——本地点击与远程注入点击
 *         同一条 clicked 路径, 计数 +1 并重绘计数标签。 */
static void rs_pingSlot(XObject* sender, XVarList* args)
{
    char buf[64];
    (void)sender; (void)args;
    if (!s_rs.m_ready) return;
    ++s_rs.m_pingCount;
    snprintf(buf, sizeof(buf), "RemotePing 计数: %d", s_rs.m_pingCount);
    if (s_rs.m_pingLabel) XLabel_setText_2(s_rs.m_pingLabel, buf);
    rs_report(buf);
}

/** @brief 信号: 会话接入。 */
static void rs_onClientConnected(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_1(args, int, sid);
        snprintf(s_rs.m_lastEvent, sizeof(s_rs.m_lastEvent),
                 "会话 #%d 已接入", sid);
        ++s_rs.m_sessions;
        rs_report(s_rs.m_lastEvent);
    }
    rs_refreshSessionLabel();
}

/** @brief 信号: 会话结束。 */
static void rs_onClientDisconnected(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_2(args, int, sid, int, reason);
        snprintf(s_rs.m_lastEvent, sizeof(s_rs.m_lastEvent),
                 "会话 #%d 断开(reason=%d)", sid, reason);
        if (s_rs.m_sessions > 0) --s_rs.m_sessions;
        rs_report(s_rs.m_lastEvent);
    }
    rs_refreshSessionLabel();
}

/** @brief 信号: 会话级错误。 */
static void rs_onSessionError(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_2(args, int, sid, int, code);
        snprintf(s_rs.m_lastError, sizeof(s_rs.m_lastError),
                 "会话 #%d 错误(code=%d)", sid, code);
        rs_report(s_rs.m_lastError);
    }
    rs_refreshSessionLabel();
}

/** @brief 信号: FB_META 已广播（尺寸/档位/标题变化）。 */
static void rs_onFbMeta(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    snprintf(s_rs.m_lastEvent, sizeof(s_rs.m_lastEvent),
             "FB_META 已广播");
    rs_refreshSessionLabel();
}

/* ==================== 构建 ==================== */

/** @brief 组装一行「标签 + 输入控件」。 */
static void rs_buildLabelAt(const char* text, int x, int y, int w, XLabel** out)
{
    XLabel* label = XLabel_create(s_rs.m_root, 0);
    if (label) {
        XLabel_setText_2(label, text);
        XWidget_setGeometry((XWidget*)label, x, y, w, 22);
        XWidget_show((XWidget*)label);
    }
    if (out) *out = label;
}

XWidget* demo_page_remote_server_build(XWidget* parent,
                                       DemoPageStatusFn status, void* user)
{
    /* demo 单实例：重复 build 前清空登记表（旧页面随旧父链析构;
     * CLI 预置 s_rsCli 不清, 由 demo_page_remote_server_cliDefaults
     * 显式重置）。 */
    memset(&s_rs, 0, sizeof(s_rs));
    s_rs.m_status = status;
    s_rs.m_user = user;
    s_rs.m_topLevel = parent; /* 主窗顶层（ext 页面注册以 m_base 为父）。 */

    if (!parent) return (XWidget*)0;
    s_rs.m_root = XWidget_create(parent, 0);
    if (!s_rs.m_root) return (XWidget*)0;

    /* ---- 标题 + 单行服务状态 ---- */
    {
        XLabel* title = NULL;
        rs_buildLabelAt("\xE8\xBF\x9C\xE7\xA8\x8B\xE7\xAA\x97\xE5\x8F\xA3"
                        "\xE6\x9C\x8D\xE5\x8A\xA1\xE8\xAE\xBE\xE7\xBD\xAE",
                        12, 8, 240, &title); /* 远程窗口服务设置 */
        if (title) XLabel_setTextPixelSize(title, 16);
    }
    s_rs.m_stateLabel = XLabel_create(s_rs.m_root, 0);
    if (!s_rs.m_stateLabel) return s_rs.m_root;
    XLabel_setText_2(s_rs.m_stateLabel, "已停止");
    XWidget_setGeometry((XWidget*)s_rs.m_stateLabel,
                        RS_PAGE_COL2_X, 10, 104, 22);
    XWidget_show((XWidget*)s_rs.m_stateLabel);

    /* ---- 服务地址行（2026-10-02 增强: 「服务地址: <本机IP>:<端口>」;
     *     2026-10-03 目验收口: 行宽按 rootW-8-480=112 会在「192.16」处
     *     截断, 右列 y72 无邻位冲突, 行原点收至 x=160 挪出 424px 宽,
     *     800x600(导航展开根宽 600)与更窄窗口均完整可见。 ---- */
    s_rs.m_addrLabel = XLabel_create(s_rs.m_root, 0);
    if (!s_rs.m_addrLabel) return s_rs.m_root;
    XLabel_setText_2(s_rs.m_addrLabel, "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x9C\xB0"
                     "\xE5\x9D\x80: -"); /* 服务地址: - */
    /* 摆位=端口(12..146)/档位(170..348)同行右侧空段（360..592）：
     * 首版收口把地址行挪到 (160,72) 与档位下拉框矩形相交（目验二轮
     * 实证叠印），改贴行内空段，右缘 592=rootW-8 恰收口。 */
    XWidget_setGeometry((XWidget*)s_rs.m_addrLabel,
                        360, 72, 232, 24);
    XWidget_show((XWidget*)s_rs.m_addrLabel);

    /* ---- 远程服务开关（启动/停止主控） ---- */
    s_rs.m_srvToggle = XCheckBox_create(s_rs.m_root, 0);
    if (!s_rs.m_srvToggle) return s_rs.m_root;
    XAbstractButton_setText_2((XAbstractButton*)s_rs.m_srvToggle,
                              "\xE5\x90\xAF\xE7\x94\xA8\xE8\xBF\x9C"
                              "\xE7\xA8\x8B\xE6\x9C\x8D\xE5\x8A\xA1"
                              "\xE5\x99\xA8"); /* 启用远程服务器 */
    XWidget_setGeometry((XWidget*)s_rs.m_srvToggle, 12, 40, 200, 24);
    XObject_connect_2((XObject*)s_rs.m_srvToggle,
                      XSignal(XAbstractButton_toggled_signal),
                      rs_srvToggleSlot);
    XWidget_show((XWidget*)s_rs.m_srvToggle);

    /* ---- 待生效提示行（显式生效时机反馈; 2026-10-03 收口: 列宽按
     *     rootW-8=592 反推 480..592=112, 全文 ~344px 于更窄窗口内随
     *     列宽裁切但组件不越窗。 ---- */
    s_rs.m_pendingLabel = XLabel_create(s_rs.m_root, 0);
    if (!s_rs.m_pendingLabel) return s_rs.m_root;
    XWidget_setGeometry((XWidget*)s_rs.m_pendingLabel,
                        RS_PAGE_COL2_X, 40, 112, 22);
    XWidget_show((XWidget*)s_rs.m_pendingLabel);

    /* ---- 端口 / 档位 ---- */
    rs_buildLabelAt("\xE7\xAB\xAF\xE5\x8F\xA3", 12, 74, 40, NULL); /* 端口 */
    s_rs.m_portEdit = XLineEdit_create(s_rs.m_root, 0);
    if (!s_rs.m_portEdit) return s_rs.m_root;
    XLineEdit_setText(s_rs.m_portEdit, "46000");
    XWidget_setGeometry((XWidget*)s_rs.m_portEdit, 56, 72, 90, 24);
    /* textChanged(非 textEdited): 虚拟键盘提交走 setText 不发 textEdited
     * (用户编辑语义区分), 服务地址行刷新须两条路径都覆盖; 刷新幂等,
     * 程序性 setText(autotest/CLI 预置)多刷一次无害。 */
    XObject_connect_2((XObject*)s_rs.m_portEdit,
                      XSignal(XLineEdit_textChanged_signal),
                      rs_configChangedSlot);
    XWidget_show((XWidget*)s_rs.m_portEdit);

    rs_buildLabelAt("\xE6\xA1\xA3\xE4\xBD\x8D", 170, 74, 40, NULL); /* 档位 */
    s_rs.m_profileCombo = XComboBox_create(s_rs.m_root, 0);
    if (!s_rs.m_profileCombo) return s_rs.m_root;
    XComboBox_addItem_2(s_rs.m_profileCombo, "performance");
    XComboBox_addItem_2(s_rs.m_profileCombo, "resource");
    XComboBox_setCurrentIndex(s_rs.m_profileCombo, 0);
    XWidget_setGeometry((XWidget*)s_rs.m_profileCombo, 214, 72, 134, 24);
    XObject_connect_2((XObject*)s_rs.m_profileCombo,
                      XSignal(XComboBox_activated_signal), rs_profileSlot);
    XWidget_show((XWidget*)s_rs.m_profileCombo);

    /* ---- TLS 开关 + 证书/私钥路径（2026-10-03 收口: 证书 56..348 与
     *     私钥 372..584 成对收窄, 行右缘=584 不越 rootW-8=592 —— 800x600
     *     导航展开根宽 600 时私钥框 414+300=714 右边框被窗口右缘裁切
     *     的根修; 更窄窗口同行收窄口径。 ---- */
    s_rs.m_tlsCheck = XCheckBox_create(s_rs.m_root, 0);
    if (!s_rs.m_tlsCheck) return s_rs.m_root;
    XAbstractButton_setText_2((XAbstractButton*)s_rs.m_tlsCheck,
                              "TLS \xE5\x8A\xA0\xE5\xAF\x86"); /* TLS 加密 */
    XWidget_setGeometry((XWidget*)s_rs.m_tlsCheck, 12, 104, 220, 24);
    XObject_connect_2((XObject*)s_rs.m_tlsCheck,
                      XSignal(XAbstractButton_toggled_signal),
                      rs_configChangedSlot);
    XWidget_show((XWidget*)s_rs.m_tlsCheck);

    rs_buildLabelAt("\xE8\xAF\x81\xE4\xB9\xA6", 12, 134, 40, NULL); /* 证书 */
    s_rs.m_certEdit = XLineEdit_create(s_rs.m_root, 0);
    if (!s_rs.m_certEdit) return s_rs.m_root;
    XWidget_setGeometry((XWidget*)s_rs.m_certEdit, 56, 132, 292, 24);
    XObject_connect_2((XObject*)s_rs.m_certEdit,
                      XSignal(XLineEdit_textEdited_signal),
                      rs_configChangedSlot);
    XWidget_show((XWidget*)s_rs.m_certEdit);

    rs_buildLabelAt("\xE7\xA7\x81\xE9\x92\xA5", 356, 134, 40, NULL); /* 私钥 */
    s_rs.m_keyEdit = XLineEdit_create(s_rs.m_root, 0);
    if (!s_rs.m_keyEdit) return s_rs.m_root;
    XWidget_setGeometry((XWidget*)s_rs.m_keyEdit, 400, 132, 184, 24);
    XObject_connect_2((XObject*)s_rs.m_keyEdit,
                      XSignal(XLineEdit_textEdited_signal),
                      rs_configChangedSlot);
    XWidget_show((XWidget*)s_rs.m_keyEdit);

    /* ---- 认证方式 + 口令 ---- */
    rs_buildLabelAt("\xE8\xAE\xA4\xE8\xAF\x81", 12, 164, 40, NULL); /* 认证 */
    s_rs.m_authCombo = XComboBox_create(s_rs.m_root, 0);
    if (!s_rs.m_authCombo) return s_rs.m_root;
    XComboBox_addItem_2(s_rs.m_authCombo, "none");
    XComboBox_addItem_2(s_rs.m_authCombo, "sha256");
    XComboBox_setCurrentIndex(s_rs.m_authCombo, 0);
    XWidget_setGeometry((XWidget*)s_rs.m_authCombo, 56, 162, 150, 24);
    XObject_connect_2((XObject*)s_rs.m_authCombo,
                      XSignal(XComboBox_activated_signal),
                      rs_configChangedSlot);
    XWidget_show((XWidget*)s_rs.m_authCombo);

    rs_buildLabelAt("\xE5\x8F\xA3\xE4\xBB\xA4", 220, 164, 40, NULL); /* 口令 */
    s_rs.m_passwordEdit = XLineEdit_create(s_rs.m_root, 0);
    if (!s_rs.m_passwordEdit) return s_rs.m_root;
    XLineEdit_setEchoMode(s_rs.m_passwordEdit, XLineEditEchoMode_Password);
    XWidget_setGeometry((XWidget*)s_rs.m_passwordEdit, 264, 162, 188, 24);
    XObject_connect_2((XObject*)s_rs.m_passwordEdit,
                      XSignal(XLineEdit_textEdited_signal),
                      rs_configChangedSlot);
    XWidget_show((XWidget*)s_rs.m_passwordEdit);

    /* ---- 「应用配置」（2026-10-03 收口: 按钮右缘=rootW-8, 800x600
     *     导航展开根宽 600 时原 480+200=680 越缘, 全角文案「应用配置」
     *     被截为「应用配」——收窄为 104 满贴文案(4 汉字×16+8 padding)。 ---- */
    s_rs.m_applyBtn = XPushButton_create(s_rs.m_root, 0);
    if (!s_rs.m_applyBtn) return s_rs.m_root;
    XPushButton_setText_2(s_rs.m_applyBtn,
                          "\xE5\xBA\x94\xE7\x94\xA8\xE9\x85\x8D\xE7\xBD"
                          "\xAE"); /* 应用配置 */
    XWidget_setGeometry((XWidget*)s_rs.m_applyBtn,
                        RS_PAGE_COL2_X, 160, 104, 28);
    XObject_connect_2((XObject*)s_rs.m_applyBtn,
                      XSignal(XAbstractButton_clicked_signal), rs_applySlot);
    XWidget_show((XWidget*)s_rs.m_applyBtn);

    /* ---- RemotePing 联调断言辅助（本地点击与远程注入点击同路 +1） ---- */
    s_rs.m_pingBtn = XPushButton_create(s_rs.m_root, 0);
    if (!s_rs.m_pingBtn) return s_rs.m_root;
    XPushButton_setText_2(s_rs.m_pingBtn, "RemotePing");
    XWidget_setGeometry((XWidget*)s_rs.m_pingBtn, 12, 200, 180, 30);
    XObject_connect_2((XObject*)s_rs.m_pingBtn,
                      XSignal(XAbstractButton_clicked_signal), rs_pingSlot);
    XWidget_show((XWidget*)s_rs.m_pingBtn);

    s_rs.m_pingLabel = XLabel_create(s_rs.m_root, 0);
    if (!s_rs.m_pingLabel) return s_rs.m_root;
    XLabel_setText_2(s_rs.m_pingLabel, "RemotePing \xE8\xAE\xA1\xE6\x95"
                     "\xB0: 0"); /* RemotePing 计数: 0 */
    XWidget_setGeometry((XWidget*)s_rs.m_pingLabel, 204, 204, 244, 22);
    XWidget_show((XWidget*)s_rs.m_pingLabel);

    /* ---- 输入回显（联调断言辅助; 2026-10-02 增强）: 每次内容变化同步
     *     主窗口标题——客户端窗口标题随 FB_META 跟随, 供键盘往返用
     *     字符串断言。 ---- */
    rs_buildLabelAt("\xE5\x9B\x9E\xE6\x98\xBE", 12, 242, 40, NULL); /* 回显 */
    s_rs.m_echoEdit = XLineEdit_create(s_rs.m_root, 0);
    if (!s_rs.m_echoEdit) return s_rs.m_root;
    XWidget_setGeometry((XWidget*)s_rs.m_echoEdit, 56, 240, 300, 24);
    XObject_connect_2((XObject*)s_rs.m_echoEdit,
                      XSignal(XLineEdit_textChanged_signal),
                      rs_echoChangedSlot);
    XWidget_show((XWidget*)s_rs.m_echoEdit);

    /* ---- 会话状态多行区（2026-10-03 收口: 宽随行右缘=584 收窄。 ---- */
    s_rs.m_sessionLabel = XLabel_create(s_rs.m_root, 0);
    if (!s_rs.m_sessionLabel) return s_rs.m_root;
    XLabel_setText_2(s_rs.m_sessionLabel,
                     "\xE6\x9C\x8D\xE5\x8A\xA1\xE7\x8A\xB6\xE6\x80\x81: "
                     "\xE6\x9C\xAA\xE5\x90\xAF\xE5\x8A\xA8"); /* 服务状态: 未启动 */
    XLabel_setAlignment(s_rs.m_sessionLabel,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setGeometry((XWidget*)s_rs.m_sessionLabel, 12, 274, 572, 150);
    XWidget_show((XWidget*)s_rs.m_sessionLabel);

    /* ---- 服务对象: 父挂主窗口顶层（对象树级联析构兜底）, 惰性 host。 */
    s_rs.m_server = XGuiServer_create((XObject*)parent);
    if (s_rs.m_server) {
        XObject_connect_2((XObject*)s_rs.m_server,
                          XSignal(XGuiServer_clientConnected_signal),
                          rs_onClientConnected);
        XObject_connect_2((XObject*)s_rs.m_server,
                          XSignal(XGuiServer_clientDisconnected_signal),
                          rs_onClientDisconnected);
        XObject_connect_2((XObject*)s_rs.m_server,
                          XSignal(XGuiServer_sessionError_signal),
                          rs_onSessionError);
        XObject_connect_2((XObject*)s_rs.m_server,
                          XSignal(XGuiServer_fbMetaChanged_signal),
                          rs_onFbMeta);
        /* 悬浮条切档(§5.3 PROFILE_SET 协商)须服务端显式放行:
         * allowClientProfile 私有块默认 false, 缺省即协商必拒——演示
         * 服务器页此前未启用, 悬浮条「档位」入口在演示拓扑下永远
         * 走不通; 与 XGuiRemoteTest 悬浮条切档用例同口径放行
         * (2026-10-03 互联复验修复, 归因: 演示页配置遗漏)。 */
        XGuiServer_setAllowClientProfile(s_rs.m_server, true);
    }

    /* ---- CLI 初始默认值预置（UI 主控; 仅摆界面状态, 不在此监听——
     *     host 需要窗口后备存储就绪, --remote-server 经首帧 autostart
     *     钩子走同一条 UI 启动路径）。 */
    s_rs.m_updating = true;
    if (s_rsCli.port > 0 && s_rsCli.port <= 65535) {
        char portText[16];
        snprintf(portText, sizeof(portText), "%d", s_rsCli.port);
        XLineEdit_setText(s_rs.m_portEdit, portText);
    }
    if (s_rsCli.profile && strcmp(s_rsCli.profile, "resource") == 0)
        XComboBox_setCurrentIndex(s_rs.m_profileCombo, 1);
    if (s_rsCli.tls)
        XAbstractButton_setChecked((XAbstractButton*)s_rs.m_tlsCheck, true);
    if (s_rsCli.cert && s_rsCli.cert[0])
        XLineEdit_setText(s_rs.m_certEdit, s_rsCli.cert);
    if (s_rsCli.key && s_rsCli.key[0])
        XLineEdit_setText(s_rs.m_keyEdit, s_rsCli.key);
    if (s_rsCli.auth && strcmp(s_rsCli.auth, "sha256") == 0)
        XComboBox_setCurrentIndex(s_rs.m_authCombo, 1);
    if (s_rsCli.password && s_rsCli.password[0])
        XLineEdit_setText(s_rs.m_passwordEdit, s_rsCli.password);
    if (s_rsCli.enabled)
        XAbstractButton_setChecked((XAbstractButton*)s_rs.m_srvToggle, true);
    s_rs.m_updating = false;

    /* 档位初值即时生效（热切换语义; 未监听=服务端默认档）。 */
    if (s_rs.m_server)
        XGuiServer_setProfileId(s_rs.m_server, rs_profileFromCombo());

    /* 服务地址行初值 + 标题基串登记（回显标题=基串+回显内容;
     * 2026-10-02 增强）。 */
    snprintf(s_rs.m_baseTitle, sizeof(s_rs.m_baseTitle),
             "XinYueC \xE6\x8E\xA7\xE4\xBB\xB6\xE5\x8F\xAF\xE8\xA7\x86"
             "\xE5\x8C\x96\xE6\xB5\x8B\xE8\xAF\x95"); /* 同主窗默认题。 */
    s_rs.m_ready = true;
    rs_refreshStateLabel();
    rs_refreshAddrLabel();
    rs_refreshSessionLabel();
    return s_rs.m_root;
}

/* ==================== 首帧自动启动（--remote-server 预置） ============= */

void demo_page_remote_server_autostart(void)
{
    static int s_retry = 0;
    if (!s_rs.m_ready || !s_rsCli.enabled) return; /* 未预置: 零开销。 */
    if (s_rs.m_server && XGuiServer_isListening(s_rs.m_server)) {
        s_rsCli.enabled = false; /* 已启动: 终态, 后续调用零开销。 */
        rs_refreshAddrLabel(); /* 系统分配实际端口 → 服务地址行终值。 */
        return;
    }
    if (++s_retry > 600) {
        /* ~10s(60fps 口径)仍无法绑定: 如实报告并放弃（不静默）。 */
        rs_report("远程服务自动启动失败(镜像绑定超时)");
        s_rsCli.enabled = false;
        return;
    }
    /* 后备存储未就绪时 host 失败, 下一帧重试; 成功后开关回弹由
     * rs_startListening 失败路径负责（开关已在预置时置真）。 */
    if (XAbstractButton_isChecked((const XAbstractButton*)s_rs.m_srvToggle))
        rs_startListening();
}

/* ==================== 退出清理 ==================== */

void demo_page_remote_server_shutdown(void)
{
    if (!s_rs.m_server) return;
    XGuiServer_close(s_rs.m_server);          /* 断会话+停监听。 */
    XGuiServer_unhost(s_rs.m_server);         /* 恢复 present 登记。 */
    XGuiServer_deleteLater((XObject*)s_rs.m_server); /* 仓库约定延迟释放;
                                             * 窗口级联析构先行时,
                                             * XObject_deinit 清挂起事件
                                             * (XObject.c:583), 两序皆安全。 */
    s_rs.m_server = NULL;
}

/* ==================== autotest ==================== */

/** @brief 远程窗口设置页自测（非阻塞; 端口 0 演练, 结束恢复未监听）。 */
int demo_page_remote_server_autotest(XWidget* page)
{
    int failures = 0;
    char portBuf[16];
#define RS_EXPECT(cond, what) \
    do { if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
         else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } } while (0)

    if (!page || !s_rs.m_ready || page != s_rs.m_root)
        return -1;

    /* ---- 控件登记完整 ---- */
    RS_EXPECT(s_rs.m_srvToggle && s_rs.m_portEdit && s_rs.m_profileCombo &&
              s_rs.m_tlsCheck && s_rs.m_certEdit && s_rs.m_keyEdit &&
              s_rs.m_authCombo && s_rs.m_passwordEdit && s_rs.m_applyBtn &&
              s_rs.m_pingBtn && s_rs.m_pingLabel && s_rs.m_sessionLabel &&
              s_rs.m_addrLabel && s_rs.m_echoEdit,
              "远程窗口页: 配置/反馈控件全部登记");

    /* ---- 服务地址行（2026-10-02 增强）: 显示含端口且非空 ---- */
    {
        char addrs[256];
        char expect[320];
        rs_collectLocalIPv4(addrs, (int)sizeof(addrs), 4);
        snprintf(expect, sizeof(expect), "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x9C\xB0"
                 "\xE5\x9D\x80: %s:46000", addrs); /* 服务地址: <ip>:46000 */
        RS_EXPECT(strstr(XString_toUtf8(XLabel_text(s_rs.m_addrLabel)), expect) != NULL ||
                  strstr(XString_toUtf8(XLabel_text(s_rs.m_addrLabel)),
                         "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x9C\xB0\xE5\x9D\x80: "
                         "none") != NULL,
                  "远程窗口页: 服务地址行含本机IP:端口(或 none 回退)");
    }

    /* ---- 输入回显 → 窗口标题（2026-10-02 增强）: 真实 textChanged 路径 ---- */
    {
        const XString* before = XWidget_windowTitle(s_rs.m_topLevel);
        (void)before;
        XLineEdit_setText(s_rs.m_echoEdit, "ECHO-PROBE-01");
        RS_EXPECT(strstr(XString_toUtf8(XWidget_windowTitle(s_rs.m_topLevel)),
                         "ECHO-PROBE-01") != NULL,
                  "远程窗口页: 回显内容同步窗口标题(FB_META 跟随供断言)");
        XLineEdit_setText(s_rs.m_echoEdit, "");
        RS_EXPECT(strcmp(XString_toUtf8(XWidget_windowTitle(s_rs.m_topLevel)),
                         s_rs.m_baseTitle) == 0,
                  "远程窗口页: 空回显恢复基串标题");
    }

    /* ---- 初始: 未监听, 默认端口显示 ---- */
    RS_EXPECT(s_rs.m_server && !XGuiServer_isListening(s_rs.m_server),
              "远程窗口页: 初始未监听");
    RS_EXPECT(strcmp(XLineEdit_text(s_rs.m_portEdit), "46000") == 0,
              "远程窗口页: 默认端口 46000");

    /* ---- RemotePing 联调断言辅助: 计数 +1 与反馈登记 ---- */
    {
        int before = s_rs.m_pingCount;
        rs_pingSlot(NULL, NULL);
        RS_EXPECT(s_rs.m_pingCount == before + 1,
                  "远程窗口页: RemotePing 点击计数 +1");
        RS_EXPECT(strstr(s_rs.m_lastStatus, "RemotePing") != NULL,
                  "远程窗口页: RemotePing 反馈文本已更新");
    }

    /* ---- 档位热切换: resource → 服务端档位即时更新 → 切回 ---- */
    XComboBox_setCurrentIndex(s_rs.m_profileCombo, 1);
    rs_profileSlot(NULL, NULL);
    RS_EXPECT(s_rs.m_server &&
              XGuiServer_profileId(s_rs.m_server) ==
                  XGUI_REMOTE_PROFILE_RESOURCE,
              "远程窗口页: 档位热切换 resource 生效");
    XComboBox_setCurrentIndex(s_rs.m_profileCombo, 0);
    rs_profileSlot(NULL, NULL);
    RS_EXPECT(s_rs.m_server &&
              XGuiServer_profileId(s_rs.m_server) ==
                  XGUI_REMOTE_PROFILE_PERFORMANCE,
              "远程窗口页: 档位热切换切回 performance");

    /* ---- 证书试装载校验: 坏路径必须失败（不静默应用） ---- */
    RS_EXPECT(!XGuiServer_setTlsCertificateFiles(
                  s_rs.m_server, "/nonexistent/rs_cert.pem",
                  "/nonexistent/rs_key.pem"),
              "远程窗口页: 证书坏路径 setTlsCertificateFiles 拒绝");

    /* ---- 监听演练(端口 0=系统分配): 开关 ON 经真实 toggled 信号启动 ---- */
    snprintf(portBuf, sizeof(portBuf), "0");
    XLineEdit_setText(s_rs.m_portEdit, portBuf);
    XAbstractButton_setChecked((XAbstractButton*)s_rs.m_srvToggle, true);
    RS_EXPECT(XGuiServer_isListening(s_rs.m_server),
              "远程窗口页: 开关 ON 启动监听(端口 0)");
    RS_EXPECT(XGuiServer_serverPort(s_rs.m_server) != 0 &&
              XGuiServer_serverPort(s_rs.m_server) <= 65535,
              "远程窗口页: 系统分配实际端口可读回");
    RS_EXPECT(strstr(s_rs.m_lastStatus,
                     "\xE5\xB7\xB2\xE5\x90\xAF\xE5\x8A\xA8") != NULL, /* 已启动 */
              "远程窗口页: 启动反馈「已启动」");

    /* ---- SHA256 未设口令契约: listen 拒绝(不静默) ---- */
    XComboBox_setCurrentIndex(s_rs.m_authCombo, 1);
    XLineEdit_setText(s_rs.m_passwordEdit, "");
    rs_applySlot(NULL, NULL); /* 监听中 → 重启监听 → 应失败并回弹开关。 */
    RS_EXPECT(!XGuiServer_isListening(s_rs.m_server),
              "远程窗口页: SHA256 无口令重启监听被拒(服务停止)");

    /* ---- 收尾: 恢复未监听 + 默认端口显示 ---- */
    XComboBox_setCurrentIndex(s_rs.m_authCombo, 0);
    XLineEdit_setText(s_rs.m_portEdit, "46000");
    rs_refreshStateLabel();
    rs_refreshPendingLabel();
    rs_refreshSessionLabel();
    RS_EXPECT(!XGuiServer_isListening(s_rs.m_server),
              "远程窗口页: 结束态未监听(不占端口)");

#undef RS_EXPECT
    XPrintf("XGuiAutoTest: 远程窗口设置页 %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}

#else /* 裁剪: 契约桩（符号常在, 主文件可无条件链接） */

XWidget* demo_page_remote_server_build(XWidget* parent,
                                       DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    XPrintf("XGuiAutoTest: [FAIL] 远程窗口页依赖 XGuiRemote/控件被裁剪，"
            "无法构建\n");
    return (XWidget*)0;
}

int demo_page_remote_server_autotest(XWidget* page)
{
    (void)page;
    XPrintf("XGuiAutoTest: [FAIL] 远程窗口页被裁剪，无法自测\n");
    return -1;
}

#endif /* XGUI_REMOTE_ON && 控件段 */
