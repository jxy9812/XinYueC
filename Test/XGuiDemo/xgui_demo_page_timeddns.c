/** @file       xgui_demo_page_timeddns.c
 * @brief      XGuiDemo 扩展页 19：时钟与 DDNS（系统设置类）。
 * @details    把 XNtp（应用内校时）与 XDdns（动态域名解析）接入 GUI 演示：
 *
 *   左列 NTP 校时：
 *     · 服务器编辑框（默认 ntp.aliyun.com）+「立即校时」按钮（异步，
 *       结果经信号回填状态区）+「自动校时(每小时)」复选框；
 *   右列 DDNS 动态域名（阿里云）：
 *     · AccessKeyId / AccessKeySecret(密码态) / 主域名 / 主机记录/
 *       IPv6(AAAA) 复选框 +「立即检查」按钮（同步自旋，期间事件循环
 *       仍在泵）+「自动检查(10分钟)」复选框；
 *     · checkNow 一轮 = 探测公网 IP → 与云端记录比对 → 增/改/跳过。
 *
 *   客户端对象懒创建并 setParent 到页面根（随父子链级联析构，页面无需
 *   shutdown 契约）；信号均为同线程直连，槽内直触控件安全。
 *
 *   布局承载与分档（2026-10-25 竖屏适配收口）：
 *     · 页根=只竖滚的 XScrollArea（水平条恒关，同 Modbus 页口径）：
 *       内容件宽恒=视口宽、高=重排后的内容首选高，超高交由右侧唯一
 *       竖滚条——窄屏单列排不下的部分滚动可达，不再被裁出屏外；
 *     · adapt 按视口宽分档：≥768（右列 400..768 的双列承载下限）保持
 *       桌面 1000x700 双列现状坐标；<768 重排为单列堆叠，可点行高
 *       统一 40 逻辑像素（手机触控目标下限，桌面档维持原尺寸不动）。
 *
 *   autotest 口径：非阻塞、不触真实网络——NTP 清空服务器后触发
 *   （NoServer 即时失败）；DDNS 不填凭据触发（NoConfig 即时失败，
 *   配置检查先于任何网络动作）。
 * @author     XinYueC 团队
 */
#include <stdio.h>
#include <string.h>
#include "XGuiConfig.h"
#include "XPrintf.h"
#include "XStringUtils.h"
#include "xgui_demo_pages.h"
#include "XLabel.h"
#include "XLineEdit.h"
#include "XCheckBox.h"
#include "XPushButton.h"
#include "XNtpClient.h"
#include "XDdnsClient.h"
#include "XDdnsProvider.h"
#include "XDdnsAliyun.h"
#include "XDateTime.h"
#include "XTimer.h"        /* 系统时间 1s 刷新定时器 */
#include "XComboBox.h"
#include "XScrollArea.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XThread.h"
#include <stdlib.h>

/* ========================================================================== */
/*                        页面静态状态（demo 单实例）                        */
/* ========================================================================== */

typedef struct DemoTimeDdnsPage {
    DemoPageStatusFn m_status; /**< 主窗口状态栏回调。 */
    void* m_user;              /**< 主窗口指针（透传给 status）。 */
    XWidget* m_root;           /**< 页面根控件（=只竖滚的 XScrollArea）。 */
    XWidget* m_content;        /**< 滚动内容件（绝对坐标承载层，宽随视口）。 */

    /* ---- 布局锚点：adapt 全量重排要挪的小标签（无句柄则挪不动） ---- */
    XLabel* m_sysTimeCap;      /**< 「系统时间」标题（系统时间卡锚点）。 */
    XLabel* m_ntpTitle;        /**< 「NTP 网络校时」标题。 */
    XLabel* m_ntpServerCap;    /**< 「服务器」行内标签。 */
    XLabel* m_ntpStatusCap;    /**< NTP「状态」行内标签。 */
    XLabel* m_ddnsTitle;       /**< 「DDNS 动态域名」标题。 */
    XLabel* m_ddnsProviderCap; /**< 「服务商」行内标签。 */
    XLabel* m_ddnsKeyIdCap;    /**< AccessKeyId 行内标签。 */
    XLabel* m_ddnsSecretCap;   /**< AccessKeySecret 行内标签。 */
    XLabel* m_ddnsDomainCap;   /**< 「主域名」行内标签。 */
    XLabel* m_ddnsRrCap;       /**< 「主机记录」行内标签。 */
    XLabel* m_ddnsStatusCap;   /**< DDNS「状态」行内标签。 */

    /* ---- NTP 控件 ---- */
    XLineEdit* m_ntpServer;    /**< 服务器编辑框。 */
    XPushButton* m_ntpSyncBtn; /**< 「立即校时」按钮（窄屏重排需挪位）。 */
    XCheckBox* m_ntpAuto;      /**< 自动校时复选框。 */
    XLabel* m_ntpStatus;       /**< NTP 状态区（多行）。 */
    XLabel* m_ntpTime;         /**< 校时成功后的时间显示区。 */
    XLabel* m_sysTime;         /**< 系统时间实时显示（1s 刷新，与 NTP 对照）。 */
    XTimer*  m_sysTimer;       /**< 系统时间刷新定时器（1s 周期；父=页根）。 */
    XNtpClient* m_ntp;         /**< 校时客户端（懒创建，父=页面根）。 */

    /* ---- DDNS 控件 ---- */
    XComboBox* m_ddnsProvider; /**< 服务商下拉框（枚举自 Provider 注册表）。 */
    XLineEdit* m_ddnsKeyId;    /**< AccessKeyId。 */
    XLineEdit* m_ddnsSecret;   /**< AccessKeySecret（密码态）。 */
    XLineEdit* m_ddnsDomain;   /**< 主域名。 */
    XLineEdit* m_ddnsRr;       /**< 主机记录。 */
    XCheckBox* m_ddnsV6;       /**< IPv6(AAAA) 复选框。 */
    XCheckBox* m_ddnsAuto;     /**< 自动检查复选框。 */
    XPushButton* m_ddnsCheckBtn;/**< 「立即检查」按钮（autotest 走真实 click 路径）。 */
    XLabel* m_ddnsStatus;      /**< DDNS 状态区（多行）。 */
    XDdnsClient* m_ddns;       /**< DDNS 客户端（懒创建，父=页面根）。 */
} DemoTimeDdnsPage;

/** @brief 页面静态单例。 */
static DemoTimeDdnsPage s_td = { 0 };

/* ========================================================================== */
/*                        NTP 槽与辅助                                       */
/* ========================================================================== */

/**
 * @brief 刷新系统时间显示区（1s 定时器驱动 + 校时完成后即时刷新）。
 * @details 只显示**本机系统时间**与同步状态一句话。
 *          【不显示 NTP 时间/时间差的理由 2026-10-25 用户裁定】NTP 不是持续
 *          推流的数据源——XNtpClient 只在「立即校时」/每小时自动校时那一刻
 *          取一次服务器时刻快照，之后不再更新。把它当成实时对照行显示，一
 *          方面快照会随时间流逝而陈旧（写完系统时钟后尤其明显：每过一秒
 *          「差值」就虚增一秒），另一方面会让人误以为存在持续的网络对时。
 *          校时结果的详细账（服务器时刻/偏差/时延/层级）留在下方 NTP 状态
 *          区与「最近一次校时」区，那里语义明确是快照记录。
 */
static void td_refreshSysTime(void)
{
    char buf[160];
    char sysText[32];
    bool synced;
    if (!s_td.m_sysTime) return;
    {
        XDateTime now = XDateTime_currentDateTime();
        /* 时间文本本地格式化：框架 XDateTime_toString_format 不补前导零
         * （minute/second 走 XString_setNum_int），直接用来显示会出现
         * 「15:8:41」这种不成型的时刻；这里统一按 %02d 拼装。 */
        XSnprintf(sysText, sizeof(sysText), "%04d-%02d-%02d %02d:%02d:%02d",
                  XDate_year(&now.m_date), XDate_month(&now.m_date),
                  XDate_day(&now.m_date), XTime_hour(&now.m_time),
                  XTime_minute(&now.m_time), XTime_second(&now.m_time));
    }
    synced = s_td.m_ntp && XNtpClient_isSynchronized(s_td.m_ntp);
    XSnprintf(buf, sizeof(buf), "系统时间: %s\n%s",
              sysText,
              synced ? "状态: 已完成 NTP 校时"
                     : "状态: 未同步(本机时间, 未与 NTP 校时)");
    XLabel_setText_2(s_td.m_sysTime, buf);
}

/** @brief 系统时间 1s 定时器回调（XTimer 周期触发）。 */
static void td_sysTimeTimerCb(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    td_refreshSysTime();
}

/**
 * @brief 刷新 NTP 状态区（从客户端 getter 回读，信号与按钮共用）。
 */
static void td_refreshNtpStatus(void)
{
    char buf[256];
    if (!s_td.m_ntp || !s_td.m_ntpStatus)
        return;
    if (XNtpClient_isSynchronized(s_td.m_ntp)) {
        XSnprintf(buf, sizeof(buf),
                  "状态: 已同步\n偏移: %lld ms\n时延: %lld ms\n服务器层级: %u",
                  (long long)XNtpClient_offsetMs(s_td.m_ntp),
                  (long long)XNtpClient_roundTripMs(s_td.m_ntp),
                  (unsigned)XNtpClient_stratum(s_td.m_ntp));
    } else if (XNtpClient_state(s_td.m_ntp) == XNtpClientState_Syncing) {
        XSnprintf(buf, sizeof(buf), "状态: 校时中 (DNS 解析 + UDP 123)...");
    } else {
        /* 错误码中文释义：让失败原因一眼可见 */
        const char* why = "未知";
        switch (XNtpClient_lastError(s_td.m_ntp)) {
        case XNtpClientError_NoServer:
            why = "服务器未填写或域名解析失败"; break;
        case XNtpClientError_Timeout:
            why = "应答超时 (UDP 123 出网可能被拦截)"; break;
        case XNtpClientError_Socket:
            why = "套接字创建/发送失败"; break;
        case XNtpClientError_KissOfDeath:
            why = "服务器拒绝服务"; break;
        case XNtpClientError_ServerUnsynchronized:
            why = "服务器时钟未同步"; break;
        case XNtpClientError_MismatchedOrigin:
            why = "应答与请求不匹配"; break;
        default:
            break;
        }
        XSnprintf(buf, sizeof(buf), "状态: 校时失败\n原因: %s", why);
    }
    XLabel_setText_2(s_td.m_ntpStatus, buf);

    /* 校时记录区：展示本次校时的服务器时间、偏差与网络时延（快照语义——
     * 这是「校时那一刻」的账，不随时间滚动；实时对照见上方系统时间卡）。 */
    if (s_td.m_ntpTime) {
        if (XNtpClient_isSynchronized(s_td.m_ntp)) {
            XDateTime server;
            char stamp[32];
            int64_t srvSecs = (int64_t)XNtpClient_serverSecs(s_td.m_ntp);
            XDateTime_setSecsSinceEpoch(&server, srvSecs);
            XSnprintf(stamp, sizeof(stamp),
                      "%04d-%02d-%02d %02d:%02d:%02d",
                      XDate_year(&server.m_date), XDate_month(&server.m_date),
                      XDate_day(&server.m_date), XTime_hour(&server.m_time),
                      XTime_minute(&server.m_time),
                      XTime_second(&server.m_time));
            XSnprintf(buf, sizeof(buf),
                      "最近一次校时\n服务器时间: %s\n偏差: %lld ms (时延 %lld ms)",
                      stamp,
                      (long long)XNtpClient_offsetMs(s_td.m_ntp),
                      (long long)XNtpClient_roundTripMs(s_td.m_ntp));
            XLabel_setText_2(s_td.m_ntpTime, buf);
        } else {
            XLabel_setText_2(s_td.m_ntpTime, "最近一次校时: (尚未校时)");
        }
    }
}

/**
 * @brief NTP 同步成功信号槽：写入系统时钟 + 回填状态区 + 主窗口状态栏。
 * @details 校时的语义就是「让本机时间对齐网络时间」，只更新界面显示而
 *          不写系统时钟等于没校时（2026-10-25 用户指正）。落地走框架既有
 *          抽象 API：XNtpClient_applyToSystem()——它把按往返时延估算的
 *          当前真实时刻写入系统墙钟，未同步/无权限时返回 false 并保持
 *          系统时间不变。
 *          权限前置门：先用 XDateTime_canSetSystemTime() 探测（POSIX 需
 *          root 或 CAP_SYS_TIME），无权限时不反复尝试写入，改为在状态栏
 *          明确提示「仅读取偏差、未写入系统时钟」，让用户知道为什么屏幕
 *          时间没变。
 *          注意：系统时钟跳变只影响墙钟（日志/文件时间戳），框架的单调
 *          时钟接口（XDateTime_currentMSecsSinceEpoch）不受影响，定时器/
 *          心跳/超时测量安全。
 */
static void td_onNtpSynced(XObject* sender, XVarList* args)
{
    XDdnsClient* unused = NULL;
    bool applied = false;
    bool canSet;
    (void)sender; (void)args; (void)unused;
    canSet = XDateTime_canSetSystemTime();
    if (canSet && s_td.m_ntp)
        applied = XNtpClient_applyToSystem(s_td.m_ntp);
    td_refreshNtpStatus();
    /* 写入系统时钟后立即刷新系统时间卡：此刻「系统时间」应已跳到 NTP
     * 时刻，差值归零或仅剩秒级取整误差，不必等下一个 1s 定时器拍。 */
    td_refreshSysTime();
    if (s_td.m_status) {
        if (!canSet)
            s_td.m_status(s_td.m_user,
                          "NTP 已同步(无写时钟权限，仅显示偏差)");
        else if (!applied)
            s_td.m_status(s_td.m_user, "NTP 已同步(写系统时钟失败)");
        else
            s_td.m_status(s_td.m_user, "NTP 校时成功，系统时钟已更新");
    }
}

/**
 * @brief NTP 同步失败信号槽。
 */
static void td_onNtpFailed(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    td_refreshNtpStatus();
    if (s_td.m_status)
        s_td.m_status(s_td.m_user, "NTP 校时失败");
}

/**
 * @brief「立即校时」按钮逻辑（按钮回调与 autotest 共用）。
 * @return 已发起同步返回 true；缺服务器等即时拒绝返回 false。
 */
static bool td_ntpSyncClicked(void);
static void td_ntpSyncSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    td_ntpSyncClicked();
}

static bool td_ntpSyncClicked(void)
{
    const char* server;
    if (!s_td.m_ntpServer)
        return false;
    server = XLineEdit_text(s_td.m_ntpServer);
    if (!server || !server[0]) {
        XLabel_setText_2(s_td.m_ntpStatus,
                         "状态: 请先填写 NTP 服务器");
        return false;
    }
    if (!s_td.m_ntp) {
        s_td.m_ntp = XNtpClient_create();
        if (!s_td.m_ntp)
            return false;
        XObject_setParent((XObject*)s_td.m_ntp, (XObject*)s_td.m_root);
        XObject_connect_2((XObject*)s_td.m_ntp,
                          XSignal(XNtpClient_synchronized_signal),
                          td_onNtpSynced);
        XObject_connect_2((XObject*)s_td.m_ntp,
                          XSignal(XNtpClient_syncFailed_signal),
                          td_onNtpFailed);
    }
    XNtpClient_setServer_2(s_td.m_ntp, server, 123);
    if (XNtpClient_setAutoSync(s_td.m_ntp,
                               XCheckBox_isChecked(s_td.m_ntpAuto), 3600000))
        ; /* 自动校时跟随复选框 */
    XLabel_setText_2(s_td.m_ntpStatus, "状态: 校时中...");
    return XNtpClient_syncNow(s_td.m_ntp);
}

/**
 * @brief「自动校时」复选框槽：开关周期定时器。
 */
static void td_onNtpAutoToggled(XObject* sender, XVarList* args)
{
    XCheckBox* box = (XCheckBox*)sender;
    (void)args;
    if (!s_td.m_ntp || !box)
        return;
    XNtpClient_setAutoSync(s_td.m_ntp, XCheckBox_isChecked(box), 3600000);
}

/* ========================================================================== */
/*                        DDNS 槽与辅助                                      */
/* ========================================================================== */

/**
 * @brief 刷新 DDNS 状态区。
 */
static void td_refreshDdnsStatus(void)
{
    char buf[256];
    const XString* ip;
    if (!s_td.m_ddns || !s_td.m_ddnsStatus)
        return;
    ip = XDdnsClient_currentIp_const(s_td.m_ddns);
    if (XDdnsClient_state(s_td.m_ddns) == XDdnsClientState_Synchronized) {
        const XString* pushed = XDdnsClient_pushedIp_const(s_td.m_ddns);
        XSnprintf(buf, sizeof(buf), "状态: 记录已一致\n当前 IP: %s\n已推送: %s",
                  ip ? XString_toUtf8(ip) : "-",
                  pushed ? XString_toUtf8(pushed) : "-");
    } else if (XDdnsClient_state(s_td.m_ddns) == XDdnsClientState_Updating
               || XDdnsClient_state(s_td.m_ddns)
                      == XDdnsClientState_Detecting) {
        XSnprintf(buf, sizeof(buf), "状态: 检查中...");
    } else {
        /* 云端已应答但非 2xx：区分「签名/权限被拒」与「网络不可达」，
           并把云端返回的机器可读错误码直接显示出来。 */
        int httpStatus = XDdnsClient_lastHttpStatus(s_td.m_ddns);
        const char* apiCode = XDdnsClient_lastApiCode_const(s_td.m_ddns);
        if (httpStatus > 0 && apiCode && apiCode[0])
            XSnprintf(buf, sizeof(buf),
                      "状态: 云端拒绝 HTTP %d\n错误码: %s",
                      httpStatus, apiCode);
        else if (httpStatus > 0)
            XSnprintf(buf, sizeof(buf), "状态: 云端拒绝 HTTP %d", httpStatus);
        else
            XSnprintf(buf, sizeof(buf), "状态: 未就绪 (err=%d)",
                      (int)XDdnsClient_lastError(s_td.m_ddns));
    }
    XLabel_setText_2(s_td.m_ddnsStatus, buf);
}

/**
 * @brief DDNS 更新成功信号槽。
 */
static void td_onDdnsUpdated(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    td_refreshDdnsStatus();
    if (s_td.m_status)
        s_td.m_status(s_td.m_user, "DDNS 记录已更新");
}

/**
 * @brief DDNS 记录一致信号槽。
 */
static void td_onDdnsUpToDate(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    td_refreshDdnsStatus();
    if (s_td.m_status)
        s_td.m_status(s_td.m_user, "DDNS 记录已一致");
}

/**
 * @brief DDNS 失败信号槽。
 */
static void td_onDdnsFailed(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    td_refreshDdnsStatus();
    if (s_td.m_status)
        s_td.m_status(s_td.m_user, "DDNS 检查失败");
}

/**
 * @brief 服务商下拉框切换槽：切厂商并恢复该厂商的默认端点。
 * @note  参数 index 即注册表下标（下拉框按 XDdnsProvider_at 顺序填充）。
 */
static void td_onProviderChanged(XObject* sender, XVarList* args)
{
    XComboBox* combo = (XComboBox*)sender;
    const XDdnsProviderOps* ops;
    int index;
    if (!combo || !s_td.m_ddns)
        return;
    index = XComboBox_currentIndex(combo);
    ops = XDdnsProvider_at(index);
    if (!ops)
        return;
    XDdnsClient_setProvider(s_td.m_ddns, ops->m_name);
    /* 端点恢复为新厂商默认（清除上一厂商的端点覆盖） */
    XDdnsClient_setEndpoint(s_td.m_ddns, NULL);
    if (s_td.m_status) {
        char text[96];
        XSnprintf(text, sizeof(text), "服务商已切换: %s", ops->m_name);
        s_td.m_status(s_td.m_user, text);
    }
}

/**
 * @brief 懒创建 DDNS 客户端（凭据/记录随每次检查从编辑框回读）。
 */
static XDdnsClient* td_ddnsEnsure(void)
{
    if (s_td.m_ddns)
        return s_td.m_ddns;
    s_td.m_ddns = XDdnsClient_create();
    if (!s_td.m_ddns)
        return NULL;
    XObject_setParent((XObject*)s_td.m_ddns, (XObject*)s_td.m_root);
    XObject_connect_2((XObject*)s_td.m_ddns,
                      XSignal(XDdnsClient_recordUpdated_signal),
                      td_onDdnsUpdated);
    XObject_connect_2((XObject*)s_td.m_ddns,
                      XSignal(XDdnsClient_recordUpToDate_signal),
                      td_onDdnsUpToDate);
    XObject_connect_2((XObject*)s_td.m_ddns,
                      XSignal(XDdnsClient_checkFailed_signal),
                      td_onDdnsFailed);
    return s_td.m_ddns;
}

/**
 * @brief「立即检查」按钮逻辑（按钮回调与 autotest 共用）。
 * @return 已发起返回 true；缺凭据等即时拒绝返回 false。
 */
static bool td_ddnsCheckClicked(void);
static void td_ddnsCheckSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    td_ddnsCheckClicked();
}

static bool td_ddnsCheckClicked(void)
{
    XDdnsClient* ddns = td_ddnsEnsure();
    const char* keyId;
    const char* keySecret;
    const char* domain;
    const char* rr;
    int statusY;

    if (!ddns || !s_td.m_ddnsKeyId || !s_td.m_ddnsStatus)
        return false;

    /* 厂商/端点随下拉框当前选择应用（切换发生在首检前也不丢） */
    if (s_td.m_ddnsProvider) {
        const XDdnsProviderOps* ops =
            XDdnsProvider_at(XComboBox_currentIndex(s_td.m_ddnsProvider));
        if (ops) {
            XDdnsClient_setProvider(ddns, ops->m_name);
            XDdnsClient_setEndpoint(ddns, NULL); /* 恢复厂商默认端点 */
        }
    }

    keyId = XLineEdit_text(s_td.m_ddnsKeyId);
    keySecret = XLineEdit_text(s_td.m_ddnsSecret);
    domain = XLineEdit_text(s_td.m_ddnsDomain);
    rr = XLineEdit_text(s_td.m_ddnsRr);
    if (!keyId[0] || !keySecret[0] || !domain[0] || !rr[0]) {
        XLabel_setText_2(s_td.m_ddnsStatus,
                         "状态: 请先填写 AccessKey/域名/主机记录");
        return false;
    }
    XDdnsClient_setCredentials_2(ddns, keyId, keySecret);
    XDdnsClient_setRecord_2(ddns, domain, rr,
                            XCheckBox_isChecked(s_td.m_ddnsV6)
                                ? XDdnsRecordType_AAAA : XDdnsRecordType_A);
    if (XCheckBox_isChecked(s_td.m_ddnsAuto))
        XDdnsClient_setCheckInterval(ddns, 10u * 60u * 1000u);
    else
        XDdnsClient_setCheckInterval(ddns, 0);
    XLabel_setText_2(s_td.m_ddnsStatus, "状态: 检查中 (探测公网 IP...)");
    statusY = 0; /* 占位：状态区高度由 adapt 统一管理 */
    (void)statusY;
    return XDdnsClient_checkNow(ddns);
}

/**
 * @brief「自动检查」复选框槽。
 */
static void td_onDdnsAutoToggled(XObject* sender, XVarList* args)
{
    XCheckBox* box = (XCheckBox*)sender;
    (void)args;
    if (!s_td.m_ddns || !box)
        return;
    XDdnsClient_setCheckInterval(s_td.m_ddns,
                                 XCheckBox_isChecked(box)
                                     ? 10u * 60u * 1000u : 0u);
}

/* ========================================================================== */
/*                        页面构建                                           */
/* ========================================================================== */

/** @brief 便捷：创建标签。 */
static XLabel* td_label(XWidget* parent, const char* utf8, int x, int y,
                        int w, int h)
{
    XLabel* label = XLabel_create(parent, 0);
    if (label) {
        XLabel_setText_2(label, utf8);
        XWidget_setGeometry((XWidget*)label, x, y, w, h);
        XWidget_show((XWidget*)label);
    }
    return label;
}

XWidget* demo_page_timeddns_build(XWidget* parent,
                                  DemoPageStatusFn status, void* user)
{
#if !XNTP_ON && !XDDNS_ON
    (void)parent; (void)status; (void)user;
    return (XWidget*)0;
#else
    memset(&s_td, 0, sizeof(s_td));
    s_td.m_status = status;
    s_td.m_user = user;
    if (!parent)
        return (XWidget*)0;
    /* 页根=只竖滚的 XScrollArea：水平条恒关（内容宽恒=视口宽，窄屏
     * 不出横向裁切），超高交由竖滚条——单列重排后的 DDNS 段滚动可达
     * （竖屏 400 宽下两段内容约一屏半，靠滚动承载而非裁切）。 */
    s_td.m_root = (XWidget*)XScrollArea_create(parent, 0);
    if (!s_td.m_root)
        return (XWidget*)0;
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)s_td.m_root, XScrollBarPolicy_AlwaysOff);
    /* 内容件承载全部可见控件（widgetResizable 缺省 false=内容自管
     * 几何，宽/高由 adapt 重排统一定）。 */
    s_td.m_content = XWidget_create(s_td.m_root, 0);
    if (!s_td.m_content)
        return (XWidget*)0;
    XScrollArea_setWidget((XScrollArea*)s_td.m_root, s_td.m_content);

#if XNTP_ON
    /* ---- 系统时间卡（最上段）----
     * [系统时间条 2026-10-25] 最上方先放本机实时时间（1s 刷新），与下方
     * 的 NTP 校时区形成「本机 vs 网络」上下对照：用户一眼能看出系统时间
     * 与 NTP 差多少、有没有同步。整块 NTP 区相对旧版下移 92px 让位。 */
    s_td.m_sysTimeCap = td_label(s_td.m_content, "系统时间", 12, 8, 380, 24);
    s_td.m_sysTime = XLabel_create(s_td.m_content, 0);
    if (s_td.m_sysTime) {
        XLabel_setText_2(s_td.m_sysTime, "系统时间: --");
        XLabel_setTextPixelSize(s_td.m_sysTime, 14);
        XLabel_setAlignment(s_td.m_sysTime,
                            XAlignment_Left | XAlignment_Top);
        XWidget_setGeometry((XWidget*)s_td.m_sysTime, 12, 34, 368, 60);
        XWidget_show((XWidget*)s_td.m_sysTime);
    }

    /* ---- NTP 左列（窄屏档在 adapt 重排为单列首段） ---- */
    s_td.m_ntpTitle = td_label(s_td.m_content, "NTP 网络校时", 12, 100, 360, 24);
    s_td.m_ntpServerCap = td_label(s_td.m_content, "服务器", 12, 138, 64, 22);
    s_td.m_ntpServer = XLineEdit_create(s_td.m_content, 0);
    if (s_td.m_ntpServer) {
        XLineEdit_setText(s_td.m_ntpServer, "ntp.aliyun.com");
        XWidget_setGeometry((XWidget*)s_td.m_ntpServer, 80, 136, 300, 24);
        XWidget_show((XWidget*)s_td.m_ntpServer);
    }
    {
        XPushButton* btn = XPushButton_create(s_td.m_content, 0);
        s_td.m_ntpSyncBtn = btn;
        if (btn) {
            XPushButton_setText_2(btn, "立即校时");
            XWidget_setGeometry((XWidget*)btn, 12, 172, 110, 30);
            XObject_connect_2((XObject*)btn,
                              XSignal(XAbstractButton_clicked_signal),
                              td_ntpSyncSlot);
            XWidget_show((XWidget*)btn);
        }
    }
    s_td.m_ntpAuto = XCheckBox_create(s_td.m_content, 0);
    if (s_td.m_ntpAuto) {
        XCheckBox_setText_2(s_td.m_ntpAuto, "自动校时(每小时)");
        XWidget_setGeometry((XWidget*)s_td.m_ntpAuto, 140, 178, 200, 22);
        /* 勾选即时生效（无需点「立即校时」才应用周期） */
        XObject_connect_2((XObject*)s_td.m_ntpAuto,
                          XSignal(XAbstractButton_toggled_signal),
                          td_onNtpAutoToggled);
        XWidget_show((XWidget*)s_td.m_ntpAuto);
    }
s_td.m_ntpStatusCap = td_label(s_td.m_content, "状态", 12, 220, 64, 22);
    s_td.m_ntpStatus = XLabel_create(s_td.m_content, 0);
    if (s_td.m_ntpStatus) {
        XLabel_setText_2(s_td.m_ntpStatus, "状态: 未同步");
        XLabel_setTextPixelSize(s_td.m_ntpStatus, 13);
        XLabel_setAlignment(s_td.m_ntpStatus,
                            XAlignment_Left | XAlignment_Top);
        XWidget_setGeometry((XWidget*)s_td.m_ntpStatus, 12, 244, 368, 88);
        XWidget_show((XWidget*)s_td.m_ntpStatus);
    }
    /* 校时记录区：校时成功后回填服务器时间与本机偏差（快照语义）。 */
    s_td.m_ntpTime = XLabel_create(s_td.m_content, 0);
    if (s_td.m_ntpTime) {
        XLabel_setText_2(s_td.m_ntpTime, "最近一次校时: (尚未校时)");
        XLabel_setTextPixelSize(s_td.m_ntpTime, 14);
        XLabel_setAlignment(s_td.m_ntpTime,
                            XAlignment_Left | XAlignment_Top);
        XWidget_setGeometry((XWidget*)s_td.m_ntpTime, 12, 336, 368, 88);
        XWidget_show((XWidget*)s_td.m_ntpTime);
    }
    /* 系统时间刷新定时器：挂内容件（与系统时间标签同父同生共死，
     * 防标签先析构后定时器仍回调的悬垂写入）。
     * 1s 周期——秒级时间显示的最低成本节奏；启动后立即刷一次首帧，避免
     * 首秒显示占位文本。 */
    s_td.m_sysTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (s_td.m_sysTimer) {
        XTimer_setInterval(s_td.m_sysTimer, 1000);
        XTimer_setTimerCallback(s_td.m_sysTimer, td_sysTimeTimerCb);
        XTimer_setUserData(s_td.m_sysTimer, NULL);
        XObject_setParent((XObject*)s_td.m_sysTimer,
                          (XObject*)s_td.m_content);
        XTimer_start_base(s_td.m_sysTimer);
    }
    td_refreshSysTime();
#endif /* XNTP_ON */

#if XDDNS_ON
    /* ---- DDNS 右列（窄屏档在 adapt 重排为单列次段） ---- */
    s_td.m_ddnsTitle = td_label(s_td.m_content, "DDNS 动态域名", 400, 8, 380, 24);
    /* 服务商下拉框：枚举注册表填充；切换即恢复该厂商默认端点 */
    s_td.m_ddnsProviderCap = td_label(s_td.m_content, "服务商", 400, 46, 110, 22);
    s_td.m_ddnsProvider = XComboBox_create(s_td.m_content, 0);
    if (s_td.m_ddnsProvider) {
        int pi;
        int count = XDdnsProvider_count();
        for (pi = 0; pi < count; ++pi) {
            const XDdnsProviderOps* ops = XDdnsProvider_at(pi);
            if (ops)
                XComboBox_addItem_2(s_td.m_ddnsProvider, ops->m_name);
        }
        /* 默认选中 aliyun（注册表 0 号位；查找兜底防顺序调整） */
        for (pi = 0; pi < count; ++pi) {
            const XDdnsProviderOps* ops = XDdnsProvider_at(pi);
            if (ops && strcmp(ops->m_name, XDDNS_PROVIDER_ALIYUN) == 0) {
                XComboBox_setCurrentIndex(s_td.m_ddnsProvider, pi);
                break;
            }
        }
        XObject_connect_2((XObject*)s_td.m_ddnsProvider,
                          XSignal(XComboBox_currentIndexChanged_signal),
                          td_onProviderChanged);
        XWidget_setGeometry((XWidget*)s_td.m_ddnsProvider, 516, 44, 252, 24);
        XWidget_show((XWidget*)s_td.m_ddnsProvider);
    }
    s_td.m_ddnsKeyIdCap = td_label(s_td.m_content, "AccessKeyId", 400, 76, 110, 22);
    s_td.m_ddnsKeyId = XLineEdit_create(s_td.m_content, 0);
    if (s_td.m_ddnsKeyId) {
        XWidget_setGeometry((XWidget*)s_td.m_ddnsKeyId, 516, 74, 252, 24);
        XWidget_show((XWidget*)s_td.m_ddnsKeyId);
    }
    {
        /* 15 个 ASCII 字符按默认 16px 点阵（8px/字）约 120px 宽，110px
         * 标签框必裁尾（实测裁成「AccessKeySecr」）；压到 14px 约 105px
         * 完整落框——行内标签与输入框坐标零移动，桌面双列现状不动。 */
        s_td.m_ddnsSecretCap = td_label(s_td.m_content, "AccessKeySecret",
                                        400, 106, 110, 22);
        if (s_td.m_ddnsSecretCap)
            XLabel_setTextPixelSize(s_td.m_ddnsSecretCap, 14);
    }
    s_td.m_ddnsSecret = XLineEdit_create(s_td.m_content, 0);
    if (s_td.m_ddnsSecret) {
        XLineEdit_setEchoMode(s_td.m_ddnsSecret,
                              (int)XLineEditEchoMode_Password);
        XWidget_setGeometry((XWidget*)s_td.m_ddnsSecret, 516, 104, 252, 24);
        XWidget_show((XWidget*)s_td.m_ddnsSecret);
    }
    s_td.m_ddnsDomainCap = td_label(s_td.m_content, "主域名", 400, 136, 110, 22);
    s_td.m_ddnsDomain = XLineEdit_create(s_td.m_content, 0);
    if (s_td.m_ddnsDomain) {
        XLineEdit_setText(s_td.m_ddnsDomain, "example.com");
        XWidget_setGeometry((XWidget*)s_td.m_ddnsDomain, 516, 134, 252, 24);
        XWidget_show((XWidget*)s_td.m_ddnsDomain);
    }
    s_td.m_ddnsRrCap = td_label(s_td.m_content, "主机记录", 400, 166, 110, 22);
    s_td.m_ddnsRr = XLineEdit_create(s_td.m_content, 0);
    if (s_td.m_ddnsRr) {
        XLineEdit_setText(s_td.m_ddnsRr, "www");
        XWidget_setGeometry((XWidget*)s_td.m_ddnsRr, 516, 164, 110, 24);
        XWidget_show((XWidget*)s_td.m_ddnsRr);
    }
    s_td.m_ddnsV6 = XCheckBox_create(s_td.m_content, 0);
    if (s_td.m_ddnsV6) {
        XCheckBox_setText_2(s_td.m_ddnsV6, "IPv6(AAAA)");
        XWidget_setGeometry((XWidget*)s_td.m_ddnsV6, 640, 166, 130, 22);
        XWidget_show((XWidget*)s_td.m_ddnsV6);
    }
    {
        XPushButton* btn = XPushButton_create(s_td.m_content, 0);
        s_td.m_ddnsCheckBtn = btn;
        if (btn) {
            XPushButton_setText_2(btn, "立即检查");
            XWidget_setGeometry((XWidget*)btn, 400, 198, 110, 30);
            XObject_connect_2((XObject*)btn,
                              XSignal(XAbstractButton_clicked_signal),
                              td_ddnsCheckSlot);
            XWidget_show((XWidget*)btn);
        }
    }
    s_td.m_ddnsAuto = XCheckBox_create(s_td.m_content, 0);
    if (s_td.m_ddnsAuto) {
        XCheckBox_setText_2(s_td.m_ddnsAuto, "自动检查(10分钟)");
        XWidget_setGeometry((XWidget*)s_td.m_ddnsAuto, 530, 204, 200, 22);
        /* 勾选即时生效 */
        XObject_connect_2((XObject*)s_td.m_ddnsAuto,
                          XSignal(XAbstractButton_toggled_signal),
                          td_onDdnsAutoToggled);
        XWidget_show((XWidget*)s_td.m_ddnsAuto);
    }
    s_td.m_ddnsStatusCap = td_label(s_td.m_content, "状态", 400, 240, 64, 22);
    s_td.m_ddnsStatus = XLabel_create(s_td.m_content, 0);
    if (s_td.m_ddnsStatus) {
        XLabel_setText_2(s_td.m_ddnsStatus, "状态: 未就绪");
        XLabel_setTextPixelSize(s_td.m_ddnsStatus, 13);
        XLabel_setAlignment(s_td.m_ddnsStatus,
                            XAlignment_Left | XAlignment_Top);
        XWidget_setGeometry((XWidget*)s_td.m_ddnsStatus, 400, 264, 368, 200);
        XWidget_show((XWidget*)s_td.m_ddnsStatus);
    }
#endif /* XDDNS_ON */

    return s_td.m_root;
#endif /* 模块开关 */
}

/* ========================================================================== */
/*                        自适应与自测                                       */
/* ========================================================================== */

/* ---- 窄屏（竖屏）档度量 ----
 * 触控行高统一 40 逻辑像素：按钮/复选框/编辑框原尺寸（22~30）低于
 * 手机触控目标下限，真机上是误触与「点不中」的主要来源；桌面双列档
 * 不套用（保持 1000x700 现状），仅窄屏重排时生效。 */
#define TD_TWO_COL_MIN_W   768  /**< 双列承载下限：右列右缘 400+368=768，
                                     视口不足即收单列（否则必裁列）。 */
#define TD_MARGIN          12   /**< 页左右边距/条目横向留白。 */
#define TD_TOUCH_H         40   /**< 可点击行统一行高（触控目标下限）。 */
#define TD_TITLE_H         24   /**< 段标题高。 */
#define TD_CAP_H           22   /**< 行内标签高（不可点，维持原尺寸）。 */
#define TD_CAP_NTP_W       64   /**< NTP 行内标签列宽（3 个全角字）。 */
#define TD_CAP_DDNS_W      110  /**< DDNS 行内标签列宽（容纳 14px 的
                                       AccessKeySecret 全词）。 */
#define TD_BTN_W           130  /**< 主操作按钮宽（4 个全角字 + 内边距）。 */
#define TD_STATUS_H        88   /**< NTP 状态/时间区高（4 行 13px + 呼吸）。 */
#define TD_SYS_TIME_H      60   /**< 系统时间卡值区高（时刻 + 同步状态两行）。 */
#define TD_DDNS_STATUS_MIN 100  /**< DDNS 状态区最小高（同宽屏档口径）。 */

/** @brief 便捷：控件非空才落几何（重排函数对个别创建失败容忍）。 */
static void td_move(XWidget* w, int x, int y, int cw, int ch)
{
    if (w)
        XWidget_setGeometry(w, x, y, cw, ch);
}

/** @brief 宽屏档全量重排：桌面双列现状坐标（与 build 字面一致），
 *         仅两块状态区随视口高伸缩。幂等——窄屏切回宽屏靠它还原。 */
static int td_reflowWide(int vw, int vh)
{
    (void)vw; /* 双列坐标钉死（现状口径），不随视口宽缩放。 */
#if XNTP_ON
/* [系统时间条 2026-10-25] 左列整体下移 92px 容纳顶部系统时间卡：
     *   系统时间标题 y=8 / 系统时间值 y=34..94（固定高 60）
     *   NTP 标题 y=100 / 服务器 136 / 按钮 172 / 状态标题 220
     *   状态区 y=244（固定 88）/ 时间区 y=336 起随根高伸展
     * （坐标与 build 字面一致，切换窄屏档靠本函数还原——幂等口径） */
    td_move((XWidget*)s_td.m_sysTimeCap, 12, 8, 380, 24);
    td_move((XWidget*)s_td.m_sysTime, 12, 34, 368, 60);
    td_move((XWidget*)s_td.m_ntpTitle, 12, 100, 360, 24);
    td_move((XWidget*)s_td.m_ntpServerCap, 12, 138, 64, 22);
    td_move((XWidget*)s_td.m_ntpServer, 80, 136, 300, 24);
    td_move((XWidget*)s_td.m_ntpSyncBtn, 12, 172, 110, 30);
    td_move((XWidget*)s_td.m_ntpAuto, 140, 178, 200, 22);
    td_move((XWidget*)s_td.m_ntpStatusCap, 12, 220, 64, 22);
    td_move((XWidget*)s_td.m_ntpStatus, 12, 244, 368, 88);
    td_move((XWidget*)s_td.m_ntpTime, 12, 336, 368,
            vh - 360 > 60 ? vh - 360 : 60);
#endif
#if XDDNS_ON
    td_move((XWidget*)s_td.m_ddnsTitle, 400, 8, 380, 24);
    td_move((XWidget*)s_td.m_ddnsProviderCap, 400, 46, 110, 22);
    td_move((XWidget*)s_td.m_ddnsProvider, 516, 44, 252, 24);
    td_move((XWidget*)s_td.m_ddnsKeyIdCap, 400, 76, 110, 22);
    td_move((XWidget*)s_td.m_ddnsKeyId, 516, 74, 252, 24);
    td_move((XWidget*)s_td.m_ddnsSecretCap, 400, 106, 110, 22);
    td_move((XWidget*)s_td.m_ddnsSecret, 516, 104, 252, 24);
    td_move((XWidget*)s_td.m_ddnsDomainCap, 400, 136, 110, 22);
    td_move((XWidget*)s_td.m_ddnsDomain, 516, 134, 252, 24);
    td_move((XWidget*)s_td.m_ddnsRrCap, 400, 166, 110, 22);
    td_move((XWidget*)s_td.m_ddnsRr, 516, 164, 110, 24);
    td_move((XWidget*)s_td.m_ddnsV6, 640, 166, 130, 22);
    td_move((XWidget*)s_td.m_ddnsCheckBtn, 400, 198, 110, 30);
    td_move((XWidget*)s_td.m_ddnsAuto, 530, 204, 200, 22);
    td_move((XWidget*)s_td.m_ddnsStatusCap, 400, 240, 64, 22);
    td_move((XWidget*)s_td.m_ddnsStatus, 400, 264, 368,
            vh - 288 > TD_DDNS_STATUS_MIN ? vh - 288 : TD_DDNS_STATUS_MIN);
#endif
    return vh > 0 ? vh : 1; /* 双列恰一屏：内容高=视口高，竖滚不发生。 */
}

/** @brief 窄屏档全量重排：单列纵向堆叠（NTP 段在上、DDNS 段在后，
 *         行宽随视口收缩），可点行高统一 TD_TOUCH_H。
 *  @return 内容首选高——超出视口的部分由页根竖滚条承载，DDNS 段
 *          滚动可达（替代旧的「钉 x=400 被裁出屏外」）。 */
static int td_reflowNarrow(int vw, int vh)
{
    int y = 8;
    int w = vw - TD_MARGIN * 2; /* 内容列满宽（左右各留边距）。 */
    int contentH;
#if !XNTP_ON && !XDDNS_ON
    (void)w; /* 两段皆裁剪的兜底配置：仅保本函数可编译。 */
#endif
#if XNTP_ON
    int fieldX = TD_MARGIN + TD_CAP_NTP_W + 8;
    /* 系统时间卡置首（与宽屏档同序：本机时间在上、NTP 校时在下）。 */
    td_move((XWidget*)s_td.m_sysTimeCap, TD_MARGIN, y, w, TD_TITLE_H);
    y += TD_TITLE_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_sysTime, TD_MARGIN, y, w, TD_SYS_TIME_H);
    y += TD_SYS_TIME_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ntpTitle, TD_MARGIN, y, w, TD_TITLE_H);
    y += TD_TITLE_H + TD_MARGIN;
    /* 服务器行：标签行内垂直居中（XLabel 缺省 Left|VCenter，加高即居中）。 */
    td_move((XWidget*)s_td.m_ntpServerCap, TD_MARGIN, y,
            TD_CAP_NTP_W, TD_TOUCH_H);
    td_move((XWidget*)s_td.m_ntpServer, fieldX, y,
            vw - fieldX - TD_MARGIN, TD_TOUCH_H);
    y += TD_TOUCH_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ntpSyncBtn, TD_MARGIN, y, TD_BTN_W, TD_TOUCH_H);
    td_move((XWidget*)s_td.m_ntpAuto, TD_MARGIN + TD_BTN_W + TD_MARGIN, y,
            vw - (TD_MARGIN + TD_BTN_W + TD_MARGIN) - TD_MARGIN, TD_TOUCH_H);
    y += TD_TOUCH_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ntpStatusCap, TD_MARGIN, y, TD_CAP_NTP_W, TD_CAP_H);
    y += TD_CAP_H + 6;
    td_move((XWidget*)s_td.m_ntpStatus, TD_MARGIN, y, w, TD_STATUS_H);
    y += TD_STATUS_H + 8;
    td_move((XWidget*)s_td.m_ntpTime, TD_MARGIN, y, w, TD_STATUS_H);
    y += TD_STATUS_H + 16; /* 段间距拉开一档，视觉分隔两段功能。 */
#endif
#if XDDNS_ON
    int dFieldX = TD_MARGIN + TD_CAP_DDNS_W + 6;
    int dFieldW = vw - dFieldX - TD_MARGIN;
    td_move((XWidget*)s_td.m_ddnsTitle, TD_MARGIN, y, w, TD_TITLE_H);
    y += TD_TITLE_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ddnsProviderCap, TD_MARGIN, y,
            TD_CAP_DDNS_W, TD_TOUCH_H);
    td_move((XWidget*)s_td.m_ddnsProvider, dFieldX, y, dFieldW, TD_TOUCH_H);
    y += TD_TOUCH_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ddnsKeyIdCap, TD_MARGIN, y,
            TD_CAP_DDNS_W, TD_TOUCH_H);
    td_move((XWidget*)s_td.m_ddnsKeyId, dFieldX, y, dFieldW, TD_TOUCH_H);
    y += TD_TOUCH_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ddnsSecretCap, TD_MARGIN, y,
            TD_CAP_DDNS_W, TD_TOUCH_H);
    td_move((XWidget*)s_td.m_ddnsSecret, dFieldX, y, dFieldW, TD_TOUCH_H);
    y += TD_TOUCH_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ddnsDomainCap, TD_MARGIN, y,
            TD_CAP_DDNS_W, TD_TOUCH_H);
    td_move((XWidget*)s_td.m_ddnsDomain, dFieldX, y, dFieldW, TD_TOUCH_H);
    y += TD_TOUCH_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ddnsRrCap, TD_MARGIN, y,
            TD_CAP_DDNS_W, TD_TOUCH_H);
    td_move((XWidget*)s_td.m_ddnsRr, dFieldX, y, dFieldW, TD_TOUCH_H);
    y += TD_TOUCH_H + TD_MARGIN;
    /* IPv6 复选框独占一行：400 宽下「标签+输入框+复选框」同行放不下。 */
    td_move((XWidget*)s_td.m_ddnsV6, TD_MARGIN, y, w, TD_TOUCH_H);
    y += TD_TOUCH_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ddnsCheckBtn, TD_MARGIN, y, TD_BTN_W, TD_TOUCH_H);
    td_move((XWidget*)s_td.m_ddnsAuto, TD_MARGIN + TD_BTN_W + TD_MARGIN, y,
            vw - (TD_MARGIN + TD_BTN_W + TD_MARGIN) - TD_MARGIN, TD_TOUCH_H);
    y += TD_TOUCH_H + TD_MARGIN;
    td_move((XWidget*)s_td.m_ddnsStatusCap, TD_MARGIN, y, TD_CAP_NTP_W, TD_CAP_H);
    y += TD_CAP_H + 6;
#endif
    /* 状态区吃掉剩余全部首选高：内容不足一屏时铺满视口（防底部
     * 空洞/滚动抖动），超一屏时保持最小高、超出部分交给竖滚条。 */
    contentH = y + TD_DDNS_STATUS_MIN;
    if (contentH < vh)
        contentH = vh;
#if XDDNS_ON
    td_move((XWidget*)s_td.m_ddnsStatus, TD_MARGIN, y, w, contentH - y);
#endif
    return contentH;
}

/** @brief 按视口尺寸分档重排并把首选高落到内容件/滚动范围。
 *  @note  adapt 与 autotest（窄屏档回归验后还原）共用此唯一入口，
 *         保证两处口径永不漂移。 */
static void td_mountContent(int vw, int vh)
{
    int contentH = (vw >= TD_TWO_COL_MIN_W) ? td_reflowWide(vw, vh)
                                            : td_reflowNarrow(vw, vh);
    XWidget_setGeometry(s_td.m_content, 0, 0, vw, contentH);
    /* 内容尺寸驱动滚动范围（宽=视口宽 ⇒ 水平恒无滚动需求，叠加
     * AlwaysOff 双保险；高超出部分即竖滚量）。 */
    XAbstractScrollArea_setContentSize((XAbstractScrollArea*)s_td.m_root,
                                       vw, contentH > vh ? contentH : vh);
}

/**
 * @brief 页面自适应（切页/resize/startup/停靠切换统一漏斗的页内入口）。
 * @details 旧实现只伸缩两块状态区高度、x/y 恒为 build 字面——400 逻辑
 *          宽竖屏下右列（x=400..768）整体被裁出屏右缘且无滚动承载，
 *          半页功能不可用。现按视口宽分档全量重排：≥768 保持桌面双列
 *          现状（1000x700 口径零变化）；<768 收单列堆叠 + 40 触控行高，
 *          重排后的内容首选高经内容件落几何并驱动滚动范围（页根
 *          XScrollArea 竖滚承载，内容宽恒=视口宽、横向不滚）。
 */
void demo_page_timeddns_adapt(XWidget* page)
{
    XWidget* viewport;
    int vw;
    int vh;
    if (page != s_td.m_root || !s_td.m_root || !s_td.m_content)
        return;
    viewport = XAbstractScrollArea_viewport(
        (XAbstractScrollArea*)s_td.m_root);
    if (!viewport)
        return;
    vw = XWidget_width(viewport);
    vh = XWidget_height(viewport);
    if (vw < 1)
        return; /* 视口未就位（堆叠尚未分配几何）不猜，帧泵兜底门会重入。 */
    if (vh < 1)
        vh = 1;
    td_mountContent(vw, vh);
}

/**
 * @brief 页面自测（非阻塞、不触真实网络）：
 *   1. 控件装配齐全；
 *   2. NTP 清空服务器触发 → NoServer 即时失败（不发包）；
 *   3. DDNS 无凭据触发 → NoConfig 即时失败（配置检查先于网络）；
 *   4. 结束恢复默认值。
 */
int demo_page_timeddns_autotest(XWidget* page)
{
    int failures = 0;
    if (page != s_td.m_root)
        return -1;

#define TD_CHECK(cond, desc)                                                \
    do {                                                                    \
        if (cond)                                                           \
            XPrintf("XGuiAutoTest: [PASS] %s\n", desc);                     \
        else {                                                              \
            XPrintf("XGuiAutoTest: [FAIL] %s\n", desc);                     \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

#if XNTP_ON
    /* 真网同步诊断（默认关；XGUI_TD_REALSYNC=1 时对真实 NTP 服务器走
     * 完整 GUI 流程，用于定位「点了没反应」卡在哪一环节）。 */
    if (getenv("XGUI_TD_REALSYNC")) {
        int pumped = 0;
        XLineEdit_setText(s_td.m_ntpServer, "ntp.aliyun.com");
        XPrintf("XGuiAutoTest: [REAL] sync ntp.aliyun.com...\n");
        td_ntpSyncClicked();
        while (pumped < 2400
               && XNtpClient_state(s_td.m_ntp) != XNtpClientState_Synchronized
               && XNtpClient_state(s_td.m_ntp) != XNtpClientState_Failed) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(5);
            ++pumped;
        }
        XPrintf("XGuiAutoTest: [REAL] state=%d err=%d offset=%lld delay=%lld "
                "(pump %d)\n",
                (int)XNtpClient_state(s_td.m_ntp),
                (int)XNtpClient_lastError(s_td.m_ntp),
                (long long)XNtpClient_offsetMs(s_td.m_ntp),
                (long long)XNtpClient_roundTripMs(s_td.m_ntp),
                pumped);
        TD_CHECK(XNtpClient_state(s_td.m_ntp)
                     == XNtpClientState_Synchronized,
                 "真网校时同步成功（GUI 进程内）");
        /* 时间标签确实显示了服务器时间（用户请求：成功后显示时间） */
        if (s_td.m_ntpTime) {
            const XString* labelText = XLabel_text(s_td.m_ntpTime);
            TD_CHECK(labelText != NULL
                         && strstr(XString_toUtf8(labelText),
                                   "校时成功") != NULL
                         && strstr(XString_toUtf8(labelText),
                                   "UTC") != NULL,
                     "时间标签显示服务器时间(UTC)");
        }
        /* 恢复默认服务器文本 */
        XLineEdit_setText(s_td.m_ntpServer, "ntp.aliyun.com");
    }

    TD_CHECK(s_td.m_ntpServer != NULL, "NTP 服务器编辑框存在");
    TD_CHECK(s_td.m_ntpAuto != NULL, "NTP 自动校时复选框存在");
    TD_CHECK(s_td.m_ntpStatus != NULL, "NTP 状态区存在");

    /* 清空服务器后触发：NoServer 即时拒绝，不发起任何网络包 */
    if (s_td.m_ntpServer) {
        XLineEdit_setText(s_td.m_ntpServer, "");
        TD_CHECK(td_ntpSyncClicked() == false, "空服务器时校时被拒");
        /* 状态感知断言：此前若已真网同步过（客户端存在且 Synchronized），
           空服务器拒绝不得破坏已同步态；首次（未创建客户端）则保持 Idle。 */
        if (s_td.m_ntp) {
            TD_CHECK(XNtpClient_isSynchronized(s_td.m_ntp),
                     "拒绝路径不破坏已同步状态");
        } else {
            TD_CHECK(XNtpClient_state(s_td.m_ntp)
                         == XNtpClientState_Idle,
                     "入口拒绝保持 Idle（未创建客户端）");
        }
        /* 恢复默认 */
        XLineEdit_setText(s_td.m_ntpServer, "ntp.aliyun.com");
    }
#endif

#if XDDNS_ON
    /* 真网 DDNS 诊断（默认关；XGUI_TD_DDNSREAL=1 且设置了
     * XDNS_AKID/XDNS_SECRET 时对真实阿里云 API 走完整 GUI 流程）。 */
    if (getenv("XGUI_TD_DDNSREAL") && getenv("XDNS_AKID")
        && getenv("XDNS_SECRET")) {
        int pumped = 0;
        XLineEdit_setText(s_td.m_ddnsKeyId, getenv("XDNS_AKID"));
        XLineEdit_setText(s_td.m_ddnsSecret, getenv("XDNS_SECRET"));
        XLineEdit_setText(s_td.m_ddnsDomain,
                          getenv("XDNS_DOMAIN") ? getenv("XDNS_DOMAIN")
                                                : "727370630.top");
        XLineEdit_setText(s_td.m_ddnsRr,
                          getenv("XDNS_RR") ? getenv("XDNS_RR") : "www");
        XPrintf("XGuiAutoTest: [DDNS-REAL] check against aliyun...\n");
        /* 走真实按钮路径：click 触发信号→槽，与用户点击完全同路
           （直调 td_ddnsCheckClicked 会绕过信号系统，可能漏掉真实故障）。 */
        if (s_td.m_ddnsCheckBtn) {
            XAbstractButton_click((XAbstractButton*)s_td.m_ddnsCheckBtn);
        } else {
            XPrintf("[DDNS-REAL] 无按钮成员，回退直调槽\n");
            td_ddnsCheckClicked();
        }
        while (pumped < 3000
               && XDdnsClient_state(s_td.m_ddns)
                      != XDdnsClientState_Synchronized
               && XDdnsClient_state(s_td.m_ddns)
                      != XDdnsClientState_Failed) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(5);
            ++pumped;
        }
        XPrintf("XGuiAutoTest: [DDNS-REAL] state=%d err=%d http=%d code=%s ip=%s\n",
                (int)XDdnsClient_state(s_td.m_ddns),
                (int)XDdnsClient_lastError(s_td.m_ddns),
                XDdnsClient_lastHttpStatus(s_td.m_ddns),
                XDdnsClient_lastApiCode_const(s_td.m_ddns),
                XDdnsClient_currentIp_const(s_td.m_ddns)
                    ? XString_toUtf8(XDdnsClient_currentIp_const(s_td.m_ddns))
                    : "-");
        TD_CHECK(XDdnsClient_state(s_td.m_ddns)
                     == XDdnsClientState_Synchronized,
                 "真网 DDNS 全链路同步成功（GUI 进程内）");
    }

    TD_CHECK(s_td.m_ddnsKeyId != NULL, "AccessKeyId 编辑框存在");
    TD_CHECK(s_td.m_ddnsSecret != NULL, "Secret 编辑框存在");
    TD_CHECK(s_td.m_ddnsDomain != NULL && s_td.m_ddnsRr != NULL,
             "域名/主机记录编辑框存在");
    TD_CHECK(s_td.m_ddnsV6 != NULL && s_td.m_ddnsAuto != NULL,
             "IPv6/自动检查复选框存在");
    TD_CHECK(s_td.m_ddnsStatus != NULL, "DDNS 状态区存在");
    TD_CHECK(s_td.m_ddnsProvider != NULL, "服务商下拉框存在");
    TD_CHECK(XComboBox_currentIndex(s_td.m_ddnsProvider) == 0
                 && XDdnsProvider_count() >= 1
                 && XDdnsProvider_at(0) != NULL
                 && strcmp(XDdnsProvider_at(0)->m_name,
                           XDDNS_PROVIDER_ALIYUN) == 0,
             "下拉框默认选中阿里云（注册表 0 号位）");
    TD_CHECK(XDdnsProvider_at(-1) == NULL
                 && XDdnsProvider_at(XDdnsProvider_count()) == NULL,
             "注册表越界访问返回 NULL");

    /* 无凭据触发：NoConfig 即时失败（配置检查先于任何网络动作） */
    TD_CHECK(td_ddnsCheckClicked() == false, "无凭据时检查被拒");
    /* 拒绝发生在入口（checkNow 未调用）：lastError 保持 None、Idle */
    TD_CHECK(XDdnsClient_lastError(s_td.m_ddns) == XDdnsError_None,
             "入口拒绝不置错误码");
    TD_CHECK(XDdnsClient_state(s_td.m_ddns) == XDdnsClientState_Idle,
             "客户端保持 Idle");
#endif

#if XNTP_ON
    /* 窄屏（竖屏）档重排回归（纯几何，无事件注入、无网络）：400 逻辑
     * 宽单列排布须满足——可点目标行高 ≥40（手机触控目标下限）、DDNS
     * 控件不越右缘且排在 NTP 段下方（旧布局钉 x=400 被整体裁出屏外、
     * 半页不可用的回归闸）。验后按视口实际尺寸还原，真实重排仍由
     * adapt 漏斗全责。 */
    {
        XWidget* vp = XAbstractScrollArea_viewport(
            (XAbstractScrollArea*)s_td.m_root);
        int vw = XWidget_width(vp);
        int vh = XWidget_height(vp);
        if (vh < 1)
            vh = 1;
        td_mountContent(400, 700);
        TD_CHECK(XWidget_height((XWidget*)s_td.m_ntpSyncBtn) >= 40
                     && XWidget_height((XWidget*)s_td.m_ntpServer) >= 40
                     && XWidget_height((XWidget*)s_td.m_ntpAuto) >= 40,
                 "窄屏 NTP 可点目标高 ≥40");
        /* [系统时间条 2026-10-25] 系统时间卡同样纳入窄屏适配口径：
         *   ①不越 400 右缘（随视口收缩，非钉死宽）；②置于 NTP 段之前
         *   （本机时间在上、NTP 校时在下的对照序不被窄屏打乱）；
         *   ③高度不塌缩（两行文本可读）。 */
        TD_CHECK(XWidget_x((XWidget*)s_td.m_sysTime)
                         + XWidget_width((XWidget*)s_td.m_sysTime)
                     <= 400,
                 "窄屏系统时间卡不越 400 右缘");
        TD_CHECK(XWidget_y((XWidget*)s_td.m_sysTimeCap)
                     < XWidget_y((XWidget*)s_td.m_ntpTitle),
                 "窄屏系统时间卡在 NTP 段之前");
        TD_CHECK(XWidget_height((XWidget*)s_td.m_sysTime) >= 40,
                 "窄屏系统时间卡高度不塌缩");
#if XDDNS_ON
        TD_CHECK(XWidget_x((XWidget*)s_td.m_ddnsKeyId)
                         + XWidget_width((XWidget*)s_td.m_ddnsKeyId)
                     <= 400,
                 "窄屏 DDNS 控件不越 400 右缘");
        TD_CHECK(XWidget_height((XWidget*)s_td.m_ddnsCheckBtn) >= 40
                     && XWidget_height((XWidget*)s_td.m_ddnsProvider) >= 40
                     && XWidget_height((XWidget*)s_td.m_ddnsSecret) >= 40,
                 "窄屏 DDNS 可点目标高 ≥40");
        TD_CHECK(XWidget_y((XWidget*)s_td.m_ddnsTitle)
                     > XWidget_y((XWidget*)s_td.m_ntpTime),
                 "窄屏 DDNS 段在 NTP 段下方（单列堆叠）");
#endif
        /* 验后还原当前档位（与 adapt 同口径同入口）。 */
        td_mountContent(vw > 0 ? vw : 400, vh);
    }
#endif

#undef TD_CHECK
    (void)page;
    return failures;
}
