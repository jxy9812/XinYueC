/**
 * @file       HsfSettingsRemote.c
 * @brief      远端设置页：运行模式 / 自动运行 / 采集配置 / 落盘配置 / 连接信息。
 * @details    [对齐 Python 客户端 RemoteSettingsPage] 五张卡：
 *              ① 运行模式（显示 + 手动/自动切换）；
 *              ② 自动运行（显示 + 启动/停止）——与模式切换**分开**两个按钮
 *                 （切自动模式只是"授权"，不等于"现在开始跑"）；
 *              ③ 采集配置（各设备开关 + 轮询周期，**只读**：C 端采集参数
 *                 是服务端配置文件里的编译期常量，改后重启生效 ⇒ 这里如实
 *                 标注，不给"改了没反应"的假控件）；
 *              ④ 落盘配置（开关 + 采样周期**可改**（运行期生效）+ 库路径 +
 *                 用量实况）；
 *              ⑤ 连接信息（服务端地址/端口/角色）。
 *              数据源：服务端 0x0116（配置快照）/ 0x0115（自动运行）/
 *              0x0105（运行模式），键名与 Python 端一致。
 */
#include "HsfSettingsRemote.h"

#include "HsfSettingsLocal.h" /* 巡检周期=本地设置「界面刷新周期」 */

#include "HsfLinkClient.h"
#include "HsfLinkConfig.h"
#include "HsfTheme.h"
#include "XBoxLayout.h"
#include "XCheckBox.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XSpinBox.h"
#include "XTimer.h"
#include "XMemory.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON

/** @brief 远端设置页对象。 */
typedef struct HsfSettingsRemote {
    XWidget      m_base;        /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;      /**< 页根竖排布局。 */
    XLabel       m_title;       /**< 页标题。 */
    XLabel       m_mode;        /**< 运行模式文案。 */
    XLabel       m_auto;        /**< 自动运行状态文案。 */
    XLabel       m_collect;     /**< 采集配置摘要。 */
    XLabel       m_storagePath; /**< 库路径。 */
    XLabel       m_storageUse;  /**< 用量实况。 */
    XLabel       m_conn;        /**< 连接信息。 */
    XCheckBox    m_storeEnable; /**< 落盘开关。 */
    XSpinBox     m_storeSample; /**< 采样周期。 */
    XPushButton  m_btnManual;   /**< 切到手动。 */
    XPushButton  m_btnAuto;     /**< 切到自动。 */
    XPushButton  m_btnAutoStart;/**< 启动自动运行。 */
    XPushButton  m_btnAutoStop; /**< 停止自动运行。 */
    XPushButton  m_btnSave;     /**< 保存落盘配置。 */
    XTimer*      m_timer;       /**< 巡检定时器（200ms：拉配置 + 刷新显示）。 */
    bool         m_fetched;     /**< 已发过配置查询（防每次刷新都发）。 */
} HsfSettingsRemote;

/* ==================== 按钮槽 ==================== */

/** @brief 切到手动模式。 */
static void srManualSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    (void)HsfLinkClient_setRunMode(false);
}

/** @brief 切到自动模式。 */
static void srAutoSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    (void)HsfLinkClient_setRunMode(true);
}

/** @brief 启动自动运行。 */
static void srAutoStartSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    (void)HsfLinkClient_autoControl(1);
}

/** @brief 停止自动运行。 */
static void srAutoStopSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    (void)HsfLinkClient_autoControl(2);
}

/** @brief 保存落盘配置（开关 + 采样周期）。 */
static void srSaveSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                            offsetof(HsfSettingsRemote, m_btnSave));
    (void)args;
    if (!self) return;
    (void)HsfLinkClient_configSetStorage(
        XCheckBox_isChecked(&self->m_storeEnable),
        XSpinBox_value(&self->m_storeSample));
}

/* ==================== 刷新 ==================== */

/** @brief 把服务端配置 + 模式状态铺到控件上。 */
static void srRefresh(HsfSettingsRemote* self)
{
    const HsfConfig* cfg;
    int autoMode = 0;
    int autoRunning = 0;

    if (!self) return;

    /* 运行模式。 */
    if (HsfLinkClient_runMode(&autoMode)) {
        XLabel_setText_2(&self->m_mode,
                         autoMode ? "\xE5\xB7\xB2\xE5\x8A\xA8"   /* 自动 */
                                  : "\xE6\x89\x8B\xE5\x8A\xA8"); /* 手动 */
    } else {
        XLabel_setText_2(&self->m_mode, "--");
    }
    /* 自动运行（与模式是两件事：授权 ≠ 在跑）。 */
    if (HsfLinkClient_autoRunning(&autoRunning)) {
        XLabel_setText_2(&self->m_auto,
                         autoRunning
                           ? "\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD"  /* 运行中 */
                           : "\xE5\xB7\xB2\xE5\x81\x9C\xE6\xAD\xA2"); /* 已停止 */
    } else {
        XLabel_setText_2(&self->m_auto, "--");
    }

    /* 服务端配置。 */
    cfg = HsfLinkClient_config();
    if (cfg && cfg->m_valid) {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "\xE5\x8F\x98\xE9\xA2\x91\xE5\x99\xA8 %s\xE3\x80\x81"
                 "\xE6\xB8\xA9\xE6\xB9\xBF\xE5\xBA\xA6 %s\xE3\x80\x81"
                 "\xE6\xB5\x81\xE9\x87\x8F\xE8\xAE\xA1 %s\xE3\x80\x81"
                 "\xE6\xB7\xA1\xE5\xBA\xA6\xE4\xBB\xA3\xE6\xA0\x87 %s\xE3\x80\x81"
                 "\xE6\xA8\xA1\xE6\x95\xB0\xE9\x87\x8F %s\xE3\x80\x81"
                 "\xE6\x95\xB0\xE5\xAD\x97\xE9\x87\x8F %s",
                 cfg->m_collect.m_vfdEnabled ? "\xE5\xBC\x80" : "\xE5\x85\xB3",
                 cfg->m_collect.m_temphumEnabled ? "\xE5\xBC\x80" : "\xE5\x85\xB3",
                 cfg->m_collect.m_flowmeterEnabled ? "\xE5\xBC\x80" : "\xE5\x85\xB3",
                 cfg->m_collect.m_turbidityEnabled ? "\xE5\xBC\x80" : "\xE5\x85\xB3",
                 cfg->m_collect.m_analogEnabled ? "\xE5\xBC\x80" : "\xE5\x85\xB3",
                 cfg->m_collect.m_digitalEnabled ? "\xE5\xBC\x80" : "\xE5\x85\xB3");
        XLabel_setText_2(&self->m_collect, buf);
        /* 落盘：开关 + 采样周期（可改）+ 路径 + 用量。 */
        XCheckBox_setChecked(&self->m_storeEnable, cfg->m_storage.m_enabled);
        XSpinBox_setValue(&self->m_storeSample, cfg->m_storage.m_sampleMs);
        XLabel_setText_2(&self->m_storagePath,
                         cfg->m_storage.m_path[0] ? cfg->m_storage.m_path
                                                  : "\xE7\xA8\x8B\xE5\xBA\x8F\xE7\x9B\xAE\xE5\xBD\x95");
        {
            const double usedMb = (double)cfg->m_stats.m_usedBytes /
                                  (1024.0 * 1024.0);
            snprintf(buf, sizeof(buf),
                     "\xE7\x94\xA8\xE9\x87\x8F %.2f MB\xE3\x80\x81"
                     "\xE8\xA1\x8C\xE6\x95\xB0 %d\xE3\x80\x81"
                     "\xE4\xB8\x8A\xE9\x99\x90 %s",
                     usedMb, cfg->m_stats.m_rows,
                     cfg->m_stats.m_maxMb > 0
                        ? "\xE6\x9C\x89\xE9\x99\x90" : "\xE4\xB8\x8D\xE9\x99\x90");
            XLabel_setText_2(&self->m_storageUse, buf);
        }
        XLabel_setText_2(&self->m_storagePath, cfg->m_storage.m_path);
    }

    /* 连接信息。 */
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "\xE6\x9C\x8D\xE5\x8A\xA1\xE7\xAB\xAF %s \xE3\x80\x81\xE8\xA7\x92\xE6\x9C\xAC %s",
                 HsfLinkClient_serverAddress(),
                 HsfLinkClient_roleName());
        XLabel_setText_2(&self->m_conn, buf);
    }
}

/** @brief 200ms 拍：拉一次配置 + 刷新显示。 */
static void srTimerCb(void* userData, XTimerData* timer)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)userData;
    (void)timer;
    if (!self) return;
    if (!self->m_fetched) {
        self->m_fetched = true;
        (void)HsfLinkClient_configGet();
    }
    srRefresh(self);
}

XWidget* HsfSettingsRemote_create(XWidget* parent, HsfMenuNavFn navFn,
                                  void* userData)
{
    HsfSettingsRemote* self;
    (void)navFn; (void)userData;

    if (!parent) return NULL;
    self = (HsfSettingsRemote*)XMemory_malloc(sizeof(HsfSettingsRemote),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    HsfTheme_applyDefaultFont(&self->m_base);

    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 10);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title,
                     "\xE8\xBF\x9C\xE7\xAB\xAF\xE8\xAE\xBE\xE7\xBD\xAE"); /* 远端设置 */
    XLabel_setTextPixelSize(&self->m_title, 16);
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    /* ---- 运行模式 ---- */
    XLabel_init(&self->m_mode, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_mode);
    XLabel_setTextPixelSize(&self->m_mode, 18);
    XLabel_setText_2(&self->m_mode, "--");
    XWidget_show((XWidget*)&self->m_mode);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_mode);
    {
        XPushButton_init(&self->m_btnManual, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_btnManual);
        XPushButton_setText_2(&self->m_btnManual,
                              "\xE5\x88\x87\xE6\x8D\xA2\xE5\x88\xB0\xE6\x89\x8B\xE5\x8A\xA8"); /* 切换到手动 */
        XWidget_setFixedHeight((XWidget*)&self->m_btnManual, 36);
        XObject_connect_1((XObject*)&self->m_btnManual,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)&self->m_btnManual, srManualSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)&self->m_btnManual);
        XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_btnManual);

        XPushButton_init(&self->m_btnAuto, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_btnAuto);
        XPushButton_setText_2(&self->m_btnAuto,
                              "\xE5\x88\x87\xE6\x8D\xA2\xE5\x88\xB0\xE8\x87\xAA\xE5\x8A\xA8"); /* 切换到自动 */
        XWidget_setFixedHeight((XWidget*)&self->m_btnAuto, 36);
        XObject_connect_1((XObject*)&self->m_btnAuto,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)&self->m_btnAuto, srAutoSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)&self->m_btnAuto);
        XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_btnAuto);
    }

    /* ---- 自动运行（与模式切换分开） ---- */
    XLabel_init(&self->m_auto, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_auto);
    XLabel_setTextPixelSize(&self->m_auto, 15);
    XLabel_setText_2(&self->m_auto, "--");
    XWidget_show((XWidget*)&self->m_auto);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_auto);
    {
        XPushButton_init(&self->m_btnAutoStart, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_btnAutoStart);
        XPushButton_setText_2(&self->m_btnAutoStart,
                              "\xE5\x90\xAF\xE5\x8A\xA8\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C"); /* 启动自动运行 */
        XWidget_setFixedHeight((XWidget*)&self->m_btnAutoStart, 36);
        XObject_connect_1((XObject*)&self->m_btnAutoStart,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)&self->m_btnAutoStart, srAutoStartSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)&self->m_btnAutoStart);
        XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_btnAutoStart);

        XPushButton_init(&self->m_btnAutoStop, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_btnAutoStop);
        XPushButton_setText_2(&self->m_btnAutoStop,
                              "\xE5\x81\x9C\xE6\xAD\xA2\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C"); /* 停止自动运行 */
        XWidget_setFixedHeight((XWidget*)&self->m_btnAutoStop, 36);
        XObject_connect_1((XObject*)&self->m_btnAutoStop,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)&self->m_btnAutoStop, srAutoStopSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)&self->m_btnAutoStop);
        XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_btnAutoStop);
    }

    /* ---- 采集配置（只读） ---- */
    XLabel_init(&self->m_collect, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_collect);
    XLabel_setTextPixelSize(&self->m_collect, 12);
    XWidget_setForegroundRole((XWidget*)&self->m_collect, XPaletteColorRole_Mid);
    XLabel_setText_2(&self->m_collect, "--");
    XWidget_show((XWidget*)&self->m_collect);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_collect);

    /* ---- 落盘配置（可改） ---- */
    {
        XCheckBox_init(&self->m_storeEnable, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_storeEnable);
        XCheckBox_setText_2(&self->m_storeEnable,
                             "\xE8\x90\xBD\xE7\x9B\x98\xE5\xBC\x80\xE5\x90\xAF"); /* 落盘开启 */
        XWidget_show((XWidget*)&self->m_storeEnable);
        XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_storeEnable);

        XSpinBox_init(&self->m_storeSample, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_storeSample);
        XSpinBox_setRange(&self->m_storeSample, 1000, 60000);
        XSpinBox_setSingleStep(&self->m_storeSample, 1000);
        XSpinBox_setSuffix(&self->m_storeSample, " ms");
        XSpinBox_setValue(&self->m_storeSample, 5000);
        XWidget_setFixedWidth((XWidget*)&self->m_storeSample, 140);
        XWidget_show((XWidget*)&self->m_storeSample);
        XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_storeSample);

        XPushButton_init(&self->m_btnSave, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_btnSave);
        XPushButton_setText_2(&self->m_btnSave,
                              "\xE4\xBF\x9D\xE5\xAD\x98"); /* 保存 */
        XWidget_setFixedHeight((XWidget*)&self->m_btnSave, 36);
        XObject_connect_1((XObject*)&self->m_btnSave,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)&self->m_btnSave, srSaveSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)&self->m_btnSave);
        XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_btnSave);
    }
    XLabel_init(&self->m_storagePath, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_storagePath);
    XLabel_setTextPixelSize(&self->m_storagePath, 12);
    XWidget_setForegroundRole((XWidget*)&self->m_storagePath, XPaletteColorRole_Mid);
    XLabel_setText_2(&self->m_storagePath, "--");
    XWidget_show((XWidget*)&self->m_storagePath);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_storagePath);

    XLabel_init(&self->m_storageUse, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_storageUse);
    XLabel_setTextPixelSize(&self->m_storageUse, 12);
    XWidget_setForegroundRole((XWidget*)&self->m_storageUse, XPaletteColorRole_Mid);
    XLabel_setText_2(&self->m_storageUse, "--");
    XWidget_show((XWidget*)&self->m_storageUse);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_storageUse);

    /* ---- 连接信息 ---- */
    XLabel_init(&self->m_conn, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_conn);
    XLabel_setTextPixelSize(&self->m_conn, 12);
    XWidget_setForegroundRole((XWidget*)&self->m_conn, XPaletteColorRole_Mid);
    XLabel_setText_2(&self->m_conn, "--");
    XWidget_show((XWidget*)&self->m_conn);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_conn);

    self->m_timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_timer) {
        /* 巡检周期取本地设置「界面刷新周期」（数据刷新卡；缺省 200ms）。 */
        XTimer_setInterval(self->m_timer, HsfSettingsLocal_refreshMs());
        XTimer_setTimerCallback(self->m_timer, srTimerCb);
        XTimer_setUserData(self->m_timer, self);
        XTimer_start_base(self->m_timer);
    }

    XWidget_show(&self->m_base);
    return &self->m_base;
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
