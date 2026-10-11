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

/** @brief 按钮角色（对标 QDialogButtonBox::ButtonRole，数值一致）。
 *         单值使用，不按位组合。 */
typedef enum XDialogButtonBoxRole
{
    XDialogButtonBoxRole_InvalidRole = -1,    /**< 无效角色（查询非成员
                                                   按钮时的返回值）。 */
    XDialogButtonBoxRole_AcceptRole = 0,      /**< 接受：点击自动发射
                                                   accepted。 */
    XDialogButtonBoxRole_RejectRole = 1,      /**< 拒绝：点击自动发射
                                                   rejected。 */
    XDialogButtonBoxRole_DestructiveRole = 2, /**< 破坏性（不可恢复操作）。 */
    XDialogButtonBoxRole_ActionRole = 3,      /**< 普通动作（无自动信号）。 */
    XDialogButtonBoxRole_HelpRole = 4,        /**< 帮助：点击自动发射
                                                   helpRequested。 */
    XDialogButtonBoxRole_YesRole = 5,         /**< 是：点击自动发射
                                                   accepted。 */
    XDialogButtonBoxRole_NoRole = 6,          /**< 否：点击自动发射
                                                   rejected。 */
    XDialogButtonBoxRole_ResetRole = 7,       /**< 重置（无自动信号）。 */
    XDialogButtonBoxRole_ApplyRole = 8,       /**< 应用（无自动信号）。 */
    XDialogButtonBoxRole_NRoles = 9           /**< 角色个数上界（不含
                                                   InvalidRole）。 */
} XDialogButtonBoxRole;

/** @brief 标准按钮位标志（对标 QDialogButtonBox::StandardButton，
 *         位值逐项一致；可按位组合传 setStandardButtons）。 */
typedef enum XDialogButtonBoxStandardButton
{
    XDialogButtonBoxStandard_NoButton = 0x00000000,        /**< 无按钮（掩码 0）。 */
    XDialogButtonBoxStandard_Ok = 0x00000400,              /**< 确定。 */
    XDialogButtonBoxStandard_Save = 0x00000800,            /**< 保存。 */
    XDialogButtonBoxStandard_SaveAll = 0x00001000,         /**< 全部保存。 */
    XDialogButtonBoxStandard_Open = 0x00002000,            /**< 打开。 */
    XDialogButtonBoxStandard_Yes = 0x00004000,             /**< 是。 */
    XDialogButtonBoxStandard_YesToAll = 0x00008000,        /**< 全部是。 */
    XDialogButtonBoxStandard_No = 0x00010000,              /**< 否。 */
    XDialogButtonBoxStandard_NoToAll = 0x00020000,         /**< 全部否。 */
    XDialogButtonBoxStandard_Abort = 0x00040000,           /**< 中止。 */
    XDialogButtonBoxStandard_Retry = 0x00080000,           /**< 重试。 */
    XDialogButtonBoxStandard_Ignore = 0x00100000,          /**< 忽略。 */
    XDialogButtonBoxStandard_Close = 0x00200000,           /**< 关闭。 */
    XDialogButtonBoxStandard_Cancel = 0x00400000,          /**< 取消。 */
    XDialogButtonBoxStandard_Discard = 0x00800000,         /**< 放弃。 */
    XDialogButtonBoxStandard_Help = 0x01000000,            /**< 帮助。 */
    XDialogButtonBoxStandard_Apply = 0x02000000,           /**< 应用。 */
    XDialogButtonBoxStandard_Reset = 0x04000000,           /**< 重置。 */
    XDialogButtonBoxStandard_RestoreDefaults = 0x08000000, /**< 恢复默认。 */
    XDialogButtonBoxStandard_FirstButton =
        XDialogButtonBoxStandard_Ok,                       /**< 位值下界（同 Ok）。 */
    XDialogButtonBoxStandard_LastButton =
        XDialogButtonBoxStandard_RestoreDefaults           /**< 位值上界（同
                                                                RestoreDefaults）。 */
} XDialogButtonBoxStandardButton;

/** @brief 按钮排布风格（对标 QDialogButtonBox::ButtonLayout，数值一致）。
 *         单值使用，不按位组合；第一版排布不区分布局，统一右对齐横排
 *         （仅记录风格值）。 */
typedef enum XDialogButtonBoxLayout
{
    XDialogButtonBoxLayout_WinLayout = 0,     /**< Windows 风格排布。 */
    XDialogButtonBoxLayout_MacLayout = 1,     /**< macOS 风格排布。 */
    XDialogButtonBoxLayout_KdeLayout = 2,     /**< KDE 风格排布。 */
    XDialogButtonBoxLayout_GnomeLayout = 3,   /**< GNOME 风格排布。 */
    XDialogButtonBoxLayout_AndroidLayout = 4  /**< Android 风格排布。 */
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

/**
 * @brief      初始化 XDialogButtonBox 类虚函数表。
 * @details    进程内单例：首次调用建表并继承 XWidget 虚表，重复调用
 *             返回同一份共享虚表。
 * @return     返回 XDialogButtonBox 共享虚表指针。
 */
XVtable* XDialogButtonBox_class_init(void);
/**
 * @brief      初始化对话框按钮排布控件（对标 QDialogButtonBox 构造）。
 * @details    默认水平方向、按钮右对齐；成员按钮列表为空。栈对象须与
 *             XClassDeinit 成对使用。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     无返回值。
 */
void XDialogButtonBox_init(XDialogButtonBox* self, XWidget* parent,
                           XWidgetFlags flags);
/**
 * @brief      以默认内存类型创建 XDialogButtonBox 堆对象（转发
 *             XDialogButtonBox_create_ex）。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新对象指针，调用方拥有，须 XClassDelete；失败返回 NULL。
 */
#define XDialogButtonBox_create(parent, flags) XDialogButtonBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      以指定内存类型创建 XDialogButtonBox 堆对象。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新对象指针，调用方拥有，须 XClassDelete；失败返回 NULL。
 */
XDialogButtonBox* XDialogButtonBox_create_ex(XMemoryType memory,
                                             XWidget* parent,
                                             XWidgetFlags flags);

/* ==================== 方向与布局 ==================== */

/**
 * @brief      设置按钮排布方向（对标 QDialogButtonBox::setOrientation；
 *             数值复用 XAbstractSliderOrientation：1=水平、2=垂直）。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      orientation 方向数值（1=水平、2=垂直）。
 * @return     无返回值。
 */
void XDialogButtonBox_setOrientation(XDialogButtonBox* self, int orientation);
/** @brief XDialog按钮盒orientation（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XDialogButtonBox_orientation(const XDialogButtonBox* self);
/** @brief XDialog按钮盒set居中Buttons（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param center bool 参数。
 * @return 无返回值。
 */
void XDialogButtonBox_setCenterButtons(XDialogButtonBox* self, bool center);
/** @brief XDialog按钮盒centerButtons（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XDialogButtonBox_centerButtons(const XDialogButtonBox* self);

/* ==================== 按钮管理 ==================== */

/**
 * @brief      添加按钮并指定角色（对标 addButton(QAbstractButton*, role)）。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      button 按钮借用指针；归调用方所有，移除/清空只解除本控件
 *             登记，不删除按钮本体。
 * @param      role 按钮角色：AcceptRole/YesRole 点击自动发射 accepted，
 *             RejectRole/NoRole 发射 rejected，HelpRole 发射
 *             helpRequested，其余角色仅发射 clicked。
 * @return     无返回值。
 */
void XDialogButtonBox_addButton(XDialogButtonBox* self,
                                XAbstractButton* button,
                                XDialogButtonBoxRole role);
/**
 * @brief      以文本创建按钮并指定角色（对标 addButton(const QString&,
 *             role)）。
 * @param      self 目标控件指针；传入 NULL 时返回 NULL。
 * @param      utf8 按钮文本；按 UTF-8 解码；NULL 按空串处理。
 * @param      role 按钮角色（自动发射规则同 addButton）。
 * @return     新建按钮指针（父控件为本控件）；失败返回 NULL。
 */
XPushButton* XDialogButtonBox_addButton_2(XDialogButtonBox* self,
                                          const char* utf8,
                                          XDialogButtonBoxRole role);
/**
 * @brief      添加标准按钮（对标 addButton(StandardButton)）。
 * @param      self 目标控件指针；传入 NULL 时返回 NULL。
 * @param      which 标准按钮枚举（按钮文本用预置中文文案，角色按枚举
 *             预设映射）。
 * @return     新建按钮指针（父控件为本控件）；未知枚举返回 NULL。
 */
XPushButton* XDialogButtonBox_addButton_3(XDialogButtonBox* self,
                                          XDialogButtonBoxStandardButton which);
/**
 * @brief      移除按钮（对标 removeButton）。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      button 要移除的成员按钮借用指针；非成员时无操作；不删除
 *             按钮本体。
 * @return     无返回值。
 */
void XDialogButtonBox_removeButton(XDialogButtonBox* self,
                                   XAbstractButton* button);
/**
 * @brief      清空全部按钮（对标 clear）。
 * @details    解除全部成员按钮的登记与内部桥接，不删除按钮本体。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @return     无返回值。
 */
void XDialogButtonBox_clear(XDialogButtonBox* self);
/**
 * @brief      成员按钮数组（XAbstractButton* 元素；对标 buttons()）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     内部数组的借用指针（元素为按钮借用指针），禁止释放或
 *             修改；无效时返回 NULL。
 */
const XVector* XDialogButtonBox_buttons(const XDialogButtonBox* self);
/**
 * @brief      查询按钮角色（对标 buttonRole）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      button 待查询的按钮借用指针。
 * @return     成员按钮的角色；非成员或无效时返回 InvalidRole。
 */
XDialogButtonBoxRole XDialogButtonBox_buttonRole(
    const XDialogButtonBox* self, XAbstractButton* button);

/* ==================== 标准按钮 ==================== */

/**
 * @brief      按位掩码批量创建标准按钮（对标 setStandardButtons）。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      buttons 标准按钮位组合（XDialogButtonBoxStandardButton
 *             按位或）。
 * @return     无返回值。
 */
void XDialogButtonBox_setStandardButtons(
    XDialogButtonBox* self, int buttons);
/**
 * @brief      查询当前标准按钮位掩码（对标 standardButtons）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     当前成员标准按钮位组合；无效时返回 0。
 */
int XDialogButtonBox_standardButtons(const XDialogButtonBox* self);
/**
 * @brief      查询成员按钮对应的标准按钮枚举（对标 standardButton）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      button 待查询的按钮借用指针。
 * @return     该按钮的标准按钮枚举；非标准按钮或无效时返回 NoButton。
 */
XDialogButtonBoxStandardButton XDialogButtonBox_standardButton(
    const XDialogButtonBox* self, XAbstractButton* button);
/**
 * @brief      查询标准按钮对应的成员按钮
 *             （对标 button(StandardButton)）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      which 标准按钮枚举。
 * @return     匹配的成员按钮指针；不存在返回 NULL。
 */
XPushButton* XDialogButtonBox_button(
    const XDialogButtonBox* self, XDialogButtonBoxStandardButton which);

/* ==================== 信号 ==================== */

/**
 * @brief      点击信号（真发射；任一成员按钮点击都发射，载荷为按钮）。
 * @param      self 目标控件指针。
 * @param      button 被点击的按钮借用指针。
 * @return     信号标识地址（发射经 XObject_emitSignal）。
 */
void* XDialogButtonBox_clicked_signal(XDialogButtonBox* self,
                                      XAbstractButton* button);
/**
 * @brief      接受信号（真发射；AcceptRole/YesRole 按钮点击时发射）。
 * @param      self 目标控件指针。
 * @return     信号标识地址（发射经 XObject_emitSignal）。
 */
void* XDialogButtonBox_accepted_signal(XDialogButtonBox* self);
/** @brief XDialog按钮盒helpRequested 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XDialogButtonBox_helpRequested_signal(XDialogButtonBox* self);
/**
 * @brief      拒绝信号（真发射；RejectRole/NoRole 按钮点击时发射）。
 * @param      self 目标控件指针。
 * @return     信号标识地址（发射经 XObject_emitSignal）。
 */
void* XDialogButtonBox_rejected_signal(XDialogButtonBox* self);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON */

#ifdef __cplusplus
}
#endif

#endif /* XDIALOGBUTTONBOX_H */
