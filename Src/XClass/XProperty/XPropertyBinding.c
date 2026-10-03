/**
 * @file       XPropertyBinding.c
 * @brief      属性绑定实现（对标 Qt QPropertyBindingPrivate/QBindingStatus 内部机制）。
 * @details    求值上下文经 XThreadData::m_bindingEvalStack（XStack，元素为
 *             XPropertyBinding*）传递：求值前入栈，结束后出栈；属性读取路径
 *             据栈顶识别"当前正在求值的绑定"并登记依赖。XSync 关闭时退化为
 *             模块内单指针栈顶（不支持线程隔离，仅保编译与基本可用）。
 *             循环检测主判据为绑定自身的 m_evaluating 重入标记：任何绑定循环
 *             必然经过一个正在求值的绑定，据此即可完整检出，栈扫描仅为辅助。
 */
#include "XPropertyBinding.h"
#if XPROPERTY_ON

#include <string.h>
#include "XMemory.h"
#include "XProperty_Protected.h"

#if defined(XSYNC_ON) && XSYNC_ON && defined(XTHREADDATA_ON) && XTHREADDATA_ON
#define XPROPERTY_HAS_THREADDATA 1
#include "XThreadData.h"
#endif

/* ==================== 求值上下文 ==================== */

#if XPROPERTY_HAS_THREADDATA
static XStack* XBinding_evalStack(void)
{
    XThreadData* threadData = XThreadData_current();
    return threadData ? &threadData->m_bindingEvalStack : NULL;
}
#else
/* 降级模式：无线程数据时使用模块内栈顶指针（仅主线程/单线程场景可用） */
static XPropertyBinding* s_fallbackEvalTop = NULL;
#endif

/**
 * @brief 查询当前正在求值的绑定（求值栈顶；对标 QBindingStatus::currentlyEvaluatingBinding）。
 * @return 绑定指针；无求值上下文返回 NULL。声明见 XProperty_Protected.h。
 */
XPropertyBinding* XBinding_currentEvalBinding(void)
{
#if XPROPERTY_HAS_THREADDATA
    XStack* stack = XBinding_evalStack();
    if (stack && !XStack_isEmpty_base(stack))
        return *(XPropertyBinding**)XStack_top_base(stack);
    return NULL;
#else
    return s_fallbackEvalTop;
#endif
}

/**
 * @brief 入栈求值上下文。
 * @param binding 待求值绑定；不能为 NULL。
 * @return 入栈成功返回 true；线程求值栈入栈失败返回 false（调用方放弃本次求值）。
 */
static bool XBinding_pushEval(XPropertyBinding* binding)
{
#if XPROPERTY_HAS_THREADDATA
    XStack* stack = XBinding_evalStack();
    if (!stack) return false;
    return XStack_push_base(stack, &binding);
#else
    (void)binding;
    s_fallbackEvalTop = binding;
    return true;
#endif
}

/**
 * @brief 出栈求值上下文并恢复此前栈顶。
 * @param prevTop 入栈前记录的栈顶；可为 NULL。
 * @return 无返回值。
 */
static void XBinding_popEval(XPropertyBinding* prevTop)
{
#if XPROPERTY_HAS_THREADDATA
    XStack* stack = XBinding_evalStack();
    if (stack && !XStack_isEmpty_base(stack))
        XStack_pop_base(stack);
#else
    (void)prevTop;
    s_fallbackEvalTop = NULL;
#endif
}

/* ==================== 生命周期与引用计数 ==================== */

static XPropertyBinding* XPropertyBinding_alloc(void)
{
    XPropertyBinding* self = (XPropertyBinding*)XNew(XPropertyBinding);
    if (!self) return NULL;
    memset(self, 0, sizeof(XPropertyBinding));
    XAtomic_init(self->m_ref, 1);
    XPropertyBindingSourceLocation_init(&self->m_location);
    XPropertyBindingError_init(&self->m_error);
    XVariant_init(&self->m_result, NULL, 0, XVariantType_NULL);
    self->m_dirty = true;   /* 新绑定待首次求值 */
    return self;
}

XPropertyBinding* XPropertyBinding_create(XPropertyBindingFunction fn, const XPropertyBindingSourceLocation* location)
{
    if (!fn.m_updater)
    {
        XERROR_PRINTF("XPropertyBinding_create: updater is NULL\n");
        return NULL;
    }
    XPropertyBinding* self = XPropertyBinding_alloc();
    if (!self) return NULL;
    self->m_updater = fn.m_updater;
    if (location) self->m_location = *location;
    return self;
}

XPropertyBinding* XPropertyBinding_create_eval(XPropertyBindingEvalFunc eval, void* user, const XPropertyBindingSourceLocation* location)
{
    if (!eval)
    {
        XERROR_PRINTF("XPropertyBinding_create_eval: eval is NULL\n");
        return NULL;
    }
    XPropertyBinding* self = XPropertyBinding_alloc();
    if (!self) return NULL;
    self->m_eval = eval;
    self->m_evalUser = user;
    if (location) self->m_location = *location;
    return self;
}

XPropertyBinding* XPropertyBinding_ref(XPropertyBinding* self)
{
    if (!self) return NULL;
    XAtomic_fetch_add_int32(&self->m_ref, 1, XAtomic_MemoryOrder_Relaxed);
    return self;
}

void XPropertyBinding_unref(XPropertyBinding* self)
{
    if (!self) return;
    if (XAtomic_fetch_sub_int32(&self->m_ref, 1, XAtomic_MemoryOrder_Acquire) != 1)
        return;
    /* 释放属性侧挂接（属性通常已先释放自己的引用并清指针，这里兜底） */
    if (self->m_propertyData)
    {
        if (self->m_propertyData->m_binding == self)
            self->m_propertyData->m_binding = NULL;
        self->m_propertyData = NULL;
    }
    XBinding_detachDependencies(self);
    XClassDeinit((XClass*)&self->m_error);
    XClassDeinit((XClass*)&self->m_result);
    XFree_System(self);
}

/* ==================== 查询与错误 ==================== */

bool XPropertyBinding_isValid(const XPropertyBinding* self)
{
    return self != NULL;
}

const XPropertyBindingError* XPropertyBinding_error_const(const XPropertyBinding* self)
{
    return self ? &self->m_error : NULL;
}

XPropertyBindingSourceLocation XPropertyBinding_sourceLocation(const XPropertyBinding* self)
{
    XPropertyBindingSourceLocation location;
    XPropertyBindingSourceLocation_init(&location);
    if (self) location = self->m_location;
    return location;
}

void XPropertyBinding_setError(XPropertyBinding* self, XPropertyBindingErrorType type, const XString* description)
{
    if (!self) return;
    XPropertyBindingError_init_ex(&self->m_error, type, description);
}

void XPropertyBinding_setError_2(XPropertyBinding* self, XPropertyBindingErrorType type, const char* description)
{
    if (!self) return;
    XPropertyBindingError_init_ex_2(&self->m_error, type, description);
}

/* ==================== 依赖与求值（内部） ==================== */

bool XBinding_isOnEvalStack(XPropertyBinding* binding)
{
    /* 任何绑定循环必然经过一个正在求值的绑定，重入标记即完整判据 */
    return binding && binding->m_evaluating;
}

const XVariant* XBinding_result_const(XPropertyBinding* binding)
{
    return binding ? &binding->m_result : NULL;
}

void XBinding_detachDependencies(XPropertyBinding* binding)
{
    if (!binding) return;
    /* 依赖节点归绑定所有：解挂并释放，依赖在每次求值中重新捕获 */
    XPropertyObserver* node = binding->m_dependencies;
    binding->m_dependencies = NULL;
    while (node)
    {
        XPropertyObserver* next = node->m_nextDependency;
        XPropertyObserver_unlink(node);
        XFree_System(node);
        node = next;
    }
}

void XBinding_evaluate(XPropertyBinding* binding)
{
    if (!binding || !binding->m_propertyData) return;
    if (binding->m_evaluating)
    {
        XPropertyBinding_setError_2(binding, XPropertyBindingError_EvaluationLoopError,
                                    "binding re-entered during evaluation");
        return;
    }
    /* 求值期间自持引用：求值/通知路径可能触发外部 unref */
    XPropertyBinding_ref(binding);
    XPropertyBinding* prevTop = XBinding_currentEvalBinding();
    if (!XBinding_pushEval(binding))
    {
        /* 入栈失败（内存不足）：保持脏标记，下次读取重试 */
        XERROR_PRINTF("XBinding_evaluate: push eval stack failed\n");
        XPropertyBinding_unref(binding);
        return;
    }
    binding->m_evaluating = true;
    XBinding_detachDependencies(binding);
    binding->m_dirty = false;
    XPropertyBindingError_clear(&binding->m_error);

    XVariant result;
    memset(&result, 0, sizeof(XVariant));   /* XVariant_init 会读旧 m_data,栈对象必须先清零 */
    XVariant_init(&result, NULL, 0, XVariantType_NULL);
    bool ok = true;
    if (binding->m_eval)
        ok = binding->m_eval(binding->m_evalUser, &result);
    XClassMove((XClass*)&binding->m_result, (XClass*)&result);
    XClassDeinit((XClass*)&result);

    binding->m_evaluating = false;
    XBinding_popEval(prevTop);

    if (ok)
    {
        if (binding->m_updater)
            binding->m_updater(binding->m_propertyData, binding);
        else
            (void)XPropertyData_updateFromBinding(binding->m_propertyData, &binding->m_result);
    }
    else if (XPropertyBindingError_type(&binding->m_error) == XPropertyBindingError_NoError)
    {
        XPropertyBinding_setError_2(binding, XPropertyBindingError_Unknown,
                                    "evaluator returned failure");
    }
    /* 最后释放自持引用；减到 0 时绑定在此销毁，之后不得再访问 */
    XPropertyBinding_unref(binding);
}

/* ==================== 源码位置（对标 QPropertyBindingSourceLocation） ==================== */

void XPropertyBindingSourceLocation_init(XPropertyBindingSourceLocation* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XPropertyBindingSourceLocation));
}

/* ==================== 绑定错误对象（对标 QPropertyBindingError） ==================== */

static void VXPropertyBindingError_deinit(XPropertyBindingError* obj)
{
    if (!obj) return;
    if (obj->m_description)
    {
        XClassDelete((XClass*)obj->m_description);
        obj->m_description = NULL;
    }
    XClass_Deinit_Parent(XClass, (XClass*)obj);
}

static void VXPropertyBindingError_copy(XPropertyBindingError* dest, const XPropertyBindingError* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XPropertyBindingError_init(dest);
    if (dest->m_description)
    {
        XClassDelete((XClass*)dest->m_description);
        dest->m_description = NULL;
    }
    dest->m_type = src->m_type;
    dest->m_description = src->m_description ? XString_create_copy(src->m_description) : NULL;
}

static void VXPropertyBindingError_move(XPropertyBindingError* dest, XPropertyBindingError* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XPropertyBindingError_init(dest);
    if (dest->m_description)
    {
        XClassDelete((XClass*)dest->m_description);
        dest->m_description = NULL;
    }
    dest->m_type = src->m_type;
    dest->m_description = src->m_description;
    src->m_type = XPropertyBindingError_NoError;
    src->m_description = NULL;
}

XVtable* XPropertyBindingError_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPropertyBindingError)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPropertyBindingError_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPropertyBindingError_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPropertyBindingError_move);
    return XVTABLE_DEFAULT;
}

void XPropertyBindingError_init(XPropertyBindingError* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XPropertyBindingError));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XPropertyBindingError);
    self->m_type = XPropertyBindingError_NoError;
    self->m_description = NULL;
}

void XPropertyBindingError_init_ex(XPropertyBindingError* self, XPropertyBindingErrorType type, const XString* description)
{
    XPropertyBindingError_init(self);
    if (!self) return;
    self->m_type = type;
    self->m_description = description ? XString_create_copy(description) : NULL;
}

void XPropertyBindingError_init_ex_2(XPropertyBindingError* self, XPropertyBindingErrorType type, const char* description)
{
    XPropertyBindingError_init(self);
    if (!self) return;
    self->m_type = type;
    self->m_description = description ? XString_create_utf8(description) : NULL;
}

XPropertyBindingError* XPropertyBindingError_create(void)
{
    XPropertyBindingError* self =
        (XPropertyBindingError*)XMemory_malloc(sizeof(XPropertyBindingError), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(XPropertyBindingError));
    XPropertyBindingError_init(self);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

XPropertyBindingError* XPropertyBindingError_create_ex(XPropertyBindingErrorType type, const XString* description)
{
    XPropertyBindingError* self = XPropertyBindingError_create();
    if (!self) return NULL;
    XPropertyBindingError_init_ex(self, type, description);
    return self;
}

XPropertyBindingError* XPropertyBindingError_create_ex_2(XPropertyBindingErrorType type, const char* description)
{
    XPropertyBindingError* self = XPropertyBindingError_create();
    if (!self) return NULL;
    XPropertyBindingError_init_ex_2(self, type, description);
    return self;
}

XPropertyBindingError* XPropertyBindingError_create_copy(const XPropertyBindingError* other)
{
    if (!other) return NULL;
    XPropertyBindingError* self = XPropertyBindingError_create();
    if (!self) return NULL;
    XClassCopy((XClass*)self, (const XClass*)other);
    return self;
}

void XPropertyBindingError_clear(XPropertyBindingError* self)
{
    if (!self) return;
    if (self->m_description)
    {
        XClassDelete((XClass*)self->m_description);
        self->m_description = NULL;
    }
    self->m_type = XPropertyBindingError_NoError;
}

XPropertyBindingErrorType XPropertyBindingError_type(const XPropertyBindingError* self)
{
    return self ? self->m_type : XPropertyBindingError_Unknown;
}

const XString* XPropertyBindingError_description_const(const XPropertyBindingError* self)
{
    return self ? self->m_description : NULL;
}

#endif /* XPROPERTY_ON */
