/**
 * @file       HsfSettingsLocal.c
 * @brief      本地设置页（三分区合并页，页号 13）。
 * @details    [2026-11-13 用户定案 三页合并] 原三页——本地设置(13)/
 *             悬浮窗设置(16)/窗口设置(17)——合并为**一个**页面（页号
 *             13 保留不变；16/17 两个页号作废，页表 18→16 页）。页内
 *             三个分区：
 *              ① 「界面设置」= 原本地设置全部项（数据刷新/字体大小）；
 *              ② 「悬浮窗」= 原悬浮窗设置全部项（显示总开关/FPS/帧耗时/
 *                 CPU·GPU/网络/内存/内存显示种类/数据来源/预设位置/固定
 *                 位置/允许拖动/字号/自适应尺寸/复位默认）；
 *              ③ 「窗口与屏幕」= 原窗口设置全部项（主题/显示边框/显示
 *                 状态栏/最大化/全屏/显示鼠标光标/鼠标外形/画布旋转/
 *                 窗口方向/自动熄屏/自动休眠/立即熄屏/唤醒方式三项/
 *                 复位默认）。
 *             ②③ 由 HsfOverlayPage / HsfWindowPage 以**分区构建器**接入
 *             （内容与原独立页一字不差、兑现路径不变），分区标题统一在
 *             本页建（同一缩进/字号/加粗）。
 * @details    [对齐 Python 客户端 LocalSettingsPage]
 *              ① 数据刷新：界面刷新周期（默认 200ms、100~2000；各数据页
 *                 轮询定时器建页时取 HsfSettingsLocal_refreshMs()——只影响
 *                 本机轮询，不改变服务端推送节拍；重启客户端生效）；
 *              ② 字体大小：**全局缩放**（FontScale/Global 键，作用全部
 *                 界面：页面/标题栏/状态栏/导航/菜单页/设置页/网络页/
 *                 登录窗/表格工具条/表格本体；改动即时全量重放，经
 *                 hsfUi_reapplyAllFonts）+ 按页缩放百分比（HsfUi
 *                 hsfUi_pageFontPx 钩子，FontScale/Page<N> 键，即时重放）。
 *                 两层**相乘**：实际字号 = 基准 × 全局% × 页%（全局 100%
 *                 时等于旧版「仅按页」行为）。
 *             [显示相关不在此页] 紧凑模式/全屏/边框等在「窗口与屏幕」分区
 *             （HsfWindowPage 分区），本页只管数据刷新与字号。
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
#include "HsfOverlayPage.h"  /* 「悬浮窗」分区构建器（并入本页）。 */
#include "HsfWindowPage.h"   /* 「窗口与屏幕」分区构建器（并入本页）。 */
#include "HsfDemoCompat.h"  /* 分区字号重放/自适应入口。 */
#include "HsfWebServer.h"   /* 「网页服务」分区（起停 + 端口，2026-11-14）。 */
#include "XAbstractButton.h"
#include "XBoxLayout.h"
#include "XCheckBox.h"
#include "XFont.h"     /* 微调框字号折算（XFont_setPixelSize）。 */
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
/* 全局字体缩放键（HsfUi.c 的 HSF_FONT_GLOBAL_KEY 同字面——两处各有一份
 * #define，注释互为对照，勿单侧改键名）。 */
#define LS_KEY_FONT_GLOBAL  "FontScale/Global" /**< 全局字体缩放 %（全部界面） */
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
    XWidget      m_globalCard;  /**< 全局缩放行容器（网格上方单独一行）。 */
    XBoxLayout   m_globalBox;   /**< 全局缩放行横排布局（成员化：栈上局部
                                 *   布局对象会成悬垂指针）。 */
    XLabel       m_fontLblGlobal; /**< 全局缩放行标签。 */
    XSpinBox     m_fontGlobal;  /**< 全局字体缩放（FontScale/Global）。 */
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
    /* [2026-11-13 三页合并] 三个分区标题 + 两个分区根（②③ 由移植页模块
     * 以分区构建器接入；标题统一在本页建，保证三段同缩进/字号/加粗）。 */
    XLabel       m_secTitle[4];  /**< 分区标题：界面设置/悬浮窗/窗口与屏幕/网页服务。 */
    XWidget*     m_overlaySec;   /**< 「悬浮窗」分区根（HsfOverlayPage 建）。 */
    XWidget*     m_windowSec;    /**< 「窗口与屏幕」分区根（HsfWindowPage 建）。 */
    /* [2026-11-14 网页服务分区] 开关 + 端口 + 状态行（本页自建）。 */
    XWidget      m_webCard;      /**< 网页服务卡容器。 */
    XBoxLayout   m_webBox;       /**< 网页服务卡竖排布局（成员化）。 */
    XBoxLayout   m_webPortRow;   /**< 端口行横排布局（成员化：标签+微调框）。 */
    XCheckBox    m_webEnabled;   /**< 「随客户端启动网页服务」。 */
    XLabel       m_webPortLbl;   /**< 「网页端口」行标签。 */
    XSpinBox     m_webPort;      /**< 网页端口（1024~65535；缺省 8085）。 */
    XLabel       m_webStatus;    /**< 状态行（运行中 / 失败原因）。 */
    char         m_webStatusText[192]; /**< 上次状态文案（变了才写控件）。 */
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
    lsFontPersist(self, &self->m_fontValve, LS_KEY_FONT_VALVE, "电动球阀页");
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

/** @brief 全局缩放改动：落盘 + 缓存失效 + 全量即时重放（全部页 + chrome）。
 *  @details 与按页槽的区别：全局层作用于**全部界面**（含标题栏/状态栏/
 *           导航/菜单页/设置页/网络页/登录窗/表格工具条/表格本体——这些
 *           无按页缩放），一次 reapplyAllFonts 全覆盖；与各页缩放的
 *           关系是**相乘**（状态行回显写明）。 */
static void lsFontGlobalSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_fontGlobal));
    char buf[96];
    (void)args;
    if (!self || self->m_noPersist) return;
    HsfSettingsStore_setInt(LS_KEY_FONT_GLOBAL,
                            XSpinBox_value(&self->m_fontGlobal));
    HsfSettingsStore_sync();
    hsfUi_invalidateGlobalFontPct();
    hsfUi_reapplyAllFonts();
    snprintf(buf, sizeof(buf),
             "全局字体：%d%%（即时生效；与各页缩放相乘）",
             XSpinBox_value(&self->m_fontGlobal));
    HsfPages_setStatus(self->m_user, buf);
}

/* ==================== 网页服务分区（2026-11-14） ==================== */

/** @brief 刷新网页服务状态行（文案变了才写控件——每拍调也不刷屏）。 */
static void lsWebStatusRefresh(HsfSettingsLocal* self)
{
    char buf[192];
    if (!self) return;
    HsfWebServer_status(buf, (int)sizeof(buf));
    if (strcmp(buf, self->m_webStatusText) == 0) return;
    snprintf(self->m_webStatusText, sizeof(self->m_webStatusText), "%s",
             buf);
    XLabel_setText_2(&self->m_webStatus, self->m_webStatusText);
}

/** @brief 网页服务开关改动：落盘 + 即时起停 + 状态行刷新。 */
static void lsWebEnabledSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                        offsetof(HsfSettingsLocal, m_webEnabled));
    bool on;
    (void)args;
    if (!self || self->m_noPersist) return;
    on = XCheckBox_isChecked(&self->m_webEnabled);
    /* [即时生效] 落盘 + 起停都在门面里做（本页只管发意图）。 */
    HsfWebServer_setEnabled(on);
    lsWebStatusRefresh(self);
    HsfPages_setStatus(self->m_user,
                       on ? "网页服务已开启（随客户端启动）"
                          : "网页服务已关闭");
}

/* ==================== 建页 ==================== */

/** @brief 本页字号基准常量（applyFonts 重放与建页折算共用同一份）。
 *  @details 本页页根无 helper（此前不接字号缩放），2026-11-10 接线：
 *           建页折算与重放都锚定这份常量——基准=源码常量、pct 现读
 *           现算，全局改动经 hsfUi_reapplyAllFonts 回放。 */
#define LS_TITLE_FONT_PX   16  /**< 页标题「本地设置」。 */
#define LS_HEAD_FONT_PX    14  /**< 卡片标题（数据刷新/字体大小）。 */
#define LS_HINT_FONT_PX    12  /**< 说明小字（两处 hint）。 */
#define LS_LABEL_FONT_PX   16  /**< 行说明标签（默认渲染口径）。 */
#define LS_SPIN_FONT_PX    16  /**< 微调框文本（默认渲染口径）。 */
/** 分区标题字号（15：与移植页原页标题同档，三段统一）。 */
#define LS_SECTION_FONT_PX 15

/** 分区标题文案（四个分区，顺序=页内出现顺序）。 */
static const char* const kLsSectionTitles[4] = {
    "界面设置",     /**< ① 原本地设置全部项。 */
    "悬浮窗",       /**< ② 原悬浮窗设置全部项。 */
    "窗口与屏幕",   /**< ③ 原窗口设置全部项（含屏幕休眠与电源策略）。 */
    "网页服务"      /**< ④ 网页端起停 + 端口（2026-11-14）。 */
};

/** @brief 本页字号实时重放（合比 pct 由 hsfUi_reapplyPageFonts 传入）。 */
static void lsApplyFonts(XWidget* page, int pct)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)page;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_title, (LS_TITLE_FONT_PX * pct + 50) / 100);
    XLabel_setTextPixelSize(&self->m_fontTitle,
                            (LS_HEAD_FONT_PX * pct + 50) / 100);
    XLabel_setTextPixelSize(&self->m_fontLblGlobal,
                            (LS_LABEL_FONT_PX * pct + 50) / 100);
    {
        /* 四个分区标题（同档同款）。 */
        int si;
        for (si = 0; si < 4; ++si)
            XLabel_setTextPixelSize(&self->m_secTitle[si],
                                    (LS_SECTION_FONT_PX * pct + 50) / 100);
    }
    {
        /* 微调框文本（刷新 + 全局 + 9 页行 + 网页端口）：绝对基准直写。 */
        XSpinBox* boxes[12];
        int i;
        boxes[0] = &self->m_refresh;
        boxes[1] = &self->m_fontGlobal;
        boxes[2] = &self->m_fontHome;
        boxes[3] = &self->m_fontDigital;
        boxes[4] = &self->m_fontAnalog;
        boxes[5] = &self->m_fontVfd;
        boxes[6] = &self->m_fontFlow;
        boxes[7] = &self->m_fontValve;
        boxes[8] = &self->m_fontHistory;
        boxes[9] = &self->m_fontLog;
        boxes[10] = &self->m_fontAlarm;
        boxes[11] = &self->m_webPort;
        for (i = 0; i < 12; ++i) {
            XFont f = XWidget_font((XWidget*)boxes[i]);
            XFont_setPixelSize(&f, (LS_SPIN_FONT_PX * pct + 50) / 100);
            XWidget_setFont((XWidget*)boxes[i], &f);
            XClassDeinit(&f);
        }
    }
    /* [2026-11-14 网页服务分区] 端口行标签 + 状态行同档。 */
    XLabel_setTextPixelSize(&self->m_webPortLbl,
                            (LS_LABEL_FONT_PX * pct + 50) / 100);
    XLabel_setTextPixelSize(&self->m_webStatus,
                            (LS_LABEL_FONT_PX * pct + 50) / 100);
    /* 无字段引用的匿名标签（数据刷新卡标题/两处说明小字）：基准挂
     * 动态属性，按属性重放（建页时 hsfUi_tagFontBase 登记）。 */
    hsfUi_reapplyTaggedFonts(page, pct);
    /* [2026-11-13 三页合并] 两个分区的字号重放转调（每页只允许注册
     * 一个重放函数 ⇒ 合并后由本页统一调度，分区模块不再自注册）。 */
    if (self->m_overlaySec)
        hsf_page_overlay_applyFonts(self->m_overlaySec, pct);
    if (self->m_windowSec)
        hsf_page_window_applyFonts(self->m_windowSec, pct);
    XWidget_update(page);
}

/** @brief 建一枚分区标题（15px 加粗；三个分区同款：界面设置/悬浮窗/窗口与屏幕）。 */
static void lsSectionTitleInit(XLabel* lb, XWidget* parent, const char* text)
{
    XLabel_init(lb, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)lb);
    {
        XFont f = XWidget_font((XWidget*)lb);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)lb, &f);
        XClassDeinit(&f);
    }
    XLabel_setText_2(lb, text);
    XLabel_setTextPixelSize(lb,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_SECTION_FONT_PX));
    XWidget_setFixedHeight((XWidget*)lb, 24);
    XWidget_show((XWidget*)lb);
}

/** @brief 建一枚字体行标签（左列说明字；补显式字号=基准 16 折算）。 */
static void lsFontLabelInit(XLabel* lb, XWidget* parent, const char* text)
{
    XLabel_init(lb, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)lb);
    XLabel_setTextPixelSize(lb,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_LABEL_FONT_PX));
    XLabel_setText_2(lb, text);
    XWidget_show((XWidget*)lb);
}

/** @brief 建一枚字体缩放 spinbox（50–200%，步进 10，初值=持久化值；
 *         先赋值后接槽，建页期初值不触发落盘；字号按本页折算）。 */
static void lsFontSpinInit(XSpinBox* box, XWidget* parent, const char* key)
{
    int pct = HsfSettingsStore_getInt(key, LS_FONT_DEF);
    if (pct < LS_FONT_MIN) pct = LS_FONT_MIN;
    if (pct > LS_FONT_MAX) pct = LS_FONT_MAX;
    XSpinBox_init(box, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)box);
    {
        XFont f = XWidget_font((XWidget*)box);
        XFont_setPixelSize(&f,
            hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_SPIN_FONT_PX));
        XWidget_setFont((XWidget*)box, &f);
        XClassDeinit(&f);
    }
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
    XLabel_setTextPixelSize(&self->m_title,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_TITLE_FONT_PX));
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    /* ---- 分区①标题「界面设置」（原本地设置全部项的内容自此开始） ---- */
    lsSectionTitleInit(&self->m_secTitle[0], &self->m_base,
                       kLsSectionTitles[0]);
    XBoxLayout_addWidget(&self->m_layout, &self->m_secTitle[0]);

    /* ---- 数据刷新卡 ----
     * [2026-11-08 补标签] 原先只有一个裸数字框（卡标题/行标签建页时漏摆），
     * 用户看不出这个 200ms 是干什么的。补：卡标题 + 「界面刷新周期」行 + 说明。
     * [2026-11-10 全局缩放] 卡标题/说明是 malloc 匿名标签（无结构体字段）：
     * 建页折算 + hsfUi_tagFontBase 登记基准，lsApplyFonts 按属性重放。 */
    {
        XLabel* head = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (head) {
            XLabel_init(head, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)head);
            XLabel_setText_2(head, "数据刷新");
            XLabel_setTextPixelSize(head,
                hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_HEAD_FONT_PX));
            hsfUi_tagFontBase((XWidget*)head, LS_HEAD_FONT_PX);
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
            {
                XFont f = XWidget_font((XWidget*)&self->m_refresh);
                XFont_setPixelSize(&f,
                    hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL,
                                     LS_SPIN_FONT_PX));
                XWidget_setFont((XWidget*)&self->m_refresh, &f);
                XClassDeinit(&f);
            }
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
            XLabel_setTextPixelSize(hint,
                hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_HINT_FONT_PX));
            hsfUi_tagFontBase((XWidget*)hint, LS_HINT_FONT_PX);
            HsfTheme_applySecondaryText((XWidget*)hint);
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
                     "字体大小（全局 + 按页，即时生效）");
    XLabel_setTextPixelSize(&self->m_fontTitle,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_HEAD_FONT_PX));
    XWidget_show((XWidget*)&self->m_fontTitle);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_fontTitle);

    /* ---- 全局缩放行（全部界面；作用面=页面+chrome+登录窗+表格） ----
     * [铁律6 真兑现] 本行接通 hsfUi_reapplyAllFonts（全部页 + 标题栏/
     * 状态栏/导航/菜单页重放 + adapt 回落重跑）之后才摆出来。 */
    XWidget_init(&self->m_globalCard, &self->m_base, 0);
    XBoxLayout_init(&self->m_globalBox, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_globalBox, 8);
    XWidget_setLayout(&self->m_globalCard, (XLayout*)&self->m_globalBox);
    lsFontLabelInit(&self->m_fontLblGlobal, &self->m_globalCard,
                    "全局缩放（全部界面）");
    lsFontSpinInit(&self->m_fontGlobal, &self->m_globalCard,
                   LS_KEY_FONT_GLOBAL);
    XBoxLayout_addWidget(&self->m_globalBox, (XWidget*)&self->m_fontLblGlobal);
    XBoxLayout_addWidget(&self->m_globalBox, (XWidget*)&self->m_fontGlobal);
    XBoxLayout_addStretch(&self->m_globalBox, 1);
    XWidget_setMaximumWidth(&self->m_globalCard, 480);
    XWidget_show(&self->m_globalCard);
    XBoxLayout_addWidget(&self->m_layout, &self->m_globalCard);
    XObject_connect_2((XObject*)&self->m_fontGlobal,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsFontGlobalSlot);
    {
        /* 相乘关系说明（用户口径：与既有按页缩放的关系要说清楚）。 */
        XLabel* hint = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (hint) {
            XLabel_init(hint, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)hint);
            XLabel_setTextPixelSize(hint,
                hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_HINT_FONT_PX));
            hsfUi_tagFontBase((XWidget*)hint, LS_HINT_FONT_PX);
            HsfTheme_applySecondaryText((XWidget*)hint);
            XLabel_setWordWrap(hint, true);
            XLabel_setText_2(hint,
                "全局缩放作用于全部界面（含标题栏/状态栏/导航/菜单页/设置页/"
                "网络页/登录窗/表格），并与各页缩放相乘：实际字号 = 基准 × "
                "全局% × 页%（例：全局 150%、页面 100% ⇒ 150%）。");
            XWidget_show((XWidget*)hint);
            XBoxLayout_addWidget(&self->m_layout, (XWidget*)hint);
        }
    }

    /* 字号重放注册延后到建页末尾（三个分区都建完再注册，见文末）。 */

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
    lsFontLabelInit(&self->m_fontLblValve, &self->m_fontCard, "电动球阀页");
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

    /* ---- 分区②「悬浮窗」（原悬浮窗设置页的全部内容，分区构建器接入） ----
     * [2026-11-13 三页合并] 内容与原独立页 16 **一字不差**（显示总开关/
     * FPS/帧耗时/CPU·GPU/网络/内存/内存显示种类/数据来源/预设位置/固定
     * 位置/允许拖动/字号/自适应尺寸/复位默认），兑现路径不变（悬浮层
     * getter + INI 落盘 + 即时生效）；分区标题统一在本页建。 */
    lsSectionTitleInit(&self->m_secTitle[1], &self->m_base,
                       kLsSectionTitles[1]);
    XBoxLayout_addWidget(&self->m_layout, &self->m_secTitle[1]);
    self->m_overlaySec = HsfOverlayPage_buildSection(&self->m_base, userData);
    if (self->m_overlaySec)
        XBoxLayout_addWidget(&self->m_layout, self->m_overlaySec);

    /* ---- 分区③「窗口与屏幕」（原窗口设置页的全部内容，含屏幕休眠/电源
     * 策略；分区构建器接入） ----
     * [2026-11-13 三页合并] 内容与原独立页 17 **一字不差**（主题/显示
     * 边框/显示状态栏/最大化/全屏/显示鼠标光标/鼠标外形/画布旋转/窗口
     * 方向/自动熄屏/自动休眠/立即熄屏/唤醒方式三项/复位默认）。 */
    lsSectionTitleInit(&self->m_secTitle[2], &self->m_base,
                       kLsSectionTitles[2]);
    XBoxLayout_addWidget(&self->m_layout, &self->m_secTitle[2]);
    self->m_windowSec = HsfWindowPage_buildSection(&self->m_base, userData);
    if (self->m_windowSec)
        XBoxLayout_addWidget(&self->m_layout, self->m_windowSec);

    /* ---- 分区④「网页服务」（2026-11-14）----
     * [为什么在这里] 网页端（webui）此前只能手工起、端口写死命令行 ⇒
     * 「客户端 + 网页同开」要靠人记命令。本分区给两件东西：**开关**
     * （随客户端启动与否）与**端口**（缺省 8085——本机 8080 常被别的
     * 程序占着）。改动即时生效（开关立刻起停；端口由 1s 刷新节拍提交，
     * 见 HsfSettingsLocal_refresh）。
     * [状态行说人话] 运行中给地址；失败给原因（端口被占用/找不到包目录/
     * 找不到 python），不静默失败。 */
    lsSectionTitleInit(&self->m_secTitle[3], &self->m_base,
                       kLsSectionTitles[3]);
    XBoxLayout_addWidget(&self->m_layout, &self->m_secTitle[3]);
    XWidget_init(&self->m_webCard, &self->m_base, 0);
    XBoxLayout_init(&self->m_webBox, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&self->m_webBox, 8);
    XWidget_setLayout(&self->m_webCard, (XLayout*)&self->m_webBox);
    XWidget_setMaximumWidth(&self->m_webCard, 480);
    /* ① 开关（先设值再接槽：建页期编程赋值不触发落盘/起停）。 */
    XCheckBox_init(&self->m_webEnabled, &self->m_webCard, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_webEnabled);
    XCheckBox_setText_2(&self->m_webEnabled, "随客户端启动网页服务");
    XCheckBox_setChecked(&self->m_webEnabled, HsfWebServer_enabled());
    XWidget_show((XWidget*)&self->m_webEnabled);
    XBoxLayout_addWidget(&self->m_webBox, (XWidget*)&self->m_webEnabled);
    /* ② 端口行（标签 + 微调框）。 */
    lsFontLabelInit(&self->m_webPortLbl, &self->m_webCard, "网页端口");
    XSpinBox_init(&self->m_webPort, &self->m_webCard, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_webPort);
    {
        XFont f = XWidget_font((XWidget*)&self->m_webPort);
        XFont_setPixelSize(&f,
            hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_SPIN_FONT_PX));
        XWidget_setFont((XWidget*)&self->m_webPort, &f);
        XClassDeinit(&f);
    }
    XSpinBox_setRange(&self->m_webPort, HSF_WEB_MIN_PORT, HSF_WEB_MAX_PORT);
    XSpinBox_setSingleStep(&self->m_webPort, 1);
    XSpinBox_setValue(&self->m_webPort, HsfWebServer_port());
    XWidget_show((XWidget*)&self->m_webPort);
    /* 端口行：标签 + 微调框同一横排。
     * [为什么用成员化的横排布局而不是临时网格] 布局对象**必须成员化**
     * （栈上/堆上临时布局会成悬垂指针）且**只能 addLayout 接进父布局**
     * ——当 widget 用 addWidget 加会把「布局」当控件塞进父布局，父布局
     * 按控件管理它 ⇒ 建页期即崩（2026-11-14 实锅：XAccessible 通知路径
     * AV，栈在 HsfSettingsLocal_create → XBoxLayout_addWidget）。 */
    XBoxLayout_init(&self->m_webPortRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_webPortRow, 8);
    XBoxLayout_addWidget(&self->m_webPortRow,
                         (XWidget*)&self->m_webPortLbl);
    XBoxLayout_addWidget(&self->m_webPortRow, (XWidget*)&self->m_webPort);
    XBoxLayout_addStretch(&self->m_webPortRow, 1);  /* 行尾余量归 stretch（控件左聚）。 */
    XBoxLayout_addLayout(&self->m_webBox, (XLayout*)&self->m_webPortRow);
    /* ③ 状态行（运行中/失败原因；1s 刷新节拍里更新）。 */
    XLabel_init(&self->m_webStatus, &self->m_webCard, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_webStatus);
    XLabel_setTextPixelSize(&self->m_webStatus,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_LABEL_FONT_PX));
    HsfTheme_applySecondaryText((XWidget*)&self->m_webStatus);
    self->m_webStatusText[0] = '\0';
    XWidget_show((XWidget*)&self->m_webStatus);
    XBoxLayout_addWidget(&self->m_webBox, (XWidget*)&self->m_webStatus);
    /* ④ 说明小字（说明它是什么、端口冲突会怎样）。 */
    {
        XLabel* hint = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (hint) {
            XLabel_init(hint, &self->m_webCard, 0);
            HsfTheme_applyDefaultFont((XWidget*)hint);
            XLabel_setTextPixelSize(hint,
                hsfUi_pageFontPx(HSF_PAGE_SETTINGS_LOCAL, LS_HINT_FONT_PX));
            hsfUi_tagFontBase((XWidget*)hint, LS_HINT_FONT_PX);
            HsfTheme_applySecondaryText((XWidget*)hint);
            XLabel_setWordWrap(hint, true);
            XLabel_setText_2(hint,
                "网页服务把同一台服务端的数据以网页形式提供（本机或局域网"
                "内浏览器均可访问，浏览器用各自的账号登录）。默认端口 8085"
                "（8080 常被别的程序占用）；端口被占用时会启动失败，状态行"
                "会说明原因。网页端日志：程序目录 cfg/hsf_web.log。");
            XWidget_show((XWidget*)hint);
            XBoxLayout_addWidget(&self->m_webBox, (XWidget*)hint);
        }
    }
    XWidget_show(&self->m_webCard);
    XBoxLayout_addWidget(&self->m_layout, &self->m_webCard);
    /* 开关槽最后接（避免建页期设值触发起停）。 */
    XObject_connect_2((XObject*)&self->m_webEnabled,
                      XSignal(XAbstractButton_toggled_signal),
                      lsWebEnabledSlot);
    lsWebStatusRefresh(self);

    /* 字号重放注册（改动即时生效；本页是自建根结构）。
     * [2026-11-13] 注册放在**三个分区都建完之后**：重放体会转调两个
     * 分区的重放函数（每页只允许注册一个重放函数），分区未建完时转调
     * 会拿到空根。 */
    hsfUi_registerPageFontApplier(HSF_PAGE_SETTINGS_LOCAL, &self->m_base,
                                  lsApplyFonts);

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
    /* [2026-11-13 三页合并] 两个分区的窄宽分档也走同一次 adapt（分区根
     * 宽度 = 页宽，分档判据与原独立页同口径）。 */
    if (self->m_overlaySec)
        hsf_page_overlay_adapt(self->m_overlaySec);
    if (self->m_windowSec)
        hsf_page_window_adapt(self->m_windowSec);
}

/** @brief 取「悬浮窗」分区根（自测钩子/主窗用；NULL=分区未建）。 */
XWidget* HsfSettingsLocal_overlaySection(XWidget* page)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)page;
    return self ? self->m_overlaySec : NULL;
}

/** @brief 取「窗口与屏幕」分区根（自测钩子/主窗用；NULL=分区未建）。 */
XWidget* HsfSettingsLocal_windowSection(XWidget* page)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)page;
    return self ? self->m_windowSec : NULL;
}

int HsfSettingsLocal_refreshMs(void)
{
    int ms = HsfSettingsStore_getInt(LS_KEY_REFRESH, LS_REFRESH_DEF);
    if (ms < LS_REFRESH_MIN) ms = LS_REFRESH_MIN;
    if (ms > LS_REFRESH_MAX) ms = LS_REFRESH_MAX;
    return ms;
}

/**
 * @brief 本页周期刷新（主窗帧泵 1s 节拍调；仅本页可见时）。
 * @details 两件事：
 *           ① **提交端口改动**：微调框里的值与已生效端口不一致时提交
 *              （提交=落盘 + 重启网页服务子进程换监听口）。放在 1s 节拍
 *              里而不是 valueChanged 槽里——边打字边重启进程会把机器
 *              折腾得没完（用户每敲一个数字就换一个监听口）。
 *           ② **刷新状态行**：子进程退出（端口被占用等）时状态要变。
 * @note  与主界面页同款口径：**不用页内定时器**，借主窗帧泵的节拍。
 */
void HsfSettingsLocal_refresh(XWidget* page)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)page;
    if (!self) return;
    /* ① 端口提交（值变了才提交；提交后控件值与生效值一致 ⇒ 幂等）。 */
    {
        int want = XSpinBox_value(&self->m_webPort);
        if (want >= HSF_WEB_MIN_PORT && want <= HSF_WEB_MAX_PORT &&
            want != HsfWebServer_port()) {
            HsfWebServer_setPort(want);
        }
    }
    /* ② 状态行。 */
    lsWebStatusRefresh(self);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON */
