/**
 * @file       XPropertyObserver.h
 * @brief      属性观察者公开 API（对标 Qt 6.8 QPropertyObserver / QPropertyChangeHandler）。
 * @details    观察者是一个可嵌入的侵入式链表节点：观察者被 attach 到属性数据
 *             （XPropertyData）的观察者链表上，属性值实际变化或依赖它的绑定被
 *             标记失效时收到通知。三种用法：
 *             1. 普通回调（setCallback，对标 QPropertyObserver::setCallback）；
 *             2. 变更处理器（setHandler，对标 QPropertyChangeHandler 的
 *                ChangeHandler；返回 true 时自动注销订阅）；
 *             3. 别名锚点/绑定依赖节点（模块内部使用，见 XPropertyAlias 与
 *                XProperty_Protected.h）。
 *             节点内存归创建方所有：可嵌入调用者结构体（init/deinit 管理链表
 *             挂接），或经 create/delete 堆分配。属性销毁时只把节点从链表
 *             解挂，不释放节点内存。
 *             实现只依赖 XinYueC 抽象层，禁止调用平台 API。
 */
#ifndef XPROPERTYOBSERVER_H
#define XPROPERTYOBSERVER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XPROPERTY_ON

#include <stdbool.h>
#include <stddef.h>
#include "XVariant.h"

typedef struct XPropertyData XPropertyData;     /**< 属性数据载体（定义见 XProperty.h）。 */
typedef struct XProperty XProperty;             /**< 独立属性对象（定义见 XProperty.h）。 */
typedef struct XPropertyBinding XPropertyBinding; /**< 绑定对象（定义见 XPropertyBinding.h）。 */
typedef struct XUntypedBindable XUntypedBindable; /**< 可绑定门面（定义见 XBindable.h）。 */

/**
 * @brief 观察者节点类别（对标 QPropertyObserver 的 ObserverTag）。
 */
typedef enum XPropertyObserverKind
{
    XPropertyObserverKind_Invalid = 0,           /**< 未挂接/未设置行为。 */
    XPropertyObserverKind_Observer = 1,          /**< 普通回调节点（值变化时调用 callback）。 */
    XPropertyObserverKind_ChangeHandler = 2,     /**< 变更处理器节点（返回 true 自动注销）。 */
    XPropertyObserverKind_AliasedProperty = 3,   /**< 别名锚点节点（XPropertyAlias 内部使用；通知时跳过）。 */
    XPropertyObserverKind_BindingDependency = 4  /**< 绑定依赖节点（模块内部使用；值变化时标脏属主绑定）。 */
} XPropertyObserverKind;

/**
 * @brief 值变化回调类型（对标 QPropertyObserverCallback）。
 * @param user 注册时传入的用户数据指针。
 * @return 无返回值。回调内可安全读取任意属性（会触发惰性重算）。
 */
typedef void (*XPropertyObserverCallback)(void* user);

/**
 * @brief 变更处理器类型（对标 QPropertyChangeHandler 的 ChangeHandler）。
 * @param user     注册时传入的用户数据指针。
 * @param newValue 属性值借用指针：直接写入路径为新值；依赖级联路径为当前
 *                 （可能尚未重算）值，回调内重新读取该属性可得到重算后的新值。
 * @return 返回 true 表示处理一次后自动注销订阅（节点解挂，内存仍归创建方）；
 *         返回 false 继续订阅。
 */
typedef bool (*XPropertyChangeHandler)(void* user, const XVariant* newValue);

/**
 * @brief 属性观察者节点（对标 QPropertyObserver）。
 * @note 可嵌入调用者结构体；节点同时挂在属性的观察者链表（双向）与属主
 *       绑定的依赖链表（单向，仅依赖节点使用）上。字段仅供模块内部使用，
 *       调用方禁止直接修改。
 */
typedef struct XPropertyObserver
{
    struct XPropertyObserver* m_next;            /**< 属性观察者链表后继；仅供内部使用。 */
    struct XPropertyObserver* m_prev;            /**< 属性观察者链表前驱；仅供内部使用。 */
    struct XPropertyObserver* m_nextDependency;  /**< 属主绑定依赖链表后继；仅供内部使用。 */
    struct XPropertyData* m_propertyData;        /**< 当前被观察的属性数据；未挂接为 NULL。仅供内部使用。 */
    struct XPropertyBinding* m_binding;          /**< 属主绑定（依赖节点）；独立观察者为 NULL。仅供内部使用。 */
    XPropertyObserverKind m_kind;                /**< 节点类别；仅供内部使用。 */
    union
    {
        XPropertyObserverCallback m_callback;          /**< 普通回调（kind=Observer）。 */
        XPropertyChangeHandler m_changeHandler;        /**< 变更处理器（kind=ChangeHandler）。 */
    };
    void* m_user;                                /**< 回调/处理器用户数据；仅供内部使用。 */
} XPropertyObserver;

/**
 * @brief 初始化观察者节点（未挂接、无行为；对标 QPropertyObserver() 默认构造）。
 * @param self 目标节点指针，不能为 NULL。
 * @return 无返回值。
 */
void XPropertyObserver_init(XPropertyObserver* self);

/**
 * @brief 反初始化观察者节点；若已挂接则先从属性观察者链表解挂。
 * @param self 目标节点指针；NULL 安全。
 * @return 无返回值。不释放节点内存（嵌入节点由拥有者继续管理）。
 */
void XPropertyObserver_deinit(XPropertyObserver* self);

/**
 * @brief 堆上创建观察者节点（对标 new QPropertyObserver）。
 * @return 新节点指针；分配失败返回 NULL。用 XPropertyObserver_delete 释放。
 */
XPropertyObserver* XPropertyObserver_create(void);

/**
 * @brief 删除堆上观察者节点；内部先解挂再释放内存。
 * @param self 目标节点指针；NULL 安全。
 * @return 无返回值。调用后不得再使用 self。
 */
void XPropertyObserver_delete(XPropertyObserver* self);

/**
 * @brief 设置普通值变化回调（对标 QPropertyObserver::setCallback）。
 * @param self 目标节点指针；NULL 不执行任何操作。
 * @param callback 回调函数；可为 NULL（清空行为，通知时跳过本节点）。
 * @param user     回调用户数据；可为 NULL，仅借用。
 * @return 无返回值。设置后节点类别为 Observer；对绑定依赖/别名锚点节点无效。
 */
void XPropertyObserver_setCallback(XPropertyObserver* self, XPropertyObserverCallback callback, void* user);

/**
 * @brief 设置变更处理器（对标 QPropertyChangeHandler；处理器返回 true 自动注销）。
 * @param self     目标节点指针；NULL 不执行任何操作。
 * @param handler  处理器函数；可为 NULL（清空行为）。
 * @param user     处理器用户数据；可为 NULL，仅借用。
 * @return 无返回值。设置后节点类别为 ChangeHandler；对绑定依赖/别名锚点节点无效。
 */
void XPropertyObserver_setHandler(XPropertyObserver* self, XPropertyChangeHandler handler, void* user);

/**
 * @brief 观察指定属性（对标 QPropertyObserver::observe(QProperty&lt;T&gt;*)）。
 * @param self     目标节点指针，不能为 NULL。
 * @param property 目标属性；NULL 等价于解挂。
 * @return 挂接成功返回 true；属性数据为空或内存不足返回 false。
 * @note 已挂接的节点会被先解挂再重新挂接（支持改投）；节点必须已通过
 *       setCallback/setHandler 设置行为，否则挂接后通知会跳过它。
 */
bool XPropertyObserver_observe_property(XPropertyObserver* self, XProperty* property);

/**
 * @brief 观察指定可绑定对象（对标 QPropertyObserver::observe(QUntypedBindable)）。
 * @param self     目标节点指针，不能为 NULL。
 * @param bindable 可绑定门面；无效（未挂接属性数据）时等价于解挂。
 * @return 挂接成功返回 true；否则返回 false。
 */
bool XPropertyObserver_observe(XPropertyObserver* self, const XUntypedBindable* bindable);

#endif /* XPROPERTY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPROPERTYOBSERVER_H */
