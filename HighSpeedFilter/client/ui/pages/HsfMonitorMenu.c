/**
 * @file       HsfMonitorMenu.c
 * @brief      监视子菜单页实现：复用通用菜单列表页装配。
 */
#include "HsfMonitorMenu.h"

#include "HsfMenuPage.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** 监视条目：数字量 / 模拟量 / 变频器。 */
static const HsfMenuEntry kEntries[] = {
    { "数字量", HSF_PAGE_MONITOR_DIGITAL },
    { "模拟量", HSF_PAGE_MONITOR_ANALOG },
    { "变频器", HSF_PAGE_MONITOR_VFD },
    { "电磁阀", HSF_PAGE_MONITOR_VALVE }
};

XWidget* HsfMonitorMenu_create(XWidget* parent, HsfMenuNavFn navFn,
                               void* userData)
{
    /* 返回钮回主菜单页。 */
    return HsfMenuPage_create(parent, "设备",
                              kEntries, (int)(sizeof(kEntries) / sizeof(kEntries[0])),
                              HSF_PAGE_MENU, navFn, userData);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
