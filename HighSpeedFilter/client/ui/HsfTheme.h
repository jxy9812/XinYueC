/**
 * @file       HsfTheme.h
 * @brief      客户端 UI 共享主题：缺省字体装配 + 品牌配色常量。
 * @details    chrome 配色单一事实源：顶栏/状态栏底色等字面量集中在此，
 *             改主题一处生效。缺省字体装配与 XGuiWindowDemo 同款
 *             （exeDir/../Library/XFont 直连轮廓字库，失败回落内置）。
 */
#ifndef HSFTHEME_H
#define HSFTHEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include <stdbool.h>

/* ---- 品牌配色（ARGB；改主题只动这里） ---- */
#define HSF_COLOR_BRAND     0xff1f4e79u /**< 顶栏深蓝底（同 XGuiDemo 品牌条）。 */
#define HSF_COLOR_STATUS_BG 0xff000000u /**< 底部状态栏黑底。 */
/* [chrome 文本脱钩调色板 2026-11-11 双主题走查] 顶栏/状态栏的文字**不得**
 * 走调色板 HighlightedText 角色：该角色的语义是「Highlight 强调蓝上的
 * 文字」，旗舰深色盘把它定制成 #101820（vs Highlight 6.82，登记见
 * HsfThemeCss.h 深色段头）——而 chrome 两块底（品牌蓝/黑）**不随主题翻**，
 * 深色下就出现「#101820 落黑底 1.17:1 / 落品牌蓝 2.07:1」的不可读项
 * （走查实锅）。chrome 底恒定 ⇒ 文字恒定白，两主题同值：
 * 白 vs #1F4E79 ≈ 8.4:1、白 vs 黑 21:1（均 ≥4.5）。 */
#define HSF_COLOR_CHROME_TEXT 0xffffffffu /**< 顶栏/状态栏文本（两主题同值）。 */

/* ---- 深色主题页面自绘面/文本令牌（2026-11-11 走查补） ----
 * 监控各页的白卡/仪表盘/状态文字是**页面自绘或调色板角色**，旗舰深色
 * 盘挂上后白面仍在、文字翻亮 ⇒ 1.2:1 级不可读（主页卡标题/表盘中心值
 * /仪表标题/说明小字，走查实锅）。页面按 HsfTheme_isDarkSkin() 在本表
 * 取深套；亮套维持各页现行字面量不动（亮色走查达标，不为改而改）。
 * 取值与 HsfThemeCss.h 深色登记表/HsfTheme.c 深色盘同源：
 * 面板=#23282F（QSS 控件底）、软描边=#39414C、正文=#E8EAED、
 * 次级=#A9B2BC（vs 窗底 7.71/面板 6.91）、强调=#4DA3FF（=Highlight）。 */
#define HSF_COLOR_PANEL_DARK       0xFF23282Fu /**< 深·卡/表盘面（=QSS 控件底）。 */
#define HSF_COLOR_WINDOW_DARK      0xFF1B1F24u /**< 深·窗底（=QSS 背景，登记见 HsfThemeCss.h 深色段头）。整页自绘看板的底色令牌：看板不再是深色页面中央的白块；正文落此底 13.74、次级 7.71（均 ≥4.5）。 */
#define HSF_COLOR_SOFT_BORDER_DARK 0xFF39414Cu /**< 深·软描边（卡框/表框线）。 */
#define HSF_COLOR_TEXT_DARK        0xFFE8EAEDu /**< 深·正文（=调色板 WindowText）。 */
/* 深·次级文本用上方 HSF_COLOR_TEXT_SECONDARY_DARK（#A9B2BC）。 */
#define HSF_COLOR_ACCENT_DARK      0xFF4DA3FFu /**< 深·强调线（=Highlight）。 */

/* ---- 四态语义色（2026-11-11 设计定稿；两主题各一套，状态色单一事实源）--
 *  亮套按白卡正文 ≥4.5:1 核算定稿（弃选未达标套一并登记在注释）；
 *  深套按深色面板/窗底/悬停底/选中底全表面 ≥4.5:1 核算定稿（对比值
 *  实跑复核，登记表见 HsfThemeCss.h 深色段头）。ISA-101 口径：饱和色
 *  只给异常——红只给报警、琥珀只给 advisory/偏差，不作运行指示。
 * [文本色通道口径] 本引擎控件文本色走调色板不走 QSS；但监控各页的
 *  状态文字是**页面自绘**（XPainter 直取色常量），故这里的常量就是
 *  可达通道——页面按当前主题取亮套/深套。若某控件文字想随 QSS 换色：
 *  不可达（唯一例外=表头 ::section 的 color:），改用徽章盒底色
 *  （盒外观层）承载。现状各页字面量（HsfHomePage HOME_CLR_*、
 *  HsfMonitorVfd VFD_COLOR_*、HsfMonitorFlowMeter FM_COLOR_*、
 *  HsfMonitorValve VALVE_CLR_*、HsfMonitorDigital DIGITAL_COLOR_*）
 *  归一到本表属页面工单（所有权之外的文件，本轮不动，见实施记录）。 */
#define HSF_COLOR_STATE_OK              0xFF2E7D32u /**< 亮·正常绿（白 5.13:1；弃 #2E9E44 白 3.45）。 */
#define HSF_COLOR_STATE_WARN            0xFFA85900u /**< 亮·警告琥珀（白 5.13:1；弃 #ED6C02 3.11/#CA8A04 2.94）。 */
#define HSF_COLOR_STATE_FAULT           0xFFC62828u /**< 亮·故障红（白 5.62:1；归一 #D13438 4.93/#D93636 4.63）。 */
#define HSF_COLOR_STATE_OFFLINE         0xFF5A6572u /**< 亮·离线灰（白 5.93:1；=亮色控件描边同值不新增色相）。 */
#define HSF_COLOR_STATE_OK_DARK         0xFF45B363u /**< 深·正常绿（面板 5.57/窗底 6.22/悬停底 4.73/选中底 4.69；弃 #34A853 悬停底 4.13）。 */
#define HSF_COLOR_STATE_WARN_DARK       0xFFF9AB00u /**< 深·警告琥珀（面板 7.66/窗底 8.56/悬停底 6.52/选中底 6.46）。 */
#define HSF_COLOR_STATE_FAULT_DARK      0xFFF6796Bu /**< 深·故障红·文字档（面板 5.55/窗底 6.20/悬停底 4.72/选中底 4.68；弃 #EA4335 面板 3.78 文字不达）。 */
#define HSF_COLOR_STATE_FAULT_FILL_DARK 0xFFEA4335u /**< 深·故障红·填充档（灯/色块等非文本件 vs 面板 3.78 ≥3；两档制与文字档配套）。 */
#define HSF_COLOR_STATE_OFFLINE_DARK    0xFF98A2ACu /**< 深·离线灰（面板 5.72/窗底 6.39/悬停底 4.86/斑马纹底 5.07）。 */
#define HSF_COLOR_TEXT_SECONDARY_DARK   0xFFA9B2BCu /**< 深·次级文本（vs 窗底 7.71/面板 6.91/选中底 5.82；层级令牌，亮色主题无此层）。 */
#define HSF_COLOR_TEXT_SECONDARY        0xFF5A6572u /**< 亮·次级文本（=亮色控件描边同值，vs 白 5.93；页面弱化文案现行字面量归一）。 */

/* ---- 旗舰主题索引（Window/Skin 键的取值空间）----
 * 窗口设置页规格表皮肤占用 0..5（浅色×3+深色×3 参数化表，见
 * HsfWindowPage.c kWinSkins/wn_applySkin）；旗舰双主题占用 0 与 6：
 * index 0 = 旗舰亮色·现代蓝（即规格表 index 0，同一样式表）；index 6 =
 * 旗舰深色·控制室（墨青，hsf_theme_dark_css + HsfTheme.c 定制深色盘）。
 * 窗口页恢复路径只认 0..5、越界夹回 0，故深色恢复由 HsfUi.c 在其后的
 * 覆盖步完成（hsfUiApplyFlagshipThemeOverride）。键名与 HsfWindowPage.c
 * 的 WN_KEY_SKIN 同字面（该文件未导出常量，改名须两处同步——已列
 * 实施记录风险）。 */
#define HSF_THEME_KEY_SKIN   "Window/Skin" /**< 主题索引键（INI 分节 [Window]）。 */
#define HSF_THEME_SKIN_LIGHT 0            /**< 旗舰亮色·现代蓝（缺省）。 */
#define HSF_THEME_SKIN_DARK  6            /**< 旗舰深色·控制室（墨青）。 */

/**
 * @brief 控件套缺省字体（新建控件统一走此口，样式/字库一处调）。
 * @param widget 目标控件；NULL 忽略。
 */
void HsfTheme_applyDefaultFont(XWidget* widget);

/**
 * @brief 当前是否旗舰深色主题（页面自绘面/文字按此取亮/深套令牌）。
 * @details 只反映 HsfTheme_applyFlagshipSkin 的落定结果；窗口页规格表
 *          皮肤（1..5）不经旗舰入口，按缺省亮套处理（与
 *          HsfTheme_currentFlagshipSkin 同口径）。
 */
bool HsfTheme_isDarkSkin(void);

/**
 * @brief 给「弱色说明文本」控件套**主题感知的次级文本色**。
 * @details 各页说明行/副标题/字段标签原先统一走调色板 Mid 角色——Mid
 *          是斜面/凹槽绘制色，旗舰深色盘把它定制成 #2E353D（喂 Fusion
 *          斜面，登记见 HsfTheme.c），落在深窗底 #1B1F24 上 ≈1.1:1，
 *          全部说明小字不可读（2026-11-11 双主题走查实锅，11 页全中）。
 *          本入口按当前主题把控件**自身调色板**的 WindowText 钉成次级
 *          文本令牌（亮 #5A6572 / 深 HSF_COLOR_TEXT_SECONDARY_DARK），
 *          并把前景角色切到 WindowText——每控件一次装配、运行期零开销
 *          （主题切换走全顶层重绘，标签重绘时按自身盘取色即新色）。
 *          对标 HsfHomePage.hsfHomeLabelSetColor 的 per-widget 盘口径。
 * @param widget 目标控件（XLabel 等文本控件）；NULL 忽略。
 */
void HsfTheme_applySecondaryText(XWidget* widget);

/**
 * @brief 主题落定后重刷子树内「随主题装配」的控件（次级文本 + 图表主题）。
 * @details 页面在**旗舰主题落定之前**装配（HsfUi 建页序：先建页 →
 *          hsfUiApplyFlagshipThemeOverride），装配期钉下的两类主题绑定
 *          会持旧值：
 *          ① HsfTheme_applySecondaryText 按当时的亮套钉的说明小字色 ⇒
 *          深色恢复后仍持亮色（#5A6572 落深底 ~2.8:1 偏暗）。按「前景
 *          角色=WindowText 且自身盘 WindowText 等于两套次级令牌之一」
 *          精确识别并重钉（同值早退，稳态零脏区）。
 *          ② XChartView 缺省亮色图表主题（白底/亮轴）在深色页面上成
 *          白块+不可读图例（2026-11-11 走查实锅）。按类名识别视图并把
 *          图表主题切 Dark/Light（页面未显式 setTheme 才是缺省 Light，
 *          显式设过的会被本入口覆盖——当前无此用法）。
 *          其余自身盘钉色（chrome 白/状态语义色）两主题同值或由页面
 *          每拍自刷，不经此路。
 * @param root 子树根（主窗根=全页覆盖）；NULL 忽略。
 */
void HsfTheme_refreshSecondaryTexts(XWidget* root);

/**
 * @brief 安装缺省主题（Fusion 样式 + 主题样式表）。
 * @details 必须在建任何控件**之前**调用（样式表是全局的，控件创建时
 *          取当时的样式）。样式表内容见 HsfThemeCss.h（移植自
 *          XGuiWindowDemo 的缺省主题：亮色·现代蓝）。缺省恒为亮色；
 *          深色主题由 HsfTheme_applyFlagshipSkin 在启动恢复点按设置
 *          覆盖（见 HsfUi.c），不在本入口做（此处尚无 XGuiApplication）。
 */
void HsfTheme_install(void);

/**
 * @brief 应用旗舰主题（亮色 index 0 / 深色·控制室 index 6）。
 * @details 深色=HsfThemeCss.h 的 hsf_theme_dark_css + 定制深色调色板
 *          （XGuiApplication_setPalette 挂盘并置显式标志，冻结系统方案
 *          联动对标 AA_SetPalette）+ XStyleHints_setColorScheme(Dark)；
 *          亮色=setPalette(NULL) 显式重置标准浅色盘（同样冻结联动）+
 *          Light 方案 + 旗舰亮色表。样式表经 XApplication_setStyleSheet_2
 *          安装并全顶层重绘——与窗口页换肤同一即时生效机制（对标 Qt
 *          QApplication::setStyleSheet 的 repolish 可见等价，2026-10-26
 *          换肤不重绘根修口径）。
 * @param   skinIndex 只接受 HSF_THEME_SKIN_LIGHT(0) / HSF_THEME_SKIN_DARK(6)；
 *          其余值（含窗口页规格表 1..5）返回 false 且不做任何事——
 *          规格表皮肤仍归 HsfWindowPage.c 的参数化路径。
 * @return  索引被接受并已应用返回 true。
 * @note    前置条件：HsfTheme_install 已执行（Fusion 样式进程级一次安装，
 *          本入口不重装——避免无谓的样式重置）；XGuiApplication 已创建
 *          （调色板/方案入口挂在其单例上，未创建时本入口内部安全早退）。
 */
bool HsfTheme_applyFlagshipSkin(int skinIndex);

/**
 * @brief 最近一次经旗舰入口落定的主题索引（0 或 6）。
 * @details 只反映 HsfTheme_applyFlagshipSkin 的落定结果；窗口页规格表
 *          皮肤（1..5）不经本入口，本值保持上次旗舰值。供后续窗口页
 *          主题下拉回显使用。
 */
int HsfTheme_currentFlagshipSkin(void);

#ifdef __cplusplus
}
#endif

#endif /* HSFTHEME_H */
