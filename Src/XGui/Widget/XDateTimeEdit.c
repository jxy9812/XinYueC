/**
 * @file       XDateTimeEdit.c
 * @brief      日期时间编辑控件实现（对标 Qt 6.8 QDateTimeEdit 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XDateTimeEdit.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XLineEdit.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON

/* ==================== 内部工具 ==================== */

/** @brief 用当前 displayFormat 将值渲染为编辑文本。 */
static void xdt_refreshText(XDateTimeEdit* self)
{
    char buf[128];
    const char* fmt = self->m_displayFormat;
    size_t o = 0;
    size_t i = 0;
    if (!self) return;
    while (fmt[i] != '\0' && o < sizeof(buf) - 1) {
        if (strncmp(&fmt[i], "yyyy", 4) == 0) {
            o += (size_t)snprintf(&buf[o], sizeof(buf) - o, "%04d",
                                  XDate_year(&self->m_dateTime.m_date));
            i += 4;
        } else if (strncmp(&fmt[i], "MM", 2) == 0) {
            o += (size_t)snprintf(&buf[o], sizeof(buf) - o, "%02d",
                                  XDate_month(&self->m_dateTime.m_date));
            i += 2;
        } else if (strncmp(&fmt[i], "dd", 2) == 0) {
            o += (size_t)snprintf(&buf[o], sizeof(buf) - o, "%02d",
                                  XDate_day(&self->m_dateTime.m_date));
            i += 2;
        } else if (strncmp(&fmt[i], "HH", 2) == 0) {
            o += (size_t)snprintf(&buf[o], sizeof(buf) - o, "%02d",
                                  XTime_hour(&self->m_dateTime.m_time));
            i += 2;
        } else if (strncmp(&fmt[i], "mm", 2) == 0) {
            o += (size_t)snprintf(&buf[o], sizeof(buf) - o, "%02d",
                                  XTime_minute(&self->m_dateTime.m_time));
            i += 2;
        } else if (strncmp(&fmt[i], "ss", 2) == 0) {
            o += (size_t)snprintf(&buf[o], sizeof(buf) - o, "%02d",
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

XVtable* XDateTimeEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDateTimeEdit)
    XVTABLE_INHERIT_XCLASS(XAbstractSpinBox);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepBy,
                             XDateTimeEdit_stepBy);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepEnabled,
                             XDateTimeEdit_stepEnabled);
    return XVTABLE_DEFAULT;
}

void XDateTimeEdit_init(XDateTimeEdit* self, XWidget* parent,
                        XWidgetFlags flags)
{
    XDateTime now;
    if (!self) return;
    memset(self, 0, sizeof(*self));
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
    strcpy(self->m_displayFormat, "yyyy-MM-dd HH:mm:ss");
    self->m_currentSection =
        (int)XDateTimeEditSection_YearSection;
    /* 以当前时间刷新编辑框文本（无信号）。 */
    (void)now;
    xdt_refreshText(self);
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
    memset(&d, 0, sizeof(d));
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
    memset(&t, 0, sizeof(t));
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

void XDateTimeEdit_setDisplayFormat(XDateTimeEdit* self,
                                    const char* utf8)
{
    if (!self) return;
    strncpy(self->m_displayFormat, utf8 ? utf8 : "",
            sizeof(self->m_displayFormat) - 1);
    self->m_displayFormat[sizeof(self->m_displayFormat) - 1] = '\0';
    xdt_refreshText(self);
}

const char* XDateTimeEdit_displayFormat(const XDateTimeEdit* self)
{
    return self ? self->m_displayFormat : "";
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
    fmt = self->m_displayFormat;
    while (fmt[i] != '\0') {
        if (strncmp(&fmt[i], "yyyy", 4) == 0) {
            mask |= (int)XDateTimeEditSection_YearSection; i += 4;
        } else if (strncmp(&fmt[i], "MM", 2) == 0) {
            mask |= (int)XDateTimeEditSection_MonthSection; i += 2;
        } else if (strncmp(&fmt[i], "dd", 2) == 0) {
            mask |= (int)XDateTimeEditSection_DaySection; i += 2;
        } else if (strncmp(&fmt[i], "HH", 2) == 0) {
            mask |= (int)XDateTimeEditSection_HourSection; i += 2;
        } else if (strncmp(&fmt[i], "mm", 2) == 0) {
            mask |= (int)XDateTimeEditSection_MinuteSection; i += 2;
        } else if (strncmp(&fmt[i], "ss", 2) == 0) {
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

#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */