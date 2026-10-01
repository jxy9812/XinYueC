/**
 * @file       XPropertyAlias.h
 * @brief      属性别名公开 API（对标 Qt 6.8 QPropertyAlias）。
 * @details    别名是既有属性的另一个"名字"：读/写/绑定/订阅全部转发到被别名
 *             属性（构造时解析到最终属性数据，别名不可重定向）。别名可以再被
 *             别名（链条在构造时收敛到同一最终数据）。别名自身不存值——其
 *             观察者节点仅作为锚点挂在被别名属性的观察者链表上（对标
 *             QPropertyObserver 的 AliasedProperty 标记），析构时解挂。
 *             生命周期：别名不延长被别名属性的生命周期；被别名属性销毁后
 *             别名变为悬空，调用方须保证别名先于被别名属性销毁（Qt 语义相同）。
 *             实现只依赖 XinYueC 抽象层，禁止调用平台 API。
 */
#ifndef XPROPERTYALIAS_H
#define XPROPERTYALIAS_H
#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XPROPERTY_ON

#include <stdbool.h>
#include <stddef.h>
#include "XVariant.h"
#include "XProperty.h"
#include "XPropertyObserver.h"
#include "XBindable.h"

/**
 * @brief 属性别名（对标 QPropertyAlias）。
 * @note 可嵌入调用者结构体；m_observer/m_finalData 仅供模块内部使用。
 */
typedef struct XPropertyAlias
{
    XPropertyObserver m_observer;       /**< 锚点节点（kind=AliasedProperty，挂在最终属性观察者链表上）。仅供内部使用。 */
    XPropertyData* m_finalData;         /**< 解析后的最终属性数据；无效别名为 NULL。仅供内部使用。 */
} XPropertyAlias;

/**
 * @brief 初始化无效别名（对标 QPropertyAlias 默认构造的无效状态）。
 * @param self 目标别名指针，不能为 NULL。
 * @return 无返回值。
 */
void XPropertyAlias_init(XPropertyAlias* self);

/**
 * @brief 别名到可绑定门面（对标 QPropertyAlias(const QUntypedBindable&)）。
 * @param self     目标别名指针，不能为 NULL。
 * @param bindable 源门面；无效门面使别名无效。
 * @return 别名有效返回 true；源为空返回 false（别名保持无效）。
 */
bool XPropertyAlias_init_ex(XPropertyAlias* self, const XUntypedBindable* bindable);

/**
 * @brief 别名到独立属性（对标 QPropertyAlias(QProperty&lt;T&gt;*)）。
 * @param self     目标别名指针，不能为 NULL。
 * @param property 源属性；可为 NULL（别名保持无效）。
 * @return 别名有效返回 true。
 */
bool XPropertyAlias_init_ex_2(XPropertyAlias* self, const XProperty* property);

/**
 * @brief 别名到对象绑定属性。
 * @param self     目标别名指针，不能为 NULL。
 * @param property 源对象绑定属性；可为 NULL（别名保持无效）。
 * @return 别名有效返回 true。
 */
bool XPropertyAlias_init_ex_3(XPropertyAlias* self, const XObjectBindableProperty* property);

/**
 * @brief 别名到另一别名（对标 QPropertyAlias(const QPropertyAlias&)；收敛到同一最终数据）。
 * @param self  目标别名指针，不能为 NULL。
 * @param other 源别名；可为 NULL 或无效（别名保持无效）。
 * @return 别名有效返回 true。
 */
bool XPropertyAlias_init_ex_4(XPropertyAlias* self, const XPropertyAlias* other);

/**
 * @brief 反初始化别名；锚点节点从最终属性观察者链表解挂。
 * @param self 目标别名指针；NULL 安全。
 * @return 无返回值。与 init 成对使用；不释放别名自身内存（嵌入节点由拥有者管理）。
 */
void XPropertyAlias_deinit(XPropertyAlias* self);

/**
 * @brief 查询别名是否有效（对标 QPropertyAlias::isValid()）。
 * @param self 目标别名指针。
 * @return 已解析到最终属性数据返回 true。
 */
bool XPropertyAlias_isValid(const XPropertyAlias* self);

/**
 * @brief 读取被别名属性值（求值后借用；对标 QPropertyAlias::value()）。
 * @param self 目标别名指针。
 * @return 内部值借用指针；无效别名返回 NULL。禁止释放或修改。
 */
const XVariant* XPropertyAlias_value_const(XPropertyAlias* self);

/**
 * @brief 写入被别名属性值（对标 QPropertyAlias::setValue()；显式赋值语义同属性）。
 * @param self 目标别名指针；无效别名不执行任何操作。
 * @param v    新值变体；借用，可为 NULL。内部深拷贝。
 * @return 无返回值。
 */
void XPropertyAlias_setValue(XPropertyAlias* self, const XVariant* v);

/** @brief 查询被别名属性是否存在绑定（对标 QPropertyAlias::hasBinding()）。 */
bool XPropertyAlias_hasBinding(const XPropertyAlias* self);

/**
 * @brief 查询被别名属性的绑定（对标 QPropertyAlias::binding()）。
 * @param self 目标别名指针。
 * @return 绑定借用指针；无效别名或无绑定返回 NULL。禁止 unref。
 */
XPropertyBinding* XPropertyAlias_binding_const(const XPropertyAlias* self);

/**
 * @brief 设置被别名属性的绑定（对标 QPropertyAlias::setBinding()；接管所有权）。
 * @param self    目标别名指针；无效别名不执行任何操作。
 * @param binding 绑定对象；所有权转移给属性，传入后禁止 unref；可为 NULL（清除）。
 * @return 无返回值。
 */
void XPropertyAlias_setBinding(XPropertyAlias* self, XPropertyBinding* binding);

/**
 * @brief 取出被别名属性的绑定（对标 QPropertyAlias::takeBinding()）；所有权移交调用方。
 */
XPropertyBinding* XPropertyAlias_takeBinding(XPropertyAlias* self);

/**
 * @brief 订阅被别名属性变化（观察者归调用方持有；对标 QPropertyAlias::subscribe）。
 * @param self     目标别名指针。
 * @param observer 观察者节点；节点归调用方所有（可嵌入）。
 * @return 挂接成功返回 true；无效别名返回 false。
 */
bool XPropertyAlias_subscribe(XPropertyAlias* self, XPropertyObserver* observer);

/**
 * @brief 以堆分配观察者订阅被别名属性值变化（对标 QPropertyAlias::onValueChanged）。
 * @param self     目标别名指针。
 * @param callback 值变化回调；可为 NULL。
 * @param user     回调用户数据；可为 NULL，仅借用。
 * @return 新建观察者指针，归调用方所有，用 XPropertyObserver_delete 注销；失败返回 NULL。
 */
XPropertyObserver* XPropertyAlias_onValueChanged(XPropertyAlias* self, XPropertyObserverCallback callback, void* user);

/**
 * @brief 查询解析后的最终属性数据（对标 Qt protected 的 aliasedPropertyData）。
 * @param self 目标别名指针。
 * @return 属性数据借用指针；无效别名返回 NULL。禁止释放。
 */
XPropertyData* XPropertyAlias_aliasedData_const(const XPropertyAlias* self);

#endif /* XPROPERTY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPROPERTYALIAS_H */
