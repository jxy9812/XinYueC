/**
 * @file       XCandlestickModelMapper.c
 * @brief      XCandlestickModelMapper 基类实现（对标 QCandlestickModelMapper）。
 * @details    五字段 section 与 set 区间由基类持有；remap 按 m_orientation
 *             决定「字段是行还是列」「set 沿列还是行推进」。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XCandlestickModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xcsmm_emitVoid(XCandlestickModelMapper* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!self || !((const XObject*)self)->m_signalSlot) return;
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 虚函数实现 ==================== */

static void VXCandlestickModelMapper_deinit(XCandlestickModelMapper* self)
{
    if (!self) return;
    /* 模型与序列均为借用指针，不在此释放。 */
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XCandlestickModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCandlestickModelMapper)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCandlestickModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XCandlestickModelMapper_init(XCandlestickModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XCandlestickModelMapper);
    self->m_timestampSection = -1;
    self->m_openSection = -1;
    self->m_highSection = -1;
    self->m_lowSection = -1;
    self->m_closeSection = -1;
    self->m_firstSetSection = -1;
    self->m_lastSetSection = -1;
    self->m_orientation = XModelMapper_Orientation_Vertical;
}

XCandlestickModelMapper* XCandlestickModelMapper_create_ex(XMemoryType memory)
{
    XCandlestickModelMapper* self =
        (XCandlestickModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XCandlestickModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 模型与序列 ==================== */

void XCandlestickModelMapper_setModel(XCandlestickModelMapper* self,
                                      XAbstractItemModel* model)
{
    if (!self) return;
    self->m_model = model;
}

XAbstractItemModel* XCandlestickModelMapper_model(
    const XCandlestickModelMapper* self)
{ return self ? self->m_model : NULL; }

void XCandlestickModelMapper_setSeries(XCandlestickModelMapper* self,
                                       XCandlestickSeries* series)
{
    if (!self) return;
    self->m_series = series;
}

XCandlestickSeries* XCandlestickModelMapper_series(
    const XCandlestickModelMapper* self)
{ return self ? self->m_series : NULL; }

int XCandlestickModelMapper_orientation(const XCandlestickModelMapper* self)
{ return self ? self->m_orientation : 0; }

/* ==================== 五字段 section ==================== */

void XCandlestickModelMapper_setTimestamp(XCandlestickModelMapper* self,
                                          int section)
{
    if (!self) return;
    self->m_timestampSection = section;
}

int XCandlestickModelMapper_timestamp(const XCandlestickModelMapper* self)
{ return self ? self->m_timestampSection : -1; }

void XCandlestickModelMapper_setOpen(XCandlestickModelMapper* self,
                                     int section)
{
    if (!self) return;
    self->m_openSection = section;
}

int XCandlestickModelMapper_open(const XCandlestickModelMapper* self)
{ return self ? self->m_openSection : -1; }

void XCandlestickModelMapper_setHigh(XCandlestickModelMapper* self,
                                     int section)
{
    if (!self) return;
    self->m_highSection = section;
}

int XCandlestickModelMapper_high(const XCandlestickModelMapper* self)
{ return self ? self->m_highSection : -1; }

void XCandlestickModelMapper_setLow(XCandlestickModelMapper* self, int section)
{
    if (!self) return;
    self->m_lowSection = section;
}

int XCandlestickModelMapper_low(const XCandlestickModelMapper* self)
{ return self ? self->m_lowSection : -1; }

void XCandlestickModelMapper_setClose(XCandlestickModelMapper* self,
                                      int section)
{
    if (!self) return;
    self->m_closeSection = section;
}

int XCandlestickModelMapper_close(const XCandlestickModelMapper* self)
{ return self ? self->m_closeSection : -1; }

void XCandlestickModelMapper_setFirstSetSection(XCandlestickModelMapper* self,
                                                int section)
{
    if (!self) return;
    self->m_firstSetSection = section;
}

int XCandlestickModelMapper_firstSetSection(
    const XCandlestickModelMapper* self)
{ return self ? self->m_firstSetSection : -1; }

void XCandlestickModelMapper_setLastSetSection(XCandlestickModelMapper* self,
                                               int section)
{
    if (!self) return;
    self->m_lastSetSection = section;
}

int XCandlestickModelMapper_lastSetSection(
    const XCandlestickModelMapper* self)
{ return self ? self->m_lastSetSection : -1; }

/* ==================== 同步 ==================== */

bool XCandlestickModelMapper_remap(XCandlestickModelMapper* self)
{
    int n;
    int i;
    int first;
    int last;
    int mapped = 0;
    if (!self || !self->m_model || !self->m_series) return false;
    if (self->m_timestampSection < 0 || self->m_openSection < 0 ||
        self->m_highSection < 0 || self->m_lowSection < 0 ||
        self->m_closeSection < 0)
        return false;
    first = self->m_firstSetSection;
    last = self->m_lastSetSection;
    if (first < 0) return false;
    if (last < first) last = first;
    n = last - first + 1;
    XCandlestickSeries_clear(self->m_series);
    for (i = 0; i < n; ++i) {
        int idx = first + i;
        int rt; int ct; int ro; int co; int rh; int ch;
        int rl; int cl; int rc; int cc;
        XCandlestickSet* set;
        xmodelmapper_sectionCell(self->m_orientation, first,
                                 self->m_timestampSection, idx, &rt, &ct);
        xmodelmapper_sectionCell(self->m_orientation, first,
                                 self->m_openSection, idx, &ro, &co);
        xmodelmapper_sectionCell(self->m_orientation, first,
                                 self->m_highSection, idx, &rh, &ch);
        xmodelmapper_sectionCell(self->m_orientation, first,
                                 self->m_lowSection, idx, &rl, &cl);
        xmodelmapper_sectionCell(self->m_orientation, first,
                                 self->m_closeSection, idx, &rc, &cc);
        set = XCandlestickSet_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
            xmodelmapper_cellToDouble(self->m_model, rt, ct));
        if (!set) continue;
        XCandlestickSet_setOpen(set,
            xmodelmapper_cellToDouble(self->m_model, ro, co));
        XCandlestickSet_setHigh(set,
            xmodelmapper_cellToDouble(self->m_model, rh, ch));
        XCandlestickSet_setLow(set,
            xmodelmapper_cellToDouble(self->m_model, rl, cl));
        XCandlestickSet_setClose(set,
            xmodelmapper_cellToDouble(self->m_model, rc, cc));
        if (!XCandlestickSeries_append(self->m_series, set)) {
            XClassDelete(set);
            continue;
        }
        ++mapped;
    }
    return mapped > 0;
}

/* ==================== 信号 ==================== */

void* XCandlestickModelMapper_modelReplaced_signal(XCandlestickModelMapper* self)
{
    xcsmm_emitVoid(self, (size_t)XCandlestickModelMapper_modelReplaced_signal);
    return (void*)(size_t)XCandlestickModelMapper_modelReplaced_signal;
}

void* XCandlestickModelMapper_seriesReplaced_signal(XCandlestickModelMapper* self)
{
    xcsmm_emitVoid(self, (size_t)XCandlestickModelMapper_seriesReplaced_signal);
    return (void*)(size_t)XCandlestickModelMapper_seriesReplaced_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
