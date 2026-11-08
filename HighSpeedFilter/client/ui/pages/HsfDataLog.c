/**
 * @file       HsfDataLog.c
 * @brief      日志叶子页实现：XTableWidget 铺满、随窗口拉伸。
 */
#include "HsfDataLog.h"

#include "HsfDataTable.h"
#include "HsfUi.h"   /* hsfUi_pageFontPct（本地设置·按页字号） */

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** 日志列头（静态存活：表格直存指针）。 */
static const char* const kLogHeaders[] = {
    "时间",   /* 时间 */
    "级别",   /* 级别 */
    "事件",   /* 事件 */
    "操作"    /* 操作 */
};

/** @brief 字号实时重放：转调通用表格页的字号接口。 */
static void logApplyFonts(XWidget* page, int pct)
{
    HsfDataTablePage_applyFont(page, pct);
}

XWidget* HsfDataLog_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData)
{
    (void)navFn;
    (void)userData;
    /* 日志表格（20 空行占位，日志流接入后续替换实现）。
     * 字号按本地设置 FontScale/Page10 折算（标题 + 表格）。 */
    {
        XWidget* page = HsfDataTablePage_create(parent,
                                "日志",
                                kLogHeaders, 4, 20); /* 日志 */
        if (page) {
            HsfDataTablePage_applyFont(
                page, hsfUi_pageFontPx(HSF_PAGE_DATA_LOG, 100));
            hsfUi_registerPageFontApplier(HSF_PAGE_DATA_LOG, page,
                                          logApplyFonts);
        }
        return page;
    }
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
