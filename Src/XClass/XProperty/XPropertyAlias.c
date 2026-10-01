/**
 * @file       XPropertyAlias.c
 * @brief      属性别名实现（对标 QPropertyAlias）。
 */
#include "XPropertyAlias.h"
#if XPROPERTY_ON

#include <string.h>
#include "XObjectBindableProperty.h"
#include "XProperty_Protected.h"

/**
 * @brief 把别名锚定到最终属性数据（构造共用路径；别名不可重定向）。
 * @param self 目标别名指针，不能为 NULL。
 * @param data 最终属性数据；NULL 使别名无效。
 * @return 别名有效返回 true。
 * @note 重复 init 前必须先 deinit（init/deinit 成对原则）。
 */
static bool XPropertyAlias_attachTo(XPropertyAlias* self, XPropertyData* data)
{
    self->m_finalData = data;
    if (!data) return false;
    self->m_observer.m_kind = XPropertyObserverKind_AliasedProperty;
    self->m_observer.m_binding = NULL;
    self->m_observer.m_callback = NULL;
    self->m_observer.m_user = NULL;
    XPropertyObserver_attach(&self->m_observer, data);
    return true;
}

void XPropertyAlias_init(XPropertyAlias* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XPropertyAlias));
    XPropertyObserver_init(&self->m_observer);
    self->m_finalData = NULL;
}

bool XPropertyAlias_init_ex(XPropertyAlias* self, const XUntypedBindable* bindable)
{
    XPropertyAlias_init(self);
    if (!self || !bindable) return false;
    return XPropertyAlias_attachTo(self, bindable->m_data);
}

bool XPropertyAlias_init_ex_2(XPropertyAlias* self, const XProperty* property)
{
    XPropertyAlias_init(self);
    if (!self || !property) return false;
    return XPropertyAlias_attachTo(self, &property->m_data);
}

bool XPropertyAlias_init_ex_3(XPropertyAlias* self, const XObjectBindableProperty* property)
{
    XPropertyAlias_init(self);
    if (!self || !property) return false;
    return XPropertyAlias_attachTo(self, &property->m_data);
}

bool XPropertyAlias_init_ex_4(XPropertyAlias* self, const XPropertyAlias* other)
{
    XPropertyAlias_init(self);
    if (!self || !other) return false;
    /* 链条收敛：别名套别名时直接锚定同一最终属性数据 */
    return XPropertyAlias_attachTo(self, other->m_finalData);
}

void XPropertyAlias_deinit(XPropertyAlias* self)
{
    if (!self) return;
    XPropertyObserver_unlink(&self->m_observer);
    self->m_finalData = NULL;
}

bool XPropertyAlias_isValid(const XPropertyAlias* self)
{
    return self && self->m_finalData != NULL;
}

const XVariant* XPropertyAlias_value_const(XPropertyAlias* self)
{
    if (!XPropertyAlias_isValid(self)) return NULL;
    return XPropertyData_value_const(self->m_finalData);
}

void XPropertyAlias_setValue(XPropertyAlias* self, const XVariant* v)
{
    if (!XPropertyAlias_isValid(self)) return;
    if (self->m_finalData->m_interface && self->m_finalData->m_interface->m_setValue)
        self->m_finalData->m_interface->m_setValue(self->m_finalData, v);
}

bool XPropertyAlias_hasBinding(const XPropertyAlias* self)
{
    return XPropertyAlias_binding_const(self) != NULL;
}

XPropertyBinding* XPropertyAlias_binding_const(const XPropertyAlias* self)
{
    if (!XPropertyAlias_isValid(self)) return NULL;
    if (self->m_finalData->m_interface && self->m_finalData->m_interface->m_getBinding)
        return self->m_finalData->m_interface->m_getBinding(self->m_finalData);
    return self->m_finalData->m_binding;
}

void XPropertyAlias_setBinding(XPropertyAlias* self, XPropertyBinding* binding)
{
    if (!XPropertyAlias_isValid(self))
    {
        XPropertyBinding_unref(binding);
        return;
    }
    if (self->m_finalData->m_interface && self->m_finalData->m_interface->m_setBinding)
        self->m_finalData->m_interface->m_setBinding(self->m_finalData, binding);
    else
        XPropertyData_setBinding(self->m_finalData, binding);
}

XPropertyBinding* XPropertyAlias_takeBinding(XPropertyAlias* self)
{
    if (!XPropertyAlias_isValid(self)) return NULL;
    /* 同门面：take 语义走 XPropertyData_takeBinding，避免 setBinding(NULL) 销毁绑定 */
    return XPropertyData_takeBinding(self->m_finalData);
}

bool XPropertyAlias_subscribe(XPropertyAlias* self, XPropertyObserver* observer)
{
    if (!XPropertyAlias_isValid(self)) return false;
    return XPropertyData_subscribe(self->m_finalData, observer);
}

XPropertyObserver* XPropertyAlias_onValueChanged(XPropertyAlias* self, XPropertyObserverCallback callback, void* user)
{
    if (!XPropertyAlias_isValid(self)) return NULL;
    return XPropertyData_onValueChanged(self->m_finalData, callback, user);
}

XPropertyData* XPropertyAlias_aliasedData_const(const XPropertyAlias* self)
{
    return self ? self->m_finalData : NULL;
}

#endif /* XPROPERTY_ON */
