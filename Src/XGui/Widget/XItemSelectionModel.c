/**
 * @file       XItemSelectionModel.c
 * @brief      XItemSelectionModel 选择模型实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

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

/** @brief 在选中集合中定位编码值；未命中返回 -1。 */
static int xism_findIndex(const XItemSelectionModel* self, int enc)
{
    int i;
    int n;
    if (!self || !self->m_selected) return -1;
    n = (int)XVector_size_base((const XContainer*)self->m_selected);
    for (i = 0; i < n; ++i) {
        if (XVector_At_Base(self->m_selected, (int64_t)i, int) == enc)
            return i;
    }
    return -1;
}

/** @brief 对矩形区间施加选择命令（Qt select(QItemSelection, command)
 *        的平铺等价物；qitemselectionmodel.cpp:1284-1330）。
 *        区间行列归一后逐格施加；Clear 先清空既有选区；仅集合实际
 *        变化时发射一次 selectionChanged（对标 Qt 单次发射）。 */
static bool xism_selectRect(XItemSelectionModel* self, int topRow,
                            int leftCol, int bottomRow, int rightCol,
                            int command)
{
    int r;
    int c;
    bool changed = false;
    if (!self || !self->m_selected) return false;
    if (command == XItemSelectionModelSelectionFlag_NoUpdate) return false;
    /* 区间归一（对标 QItemSelectionRange 的 topLeft/bottomRight 收敛）。 */
    if (topRow > bottomRow) { int t = topRow; topRow = bottomRow; bottomRow = t; }
    if (leftCol > rightCol) { int t = leftCol; leftCol = rightCol; rightCol = t; }
    /* Clear 先于区间有效性判定：Qt 的 select(无效索引, Clear|ClearAndSelect)
     * 以空 QItemSelection 施加命令，Clear 分支仍清空既有选区
     * （qitemselectionmodel.cpp:1300-1327 + QItemSelection 无效索引
     * 构造为空集）。 */
    if (command & XItemSelectionModelSelectionFlag_Clear) {
        if ((int)XVector_size_base((const XContainer*)self->m_selected) > 0) {
            XVector_clear_base(self->m_selected);
            changed = true;
        }
    }
    if (topRow < 0 || leftCol < 0) {
        if (changed) xism_emitChanged(self);
        return changed;
    }
    for (r = topRow; r <= bottomRow; ++r) {
        for (c = leftCol; c <= rightCol; ++c) {
            int enc = XISM_ENC(r, c);
            int idx = xism_findIndex(self, enc);
            if (command & XItemSelectionModelSelectionFlag_Toggle) {
                if (idx >= 0) {
                    XVector_remove_base(self->m_selected, (int64_t)idx, 1);
                } else {
                    XVector_push_back_1_base(self->m_selected, &enc);
                }
                changed = true;
            } else if (command & XItemSelectionModelSelectionFlag_Select) {
                if (idx < 0) {
                    XVector_push_back_1_base(self->m_selected, &enc);
                    changed = true;
                }
            } else if (command & XItemSelectionModelSelectionFlag_Deselect) {
                if (idx >= 0) {
                    XVector_remove_base(self->m_selected, (int64_t)idx, 1);
                    changed = true;
                }
            }
        }
    }
    /* 单次发射（对标 Qt select(range, command) 一次 selectionChanged）。 */
    if (changed) xism_emitChanged(self);
    return changed;
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
                                int command)
{
    int topRow;
    int leftCol;
    int bottomRow;
    int rightCol;
    if (!self || !self->m_selected) return false;
    if (command == XItemSelectionModelSelectionFlag_NoUpdate) return false;
    /* 无效索引：Qt 的 select(QModelIndex(), command) 以空 QItemSelection
     * 施加命令（仅 Clear 分支生效，qitemselectionmodel.cpp:1300-1327）。 */
    if (row < 0 || col < 0)
        return xism_selectRect(self, -1, -1, -1, -1, command);
    topRow = row;
    bottomRow = row;
    leftCol = col;
    rightCol = col;
    /* Rows/Columns 展开（对标 Qt expandSelection，
     * qitemselectionmodel.cpp:1309-1311）。 */
    if (command & XItemSelectionModelSelectionFlag_Rows) {
        int cols = self->m_model
                       ? XAbstractItemModel_columnCount(self->m_model)
                       : 0;
        if (cols > 0) { leftCol = 0; rightCol = cols - 1; }
    }
    if (command & XItemSelectionModelSelectionFlag_Columns) {
        int rows = self->m_model ? XAbstractItemModel_rowCount(self->m_model)
                                 : 0;
        if (rows > 0) { topRow = 0; bottomRow = rows - 1; }
    }
    return xism_selectRect(self, topRow, leftCol, bottomRow, rightCol,
                           command);
}

bool XItemSelectionModel_selectSelection(XItemSelectionModel* self, int topRow,
                                         int leftCol, int bottomRow,
                                         int rightCol, int command)
{
    if (!self || !self->m_selected) return false;
    if (command == XItemSelectionModelSelectionFlag_NoUpdate) return false;
    return xism_selectRect(self, topRow, leftCol, bottomRow, rightCol,
                           command);
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

bool XItemSelectionModel_selectRow(XItemSelectionModel* self, int row)
{
    XAbstractItemModel* model;
    if (!self || row < 0) return false;
    model = self->m_model;
    if (!model) return false;
    if (row >= XAbstractItemModel_rowCount(model)) return false;
    if (XAbstractItemModel_columnCount(model) <= 0) return false;
    return XItemSelectionModel_selectSelection(
        self, row, 0, row, XAbstractItemModel_columnCount(model) - 1,
        XItemSelectionModelSelectionFlag_Select);
}

bool XItemSelectionModel_selectColumn(XItemSelectionModel* self, int column)
{
    XAbstractItemModel* model;
    if (!self || column < 0) return false;
    model = self->m_model;
    if (!model) return false;
    if (column >= XAbstractItemModel_columnCount(model)) return false;
    if (XAbstractItemModel_rowCount(model) <= 0) return false;
    return XItemSelectionModel_selectSelection(
        self, 0, column, XAbstractItemModel_rowCount(model) - 1, column,
        XItemSelectionModelSelectionFlag_Select);
}

bool XItemSelectionModel_clearSelection(XItemSelectionModel* self)
{
    int n;
    if (!self || !self->m_selected) return false;
    n = (int)XVector_size_base((const XContainer*)self->m_selected);
    if (n == 0) return false;
    XVector_clear_base(self->m_selected);
    xism_emitChanged(self);
    return true;
}

bool XItemSelectionModel_clear(XItemSelectionModel* self)
{
    bool selectionChanged = XItemSelectionModel_clearSelection(self);
    /* 对标 Qt QItemSelectionModel::clear（qitemselectionmodel.cpp:1335-1339）：
     * clear = clearSelection() + clearCurrentIndex()。 */
    XItemSelectionModel_clearCurrentIndex(self);
    return selectionChanged;
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

void XItemSelectionModel_setModel(XItemSelectionModel* self,
                                  XAbstractItemModel* model)
{
    if (self) self->m_model = model;
}

XAbstractItemModel* XItemSelectionModel_model(const XItemSelectionModel* self)
{ return self ? self->m_model : NULL; }

void XItemSelectionModel_setCurrentIndex(XItemSelectionModel* self,
                                         int row, int col, int command)
{
    int prevRow;
    int prevCol;
    if (!self) return;
    prevRow = self->m_currentRow;
    prevCol = self->m_currentCol;
    /* 对标 Qt QItemSelectionModel::setCurrentIndex
     * （qitemselectionmodel.cpp:1389-1409）：索引未变时仍按 command
     * 施加选择命令（可"保持当前格但改变选择"），随后返回，不发射
     * currentChanged 族信号。 */
    if (row == prevRow && col == prevCol) {
        if (command != XItemSelectionModelSelectionFlag_NoUpdate)
            XItemSelectionModel_select(self, row, col, command);
        return;
    }
    self->m_currentRow = row;
    self->m_currentCol = col;
    /* Qt 序（qitemselectionmodel.cpp:1397-1400）：先写当前索引，再施加
     * 选择命令（其 selectionChanged 先于 currentChanged 发出），最后
     * 发射 currentChanged 族信号。 */
    if (command != XItemSelectionModelSelectionFlag_NoUpdate)
        XItemSelectionModel_select(self, row, col, command);
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

void XItemSelectionModel_clearCurrentIndex(XItemSelectionModel* self)
{
    int prevRow;
    int prevCol;
    if (!self) return;
    prevRow = self->m_currentRow;
    prevCol = self->m_currentCol;
    if (prevRow < 0 && prevCol < 0) return;
    self->m_currentRow = -1;
    self->m_currentCol = -1;
    /* 对标 Qt clearCurrentIndex（qitemselectionmodel.cpp:1344-1353）：
     * 复位当前索引并发射 currentChanged/currentRowChanged/
     * currentColumnChanged。 */
    xism_emitCurrentChanged(self, -1, -1, prevRow, prevCol);
    xism_emitInt2(self, (size_t)XItemSelectionModel_currentRowChanged_signal,
                  -1, prevRow);
    xism_emitInt2(self,
                  (size_t)XItemSelectionModel_currentColumnChanged_signal,
                  -1, prevCol);
}

bool XItemSelectionModel_currentIndex(const XItemSelectionModel* self,
                                      int* outRow, int* outCol)
{
    if (outRow) *outRow = self ? self->m_currentRow : -1;
    if (outCol) *outCol = self ? self->m_currentCol : -1;
    return self && self->m_currentRow >= 0 && self->m_currentCol >= 0;
}

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

#endif /* XGUI_ON */
