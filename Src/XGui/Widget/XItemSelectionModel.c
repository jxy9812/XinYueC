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

static void xism_emitVoid(XItemSelectionModel* self, size_t signal)
{
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    }
}

static void xism_emitInt2(XItemSelectionModel* self, size_t signal, int a,
                          int b)
{
    XVarList* args = XVarList_Create(XVar(int, a), XVar(int, b));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xism_emitCurrentChanged(XItemSelectionModel* self, int row,
                                    int col, int prevRow, int prevCol)
{
    XVarList* args = XVarList_Create(XVar(int, row), XVar(int, col),
                                     XVar(int, prevRow),
                                     XVar(int, prevCol));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XItemSelectionModel_currentChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void VXItemSelectionModel_deinit(XItemSelectionModel* self)
{
    if (!self) return;
    if (self->m_selected) {
        XClassDelete(self->m_selected);
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
    /* 绑定模型为借用指针，浅拷贝即可（同 XHeaderView m_model 范式）。 */
    self->m_model = other->m_model;
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
    if (self->m_selected) XClassDelete(self->m_selected);
    self->m_selected = other->m_selected;
    self->m_model = other->m_model;
    self->m_currentRow = other->m_currentRow;
    self->m_currentCol = other->m_currentCol;
    other->m_selected = XVector_Create(int);
    other->m_model = NULL;
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

bool XItemSelectionModel_selectRange(XItemSelectionModel* self, int topRow,
                                     int leftCol, int bottomRow,
                                     int rightCol)
{
    int r;
    int c;
    int startCount;
    bool changed = false;
    if (!self || !self->m_selected) return false;
    /* 区间归一（对标 QItemSelectionRange 的 topLeft/bottomRight 收敛）。 */
    if (topRow > bottomRow) { int t = topRow; topRow = bottomRow; bottomRow = t; }
    if (leftCol > rightCol) { int t = leftCol; leftCol = rightCol; rightCol = t; }
    if (topRow < 0 || leftCol < 0) return false;
    startCount = (int)XVector_size_base((const XContainer*)self->m_selected);
    for (r = topRow; r <= bottomRow; ++r) {
        for (c = leftCol; c <= rightCol; ++c) {
            int enc = XISM_ENC(r, c);
            int i;
            int n = (int)XVector_size_base(
                (const XContainer*)self->m_selected);
            bool found = false;
            for (i = 0; i < n; ++i) {
                if (XVector_At_Base(self->m_selected, (int64_t)i, int) ==
                    enc) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                XVector_push_back_1_base(self->m_selected, &enc);
                changed = true;
            }
        }
    }
    /* 单次发射（对标 Qt select(range, Select) 一次 selectionChanged）。 */
    if (changed &&
        (int)XVector_size_base((const XContainer*)self->m_selected) !=
            startCount)
        xism_emitChanged(self);
    return changed;
}

bool XItemSelectionModel_selectRow(XItemSelectionModel* self, int row)
{
    XAbstractItemModel* model;
    if (!self || row < 0) return false;
    model = self->m_model;
    if (!model) return false;
    if (row >= XAbstractItemModel_rowCount(model)) return false;
    if (XAbstractItemModel_columnCount(model) <= 0) return false;
    return XItemSelectionModel_selectRange(self, row, 0, row,
                                           XAbstractItemModel_columnCount(
                                               model) - 1);
}

bool XItemSelectionModel_selectColumn(XItemSelectionModel* self, int column)
{
    XAbstractItemModel* model;
    if (!self || column < 0) return false;
    model = self->m_model;
    if (!model) return false;
    if (column >= XAbstractItemModel_columnCount(model)) return false;
    if (XAbstractItemModel_rowCount(model) <= 0) return false;
    return XItemSelectionModel_selectRange(self, 0, column,
                                           XAbstractItemModel_rowCount(model)
                                               - 1,
                                           column);
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

bool XItemSelectionModel_clearSelection(XItemSelectionModel* self)
{
    /* 对标 Qt clearSelection：仅清选择集合、保留当前索引（本库既有
     * clear 的行为即此语义，此处为 Qt 命名对齐别名）。 */
    return XItemSelectionModel_clear(self);
}

bool XItemSelectionModel_hasSelection(const XItemSelectionModel* self)
{
    return XItemSelectionModel_selectedCount(self) > 0;
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

void XItemSelectionModel_setModel(XItemSelectionModel* self,
                                  XAbstractItemModel* model)
{
    if (self) self->m_model = model;
}

XAbstractItemModel* XItemSelectionModel_model(const XItemSelectionModel* self)
{ return self ? self->m_model : NULL; }

void XItemSelectionModel_setCurrentIndex(XItemSelectionModel* self,
                                         int row, int col)
{
    int prevRow;
    int prevCol;
    if (!self) return;
    prevRow = self->m_currentRow;
    prevCol = self->m_currentCol;
    if (row == prevRow && col == prevCol) return;
    self->m_currentRow = row;
    self->m_currentCol = col;
    /* 对标 QItemSelectionModel::setCurrentIndex 的发射序：
     * currentChanged(current, previous) 先行，行/列分别变化时再发射
     * currentRowChanged/currentColumnChanged（载荷为当前/前值）。 */
    xism_emitCurrentChanged(self, row, col, prevRow, prevCol);
    if (row != prevRow)
        xism_emitInt2(self,
                      (size_t)XItemSelectionModel_currentRowChanged_signal,
                      row, prevRow);
    if (col != prevCol)
        xism_emitInt2(self,
                      (size_t)XItemSelectionModel_currentColumnChanged_signal,
                      col, prevCol);
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

void* XItemSelectionModel_currentChanged_signal(XItemSelectionModel* self,
                                                int row, int col, int prevRow,
                                                int prevCol)
{
    xism_emitCurrentChanged(self, row, col, prevRow, prevCol);
    return (void*)(size_t)XItemSelectionModel_currentChanged_signal;
}

void* XItemSelectionModel_currentRowChanged_signal(XItemSelectionModel* self,
                                                   int row, int prevRow)
{
    xism_emitInt2(self,
                  (size_t)XItemSelectionModel_currentRowChanged_signal,
                  row, prevRow);
    return (void*)(size_t)XItemSelectionModel_currentRowChanged_signal;
}

void* XItemSelectionModel_currentColumnChanged_signal(
    XItemSelectionModel* self, int col, int prevCol)
{
    xism_emitInt2(self,
                  (size_t)XItemSelectionModel_currentColumnChanged_signal,
                  col, prevCol);
    return (void*)(size_t)XItemSelectionModel_currentColumnChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
