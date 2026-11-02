/**
 * @file       HsfDataLog.c
 * @brief      日志叶子页实现：复用 HsfPlaceHolder 占位承载。
 */
#include "HsfDataLog.h"

#include "HsfPlaceHolder.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

XWidget* HsfDataLog_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData)
{
    /* TODO(日志): 占位骨架，真实页面内容在此替换实现。 */
    return HsfPlaceHolder_create(parent,
                            "\xE6\x97\xA5\xE5\xBF\x97", "\xE8\xBF\x90\xE8\xA1\x8C\xE6\x97\xA5\xE5\xBF\x97\xEF\xBC\x9A\xE4\xBA\x8B\xE4\xBB\xB6\xE3\x80\x81\xE5\x91\x8A\xE8\xAD\xA6\xE4\xB8\x8E\xE6\x93\x8D\xE4\xBD\x9C\xE8\xAE\xB0\xE5\xBD\x95\xE6\xA3\x80\xE7\xB4\xA2", "\xE5\xBE\x85\xE6\x8E\xA5\xE5\x85\xA5\xEF\xBC\x9A\xE6\x97\xA5\xE5\xBF\x97\xE6\xB5\x81",
                            HSF_PAGE_DATA_MENU, navFn, userData);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
