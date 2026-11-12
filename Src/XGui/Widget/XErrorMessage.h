/**
 * @file       XErrorMessage.h
 * @brief      XErrorMessage 错误消息控件（对标 Qt 6.8 QErrorMessage
 *             核心公共 API）。
 * @details    继承 XDialog；showMessage(msg)/showMessage_2(msg, type)
 *             弹出错误消息；setDoneShown(bool) 为 "再次显示" 复选框
 *             等价位；done(result) 收口时按复选框等价位把当前消息/
 *             类型记入抑制表，后续 showMessage 对已抑制消息不再弹出
 *             （对标 Qt 的 doNotShow/doNotShowType 去重链）。
 *             排队语义对齐 Qt 6.8（qerrormessage.cpp）：
 *             - showMessage 先过 isMessageToBeShown 过滤（**空串直接
 *               短路**，不显示也不入队），通过后入 pending 队列；
 *               对话框不可见时立即弹出队首可显示消息；
 *             - 显示中再 showMessage 只入队（横幅文本保持前一条，
 *               实测 Qt 同口径）；
 *             - done() 记抑制表并清 currentMessage/currentType，随后
 *               弹出下一条并保持可见；队列排空才真正收口隐藏。
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
#if XFRAME_ON && XLABEL_ON
#include "XLabel.h" /* P0-E8：横幅文本改由内建 XLabel 承载（AutoText
                     * 富文本子集，与 XMessageBox::m_textLabel 同款）。 */
#endif
#if XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON

XCLASS_DEFINE_BEGING(XErrorMessage)
XCLASS_DEFINE_EXTEND_END(XErrorMessage, XDialog)

typedef struct XErrorMessage
{
    XDialog m_base;        /**< 基类成员；必须是第一个。 */
    XString* m_message;    /**< 横幅显示文本（对象拥有）。对标 QTextEdit
                                的可见文本：done() 排空队列后**保留**最后
                                一条（本机 Qt 6.8.3 实测 done 后 QTextEdit
                                文本仍为最后消息）。 */
#if XFRAME_ON && XLABEL_ON
    XLabel* m_textLabel;   /**< 横幅文本标签（对象拥有，parent=本对话框，
                               随父子链级联释放）。P0-E8：承载显示文本，
                               文本格式 AutoText——`<b>x</b>` 按 XLabel 的
                               富文本子集渲染（对标 Qt 用 QTextEdit::setHtml
                               qerrormessage.cpp:363-367；复用的是同一份
                               XLabel 解析/绘制，不另写解析器）。
                               XLABEL_ON=0 时退化为本控件自绘纯文本。 */
#endif
    XString* m_currentMessage; /**< 当前消息（对象拥有；对标
                                QErrorMessagePrivate::currentMessage）。
                                done() 排空队列时清空——与显示文本分离，
                                因为 Qt 的 currentMessage 在 done 里 clear，
                                而 QTextEdit 文本不清（r2 对齐，账本 226）。 */
    XString* m_currentType;/**< 当前消息的消息类型（对象拥有；对标
                                QErrorMessagePrivate::currentType）。 */
    XStringList* m_doNotShow;     /**< 抑制消息表（对象拥有；不再显示的
                                       消息文本，对标 doNotShow 去重集）。 */
    XStringList* m_doNotShowType; /**< 抑制类型表（对象拥有；不再显示的
                                       消息类型，对标 doNotShowType）。 */
    struct XVector* m_pendingMessages; /**< 待显示消息队列（XString* 元素，
                                    对象拥有；对标 std::queue<Message>）。 */
    struct XVector* m_pendingTypes;    /**< 与 m_pendingMessages 同序的消息
                                    类型队列（XString* 元素，对象拥有）。 */
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
 * @details 与 Qt 6.8 同口径（qerrormessage.cpp:398-405）：
 *          ① 先过 isMessageToBeShown 过滤——**空串（""）直接短路**，
 *             既不显示也不入队；
 *          ② 通过后入待显示队列；对话框当前不可见时立即弹出队首
 *             可显示消息并显示（visible 期再调用只入队，横幅文本
 *             保持前一条）。
 * @param self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param msg 消息文本；按 UTF-8 解码，内部存为对象拥有的 XString；
 *        传入 NULL 时函数不执行任何操作（空串按 Qt 短路，无操作）。
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
/** @brief 关闭收口（对标 QErrorMessage::done，qerrormessage.cpp:292-307）。
 * @details "再次显示" 复选框等价位（isDoneShown）为 false 时，把当前
 *          消息/类型记入抑制表（type 空记消息文本，非空记类型）；
 *          随后**清空 currentMessage/currentType**（Qt 同段 clear），
 *          队列非空则弹出下一条可显示消息并保持可见（不转发
 *          XDialog_done）；队列排空才转发 XDialog_done 隐藏收口。
 * @param self 目标控件指针；NULL 时函数不执行任何操作。
 * @param result 结果码（转发 XDialog_done）。
 * @return 无返回值。
 */
void XErrorMessage_done(XErrorMessage* self, int result);
/** @brief XError消息current消息（当前消息文本；对标 Qt 同名接口）。
 * @details 对应 Qt 的 QErrorMessagePrivate::currentMessage（Qt 无公开
 *          访问器，本库开放为普通函数便于测试）：**显示期间**为当前
 *          横幅消息，done() 收口/弹下一条前清空——与"显示文本"
 *          （done 后保留，见 m_message 注）不同。空串消息按 Qt 短路，
 *          不会成为当前消息。
 * @param self 目标控件指针；可为 NULL。
 * @return 返回借用指针：内部当前消息的 UTF-8 文本，禁止释放或修改；
 *         无当前消息（从未显示或已收口）或无效时返回空串。
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
