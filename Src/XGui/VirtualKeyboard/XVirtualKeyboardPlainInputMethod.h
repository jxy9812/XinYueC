/**
 * @file       XVirtualKeyboardPlainInputMethod.h
 * @brief      XVirtualKeyboardPlainInputMethod 拉丁直通输入法插件
 *             （对标 Qt VK plaininputmethod：defaultInputMethod 回落）。
 * @details    语言插件形态：继承 XVirtualKeyboardAbstractInputMethod，
 *             纯虚四件注册实现——inputModes={Latin}；setInputMode 仅接
 *             受 Latin；setTextCase 接受并应用；keyEvent 恒 false（直
 *             通：字符键由面板既有写入链承载，插件不消费）。结构体扩
 *             展继承（m_base 后无自有字段）。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDPLAININPUTMETHOD_H
#define XVIRTUALKEYBOARDPLAININPUTMETHOD_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XVirtualKeyboardAbstractInputMethod.h"

#if XVIRTUALKEYBOARD_ON

/** @brief 声明 XVirtualKeyboardPlainInputMethod 虚函数枚举：继承
 *         XVirtualKeyboardAbstractInputMethod（重载基类槽位，无新增）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardPlainInputMethod)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardPlainInputMethod,
                         XVirtualKeyboardAbstractInputMethod)

/**
 * @brief      拉丁直通输入法对象；m_class 必须为第一个成员。
 * @details    结构体扩展继承：首成员为基类，无自有字段。
 */
typedef struct XVirtualKeyboardPlainInputMethod
{
    XVirtualKeyboardAbstractInputMethod m_base; /**< 基类成员；必须是第一个。 */
} XVirtualKeyboardPlainInputMethod;

/** @brief 初始化类虚函数表并返回共享表指针。 */
XVtable* XVirtualKeyboardPlainInputMethod_class_init(void);

/**
 * @brief      初始化（栈对象路径）。
 * @param      self 待初始化对象。
 */
void XVirtualKeyboardPlainInputMethod_init(
        XVirtualKeyboardPlainInputMethod* self);

/**
 * @brief      使用默认内存类型创建。
 * @return     新对象指针（基类指针形态，装配面通用）；失败返回 NULL。
 */
#define XVirtualKeyboardPlainInputMethod_create() \
    XVirtualKeyboardPlainInputMethod_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型创建。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XVirtualKeyboardPlainInputMethod*
XVirtualKeyboardPlainInputMethod_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放资源。 */
#define XVirtualKeyboardPlainInputMethod_deinit_base(self) \
    XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上对象。 */
#define XVirtualKeyboardPlainInputMethod_delete_base(self) \
    XClass_delete_base((XClass*)(self))

/**
 * @brief      locale 工厂（注册表签名；返回基类指针形态新实例）。
 * @return     新建输入法实例；失败返回 NULL。
 */
XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardPlainInputMethod_factory(void);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDPLAININPUTMETHOD_H */
