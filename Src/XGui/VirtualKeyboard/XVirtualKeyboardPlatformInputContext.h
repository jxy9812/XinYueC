/**
 * @file       XVirtualKeyboardPlatformInputContext.h
 * @brief      XVirtualKeyboardPlatformInputContext 虚拟键盘平台输入上
 *             下文（模块内部；对标 Qt VK platforminputcontext.cpp 的
 *             面板驱动面，继承 XPlatformInputContext 空后端基座）。
 * @details    覆写 show/hide/update/setFocusObject 驱动默认面板（Qt
 *             platforminputcontext.cpp:111-132/266-283 口径）：show→
 *             面板 popup 当前焦点编辑框；hide→面板 closePopup；
 *             isInputPanelVisible→面板可见态。注意 commit 不走平台上
 *             下文——落地契约是 InputContext 公共信号
 *             commitRequested/keyEventRequested（apiMapping#1 定型）；
 *             焦点跟随由 XVirtualKeyboard 守护轮询承载（XVirtualKeyboard.c 事件不可
 *             达口径），本类是 XInputMethod_show/hide（XInputMethod.h
 *             :337-343）经平台上下文落面板的通道。
 *             集成点（TODO 指向并行所有者）：XGuiApplication 单例装配
 *             链创建本类并注册为进程平台上下文、绑定默认面板
 *             （setInputPanel）——装配入口在 XGuiApplication（非本路
 *             所有权），未装配时本类不创建、面板守护轮询独立工作。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON（级联要求
 *             XPLATFORMINPUTCTX_ON）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDPLATFORMINPUTCONTEXT_H
#define XVIRTUALKEYBOARDPLATFORMINPUTCONTEXT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XPlatformInputContext.h"
#include "XWidget.h"

#if XVIRTUALKEYBOARD_ON

/* 前向 typedef（槽位签名先于结构体使用；XPlatformInputContext.h 同款
 * 模式）。 */
typedef struct XVirtualKeyboardPlatformInputContext
    XVirtualKeyboardPlatformInputContext;

/**
 * @brief      虚拟键盘平台输入上下文虚函数表枚举（继承
 *             XPlatformInputContext）。
 * @details    本类无新增槽位：四槽（show/hide/update/setFocusObject
 *             面板驱动面）已上移为基座槽位（XPlatformInputContext 枚
 *             举），本类经虚表 OVERLOAD 覆盖（入口函数经虚表分派，未
 *             绑虚表时回落基座行为）。
 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardPlatformInputContext)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardPlatformInputContext,
                         XPlatformInputContext)

/** @brief 显示面板槽签名。 */
typedef void (*XVkPicShowSlot)(XVirtualKeyboardPlatformInputContext* self);
/** @brief 更新面板槽签名。 */
typedef void (*XVkPicUpdateSlot)(
        XVirtualKeyboardPlatformInputContext* self, uint32_t queries);
/** @brief 焦点对象槽签名。 */
typedef void (*XVkPicFocusSlot)(
        XVirtualKeyboardPlatformInputContext* self, XObject* focusObject);

/**
 * @brief      平台输入上下文对象；m_base 必须为第一个成员。
 * @details    m_panel 默认面板（借用，可 NULL）——显隐驱动目标。
 */
typedef struct XVirtualKeyboardPlatformInputContext
{
    XPlatformInputContext m_base; /**< 基类成员；必须是第一个。 */
    XWidget* m_panel;             /**< 默认面板（XVirtualKeyboard 借用；NULL=未绑定）。 */
} XVirtualKeyboardPlatformInputContext;

/** @brief 初始化类虚函数表并返回共享表指针。 */
XVtable* XVirtualKeyboardPlatformInputContext_class_init(void);

/**
 * @brief      初始化（栈对象路径；空面板绑定）。
 * @param      self 待初始化对象；与 *_deinit_base 成对调用。
 */
void XVirtualKeyboardPlatformInputContext_init(
        XVirtualKeyboardPlatformInputContext* self);

/**
 * @brief      使用默认内存类型创建。
 * @return     新对象指针；失败返回 NULL。
 */
#define XVirtualKeyboardPlatformInputContext_create() \
    XVirtualKeyboardPlatformInputContext_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型创建。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XVirtualKeyboardPlatformInputContext*
XVirtualKeyboardPlatformInputContext_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放资源。 */
#define XVirtualKeyboardPlatformInputContext_deinit_base(self) \
    XPlatformInputContext_deinit_base((XPlatformInputContext*)(self))
/** @brief 删除堆上对象。 */
#define XVirtualKeyboardPlatformInputContext_delete_base(self) \
    XPlatformInputContext_delete_base((XPlatformInputContext*)(self))

/**
 * @brief      绑定默认面板（借用；TODO 指向 XGuiApplication 装配链所
 *             有者：单例创建后经本接口绑定默认 XVirtualKeyboard）。
 * @param      self 目标对象；可为 NULL。
 * @param      panel 面板借用指针；可为 NULL 解绑。
 */
void XVirtualKeyboardPlatformInputContext_setInputPanel(
        XVirtualKeyboardPlatformInputContext* self, XWidget* panel);

/**
 * @brief      请求显示面板（覆写入口；show→面板 popup 焦点编辑框，
 *             总开关关或无焦点时无操作）。
 */
void XVirtualKeyboardPlatformInputContext_showInputPanel(
        XVirtualKeyboardPlatformInputContext* self);

/**
 * @brief      请求隐藏面板（覆写入口；hide→面板 closePopup）。
 */
void XVirtualKeyboardPlatformInputContext_hideInputPanel(
        XVirtualKeyboardPlatformInputContext* self);

/**
 * @brief      面板是否可见（覆写入口；=面板可见态；未绑定返回基座态）。
 */
bool XVirtualKeyboardPlatformInputContext_isInputPanelVisible(
        const XVirtualKeyboardPlatformInputContext* self);

/**
 * @brief      输入态更新（覆写入口；转发 InputContext.update 刷新缓存，
 *             并按 Qt updateInputPanelVisible 口径同步面板可见性）。
 */
void XVirtualKeyboardPlatformInputContext_update(
        XVirtualKeyboardPlatformInputContext* self, uint32_t queries);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDPLATFORMINPUTCONTEXT_H */
