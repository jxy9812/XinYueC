/**
 * @file       XMessageBox.h
 * @brief      XMessageBox 消息对话框控件（对标 Qt 6.8 QMessageBox 核心
 *             公共 API）。
 * @details    功能范围：
 *             - Icon 枚举（NoIcon/Information/Warning/Critical/Question，
 *               数值对齐）；StandardButton 位标志（与 XDialogButtonBox
 *               的枚举复用，位值逐项对齐 Qt）；
 *             - setText/title/icon、setStandardButtons/standardButtons、
 *               addButton(button, role)/addButton(text, role)/
 *               addButton(standard)、button(standard)、clickedButton；
 *             - exec()：模态事件循环（processEvents 驱动，对标 QDialog
 *               的模态语义）；
 *             - 静态便捷方法：information/warning/critical/question/
 *               about（阻塞直至用户选择，返回被点击的标准按钮）。
 *             与 Qt 的差异：XMessageBox 继承 XWidget（Qt 为 QDialog），
 *             因库内对话框基础设施尚在建设中；API 形状保持一致。
 * @note       模块总开关 XMESSAGEBOX_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XDIALOGBUTTONBOX_ON、
 *             XPUSHBUTTON_ON、XLABEL_ON。
 * @author     XinYueC 团队
 */
#ifndef XMESSAGEBOX_H
#define XMESSAGEBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#if XDIALOG_ON
#include "XDialog.h"
#endif
#if XDIALOGBUTTONBOX_ON
#include "XDialogButtonBox.h"
#endif
#if XLABEL_ON
#include "XLabel.h"
#endif

#if XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON

/** @brief 消息图标（对标 QMessageBox::Icon，数值一致）。 */
typedef enum XMessageBoxIcon
{
    XMessageBoxIcon_NoIcon = 0,
    XMessageBoxIcon_Information = 1,
    XMessageBoxIcon_Warning = 2,
    XMessageBoxIcon_Critical = 3,
    XMessageBoxIcon_Question = 4
} XMessageBoxIcon;

/** @brief 按钮角色（对标 QMessageBox::ButtonRole，与 XDialogButtonBox
 *         Role 数值一致）。 */
typedef enum XMessageBoxButtonRole
{
    XMessageBoxButtonRole_InvalidRole = -1,
    XMessageBoxButtonRole_AcceptRole = 0,
    XMessageBoxButtonRole_RejectRole = 1,
    XMessageBoxButtonRole_DestructiveRole = 2,
    XMessageBoxButtonRole_ActionRole = 3,
    XMessageBoxButtonRole_HelpRole = 4,
    XMessageBoxButtonRole_YesRole = 5,
    XMessageBoxButtonRole_NoRole = 6,
    XMessageBoxButtonRole_ResetRole = 7,
    XMessageBoxButtonRole_ApplyRole = 8,
    XMessageBoxButtonRole_NRoles = 9
} XMessageBoxButtonRole;

XCLASS_DEFINE_BEGING(XMessageBox)
XCLASS_DEFINE_EXTEND_END(XMessageBox, XDialog)

typedef struct XMessageBox
{
    XWidget m_base;              /**< 基类成员；必须是第一个。 */
    XLabel* m_textLabel;         /**< 消息文本标签（拥有）。 */
#if XDIALOGBUTTONBOX_ON
    XDialogButtonBox* m_buttonBox; /**< 按钮盒（拥有）。 */
    XVector* m_standards;        /**< 与成员顺序对应的标准按钮值。 */
#endif
    XString* m_title;           /**< 窗口标题（对象拥有）。 */
    XString* m_text;            /**< 消息文本缓存（对象拥有；对标 text）。 */
    XString* m_detailedText;    /**< 详细文本（对象拥有；对标 detailedText）。 */
    XString* m_informativeText; /**< 补充文本（对象拥有；对标 informativeText）。 */
    int m_icon;                  /**< 图标（XMessageBoxIcon）。 */
    int m_options;               /**< 选项位集（对标 options）。 */
    XAbstractButton* m_clicked;  /**< 最近点击的按钮（exec 结果）。 */
    XAbstractButton* m_defaultButton; /**< 默认按钮（借用；对标 defaultButton）。 */
    XAbstractButton* m_escapeButton;  /**< 转义按钮（借用；对标 escapeButton）。 */
    bool m_inExec;               /**< exec 循环进行中。 */
} XMessageBox;

/** @brief X消息盒classinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XMessageBox_class_init(void);
void XMessageBox_init(XMessageBox* self, XWidget* parent,
                      XWidgetFlags flags);
#define XMessageBox_create(parent, flags) XMessageBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XMessageBox* XMessageBox_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XMessageBox_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XMessageBox_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 文本与图标 ==================== */

void XMessageBox_setText(XMessageBox* self, const char* utf8);
/** @brief X消息盒text（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XMessageBox_text(const XMessageBox* self);
/** @brief X消息盒set标题（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param utf8 UTF-8 文本。
 * @return 无返回值。
 */
void XMessageBox_setTitle(XMessageBox* self, const char* utf8);
/** @brief X消息盒title（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XMessageBox_title(const XMessageBox* self);
/** @brief X消息盒set图标（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param icon 图标路径（UTF-8）。
 * @return 无返回值。
 */
void XMessageBox_setIcon(XMessageBox* self, XMessageBoxIcon icon);
/** @brief X消息盒icon（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应值。
 */
XMessageBoxIcon XMessageBox_icon(const XMessageBox* self);

/* ==================== 按钮管理 ==================== */

/**
 * @brief      设置标准按钮组。
 */
void XMessageBox_setStandardButtons(XMessageBox* self, int buttons);
/**
 * @brief      获取标准按钮组。
 */
int XMessageBox_standardButtons(const XMessageBox* self);
/**
 * @brief      获取标准按钮。
 */
XAbstractButton* XMessageBox_button(const XMessageBox* self,
                                    XDialogButtonBoxStandardButton which);
/**
 * @brief      获取被点击按钮。
 */
XAbstractButton* XMessageBox_clickedButton(const XMessageBox* self);

/* ==================== 模态执行 ==================== */

/**
 * @brief      启动模态对话框事件循环（对标 QDialog::exec）。
 */
XDialogButtonBoxStandardButton XMessageBox_exec(XMessageBox* self);

/* ==================== 静态便捷方法 ==================== */

/**
 * @brief      信息对话框（静态）。
 */
XDialogButtonBoxStandardButton XMessageBox_information(
    XWidget* parent, const char* title, const char* text, int buttons);
/**
 * @brief      警告对话框（静态）。
 */
XDialogButtonBoxStandardButton XMessageBox_warning(
    XWidget* parent, const char* title, const char* text, int buttons);
/**
 * @brief      严重错误对话框（静态）。
 */
XDialogButtonBoxStandardButton XMessageBox_critical(
    XWidget* parent, const char* title, const char* text, int buttons);
/**
 * @brief      询问对话框（静态）。
 */
XDialogButtonBoxStandardButton XMessageBox_question(
    XWidget* parent, const char* title, const char* text, int buttons);
/**
 * @brief      关于对话框（静态）。
 */
void XMessageBox_about(XWidget* parent, const char* title,
                       const char* text);

/* ==================== 补充文本（对标 QMessageBox） ==================== */

/** @brief 设置详细文本。
 * @param self 目标对话框。
 * @param utf8 UTF-8 文本；可为 NULL（清空）。
 * @return 无返回值。
 */
void XMessageBox_setDetailedText(XMessageBox* self, const char* utf8);
/** @brief 读取详细文本。 @param self 目标对话框。 @return UTF-8 文本。 */
const char* XMessageBox_detailedText(const XMessageBox* self);
/** @brief 设置补充文本。
 * @param self 目标对话框。
 * @param utf8 UTF-8 文本；可为 NULL（清空）。
 * @return 无返回值。
 */
void XMessageBox_setInformativeText(XMessageBox* self, const char* utf8);
/** @brief 读取补充文本。 @param self 目标对话框。 @return UTF-8 文本。 */
const char* XMessageBox_informativeText(const XMessageBox* self);

/* ==================== 按钮管理（对标 QMessageBox） ==================== */

/** @brief 追加自定义按钮（按钮+角色；对标 addButton(QAbstractButton*, role)）。
 * @param self 目标对话框。
 * @param button 按钮借用指针；不能为 NULL。
 * @param role 按钮角色（XMessageBoxButtonRole）。
 * @return 无返回值。
 */
void XMessageBox_addButton(XMessageBox* self, XAbstractButton* button,
                           int role);
/** @brief 追加文本按钮（对标 addButton(QString, role)）。
 * @param self 目标对话框。
 * @param text UTF-8 按钮文本；不能为 NULL。
 * @param role 按钮角色。
 * @return 新建按钮借用指针；失败 NULL。
 */
XAbstractButton* XMessageBox_addButton_2(XMessageBox* self,
                                         const char* text, int role);
/** @brief 追加标准按钮（对标 addButton(StandardButton)）。
 * @param self 目标对话框。
 * @param button 标准按钮位（XDialogButtonBoxStandardButton 单值）。
 * @return 新建/既有按钮借用指针；失败 NULL。
 */
XAbstractButton* XMessageBox_addButton_3(XMessageBox* self, int button);
/** @brief 设置默认按钮（按钮对象；对标 setDefaultButton(QPushButton*)）。
 * @param self 目标对话框。
 * @param button 按钮借用指针；可为 NULL。
 * @return 无返回值。
 */
void XMessageBox_setDefaultButton(XMessageBox* self,
                                  XAbstractButton* button);
/** @brief 设置默认按钮（标准按钮位；对标 setDefaultButton(StandardButton)）。
 * @param self 目标对话框。
 * @param button 标准按钮位。
 * @return 无返回值。
 */
void XMessageBox_setDefaultButton_2(XMessageBox* self, int button);
/** @brief 查询默认按钮。 @param self 目标对话框。 @return 借用指针；无返回 NULL。 */
XAbstractButton* XMessageBox_defaultButton(const XMessageBox* self);
/** @brief 设置转义按钮（按钮对象；对标 setEscapeButton(QAbstractButton*)）。
 * @param self 目标对话框。
 * @param button 按钮借用指针；可为 NULL。
 * @return 无返回值。
 */
void XMessageBox_setEscapeButton(XMessageBox* self, XAbstractButton* button);
/** @brief 设置转义按钮（标准按钮位；对标 setEscapeButton(StandardButton)）。
 * @param self 目标对话框。
 * @param button 标准按钮位。
 * @return 无返回值。
 */
void XMessageBox_setEscapeButton_2(XMessageBox* self, int button);
/** @brief 查询转义按钮。 @param self 目标对话框。 @return 借用指针；无返回 NULL。 */
XAbstractButton* XMessageBox_escapeButton(const XMessageBox* self);
/** @brief 全部按钮（返回新建 XVector<XAbstractButton*> 借用副本，调用方 delete）。
 * @param self 目标对话框。
 * @return 新建 XVector；无按钮返回空列表。
 */
XVector* XMessageBox_buttons(const XMessageBox* self);
/** @brief 查询按钮对应的标准值（对标 QMessageBox::standardButton(button)）。
 * @param self 目标对话框。
 * @param button 按钮借用指针。
 * @return 标准按钮位；非标准按钮返回 NoButton。
 */
int XMessageBox_standardButton(const XMessageBox* self,
                               XAbstractButton* button);
/** @brief 设置选项位集（对标 setOptions）。
 * @param self 目标对话框。
 * @param options 选项位组合。
 * @return 无返回值。
 */
void XMessageBox_setOptions(XMessageBox* self, int options);
/** @brief 查询选项位集。 @param self 目标对话框。 @return 位组合。 */
int XMessageBox_options(const XMessageBox* self);
/** @brief 测试选项位（对标 testOption）。
 * @param self 目标对话框。
 * @param option 选项位。
 * @return 置位返回 true。
 */
bool XMessageBox_testOption(const XMessageBox* self, int option);

/** @brief buttonClicked(XAbstractButton*) 信号（对标 QMessageBox::buttonClicked；
 *         载荷：被点击按钮）。 */
void* XMessageBox_buttonClicked_signal(XMessageBox* self,
                                       XAbstractButton* button);

#endif /* XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON */

#endif /* XMESSAGEBOX_H */