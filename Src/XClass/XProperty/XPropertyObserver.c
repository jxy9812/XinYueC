/**
 * @file       XPropertyObserver.c
 * @brief      属性观察者实现（对标 Qt QPropertyObserver 内部机制）。
 */
#include "XPropertyObserver.h"
#if XPROPERTY_ON

#include <string.h>
#include "XMemory.h"
#include "XBindable.h"
#include "XProperty.h"
#include "XProperty_Protected.h"

/* ==================== 生命周期 ==================== */

void XPropertyObserver_init(XPropertyObserver* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XPropertyObserver));
    self->m_kind = XPropertyObserverKind_Invalid;
}

void XPropertyObserver_deinit(XPropertyObserver* self)
{
    if (!self) return;
    XPropertyObserver_unlink(self);
}

XPropertyObserver* XPropertyObserver_create(void)
{
    XPropertyObserver* self = (XPropertyObserver*)XNew(XPropertyObserver);
    if (!self) return NULL;
    XPropertyObserver_init(self);
    return self;
}

void XPropertyObserver_delete(XPropertyObserver* self)
{
    if (!self) return;
    XPropertyObserver_deinit(self);
    XFree_System(self);
}

/* ==================== 行为设置 ==================== */

void XPropertyObserver_setCallback(XPropertyObserver* self, XPropertyObserverCallback callback, void* user)
{
    if (!self) return;
    /* 绑定依赖/别名锚点节点由模块内部管理，禁止外部改写行为 */
    if (self->m_kind == XPropertyObserverKind_BindingDependency ||
        self->m_kind == XPropertyObserverKind_AliasedProperty)
        return;
    self->m_callback = callback;
    self->m_user = user;
    self->m_kind = XPropertyObserverKind_Observer;
}

void XPropertyObserver_setHandler(XPropertyObserver* self, XPropertyChangeHandler handler, void* user)
{
    if (!self) return;
    if (self->m_kind == XPropertyObserverKind_BindingDependency ||
        self->m_kind == XPropertyObserverKind_AliasedProperty)
        return;
    self->m_changeHandler = handler;
    self->m_user = user;
    self->m_kind = XPropertyObserverKind_ChangeHandler;
}

/* ==================== 挂接 ==================== */

bool XPropertyObserver_observe_property(XPropertyObserver* self, XProperty* property)
{
    if (!self) return false;
    XPropertyObserver_unlink(self);
    if (!property) return false;
    XPropertyObserver_attach(self, &property->m_data);
    return true;
}

bool XPropertyObserver_observe(XPropertyObserver* self, const XUntypedBindable* bindable)
{
    if (!self) return false;
    XPropertyObserver_unlink(self);
    if (!bindable || !bindable->m_data) return false;
    XPropertyObserver_attach(self, bindable->m_data);
    return true;
}

/* ==================== 内部链表操作（见 XProperty_Protected.h） ==================== */

void XPropertyObserver_attach(XPropertyObserver* node, XPropertyData* data)
{
    if (!node || !data) return;
    node->m_prev = NULL;
    node->m_next = data->m_firstObserver;
    if (data->m_firstObserver)
        data->m_firstObserver->m_prev = node;
    data->m_firstObserver = node;
    node->m_propertyData = data;
}

void XPropertyObserver_unlink(XPropertyObserver* node)
{
    if (!node || !node->m_propertyData) return;
    XPropertyData* data = node->m_propertyData;
    if (node->m_prev)
        node->m_prev->m_next = node->m_next;
    else if (data->m_firstObserver == node)
        data->m_firstObserver = node->m_next;
    if (node->m_next)
        node->m_next->m_prev = node->m_prev;
    node->m_next = NULL;
    node->m_prev = NULL;
    node->m_propertyData = NULL;
}

void XPropertyObserver_attachDependency(XPropertyData* data, XPropertyBinding* binding)
{
    if (!data || !binding) return;
    /* 去重：同一绑定对同一属性只登记一次（求值内重复读取） */
    XPropertyObserver* it = binding->m_dependencies;
    while (it)
    {
        if (it->m_propertyData == data) return;
        it = it->m_nextDependency;
    }
    XPropertyObserver* node = (XPropertyObserver*)XNew(XPropertyObserver);
    if (!node)
    {
        /* 分配失败：该依赖在本轮求值中缺失，可能漏更新 */
        XERROR_PRINTF("XPropertyObserver_attachDependency: alloc failed\n");
        return;
    }
    XPropertyObserver_init(node);
    node->m_kind = XPropertyObserverKind_BindingDependency;
    node->m_binding = binding;
    node->m_nextDependency = binding->m_dependencies;
    binding->m_dependencies = node;
    XPropertyObserver_attach(node, data);
}

#endif /* XPROPERTY_ON */
