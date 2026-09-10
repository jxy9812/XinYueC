/**
 * @file       XStackedWidget.c
 * @brief      堆叠容器控件实现（对标 Qt 6.8 QStackedWidget 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XStackedWidget.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGuiConfig.h"
#include <string.h>

#if XWIDGET_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON

/* ==================== 信号转发槽 ==================== */

/** @brief 布局 currentChanged → 控件 currentChanged 转发。 */
static void xsw_layoutCurrentChangedSlot(XObject* receiver, XVarList* args)
{
    XStackedWidget* self = (XStackedWidget*)receiver;
    XVarList* out;
    if (!self || !args) return;
    /* XVarList_args_1 以 const 局部变量定义 index，勿提前声明。 */
    XVarList_args_1(args, int, index);
    out = XVarList_Create(XVar(int, index));
    if (!out) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self,
                           (size_t)XStackedWidget_currentChanged_signal,
                           out, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(out);
}

/** @brief 布局 widgetRemoved → 控件 widgetRemoved 转发。 */
static void xsw_layoutWidgetRemovedSlot(XObject* receiver, XVarList* args)
{
    XStackedWidget* self = (XStackedWidget*)receiver;
    XVarList* out;
    if (!self || !args) return;
    /* XVarList_args_1 以 const 局部变量定义 index，勿提前声明。 */
    XVarList_args_1(args, int, index);
    out = XVarList_Create(XVar(int, index));
    if (!out) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self,
                           (size_t)XStackedWidget_widgetRemoved_signal,
                           out, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(out);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_stackedWidget_deinit(XStackedWidget* self);

XVtable* XStackedWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStackedWidget)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_stackedWidget_deinit);
    return XVTABLE_DEFAULT;
}

void XStackedWidget_init(XStackedWidget* self, XWidget* parent,
                         XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XStackedWidget);
    fprintf(stderr, "[sw-dbg] init self=%p layout=%p szS=%d szL=%d vt=%p deinit=%p\n",
            (void*)self, (void*)&self->m_layout,
            (int)sizeof(XStackedWidget), (int)sizeof(XStackedLayout),
            (void*)XClassGetVtable(self),
            (void*)XClassGetVirtualFunc(self, EXClass_Deinit, void*));
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 内部堆叠布局挂到本控件（对标 QStackedWidget 持有 QStackedLayout）。 */
    XStackedLayout_init(&self->m_layout);
    XWidget_setLayout((XWidget*)self, (XLayout*)&self->m_layout);
    XObject_connect_1((XObject*)&self->m_layout,
                      XSignal(XStackedLayout_currentChanged_signal),
                      (XObject*)self, xsw_layoutCurrentChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_layout,
                      XSignal(XStackedLayout_widgetRemoved_signal),
                      (XObject*)self, xsw_layoutWidgetRemovedSlot,
                      XConnectionType_Direct);
}

/** @brief 析构：先释放内嵌布局，再交父类。 */
static void VX_stackedWidget_deinit(XStackedWidget* self)
{
    if (!self) return;
    /* 先断开布局信号转发：布局析构（releaseItems/takeAt）会发射
       widgetRemoved/currentChanged，转发槽随后访问半析构的宿主对象。 */
    XObject_disconnect_1((XObject*)&self->m_layout,
                         XSignal(XStackedLayout_currentChanged_signal),
                         (XObject*)self, xsw_layoutCurrentChangedSlot);
    XObject_disconnect_1((XObject*)&self->m_layout,
                         XSignal(XStackedLayout_widgetRemoved_signal),
                         (XObject*)self, xsw_layoutWidgetRemovedSlot);
    XStackedLayout_deinit_base(&self->m_layout);
    XClass_Deinit_Parent(XFrame, (XFrame*)self);
}

XStackedWidget* XStackedWidget_create_ex(XMemoryType memory, XWidget* parent,
                                         XWidgetFlags flags)
{
    XStackedWidget* self = (XStackedWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XStackedWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 页面管理 ==================== */

int XStackedWidget_addWidget(XStackedWidget* self, XWidget* widget)
{
    if (!self || !widget) return -1;
    return XStackedLayout_addWidget(&self->m_layout, widget);
}

int XStackedWidget_insertWidget(XStackedWidget* self, int index,
                                XWidget* widget)
{
    if (!self || !widget) return -1;
    return XStackedLayout_insertWidget(&self->m_layout, index, widget);
}

void XStackedWidget_removeWidget(XStackedWidget* self, XWidget* widget)
{
    if (!self || !widget) return;
    XStackedLayout_removeWidget(&self->m_layout, widget);
}

int XStackedWidget_currentIndex(const XStackedWidget* self)
{
    return self ? XStackedLayout_currentIndex(&self->m_layout) : -1;
}

XWidget* XStackedWidget_currentWidget(const XStackedWidget* self)
{
    return self ? XStackedLayout_currentWidget(&self->m_layout) : NULL;
}

int XStackedWidget_indexOf(const XStackedWidget* self, const XWidget* widget)
{
    return self ? XStackedLayout_indexOf(&self->m_layout, widget) : -1;
}

XWidget* XStackedWidget_widget(const XStackedWidget* self, int index)
{
    return self ? XStackedLayout_widget(&self->m_layout, index) : NULL;
}

int XStackedWidget_count(const XStackedWidget* self)
{
    return self ? XStackedLayout_count(&self->m_layout) : 0;
}

void XStackedWidget_setCurrentIndex(XStackedWidget* self, int index)
{
    if (!self) return;
    XStackedLayout_setCurrentIndex(&self->m_layout, index);
}

void XStackedWidget_setCurrentWidget(XStackedWidget* self, XWidget* widget)
{
    if (!self || !widget) return;
    XStackedLayout_setCurrentWidget(&self->m_layout, widget);
}

/* ==================== 信号 ==================== */

void* XStackedWidget_currentChanged_signal(XStackedWidget* self, int index)
{
    XVarList* args;
    (void)index;
    args = XVarList_Create(XVar(int, index));
    if (self && ((XObject*)self)->m_signalSlot) {
        if (args)
            XObject_emitSignal((XObject*)self,
                               (size_t)XStackedWidget_currentChanged_signal,
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else if (args) {
        XVarList_delete(args);
    }
    return (void*)(size_t)XStackedWidget_currentChanged_signal;
}

void* XStackedWidget_widgetRemoved_signal(XStackedWidget* self, int index)
{
    XVarList* args;
    (void)index;
    args = XVarList_Create(XVar(int, index));
    if (self && ((XObject*)self)->m_signalSlot) {
        if (args)
            XObject_emitSignal((XObject*)self,
                               (size_t)XStackedWidget_widgetRemoved_signal,
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else if (args) {
        XVarList_delete(args);
    }
    return (void*)(size_t)XStackedWidget_widgetRemoved_signal;
}

#endif /* XWIDGET_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON */
