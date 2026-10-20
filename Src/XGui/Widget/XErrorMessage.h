/**
 * @file       XErrorMessage.h
 * @brief      XErrorMessage 错误消息控件（对标 Qt 6.8 QErrorMessage
 *             核心公共 API）。
 * @details    继承 XDialog；showMessage(msg)/showMessage_2(msg, type)
 *             弹出错误消息；setDoneShown(bool) 为 "再次显示" 复选框
 *             等价位；done(result) 收口时按复选框等价位把当前消息/
 *             类型记入抑制表，后续 showMessage 对已抑制消息不再弹出
 *             （对标 Qt 的 doNotShow/doNotShowType 去重链）。
 * @note       模块总开关 XERRORMESSAGE_ON 定义于 XGuiConfig.h。
 * @note       Qt 的静态 qtHandler()（qerrormessage.h:25）依赖
 *             qInstallMessageHandler 全局消息处理器安装设施，框架无
 *             对应物，未提供（依赖缺口记录）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XERRORMESSAGE_H
#define XERRORMESSAGE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XDialog.h"
#include "XStringList.h"
#if XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON

XCLASS_DEFINE_BEGING(XErrorMessage)
XCLASS_DEFINE_EXTEND_END(XErrorMessage, XDialog)

typedef struct XErrorMessage
{
    XDialog m_base;        /**< 基类成员；必须是第一个。 */
    XString* m_message;    /**< 当前消息（对象拥有）。 */
    XString* m_currentType;/**< 当前消息的消息类型（对象拥有；对标
                                QErrorMessagePrivate::currentType）。 */
    XStringList* m_doNotShow;     /**< 抑制消息表（对象拥有；不再显示的
                                       消息文本，对标 doNotShow 去重集）。 */
    XStringList* m_doNotShowType; /**< 抑制类型表（对象拥有；不再显示的
                                       消息类型，对标 doNotShowType）。 */
    bool m_doneShown;      /**< "再次显示" 复选框等价位（默认 true=勾选，
                                收口时不抑制；false=收口时把当前消息/
                                类型记入抑制表，对标 againCheckBox）。 */
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

/** @brief XError消息show消息（弹出错误消息；对标 Qt 同名接口）。
 * @param self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param msg 消息文本；按 UTF-8 解码，内部存为对象拥有的 XString；
 *        传入 NULL 时函数不执行任何操作。
 * @return 无返回值。
 */
void XErrorMessage_showMessage(XErrorMessage* self, const char* msg);
/** @brief 弹出带消息类型的错误消息（对标 showMessage(message, type)
 *         双参重载，qerrormessage.cpp:389-402）。
 * @details 已被抑制的消息（done 收口时按复选框等价位记入去重表，
 *          isMessageToBeShown 过滤，qerrormessage.cpp:376-381）不再
 *          弹出：type 非空按「消息类型」维度去重（同类型全部不再
 *          显示），type 空按「消息文本」维度去重。
 * @param self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param msg 消息文本（UTF-8）；NULL 不执行任何操作。
 * @param type 消息类型（UTF-8）；NULL 按空类型处理。
 * @return 无返回值。
 */
void XErrorMessage_showMessage_2(XErrorMessage* self, const char* msg,
                                 const char* type);
/** @brief 关闭收口（对标 QErrorMessage::done，qerrormessage.cpp:283-306）。
 * @details "再次显示" 复选框等价位（isDoneShown）为 false 时，把当前
 *          消息/类型记入抑制表（type 空记消息文本，非空记类型），
 *          随后的 showMessage/showMessage_2 对已记入消息不再弹出；
 *          为 true（默认）时不记入。C 无虚函数，沿用 XWizard_done 的
 *          同名漏斗惯例：抑制记忆经本函数收口。
 * @param self 目标控件指针；NULL 时函数不执行任何操作。
 * @param result 结果码（转发 XDialog_done）。
 * @return 无返回值。
 */
void XErrorMessage_done(XErrorMessage* self, int result);
/** @brief XError消息current消息（当前消息文本；对标 Qt 同名接口）。
 * @param self 目标控件指针；可为 NULL。
 * @return 返回借用指针：内部消息的 UTF-8 文本，禁止释放或修改；
 *         无消息或无效时返回空串。
 */
const char* XErrorMessage_currentMessage(const XErrorMessage* self);
/** @brief XError消息setDoneShown（对标 Qt 同名接口；语义为 done 收口
 *         时是否记抑制表：true=复选框勾选等价位、不记（默认），
 *         false=收口时把当前消息/类型记入"不再显示"表）。 */
void XErrorMessage_setDoneShown(XErrorMessage* self, bool on);
/** @brief XError消息isDoneShown（对标 Qt 同名接口；true=不记抑制表）。 */
bool XErrorMessage_isDoneShown(const XErrorMessage* self);

#endif /* XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XERRORMESSAGE_H */
