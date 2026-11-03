/**
 * @file       HsfDataLog.c
 * @brief      日志叶子页实现：XTableWidget 铺满、随窗口拉伸。
 */
#include "HsfDataLog.h"

#include "HsfDataTable.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** 日志列头（静态存活：表格直存指针）。 */
static const char* const kLogHeaders[] = {
    "\xE6\x97\xB6\xE9\x97\xB4",   /* 时间 */
    "\xE7\xBA\xA7\xE5\x88\xAB",   /* 级别 */
    "\xE4\xBA\x8B\xE4\xBB\xB6",   /* 事件 */
    "\xE6\x93\x8D\xE4\xBD\x9C"    /* 操作 */
};

XWidget* HsfDataLog_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData)
{
    (void)navFn;
    (void)userData;
    /* 日志表格（20 空行占位，日志流接入后续替换实现）。 */
    return HsfDataTablePage_create(parent,
                            "\xE6\x97\xA5\xE5\xBF\x97",
                            kLogHeaders, 4, 20); /* 日志 */
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
