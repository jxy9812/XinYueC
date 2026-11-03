/**
 * @file       HsfDataHistory.c
 * @brief      历史数据叶子页实现：XTableWidget 铺满、随窗口拉伸。
 */
#include "HsfDataHistory.h"

#include "HsfDataTable.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** 历史数据列头（静态存活：表格直存指针）。 */
static const char* const kHistoryHeaders[] = {
    "\xE6\x97\xB6\xE9\x97\xB4",   /* 时间 */
    "\xE9\x87\x87\xE6\xA0\xB7\xE5\x80\xBC", /* 采样值 */
    "\xE6\xBB\xA4\xE6\xB3\xA2\xE5\x90\x8E"  /* 滤波后 */
};

XWidget* HsfDataHistory_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData)
{
    (void)navFn;
    (void)userData;
    /* 历史数据表格（12 空行占位，数据接入后续替换实现）。 */
    return HsfDataTablePage_create(parent,
                            "\xE5\x8E\x86\xE5\x8F\xB2\xE6\x95\xB0\xE6\x8D\xAE",
                            kHistoryHeaders, 3, 12); /* 历史数据 */
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
