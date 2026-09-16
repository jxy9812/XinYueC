/******************************************************************************
 * @file       XGraphicsEffect.c
 * @brief      图形效果基类实现（对标 Qt 6.8 QGraphicsEffect 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。setEnabled 在状态实际变化
 *             时发射 enabledChanged；update() 为占位实现。效果对象由
 *             XWidget_setGraphicsEffect 挂接（XWidget 拥有，Qt 语义）。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"

#if XWIDGET_ON

#include "XGraphicsEffect.h"

/* ==================== 内部辅助 ==================== */

/** @brief 发射携带 bool 参数的信号。 */
static void xgraphicseffect_emitBool(XGraphicsEffect* self, size_t signal,
                                     bool enabled)
{
    XVarList* args = XVarList_Create(XVar(bool, enabled));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 类与实例生命周期 ==================== */

XVtable* XGraphicsEffect_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsEffect)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

void XGraphicsEffect_init(XGraphicsEffect* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XGraphicsEffect);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_enabled = true;
}

XGraphicsEffect* XGraphicsEffect_create_ex(XMemoryType memory)
{
    XGraphicsEffect* self =
        (XGraphicsEffect*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGraphicsEffect_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

bool XGraphicsEffect_isEnabled(const XGraphicsEffect* self)
{ return self ? self->m_enabled : false; }

void XGraphicsEffect_setEnabled(XGraphicsEffect* self, bool enable)
{
    if (!self || self->m_enabled == enable) return;
    self->m_enabled = enable;
    xgraphicseffect_emitBool(self,
                             (size_t)XGraphicsEffect_enabledChanged_signal,
                             enable);
}

void XGraphicsEffect_update(XGraphicsEffect* self)
{
    /* 占位：XGui 渲染管线尚未接入效果绘制（见头文件 @note）。 */
    (void)self;
}

/* ==================== 信号 ==================== */

void* XGraphicsEffect_enabledChanged_signal(XGraphicsEffect* self, bool enabled)
{
    xgraphicseffect_emitBool(self,
                             (size_t)XGraphicsEffect_enabledChanged_signal,
                             enabled);
    return (void*)(size_t)XGraphicsEffect_enabledChanged_signal;
}

#endif /* XWIDGET_ON */
