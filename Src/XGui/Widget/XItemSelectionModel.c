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
#include "XModelIndex.h"      /* §selrange 构造范围表的合成角标。 */
#include <stdlib.h>           /* qsort（rebuildSelection 坐标排序）。 */

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

/* ==================== §selrange 范围表辅助（Qt 命令历史模型） ====================
 * 取证：Qt 6.8 qitemselectionmodel.cpp:1284-1330（select）、:1816-1825
 * （selection）、qitemselectionmodel_p.h:66-83（finalize 与三段字段）。
 * 事实源 = m_ranges（已 finalize）+ m_currentSelection/m_currentCommand
 * （最后一条命令）；m_selected 平铺集仅为派生镜像（绘制/读热路径）。
 */

/** @brief 构造绑定模型下的根级合成角标基底（m_model 借用，parent 恒无效——
 *        平铺模型根级；与 §B2 XModelIndex 映射口径一致）。 */
static XModelIndex xism_baseIndex(const XItemSelectionModel* self)
{
    if (!self || !self->m_model) return XModelIndex_invalid();
    return XModelIndex_make(0, 0, 0, self->m_model);
}

/** @brief 把归一化后的矩形构造成单 range 的 XItemSelection（栈值，须 deinit）。
 *         两角经 XItemSelection_select 校验（同模型同父、四边归一）。 */
static void xism_makeRectSelection(const XItemSelectionModel* self, int top,
                                   int left, int bottom, int right,
                                   XItemSelection* out)
{
    XModelIndex base;
    XModelIndex tl;
    XModelIndex br;
    if (!out) return;
    (void)XItemSelection_init(out);
    if (!self || !self->m_model || top < 0 || left < 0) return;
    if (top > bottom || left > right) return;
    base = xism_baseIndex(self);
    tl = XModelIndex_withRowColumn(&base, top, left);
    br = XModelIndex_withRowColumn(&base, bottom, right);
    (void)XItemSelection_select(out, tl, br);
}

/** @brief 计算 selection() 视图 = ranges + merge(currentSelection,
 *         currentCommand)（Qt qitemselectionmodel.cpp:1816-1825 同款）。
 *         out 为已 init 的空集合；调用方用完 deinit。 */
static void xism_selectionView(const XItemSelectionModel* self,
                               XItemSelection* out)
{
    int i;
    int n;
    if (!self || !out) return;
    n = XItemSelection_count(&self->m_ranges);
    for (i = 0; i < n; ++i)
        (void)XItemSelection_append(out, XItemSelection_at(&self->m_ranges, i));
    (void)XItemSelection_merge(out, &self->m_currentSelection,
                               self->m_currentCommand);
}

/** @brief Qt QItemSelectionModelPrivate::finalize（qitemselectionmodel_p.h:
 *         66-71）：把当前命令选区并进固化范围表并清空当前选区。 */
static void xism_finalize(XItemSelectionModel* self)
{
    if (!self) return;
    (void)XItemSelection_merge(&self->m_ranges, &self->m_currentSelection,
                               self->m_currentCommand);
    if (!XItemSelection_isEmpty(&self->m_currentSelection))
        XItemSelection_clear(&self->m_currentSelection);
}

/** @brief 从 selection() 视图重建平铺镜像 m_selected（按 range 顺序、范围内
 *         行主序编码，与 Qt selectedIndexes 的顺序一致）。§selrange：镜像
 *         只作 O(1)/O(N) 快速通道，绝不反向决定范围表。 */
static void xism_rebuildMirror(XItemSelectionModel* self)
{
    XItemSelection view;
    int i;
    int n;
    if (!self || !self->m_selected) return;
    XVector_clear_base((XContainer*)self->m_selected);
    (void)XItemSelection_init(&view);
    xism_selectionView(self, &view);
    n = XItemSelection_count(&view);
    for (i = 0; i < n; ++i) {
        XItemSelectionRange r = XItemSelection_at(&view, i);
        int top = XItemSelectionRange_top(&r);
        int left = XItemSelectionRange_left(&r);
        int bottom = XItemSelectionRange_bottom(&r);
        int right = XItemSelectionRange_right(&r);
        int row;
        int col;
        if (!XItemSelectionRange_isValid(&r)) continue;
        for (row = top; row <= bottom; ++row) {
            for (col = left; col <= right; ++col) {
                int enc = XISM_ENC(row, col);
                (void)XVector_push_back_1_base(self->m_selected, &enc);
            }
        }
    }
    XItemSelection_deinit(&view);
}

/** @brief 清空三段范围表 + 平铺镜像（clear/reset/setModel 共用，静默）。 */
static void xism_clearAll(XItemSelectionModel* self)
{
    if (!self) return;
    XItemSelection_clear(&self->m_ranges);
    XItemSelection_clear(&self->m_currentSelection);
    self->m_currentCommand = XItemSelectionModelSelectionFlag_NoUpdate;
    if (self->m_selected) XVector_clear_base((XContainer*)self->m_selected);
}

/** @brief int 比较器（供 xism_sameCells 的 qsort）。 */
static int xism_intCompare(const void* a, const void* b)
{
    int x = *(const int*)a;
    int y = *(const int*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/** @brief 把选择集合展开成**已排序**的 (row<<16)|col 编码向量（栈值，
 *         调用方用 XClassDeinit 释放）。用于幂等判定。 */
static void xism_expandCells(const XItemSelection* sel, XVector* out)
{
    int i;
    int n = XItemSelection_count(sel);
    XVector_init(out, sizeof(int), false);
    for (i = 0; i < n; ++i) {
        XItemSelectionRange r = XItemSelection_at(sel, i);
        int top;
        int left;
        int bottom;
        int right;
        int row;
        int col;
        if (!XItemSelectionRange_isValid(&r)) continue;
        top = XItemSelectionRange_top(&r);
        left = XItemSelectionRange_left(&r);
        bottom = XItemSelectionRange_bottom(&r);
        right = XItemSelectionRange_right(&r);
        for (row = top; row <= bottom; ++row) {
            for (col = left; col <= right; ++col) {
                int enc = XISM_ENC(row, col);
                (void)XVector_push_back_1_base(out, &enc);
            }
        }
    }
    {
        size_t cnt = XVector_size_base((const XContainer*)out);
        if (cnt > 1)
            qsort(XVector_at_base(out, 0), cnt, sizeof(int), xism_intCompare);
    }
}

/** @brief 判定两个选择集合是否覆盖**相同的格集合**（无序）。
 *
 *  §selrange：Qt emitSelectionChanged（qitemselectionmodel.cpp:1901-1967）
 *  的判据不是「范围表顺序相等」，而是「对称差为空」——重复 Select 已选中
 *  格时范围表顺序会变（本机 Qt 6.8 探针 idem_probe：(3,3) 从中间挪到
 *  末尾）却**不发信号**。故以格集合等价承载 Qt 的幂等判定，避免顺序敏感
 *  的 XItemSelection_equal 误报变化。 */
static bool xism_sameCells(const XItemSelection* a, const XItemSelection* b)
{
    XVector va;
    XVector vb;
    size_t na;
    size_t nb;
    size_t i;
    bool same;
    xism_expandCells(a, &va);
    xism_expandCells(b, &vb);
    na = XVector_size_base((const XContainer*)&va);
    nb = XVector_size_base((const XContainer*)&vb);
    same = (na == nb);
    if (same) {
        for (i = 0; i < na; ++i) {
            if (XVector_At_Base(&va, (int64_t)i, int) !=
                XVector_At_Base(&vb, (int64_t)i, int)) {
                same = false;
                break;
            }
        }
    }
    XClassDeinit((XClass*)&vb);
    XClassDeinit((XClass*)&va);
    return same;
}

/** @brief 深拷贝选择集合（值对象逐 range append，§selrange copy 用）。 */
static void xism_copySelection(XItemSelection* dst, const XItemSelection* src)
{
    int i;
    int n;
    if (!dst || !src) return;
    XItemSelection_clear(dst);
    n = XItemSelection_count(src);
    for (i = 0; i < n; ++i)
        (void)XItemSelection_append(dst, XItemSelection_at(src, i));
}

/** @brief 在选中集合中定位编码值；未命中返回 -1。 */
/** @brief 对矩形区间施加选择命令（§selrange 命令历史模型，逐条对齐 Qt 6.8
 *         qitemselectionmodel.cpp:1284-1330 + qitemselectionmodel_p.h:66-71）。
 *
 *  流程（与 Qt 原文同序）：
 *   ① old = ranges + merge(currentSelection, currentCommand)  [:1303-1304]
 *   ② sel = 单矩形选区（无效索引构造为空集）                  [:1295]
 *   ③ command & Clear ⇒ 清 ranges 与 currentSelection            [:1311-1314]
 *   ④ !(command & Current) ⇒ finalize()（上一条命令固化进 ranges）[:1317-1318]
 *   ⑤ Toggle/Select/Deselect ⇒ currentCommand=command、
 *      currentSelection=sel                                      [:1321-1324]
 *   ⑥ newSelection = ranges + merge(currentSelection, currentCommand) [:1327-1328]
 *   ⑦ emitSelectionChanged(new, old)：格集合等价则不发，否则发一次
 *      （qitemselectionmodel.cpp:1901-1967；本库以 xism_sameCells 承载
 *      Qt 的「selected/deselected 双空」判定——范围表顺序变化不算变化）。
 *
 *  退役了旧的「逐格 findIndex O(N²)」路径：矩形只走 XItemSelection_merge
 *  （O(R)，R=范围数），8000 格全选由一条矩形命令完成。
 */
static bool xism_selectRect(XItemSelectionModel* self, int topRow,
                            int leftCol, int bottomRow, int rightCol,
                            int command)
{
    XItemSelection old;
    XItemSelection sel;
    XItemSelection newSelection;
    bool changed;
    if (!self) return false;
    if (command == XItemSelectionModelSelectionFlag_NoUpdate) return false;
    /* 区间归一（对标 QItemSelectionRange 的 topLeft/bottomRight 收敛）。 */
    if (topRow > bottomRow) { int t = topRow; topRow = bottomRow; bottomRow = t; }
    if (leftCol > rightCol) { int t = leftCol; leftCol = rightCol; rightCol = t; }

    (void)XItemSelection_init(&old);
    (void)XItemSelection_init(&sel);
    (void)XItemSelection_init(&newSelection);

    /* ① old：固化范围表 + 上一条命令的合并结果。 */
    xism_selectionView(self, &old);
    /* ② sel：无效索引（负坐标）构造为空 QItemSelection（Qt :1295 + 无效
     *   索引不进 QItemSelection::select）。Clear 分支仍照常生效。 */
    if (topRow >= 0 && leftCol >= 0 && self->m_model)
        xism_makeRectSelection(self, topRow, leftCol, bottomRow, rightCol, &sel);
    /* ③ Clear：清 ranges 与 currentSelection。 */
    if (command & XItemSelectionModelSelectionFlag_Clear) {
        XItemSelection_clear(&self->m_ranges);
        XItemSelection_clear(&self->m_currentSelection);
    }
    /* ④ 非 Current ⇒ finalize 上一条命令（把 currentSelection 并进 ranges）。 */
    if (!(command & XItemSelectionModelSelectionFlag_Current))
        xism_finalize(self);
    /* ⑤ 更新当前命令选区。 */
    if (command & (XItemSelectionModelSelectionFlag_Toggle |
                   XItemSelectionModelSelectionFlag_Select |
                   XItemSelectionModelSelectionFlag_Deselect)) {
        self->m_currentCommand = command;
        xism_copySelection(&self->m_currentSelection, &sel);
    }
    /* ⑥ newSelection：固化范围表 + 当前命令的合并结果。 */
    xism_selectionView(self, &newSelection);
    /* ⑦ 格集合等价（含双空）⇒ 幂等不发；否则单次发射。
     * 对齐 Qt emitSelectionChanged:1901-1967：判据是对称差为空，而非范围表
     * 顺序相等（重复 Select 同格会改变 range 顺序但不发信号，本机 Qt 6.8
     * 探针 idem_probe 实测 hits 不变）。 */
    changed = !xism_sameCells(&old, &newSelection);

    /* 同步派生镜像（平铺集）：范围表为事实源，镜像只作 O(1)/O(N) 热路径。 */
    if (changed) xism_rebuildMirror(self);
    if (changed) xism_emitChanged(self);

    XItemSelection_deinit(&newSelection);
    XItemSelection_deinit(&sel);
    XItemSelection_deinit(&old);
    return changed;
}

static void VXItemSelectionModel_deinit(XItemSelectionModel* self)
{
    if (!self) return;
    if (self->m_selected) {
        XClassDelete(self->m_selected);
        self->m_selected = NULL;
    }
    /* §selrange：三段范围表值对象显式 deinit（内嵌 XVector 拥有存储）。 */
    XItemSelection_deinit(&self->m_ranges);
    XItemSelection_deinit(&self->m_currentSelection);
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXItemSelectionModel_copy(XItemSelectionModel* self,
                                      const XItemSelectionModel* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XItemSelectionModel_init(self);
    XClass_Parent(XObject, EXClass_Copy,
                  void(*)(XObject*, const XObject*))((XObject*)self,
                                                     (const XObject*)other);
    /* §selrange：范围表为事实源，逐 range 深拷贝三段（值对象语义）。 */
    xism_copySelection(&self->m_ranges, &other->m_ranges);
    xism_copySelection(&self->m_currentSelection, &other->m_currentSelection);
    self->m_currentCommand = other->m_currentCommand;
    /* 平铺镜像从拷贝后的范围表重建（不从 other 平铺集直拷，保证同源）。 */
    xism_rebuildMirror(self);
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
    /* §selrange：三段范围表逐 range 转移（append 到 self 后清空 other 的
     * 值对象存储，other 保留已 init 的空壳，符合 _move「转移留壳」契约）。 */
    xism_copySelection(&self->m_ranges, &other->m_ranges);
    xism_copySelection(&self->m_currentSelection, &other->m_currentSelection);
    self->m_currentCommand = other->m_currentCommand;
    XItemSelection_clear(&other->m_ranges);
    XItemSelection_clear(&other->m_currentSelection);
    other->m_currentCommand = XItemSelectionModelSelectionFlag_NoUpdate;
    xism_rebuildMirror(self);
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
    /* §selrange：三段范围表值对象显式 init（XMemset 只清字节，XVector
     * 的 typeSize/内存标记须由 XItemSelection_init 设置）；尾部追加字段
     * 的显式清零同时满足 MSVC /RTC1。 */
    (void)XItemSelection_init(&self->m_ranges);
    (void)XItemSelection_init(&self->m_currentSelection);
    self->m_currentCommand = XItemSelectionModelSelectionFlag_NoUpdate;
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
    /* 未绑定模型 ⇒ 空操作（对齐 Qt 6.8 QItemSelectionModel::select，
     * qitemselectionmodel.cpp:1284-1290：无模型时告警并 return；本机
     * Qt 6.8.0 实测选区不变）。旧实现允许无模型维护选区，gap 164。 */
    if (!self->m_model) return false;
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
    /* 同 select：未绑定模型 ⇒ 空操作（Qt 只在新 select(selection,command)
     * 里统一告警 return，selectSelection 是同一入口的 C 形态）。 */
    if (!self->m_model) return false;
    if (command == XItemSelectionModelSelectionFlag_NoUpdate) return false;
    return xism_selectRect(self, topRow, leftCol, bottomRow, rightCol,
                           command);
}

/** @brief 判断某格是否在某 range 集合内（无效父级，平铺模型根级）。 */
static bool xism_rangeSetContains(const XItemSelection* sel, int row, int col)
{
    int i;
    int n = XItemSelection_count(sel);
    for (i = 0; i < n; ++i) {
        XItemSelectionRange r = XItemSelection_at(sel, i);
        if (XItemSelectionRange_isValid(&r) &&
            XItemSelectionRange_containsCell(&r, row, col, NULL))
            return true;
    }
    return false;
}

bool XItemSelectionModel_isSelected(const XItemSelectionModel* self,
                                    int row, int col)
{
    bool selected;
    int curIn;
    /* §selrange：走范围表事实源（Qt isSelected 同款算法，
     * qitemselectionmodel.cpp:1426-1456）：先看固化 ranges，再按
     * currentCommand 叠加 currentSelection 的 Deselect/Toggle/Select 语义。
     * 无分配、O(R)；平铺镜像 m_selected 与 §B4 的 R×C 位图快照仍是
     * 绘制快速通道，二者由 xism_rebuildMirror 保证与范围表同源，
     * 不互相顶掉。 */
    if (!self || !self->m_model || row < 0 || col < 0) return false;
    selected = xism_rangeSetContains(&self->m_ranges, row, col);
    curIn = xism_rangeSetContains(&self->m_currentSelection, row, col);
    if (XItemSelection_count(&self->m_currentSelection) > 0) {
        if ((self->m_currentCommand & XItemSelectionModelSelectionFlag_Deselect)
            && selected)
            selected = !curIn;
        else if (self->m_currentCommand & XItemSelectionModelSelectionFlag_Toggle)
            selected = selected != curIn; /* 异或翻转（Qt :1446-1447）。 */
        else if ((self->m_currentCommand & XItemSelectionModelSelectionFlag_Select)
                 && !selected)
            selected = curIn;
    }
    if (!selected) return false;
    /* Qt 末道门禁 isSelectableAndEnabled(model->flags(index))（:1452-1453）。 */
    {
        XModelIndex base = xism_baseIndex(self);
        XModelIndex idx = XModelIndex_withRowColumn(&base, row, col);
        int flags = XModelIndex_flags(&idx);
        int need = XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled;
        return (flags & need) == need;
    }
}

/* 注：整行/整列选择在 Qt 属 QTableView（qtableview.h:87-88），
 * QItemSelectionModel 无 selectRow/selectColumn；2026-11-13 P2-41/42
 * 已删本类同名包装。等价写法见 XItemSelectionModel.h 的迁移注
 * （selectSelection + 模型维度取界）。 */

bool XItemSelectionModel_clearSelection(XItemSelectionModel* self)
{
    if (!self || !self->m_selected) return false;
    /* §selrange：clearSelection 对齐 Qt `select(QItemSelection(), Clear)`
     * （qitemselectionmodel.cpp:1369-1376）：走命令历史模型的空选区 Clear
     * 路径，而不是直接清平铺集——保证 ranges/currentSelection/镜像一并
     * 收口、且 old/new 相等时不发信号。 */
    return XItemSelectionModel_selectSelection(
        self, -1, -1, -1, -1, XItemSelectionModelSelectionFlag_Clear);
}

bool XItemSelectionModel_clear(XItemSelectionModel* self)
{
    bool selectionChanged = XItemSelectionModel_clearSelection(self);
    /* 对标 Qt QItemSelectionModel::clear（qitemselectionmodel.cpp:1335-1339）：
     * clear = clearSelection() + clearCurrentIndex()。 */
    XItemSelectionModel_clearCurrentIndex(self);
    return selectionChanged;
}

/** @brief qsort 比较器：坐标按 (row,col) 升序（Qt mergeIndexes 假定已排序，
 *         qitemselectionmodel.cpp:918-919）。 */
typedef struct XismCoord { int row; int col; } XismCoord;

static int xism_coordCompare(const void* a, const void* b)
{
    const XismCoord* x = (const XismCoord*)a;
    const XismCoord* y = (const XismCoord*)b;
    if (x->row != y->row) return x->row < y->row ? -1 : 1;
    if (x->col != y->col) return x->col < y->col ? -1 : 1;
    return 0;
}

bool XItemSelectionModel_rebuildSelection(XItemSelectionModel* self,
                                          const int* rows, const int* cols,
                                          int n)
{
    XismCoord* coords = NULL;
    XItemSelection old;
    XItemSelection colSpans;
    int i;
    int m = 0;
    bool changed;
    if (!self || !self->m_selected) return false;
    /* §selrange：rebuildSelection 从零重建范围表（对齐 Qt 持久索引迁移后
     * layoutChanged 的 mergeIndexes，qitemselectionmodel.cpp:920-979）：
     * 排序 → 合并同行连续列段 → 合并同跨度连续行 → 无重叠矩形装入 m_ranges；
     * currentSelection/currentCommand 清空；平铺镜像同步重建。 */
    (void)XItemSelection_init(&old);
    xism_selectionView(self, &old);

    xism_clearAll(self);
    if (rows && cols && n > 0) {
        coords = (XismCoord*)XMalloc_System(sizeof(XismCoord) * (size_t)n);
        if (coords) {
            for (i = 0; i < n; ++i) {
                if (rows[i] < 0 || cols[i] < 0) continue;
                coords[m].row = rows[i];
                coords[m].col = cols[i];
                ++m;
            }
            if (m > 1) qsort(coords, (size_t)m, sizeof(XismCoord),
                             xism_coordCompare);
            (void)XItemSelection_init(&colSpans);
            /* ① 合并同行连续列段（先去重，避免 0 宽段）。 */
            i = 0;
            while (i < m) {
                int segLeft = coords[i].col;
                int segRight = segLeft;
                int r = coords[i].row;
                ++i;
                while (i < m && coords[i].row == r &&
                       (coords[i].col == segRight + 1 ||
                        coords[i].col == segRight)) {
                    if (coords[i].col > segRight) segRight = coords[i].col;
                    ++i;
                }
                {
                    XModelIndex base = xism_baseIndex(self);
                    XModelIndex tl = XModelIndex_withRowColumn(&base, r, segLeft);
                    XModelIndex br = XModelIndex_withRowColumn(&base, r, segRight);
                    (void)XItemSelection_append(
                        &colSpans, XItemSelectionRange_make(tl, br));
                }
            }
            /* ② 合并同 left/right 且行连续的列段为矩形（Qt :955-978）。 */
            i = 0;
            while (i < XItemSelection_count(&colSpans)) {
                XItemSelectionRange r0 = XItemSelection_at(&colSpans, i);
                int top = XItemSelectionRange_top(&r0);
                int left = XItemSelectionRange_left(&r0);
                int right = XItemSelectionRange_right(&r0);
                int bottom = top;
                int j = i + 1;
                while (j < XItemSelection_count(&colSpans)) {
                    XItemSelectionRange rj = XItemSelection_at(&colSpans, j);
                    if (XItemSelectionRange_top(&rj) == bottom + 1 &&
                        XItemSelectionRange_left(&rj) == left &&
                        XItemSelectionRange_right(&rj) == right) {
                        bottom = XItemSelectionRange_top(&rj);
                        ++j;
                    } else break;
                }
                {
                    XModelIndex base = xism_baseIndex(self);
                    XModelIndex tl = XModelIndex_withRowColumn(&base, top, left);
                    XModelIndex br = XModelIndex_withRowColumn(&base, bottom,
                                                               right);
                    (void)XItemSelection_append(&self->m_ranges,
                        XItemSelectionRange_make(tl, br));
                }
                i = j;
            }
            XItemSelection_deinit(&colSpans);
            XFree_System(coords);
        }
    }
    /* 镜像按新范围表重建（selectionView 此时 = m_ranges，命令段已清）。 */
    xism_rebuildMirror(self);
    {
        XItemSelection now;
        (void)XItemSelection_init(&now);
        xism_selectionView(self, &now);
        /* 格集合等价即无变化（rebuild 会把命令历史切分压成几何合并，
         * 范围表顺序可能变但选中格不变 ⇒ 不误报，§B4 remap/trim 单次发射）。 */
        changed = !xism_sameCells(&old, &now);
        XItemSelection_deinit(&now);
    }
    XItemSelection_deinit(&old);
    /* 单次发射（调用方多在桥事务闸内：信号照发、桥槽静默登记，尾部收口）。 */
    if (changed) xism_emitChanged(self);
    return changed;
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

/* ==================== 选区查询族（对标 Qt selectedIndexes/selectedRows/
 * isRowSelected 等；语义见各头注释） ==================== */

/** @brief 行内选中格数（供整行/相交判定）。
 * @param colLimit >0 时只统计列号 < colLimit 的格（模型列上界校验，
 *        避免模型外的历史选中格把行误判为"整行"）；<=0 不限。 */
static int xism_rowSelectedCells(const XItemSelectionModel* self, int row,
                                 int colLimit)
{
    int i;
    int n;
    int hits = 0;
    if (!self || !self->m_selected) return 0;
    n = (int)XVector_size_base((const XContainer*)self->m_selected);
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(self->m_selected, (int64_t)i, int);
        if (XISM_ROW(v) != row) continue;
        if (colLimit > 0 && XISM_COL(v) >= colLimit) continue;
        ++hits;
    }
    return hits;
}

/** @brief 列内选中格数（供整列/相交判定）。
 * @param rowLimit >0 时只统计行号 < rowLimit 的格；<=0 不限。 */
static int xism_colSelectedCells(const XItemSelectionModel* self, int col,
                                 int rowLimit)
{
    int i;
    int n;
    int hits = 0;
    if (!self || !self->m_selected) return 0;
    n = (int)XVector_size_base((const XContainer*)self->m_selected);
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(self->m_selected, (int64_t)i, int);
        if (XISM_COL(v) != col) continue;
        if (rowLimit > 0 && XISM_ROW(v) >= rowLimit) continue;
        ++hits;
    }
    return hits;
}

bool XItemSelectionModel_isRowSelected(const XItemSelectionModel* self,
                                       int row)
{
    int rows;
    int cols;
    if (!self || !self->m_model || row < 0) return false;
    rows = XAbstractItemModel_rowCount(self->m_model);
    cols = XAbstractItemModel_columnCount(self->m_model);
    if (row >= rows || cols <= 0) return false; /* 无模型/空模型恒 false。 */
    /* 整行判定：行内每一列都在选区（实测 Qt：2/3 格选中 ⇒ false）。 */
    return xism_rowSelectedCells(self, row, cols) >= cols;
}

bool XItemSelectionModel_isColumnSelected(const XItemSelectionModel* self,
                                          int column)
{
    int rows;
    int cols;
    if (!self || !self->m_model || column < 0) return false;
    rows = XAbstractItemModel_rowCount(self->m_model);
    cols = XAbstractItemModel_columnCount(self->m_model);
    if (column >= cols || rows <= 0) return false;
    return xism_colSelectedCells(self, column, rows) >= rows;
}

bool XItemSelectionModel_rowIntersectsSelection(const XItemSelectionModel* self,
                                                int row)
{
    int rows;
    int cols;
    if (!self || row < 0) return false;
    if (!self->m_model) return xism_rowSelectedCells(self, row, 0) > 0;
    rows = XAbstractItemModel_rowCount(self->m_model);
    cols = XAbstractItemModel_columnCount(self->m_model);
    if (row >= rows) return false;
    /* 相交口径：行内任一格选中即 true（不要求整行，实测 Qt 同）。 */
    return xism_rowSelectedCells(self, row, cols) > 0;
}

bool XItemSelectionModel_columnIntersectsSelection(
    const XItemSelectionModel* self, int column)
{
    int rows;
    int cols;
    if (!self || column < 0) return false;
    if (!self->m_model) return xism_colSelectedCells(self, column, 0) > 0;
    rows = XAbstractItemModel_rowCount(self->m_model);
    cols = XAbstractItemModel_columnCount(self->m_model);
    if (column >= cols) return false;
    return xism_colSelectedCells(self, column, rows) > 0;
}

int XItemSelectionModel_selectedIndexes(const XItemSelectionModel* self,
                                        int* outRows, int* outCols,
                                        int maxCount)
{
    int i;
    int n;
    int written = 0;
    int total = 0;
    if (!self || !self->m_selected) return 0;
    n = (int)XVector_size_base((const XContainer*)self->m_selected);
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(self->m_selected, (int64_t)i, int);
        if (outRows && outCols && written < maxCount) {
            outRows[written] = XISM_ROW(v);
            outCols[written] = XISM_COL(v);
            ++written;
        }
        ++total;
    }
    return total;
}

int XItemSelectionModel_selection(const XItemSelectionModel* self,
                                  XItemSelection* out)
{
    XItemSelection view;
    int i;
    int n;
    int written = 0;
    if (!out) {
        /* out 为 NULL 时仅计数（无分配）；同时剔除无效 range，与写出口径一致。 */
        if (!self) return 0;
        (void)XItemSelection_init(&view);
        xism_selectionView(self, &view);
        n = XItemSelection_count(&view);
        for (i = 0; i < n; ++i) {
            XItemSelectionRange r = XItemSelection_at(&view, i);
            if (XItemSelectionRange_isValid(&r)) ++written;
        }
        XItemSelection_deinit(&view);
        return written;
    }
    /* out 由调用方 init（空集合）；逐 range 按值追加，剔除无效 range
     * （对齐 Qt selection() 的 `selected.removeIf(IsNotValid())`，
     *  qitemselectionmodel.cpp:1821-1823）。 */
    if (!self) return 0;
    (void)XItemSelection_init(&view);
    xism_selectionView(self, &view);
    n = XItemSelection_count(&view);
    for (i = 0; i < n; ++i) {
        XItemSelectionRange r = XItemSelection_at(&view, i);
        if (!XItemSelectionRange_isValid(&r)) continue;
        (void)XItemSelection_append(out, r);
        ++written;
    }
    XItemSelection_deinit(&view);
    return written;
}

int XItemSelectionModel_selectedRows(const XItemSelectionModel* self,
                                     int column, int* outRows, int maxCount)
{
    int rows;
    int cols;
    int row;
    int written = 0;
    int total = 0;
    (void)column; /* Qt 只以该列取索引；本库以行号承载，列号不参与判定。 */
    if (!self || !self->m_model) return 0;
    rows = XAbstractItemModel_rowCount(self->m_model);
    cols = XAbstractItemModel_columnCount(self->m_model);
    if (cols <= 0) return 0;
    for (row = 0; row < rows; ++row) {
        if (xism_rowSelectedCells(self, row, cols) < cols) continue;
        if (outRows && written < maxCount) {
            outRows[written] = row;
            ++written;
        }
        ++total;
    }
    return total;
}

int XItemSelectionModel_selectedColumns(const XItemSelectionModel* self,
                                        int row, int* outCols, int maxCount)
{
    int rows;
    int cols;
    int col;
    int written = 0;
    int total = 0;
    (void)row;
    if (!self || !self->m_model) return 0;
    rows = XAbstractItemModel_rowCount(self->m_model);
    cols = XAbstractItemModel_columnCount(self->m_model);
    if (rows <= 0) return 0;
    for (col = 0; col < cols; ++col) {
        if (xism_colSelectedCells(self, col, rows) < rows) continue;
        if (outCols && written < maxCount) {
            outCols[written] = col;
            ++written;
        }
        ++total;
    }
    return total;
}

void XItemSelectionModel_reset(XItemSelectionModel* self)
{
    if (!self) return;
    /* 对标 Qt 6.8.0 实测：reset 静默清空选区与当前索引（两信号命中 0）。
     * §selrange：三段范围表 + 平铺镜像一并静默清空。 */
    xism_clearAll(self);
    self->m_currentRow = -1;
    self->m_currentCol = -1;
}

void XItemSelectionModel_setModel(XItemSelectionModel* self,
                                  XAbstractItemModel* model)
{
    /* 对标 Qt 6.8.0 实测：指针未变 ⇒ 空操作（不发射 modelChanged）；
     * 指针变化 ⇒ 静默清空选区与当前索引（不发 selectionChanged /
     * currentChanged），最后发射一次 modelChanged。 */
    if (!self || self->m_model == model) return;
    /* §selrange：换模型静默清空三段范围表 + 平铺镜像（Qt setModel 同款）。 */
    xism_clearAll(self);
    self->m_currentRow = -1;
    self->m_currentCol = -1;
    self->m_model = model;
    if (((XObject*)self)->m_signalSlot) {
        XVarList* args = XVarList_Create(XVar(XAbstractItemModel*, model));
        if (args) {
            XObject_emitSignal(
                (XObject*)self,
                (size_t)XItemSelectionModel_modelChanged_signal, args,
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
}

XAbstractItemModel* XItemSelectionModel_model(const XItemSelectionModel* self)
{ return self ? self->m_model : NULL; }

void XItemSelectionModel_setCurrentIndex(XItemSelectionModel* self,
                                         int row, int col, int command)
{
    int prevRow;
    int prevCol;
    if (!self) return;
    /* 未绑定模型 ⇒ 空操作（对齐 Qt 6.8 setCurrentIndex，
     * qitemselectionmodel.cpp:1389-1395：无模型时告警并 return）。 */
    if (!self->m_model) return;
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

void* XItemSelectionModel_modelChanged_signal(XItemSelectionModel* self,
                                              XAbstractItemModel* model)
{
    /* 真实发射点：XItemSelectionModel_setModel（指针实际变化时一次）。 */
    if (self && ((XObject*)self)->m_signalSlot) {
        XVarList* args = XVarList_Create(XVar(XAbstractItemModel*, model));
        if (args) {
            XObject_emitSignal(
                (XObject*)self,
                (size_t)XItemSelectionModel_modelChanged_signal, args,
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
    return (void*)(size_t)XItemSelectionModel_modelChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#endif /* XGUI_ON */
