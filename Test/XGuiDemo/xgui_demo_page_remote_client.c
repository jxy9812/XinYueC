/******************************************************************************
 * @file       xgui_demo_page_remote_client.c
 * @brief      XGuiWindowDemo「远程客户端」设置页（XGuiClient 远程连接的
 *             UI 主控页, 与远程服务器页对偶）。
 * @details    契约见 xgui_demo_pages.h（demo_page_remote_client_build /
 *             demo_page_remote_client_autotest）与
 *             xgui_demo_page_remote_client.h（CLI 预置/autostart/shutdown）。
 *
 *             页面内容（2026-10-03 RS-d 结构性改造: 左视图/右控制列——
 *             镜像视图进页面容器布局, 本地控件全部可达; 旧版全页镜像
 *             盖死设置控件的缺陷见 build/xgui-remote-live/run_pairs.sh
 *             RS-d 阻塞记录）：
 *              - 视图区（页内 (0,0) 504x458, 普通容器控件）: XGuiClient
 *                1:1 镜像视图宿主。FB_META 后客户端 setFixedSize(远端
 *                尺寸)（冻结语义保留）, 超出宿主的部分经绘制裁剪
 *                （XWidget_paintTree 祖先矩形交集）与命中隔离
 *                （XWidget_childAt 矩形链）收窄到宿主矩形——不覆盖任何
 *                本地控件; 断开后把视图复位为宿主尺寸, 占位文本
 *                （「未连接远端服务器」）在视图区可见;
 *              - 控制列（页内 x=516..768）: IP/端口/连接/断开/档位/TLS/
 *                认证/口令/「模拟触摸」/实时统计, 全部本地可达; 口令经
 *                XGuiClient_setAccessPassword/clearAccessPassword 接入
 *                连接流程（2026-10-04 访问口令: 空输入=显式清除, 连接
 *                自动适配服务端挑战要求, 要求认证而无口令快速失败）;
 *              - 悬浮会话工具条（XRemoteSessionBar, 页根子控件吸附视图
 *                区顶部居中）: 连接期浮现, 断开即隐藏——「连上后怎么
 *                退出」的 UI 入口（断开不杀进程不退出应用）; 展开态
 *                带「键盘」钮（屏幕键盘显式弹收唯一入口）;
 *              - 「键盘」接管（RC 页全权接管, 2026-10-03）: build 末尾
 *                关断应用虚拟键盘单例 autoPopup——RC 页触摸点击不再
 *                自动弹屏幕键盘, 显式弹收收归悬浮条「键盘」钮;
 *              - 「模拟触摸」按钮：程序化发一轮 INPUT_TOUCH 按下-移动-
 *                抬起序列经协议转发远端（XGuiClient_sendTouchFrame 直发,
 *                覆盖触摸路径; 目标点=服务器页 RemotePing 按钮中心
 *                （页内 (102,215) → 顶层 (114,323)）——2026-10-03 由
 *                「远端画面中心」重定靶: 中心处无可响应控件, 联调矩阵
 *                需要触摸回显可断言（RemotePing 计数 +1）。
 *
 *             生效时机（显式）：
 *              - 档位 = 连接中经 requestProfileId PROFILE_SET 协商
 *                （结果以远端 FB_META 或会话维持原档为准）；未连接仅
 *                更新界面值, 下次连接握手后按需请求；
 *              - IP/端口/TLS/认证/口令 = 连接期配置：改动即下次「连接」
 *                按下时定版（连接中改动不热切, 状态行提示需断开重连）。
 *
 *             生命周期：XGuiClient 父挂 build 传入的主窗口顶层（对象树
 *             级联析构兜底）；退出经 demo_page_remote_client_shutdown()
 *             断链 + deleteLater（XObject 析构清挂起 deferred-delete,
 *             两序皆安全）。
 *
 *             autotest 口径：全程非阻塞（直呼槽/真实信号路径）, 不做
 *             真实网络连接演练（服务器页 autotest 的端口 0 演练已覆盖
 *             监听侧）, 仅断言控件装配/预置/状态机门控。
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
#include "XGuiClient.h"
#include "XRemoteSessionBar.h" /* 悬浮会话工具条(2026-10-03 断开/统计/切档入口)。 */
#include "XTimer.h"       /* 基准采样定时器(XGUI_REMOTE_BENCH_STATS)。 */
#include "XDateTime.h"    /* 单调毫秒时间戳(采样行 t= 字段)。 */
#include "XSystem.h"      /* XSystem_environment: 环境变量唯一入口。 */
#include "xgui_demo_pages.h"
#include "xgui_demo_splitter.h" /* 视图区/控制列分割条（拖宽/双击收/单击展）。 */
#include "xgui_demo_page_remote_client.h"

/*
 * 控件段宏守卫：与服务器页同族（XGuiRemote + PushButton/Label/CheckBox/
 * LineEdit/ComboBox）。任一模块被裁剪时降级为契约桩（build 返回 NULL、
 * autotest 返回 -1），保证契约符号始终存在、主文件可无条件链接。
 */
#if XGUI_REMOTE_ON && XWIDGET_ON && XPUSHBUTTON_ON && \
    XLABEL_ON && XCHECKBOX_ON && XLINEEDIT_ON && XCOMBOBOX_ON

/* ==================== 布局常量（2026-10-03 RS-d 改造: 左视图/右控制列） === */

/** @brief 视图区宽（页内 (0,0) 起; 504+8 间隙+252 控制列 ≤ 页宽 776）。 */
#define RC_VIEW_W 504
/** @brief 视图区高（页高 ~458 全高——800x600 镜像 1:1 超出部分经
 *         祖先矩形链裁剪, 不越出视图区, 不覆盖本地控件）。 */
#define RC_VIEW_H 458
/** @brief 控制列左缘（页内 x）。 */
#define RC_COL_X 516
/** @brief 控制列宽。 */
#define RC_COL_W 252

/* ==================== CLI 初始默认值（进程级, build 前预置） ============ */

/** @brief CLI 预置暂存（指针借用；未预置时全零=等价默认）。 */
static DemoRemoteClientCliOptions s_rcCli;

void demo_page_remote_client_cliDefaults(const DemoRemoteClientCliOptions* opts)
{
    memset(&s_rcCli, 0, sizeof(s_rcCli));
    if (opts) s_rcCli = *opts;
}

/* ==================== 页面内部控件登记（demo 单实例 static 自持） ======== */

/**
 * @brief 远程客户端页内部登记表。
 * @details 控件经 *_create 堆创建挂在页面根下（父子链级联析构），本表
 *          只存借用指针；XGuiClient 父挂主窗口顶层（非页面根），退出
 *          经 demo_page_remote_client_shutdown() 显式收尾。
 */
static struct
{
    XWidget*         m_root;        /**< 页面根控件（契约返回值；堆对象）。 */
    DemoPageStatusFn m_status;      /**< 主窗口状态栏反馈回调（借用）。 */
    void*            m_user;        /**< 回调上下文（主窗口指针，借用）。 */
    bool             m_shuttingDown; /**< 退出收尾中: 标签树已析构, 状态只留
                                       静态副本不再回写(ASan 实测断链信号在
                                       标签析构后仍回写, 每次泄漏重建串 96B)。 */
    char             m_lastStatus[160]; /**< 最近一次反馈文本（自测断言用）。 */
    bool             m_ready;       /**< build 已完成且控件指针有效。 */

    /* ---- 配置区控件 ---- */
    XLineEdit* m_hostEdit;    /**< 远端 IP（默认 127.0.0.1）。 */
    XLineEdit* m_portEdit;    /**< 远端端口（默认 46000）。 */
    XComboBox* m_profileCombo;/**< 档位 performance|resource|latency。 */
    XCheckBox* m_tlsCheck;    /**< TLS 开关。 */
    XCheckBox* m_udpCheck;    /**< UDP 低延迟旁路开关(2026-10-04 加法式;
                                   *   运行期可切)。 */
    XComboBox* m_authCombo;   /**< 认证：0=无 1=SHA256 挑战应答。 */
    XLineEdit* m_passwordEdit;/**< 认证口令（密码回显）。 */
    XPushButton* m_connectBtn;   /**< 连接按钮。 */
    XPushButton* m_disconnectBtn;/**< 断开按钮。 */

    /* ---- 反馈区控件 ---- */
    XLabel*    m_stateLabel;  /**< 连接状态行。 */
    XLabel*    m_statsLabel;  /**< 实时统计多行区。 */
    XLabel*    m_titleLabel;  /**< 页标题（自适应重排需随控制列移动）。 */
    XLabel*    m_capIp;       /**< 「IP」字段标题（随列移动）。 */
    XLabel*    m_capPort;     /**< 「端口」字段标题。 */
    XLabel*    m_capProfile;  /**< 「档位」字段标题。 */
    XLabel*    m_capAuth;     /**< 「认证」字段标题。 */
    XLabel*    m_capPassword; /**< 「口令」字段标题。 */

    /* ---- 联调断言辅助 ---- */
    XPushButton* m_touchBtn;  /**< 模拟触摸按钮（INPUT_TOUCH 序列直发）。 */
    XLabel*      m_touchLabel;/**< 模拟触摸回执计数。 */
    int          m_touchCount;/**< 已发送触摸轮数。 */

    /* ---- 客户端与运行态 ---- */
    XWidget*    m_viewHost;   /**< 视图区宿主（页根子控件; 镜像视图 1:1
                               *   超出部分的绘制/命中收窄矩形, 2026-10-03）。 */
    XRemoteSessionBar* m_sessionBar; /**< 悬浮会话工具条（页根子控件,
                               *   连接期浮现/断开隐藏; 断开=「退出会话
                               *   不退出应用」入口, 2026-10-03）。 */
    XGuiClient* m_client;     /**< 客户端对象（父挂视图区宿主）。 */
    XWidget*    m_split;      /**< 视图区/控制列分割条（拖宽/双击收列/单击展）。 */
    int         m_colW;       /**< 控制列宽（分割条可拖 200..420，默认 RC_COL_W）。 */
    bool        m_colCollapsed; /**< 控制列收起态（细条单击展开）。 */
    XWidget*    m_topLevel;   /**< 主窗口顶层（借用; build 时的 parent）。 */
    XGuiRemoteProfileId m_profile; /**< 界面档位（连接后经 PROFILE_SET 协商）。 */

    /* ---- 基准采样辅助（XGUI_REMOTE_BENCH_STATS=1 启用; 2026-10-02
     *      延迟基准追加: 定时拉 statisticsExtended 打 stderr, 供
     *      bench_latency.sh 读 PING RTT 序列。零 UI 影响, 未启用时
     *      不建定时器） ---- */
    XTimer*    m_benchTimer;  /**< 采样定时器（启用基准时拥有）。 */
    XTimer*    m_statsTimer;  /**< 统计/UDP 状态刷新定时器(2026-10-04 加法式:
                                   *   UDP 建链/回退状态无事件驱动刷新口,
                                   *   1s 周期轻刷统计区)。 */
} s_rc;

/* 分割条回调前置声明（定义在 adapt 段；装配点在其之前使用）。 */
static int  rc_splitSizeFor(void* owner);
static void rc_splitApplySize(void* owner, int size);
static void rc_splitToggle(void* owner);

/* ==================== 内部辅助 ==================== */

/** @brief 基准采样定时回调（XGUI_REMOTE_BENCH_STATS=1 时启用）。
 *  @details 每 250ms 检查 XGuiClient_statisticsExtended, 仅当
 *           bytesReceived 自上次打印后有增量时输出 stderr 单行
 *           （XGUI_BENCH_STATS t=毫秒 rtt=ms fps=千分值 up=帧 bytes=收）;
 *           bytes 增量=有对端帧到达(PONG/FB_UPDATE), 避免空转刷屏。
 *           供 build/xgui-remote-live/bench_latency.sh 归档协议 RTT
 *           序列。仅演示进程 stderr 诊断流, 不触任何 UI 控件。 */
/** @brief 实时统计多行区整体刷新前置声明(定时回调先行引用)。 */
static void rc_refreshStatsLabel(void);

/** @brief 统计/UDP 状态刷新定时回调(2026-10-04): 1s 轻刷统计区。 */
static void rc_statsTimerCb(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    rc_refreshStatsLabel();
}

static void rc_benchStatsCb(void* userData, XTimerData* timer)
{
    static uint64_t s_lastBytes;
    XGuiClient* cli;
    XGuiRemoteStatsExtended st;
    (void)userData; (void)timer;
    cli = s_rc.m_client;
    if (!cli) return;
    XGuiClient_statisticsExtended(cli, &st);
    if (st.bytesReceived == s_lastBytes) return; /* 无新对端帧不刷。 */
    s_lastBytes = st.bytesReceived;
    fprintf(stderr, "XGUI_BENCH_STATS t=%llu rtt=%u fps=%u up=%u bytes=%llu\n",
            (unsigned long long)(uint64_t)XDateTime_currentMSecsSinceEpoch(),
            st.rttMs, st.fpsMilli, st.updateCount,
            (unsigned long long)st.bytesReceived);
}

/** @brief 向主窗口状态栏反馈并登记最近文本（autotest 断言用）。 */
static void rc_report(const char* text)
{
    if (!text) return;
    snprintf(s_rc.m_lastStatus, sizeof(s_rc.m_lastStatus), "%s", text);
    /* 退出收尾期不再回写标签: 断链信号在标签析构后仍会到达, 回写会在
     * 已析构标签上重建显示串(ASan 实测每次泄漏 96B/1obj)——静态副本
     * m_lastStatus 已留档, 活体运行期回写不受影响。 */
    if (s_rc.m_shuttingDown) return;
    if (s_rc.m_status)
        s_rc.m_status(s_rc.m_user, text);
}

/** @brief 档位组合框当前值 → 档位 id。 */
static XGuiRemoteProfileId rc_profileFromCombo(void)
{
    int idx = s_rc.m_profileCombo ? XComboBox_currentIndex(s_rc.m_profileCombo) : 0;
    if (idx == 1) return XGUI_REMOTE_PROFILE_RESOURCE;
    if (idx == 2) return XGUI_REMOTE_PROFILE_LATENCY;
    return XGUI_REMOTE_PROFILE_PERFORMANCE;
}

static const char* rc_profileName(XGuiRemoteProfileId id)
{
    switch (id) {
    case XGUI_REMOTE_PROFILE_PERFORMANCE: return "performance";
    case XGUI_REMOTE_PROFILE_RESOURCE:    return "resource";
    case XGUI_REMOTE_PROFILE_LATENCY:     return "latency";
    case XGUI_REMOTE_PROFILE_AUTO:        return "auto";
    default:                              return "custom";
    }
}

/** @brief 认证组合框当前值 → 认证方法。 */
static XGuiRemoteAuthMethod rc_authFromCombo(void)
{
    int idx = s_rc.m_authCombo ? XComboBox_currentIndex(s_rc.m_authCombo) : 0;
    return idx == 1 ? XGUI_REMOTE_AUTH_SHA256_CHALLENGE
                    : XGUI_REMOTE_AUTH_NONE;
}

static const char* rc_stateName(XGuiRemoteSessionState st)
{
    switch (st) {
    case XGUI_REMOTE_STATE_DISCONNECTED:   return "未连接";
    case XGUI_REMOTE_STATE_BANNER_WAIT:    return "连接横幅中";
    case XGUI_REMOTE_STATE_HANDSHAKING:    return "握手中";
    case XGUI_REMOTE_STATE_AUTHENTICATING: return "认证中";
    case XGUI_REMOTE_STATE_STREAMING:      return "已连接(流式)";
    default:                               return "?";
    }
}

/** @brief UDP 旁路状态短名(2026-10-04 加法式; 显示/回退口径)。 */
static const char* rc_udpStateName(int state)
{
    switch (state) {
    case XGUI_REMOTE_UDP_STATE_TRYING:   return "协商中";
    case XGUI_REMOTE_UDP_STATE_ACTIVE:   return "激活(低延迟)";
    case XGUI_REMOTE_UDP_STATE_FALLBACK: return "已回退TCP";
    default:                             return "关";
    }
}

/** @brief 实时统计多行区整体刷新（状态/远端尺寸/帧数/RTT/重连次数）。 */
static void rc_refreshStatsLabel(void)
{
    char buf[512];
    XGuiClient* cli = s_rc.m_client;
    if (!s_rc.m_statsLabel) return;
    if (!cli) {
        snprintf(buf, sizeof(buf), "客户端: 未创建");
    }
    else {
        XGuiRemoteStatsExtended st;
        XSize rsz;
        XGuiClient_statisticsExtended(cli, &st);
        XGuiClient_remoteSize(cli, &rsz);
        snprintf(buf, sizeof(buf),
                 "状态: %s | UDP: %s\n远端画面: %ux%u | 档位: %s\n"
                 "帧数: %u | tile: %u | 实际刷新率: %u.%02u fps\n"
                 "RTT: %u ms | 收: %llu B | 发: %llu B\n"
                 "重连次数: %u",
                 rc_stateName(XGuiClient_state(cli)),
                 rc_udpStateName(XGuiClient_udpState(cli)),
                 (unsigned)rsz.width, (unsigned)rsz.height,
                 rc_profileName(s_rc.m_profile),
                 st.updateCount, st.tileCount,
                 st.fpsMilli / 100u, st.fpsMilli % 100u,
                 st.rttMs,
                 (unsigned long long)st.bytesReceived,
                 (unsigned long long)st.bytesSent,
                 st.reconnectCount);
    }
    XLabel_setText_2(s_rc.m_statsLabel, buf);
}

/** @brief UDP 旁路开关 toggled：运行期即切(UDP_MODE 协商, 会话不断)。 */
static void rc_udpSlot(XObject* sender, XVarList* args)
{
    char buf[128];
    (void)sender; (void)args;
    if (!s_rc.m_ready) return;
    if (s_rc.m_client) {
        XGuiClient_setUdpEnabled(s_rc.m_client,
            XAbstractButton_isChecked((const XAbstractButton*)s_rc.m_udpCheck));
        snprintf(buf, sizeof(buf), "UDP 旁路: %s",
                 XAbstractButton_isChecked((const XAbstractButton*)s_rc.m_udpCheck)
                     ? "开(协商中/激活)" : "关(纯 TCP)");
        if (s_rc.m_status) s_rc.m_status(s_rc.m_user, buf);
    }
    rc_refreshStatsLabel();
}

/** @brief 连接状态单行刷新。 */
static void rc_refreshStateLabel(void)
{
    char buf[96];
    if (!s_rc.m_stateLabel) return;
    if (s_rc.m_client)
        snprintf(buf, sizeof(buf), "连接状态: %s",
                 rc_stateName(XGuiClient_state(s_rc.m_client)));
    else
        snprintf(buf, sizeof(buf), "连接状态: 未创建");
    XLabel_setText_2(s_rc.m_stateLabel, buf);
}

/** @brief 把界面当前配置登记到客户端对象（认证口令/触摸转发等）。 */
static void rc_applyConfigToClient(void)
{
    XGuiClient* cli = s_rc.m_client;
    if (!cli) return;
    /* 认证(2026-10-04 访问口令加法式): 非空即设访问口令, 空即显式清除
     * （修复: 旧口径空输入不触 setPassword, 上一连接的口令残留至重连）。
     * 连接流程自动适配: 服务端 HELLO_ACK 选定挑战应答才走认证, 选定
     * NONE 直连; 服务端要求认证而本端无口令 → 客户端快速失败断链。 */
    if (!s_rc.m_passwordEdit) return;
    if (XLineEdit_text(s_rc.m_passwordEdit)[0])
        XGuiClient_setAccessPassword(cli, XLineEdit_text(s_rc.m_passwordEdit));
    else
        XGuiClient_clearAccessPassword(cli);
}

/* ==================== 槽 ==================== */

/** @brief 「连接」clicked：按界面值定版并发起连接（异步, 状态行跟进）。 */
static void rc_connectSlot(XObject* sender, XVarList* args)
{
    char buf[192];
    int port;
    const char* host;
    (void)sender; (void)args;
    if (!s_rc.m_ready || !s_rc.m_client) return;
    host = s_rc.m_hostEdit ? XLineEdit_text(s_rc.m_hostEdit) : "";
    port = s_rc.m_portEdit ? atoi(XLineEdit_text(s_rc.m_portEdit)) : 0;
    if (!host[0]) {
        rc_report("连接失败: IP 为空");
        return;
    }
    if (port <= 0 || port > 65535) {
        rc_report("端口非法(1..65535)");
        return;
    }
    rc_applyConfigToClient();
    s_rc.m_profile = rc_profileFromCombo();
    {
        bool tls = s_rc.m_tlsCheck &&
                   XAbstractButton_isChecked((const XAbstractButton*)s_rc.m_tlsCheck);
#if XGUI_REMOTE_TLS_ON
        if (tls)
            XGuiClient_connectToHostEncrypted(s_rc.m_client, host,
                                              (uint16_t)port, NULL);
        else
            XGuiClient_connectToHost(s_rc.m_client, host, (uint16_t)port);
#else
        if (tls)
            rc_report("本构建未启用 TLS 档, 回退明文");
        XGuiClient_connectToHost(s_rc.m_client, host, (uint16_t)port);
#endif
        /* 自动重连: 断线退避重连（指数上限 30s; 与断开按钮的显式断链
         * 区分——disconnectFromServer 不触发重连, 冻结头注）。 */
        XGuiClient_setAutoReconnect(s_rc.m_client, true, 1000);
        /* 诊断走 stdout(演示 main 已置行缓冲): 供联调矩阵等待连接就绪。 */
        XPrintf("XGuiWindowDemo: remote-client connecting host=%s port=%d "
                "tls=%s auth=%s\n", host, port, tls ? "on" : "off",
                rc_authFromCombo() == XGUI_REMOTE_AUTH_SHA256_CHALLENGE
                    ? "sha256" : "none");
        snprintf(buf, sizeof(buf), "连接中: %s:%d (tls=%s 认证=%s)",
                 host, port, tls ? "on" : "off", rc_authFromCombo() ==
                 XGUI_REMOTE_AUTH_SHA256_CHALLENGE ? "sha256" : "none");
        rc_report(buf);
    }
    rc_refreshStateLabel();
    rc_refreshStatsLabel();
}

/** @brief 「断开」clicked：显式断链（BYE(NORMAL), 不触发自动重连）。 */
static void rc_disconnectSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (!s_rc.m_ready || !s_rc.m_client) return;
    if (XGuiClient_state(s_rc.m_client) == XGUI_REMOTE_STATE_DISCONNECTED) {
        rc_report("未连接");
    }
    else {
        XGuiClient_disconnectFromServer(s_rc.m_client);
        rc_report("已断开(显式, 不自动重连)");
    }
    rc_refreshStateLabel();
    rc_refreshStatsLabel();
}

/** @brief 档位 activated：连接中经 PROFILE_SET 协商热切换。 */
static void rc_profileSlot(XObject* sender, XVarList* args)
{
    char buf[128];
    (void)sender; (void)args;
    if (!s_rc.m_ready || !s_rc.m_client) return;
    s_rc.m_profile = rc_profileFromCombo();
    XGuiClient_requestProfileId(s_rc.m_client, s_rc.m_profile);
    snprintf(buf, sizeof(buf), "已请求切档 → %s (待服务端 PROFILE_RESULT/FB_META)",
             rc_profileName(s_rc.m_profile));
    rc_report(buf);
    rc_refreshStatsLabel();
}

/** @brief 配置控件变动（textEdited/toggled）：状态行提示连接期配置口径。 */
static void rc_configChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (!s_rc.m_ready) return;
    /* 显式反馈: IP/端口/TLS/认证为连接期配置, 连接中改动需重连生效。 */
    if (s_rc.m_client &&
        XGuiClient_state(s_rc.m_client) != XGUI_REMOTE_STATE_DISCONNECTED)
        rc_report("连接期配置已修改, 断开后重连生效");
}

/** @brief 「模拟触摸」clicked：程序化发一轮 INPUT_TOUCH 按下-移动-抬起
 *         序列经协议转发远端（覆盖触摸路径）。目标点 = 服务器页
 *         RemotePing 按钮中心（2026-10-03 由「远端画面中心」重定靶：
 *         中心处无可响应控件、触摸回显不可断言；RemotePing 中心在
 *         远端顶层 (114,323)，触摸经服务端 touch 注入为一次点击，
 *         计数 +1 即触摸回显的联调断言锚点，RS-d 用）。 */
static void rc_touchSlot(XObject* sender, XVarList* args)
{
    XGuiRemoteMsgInputTouch msg;
    XSize rsz;
    int cx, cy;
    (void)sender; (void)args;
    if (!s_rc.m_ready || !s_rc.m_client) return;
    XGuiClient_remoteSize(s_rc.m_client, &rsz);
    cx = 114; /* 服务器页 RemotePing 中心: 页内 (102,215)+页根 (12,108)。 */
    cy = 323;
    if (cx >= (int)rsz.width) cx = (int)rsz.width / 2;   /* 防远端异常尺寸。 */
    if (cy >= (int)rsz.height) cy = (int)rsz.height / 2;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    XMemset(&msg, 0, sizeof(msg));
    msg.pointCount = 1; /* 主点必须放 points[0](§6.6)。 */
    /* BEGIN(按下, 中心) → UPDATE(移动, +16,+12) → END(抬起)。 */
    msg.action = XGUI_REMOTE_TOUCH_BEGIN;
    msg.points[0].id = 1;
    msg.points[0].state = XGUI_REMOTE_TP_PRESSED;
    msg.points[0].x = (int16_t)cx;
    msg.points[0].y = (int16_t)cy;
    msg.points[0].pressureQ8 = 255;
    XGuiClient_sendTouchFrame(s_rc.m_client, &msg);
    msg.action = XGUI_REMOTE_TOUCH_UPDATE;
    msg.points[0].state = XGUI_REMOTE_TP_UPDATED;
    msg.points[0].x = (int16_t)(cx + 16 < (int)rsz.width ? cx + 16 : cx);
    msg.points[0].y = (int16_t)(cy + 12 < (int)rsz.height ? cy + 12 : cy);
    XGuiClient_sendTouchFrame(s_rc.m_client, &msg);
    msg.action = XGUI_REMOTE_TOUCH_END;
    msg.points[0].state = XGUI_REMOTE_TP_RELEASED;
    msg.points[0].x = (int16_t)cx;
    msg.points[0].y = (int16_t)cy;
    XGuiClient_sendTouchFrame(s_rc.m_client, &msg);
    ++s_rc.m_touchCount;
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "模拟触摸: 已发 %d 轮(BEGIN→UPDATE→END)",
                 s_rc.m_touchCount);
        if (s_rc.m_touchLabel) XLabel_setText_2(s_rc.m_touchLabel, buf);
        rc_report(buf);
    }
    rc_refreshStatsLabel();
}

/** @brief 信号: 已连接（进入 Streaming, 首帧 FB_META 已收到）。 */
static void rc_onConnected(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    rc_report("已连接(流式)");
    /* 诊断走 stdout(演示 main 已置行缓冲): 连接达成标志。 */
    XPrintf("XGuiWindowDemo: remote-client connected (streaming)\n");
    /* 连接达成即按界面档位请求一次协商(服务端允许时 PROFILE_RESULT/FB_META)。 */
    if (s_rc.m_client)
        XGuiClient_requestProfileId(s_rc.m_client, s_rc.m_profile);
    rc_refreshStateLabel();
    rc_refreshStatsLabel();
}

/** @brief 信号: 会话断开（自动重连前发射）。 */
static void rc_onDisconnected(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_1(args, int, reason);
        {
            char buf[128];
            /* 口径(2026-10-03 修正): 显式断开(disconnectFromServer,
             * reason=NORMAL)在 XGuiClient.c:2092 已清 wantConnect——
             * 不再自动重连, 文案不得谎报「自动重连中」; 异常断链
             * (非 NORMAL)才走退避重连。 */
            if (reason == (int)XGUI_REMOTE_BYE_NORMAL)
                snprintf(buf, sizeof(buf), "已断开(不自动重连)");
            else
                snprintf(buf, sizeof(buf), "断开(reason=%d), 自动重连中…",
                         reason);
            rc_report(buf);
        }
    }
    /* RS-d 改造: 断开即回「未连接」视图态——控件尺寸复位为视图区宿主
     * 尺寸, 占位文本（「未连接远端服务器」, XGuiClient 占位绘制）落回
     * 可视区; 悬浮条隐藏由工具条自身 disconnected 槽完成（悬浮条自管
     * 理可见性）。重连达成后悬浮条随 connected 信号复现; 画面几何由
     * 视图适配模式决定（默认 FIT: 控件保持宿主几何、信箱缩放;
     * 1:1: FB_META 后 setFixedSize(远端尺寸), 2026-10-04）。 */
    if (s_rc.m_client && s_rc.m_viewHost)
        XWidget_setFixedSize((XWidget*)s_rc.m_client,
                             XWidget_width(s_rc.m_viewHost),
                             XWidget_height(s_rc.m_viewHost));
    rc_refreshStateLabel();
    rc_refreshStatsLabel();
}

/** @brief 信号: 远端元信息变化（FB_META: 尺寸/档位/标题）。 */
static void rc_onMeta(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    rc_refreshStateLabel();
    rc_refreshStatsLabel();
}

/** @brief 信号: 会话错误（不一定断链）。 */
static void rc_onError(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_1(args, int, code);
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "会话错误(code=%d)", code);
            rc_report(buf);
        }
    }
}

/* ==================== 构建 ==================== */

/** @brief 组装一行「标签 + 输入控件」。 */
static void rc_buildLabelAt(const char* text, int x, int y, int w, XLabel** out)
{
    XLabel* label = XLabel_create(s_rc.m_root, 0);
    if (label) {
        XLabel_setText_2(label, text);
        XWidget_setGeometry((XWidget*)label, x, y, w, 22);
        XWidget_show((XWidget*)label);
    }
    if (out) *out = label;
}

XWidget* demo_page_remote_client_build(XWidget* parent,
                                       DemoPageStatusFn status, void* user)
{
    /* demo 单实例：重复 build 前清空登记表（旧页面随旧父链析构;
     * CLI 预置 s_rcCli 不清, 由 demo_page_remote_client_cliDefaults
     * 显式重置）。 */
    memset(&s_rc, 0, sizeof(s_rc));
    s_rc.m_status = status;
    s_rc.m_user = user;
    s_rc.m_topLevel = parent; /* 主窗顶层（ext 页面注册以 m_base 为父）。 */
    s_rc.m_profile = XGUI_REMOTE_PROFILE_PERFORMANCE;

    if (!parent) return (XWidget*)0;
    s_rc.m_root = XWidget_create(parent, 0);
    if (!s_rc.m_root) return (XWidget*)0;

    /* ---- 视图区宿主（RS-d 改造: 镜像视图进页面容器, 1:1 超出部分经
     *     祖先矩形链收窄——绘制裁剪 XWidget_paintTree / 命中隔离
     *     XWidget_childAt 均按父链矩形, 无需遮罩; 先建=z 底, 后建控件
     *     全在其上, 本地控件永不被镜像覆盖）。 ---- */
    s_rc.m_viewHost = XWidget_create(s_rc.m_root, 0);
    if (!s_rc.m_viewHost) return s_rc.m_root;
    XWidget_setGeometry(s_rc.m_viewHost, 0, 0, RC_VIEW_W, RC_VIEW_H);
    XWidget_show(s_rc.m_viewHost);

    /* ---- 标题 + 连接状态行（控制列顶部） ---- */
    {
        XLabel* title = NULL;
        rc_buildLabelAt("\xE8\xBF\x9C\xE7\xA8\x8B\xE5\xAE\xA2\xE6\x88\xB7"
                        "\xE7\xAB\xAF\xE8\xAE\xBE\xE7\xBD\xAE",
                        RC_COL_X, 8, RC_COL_W, &title); /* 远程客户端设置 */
        s_rc.m_titleLabel = title;
        if (title) XLabel_setTextPixelSize(title, 15);
    }
    s_rc.m_stateLabel = XLabel_create(s_rc.m_root, 0);
    if (!s_rc.m_stateLabel) return s_rc.m_root;
    XLabel_setText_2(s_rc.m_stateLabel, "连接状态: 未连接");
    XWidget_setGeometry((XWidget*)s_rc.m_stateLabel, RC_COL_X, 36,
                        RC_COL_W, 22);
    XWidget_show((XWidget*)s_rc.m_stateLabel);

    /* ---- IP / 端口 ---- */
    rc_buildLabelAt("IP", RC_COL_X, 68, 28, &s_rc.m_capIp);
    s_rc.m_hostEdit = XLineEdit_create(s_rc.m_root, 0);
    if (!s_rc.m_hostEdit) return s_rc.m_root;
    XLineEdit_setText(s_rc.m_hostEdit, "127.0.0.1");
    XWidget_setGeometry((XWidget*)s_rc.m_hostEdit, RC_COL_X + 32, 66, 124, 24);
    XObject_connect_2((XObject*)s_rc.m_hostEdit,
                      XSignal(XLineEdit_textEdited_signal),
                      rc_configChangedSlot);
    XWidget_show((XWidget*)s_rc.m_hostEdit);

    rc_buildLabelAt("\xE7\xAB\xAF\xE5\x8F\xA3", RC_COL_X, 98, 40, &s_rc.m_capPort); /* 端口 */
    s_rc.m_portEdit = XLineEdit_create(s_rc.m_root, 0);
    if (!s_rc.m_portEdit) return s_rc.m_root;
    XLineEdit_setText(s_rc.m_portEdit, "46000");
    XWidget_setGeometry((XWidget*)s_rc.m_portEdit, RC_COL_X + 32, 96, 124, 24);
    XObject_connect_2((XObject*)s_rc.m_portEdit,
                      XSignal(XLineEdit_textEdited_signal),
                      rc_configChangedSlot);
    XWidget_show((XWidget*)s_rc.m_portEdit);

    /* ---- 连接 / 断开按钮 ---- */
    s_rc.m_connectBtn = XPushButton_create(s_rc.m_root, 0);
    if (!s_rc.m_connectBtn) return s_rc.m_root;
    XPushButton_setText_2(s_rc.m_connectBtn,
                          "\xE8\xBF\x9E\xE6\x8E\xA5"); /* 连接 */
    XWidget_setGeometry((XWidget*)s_rc.m_connectBtn, RC_COL_X, 130, 80, 28);
    XObject_connect_2((XObject*)s_rc.m_connectBtn,
                      XSignal(XAbstractButton_clicked_signal), rc_connectSlot);
    XWidget_show((XWidget*)s_rc.m_connectBtn);

    s_rc.m_disconnectBtn = XPushButton_create(s_rc.m_root, 0);
    if (!s_rc.m_disconnectBtn) return s_rc.m_root;
    XPushButton_setText_2(s_rc.m_disconnectBtn,
                          "\xE6\x96\xAD\xE5\xBC\x80"); /* 断开 */
    XWidget_setGeometry((XWidget*)s_rc.m_disconnectBtn, RC_COL_X + 88, 130,
                        80, 28);
    XObject_connect_2((XObject*)s_rc.m_disconnectBtn,
                      XSignal(XAbstractButton_clicked_signal),
                      rc_disconnectSlot);
    XWidget_show((XWidget*)s_rc.m_disconnectBtn);

    /* ---- 档位 ---- */
    rc_buildLabelAt("\xE6\xA1\xA3\xE4\xBD\x8D", RC_COL_X, 172, 40, &s_rc.m_capProfile); /* 档位 */
    s_rc.m_profileCombo = XComboBox_create(s_rc.m_root, 0);
    if (!s_rc.m_profileCombo) return s_rc.m_root;
    XComboBox_addItem_2(s_rc.m_profileCombo, "performance");
    XComboBox_addItem_2(s_rc.m_profileCombo, "resource");
    XComboBox_addItem_2(s_rc.m_profileCombo, "latency");
    XComboBox_setCurrentIndex(s_rc.m_profileCombo, 0);
    XWidget_setGeometry((XWidget*)s_rc.m_profileCombo, RC_COL_X + 32, 170,
                        124, 24);
    XObject_connect_2((XObject*)s_rc.m_profileCombo,
                      XSignal(XComboBox_activated_signal), rc_profileSlot);
    XWidget_show((XWidget*)s_rc.m_profileCombo);

    /* ---- TLS 开关（与服务器页对等选项） ---- */
    s_rc.m_tlsCheck = XCheckBox_create(s_rc.m_root, 0);
    if (!s_rc.m_tlsCheck) return s_rc.m_root;
    XAbstractButton_setText_2((XAbstractButton*)s_rc.m_tlsCheck,
                              "TLS \xE5\x8A\xA0\xE5\xAF\x86"); /* TLS 加密 */
    XWidget_setGeometry((XWidget*)s_rc.m_tlsCheck, RC_COL_X, 202, 96, 24);
    XObject_connect_2((XObject*)s_rc.m_tlsCheck,
                      XSignal(XAbstractButton_toggled_signal),
                      rc_configChangedSlot);
    XWidget_show((XWidget*)s_rc.m_tlsCheck);

    /* ---- UDP 低延迟旁路开关(2026-10-04 加法式; 默认开, 运行期可切;
     *     与 TLS 同排右段, 不叠印) ---- */
    s_rc.m_udpCheck = XCheckBox_create(s_rc.m_root, 0);
    if (!s_rc.m_udpCheck) return s_rc.m_root;
    XAbstractButton_setText_2((XAbstractButton*)s_rc.m_udpCheck,
                              "UDP \xE4\xBD\x8E\xE5\xBB\xB6\xE8\xBF"
                              "\x9F");
                              /* UDP 低延迟 */
    XAbstractButton_setChecked((XAbstractButton*)s_rc.m_udpCheck, true);
    XWidget_setGeometry((XWidget*)s_rc.m_udpCheck, RC_COL_X + 100, 202, 152, 24);
    XObject_connect_2((XObject*)s_rc.m_udpCheck,
                      XSignal(XAbstractButton_toggled_signal), rc_udpSlot);
    XWidget_show((XWidget*)s_rc.m_udpCheck);

    /* ---- 认证方式 + 口令（与服务器页对等选项） ---- */
    rc_buildLabelAt("\xE8\xAE\xA4\xE8\xAF\x81", RC_COL_X, 234, 40, &s_rc.m_capAuth); /* 认证 */
    s_rc.m_authCombo = XComboBox_create(s_rc.m_root, 0);
    if (!s_rc.m_authCombo) return s_rc.m_root;
    XComboBox_addItem_2(s_rc.m_authCombo, "none");
    XComboBox_addItem_2(s_rc.m_authCombo, "sha256");
    XComboBox_setCurrentIndex(s_rc.m_authCombo, 0);
    XWidget_setGeometry((XWidget*)s_rc.m_authCombo, RC_COL_X + 32, 232,
                        124, 24);
    XObject_connect_2((XObject*)s_rc.m_authCombo,
                      XSignal(XComboBox_activated_signal),
                      rc_configChangedSlot);
    XWidget_show((XWidget*)s_rc.m_authCombo);

    rc_buildLabelAt("\xE5\x8F\xA3\xE4\xBB\xA4", RC_COL_X, 264, 40, &s_rc.m_capPassword); /* 口令 */
    s_rc.m_passwordEdit = XLineEdit_create(s_rc.m_root, 0);
    if (!s_rc.m_passwordEdit) return s_rc.m_root;
    XLineEdit_setEchoMode(s_rc.m_passwordEdit, XLineEditEchoMode_Password);
    XWidget_setGeometry((XWidget*)s_rc.m_passwordEdit, RC_COL_X + 32, 262,
                        124, 24);
    XObject_connect_2((XObject*)s_rc.m_passwordEdit,
                      XSignal(XLineEdit_textEdited_signal),
                      rc_configChangedSlot);
    XWidget_show((XWidget*)s_rc.m_passwordEdit);

    /* ---- 模拟触摸联调断言辅助（INPUT_TOUCH 序列直发远端; RS-d 改造后
     *     控制列固定可达——不再被全页镜像盖死）。 ---- */
    s_rc.m_touchBtn = XPushButton_create(s_rc.m_root, 0);
    if (!s_rc.m_touchBtn) return s_rc.m_root;
    XPushButton_setText_2(s_rc.m_touchBtn,
                          "\xE6\xA8\xA1\xE6\x8B\x9F\xE8\xA7\xA6\xE6\x91\xB8");
                          /* 模拟触摸 */
    XWidget_setGeometry((XWidget*)s_rc.m_touchBtn, RC_COL_X, 298, 150, 32);
    XObject_connect_2((XObject*)s_rc.m_touchBtn,
                      XSignal(XAbstractButton_clicked_signal), rc_touchSlot);
    XWidget_show((XWidget*)s_rc.m_touchBtn);

    s_rc.m_touchLabel = XLabel_create(s_rc.m_root, 0);
    if (!s_rc.m_touchLabel) return s_rc.m_root;
    XLabel_setText_2(s_rc.m_touchLabel,
                     "\xE6\xA8\xA1\xE6\x8B\x9F\xE8\xA7\xA6\xE6\x91\xB8: 0 \xE8\xBD\xAE");
                     /* 模拟触摸: 0 轮 */
    XWidget_setGeometry((XWidget*)s_rc.m_touchLabel, RC_COL_X, 336, RC_COL_W, 22);
    XWidget_show((XWidget*)s_rc.m_touchLabel);

    /* ---- 实时统计多行区（控制列底部） ---- */
    s_rc.m_statsLabel = XLabel_create(s_rc.m_root, 0);
    if (!s_rc.m_statsLabel) return s_rc.m_root;
    XLabel_setText_2(s_rc.m_statsLabel, "\xE5\xAE\xA2\xE6\x88\xB7\xE7\xAB\xAF: "
                     "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5"); /* 客户端: 未连接 */
    /* 13px 小字：默认 16px 下「帧数|tile|实际刷新率」行 ~270px 超列宽
       252，尾字被列右缘裁切（目验二轮）；小字后 ~220px 行宽留余量。 */
    XLabel_setTextPixelSize(s_rc.m_statsLabel, 13);
    XLabel_setAlignment(s_rc.m_statsLabel,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setGeometry((XWidget*)s_rc.m_statsLabel, RC_COL_X, 364,
                        RC_COL_W, 88);
    XWidget_show((XWidget*)s_rc.m_statsLabel);

    /* ---- 客户端对象: 父挂视图区宿主（RS-d 改造; 对象树级联析构兜底）。
     *     初始=宿主尺寸 → 占位文本充满视图区。默认 FIT 模式下 FB_META
     *     不再改几何: 远端画面按 contain 信箱缩放进本视图区(2026-10-04
     *     视图适配缩放); 1:1 模式才恢复 setFixedSize(远端尺寸) 旧口径。 ---- */
    s_rc.m_client = XGuiClient_create(s_rc.m_viewHost, 0);
    if (s_rc.m_client) {
        XWidget_setGeometry((XWidget*)s_rc.m_client, 0, 0,
                            RC_VIEW_W, RC_VIEW_H);
        XGuiClient_setAutoReconnect(s_rc.m_client, false, 1000); /* 连接时再开。 */
        XObject_connect_2((XObject*)s_rc.m_client,
                          XSignal(XGuiClient_connected_signal), rc_onConnected);
        XObject_connect_2((XObject*)s_rc.m_client,
                          XSignal(XGuiClient_disconnected_signal),
                          rc_onDisconnected);
        XObject_connect_2((XObject*)s_rc.m_client,
                          XSignal(XGuiClient_remoteMetaChanged_signal),
                          rc_onMeta);
        XObject_connect_2((XObject*)s_rc.m_client,
                          XSignal(XGuiClient_errorOccurred_signal), rc_onError);
    }

    /* ---- 悬浮会话工具条（页根子控件, z 顶; 连接期浮现/断开隐藏,
     *     断开=「退出会话不退出应用」的 UI 入口, 2026-10-03）。 ---- */
    s_rc.m_sessionBar = XRemoteSessionBar_create(s_rc.m_root, 0);
    if (s_rc.m_sessionBar) {
        XRect viewRect;
        XRect_init(&viewRect, 0, 0, RC_VIEW_W, RC_VIEW_H);
        XRemoteSessionBar_setClient(s_rc.m_sessionBar, s_rc.m_client);
        XRemoteSessionBar_attachView(s_rc.m_sessionBar, &viewRect);
        XWidget_hide((XWidget*)s_rc.m_sessionBar); /* 未连接: 隐藏。 */
    }

    /* ---- 分割条：视图区/控制列之间（拖动调列宽, 双击收列,
     *     收起后细条单击展开）。 ---- */
    {
        static const DemoSplitterCallbacks kRcSplitCbs = {
            rc_splitSizeFor, rc_splitApplySize, rc_splitToggle
        };
        s_rc.m_colW = RC_COL_W;
        s_rc.m_colCollapsed = false;
        s_rc.m_split = DemoSplitter_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              s_rc.m_root, 1,
                                              &kRcSplitCbs, &s_rc);
    }

    /* ---- 虚拟键盘接管（历史记录）: RC 页曾在此全局关断应用虚拟键盘
     *     单例的 autoPopup（「点编辑框自动弹键盘/点空白自动收起」失效,
     *     屏幕键盘唯一入口=悬浮会话工具条的「键盘」钮, 显式 popup/
     *     closePopup, 见 XRemoteSessionBar.c）。但全局关断导致键盘页等
     *     其余页面点击不弹（2026-10-03 用户反馈），已改为随页切换:
     *     见主文件 xgui_window_demo.c 的 demo_switchPage——进入远程客户
     *     端页(index==11)关断、离开恢复，本页装配期不再动单例状态。 ---- */

    /* ---- CLI 初始默认值预置（UI 主控; 不在此连接——自动连接经首帧
     *     autostart 钩子走同一条 UI 连接路径）。 */
    if (s_rcCli.host && s_rcCli.host[0])
        XLineEdit_setText(s_rc.m_hostEdit, s_rcCli.host);
    if (s_rcCli.port > 0 && s_rcCli.port <= 65535) {
        char portText[16];
        snprintf(portText, sizeof(portText), "%d", s_rcCli.port);
        XLineEdit_setText(s_rc.m_portEdit, portText);
    }
    if (s_rcCli.profile && strcmp(s_rcCli.profile, "resource") == 0)
        XComboBox_setCurrentIndex(s_rc.m_profileCombo, 1);
    else if (s_rcCli.profile && strcmp(s_rcCli.profile, "latency") == 0)
        XComboBox_setCurrentIndex(s_rc.m_profileCombo, 2);
    if (s_rcCli.tls)
        XAbstractButton_setChecked((XAbstractButton*)s_rc.m_tlsCheck, true);
    if (s_rcCli.auth && strcmp(s_rcCli.auth, "sha256") == 0)
        XComboBox_setCurrentIndex(s_rc.m_authCombo, 1);
    if (s_rcCli.password && s_rcCli.password[0])
        XLineEdit_setText(s_rc.m_passwordEdit, s_rcCli.password);
    s_rc.m_profile = rc_profileFromCombo();

    s_rc.m_ready = true;
    rc_refreshStateLabel();
    rc_refreshStatsLabel();

    /* ---- 统计/UDP 状态刷新定时器(2026-10-04 加法式): UDP 建链/回退无
     *      事件刷新口, 1s 周期轻刷统计区(未连接时空转近零成本)。 ---- */
    s_rc.m_statsTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (s_rc.m_statsTimer) {
        XTimer_setInterval(s_rc.m_statsTimer, 1000);
        XTimer_setTimerCallback(s_rc.m_statsTimer, rc_statsTimerCb);
        XTimer_setUserData(s_rc.m_statsTimer, NULL);
        XTimer_start_base(s_rc.m_statsTimer);
    }

    /* ---- 基准采样定时器（XGUI_REMOTE_BENCH_STATS=1 才建; 延迟基准
     *      专用, 常规演示零开销）。 ---- */
    if (XSystem_environment("XGUI_REMOTE_BENCH_STATS")) {
        s_rc.m_benchTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (s_rc.m_benchTimer) {
            XTimer_setInterval(s_rc.m_benchTimer, 250);
            XTimer_setTimerCallback(s_rc.m_benchTimer, rc_benchStatsCb);
            XTimer_setUserData(s_rc.m_benchTimer, NULL);
            XTimer_start_base(s_rc.m_benchTimer);
        }
    }
    return s_rc.m_root;
}

/* ---- 分割条回调（直接操作 s_rc 单例；owner 借用不另行解引用） ---- */
static int rc_splitSizeFor(void* owner)
{
    (void)owner;
    return s_rc.m_colW;
}

static void rc_splitApplySize(void* owner, int size)
{
    (void)owner;
    if (size < 236) size = 236;
    if (size > 420) size = 420;
    if (size == s_rc.m_colW) return;
    s_rc.m_colW = size;
    demo_page_remote_client_adapt(s_rc.m_root);
}

static void rc_splitToggle(void* owner)
{
    (void)owner;
    s_rc.m_colCollapsed = !s_rc.m_colCollapsed;
    demo_page_remote_client_adapt(s_rc.m_root);
}

/** @brief 控制列整体显隐（收起/展开时统一切换）。 */
static void rc_setColumnVisible(bool visible)
{
    XWidget* widgets[16];
    int i;
    widgets[0] = (XWidget*)s_rc.m_titleLabel;
    widgets[1] = (XWidget*)s_rc.m_capIp;
    widgets[2] = (XWidget*)s_rc.m_capPort;
    widgets[3] = (XWidget*)s_rc.m_capProfile;
    widgets[4] = (XWidget*)s_rc.m_capAuth;
    widgets[5] = (XWidget*)s_rc.m_capPassword;
    widgets[6] = (XWidget*)s_rc.m_stateLabel;
    widgets[7] = (XWidget*)s_rc.m_hostEdit;
    widgets[8] = (XWidget*)s_rc.m_portEdit;
    widgets[9] = (XWidget*)s_rc.m_connectBtn;
    widgets[10] = (XWidget*)s_rc.m_disconnectBtn;
    widgets[11] = (XWidget*)s_rc.m_profileCombo;
    widgets[12] = (XWidget*)s_rc.m_tlsCheck;
    widgets[13] = (XWidget*)s_rc.m_authCombo;
    widgets[14] = (XWidget*)s_rc.m_passwordEdit;
    widgets[15] = (XWidget*)s_rc.m_touchBtn;
    for (i = 0; i < 16; ++i)
        if (widgets[i]) {
            if (visible) XWidget_show(widgets[i]);
            else XWidget_hide(widgets[i]);
        }
    if (s_rc.m_touchLabel) {
        if (visible) XWidget_show((XWidget*)s_rc.m_touchLabel);
        else XWidget_hide((XWidget*)s_rc.m_touchLabel);
    }
    if (s_rc.m_statsLabel) {
        if (visible) XWidget_show((XWidget*)s_rc.m_statsLabel);
        else XWidget_hide((XWidget*)s_rc.m_statsLabel);
    }
}

/** @brief 自适应重排（xgui_demo_pages.h 契约）：镜像区伸缩+控制列贴右。
 * @details 旧布局按 760 设计稿硬编码（视图区 504 宽、控制列 x=516），
 *          窗口缩放后既留大片空档又被裁；改为控制列（RC_COL_W 宽）恒
 *          贴右缘，镜像视图区宿主占其余全部宽高，悬浮会话条跟随视图
 *          矩形，实时统计块贴列底。由 demo_layout_content 全路径调用；
 *          page 与登记根不符时静默返回。 */
void demo_page_remote_client_adapt(XWidget* page)
{
    int rootW;
    int rootH;
    int hostW;
    int hostH;
    int colX;
    int colW;
    XRect viewRect;
    if (!page || page != s_rc.m_root) return;
    rootW = XWidget_width(page);
    rootH = XWidget_height(page);
    if (rootW < 420 || rootH < 300) return; /* 过窄保持装配几何。 */
    colW = s_rc.m_colW;
    if (s_rc.m_colCollapsed) {
        /* 收起：控制列整隐，镜像区占满，右缘 8px 细条单击展开。 */
        colX = rootW - 8;
        hostW = colX - 12;
        if (hostW < 240) hostW = 240;
        rc_setColumnVisible(false);
    } else {
        colX = rootW - colW - 8;
        if (colX < 180) colX = 180; /* 保底视图区宽度。 */
        hostW = colX - 12;
        if (hostW < 240) hostW = 240;
        rc_setColumnVisible(true);
    }
    hostH = rootH;
    /* 镜像视图区宿主占左半全部（1:1 超出部分经祖先矩形链收窄）。 */
    XWidget_setGeometry(s_rc.m_viewHost, 0, 0, hostW, hostH);
    /* 控制列贴右（字段标题随列）。布局纵向序（用户 2026-10-03 裁定
     * 「选中的信息放到首行」）：实时统计块置顶（贴底时右半被 FPS 悬浮
     * 层盖住），其后为标题/状态行与配置表单。 */
    if (s_rc.m_statsLabel)
        XWidget_setGeometry((XWidget*)s_rc.m_statsLabel, colX, 8,
                            colW, 88);
    if (s_rc.m_titleLabel)
        XWidget_setGeometry((XWidget*)s_rc.m_titleLabel, colX, 104,
                            colW, 22);
    if (s_rc.m_stateLabel)
        XWidget_setGeometry((XWidget*)s_rc.m_stateLabel, colX, 132,
                            colW, 22);
    if (s_rc.m_capIp)
        XWidget_setGeometry((XWidget*)s_rc.m_capIp, colX, 164, 28, 22);
    if (s_rc.m_capPort)
        XWidget_setGeometry((XWidget*)s_rc.m_capPort, colX, 194, 40, 22);
    if (s_rc.m_capProfile)
        XWidget_setGeometry((XWidget*)s_rc.m_capProfile, colX, 268, 40, 22);
    if (s_rc.m_capAuth)
        XWidget_setGeometry((XWidget*)s_rc.m_capAuth, colX, 330, 40, 22);
    if (s_rc.m_capPassword)
        XWidget_setGeometry((XWidget*)s_rc.m_capPassword, colX, 360, 40, 22);
    if (s_rc.m_hostEdit)
        XWidget_setGeometry((XWidget*)s_rc.m_hostEdit, colX + 32, 162,
                            colW - 32, 24);
    if (s_rc.m_portEdit)
        XWidget_setGeometry((XWidget*)s_rc.m_portEdit, colX + 32, 192,
                            colW - 32, 24);
    if (s_rc.m_connectBtn)
        XWidget_setGeometry((XWidget*)s_rc.m_connectBtn, colX, 226, 80, 28);
    if (s_rc.m_disconnectBtn)
        XWidget_setGeometry((XWidget*)s_rc.m_disconnectBtn, colX + 88, 226,
                            80, 28);
    if (s_rc.m_profileCombo)
        XWidget_setGeometry((XWidget*)s_rc.m_profileCombo, colX + 32, 266,
                            colW - 32, 26);
    if (s_rc.m_tlsCheck)
        XWidget_setGeometry((XWidget*)s_rc.m_tlsCheck, colX, 298, 96, 24);
    /* UDP 低延迟旁路开关随列重排（2026-10-04 修: 此前只有 build 装配
     * 几何 (RC_COL_X+100,202)——RC_COL_X=516 在默认 600 根宽下已越出
     * 页面, 重排后列内同位又是端口编辑框, 双重叠印/不可见（用户实测
     * 「没加进布局」）。与 TLS 同排右段; TLS 宽回装配同款 96; 列宽拖
     * 至 200 下限时本开关文本轻微裁切, 与页面窄列裁切口径一致。 */
    if (s_rc.m_udpCheck)
        XWidget_setGeometry((XWidget*)s_rc.m_udpCheck, colX + 104, 298,
                            colW - 104, 24);
    if (s_rc.m_authCombo)
        XWidget_setGeometry((XWidget*)s_rc.m_authCombo, colX + 32, 328,
                            colW - 32, 26);
    if (s_rc.m_passwordEdit)
        XWidget_setGeometry((XWidget*)s_rc.m_passwordEdit, colX + 32, 358,
                            colW - 32, 24);
    if (s_rc.m_touchBtn)
        XWidget_setGeometry((XWidget*)s_rc.m_touchBtn, colX, 394, 150, 32);
    if (s_rc.m_touchLabel)
        XWidget_setGeometry((XWidget*)s_rc.m_touchLabel, colX, 432,
                            colW, 22);
    /* 分割条：展开=控制列左缘 6px；收起=右缘 8px 细条（单击展开）。
     * setState 必须每次重排同步——分割条内部收起态决定单击语义
     * （展开 vs 拖拽），漏同步则细条单击永远进不了展开分支。 */
    if (s_rc.m_split) {
        if (s_rc.m_colCollapsed)
            XWidget_setGeometry(s_rc.m_split, rootW - 8, 0, 8, rootH);
        else
            XWidget_setGeometry(s_rc.m_split, colX - 6, 0, 6, rootH);
        DemoSplitter_setState(s_rc.m_split, 1, s_rc.m_colCollapsed);
        XWidget_show(s_rc.m_split);
    }
    /* 镜像几何: 未连接=占位撑满视图区; 已连接 FIT 模式同样撑满——
     * FIT 信箱变换按控件现尺寸重算, 若连接后不再随视图区缩放, 拉伸
     * 窗口时镜像停在连接时刻尺寸、右侧留白(用户实测 2026-10-06);
     * 1:1 模式保持 FB_META 定径(setFixedSize 远端尺寸)不动。 */
    if (s_rc.m_client &&
        (XGuiClient_state(s_rc.m_client) == XGUI_REMOTE_STATE_DISCONNECTED ||
         XGuiClient_viewFitMode(s_rc.m_client) == XGUI_CLIENT_VIEW_FIT))
        XWidget_setGeometry((XWidget*)s_rc.m_client, 0, 0, hostW, hostH);
    /* 悬浮会话条跟随视图矩形（RS-d 附着语义）。 */
    if (s_rc.m_sessionBar) {
        XRect_init(&viewRect, 0, 0, hostW, hostH);
        XRemoteSessionBar_attachView(s_rc.m_sessionBar, &viewRect);
    }
}

/* ==================== 首帧自动连接（--remote-client 预置） ============= */

void demo_page_remote_client_autostart(void)
{
    static int s_retry = 0;
    if (!s_rc.m_ready || !s_rcCli.enabled) return; /* 未预置: 零开销。 */
    if (!s_rc.m_client) {
        /* 控件树未就绪/创建失败: ~1s(60fps 口径)后如实放弃（不静默）。 */
        if (++s_retry > 60) {
            rc_report("远程客户端自动连接失败(客户端控件未就绪)");
            s_rcCli.enabled = false;
        }
        return;
    }
    s_rcCli.enabled = false; /* 终态: 只发一次（重试由会话自动重连承载）。 */
    rc_connectSlot(NULL, NULL);
}

/* ==================== 退出清理 ==================== */

void demo_page_remote_client_shutdown(void)
{
    if (!s_rc.m_client) return;
    s_rc.m_shuttingDown = true; /* 断链信号将到达, 此后状态只留静态副本。 */
    /* 悬浮条先解绑（信号连接持有本页/客户端借用, 随后客户端
     * deleteLater——先断链防析构窗口期悬垂回调, 2026-10-03）。 */
    if (s_rc.m_sessionBar)
        XRemoteSessionBar_setClient(s_rc.m_sessionBar, NULL);
    XGuiClient_disconnectFromServer(s_rc.m_client); /* BYE(NORMAL) 干净断链。 */
    XGuiClient_deleteLater((XObject*)s_rc.m_client); /* 仓库约定延迟释放;
                                             * 窗口级联析构先行时,
                                             * XObject_deinit 清挂起事件,
                                             * 两序皆安全。 */
    s_rc.m_client = NULL;
}

/* ==================== autotest ==================== */

/** @brief 远程客户端页自测（非阻塞; 不做真实网络演练）。 */
int demo_page_remote_client_autotest(XWidget* page)
{
    int failures = 0;
#define RC_EXPECT(cond, what) \
    do { if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
         else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } } while (0)

    if (!page || !s_rc.m_ready || page != s_rc.m_root)
        return -1;

    /* ---- 控件登记完整 ---- */
    RC_EXPECT(s_rc.m_hostEdit && s_rc.m_portEdit && s_rc.m_profileCombo &&
              s_rc.m_tlsCheck && s_rc.m_authCombo && s_rc.m_passwordEdit &&
              s_rc.m_connectBtn && s_rc.m_disconnectBtn && s_rc.m_touchBtn &&
              s_rc.m_touchLabel && s_rc.m_statsLabel && s_rc.m_stateLabel &&
              s_rc.m_client && s_rc.m_viewHost && s_rc.m_sessionBar,
              "远程客户端页: 配置/反馈控件全部登记(含视图宿主+悬浮条)");

    /* ---- RS-d 布局: 镜像视图入页面容器 + 悬浮条初始隐藏 ---- */
    RC_EXPECT(XObject_parent((XObject*)s_rc.m_client) ==
                  (XObject*)s_rc.m_viewHost,
              "远程客户端页: 镜像视图父挂视图区宿主(进页面容器)");
    RC_EXPECT(!XWidget_isVisible((XWidget*)s_rc.m_sessionBar) &&
                  !XRemoteSessionBar_isExpanded(s_rc.m_sessionBar) &&
                  XRemoteSessionBar_client(s_rc.m_sessionBar) == s_rc.m_client,
              "远程客户端页: 悬浮条初始隐藏收起且已绑定客户端");
    RC_EXPECT(XRemoteSessionBar_disconnectButton(s_rc.m_sessionBar) != NULL,
              "远程客户端页: 悬浮条断开按钮就位(连上后退出会话入口)");
    /* 「模拟触摸」按钮可达性: 控制列 x 恒在视图区宿主右缘之外（与
     * 视图区矩形无重叠——旧版全页镜像盖死缺陷的回归锁；视图区宽度
     * 自适应后以宿主实际几何为准，不再锚 504 设计稿常量）。 */
    RC_EXPECT(XWidget_x(s_rc.m_touchBtn) >= XWidget_width(s_rc.m_viewHost),
              "远程客户端页: 模拟触摸按钮在视图区之外(可达)");

    /* ---- 初始默认值 ---- */
    RC_EXPECT(strcmp(XLineEdit_text(s_rc.m_hostEdit), "127.0.0.1") == 0,
              "远程客户端页: 默认 IP 127.0.0.1");
    RC_EXPECT(strcmp(XLineEdit_text(s_rc.m_portEdit), "46000") == 0,
              "远程客户端页: 默认端口 46000");
    RC_EXPECT(XGuiClient_state(s_rc.m_client) ==
                  XGUI_REMOTE_STATE_DISCONNECTED,
              "远程客户端页: 初始未连接");

    /* ---- 端口定版门控: 空/超界拒连（不静默） ---- */
    {
        char saved[16];
        snprintf(saved, sizeof(saved), "%s", XLineEdit_text(s_rc.m_portEdit));
        XLineEdit_setText(s_rc.m_portEdit, "99999");
        rc_connectSlot(NULL, NULL);
        RC_EXPECT(strstr(s_rc.m_lastStatus,
                         "\xE7\xAB\xAF\xE5\x8F\xA3\xE9\x9D\x9E\xE6\xB3\x95")
                  != NULL, /* 端口非法 */
                  "远程客户端页: 非法端口拒连(不静默)");
        XLineEdit_setText(s_rc.m_portEdit, saved);
    }

    /* ---- 统计 API: 扩展统计可读回且重连计数初始为 0 ---- */
    {
        XGuiRemoteStatsExtended st;
        XGuiClient_statisticsExtended(s_rc.m_client, &st);
        RC_EXPECT(st.reconnectCount == 0 && st.updateCount == 0 &&
                  st.rttMs == 0,
                  "远程客户端页: statisticsExtended 初始计数全零");
    }

    /* ---- 模拟触摸槽: 未连接时直发为无操作(门控), 计数照走 UI 路径 ---- */
    {
        int before = s_rc.m_touchCount;
        rc_touchSlot(NULL, NULL);
        RC_EXPECT(s_rc.m_touchCount == before + 1,
                  "远程客户端页: 模拟触摸点击计数 +1(无会话门控不崩)");
        RC_EXPECT(strstr(s_rc.m_lastStatus, "\xE6\xA8\xA1\xE6\x8B\x9F\xE8\xA7\xA6"
                         "\xE6\x91\xB8") != NULL, /* 模拟触摸 */
                  "远程客户端页: 模拟触摸反馈文本已更新");
    }

    /* ---- 档位槽: 未连接时 requestProfileId 为无操作(门控), 不崩 ---- */
    {
        XComboBox_setCurrentIndex(s_rc.m_profileCombo, 1);
        rc_profileSlot(NULL, NULL);
        RC_EXPECT(s_rc.m_profile == XGUI_REMOTE_PROFILE_RESOURCE,
                  "远程客户端页: 档位切换 resource 生效(待连接后协商)");
        XComboBox_setCurrentIndex(s_rc.m_profileCombo, 0);
        rc_profileSlot(NULL, NULL);
        RC_EXPECT(s_rc.m_profile == XGUI_REMOTE_PROFILE_PERFORMANCE,
                  "远程客户端页: 档位切回 performance");
    }

#undef RC_EXPECT
    XPrintf("XGuiAutoTest: 远程客户端设置页 %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}

#else /* 裁剪: 契约桩（符号常在, 主文件可无条件链接） */

XWidget* demo_page_remote_client_build(XWidget* parent,
                                       DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    XPrintf("XGuiAutoTest: [FAIL] 远程客户端页依赖 XGuiRemote/控件被裁剪，"
            "无法构建\n");
    return (XWidget*)0;
}

int demo_page_remote_client_autotest(XWidget* page)
{
    (void)page;
    XPrintf("XGuiAutoTest: [FAIL] 远程客户端页被裁剪，无法自测\n");
    return -1;
}

#endif /* XGUI_REMOTE_ON && 控件段 */
