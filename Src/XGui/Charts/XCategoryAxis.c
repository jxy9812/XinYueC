/**
 * @file       XCategoryAxis.c
 * @brief      XCategoryAxis 类别轴实现（对标 QBarCategoryAxis）。
 * @details    类别集合增删改查 + Qt 的字符串形态 min/max（类别名而非
 *             数值）+ 数值下标范围（渲染内部语义）。
 * @author     XinYueC 团队
 */
#if XGUI_ON

#include "XCategoryAxis.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"
#include "XChar.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xcat_canEmit(const XCategoryAxis* self)
{
    return self && ((const XObject*)&self->m_base)->m_signalSlot;
}

static void xcat_emitVoid(XCategoryAxis* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!xcat_canEmit(self)) return;
    /* XVarList_Create 宏至少需要一个实参（对实参取址，不能传字面量）；
     * 无载荷信号用哑元占位，槽侧不读取（与 Qt 无参信号语义一致）。 */
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcat_emitCount(XCategoryAxis* self, int count)
{
    XVarList* args;
    if (!xcat_canEmit(self)) return;
    args = XVarList_Create(XVar(int, count));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base,
                       (size_t)XCategoryAxis_countChanged_signal,
                       args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static void xcat_emitStr(XCategoryAxis* self, size_t signal, const char* s)
{
    XVarList* args;
    if (!xcat_canEmit(self)) return;
    args = XVarList_Create(XVar(const char*, s));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcat_emitRangeStr(XCategoryAxis* self, size_t signal,
                              const char* mi, const char* ma)
{
    XVarList* args;
    if (!xcat_canEmit(self)) return;
    args = XVarList_Create(XVar(const char*, mi), XVar(const char*, ma));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 内部助手 ==================== */

/** @brief 确保容量足够（不足时倍增）。 @return 成功返回 true。 */
static bool xcat_reserve(XCategoryAxis* self, int need)
{
    int cap;
    int oldCap;
    int ci;
    XString** cat;
    if (!self || need < 0) return false;
    if (need <= self->m_capacity) return true;
    cap = self->m_capacity > 0 ? self->m_capacity : 8;
    while (cap < need) cap *= 2;
    oldCap = self->m_capacity;
    cat = (XString**)XRealloc_System(self->m_categories,
                                     sizeof(XString*) * (size_t)cap);
    if (!cat) return false;
    self->m_categories = cat;
    for (ci = oldCap; ci < cap; ++ci)
        self->m_categories[ci] = NULL;
    self->m_capacity = cap;
    return true;
}

/** @brief 同步数值上限 = 类别数，并广播 countChanged。 */
static void xcat_syncCount(XCategoryAxis* self)
{
    self->m_base.m_max = (double)self->m_count;
    xcat_emitCount(self, self->m_count);
    xcat_emitVoid(self, (size_t)XCategoryAxis_categoriesChanged_signal);
}

/** @brief 查找类别下标；未找到返回 -1。 */
static int xcat_indexOf(const XCategoryAxis* self, const XString* label)
{
    int i;
    if (!self || !label || !self->m_categories) return -1;
    for (i = 0; i < self->m_count; ++i) {
        const XString* c = self->m_categories[i];
        if (!c) continue;
        if (XString_compare(c, label) == 0) return i;
    }
    return -1;
}

/** @brief 查找 UTF-8 类别下标；未找到返回 -1。 */
static int xcat_indexOfUtf8(const XCategoryAxis* self, const char* label)
{
    int i;
    if (!self || !label || !self->m_categories) return -1;
    for (i = 0; i < self->m_count; ++i) {
        const XString* c = self->m_categories[i];
        if (!c) continue;
        if (XString_equals_utf8(c, label, XChar_CaseSensitive) == true)
            return i;
    }
    return -1;
}

/* ==================== 类与对象生命周期 ==================== */

static void VXCategoryAxis_deinit(XCategoryAxis* self)
{
    int i;
    if (!self) return;
    if (self->m_categories) {
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_categories[i]) {
                XClassDelete(self->m_categories[i]);
                self->m_categories[i] = NULL;
            }
        }
        XFree_System(self->m_categories);
        self->m_categories = NULL;
    }
    self->m_count = 0;
    self->m_capacity = 0;
    XClass_Deinit_Parent(XAbstractAxis, &self->m_base);
}

XVtable* XCategoryAxis_class_init(void)
{
    /* 本类为组合式继承且不新增虚槽（头文件未定义虚表枚举）：
       按父类槽位容量建表，仅重载 EXClass_Deinit 释放类别表。 */
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(XAbstractAxis))
    XCLASS_SET_CLASS_NAME_DEFAULT("XCategoryAxis");
    XVTABLE_INHERIT_XCLASS(XAbstractAxis);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCategoryAxis_deinit);
    return XVTABLE_DEFAULT;
}

void XCategoryAxis_init(XCategoryAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractAxis_init(&self->m_base);
    XClassSetVtable(self, XCategoryAxis);
    self->m_base.m_min = 0.0;
    self->m_base.m_max = 0.0;
    self->m_base.m_axisType = XAbstractAxis_AxisType_BarCategory;
}

XCategoryAxis* XCategoryAxis_create_ex(XMemoryType memory)
{
    XCategoryAxis* self =
        (XCategoryAxis*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XCategoryAxis_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XCategoryAxis_deinit_impl(XCategoryAxis* self)
{
    int i;
    if (!self) return;
    if (self->m_categories) {
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_categories[i]) {
                XClassDelete(self->m_categories[i]);
                self->m_categories[i] = NULL;
            }
        }
        XFree_System(self->m_categories);
        self->m_categories = NULL;
    }
    XClassDeinit(&self->m_base);
}

/* ==================== 类型查询 ==================== */

int XCategoryAxis_type(const XCategoryAxis* self)
{
    (void)self;
    return XAbstractAxis_AxisType_BarCategory;
}

/* ==================== 类别集合 ==================== */

int XCategoryAxis_append(XCategoryAxis* self, const XString* label)
{
    int idx;
    if (!self || !label) return -1;
    if (!xcat_reserve(self, self->m_count + 1)) return -1;
    idx = self->m_count;
    self->m_categories[idx] = XString_create_copy(label);
    if (!self->m_categories[idx]) return -1;
    self->m_count++;
    xcat_syncCount(self);
    return idx;
}

int XCategoryAxis_append_utf8(XCategoryAxis* self, const char* label)
{
    XString* tmp;
    int idx;
    if (!label) return -1;
    tmp = XString_create_utf8(label);
    if (!tmp) return -1;
    idx = XCategoryAxis_append(self, tmp);
    XClassDelete(tmp);
    return idx;
}

int XCategoryAxis_appendList(XCategoryAxis* self, const XString* const* labels,
                             int count)
{
    int i;
    int n = 0;
    if (!self || !labels || count <= 0) return 0;
    for (i = 0; i < count; ++i) {
        if (!labels[i]) continue;
        if (XCategoryAxis_append(self, labels[i]) >= 0) ++n;
    }
    return n;
}

void XCategoryAxis_setCategories(XCategoryAxis* self,
                                 const XString* const* labels, int count)
{
    if (!self) return;
    XCategoryAxis_clear(self);
    if (labels && count > 0) XCategoryAxis_appendList(self, labels, count);
}

bool XCategoryAxis_remove(XCategoryAxis* self, const XString* label)
{
    int idx = xcat_indexOf(self, label);
    int i;
    if (idx < 0) return false;
    XClassDelete(self->m_categories[idx]);
    for (i = idx; i + 1 < self->m_count; ++i)
        self->m_categories[i] = self->m_categories[i + 1];
    self->m_categories[--self->m_count] = NULL;
    xcat_syncCount(self);
    return true;
}

bool XCategoryAxis_remove_utf8(XCategoryAxis* self, const char* label)
{
    int idx = xcat_indexOfUtf8(self, label);
    int i;
    if (idx < 0) return false;
    XClassDelete(self->m_categories[idx]);
    for (i = idx; i + 1 < self->m_count; ++i)
        self->m_categories[i] = self->m_categories[i + 1];
    self->m_categories[--self->m_count] = NULL;
    xcat_syncCount(self);
    return true;
}

bool XCategoryAxis_insert(XCategoryAxis* self, int index, const XString* label)
{
    int i;
    XString* cp;
    if (!self || !label) return false;
    if (index < 0) index = 0;
    if (index > self->m_count) index = self->m_count;
    if (!xcat_reserve(self, self->m_count + 1)) return false;
    cp = XString_create_copy(label);
    if (!cp) return false;
    for (i = self->m_count; i > index; --i)
        self->m_categories[i] = self->m_categories[i - 1];
    self->m_categories[index] = cp;
    self->m_count++;
    xcat_syncCount(self);
    return true;
}

bool XCategoryAxis_insert_utf8(XCategoryAxis* self, int index, const char* label)
{
    XString* tmp;
    bool ok;
    if (!label) return false;
    tmp = XString_create_utf8(label);
    if (!tmp) return false;
    ok = XCategoryAxis_insert(self, index, tmp);
    XClassDelete(tmp);
    return ok;
}

bool XCategoryAxis_replace(XCategoryAxis* self, const XString* oldLabel,
                           const XString* newLabel)
{
    int idx = xcat_indexOf(self, oldLabel);
    XString* cp;
    if (idx < 0 || !newLabel) return false;
    cp = XString_create_copy(newLabel);
    if (!cp) return false;
    XClassDelete(self->m_categories[idx]);
    self->m_categories[idx] = cp;
    xcat_emitVoid(self, (size_t)XCategoryAxis_categoriesChanged_signal);
    return true;
}

bool XCategoryAxis_replace_utf8(XCategoryAxis* self, const char* oldLabel,
                                const char* newLabel)
{
    XString* o;
    XString* n;
    bool ok;
    if (!oldLabel || !newLabel) return false;
    o = XString_create_utf8(oldLabel);
    n = XString_create_utf8(newLabel);
    if (!o || !n) {
        if (o) XClassDelete(o);
        if (n) XClassDelete(n);
        return false;
    }
    ok = XCategoryAxis_replace(self, o, n);
    XClassDelete(o);
    XClassDelete(n);
    return ok;
}

void XCategoryAxis_clear(XCategoryAxis* self)
{
    int i;
    if (!self) return;
    if (self->m_categories) {
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_categories[i]) {
                XClassDelete(self->m_categories[i]);
                self->m_categories[i] = NULL;
            }
        }
    }
    self->m_count = 0;
    xcat_syncCount(self);
}

int XCategoryAxis_count(const XCategoryAxis* self)
{ return self ? self->m_count : 0; }

const XString* XCategoryAxis_at_const(const XCategoryAxis* self, int index)
{
    if (!self || index < 0 || index >= self->m_count ||
        !self->m_categories || !self->m_categories[index])
        return NULL;
    return self->m_categories[index];
}

const char* XCategoryAxis_at_2(const XCategoryAxis* self, int index)
{
    const XString* s = XCategoryAxis_at_const(self, index);
    return s ? XString_toUtf8(s) : "";
}

/* ==================== 字符串范围 ==================== */

void XCategoryAxis_setMinCategory(XCategoryAxis* self,
                                  const XString* minCategory)
{
    int idx;
    if (!self) return;
    if (!minCategory) {
        XAbstractAxis_setRange(&self->m_base, 0.0, (double)self->m_count);
        return;
    }
    idx = xcat_indexOf(self, minCategory);
    if (idx < 0) return;
    XAbstractAxis_setRange(&self->m_base, (double)idx,
                           self->m_base.m_max < (double)idx
                               ? (double)idx : self->m_base.m_max);
    xcat_emitStr(self, (size_t)XCategoryAxis_minChanged_signal,
                 XString_toUtf8(minCategory));
}

void XCategoryAxis_setMinCategory_utf8(XCategoryAxis* self, const char* minCategory)
{
    XString* tmp;
    if (!minCategory) {
        XCategoryAxis_setMinCategory(self, NULL);
        return;
    }
    tmp = XString_create_utf8(minCategory);
    if (!tmp) return;
    XCategoryAxis_setMinCategory(self, tmp);
    XClassDelete(tmp);
}

const XString* XCategoryAxis_minCategory_const(const XCategoryAxis* self)
{
    int idx;
    if (!self) return NULL;
    idx = (int)self->m_base.m_min;
    if (idx < 0) idx = 0;
    if (idx >= self->m_count) idx = self->m_count - 1;
    if (idx < 0) return NULL;
    return XCategoryAxis_at_const(self, idx);
}

const char* XCategoryAxis_minCategory_2(const XCategoryAxis* self)
{
    const XString* s = XCategoryAxis_minCategory_const(self);
    return s ? XString_toUtf8(s) : "";
}

void XCategoryAxis_setMaxCategory(XCategoryAxis* self,
                                  const XString* maxCategory)
{
    int idx;
    if (!self) return;
    if (!maxCategory) {
        XAbstractAxis_setRange(&self->m_base, 0.0, (double)self->m_count);
        return;
    }
    idx = xcat_indexOf(self, maxCategory);
    if (idx < 0) return;
    XAbstractAxis_setRange(&self->m_base, self->m_base.m_min,
                           (double)idx);
    xcat_emitStr(self, (size_t)XCategoryAxis_maxChanged_signal,
                 XString_toUtf8(maxCategory));
}

void XCategoryAxis_setMaxCategory_utf8(XCategoryAxis* self, const char* maxCategory)
{
    XString* tmp;
    if (!maxCategory) {
        XCategoryAxis_setMaxCategory(self, NULL);
        return;
    }
    tmp = XString_create_utf8(maxCategory);
    if (!tmp) return;
    XCategoryAxis_setMaxCategory(self, tmp);
    XClassDelete(tmp);
}

const XString* XCategoryAxis_maxCategory_const(const XCategoryAxis* self)
{
    int idx;
    if (!self) return NULL;
    idx = (int)self->m_base.m_max;
    if (idx >= self->m_count) idx = self->m_count - 1;
    if (idx < 0) idx = 0;
    if (idx >= self->m_count) return NULL;
    return XCategoryAxis_at_const(self, idx);
}

const char* XCategoryAxis_maxCategory_2(const XCategoryAxis* self)
{
    const XString* s = XCategoryAxis_maxCategory_const(self);
    return s ? XString_toUtf8(s) : "";
}

void XCategoryAxis_setRangeCategory(XCategoryAxis* self,
                                    const XString* minCategory,
                                    const XString* maxCategory)
{
    int lo;
    int hi;
    if (!self) return;
    lo = minCategory ? xcat_indexOf(self, minCategory) : 0;
    hi = maxCategory ? xcat_indexOf(self, maxCategory) : self->m_count;
    if (lo < 0 || hi < 0) return;
    if (hi < lo) hi = lo;
    XAbstractAxis_setRange(&self->m_base, (double)lo, (double)hi);
    if (minCategory)
        xcat_emitStr(self, (size_t)XCategoryAxis_minChanged_signal,
                     XString_toUtf8(minCategory));
    if (maxCategory)
        xcat_emitStr(self, (size_t)XCategoryAxis_maxChanged_signal,
                     XString_toUtf8(maxCategory));
    {
        const XString* a = XCategoryAxis_minCategory_const(self);
        const XString* b = XCategoryAxis_maxCategory_const(self);
        xcat_emitRangeStr(self, (size_t)XCategoryAxis_rangeChanged_signal,
                          a ? XString_toUtf8(a) : "",
                          b ? XString_toUtf8(b) : "");
    }
}

void XCategoryAxis_setRangeCategory_utf8(XCategoryAxis* self,
                                         const char* minCategory,
                                         const char* maxCategory)
{
    XString* a = minCategory ? XString_create_utf8(minCategory) : NULL;
    XString* b = maxCategory ? XString_create_utf8(maxCategory) : NULL;
    XCategoryAxis_setRangeCategory(self, a, b);
    if (a) XClassDelete(a);
    if (b) XClassDelete(b);
}

/* ==================== Qt 命名的 min/max/categories ==================== */

void XCategoryAxis_setMinCategoryQt(XCategoryAxis* self,
                                    const XString* minCategory)
{ XCategoryAxis_setMinCategory(self, minCategory); }

void XCategoryAxis_setMaxCategoryQt(XCategoryAxis* self,
                                    const XString* maxCategory)
{ XCategoryAxis_setMaxCategory(self, maxCategory); }

const XString* XCategoryAxis_categories_const(const XCategoryAxis* self, int index)
{ return XCategoryAxis_at_const(self, index); }

const char* XCategoryAxis_categories_2(const XCategoryAxis* self, int index)
{ return XCategoryAxis_at_2(self, index); }

/* ==================== Qt 命名形态（对标 QBarCategoryAxis::setMin/setMax/setRange） ==================== */

void XCategoryAxis_setMin(XCategoryAxis* self, const XString* minCategory)
{ XCategoryAxis_setMinCategory(self, minCategory); }

void XCategoryAxis_setMin_utf8(XCategoryAxis* self, const char* minCategory)
{ XCategoryAxis_setMinCategory_utf8(self, minCategory); }

void XCategoryAxis_setMax(XCategoryAxis* self, const XString* maxCategory)
{ XCategoryAxis_setMaxCategory(self, maxCategory); }

void XCategoryAxis_setMax_utf8(XCategoryAxis* self, const char* maxCategory)
{ XCategoryAxis_setMaxCategory_utf8(self, maxCategory); }

void XCategoryAxis_setRange(XCategoryAxis* self, const XString* minCategory,
                            const XString* maxCategory)
{ XCategoryAxis_setRangeCategory(self, minCategory, maxCategory); }

void XCategoryAxis_setRange_utf8(XCategoryAxis* self, const char* minCategory,
                                 const char* maxCategory)
{ XCategoryAxis_setRangeCategory_utf8(self, minCategory, maxCategory); }

/* ==================== 数值范围 ==================== */

double XCategoryAxis_min(const XCategoryAxis* self)
{ return self ? self->m_base.m_min : 0.0; }

double XCategoryAxis_max(const XCategoryAxis* self)
{ return self ? self->m_base.m_max : 0.0; }

void XCategoryAxis_setRangeIndex(XCategoryAxis* self, double min, double max)
{
    double lo;
    double hi;
    if (!self) return;
    lo = (min < 0.0) ? 0.0 : min;
    hi = (max > (double)self->m_count) ? (double)self->m_count : max;
    if (hi < lo) hi = lo;
    XAbstractAxis_setRange(&self->m_base, lo, hi);
}

/* ==================== 信号 ==================== */

void* XCategoryAxis_categoriesChanged_signal(XCategoryAxis* self)
{
    xcat_emitVoid(self, (size_t)XCategoryAxis_categoriesChanged_signal);
    return (void*)(size_t)XCategoryAxis_categoriesChanged_signal;
}

void* XCategoryAxis_countChanged_signal(XCategoryAxis* self, int count)
{
    xcat_emitCount(self, count);
    return (void*)(size_t)XCategoryAxis_countChanged_signal;
}

void* XCategoryAxis_minChanged_signal(XCategoryAxis* self, const char* min)
{
    xcat_emitStr(self, (size_t)XCategoryAxis_minChanged_signal, min);
    return (void*)(size_t)XCategoryAxis_minChanged_signal;
}

void* XCategoryAxis_maxChanged_signal(XCategoryAxis* self, const char* max)
{
    xcat_emitStr(self, (size_t)XCategoryAxis_maxChanged_signal, max);
    return (void*)(size_t)XCategoryAxis_maxChanged_signal;
}

void* XCategoryAxis_rangeChanged_signal(XCategoryAxis* self,
                                        const char* min, const char* max)
{
    xcat_emitRangeStr(self, (size_t)XCategoryAxis_rangeChanged_signal,
                      min, max);
    return (void*)(size_t)XCategoryAxis_rangeChanged_signal;
}

/* ==================== UTF-8 兼容别名（_2，转发 _utf8 版本） ==================== */

int XCategoryAxis_append_2(XCategoryAxis* self, const char* label)
{ return XCategoryAxis_append_utf8(self, label); }

bool XCategoryAxis_remove_2(XCategoryAxis* self, const char* label)
{ return XCategoryAxis_remove_utf8(self, label); }

bool XCategoryAxis_insert_2(XCategoryAxis* self, int index, const char* label)
{ return XCategoryAxis_insert_utf8(self, index, label); }

bool XCategoryAxis_replace_2(XCategoryAxis* self, const char* oldLabel,
                             const char* newLabel)
{ return XCategoryAxis_replace_utf8(self, oldLabel, newLabel); }

void XCategoryAxis_setMinCategory_2(XCategoryAxis* self, const char* minCategory)
{ XCategoryAxis_setMinCategory_utf8(self, minCategory); }

void XCategoryAxis_setMaxCategory_2(XCategoryAxis* self, const char* maxCategory)
{ XCategoryAxis_setMaxCategory_utf8(self, maxCategory); }

void XCategoryAxis_setRangeCategory_2(XCategoryAxis* self,
                                      const char* minCategory,
                                      const char* maxCategory)
{ XCategoryAxis_setRangeCategory_utf8(self, minCategory, maxCategory); }

/* ==================== 借用 getter 兼容别名（无 _const，转发 _const 版） ==================== */

const XString* XCategoryAxis_at(const XCategoryAxis* self, int index)
{ return XCategoryAxis_at_const(self, index); }

const XString* XCategoryAxis_minCategory(const XCategoryAxis* self)
{ return XCategoryAxis_minCategory_const(self); }

const XString* XCategoryAxis_maxCategory(const XCategoryAxis* self)
{ return XCategoryAxis_maxCategory_const(self); }

const XString* XCategoryAxis_categories(const XCategoryAxis* self, int index)
{ return XCategoryAxis_categories_const(self, index); }

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
