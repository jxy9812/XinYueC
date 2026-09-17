/**
 * @file       XDateTimeEdit.c
 * @brief      日期时间编辑控件实现（对标 Qt 6.8 QDateTimeEdit 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XDateTimeEdit.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XLineEdit.h"
#include "XCalendarWidget.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include <stdio.h>

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON

/* ==================== 内部工具 ==================== */

/** @brief 用当前 displayFormat 将值渲染为编辑文本。 */
static void xdt_refreshText(XDateTimeEdit* self)
{
    char buf[128];
    const char* fmt = (self->m_displayFormat
                       ? XString_toUtf8(self->m_displayFormat) : NULL);
    if (!fmt) fmt = "yyyy-MM-dd HH:mm:ss";
    size_t o = 0;
    size_t i = 0;
    if (!self) return;
    while (fmt[i] != '\0' && o < sizeof(buf) - 1) {
        if (XStrncmp(&fmt[i], "yyyy", 4) == 0) {
            o += (size_t)XSnprintf(&buf[o], sizeof(buf) - o, "%04d",
                                  XDate_year(&self->m_dateTime.m_date));
            i += 4;
        } else if (XStrncmp(&fmt[i], "MM", 2) == 0) {
            o += (size_t)XSnprintf(&buf[o], sizeof(buf) - o, "%02d",
                                  XDate_month(&self->m_dateTime.m_date));
            i += 2;
        } else if (XStrncmp(&fmt[i], "dd", 2) == 0) {
            o += (size_t)XSnprintf(&buf[o], sizeof(buf) - o, "%02d",
                                  XDate_day(&self->m_dateTime.m_date));
            i += 2;
        } else if (XStrncmp(&fmt[i], "HH", 2) == 0) {
            o += (size_t)XSnprintf(&buf[o], sizeof(buf) - o, "%02d",
                                  XTime_hour(&self->m_dateTime.m_time));
            i += 2;
        } else if (XStrncmp(&fmt[i], "mm", 2) == 0) {
            o += (size_t)XSnprintf(&buf[o], sizeof(buf) - o, "%02d",
                                  XTime_minute(&self->m_dateTime.m_time));
            i += 2;
        } else if (XStrncmp(&fmt[i], "ss", 2) == 0) {
            o += (size_t)XSnprintf(&buf[o], sizeof(buf) - o, "%02d",
                                  XTime_second(&self->m_dateTime.m_time));
            i += 2;
        } else {
            buf[o++] = fmt[i++];
        }
    }
    buf[o] = '\0';
    /* 更新基类编辑框文本（不经 signals，避免回环）。 */
    {
        XLineEdit* edit = XAbstractSpinBox_lineEdit(
            (XAbstractSpinBox*)self);
        if (edit) XLineEdit_setText(edit, buf);
        XLineEdit_setCursorPosition(edit, 0);
    }
}

static void xdt_clamp(XDateTimeEdit* self)
{
    if (XDateTime_compare(&self->m_dateTime, &self->m_minimum) < 0)
        self->m_dateTime = self->m_minimum;
    if (XDateTime_compare(&self->m_dateTime, &self->m_maximum) > 0)
        self->m_dateTime = self->m_maximum;
}

static void xdt_emitChanged(XDateTimeEdit* self)
{
    XDateTime* ptr = &self->m_dateTime;
    XVarList* args = XVarList_Create(
        XVar(XDateTime*, ptr));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XDateTimeEdit_dateTimeChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 虚槽重载 ==================== */

static void XDateTimeEdit_stepBy(XAbstractSpinBox* self, int steps)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XDateTime old;
    int section;
    if (!edit || steps == 0) return;
    old = edit->m_dateTime;
    section = edit->m_currentSection;
    if (section == (int)XDateTimeEditSection_YearSection) {
        int y = XDate_year(&edit->m_dateTime.m_date) + steps;
        XDate_setDate(&edit->m_dateTime.m_date, y,
                      XDate_month(&edit->m_dateTime.m_date),
                      XDate_day(&edit->m_dateTime.m_date));
    } else if (section == (int)XDateTimeEditSection_MonthSection) {
        int m = XDate_month(&edit->m_dateTime.m_date) + steps;
        int y = XDate_year(&edit->m_dateTime.m_date);
        while (m > 12) { m -= 12; ++y; }
        while (m < 1) { m += 12; --y; }
        XDate_setDate(&edit->m_dateTime.m_date, y, m,
                      XDate_day(&edit->m_dateTime.m_date));
    } else if (section == (int)XDateTimeEditSection_DaySection) {
        /* 简化：日增减经 epoch 毫秒换算。 */
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps * 86400000;
        XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    } else if (section == (int)XDateTimeEditSection_HourSection) {
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps * 3600000;
        XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    } else if (section == (int)XDateTimeEditSection_MinuteSection) {
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps * 60000;
        XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    } else {
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps * 1000;
        XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    }
    xdt_clamp(edit);
    if (XDateTime_compare(&old, &edit->m_dateTime) != 0) {
        xdt_refreshText(edit);
        xdt_emitChanged(edit);
    }
}

static int XDateTimeEdit_stepEnabled(const XAbstractSpinBox* self)
{
    (void)self;
    return (int)XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
           (int)XAbstractSpinBoxStepEnabledFlag_StepDownEnabled;
}

/* ==================== 生命周期与虚表 ==================== */

static void VXDateTimeEdit_deinit(XDateTimeEdit* self)
{
    if (!self) return;
    if (self->m_displayFormat) {
        XString_delete_base((XClass*)self->m_displayFormat);
        self->m_displayFormat = NULL;
    }
#if XCALENDARWIDGET_ON
    /* 释放内置/接管的日历控件（对象由本控件拥有）。 */
    if (self->m_calendar) {
        XCalendarWidget_delete_base((XClass*)self->m_calendar);
        self->m_calendar = NULL;
    }
#endif
    XClass_Deinit_Parent(XAbstractSpinBox, (XAbstractSpinBox*)self);
}

XVtable* XDateTimeEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDateTimeEdit)
    XVTABLE_INHERIT_XCLASS(XAbstractSpinBox);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepBy,
                             XDateTimeEdit_stepBy);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepEnabled,
                             XDateTimeEdit_stepEnabled);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDateTimeEdit_deinit);
    return XVTABLE_DEFAULT;
}

void XDateTimeEdit_init(XDateTimeEdit* self, XWidget* parent,
                        XWidgetFlags flags)
{
    XDateTime now;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSpinBox_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDateTimeEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_dateTime = XDateTime_create();
    XDateTime_setMSecsSinceEpoch(&self->m_dateTime,
        XDateTime_currentMSecsSinceEpoch());
    now = self->m_dateTime;
    self->m_minimum = XDateTime_create();
    XDate_setDate(&self->m_minimum.m_date, 1900, 1, 1);
    XTime_setHMS(&self->m_minimum.m_time, 0, 0, 0, 0);
    self->m_maximum = XDateTime_create();
    XDate_setDate(&self->m_maximum.m_date, 2999, 12, 31);
    XTime_setHMS(&self->m_maximum.m_time, 23, 59, 59, 999);
    self->m_displayFormat = XString_create_utf8("yyyy-MM-dd HH:mm:ss");
    self->m_currentSection =
        (int)XDateTimeEditSection_YearSection;
    /* 以当前时间刷新编辑框文本（无信号）。 */
    (void)now;
    xdt_refreshText(self);

    self->m_calendarPopup = true;
    self->m_timeSpec = 0;
}

XDateTimeEdit* XDateTimeEdit_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags)
{
    XDateTimeEdit* self =
        (XDateTimeEdit*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDateTimeEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

void XDateTimeEdit_setDateTime(XDateTimeEdit* self,
                               const XDateTime* dateTime)
{
    if (!self || !dateTime) return;
    self->m_dateTime = *dateTime;
    xdt_clamp(self);
    xdt_refreshText(self);
    xdt_emitChanged(self);
}

const XDateTime* XDateTimeEdit_dateTime(const XDateTimeEdit* self)
{
    return self ? &self->m_dateTime : NULL;
}

void XDateTimeEdit_setDate(XDateTimeEdit* self, const XDate* date)
{
    if (!self || !date) return;
    XDateTime_setDate(&self->m_dateTime, *date);
    xdt_clamp(self);
    xdt_refreshText(self);
    xdt_emitChanged(self);
}

XDate XDateTimeEdit_date(const XDateTimeEdit* self)
{
    XDate d;
    XMemset(&d, 0, sizeof(d));
    if (self) d = self->m_dateTime.m_date;
    return d;
}

void XDateTimeEdit_setTime(XDateTimeEdit* self, const XTime* time)
{
    if (!self || !time) return;
    XDateTime_setTime(&self->m_dateTime, *time);
    xdt_clamp(self);
    xdt_refreshText(self);
    xdt_emitChanged(self);
}

XTime XDateTimeEdit_time(const XDateTimeEdit* self)
{
    XTime t;
    XMemset(&t, 0, sizeof(t));
    if (self) t = self->m_dateTime.m_time;
    return t;
}

const XDateTime* XDateTimeEdit_minimumDateTime(const XDateTimeEdit* self)
{
    return self ? &self->m_minimum : NULL;
}

void XDateTimeEdit_setMinimumDateTime(XDateTimeEdit* self,
                                      const XDateTime* dateTime)
{
    if (!self || !dateTime) return;
    self->m_minimum = *dateTime;
    xdt_clamp(self);
}

const XDateTime* XDateTimeEdit_maximumDateTime(const XDateTimeEdit* self)
{
    return self ? &self->m_maximum : NULL;
}

void XDateTimeEdit_setMaximumDateTime(XDateTimeEdit* self,
                                      const XDateTime* dateTime)
{
    if (!self || !dateTime) return;
    self->m_maximum = *dateTime;
    xdt_clamp(self);
}

XDate XDateTimeEdit_minimumDate(const XDateTimeEdit* self)
{
    XDate d;
    XMemset(&d, 0, sizeof(d));
    if (self) d = XDateTime_date(&self->m_minimum);
    return d;
}

void XDateTimeEdit_setMinimumDate(XDateTimeEdit* self, const XDate* date)
{
    XDateTime tmp;
    if (!self || !date) return;
    tmp = self->m_minimum;
    XDateTime_setDate(&tmp, *date);
    XDateTimeEdit_setMinimumDateTime(self, &tmp);
}

XDate XDateTimeEdit_maximumDate(const XDateTimeEdit* self)
{
    XDate d;
    XMemset(&d, 0, sizeof(d));
    if (self) d = XDateTime_date(&self->m_maximum);
    return d;
}

void XDateTimeEdit_setMaximumDate(XDateTimeEdit* self, const XDate* date)
{
    XDateTime tmp;
    if (!self || !date) return;
    tmp = self->m_maximum;
    XDateTime_setDate(&tmp, *date);
    XDateTimeEdit_setMaximumDateTime(self, &tmp);
}

XTime XDateTimeEdit_minimumTime(const XDateTimeEdit* self)
{
    XTime t;
    XMemset(&t, 0, sizeof(t));
    if (self) t = XDateTime_time(&self->m_minimum);
    return t;
}

void XDateTimeEdit_setMinimumTime(XDateTimeEdit* self, const XTime* time)
{
    XDateTime tmp;
    if (!self || !time) return;
    tmp = self->m_minimum;
    XDateTime_setTime(&tmp, *time);
    XDateTimeEdit_setMinimumDateTime(self, &tmp);
}

XTime XDateTimeEdit_maximumTime(const XDateTimeEdit* self)
{
    XTime t;
    XMemset(&t, 0, sizeof(t));
    if (self) t = XDateTime_time(&self->m_maximum);
    return t;
}

void XDateTimeEdit_setMaximumTime(XDateTimeEdit* self, const XTime* time)
{
    XDateTime tmp;
    if (!self || !time) return;
    tmp = self->m_maximum;
    XDateTime_setTime(&tmp, *time);
    XDateTimeEdit_setMaximumDateTime(self, &tmp);
}

void XDateTimeEdit_clearMinimumDate(XDateTimeEdit* self)
{
    XDate d;
    if (!self) return;
    XDate_setDate(&d, 1900, 1, 1);
    XDateTimeEdit_setMinimumDate(self, &d);
}

void XDateTimeEdit_clearMaximumDate(XDateTimeEdit* self)
{
    XDate d;
    if (!self) return;
    XDate_setDate(&d, 2999, 12, 31);
    XDateTimeEdit_setMaximumDate(self, &d);
}

void XDateTimeEdit_clearMinimumTime(XDateTimeEdit* self)
{
    XTime t;
    if (!self) return;
    XTime_setHMS(&t, 0, 0, 0, 0);
    XDateTimeEdit_setMinimumTime(self, &t);
}

void XDateTimeEdit_clearMaximumTime(XDateTimeEdit* self)
{
    XTime t;
    if (!self) return;
    XTime_setHMS(&t, 23, 59, 59, 999);
    XDateTimeEdit_setMaximumTime(self, &t);
}

void XDateTimeEdit_clearMinimumDateTime(XDateTimeEdit* self)
{
    XDateTime dt;
    if (!self) return;
    XDate_setDate(&dt.m_date, 1900, 1, 1);
    XTime_setHMS(&dt.m_time, 0, 0, 0, 0);
    XDateTimeEdit_setMinimumDateTime(self, &dt);
}

void XDateTimeEdit_clearMaximumDateTime(XDateTimeEdit* self)
{
    XDateTime dt;
    if (!self) return;
    XDate_setDate(&dt.m_date, 2999, 12, 31);
    XTime_setHMS(&dt.m_time, 23, 59, 59, 999);
    XDateTimeEdit_setMaximumDateTime(self, &dt);
}

void XDateTimeEdit_setDateRange(XDateTimeEdit* self, const XDate* min,
                                const XDate* max)
{
    if (min) XDateTimeEdit_setMinimumDate(self, min);
    if (max) XDateTimeEdit_setMaximumDate(self, max);
}

void XDateTimeEdit_setTimeRange(XDateTimeEdit* self, const XTime* min,
                                const XTime* max)
{
    if (min) XDateTimeEdit_setMinimumTime(self, min);
    if (max) XDateTimeEdit_setMaximumTime(self, max);
}

void XDateTimeEdit_setDateTimeRange(XDateTimeEdit* self,
                                    const XDateTime* min,
                                    const XDateTime* max)
{
    if (min) XDateTimeEdit_setMinimumDateTime(self, min);
    if (max) XDateTimeEdit_setMaximumDateTime(self, max);
}

void XDateTimeEdit_setDisplayFormat(XDateTimeEdit* self,
                                    const char* utf8)
{
    if (!self) return;
    if (!self->m_displayFormat) self->m_displayFormat = XString_create();
    if (self->m_displayFormat)
        XString_assign_utf8(self->m_displayFormat, utf8 ? utf8 : "");
    xdt_refreshText(self);
}

const char* XDateTimeEdit_displayFormat(const XDateTimeEdit* self)
{
    {
        const char* text;
        if (!self || !self->m_displayFormat) return "";
        text = XString_toUtf8(self->m_displayFormat);
        return text ? text : "";
    }
}

int XDateTimeEdit_currentSection(const XDateTimeEdit* self)
{
    return self ? self->m_currentSection : 0;
}

void XDateTimeEdit_setCurrentSection(XDateTimeEdit* self, int section)
{
    if (!self) return;
    self->m_currentSection = section;
}

int XDateTimeEdit_sections(const XDateTimeEdit* self)
{
    int mask = 0;
    const char* fmt;
    size_t i = 0;
    if (!self) return 0;
    fmt = (self->m_displayFormat ? XString_toUtf8(self->m_displayFormat) : NULL);
    if (!fmt) fmt = "yyyy-MM-dd HH:mm:ss";
    while (fmt[i] != '\0') {
        if (XStrncmp(&fmt[i], "yyyy", 4) == 0) {
            mask |= (int)XDateTimeEditSection_YearSection; i += 4;
        } else if (XStrncmp(&fmt[i], "MM", 2) == 0) {
            mask |= (int)XDateTimeEditSection_MonthSection; i += 2;
        } else if (XStrncmp(&fmt[i], "dd", 2) == 0) {
            mask |= (int)XDateTimeEditSection_DaySection; i += 2;
        } else if (XStrncmp(&fmt[i], "HH", 2) == 0) {
            mask |= (int)XDateTimeEditSection_HourSection; i += 2;
        } else if (XStrncmp(&fmt[i], "mm", 2) == 0) {
            mask |= (int)XDateTimeEditSection_MinuteSection; i += 2;
        } else if (XStrncmp(&fmt[i], "ss", 2) == 0) {
            mask |= (int)XDateTimeEditSection_SecondSection; i += 2;
        } else {
            ++i;
        }
    }
    return mask;
}

/* ==================== 信号 ==================== */

void* XDateTimeEdit_dateTimeChanged_signal(XDateTimeEdit* self,
                                           const XDateTime* dateTime)
{
    (void)self; (void)dateTime;
    return (void*)(size_t)XDateTimeEdit_dateTimeChanged_signal;
}

void* XDateTimeEdit_dateChanged_signal(XDateTimeEdit* self,
                                       const XDate* date)
{
    (void)self; (void)date;
    return (void*)(size_t)XDateTimeEdit_dateChanged_signal;
}

void* XDateTimeEdit_timeChanged_signal(XDateTimeEdit* self,
                                       const XTime* time)
{
    (void)self; (void)time;
    return (void*)(size_t)XDateTimeEdit_timeChanged_signal;
}



















/* ==================== Task 2.5：日历弹出/时区/分段 ==================== */

void XDateTimeEdit_setCalendarPopup(XDateTimeEdit* self, bool popup)
{ if (self) self->m_calendarPopup = popup; }
bool XDateTimeEdit_calendarPopup(const XDateTimeEdit* self)
{ return self ? self->m_calendarPopup : true; }

#if XCALENDARWIDGET_ON

/* ==================== calendarWidget 族（对标 QDateTimeEdit::
 *                     calendarWidget / setCalendarWidget；聚合思路
 *                     与 QComboBox::view/setView 一致） ==================== */

/** @brief 日历选中变化联动槽：把日历选中日期回填到编辑框（单向同步，
 *         setDate 不回写日历，故无回环）。 */
static void xdt_calendarSelectionSlot(XObject* receiver, XVarList* args)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)receiver;
    XDate d;
    (void)args;
    if (!edit || !edit->m_calendar) return;
    d = XCalendarWidget_selectedDate(edit->m_calendar);
    XDateTimeEdit_setDate(edit, &d);
}

XCalendarWidget* XDateTimeEdit_calendarWidget(const XDateTimeEdit* self)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XCalendarWidget* cal;
    if (!edit) return NULL;
    if (edit->m_calendar) return edit->m_calendar;
    /* 懒创建：保持 NULL 父控件（对象由本控件持有，deinit 释放）。 */
    cal = XCalendarWidget_create(NULL, 0);
    if (!cal) return NULL;
    edit->m_calendar = cal;
    /* 以当前日期初始化日历选中态（不触发编辑框信号）。 */
    XCalendarWidget_setSelectedDate(cal, &edit->m_dateTime.m_date);
    /* 连接日历选中信号 → setDate 联动槽。 */
    XObject_connect_1((XObject*)cal,
        (size_t)XCalendarWidget_selectionChanged_signal(cal),
        (XObject*)edit, xdt_calendarSelectionSlot,
        XConnectionType_Direct);
    return cal;
}

void XDateTimeEdit_setCalendarWidget(XDateTimeEdit* self,
                                     XCalendarWidget* calendar)
{
    if (!self || calendar == self->m_calendar) return;
    /* 取得所有权：先释放旧的内置/接管日历。 */
    if (self->m_calendar) {
        XCalendarWidget_delete_base((XClass*)self->m_calendar);
        self->m_calendar = NULL;
    }
    self->m_calendar = calendar;
    if (calendar) {
        /* 以外部日历的选中日期回填本控件（触发 dateChanged 等信号）。 */
        XDate d = XCalendarWidget_selectedDate(calendar);
        XDateTimeEdit_setDate(self, &d);
        /* 连接选中信号 → setDate 联动槽。 */
        XObject_connect_1((XObject*)calendar,
            (size_t)XCalendarWidget_selectionChanged_signal(calendar),
            (XObject*)self, xdt_calendarSelectionSlot,
            XConnectionType_Direct);
    }
}

#endif /* XCALENDARWIDGET_ON */

void XDateTimeEdit_setTimeSpec(XDateTimeEdit* self, int spec)
{ if (self) self->m_timeSpec = spec; }
int XDateTimeEdit_timeSpec(const XDateTimeEdit* self)
{ return self ? self->m_timeSpec : 0; }

void XDateTimeEdit_setCurrentSectionIndex(XDateTimeEdit* self, int index)
{
    if (self && index >= 0) {
        self->m_currentSection = index;
        XWidget_update((XWidget*)self);
    }
}

/* ==================== 分段查询族 ==================== */

/** @brief 分段记号（内部解析产物）。 */
typedef struct XdtSectionTok
{
    int code;    /**< 分段枚举值（XDateTimeEditSection）。 */
    int width;   /**< 渲染宽度：年份 4 位，其余 2 位。 */
} XdtSectionTok;

/** @brief 解析 displayFormat 中可识别的分段记号（与 xdt_refreshText
 *         记号集一致；最多 8 个）。返回记号个数。 */
static int xdt_parseSections(const char* fmt, XdtSectionTok* out, int max)
{
    int n = 0;
    size_t i = 0;
    if (!fmt) fmt = "yyyy-MM-dd HH:mm:ss";
    while (fmt[i] != '\0' && n < max) {
        if (XStrncmp(&fmt[i], "yyyy", 4) == 0) {
            out[n].code = (int)XDateTimeEditSection_YearSection;
            out[n++].width = 4;
            i += 4;
        } else if (XStrncmp(&fmt[i], "MM", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_MonthSection;
            out[n++].width = 2;
            i += 2;
        } else if (XStrncmp(&fmt[i], "dd", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_DaySection;
            out[n++].width = 2;
            i += 2;
        } else if (XStrncmp(&fmt[i], "HH", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_HourSection;
            out[n++].width = 2;
            i += 2;
        } else if (XStrncmp(&fmt[i], "mm", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_MinuteSection;
            out[n++].width = 2;
            i += 2;
        } else if (XStrncmp(&fmt[i], "ss", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_SecondSection;
            out[n++].width = 2;
            i += 2;
        } else {
            ++i;
        }
    }
    return n;
}

/** @brief 取控件当前生效格式串（NULL 控件/格式时回退默认格式）。 */
static const char* xdt_effectiveFormat(const XDateTimeEdit* self)
{
    const char* fmt = (self && self->m_displayFormat)
        ? XString_toUtf8(self->m_displayFormat) : NULL;
    return fmt ? fmt : "yyyy-MM-dd HH:mm:ss";
}

int XDateTimeEdit_sectionCount(const XDateTimeEdit* self)
{
    XdtSectionTok toks[8];
    if (!self) return 0;
    return xdt_parseSections(xdt_effectiveFormat(self), toks, 8);
}

int XDateTimeEdit_sectionAt(const XDateTimeEdit* self, int index)
{
    XdtSectionTok toks[8];
    int n;
    if (!self || index < 0) return (int)XDateTimeEditSection_NoSection;
    n = xdt_parseSections(xdt_effectiveFormat(self), toks, 8);
    if (index >= n) return (int)XDateTimeEditSection_NoSection;
    return toks[index].code;
}

XString* XDateTimeEdit_sectionText(const XDateTimeEdit* self, int section)
{
    XString* out;
    XdtSectionTok toks[8];
    int n;
    int i;
    int value = 0;
    bool found = false;
    int width = 2;
    char buf[16];
    out = XString_create();
    if (!out) return NULL;
    if (!self) {
        XString_assign_utf8(out, "");
        return out;
    }
    n = xdt_parseSections(xdt_effectiveFormat(self), toks, 8);
    for (i = 0; i < n; ++i) {
        if (toks[i].code == section) {
            found = true;
            width = toks[i].width;
            break;
        }
    }
    if (found) {
        switch ((XDateTimeEditSection)section) {
        case XDateTimeEditSection_YearSection:
            value = XDate_year(&self->m_dateTime.m_date); break;
        case XDateTimeEditSection_MonthSection:
            value = XDate_month(&self->m_dateTime.m_date); break;
        case XDateTimeEditSection_DaySection:
            value = XDate_day(&self->m_dateTime.m_date); break;
        case XDateTimeEditSection_HourSection:
            value = XTime_hour(&self->m_dateTime.m_time); break;
        case XDateTimeEditSection_MinuteSection:
            value = XTime_minute(&self->m_dateTime.m_time); break;
        case XDateTimeEditSection_SecondSection:
            value = XTime_second(&self->m_dateTime.m_time); break;
        default:
            found = false; break;
        }
    }
    if (found)
        XSnprintf(buf, sizeof(buf), "%0*d", width, value);
    else
        XSnprintf(buf, sizeof(buf), "");
    XString_assign_utf8(out, buf);
    return out;
}

void XDateTimeEdit_setSelectedSection(XDateTimeEdit* self, int section)
{
    XdtSectionTok toks[8];
    int n;
    int i;
    if (!self) return;
    n = xdt_parseSections(xdt_effectiveFormat(self), toks, 8);
    for (i = 0; i < n; ++i) {
        if (toks[i].code == section) {
            self->m_currentSection = section;
            XWidget_update((XWidget*)self);
            return;
        }
    }
}


#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */