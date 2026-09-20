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

/** @brief 分段记号容量上限（默认格式 6 段；含毫秒/星期/上下午的
 *         "yyyy-MM-dd HH:mm:ss.zzz dddd AP" 记 9 段，取 16 覆盖常用格式）。 */
#define XDT_SECTION_MAX 16

/** @brief 分段记号（内部解析产物，渲染/掩码/查询共用一份解析）。 */
typedef struct XdtSectionTok
{
    int  code;  /**< 分段枚举值（XDateTimeEditSection）。 */
    int  width; /**< 数字位数/档位：yyyy=4；MM/dd/HH/mm/ss=2；h=1|2；
                     z=1|2|3；ddd=3/dddd=4（区分星期文案档）；AP/A=1|2。
                     现有记号全为 ASCII，宽度恰等于格式串字节长度。 */
    char spec;  /**< 格式字母（'y''M''d''H''h''m''s''z''A'），渲染分派用。 */
    size_t pos; /**< 记号在格式串中的字节偏移（渲染定位）。 */
} XdtSectionTok;

/** @brief 取控件当前生效格式串（NULL 控件/格式时回退默认格式）。 */
static const char* xdt_effectiveFormat(const XDateTimeEdit* self)
{
    const char* fmt = (self && self->m_displayFormat)
        ? XString_toUtf8(self->m_displayFormat) : NULL;
    return fmt ? fmt : "yyyy-MM-dd HH:mm:ss";
}

/** @brief 解析 displayFormat 的分段记号（唯一解析入口，最长匹配优先，
 *         字面字符原样跳过；对标 Qt 记号连续区语义）。返回记号个数。
 * @note  记号集对标 QDateTimeEdit::setDisplayFormat 常用子集：yyyy/MM/
 *        dd/dddd/ddd/HH/h/hh/mm/ss/zzz/zz/z/AP（A、ap、a 同 AP，Qt 对
 *        上下午记号大小写不敏感）；单字母 d/M/H/m/s 维持字面输出。 */
static int xdt_tokenize(const char* fmt, XdtSectionTok* out, int max)
{
    int n = 0;
    size_t i = 0;
    if (!fmt) fmt = "yyyy-MM-dd HH:mm:ss";
    while (fmt[i] != '\0' && n < max) {
        char c = fmt[i];
        if (c == 'y' && XStrncmp(&fmt[i], "yyyy", 4) == 0) {
            out[n].code = (int)XDateTimeEditSection_YearSection;
            out[n].width = 4;
            out[n].spec = 'y';
            out[n++].pos = i;
            i += 4;
        } else if (c == 'M' && XStrncmp(&fmt[i], "MM", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_MonthSection;
            out[n].width = 2;
            out[n].spec = 'M';
            out[n++].pos = i;
            i += 2;
        } else if (c == 'd' && XStrncmp(&fmt[i], "dddd", 4) == 0) {
            out[n].code = (int)XDateTimeEditSection_DaySection;
            out[n].width = 4;
            out[n].spec = 'd';
            out[n++].pos = i;
            i += 4;
        } else if (c == 'd' && XStrncmp(&fmt[i], "ddd", 3) == 0) {
            out[n].code = (int)XDateTimeEditSection_DaySection;
            out[n].width = 3;
            out[n].spec = 'd';
            out[n++].pos = i;
            i += 3;
        } else if (c == 'd' && XStrncmp(&fmt[i], "dd", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_DaySection;
            out[n].width = 2;
            out[n].spec = 'd';
            out[n++].pos = i;
            i += 2;
        } else if (c == 'H' && XStrncmp(&fmt[i], "HH", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_HourSection;
            out[n].width = 2;
            out[n].spec = 'H';
            out[n++].pos = i;
            i += 2;
        } else if (c == 'h' && XStrncmp(&fmt[i], "hh", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_HourSection;
            out[n].width = 2;
            out[n].spec = 'h';
            out[n++].pos = i;
            i += 2;
        } else if (c == 'h') {
            out[n].code = (int)XDateTimeEditSection_HourSection;
            out[n].width = 1;
            out[n].spec = 'h';
            out[n++].pos = i++;
        } else if (c == 'm' && XStrncmp(&fmt[i], "mm", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_MinuteSection;
            out[n].width = 2;
            out[n].spec = 'm';
            out[n++].pos = i;
            i += 2;
        } else if (c == 's' && XStrncmp(&fmt[i], "ss", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_SecondSection;
            out[n].width = 2;
            out[n].spec = 's';
            out[n++].pos = i;
            i += 2;
        } else if (c == 'z' && XStrncmp(&fmt[i], "zzz", 3) == 0) {
            out[n].code = (int)XDateTimeEditSection_MSecSection;
            out[n].width = 3;
            out[n].spec = 'z';
            out[n++].pos = i;
            i += 3;
        } else if (c == 'z' && XStrncmp(&fmt[i], "zz", 2) == 0) {
            out[n].code = (int)XDateTimeEditSection_MSecSection;
            out[n].width = 2;
            out[n].spec = 'z';
            out[n++].pos = i;
            i += 2;
        } else if (c == 'z') {
            out[n].code = (int)XDateTimeEditSection_MSecSection;
            out[n].width = 1;
            out[n].spec = 'z';
            out[n++].pos = i++;
        } else if ((c == 'A' || c == 'a')
                   && (XStrncmp(&fmt[i], "AP", 2) == 0
                       || XStrncmp(&fmt[i], "ap", 2) == 0)) {
            out[n].code = (int)XDateTimeEditSection_AmPmSection;
            out[n].width = 2;
            out[n].spec = 'A';
            out[n++].pos = i;
            i += 2;
        } else if (c == 'A' || c == 'a') {
            out[n].code = (int)XDateTimeEditSection_AmPmSection;
            out[n].width = 1;
            out[n].spec = 'A';
            out[n++].pos = i++;
        } else {
            ++i;
        }
    }
    return n;
}

/** @brief 统计一段 UTF-8 文本的字符数（XLineEdit 光标/选区按字符索引，
 *         非字节）。 */
static int xdt_utf8Chars(const char* s, int bytes)
{
    int chars = 0;
    int j;
    for (j = 0; j < bytes; ++j) {
        if (((unsigned char)s[j] & 0xC0) != 0x80) ++chars;
    }
    return chars;
}

/** @brief 按单个记号渲染当前值到 buf；返回写入字节数（渲染与
 *         sectionText 共用，保证两路输出一致）。
 * @note  上下午/星期文案固定中文（框架无完整 locale，与库内既有中文化
 *         风格一致；对标 zh_CN 的 amText/pmText 与 dayName）：
 *         ddd=周一..周日，dddd=星期一..星期日，A=上午/下午。 */
static int xdt_renderToken(const XDateTimeEdit* self,
                           const XdtSectionTok* tok,
                           char* buf, size_t cap)
{
    switch (tok->spec) {
    case 'y':
        return XSnprintf(buf, cap, "%04d",
                         XDate_year(&self->m_dateTime.m_date));
    case 'M':
        return XSnprintf(buf, cap, "%02d",
                         XDate_month(&self->m_dateTime.m_date));
    case 'd':
        if (tok->width >= 3) {
            /* XDate_dayOfWeek 1=周一..7=周日；无星期文案起始日偏移。 */
            static const char* const shortNames[7] = {
                "周一", "周二", "周三", "周四", "周五", "周六", "周日" };
            static const char* const longNames[7] = {
                "星期一", "星期二", "星期三", "星期四", "星期五",
                "星期六", "星期日" };
            int dow = XDate_dayOfWeek(&self->m_dateTime.m_date);
            int idx = (dow >= 1 && dow <= 7) ? dow - 1 : 0;
            return XSnprintf(buf, cap, "%s",
                             (tok->width >= 4 ? longNames
                                              : shortNames)[idx]);
        }
        return XSnprintf(buf, cap, "%02d",
                         XDate_day(&self->m_dateTime.m_date));
    case 'H':
        return XSnprintf(buf, cap, "%02d",
                         XTime_hour(&self->m_dateTime.m_time));
    case 'h': {
        /* 12 小时制折算（对标 Qt 含 AP 的 'h'：13→1，0→12）。XGui 固定
         * 折算，不依赖格式是否含 AP（差异已注释，见 h/hh 语义）。 */
        int h = XTime_hour(&self->m_dateTime.m_time);
        h = (h > 12) ? (h - 12) : ((h == 0) ? 12 : h);
        if (tok->width >= 2) return XSnprintf(buf, cap, "%02d", h);
        return XSnprintf(buf, cap, "%d", h);
    }
    case 'm':
        return XSnprintf(buf, cap, "%02d",
                         XTime_minute(&self->m_dateTime.m_time));
    case 's':
        return XSnprintf(buf, cap, "%02d",
                         XTime_second(&self->m_dateTime.m_time));
    case 'z': {
        /* 对标 QLocale::dateTimeToString：毫秒按"秒的小数部分"渲染，
         * 先零填充到 3 位，z/zz 档再截去不保留的尾零（如 ms=200 →
         * z 为 "2"、zz 为 "20"，ms=45 三档均为 "045"）。 */
        int ms = XTime_msec(&self->m_dateTime.m_time);
        int len = XSnprintf(buf, cap, "%03d", ms);
        int chop = 3 - tok->width;
        while (chop-- > 0 && len > 1 && buf[len - 1] == '0') {
            buf[--len] = '\0';
        }
        return len;
    }
    case 'A': {
        int h = XTime_hour(&self->m_dateTime.m_time);
        return XSnprintf(buf, cap, "%s", (h < 12) ? "上午" : "下午");
    }
    default:
        break;
    }
    return 0;
}

/** @brief 分段在编辑文本中的选区（UTF-8 字符索引，供 XLineEdit_setSelection）。 */
typedef struct XdtSectionRange
{
    int start; /**< 选区起点（字符索引）。 */
    int len;   /**< 选区长度（字符数）。 */
} XdtSectionRange;

/** @brief 用当前 displayFormat 将值渲染为编辑文本；ranges 非 NULL 时
 *         顺带输出各分段选区（对标 Qt 编辑时段落整体反选的呈现基础）。 */
static void xdt_render(XDateTimeEdit* self, char* buf, size_t cap,
                       XdtSectionRange* ranges, int maxRanges,
                       int* rangeCount)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    const char* fmt;
    size_t o = 0;
    size_t i = 0;
    int n;
    int ri = 0;
    int chars = 0;
    int ti = 0;
    if (!self) return;
    fmt = xdt_effectiveFormat(self);
    n = xdt_tokenize(fmt, toks, XDT_SECTION_MAX);
    while (fmt[i] != '\0' && o < cap - 1) {
        if (ti < n && i == toks[ti].pos) {
            /* 命中分段记号：整段渲染并记录选区（字符索引）。 */
            char piece[32];
            int k = xdt_renderToken(self, &toks[ti], piece, sizeof(piece));
            int j;
            if (k < 0) k = 0;
            if ((size_t)k > cap - o - 1) k = (int)(cap - o - 1);
            if (ranges && ri < maxRanges) {
                ranges[ri].start = chars;
                ranges[ri].len = xdt_utf8Chars(piece, k);
                ++ri;
            }
            for (j = 0; j < k; ++j) buf[o++] = piece[j];
            chars += xdt_utf8Chars(piece, k);
            i += toks[ti].width; /* 现有记号均为 ASCII：宽度=格式字节长。 */
            ++ti;
        } else {
            /* 字面字符原样输出（UTF-8 多字节按整体计入字符数）。 */
            buf[o++] = fmt[i++];
            if (((unsigned char)buf[o - 1] & 0xC0) != 0x80) ++chars;
        }
    }
    buf[o] = '\0';
    if (rangeCount) *rangeCount = ri;
}

/** @brief 用当前 displayFormat 将值渲染为编辑文本（对标
 *         QDateTimeEditPrivate::textFromDateTime → QLocale::toString）。 */
static void xdt_refreshText(XDateTimeEdit* self)
{
    char buf[128];
    if (!self) return;
    xdt_render(self, buf, sizeof(buf), NULL, 0, NULL);
    /* 更新基类编辑框文本（不经 signals，避免回环）。 */
    {
        XLineEdit* edit = XAbstractSpinBox_lineEdit(
            (XAbstractSpinBox*)self);
        if (edit) XLineEdit_setText(edit, buf);
        XLineEdit_setCursorPosition(edit, 0);
    }
}

/** @brief 整段选中当前分段文本（对标 QDateTimeEdit 编辑时段落整体
 *         反选）；当前分段不在格式中时清除选区保持原状。 */
static void xdt_selectCurrentSection(XDateTimeEdit* self)
{
    char buf[128];
    XdtSectionRange ranges[XDT_SECTION_MAX];
    XdtSectionTok toks[XDT_SECTION_MAX];
    int count = 0;
    int n;
    int index = -1;
    int i;
    XLineEdit* edit;
    if (!self) return;
    xdt_render(self, buf, sizeof(buf), ranges, XDT_SECTION_MAX, &count);
    n = xdt_tokenize(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX);
    for (i = 0; i < n; ++i) {
        if (toks[i].code == self->m_currentSection) { index = i; break; }
    }
    edit = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (!edit) return;
    if (index >= 0 && index < count && ranges[index].len > 0) {
        XLineEdit_setSelection(edit, ranges[index].start, ranges[index].len);
    } else {
        XLineEdit_deselect(edit);
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

/** @brief 发射用户改期信号 userDateChanged(XDate*)（步进路径专用）。 */
static void xdt_emitUserDate(XDateTimeEdit* self)
{
    XDate* ptr = &self->m_dateTime.m_date;
    XVarList* args = XVarList_Create(XVar(XDate*, ptr));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XDateTimeEdit_userDateChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射用户改时信号 userTimeChanged(XTime*)（步进路径专用）。 */
static void xdt_emitUserTime(XDateTimeEdit* self)
{
    XTime* ptr = &self->m_dateTime.m_time;
    XVarList* args = XVarList_Create(XVar(XTime*, ptr));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XDateTimeEdit_userTimeChanged_signal,
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
        /* 简化：日增减经 epoch 毫秒换算；ddd/dddd 星期段同挂 DaySection，
         * ±1 天即星期变化（对标 Qt 星期段步进语义）。 */
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
    } else if (section == (int)XDateTimeEditSection_MSecSection) {
        /* 毫秒段步进：1 毫秒/步（跨秒/分/时的进位由 epoch 换算天然承担）。 */
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps;
        XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    } else if (section == (int)XDateTimeEditSection_AmPmSection) {
        /* 上下午段步进 = 翻转上午/下午（±12 小时，对标 Qt 步进 AmPm 段）。 */
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps * 43200000;
        XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    } else {
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps * 1000;
        XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    }
    xdt_clamp(edit);
    if (XDateTime_compare(&old, &edit->m_dateTime) != 0) {
        xdt_refreshText(edit);
        /* 步进后保持当前分段整体反选（对标 Qt 步进不清除段落选区）。 */
        xdt_selectCurrentSection(edit);
        xdt_emitChanged(edit);
        /* 用户编辑变体（对标 QDateEdit::userDateChanged/QTimeEdit::
         * userTimeChanged）：按日期/时间部分是否变化分别发射。 */
        if (XDate_compare(&old.m_date, &edit->m_dateTime.m_date) != 0)
            xdt_emitUserDate(edit);
        if (XTime_compare(&old.m_time, &edit->m_dateTime.m_time) != 0)
            xdt_emitUserTime(edit);
    }
}

static int XDateTimeEdit_stepEnabled(const XAbstractSpinBox* self)
{
    (void)self;
    return (int)XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
           (int)XAbstractSpinBoxStepEnabledFlag_StepDownEnabled;
}

/** @brief 键盘按下：Left/Right 在分段间移动并整段选中当前段（对标
 *         QDateTimeEdit 方向键跨段导航，段间不循环）；其余按键交基类
 *         （Up/Down/PageUp/PageDown 步进、Home/End 边界、其余转发
 *         内嵌编辑框）。 */
static void XDateTimeEdit_keyPressEvent(XWidget* self, XEvent* event)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XKeyEvent* ke;
    int key;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = XKeyEvent_key(ke);
    if (key == XKey_Left || key == XKey_Right) {
        XdtSectionTok toks[XDT_SECTION_MAX];
        int count = xdt_tokenize(xdt_effectiveFormat(edit), toks,
                                 XDT_SECTION_MAX);
        if (count > 0) {
            int index = XDateTimeEdit_currentSectionIndex(edit);
            int target = (key == XKey_Left)
                ? (index > 0 ? index - 1 : 0)
                : (index + 1 < count ? index + 1 : count - 1);
            /* 段序号 → 段枚举码落地（与 setCurrentSectionIndex 同映射）。 */
            edit->m_currentSection = toks[target].code;
            XWidget_update((XWidget*)self);
            xdt_selectCurrentSection(edit);
            XEvent_accept(event);
            return;
        }
        /* 无分段格式：退回基类，让方向键回到编辑框光标移动。 */
    }
    XClass_Parent(XAbstractSpinBox, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))((XWidget*)self, event);
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
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             XDateTimeEdit_keyPressEvent);
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
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int i;
    int mask = 0;
    if (!self) return 0;
    n = xdt_tokenize(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX);
    for (i = 0; i < n; ++i) mask |= toks[i].code;
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

void* XDateTimeEdit_userDateChanged_signal(XDateTimeEdit* self,
                                           const XDate* date)
{
    (void)self; (void)date;
    return (void*)(size_t)XDateTimeEdit_userDateChanged_signal;
}

void* XDateTimeEdit_userTimeChanged_signal(XDateTimeEdit* self,
                                           const XTime* time)
{
    (void)self; (void)time;
    return (void*)(size_t)XDateTimeEdit_userTimeChanged_signal;
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
    int code;
    if (!self || index < 0) return;
    code = XDateTimeEdit_sectionAt(self, index);
    if (code == (int)XDateTimeEditSection_NoSection) return;
    /* 分段序号与分段枚举码此前共用同一字段：传普通序号会落入非法
     * 分段码，步进定位错段（14.124 扫描 中 项）。现按序号映射为
     * 对应分段码后再落地。 */
    self->m_currentSection = code;
    XWidget_update((XWidget*)self);
}

/* ==================== 分段查询族 ==================== */

int XDateTimeEdit_sectionCount(const XDateTimeEdit* self)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    if (!self) return 0;
    return xdt_tokenize(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX);
}

int XDateTimeEdit_currentSectionIndex(const XDateTimeEdit* self)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int i;
    if (!self) return 0;
    n = xdt_tokenize(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX);
    for (i = 0; i < n; ++i)
        if (toks[i].code == self->m_currentSection) return i;
    return 0;
}

int XDateTimeEdit_sectionAt(const XDateTimeEdit* self, int index)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    if (!self || index < 0) return (int)XDateTimeEditSection_NoSection;
    n = xdt_tokenize(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX);
    if (index >= n) return (int)XDateTimeEditSection_NoSection;
    return toks[index].code;
}

XString* XDateTimeEdit_sectionText(const XDateTimeEdit* self, int section)
{
    XString* out;
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int i;
    bool found = false;
    XdtSectionTok hit;
    char buf[32];
    out = XString_create();
    if (!out) return NULL;
    if (!self) {
        XString_assign_utf8(out, "");
        return out;
    }
    n = xdt_tokenize(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX);
    for (i = 0; i < n; ++i) {
        if (toks[i].code == section) {
            /* 取首个匹配记号（"dd ddd" 同码段时以先出现者为准）。 */
            hit = toks[i];
            found = true;
            break;
        }
    }
    if (found) {
        /* 渲染与整体显示共用 xdt_renderToken：星期/上下午为中文文案，
         * 其余按记号位宽补零。 */
        xdt_renderToken(self, &hit, buf, sizeof(buf));
    } else {
        buf[0] = '\0';
    }
    XString_assign_utf8(out, buf);
    return out;
}

void XDateTimeEdit_setSelectedSection(XDateTimeEdit* self, int section)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int i;
    if (!self) return;
    n = xdt_tokenize(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX);
    for (i = 0; i < n; ++i) {
        if (toks[i].code == section) {
            self->m_currentSection = section;
            XWidget_update((XWidget*)self);
            return;
        }
    }
}


#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */