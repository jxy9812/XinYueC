/**
 * @file       XObjectBindableProperty_Protected.h
 * @brief      对象绑定属性受保护接口（对标 Qt protected 的 addObserver）。
 * @details    仅供子类与模块内部实现使用，不对外公开；外部代码禁止 include
 *             本头文件。
 */
#ifndef XOBJECTBINDABLEPROPERTY_PROTECTED_H
#define XOBJECTBINDABLEPROPERTY_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XPROPERTY_ON

#include "XObjectBindableProperty.h"

/**
 * @brief 直接挂接观察者（对标 Qt protected QObjectBindableProperty::addObserver）。
 * @param self     目标属性指针；NULL 返回 false。
 * @param observer 观察者节点；节点归调用方所有。
 * @return 挂接成功返回 true。
 * @note Qt 中为 protected：一般经门面 subscribe/onValueChanged 订阅；本入口
 *       供子类内部挂钩（如派生类联动）使用。
 */
bool XObjectBindableProperty_addObserver(XObjectBindableProperty* self, XPropertyObserver* observer);

/**
 * @brief 对象绑定属性的行为接口实例（模块内部使用）。
 * @return 接口借用指针；值变化钩子发射属主的 notify 信号。
 */
const XBindableInterface* XObjectBindableProperty_interface(void);

#endif /* XPROPERTY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XOBJECTBINDABLEPROPERTY_PROTECTED_H */
