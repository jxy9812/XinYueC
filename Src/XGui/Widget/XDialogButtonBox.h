/**
 * @file       XDialogButtonBox.h
 * @brief      XDialogButtonBox 对话框按钮排布控件（对标 Qt 6.8
 *             QDialogButtonBox 全部公共 API）。
 * @details    功能范围：
 *             - ButtonRole 枚举（InvalidRole..NRoles，数值对齐）；
 *             - StandardButton 位标志（Ok..RestoreDefaults，**位值与
 *               Qt 逐项一致**，含 FirstButton/LastButton）；
 *             - ButtonLayout 枚举（Win/Mac/Kde/Gnome/Android，数值对齐；
 *               第一版排布不区分布局，统一右对齐横排）；
 *             - addButton 三重载（控件+角色 / 文本+角色创建
 *               QPushButton / 标准按钮枚举）；removeButton/clear；
 *             - buttons/buttonRole/setStandardButtons/standardButtons/
 *               standardButton/button(standard)；
 *             - setCenterButtons/centerButtons；
 *             - 信号：clicked(button)/accepted/helpRequested/rejected
 *               （标准按钮按角色自动发射 accepted/rejected）。
 *             标准按钮文本（中文）：确定/保存/全部保存/打开/是/全部是/
 *             否/全部否/中止/重试/忽略/关闭/取消/放弃/帮助/应用/重置/
 *             恢复默认（对标 Qt 中文翻译风格）。
 * @note       模块总开关 XDIALOGBUTTONBOX_ON 定义于 XGuiConfig.h；=0 时
 *             裁剪全部公共 API。依赖 XWIDGET_ON、XPUSHBUTTON_ON。
 * @author     XinYueC 团队
 */
#ifndef XDIALOGBUTTONBOX_H
#define XDIALOGBUTTONBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#if XPUSHBUTTON_ON
#include "XPushButton.h"
#endif

#if XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON

/** @brief 按钮角色（对标 QDialogButtonBox::ButtonRole，数值一致）。 */
typedef enum XDialogButtonBoxRole
{
    XDialogButtonBoxRole_InvalidRole = -1,
    XDialogButtonBoxRole_AcceptRole = 0,
    XDialogButtonBoxRole_RejectRole = 1,
    XDialogButtonBoxRole_DestructiveRole = 2,
    XDialogButtonBoxRole_ActionRole = 3,
    XDialogButtonBoxRole_HelpRole = 4,
    XDialogButtonBoxRole_YesRole = 5,
    XDialogButtonBoxRole_NoRole = 6,
    XDialogButtonBoxRole_ResetRole = 7,
    XDialogButtonBoxRole_ApplyRole = 8,
    XDialogButtonBoxRole_NRoles = 9
} XDialogButtonBoxRole;

/** @brief 标准按钮位标志（对标 QDialogButtonBox::StandardButton，
 *         位值逐项一致）。 */
typedef enum XDialogButtonBoxStandardButton
{
    XDialogButtonBoxStandard_NoButton = 0x00000000,
    XDialogButtonBoxStandard_Ok = 0x00000400,
    XDialogButtonBoxStandard_Save = 0x00000800,
    XDialogButtonBoxStandard_SaveAll = 0x00001000,
    XDialogButtonBoxStandard_Open = 0x00002000,
    XDialogButtonBoxStandard_Yes = 0x00004000,
    XDialogButtonBoxStandard_YesToAll = 0x00008000,
    XDialogButtonBoxStandard_No = 0x00010000,
    XDialogButtonBoxStandard_NoToAll = 0x00020000,
    XDialogButtonBoxStandard_Abort = 0x00040000,
    XDialogButtonBoxStandard_Retry = 0x00080000,
    XDialogButtonBoxStandard_Ignore = 0x00100000,
    XDialogButtonBoxStandard_Close = 0x00200000,
    XDialogButtonBoxStandard_Cancel = 0x00400000,
    XDialogButtonBoxStandard_Discard = 0x00800000,
    XDialogButtonBoxStandard_Help = 0x01000000,
    XDialogButtonBoxStandard_Apply = 0x02000000,
    XDialogButtonBoxStandard_Reset = 0x04000000,
    XDialogButtonBoxStandard_RestoreDefaults = 0x08000000,
    XDialogButtonBoxStandard_FirstButton = XDialogButtonBoxStandard_Ok,
    XDialogButtonBoxStandard_LastButton =
        XDialogButtonBoxStandard_RestoreDefaults
} XDialogButtonBoxStandardButton;

/** @brief 按钮排布风格（对标 QDialogButtonBox::ButtonLayout，数值一致）。 */
typedef enum XDialogButtonBoxLayout
{
    XDialogButtonBoxLayout_WinLayout = 0,
    XDialogButtonBoxLayout_MacLayout = 1,
    XDialogButtonBoxLayout_KdeLayout = 2,
    XDialogButtonBoxLayout_GnomeLayout = 3,
    XDialogButtonBoxLayout_AndroidLayout = 4
} XDialogButtonBoxLayout;

XCLASS_DEFINE_BEGING(XDialogButtonBox)
XCLASS_DEFINE_EXTEND_END(XDialogButtonBox, XWidget)

/**
 * @brief      XDialogButtonBox 控件对象；m_base 必须是第一个成员。
 */
typedef struct XDialogButtonBox
{
    XWidget m_base;           /**< 基类成员；必须是第一个。 */
    XVector* m_buttons;       /**< 成员按钮借用指针数组（XAbstractButton*）。 */
    XVector* m_roles;         /**< 与成员顺序一致的 role 数组（int）。 */
    XVector* m_standards;     /**< 与成员顺序一致的标准按钮值（int）。 */
    XVector* m_bridges;       /**< 成员桥接对象数组（XDBBridge*，拥有）。 */
    int m_orientation;        /**< 方向（XAbstractSliderOrientation 复用
                                   数值：1=水平 2=垂直；默认水平）。 */
    bool m_center;            /**< 按钮居中（默认 false 右对齐）。 */
} XDialogButtonBox;

/* ==================== 生命周期 ==================== */

XVtable* XDialogButtonBox_class_init(void);
void XDialogButtonBox_init(XDialogButtonBox* self, XWidget* parent,
                           XWidgetFlags flags);
#define XDialogButtonBox_create(parent, flags) XDialogButtonBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XDialogButtonBox* XDialogButtonBox_create_ex(XMemoryType memory,
                                             XWidget* parent,
                                             XWidgetFlags flags);
#define XDialogButtonBox_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 方向与布局 ==================== */

void XDialogButtonBox_setOrientation(XDialogButtonBox* self, int orientation);
int XDialogButtonBox_orientation(const XDialogButtonBox* self);
void XDialogButtonBox_setCenterButtons(XDialogButtonBox* self, bool center);
bool XDialogButtonBox_centerButtons(const XDialogButtonBox* self);

/* ==================== 按钮管理 ==================== */

/** @brief 添加按钮并指定角色（对标 addButton(QAbstractButton*, role)；
 *         按钮归调用方所有）。 */
/**
 * @brief      添加按钮到组。
 */
void XDialogButtonBox_addButton(XDialogButtonBox* self,
                                XAbstractButton* button,
                                XDialogButtonBoxRole role);
/** @brief 以文本创建按钮并指定角色（对标 addButton(const QString&, role)；
 *         返回的按钮归调用方所有）。 */
/**
 * @brief      添加按钮。
 */
XPushButton* XDialogButtonBox_addButton_2(XDialogButtonBox* self,
                                          const char* utf8,
                                          XDialogButtonBoxRole role);
/** @brief 添加标准按钮（对标 addButton(StandardButton)；返回的按钮归
 *         调用方所有；未知枚举返回 NULL）。 */
/**
 * @brief      添加按钮。
 */
XPushButton* XDialogButtonBox_addButton_3(XDialogButtonBox* self,
                                          XDialogButtonBoxStandardButton which);
/** @brief 移除按钮（对标 removeButton）。 */
/**
 * @brief      从组移除按钮。
 */
void XDialogButtonBox_removeButton(XDialogButtonBox* self,
                                   XAbstractButton* button);
/** @brief 清空全部按钮（对标 clear）。 */
/**
 * @brief      清空内容（对标 Qt 同名槽）。
 */
void XDialogButtonBox_clear(XDialogButtonBox* self);
/** @brief 成员按钮数组（XAbstractButton* 元素；对标 buttons()）。 */
const XVector* XDialogButtonBox_buttons(const XDialogButtonBox* self);
/** @brief 查询按钮角色；非成员返回 InvalidRole。 */
XDialogButtonBoxRole XDialogButtonBox_buttonRole(
    const XDialogButtonBox* self, XAbstractButton* button);

/* ==================== 标准按钮 ==================== */

/** @brief 按位掩码批量创建标准按钮（对标 setStandardButtons）。 */
/**
 * @brief      设置标准按钮组。
 */
void XDialogButtonBox_setStandardButtons(
    XDialogButtonBox* self, int buttons);
/** @brief 查询当前标准按钮位掩码（对标 standardButtons）。 */
/**
 * @brief      获取标准按钮组。
 */
int XDialogButtonBox_standardButtons(const XDialogButtonBox* self);
/** @brief 查询成员按钮对应的标准按钮枚举
 *         （对标 standardButton；非标准按钮返回 NoButton）。 */
XDialogButtonBoxStandardButton XDialogButtonBox_standardButton(
    const XDialogButtonBox* self, XAbstractButton* button);
/** @brief 查询标准按钮对应的成员按钮
 *         （对标 button(StandardButton)；不存在返回 NULL）。 */
/**
 * @brief      获取标准按钮。
 */
XPushButton* XDialogButtonBox_button(
    const XDialogButtonBox* self, XDialogButtonBoxStandardButton which);

/* ==================== 信号 ==================== */

/**
 * @brief      点击信号（真发射）。
 */
void* XDialogButtonBox_clicked_signal(XDialogButtonBox* self,
                                      XAbstractButton* button);
/**
 * @brief      接受信号（真发射）。
 */
void* XDialogButtonBox_accepted_signal(XDialogButtonBox* self);
void* XDialogButtonBox_helpRequested_signal(XDialogButtonBox* self);
/**
 * @brief      拒绝信号（真发射）。
 */
void* XDialogButtonBox_rejected_signal(XDialogButtonBox* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDIALOGBUTTONBOX_H */
