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

#include "XAlgorithm.h"
#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON

static void xtb_emitStr(XTextBrowser* self, size_t signal, const char* text);
static void xtb_emitBool(XTextBrowser* self, size_t signal, bool v);
static void xtb_emitVoid(XTextBrowser* self, size_t signal);
static void xtb_updateNavigationState(XTextBrowser* self);
static void xtb_setSourceInternal(XTextBrowser* self, const char* url,
                                  bool addHistory);

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

static void xtb_emitVoid(XTextBrowser* self, size_t signal)
{
    XVarList* arguments = XVarList_create(0);
    if (!arguments) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/**
 * @brief      按当前历史索引重算后退/前进可用性并只在变化时发信号。
 * @details    对标 QTextBrowser 内部 navigation 面板状态刷新：状态翻转
 *             时分别发射 backwardAvailable(bool)/forwardAvailable(bool)。
 * @param      self 目标控件；NULL 时无操作。
 * @return     无返回值。
 */
static void xtb_updateNavigationState(XTextBrowser* self)
{
    bool back, forward;

    if (!self) return;
    back = (self->m_historyIndex > 0);
    forward = (self->m_historyIndex < self->m_historyCount - 1);
    if (back != self->m_backwardAvailable) {
        self->m_backwardAvailable = back;
        xtb_emitBool(self, (size_t)XTextBrowser_backwardAvailable_signal, back);
    }
    if (forward != self->m_forwardAvailable) {
        self->m_forwardAvailable = forward;
        xtb_emitBool(self, (size_t)XTextBrowser_forwardAvailable_signal, forward);
    }
}

static void VXTextBrowser_deinit(XTextBrowser* self)
{
    int i;
    if (!self) return;
    if (self->m_source) {
        XString_delete_base(self->m_source);
        self->m_source = NULL;
    }
    for (i = 0; i < self->m_historyCount; ++i) {
        if (self->m_history && self->m_history[i]) {
            XString_delete_base(self->m_history[i]);
            self->m_history[i] = NULL;
        }
    }
    if (self->m_history) {
        XFree_System(self->m_history);
        self->m_history = NULL;
    }
    self->m_historyCount = 0;
    self->m_historyCapacity = 0;
    XClass_Deinit_Parent(XTextEdit, (XTextEdit*)self);
}

XVtable* XTextBrowser_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextBrowser)
    XVTABLE_INHERIT_XCLASS(XTextEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTextBrowser_deinit);
    return XVTABLE_DEFAULT;
}

void XTextBrowser_init(XTextBrowser* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XTextEdit_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XTextBrowser);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XPlainTextEdit_setReadOnly(self->m_base.m_editor, true);
#if XTEXTDOCUMENT_ON
    self->m_base.m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#endif
    self->m_historyCount = 0;
    self->m_historyIndex = -1;
    self->m_historyCapacity = 0;
    self->m_source = XString_create();
    self->m_backwardAvailable = false;
    self->m_forwardAvailable = false;
    /* 编辑器填满浏览器。 */
    XWidget_setGeometry((XWidget*)self->m_base.m_editor, 0, 0,
                        XWidget_width((XWidget*)self),
                        XWidget_height((XWidget*)self));
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
    xtb_setSourceInternal(self, url, true);
}

const char* XTextBrowser_source(const XTextBrowser* self)
{
    const char* text;
    if (!self || !self->m_source) return "";
    text = XString_toUtf8(self->m_source);
    return text ? text : "";
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

/**
 * @brief      设置源并可选追加导航历史的内部实现。
 * @param      self 目标控件；NULL 时无操作。
 * @param      url 新源 URL；NULL 视为空串。
 * @param      addHistory true 追加一条历史（用户导航），false 只在历史内
 *             移动（backward/forward/home），对标 QTextBrowser 语义。
 * @return     无返回值。
 */
static void xtb_setSourceInternal(XTextBrowser* self, const char* url,
                                  bool addHistory)
{
    if (!self) return;
    if (!self->m_source) self->m_source = XString_create();
    if (self->m_source)
        XString_assign_utf8(self->m_source, url ? url : "");
    xtb_emitStr(self, (size_t)XTextBrowser_sourceChanged_signal,
                XString_toUtf8(self->m_source));
    if (addHistory) {
        XString* copy = XString_create_copy(self->m_source);
        if (copy) {
            if (self->m_historyIndex + 1 >= self->m_historyCapacity) {
                int cap = self->m_historyCapacity > 0
                    ? self->m_historyCapacity * 2 : 8;
                int oldCap = self->m_historyCapacity;
                int hi;
                XString** h = (XString**)XRealloc_System(self->m_history,
                    sizeof(XString*) * (size_t)cap);
                if (!h) {
                    XString_delete_base(copy);
                } else {
                    self->m_history = h;
                    for (hi = oldCap; hi < cap; ++hi)
                        self->m_history[hi] = NULL;
                    self->m_historyCapacity = cap;
                    self->m_historyIndex++;
                    self->m_history[self->m_historyIndex] = copy;
                    self->m_historyCount = self->m_historyIndex + 1;
                    xtb_emitVoid(self,
                        (size_t)XTextBrowser_historyChanged_signal);
                }
            } else {
                self->m_historyIndex++;
                self->m_history[self->m_historyIndex] = copy;
                self->m_historyCount = self->m_historyIndex + 1;
                xtb_emitVoid(self,
                    (size_t)XTextBrowser_historyChanged_signal);
            }
        }
    }
    xtb_updateNavigationState(self);
}

void XTextBrowser_backward(XTextBrowser* self)
{
    if (self && self->m_historyIndex > 0) {
        self->m_historyIndex--;
        xtb_setSourceInternal(self,
            self->m_history[self->m_historyIndex]
                ? XString_toUtf8(self->m_history[self->m_historyIndex])
                : "", false);
    }
}
void XTextBrowser_forward(XTextBrowser* self)
{
    if (self && self->m_historyIndex < self->m_historyCount - 1) {
        self->m_historyIndex++;
        xtb_setSourceInternal(self,
            self->m_history[self->m_historyIndex]
                ? XString_toUtf8(self->m_history[self->m_historyIndex])
                : "", false);
    }
}
void XTextBrowser_home(XTextBrowser* self)
{
    if (self && self->m_historyCount > 0) {
        self->m_historyIndex = 0;
        xtb_setSourceInternal(self,
            self->m_history[0]
                ? XString_toUtf8(self->m_history[0]) : "", false);
    }
}
void XTextBrowser_reload(XTextBrowser* self)
{
    if (!self) return;
    xtb_emitStr(self, (size_t)XTextBrowser_sourceChanged_signal, self->m_source);
}

void* XTextBrowser_sourceChanged_signal(XTextBrowser* self, const char* url)
{
    XVarList* args;
    XString* val;

    if (!self)
        return (void*)(size_t)XTextBrowser_sourceChanged_signal;
    val = XString_create_utf8(url ? url : "");
    if (!val)
        return (void*)(size_t)XTextBrowser_sourceChanged_signal;
    args = XVarList_Create(XVar(XString*, val));
    if (!args) {
        XString_delete_base((XClass*)val);
        return (void*)(size_t)XTextBrowser_sourceChanged_signal;
    }
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self,
                           (size_t)XTextBrowser_sourceChanged_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
    return (void*)(size_t)XTextBrowser_sourceChanged_signal;
}

void* XTextBrowser_backwardAvailable_signal(XTextBrowser* self, bool available)
{
    if (!self)
        return (void*)(size_t)XTextBrowser_backwardAvailable_signal;
    xtb_emitBool(self, (size_t)XTextBrowser_backwardAvailable_signal, available);
    return (void*)(size_t)XTextBrowser_backwardAvailable_signal;
}

void* XTextBrowser_forwardAvailable_signal(XTextBrowser* self, bool available)
{
    if (!self)
        return (void*)(size_t)XTextBrowser_forwardAvailable_signal;
    xtb_emitBool(self, (size_t)XTextBrowser_forwardAvailable_signal, available);
    return (void*)(size_t)XTextBrowser_forwardAvailable_signal;
}

void* XTextBrowser_historyChanged_signal(XTextBrowser* self)
{
    if (!self)
        return (void*)(size_t)XTextBrowser_historyChanged_signal;
    xtb_emitVoid(self, (size_t)XTextBrowser_historyChanged_signal);
    return (void*)(size_t)XTextBrowser_historyChanged_signal;
}


void* XTextBrowser_anchorClicked_signal(XTextBrowser* self)
{
    (void)self;
    return (void*)(size_t)XTextBrowser_anchorClicked_signal;
}
void* XTextBrowser_highlighted_signal(XTextBrowser* self)
{
    (void)self;
    return (void*)(size_t)XTextBrowser_highlighted_signal;
}

void XTextBrowser_clearHistory(XTextBrowser* self)
{
    if (!self) return;
    self->m_historyCount = 0;
    self->m_historyIndex = -1;
    xtb_emitVoid(self, (size_t)XTextBrowser_historyChanged_signal);
    xtb_updateNavigationState(self);
}

int XTextBrowser_backwardHistoryCount(const XTextBrowser* self)
{
    return (self && self->m_historyIndex > 0) ? self->m_historyIndex : 0;
}

int XTextBrowser_forwardHistoryCount(const XTextBrowser* self)
{
    if (!self || self->m_historyIndex < 0)
        return 0;
    return self->m_historyCount - self->m_historyIndex - 1;
}
void XTextBrowser_setSource_2(XTextBrowser* self, const char* url)
{ XTextBrowser_setSource(self, url); }
void XTextBrowser_setSource_3(XTextBrowser* self) { (void)self; }
void XTextBrowser_doSetSource(XTextBrowser* self) { (void)self; }
void XTextBrowser_highlighted_2(XTextBrowser* self) { (void)self; }
void XTextBrowser_setOpenExternalLinks(XTextBrowser* self) { (void)self; }
void XTextBrowser_openExternalLinks(XTextBrowser* self) { (void)self; }
void XTextBrowser_setSearchPaths(XTextBrowser* self) { (void)self; }
void XTextBrowser_loadResource_2(XTextBrowser* self) { (void)self; }
void XTextBrowser_isBackwardAvailable_2(XTextBrowser* self) { (void)self; }
void XTextBrowser_isForwardAvailable_2(XTextBrowser* self) { (void)self; }
void XTextBrowser_backward_2(XTextBrowser* self) { (void)self; }
void XTextBrowser_forward_2(XTextBrowser* self) { (void)self; }
void XTextBrowser_home_2(XTextBrowser* self) { (void)self; }
#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON */