/**
 * @file       HsfSettingsLocal.c
 * @brief      本地设置页：数据刷新（界面刷新周期）/ 字体大小（按页缩放）。
 * @details    [对齐 Python 客户端 LocalSettingsPage]
 *              ① 数据刷新：界面刷新周期（默认 200ms、100~2000；各数据页
 *                 轮询定时器建页时取 HsfSettingsLocal_refreshMs()——只影响
 *                 本机轮询，不改变服务端推送节拍；重启客户端生效）；
 *              ② 字体大小：按页缩放百分比（HsfUi hsfUi_pageFontPx 钩子，
 *                 FontScale/Page<N> 键，即时重放）。
 *             [显示相关不在此页] 紧凑模式/全屏/边框等在「窗口设置」页
 *             （HsfWindowPage），本页只管数据刷新与字号。
 *             仅本机生效，不发给服务端；改动即写 INI 并 sync 落盘
 *             （XSettings 无空闲落盘钩子，不 sync 则退出前崩溃会丢）。
 *             [字体大小只列已接线页] 缩放钩子要求页内字号/联动高度统一
 *             走 hsfUi_pageFontPx 折算——只摆已接线的页，没接线的页摆出来
 *             就是改了没反应的假控件（远端设置页采集配置只读化的同一原则）。
 */
#include "HsfSettingsLocal.h"

#include "HsfTheme.h"
#include "HsfUi.h"
#include "HsfSettingsStore.h"
#include "XAbstractButton.h"
#include "XBoxLayout.h"
#include "XCheckBox.h"
#include "XGridLayout.h"
#include "XLabel.h"
#include "XSpinBox.h"
#include "XMemory.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON

/* ---- 键名（hsf_client.ini） ---- */
#define LS_KEY_REFRESH   "Data/RefreshMs"    /**< 界面刷新周期 ms */
/* 字体缩放键按 HsfPages 页序编号（N=页枚举值），HsfUi 页字体钩子同名
 * 共读：Page0=主界面 Page3=数字量 Page4=模拟量 Page5=变频器
 * Page6=流量计 Page7=电磁阀 Page9=历史 Page10=日志 Page11=报警。 */
#define LS_KEY_FONT_HOME    "FontScale/Page0"  /**< 主界面字体缩放 % */
#define LS_KEY_FONT_DIGITAL "FontScale/Page3"  /**< 数字量页字体缩放 % */
#define LS_KEY_FONT_ANALOG  "FontScale/Page4"  /**< 模拟量页字体缩放 % */
#define LS_KEY_FONT_VFD     "FontScale/Page5"  /**< 变频器页字体缩放 % */
#define LS_KEY_FONT_FLOW    "FontScale/Page6"  /**< 流量计页字体缩放 % */
#define LS_KEY_FONT_VALVE   "FontScale/Page7"  /**< 电磁阀页字体缩放 % */
#define LS_KEY_FONT_HISTORY "FontScale/Page9"  /**< 历史数据页字体缩放 % */
#define LS_KEY_FONT_LOG     "FontScale/Page10" /**< 日志页字体缩放 % */
#define LS_KEY_FONT_ALARM   "FontScale/Page11" /**< 报警页字体缩放 % */

#define LS_REFRESH_DEF    200  /**< 刷新周期缺省（Python 同值）。 */
#define LS_REFRESH_MIN    100  /**< 刷新周期下限（Python 同值）。 */
#define LS_REFRESH_MAX    2000 /**< 刷新周期上限（Python 同值）。 */

#define LS_FONT_DEF       100  /**< 字体缩放缺省（=内定字号原样）。 */
#define LS_FONT_MIN        50  /**< 字体缩放下限 %。 */
#define LS_FONT_MAX       200  /**< 字体缩放上限 %。 */

/** @brief 本地设置页对象。 */
typedef struct HsfSettingsLocal {
    XWidget      m_base;        /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;      /**< 页根竖排布局。 */
    XLabel       m_title;       /**< 页标题。 */
    XSpinBox     m_refresh;     /**< 界面刷新周期（ms）。 */
    XLabel       m_fontTitle;   /**< 「字体大小」区标题。 */
    XWidget      m_fontCard;    /**< 字体缩放行容器。 */
    XGridLayout  m_fontGrid;    /**< 标签 × spinbox 两列网格。 */
    XLabel       m_fontLblHome;    /**< 主界面行标签。 */
    XSpinBox     m_fontHome;    /**< 主界面字体缩放（FontScale/Page0）。 */
    XLabel       m_fontLblDigital; /**< 数字量页行标签。 */
    XSpinBox     m_fontDigital; /**< 数字量页字体缩放（FontScale/Page3）。 */
    XLabel       m_fontLblAnalog;  /**< 模拟量页行标签。 */
    XSpinBox     m_fontAnalog;  /**< 模拟量页字体缩放（FontScale/Page4）。 */
    XLabel       m_fontLblVfd;     /**< 变频器页行标签。 */
    XSpinBox     m_fontVfd;     /**< 变频器页字体缩放（FontScale/Page5）。 */
    XLabel       m_fontLblFlow;    /**< 流量计页行标签。 */
    XSpinBox     m_fontFlow;    /**< 流量计页字体缩放（FontScale/Page6）。 */
    XLabel       m_fontLblValve;   /**< 电磁阀页行标签。 */
    XSpinBox     m_fontValve;   /**< 电磁阀页字体缩放（FontScale/Page7）。 */
    XLabel       m_fontLblHistory; /**< 历史数据页行标签。 */
    XSpinBox     m_fontHistory; /**< 历史数据页字体缩放（FontScale/Page9）。 */
    XLabel       m_fontLblLog;     /**< 日志页行标签。 */
    XSpinBox     m_fontLog;     /**< 日志页字体缩放（FontScale/Page10）。 */
    XLabel       m_fontLblAlarm;   /**< 报警页行标签。 */
    XSpinBox     m_fontAlarm;   /**< 报警页字体缩放（FontScale/Page11）。 */
    void*        m_user;        /**< 主窗契约上下文（HsfPages_setStatus 用）。 */
    bool         m_noPersist;   /**< 建页期编程赋值中（不得落盘）。 */
} HsfSettingsLocal;

/* ==================== 槽 ==================== */

/** @brief 刷新周期改动：落盘（各页轮询定时器在下一次建页/重启后生效）。 */
static void lsRefreshSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_refresh));
    char buf[64];
    (void)args;
    if (!self || self->m_noPersist) return;
    HsfSettingsStore_setInt(LS_KEY_REFRESH, XSpinBox_value(&self->m_refresh));
    HsfSettingsStore_sync();
    /* [2026-11-07 修] 原格式串写成字面「N ms」（缺 %d），状态栏永远
     * 显示 N 而非实际值——顺手补回。 */
    snprintf(buf, sizeof(buf),
             "界面刷新周期：%d ms",
             XSpinBox_value(&self->m_refresh));
    HsfPages_setStatus(self->m_user, buf);
}

/** @brief 字体缩放改动共用体：落盘 + 状态栏回显（各页槽转调）。 */
static void lsFontPersist(HsfSettingsLocal* self, XSpinBox* box,
                          const char* key, const char* pageName)
{
    char buf[64];
    HsfSettingsStore_setInt(key, XSpinBox_value(box));
    HsfSettingsStore_sync();
    snprintf(buf, sizeof(buf), "%s字体：%d%%",
             pageName, XSpinBox_value(box));
    HsfPages_setStatus(self->m_user, buf);
}

/** @brief 数字量页字体改动：落盘（下次建页生效，建页时经钩子读键折算）。 */
static void lsFontDigitalSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontDigital));
    (void)args;
    if (!self || self->m_noPersist) return;
    lsFontPersist(self, &self->m_fontDigital,
                  LS_KEY_FONT_DIGITAL, "数字量页");
    hsfUi_reapplyPageFonts(HSF_PAGE_MONITOR_DIGITAL);
}

/** @brief 模拟量页字体改动：落盘（下次建页生效）。 */
static void lsFontAnalogSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontAnalog));
    (void)args;
    if (!self || self->m_noPersist) return;
    lsFontPersist(self, &self->m_fontAnalog,
                  LS_KEY_FONT_ANALOG, "模拟量页");
    hsfUi_reapplyPageFonts(HSF_PAGE_MONITOR_ANALOG);
}

/** @brief 流量计页字体改动：落盘（下次建页生效）。 */
static void lsFontFlowSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontFlow));
    (void)args;
    if (!self || self->m_noPersist) return;
    lsFontPersist(self, &self->m_fontFlow,
                  LS_KEY_FONT_FLOW, "流量计页");
    hsfUi_reapplyPageFonts(HSF_PAGE_MONITOR_FLOWMETER);
}

/** @brief 主界面字体改动。 */
static void lsFontHomeSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontHome));
    (void)args;
    if (!self || self->m_noPersist) return;
    lsFontPersist(self, &self->m_fontHome, LS_KEY_FONT_HOME, "主界面");
    hsfUi_reapplyPageFonts(HSF_PAGE_HOME);
}

/** @brief 变频器页字体改动。 */
static void lsFontVfdSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontVfd));
    (void)args;
    if (!self || self->m_noPersist) return;
    lsFontPersist(self, &self->m_fontVfd, LS_KEY_FONT_VFD, "变频器页");
    hsfUi_reapplyPageFonts(HSF_PAGE_MONITOR_VFD);
}

/** @brief 电磁阀页字体改动。 */
static void lsFontValveSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontValve));
    (void)args;
    if (!self || self->m_noPersist) return;
    lsFontPersist(self, &self->m_fontValve, LS_KEY_FONT_VALVE, "电磁阀页");
    hsfUi_reapplyPageFonts(HSF_PAGE_MONITOR_VALVE);
}

/** @brief 历史数据页字体改动。 */
static void lsFontHistorySlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontHistory));
    (void)args;
    if (!self || self->m_noPersist) return;
    lsFontPersist(self, &self->m_fontHistory, LS_KEY_FONT_HISTORY,
                  "历史数据页");
    hsfUi_reapplyPageFonts(HSF_PAGE_DATA_HISTORY);
}

/** @brief 日志页字体改动。 */
static void lsFontLogSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontLog));
    (void)args;
    if (!self || self->m_noPersist) return;
    lsFontPersist(self, &self->m_fontLog, LS_KEY_FONT_LOG, "日志页");
    hsfUi_reapplyPageFonts(HSF_PAGE_DATA_LOG);
}

/** @brief 报警页字体改动。 */
static void lsFontAlarmSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontAlarm));
    (void)args;
    if (!self || self->m_noPersist) return;
    lsFontPersist(self, &self->m_fontAlarm, LS_KEY_FONT_ALARM, "报警页");
    hsfUi_reapplyPageFonts(HSF_PAGE_DATA_ALARM);
}

/* ==================== 建页 ==================== */

/** @brief 建一枚字体行标签（左列说明字）。 */
static void lsFontLabelInit(XLabel* lb, XWidget* parent, const char* text)
{
    XLabel_init(lb, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)lb);
    XLabel_setText_2(lb, text);
    XWidget_show((XWidget*)lb);
}

/** @brief 建一枚字体缩放 spinbox（50–200%，步进 10，初值=持久化值；
 *         先赋值后接槽，建页期初值不触发落盘）。 */
static void lsFontSpinInit(XSpinBox* box, XWidget* parent, const char* key)
{
    int pct = HsfSettingsStore_getInt(key, LS_FONT_DEF);
    if (pct < LS_FONT_MIN) pct = LS_FONT_MIN;
    if (pct > LS_FONT_MAX) pct = LS_FONT_MAX;
    XSpinBox_init(box, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)box);
    XSpinBox_setRange(box, LS_FONT_MIN, LS_FONT_MAX);
    XSpinBox_setSingleStep(box, 10);
    XSpinBox_setSuffix(box, " %");
    XSpinBox_setValue(box, pct);
    XWidget_setFixedWidth((XWidget*)box, 140);
    XWidget_show((XWidget*)box);
}

XWidget* HsfSettingsLocal_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData)
{
    HsfSettingsLocal* self;
    (void)navFn;

    if (!parent) return NULL;
    self = (HsfSettingsLocal*)XMemory_malloc(sizeof(HsfSettingsLocal),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    self->m_user = userData;       /* 状态栏契约上下文（HsfWin）。 */
    HsfTheme_applyDefaultFont(&self->m_base);

    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title,
                     "本地设置"); /* 本地设置 */
    XLabel_setTextPixelSize(&self->m_title, 16);
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    /* ---- 数据刷新卡 ----
     * [2026-11-08 补标签] 原先只有一个裸数字框（卡标题/行标签建页时漏摆），
     * 用户看不出这个 200ms 是干什么的。补：卡标题 + 「界面刷新周期」行 + 说明。 */
    {
        XLabel* head = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (head) {
            XLabel_init(head, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)head);
            XLabel_setText_2(head, "数据刷新");
            XLabel_setTextPixelSize(head, 14);
            XWidget_show((XWidget*)head);
            XBoxLayout_addWidget(&self->m_layout, (XWidget*)head);
        }
    }
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
            XLabel_setText_2(name, "界面刷新周期");
            XWidget_setFixedWidth((XWidget*)name, 110);
            XWidget_show((XWidget*)name);
            XBoxLayout_addWidget(row, (XWidget*)name);

            XSpinBox_init(&self->m_refresh, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_refresh);
            XSpinBox_setRange(&self->m_refresh, LS_REFRESH_MIN, LS_REFRESH_MAX);
            XSpinBox_setSingleStep(&self->m_refresh, 100);
            XSpinBox_setSuffix(&self->m_refresh, " ms");
            self->m_noPersist = true;
            XSpinBox_setValue(&self->m_refresh, HsfSettingsLocal_refreshMs());
            self->m_noPersist = false;
            XWidget_setFixedWidth((XWidget*)&self->m_refresh, 140);
            XWidget_show((XWidget*)&self->m_refresh);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_refresh);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
            XObject_connect_2((XObject*)&self->m_refresh,
                              XSignal(XSpinBox_valueChanged_signal),
                              lsRefreshSlot);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name) XClassDelete((XClass*)name);
        }
    }
    {
        /* 作用范围说明（避免被当成"服务端推送频率"）：只影响客户端本地
         * 轮询（历史/报警/远端设置）；监视页刷新由服务端推送驱动。 */
        XLabel* hint = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (hint) {
            XLabel_init(hint, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)hint);
            XLabel_setTextPixelSize(hint, 12);
            XWidget_setForegroundRole((XWidget*)hint, XPaletteColorRole_Mid);
            XLabel_setWordWrap(hint, true);
            XLabel_setText_2(hint,
                "只影响本机轮询（历史/日志/报警/远端设置页），不改变服务端推送节拍；"
                "改完重启客户端生效。");
            XWidget_show((XWidget*)hint);
            XBoxLayout_addWidget(&self->m_layout, (XWidget*)hint);
        }
    }

    /* ---- 字体大小卡（按页缩放；FontScale/Page<N>，下次建页生效） ---- */
    XLabel_init(&self->m_fontTitle, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_fontTitle);
    {
        XFont f = XWidget_font((XWidget*)&self->m_fontTitle);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_fontTitle, &f);
        XClassDeinit(&f);
    }
    XLabel_setText_2(&self->m_fontTitle,
                     "字体大小（按页，即时生效）");
    XLabel_setTextPixelSize(&self->m_fontTitle, 14);
    XWidget_show((XWidget*)&self->m_fontTitle);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_fontTitle);

    XWidget_init(&self->m_fontCard, &self->m_base, 0);
    XGridLayout_init(&self->m_fontGrid);
    XLayout_setSpacing((XLayout*)&self->m_fontGrid, 8);
    XWidget_setLayout(&self->m_fontCard, (XLayout*)&self->m_fontGrid);
    /* 网格默认摊满页宽，spinbox 会被推到最右、离标签一整行——限宽收拢。 */
    XWidget_setMaximumWidth(&self->m_fontCard, 420);
    lsFontLabelInit(&self->m_fontLblHome, &self->m_fontCard, "主界面");
    lsFontSpinInit(&self->m_fontHome, &self->m_fontCard, LS_KEY_FONT_HOME);
    lsFontLabelInit(&self->m_fontLblDigital, &self->m_fontCard, "数字量页");
    lsFontSpinInit(&self->m_fontDigital, &self->m_fontCard,
                   LS_KEY_FONT_DIGITAL);
    lsFontLabelInit(&self->m_fontLblAnalog, &self->m_fontCard, "模拟量页");
    lsFontSpinInit(&self->m_fontAnalog, &self->m_fontCard,
                   LS_KEY_FONT_ANALOG);
    lsFontLabelInit(&self->m_fontLblVfd, &self->m_fontCard, "变频器页");
    lsFontSpinInit(&self->m_fontVfd, &self->m_fontCard, LS_KEY_FONT_VFD);
    lsFontLabelInit(&self->m_fontLblFlow, &self->m_fontCard, "流量计页");
    lsFontSpinInit(&self->m_fontFlow, &self->m_fontCard,
                   LS_KEY_FONT_FLOW);
    lsFontLabelInit(&self->m_fontLblValve, &self->m_fontCard, "电磁阀页");
    lsFontSpinInit(&self->m_fontValve, &self->m_fontCard, LS_KEY_FONT_VALVE);
    lsFontLabelInit(&self->m_fontLblHistory, &self->m_fontCard, "历史数据页");
    lsFontSpinInit(&self->m_fontHistory, &self->m_fontCard,
                   LS_KEY_FONT_HISTORY);
    lsFontLabelInit(&self->m_fontLblLog, &self->m_fontCard, "日志页");
    lsFontSpinInit(&self->m_fontLog, &self->m_fontCard, LS_KEY_FONT_LOG);
    lsFontLabelInit(&self->m_fontLblAlarm, &self->m_fontCard, "报警页");
    lsFontSpinInit(&self->m_fontAlarm, &self->m_fontCard, LS_KEY_FONT_ALARM);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLblHome, 0, 0, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontHome, 0, 1, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLblDigital, 1, 0, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontDigital, 1, 1, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLblAnalog, 2, 0, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontAnalog, 2, 1, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLblVfd, 3, 0, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontVfd, 3, 1, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLblFlow, 4, 0, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontFlow, 4, 1, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLblValve, 5, 0, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontValve, 5, 1, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLblHistory, 6, 0, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontHistory, 6, 1, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLblLog, 7, 0, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLog, 7, 1, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontLblAlarm, 8, 0, 0);
    XGridLayout_addWidget(&self->m_fontGrid,
                          (XWidget*)&self->m_fontAlarm, 8, 1, 0);
    XWidget_show(&self->m_fontCard);
    XBoxLayout_addWidget(&self->m_layout, &self->m_fontCard);
    XObject_connect_2((XObject*)&self->m_fontHome,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontHomeSlot);
    XObject_connect_2((XObject*)&self->m_fontDigital,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontDigitalSlot);
    XObject_connect_2((XObject*)&self->m_fontAnalog,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontAnalogSlot);
    XObject_connect_2((XObject*)&self->m_fontVfd,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontVfdSlot);
    XObject_connect_2((XObject*)&self->m_fontFlow,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontFlowSlot);
    XObject_connect_2((XObject*)&self->m_fontValve,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontValveSlot);
    XObject_connect_2((XObject*)&self->m_fontHistory,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontHistorySlot);
    XObject_connect_2((XObject*)&self->m_fontLog,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontLogSlot);
    XObject_connect_2((XObject*)&self->m_fontAlarm,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontAlarmSlot);

    /* 尾部弹性空白：多余竖向空间归页尾，控件保持自然高（Python
     * root.addStretch(1) 同款——不加则布局把余量摊给各控件拉高）。 */
    XBoxLayout_addStretch(&self->m_layout, 1);

    XWidget_show(&self->m_base);
    return &self->m_base;
}

void HsfSettingsLocal_adapt(XWidget* page, bool compact)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)page;
    if (!self) return;
    /* 竖屏/窄窗：页边距 16,12→8,6 腾内容宽（与本页 create 边距同源）。
     * 幂等（帧泵每循环调用）。自建根结构必须走自己的 adapt（见
     * HsfDataHistory_adapt 处的类型混淆教训）。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 8 : 16, compact ? 6 : 12,
                               compact ? 8 : 16, compact ? 6 : 12);
}

int HsfSettingsLocal_refreshMs(void)
{
    int ms = HsfSettingsStore_getInt(LS_KEY_REFRESH, LS_REFRESH_DEF);
    if (ms < LS_REFRESH_MIN) ms = LS_REFRESH_MIN;
    if (ms > LS_REFRESH_MAX) ms = LS_REFRESH_MAX;
    return ms;
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON */
