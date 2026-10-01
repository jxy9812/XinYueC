/**
 * @file       XObjectBindableProperty.h
 * @brief      对象绑定属性公开 API（对标 Qt 6.8 QObjectBindableProperty）。
 * @details    嵌入 XObject 派生类的可绑定属性：与 XProperty 共用数据载体与绑定
 *             机制，差异在值实际变化时经属主对象发出 notify 信号（对标模板参数
 *             QPropertyChangedSignal 指定的 changed 信号），使既有信号槽驱动的
 *             界面代码无需改造即可接入数据绑定。
 *             notify 信号由使用方按自身信号规范声明（如 void XFoo_valueChanged_
 *             signal(XFoo* self)），把其信号标识（XSignal(...) 的值）传给 init。
 *             信号以无参数形式同步发射（对标 Qt 无参 changed 通知信号）；
 *             依赖变化的级联路径同样会触发发射（属性被标脏即视为"可能已变化"，
 *             槽内重读属性得到重算后的新值）。
 *             m_data 必须保持为首成员：接口回调按成员偏移回推容器指针。
 *             实现只依赖 XinYueC 抽象层，禁止调用平台 API。
 */
#ifndef XOBJECTBINDABLEPROPERTY_H
#define XOBJECTBINDABLEPROPERTY_H
#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XPROPERTY_ON

#include <stdbool.h>
#include <stddef.h>
#include "XObject.h"
#include "XVariant.h"
#include "XProperty.h"
#include "XPropertyBinding.h"
#include "XPropertyObserver.h"
#include "XBindable.h"

/**
 * @brief 对象绑定属性（对标 QObjectBindableProperty）。
 * @note 可嵌入 XObject 派生结构体；字段仅供模块内部使用，读写一律走下方函数。
 */
typedef struct XObjectBindableProperty
{
    XPropertyData m_data;   /**< 属性数据载体；必须保持为首成员（接口回推依赖）。 */
    XObject* m_object;      /**< 属主对象（弱引用，不增减引用计数；对标模板参数 Class*）。 */
    size_t m_notifySignal;  /**< notify 信号标识（对标 QPropertyChangedSignal 模板参数）；0 表示不发射。 */
} XObjectBindableProperty;

/**
 * @brief 初始化对象绑定属性（NULL 变体、无绑定；对标 QObjectBindableProperty(owner, signal)）。
 * @param self   目标属性指针，不能为 NULL。
 * @param object 属主对象；借用，可为 NULL（不发信号，退化为独立属性行为）。
 * @param signal notify 信号标识；0 表示不发射。
 * @return 无返回值。
 */
void XObjectBindableProperty_init(XObjectBindableProperty* self, XObject* object, size_t signal);

/**
 * @brief 初始化对象绑定属性并设置初值（对标 QObjectBindableProperty(owner, signal, initialValue)）。
 * @param self         目标属性指针，不能为 NULL。
 * @param object       属主对象；借用，可为 NULL。
 * @param signal       notify 信号标识；0 表示不发射。
 * @param initialValue 初值变体；借用，可为 NULL（等价 init）。内部深拷贝。
 * @return 无返回值。
 */
void XObjectBindableProperty_init_ex(XObjectBindableProperty* self, XObject* object, size_t signal, const XVariant* initialValue);

/**
 * @brief 反初始化对象绑定属性；解挂观察者、解除绑定并释放值；不发射信号。
 * @param self 目标属性指针；NULL 安全。
 * @return 无返回值。与 init 成对使用。
 */
void XObjectBindableProperty_deinit(XObjectBindableProperty* self);

/**
 * @brief 读取属性值（求值后借用指针；对标 value()）。
 * @param self 目标属性指针。
 * @return 内部值借用指针；NULL 返回 NULL。禁止释放或修改。
 */
const XVariant* XObjectBindableProperty_value_const(XObjectBindableProperty* self);

/**
 * @brief 读取属性值并深拷贝到输出变体。
 * @param self 目标属性指针。
 * @param out  输出变体；可为未初始化对象，调用方负责 deinit。
 * @return 成功返回 true；失败返回 false。
 */
bool XObjectBindableProperty_value(XObjectBindableProperty* self, XVariant* out);

/**
 * @brief 写入属性值（对标 setValue()；显式赋值先移除既有绑定，变化时发 notify 信号）。
 * @param self 目标属性指针；NULL 不执行任何操作。
 * @param v    新值变体；借用，可为 NULL。内部深拷贝。
 * @return 无返回值。
 */
void XObjectBindableProperty_setValue(XObjectBindableProperty* self, const XVariant* v);

/**
 * @brief 写入属性值（移动语义；显式赋值语义同上）。
 * @param self 目标属性指针；NULL 不执行任何操作。
 * @param v    新值变体；资源所有权转移到属性；调用方仍需 deinit 空源对象。
 * @return 无返回值。
 */
void XObjectBindableProperty_setValue_move(XObjectBindableProperty* self, XVariant* v);

/** @brief 查询是否存在绑定（对标 hasBinding()）。 */
bool XObjectBindableProperty_hasBinding(const XObjectBindableProperty* self);

/** @brief 查询当前绑定借用指针（对标 binding()）；无绑定返回 NULL，禁止 unref。 */
XPropertyBinding* XObjectBindableProperty_binding_const(const XObjectBindableProperty* self);

/**
 * @brief 设置绑定（对标 setBinding()；接管绑定所有权，传入后禁止 unref）。
 * @param self    目标属性指针；NULL 不执行任何操作。
 * @param binding 绑定对象；可为 NULL（清除绑定）。
 * @return 无返回值。惰性求值；依赖变化级联时发 notify 信号。
 */
void XObjectBindableProperty_setBinding(XObjectBindableProperty* self, XPropertyBinding* binding);

/**
 * @brief 用求值函数设置绑定（对标 setBinding(Functor)）。
 * @param self     目标属性指针；NULL 返回 NULL。
 * @param eval     求值函数；不能为 NULL。
 * @param user     求值函数用户数据；可为 NULL，仅借用。
 * @param location 源码位置；可为 NULL。
 * @return 新建绑定指针；归属性所有，禁止 unref；失败返回 NULL。
 */
XPropertyBinding* XObjectBindableProperty_setBinding_eval(XObjectBindableProperty* self, XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location);

/**
 * @brief 取出绑定（对标 takeBinding()）；所有权移交调用方；无绑定返回 NULL。
 */
XPropertyBinding* XObjectBindableProperty_takeBinding(XObjectBindableProperty* self);

/**
 * @brief 订阅属性变化（观察者归调用方持有；对标门面 subscribe）。
 * @param self     目标属性指针；NULL 返回 false。
 * @param observer 观察者节点；节点归调用方所有（可嵌入）。
 * @return 挂接成功返回 true。
 */
bool XObjectBindableProperty_subscribe(XObjectBindableProperty* self, XPropertyObserver* observer);

/**
 * @brief 以堆分配观察者订阅值变化（对标门面 onValueChanged）。
 * @param self     目标属性指针。
 * @param callback 值变化回调；可为 NULL。
 * @param user     回调用户数据；可为 NULL，仅借用。
 * @return 新建观察者指针，归调用方所有，用 XPropertyObserver_delete 注销；失败返回 NULL。
 */
XPropertyObserver* XObjectBindableProperty_onValueChanged(XObjectBindableProperty* self, XPropertyObserverCallback callback, void* user);

/**
 * @brief 获取门面视图（对标取 QBindable 观察同一属性的用法）。
 * @param self 目标属性指针。
 * @param out  输出门面；不能为 NULL。
 * @return 无返回值。
 */
void XObjectBindableProperty_bindable(XObjectBindableProperty* self, XBindable* out);

#endif /* XPROPERTY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XOBJECTBINDABLEPROPERTY_H */
