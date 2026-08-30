/******************************************************************************
 * @file       XPlatformInputContext.h
 * @brief      XPlatformInputContext 平台输入上下文类（对标 Qt 6.8
 *             QPlatformInputContext 全部公共 API）。
 * @details    XPlatformInputContext 继承 XObject，是平台集成层提供的输入
 *             上下文「空后端」：嵌入式无系统输入法，本类承载隐式输入法
 *             状态（输入面板可见性/动画/键盘矩形/区域/方向）与焦点对象，
 *             并作为 XInputMethod 的网络层目标。
 *             - isValid() 恒 true（内置后端有效）；
 *             - hasCapability() 恒 true（支持隐藏文本能力位）；
 *             - showInputPanel/hideInputPanel 只记录可见性并转发
 *               可见性变化信号（无真实系统键盘）；
 *             - keybboardRect/动画/区域/方向提供 setter + emit 系列供
 *               未来真实平台后端驱动，与 Qt 的 emitXxxChanged 语义一致；
 *             - 全部 emit 系列信号经 XInputMethod_ 同名信号转发到
 *               XGuiApplication_inputMethod()（对标 QPlatformInputContext
 *               emit 到 QGuiApplication::inputMethod()）。
 * 本模块不依赖任何平台 API，也未连接系统输入法框架。
 * @note       模块开关 XPLATFORMINPUTCTX_ON 定义于 XGuiConfig.h；置 0 时
 *             裁剪整个公共 API，XInputMethod 的网络层转发退化为进程内直存
 *             （默认值），XPlatformIntegration 的 inputContext() 返回 NULL。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMINPUTCONTEXT_H
#define XPLATFORMINPUTCONTEXT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XGeometry.h"
#include "XEvent.h"
#include "XString.h"
#include "XVariant.h"
#include "XInputMethod.h"
#if XPLATFORMINPUTCTX_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XPlatformInputContextPrivate XPlatformInputContextPrivate;/** @brief 声明 XPlatformInputContext 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XPlatformInputContext)
XCLASS_DEFINE_EXTEND_END(XPlatformInputContext, XObject)



/** @brief 平台输入上下文能力位（对标 QPlatformInputContext::Capability）。 */
typedef enum XPlatformInputContextCapability
{
    XPlatformInputContextCapability_HiddenTextCapability = 0x1 /**< 支持隐藏文本输入。 */
} XPlatformInputContextCapability;

/**
 * @brief      XPlatformInputContext 平台输入上下文对象；m_class 必须为第一个成员。
 * @details    输入法状态保存在 m_data 私有块中，调用方不得直接访问。
 */
typedef struct XPlatformInputContext
{
    XObject                       m_class; /**< 第一个成员，由 XObject 管理。 */
    XPlatformInputContextPrivate* m_data;  /**< 私有数据块，由 XPlatformInputContext 拥有。 */
} XPlatformInputContext;

/**
 * @brief      初始化 XPlatformInputContext 类虚函数表并返回共享表指针。
 * @return     XPlatformInputContext 类的共享 XVtable 指针。
 */
XVtable* XPlatformInputContext_class_init(void);

/**
 * @brief      初始化空输入上下文（有效、无键盘、LTR、"C" 区域、无焦点对象）。
 * @param      self 待初始化对象；必须与 XPlatformInputContext_deinit_base 成对调用。
 */
void XPlatformInputContext_init(XPlatformInputContext* self);

/**
 * @brief      使用默认内存类型在堆上创建 XPlatformInputContext。
 * @return     新对象指针；失败返回 NULL，调用方用
 *             XPlatformInputContext_delete_base 释放。
 */
#define XPlatformInputContext_create() \
    XPlatformInputContext_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建 XPlatformInputContext。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XPlatformInputContext* XPlatformInputContext_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放 XPlatformInputContext 资源（栈/外部存储对象使用）。 */
#define XPlatformInputContext_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 深拷贝 XPlatformInputContext 资源。 */
#define XPlatformInputContext_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 移动 XPlatformInputContext 资源。 */
#define XPlatformInputContext_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))
/** @brief 删除堆上的 XPlatformInputContext 对象。 */
#define XPlatformInputContext_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 能力与有效性（对标 QPlatformInputContext） ==================== */

/**
 * @brief      输入上下文是否有效（对标 isValid）。
 * @details    嵌入式内置后端恒有效。
 * @return     恒 true。
 */
bool XPlatformInputContext_isValid(const XPlatformInputContext* self);

/**
 * @brief      是否支持指定能力位（对标 hasCapability）。
 * @details    嵌入式隐式输入法恒支持隐藏文本能力位（与 Qt 默认实现一致）。
 * @param      capability 能力位。
 * @return     恒 true。
 */
bool XPlatformInputContext_hasCapability(
        const XPlatformInputContext* self,
        XPlatformInputContextCapability capability);

/* ==================== 输入法动作转发（对标 QPlatformInputContext） ==================== */

/**
 * @brief      复位输入法状态（对标 reset；空后端 no-op）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformInputContext_reset(XPlatformInputContext* self);

/**
 * @brief      提交当前组合词句（对标 commit；空后端 no-op）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformInputContext_commit(XPlatformInputContext* self);

/**
 * @brief      通知编辑状态变化（对标 update；空后端 no-op）。
 * @param      self 目标对象；可为 NULL。
 * @param      queries 变化的查询项位集合。
 */
void XPlatformInputContext_update(XPlatformInputContext* self,
                                  XInputMethodQueries queries);

/**
 * @brief      通知组合词被点击（对标 invokeAction）。
 * @details    与 Qt 默认行为一致：Click 动作等价 reset()。
 * @param      self 目标对象；可为 NULL。
 * @param      action 动作类型。
 * @param      cursorPosition 点击处光标位置。
 */
void XPlatformInputContext_invokeAction(XPlatformInputContext* self,
                                        XInputMethodAction action,
                                        int cursorPosition);

/**
 * @brief      过滤输入事件（对标 filterEvent；空后端不消费任何事件）。
 * @param      self 目标对象；可为 NULL。
 * @param      event 输入事件借用指针；可为 NULL。
 * @return     恒 false（事件未被消费）。
 */
bool XPlatformInputContext_filterEvent(const XPlatformInputContext* self,
                                       const XEvent* event);

/* ==================== 虚拟键盘矩形（对标 QPlatformInputContext） ==================== */

/**
 * @brief      返回虚拟键盘矩形（对标 keyboardRect）。
 * @return     当前键盘矩形副本；未设置为零矩形。
 */
XRectF XPlatformInputContext_keyboardRect(const XPlatformInputContext* self);

/**
 * @brief      设置虚拟键盘矩形并转发 keyboardRectangleChanged。
 * @details    值变化时更新内部状态并调用 emitKeyboardRectChanged。
 * @param      self 目标对象；可为 NULL。
 * @param      rect 新键盘矩形；NULL 按零矩形处理。
 */
void XPlatformInputContext_setKeyboardRect(XPlatformInputContext* self,
                                           const XRectF* rect);

/**
 * @brief      通知键盘矩形变化（对标 emitKeyboardRectChanged；转发到
 *             绑定的 XInputMethod::keyboardRectangleChanged）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformInputContext_emitKeyboardRectChanged(XPlatformInputContext* self);

/* ==================== 动画状态（对标 QPlatformInputContext） ==================== */

/**
 * @brief      虚拟键盘是否正在动画（对标 isAnimating）。
 * @return     当前动画状态。
 */
bool XPlatformInputContext_isAnimating(const XPlatformInputContext* self);

/**
 * @brief      设置动画状态并转发 animatingChanged。
 * @param      self 目标对象；可为 NULL。
 * @param      animating 新动画状态。
 */
void XPlatformInputContext_setAnimating(XPlatformInputContext* self,
                                        bool animating);

/**
 * @brief      通知动画状态变化（对标 emitAnimatingChanged）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformInputContext_emitAnimatingChanged(XPlatformInputContext* self);

/* ==================== 输入面板显隐（对标 QPlatformInputContext） ==================== */

/**
 * @brief      请求显示输入面板（对标 showInputPanel）。
 * @details    空后端只记录状态：可见性变化时转发 visibleChanged。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformInputContext_showInputPanel(XPlatformInputContext* self);

/**
 * @brief      请求隐藏输入面板（对标 hideInputPanel）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformInputContext_hideInputPanel(XPlatformInputContext* self);

/**
 * @brief      输入面板是否可见（对标 isInputPanelVisible）。
 * @return     当前可见性。
 */
bool XPlatformInputContext_isInputPanelVisible(const XPlatformInputContext* self);

/**
 * @brief      通知输入面板可见性变化（对标 emitInputPanelVisibleChanged）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformInputContext_emitInputPanelVisibleChanged(XPlatformInputContext* self);

/* ==================== 区域与方向（对标 QPlatformInputContext） ==================== */

/**
 * @brief      返回当前区域语言。
 * @return     新建 XString（IETF 语言标签，默认 "C"）；调用方用
 *             XString_delete_base 释放。
 */
XString* XPlatformInputContext_locale(const XPlatformInputContext* self);

/**
 * @brief      设置当前区域语言并转发 localeChanged / inputDirectionChanged。
 * @details    空后端只记录语言标签；区域变化按 Qt QLocale::textDirection
 *             语义重新评估输入方向（常见阿拉伯/希伯来等语言为 RTL），变化
 *             时转发 inputDirectionChanged。
 * @param      self 目标对象；可为 NULL。
 * @param      locale UTF-8 IETF 语言标签；NULL 按 "C" 处理。
 */
void XPlatformInputContext_setLocale(XPlatformInputContext* self,
                                     const char* locale);

/**
 * @brief      通知区域语言变化（对标 emitLocaleChanged）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformInputContext_emitLocaleChanged(XPlatformInputContext* self);

/**
 * @brief      返回当前输入方向（对标 inputDirection）。
 * @return     方向枚举。
 */
XInputMethodLayoutDirection XPlatformInputContext_inputDirection(
        const XPlatformInputContext* self);

/**
 * @brief      设置输入方向（平台注入接口；对标 QPlatformInputContext 内部
 *             m_inputDirection 状态）。
 * @details    方向变化时转发 inputDirectionChanged。
 * @param      self 目标对象；可为 NULL。
 * @param      direction 新方向。
 */
void XPlatformInputContext_setInputDirection(
        XPlatformInputContext* self, XInputMethodLayoutDirection direction);

/**
 * @brief      通知输入方向变化（对标 emitInputDirectionChanged）。
 * @param      self 目标对象；可为 NULL。
 * @param      newDirection 新方向。
 */
void XPlatformInputContext_emitInputDirectionChanged(
        XPlatformInputContext* self, XInputMethodLayoutDirection newDirection);

/* ==================== 焦点对象（对标 QPlatformInputContext） ==================== */

/**
 * @brief      设置焦点对象（对标 setFocusObject；借用不持有）。
 * @param      self 目标对象；可为 NULL。
 * @param      object 焦点对象借用指针；可为 NULL 清除。
 */
void XPlatformInputContext_setFocusObject(XPlatformInputContext* self,
                                          XObject* object);

/**
 * @brief      返回当前焦点对象。
 * @return     借用指针；未设置返回 NULL。
 */
XObject* XPlatformInputContext_focusObject(const XPlatformInputContext* self);

/**
 * @brief      焦点对象是否接受输入法（对标 inputMethodAccepted）。
 * @details    该状态由 XInputMethod::update(ImEnabled) 根据焦点对象查询更新；
 *             未提供查询回调时与 Qt 的无效 QVariant 默认语义一致，为 false。
 * @return     true 接受；false 无焦点对象或入参非法。
 */
bool XPlatformInputContext_inputMethodAccepted(const XPlatformInputContext* self);

/** @brief 更新当前焦点对象是否接受输入法（由 XInputMethod::update 驱动）。 */
void XPlatformInputContext_setInputMethodAccepted(XPlatformInputContext* self,
                                                  bool accepted);

/**
 * @brief      绑定输入法对象（供 emit 系列转发信号；借用不持有）。
 * @details    由 XGuiApplication_inputMethod() 创建 XInputMethod 后调用，
 *             与 XInputMethod_setPlatformContext 成对。
 * @param      self 目标对象；可为 NULL。
 * @param      inputMethod 输入法对象借用指针；可为 NULL 解除。
 */
void XPlatformInputContext_setInputMethod(XPlatformInputContext* self,
                                          XInputMethod* inputMethod);

/* ==================== 静态辅助（对标 QPlatformInputContext 静态函数） ==================== */

/**
 * @brief      在焦点对象上设置选区（对标 setSelectionOnFocusObject；空后端 no-op）。
 */
void XPlatformInputContext_setSelectionOnFocusObject(const XPointF* anchorPos,
                                                     const XPointF* cursorPos);

/**
 * @brief      向焦点对象查询输入法属性（对标 queryFocusObject）。
 * @details    按 Qt 语义先用输入项变换的逆矩阵把窗口坐标还原到焦点控件
 *             坐标，再转发 XInputMethod_queryFocusObject；position 以 XPointF
 *             封装到 User 变体中供查询回调读取。
 */
XVariant* XPlatformInputContext_queryFocusObject(XInputMethodQuery query,
                                                 XPointF position);

/**
 * @brief      返回当前输入法输入项矩形（静态辅助，对标 Qt；窗口坐标）。
 */
XRectF XPlatformInputContext_inputItemRectangle(void);

/**
 * @brief      返回当前输入法输入项裁剪矩形（静态辅助，对标 Qt）。
 */
XRectF XPlatformInputContext_inputItemClipRectangle(void);

/**
 * @brief      返回当前输入法光标矩形（静态辅助，对标 Qt）。
 */
XRectF XPlatformInputContext_cursorRectangle(void);

/**
 * @brief      返回当前输入法锚点矩形（静态辅助，对标 Qt）。
 */
XRectF XPlatformInputContext_anchorRectangle(void);

/**
 * @brief      返回当前输入法键盘矩形（静态辅助，对标 Qt）。
 */
XRectF XPlatformInputContext_keyboardRectangle_static(void);

#ifdef __cplusplus
}
#endif

#endif /* XPLATFORMINPUTCTX_ON */
#endif /* XPLATFORMINPUTCONTEXT_H */
