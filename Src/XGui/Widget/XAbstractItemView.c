#include "XAbstractItemView.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XEventType.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

static void VXAbstractItemView_deinit(XAbstractItemView* self);
static void VXAbstractItemView_mousePressEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_mouseReleaseEvent(XWidget* self,
                                                 XEvent* event);
static void VXAbstractItemView_mouseDoubleClickEvent(XWidget* self,
                                                     XEvent* event);
static void VXAbstractItemView_mouseMoveEvent(XWidget* self, XEvent* event);
static bool VXAbstractItemView_indexAt(const XAbstractItemView* self,
                                       int x, int y,
                                       int* outRow, int* outCol);

XVtable* XAbstractItemView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractItemView)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractItemView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXAbstractItemView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXAbstractItemView_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXAbstractItemView_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXAbstractItemView_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_IndexAt,
                             VXAbstractItemView_indexAt);
    return XVTABLE_DEFAULT;
}

void XAbstractItemView_init(XAbstractItemView* self, XWidget* parent,
                            XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XAbstractItemView);
    self->m_currentRow = -1;
    self->m_currentColumn = -1;
    self->m_selectionMode = XAbstractItemViewSelectionMode_ExtendedSelection;
    self->m_selectionBehavior =
        XAbstractItemViewSelectionBehavior_SelectItems;
    self->m_editTriggers = XAbstractItemViewEditTrigger_CurrentChanged |
        XAbstractItemViewEditTrigger_DoubleClicked |
        XAbstractItemViewEditTrigger_EditKeyPressed;
    self->m_alternatingRowColors = false;
    self->m_autoScroll = true;
    self->m_model = NULL;
    self->m_selectionModel = XItemSelectionModel_create();
    self->m_rootRow = -1;
    self->m_rootCol = -1;
    self->m_iconW = 16;
    self->m_iconH = 16;
}

XAbstractItemView* XAbstractItemView_create_ex(XMemoryType memory,
                                               XWidget* parent,
                                               XWidgetFlags flags)
{
    XAbstractItemView* self =
        (XAbstractItemView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractItemView_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXAbstractItemView_deinit(XAbstractItemView* self)
{
    if (!self) return;
    if (self->m_selectionModel) {
        XItemSelectionModel_delete_base(self->m_selectionModel);
        self->m_selectionModel = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

void XAbstractItemView_setCurrentIndex(XAbstractItemView* self, int row,
                                       int column)
{
    if (!self) return;
    self->m_currentRow = row;
    self->m_currentColumn = column;
}

int XAbstractItemView_currentRow(const XAbstractItemView* self)
{ return self ? self->m_currentRow : -1; }
int XAbstractItemView_currentColumn(const XAbstractItemView* self)
{ return self ? self->m_currentColumn : -1; }

void XAbstractItemView_setSelectionMode(XAbstractItemView* self, int mode)
{ if (self) self->m_selectionMode = mode; }
int XAbstractItemView_selectionMode(const XAbstractItemView* self)
{ return self ? self->m_selectionMode : 0; }

void XAbstractItemView_setSelectionBehavior(XAbstractItemView* self,
                                            int behavior)
{ if (self) self->m_selectionBehavior = behavior; }
int XAbstractItemView_selectionBehavior(const XAbstractItemView* self)
{ return self ? self->m_selectionBehavior : 0; }

void XAbstractItemView_setEditTriggers(XAbstractItemView* self, int triggers)
{ if (self) self->m_editTriggers = triggers; }
int XAbstractItemView_editTriggers(const XAbstractItemView* self)
{ return self ? self->m_editTriggers : 0; }

void XAbstractItemView_setAlternatingRowColors(XAbstractItemView* self,
                                               bool enable)
{ if (self) self->m_alternatingRowColors = enable; }
bool XAbstractItemView_alternatingRowColors(const XAbstractItemView* self)
{ return self ? self->m_alternatingRowColors : false; }

void XAbstractItemView_setAutoScroll(XAbstractItemView* self, bool enable)
{ if (self) self->m_autoScroll = enable; }
bool XAbstractItemView_hasAutoScroll(const XAbstractItemView* self)
{ return self ? self->m_autoScroll : true; }

void XAbstractItemView_scrollTo(XAbstractItemView* self, int row, int column)
{
    /* 默认无滚动区域实现；派生视图可提升具体滚动。 */
    if (!self) return;
    (void)row;
    (void)column;
}

/* ==================== 模型与选择 ==================== */

XAbstractItemModel* XAbstractItemView_model(const XAbstractItemView* self)
{ return self ? self->m_model : NULL; }

void XAbstractItemView_setModel(XAbstractItemView* self,
                                XAbstractItemModel* model)
{
    if (!self) return;
    self->m_model = model;
    if (model) {
        if (self->m_currentRow >= model->m_rows)
            self->m_currentRow = model->m_rows - 1;
        if (self->m_currentColumn >= model->m_cols)
            self->m_currentColumn = model->m_cols - 1;
    } else {
        self->m_currentRow = -1;
        self->m_currentColumn = -1;
    }
    XWidget_update((XWidget*)self);
}

XItemSelectionModel* XAbstractItemView_selectionModel(
    const XAbstractItemView* self)
{
    return self ? self->m_selectionModel : NULL;
}

void XAbstractItemView_setSelectionModel(XAbstractItemView* self,
                                         XItemSelectionModel* selectionModel)
{
    if (!self) return;
    if (self->m_selectionModel && self->m_selectionModel != selectionModel)
        XItemSelectionModel_delete_base(self->m_selectionModel);
    self->m_selectionModel = selectionModel;
    if (!self->m_selectionModel)
        self->m_selectionModel = XItemSelectionModel_create();
}

int XAbstractItemView_rootRow(const XAbstractItemView* self)
{ return self ? self->m_rootRow : -1; }

int XAbstractItemView_rootColumn(const XAbstractItemView* self)
{ return self ? self->m_rootCol : -1; }

void XAbstractItemView_setRootIndex(XAbstractItemView* self, int row, int col)
{
    if (!self) return;
    self->m_rootRow = row;
    self->m_rootCol = col;
}

/** @brief 基类命中实现：默认网格布局（行高 24、列宽 80）。 */
static bool VXAbstractItemView_indexAt(const XAbstractItemView* self,
                                       int x, int y,
                                       int* outRow, int* outCol)
{
    int row;
    int col;
    if (outRow) *outRow = -1;
    if (outCol) *outCol = -1;
    if (!self) return false;
    row = y / 24;
    col = x / 80;
    if (row < 0 || col < 0) return false;
    if (self->m_model) {
        if (row >= self->m_model->m_rows) return false;
        if (col >= self->m_model->m_cols) return false;
    }
    if (outRow) *outRow = row;
    if (outCol) *outCol = col;
    return true;
}

bool XAbstractItemView_indexAt_base(const XAbstractItemView* self, int x,
                                    int y, int* outRow, int* outCol)
{
    bool (*fn)(const XAbstractItemView*, int, int, int*, int*);
    if (!self) return false;
    fn = (bool (*)(const XAbstractItemView*, int, int, int*, int*))
        XVtableGetFunc(XClassGetVtable((XClass*)self),
                       EXAbstractItemView_IndexAt, void*);
    if (!fn) return false;
    return fn(self, x, y, outRow, outCol);
}

bool XAbstractItemView_visualRect(const XAbstractItemView* self,
                                  int row, int col, XRect* out)
{
    if (!self || !out || row < 0 || col < 0) return false;
    XRect_init(out, col * 80, row * 24, 80, 24);
    return true;
}

void XAbstractItemView_setIconSize(XAbstractItemView* self, int w, int h)
{
    XVarList* args;
    if (!self || w <= 0 || h <= 0) return;
    if (self->m_iconW == w && self->m_iconH == h) return;
    self->m_iconW = w;
    self->m_iconH = h;
    args = XVarList_Create(XVar(int, w), XVar(int, h));
    if (self && ((XObject*)self)->m_signalSlot && args) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XAbstractItemView_iconSizeChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else if (args) {
        XVarList_delete(args);
    }
    XWidget_update((XWidget*)self);
}

int XAbstractItemView_iconWidth(const XAbstractItemView* self)
{ return self ? self->m_iconW : 0; }

int XAbstractItemView_iconHeight(const XAbstractItemView* self)
{ return self ? self->m_iconH : 0; }

/* ==================== 信号发射助手 ==================== */

static void xaiv_emitIndex(XAbstractItemView* self, size_t signal,
                           int row, int col)
{
    XVarList* args = XVarList_Create(XVar(int, row), XVar(int, col));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xaiv_emitVoid(XAbstractItemView* self, size_t signal)
{
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    }
}

/* ==================== 鼠标事件（点击选择 + 信号） ==================== */

static void VXAbstractItemView_mousePressEvent(XWidget* self, XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    int col;
    if (!view || !event) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (XAbstractItemView_indexAt_base(view, pos.x, pos.y, &row, &col)) {
        XAbstractItemView_setCurrentIndex(view, row, col);
        if (view->m_selectionModel) {
            if (view->m_selectionMode !=
                XAbstractItemViewSelectionMode_NoSelection) {
                XItemSelectionModel_select(view->m_selectionModel,
                                           row, col, true);
                XItemSelectionModel_setCurrentIndex(
                    view->m_selectionModel, row, col);
            }
        }
        xaiv_emitIndex(view, (size_t)XAbstractItemView_pressed_signal,
                       row, col);
    }
    XEvent_accept(event);
}

static void VXAbstractItemView_mouseReleaseEvent(XWidget* self,
                                                 XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    int col;
    if (!view || !event) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (XAbstractItemView_indexAt_base(view, pos.x, pos.y, &row, &col)) {
        xaiv_emitIndex(view, (size_t)XAbstractItemView_clicked_signal,
                       row, col);
        xaiv_emitIndex(view, (size_t)XAbstractItemView_activated_signal,
                       row, col);
    }
    XEvent_accept(event);
}

static void VXAbstractItemView_mouseDoubleClickEvent(XWidget* self,
                                                     XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    int col;
    if (!view || !event) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (XAbstractItemView_indexAt_base(view, pos.x, pos.y, &row, &col)) {
        xaiv_emitIndex(view,
                       (size_t)XAbstractItemView_doubleClicked_signal,
                       row, col);
    }
    XEvent_accept(event);
}

static void VXAbstractItemView_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    int col;
    if (!view || !event) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (XAbstractItemView_indexAt_base(view, pos.x, pos.y, &row, &col)) {
        xaiv_emitIndex(view, (size_t)XAbstractItemView_entered_signal,
                       row, col);
        xaiv_emitVoid(view,
                      (size_t)XAbstractItemView_viewportEntered_signal);
    }
    XEvent_accept(event);
}

/* ==================== 信号 ==================== */

void* XAbstractItemView_pressed_signal(XAbstractItemView* self,
                                       int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_pressed_signal, row, col);
    return (void*)(size_t)XAbstractItemView_pressed_signal;
}

void* XAbstractItemView_clicked_signal(XAbstractItemView* self,
                                       int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_clicked_signal, row, col);
    return (void*)(size_t)XAbstractItemView_clicked_signal;
}

void* XAbstractItemView_doubleClicked_signal(XAbstractItemView* self,
                                             int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_doubleClicked_signal,
                   row, col);
    return (void*)(size_t)XAbstractItemView_doubleClicked_signal;
}

void* XAbstractItemView_activated_signal(XAbstractItemView* self,
                                         int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_activated_signal,
                   row, col);
    return (void*)(size_t)XAbstractItemView_activated_signal;
}

void* XAbstractItemView_entered_signal(XAbstractItemView* self,
                                       int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_entered_signal, row, col);
    return (void*)(size_t)XAbstractItemView_entered_signal;
}

void* XAbstractItemView_viewportEntered_signal(XAbstractItemView* self)
{
    xaiv_emitVoid(self, (size_t)XAbstractItemView_viewportEntered_signal);
    return (void*)(size_t)XAbstractItemView_viewportEntered_signal;
}

void* XAbstractItemView_iconSizeChanged_signal(XAbstractItemView* self,
                                               int width, int height)
{
    XVarList* args;
    (void)width; (void)height;
    args = XVarList_Create(XVar(int, width), XVar(int, height));
    if (!args) return (void*)(size_t)XAbstractItemView_iconSizeChanged_signal;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XAbstractItemView_iconSizeChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
    return (void*)(size_t)XAbstractItemView_iconSizeChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
