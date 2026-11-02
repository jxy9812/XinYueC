/**
 * @file       HsfDataMenu.c
 * @brief      数据子菜单页实现：复用通用菜单列表页装配。
 */
#include "HsfDataMenu.h"

#include "HsfMenuPage.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** 数据条目：历史数据 / 日志 / 报警。 */
static const HsfMenuEntry kEntries[] = {
    { "历史数据", HSF_PAGE_DATA_HISTORY },
    { "日志",     HSF_PAGE_DATA_LOG },
    { "报警",     HSF_PAGE_DATA_ALARM }
};

XWidget* HsfDataMenu_create(XWidget* parent, HsfMenuNavFn navFn,
                            void* userData)
{
    /* 返回钮回主菜单页。 */
    return HsfMenuPage_create(parent, "数据",
                              kEntries, (int)(sizeof(kEntries) / sizeof(kEntries[0])),
                              HSF_PAGE_MENU, navFn, userData);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
