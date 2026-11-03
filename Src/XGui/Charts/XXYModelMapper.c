/**
 * @file       XXYModelMapper.c
 * @brief      XXYModelMapper 基类实现（对标 QXYModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XXYModelMapper.h"
#include "XModelMapper_Protected.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XString.h"

#include <stdlib.h>

#if XCHARTS_ON

/* ==================== 内部助手 ==================== */

/** @brief 把单元格文本解析成 double；空串或非法返回 0。 */
double xmodelmapper_cellToDouble(const XAbstractItemModel* model, int row,
                                 int col)
{
    const char* s;
    if (!model) return 0.0;
    s = XAbstractItemModel_data_2(model, row, col);
    if (!s || !*s) return 0.0;
    return strtod(s, NULL);
}

/** @brief 按方向把「区间内第 idx 项」映射到 (row,col)。
 * @details 横向：固定首行为基准行，列按 idx 推进；
 *          纵向：固定首列为基准列，行按 idx 推进。 */
void xmodelmapper_stepCell(int orientation, int firstIdx, int idx,
                           int* row, int* col)
{
    if (orientation == XModelMapper_Orientation_Horizontal) {
        *row = firstIdx;
        *col = idx;
    } else {
        *row = idx;
        *col = firstIdx;
    }
}

/** @brief 按方向取「指定 section 在某项处的单元格」。 */
void xmodelmapper_sectionCell(int orientation, int firstIdx, int section,
                              int idx, int* row, int* col)
{
    if (orientation == XModelMapper_Orientation_Horizontal) {
        *row = section;
        *col = idx;
    } else {
        *row = idx;
        *col = section;
    }
}

/** @brief 按方向计算可映射项数（count<0 时取模型维度减起始偏移）。 */
int xmodelmapper_availableCount(const XAbstractItemModel* model,
                                int orientation, int first, int count)
{
    int limit;
    if (!model) return 0;
    limit = (orientation == XModelMapper_Orientation_Horizontal)
        ? XAbstractItemModel_columnCount(model)
        : XAbstractItemModel_rowCount(model);
    if (count >= 0) {
        if (count < limit) limit = count;
    } else {
        limit -= first;
    }
    return limit < 0 ? 0 : limit;
}

/* ==================== 虚函数实现 ==================== */

static void VXXYModelMapper_deinit(XXYModelMapper* self)
{
    if (!self) return;
    /* 模型与序列均为借用指针，不在此释放。 */
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XXYModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XXYModelMapper)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXXYModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XXYModelMapper_init(XXYModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XXYModelMapper);
    self->m_first = 0;
    self->m_count = -1;             /* Qt 默认：按模型维度推导 */
    self->m_orientation = XModelMapper_Orientation_Vertical;
    self->m_xSection = -1;
    self->m_ySection = -1;
}

XXYModelMapper* XXYModelMapper_create_ex(XMemoryType memory)
{
    XXYModelMapper* self =
        (XXYModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XXYModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 模型与序列 ==================== */

void XXYModelMapper_setModel(XXYModelMapper* self, XAbstractItemModel* model)
{
    if (!self) return;
    self->m_model = model;
}

XAbstractItemModel* XXYModelMapper_model(const XXYModelMapper* self)
{ return self ? self->m_model : NULL; }

void XXYModelMapper_setSeries(XXYModelMapper* self, XXYSeries* series)
{
    if (!self) return;
    self->m_series = series;
}

XXYSeries* XXYModelMapper_series(const XXYModelMapper* self)
{ return self ? self->m_series : NULL; }

/* ==================== 映射参数 ==================== */

void XXYModelMapper_setFirst(XXYModelMapper* self, int first)
{
    if (!self || first < 0) return;
    self->m_first = first;
}

int XXYModelMapper_first(const XXYModelMapper* self)
{ return self ? self->m_first : 0; }

void XXYModelMapper_setCount(XXYModelMapper* self, int count)
{
    if (!self) return;
    self->m_count = count;
}

int XXYModelMapper_count(const XXYModelMapper* self)
{ return self ? self->m_count : -1; }

void XXYModelMapper_setOrientation(XXYModelMapper* self, int orientation)
{
    if (!self) return;
    self->m_orientation = orientation;
}

int XXYModelMapper_orientation(const XXYModelMapper* self)
{ return self ? self->m_orientation : 0; }

void XXYModelMapper_setXSection(XXYModelMapper* self, int xSection)
{
    if (!self) return;
    self->m_xSection = xSection;
}

int XXYModelMapper_xSection(const XXYModelMapper* self)
{ return self ? self->m_xSection : -1; }

void XXYModelMapper_setYSection(XXYModelMapper* self, int ySection)
{
    if (!self) return;
    self->m_ySection = ySection;
}

int XXYModelMapper_ySection(const XXYModelMapper* self)
{ return self ? self->m_ySection : -1; }

/* ==================== 同步 ==================== */

bool XXYModelMapper_remap(XXYModelMapper* self)
{
    int n;
    int i;
    if (!self || !self->m_model || !self->m_series) return false;
    if (self->m_xSection < 0 || self->m_ySection < 0) return false;
    n = xmodelmapper_availableCount(self->m_model, self->m_orientation,
                                    self->m_first, self->m_count);
    XXYSeries_clear(self->m_series);
    for (i = 0; i < n; ++i) {
        int idx = self->m_first + i;
        int rx; int cx; int ry; int cy;
        xmodelmapper_sectionCell(self->m_orientation, self->m_first,
                                 self->m_xSection, idx, &rx, &cx);
        xmodelmapper_sectionCell(self->m_orientation, self->m_first,
                                 self->m_ySection, idx, &ry, &cy);
        XXYSeries_append(self->m_series,
                         xmodelmapper_cellToDouble(self->m_model, rx, cx),
                         xmodelmapper_cellToDouble(self->m_model, ry, cy));
    }
    return n > 0;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
