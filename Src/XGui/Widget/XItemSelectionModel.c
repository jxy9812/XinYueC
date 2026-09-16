/**
 * @file       XItemSelectionModel.c
 * @brief      XItemSelectionModel 选择模型实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XItemSelectionModel.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XISM_ENC(row, col) ((((int)(row)) << 16) | ((int)(col)))
#define XISM_ROW(v)        (((v) >> 16) & 0xFFFF)
#define XISM_COL(v)        ((v) & 0xFFFF)

static void xism_emitChanged(XItemSelectionModel* self)
{
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XItemSelectionModel_selectionChanged_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

static void VXItemSelectionModel_deinit(XItemSelectionModel* self)
{
    if (!self) return;
    if (self->m_selected) {
        XVector_delete_base(self->m_selected);
        self->m_selected = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXItemSelectionModel_copy(XItemSelectionModel* self,
                                      const XItemSelectionModel* other)
{
    int i;
    int n;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XItemSelectionModel_init(self);
    XClass_Parent(XObject, EXClass_Copy,
                  void(*)(XObject*, const XObject*))((XObject*)self,
                                                     (const XObject*)other);
    XVector_clear_base(self->m_selected);
    n = other->m_selected
            ? (int)XVector_size_base((const XContainer*)other->m_selected)
            : 0;
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(other->m_selected, (int64_t)i, int);
        XVector_push_back_1_base(self->m_selected, &v);
    }
    self->m_currentRow = other->m_currentRow;
    self->m_currentCol = other->m_currentCol;
}

static void VXItemSelectionModel_move(XItemSelectionModel* self,
                                      XItemSelectionModel* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XItemSelectionModel_init(self);
    XClass_Parent(XObject, EXClass_Move,
                  void(*)(XObject*, XObject*))((XObject*)self,
                                               (XObject*)other);
    if (self->m_selected) XVector_delete_base(self->m_selected);
    self->m_selected = other->m_selected;
    self->m_currentRow = other->m_currentRow;
    self->m_currentCol = other->m_currentCol;
    other->m_selected = XVector_Create(int);
    other->m_currentRow = -1;
    other->m_currentCol = -1;
}

XVtable* XItemSelectionModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XItemSelectionModel)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXItemSelectionModel_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXItemSelectionModel_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXItemSelectionModel_move);
    return XVTABLE_DEFAULT;
}

void XItemSelectionModel_init(XItemSelectionModel* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XItemSelectionModel);
    self->m_selected = XVector_Create(int);
    self->m_currentRow = -1;
    self->m_currentCol = -1;
}

XItemSelectionModel* XItemSelectionModel_create_ex(XMemoryType memory)
{
    XItemSelectionModel* self =
        (XItemSelectionModel*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XItemSelectionModel_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

bool XItemSelectionModel_select(XItemSelectionModel* self, int row, int col,
                                bool selected)
{
    int enc;
    int i;
    int n;
    if (!self || !self->m_selected || row < 0 || col < 0) return false;
    enc = XISM_ENC(row, col);
    n = (int)XVector_size_base((const XContainer*)self->m_selected);
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(self->m_selected, (int64_t)i, int);
        if (v == enc) {
            if (selected) return false;
            XVector_remove_base(self->m_selected, (int64_t)i, 1);
            xism_emitChanged(self);
            return true;
        }
    }
    if (!selected) return false;
    XVector_push_back_1_base(self->m_selected, &enc);
    xism_emitChanged(self);
    return true;
}

bool XItemSelectionModel_isSelected(const XItemSelectionModel* self,
                                    int row, int col)
{
    int enc;
    int i;
    int n;
    if (!self || !self->m_selected || row < 0 || col < 0) return false;
    enc = XISM_ENC(row, col);
    n = (int)XVector_size_base((const XContainer*)self->m_selected);
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(self->m_selected, (int64_t)i, int);
        if (v == enc) return true;
    }
    return false;
}

bool XItemSelectionModel_clear(XItemSelectionModel* self)
{
    int n;
    if (!self || !self->m_selected) return false;
    n = (int)XVector_size_base((const XContainer*)self->m_selected);
    if (n == 0) return false;
    XVector_clear_base(self->m_selected);
    xism_emitChanged(self);
    return true;
}

int XItemSelectionModel_selectedCount(const XItemSelectionModel* self)
{
    return (self && self->m_selected)
               ? (int)XVector_size_base((const XContainer*)self->m_selected)
               : 0;
}

bool XItemSelectionModel_selectedAt(const XItemSelectionModel* self,
                                    int index, int* outRow, int* outCol)
{
    int v;
    int n;
    if (!self || !self->m_selected || index < 0) return false;
    n = (int)XVector_size_base((const XContainer*)self->m_selected);
    if (index >= n) return false;
    v = XVector_At_Base(self->m_selected, (int64_t)index, int);
    if (outRow) *outRow = XISM_ROW(v);
    if (outCol) *outCol = XISM_COL(v);
    return true;
}

void XItemSelectionModel_setCurrentIndex(XItemSelectionModel* self,
                                         int row, int col)
{
    if (!self) return;
    self->m_currentRow = row;
    self->m_currentCol = col;
}

int XItemSelectionModel_currentRow(const XItemSelectionModel* self)
{ return self ? self->m_currentRow : -1; }

int XItemSelectionModel_currentColumn(const XItemSelectionModel* self)
{ return self ? self->m_currentCol : -1; }

void* XItemSelectionModel_selectionChanged_signal(
    XItemSelectionModel* self)
{
    xism_emitChanged(self);
    return (void*)(size_t)XItemSelectionModel_selectionChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
