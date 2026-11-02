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

/* ---- 品牌配色（ARGB；改主题只动这里） ---- */
#define HSF_COLOR_BRAND     0xff1f4e79u /**< 顶栏深蓝底（同 XGuiDemo 品牌条）。 */
#define HSF_COLOR_STATUS_BG 0xff000000u /**< 底部状态栏黑底。 */

/**
 * @brief 控件套缺省字体（新建控件统一走此口，样式/字库一处调）。
 * @param widget 目标控件；NULL 忽略。
 */
void HsfTheme_applyDefaultFont(XWidget* widget);

/**
 * @brief 安装缺省主题（Fusion 样式 + 主题样式表）。
 * @details 必须在建任何控件**之前**调用（样式表是全局的，控件创建时
 *          取当时的样式）。样式表内容见 HsfThemeCss.h（移植自
 *          XGuiWindowDemo 的缺省主题：亮色·现代蓝）。
 */
void HsfTheme_install(void);

#ifdef __cplusplus
}
#endif

#endif /* HSFTHEME_H */
