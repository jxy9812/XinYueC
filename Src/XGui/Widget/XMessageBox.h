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

/** @brief XImage 前向声明（iconPixmap 承载自定义图标位图）。 */
typedef struct XImage XImage;
/** @brief XIcon 前向声明（standardIcon 返回标准图标）。 */
typedef struct XIcon XIcon;
#if XCHECKBOX_ON
/** @brief XCheckBox 前向声明（checkBox 消息框复选框）。 */
typedef struct XCheckBox XCheckBox;
#endif

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
    XDialog m_base;              /**< 基类成员；必须是第一个；XMessageBox 继承
                                  *   XDialog（与 class_init 的 INHERIT_XCLASS、
                                  *   init/deinit 的父类调用保持一致）。此前误写为
                                  *   XWidget，x86 下 XDialog 尾部的 m_result/
                                  *   m_modal/m_inExec/m_sizeGripEnabled 八字节会
                                  *   按别名覆盖 m_textLabel 与 m_buttonBox 低
                                  *   字节，accept/done 后按钮盒变野指针。 */
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
#if XCHECKBOX_ON
    XCheckBox* m_checkBox;       /**< 复选框（对象拥有；NULL 表示未设置；对标 checkBox）。 */
#endif
    XImage* m_iconPixmap;        /**< 自定义图标位图（对象拥有；对标 iconPixmap；暂不参与绘制）。 */
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
/** @brief 设置/清除单个选项位（对标 QMessageBox::setOption）。
 * @param self 目标对话框；传入 NULL 时函数不执行任何操作。
 * @param option 选项位。
 * @param on true 置位该选项，false 清除该选项；其余选项位保持不变。
 * @return 无返回值。
 */
void XMessageBox_setOption(XMessageBox* self, int option, bool on);

/** @brief buttonClicked(XAbstractButton*) 信号（对标 QMessageBox::buttonClicked；
 *         载荷：被点击按钮）。 */
void* XMessageBox_buttonClicked_signal(XMessageBox* self,
                                       XAbstractButton* button);

/* ==================== 复选框 / 图标位图 / 文本呈现（对标 QCheckBox、
 *                     iconPixmap、textFormat、textInteractionFlags） ==================== */

#if XCHECKBOX_ON
/**
 * @brief      设置消息框复选框（对标 QMessageBox::setCheckBox）。
 * @details    复选框所有权转移给消息框：旧复选框被释放；复选框显示在
 *             消息文本与按钮盒之间。传入 NULL 仅清除并释放当前复选框。
 * @param      self 目标对话框；传入 NULL 时函数不执行任何操作。
 * @param      checkBox 新复选框；可为 NULL；可带任意父控件，设置后归
 *             消息框管理（由消息框释放，调用方不得重复释放）。
 * @return     无返回值。
 */
void XMessageBox_setCheckBox(XMessageBox* self, XCheckBox* checkBox);
/**
 * @brief      查询消息框复选框（对标 QMessageBox::checkBox）。
 * @param      self 目标对话框；NULL 返回 NULL。
 * @return     借用指针，属于消息框内部存储，不能释放；未设置返回 NULL。
 */
XCheckBox* XMessageBox_checkBox(const XMessageBox* self);
#endif /* XCHECKBOX_ON */

/**
 * @brief      设置自定义图标位图（对标 QMessageBox::setIconPixmap）。
 * @details    深拷贝存储；当前版本仅保存状态不参与绘制（绘制仍按
 *             icon 枚举交给样式层），头文件与文档已注明。
 * @param      self 目标对话框；传入 NULL 时函数不执行任何操作。
 * @param      pixmap 位图源；NULL 清除当前位图；只借用，内部深拷贝。
 * @return     无返回值。
 */
void XMessageBox_setIconPixmap(XMessageBox* self, const XImage* pixmap);
/**
 * @brief      查询自定义图标位图（对标 QMessageBox::iconPixmap）。
 * @param      self 目标对话框；NULL 返回 NULL。
 * @return     借用指针，属于对象内部存储，不能释放；未设置返回 NULL。
 */
const XImage* XMessageBox_iconPixmap(const XMessageBox* self);

/**
 * @brief      设置消息文本呈现格式（对标 QMessageBox::setTextFormat）。
 * @details    转发给内部文本标签（XLabel_setTextFormat）；格式数值与
 *             Qt::TextFormat 一致（PlainText/RichText/AutoText）。
 * @param      self 目标对话框；传入 NULL 或标签被裁剪时函数不执行任何操作。
 * @param      format 文本格式（XLabelTextFormat）。
 * @return     无返回值。
 */
void XMessageBox_setTextFormat(XMessageBox* self, XLabelTextFormat format);
/**
 * @brief      查询消息文本呈现格式（对标 QMessageBox::textFormat）。
 * @param      self 目标对话框；NULL 或无标签返回 AutoText（标签默认）。
 * @return     当前文本格式。
 */
XLabelTextFormat XMessageBox_textFormat(const XMessageBox* self);
/**
 * @brief      设置文本交互标志（对标 QMessageBox::setTextInteractionFlags）。
 * @details    转发给内部文本标签；标志位值与 Qt::TextInteractionFlag 一致。
 * @param      self 目标对话框；传入 NULL 或标签被裁剪时函数不执行任何操作。
 * @param      flags 交互标志位集。
 * @return     无返回值。
 */
void XMessageBox_setTextInteractionFlags(XMessageBox* self,
                                         XLabelTextInteractionFlags flags);
/**
 * @brief      查询文本交互标志（对标 QMessageBox::textInteractionFlags）。
 * @param      self 目标对话框；NULL 或无标签返回 0。
 * @return     当前交互标志位集。
 */
XLabelTextInteractionFlags XMessageBox_textInteractionFlags(
    const XMessageBox* self);

/* ==================== 按钮角色 / 移除 / 文本（对标 buttonRole、
 *                     removeButton、buttonText、setButtonText） ==================== */

/**
 * @brief      查询按钮角色（对标 QMessageBox::buttonRole）。
 * @param      self 目标对话框；传入 NULL 时返回 InvalidRole。
 * @param      button 目标按钮；NULL 或不属于本框时返回 InvalidRole。
 * @return     按钮角色（XMessageBoxButtonRole；数值与 XDialogButtonBox
 *             Role 一致）。
 */
XMessageBoxButtonRole XMessageBox_buttonRole(const XMessageBox* self,
                                             XAbstractButton* button);
/**
 * @brief      从按钮盒移除按钮（对标 QMessageBox::removeButton）。
 * @details    只从按钮盒摘除，不释放按钮对象；默认/转义/最近点击指针
 *             若指向该按钮则一并清空。
 * @param      self 目标对话框；传入 NULL 时函数不执行任何操作。
 * @param      button 待移除按钮；NULL 或不属于本框时保持原状。
 * @return     无返回值。
 */
void XMessageBox_removeButton(XMessageBox* self, XAbstractButton* button);
/**
 * @brief      查询标准按钮文本（对标 QMessageBox::buttonText；Qt 中已弃用）。
 * @param      self 目标对话框；传入 NULL 时返回 NULL。
 * @param      button 标准按钮值（StandardButton 位标志单值）。
 * @return     新建 XString*（找不到按钮时为空文本）；调用方负责
 *             XString_delete_base 释放。
 */
XString* XMessageBox_buttonText(const XMessageBox* self, int button);
/**
 * @brief      设置标准按钮文本（对标 QMessageBox::setButtonText；Qt 中已
 *             弃用；XString 主版本）。
 * @param      self 目标对话框；传入 NULL 时函数不执行任何操作。
 * @param      button 标准按钮值。
 * @param      text 新文本；NULL 按空文本处理；只借用，内部拷贝。
 * @return     无返回值；按钮不存在时保持原状。
 */
void XMessageBox_setButtonText(XMessageBox* self, int button,
                               const XString* text);
/**
 * @brief      设置标准按钮文本（UTF-8 兼容重载；对标 setButtonText）。
 * @param      self 目标对话框；传入 NULL 时函数不执行任何操作。
 * @param      button 标准按钮值。
 * @param      utf8 UTF-8 编码文本；NULL 按空文本处理。
 * @return     无返回值；按钮不存在时保持原状。
 */
void XMessageBox_setButtonText_2(XMessageBox* self, int button,
                                 const char* utf8);

/* ==================== 静态便捷（对标 aboutQt、standardIcon） ==================== */

/**
 * @brief      显示“关于 Qt”对话框（对标静态 QMessageBox::aboutQt）。
 * @details    与 XApplication_aboutQt 保持一致：XGui 无 Qt 运行时信息，
 *             本实现为文档化空操作；参数仅保留 API 形状。
 * @param      parent 父控件；可空；本实现不使用。
 * @param      title 标题；可空；本实现不使用。
 * @return     无返回值。
 */
void XMessageBox_aboutQt(XWidget* parent, const XString* title);
/**
 * @brief      生成消息框标准图标（对标静态 QMessageBox::standardIcon）。
 * @details    按图标枚举映射 XStyleStandardPixmap（Information/Warning/
 *             Critical/Question）并经当前应用样式生成。
 * @param      icon 图标枚举（XMessageBoxIcon）；NoIcon/未知值返回 NULL。
 * @return     新建 XIcon*（NULL=无）；调用方负责 XIcon_delete_base 释放。
 */
XIcon* XMessageBox_standardIcon(int icon);

#endif /* XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON */

#endif /* XMESSAGEBOX_H */