/**
 * @file       XProperty.c
 * @brief      声明式属性实现（对标 Qt QProperty/QPropertyData/QPropertyPrivate 内部机制）。
 */
#include "XProperty.h"
#if XPROPERTY_ON

#include <string.h>
#include "XMemory.h"
#include "XBindable.h"
#include "XProperty_Protected.h"

/* ==================== 内部辅助 ==================== */

/**
 * @brief 变体相等判断（类型与尺寸一致且内容相等；XVariant_compare 对异类型返回 0，
 *        不能直接作相等判据）。
 */
static bool XProperty_valueEquals(const XVariant* a, const XVariant* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->m_type != b->m_type || a->m_dataSize != b->m_dataSize) return false;
    if (a->m_type == XVariantType_NULL) return true;
    return XVariant_compare((XVariant*)a, (XVariant*)b) == 0;
}

/* ==================== 属性数据载体生命周期 ==================== */

void XPropertyData_init(XPropertyData* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XPropertyData));
    XVariant_init(&self->m_value, NULL, 0, XVariantType_NULL);
    self->m_interface = XProperty_interface();
}

void XPropertyData_init_ex(XPropertyData* self, const XVariant* initialValue)
{
    XPropertyData_init(self);
    if (!self || !initialValue)
        return;
    XClassCopy((XClass*)&self->m_value, (const XClass*)initialValue);
}

void XPropertyData_deinit(XPropertyData* self)
{
    if (!self) return;
    XPropertyData_removeBindingInternal(self);
    {
        /* 只解挂观察者节点（节点内存归各创建方），不释放 */
        XPropertyObserver* node = self->m_firstObserver;
        while (node)
        {
            XPropertyObserver* next = node->m_next;
            XPropertyObserver_unlink(node);
            node = next;
        }
        self->m_firstObserver = NULL;
    }
    XClassDeinit((XClass*)&self->m_value);
    self->m_interface = NULL;
}

/* ==================== 读取路径（求值 + 依赖捕获） ==================== */

const XVariant* XPropertyData_value_const(XPropertyData* data)
{
    if (!data) return NULL;
    if (data->m_binding)
    {
        if (XBinding_isOnEvalStack(data->m_binding))
        {
            /* 绑定循环：求值中读取自身目标，记错误并返回现值，绝不重入 */
            XPropertyBinding_setError_2(data->m_binding, XPropertyBindingError_BindingLoop,
                                        "property read during its own binding evaluation");
            return &data->m_value;
        }
        if (data->m_binding->m_dirty)
            XBinding_evaluate(data->m_binding);
    }
    {
        /* 依赖捕获：处于求值上下文时把本属性登记为当前绑定（栈顶）的依赖 */
        XPropertyBinding* evaluating = XBinding_currentEvalBinding();
        if (evaluating)
            XPropertyData_captureDependency(data, evaluating);
    }
    return &data->m_value;
}

bool XPropertyData_value(XPropertyData* data, XVariant* out)
{
    if (!data || !out) return false;
    const XVariant* value = XPropertyData_value_const(data);
    if (!value) return false;
    if (XClassIsVtableNull(out))
    {
        memset(out, 0, sizeof(XVariant));
        XVariant_init(out, NULL, 0, XVariantType_NULL);
    }
    XClassCopy((XClass*)out, (const XClass*)value);
    return true;
}

/* ==================== 写入路径（断开绑定 + 比较 + 通知） ==================== */

void XPropertyData_setValue(XPropertyData* data, const XVariant* v)
{
    if (!data) return;
    /* 显式赋值先移除既有绑定（与值是否相等无关，对标 Qt setValue） */
    if (data->m_binding)
        XPropertyData_removeBindingInternal(data);
    (void)XPropertyData_writeAndNotify(data, v);
}

void XPropertyData_setValue_move(XPropertyData* data, XVariant* v)
{
    if (!data || !v) return;
    if (data->m_binding)
        XPropertyData_removeBindingInternal(data);
    {
        bool changed = !XProperty_valueEquals(&data->m_value, v);
        XClassMove((XClass*)&data->m_value, (XClass*)v);
        if (changed)
        {
            XPropertyData_notifyObservers(data);
            if (data->m_interface && data->m_interface->m_notifyChanged)
                data->m_interface->m_notifyChanged(data);
        }
    }
}

bool XPropertyData_writeAndNotify(XPropertyData* data, const XVariant* v)
{
    if (!data) return false;
    {
        XVariant nullVariant;
        memset(&nullVariant, 0, sizeof(XVariant));   /* XVariant_init 会读旧 m_data,栈对象必须先清零 */
        XVariant_init(&nullVariant, NULL, 0, XVariantType_NULL);
        const XVariant* target = v ? v : &nullVariant;
        bool changed = !XProperty_valueEquals(&data->m_value, target);
        if (changed)
        {
            XClassCopy((XClass*)&data->m_value, (const XClass*)target);
            XPropertyData_notifyObservers(data);
            if (data->m_interface && data->m_interface->m_notifyChanged)
                data->m_interface->m_notifyChanged(data);
        }
        XClassDeinit((XClass*)&nullVariant);
        return changed;
    }
}

bool XPropertyData_updateFromBinding(XPropertyData* data, const XVariant* result)
{
    if (!data) return false;
    return XPropertyData_writeAndNotify(data, result);
}

void XPropertyData_notifyObservers(XPropertyData* data)
{
    if (!data) return;
    XPropertyObserver* node = data->m_firstObserver;
    while (node)
    {
        /* 先取后继：回调可能解挂甚至删除链上任意节点 */
        XPropertyObserver* next = node->m_next;
        switch (node->m_kind)
        {
        case XPropertyObserverKind_Observer:
            if (node->m_callback)
                node->m_callback(node->m_user);
            break;
        case XPropertyObserverKind_ChangeHandler:
            if (node->m_changeHandler &&
                node->m_changeHandler(node->m_user, &data->m_value))
                XPropertyObserver_unlink(node);
            break;
        case XPropertyObserverKind_BindingDependency:
            {
                /* 级联：依赖变化 → 属主绑定标脏（首次）→ 立即通知其目标属性
                 * 观察者并调用值变化钩子（对象属性由此发 notify 信号，
                 * 对标 Qt 依赖级联时对 QBindable 目标属性的 Asynchronous 通知） */
                XPropertyBinding* binding = node->m_binding;
                if (binding && !binding->m_dirty)
                {
                    XPropertyData* target;
                    binding->m_dirty = true;
                    target = binding->m_propertyData;
                    if (target)
                    {
                        XPropertyData_notifyObservers(target);
                        if (target->m_interface && target->m_interface->m_notifyChanged)
                            target->m_interface->m_notifyChanged(target);
                    }
                }
            }
            break;
        default:
            break;   /* 别名锚点等：仅作链表挂接，通知跳过 */
        }
        node = next;
    }
}

/* ==================== 绑定管理 ==================== */

bool XPropertyData_hasBinding(const XPropertyData* data)
{
    return data && data->m_binding != NULL;
}

XPropertyBinding* XPropertyData_binding_const(const XPropertyData* data)
{
    return data ? data->m_binding : NULL;
}

void XPropertyData_removeBindingInternal(XPropertyData* data)
{
    if (!data || !data->m_binding) return;
    XPropertyBinding* binding = data->m_binding;
    data->m_binding = NULL;
    if (binding->m_propertyData == data)
        binding->m_propertyData = NULL;
    XBinding_detachDependencies(binding);
    /* 释放属性持有的引用；绑定对象由剩余引用方（若有）继续持有 */
    XPropertyBinding_unref(binding);
}

void XPropertyData_captureDependency(XPropertyData* data, XPropertyBinding* binding)
{
    if (!data || !binding) return;
    if (binding == data->m_binding)
        return;   /* 依赖自身目标属于绑定循环，已另行报告 */
    XPropertyObserver_attachDependency(data, binding);
}

void XPropertyData_setBinding(XPropertyData* data, XPropertyBinding* binding)
{
    if (!data) return;
    if (data->m_binding == binding)
    {
        /* 重复设置同一绑定：仅需保证待求值状态 */
        if (binding)
            binding->m_dirty = true;
        return;
    }
    if (data->m_binding)
        XPropertyData_removeBindingInternal(data);
    if (!binding) return;
    /* 接管调用方引用 */
    binding->m_propertyData = data;
    binding->m_dirty = true;
    data->m_binding = binding;
}

XPropertyBinding* XPropertyData_setBinding_eval(XPropertyData* data, XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location)
{
    if (!data || !eval) return NULL;
    XPropertyBinding* binding = XPropertyBinding_create_eval(eval, user, location);
    if (!binding) return NULL;
    XPropertyData_setBinding(data, binding);
    return binding;   /* 归属性所有 */
}

XPropertyBinding* XPropertyData_takeBinding(XPropertyData* data)
{
    if (!data || !data->m_binding) return NULL;
    XPropertyBinding* binding = data->m_binding;
    data->m_binding = NULL;
    if (binding->m_propertyData == data)
        binding->m_propertyData = NULL;
    XBinding_detachDependencies(binding);
    return binding;   /* 所有权移交调用方 */
}

/* ==================== 观察者订阅 ==================== */

bool XPropertyData_subscribe(XPropertyData* data, XPropertyObserver* observer)
{
    if (!data || !observer) return false;
    XPropertyObserver_unlink(observer);
    XPropertyObserver_attach(observer, data);
    return true;
}

XPropertyObserver* XPropertyData_onValueChanged(XPropertyData* data, XPropertyObserverCallback callback, void* user)
{
    if (!data) return NULL;
    XPropertyObserver* observer = XPropertyObserver_create();
    if (!observer) return NULL;
    XPropertyObserver_setCallback(observer, callback, user);
    XPropertyObserver_attach(observer, data);
    return observer;
}

/* ==================== 绕过绑定的逃生舱（对标 valueBypassingBindings） ==================== */

const XVariant* XPropertyData_valueBypassingBindings_const(const XPropertyData* data)
{
    return data ? &data->m_value : NULL;
}

void XPropertyData_setValueBypassingBinding(XPropertyData* data, const XVariant* v)
{
    if (!data) return;
    if (v)
        XClassCopy((XClass*)&data->m_value, (const XClass*)v);
    else
    {
        XVariant_init(&data->m_value, NULL, 0, XVariantType_NULL);
    }
}

/* ==================== 独立属性类（对标 QProperty） ==================== */

static void VXProperty_deinit(XProperty* obj)
{
    if (!obj) return;
    XPropertyData_deinit(&obj->m_data);
    XClass_Deinit_Parent(XClass, (XClass*)obj);
}

static void VXProperty_copy(XProperty* dest, const XProperty* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XProperty_init(dest);
    /* Qt 禁止拷贝 QProperty；此处按库规范弱化为仅拷贝值，绑定/观察者不随拷贝 */
    XClassCopy((XClass*)&dest->m_data.m_value, (const XClass*)&src->m_data.m_value);
}

static void VXProperty_move(XProperty* dest, XProperty* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XProperty_init(dest);
    /* 先清空目标自身挂接（旧观察者节点归各自创建方） */
    if (dest->m_data.m_binding)
        XPropertyData_removeBindingInternal(dest);
    {
        XPropertyObserver* node = dest->m_data.m_firstObserver;
        while (node)
        {
            XPropertyObserver* next = node->m_next;
            XPropertyObserver_unlink(node);
            node = next;
        }
        dest->m_data.m_firstObserver = NULL;
    }
    /* 转移值/接口/绑定/观察者，并重定向节点与绑定的回指指针 */
    XClassMove((XClass*)&dest->m_data.m_value, (XClass*)&src->m_data.m_value);
    dest->m_data.m_interface = src->m_data.m_interface;
    dest->m_data.m_binding = src->m_data.m_binding;
    if (dest->m_data.m_binding)
        dest->m_data.m_binding->m_propertyData = &dest->m_data;
    dest->m_data.m_firstObserver = src->m_data.m_firstObserver;
    {
        XPropertyObserver* node;
        for (node = dest->m_data.m_firstObserver; node; node = node->m_next)
            node->m_propertyData = &dest->m_data;
    }
    src->m_data.m_binding = NULL;
    src->m_data.m_firstObserver = NULL;
}

XVtable* XProperty_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XProperty)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXProperty_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXProperty_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXProperty_move);
    return XVTABLE_DEFAULT;
}

void XProperty_init(XProperty* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XProperty));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XProperty);
    XPropertyData_init(&self->m_data);
}

void XProperty_init_ex(XProperty* self, const XVariant* initialValue)
{
    XProperty_init(self);
    if (!self || !initialValue) return;
    XClassCopy((XClass*)&self->m_data.m_value, (const XClass*)initialValue);
}

XProperty* XProperty_create(void)
{
    XProperty* self = (XProperty*)XMemory_malloc(sizeof(XProperty), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(XProperty));
    XProperty_init(self);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

XProperty* XProperty_create_ex(const XVariant* initialValue)
{
    XProperty* self = XProperty_create();
    if (!self) return NULL;
    if (initialValue)
        XClassCopy((XClass*)&self->m_data.m_value, (const XClass*)initialValue);
    return self;
}

XProperty* XProperty_create_copy(const XProperty* other)
{
    if (!other) return NULL;
    XProperty* self = XProperty_create();
    if (!self) return NULL;
    XClassCopy((XClass*)self, (const XClass*)other);
    return self;
}

/* ==================== 独立属性公开包装 ==================== */

const XVariant* XProperty_value_const(XProperty* self)
{
    return self ? XPropertyData_value_const(&self->m_data) : NULL;
}

bool XProperty_value(XProperty* self, XVariant* out)
{
    return self ? XPropertyData_value(&self->m_data, out) : false;
}

void XProperty_setValue(XProperty* self, const XVariant* v)
{
    if (self) XPropertyData_setValue(&self->m_data, v);
}

void XProperty_setValue_move(XProperty* self, XVariant* v)
{
    if (self) XPropertyData_setValue_move(&self->m_data, v);
}

bool XProperty_hasBinding(const XProperty* self)
{
    return self ? XPropertyData_hasBinding(&self->m_data) : false;
}

XPropertyBinding* XProperty_binding_const(const XProperty* self)
{
    return self ? XPropertyData_binding_const(&self->m_data) : NULL;
}

void XProperty_setBinding(XProperty* self, XPropertyBinding* binding)
{
    if (self) XPropertyData_setBinding(&self->m_data, binding);
}

XPropertyBinding* XProperty_setBinding_eval(XProperty* self, XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location)
{
    return self ? XPropertyData_setBinding_eval(&self->m_data, eval, user, location) : NULL;
}

XPropertyBinding* XProperty_takeBinding(XProperty* self)
{
    return self ? XPropertyData_takeBinding(&self->m_data) : NULL;
}

bool XProperty_subscribe(XProperty* self, XPropertyObserver* observer)
{
    return self ? XPropertyData_subscribe(&self->m_data, observer) : false;
}

XPropertyObserver* XProperty_onValueChanged(XProperty* self, XPropertyObserverCallback callback, void* user)
{
    return self ? XPropertyData_onValueChanged(&self->m_data, callback, user) : NULL;
}

const XVariant* XProperty_valueBypassingBindings_const(const XProperty* self)
{
    return self ? XPropertyData_valueBypassingBindings_const(&self->m_data) : NULL;
}

void XProperty_setValueBypassingBinding(XProperty* self, const XVariant* v)
{
    if (self) XPropertyData_setValueBypassingBinding(&self->m_data, v);
}

/* ==================== 独立属性行为接口 ==================== */

static const XVariant* VXPropertyData_getValue(XPropertyData* data)
{
    return XPropertyData_value_const(data);
}

static void VXPropertyData_setValue(XPropertyData* data, const XVariant* v)
{
    XPropertyData_setValue(data, v);
}

static void VXPropertyData_notifyChanged(XPropertyData* data)
{
    (void)data;   /* 独立属性无 notify 信号 */
}

static XPropertyBinding* VXPropertyData_getBinding(const XPropertyData* data)
{
    return data ? data->m_binding : NULL;
}

static void VXPropertyData_setBindingImp(XPropertyData* data, XPropertyBinding* binding)
{
    XPropertyData_setBinding(data, binding);
}

static const XBindableInterface s_propertyInterface = {
    VXPropertyData_getValue,
    VXPropertyData_setValue,
    VXPropertyData_notifyChanged,
    VXPropertyData_getBinding,
    VXPropertyData_setBindingImp,
};

const XBindableInterface* XProperty_interface(void)
{
    return &s_propertyInterface;
}

#endif /* XPROPERTY_ON */
