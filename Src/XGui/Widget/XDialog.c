/**
 * @file       XDialog.c
 * @brief      对话框控件实现（对标 Qt 6.8 QDialog 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XDialog.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiApplication.h"
#include "XEventLoop.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <string.h>

#if XWIDGET_ON && XDIALOG_ON

static void xdlg_emitVoid(XDialog* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xdlg_emitFinished(XDialog* self, int result)
{
    XVarList* args = XVarList_Create(XVar(int, result));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XDialog_finished_signal, args, NULL, NULL,
            XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

XVtable* XDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDialog)
    XVTABLE_INHERIT_XCLASS(XWidget);
    return XVTABLE_DEFAULT;
}

void XDialog_init(XDialog* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_modal = true;
    self->m_result = 0;
    self->m_inExec = false;
}

XDialog* XDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XDialog* self = (XDialog*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDialog_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

int XDialog_exec(XDialog* self)
{
    if (!self) return 0;
    self->m_inExec = true;
    XWidget_show((XWidget*)self);
    XGuiApplication_processEvents(XEventLoop_DialogExec);
    self->m_inExec = false;
    return self->m_result;
}

void XDialog_done(XDialog* self, int result)
{
    if (!self) return;
    self->m_result = result;
    self->m_inExec = false;
    XWidget_setVisible((XWidget*)self, false);
    xdlg_emitFinished(self, result);
}

void XDialog_accept(XDialog* self)
{
    if (!self) return;
    self->m_result = 1;
    XDialog_done(self, 1);
    xdlg_emitVoid(self, (size_t)XDialog_accepted_signal);
}

void XDialog_reject(XDialog* self)
{
    if (!self) return;
    self->m_result = 0;
    XDialog_done(self, 0);
    xdlg_emitVoid(self, (size_t)XDialog_rejected_signal);
}

int XDialog_result(const XDialog* self) { return self ? self->m_result : 0; }
void XDialog_setResult(XDialog* self, int result) { if (self) self->m_result = result; }
void XDialog_setModal(XDialog* self, bool modal) { if (self) self->m_modal = modal; }
bool XDialog_isModal(const XDialog* self) { return self ? self->m_modal : false; }

void* XDialog_accepted_signal(XDialog* self)
{ (void)self; return (void*)(size_t)XDialog_accepted_signal; }
void* XDialog_rejected_signal(XDialog* self)
{ (void)self; return (void*)(size_t)XDialog_rejected_signal; }
void* XDialog_finished_signal(XDialog* self, int result)
{ (void)self; (void)result; return (void*)(size_t)XDialog_finished_signal; }

#endif /* XWIDGET_ON && XDIALOG_ON */