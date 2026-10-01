/**
 * @file       XBindable.h
 * @brief      可绑定门面公开 API（对标 Qt 6.8 QUntypedBindable / QBindable&lt;T&gt; 与
 *             QtPrivate::QBindableInterface）。
 * @details    门面 = 属性数据指针 + 行为接口。独立属性（XProperty）与对象绑定
 *             属性（XObjectBindableProperty）共用同一数据载体（XPropertyData），
 *             差异全部收敛在接口里：对象属性的写入/值变化会经属主 XObject 发出
 *             notify 信号，独立属性则只走观察者链表。绑定/观察者/别名一律通过
 *             门面或直接通过属性数据操作，无需区分属性种类。
 *             实现只依赖 XinYueC 抽象层，禁止调用平台 API。
 */
#ifndef XBINDABLE_H
#define XBINDABLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XPROPERTY_ON

#include <stdbool.h>
#include <stddef.h>
#include "XVariant.h"
#include "XProperty.h"
#include "XPropertyBinding.h"
#include "XPropertyObserver.h"

typedef struct XObjectBindableProperty XObjectBindableProperty; /**< 对象绑定属性（定义见 XObjectBindableProperty.h）。 */

/* ==================== 行为接口（对标 QtPrivate::QBindableInterface） ==================== */

/**
 * @brief 属性行为接口（对标 QBindableInterface；每种属性类型一个静态实例）。
 * @note Qt 的接口含 metaType 与观察器工厂槽位；C 侧类型经 XVariant 动态承载、
 *       观察者挂接在数据载体上统一完成，故收敛为以下槽位。函数指针均不得为 NULL。
 */
typedef struct XBindableInterface
{
    const XVariant* (*m_getValue)(XPropertyData* data);      /**< 读取值（求值+依赖捕获后返回借用）。 */
    void (*m_setValue)(XPropertyData* data, const XVariant* v); /**< 写入值（显式赋值语义：断开绑定+比较+通知）。 */
    void (*m_notifyChanged)(XPropertyData* data);            /**< 值实际变化后的钩子（对象属性发 notify 信号；独立属性为空操作）。 */
    XPropertyBinding* (*m_getBinding)(const XPropertyData* data); /**< 查询当前绑定（借用）。 */
    void (*m_setBinding)(XPropertyData* data, XPropertyBinding* binding); /**< 设置绑定（接管所有权）。 */
} XBindableInterface;

/* ==================== 类型擦除门面（对标 QUntypedBindable） ==================== */

/**
 * @brief 类型擦除的可绑定门面（对标 QUntypedBindable）。
 * @note m_data 为 NULL 表示无效门面（对标默认构造）。C 无模板，QBindable&lt;T&gt;
 *       与其同构，共用本类型（见下方 typedef）。
 */
typedef struct XUntypedBindable
{
    XPropertyData* m_data;                   /**< 被封装的属性数据；无效门面为 NULL。 */
    const XBindableInterface* m_interface;   /**< 行为接口；无效门面为 NULL。 */
} XUntypedBindable;

/** @brief 类型化门面（对标 QBindable&lt;T&gt;；C 无模板，与 XUntypedBindable 同构）。 */
typedef XUntypedBindable XBindable;

/**
 * @brief 初始化为无效门面（对标 QUntypedBindable() 默认构造）。
 * @param self 目标门面指针，不能为 NULL。
 * @return 无返回值。
 */
void XUntypedBindable_init(XUntypedBindable* self);

/**
 * @brief 初始化为指向指定属性数据的门面（对标 protected 构造 QUntypedBindable(data, iface)）。
 * @param self      目标门面指针，不能为 NULL。
 * @param data      属性数据；可为 NULL（等价无效门面）。
 * @param interface 行为接口；data 非 NULL 时不能为 NULL。
 * @return 无返回值。
 */
void XUntypedBindable_init_ex(XUntypedBindable* self, XPropertyData* data, const XBindableInterface* interface);

/**
 * @brief 查询门面是否有效（对标 QUntypedBindable::isValid()）。
 * @param self 目标门面指针。
 * @return 已挂接属性数据且接口可用返回 true。
 */
bool XUntypedBindable_isValid(const XUntypedBindable* self);

/** @brief 查询是否存在绑定（对标 QUntypedBindable::hasBinding()）。 */
bool XUntypedBindable_hasBinding(const XUntypedBindable* self);

/**
 * @brief 查询当前绑定（对标 QUntypedBindable::binding()）。
 * @param self 目标门面指针。
 * @return 绑定借用指针；无效门面或无绑定返回 NULL。禁止 unref。
 */
XPropertyBinding* XUntypedBindable_binding_const(const XUntypedBindable* self);

/**
 * @brief 设置绑定（对标 QUntypedBindable::setBinding()；接管绑定所有权）。
 * @param self    目标门面指针；无效门面不执行任何操作（Qt 同样忽略）。
 * @param binding 绑定对象；所有权转移给属性，传入后禁止 unref；可为 NULL（清除）。
 * @return 无返回值。
 */
void XUntypedBindable_setBinding(XUntypedBindable* self, XPropertyBinding* binding);

/**
 * @brief 用求值函数设置绑定（对标 setBinding(Functor) 便捷形式）。
 * @param self     目标门面指针；无效门面返回 NULL。
 * @param eval     求值函数；不能为 NULL。
 * @param user     求值函数用户数据；可为 NULL，仅借用。
 * @param location 源码位置；可为 NULL。
 * @return 新建绑定指针；归属性所有，禁止 unref；失败返回 NULL。
 */
XPropertyBinding* XUntypedBindable_setBinding_eval(XUntypedBindable* self, XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location);

/**
 * @brief 取出绑定（对标 QUntypedBindable::takeBinding()）；所有权移交调用方。
 * @param self 目标门面指针。
 * @return 绑定指针（调用方用完 XPropertyBinding_unref）；无效门面或无绑定返回 NULL。
 */
XPropertyBinding* XUntypedBindable_takeBinding(XUntypedBindable* self);

/**
 * @brief 订阅属性变化（观察者归调用方持有；对标 QUntypedBindable::subscribe）。
 * @param self     目标门面指针。
 * @param observer 观察者节点；节点归调用方所有（可嵌入），须用 setCallback/setHandler 设置行为。
 * @return 挂接成功返回 true；无效门面返回 false。
 */
bool XUntypedBindable_subscribe(XUntypedBindable* self, XPropertyObserver* observer);

/**
 * @brief 以堆分配观察者订阅值变化（对标 QUntypedBindable::onValueChanged）。
 * @param self     目标门面指针。
 * @param callback 值变化回调；可为 NULL。
 * @param user     回调用户数据；可为 NULL，仅借用。
 * @return 新建观察者指针，归调用方所有，用 XPropertyObserver_delete 注销；失败返回 NULL。
 */
XPropertyObserver* XUntypedBindable_onValueChanged(XUntypedBindable* self, XPropertyObserverCallback callback, void* user);

/* ==================== 类型化门面（对标 QBindable<T>） ==================== */

/**
 * @brief 封装独立属性（对标 QBindable&lt;T&gt;(QProperty&lt;T&gt;*)）。
 * @param self     目标门面指针，不能为 NULL。
 * @param property 目标属性；可为 NULL（等价无效门面）。
 * @return 无返回值。
 */
void XBindable_init_property(XBindable* self, XProperty* property);

/**
 * @brief 封装对象绑定属性（对标 QBindable 绑定 QObjectBindableProperty 的用法）。
 * @param self     目标门面指针，不能为 NULL。
 * @param property 目标对象绑定属性；可为 NULL（等价无效门面）。
 * @return 无返回值。
 */
void XBindable_init_objectProperty(XBindable* self, XObjectBindableProperty* property);

/**
 * @brief 读取值（求值+依赖捕获后返回借用；对标 QBindable::value()）。
 * @param self 目标门面指针。
 * @return 内部值借用指针；无效门面返回 NULL。禁止释放或修改。
 */
const XVariant* XBindable_value_const(XBindable* self);

/**
 * @brief 读取值并深拷贝到输出变体。
 * @param self 目标门面指针。
 * @param out  输出变体；可为未初始化对象，调用方负责 deinit。
 * @return 成功返回 true；无效门面或失败返回 false。
 */
bool XBindable_value(XBindable* self, XVariant* out);

/**
 * @brief 写入值（对标 QBindable::setValue()；经接口分派，对象属性会发 notify 信号）。
 * @param self 目标门面指针；无效门面不执行任何操作。
 * @param v    新值变体；借用，可为 NULL。
 * @return 无返回值。
 */
void XBindable_setValue(XBindable* self, const XVariant* v);

#endif /* XPROPERTY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XBINDABLE_H */
