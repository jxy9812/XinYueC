/**
 * @file       XSplitter.c
 * @brief      分割器控件实现（对标 Qt 6.8 QSplitter 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 *             [部件模型 2026-10-18 第二阶段] m_pages 权威序表（页序与
 *             children 解耦）；把手经 EXSplitter_CreateHandle 工厂创建、
 *             m_index 槽位单点同步；页增删走 childEvent（CHILD_ADDED
 *             自动收录/CHILD_REMOVED 摘槽，m_blockChildAdd 护栏覆盖
 *             insertWidget/replaceWidget/setRubberBand）；farMin/farMax
 *             折叠迟滞（Threshold=40）+ m_collapsed 位；restoreState
 *             按实际页数钳位消费。
 * @author     XinYueC 团队
 */

#include "XSplitter.h"
#include "XSplitterHandle.h"
#include "XRubberBand.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPrintf.h" /* 越界/护栏告警（对标 qWarning）。 */
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XObject.h"
#include "XVariant.h"
#include "XString.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XFRAME_ON && XSPLITTER_ON

/* ==================== 页表与内部工具 ==================== */

#define XSPLITTER_MIN_SIZE 0

/* [Qt 对齐] saveState/restoreState 文本头长度：
   "XSP"(3) + 方向(3) + 页数(3) + 把手宽(3) + 子页可折叠(1) = 13，
   其后每页 5 字节尺寸（不扩格式：collapsed 不落，restore 按位重建）。 */
#define XSPLITTER_STATE_HEADER 13

/* [迟滞] adjustPos 吸附阈值（对标 qsplitter.cpp:632 const int Threshold=40）。 */
#define XSPLITTER_ADJUST_THRESHOLD 40

static int xsp_childCount(const XSplitter* self)
{
    if (!self) return 0;
    return self->m_pagesCount;
}

static XWidget* xsp_childAt(const XSplitter* self, int index)
{
    if (!self || index < 0 || index >= self->m_pagesCount) return NULL;
    return self->m_pages ? self->m_pages[index] : NULL;
}

/** @brief 页在权威序表中的位置（indexOf/事件收录/替换共用）。 */
static int xsp_pageIndexOf(const XSplitter* self, const XWidget* widget)
{
    int i;
    if (!self || !widget) return -1;
    for (i = 0; i < self->m_pagesCount; ++i) {
        if (self->m_pages && self->m_pages[i] == widget) return i;
    }
    return -1;
}

/** @brief 现役把手数（=页数-1，页数 0 时 0；由页数推导防双真源漂移）。 */
static int xsp_handleCountOf(const XSplitter* self)
{
    if (!self || self->m_pagesCount <= 0) return 0;
    return self->m_pagesCount - 1;
}

/** @brief 折叠位查询（数组未就绪/越界=展开）。 */
static bool xsp_isCollapsed(const XSplitter* self, int index)
{
    if (!self || !self->m_collapsed || index < 0 ||
        index >= self->m_collapsedCap)
        return false;
    return self->m_collapsed[index] != 0;
}

static int xsp_horiz(const XSplitter* self)
{
    return self->m_orientation != 2; /* 非 Vertical 即水平 */
}

/** @brief 有效把手宽度（存储值 >=0 直接用；-1 哨兵回落样式
 *  PM_SplitterWidth，qsplitter.cpp:1614-1629）。 */
static int xsp_handleWidthEff(const XSplitter* self)
{
    int w;
    if (!self) return 0;
    w = self->m_handleWidth;
    if (w >= 0) return w;
#if XSTYLE_ON
    {
        XStyle* style = XStyle_defaultStyle();
        if (style) {
            w = XStyle_pixelMetric(style, XStylePM_SplitterWidth, NULL);
            return w < 0 ? 0 : w;
        }
    }
#endif /* XSTYLE_ON */
    return 0;
}

/** @brief 槽位 index 之后是否保留一条把手条带（布局/把手可见性/
 *  contentLen 的唯一定义处，三处必须同口径）。
 *  @details [防死态设计 2026-10-18，⑥] 规则：**相邻两槽位之间恒有一条
 *           把手条带，仅当两侧同时为隐藏页时不留**（Qt :376-380 对隐藏
 *           页整条隐藏把手——第一个可见页的把手同样隐藏，收起一页后不
 *           留任何把手）。部件模型下本可照 Qt，但保留条带使「收起态
 *           可点、可拖、可展开」防死态设计延续（冒烟实证：照 Qt 隐藏后
 *           收起页无任何入口、点边缘 0/10 无法展开）；折叠页（collapsed
 *           位，非隐藏）按可见参与计数——其条带恒保留，收起入口在部件
 *           模型下重新落地。
 *  @note    隐藏页位于**末位槽**时条带由前一槽承接（落在控件内侧），
 *           否则可见页铺满全轴把条带顶出控件（右/下停靠收起死态）。 */
static bool xsp_bandAfter(const XSplitter* self, int index)
{
    XWidget* a;
    XWidget* b;
    if (!self || index < 0) return false;
    if (index + 1 >= xsp_childCount(self)) return false; /* 末槽之后无条带 */
    a = xsp_childAt(self, index);
    b = xsp_childAt(self, index + 1);
    if (!a || !b) return false;
    if (XWidget_isHidden(a) && XWidget_isHidden(b)) return false;
    return true;
}

/** @brief 预留把手条带总宽（轴向可分给页的长度 = 轴长 - 该值）。 */
static int xsp_reservedBandLen(const XSplitter* self)
{
    int count = xsp_childCount(self);
    int bands = 0;
    int i;
    for (i = 0; i < count; ++i) {
        if (xsp_bandAfter(self, i)) ++bands;
    }
    return bands * xsp_handleWidthEff(self);
}

static int xsp_contentLen(const XSplitter* self)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int len = xsp_horiz(self) ? w : h;
    int reserved;
    if (!self) return 0;
    /* 隐藏页判据=isHidden（控件自身显式隐藏位）；折叠页非隐藏、其条带
       照计（见 xsp_bandAfter）。 */
    reserved = xsp_reservedBandLen(self);
    len -= reserved;
    if (len < 0) len = 0;
    return len;
}

/* ---- 页尺寸意图（对标 Qt QSplitterLayoutStruct::sizer）----
 * 意图 0=未设定或折叠；拖动/setSizes 落位后写入实际尺寸。容器缩放按
 * 已设定页的意图比例重排（doResize），全零=首次布局均分。 */

static int xsp_sizesAt(const XSplitter* self, int index)
{
    if (!self || !self->m_sizes || index < 0 || index >= self->m_sizesCap)
        return 0;
    return self->m_sizes[index];
}

static void xsp_setSizesAt(XSplitter* self, int index, int value)
{
    if (!self || index < 0 || value < 0) return;
    if (index >= self->m_sizesCap) {
        int newCap = index + 8;
        int* p = (int*)XRealloc_System(self->m_sizes,
                                       (size_t)newCap * sizeof(int));
        int i;
        if (!p) return;
        for (i = self->m_sizesCap; i < newCap; ++i) p[i] = 0;
        self->m_sizes = p;
        self->m_sizesCap = newCap;
    }
    self->m_sizes[index] = value;
}

static void xsp_setStretchAt(XSplitter* self, int index, int value)
{
    if (!self || index < 0 || value < 0) return;
    if (index >= self->m_stretchCap) {
        int newCap = index + 8;
        int* p = (int*)XRealloc_System(self->m_stretch,
                                       (size_t)newCap * sizeof(int));
        int i;
        if (!p) return;
        for (i = self->m_stretchCap; i < newCap; ++i) p[i] = 0;
        self->m_stretch = p;
        self->m_stretchCap = newCap;
    }
    self->m_stretch[index] = value;
}

static int xsp_stretchAt(const XSplitter* self, int index)
{
    if (!self || !self->m_stretch || index < 0 || index >= self->m_stretchCap)
        return 0;
    return self->m_stretch[index];
}

/** @brief 折叠位数组扩容（补 0=展开哨兵）。 */
static void xsp_ensureCollapsedCap(XSplitter* self, int pages)
{
    if (pages <= self->m_collapsedCap) return;
    {
        int newCap = pages + 8;
        int8_t* p = (int8_t*)XRealloc_System(self->m_collapsed,
                                             (size_t)newCap * sizeof(int8_t));
        int i;
        if (!p) return;
        for (i = self->m_collapsedCap; i < newCap; ++i) p[i] = 0;
        self->m_collapsed = p;
        self->m_collapsedCap = newCap;
    }
}

static void xsp_ensureCollapsibleCap(XSplitter* self, int pages)
{
    if (pages <= self->m_collapsibleCap) return;
    {
        int newCap = pages + 8;
        int* p = (int*)XRealloc_System(self->m_collapsible,
                                       (size_t)newCap * sizeof(int));
        int i;
        if (!p) return;
        /* 新槽位补 -1（未覆写哨兵）：isCollapsible 以 <0 回退全局开关。 */
        for (i = self->m_collapsibleCap; i < newCap; ++i) p[i] = -1;
        self->m_collapsible = p;
        self->m_collapsibleCap = newCap;
    }
}

/* ==================== 布局 ==================== */

static void xsp_pageSizeRect(const XSplitter* self, int x, int y, int size,
                             XRect* r)
{
    if (xsp_horiz(self))
        XRect_init(r, x, 0, size, XWidget_height((XWidget*)self));
    else
        XRect_init(r, 0, y, XWidget_width((XWidget*)self), size);
}

/** @brief 把手定位（布局尾步与 moveSplitter 落位共用）：把手 i 几何=
 *  页 i 几何之后的条带（游标与页循环同口径推进），可见性随
 *  xsp_bandAfter（相邻两页全隐藏→隐藏；折叠页照留——防死态）。 */
static void xsp_placeHandles(XSplitter* self)
{
    int count = xsp_childCount(self);
    int eff = xsp_handleWidthEff(self);
    int hx = 0;
    int hy = 0;
    int i;
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        XWidget* hw;
        XRect hr;
        bool band = xsp_bandAfter(self, i);
        if (i >= xsp_handleCountOf(self) || !self->m_handles) continue;
        hw = (XWidget*)self->m_handles[i];
        if (!hw) continue;
        if (child && !XWidget_isHidden(child)) {
            if (xsp_horiz(self)) hx += XWidget_width(child);
            else hy += XWidget_height(child);
        }
        if (xsp_horiz(self))
            XRect_init(&hr, hx, 0, eff, XWidget_height((XWidget*)self));
        else
            XRect_init(&hr, 0, hy, XWidget_width((XWidget*)self), eff);
        if (XWidget_x(hw) != hr.x || XWidget_y(hw) != hr.y ||
            XWidget_width(hw) != hr.width ||
            XWidget_height(hw) != hr.height)
            XWidget_update(hw);
        XWidget_setGeometryRect(hw, &hr);
        if (band) {
            if (XWidget_isHidden(hw)) XWidget_setVisible(hw, true);
            if (xsp_horiz(self)) hx += eff;
            else hy += eff;
        } else {
            if (!XWidget_isHidden(hw)) XWidget_setVisible(hw, false);
        }
    }
}

/** @brief 布局四分支（对标 doResize + 折叠语义）：
 *  ① isHidden→零几何+意图保留+不计 unset；② collapsed→零几何+条带
 *  保留+不计 unset（与①互斥于③④——收起页不复活钉子）；③ sizer>0→
 *  按权重；④ 未设定→均分/stretch 池。末位可见页吸收余量。 */
static void xsp_layout(XSplitter* self)
{
    int count = xsp_childCount(self);
    int i;
    int len;
    int used = 0;
    int perPage;
    int x = 0;
    int y = 0;
    int curTotal = 0;
    int unset = 0;
    int weightLen;
    int stretchSum = 0;
    int visible = 0;
    int lastVisible = -1;
    bool geoChanged = false;
    if (!self || count <= 0) return;
    len = xsp_contentLen(self);
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        if (!child || XWidget_isHidden(child) || xsp_isCollapsed(self, i))
            continue; /* ①②均不入池 */
        ++visible;
        lastVisible = i;
        if (xsp_sizesAt(self, i) > 0) curTotal += xsp_sizesAt(self, i);
        else ++unset;
    }
    perPage = visible > 0 ? len / visible : 0;
    weightLen = len - unset * perPage;
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        if (!child || XWidget_isHidden(child) || xsp_isCollapsed(self, i))
            continue;
        if (xsp_sizesAt(self, i) <= 0 && xsp_stretchAt(self, i) > 0)
            stretchSum += xsp_stretchAt(self, i);
    }
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        XRect r;
        int sizer;
        int size;
        int band;
        if (!child) continue;
        band = xsp_bandAfter(self, i) ? xsp_handleWidthEff(self) : 0;
        if (XWidget_isHidden(child)) {
            /* ① 隐藏页：零几何（意图保留），右缘=当前游标；条带按
               xsp_bandAfter 保留（防死态，见该函数注）。 */
            xsp_pageSizeRect(self, x, y, 0, &r);
            if (XWidget_x(child) != r.x || XWidget_y(child) != r.y ||
                XWidget_width(child) != r.width ||
                XWidget_height(child) != r.height)
                geoChanged = true;
            XWidget_setGeometryRect(child, &r);
            if (xsp_horiz(self)) x += band;
            else y += band;
            continue;
        }
        if (xsp_isCollapsed(self, i)) {
            /* ② 折叠页：零几何+条带保留+不入池（hysteresis#6；非隐藏
               故 bandAfter 照留——收起入口在部件模型下重建）。 */
            xsp_pageSizeRect(self, x, y, 0, &r);
            if (XWidget_x(child) != r.x || XWidget_y(child) != r.y ||
                XWidget_width(child) != r.width ||
                XWidget_height(child) != r.height)
                geoChanged = true;
            XWidget_setGeometryRect(child, &r);
            if (xsp_horiz(self)) x += band;
            else y += band;
            continue;
        }
        sizer = xsp_sizesAt(self, i);
        if (sizer > 0) {
            size = curTotal > 0
                ? (int)(((long long)weightLen * sizer) / curTotal)
                       : 0;
        } else if (stretchSum > 0) {
            int sf = xsp_stretchAt(self, i);
            size = (sf > 0)
                ? (int)(((long long)perPage * unset * sf) / stretchSum)
                : 0;
        } else {
            size = perPage;
        }
        if (i == lastVisible) size = len - used;   /* 末位可见页吸收余量。 */
        if (size < 0) size = 0;
        xsp_pageSizeRect(self, x, y, size, &r);
        if (XWidget_x(child) != r.x || XWidget_y(child) != r.y ||
            XWidget_width(child) != r.width ||
            XWidget_height(child) != r.height) {
            geoChanged = true;
            XWidget_update(child);
        }
        XWidget_setGeometryRect(child, &r);
        used += size;
        if (xsp_horiz(self)) x += size + band;
        else y += size + band;
    }
    xsp_placeHandles(self);
    if (geoChanged) XWidget_update((XWidget*)self);
}

static void xsp_emitMoved(XSplitter* self, int pos, int index)
{
    XVarList* args = XVarList_Create(XVar(int, pos), XVar(int, index));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XSplitter_splitterMoved_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 拖动区间（近/远）与迟滞 ==================== */

/** @brief 单页在拖动方向上的 smartMin（max(minimumSize, minimumSizeHint)
 *  主轴分量）——setSizes 钳位与拖拽钳位共用的唯一口径（防闪烁）。 */
static int xsp_smartMin(const XSplitter* self, int index)
{
    XWidget* child = xsp_childAt(self, index);
    XSize ms;
    XSize mh;
    int minz;
    if (!child) return 0;
    ms = XWidget_minimumSize(child);
    mh = XWidget_minimumSizeHint(child);
    minz = xsp_horiz(self)
               ? (ms.width > mh.width ? ms.width : mh.width)
               : (ms.height > mh.height ? ms.height : mh.height);
    return minz > 0 ? minz : 0;
}

static int xsp_pageSize(const XSplitter* self, int index)
{
    XWidget* child = xsp_childAt(self, index);
    if (!child) return 0;
    return xsp_horiz(self) ? XWidget_width(child) : XWidget_height(child);
}

/** @brief 分隔点 b 的近/远区间（+hw·B_left 把手贡献修正）。
 *  nearMin'(b)=Σ_{i≤b,可见}smartMin(i)+hw·B_left(b)；
 *  nearMax'(b)=contentLen−Σ_{i>b,可见}smartMin(i)+hw·B_left(b)；
 *  farMin=nearMin−近邻首个可见可折叠页 smartMin；
 *  farMax=nearMax+近邻首个可见可折叠页 smartMin（qsplitter.cpp:545-571/
 *  :614-618；B_left=分隔点前条带数——Qt addContribution 把手宽计入
 *  min/max，XGui 现坐标（条带原点）漏计中间条带，本修正是其落地）。 */
static void xsp_rangeEx(const XSplitter* self, int b, int* nearMin,
                        int* nearMax, int* farMin, int* farMax)
{
    int count = xsp_childCount(self);
    int hw = xsp_handleWidthEff(self);
    int total = xsp_contentLen(self);
    int bLeft = 0;
    int mn = 0;
    int mx;
    int cB = 0;
    int cA = 0;
    int i;
    for (i = 0; i < b; ++i) {
        if (xsp_bandAfter(self, i)) ++bLeft;
    }
    for (i = 0; i <= b; ++i) {
        XWidget* ch = xsp_childAt(self, i);
        if (!ch || XWidget_isHidden(ch)) continue;
        mn += xsp_smartMin(self, i);
    }
    mx = total;
    for (i = b + 1; i < count; ++i) {
        XWidget* ch = xsp_childAt(self, i);
        if (!ch || XWidget_isHidden(ch)) continue;
        mx -= xsp_smartMin(self, i);
    }
    /* 交叉钳位（qsplitter.cpp:607-611）：区间不反向。 */
    if (mx < mn) {
        int over = mn - mx;
        for (i = b; i >= 0 && over > 0; --i) {
            XWidget* ch = xsp_childAt(self, i);
            int contrib;
            if (!ch || XWidget_isHidden(ch)) continue;
            contrib = xsp_smartMin(self, i);
            if (contrib > over) contrib = over;
            mn -= contrib;
            over -= contrib;
        }
        if (mx < mn) mx = mn;
    }
    mn += hw * bLeft;
    mx += hw * bLeft;
    if (mn < 0) mn = 0;
    if (mx < mn) mx = mn;
    /* 近邻首个可见可折叠页的 smartMin（smartMin=0 邻页→far≡near）。 */
    for (i = b; i >= 0; --i) {
        XWidget* ch = xsp_childAt(self, i);
        if (!ch || XWidget_isHidden(ch)) continue;
        if (XSplitter_isCollapsible(self, i)) cB = xsp_smartMin(self, i);
        break;
    }
    for (i = b + 1; i < count; ++i) {
        XWidget* ch = xsp_childAt(self, i);
        if (!ch || XWidget_isHidden(ch)) continue;
        if (XSplitter_isCollapsible(self, i)) cA = xsp_smartMin(self, i);
        break;
    }
    if (nearMin) *nearMin = mn;
    if (nearMax) *nearMax = mx;
    if (farMin) *farMin = mn - cB;
    if (farMax) *farMax = mx + cA;
}

/** @brief adjustPos（对标 qsplitter.cpp:630-659，Threshold=40）：近区间
 *  内原样；出近区间 delta>cSize/2 且 delta>=min(40,cSize)→吸附 far 端
 *  （折叠），否则弹回近端。 */
static int xsp_adjustPos(const XSplitter* self, int b, int pos)
{
    int nearMin, nearMax, farMin, farMax;
    xsp_rangeEx(self, b, &nearMin, &nearMax, &farMin, &farMax);
    if (pos >= nearMin && pos <= nearMax) return pos;
    if (pos < nearMin) {
        int delta = nearMin - pos;
        int cSize = nearMin - farMin;
        int threshold = cSize < XSPLITTER_ADJUST_THRESHOLD
                            ? cSize : XSPLITTER_ADJUST_THRESHOLD;
        if (cSize > 0 && delta > cSize / 2 && delta >= threshold)
            return farMin;
        return nearMin;
    }
    {
        int delta = pos - nearMax;
        int cSize = farMax - nearMax;
        int threshold = cSize < XSPLITTER_ADJUST_THRESHOLD
                            ? cSize : XSPLITTER_ADJUST_THRESHOLD;
        if (cSize > 0 && delta > cSize / 2 && delta >= threshold)
            return farMax;
        return nearMax;
    }
}

/** @brief 写回页状态：sizer=几何尺寸；尺寸 0 且可折叠→collapsed=1，
 *  尺寸>0→清 collapsed（拖 far 落 0/拖回复原，hysteresis#4/#5）。 */
static void xsp_storePageState(XSplitter* self, int index, int size)
{
    xsp_setSizesAt(self, index, size);
    xsp_ensureCollapsedCap(self, index + 1);
    if (!self->m_collapsed || index >= self->m_collapsedCap) return;
    if (size == 0)
        self->m_collapsed[index] =
            XSplitter_isCollapsible(self, index) ? 1 : 0;
    else
        self->m_collapsed[index] = 0;
}

/** @brief 落几何：页 b 吸收 move、页 b+1 让出（可负号双向），其后各页
 *  整体平移；随后 storeSizes+把手定位+splitterMoved（对标 doMove :757-791
 *  +setGeo+storeSizes :533-539；隐藏页自然跳传——其尺寸钳 0 不复活）。 */
static void xsp_applyMove(XSplitter* self, int b, int move, int posAdj)
{
    int count = xsp_childCount(self);
    XWidget* cur = xsp_childAt(self, b);
    XWidget* next = xsp_childAt(self, b + 1);
    int i;
    if (!cur || !next || move == 0) return;
    if (xsp_horiz(self)) {
        XRect r;
        int curW = XWidget_width(cur) + move;
        int nextX = XWidget_x(next) + move;
        int nextW = XWidget_width(next) - move;
        int trailing;
        if (curW < 0) curW = 0;
        if (nextW < 0) nextW = 0;
        XRect_init(&r, XWidget_x(cur), 0, curW, XWidget_height(cur));
        XWidget_setGeometryRect(cur, &r);
        XRect_init(&r, nextX, 0, nextW, XWidget_height(next));
        XWidget_setGeometryRect(next, &r);
        /* 后续页自 next 右缘重排（保持各自尺寸；对标 doMove 尾页处理
           ——next 右缘不变式使重排回原位，不产生缝隙/溢出）。 */
        trailing = nextX + nextW + xsp_handleWidthEff(self);
        for (i = b + 2; i < count; ++i) {
            XWidget* ch = xsp_childAt(self, i);
            if (!ch) continue;
            XRect_init(&r, trailing, 0, XWidget_width(ch),
                       XWidget_height(ch));
            XWidget_setGeometryRect(ch, &r);
            trailing += XWidget_width(ch) + xsp_handleWidthEff(self);
        }
    } else {
        XRect r;
        int curH = XWidget_height(cur) + move;
        int nextY = XWidget_y(next) + move;
        int nextH = XWidget_height(next) - move;
        int trailing;
        if (curH < 0) curH = 0;
        if (nextH < 0) nextH = 0;
        XRect_init(&r, 0, XWidget_y(cur), XWidget_width((XWidget*)self),
                   curH);
        XWidget_setGeometryRect(cur, &r);
        XRect_init(&r, 0, nextY, XWidget_width((XWidget*)self), nextH);
        XWidget_setGeometryRect(next, &r);
        trailing = nextY + nextH + xsp_handleWidthEff(self);
        for (i = b + 2; i < count; ++i) {
            XWidget* ch = xsp_childAt(self, i);
            if (!ch) continue;
            XRect_init(&r, 0, trailing, XWidget_width(ch),
                       XWidget_height(ch));
            XWidget_setGeometryRect(ch, &r);
            trailing += XWidget_height(ch) + xsp_handleWidthEff(self);
        }
    }
    /* storeSizes（qsplitter.cpp:533-539）：以现几何刷新全页意图+折叠位。 */
    for (i = 0; i < count; ++i)
        xsp_storePageState(self, i, xsp_pageSize(self, i));
    xsp_placeHandles(self);
    XWidget_update((XWidget*)self);
    xsp_emitMoved(self, posAdj, b);
}

void XSplitter_moveSplitter(XSplitter* self, int pos, int b)
{
    int count;
    int nearMin, nearMax, farMin, farMax;
    int oldPos, delta, posAdj;
    int curSize, nextSize, give;
    int collapseCur, collapseNext;
    XWidget* cur;
    XWidget* next;
    if (!self) return;
    count = xsp_childCount(self);
    if (b < 0 || b >= count - 1) return;
    cur = xsp_childAt(self, b);
    next = xsp_childAt(self, b + 1);
    if (!cur || !next) return;
    xsp_rangeEx(self, b, &nearMin, &nearMax, &farMin, &farMax);
    posAdj = xsp_adjustPos(self, b, pos);
    /* 旧把位 = 条带原点 = 页 b 右缘/下缘。 */
    oldPos = xsp_horiz(self)
        ? XWidget_x(cur) + XWidget_width(cur)
        : XWidget_y(cur) + XWidget_height(cur);
    delta = posAdj - oldPos;
    if (delta == 0) return;
    curSize = xsp_pageSize(self, b);
    nextSize = xsp_pageSize(self, b + 1);
    /* mayCollapse=collapsible && 越近端（吸附端）；隐藏页不参与折叠。 */
    collapseCur = (farMin < nearMin && posAdj <= farMin &&
                   !XWidget_isHidden(cur));
    collapseNext = (farMax > nearMax && posAdj >= farMax &&
                    !XWidget_isHidden(next));
    if (delta > 0) {
        /* 页 b 吸收、页 b+1 让出：折叠端可让到 0，否则钳 smartMin
           （隐藏页跳传：无钳——其几何恒 0）。 */
        int limit = collapseNext ? 0
                    : (XWidget_isHidden(next) ? 0 : xsp_smartMin(self, b + 1));
        give = nextSize - limit;
        if (give < 0) give = 0;
        if (give > delta) give = delta;
        if (give == 0) return;
        xsp_applyMove(self, b, give, posAdj);
    } else {
        int limit = collapseCur ? 0
                    : (XWidget_isHidden(cur) ? 0 : xsp_smartMin(self, b));
        give = curSize - limit;
        if (give < 0) give = 0;
        if (give > -delta) give = -delta;
        if (give == 0) return;
        xsp_applyMove(self, b, -give, posAdj);
    }
}

/** @brief 非透明拖动橡皮筋（对标 QSplitterPrivate::setRubberBand，
 *  qsplitter.cpp:1301-1327）：pos<0 摘除（deleteLater），否则创建/复用
 *  XRubberBand(Line) 子件定位显示。rBord=3 的 3px 内缩在 XRubberBand
 *  自绘路径无对应物，登记偏差。 */
void xsp_setRubberBand(XSplitter* self, int pos)
{
#if XRUBBERBAND_ON
    XRubberBand* rb;
    if (!self) return;
    rb = (XRubberBand*)self->m_rubberBand;
    if (pos < 0) {
        if (rb) {
            self->m_rubberBand = NULL;
            XObject_deleteLater((XObject*)rb);
        }
        return;
    }
    if (!rb) {
        /* 橡皮筋不收录为页；保存-恢复外层护栏（嵌套安全）。 */
        int wasBlocked = self->m_blockChildAdd;
        self->m_blockChildAdd = 1;
        rb = XRubberBand_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   XRubberBandShape_Line, (XWidget*)self);
        self->m_blockChildAdd = wasBlocked;
        if (!rb) return;
        self->m_rubberBand = rb;
    }
    {
        XRect r;
        int hw = xsp_handleWidthEff(self);
        if (xsp_horiz(self))
            XRect_init(&r, pos, 0, hw, XWidget_height((XWidget*)self));
        else
            XRect_init(&r, 0, pos, XWidget_width((XWidget*)self), hw);
        XWidget_setGeometryRect((XWidget*)rb, &r);
        XWidget_setVisible((XWidget*)rb, true);
    }
#else
    (void)self;
    (void)pos;
#endif /* XRUBBERBAND_ON */
}

/* ==================== 事件处理 ==================== */

static void VX_splitter_resizeEvent(XWidget* self, XEvent* event)
{
    xsp_layout((XSplitter*)self);
}

/** @brief STYLE_CHANGE→updateHandles（=重排+各把手重绘，qsplitter.cpp:
 *  1634-1640/:670-674）；其余事件走基类。 */
static void xsp_updateHandles(XSplitter* self)
{
    int i;
    int handles;
    xsp_layout(self);
    handles = xsp_handleCountOf(self);
    for (i = 0; i < handles; ++i) {
        if (self->m_handles && self->m_handles[i])
            XWidget_update((XWidget*)self->m_handles[i]);
    }
}

static void VX_splitter_changeEvent(XWidget* self, XEvent* event)
{
    if (self && event && XEvent_type(event) == XEVENT_TYPE_STYLE_CHANGE) {
        xsp_updateHandles((XSplitter*)self);
        XEvent_accept(event);
        return;
    }
    XEvent_accept(event);
}

/** @brief 内部子件判别（把手/橡皮筋不收录为页——类虚表比对）。 */
static bool xsp_isInternalChild(const XObject* child)
{
    XVtable* vt;
    if (!child) return true;
    vt = XClassGetVtable((XObject*)child);
    if (vt == XSplitterHandle_class_init()) return true;
#if XRUBBERBAND_ON
    if (vt == XRubberBand_class_init()) return true;
#endif
    return false;
}

/** @brief EXObject_Event 重载（对标 QSplitter::event/childEvent 链）：
 *  CHILD_ADDED 自动收录（qsplitter.cpp:1263-1293）、CHILD_REMOVED 摘槽
 *  +deleteLater 把手、LAYOUT_REQUEST→重排（m_inLayout 防重入）、
 *  STYLE_CHANGE→updateHandles；CHILD_POLISHED 补 show 一并处理（框架
 *  现无发送方，防御性保留）。 */
static void xsp_insertSlot(XSplitter* self, int index, XWidget* widget);
static void xsp_removeSlot(XSplitter* self, int index);

static bool VX_splitter_event(XObject* self, XEvent* event)
{
    XSplitter* sp = (XSplitter*)self;
    if (sp && event) {
        int type = XEvent_type(event);
        if (type == XEVENT_TYPE_CHILD_ADDED ||
            type == XEVENT_TYPE_CHILD_REMOVED ||
            type == XEVENT_TYPE_CHILD_POLISHED) {
            XChildEvent* ce = (XChildEvent*)event;
            XObject* child = XChildEvent_child(ce);
            if (!sp->m_blockChildAdd && child && child->m_is_widget &&
                !xsp_isInternalChild(child)) {
                int idx = xsp_pageIndexOf(sp, (const XWidget*)child);
                if (type == XEVENT_TYPE_CHILD_ADDED) {
                    if (idx < 0) {
                        xsp_insertSlot(sp, sp->m_pagesCount,
                                       (XWidget*)child);
                        xsp_layout(sp);
                    }
                } else if (type == XEVENT_TYPE_CHILD_POLISHED) {
                    if (idx >= 0 && !XWidget_isHidden((XWidget*)child))
                        XWidget_setVisible((XWidget*)child, true);
                } else if (idx >= 0) {
                    xsp_removeSlot(sp, idx);
                    xsp_layout(sp);
                }
            }
            XEvent_accept(event);
            return true;
        }
        if (type == XEVENT_TYPE_LAYOUT_REQUEST) {
            if (!sp->m_inLayout) {
                sp->m_inLayout = 1;
                xsp_layout(sp);
                sp->m_inLayout = 0;
            }
            XEvent_accept(event);
            return true;
        }
        if (type == XEVENT_TYPE_STYLE_CHANGE) {
            xsp_updateHandles(sp);
            XEvent_accept(event);
            return true;
        }
    }
    /* [基类调用定式 2026-10-18] XObject_event_base 内部走虚分派
       （XClassGetVirtualFunc(self, EXObject_Event)），从重载内调用即
       无限自递归（首跑 gdb 实证栈溢出）——基类调用必须取基类虚表
       条目。**父级必须取 XWidget 而非 XObject**：EXObject_Event 在
       XWidget 虚表=VXWidget_event（按 XEventType 分派到 RESIZE/MOUSE/
       ENTER 等 25 个 widget 事件槽），挂到 XObject（VXObject_event）
       会整层跳过 XWidget 派发——RESIZE 永远到不了 EXWidget_ResizeEvent
       → xsp_layout 在 resize 后永不重跑 → 分割条子级恒卡旧几何
       （10-18 实证：最大化/90→0 后中段不跟随，SPPROBE 零命中）。
       VXWidget_event 为静态函数，经 XClass_Parent 调用无自递归。 */
    return XClass_Parent(XWidget, EXObject_Event,
                         bool (*)(XObject*, XEvent*))(self, event);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_splitter_deinit(XSplitter* self)
{
    if (!self) return;
    /* 级联摘子的 CHILD_REMOVED 不回写：先摘权威序表/把手表/折叠位。 */
    self->m_blockChildAdd = 1;
    self->m_rubberBand = NULL; /* 橡皮筋为子件随级联析构。 */
    if (self->m_collapsible) {
        XFree_System(self->m_collapsible);
        self->m_collapsible = NULL;
        self->m_collapsibleCap = 0;
    }
    if (self->m_sizes) {
        XFree_System(self->m_sizes);
        self->m_sizes = NULL;
        self->m_sizesCap = 0;
    }
    if (self->m_stretch) {
        XFree_System(self->m_stretch);
        self->m_stretch = NULL;
        self->m_stretchCap = 0;
    }
    if (self->m_collapsed) {
        XFree_System(self->m_collapsed);
        self->m_collapsed = NULL;
        self->m_collapsedCap = 0;
    }
    if (self->m_pages) {
        XFree_System(self->m_pages);
        self->m_pages = NULL;
        self->m_pagesCap = 0;
    }
    self->m_pagesCount = 0;
    if (self->m_handles) {
        XFree_System(self->m_handles);
        self->m_handles = NULL;
        self->m_handlesCap = 0;
    }
    XClass_Deinit_Parent(XFrame, (XFrame*)self);
}

/** @brief 把手工厂默认实现（EXSplitter_CreateHandle）：new XSplitterHandle
 *  并回写 m_index；子类可重载定制把手。 */
static XSplitterHandle* VX_splitter_createHandle(XSplitter* self, int index)
{
    XSplitterHandle* h = XSplitterHandle_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, (XOrientation)self->m_orientation,
        self, 0);
    if (h) h->m_index = (int16_t)index;
    return h;
}

XVtable* XSplitter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSplitter)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VX_splitter_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_splitter_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VX_splitter_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXSplitter_CreateHandle,
                             VX_splitter_createHandle);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_splitter_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 槽位单点同步（页/把手序表维护） ==================== */

/** @brief 页插入槽：m_pages 插入 + 折叠位扩容 + 新分隔点把手（贴近新页
 *  插入，后移槽回写 m_index——单点同步真源）。须在 m_blockChildAdd
 *  护栏内调用（把手创建的 setParent 触发 CHILD_ADDED 被吞）。 */
static void xsp_insertSlot(XSplitter* self, int index, XWidget* widget)
{
    int i;
    int at;
    int handles;
    if (!self || !widget || index < 0 || index > self->m_pagesCount) return;
    if (self->m_pagesCount >= self->m_pagesCap || !self->m_pages) {
        int newCap = self->m_pagesCount + 8;
        XWidget** p = (XWidget**)XRealloc_System(
            self->m_pages, (size_t)newCap * sizeof(XWidget*));
        if (!p) return;
        self->m_pages = p;
        self->m_pagesCap = newCap;
    }
    for (i = self->m_pagesCount; i > index; --i)
        self->m_pages[i] = self->m_pages[i - 1];
    self->m_pages[index] = widget;
    self->m_pagesCount++;
    xsp_ensureCollapsedCap(self, self->m_pagesCount);
    handles = xsp_handleCountOf(self); /* 插页后把手数 */
    if (handles <= 0) return;
    if (handles > self->m_handlesCap || !self->m_handles) {
        int newCap = handles + 4;
        XSplitterHandle** p = (XSplitterHandle**)XRealloc_System(
            self->m_handles, (size_t)newCap * sizeof(XSplitterHandle*));
        if (!p) return;
        self->m_handles = p;
        self->m_handlesCap = newCap;
    }
    at = index < handles ? index : handles - 1;
    for (i = handles - 1; i > at; --i) {
        self->m_handles[i] = self->m_handles[i - 1];
        if (self->m_handles[i]) self->m_handles[i]->m_index = (int16_t)i;
    }
    {
        /* 把手 setParent 的 CHILD_ADDED 被吞；保存-恢复外层护栏
           （insertWidget 等调用方可能已置位，嵌套不得提前解除）。 */
        int wasBlocked = self->m_blockChildAdd;
        XSplitterHandle* h = NULL;
        self->m_blockChildAdd = 1;
        if (!XClassIsVtableNull(self)) {
            EXSplitter_CreateHandle_t fn = XClassGetVirtualFunc(
                self, EXSplitter_CreateHandle, EXSplitter_CreateHandle_t);
            if (fn) h = fn(self, at);
        }
        if (!h) {
            h = XSplitterHandle_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                          (XOrientation)self->m_orientation,
                                          self, 0);
            if (h) h->m_index = (int16_t)at;
        }
        self->m_blockChildAdd = wasBlocked;
        self->m_handles[at] = h;
    }
}

/** @brief 页摘除槽：m_pages 摘除 + 分隔点把手摘除（deleteLater，定式
 *  XTableWidget.c:206）+ 后移槽回写 m_index。 */
static void xsp_removeSlot(XSplitter* self, int index)
{
    int i;
    int oldHandles;
    int newHandles;
    int removeAt;
    XSplitterHandle* h;
    if (!self || index < 0 || index >= self->m_pagesCount) return;
    oldHandles = self->m_pagesCount > 0 ? self->m_pagesCount - 1 : 0;
    for (i = index; i < self->m_pagesCount - 1; ++i)
        self->m_pages[i] = self->m_pages[i + 1];
    self->m_pagesCount--;
    newHandles = xsp_handleCountOf(self);
    if (oldHandles <= newHandles) return; /* 无把手可摘（防御） */
    if (!self->m_handles) return;
    removeAt = index < oldHandles - 1 ? index : oldHandles - 1;
    h = self->m_handles[removeAt];
    for (i = removeAt; i < newHandles; ++i) {
        self->m_handles[i] = self->m_handles[i + 1];
        if (self->m_handles[i]) self->m_handles[i]->m_index = (int16_t)i;
    }
    if (h) {
        XWidget_setParent((XWidget*)h, NULL, 0);
        XObject_deleteLater((XObject*)h);
    }
}

/** @brief 页移位槽（换边/换序）：仅平移 m_pages——把手数与分隔点号
 *  不变，把手与邻页关系随页序自然更新（修 XVector_move 不平移并行
 *  数组的错位隐患：权威序表自身即真源）。 */
static void xsp_moveSlot(XSplitter* self, int cur, int index)
{
    XWidget* w;
    int i;
    if (!self || cur < 0 || cur >= self->m_pagesCount || index < 0 ||
        index >= self->m_pagesCount || cur == index)
        return;
    w = self->m_pages[cur];
    if (cur < index) {
        for (i = cur; i < index; ++i) self->m_pages[i] = self->m_pages[i + 1];
    } else {
        for (i = cur; i > index; --i) self->m_pages[i] = self->m_pages[i - 1];
    }
    self->m_pages[index] = w;
}

/* 槽位函数的前置声明见事件处理节（VX_splitter_event 先用后定义）。 */

/* ==================== 生命周期 ==================== */

void XSplitter_init(XSplitter* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XSplitter);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_orientation = 1;
    /* 把手宽保持显式 5（默认样式 PM_SplitterWidth 非 5：Fusion=dpiScaled4/
       XCommonStyle=0，照 Qt 哨兵默认会丢把手宽）；不透明拖动存 true 但
       不置已设置位——getter 走 SH_Splitter_OpaqueResize 样式回落。 */
    self->m_handleWidth = 5;
    self->m_childrenCollapsible = true;
    self->m_opaqueResize = true;
    self->m_opaqueResizeSet = 0;
    /* [Qt 对齐 2026-10-18] 缺省 sizePolicy=双向 Expanding（对标 QSplitter
       构造 setSizePolicy(Expanding, Expanding)）：引入 sizeHint/
       minimumSizeHint 后若保持缺省 Preferred，盒式布局按 hint 落尺寸、
       余量留白——放大/最大化后中段不跟随（品牌/状态跟到满宽、分割条
       与页停在旧尺寸，10-18 真机实证）。 */
    XWidget_setSizePolicy((XWidget*)self,
                          XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);
}

void XSplitter_init_2(XSplitter* self, XOrientation orientation,
                      XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XSplitter_init(self, parent, flags);
    self->m_orientation = (uint8_t)(int)orientation;
}

XSplitter* XSplitter_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XSplitter* self = (XSplitter*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSplitter_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XSplitter* XSplitter_create_ex_2(XMemoryType memory, XOrientation orientation,
                                 XWidget* parent, XWidgetFlags flags)
{
    XSplitter* self = (XSplitter*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSplitter_init_2(self, orientation, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 页面管理 ==================== */

void XSplitter_addWidget(XSplitter* self, XWidget* widget)
{
    if (!self || !widget) return;
    /* addWidget=insertWidget(count)（qsplitter.cpp:1079-1081 同式）。 */
    XSplitter_insertWidget(self, xsp_childCount(self), widget);
}

void XSplitter_insertWidget(XSplitter* self, int index, XWidget* widget)
{
    int cur;
    if (!self || !widget) return;
    /* [护栏] insertWidget_helper（Qt :809）：childEvent 增删被吞，槽位
       操作在护栏内单点完成。 */
    self->m_blockChildAdd = 1;
    XWidget_setParent(widget, (XWidget*)self, 0);
    /* [Qt 对齐 show 语义] shouldShowWidget（qsplitter.cpp:709-713,810）：
       仅当页未被显式隐藏才 show。 */
    if (!XWidget_isHidden(widget))
        XWidget_setVisible(widget, true);
    cur = xsp_pageIndexOf(self, widget);
    if (cur < 0) {
        xsp_insertSlot(self, self->m_pagesCount, widget); /* 追加尾 */
        cur = self->m_pagesCount - 1;
    }
    if (index < 0 || index > self->m_pagesCount - 1)
        index = self->m_pagesCount - 1;
    if (cur != index)
        xsp_moveSlot(self, cur, index);
    self->m_blockChildAdd = 0;
    xsp_layout(self);
}

XWidget* XSplitter_widget(const XSplitter* self, int index)
{
    return xsp_childAt(self, index);
}

int XSplitter_count(const XSplitter* self)
{
    return xsp_childCount(self);
}

int XSplitter_indexOf(const XSplitter* self, const XWidget* widget)
{
    int i;
    int handles;
    if (!self || !widget) return -1;
    for (i = 0; i < self->m_pagesCount; ++i) {
        if (self->m_pages && self->m_pages[i] == widget) return i;
    }
    /* [对齐] 扩把手匹配（qsplitter.cpp:1183-1186）：命中把手 i 返回
       分隔点 i。 */
    handles = xsp_handleCountOf(self);
    for (i = 0; i < handles; ++i) {
        if (self->m_handles && (const XWidget*)self->m_handles[i] == widget)
            return i;
    }
    return -1;
}

XWidget* XSplitter_replaceWidget(XSplitter* self, int index, XWidget* widget)
{
    XWidget* current;
    XRect geom;
    bool wasVisible;
    if (!self || !widget) return NULL;
    /* [Qt 对齐] 越界告警（qsplitter.cpp:1127-1149 qWarning 分支）。 */
    if (index < 0 || index >= xsp_childCount(self)) {
        XPrintf("[XSplitter] replaceWidget: index %d out of range\n", index);
        return NULL;
    }
    current = xsp_childAt(self, index);
    if (!current || current == widget) return NULL;
    /* Qt 护栏：新控件已是本分割器子页（兄弟）时不替换。 */
    if (xsp_pageIndexOf(self, widget) >= 0) {
        XPrintf("[XSplitter] replaceWidget: widget is already a child\n");
        return NULL;
    }
    /* [护栏序 2026-10-18] 护栏→读 geom/wasHidden→先换 m_pages[i]→
       current 摘父（CHILD_REMOVED 到达时查无→把手存活）→新页挂父
       （CHILD_ADDED 被护栏吞）→解除→重排（qsplitter.cpp:1149）。 */
    self->m_blockChildAdd = 1;
    geom = XWidget_geometry(current);
    /* 以 isHidden（显式显隐位）继承可见性（headless 不随父链波动）。 */
    wasVisible = !XWidget_isHidden(current);
    self->m_pages[index] = widget;
    XWidget_setParent(current, NULL, 0);
    XWidget_setParent(widget, (XWidget*)self, 0);
    self->m_blockChildAdd = 0;
    /* 继承被替换控件的几何与可见状态（对标 Qt）。 */
    XWidget_setGeometryRect(widget, &geom);
    XWidget_setVisible(widget, wasVisible);
    xsp_layout(self);
    return current;
}

XSplitterHandle* XSplitter_handle(const XSplitter* self, int index)
{
    if (!self || index < 0) return NULL;
    if (index >= xsp_handleCountOf(self)) return NULL;
    if (!self->m_handles) return NULL;
    /* 把手为真实子部件：矩形经 XWidget_geometry(把手) 读回（=页 index
       几何之后的把手宽条带，布局直配，见 xsp_placeHandles）。 */
    return self->m_handles[index];
}

bool XSplitter_getRange(const XSplitter* self, int index, int* min, int* max)
{
    int count;
    int nearMin;
    int nearMax;
    if (!self) return false;
    count = xsp_childCount(self);
    if (index < 0 || count < 2 || index >= count - 1) return false;
    xsp_rangeEx(self, index, &nearMin, &nearMax, NULL, NULL);
    if (min) *min = nearMin;
    if (max) *max = nearMax;
    return true;
}

/* ==================== 属性 ==================== */

void XSplitter_setOrientation(XSplitter* self, XOrientation orientation)
{
    int orient = (int)orientation;
    int i;
    int handles;
    if (!self || self->m_orientation == orient) return;
    self->m_orientation = (uint8_t)orient;
    /* 遍历把手传播（qsplitter.cpp:987-990）：方向光标/自绘横竖随之切换。 */
    handles = xsp_handleCountOf(self);
    for (i = 0; i < handles; ++i) {
        if (self->m_handles && self->m_handles[i])
            XSplitterHandle_setOrientation(self->m_handles[i], orientation);
    }
    xsp_layout(self);
}

XOrientation XSplitter_orientation(const XSplitter* self)
{
    if (!self) return XOrientation_Horizontal;
    return (XOrientation)self->m_orientation;
}

void XSplitter_setChildrenCollapsible(XSplitter* self, bool collapsible)
{
    if (!self) return;
    self->m_childrenCollapsible = collapsible;
}

bool XSplitter_childrenCollapsible(const XSplitter* self)
{
    return self ? self->m_childrenCollapsible : false;
}

void XSplitter_setCollapsible(XSplitter* self, int index, bool collapsible)
{
    if (!self) return;
    /* [Qt 对齐] 越界告警+早退（qsplitter.cpp:1040-1043）。 */
    if (index < 0 || index >= xsp_childCount(self)) {
        XPrintf("[XSplitter] setCollapsible: index %d out of range\n",
                index);
        return;
    }
    xsp_ensureCollapsibleCap(self, index + 1);
    if (!self->m_collapsible || index >= self->m_collapsibleCap) return;
    self->m_collapsible[index] = collapsible ? 1 : 0;
}

bool XSplitter_isCollapsible(const XSplitter* self, int index)
{
    if (!self || index < 0) return false;
    if (!self->m_collapsible || index >= self->m_collapsibleCap)
        return self->m_childrenCollapsible;
    if (self->m_collapsible[index] < 0)
        return self->m_childrenCollapsible;
    return self->m_collapsible[index] != 0;
}

void XSplitter_setOpaqueResize(XSplitter* self, bool opaque)
{
    if (!self) return;
    self->m_opaqueResize = opaque;
    self->m_opaqueResizeSet = 1; /* 此后 getter 不再回落样式（:1472-1484）。 */
}

bool XSplitter_opaqueResize(const XSplitter* self)
{
    if (!self) return false;
    if (self->m_opaqueResizeSet) return self->m_opaqueResize != 0;
#if XSTYLE_ON
    {
        XStyle* style = XStyle_defaultStyle();
        if (style)
            return XStyle_styleHint(style, XStyleSH_Splitter_OpaqueResize,
                                    NULL, NULL) != 0;
    }
#endif /* XSTYLE_ON */
    return true;
}

int XSplitter_handleWidth(const XSplitter* self)
{
    if (!self) return 0;
    if (self->m_handleWidth >= 0) return self->m_handleWidth;
    return xsp_handleWidthEff(self);
}

void XSplitter_setHandleWidth(XSplitter* self, int width)
{
    if (!self || width < -1) return;
    self->m_handleWidth = (int16_t)width;
    xsp_layout(self);
}

void XSplitter_setStretchFactor(XSplitter* self, int index, int stretch)
{
    if (!self || stretch < 0) return;
    /* 越界索引早退（qsplitter.cpp:1754-1755）。 */
    if (index < 0 || index >= xsp_childCount(self)) return;
    xsp_setStretchAt(self, index, stretch);
    xsp_layout(self);
}

void XSplitter_refresh(XSplitter* self)
{
    xsp_layout(self);
}

/* ==================== 尺寸提示（qsplitter.cpp:1490-1541 逐式） ==================== */

XSize XSplitter_sizeHint(const XSplitter* self)
{
    XSize out;
    int len = 0;
    int trans = 0;
    int bands = 0;
    int count;
    int i;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    count = xsp_childCount(self);
    for (i = 0; i < count; ++i) {
        XWidget* ch = xsp_childAt(self, i);
        XSize sh;
        if (xsp_bandAfter(self, i)) ++bands;
        if (!ch || XWidget_isHidden(ch) || xsp_isCollapsed(self, i)) continue;
        sh = XWidget_sizeHint(ch);
        if (xsp_horiz(self)) {
            if (sh.width > 0) len += sh.width;
            if (sh.height > trans) trans = sh.height;
        } else {
            if (sh.height > 0) len += sh.height;
            if (sh.width > trans) trans = sh.width;
        }
    }
    len += bands * xsp_handleWidthEff(self);
    if (xsp_horiz(self)) XSize_init(&out, len, trans);
    else XSize_init(&out, trans, len);
    return out;
}

XSize XSplitter_minimumSizeHint(const XSplitter* self)
{
    XSize out;
    int len = 0;
    int trans = 0;
    int bands = 0;
    int count;
    int i;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    count = xsp_childCount(self);
    for (i = 0; i < count; ++i) {
        XWidget* ch = xsp_childAt(self, i);
        XSize mh;
        if (xsp_bandAfter(self, i)) ++bands;
        if (!ch || XWidget_isHidden(ch) || xsp_isCollapsed(self, i)) continue;
        mh = XWidget_minimumSizeHint(ch);
        len += xsp_smartMin(self, i);
        if (xsp_horiz(self)) {
            if (mh.height > trans) trans = mh.height;
        } else {
            if (mh.width > trans) trans = mh.width;
        }
    }
    len += bands * xsp_handleWidthEff(self);
    if (xsp_horiz(self)) XSize_init(&out, len, trans);
    else XSize_init(&out, trans, len);
    return out;
}

/* ==================== 尺寸与状态 ==================== */

void XSplitter_sizes(const XSplitter* self, int* outSizes, int count)
{
    int i;
    int n;
    if (!self || !outSizes || count <= 0) return;
    n = xsp_childCount(self);
    /* 出参契约（qsplitter.cpp:1560-1574）：超页数完全不写。 */
    if (count > n) return;
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        if (!child) continue;
        outSizes[i] = xsp_horiz(self) ? XWidget_width(child)
                                      : XWidget_height(child);
    }
}

void XSplitter_setSizes(XSplitter* self, const int* sizes, int count)
{
    int i;
    int n;
    int rawSum = 0;
    int clampedSum = 0;
    int rawIndexes[64];
    int clampedIndexes[64];
    int rawCount = 0;
    int clampedCount = 0;
    if (!self || !sizes || count <= 0) return;
    n = xsp_childCount(self);
    if (n > 64) n = 64;   /* 探针数组定长上限。 */
    /* sizer=像素意图（setSizes_helper，qsplitter.cpp:676-702）：负值钳 0、
       正值下限钳 smartMin；交 xsp_layout 按意图权重分配。 */
    for (i = 0; i < count && i < n; ++i) {
        XWidget* child = xsp_childAt(self, i);
        int s = sizes[i];
        int raw = s;
        int minz = 0;
        if (s < 0) s = 0;
        if (child && s > 0) {
            minz = xsp_smartMin(self, i);
            if (minz > 0 && s < minz) s = minz;
        }
        rawSum += raw;
        clampedSum += s;
        xsp_setSizesAt(self, i, s);
        if (minz > 0 && s != raw) clampedIndexes[clampedCount++] = i;
        else rawIndexes[rawCount++] = i;
    }
    /* [空间不足回落] 意图被顶起后总和超可用长度→按调用方原始意图比例
       落位（qlayoutengine.cpp:80-136 取向），常态路径钳位仍生效。 */
    if (clampedCount > 0 && rawSum > 0 && clampedSum > xsp_contentLen(self)) {
        for (i = 0; i < rawCount; ++i)
            xsp_setSizesAt(self, rawIndexes[i], sizes[rawIndexes[i]] > 0
                                               ? sizes[rawIndexes[i]] : 0);
        for (i = 0; i < clampedCount; ++i) {
            int idx = clampedIndexes[i];
            xsp_setSizesAt(self, idx, sizes[idx] > 0 ? sizes[idx] : 0);
        }
    }
    /* [折叠重建] setSizes_helper XGui 版（qsplitter.cpp:690-694 无「可见」
       门）：sizer==0→可折叠 collapsed=1 / 不可折叠钳 smartMin（smartMin=0
       落④未设定池，Qt 留 0，登记偏差）；sizer>0→清 collapsed。 */
    for (i = 0; i < count && i < n; ++i) {
        int s = xsp_sizesAt(self, i);
        xsp_ensureCollapsedCap(self, i + 1);
        if (!self->m_collapsed || i >= self->m_collapsedCap) continue;
        if (s == 0) {
            if (XSplitter_isCollapsible(self, i)) {
                self->m_collapsed[i] = 1;
            } else {
                int minz = xsp_smartMin(self, i);
                if (minz > 0) xsp_setSizesAt(self, i, minz);
            }
        } else {
            self->m_collapsed[i] = 0;
        }
    }
    xsp_layout(self);
}

#if XByteArray_ON
XByteArray* XSplitter_saveState(const XSplitter* self)
{
    XByteArray* out;
    int i;
    int count;
    char header[32];
    if (!self) return NULL;
    out = XByteArray_create();
    if (!out) return NULL;
    count = xsp_childCount(self);
    /* 快照字段（qsplitter.cpp:1642-1679）：方向/页数/把手宽/子页可折叠/
       逐页尺寸；不落 collapsed（Qt 同不落）与 opaqueResize 两字段（登记
       偏差）。把手宽写存储值：-1 哨兵经 %03d 落 "-01"。 */
    XSnprintf(header, sizeof(header), "XSP%03d%03d%03d%d",
              (int)self->m_orientation, count, (int)self->m_handleWidth,
              self->m_childrenCollapsible ? 1 : 0);
    XByteArray_append_utf8(out, header);
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        char buf[16];
        int size = 0;
        if (child)
            size = xsp_horiz(self) ? XWidget_width(child)
                                   : XWidget_height(child);
        XSnprintf(buf, sizeof(buf), "%05d", size);
        XByteArray_append_utf8(out, buf);
    }
    return out;
}

bool XSplitter_restoreState(XSplitter* self, const XByteArray* state)
{
    const char* data;
    int orientation;
    int savedCount;
    int handleWidth;
    int childrenCollapsible;
    int actual;
    int i;
    char buf[8];
    if (!self || !state) return false;
    /* [告警消解] XByteArray.h:455 constData 为 data 的非 const 别名宏，
       调用点显式转型，容器头不动。 */
    data = (const char*)XByteArray_data((XByteArray*)state);
    if (!data || XStrncmp(data, "XSP", 3) != 0) return false;
    /* 长度校验（短包不越界读）。 */
    if (XByteArray_size_base((const XContainer*)state) <
        (size_t)XSPLITTER_STATE_HEADER)
        return false;
    orientation = (data[3] - '0') * 100 + (data[4] - '0') * 10 +
                  (data[5] - '0');
    savedCount = (data[6] - '0') * 100 + (data[7] - '0') * 10 +
                 (data[8] - '0');
    /* 把手宽 3 字符有符号解析（-1 哨兵 "-01" 往返）。 */
    if (data[9] == '-')
        handleWidth = -((data[10] - '0') * 10 + (data[11] - '0'));
    else
        handleWidth = (data[9] - '0') * 100 + (data[10] - '0') * 10 +
                      (data[11] - '0');
    childrenCollapsible = data[12] - '0';
    if (orientation != 1 && orientation != 2) return false;
    if (handleWidth < -1) handleWidth = 0;
    if (childrenCollapsible != 0 && childrenCollapsible != 1)
        return false;
    if ((size_t)XSPLITTER_STATE_HEADER + (size_t)savedCount * 5 >
        XByteArray_size_base((const XContainer*)state))
        return false;
    /* [Qt 钳位语义 2026-10-18] 按实际页数钳位消费（qsplitter.cpp:
       1712-1732）：逐实际页 sizer=(k<保存页数?保存值:0)（value() 默认
       0）→经 setSizes 的折叠重建分支落位；保存页多于实际页的尾部忽略。
       旧「页数不匹配即失败」契约已随本修订翻转（回归 :30627 同步）。 */
    self->m_orientation = (uint8_t)orientation;
    self->m_childrenCollapsible = childrenCollapsible ? true : false;
    actual = xsp_childCount(self);
    if (actual > 0) {
        int* sizes = (int*)XMalloc_System((size_t)actual * sizeof(int));
        if (!sizes) return false;
        for (i = 0; i < actual; ++i) {
            if (i < savedCount) {
                int32_t v = 0;
                XMemcpy(buf, data + XSPLITTER_STATE_HEADER + i * 5, 5);
                buf[5] = '\0';
                sizes[i] = (str_to_int32(buf, &v) == CONV_OK) ? (int)v : 0;
            } else {
                sizes[i] = 0;
            }
        }
        XSplitter_setSizes(self, sizes, actual);
        XFree_System(sizes);
    }
    /* 把手宽随快照恢复（置于 setSizes 之后，避免提前重排被覆盖）；
       -1 哨兵原样写入（getter 届时回落样式）。 */
    XSplitter_setHandleWidth(self, handleWidth);
    return true;
}
#endif /* XByteArray_ON */

/* ==================== 信号 ==================== */

void* XSplitter_splitterMoved_signal(XSplitter* self, int pos, int index)
{
    (void)self;
    (void)pos;
    (void)index;
    return (void*)(size_t)XSplitter_splitterMoved_signal;
}

void* XSplitter_handleClicked_signal(XSplitter* self, int index,
                                     int doubleClick)
{
    (void)self;
    (void)index;
    (void)doubleClick;
    return (void*)(size_t)XSplitter_handleClicked_signal;
}

#endif /* XWIDGET_ON && XFRAME_ON && XSPLITTER_ON */
