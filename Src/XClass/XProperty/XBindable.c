/**
 * @file       XBindable.c
 * @brief      可绑定门面实现（对标 QUntypedBindable/QBindable）。
 */
#include "XBindable.h"
#if XPROPERTY_ON

#include <string.h>
#include "XObjectBindableProperty.h"
#include "XProperty_Protected.h"
#include "XObjectBindableProperty_Protected.h"

/* ==================== 类型擦除门面 ==================== */

void XUntypedBindable_init(XUntypedBindable* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XUntypedBindable));
}

void XUntypedBindable_init_ex(XUntypedBindable* self, XPropertyData* data, const XBindableInterface* interface)
{
    if (!self) return;
    self->m_data = data;
    self->m_interface = data ? interface : NULL;
}

bool XUntypedBindable_isValid(const XUntypedBindable* self)
{
    return self && self->m_data && self->m_interface;
}

bool XUntypedBindable_hasBinding(const XUntypedBindable* self)
{
    return XUntypedBindable_binding_const(self) != NULL;
}

XPropertyBinding* XUntypedBindable_binding_const(const XUntypedBindable* self)
{
    if (!XUntypedBindable_isValid(self)) return NULL;
    return self->m_interface->m_getBinding(self->m_data);
}

void XUntypedBindable_setBinding(XUntypedBindable* self, XPropertyBinding* binding)
{
    if (!XUntypedBindable_isValid(self))
    {
        /* 无效门面无法接管所有权：释放以兑现转移承诺，对标 Qt 忽略无效目标 */
        XPropertyBinding_unref(binding);
        return;
    }
    self->m_interface->m_setBinding(self->m_data, binding);
}

XPropertyBinding* XUntypedBindable_setBinding_eval(XUntypedBindable* self, XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location)
{
    if (!eval) return NULL;
    XPropertyBinding* binding = XPropertyBinding_create_eval(eval, user, location);
    if (!binding) return NULL;
    if (!XUntypedBindable_isValid(self))
    {
        XPropertyBinding_unref(binding);
        return NULL;
    }
    self->m_interface->m_setBinding(self->m_data, binding);
    return binding;
}

XPropertyBinding* XUntypedBindable_takeBinding(XUntypedBindable* self)
{
    if (!XUntypedBindable_isValid(self)) return NULL;
    /* take 语义必须走 XPropertyData_takeBinding：所有权移交调用方；
     * 若经接口 setBinding(NULL) 清除，属性会 unref 到 0 销毁绑定，返回悬垂指针 */
    return XPropertyData_takeBinding(self->m_data);
}

bool XUntypedBindable_subscribe(XUntypedBindable* self, XPropertyObserver* observer)
{
    if (!XUntypedBindable_isValid(self)) return false;
    return XPropertyData_subscribe(self->m_data, observer);
}

XPropertyObserver* XUntypedBindable_onValueChanged(XUntypedBindable* self, XPropertyObserverCallback callback, void* user)
{
    if (!XUntypedBindable_isValid(self)) return NULL;
    return XPropertyData_onValueChanged(self->m_data, callback, user);
}

/* ==================== 类型化门面 ==================== */

void XBindable_init_property(XBindable* self, XProperty* property)
{
    if (!self) return;
    XUntypedBindable_init_ex(self, property ? &property->m_data : NULL,
                             property ? XProperty_interface() : NULL);
}

void XBindable_init_objectProperty(XBindable* self, XObjectBindableProperty* property)
{
    if (!self) return;
    XUntypedBindable_init_ex(self, property ? &property->m_data : NULL,
                             property ? XObjectBindableProperty_interface() : NULL);
}

const XVariant* XBindable_value_const(XBindable* self)
{
    if (!XUntypedBindable_isValid(self)) return NULL;
    return self->m_interface->m_getValue(self->m_data);
}

bool XBindable_value(XBindable* self, XVariant* out)
{
    if (!out) return false;
    const XVariant* value = XBindable_value_const(self);
    if (!value) return false;
    if (XClassIsVtableNull(out))
    {
        memset(out, 0, sizeof(XVariant));
        XVariant_init(out, NULL, 0, XVariantType_NULL);
    }
    XClassCopy((XClass*)out, (const XClass*)value);
    return true;
}

void XBindable_setValue(XBindable* self, const XVariant* v)
{
    if (!XUntypedBindable_isValid(self)) return;
    self->m_interface->m_setValue(self->m_data, v);
}

#endif /* XPROPERTY_ON */
