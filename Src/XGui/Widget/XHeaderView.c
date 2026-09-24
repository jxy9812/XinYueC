/**
 * @file       XHeaderView.c
 * @brief      XHeaderView 表头视图实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XHeaderView.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XEventType.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/** @brief 区间尺寸硬上限（对标 Qt qheaderview.cpp 的 maxSizeSection=1048575）。 */
#define XHEADERVIEW_SECTION_SIZE_LIMIT 1048575
/** @brief 最小区间尺寸缺省值（Qt 按字体测算，项目简化为固定值）。 */
#define XHEADERVIEW_DEFAULT_MINIMUM_SECTION_SIZE 20
/** @brief 默认区间尺寸缺省值（Qt 按样式测算，项目简化为固定值；resetDefaultSectionSize 复位点）。 */
#define XHEADERVIEW_DEFAULT_SECTION_SIZE 30
/** @brief ResizeToContents 测算精度缺省值（对标 Qt 缺省 1000）。 */
#define XHEADERVIEW_DEFAULT_RESIZE_CONTENTS_PRECISION 1000

/** @brief 发射 int 信号（载荷单值）。 */
static void xhv_emitInt(XHeaderView* self, size_t signal, int value)
{
    XVarList* arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射 bool 信号（载荷单值，对标 sortIndicatorClearableChanged）。 */
static void xhv_emitBool(XHeaderView* self, size_t signal, bool value)
{
    XVarList* arguments = XVarList_Create(XVar(bool, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射双 int 信号（载荷两值，对标 sectionCountChanged）。 */
static void xhv_emitInt2(XHeaderView* self, size_t signal, int a, int b)
{
    XVarList* arguments = XVarList_Create(XVar(int, a), XVar(int, b));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射三 int 信号（载荷三值，对标 sectionMoved/sectionResized）。 */
static void xhv_emitInt3(XHeaderView* self, size_t signal, int a, int b, int c)
{
    XVarList* arguments = XVarList_Create(XVar(int, a), XVar(int, b),
                                          XVar(int, c));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射无载荷信号（对标 geometriesChanged）。 */
static void xhv_emitVoid(XHeaderView* self, size_t signal)
{
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    }
}

/* ==================== 鼠标点击链（sectionPressed/sectionClicked） ==================== */

/* 头文件无按压段字段的替代承载：表头 → 按压段逻辑号 的单链旁表
 * （对照 XAbstractItemView 悬停差分旁表范式）。节点随首次按压创建、
 * 随表头析构移除（VXHeaderView_deinit 为全路径唯一析构入口），无
 * 泄漏与悬垂键。 */
typedef struct XhvPressEntry
{
    struct XhvPressEntry* m_next; /**< 链表后继。 */
    const XHeaderView* m_view;    /**< 键（表头借用指针）。 */
    int m_pressed;                /**< 按压段逻辑号；-1=无按压。 */
} XhvPressEntry;

static XhvPressEntry* xhv_pressEntries = NULL;

/** @brief 读表头的按压段；无记录返回 -1。 */
static int xhv_pressGet(const XHeaderView* view)
{
    const XhvPressEntry* e;
    for (e = xhv_pressEntries; e; e = e->m_next)
        if (e->m_view == view) return e->m_pressed;
    return -1;
}

/** @brief 写表头的按压段（无记录则头插建节点；分配失败忽略——
 *         释放路径按 -1 基准收敛，最多不发射 sectionClicked）。 */
static void xhv_pressSet(const XHeaderView* view, int section)
{
    XhvPressEntry* e;
    for (e = xhv_pressEntries; e; e = e->m_next) {
        if (e->m_view == view) {
            e->m_pressed = section;
            return;
        }
    }
    e = (XhvPressEntry*)XMalloc_System(sizeof(*e));
    if (!e) return;
    e->m_next = xhv_pressEntries;
    e->m_view = view;
    e->m_pressed = section;
    xhv_pressEntries = e;
}

/** @brief 移除表头的按压记录（析构路径）。 */
static void xhv_pressRelease(const XHeaderView* view)
{
    XhvPressEntry** p = &xhv_pressEntries;
    while (*p) {
        if ((*p)->m_view == view) {
            XhvPressEntry* dead = *p;
            *p = dead->m_next;
            XFree_System(dead);
            return;
        }
        p = &(*p)->m_next;
    }
}

static void VXHeaderView_mousePressEvent(XWidget* self, XEvent* event);
static void VXHeaderView_mouseReleaseEvent(XWidget* self, XEvent* event);

/* 对标 Qt QHeaderView::mousePressEvent（qheaderview.cpp:2505）：左键
 * 按压于段上（非段间手柄，本库暂无调宽状态机）时记录按压段，
 * clickableSections 时发射 sectionPressed。 */
static void VXHeaderView_mousePressEvent(XWidget* self, XEvent* event)
{
    XHeaderView* header = (XHeaderView*)self;
    XMouseEvent* me;
    XPoint pos;
    int position;
    int section;
    if (!header || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    pos = XMouseEvent_position(me);
    position = (header->m_orientation == 0) ? pos.x : pos.y;
    section = XHeaderView_logicalIndexAt(header, position);
    xhv_pressSet(header, section);
    if (section >= 0 && header->m_sectionsClickable)
        XHeaderView_sectionPressed_signal(header, section);
    XEvent_accept(event);
}

/* 对标 Qt QHeaderView::mouseReleaseEvent（qheaderview.cpp:2668 尾段）：
 * clickable 且释放位于按压段内时发射 sectionClicked（Qt 另按段矩形
 * contains 判定并翻转排序指示器；本库无段拖拽/指示器状态机，简化
 * 为释放段==按压段同段判定）。 */
static void VXHeaderView_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XHeaderView* header = (XHeaderView*)self;
    XMouseEvent* me;
    XPoint pos;
    int position;
    int section;
    int pressed;
    if (!header || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    pos = XMouseEvent_position(me);
    position = (header->m_orientation == 0) ? pos.x : pos.y;
    section = XHeaderView_logicalIndexAt(header, position);
    pressed = xhv_pressGet(header);
    xhv_pressSet(header, -1);
    if (header->m_sectionsClickable && section >= 0 && section == pressed)
        XHeaderView_sectionClicked_signal(header, section);
    XEvent_accept(event);
}

static void VXHeaderView_deinit(XHeaderView* self)
{
    if (!self) return;
    /* 按压段旁表节点随表头析构移除（防悬垂键；旁表头为静态承载）。 */
    xhv_pressRelease(self);
    if (self->m_sections) {
        XVector_delete_base(self->m_sections);
        self->m_sections = NULL;
    }
    if (self->m_sectionModes) {
        XVector_delete_base(self->m_sectionModes);
        self->m_sectionModes = NULL;
    }
    if (self->m_hidden) {
        XFree_System(self->m_hidden);
        self->m_hidden = NULL;
    }
    /* m_model 为借用指针（表头不拥有），仅解除关联。 */
    self->m_model = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

static void VXHeaderView_copy(XHeaderView* self, const XHeaderView* other)
{
    int i;
    int n;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XHeaderView_init(self, NULL, 0, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_orientation = other->m_orientation;
    self->m_count = other->m_count;
    self->m_defaultSize = other->m_defaultSize;
    self->m_stretchLast = other->m_stretchLast;
    XVector_clear_base(self->m_sections);
    n = other->m_sections
            ? (int)XVector_size_base((const XContainer*)other->m_sections)
            : 0;
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(other->m_sections, (int64_t)i, int);
        XVector_push_back_1_base(self->m_sections, &v);
    }
    XVector_clear_base(self->m_sectionModes);
    n = other->m_sectionModes
            ? (int)XVector_size_base((const XContainer*)other->m_sectionModes)
            : 0;
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(other->m_sectionModes, (int64_t)i, int);
        XVector_push_back_1_base(self->m_sectionModes, &v);
    }
    if (self->m_hidden) {
        XFree_System(self->m_hidden);
        self->m_hidden = NULL;
    }
    if (n > 0) {
        self->m_hidden = (bool*)XMalloc_System(sizeof(bool) * (size_t)n);
        if (self->m_hidden)
            XMemmove(self->m_hidden, other->m_hidden,
                     sizeof(bool) * (size_t)n);
    }
    self->m_hiddenCount = other->m_hiddenCount;
    self->m_sectionsClickable = other->m_sectionsClickable;
    self->m_sectionsMovable = other->m_sectionsMovable;
    self->m_sortIndicatorShown = other->m_sortIndicatorShown;
    self->m_sortIndicatorSection = other->m_sortIndicatorSection;
    self->m_sortIndicatorOrder = other->m_sortIndicatorOrder;
    self->m_sortIndicatorClearable = other->m_sortIndicatorClearable;
    self->m_minimumSectionSize = other->m_minimumSectionSize;
    self->m_maximumSectionSize = other->m_maximumSectionSize;
    self->m_resizeMode = other->m_resizeMode;
    self->m_firstSectionMovable = other->m_firstSectionMovable;
    self->m_defaultAlignment = other->m_defaultAlignment;
    self->m_highlightSections = other->m_highlightSections;
    self->m_cascadingResizes = other->m_cascadingResizes;
    self->m_resizeContentsPrecision = other->m_resizeContentsPrecision;
    self->m_offset = other->m_offset;
    /* m_model 为借用指针，浅拷贝即可（不转移所有权）。 */
    self->m_model = other->m_model;
}

static void VXHeaderView_move(XHeaderView* self, XHeaderView* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XHeaderView_init(self, NULL, 0, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    if (self->m_sections) XVector_delete_base(self->m_sections);
    if (self->m_sectionModes) XVector_delete_base(self->m_sectionModes);
    if (self->m_hidden) XFree_System(self->m_hidden);
    self->m_orientation = other->m_orientation;
    self->m_count = other->m_count;
    self->m_defaultSize = other->m_defaultSize;
    self->m_stretchLast = other->m_stretchLast;
    self->m_sections = other->m_sections;
    other->m_sections = XVector_Create(int);
    self->m_sectionModes = other->m_sectionModes;
    other->m_sectionModes = XVector_Create(int);
    self->m_hidden = other->m_hidden;
    other->m_hidden = NULL;
    self->m_hiddenCount = other->m_hiddenCount;
    other->m_hiddenCount = 0;
    self->m_sectionsClickable = other->m_sectionsClickable;
    self->m_sectionsMovable = other->m_sectionsMovable;
    self->m_sortIndicatorShown = other->m_sortIndicatorShown;
    self->m_sortIndicatorSection = other->m_sortIndicatorSection;
    self->m_sortIndicatorOrder = other->m_sortIndicatorOrder;
    self->m_sortIndicatorClearable = other->m_sortIndicatorClearable;
    self->m_minimumSectionSize = other->m_minimumSectionSize;
    self->m_maximumSectionSize = other->m_maximumSectionSize;
    self->m_resizeMode = other->m_resizeMode;
    self->m_firstSectionMovable = other->m_firstSectionMovable;
    self->m_defaultAlignment = other->m_defaultAlignment;
    self->m_highlightSections = other->m_highlightSections;
    self->m_cascadingResizes = other->m_cascadingResizes;
    self->m_resizeContentsPrecision = other->m_resizeContentsPrecision;
    other->m_count = 0;
    self->m_offset = other->m_offset;
    /* m_model 为借用指针，移动即转移（源对象解除关联）。 */
    self->m_model = other->m_model;
    other->m_model = NULL;
}

XVtable* XHeaderView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHeaderView)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHeaderView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHeaderView_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHeaderView_move);
    /* 对标 Qt QHeaderView 的鼠标事件承接（qheaderview.cpp:2505/:2668）：
     * 段按压/释放驱动 sectionPressed/sectionClicked 发射。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXHeaderView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXHeaderView_mouseReleaseEvent);
    return XVTABLE_DEFAULT;
}

void XHeaderView_init(XHeaderView* self, XWidget* parent, XWidgetFlags flags,
                      int orientation)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XHeaderView);
    self->m_orientation = (orientation == 1) ? 1 : 0;
    self->m_count = 0;
    self->m_defaultSize = XHEADERVIEW_DEFAULT_SECTION_SIZE;
    self->m_stretchLast = false;
    self->m_sectionMovedFrom = -1;
    self->m_offset = 0;
    self->m_model = NULL;
    self->m_sections = XVector_Create(int);
    self->m_sectionModes = XVector_Create(int);
    self->m_hidden = NULL;
    self->m_hiddenCount = 0;
    self->m_sectionsClickable = false;
    self->m_sectionsMovable = false;
    self->m_sortIndicatorShown = false;
    self->m_sortIndicatorSection = -1;
    self->m_sortIndicatorOrder = 0;
    self->m_sortIndicatorClearable = false;
    /* 对标 Qt：QTableView 场景下首段默认允许拖动（QTreeView 为 false）。 */
    self->m_firstSectionMovable = true;
    self->m_minimumSectionSize = XHEADERVIEW_DEFAULT_MINIMUM_SECTION_SIZE;
    self->m_maximumSectionSize = XHEADERVIEW_SECTION_SIZE_LIMIT;
    self->m_resizeMode = (int)XHeaderViewResizeMode_Interactive;
    /* 对标 Qt setDefaultValues：水平头居中，垂直头左对齐+垂直居中。 */
    self->m_defaultAlignment = (self->m_orientation == 0)
        ? (int)XAlignment_Center
        : ((int)XAlignment_Left | (int)XAlignment_VCenter);
    self->m_highlightSections = false;
    self->m_cascadingResizes = false;
    self->m_resizeContentsPrecision =
        XHEADERVIEW_DEFAULT_RESIZE_CONTENTS_PRECISION;
}

XHeaderView* XHeaderView_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags, int orientation)
{
    XHeaderView* self = (XHeaderView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHeaderView_init(self, parent, flags, orientation);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

int XHeaderView_orientation(const XHeaderView* self)
{ return self ? self->m_orientation : 0; }

int XHeaderView_count(const XHeaderView* self)
{ return self ? self->m_count : 0; }

void XHeaderView_setCount(XHeaderView* self, int count)
{
    int i;
    int n;
    int oldCount;
    if (!self || !self->m_sections || count < 0) return;
    oldCount = self->m_count;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (count > n) {
        for (i = n; i < count; ++i) {
            int v = self->m_defaultSize;
            int mv = -1;
            bool h = false;
            XVector_push_back_1_base(self->m_sections, &v);
            if (self->m_sectionModes)
                XVector_push_back_1_base(self->m_sectionModes, &mv);
            self->m_hidden = (bool*)XRealloc_System(
                self->m_hidden, sizeof(bool) * (size_t)(i + 1));
            if (self->m_hidden) self->m_hidden[i] = h;
        }
    } else if (count < n) {
        for (i = count; i < n; ++i) {
            if (self->m_hidden && self->m_hidden[i]) --self->m_hiddenCount;
        }
        XVector_remove_base(self->m_sections, (int64_t)count,
                            (int64_t)(n - count));
        if (self->m_sectionModes)
            XVector_remove_base(self->m_sectionModes, (int64_t)count,
                                (int64_t)(n - count));
    }
    if (count == oldCount) return; /* 对标 Qt：段数未变不发信号。 */
    self->m_count = count;
    /* 对标 Qt emitSectionCountChanged + updateGeometries→geometriesChanged。 */
    xhv_emitInt2(self, (size_t)XHeaderView_sectionCountChanged_signal(
                      self, oldCount, count), oldCount, count);
    xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
}

/** @brief 按当前 min/max 上下限钳制尺寸（对标 Qt qBound(minimumSectionSize, size, maximumSectionSize)）。 */
static int xhv_clampSize(const XHeaderView* self, int size)
{
    if (size < self->m_minimumSectionSize) return self->m_minimumSectionSize;
    if (size > self->m_maximumSectionSize) return self->m_maximumSectionSize;
    return size;
}

void XHeaderView_resizeSection(XHeaderView* self, int logicalIndex, int size)
{
    int n;
    if (!self || !self->m_sections || logicalIndex < 0 || size <= 0) return;
    if (size > XHEADERVIEW_SECTION_SIZE_LIMIT) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (logicalIndex >= n) return;
    /* 对标 Qt resizeSection：程序化调整前先钳制到 [min,max]，随后走
     * setSectionSize 统一发射 sectionResized 与 geometriesChanged（隐藏
     * 段在本实现仍持有尺寸表项，直接改写该表项即可，无需独立隐藏表）。 */
    XHeaderView_setSectionSize(self, logicalIndex, xhv_clampSize(self, size));
}

/** @brief 查询区间是否隐藏（对标 isSectionHidden）。 */
bool XHeaderView_isSectionHidden(const XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_hidden || section < 0) return false;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (section >= n) return false;
    return self->m_hidden[section];
}

void XHeaderView_hideSection(XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_hidden || section < 0) return;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (section >= n) return;
    if (!self->m_hidden[section]) {
        self->m_hidden[section] = true;
        ++self->m_hiddenCount;
        /* 隐藏影响表头几何（对标 Qt updateGeometries→geometriesChanged）。 */
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    }
}

void XHeaderView_showSection(XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_hidden || section < 0) return;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (section >= n) return;
    if (self->m_hidden[section]) {
        self->m_hidden[section] = false;
        --self->m_hiddenCount;
        /* 显示影响表头几何（对标 Qt updateGeometries→geometriesChanged）。 */
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    }
}

int XHeaderView_hiddenSectionCount(const XHeaderView* self)
{ return self ? self->m_hiddenCount : 0; }

void XHeaderView_setSectionHidden(XHeaderView* self, int section, bool hide)
{
    /* 对标 Qt：setSectionHidden 为原始入口（本实现反向转发便捷接口，
     * 信号语义与 hideSection/showSection 一致）。 */
    if (hide) XHeaderView_hideSection(self, section);
    else XHeaderView_showSection(self, section);
}

void XHeaderView_setSectionsClickable(XHeaderView* self, bool clickable)
{ if (self) self->m_sectionsClickable = clickable; }

bool XHeaderView_sectionsClickable(const XHeaderView* self)
{ return self ? self->m_sectionsClickable : false; }

void XHeaderView_setSectionsMovable(XHeaderView* self, bool movable)
{ if (self) self->m_sectionsMovable = movable; }

bool XHeaderView_sectionsMovable(const XHeaderView* self)
{ return self ? self->m_sectionsMovable : false; }

void XHeaderView_swapSections(XHeaderView* self, int first, int second)
{
    int n;
    int tmp;
    bool htmp;
    if (!self || !self->m_sections || first < 0 || second < 0) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (first >= n || second >= n || first == second) return;
    tmp = XVector_At_Base(self->m_sections, (int64_t)first, int);
    XVector_At_Base(self->m_sections, (int64_t)first, int) =
        XVector_At_Base(self->m_sections, (int64_t)second, int);
    XVector_At_Base(self->m_sections, (int64_t)second, int) = tmp;
    if (self->m_sectionModes) {
        tmp = XVector_At_Base(self->m_sectionModes, (int64_t)first, int);
        XVector_At_Base(self->m_sectionModes, (int64_t)first, int) =
            XVector_At_Base(self->m_sectionModes, (int64_t)second, int);
        XVector_At_Base(self->m_sectionModes, (int64_t)second, int) = tmp;
    }
    if (self->m_hidden) {
        htmp = self->m_hidden[first];
        self->m_hidden[first] = self->m_hidden[second];
        self->m_hidden[second] = htmp;
    }
    self->m_sectionMovedFrom = first;
    /* 对标 Qt emitSectionMoved：交换即两个方向各发生一次段移动
     * （逻辑序与视觉序恒一致，载荷逻辑号取移动后的新位置）。 */
    xhv_emitInt3(self, (size_t)XHeaderView_sectionMoved_signal(
                      self, second, first, second), second, first, second);
    xhv_emitInt3(self, (size_t)XHeaderView_sectionMoved_signal(
                      self, first, second, first), first, second, first);
    xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
}

void XHeaderView_moveSection(XHeaderView* self, int from, int to)
{
    int i;
    int n;
    int size;
    int mode = -1;
    bool hidden;
    if (!self || !self->m_sections || from < 0 || to < 0 || from == to)
        return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (from >= n || to >= n) return;
    size = XVector_At_Base(self->m_sections, (int64_t)from, int);
    hidden = self->m_hidden ? self->m_hidden[from] : false;
    if (self->m_sectionModes)
        mode = XVector_At_Base(self->m_sectionModes, (int64_t)from, int);
    if (from < to) {
        for (i = from; i < to; ++i) {
            XVector_At_Base(self->m_sections, (int64_t)i, int) =
                XVector_At_Base(self->m_sections, (int64_t)(i + 1), int);
            if (self->m_sectionModes)
                XVector_At_Base(self->m_sectionModes, (int64_t)i, int) =
                    XVector_At_Base(self->m_sectionModes,
                                    (int64_t)(i + 1), int);
            if (self->m_hidden)
                self->m_hidden[i] = self->m_hidden[i + 1];
        }
    } else {
        for (i = from; i > to; --i) {
            XVector_At_Base(self->m_sections, (int64_t)i, int) =
                XVector_At_Base(self->m_sections, (int64_t)(i - 1), int);
            if (self->m_sectionModes)
                XVector_At_Base(self->m_sectionModes, (int64_t)i, int) =
                    XVector_At_Base(self->m_sectionModes,
                                    (int64_t)(i - 1), int);
            if (self->m_hidden)
                self->m_hidden[i] = self->m_hidden[i - 1];
        }
    }
    XVector_At_Base(self->m_sections, (int64_t)to, int) = size;
    if (self->m_sectionModes)
        XVector_At_Base(self->m_sectionModes, (int64_t)to, int) = mode;
    if (self->m_hidden) self->m_hidden[to] = hidden;
    self->m_sectionMovedFrom = from;
    /* 对标 Qt emitSectionMoved(from→to)：载荷逻辑号取移动后的新位置
     * （逻辑序与视觉序恒一致），原视觉序 from、新视觉序 to。 */
    xhv_emitInt3(self, (size_t)XHeaderView_sectionMoved_signal(
                      self, to, from, to), to, from, to);
    xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
}

void XHeaderView_setSortIndicator(XHeaderView* self, int section, int order)
{
    if (!self) return;
    if (section < -1) section = -1;
    if (self->m_sortIndicatorSection == section &&
        self->m_sortIndicatorOrder == order)
        return;
    self->m_sortIndicatorSection = section;
    self->m_sortIndicatorOrder = order;
    self->m_sortIndicatorShown = true;
    xhv_emitInt(self, (size_t)XHeaderView_sortIndicatorChanged_signal(
                          self, section, order), section);
}

int XHeaderView_sortIndicatorSection(const XHeaderView* self)
{ return self ? self->m_sortIndicatorSection : -1; }

int XHeaderView_sortIndicatorOrder(const XHeaderView* self)
{ return self ? self->m_sortIndicatorOrder : 0; }

void XHeaderView_setSortIndicatorShown(XHeaderView* self, bool shown)
{ if (self) self->m_sortIndicatorShown = shown; }

bool XHeaderView_isSortIndicatorShown(const XHeaderView* self)
{ return self ? self->m_sortIndicatorShown : false; }

void XHeaderView_setSortIndicatorClearable(XHeaderView* self, bool clearable)
{
    if (!self || self->m_sortIndicatorClearable == clearable) return;
    self->m_sortIndicatorClearable = clearable;
    /* 对标 Qt：状态实际变化时发射 sortIndicatorClearableChanged。 */
    xhv_emitBool(self, (size_t)XHeaderView_sortIndicatorClearableChanged_signal(
                          self, clearable), clearable);
}

bool XHeaderView_isSortIndicatorClearable(const XHeaderView* self)
{ return self ? self->m_sortIndicatorClearable : false; }

/* ==================== 尺寸上下限/模式与状态（对标 QHeaderView min/max/resizeMode 族） ==================== */

void XHeaderView_setMaximumSectionSize(XHeaderView* self, int size)
{
    int n;
    int i;
    if (!self) return;
    if (size == -1) {
        /* Qt 语义：-1 表示重置为段尺寸硬上限。 */
        self->m_maximumSectionSize = XHEADERVIEW_SECTION_SIZE_LIMIT;
        return;
    }
    if (size < 0 || size > XHEADERVIEW_SECTION_SIZE_LIMIT) return;
    /* 上限落库（此前漏写：仅钳制既有段/抬高下限，maximumSectionSize
       查询恒返回缺省值，resizeSection 的钳制随之失效）。 */
    self->m_maximumSectionSize = size;
    /* 新上限低于既有限制时，Qt 同步抬高最小值，维持 min<=max 不变式。 */
    if (self->m_minimumSectionSize > size)
        self->m_minimumSectionSize = size;
    if (!self->m_sections) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(self->m_sections, (int64_t)i, int);
        if (v > size)
            XVector_At_Base(self->m_sections, (int64_t)i, int) = size;
    }
}

int XHeaderView_maximumSectionSize(const XHeaderView* self)
{ return self ? self->m_maximumSectionSize : 0; }

void XHeaderView_setMinimumSectionSize(XHeaderView* self, int size)
{
    int n;
    int i;
    if (!self || size < 0 || size > XHEADERVIEW_SECTION_SIZE_LIMIT) return;
    /* 新下限超过既有限制时，Qt 同步抬高最大值，维持 min<=max 不变式。 */
    if (size > self->m_maximumSectionSize)
        self->m_maximumSectionSize = size;
    if (!self->m_sections) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(self->m_sections, (int64_t)i, int);
        if (v < size)
            XVector_At_Base(self->m_sections, (int64_t)i, int) = size;
    }
}

int XHeaderView_minimumSectionSize(const XHeaderView* self)
{ return self ? self->m_minimumSectionSize : 0; }

void XHeaderView_setSectionResizeMode(XHeaderView* self, int mode)
{
    int n;
    int i;
    if (!self) return;
    if (mode < (int)XHeaderViewResizeMode_Interactive ||
        mode > (int)XHeaderViewResizeMode_ResizeToContents)
        return;
    self->m_resizeMode = mode;
    /* 对标 Qt setGlobalHeaderResizeMode：全局模式覆写全部段的单段
     * 模式；本实现以清除全部单段覆写（复位 -1=跟随全局）达成等价
     * 语义。 */
    if (self->m_sectionModes) {
        n = (int)XVector_size_base((const XContainer*)self->m_sectionModes);
        for (i = 0; i < n; ++i)
            XVector_At_Base(self->m_sectionModes, (int64_t)i, int) = -1;
    }
    /* 对标 Qt setSectionResizeMode(ResizeMode)：设置全局模式后，模式为
     * Stretch/ResizeToContents（hasAutoResizeSections）时调度
     * doDelayedResizeSections（等效 resizeSections(mode)）；本实现收敛
     * 为同步调用——Stretch 以当前 length 充当可用空间在可见段间均分
     * （几何实际变化时发射 geometriesChanged，见 resizeSections 的批量
     * 路径 @note），其余模式内部仅调度全量重绘。 */
    XHeaderView_resizeSections(self, mode);
}

int XHeaderView_sectionResizeMode(const XHeaderView* self)
{
    return self ? self->m_resizeMode
                : (int)XHeaderViewResizeMode_Interactive;
}

void XHeaderView_resizeSections(XHeaderView* self, int mode)
{
    int n;
    int i;
    int total = 0;
    int visible = 0;
    int each = 0;
    bool changed = false;
    if (!self || !self->m_sections) return;
    if (mode < (int)XHeaderViewResizeMode_Interactive ||
        mode > (int)XHeaderViewResizeMode_ResizeToContents)
        return;
    if (mode != (int)XHeaderViewResizeMode_Stretch) {
        /* Interactive/Fixed 各段保持现尺寸（Qt 即钳制到现值）；本实现
         * 段尺寸已恒在 [min,max] 内，无需改写；ResizeToContents 内容
         * 测算未接，同样保持。仅调度全量重绘（见头文件 @note）。 */
        XWidget_update((XWidget*)self);
        return;
    }
    /* Stretch：以当前 length 充当可用空间（视口宽度未接），在可见段间
     * 均分；逐段钳制到 [min,max]，隐藏段不参与（Qt 亦跳过隐藏段）。 */
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        if (self->m_hidden && self->m_hidden[i]) continue;
        total += XVector_At_Base(self->m_sections, (int64_t)i, int);
        ++visible;
    }
    if (visible <= 0) return;
    each = total / visible;
    for (i = 0; i < n; ++i) {
        int oldSize;
        int newSize;
        if (self->m_hidden && self->m_hidden[i]) continue;
        newSize = xhv_clampSize(self, each);
        oldSize = XVector_At_Base(self->m_sections, (int64_t)i, int);
        if (newSize != oldSize) {
            XVector_At_Base(self->m_sections, (int64_t)i, int) = newSize;
            changed = true;
        }
    }
    if (changed) {
        /* 批量路径仅整体刷新：不逐段发 sectionResized（同 restoreState
         * 的批量先例），几何有实际变化时发射一次 geometriesChanged。 */
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    }
    XWidget_update((XWidget*)self);
}

void XHeaderView_setSectionResizeModeAt(XHeaderView* self, int section,
                                        int mode)
{
    int n;
    if (!self || !self->m_sections || section < 0) return;
    /* -1 哨兵=清除覆写（读侧以越界值判"未设置"回落全局模式），其余
       非法模式值仍拒绝。 */
    if (mode != -1 &&
        (mode < (int)XHeaderViewResizeMode_Interactive ||
         mode > (int)XHeaderViewResizeMode_ResizeToContents))
        return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (section >= n || !self->m_sectionModes) return;
    XVector_At_Base(self->m_sectionModes, (int64_t)section, int) = mode;
}

int XHeaderView_sectionResizeModeAt(const XHeaderView* self, int logicalIndex)
{
    int n;
    int mode;
    if (!self) return (int)XHeaderViewResizeMode_Interactive;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (logicalIndex >= 0 && logicalIndex < n && self->m_sectionModes) {
        mode = XVector_At_Base(self->m_sectionModes,
                               (int64_t)logicalIndex, int);
        if (mode >= (int)XHeaderViewResizeMode_Interactive &&
            mode <= (int)XHeaderViewResizeMode_ResizeToContents)
            return mode;
    }
    /* 无覆写（-1）或越界：返回全局模式（Qt 对越界返回 Fixed，此处
     * 收敛为全局模式以免与"未设置"语义混淆）。 */
    return self->m_resizeMode;
}

void XHeaderView_setFirstSectionMovable(XHeaderView* self, bool movable)
{ if (self) self->m_firstSectionMovable = movable; }

bool XHeaderView_isFirstSectionMovable(const XHeaderView* self)
{
    return self ? (self->m_firstSectionMovable && self->m_sectionsMovable)
                : false;
}

void XHeaderView_reset(XHeaderView* self)
{
    int n;
    int i;
    if (!self || !self->m_sections) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        XVector_At_Base(self->m_sections, (int64_t)i, int) =
            self->m_defaultSize;
        if (self->m_sectionModes)
            XVector_At_Base(self->m_sectionModes, (int64_t)i, int) = -1;
        if (self->m_hidden) self->m_hidden[i] = false;
    }
    self->m_hiddenCount = 0;
    self->m_sectionMovedFrom = -1;
    /* 清空排序指示器（静默复位，不发信号；Qt reset 亦不发射）。 */
    self->m_sortIndicatorShown = false;
    self->m_sortIndicatorSection = -1;
    self->m_sortIndicatorOrder = 0;
    if (n > 0) {
        /* 尺寸与隐藏状态的恢复影响表头几何，发 geometriesChanged。 */
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    }
}

void XHeaderView_setDefaultAlignment(XHeaderView* self, int alignment)
{ if (self) self->m_defaultAlignment = alignment; }

int XHeaderView_defaultAlignment(const XHeaderView* self)
{ return self ? self->m_defaultAlignment : 0; }

int XHeaderView_stretchSectionCount(const XHeaderView* self)
{ return (self && self->m_stretchLast) ? 1 : 0; }

bool XHeaderView_sectionsHidden(const XHeaderView* self)
{ return self ? (self->m_hiddenCount > 0) : false; }

void XHeaderView_setHighlightSections(XHeaderView* self, bool highlight)
{ if (self) self->m_highlightSections = highlight; }

bool XHeaderView_highlightSections(const XHeaderView* self)
{ return self ? self->m_highlightSections : false; }

void XHeaderView_setCascadingSectionResizes(XHeaderView* self, bool enable)
{ if (self) self->m_cascadingResizes = enable; }

bool XHeaderView_cascadingSectionResizes(const XHeaderView* self)
{ return self ? self->m_cascadingResizes : false; }

void XHeaderView_setResizeContentsPrecision(XHeaderView* self, int precision)
{ if (self) self->m_resizeContentsPrecision = precision; }

int XHeaderView_resizeContentsPrecision(const XHeaderView* self)
{ return self ? self->m_resizeContentsPrecision : 0; }

/* ==================== 布局与状态持久化（对标 doItemsLayout/saveState/restoreState） ==================== */

void XHeaderView_doItemsLayout(XHeaderView* self)
{
    if (!self) return;
    /* 对标 Qt doItemsLayout：Qt 会重算段几何并驱动一次模型布局；本项目
     * 段几何即时维护、表头渲染由 XTableView 统一完成，此处仅调度整控件
     * 全量重绘（等效"全量 update"，见头文件 @note）。 */
    XWidget_update((XWidget*)self);
}

#if XByteArray_ON

/** @brief 状态序列化格式版本（XHV 文本格式 v1；字段结构变更时递增）。 */
#define XHEADERVIEW_STATE_VERSION "001"
/** @brief 状态序列化整数字段位宽（含符号位；可表示 -999999..9999999）。 */
#define XHEADERVIEW_STATE_INT_WIDTH 7

/** @brief 追加 1 位数字字段（布尔 0/1 或方向 0/1）。 */
static bool xhv_stateWriteDigit(XByteArray* out, int value)
{
    char buf[2];
    if (!out || value < 0 || value > 9) return false;
    buf[0] = (char)('0' + value);
    buf[1] = '\0';
    return XByteArray_append_utf8(out, buf);
}

/** @brief 追加固定位宽整数字段（%0*d 语义；负数占符号位）。 */
static bool xhv_stateWriteInt(XByteArray* out, int value)
{
    char buf[16];
    if (!out) return false;
    if (XSnprintf(buf, sizeof(buf), "%0*d",
                  XHEADERVIEW_STATE_INT_WIDTH, value) !=
        XHEADERVIEW_STATE_INT_WIDTH)
        return false;
    return XByteArray_append_utf8(out, buf);
}

/** @brief 追加单段模式字段：'0'..'3' 为显式模式，'g' 为跟随全局（-1）。 */
static bool xhv_stateWriteMode(XByteArray* out, int mode)
{
    char buf[2];
    if (!out) return false;
    if (mode >= (int)XHeaderViewResizeMode_Interactive &&
        mode <= (int)XHeaderViewResizeMode_ResizeToContents)
        buf[0] = (char)('0' + mode);
    else if (mode == -1)
        buf[0] = 'g';
    else
        return false;
    buf[1] = '\0';
    return XByteArray_append_utf8(out, buf);
}

/** @brief 读取固定位宽整数字段（支持前导负号；宽度不足或含非数字失败）。 */
static bool xhv_stateReadInt(const char** cursor, const char* end, int* out)
{
    const char* p;
    int sign = 1;
    int value = 0;
    int consumed = 0;
    if (!cursor || !*cursor || !out) return false;
    p = *cursor;
    if (end - p < XHEADERVIEW_STATE_INT_WIDTH) return false;
    /* 跳过前导空格(WriteInt 的 %0*d 对负数产生空格前缀)。 */
    while (p < end && *p == ' ') ++p;
    if (*p == '-') {
        sign = -1;
        ++p;
        ++consumed;
    }
    for (; consumed < XHEADERVIEW_STATE_INT_WIDTH; ++consumed, ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10 + (*p - '0');
    }
    *out = sign * value;
    *cursor = p;
    return true;
}

/** @brief 读取 1 字符字段（越界失败）。 */
static bool xhv_stateReadChar(const char** cursor, const char* end, char* out)
{
    if (!cursor || !*cursor || !out) return false;
    if (end - *cursor < 1) return false;
    *out = **cursor;
    ++(*cursor);
    return true;
}

/** @brief 释放 restoreState 校验阶段的段字段暂存数组（三指针可为 NULL）。 */
static void xhv_freeStateArrays(int* sizes, bool* hiddens, int* modes)
{
    if (sizes) XFree_System(sizes);
    if (hiddens) XFree_System(hiddens);
    if (modes) XFree_System(modes);
}

XByteArray* XHeaderView_saveState(const XHeaderView* self)
{
    XByteArray* out;
    int n;
    int i;
    bool ok;
    if (!self) return NULL;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    out = XByteArray_create();
    if (!out) return NULL;
    /* 字段顺序必须与 XHeaderView_restoreState 的解析严格对称（格式见
     * 头文件 @note；对标 Qt QHeaderViewPrivate::write 的状态集合）。 */
    ok = XByteArray_append_utf8(out, "XHV" XHEADERVIEW_STATE_VERSION) &&
         xhv_stateWriteInt(out, self->m_orientation) &&
         xhv_stateWriteInt(out, n) &&
         xhv_stateWriteInt(out, self->m_defaultSize) &&
         xhv_stateWriteInt(out, self->m_minimumSectionSize) &&
         xhv_stateWriteInt(out, self->m_maximumSectionSize) &&
         xhv_stateWriteDigit(out, self->m_stretchLast ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_sortIndicatorShown ? 1 : 0) &&
         xhv_stateWriteInt(out, self->m_sortIndicatorSection) &&
         xhv_stateWriteDigit(out,
                             self->m_sortIndicatorOrder ==
                                     (int)XHeaderViewSortOrder_Descending
                                 ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_sortIndicatorClearable ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_sectionsClickable ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_sectionsMovable ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_firstSectionMovable ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_highlightSections ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_cascadingResizes ? 1 : 0) &&
         xhv_stateWriteInt(out, self->m_resizeContentsPrecision) &&
         xhv_stateWriteInt(out, self->m_defaultAlignment);
    for (i = 0; ok && i < n; ++i) {
        int mode = self->m_sectionModes
                ? XVector_At_Base(self->m_sectionModes, (int64_t)i, int)
                : -1;
        ok = xhv_stateWriteInt(out, XVector_At_Base(self->m_sections,
                                                    (int64_t)i, int)) &&
             xhv_stateWriteDigit(out,
                                 (self->m_hidden && self->m_hidden[i])
                                     ? 1 : 0) &&
             xhv_stateWriteMode(out, mode);
    }
    if (!ok) {
        XByteArray_delete_base(out);
        return NULL;
    }
    return out;
}

bool XHeaderView_restoreState(XHeaderView* self, const XByteArray* state)
{
    const char* data;
    const char* cur;
    const char* end;
    int orientation;
    int count;
    int defaultSize;
    int minSize;
    int maxSize;
    int stretchLast;
    int sortShown;
    int sortSection;
    int sortOrder;
    int sortClearable;
    int clickable;
    int movable;
    int firstMovable;
    int highlight;
    int cascading;
    int precision;
    int alignment;
    int* sizes = NULL;
    bool* hiddens = NULL;
    int* modes = NULL;
    int i;
    bool changed;
    if (!self || !state) return false;
    data = (const char*)XByteArray_constData((XByteArray*)state); /* 只读借用。 */
    if (!data) return false;
    end = data + (int)XByteArray_size_base((const XContainer*)state);
    cur = data;
    /* 校验先行：任何字段非法即整体拒绝（不做部分恢复）；失败路径
     * 静默（生产路径无诊断输出，§8.0c2/R4 静默化纪律——此前 25 处
     * [RS-fail] fprintf 已移除）。 */
    if (end - cur < 6 || XStrncmp(cur, "XHV", 3) != 0 ||
        XStrncmp(cur + 3, XHEADERVIEW_STATE_VERSION, 3) != 0)
        return false;
    cur += 6;
    if (!xhv_stateReadInt(&cur, end, &orientation) ||
        (orientation != 0 && orientation != 1) ||
        orientation != self->m_orientation)
        return false;
    if (!xhv_stateReadInt(&cur, end, &count) || count < 0 ||
        count > XHEADERVIEW_SECTION_SIZE_LIMIT)
        return false;
    if (!xhv_stateReadInt(&cur, end, &defaultSize) || defaultSize <= 0)
        return false;
    if (!xhv_stateReadInt(&cur, end, &minSize) || minSize < 0 ||
        minSize > XHEADERVIEW_SECTION_SIZE_LIMIT)
        return false;
    if (!xhv_stateReadInt(&cur, end, &maxSize) || maxSize <= 0 ||
        maxSize > XHEADERVIEW_SECTION_SIZE_LIMIT || minSize > maxSize)
        return false;
    {
        char flag;
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        stretchLast = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        sortShown = (flag == '1');
        if (!xhv_stateReadInt(&cur, end, &sortSection) ||
            sortSection < -1 || sortSection >= count)
            return false;
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        sortOrder = (flag == '1')
                ? (int)XHeaderViewSortOrder_Descending
                : (int)XHeaderViewSortOrder_Ascending;
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        sortClearable = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        clickable = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        movable = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        firstMovable = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        highlight = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        cascading = (flag == '1');
    }
    if (!xhv_stateReadInt(&cur, end, &precision) || precision < -1)
        return false;
    if (!xhv_stateReadInt(&cur, end, &alignment) || alignment < 0)
        return false;
    /* 每段尺寸/隐藏/模式字段；校验同时暂存（应用阶段使用）。 */
    if (count > 0) {
        sizes = (int*)XMalloc_System(sizeof(int) * (size_t)count);
        hiddens = (bool*)XMalloc_System(sizeof(bool) * (size_t)count);
        modes = (int*)XMalloc_System(sizeof(int) * (size_t)count);
        if (!sizes || !hiddens || !modes) {
            XFree_System(sizes);
            XFree_System(hiddens);
            XFree_System(modes);
            return false;
        }
    }
    for (i = 0; i < count; ++i) {
        char hidden;
        char mode;
        int size;
        if (!xhv_stateReadInt(&cur, end, &size) || size <= 0 ||
            size > XHEADERVIEW_SECTION_SIZE_LIMIT) {
            xhv_freeStateArrays(sizes, hiddens, modes);
            return false;
        }
        if (!xhv_stateReadChar(&cur, end, &hidden) ||
            (hidden != '0' && hidden != '1')) {
            xhv_freeStateArrays(sizes, hiddens, modes);
            return false;
        }
        if (!xhv_stateReadChar(&cur, end, &mode) ||
            (mode != 'g' &&
             (mode < '0' ||
              mode > '0' + (int)XHeaderViewResizeMode_ResizeToContents))) {
            xhv_freeStateArrays(sizes, hiddens, modes);
            return false;
        }
        sizes[i] = size;
        hiddens[i] = (hidden == '1');
        modes[i] = (mode == 'g')
                ? -1
                : (int)(mode - '0');
    }
    if (cur != end) {
        xhv_freeStateArrays(sizes, hiddens, modes);
        return false;
    }
    /* ==================== 应用阶段 ==================== */
    /* 段数变化经 setCount 发射 sectionCountChanged/geometriesChanged。 */
    XHeaderView_setCount(self, count);
    XHeaderView_setMaximumSectionSize(self, maxSize);
    XHeaderView_setMinimumSectionSize(self, minSize);
    XHeaderView_setDefaultSectionSize(self, defaultSize);
    self->m_stretchLast = stretchLast;
    XHeaderView_setSectionsClickable(self, clickable);
    XHeaderView_setSectionsMovable(self, movable);
    XHeaderView_setFirstSectionMovable(self, firstMovable);
    XHeaderView_setHighlightSections(self, highlight);
    XHeaderView_setCascadingSectionResizes(self, cascading);
    self->m_resizeContentsPrecision = precision;
    XHeaderView_setDefaultAlignment(self, alignment);
    XHeaderView_setSortIndicatorClearable(self, sortClearable);
    /* 恢复排序指示器：setSortIndicator 在状态实际变化时发射
     * sortIndicatorChanged（Qt 恢复后无条件发射，此处收敛为变化才发）。 */
    XHeaderView_setSortIndicator(self, sortSection, sortOrder);
    XHeaderView_setSortIndicatorShown(self, sortShown);
    /* 逐段直写尺寸/隐藏/模式：不逐段发 sectionResized（对标 Qt 恢复
     * 路径仅整体刷新），几何有实际变化时追加一次 geometriesChanged。 */
    changed = false;
    for (i = 0; i < count && self->m_sections; ++i) {
        int newSize = sizes[i];
        int oldSize;
        if (self->m_sectionModes)
            XVector_At_Base(self->m_sectionModes, (int64_t)i, int) =
                modes[i];
        newSize = xhv_clampSize(self, newSize);
        oldSize = XVector_At_Base(self->m_sections, (int64_t)i, int);
        if (newSize != oldSize) {
            XVector_At_Base(self->m_sections, (int64_t)i, int) = newSize;
            changed = true;
        }
        if (self->m_hidden) {
            if (hiddens[i] != self->m_hidden[i]) {
                if (hiddens[i] && !self->m_hidden[i]) ++self->m_hiddenCount;
                else if (!hiddens[i] && self->m_hidden[i])
                    --self->m_hiddenCount;
                self->m_hidden[i] = hiddens[i];
                changed = true;
            }
        }
    }
    xhv_freeStateArrays(sizes, hiddens, modes);
    if (changed)
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    return true;
}

#endif /* XByteArray_ON */

int XHeaderView_sectionSize(const XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_sections || section < 0) return 0;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (section >= n) return self->m_defaultSize;
    return XVector_At_Base(self->m_sections, (int64_t)section, int);
}

void XHeaderView_setSectionSize(XHeaderView* self, int section, int size)
{
    int n;
    int oldSize;
    if (!self || !self->m_sections || section < 0 || size <= 0) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (section >= n) return;
    oldSize = XVector_At_Base(self->m_sections, (int64_t)section, int);
    XVector_At_Base(self->m_sections, (int64_t)section, int) = size;
    if (size == oldSize) return; /* 对标 Qt：尺寸未变不发信号。 */
    /* 对标 Qt emitSectionResized + updateGeometries→geometriesChanged。 */
    xhv_emitInt3(self, (size_t)XHeaderView_sectionResized_signal(
                      self, section, oldSize, size), section, oldSize, size);
    xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
}

int XHeaderView_defaultSectionSize(const XHeaderView* self)
{ return self ? self->m_defaultSize : 0; }

void XHeaderView_setDefaultSectionSize(XHeaderView* self, int size)
{
    if (self && size > 0) self->m_defaultSize = size;
}

void XHeaderView_resetDefaultSectionSize(XHeaderView* self)
{
    /* 对标 Qt resetDefaultSectionSize：恢复为样式/字体测算的缺省值；
     * 项目缺省值为固定常量 30，已有段尺寸不受影响。 */
    if (self) self->m_defaultSize = XHEADERVIEW_DEFAULT_SECTION_SIZE;
}

int XHeaderView_sectionPosition(const XHeaderView* self, int section)
{
    int pos;
    int i;
    if (!self || !self->m_sections || section < 0) return -1;
    pos = 0;
    for (i = 0; i < section; ++i)
        pos += XHeaderView_sectionSize(self, i);
    return pos;
}

int XHeaderView_sectionViewportPosition(const XHeaderView* self, int section)
{
    /* 视口坐标系：当前无滚动偏移承载（offset 恒 0），与 sectionPosition
     * 同一坐标系，恒等返回（同 Qt offset==0 时的语义）。 */
    return XHeaderView_sectionPosition(self, section);
}

void XHeaderView_setStretchLastSection(XHeaderView* self, bool stretch)
{
    if (self) self->m_stretchLast = stretch;
}

bool XHeaderView_isStretchLastSection(const XHeaderView* self)
{ return self ? self->m_stretchLast : false; }

bool XHeaderView_stretchLastSection(const XHeaderView* self)
{
    /* 对标 Qt 属性读取器 stretchLastSection（isStretchLastSection 的别名）。 */
    return XHeaderView_isStretchLastSection(self);
}

/* ==================== 视觉序与位置反查（对标 QHeaderView visualIndex 族） ==================== */

int XHeaderView_visualIndex(const XHeaderView* self, int logicalIndex)
{
    int n;
    if (!self || logicalIndex < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (logicalIndex >= n) return -1;
    /* 移动即重排：逻辑序与视觉序恒一致（见头文件 @note）。 */
    return logicalIndex;
}

int XHeaderView_visualIndexAt(const XHeaderView* self, int visualIndex)
{
    int n;
    if (!self || visualIndex < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (visualIndex >= n) return -1;
    return visualIndex;
}

int XHeaderView_logicalIndex(const XHeaderView* self, int visualIndex)
{
    int n;
    if (!self || visualIndex < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (visualIndex >= n) return -1;
    /* 移动即重排：视觉序与逻辑序恒一致（同 visualIndexAt，见头文件 @note）。 */
    return visualIndex;
}

int XHeaderView_logicalIndexAt(const XHeaderView* self, int position)
{
    int n;
    int i;
    if (!self || position < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    for (i = 0; i < n; ++i) {
        int size = XVector_At_Base(self->m_sections, (int64_t)i, int);
        if (position < size) return i;
        position -= size;
    }
    return -1;
}

int XHeaderView_sectionSizeHint(const XHeaderView* self, int logicalIndex)
{
    (void)logicalIndex;
    return self ? self->m_defaultSize : 0;
}

int XHeaderView_length(const XHeaderView* self)
{
    int n;
    int i;
    int total = 0;
    if (!self || !self->m_sections) return 0;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        if (self->m_hidden && self->m_hidden[i]) continue;
        total += XVector_At_Base(self->m_sections, (int64_t)i, int);
    }
    return total;
}

void XHeaderView_setOffset(XHeaderView* self, int offset)
{
    /* 仅存储承载：滚动联动未接入，写入值不参与位置计算（见头文件 @note）。 */
    if (self) self->m_offset = offset;
}

int XHeaderView_offset(const XHeaderView* self)
{
    return self ? self->m_offset : 0;
}

void XHeaderView_setOffsetToLastSection(XHeaderView* self)
{
    int off;
    if (!self) return;
    /* 对标 Qt：offset = length − 视口宽度；视口宽度未接（按 0 参与），
     * 偏移退化为 length，再钳制到 >=0。 */
    off = XHeaderView_length(self);
    if (off < 0) off = 0;
    XHeaderView_setOffset(self, off);
}

void XHeaderView_setOffsetToSectionPosition(XHeaderView* self, int index)
{
    int pos;
    if (!self) return;
    pos = XHeaderView_sectionPosition(self, index);
    /* 越界（sectionPosition 返回 -1）忽略，同 Qt 先校验区间号有效。 */
    if (pos < 0) return;
    XHeaderView_setOffset(self, pos);
}

void XHeaderView_setModel(XHeaderView* self, void* model)
{
    /* 不透明承载：仅保存借用指针，表头独立于模型，不触发重建或信号。 */
    if (self) self->m_model = model;
}

XWidget* XHeaderView_viewport(XHeaderView* self)
{
    /* 表头自绘无独立视口（Qt 视口由 QAbstractScrollArea 基类持有），
     * 收敛为返回自身，等效"整个表头即视口"。 */
    return (XWidget*)self;
}

void* XHeaderView_sectionClicked_signal(XHeaderView* self, int section)
{
    /* 真实发射点：VXHeaderView_mouseReleaseEvent（clickable 且释放于
     * 按压段，对标 Qt mouseReleaseEvent 尾段；连接方亦可调用本句柄
     * 手动发射）。 */
    xhv_emitInt(self, (size_t)XHeaderView_sectionClicked_signal, section);
    return (void*)(size_t)XHeaderView_sectionClicked_signal;
}

void* XHeaderView_sectionPressed_signal(XHeaderView* self, int section)
{
    /* 真实发射点：VXHeaderView_mousePressEvent（clickable 且按压于
     * 段上，对标 Qt mousePressEvent 的 sectionPressed 分支）。 */
    xhv_emitInt(self, (size_t)XHeaderView_sectionPressed_signal, section);
    return (void*)(size_t)XHeaderView_sectionPressed_signal;
}

void* XHeaderView_sortIndicatorChanged_signal(XHeaderView* self,
                                              int logicalIndex, int order)
{
    (void)logicalIndex; (void)order;
    return (void*)(size_t)XHeaderView_sortIndicatorChanged_signal;
}

void* XHeaderView_sortIndicatorClearableChanged_signal(XHeaderView* self,
                                                      bool clearable)
{
    (void)clearable;
    return (void*)(size_t)XHeaderView_sortIndicatorClearableChanged_signal;
}

void* XHeaderView_sectionDoubleClicked_signal(XHeaderView* self, int section)
{
    (void)section;
    /* 鼠标事件路径未接入：句柄预留，暂无发射点。 */
    return (void*)(size_t)XHeaderView_sectionDoubleClicked_signal;
}

void* XHeaderView_sectionEntered_signal(XHeaderView* self, int section)
{
    (void)section;
    /* 悬停事件路径未接入：句柄预留，暂无发射点。 */
    return (void*)(size_t)XHeaderView_sectionEntered_signal;
}

void* XHeaderView_sectionHandleDoubleClicked_signal(XHeaderView* self,
                                                    int section)
{
    (void)section;
    /* 排序指示器把手交互路径未接入：句柄预留，暂无发射点。 */
    return (void*)(size_t)XHeaderView_sectionHandleDoubleClicked_signal;
}

void* XHeaderView_sectionMoved_signal(XHeaderView* self, int logicalIndex,
                                      int oldVisualIndex, int newVisualIndex)
{
    (void)logicalIndex; (void)oldVisualIndex; (void)newVisualIndex;
    return (void*)(size_t)XHeaderView_sectionMoved_signal;
}

void* XHeaderView_sectionsMoved_signal(XHeaderView* self, int logicalIndex,
                                       int oldVisualIndex, int newVisualIndex)
{
    (void)self; (void)logicalIndex; (void)oldVisualIndex; (void)newVisualIndex;
    /* 别名句柄：返回与 sectionMoved 相同的信号令牌（函数地址），
     * moveSection/swapSections 的既有 sectionMoved 发射点即为本句柄
     * 的真实发射点，无需重复发射。 */
    return (void*)(size_t)XHeaderView_sectionMoved_signal;
}

void* XHeaderView_sectionResized_signal(XHeaderView* self, int logicalIndex,
                                        int oldSize, int newSize)
{
    (void)logicalIndex; (void)oldSize; (void)newSize;
    return (void*)(size_t)XHeaderView_sectionResized_signal;
}

void* XHeaderView_sectionCountChanged_signal(XHeaderView* self, int oldCount,
                                             int newCount)
{
    (void)oldCount; (void)newCount;
    return (void*)(size_t)XHeaderView_sectionCountChanged_signal;
}

void* XHeaderView_geometriesChanged_signal(XHeaderView* self)
{
    return (void*)(size_t)XHeaderView_geometriesChanged_signal;
}

void* XHeaderView_headerDataChanged_signal(XHeaderView* self, int section)
{
    (void)section;
    /* 模型表头数据变化的 XTableWidget 转发路径未接线：句柄预留，
     * 暂无发射点（对标 Qt 同名公开槽，本项目载荷收敛为单 int）。 */
    return (void*)(size_t)XHeaderView_headerDataChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
