/**
 * @file       XItemSelection.c
 * @brief      XItemSelectionRange / XItemSelection 值对象实现。
 * @details    范围代数逐条对齐 Qt 6.8 qitemselectionmodel.cpp 的
 *             intersects/intersected/isEmpty/indexes/select/contains/
 *             merge/split；索引父级比较走 XModelIndex_equal（含 model 指针
 *             与 internalId，等价 Qt comparesEqual 的四字段口径）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XItemSelection.h"

#include "XAbstractItemModel.h"
#include "XMemory.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* 命令位（与 XItemSelectionModel.h 的 XItemSelectionModelSelectionFlag 同值；
 * 本文件不反向依赖选择模型头，按 Qt qitemselectionmodel.h:108-120 的数值）。 */
#define XIS_CMD_SELECT   0x0002
#define XIS_CMD_DESELECT 0x0004
#define XIS_CMD_TOGGLE   0x0008

/* ==================== XItemSelectionRange ==================== */

XItemSelectionRange XItemSelectionRange_invalid(void)
{
    XItemSelectionRange r;
    r.m_topLeft = XModelIndex_invalid();
    r.m_bottomRight = XModelIndex_invalid();
    return r;
}

XItemSelectionRange XItemSelectionRange_make(XModelIndex topLeft,
                                              XModelIndex bottomRight)
{
    XItemSelectionRange r;
    r.m_topLeft = topLeft;
    r.m_bottomRight = bottomRight;
    return r;
}

XItemSelectionRange XItemSelectionRange_fromIndex(XModelIndex index)
{
    /* 对标 explicit QItemSelectionRange(index)：tl = br = index。 */
    return XItemSelectionRange_make(index, index);
}

XItemSelectionRange XItemSelectionRange_makeRect(XModelIndex base, int top,
                                                  int left, int bottom,
                                                  int right)
{
    return XItemSelectionRange_make(
        XModelIndex_withRowColumn(&base, top, left),
        XModelIndex_withRowColumn(&base, bottom, right));
}

int XItemSelectionRange_top(const XItemSelectionRange* self)
{ return self ? XModelIndex_row(&self->m_topLeft) : -1; }

int XItemSelectionRange_left(const XItemSelectionRange* self)
{ return self ? XModelIndex_column(&self->m_topLeft) : -1; }

int XItemSelectionRange_bottom(const XItemSelectionRange* self)
{ return self ? XModelIndex_row(&self->m_bottomRight) : -1; }

int XItemSelectionRange_right(const XItemSelectionRange* self)
{ return self ? XModelIndex_column(&self->m_bottomRight) : -1; }

int XItemSelectionRange_width(const XItemSelectionRange* self)
{
    /* 对标 QItemSelectionRange::width：br.column - tl.column + 1。 */
    return self ? XItemSelectionRange_right(self) -
                      XItemSelectionRange_left(self) + 1 : 0;
}

int XItemSelectionRange_height(const XItemSelectionRange* self)
{
    /* 对标 QItemSelectionRange::height：br.row - tl.row + 1。 */
    return self ? XItemSelectionRange_bottom(self) -
                      XItemSelectionRange_top(self) + 1 : 0;
}

XModelIndex XItemSelectionRange_topLeft(const XItemSelectionRange* self)
{ return self ? self->m_topLeft : XModelIndex_invalid(); }

XModelIndex XItemSelectionRange_bottomRight(const XItemSelectionRange* self)
{ return self ? self->m_bottomRight : XModelIndex_invalid(); }

XModelIndex XItemSelectionRange_parent(const XItemSelectionRange* self)
{
    /* 对标 QItemSelectionRange::parent：tl.parent()。 */
    return self ? XModelIndex_parent(&self->m_topLeft) : XModelIndex_invalid();
}

const struct XAbstractItemModel* XItemSelectionRange_model(
    const XItemSelectionRange* self)
{
    /* 对标 QItemSelectionRange::model：tl.model()。 */
    return self ? XModelIndex_model(&self->m_topLeft) : NULL;
}

bool XItemSelectionRange_isValid(const XItemSelectionRange* self)
{
    XModelIndex tl;
    XModelIndex br;
    XModelIndex pTl;
    XModelIndex pBr;
    if (!self) return false;
    tl = self->m_topLeft;
    br = self->m_bottomRight;
    /* 逐条对齐 Qt qitemselectionmodel.h:66-70：
       tl.isValid() && br.isValid() && tl.parent()==br.parent()
       && top()<=bottom() && left()<=right()。 */
    if (!XModelIndex_isValid(&tl) || !XModelIndex_isValid(&br)) return false;
    pTl = XModelIndex_parent(&tl);
    pBr = XModelIndex_parent(&br);
    if (!XModelIndex_equal(&pTl, &pBr)) return false;
    return XModelIndex_row(&tl) <= XModelIndex_row(&br) &&
           XModelIndex_column(&tl) <= XModelIndex_column(&br);
}

bool XItemSelectionRange_isEmpty(const XItemSelectionRange* self)
{
    int row;
    int col;
    int bottom;
    int right;
    const struct XAbstractItemModel* model;
    /* 对标 qitemselectionmodel.cpp:296-312（isEmpty）。 */
    if (!XItemSelectionRange_isValid(self)) return true;
    model = XItemSelectionRange_model(self);
    if (!model) return true;
    bottom = XItemSelectionRange_bottom(self);
    right = XItemSelectionRange_right(self);
    for (col = XItemSelectionRange_left(self); col <= right; ++col) {
        for (row = XItemSelectionRange_top(self); row <= bottom; ++row) {
            /* Qt 用 model()->index(row,column,parent) + flags；本库以同位
               XModelIndex 走 flags 虚槽分派，派生模型重载 flags 同样生效。 */
            XModelIndex idx = XModelIndex_withRowColumn(&self->m_topLeft,
                                                         row, col);
            int flags = XModelIndex_flags(&idx);
            int need = XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled;
            if ((flags & need) == need) return false;
        }
    }
    return true;
}

bool XItemSelectionRange_contains(const XItemSelectionRange* self,
                                  XModelIndex index)
{
    XModelIndex pRange;
    XModelIndex pIndex;
    if (!self) return false;
    /* 对标 qitemselectionmodel.h:53-58：parent 相等 + 四边夹逼。 */
    pRange = XItemSelectionRange_parent(self);
    pIndex = XModelIndex_parent(&index);
    if (!XModelIndex_equal(&pRange, &pIndex)) return false;
    return XItemSelectionRange_top(self) <= XModelIndex_row(&index) &&
           XItemSelectionRange_left(self) <= XModelIndex_column(&index) &&
           XItemSelectionRange_bottom(self) >= XModelIndex_row(&index) &&
           XItemSelectionRange_right(self) >= XModelIndex_column(&index);
}

bool XItemSelectionRange_containsCell(const XItemSelectionRange* self, int row,
                                      int column,
                                      const XModelIndex* parentIndex)
{
    XModelIndex pRange;
    XModelIndex pWant;
    if (!self) return false;
    /* 对标 qitemselectionmodel.h:60-64。 */
    pRange = XItemSelectionRange_parent(self);
    pWant = parentIndex ? *parentIndex : XModelIndex_invalid();
    if (!XModelIndex_equal(&pRange, &pWant)) return false;
    return XItemSelectionRange_top(self) <= row &&
           XItemSelectionRange_left(self) <= column &&
           XItemSelectionRange_bottom(self) >= row &&
           XItemSelectionRange_right(self) >= column;
}

bool XItemSelectionRange_intersects(const XItemSelectionRange* self,
                                    const XItemSelectionRange* other)
{
    XItemSelectionRange a;
    XItemSelectionRange b;
    int at;
    int ab;
    int al;
    int ar;
    int bt;
    int bb;
    int bl;
    int br;
    XModelIndex pa;
    XModelIndex pb;
    if (!self || !other) return false;
    a = *self;
    b = *other;
    /* 逐条对齐 qitemselectionmodel.cpp:183-193：
       model 相等 + 行区间相交 + 列区间相交 + parent 相等 + 双方 isValid。 */
    if (XItemSelectionRange_model(&a) != XItemSelectionRange_model(&b))
        return false;
    at = XItemSelectionRange_top(&a);
    ab = XItemSelectionRange_bottom(&a);
    al = XItemSelectionRange_left(&a);
    ar = XItemSelectionRange_right(&a);
    bt = XItemSelectionRange_top(&b);
    bb = XItemSelectionRange_bottom(&b);
    bl = XItemSelectionRange_left(&b);
    br = XItemSelectionRange_right(&b);
    if (!((at <= bt && ab >= bt) || (at >= bt && at <= bb))) return false;
    if (!((al <= bl && ar >= bl) || (al >= bl && al <= br))) return false;
    pa = XItemSelectionRange_parent(&a);
    pb = XItemSelectionRange_parent(&b);
    if (!XModelIndex_equal(&pa, &pb)) return false;
    return XItemSelectionRange_isValid(&a) && XItemSelectionRange_isValid(&b);
}

XItemSelectionRange XItemSelectionRange_intersected(
    const XItemSelectionRange* self, const XItemSelectionRange* other)
{
    int top;
    int bottom;
    int left;
    int right;
    XModelIndex pa;
    XModelIndex pb;
    if (!self || !other) return XItemSelectionRange_invalid();
    /* 对标 qitemselectionmodel.cpp:204-214：仅同模型同父才产出交集。 */
    if (XItemSelectionRange_model(self) != XItemSelectionRange_model(other))
        return XItemSelectionRange_invalid();
    pa = XItemSelectionRange_parent(self);
    pb = XItemSelectionRange_parent(other);
    if (!XModelIndex_equal(&pa, &pb)) return XItemSelectionRange_invalid();
    top = XItemSelectionRange_top(self) > XItemSelectionRange_top(other)
              ? XItemSelectionRange_top(self)
              : XItemSelectionRange_top(other);
    left = XItemSelectionRange_left(self) > XItemSelectionRange_left(other)
               ? XItemSelectionRange_left(self)
               : XItemSelectionRange_left(other);
    bottom = XItemSelectionRange_bottom(self) <
                     XItemSelectionRange_bottom(other)
                 ? XItemSelectionRange_bottom(self)
                 : XItemSelectionRange_bottom(other);
    right = XItemSelectionRange_right(self) < XItemSelectionRange_right(other)
                ? XItemSelectionRange_right(self)
                : XItemSelectionRange_right(other);
    if (top > bottom || left > right) return XItemSelectionRange_invalid();
    /* Qt 经 model->index(r,c,parent) 构造；本库以基底保留父级语义。 */
    return XItemSelectionRange_makeRect(self->m_topLeft, top, left, bottom,
                                        right);
}

bool XItemSelectionRange_equal(const XItemSelectionRange* a,
                               const XItemSelectionRange* b)
{
    XItemSelectionRange la;
    XItemSelectionRange lb;
    la = a ? *a : XItemSelectionRange_invalid();
    lb = b ? *b : XItemSelectionRange_invalid();
    /* 对标 comparesEqual（qitemselectionmodel.h:80-83）：tl/br 各自相等。 */
    return XModelIndex_equal(&la.m_topLeft, &lb.m_topLeft) &&
           XModelIndex_equal(&la.m_bottomRight, &lb.m_bottomRight);
}

int XItemSelectionRange_indexes(const XItemSelectionRange* self,
                                XModelIndex* out, int maxCount)
{
    int row;
    int col;
    int bottom;
    int right;
    int total = 0;
    int need = XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled;
    /* 对标 qitemselectionmodel.cpp:315-318（indexes → indexesFromRange
       :263-278，门禁判据 isSelectableAndEnabled:257）：isValid && model
       才输出；行主序逐格，且每格必须 flags 含 Selectable|Enabled 才
       计入——2026-11-15 返工对齐：旧实现把禁选格也枚举出去（派生模型
       禁选格会以 indexes() 形式泄漏给调用方）。 */
    if (!XItemSelectionRange_isValid(self) || !XItemSelectionRange_model(self))
        return 0;
    bottom = XItemSelectionRange_bottom(self);
    right = XItemSelectionRange_right(self);
    for (row = XItemSelectionRange_top(self); row <= bottom; ++row) {
        for (col = XItemSelectionRange_left(self); col <= right; ++col) {
            XModelIndex idx = XModelIndex_withRowColumn(&self->m_topLeft,
                                                        row, col);
            if ((XModelIndex_flags(&idx) & need) == need) {
                if (out && total < maxCount) out[total] = idx;
                ++total;
            }
        }
    }
    return total;
}

/* ==================== XItemSelection 生命周期 ==================== */

bool XItemSelection_init(XItemSelection* self)
{
    if (!self) return false;
    XVector_init(&self->m_ranges, sizeof(XItemSelectionRange), false);
    /* 元素为 POD 值（XModelIndex 内只有借用指针），不设拷贝/析构回调：
       XVector 默认按字节拷贝元素（已核对 XVector.c:114-120 的 memcpy 分支）。 */
    self->m_memoryType = (int)XMEMORY_TYPE_SYSTEM;
    return true;
}

void XItemSelection_deinit(XItemSelection* self)
{
    if (!self) return;
    XClassDeinit(&self->m_ranges);
}

XItemSelection* XItemSelection_create_ex(XMemoryType memory)
{
    XItemSelection* self =
        (XItemSelection*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XItemSelection_init(self);
    /* XItemSelection 是纯值结构（非 XObject 派生）：不能用
       Set_Class_Memory/Set_Class_IsHeap（那会写进内嵌 XVector 的 XClass
       头，破坏容器）。此处只在尾部字段记录分配器，delete 用它配对释放。 */
    self->m_memoryType = (int)memory;
    return self;
}

void XItemSelection_delete(XItemSelection* self)
{
    if (!self) return;
    XItemSelection_deinit(self);
    /* 与 XMemory_malloc(size, memory) 逐一配对（XMemory.h:147）。 */
    XMemory_free(self, (XMemoryType)self->m_memoryType);
}

XItemSelection* XItemSelection_createFromRect(XModelIndex topLeft,
                                               XModelIndex bottomRight)
{
    XItemSelection* self = XItemSelection_create();
    if (!self) return NULL;
    /* 对标 QItemSelection(topLeft,bottomRight) → select()。 */
    XItemSelection_select(self, topLeft, bottomRight);
    return self;
}

/* ==================== XItemSelection 列表操作 ==================== */

int XItemSelection_count(const XItemSelection* self)
{
    return self ? (int)XVector_size_base(&self->m_ranges) : 0;
}

bool XItemSelection_isEmpty(const XItemSelection* self)
{ return XItemSelection_count(self) == 0; }

XItemSelectionRange XItemSelection_at(const XItemSelection* self, int index)
{
    XItemSelectionRange* p;
    if (!self || index < 0) return XItemSelectionRange_invalid();
    p = (XItemSelectionRange*)XVector_at_base(&self->m_ranges, index);
    return p ? *p : XItemSelectionRange_invalid();
}

bool XItemSelection_append(XItemSelection* self, XItemSelectionRange range)
{
    if (!self) return false;
    return XVector_push_back_1_base(&self->m_ranges, &range);
}

void XItemSelection_removeAt(XItemSelection* self, int index)
{
    if (!self || index < 0 ||
        (size_t)index >= XVector_size_base(&self->m_ranges))
        return;
    XVector_remove_base(&self->m_ranges, index, 1);
}

void XItemSelection_clear(XItemSelection* self)
{
    if (self) XVector_clear_base(&self->m_ranges);
}

/* ==================== 范围代数 ==================== */

bool XItemSelection_select(XItemSelection* self, XModelIndex topLeft,
                           XModelIndex bottomRight)
{
    XModelIndex pTl;
    XModelIndex pBr;
    XModelIndex tl;
    XModelIndex br;
    if (!self) return false;
    /* 对标 qitemselectionmodel.cpp:387-411：两端无效/异模型/异父不追加。 */
    if (!XModelIndex_isValid(&topLeft) || !XModelIndex_isValid(&bottomRight))
        return false;
    if (XModelIndex_model(&topLeft) != XModelIndex_model(&bottomRight))
        return false;
    pTl = XModelIndex_parent(&topLeft);
    pBr = XModelIndex_parent(&bottomRight);
    if (!XModelIndex_equal(&pTl, &pBr)) return false;
    tl = topLeft;
    br = bottomRight;
    if (XModelIndex_row(&tl) > XModelIndex_row(&br) ||
        XModelIndex_column(&tl) > XModelIndex_column(&br)) {
        /* 归一四边（Qt 用 sibling(top,left)/sibling(bottom,right)）。 */
        int top = XModelIndex_row(&tl) < XModelIndex_row(&br)
                      ? XModelIndex_row(&tl) : XModelIndex_row(&br);
        int bottom = XModelIndex_row(&tl) > XModelIndex_row(&br)
                         ? XModelIndex_row(&tl) : XModelIndex_row(&br);
        int left = XModelIndex_column(&tl) < XModelIndex_column(&br)
                       ? XModelIndex_column(&tl) : XModelIndex_column(&br);
        int right = XModelIndex_column(&tl) > XModelIndex_column(&br)
                        ? XModelIndex_column(&tl) : XModelIndex_column(&br);
        tl = XModelIndex_withRowColumn(&tl, top, left);
        br = XModelIndex_withRowColumn(&br, bottom, right);
    }
    return XItemSelection_append(self, XItemSelectionRange_make(tl, br));
}

bool XItemSelection_contains(const XItemSelection* self, XModelIndex index)
{
    int i;
    int n;
    if (!self) return false;
    /* 对标 qitemselectionmodel.cpp:415-424：Qt 先以
       isSelectableAndEnabled(index.flags()) 短路整次查找——索引本身
       不可选/不可用时**不扫任何范围**直接 false（2026-11-15 返工对齐：
       旧实现漏了这道门禁，派生模型禁选的格也会被判「已选中」）。 */
    {
        int flags = XModelIndex_flags(&index);
        int need = XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled;
        if ((flags & need) != need) return false;
    }
    n = XItemSelection_count(self);
    for (i = 0; i < n; ++i) {
        XItemSelectionRange r = XItemSelection_at(self, i);
        if (XItemSelectionRange_contains(&r, index)) return true;
    }
    return false;
}

int XItemSelection_indexes(const XItemSelection* self, XModelIndex* out,
                           int maxCount)
{
    int i;
    int n;
    int total = 0;
    if (!self) return 0;
    /* 对标 qitemselectionmodel.cpp:430-433：逐范围 indexes() 直接拼接。 */
    n = XItemSelection_count(self);
    for (i = 0; i < n; ++i) {
        XItemSelectionRange r = XItemSelection_at(self, i);
        int cap = maxCount > total ? maxCount - total : 0;
        int slotOut = (out && cap > 0) ? 1 : 0;
        int got = XItemSelectionRange_indexes(
            &r, slotOut ? out + total : NULL, cap);
        total += got;
    }
    return total;
}

void XItemSelection_split(XItemSelectionRange range,
                          XItemSelectionRange other,
                          XItemSelection* result)
{
    XModelIndex pRange;
    XModelIndex pOther;
    int top;
    int left;
    int bottom;
    int right;
    int oTop;
    int oLeft;
    int oBottom;
    int oRight;
    if (!result) return;
    /* 对标 qitemselectionmodel.cpp:507-555：异父/异模型直接返回。 */
    pRange = XItemSelectionRange_parent(&range);
    pOther = XItemSelectionRange_parent(&other);
    if (!XModelIndex_equal(&pRange, &pOther)) return;
    if (XItemSelectionRange_model(&range) != XItemSelectionRange_model(&other))
        return;
    top = XItemSelectionRange_top(&range);
    left = XItemSelectionRange_left(&range);
    bottom = XItemSelectionRange_bottom(&range);
    right = XItemSelectionRange_right(&range);
    oTop = XItemSelectionRange_top(&other);
    oLeft = XItemSelectionRange_left(&other);
    oBottom = XItemSelectionRange_bottom(&other);
    oRight = XItemSelectionRange_right(&other);
    /* 四块顺序与 Qt 一致：上、下、左、右；每块 append 后收紧对应边。 */
    if (oTop > top) {
        XItemSelection_append(result, XItemSelectionRange_makeRect(
            range.m_topLeft, top, left, oTop - 1, right));
        top = oTop;
    }
    if (oBottom < bottom) {
        XItemSelection_append(result, XItemSelectionRange_makeRect(
            range.m_topLeft, oBottom + 1, left, bottom, right));
        bottom = oBottom;
    }
    if (oLeft > left) {
        XItemSelection_append(result, XItemSelectionRange_makeRect(
            range.m_topLeft, top, left, bottom, oLeft - 1));
        left = oLeft;
    }
    if (oRight < right) {
        XItemSelection_append(result, XItemSelectionRange_makeRect(
            range.m_topLeft, top, oRight + 1, bottom, right));
    }
}

bool XItemSelection_merge(XItemSelection* self, const XItemSelection* other,
                          int command)
{
    XItemSelection newSelection;
    XItemSelection intersections;
    int i;
    int n;
    bool ok;
    /* 对标 qitemselectionmodel.cpp:453-497：
       other 空 / 命令无 Select|Deselect|Toggle ⇒ 早退（返回 false）。 */
    if (!self || !other || XItemSelection_isEmpty(other)) return false;
    if (!(command & (XIS_CMD_SELECT | XIS_CMD_DESELECT | XIS_CMD_TOGGLE)))
        return false;
    ok = XItemSelection_init(&newSelection) &&
         XItemSelection_init(&intersections);
    if (!ok) {
        XItemSelection_deinit(&newSelection);
        XItemSelection_deinit(&intersections);
        return false;
    }
    /* ① 收集交集（"Collect intersections"，:471-482）：other 各有效 range
       先入 newSelection；每个 range 与 self 各 range 的交集记入
       intersections。 */
    n = XItemSelection_count(other);
    for (i = 0; i < n; ++i) {
        XItemSelectionRange r = XItemSelection_at(other, i);
        int t;
        int m;
        if (!XItemSelectionRange_isValid(&r)) continue;
        XItemSelection_append(&newSelection, r);
        m = XItemSelection_count(self);
        for (t = 0; t < m; ++t) {
            XItemSelectionRange s = XItemSelection_at(self, t);
            if (XItemSelectionRange_intersects(&r, &s))
                XItemSelection_append(&intersections,
                    XItemSelectionRange_intersected(&s, &r));
        }
    }
    /* ② 用交集把 self（及 Toggle 时的 newSelection）中相交的 range 切成
       不相交碎片（:483-497）。

       与 Qt 同款**就地**算法：split 把碎片 append 到 self 尾部，随后
       removeAt(t) 删掉被切的 range，且 t 不前进（下一轮检查移到 t 位的
       原 t+1 项）。split 产出的碎片都落在交集之外，对本交集 intersects
       为 false，故此后 t 前进、循环必然终止（Qt 原文同款遍历）。 */
    {
        int nInt = XItemSelection_count(&intersections);
        for (i = 0; i < nInt; ++i) {
            XItemSelectionRange inter = XItemSelection_at(&intersections, i);
            int t = 0;
            while (t < XItemSelection_count(self)) {
                XItemSelectionRange s = XItemSelection_at(self, t);
                if (XItemSelectionRange_intersects(&s, &inter)) {
                    XItemSelection_split(s, inter, self);
                    XItemSelection_removeAt(self, t);
                } else {
                    ++t;
                }
            }
            if (command & XIS_CMD_TOGGLE) {
                t = 0;
                while (t < XItemSelection_count(&newSelection)) {
                    XItemSelectionRange s =
                        XItemSelection_at(&newSelection, t);
                    if (XItemSelectionRange_intersects(&s, &inter)) {
                        XItemSelection_split(s, inter, &newSelection);
                        XItemSelection_removeAt(&newSelection, t);
                    } else {
                        ++t;
                    }
                }
            }
        }
    }
    /* ③ Deselect 不并入 newSelection（"do not add newSelection for
       Deselect"，:496）；其余命令把 newSelection 全部追加（:497）。 */
    if (!(command & XIS_CMD_DESELECT)) {
        int nn = XItemSelection_count(&newSelection);
        for (i = 0; i < nn; ++i)
            XItemSelection_append(self, XItemSelection_at(&newSelection, i));
    }
    XItemSelection_deinit(&intersections);
    XItemSelection_deinit(&newSelection);
    return true;
}

bool XItemSelection_equal(const XItemSelection* a, const XItemSelection* b)
{
    int i;
    int n;
    if (!a || !b) return a == b;
    n = XItemSelection_count(a);
    if (n != XItemSelection_count(b)) return false;
    for (i = 0; i < n; ++i) {
        XItemSelectionRange ra = XItemSelection_at(a, i);
        XItemSelectionRange rb = XItemSelection_at(b, i);
        if (!XItemSelectionRange_equal(&ra, &rb)) return false;
    }
    return true;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#endif /* XGUI_ON */
