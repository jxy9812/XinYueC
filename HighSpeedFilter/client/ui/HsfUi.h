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
#include "XWidget.h"   /* 页根类型（字号重放接口） */
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

/**
 * @brief      强制紧凑（竖屏）形态档（本地设置页「紧凑模式」开关入口）。
 * @param on   true=强制窄档（覆盖窗口宽度自动判定）；false=恢复自动判定。
 * @note       立即生效（形态切换 + 全页重分档 + 整窗重绘）；持久化由
 *             调用方负责（Display/Compact 键），启动恢复在主窗装配内。
 */
void HsfUi_setForceCompact(bool on);

/**
 * @brief      页面字体缩放折算（全局 FontScale/Global × 页 FontScale/Page<N>）。
 * @param page 页索引（HsfPages 枚举值；越界按 100% 原样返回）。
 * @param px   该页基准像素值（字号或与字号联动的高度）。
 * @return     折算后的像素值（四舍五入；两键均未配置时=px 原值）。
 * @note       各页在 create/adapt 里把写死的字号/高度过一遍本函数即可
 *             实现缩放。折算口径 = **全局% × 页% 合比**（本地设置页
 *             「字体大小」区的「全局缩放」写 FontScale/Global、各页行写
 *             FontScale/Page<N>，N=HsfPages 页序）：全局=100 时与旧版
 *             「仅按页」逐位等值（零行为变化）；全局缩放改动经
 *             hsfUi_reapplyAllFonts 即时重放全部页面与 chrome。
 */
int hsfUi_pageFontPx(int page, int px);

/**
 * @brief      chrome（无页归属界面）字号折算：只乘全局缩放百分比。
 * @param px   基准像素值（字号或与字号联动的高度）。
 * @return     折算后的像素值（四舍五入；全局未配置=px 原值）。
 * @note       供标题栏/状态栏/导航面板/菜单页/登录窗等没有页索引的
 *             模块在建页时折算基准字号用（页面请用 hsfUi_pageFontPx，
 *             页面另有按页缩放一层）。
 */
int hsfUi_fontPx(int px);

/** @brief 全局字体百分比（FontScale/Global；50–200，缺省 100，带缓存）。 */
int hsfUi_globalFontPct(void);

/** @brief 全局字体百分比缓存失效（设置改动后、重放前调用）。 */
void hsfUi_invalidateGlobalFontPct(void);

/** @brief 页字体百分比（合比口径：全局% × 页% 的合计；50–200 夹取后）。 */
int hsfUi_pageFontPct(int page);

/** @brief 每页字号重放回调（页自查重设已知标签的字号）。 */
typedef void (*HsfPageFontApplyFn)(XWidget* page, int pct);

/** @brief 登记某页的字号重放函数与页根（建页后调用）。 */
void hsfUi_registerPageFontApplier(int page, XWidget* root,
                                   HsfPageFontApplyFn fn);

/** @brief 字号设置变更后实时重放该页（不重启客户端）。 */
void hsfUi_reapplyPageFonts(int page);

/**
 * @brief 全局字号改动后的**全量**实时重放（不重启客户端）。
 * @details 覆盖：全部已建页（注册了重放函数的走页自查重放，否则走
 *          adapt 回落）+ chrome（标题栏/状态栏/导航面板）+ 移植页的
 *          adapt 去重标记失效重跑。本地设置页「全局缩放」spinbox 的
 *          变更槽调用它（先 setInt 落盘 + invalidate 缓存）。
 */
void hsfUi_reapplyAllFonts(void);

/**
 * @brief 给「无字段引用」的子控件登记字号基准（动态属性 hsfFontBase）。
 * @param w      目标控件（通常是无字段引用的 malloc 标签）。
 * @param basePx 源码基准像素值（**未折算**的源码常量；同值可重复登记）。
 * @details 基准挂 XObject 动态属性（随对象析构，无需销毁清账）；
 *          hsfUi_reapplyTaggedFonts 按属性重放。带字段引用的控件请直接
 *          在模块 applyFont 里重放，不走本机制。
 */
void hsfUi_tagFontBase(XWidget* w, int basePx);

/**
 * @brief 按 pct 重放 root 子树内登记过字号基准的控件（递归）。
 * @param root 子树根（页根）。
 * @param pct  合比百分比（全局 × 页）。
 * @note  与 hsfUi_tagFontBase 配套：远端设置页等存在大量无字段引用的
 *        malloc 标签，无法逐个存字段，用属性登记 + 子树重放兜底。
 */
void hsfUi_reapplyTaggedFonts(XWidget* root, int pct);

#ifdef __cplusplus
}
#endif

#endif /* HSFUI_H */
