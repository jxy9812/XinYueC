/**
 * @file       XObjectBindableProperty.c
 * @brief      对象绑定属性实现（对标 QObjectBindableProperty + QPropertyChangedSignal）。
 * @note       m_data 必须是 XObjectBindableProperty 首成员：接口回调按指针回推容器。
 */
#include "XObjectBindableProperty.h"
#if XPROPERTY_ON

#include <string.h>
#include "XBindable.h"
#include "XObjectBindableProperty_Protected.h"

/**
 * @brief 值实际变化钩子：经属主对象发射 notify 信号（对标 QPropertyChangedSignal）。
 */
static void VXObjectBindableProperty_notifyChanged(XPropertyData* data)
{
    XObjectBindableProperty* self = (XObjectBindableProperty*)data;
    if (!self) return;
    if (self->m_object && self->m_object->m_signalSlot && self->m_notifySignal)
        XObject_emitSignal(self->m_object, self->m_notifySignal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

static const XVariant* VXObjectBindableProperty_getValue(XPropertyData* data)
{
    return XPropertyData_value_const(data);
}

static void VXObjectBindableProperty_setValue(XPropertyData* data, const XVariant* v)
{
    XPropertyData_setValue(data, v);
}

static XPropertyBinding* VXObjectBindableProperty_getBinding(const XPropertyData* data)
{
    return data ? data->m_binding : NULL;
}

static void VXObjectBindableProperty_setBinding(XPropertyData* data, XPropertyBinding* binding)
{
    XPropertyData_setBinding(data, binding);
}

static const XBindableInterface s_objectBindableInterface = {
    VXObjectBindableProperty_getValue,
    VXObjectBindableProperty_setValue,
    VXObjectBindableProperty_notifyChanged,
    VXObjectBindableProperty_getBinding,
    VXObjectBindableProperty_setBinding,
};

const XBindableInterface* XObjectBindableProperty_interface(void)
{
    return &s_objectBindableInterface;
}

/* ==================== 生命周期 ==================== */

void XObjectBindableProperty_init(XObjectBindableProperty* self, XObject* object, size_t signal)
{
    if (!self) return;
    XPropertyData_init(&self->m_data);
    self->m_data.m_interface = XObjectBindableProperty_interface();
    self->m_object = object;
    self->m_notifySignal = signal;
}

void XObjectBindableProperty_init_ex(XObjectBindableProperty* self, XObject* object, size_t signal, const XVariant* initialValue)
{
    XObjectBindableProperty_init(self, object, signal);
    if (!self || !initialValue) return;
    XClassCopy((XClass*)&self->m_data.m_value, (const XClass*)initialValue);
}

void XObjectBindableProperty_deinit(XObjectBindableProperty* self)
{
    if (!self) return;
    XPropertyData_deinit(&self->m_data);
    self->m_object = NULL;
    self->m_notifySignal = 0;
}

/* ==================== 读写与绑定（委托属性数据载体） ==================== */

const XVariant* XObjectBindableProperty_value_const(XObjectBindableProperty* self)
{
    return self ? XPropertyData_value_const(&self->m_data) : NULL;
}

bool XObjectBindableProperty_value(XObjectBindableProperty* self, XVariant* out)
{
    return self ? XPropertyData_value(&self->m_data, out) : false;
}

void XObjectBindableProperty_setValue(XObjectBindableProperty* self, const XVariant* v)
{
    if (self) XPropertyData_setValue(&self->m_data, v);
}

void XObjectBindableProperty_setValue_move(XObjectBindableProperty* self, XVariant* v)
{
    if (self) XPropertyData_setValue_move(&self->m_data, v);
}

bool XObjectBindableProperty_hasBinding(const XObjectBindableProperty* self)
{
    return self ? XPropertyData_hasBinding(&self->m_data) : false;
}

XPropertyBinding* XObjectBindableProperty_binding_const(const XObjectBindableProperty* self)
{
    return self ? XPropertyData_binding_const(&self->m_data) : NULL;
}

void XObjectBindableProperty_setBinding(XObjectBindableProperty* self, XPropertyBinding* binding)
{
    if (self) XPropertyData_setBinding(&self->m_data, binding);
}

XPropertyBinding* XObjectBindableProperty_setBinding_eval(XObjectBindableProperty* self, XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location)
{
    return self ? XPropertyData_setBinding_eval(&self->m_data, eval, user, location) : NULL;
}

XPropertyBinding* XObjectBindableProperty_takeBinding(XObjectBindableProperty* self)
{
    return self ? XPropertyData_takeBinding(&self->m_data) : NULL;
}

bool XObjectBindableProperty_subscribe(XObjectBindableProperty* self, XPropertyObserver* observer)
{
    return self ? XPropertyData_subscribe(&self->m_data, observer) : false;
}

XPropertyObserver* XObjectBindableProperty_onValueChanged(XObjectBindableProperty* self, XPropertyObserverCallback callback, void* user)
{
    return self ? XPropertyData_onValueChanged(&self->m_data, callback, user) : NULL;
}

void XObjectBindableProperty_bindable(XObjectBindableProperty* self, XBindable* out)
{
    if (out) XBindable_init_objectProperty(out, self);
}

/* ==================== 受保护接口（见 XObjectBindableProperty_Protected.h） ==================== */

bool XObjectBindableProperty_addObserver(XObjectBindableProperty* self, XPropertyObserver* observer)
{
    return self ? XPropertyData_subscribe(&self->m_data, observer) : false;
}

#endif /* XPROPERTY_ON */
