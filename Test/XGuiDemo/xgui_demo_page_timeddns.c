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
#include "XComboBox.h"
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
    XWidget* m_root;           /**< 页面根控件。 */

    /* ---- NTP 控件 ---- */
    XLineEdit* m_ntpServer;    /**< 服务器编辑框。 */
    XCheckBox* m_ntpAuto;      /**< 自动校时复选框。 */
    XLabel* m_ntpStatus;       /**< NTP 状态区（多行）。 */
    XLabel* m_ntpTime;         /**< 校时成功后的时间显示区。 */
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

    /* 时间显示区：同步成功后展示服务器时间（UTC）与本机偏差。 */
    if (s_td.m_ntpTime) {
        if (XNtpClient_isSynchronized(s_td.m_ntp)) {
            XDateTime server;
            XString* text;
            XDateTime_setSecsSinceEpoch(
                &server, (int64_t)XNtpClient_serverSecs(s_td.m_ntp));
            text = XDateTime_toString_iso(&server);
            XSnprintf(buf, sizeof(buf),
                      "校时成功\n服务器时间(UTC): %s\n本机偏差: %lld ms "
                      "(时延 %lld ms)",
                      text ? XString_toUtf8(text) : "?",
                      (long long)XNtpClient_offsetMs(s_td.m_ntp),
                      (long long)XNtpClient_roundTripMs(s_td.m_ntp));
            if (text) XClassDelete(text);
            XLabel_setText_2(s_td.m_ntpTime, buf);
        } else {
            XLabel_setText_2(s_td.m_ntpTime, "时间: (尚未校时)");
        }
    }
}

/**
 * @brief NTP 同步成功信号槽：回填状态区 + 主窗口状态栏。
 */
static void td_onNtpSynced(XObject* sender, XVarList* args)
{
    XDdnsClient* unused = NULL;
    (void)sender; (void)args; (void)unused;
    td_refreshNtpStatus();
    if (s_td.m_status)
        s_td.m_status(s_td.m_user, "NTP 校时成功");
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
    s_td.m_root = XWidget_create(parent, 0);
    if (!s_td.m_root)
        return (XWidget*)0;

#if XNTP_ON
    /* ---- NTP 左列 ---- */
    td_label(s_td.m_root, "NTP 网络校时", 12, 8, 360, 24);
    td_label(s_td.m_root, "服务器", 12, 46, 64, 22);
    s_td.m_ntpServer = XLineEdit_create(s_td.m_root, 0);
    if (s_td.m_ntpServer) {
        XLineEdit_setText(s_td.m_ntpServer, "ntp.aliyun.com");
        XWidget_setGeometry((XWidget*)s_td.m_ntpServer, 80, 44, 300, 24);
        XWidget_show((XWidget*)s_td.m_ntpServer);
    }
    {
        XPushButton* btn = XPushButton_create(s_td.m_root, 0);
        if (btn) {
            XPushButton_setText_2(btn, "立即校时");
            XWidget_setGeometry((XWidget*)btn, 12, 80, 110, 30);
            XObject_connect_2((XObject*)btn,
                              XSignal(XAbstractButton_clicked_signal),
                              td_ntpSyncSlot);
            XWidget_show((XWidget*)btn);
        }
    }
    s_td.m_ntpAuto = XCheckBox_create(s_td.m_root, 0);
    if (s_td.m_ntpAuto) {
        XCheckBox_setText_2(s_td.m_ntpAuto, "自动校时(每小时)");
        XWidget_setGeometry((XWidget*)s_td.m_ntpAuto, 140, 86, 200, 22);
        /* 勾选即时生效（无需点「立即校时」才应用周期） */
        XObject_connect_2((XObject*)s_td.m_ntpAuto,
                          XSignal(XAbstractButton_toggled_signal),
                          td_onNtpAutoToggled);
        XWidget_show((XWidget*)s_td.m_ntpAuto);
    }
    td_label(s_td.m_root, "状态", 12, 128, 64, 22);
    s_td.m_ntpStatus = XLabel_create(s_td.m_root, 0);
    if (s_td.m_ntpStatus) {
        XLabel_setText_2(s_td.m_ntpStatus, "状态: 未同步");
        XLabel_setTextPixelSize(s_td.m_ntpStatus, 13);
        XLabel_setAlignment(s_td.m_ntpStatus,
                            XAlignment_Left | XAlignment_Top);
        XWidget_setGeometry((XWidget*)s_td.m_ntpStatus, 12, 152, 368, 88);
        XWidget_show((XWidget*)s_td.m_ntpStatus);
    }
    /* 时间显示区：校时成功后回填服务器时间与本机偏差 */
    s_td.m_ntpTime = XLabel_create(s_td.m_root, 0);
    if (s_td.m_ntpTime) {
        XLabel_setText_2(s_td.m_ntpTime, "时间: (尚未校时)");
        XLabel_setTextPixelSize(s_td.m_ntpTime, 14);
        XLabel_setAlignment(s_td.m_ntpTime,
                            XAlignment_Left | XAlignment_Top);
        XWidget_setGeometry((XWidget*)s_td.m_ntpTime, 12, 244, 368, 108);
        XWidget_show((XWidget*)s_td.m_ntpTime);
    }
#endif /* XNTP_ON */

#if XDDNS_ON
    /* ---- DDNS 右列 ---- */
    td_label(s_td.m_root, "DDNS 动态域名", 400, 8, 380, 24);
    /* 服务商下拉框：枚举注册表填充；切换即恢复该厂商默认端点 */
    td_label(s_td.m_root, "服务商", 400, 46, 110, 22);
    s_td.m_ddnsProvider = XComboBox_create(s_td.m_root, 0);
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
    td_label(s_td.m_root, "AccessKeyId", 400, 76, 110, 22);
    s_td.m_ddnsKeyId = XLineEdit_create(s_td.m_root, 0);
    if (s_td.m_ddnsKeyId) {
        XWidget_setGeometry((XWidget*)s_td.m_ddnsKeyId, 516, 74, 252, 24);
        XWidget_show((XWidget*)s_td.m_ddnsKeyId);
    }
    td_label(s_td.m_root, "AccessKeySecret", 400, 106, 110, 22);
    s_td.m_ddnsSecret = XLineEdit_create(s_td.m_root, 0);
    if (s_td.m_ddnsSecret) {
        XLineEdit_setEchoMode(s_td.m_ddnsSecret,
                              (int)XLineEditEchoMode_Password);
        XWidget_setGeometry((XWidget*)s_td.m_ddnsSecret, 516, 104, 252, 24);
        XWidget_show((XWidget*)s_td.m_ddnsSecret);
    }
    td_label(s_td.m_root, "主域名", 400, 136, 110, 22);
    s_td.m_ddnsDomain = XLineEdit_create(s_td.m_root, 0);
    if (s_td.m_ddnsDomain) {
        XLineEdit_setText(s_td.m_ddnsDomain, "example.com");
        XWidget_setGeometry((XWidget*)s_td.m_ddnsDomain, 516, 134, 252, 24);
        XWidget_show((XWidget*)s_td.m_ddnsDomain);
    }
    td_label(s_td.m_root, "主机记录", 400, 166, 110, 22);
    s_td.m_ddnsRr = XLineEdit_create(s_td.m_root, 0);
    if (s_td.m_ddnsRr) {
        XLineEdit_setText(s_td.m_ddnsRr, "www");
        XWidget_setGeometry((XWidget*)s_td.m_ddnsRr, 516, 164, 110, 24);
        XWidget_show((XWidget*)s_td.m_ddnsRr);
    }
    s_td.m_ddnsV6 = XCheckBox_create(s_td.m_root, 0);
    if (s_td.m_ddnsV6) {
        XCheckBox_setText_2(s_td.m_ddnsV6, "IPv6(AAAA)");
        XWidget_setGeometry((XWidget*)s_td.m_ddnsV6, 640, 166, 130, 22);
        XWidget_show((XWidget*)s_td.m_ddnsV6);
    }
    {
        XPushButton* btn = XPushButton_create(s_td.m_root, 0);
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
    s_td.m_ddnsAuto = XCheckBox_create(s_td.m_root, 0);
    if (s_td.m_ddnsAuto) {
        XCheckBox_setText_2(s_td.m_ddnsAuto, "自动检查(10分钟)");
        XWidget_setGeometry((XWidget*)s_td.m_ddnsAuto, 530, 204, 200, 22);
        /* 勾选即时生效 */
        XObject_connect_2((XObject*)s_td.m_ddnsAuto,
                          XSignal(XAbstractButton_toggled_signal),
                          td_onDdnsAutoToggled);
        XWidget_show((XWidget*)s_td.m_ddnsAuto);
    }
    td_label(s_td.m_root, "状态", 400, 240, 64, 22);
    s_td.m_ddnsStatus = XLabel_create(s_td.m_root, 0);
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

/** @brief 页面自适应：状态区随根高伸缩（底部留 24px 边距）。 */
void demo_page_timeddns_adapt(XWidget* page)
{
    int rootH;
    if (page != s_td.m_root || !s_td.m_root)
        return;
    rootH = (int)XWidget_height(s_td.m_root);
#if XNTP_ON
    if (s_td.m_ntpStatus)
        XWidget_setGeometry((XWidget*)s_td.m_ntpStatus, 12, 152, 368, 88);
    if (s_td.m_ntpTime)
        XWidget_setGeometry((XWidget*)s_td.m_ntpTime, 12, 244, 368,
                            rootH - 268 > 60 ? rootH - 268 : 60);
#endif
#if XDDNS_ON
    if (s_td.m_ddnsStatus)
        XWidget_setGeometry((XWidget*)s_td.m_ddnsStatus, 400, 264, 368,
                            rootH - 288 > 100 ? rootH - 288 : 100);
#endif
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

#undef TD_CHECK
    (void)page;
    return failures;
}
