/******************************************************************************
 * @file       XStyleHints.h
 * @brief      XStyleHints 平台风格提示类（对标 Qt 6.8 QStyleHints 全部公开 API）。
 * @details    XStyleHints 继承 XObject，集中存放平台/桌面提供的交互参数
 *             与风格提示：光标闪烁周期、键盘输入间隔、鼠标双击/长按参数、
 *             拖拽启动距离与时间、Tab 焦点行为、右键菜单触发时机、滚轮
 *             滚动行数、文本快速选择阈值、颜色方案（浅色/深色）等。本模块
 *             不依赖任何平台 API：参数默认值参考桌面 Qt，全部可程序化
 *             修改；支持 setter + 通知信号的 13 个属性与 Qt 6.8 一致。
 *             只读常量（如 showIsFullScreen / showIsMaximized / passwordMask
 *             系列）提供 getter 与 Qt 一致。XGuiApplication_styleHints 返回
 *             进程内惰性单例。
 * @note       模块开关 XSTYLEHINTS_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XStyleHints 公共 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSTYLEHINTS_H
#define XSTYLEHINTS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"

#if XSTYLEHINTS_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XStyleHintsPrivate XStyleHintsPrivate;/** @brief 声明 XStyleHints 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XStyleHints)
XCLASS_DEFINE_EXTEND_END(XStyleHints, XObject)



/** @brief Tab 键焦点行为（对标 Qt 6.8 Qt::TabFocusBehavior）。 */
typedef enum XStyleHintsTabFocusBehavior
{
    XStyleHintsTabFocusBehavior_NoTabFocus = 0x00,         /**< Tab 不移动焦点。 */
    XStyleHintsTabFocusBehavior_TabFocusTextControls = 0x01, /**< 仅文本控件。 */
    XStyleHintsTabFocusBehavior_TabFocusListControls = 0x02, /**< 仅列表控件。 */
    XStyleHintsTabFocusBehavior_TabFocusAllControls = 0xff   /**< 所有控件。 */
} XStyleHintsTabFocusBehavior;

/** @brief 上下文菜单触发时机（对标 Qt 6.8 Qt::ContextMenuTrigger）。 */
typedef enum XStyleHintsContextMenuTrigger
{
    XStyleHintsContextMenuTrigger_Press = 0,   /**< 按下按键/鼠标时触发。 */
    XStyleHintsContextMenuTrigger_Release      /**< 释放按键/鼠标时触发。 */
} XStyleHintsContextMenuTrigger;

/** @brief 系统颜色方案（对标 Qt 6.8 Qt::ColorScheme）。 */
typedef enum XStyleHintsColorScheme
{
    XStyleHintsColorScheme_Unknown = 0, /**< 未知（未指定）。 */
    XStyleHintsColorScheme_Light,       /**< 浅色方案。 */
    XStyleHintsColorScheme_Dark         /**< 深色方案。 */
} XStyleHintsColorScheme;

/**
 * @brief      XStyleHints 风格提示对象；m_class 必须为第一个成员。
 * @details    所有参数保存在 m_data 私有块中，调用方不得直接访问。
 */
typedef struct XStyleHints
{
    XObject              m_class; /**< 第一个成员，由 XObject 管理。 */
    XStyleHintsPrivate*  m_data;  /**< 私有参数块，由 XStyleHints 拥有。 */
} XStyleHints;

/**
 * @brief      初始化 XStyleHints 类虚函数表并返回共享表指针。
 * @return     XStyleHints 类的共享 XVtable 指针。
 */
XVtable* XStyleHints_class_init(void);

/**
 * @brief      以平台默认参数初始化 XStyleHints。
 * @param      self 待初始化对象；必须与 XStyleHints_deinit_base 成对调用。
 */
void XStyleHints_init(XStyleHints* self);

/**
 * @brief      使用默认内存类型在堆上创建 XStyleHints。
 * @return     新对象指针；失败返回 NULL，调用方用 XStyleHints_delete_base 释放。
 */
#define XStyleHints_create() XStyleHints_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建 XStyleHints。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XStyleHints* XStyleHints_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放 XStyleHints 资源（栈/外部存储对象使用）。 */
#define XStyleHints_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XStyleHints 对象。 */
#define XStyleHints_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 可写属性（setter + 通知信号） ==================== */

/** @brief 光标闪烁周期（毫秒）；默认 1000。对标 cursorFlashTime。 */
int  XStyleHints_cursorFlashTime(const XStyleHints* self);
void XStyleHints_setCursorFlashTime(XStyleHints* self, int cursorFlashTime);

/** @brief 键盘自动重复率（毫秒/字符）；默认 33。对标 keyboardAutoRepeatRateF。 */
float XStyleHints_keyboardAutoRepeatRateF(const XStyleHints* self);

/** @brief 键盘自动重复率（毫秒/字符，整型别名，对标已弃用的 keyboardAutoRepeatRate）。 */
int XStyleHints_keyboardAutoRepeatRate(const XStyleHints* self);

/** @brief 键盘输入生效前的长按间隔（毫秒）；默认 400。对标 keyboardInputInterval。 */
int  XStyleHints_keyboardInputInterval(const XStyleHints* self);
void XStyleHints_setKeyboardInputInterval(XStyleHints* self, int keyboardInputInterval);

/** @brief 鼠标双击判定间隔（毫秒）；默认 400。对标 mouseDoubleClickInterval。 */
int  XStyleHints_mouseDoubleClickInterval(const XStyleHints* self);
void XStyleHints_setMouseDoubleClickInterval(XStyleHints* self, int mouseDoubleClickInterval);

/** @brief 鼠标长按判定间隔（毫秒）；默认 500。对标 mousePressAndHoldInterval。 */
int  XStyleHints_mousePressAndHoldInterval(const XStyleHints* self);
void XStyleHints_setMousePressAndHoldInterval(XStyleHints* self, int mousePressAndHoldInterval);

/** @brief 拖拽启动距离（像素）；默认 10。对标 startDragDistance。 */
int  XStyleHints_startDragDistance(const XStyleHints* self);
void XStyleHints_setStartDragDistance(XStyleHints* self, int startDragDistance);

/** @brief 拖拽启动耗时（毫秒）；默认 500。对标 startDragTime。 */
int  XStyleHints_startDragTime(const XStyleHints* self);
void XStyleHints_setStartDragTime(XStyleHints* self, int startDragTime);

/** @brief Tab 焦点行为；默认全部控件。对标 tabFocusBehavior。 */
XStyleHintsTabFocusBehavior XStyleHints_tabFocusBehavior(const XStyleHints* self);
void XStyleHints_setTabFocusBehavior(XStyleHints* self, XStyleHintsTabFocusBehavior behavior);

/** @brief 是否启用悬停效果；默认 true。对标 useHoverEffects。 */
bool XStyleHints_useHoverEffects(const XStyleHints* self);
void XStyleHints_setUseHoverEffects(XStyleHints* self, bool on);

/** @brief 上下文菜单是否显示快捷键；默认 false。对标 showShortcutsInContextMenus。 */
bool XStyleHints_showShortcutsInContextMenus(const XStyleHints* self);
void XStyleHints_setShowShortcutsInContextMenus(XStyleHints* self, bool show);

/** @brief 上下文菜单触发时机；默认 Press。对标 contextMenuTrigger。 */
XStyleHintsContextMenuTrigger XStyleHints_contextMenuTrigger(const XStyleHints* self);
void XStyleHints_setContextMenuTrigger(XStyleHints* self, XStyleHintsContextMenuTrigger trigger);

/** @brief 滚轮单步滚动行数；默认 3。对标 wheelScrollLines。 */
int  XStyleHints_wheelScrollLines(const XStyleHints* self);
void XStyleHints_setWheelScrollLines(XStyleHints* self, int scrollLines);

/** @brief 文本快速选择距离阈值（像素）；默认 0。对标 mouseQuickSelectionThreshold。 */
int  XStyleHints_mouseQuickSelectionThreshold(const XStyleHints* self);
void XStyleHints_setMouseQuickSelectionThreshold(XStyleHints* self, int threshold);

/** @brief 系统颜色方案；默认 Unknown。对标 colorScheme（含 unsetColorScheme）。 */
XStyleHintsColorScheme XStyleHints_colorScheme(const XStyleHints* self);
void XStyleHints_setColorScheme(XStyleHints* self, XStyleHintsColorScheme scheme);
/** @brief 复位颜色方案为 Unknown（对标 unsetColorScheme）。 */
void XStyleHints_unsetColorScheme(XStyleHints* self);

/* ==================== 只读常量（对标 Qt 只读属性） ==================== */

/** @brief 字体平滑 Gamma 值；恒为 1.0。对标 fontSmoothingGamma。 */
float XStyleHints_fontSmoothingGamma(const XStyleHints* self);

/** @brief 密码掩码字符（UTF-32）；默认 U+2022（•）。对标 passwordMaskCharacter。 */
uint32_t XStyleHints_passwordMaskCharacter(const XStyleHints* self);

/** @brief 密码掩码延迟（毫秒）；默认 0。对标 passwordMaskDelay。 */
int XStyleHints_passwordMaskDelay(const XStyleHints* self);

/** @brief 触摸释放时是否设置焦点；默认 true。对标 setFocusOnTouchRelease。 */
bool XStyleHints_setFocusOnTouchRelease(const XStyleHints* self);

/** @brief 窗口显示时是否应全屏；默认 false（平台注入）。对标 showIsFullScreen。 */
bool XStyleHints_showIsFullScreen(const XStyleHints* self);

/** @brief 窗口显示时是否应最大化；默认 false（平台注入）。对标 showIsMaximized。 */
bool XStyleHints_showIsMaximized(const XStyleHints* self);

/** @brief 拖拽启动速度（像素/秒）；默认 0。对标 startDragVelocity。 */
int XStyleHints_startDragVelocity(const XStyleHints* self);

/** @brief 是否启用 RTL 布局扩展；默认 false。对标 useRtlExtensions。 */
bool XStyleHints_useRtlExtensions(const XStyleHints* self);

/** @brief 单击激活（快捷键风格）；默认 false。对标 singleClickActivation。 */
bool XStyleHints_singleClickActivation(const XStyleHints* self);

/** @brief 鼠标双击判定距离（像素）；恒为 5。对标 mouseDoubleClickDistance。 */
int XStyleHints_mouseDoubleClickDistance(const XStyleHints* self);

/** @brief 触摸双击判定距离（像素）；恒为 40。对标 touchDoubleTapDistance。 */
int XStyleHints_touchDoubleTapDistance(const XStyleHints* self);

/* ==================== 信号（13 个，对标 QStyleHints 全部信号） ==================== */

void* XStyleHints_cursorFlashTimeChanged_signal(XStyleHints* self, int cursorFlashTime);
void* XStyleHints_keyboardInputIntervalChanged_signal(XStyleHints* self, int keyboardInputInterval);
void* XStyleHints_mouseDoubleClickIntervalChanged_signal(XStyleHints* self, int mouseDoubleClickInterval);
void* XStyleHints_mousePressAndHoldIntervalChanged_signal(XStyleHints* self, int mousePressAndHoldInterval);
void* XStyleHints_startDragDistanceChanged_signal(XStyleHints* self, int startDragDistance);
void* XStyleHints_startDragTimeChanged_signal(XStyleHints* self, int startDragTime);
void* XStyleHints_tabFocusBehaviorChanged_signal(XStyleHints* self, XStyleHintsTabFocusBehavior tabFocusBehavior);
void* XStyleHints_useHoverEffectsChanged_signal(XStyleHints* self, bool useHoverEffects);
void* XStyleHints_showShortcutsInContextMenusChanged_signal(XStyleHints* self, bool showShortcutsInContextMenus);
void* XStyleHints_contextMenuTriggerChanged_signal(XStyleHints* self, XStyleHintsContextMenuTrigger contextMenuTrigger);
void* XStyleHints_wheelScrollLinesChanged_signal(XStyleHints* self, int scrollLines);
void* XStyleHints_mouseQuickSelectionThresholdChanged_signal(XStyleHints* self, int threshold);
void* XStyleHints_colorSchemeChanged_signal(XStyleHints* self, XStyleHintsColorScheme scheme);

#endif /* XSTYLEHINTS_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSTYLEHINTS_H */
