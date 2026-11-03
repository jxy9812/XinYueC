/**
 * @file       HsfDataAlarm.c
 * @brief      报警叶子页实现：XTableWidget 铺满、随窗口拉伸。
 */
#include "HsfDataAlarm.h"

#include "HsfDataTable.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** 报警列头（静态存活：表格直存指针）。 */
static const char* const kAlarmHeaders[] = {
    "\xE6\x97\xB6\xE9\x97\xB4",   /* 时间 */
    "\xE7\xB1\xBB\xE5\x9E\x8B",   /* 类型 */
    "\xE6\x8F\x8F\xE8\xBF\xB0",   /* 描述 */
    "\xE7\x8A\xB6\xE6\x80\x81"    /* 状态 */
};

XWidget* HsfDataAlarm_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData)
{
    (void)navFn;
    (void)userData;
    /* 报警表格（15 空行占位，报警规则与记录接入后续替换实现）。 */
    return HsfDataTablePage_create(parent,
                            "\xE6\x8A\xA5\xE8\xAD\xA6",
                            kAlarmHeaders, 4, 15); /* 报警 */
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
