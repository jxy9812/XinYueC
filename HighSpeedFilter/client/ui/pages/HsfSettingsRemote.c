/**
 * @file       HsfSettingsRemote.c
 * @brief      远端设置页：运行模式 / 自动运行 / 连接信息 / 采集配置 / 数据存储。
 * @details    [对齐 Python 客户端 RemoteSettingsPage] 五张卡、字段与动作逐项对应：
 *              ① 运行模式：模式文案 + 说明 + 切换到手动/自动（需 ModeSwitch）；
 *              ② 自动运行：运行态文案 + 说明 + 启动/停止（需 AutoControl）；
 *                 与模式切换**分开**——切自动模式只是"授权"，不等于"在跑"；
 *              ③ 连接信息：服务端地址 / 用户 / 角色；
 *              ④ 采集配置：5 路设备（搅拌泵/温湿度/浊度仪/模拟量/数字量）
 *                 开关 + 轮询周期，刷新/应用（需 AccountAdmin）；
 *              ⑤ 数据存储：启用/路径/入库周期/容量上限/到限策略/保留分段 +
 *                 容量实况 + 当前库文件，保存（需 AccountAdmin）。
 *              数据源：0x0116 配置快照、0x0115 自动运行、0x0105 运行模式，
 *              键名与 Python 端 proto.CFGK_* 一致。
 *              [回显时机] 只在**新快照到达**（configGetSeq 变化）时回填控件，
 *              否则 200ms 巡检会把用户正在改的值覆盖回去。
 */
#include "HsfSettingsRemote.h"

#include "HsfSettingsLocal.h" /* 巡检周期=本地设置「界面刷新周期」 */

#include "HsfLinkClient.h"
#include "HsfLinkConfig.h"
#include "HsfTheme.h"
#include "XBoxLayout.h"
#include "XCheckBox.h"
#include "XComboBox.h"
#include "XLabel.h"
#include "XLineEdit.h"
#include "XPushButton.h"
#include "XSpinBox.h"
#include "XTimer.h"
#include "XMemory.h"
#include "XString.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON

/** @brief 采集配置行数（与 Python 五行一一对应）。 */
#define SR_COLLECT_ROWS 5

/** @brief 轮询周期范围（与 Python CFG_POLL_MS_MIN/MAX 同值）。 */
#define SR_POLL_MS_MIN 50
#define SR_POLL_MS_MAX 60000
/** @brief 容量上限范围（MB；与 Python STORAGE_MAX_MB_* 同值；0=不限）。 */
#define SR_MAX_MB_MIN 0
#define SR_MAX_MB_MAX (1024 * 1024)

/** @brief 采集配置行（开关 + 周期；标签为静态串）。 */
typedef struct HsfSrCollectRow {
    XCheckBox m_chk;
    XSpinBox  m_spin;
} HsfSrCollectRow;

/** @brief 远端设置页对象。 */
typedef struct HsfSettingsRemote {
    XWidget      m_base;        /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;      /**< 页根竖排布局。 */
    XLabel       m_title;       /**< 页标题。 */
    /* ---- 运行模式 ---- */
    XLabel       m_mode;
    XLabel       m_modeHint;
    XPushButton  m_btnManual;
    XPushButton  m_btnAuto;
    /* ---- 自动运行 ---- */
    XLabel       m_auto;
    XLabel       m_autoHint;
    XPushButton  m_btnAutoStart;
    XPushButton  m_btnAutoStop;
    /* ---- 连接信息 ---- */
    XLabel       m_conn;
    /* ---- 采集配置 ---- */
    HsfSrCollectRow m_collect[SR_COLLECT_ROWS];
    XLabel       m_collectHint;
    XPushButton  m_btnCfgRefresh;
    XPushButton  m_btnCfgApply;
    /* ---- 数据存储 ---- */
    XCheckBox    m_storeEnable;
    XLineEdit    m_storePath;
    XSpinBox     m_storeSample;
    XSpinBox     m_storeMax;
    XComboBox    m_storePolicy;
    XSpinBox     m_storeKeep;
    XLabel       m_policyHint;
    XLabel       m_storageUse;
    XLabel       m_dbPath;
    XPushButton  m_btnSave;
    /* ---- 登录策略（账号）---- */
    XCheckBox    m_loginAnon;     /**< 允许匿名登录。 */
    XCheckBox    m_loginConcur;   /**< 允许同账并发。 */
    XSpinBox     m_loginMax;      /**< 会话上限（0=不限）。 */
    XLabel       m_loginHint;     /**< 策略说明/生效提示。 */
    XPushButton  m_btnLoginSave;  /**< 应用登录策略。 */
    /* ---- 运行期 ---- */
    XTimer*      m_timer;
    void*        m_statusUser;   /**< 状态栏宿主（HsfPages_setStatus 的 user）。 */
    bool         m_fetched;      /**< 已发过配置查询（断开后复位）。 */
    int          m_seenGetSeq;   /**< 已回填的快照序号。 */
    int          m_seenWriteSeq; /**< 已提示的写应答序号。 */
    int          m_seenCmdSeq;   /**< 已提示的控制应答序号（自动运行）。 */
    char         m_lastModeErr[96]; /**< 上次提示过的模式切换错误。 */
} HsfSettingsRemote;

/* ==================== 小工具 ==================== */

/** @brief 状态栏提示（userData 为导航宿主窗口；NULL 时忽略）。 */
static void srStatus(HsfSettingsRemote* self, const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    if (!self || !self->m_statusUser) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    HsfPages_setStatus(self->m_statusUser, buf);
}

/** @brief 只在值真变时 setEnabled（避免巡检每拍触发重绘）。 */
static void srEnable(XWidget* w, bool on)
{
    if (!w) return;
    if (XWidget_isEnabled(w) != on) XWidget_setEnabled(w, on);
}

/** @brief 采集行控件（开关/周期）启用态。 */
static void srEnableCollect(HsfSettingsRemote* self, bool on)
{
    int i;
    for (i = 0; i < SR_COLLECT_ROWS; ++i) {
        srEnable((XWidget*)&self->m_collect[i].m_chk, on);
        srEnable((XWidget*)&self->m_collect[i].m_spin, on);
    }
    srEnable((XWidget*)&self->m_btnCfgApply, on);
}

/* ==================== 槽：运行模式 / 自动运行 ==================== */

static void srManualSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                            offsetof(HsfSettingsRemote, m_btnManual));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_MODE_SWITCH)) {
        srStatus(self, "权限不足：切换运行模式需要管理员账户");
        return;
    }
    if (HsfLinkClient_setRunMode(false))
        srStatus(self, "正在切换到手动模式…");
    else
        srStatus(self, "切换失败：服务端未连接");
}

static void srAutoSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                            offsetof(HsfSettingsRemote, m_btnAuto));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_MODE_SWITCH)) {
        srStatus(self, "权限不足：切换运行模式需要管理员账户");
        return;
    }
    if (HsfLinkClient_setRunMode(true))
        srStatus(self, "正在切换到自动模式…");
    else
        srStatus(self, "切换失败：服务端未连接");
}

static void srAutoStartSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnAutoStart));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_AUTO_CONTROL)) {
        srStatus(self, "权限不足：自动运行控制需要管理员账户");
        return;
    }
    if (HsfLinkClient_autoControl(HSF_AUTO_OP_START))
        srStatus(self, "正在启动自动运行…");
    else
        srStatus(self, "操作失败：服务端未连接");
}

static void srAutoStopSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnAutoStop));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_AUTO_CONTROL)) {
        srStatus(self, "权限不足：自动运行控制需要管理员账户");
        return;
    }
    if (HsfLinkClient_autoControl(HSF_AUTO_OP_STOP))
        srStatus(self, "正在停止自动运行…");
    else
        srStatus(self, "操作失败：服务端未连接");
}

/* ==================== 槽：采集配置 / 数据存储 ==================== */

/** @brief 拉一次配置快照（刷新按钮/首次连接）。 */
static bool srFetchConfig(HsfSettingsRemote* self, bool silent)
{
    if (!self) return false;
    if (!HsfLinkClient_isReady()) {
        if (!silent) srStatus(self, "未连接服务端");
        return false;
    }
    (void)HsfLinkClient_queryRunMode();   /* 顺带对账模式/自动运行态 */
    if (!HsfLinkClient_configGet()) {
        if (!silent) srStatus(self, "读取配置失败：请求未发出");
        return false;
    }
    if (!silent) srStatus(self, "正在刷新配置…");
    return true;
}

static void srCfgRefreshSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnCfgRefresh));
    (void)args;
    srFetchConfig(self, false);
}

/** @brief 应用采集配置（开关即时生效；周期下一拍生效）。 */
static void srCfgApplySlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnCfgApply));
    HsfConfigCollect c;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        srStatus(self, "未连接服务端");
        return;
    }
    if (!HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN)) {
        srStatus(self, "权限不足：修改服务端配置需要管理员账户");
        return;
    }
    memset(&c, 0, sizeof(c));
    c.m_vfdEnabled = XCheckBox_isChecked(&self->m_collect[0].m_chk);
    c.m_temphumEnabled = XCheckBox_isChecked(&self->m_collect[1].m_chk);
    c.m_turbidityEnabled = XCheckBox_isChecked(&self->m_collect[2].m_chk);
    c.m_analogEnabled = XCheckBox_isChecked(&self->m_collect[3].m_chk);
    c.m_digitalEnabled = XCheckBox_isChecked(&self->m_collect[4].m_chk);
    c.m_vfdPollMs = XSpinBox_value(&self->m_collect[0].m_spin);
    c.m_temphumPollMs = XSpinBox_value(&self->m_collect[1].m_spin);
    c.m_turbidityPollMs = XSpinBox_value(&self->m_collect[2].m_spin);
    c.m_analogPollMs = XSpinBox_value(&self->m_collect[3].m_spin);
    c.m_digitalPollMs = XSpinBox_value(&self->m_collect[4].m_spin);
    /* 流量计跟随温湿度周期（服务端契约），客户端按同值带上。 */
    c.m_flowmeterEnabled = c.m_temphumEnabled;
    if (HsfLinkClient_configSetCollect(&c))
        srStatus(self, "正在应用采集配置…");
    else
        srStatus(self, "应用失败：请求未发出");
}

/** @brief 策略键（下拉项数据；缺省 ring）。 */
static const char* srPolicyKey(const HsfSettingsRemote* self)
{
    const XString* s;
    if (!self) return "ring";
    s = XComboBox_currentData(&self->m_storePolicy);
    if (!s) return "ring";
    return XString_toUtf8(s);
}

/** @brief 策略说明文字（与 Python STORAGE_POLICY_HINTS 同义）。 */
static const char* srPolicyHintText(const char* key)
{
    if (!key) return "";
    if (strcmp(key, "stop") == 0)
        return "库满后停止入库，已有数据原样保留只读。";
    if (strcmp(key, "rotate") == 0)
        return "库满后改名归档成带时间戳的分段文件并另起新库，只保留最近 N 段。";
    return "库满时删最旧的整段数据，磁盘占用封顶在上限。适合长期无人值守。";
}

/** @brief 策略变化：更新说明 + 「保留分段」只在 rotate 下可改。 */
static void srPolicyChanged(HsfSettingsRemote* self)
{
    const char* key;
    if (!self) return;
    key = srPolicyKey(self);
    XLabel_setText_2(&self->m_policyHint, srPolicyHintText(key));
    srEnable((XWidget*)&self->m_storeKeep, strcmp(key, "rotate") == 0);
}

static void srPolicySlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_storePolicy));
    (void)args;
    srPolicyChanged(self);
}

/** @brief 保存数据存储配置（全字段，与 Python 同键）。 */
static void srSaveSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                            offsetof(HsfSettingsRemote, m_btnSave));
    const char* path;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        srStatus(self, "未连接服务端");
        return;
    }
    if (!HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN)) {
        srStatus(self, "权限不足：修改服务端配置需要管理员账户");
        return;
    }
    path = XLineEdit_text(&self->m_storePath);
    if (!path) path = "";
    if (HsfLinkClient_configSetStorageEx(
            XCheckBox_isChecked(&self->m_storeEnable), path,
            XSpinBox_value(&self->m_storeSample),
            XSpinBox_value(&self->m_storeMax), srPolicyKey(self),
            XSpinBox_value(&self->m_storeKeep)))
        srStatus(self, "正在应用数据存储配置…");
    else
        srStatus(self, "应用失败：请求未发出");
}

/** @brief 应用登录策略（匿名/同账并发/会话上限；需管理员）。 */
static void srLoginSaveSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                    offsetof(HsfSettingsRemote, m_btnLoginSave));
    HsfConfigLogin login;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        srStatus(self, "未连接服务端");
        return;
    }
    if (!HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN)) {
        srStatus(self, "权限不足：修改登录策略需要管理员账户");
        return;
    }
    memset(&login, 0, sizeof(login));
    login.m_allowAnonymous = XCheckBox_isChecked(&self->m_loginAnon);
    login.m_allowConcurrent = XCheckBox_isChecked(&self->m_loginConcur);
    login.m_maxSessions = XSpinBox_value(&self->m_loginMax);
    if (HsfLinkClient_configSetLogin(&login))
        srStatus(self, "正在应用登录策略…");
    else
        srStatus(self, "应用失败：请求未发出");
}

/* ==================== 回填 / 刷新 ==================== */

/** @brief 把服务端配置快照铺到控件（仅新快照到达时调用）。 */
static void srPopulate(HsfSettingsRemote* self)
{
    const HsfConfig* cfg;
    if (!self) return;
    cfg = HsfLinkClient_config();
    if (!cfg || !cfg->m_valid) return;

    XCheckBox_setChecked(&self->m_collect[0].m_chk, cfg->m_collect.m_vfdEnabled);
    XCheckBox_setChecked(&self->m_collect[1].m_chk, cfg->m_collect.m_temphumEnabled);
    XCheckBox_setChecked(&self->m_collect[2].m_chk, cfg->m_collect.m_turbidityEnabled);
    XCheckBox_setChecked(&self->m_collect[3].m_chk, cfg->m_collect.m_analogEnabled);
    XCheckBox_setChecked(&self->m_collect[4].m_chk, cfg->m_collect.m_digitalEnabled);
    XSpinBox_setValue(&self->m_collect[0].m_spin, cfg->m_collect.m_vfdPollMs);
    XSpinBox_setValue(&self->m_collect[1].m_spin, cfg->m_collect.m_temphumPollMs);
    XSpinBox_setValue(&self->m_collect[2].m_spin, cfg->m_collect.m_turbidityPollMs);
    XSpinBox_setValue(&self->m_collect[3].m_spin, cfg->m_collect.m_analogPollMs);
    XSpinBox_setValue(&self->m_collect[4].m_spin, cfg->m_collect.m_digitalPollMs);

    XCheckBox_setChecked(&self->m_storeEnable, cfg->m_storage.m_enabled);
    XLineEdit_setText(&self->m_storePath, cfg->m_storage.m_path);
    XSpinBox_setValue(&self->m_storeSample, cfg->m_storage.m_sampleMs);
    XSpinBox_setValue(&self->m_storeMax, cfg->m_storage.m_maxMb);
    {
        const char* pol = cfg->m_storage.m_fullPolicy[0]
                              ? cfg->m_storage.m_fullPolicy : "ring";
        int idx = XComboBox_findData_2(&self->m_storePolicy, pol);
        if (idx < 0 && strcmp(pol, "keepOldest") == 0)   /* 旧服务端兼容。 */
            idx = XComboBox_findData_2(&self->m_storePolicy, "ring");
        XComboBox_setCurrentIndex(&self->m_storePolicy, idx >= 0 ? idx : 0);
        srPolicyChanged(self);
    }
    XSpinBox_setValue(&self->m_storeKeep,
                      cfg->m_storage.m_keepFiles > 0
                          ? cfg->m_storage.m_keepFiles : 3);
    /* 登录策略（旧服务端不下发 login 节 ⇒ 解析出缺省值，界面照常显示）。 */
    XCheckBox_setChecked(&self->m_loginAnon, cfg->m_login.m_allowAnonymous);
    XCheckBox_setChecked(&self->m_loginConcur, cfg->m_login.m_allowConcurrent);
    XSpinBox_setValue(&self->m_loginMax,
                      cfg->m_login.m_maxSessions > 0
                          ? cfg->m_login.m_maxSessions : 0);
    XLabel_setText_2(&self->m_dbPath,
                     cfg->m_dbPath[0] ? cfg->m_dbPath
                                      : "未启用（落盘关闭）");
    /* 用量实况（与 Python _populate_usage 同字段）。 */
    {
        const HsfConfigStats* st = &cfg->m_stats;
        char buf[384];
        const double usedMb = st->m_usedMb > 0.0
                                  ? st->m_usedMb
                                  : (double)st->m_usedBytes / (1024.0 * 1024.0);
        if (st->m_maxMb > 0) {
            int pct = (int)(usedMb / (double)st->m_maxMb * 100.0 + 0.5);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            snprintf(buf, sizeof(buf),
                     "用量：已用 %.2f MB · 上限 %d MB · %d%% · 行数 %d",
                     usedMb, st->m_maxMb, pct, st->m_rows);
        } else {
            snprintf(buf, sizeof(buf),
                     "用量：已用 %.2f MB · 上限不限 · 行数 %d",
                     usedMb, st->m_rows);
        }
        if (st->m_segments > 0) {
            char extra[64];
            snprintf(extra, sizeof(extra), " · 分段 %d", st->m_segments);
            strncat(buf, extra, sizeof(buf) - strlen(buf) - 1);
        }
        if (st->m_droppedRows > 0) {
            char extra[80];
            snprintf(extra, sizeof(extra), " · 已循环丢弃 %d 行",
                     st->m_droppedRows);
            strncat(buf, extra, sizeof(buf) - strlen(buf) - 1);
        }
        if (st->m_paused) {
            char extra[192];
            snprintf(extra, sizeof(extra), "\n[已停写] %s", st->m_pauseReason);
            strncat(buf, extra, sizeof(buf) - strlen(buf) - 1);
        }
        XLabel_setText_2(&self->m_storageUse, buf);
    }
}

/** @brief 巡检：模式/自动标签 + 权限门 + 按序号回填/提示。 */
static void srRefresh(HsfSettingsRemote* self)
{
    const bool connected = HsfLinkClient_isReady();
    const bool canMode = HsfLinkClient_can(HSF_PERM_MODE_SWITCH);
    const bool canAuto = HsfLinkClient_can(HSF_PERM_AUTO_CONTROL);
    const bool canCfg = HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN);
    int autoMode = 0;
    int running = 0;
    bool modeKnown;

    if (!self) return;
    modeKnown = HsfLinkClient_runMode(&autoMode);

    /* 运行模式。 */
    XLabel_setText_2(&self->m_mode,
                     !modeKnown ? "未知"
                     : autoMode ? "自动（远程控制已禁用）"
                                : "手动（允许远程操作）");

    /* 自动运行：手动 / 自动未运行 / 自动运行中 / 未知。 */
    if (!modeKnown)
        XLabel_setText_2(&self->m_auto, "未知");
    else if (!autoMode)
        XLabel_setText_2(&self->m_auto, "手动模式（自动运行不可用）");
    else if (!HsfLinkClient_autoRunning(&running))
        XLabel_setText_2(&self->m_auto, "自动模式 · 运行状态未知");
    else
        XLabel_setText_2(&self->m_auto,
                         running ? "自动模式 · 运行中"
                                 : "自动模式 · 已停止");

    /* 连接信息。 */
    {
        char buf[256];
        if (connected)
            snprintf(buf, sizeof(buf), "服务端 %s · 用户 %s · 角色 %s",
                     HsfLinkClient_serverAddress(),
                     HsfLinkClient_user(), HsfLinkClient_roleName());
        else
            snprintf(buf, sizeof(buf), "未连接");
        XLabel_setText_2(&self->m_conn, buf);
    }

    /* 权限门（界面只做可达性；服务端逐帧强制）。 */
    srEnable((XWidget*)&self->m_btnManual,
             connected && canMode && modeKnown && autoMode);
    srEnable((XWidget*)&self->m_btnAuto,
             connected && canMode && modeKnown && !autoMode);
    {
        const bool ctrl = connected && canAuto && modeKnown && autoMode;
        srEnable((XWidget*)&self->m_btnAutoStart, ctrl && !(running && 1));
        srEnable((XWidget*)&self->m_btnAutoStop, ctrl && running);
    }
    srEnable((XWidget*)&self->m_btnCfgRefresh, connected);
    srEnableCollect(self, connected && canCfg);
    srEnable((XWidget*)&self->m_storeEnable, connected && canCfg);
    srEnable((XWidget*)&self->m_storePath, connected && canCfg);
    srEnable((XWidget*)&self->m_storeSample, connected && canCfg);
    srEnable((XWidget*)&self->m_storeMax, connected && canCfg);
    srEnable((XWidget*)&self->m_storePolicy, connected && canCfg);
    srEnable((XWidget*)&self->m_btnSave, connected && canCfg);
    srEnable((XWidget*)&self->m_loginAnon, connected && canCfg);
    srEnable((XWidget*)&self->m_loginConcur, connected && canCfg);
    srEnable((XWidget*)&self->m_loginMax, connected && canCfg);
    srEnable((XWidget*)&self->m_btnLoginSave, connected && canCfg);
    srPolicyChanged(self);   /* 保留分段：rotate 且有权才可改。 */

    /* 配置快照到达 → 回填。 */
    {
        const int gseq = HsfLinkClient_configGetSeq();
        if (gseq != self->m_seenGetSeq) {
            self->m_seenGetSeq = gseq;
            srPopulate(self);
            srStatus(self, "配置已刷新");
        }
    }
    /* 配置写应答 → 提示 + 回拉一次（服务端可能夹取数值）。 */
    {
        const int wseq = HsfLinkClient_configSeq();
        if (wseq != self->m_seenWriteSeq) {
            self->m_seenWriteSeq = wseq;
            if (HsfLinkClient_configOk()) {
                srStatus(self, "配置已保存（以服务端回显为准）");
                (void)HsfLinkClient_configGet();
            } else {
                srStatus(self, "保存失败：%s", HsfLinkClient_configError());
            }
        }
    }
    /* 自动运行应答 → 提示（比"已发送"更准确）。 */
    {
        const int cseq = HsfLinkClient_commandSeq();
        if (cseq != self->m_seenCmdSeq) {
            self->m_seenCmdSeq = cseq;
            if (HsfLinkClient_lastCommandError()[0])
                srStatus(self, "操作失败：%s",
                         HsfLinkClient_lastCommandError());
            else
                srStatus(self, "操作已受理");
        }
    }
    /* 模式切换失败原因（RUN_MODE 应答 err）。 */
    {
        const char* err = HsfLinkClient_lastModeError();
        if (err && err[0] && strcmp(err, self->m_lastModeErr) != 0) {
            snprintf(self->m_lastModeErr, sizeof(self->m_lastModeErr), "%s",
                     err);
            srStatus(self, "切换失败：%s", err);
        }
    }
}

/** @brief 巡检定时器。 */
static void srTimerCb(void* userData, XTimerData* timer)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)userData;
    (void)timer;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        self->m_fetched = false;   /* 断开后复位：重连首拍补拉。 */
    } else if (!self->m_fetched) {
        self->m_fetched = true;
        srFetchConfig(self, true);
    }
    srRefresh(self);
}

/* ==================== 创建 ==================== */

/** @brief 建一个说明标签（小字、自动换行）。 */
static void srInitHint(XLabel* label, XWidget* parent, const char* text)
{
    XLabel_init(label, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)label);
    XLabel_setTextPixelSize(label, 12);
    XWidget_setForegroundRole((XWidget*)label, XPaletteColorRole_Mid);
    XLabel_setWordWrap(label, true);
    XLabel_setText_2(label, text);
    XWidget_show((XWidget*)label);
}

/** @brief 建一个按钮并接到槽。 */
static void srInitButton(XPushButton* btn, XWidget* parent, const char* text,
                         XObject* receiver, XSlotFunc1 slot)
{
    XPushButton_init(btn, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)btn);
    XPushButton_setText_2(btn, text);
    XWidget_setFixedHeight((XWidget*)btn, 34);
    XObject_connect_1((XObject*)btn, XSignal(XAbstractButton_clicked_signal),
                      receiver, slot, XConnectionType_Direct);
    XWidget_show((XWidget*)btn);
}

XWidget* HsfSettingsRemote_create(XWidget* parent, HsfMenuNavFn navFn,
                                  void* userData)
{
    static const char* const kCollectLabels[SR_COLLECT_ROWS] = {
        "搅拌泵", "温湿度", "浊度仪", "模拟量", "数字量"
    };
    HsfSettingsRemote* self;
    int i;

    (void)navFn;
    if (!parent) return NULL;
    self = (HsfSettingsRemote*)XMemory_malloc(sizeof(HsfSettingsRemote),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    self->m_statusUser = userData;
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    HsfTheme_applyDefaultFont(&self->m_base);

    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title, "远端设置");
    XLabel_setTextPixelSize(&self->m_title, 16);
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    /* ---- ① 运行模式 ---- */
    XLabel_init(&self->m_mode, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_mode);
    XLabel_setTextPixelSize(&self->m_mode, 20);
    XLabel_setText_2(&self->m_mode, "未知");
    XWidget_show((XWidget*)&self->m_mode);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_mode);

    srInitHint(&self->m_modeHint, &self->m_base,
               "自动模式会拒绝全部远程控制（设备由现场逻辑驱动）；"
               "手动模式允许管理员远程操作。");
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_modeHint);
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitButton(&self->m_btnManual, &self->m_base, "切换到手动",
                         (XObject*)&self->m_btnManual, srManualSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnManual);
            srInitButton(&self->m_btnAuto, &self->m_base, "切换到自动",
                         (XObject*)&self->m_btnAuto, srAutoSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnAuto);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
        }
    }

    /* ---- ② 自动运行 ---- */
    XLabel_init(&self->m_auto, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_auto);
    XLabel_setTextPixelSize(&self->m_auto, 15);
    XLabel_setText_2(&self->m_auto, "--");
    XWidget_show((XWidget*)&self->m_auto);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_auto);

    srInitHint(&self->m_autoHint, &self->m_base,
               "切到自动模式只是授权；要真正开始自动运行，需按「启动自动运行」。"
               "服务端重启后控制器一律为停止状态。");
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_autoHint);
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitButton(&self->m_btnAutoStart, &self->m_base, "启动自动运行",
                         (XObject*)&self->m_btnAutoStart, srAutoStartSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnAutoStart);
            srInitButton(&self->m_btnAutoStop, &self->m_base, "停止自动运行",
                         (XObject*)&self->m_btnAutoStop, srAutoStopSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnAutoStop);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
        }
    }

    /* ---- ③ 连接信息 ---- */
    srInitHint(&self->m_conn, &self->m_base, "--");
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_conn);

    /* ---- ④ 采集配置 ---- */
    {
        XLabel* head = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (head) {
            XLabel_init(head, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)head);
            XLabel_setText_2(head, "采集配置");
            XLabel_setTextPixelSize(head, 14);
            XWidget_show((XWidget*)head);
            XBoxLayout_addWidget(&self->m_layout, (XWidget*)head);
        }
    }
    for (i = 0; i < SR_COLLECT_ROWS; ++i) {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
        if (!row || !name) {
            if (row) XClassDelete((XClass*)row);
            if (name) XClassDelete((XClass*)name);
            continue;
        }
        XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
        XLayout_setSpacing((XLayout*)row, 10);
        XLabel_init(name, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)name);
        XLabel_setText_2(name, kCollectLabels[i]);
        XWidget_setFixedWidth((XWidget*)name, 96);
        XWidget_show((XWidget*)name);
        XBoxLayout_addWidget(row, (XWidget*)name);

        XCheckBox_init(&self->m_collect[i].m_chk, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_collect[i].m_chk);
        XCheckBox_setText_2(&self->m_collect[i].m_chk, "采集");
        XWidget_show((XWidget*)&self->m_collect[i].m_chk);
        XBoxLayout_addWidget(row, (XWidget*)&self->m_collect[i].m_chk);

        XSpinBox_init(&self->m_collect[i].m_spin, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_collect[i].m_spin);
        XSpinBox_setRange(&self->m_collect[i].m_spin, SR_POLL_MS_MIN,
                          SR_POLL_MS_MAX);
        XSpinBox_setSingleStep(&self->m_collect[i].m_spin, 50);
        XSpinBox_setSuffix(&self->m_collect[i].m_spin, " ms");
        XSpinBox_setValue(&self->m_collect[i].m_spin, 200);
        XWidget_setFixedWidth((XWidget*)&self->m_collect[i].m_spin, 150);
        XWidget_show((XWidget*)&self->m_collect[i].m_spin);
        XBoxLayout_addWidget(row, (XWidget*)&self->m_collect[i].m_spin);
        XBoxLayout_addStretch(row, 1);
        XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
    }
    srInitHint(&self->m_collectHint, &self->m_base,
               "流量计跟随温湿度的周期；电磁阀/进水泵跟随数字量的开关。"
               "改开关立即生效（被停的设备健康转数据过期），改周期下一拍生效。");
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_collectHint);
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitButton(&self->m_btnCfgRefresh, &self->m_base, "刷新",
                         (XObject*)&self->m_btnCfgRefresh, srCfgRefreshSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnCfgRefresh);
            srInitButton(&self->m_btnCfgApply, &self->m_base, "应用（需管理员）",
                         (XObject*)&self->m_btnCfgApply, srCfgApplySlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnCfgApply);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
        }
    }

    /* ---- ⑤ 数据存储 ---- */
    {
        XLabel* head = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (head) {
            XLabel_init(head, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)head);
            XLabel_setText_2(head, "数据存储（SQLite：历史/日志/报警）");
            XLabel_setTextPixelSize(head, 14);
            XWidget_show((XWidget*)head);
            XBoxLayout_addWidget(&self->m_layout, (XWidget*)head);
        }
    }
    XCheckBox_init(&self->m_storeEnable, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_storeEnable);
    XCheckBox_setText_2(&self->m_storeEnable, "启用落盘");
    XWidget_show((XWidget*)&self->m_storeEnable);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_storeEnable);

    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
        if (row && name) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            XLabel_init(name, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)name);
            XLabel_setText_2(name, "库文件路径");
            XWidget_setFixedWidth((XWidget*)name, 96);
            XWidget_show((XWidget*)name);
            XBoxLayout_addWidget(row, (XWidget*)name);

            XLineEdit_init(&self->m_storePath, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storePath);
            XLineEdit_setPlaceholderText(&self->m_storePath,
                                         "空 = 服务端程序目录\\hsf_data.db");
            XWidget_setFixedHeight((XWidget*)&self->m_storePath, 34);
            XWidget_show((XWidget*)&self->m_storePath);
            XBoxLayout_addWidgetEx(row, (XWidget*)&self->m_storePath, 1, 0);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name) XClassDelete((XClass*)name);
        }
    }
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name1 = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name2 = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (row && name1 && name2) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            XLabel_init(name1, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)name1);
            XLabel_setText_2(name1, "入库周期");
            XWidget_show((XWidget*)name1);
            XBoxLayout_addWidget(row, (XWidget*)name1);
            XSpinBox_init(&self->m_storeSample, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storeSample);
            XSpinBox_setRange(&self->m_storeSample, 1000, 3600000);
            XSpinBox_setSingleStep(&self->m_storeSample, 1000);
            XSpinBox_setSuffix(&self->m_storeSample, " ms");
            XSpinBox_setValue(&self->m_storeSample, 5000);
            XWidget_setFixedWidth((XWidget*)&self->m_storeSample, 150);
            XWidget_show((XWidget*)&self->m_storeSample);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_storeSample);

            XLabel_init(name2, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)name2);
            XLabel_setText_2(name2, "容量上限");
            XWidget_show((XWidget*)name2);
            XBoxLayout_addWidget(row, (XWidget*)name2);
            XSpinBox_init(&self->m_storeMax, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storeMax);
            XSpinBox_setRange(&self->m_storeMax, SR_MAX_MB_MIN, SR_MAX_MB_MAX);
            XSpinBox_setSingleStep(&self->m_storeMax, 50);
            XSpinBox_setSuffix(&self->m_storeMax, " MB");
            XSpinBox_setValue(&self->m_storeMax, 0);
            XWidget_setFixedWidth((XWidget*)&self->m_storeMax, 150);
            XWidget_show((XWidget*)&self->m_storeMax);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_storeMax);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name1) XClassDelete((XClass*)name1);
            if (name2) XClassDelete((XClass*)name2);
        }
    }
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name1 = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name2 = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (row && name1 && name2) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            XLabel_init(name1, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)name1);
            XLabel_setText_2(name1, "写满后");
            XWidget_show((XWidget*)name1);
            XBoxLayout_addWidget(row, (XWidget*)name1);

            XComboBox_init(&self->m_storePolicy, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storePolicy);
            XComboBox_addItem_2(&self->m_storePolicy, "循环覆盖（删最旧）");
            XComboBox_setItemData_2(&self->m_storePolicy, 0, "ring");
            XComboBox_addItem_2(&self->m_storePolicy, "到限停写（只读保留）");
            XComboBox_setItemData_2(&self->m_storePolicy, 1, "stop");
            XComboBox_addItem_2(&self->m_storePolicy, "分段滚动（按段归档）");
            XComboBox_setItemData_2(&self->m_storePolicy, 2, "rotate");
            XComboBox_setCurrentIndex(&self->m_storePolicy, 0);
            XWidget_setFixedHeight((XWidget*)&self->m_storePolicy, 34);
            XWidget_setMinimumWidth((XWidget*)&self->m_storePolicy, 240);
            XObject_connect_1((XObject*)&self->m_storePolicy,
                              XSignal(XComboBox_currentIndexChanged_signal),
                              (XObject*)&self->m_storePolicy, srPolicySlot,
                              XConnectionType_Direct);
            XWidget_show((XWidget*)&self->m_storePolicy);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_storePolicy);

            XLabel_init(name2, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)name2);
            XLabel_setText_2(name2, "保留分段");
            XWidget_show((XWidget*)name2);
            XBoxLayout_addWidget(row, (XWidget*)name2);
            XSpinBox_init(&self->m_storeKeep, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storeKeep);
            XSpinBox_setRange(&self->m_storeKeep, 1, 99);
            XSpinBox_setValue(&self->m_storeKeep, 3);
            XWidget_setFixedWidth((XWidget*)&self->m_storeKeep, 100);
            XWidget_show((XWidget*)&self->m_storeKeep);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_storeKeep);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name1) XClassDelete((XClass*)name1);
            if (name2) XClassDelete((XClass*)name2);
        }
    }
    srInitHint(&self->m_policyHint, &self->m_base, srPolicyHintText("ring"));
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_policyHint);

    srInitHint(&self->m_storageUse, &self->m_base, "用量：--");
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_storageUse);

    srInitHint(&self->m_dbPath, &self->m_base, "当前库文件：--");
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_dbPath);

    srInitButton(&self->m_btnSave, &self->m_base, "保存（需管理员）",
                 (XObject*)&self->m_btnSave, srSaveSlot);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_btnSave);

    /* ---- ⑥ 登录策略（账号；2026-11-08 新增）---- */
    {
        XLabel* head = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (head) {
            XLabel_init(head, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)head);
            XLabel_setText_2(head, "登录策略（账号）");
            XLabel_setTextPixelSize(head, 14);
            XWidget_show((XWidget*)head);
            XBoxLayout_addWidget(&self->m_layout, (XWidget*)head);
        }
    }
    XCheckBox_init(&self->m_loginAnon, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_loginAnon);
    XCheckBox_setText_2(&self->m_loginAnon, "允许匿名登录（只读）");
    XWidget_show((XWidget*)&self->m_loginAnon);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_loginAnon);

    XCheckBox_init(&self->m_loginConcur, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_loginConcur);
    XCheckBox_setText_2(&self->m_loginConcur, "允许同账并发（同账户多端在线）");
    XWidget_show((XWidget*)&self->m_loginConcur);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_loginConcur);

    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
        if (row && name) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            XLabel_init(name, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)name);
            XLabel_setText_2(name, "会话上限");
            XWidget_setFixedWidth((XWidget*)name, 96);
            XWidget_show((XWidget*)name);
            XBoxLayout_addWidget(row, (XWidget*)name);
            XSpinBox_init(&self->m_loginMax, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_loginMax);
            XSpinBox_setRange(&self->m_loginMax, 0, 256);
            XSpinBox_setSingleStep(&self->m_loginMax, 1);
            XSpinBox_setSuffix(&self->m_loginMax, " 个（0=不限）");
            XSpinBox_setValue(&self->m_loginMax, 8);
            XWidget_setFixedWidth((XWidget*)&self->m_loginMax, 170);
            XWidget_show((XWidget*)&self->m_loginMax);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_loginMax);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name) XClassDelete((XClass*)name);
        }
    }
    srInitHint(&self->m_loginHint, &self->m_base,
               "写入立即对下一次登录生效并落盘 hsf_login_policy.json；"
               "已在线会话不受影响（关闭同账并发不会踢人）。");
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_loginHint);

    srInitButton(&self->m_btnLoginSave, &self->m_base, "应用登录策略（需管理员）",
                 (XObject*)&self->m_btnLoginSave, srLoginSaveSlot);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_btnLoginSave);

    XBoxLayout_addStretch(&self->m_layout, 1);

    self->m_seenCmdSeq = HsfLinkClient_commandSeq();
    self->m_seenWriteSeq = HsfLinkClient_configSeq();
    self->m_seenGetSeq = HsfLinkClient_configGetSeq();

    self->m_timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_timer) {
        XTimer_setInterval(self->m_timer, HsfSettingsLocal_refreshMs());
        XTimer_setTimerCallback(self->m_timer, srTimerCb);
        XTimer_setUserData(self->m_timer, self);
        XTimer_start_base(self->m_timer);
    }

    XWidget_show(&self->m_base);
    return &self->m_base;
}

/**
 * @brief 远端设置页窄宽自适应（2026-11-09 竖屏适配批次补齐）。
 * @details [为什么只动两处] 本页行最宽的是「写满后 + 策略下拉(最小宽 240)
 *          + 保留分段 + spin(100)」≈513px——480 手机档必裁右端。其余行
 *          （采集 96+60+150、登录 96+170）在 480 档视口 ~456px 内本就放得
 *          下，不动。窄档两板斧：页边距 16,12→8,6（与 HsfDataAlarm_adapt
 *          同口径）；策略下拉最小宽 240→160（行宽 ~433 放得下，下拉本身
 *          可横向滚动条目）。宽档恢复 create 原值，幂等（帧泵每轮驱动）。
 */
void HsfSettingsRemote_adapt(XWidget* page, bool compact)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)page;
    if (!self) return;
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 8 : 16, compact ? 6 : 12,
                               compact ? 8 : 16, compact ? 6 : 12);
    XWidget_setMinimumWidth((XWidget*)&self->m_storePolicy,
                            compact ? 160 : 240);
    XLayout_activate((XLayout*)&self->m_layout);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON */
