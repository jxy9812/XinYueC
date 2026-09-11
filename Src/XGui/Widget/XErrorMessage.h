/**
 * @file       XErrorMessage.h
 * @brief      XErrorMessage 错误消息控件（对标 Qt 6.8 QErrorMessage
 *             核心公共 API）。
 * @details    继承 XDialog；showMessage(msg) 弹出错误消息；
 *             setDoneShown(bool) 控制 "再次显示" 复选框。
 * @note       模块总开关 XERRORMESSAGE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XERRORMESSAGE_H
#define XERRORMESSAGE_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XDialog.h"
#if XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON

XCLASS_DEFINE_BEGING(XErrorMessage)
XCLASS_DEFINE_EXTEND_END(XErrorMessage, XDialog)

typedef struct XErrorMessage
{
    XDialog m_base;      /**< 基类成员；必须是第一个。 */
    char m_message[512]; /**< 当前消息。 */
    bool m_doneShown;    /**< 是否显示 "不再显示" 复选框。 */
} XErrorMessage;

XVtable* XErrorMessage_class_init(void);
void XErrorMessage_init(XErrorMessage* self, XWidget* parent, XWidgetFlags flags);
#define XErrorMessage_create(parent, flags) XErrorMessage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XErrorMessage* XErrorMessage_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XErrorMessage_delete_base(self) XDialog_delete_base((XDialog*)(self))

void XErrorMessage_showMessage(XErrorMessage* self, const char* msg);
const char* XErrorMessage_currentMessage(const XErrorMessage* self);
void XErrorMessage_setDoneShown(XErrorMessage* self, bool on);
bool XErrorMessage_isDoneShown(const XErrorMessage* self);

#endif /* XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON */

#ifdef __cplusplus
}
#endif
void XErrorMessage_done(XErrorMessage* self, int result);
#endif /* XERRORMESSAGE_H */