/**
 * @file       XVirtualKeyboardInputContext_Protected.h
 * @brief      XVirtualKeyboardInputContext 保护头（类名级）：内部访问
 *             函数、焦点/更新驱动入口与 ShiftHandler 状态机（Qt 私有
 *             shifthandler.cpp 口径）。
 * @details    契约头（签名冻结）：平台上下文/面板/插件装配链经本头驱
 *             动上下文——setFocusObject（焦点切换）、update（hints/位
 *             置刷新）、reset（收层弃草稿链）；ShiftHandler 的
 *             toggleShift/reset/autoCapitalize 为面板 shift 键与焦点
 *             变化回调；XVirtualKeyboardInputContext_instanceEngine
 *             供引擎注册表静态访问进程单例引擎。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDINPUTCONTEXT_PROTECTED_H
#define XVIRTUALKEYBOARDINPUTCONTEXT_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XVirtualKeyboardInputContext.h"
#include "XVirtualKeyboardInputEngine.h"

#if XVIRTUALKEYBOARD_ON

/* ==================== 生命周期（创建入口受控） ==================== */

/**
 * @brief      初始化（instance 惰性单例与测试路径使用）。
 * @details    创建引擎、注册内置语言插件工厂（zh_CN=拼音、en/C=Plain）
 *             并装配默认 Plain 输入法。
 * @param      self 待初始化上下文。
 */
void XVirtualKeyboardInputContext_init(XVirtualKeyboardInputContext* self);

/**
 * @brief      创建实例（Qt 私有构造口径的受控入口；公共面仅 instance）。
 * @param      memory 对象内存类型。
 * @return     新上下文指针；失败返回 NULL。
 */
XVirtualKeyboardInputContext*
XVirtualKeyboardInputContext_create_ex(XMemoryType memory);

/* ==================== 焦点与更新驱动 ==================== */

/**
 * @brief      设置焦点对象（对标 QVirtualKeyboardInputContextPrivate
 *             焦点链路；平台上下文/守护轮询调用）。
 * @details    记录借用指针并发 inputItemChanged（变化时）；随后触发
 *             update(全量) 刷新 hints/位置缓存。
 * @param      self 上下文对象；可为 NULL。
 * @param      focusObject 焦点对象借用指针；可为 NULL。
 */
void XVirtualKeyboardInputContext_setFocusObject(
        XVirtualKeyboardInputContext* self, XObject* focusObject);

/**
 * @brief      刷新输入态（对标 QVirtualKeyboardInputContextPrivate::
 *             update(queries)）。
 * @details    向焦点对象查询 ImHints（经 XInputMethod_defaultQueryHandler
 *             桥）并 OR 叠加 Settings.inputMethodHints；变化时 reset
 *             引擎并发 inputMethodHintsChanged；同步 anchor/cursor 位
 *             置缓存并发对应信号。
 * @param      self 上下文对象；可为 NULL。
 * @param      queries 查询项位集合（XInputMethodQuery 位或；0 等价全量）。
 */
void XVirtualKeyboardInputContext_update(XVirtualKeyboardInputContext* self,
                                         uint32_t queries);

/**
 * @brief      复位输入态（对标 priv reset）：清组串 + 引擎 reset +
 *             ShiftHandler reset。
 * @details    面板 closePopup 弃草稿与焦点切换路径调用。
 */
void XVirtualKeyboardInputContext_reset(XVirtualKeyboardInputContext* self);

/* ==================== ShiftHandler 状态机（Qt 私有口径） ==================== */

/**
 * @brief      切换 shift（对标 ShiftHandler::toggleShift）。
 * @details    双击（间隔 <= XStyleHints_mouseDoubleClickInterval）→
 *             capsLock 锁定；capsLock 下单击解除；普通单击在
 *             toggleShiftEnabled（hints 无 UppercaseOnly|LowercaseOnly）
 *             时翻转临时 shift。变化发 shiftActiveChanged/
 *             capsLockActiveChanged/uppercaseChanged。
 * @param      self 上下文对象；可为 NULL。
 */
void XVirtualKeyboardInputContext_toggleShift(
        XVirtualKeyboardInputContext* self);

/**
 * @brief      自动大写评估（对标 ShiftHandler::autoCapitalize）。
 * @details    组串非空→shift 关；光标 0→开（PreferLowercase 例外）；
 *             句末字符 ".!?¡¿" 后跟空格→再开大写；autoCapitalization
 *             禁止集（NoAutoUppercase|UppercaseOnly|LowercaseOnly|
 *             EmailCharactersOnly|UrlCharactersOnly|DialableCharacters
 *             Only|FormattedNumbersOnly|DigitsOnly 或 Pinyin 系模式）
 *             时不动。变化发对应信号。
 * @param      self 上下文对象；可为 NULL。
 * @param      cursorPosition 当前光标位置。
 * @param      composing 是否组串中。
 */
void XVirtualKeyboardInputContext_autoCapitalize(
        XVirtualKeyboardInputContext* self, int cursorPosition,
        bool composing);

/**
 * @brief      引擎注册表静态访问（工厂注册/创建走进程单例引擎；语言
 *             插件 TU 调用）。
 * @return     进程单例上下文的引擎借用指针；未初始化返回 NULL。
 */
XVirtualKeyboardInputEngine* XVirtualKeyboardInputContext_instanceEngine(
        void);

#endif /* XVIRTUALKEYBOARD_ON */
#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDINPUTCONTEXT_PROTECTED_H */
