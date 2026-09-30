/**
 * @file       XVirtualKeyboardAbstractInputMethod_Protected.h
 * @brief      XVirtualKeyboardAbstractInputMethod 保护头（类名级，与现
 *             有保护头同款命名）：引擎装配入口与私有访问。
 * @details    契约头（A 先行冻结签名）：setInputEngine 仅
 *             XVirtualKeyboardInputEngine TU 可调（Qt friend 口径，私
 *             有构造等价）；setInputContext 由引擎装配链同步调用。
 *             B/D 侧实现归属：语言插件经公共头槽位注册即可，不触碰本
 *             头；后续演进须所有权方会签。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDABSTRACTINPUTMETHOD_PROTECTED_H
#define XVIRTUALKEYBOARDABSTRACTINPUTMETHOD_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XVirtualKeyboardAbstractInputMethod.h"

#if XVIRTUALKEYBOARD_ON

/**
 * @brief      装配输入引擎（仅 XVirtualKeyboardInputEngine TU 可调；
 *             对标 Qt friend setInputEngine）。
 * @details    引擎 setInputMethod 装配链调用：记录反向引用；重复装配
 *             以最后一次为准。engine 传 NULL 表示解除装配。
 * @param      self 输入法对象；可为 NULL。
 * @param      engine 引擎借用指针；可为 NULL。
 */
void XVirtualKeyboardAbstractInputMethod_setInputEngine(
        XVirtualKeyboardAbstractInputMethod* self,
        XVirtualKeyboardInputEngine* engine);

/**
 * @brief      装配输入上下文（引擎装配链同步调用；借用不持有）。
 * @param      self 输入法对象；可为 NULL。
 * @param      context 上下文借用指针；可为 NULL。
 */
void XVirtualKeyboardAbstractInputMethod_setInputContext(
        XVirtualKeyboardAbstractInputMethod* self,
        XVirtualKeyboardInputContext* context);

#endif /* XVIRTUALKEYBOARD_ON */
#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDABSTRACTINPUTMETHOD_PROTECTED_H */
