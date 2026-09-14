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
    XString* m_message; /**< 当前消息（对象拥有）。 */
    bool m_doneShown;    /**< 是否显示 "不再显示" 复选框。 */
} XErrorMessage;

/** @brief XError消息classinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XErrorMessage_class_init(void);
/** @brief XError消息init（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XErrorMessage_init(XErrorMessage* self, XWidget* parent, XWidgetFlags flags);
#define XErrorMessage_create(parent, flags) XErrorMessage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief XError消息createex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XErrorMessage* XErrorMessage_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XErrorMessage_delete_base(self) XDialog_delete_base((XDialog*)(self))

/** @brief XError消息show消息（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param msg 消息文本。
 * @return 无返回值。
 */
void XErrorMessage_showMessage(XErrorMessage* self, const char* msg);
/** @brief XError消息current消息（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XErrorMessage_currentMessage(const XErrorMessage* self);
/** @brief XError消息setDoneShown（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param on bool：true 开启。
 * @return 无返回值。
 */
void XErrorMessage_setDoneShown(XErrorMessage* self, bool on);
/** @brief XError消息isDoneShown（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XErrorMessage_isDoneShown(const XErrorMessage* self);

#endif /* XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON */

#ifdef __cplusplus
}
#endif
/** @brief XError消息done（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param result int 参数。
 * @return 无返回值。
 */
void XErrorMessage_done(XErrorMessage* self, int result);
/** @brief XError消息setDoneShown2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XErrorMessage_setDoneShown_2(XErrorMessage* self);
/** @brief XError消息isDoneShown2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XErrorMessage_isDoneShown_2(XErrorMessage* self);
/** @brief XError消息show消息2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XErrorMessage_showMessage_2(XErrorMessage* self);
/** @brief XError消息current消息2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XErrorMessage_currentMessage_2(XErrorMessage* self);
#endif /* XERRORMESSAGE_H */