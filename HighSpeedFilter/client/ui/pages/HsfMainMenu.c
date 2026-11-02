/**
 * @file       HsfMainMenu.c
 * @brief      主菜单页实现：复用通用菜单列表页（HsfMenuPage）装配。
 */
#include "HsfMainMenu.h"

#include "HsfMenuPage.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** 一级条目：主界面 / 监视 / 数据 / 设置。 */
static const HsfMenuEntry kEntries[] = {
    { "主界面", HSF_PAGE_HOME },
    { "监视",   HSF_PAGE_MONITOR_MENU },
    { "数据",   HSF_PAGE_DATA_MENU },
    { "设置",   HSF_PAGE_SETTINGS_MENU }
};

XWidget* HsfMainMenu_create(XWidget* parent, HsfMenuNavFn navFn,
                            void* userData)
{
    /* 一级菜单无「返回」钮（backIndex=-1）。 */
    return HsfMenuPage_create(parent, "导航菜单",
                              kEntries, (int)(sizeof(kEntries) / sizeof(kEntries[0])),
                              -1, navFn, userData);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
