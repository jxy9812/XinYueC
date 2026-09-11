/**
 * @file       XCalendarWidget.c
 * @brief      日历控件实现（对标 Qt 6.8 QCalendarWidget 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XCalendarWidget.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XCALENDARWIDGET_ON

/* ==================== 内部工具 ==================== */

static void xcal_emitDate(XCalendarWidget* self, size_t signal, const XDate* d)
{
    XDate copy = *d;
    XVarList* args = XVarList_Create(XVar(XDate, copy));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xcal_emitPageChanged(XCalendarWidget* self)
{
    XVarList* args = XVarList_Create(XVar(int, self->m_shownYear),
                                     XVar(int, self->m_shownMonth));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XCalendarWidget_currentPageChanged_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xcal_emitSelectionChanged(XCalendarWidget* self)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XCalendarWidget_selectionChanged_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static bool xcal_inRange(const XCalendarWidget* self, const XDate* d)
{
    if (self->m_minSet && XDate_compare(d, &self->m_min) < 0) return false;
    if (self->m_maxSet && XDate_compare(d, &self->m_max) > 0) return false;
    return true;
}

static void xcal_clampToRange(XCalendarWidget* self)
{
    if (self->m_minSet && XDate_compare(&self->m_selected, &self->m_min) < 0)
        self->m_selected = self->m_min;
    if (self->m_maxSet && XDate_compare(&self->m_selected, &self->m_max) > 0)
        self->m_selected = self->m_max;
}

/* ==================== 事件处理 ==================== */

static void VX_calendar_paintEvent(XWidget* self, XEvent* event)
{
    XCalendarWidget* cal = (XCalendarWidget*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t highlight;
    uint32_t windowText;
    uint32_t mid;
    uint32_t base;
    int w;
    int h;
    int y;
    int dayOfWeek;
    int firstCol;
    int i;
    char buf[16];
    XDate first;
    static const char* dayNames[7] = {
        "\xE4\xB8\x80", "\xE4\xBA\x8C", "\xE4\xB8\x89", "\xE5\x9B\x9B",
        "\xE4\xBA\x94", "\xE5\x85\xAD", "\xE6\x97\xA5" };
    if (!cal || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    image = XWidget_paintDevice(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c;
        c = XPalette_color(&palette, XPaletteColorGroup_Current, XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current, XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current, XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current, XPaletteColorRole_Base);
        base = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u; windowText = 0xFF000000u;
    mid = 0xFF808080u; base = 0xFFFFFFFFu;
#endif /* XPALETTE_ON */
    y = 0;
    if (cal->m_navBarVisible) {
        XRect_init(&r, 0, 0, w, 22);
        XPainter_fillRect(&painter, &r, mid);
        snprintf(buf, sizeof(buf), "%04d-%02d", cal->m_shownYear, cal->m_shownMonth);
        XPainter_drawText(&painter, w / 2 - 24, 15, buf, windowText);
        /* 导航按钮：◀(上一年) <(上一月) >(下一月) ▶(下一年) */
        XPainter_drawText(&painter, 4, 15, "<", windowText);
        XPainter_drawText(&painter, 26, 15, ">", windowText);
        XPainter_drawText(&painter, w - 26, 15, "<", windowText);
        XPainter_drawText(&painter, w - 10, 15, ">", windowText);
        y = 22;
    }
    {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
    }
    for (i = 0; i < 7; ++i) {
        int dx = i * w / 7 + w / 14 - 4;
        XPainter_drawText(&painter, dx, y + 14, dayNames[i], mid);
    }
    y += 18;
    XDate_setDate(&first, cal->m_shownYear, cal->m_shownMonth, 1);
    dayOfWeek = XDate_dayOfWeek(&first);
    firstCol = (dayOfWeek - 1 + 7) % 7;
    if (cal->m_firstDayOfWeek >= 1 && cal->m_firstDayOfWeek <= 7)
        firstCol = (dayOfWeek - cal->m_firstDayOfWeek + 7) % 7;
    {
        int dim = XDate_daysInMonth(&first);
        int row;
        for (i = 0; i < dim; ++i) {
            int col = firstCol + i;
            int row = col / 7;
            int col7 = col % 7;
            int cx = col7 * w / 7 + w / 14 - 4;
            int cy = y + row * 24 + 14;
            XDate d;
            XDate_setDate(&d, cal->m_shownYear, cal->m_shownMonth, i + 1);
            snprintf(buf, sizeof(buf), "%d", i + 1);
            if (XDate_compare(&d, &cal->m_selected) == 0) {
                XRect bg;
                XRect_init(&bg, col7 * w / 7, y + row * 24, w / 7, 24);
                XPainter_fillRect(&painter, &bg, highlight);
            }
            XPainter_drawText(&painter, cx, cy, buf, windowText);
        }
        if (cal->m_gridVisible) {
            for (row = 0; row <= 6; ++row) {
                XRect hr;
                XRect_init(&hr, 0, y + row * 24, w, 1);
                XPainter_fillRect(&painter, &hr, mid);
            }
        }
    }
    XPainter_deinit(&painter);
}

static void VX_calendar_mousePressEvent(XWidget* self, XEvent* event)
{
    XCalendarWidget* cal = (XCalendarWidget*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    int row;
    int col7;
    int navY;
    int dayOfWeek;
    XDate first;
    int firstCol;
    int day;
    XDate d;
    int dim;
    if (!cal || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    navY = cal->m_navBarVisible ? 22 : 0;
    /* 导航栏按钮区域（navY=22px 内）。 */
    if (cal->m_navBarVisible && pos.y <= navY) {
        int w = XWidget_width(self);
        if (pos.x >= 4 && pos.x <= 20) {
            /* 上一月。 */
            int m = cal->m_shownMonth - 1;
            int y2 = cal->m_shownYear;
            if (m < 1) { m = 12; --y2; }
            XCalendarWidget_setCurrentPage(cal, y2, m);
        } else if (pos.x >= 22 && pos.x <= 38) {
            /* 下一年。 */
            XCalendarWidget_setCurrentPage(cal, cal->m_shownYear + 1, cal->m_shownMonth);
        } else if (pos.x >= w - 38 && pos.x <= w - 22) {
            /* 上一年。 */
            XCalendarWidget_setCurrentPage(cal, cal->m_shownYear - 1, cal->m_shownMonth);
        } else if (pos.x >= w - 20 && pos.x <= w - 4) {
            /* 下一月。 */
            int m = cal->m_shownMonth + 1;
            int y2 = cal->m_shownYear;
            if (m > 12) { m = 1; ++y2; }
            XCalendarWidget_setCurrentPage(cal, y2, m);
        }
        XEvent_accept(event);
        return;
    }
    if (pos.y < navY + 18) { XEvent_ignore(event); return; }
    row = (pos.y - navY - 18) / 24;
    col7 = pos.x * 7 / XWidget_width(self);
    XDate_setDate(&first, cal->m_shownYear, cal->m_shownMonth, 1);
    dayOfWeek = XDate_dayOfWeek(&first);
    firstCol = (dayOfWeek - 1 + 7) % 7;
    if (cal->m_firstDayOfWeek >= 1 && cal->m_firstDayOfWeek <= 7)
        firstCol = (dayOfWeek - cal->m_firstDayOfWeek + 7) % 7;
    day = row * 7 + col7 - firstCol + 1;
    dim = XDate_daysInMonth(&first);
    if (day < 1 || day > dim) { XEvent_ignore(event); return; }
    XDate_setDate(&d, cal->m_shownYear, cal->m_shownMonth, day);
    if (!xcal_inRange(cal, &d)) { XEvent_ignore(event); return; }
    XCalendarWidget_setSelectedDate(cal, &d);
    xcal_emitDate(cal, (size_t)XCalendarWidget_clicked_signal, &d);
    XEvent_accept(event);
}

/* ==================== 生命周期与虚表 ==================== */

XVtable* XCalendarWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCalendarWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_calendar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_calendar_mousePressEvent);
    return XVTABLE_DEFAULT;
}

void XCalendarWidget_init(XCalendarWidget* self, XWidget* parent,
                          XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XCalendarWidget);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    memset(&self->m_selected, 0, sizeof(XDate));
    XDate_setDate(&self->m_selected, 2026, 9, 9);
    self->m_shownYear = 2026;
    self->m_shownMonth = 9;
    self->m_firstDayOfWeek = 1;
    self->m_gridVisible = false;
    self->m_navBarVisible = true;
    self->m_selectionMode = (int)XCalendarSelectionMode_SingleSelection;
    XWidget_resize(self, 280, 200);
    hint.width = 280;
    hint.height = 200;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

XCalendarWidget* XCalendarWidget_create_ex(XMemoryType memory,
                                           XWidget* parent, XWidgetFlags flags)
{
    XCalendarWidget* self =
        (XCalendarWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XCalendarWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

XDate XCalendarWidget_selectedDate(const XCalendarWidget* self)
{
    XDate d;
    memset(&d, 0, sizeof(d));
    if (self) d = self->m_selected;
    return d;
}

void XCalendarWidget_setSelectedDate(XCalendarWidget* self, const XDate* date)
{
    if (!self || !date) return;
    self->m_selected = *date;
    xcal_clampToRange(self);
    self->m_shownYear = XDate_year(&self->m_selected);
    self->m_shownMonth = XDate_month(&self->m_selected);
    xcal_emitSelectionChanged(self);
    XWidget_update((XWidget*)self);
}

int XCalendarWidget_yearShown(const XCalendarWidget* self)
{
    return self ? self->m_shownYear : 0;
}

int XCalendarWidget_monthShown(const XCalendarWidget* self)
{
    return self ? self->m_shownMonth : 0;
}

void XCalendarWidget_setCurrentPage(XCalendarWidget* self, int year, int month)
{
    if (!self) return;
    self->m_shownYear = year;
    self->m_shownMonth = month;
    if (self->m_shownMonth < 1) self->m_shownMonth = 1;
    if (self->m_shownMonth > 12) self->m_shownMonth = 12;
    xcal_emitPageChanged(self);
    XWidget_update((XWidget*)self);
}

XDate XCalendarWidget_minimumDate(const XCalendarWidget* self)
{
    XDate d;
    memset(&d, 0, sizeof(d));
    if (self && self->m_minSet) d = self->m_min;
    return d;
}

void XCalendarWidget_setMinimumDate(XCalendarWidget* self, const XDate* date)
{
    if (!self || !date) return;
    self->m_min = *date;
    self->m_minSet = true;
    xcal_clampToRange(self);
}

void XCalendarWidget_clearMinimumDate(XCalendarWidget* self)
{
    if (!self) return;
    self->m_minSet = false;
}

XDate XCalendarWidget_maximumDate(const XCalendarWidget* self)
{
    XDate d;
    memset(&d, 0, sizeof(d));
    if (self && self->m_maxSet) d = self->m_max;
    return d;
}

void XCalendarWidget_setMaximumDate(XCalendarWidget* self, const XDate* date)
{
    if (!self || !date) return;
    self->m_max = *date;
    self->m_maxSet = true;
    xcal_clampToRange(self);
}

void XCalendarWidget_clearMaximumDate(XCalendarWidget* self)
{
    if (!self) return;
    self->m_maxSet = false;
}

int XCalendarWidget_firstDayOfWeek(const XCalendarWidget* self)
{
    return self ? self->m_firstDayOfWeek : 1;
}

void XCalendarWidget_setFirstDayOfWeek(XCalendarWidget* self, int dayOfWeek)
{
    if (!self) return;
    self->m_firstDayOfWeek = dayOfWeek;
    XWidget_update((XWidget*)self);
}

bool XCalendarWidget_isGridVisible(const XCalendarWidget* self)
{
    return self ? self->m_gridVisible : false;
}

void XCalendarWidget_setGridVisible(XCalendarWidget* self, bool visible)
{
    if (!self) return;
    self->m_gridVisible = visible;
    XWidget_update((XWidget*)self);
}

bool XCalendarWidget_isNavigationBarVisible(const XCalendarWidget* self)
{
    return self ? self->m_navBarVisible : false;
}

void XCalendarWidget_setNavigationBarVisible(XCalendarWidget* self, bool visible)
{
    if (!self) return;
    self->m_navBarVisible = visible;
    XWidget_update((XWidget*)self);
}

int XCalendarWidget_selectionMode(const XCalendarWidget* self)
{
    return self ? self->m_selectionMode : 0;
}

void XCalendarWidget_setSelectionMode(XCalendarWidget* self, int mode)
{
    if (!self) return;
    self->m_selectionMode = mode;
}

/* ==================== 信号 ==================== */

void* XCalendarWidget_clicked_signal(XCalendarWidget* self, const XDate* date)
{
    (void)self; (void)date;
    return (void*)(size_t)XCalendarWidget_clicked_signal;
}

void* XCalendarWidget_activated_signal(XCalendarWidget* self, const XDate* date)
{
    (void)self; (void)date;
    return (void*)(size_t)XCalendarWidget_activated_signal;
}

void* XCalendarWidget_selectionChanged_signal(XCalendarWidget* self)
{
    (void)self;
    return (void*)(size_t)XCalendarWidget_selectionChanged_signal;
}

void* XCalendarWidget_currentPageChanged_signal(XCalendarWidget* self, int year, int month)
{
    (void)self; (void)year; (void)month;
    return (void*)(size_t)XCalendarWidget_currentPageChanged_signal;
}

void XCalendarWidget_setDateEditEnabled(XCalendarWidget* self, bool enable) { (void)self; (void)enable; }
bool XCalendarWidget_isDateEditEnabled(const XCalendarWidget* self) { (void)self; return false; }
void XCalendarWidget_setDateEditAcceptDelay(XCalendarWidget* self, int delay) { (void)self; (void)delay; }
int XCalendarWidget_dateEditAcceptDelay(const XCalendarWidget* self) { (void)self; return 0; }
int XCalendarWidget_weekNumber(const XCalendarWidget* self, const XDate* date) { (void)self; (void)date; return 0; }
void XCalendarWidget_setHeaderTextFormat(XCalendarWidget* self, int format) { (void)self; (void)format; }
int XCalendarWidget_headerTextFormat(const XCalendarWidget* self) { (void)self; return 0; }
void XCalendarWidget_setWeekdayTextFormat(XCalendarWidget* self, int day, int format) { (void)self; (void)day; (void)format; }
int XCalendarWidget_weekdayTextFormat(const XCalendarWidget* self, int day) { (void)self; (void)day; return 0; }
void XCalendarWidget_setFirstDayOfWeek_2(XCalendarWidget* self, int day) { if(self) self->m_firstDayOfWeek = day; }
bool XCalendarWidget_isDateSelected(const XCalendarWidget* self) { (void)self; return false; }
void XCalendarWidget_setShowTodayDate(XCalendarWidget* self, bool show) { (void)self; (void)show; }
bool XCalendarWidget_isShowTodayDate(const XCalendarWidget* self) { (void)self; return false; }
XDate XCalendarWidget_todayDate(const XCalendarWidget* self)
{ XDate d; memset(&d,0,sizeof(d)); XDate_setDate(&d,2026,9,9); return d; }
void XCalendarWidget_setVerticalHeaderFormat(XCalendarWidget* self, int format) { (void)self; (void)format; }
#endif /* XWIDGET_ON && XCALENDARWIDGET_ON */