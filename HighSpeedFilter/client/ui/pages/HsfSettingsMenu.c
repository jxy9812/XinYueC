/**
 * @file       HsfSettingsMenu.c
 * @brief      设置子菜单页实现：复用通用菜单列表页装配。
 */
#include "HsfSettingsMenu.h"

#include "HsfMenuPage.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** 设置条目：本地设置 / 远端设置 / 网络设置。
 *  [2026-11-13 三页合并] 原「悬浮窗设置」「窗口设置」两个独立条目删掉：
 *  两页内容已并入「本地设置」页（页号 13）的「悬浮窗」「窗口与屏幕」
 *  两个分区（页号 16/17 作废）。 */
static const HsfMenuEntry kEntries[] = {
    { "本地设置",   HSF_PAGE_SETTINGS_LOCAL },
    { "远端设置",   HSF_PAGE_SETTINGS_REMOTE },
    { "网络设置",   HSF_PAGE_SETTINGS_NETWORK }
};

XWidget* HsfSettingsMenu_create(XWidget* parent, HsfMenuNavFn navFn,
                                void* userData)
{
    /* 返回钮回主菜单页。 */
    return HsfMenuPage_create(parent, "设置",
                              kEntries, (int)(sizeof(kEntries) / sizeof(kEntries[0])),
                              HSF_PAGE_MENU, navFn, userData);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
