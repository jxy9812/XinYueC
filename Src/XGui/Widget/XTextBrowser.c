/**
 * @file       XTextBrowser.c
 * @brief      富文本浏览控件实现（对标 Qt 6.8 QTextBrowser 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XTextBrowser.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include <string.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON

static void xtb_emitStr(XTextBrowser* self, size_t signal, const char* text)
{
    XVarList* args;
    XString* val;
    if (!self || !((XObject*)self)->m_signalSlot) return;
    val = XString_create_utf8(text ? text : "");
    if (!val) return;
    args = XVarList_Create(XVar(XString*, val));
    if (!args) { XString_delete_base((XClass*)val); return; }
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xtb_emitBool(XTextBrowser* self, size_t signal, bool v)
{
    XVarList* args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

XVtable* XTextBrowser_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextBrowser)
    XVTABLE_INHERIT_XCLASS(XTextEdit);
    return XVTABLE_DEFAULT;
}

void XTextBrowser_init(XTextBrowser* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XTextEdit_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XTextBrowser);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XPlainTextEdit_setReadOnly(self->m_base.m_editor, true);
    self->m_openLinks = true;
}

XTextBrowser* XTextBrowser_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XTextBrowser* self = (XTextBrowser*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTextBrowser_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XTextBrowser_setSource(XTextBrowser* self, const char* url)
{
    if (!self) return;
    strncpy(self->m_source, url ? url : "", sizeof(self->m_source) - 1);
    self->m_source[sizeof(self->m_source) - 1] = '\0';
    xtb_emitStr(self, (size_t)XTextBrowser_sourceChanged_signal, self->m_source);
}

const char* XTextBrowser_source(const XTextBrowser* self)
{
    return self ? self->m_source : "";
}

void XTextBrowser_setOpenLinks(XTextBrowser* self, bool open)
{
    if (!self) return;
    self->m_openLinks = open;
}

bool XTextBrowser_openLinks(const XTextBrowser* self)
{
    return self ? self->m_openLinks : false;
}

void XTextBrowser_backward(XTextBrowser* self) { (void)self; }
void XTextBrowser_forward(XTextBrowser* self) { (void)self; }
void XTextBrowser_home(XTextBrowser* self) { (void)self; }
void XTextBrowser_reload(XTextBrowser* self) { (void)self; }

void* XTextBrowser_sourceChanged_signal(XTextBrowser* self, const char* url)
{
    (void)self; (void)url;
    return (void*)(size_t)XTextBrowser_sourceChanged_signal;
}

void* XTextBrowser_backwardAvailable_signal(XTextBrowser* self, bool available)
{
    (void)self; (void)available;
    return (void*)(size_t)XTextBrowser_backwardAvailable_signal;
}

void* XTextBrowser_forwardAvailable_signal(XTextBrowser* self, bool available)
{
    (void)self; (void)available;
    return (void*)(size_t)XTextBrowser_forwardAvailable_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON */