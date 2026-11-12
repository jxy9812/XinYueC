/**
 * @file       XModelIndex.c
 * @brief      XModelIndex 模型索引值对象实现（对标 Qt 6.8 QModelIndex）。
 * @details    纯值语义：所有函数对 self==NULL 安全；模型委托经过 NULL 与
 *             有效性守卫后转发给 XAbstractItemModel 的对应入口。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XModelIndex.h"

#include "XAbstractItemModel.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 构造 / 值语义 ==================== */

XModelIndex XModelIndex_make(int row, int column, uintptr_t internalId,
                             const struct XAbstractItemModel* model)
{
    XModelIndex v;
    v.m_row = row;
    v.m_column = column;
    v.m_internalId = internalId;
    v.m_model = model;
    return v;
}

XModelIndex XModelIndex_invalid(void)
{
    /* 对标 Qt 默认构造：r=-1,c=-1,i=0,m=nullptr
       （qabstractitemmodel.h:122）。 */
    return XModelIndex_make(-1, -1, 0, NULL);
}

/* ==================== 访问器 ==================== */

int XModelIndex_row(const XModelIndex* self)
{ return self ? self->m_row : -1; }

int XModelIndex_column(const XModelIndex* self)
{ return self ? self->m_column : -1; }

uintptr_t XModelIndex_internalId(const XModelIndex* self)
{ return self ? self->m_internalId : 0; }

void* XModelIndex_internalPointer(const XModelIndex* self)
{
    /* 对标 QModelIndex::internalPointer：reinterpret_cast<void*>(i)。 */
    return self ? (void*)(self->m_internalId) : NULL;
}

const void* XModelIndex_constInternalPointer(const XModelIndex* self)
{
    /* 对标 QModelIndex::constInternalPointer。 */
    return self ? (const void*)(self->m_internalId) : NULL;
}

const struct XAbstractItemModel* XModelIndex_model(const XModelIndex* self)
{ return self ? self->m_model : NULL; }

bool XModelIndex_isValid(const XModelIndex* self)
{
    /* 逐条对齐 Qt 6.8 qabstractitemmodel.h:139：
       (r >= 0) && (c >= 0) && (m != nullptr)。internalId 不参与。 */
    return self && self->m_row >= 0 && self->m_column >= 0 &&
           self->m_model != NULL;
}

/* ==================== 比较 ==================== */

bool XModelIndex_equal(const XModelIndex* a, const XModelIndex* b)
{
    XModelIndex la;
    XModelIndex lb;
    /* NULL 入参按无效索引参与（与 XModelIndex_invalid 同形）。 */
    la = a ? *a : XModelIndex_invalid();
    lb = b ? *b : XModelIndex_invalid();
    /* 对标 comparesEqual（qabstractitemmodel.h:145-149）：四字段逐一相等。 */
    return la.m_row == lb.m_row && la.m_column == lb.m_column &&
           la.m_internalId == lb.m_internalId && la.m_model == lb.m_model;
}

int XModelIndex_compare(const XModelIndex* a, const XModelIndex* b)
{
    XModelIndex la;
    XModelIndex lb;
    la = a ? *a : XModelIndex_invalid();
    lb = b ? *b : XModelIndex_invalid();
    /* 对标 compareThreeWay（qabstractitemmodel.h:150-160）：r→c→i→m。 */
    if (la.m_row != lb.m_row) return la.m_row < lb.m_row ? -1 : 1;
    if (la.m_column != lb.m_column)
        return la.m_column < lb.m_column ? -1 : 1;
    if (la.m_internalId != lb.m_internalId)
        return la.m_internalId < lb.m_internalId ? -1 : 1;
    if ((uintptr_t)la.m_model != (uintptr_t)lb.m_model)
        return (uintptr_t)la.m_model < (uintptr_t)lb.m_model ? -1 : 1;
    return 0;
}

/* ==================== 模型委托 ==================== */

XModelIndex XModelIndex_parent(const XModelIndex* self)
{
    /* 对标 QModelIndex::parent：`m ? m->parent(*this) : QModelIndex()`
       （qabstractitemmodel.h:505-506）。真实父级由模型树节点表解码。 */
    if (!self || !self->m_model) return XModelIndex_invalid();
    return XAbstractItemModel_parentIndex(
        (const XAbstractItemModel*)self->m_model, self);
}

XModelIndex XModelIndex_sibling(const XModelIndex* self, int row, int column)
{
    /* 对标 QModelIndex::sibling（qabstractitemmodel.h:508-509）：
       行列不变时原样返回；否则换 row/column 但**保留父级**。

       Qt 经模型 sibling() → index(row,column,parent(idx))，产出的索引
       internalId 由模型重新分配；本库 XModelIndex 是纯值，internalId 直接
       复用基底即可保证 parent() 解码到同一父级（§B2 映射口径：
       合成兄弟索引不另建树节点，父链语义由 internalId 承载）。 */
    if (!self || !self->m_model) return XModelIndex_invalid();
    return XModelIndex_make(row, column, self->m_internalId, self->m_model);
}

XModelIndex XModelIndex_siblingAtColumn(const XModelIndex* self, int column)
{
    /* 对标 siblingAtColumn（qabstractitemmodel.h:511-512）。 */
    if (!self || !self->m_model) return XModelIndex_invalid();
    return XModelIndex_make(self->m_row, column, self->m_internalId,
                            self->m_model);
}

XModelIndex XModelIndex_siblingAtRow(const XModelIndex* self, int row)
{
    /* 对标 siblingAtRow（qabstractitemmodel.h:514-515）。 */
    if (!self || !self->m_model) return XModelIndex_invalid();
    return XModelIndex_make(row, self->m_column, self->m_internalId,
                            self->m_model);
}

const XString* XModelIndex_data(const XModelIndex* self, int role)
{
    /* 对标 QModelIndex::data(role)：`m ? m->data(*this, role) : QVariant()`
       （qabstractitemmodel.h:517-518）；本库返回借用 XString*。 */
    if (!XModelIndex_isValid(self)) return NULL;
    return XAbstractItemModel_dataRole(
        (const XAbstractItemModel*)self->m_model, self->m_row,
        self->m_column, role);
}

int XModelIndex_flags(const XModelIndex* self)
{
    /* 对标 QModelIndex::flags（qabstractitemmodel.h:523-524）。 */
    if (!XModelIndex_isValid(self)) return XItemFlag_NoItemFlags;
    return XAbstractItemModel_flags_base(
        (const XAbstractItemModel*)self->m_model, self->m_row,
        self->m_column);
}

void XModelIndex_multiData(const XModelIndex* self, XModelRoleData* span,
                           int count)
{
    int i;
    /* 对标 QModelIndex::multiData（qabstractitemmodel.h:520-521）：
       委托模型 multiData；非法入参时把各槽 m_data 置 NULL（空 QVariant）。 */
    if (span && count > 0) {
        for (i = 0; i < count; ++i) span[i].m_data = NULL;
    }
    if (!XModelIndex_isValid(self) || !span || count <= 0) return;
    XAbstractItemModel_multiData(
        (const XAbstractItemModel*)self->m_model, self->m_row,
        self->m_column, span, count);
}

/* ==================== 值操作 ==================== */

XModelIndex XModelIndex_withRowColumn(const XModelIndex* self, int row,
                                      int column)
{
    /* 供 XItemSelectionRange::intersected/split 构造同父角标（对标 Qt
       split 里的 model->index(r,c,parent)）。 */
    if (!self || !self->m_model) return XModelIndex_invalid();
    return XModelIndex_make(row, column, self->m_internalId, self->m_model);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#endif /* XGUI_ON */
