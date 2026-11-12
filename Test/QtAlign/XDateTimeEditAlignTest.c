/**
 * @file       XDateTimeEditAlignTest.c
 * @brief      XDateTimeEdit × Qt 6.8 QDateTimeEdit 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/
 *             qdatetimeedit.h 与 Qt 6.8 源码
 *             src/widgets/widgets/qdatetimeedit.cpp；关键行为用本机
 *             Qt 6.8.0（系统 Qt，offscreen）探针实测：
 *             - 分段枚举数值：Year=0x400/Month=0x200/Day=0x100/
 *               Hour=0x10/Minute=0x8/Second=0x4（与 Qt 逐值一致）；
 *               sectionAt(0..) 按显示格式顺序映射，越界 NoSection；
 *             - sectionText 按记号位宽渲染（"yyyy-MM-dd HH:mm:ss" 实测
 *               Y=2024/M=03/d=05/H=14/m=25/s=36），未出现的分段为空串；
 *             - setCurrentSection 对「不在格式中的分段」不动作
 *               （qdatetimeedit.cpp:717-721；实测 MSec 不在默认格式时
 *               仍停在 Year）；
 *             - stepBy 按当前分段增减（实测月段 +1、年段 -1）；
 *             - setDisplayFormat 纯时间段收窄：日期范围收到值日期、时间
 *               范围复位整天（实测 displayedSections=0x18）；
 *               纯日期段：时间范围复位整天且值时间清零；
 *             - 范围钳位：setDate 越界钳到 minimum/maximum；
 *             - 信号：dateTimeChanged/dateChanged/timeChanged 按「实际
 *               变化」发射（EmitIfChanged，qdatetimeedit.cpp:2289-2312），
 *               同值 set 零信号；
 *             - stepEnabled（qdatetimeedit.cpp:1514-1575）：readOnly 全禁、
 *               specialValue 只开上步、min==max 全禁、wrapping 双向、
 *               否则按当前分段做 ±1 步 test-mode 试探（实测默认 3、
 *               at min=1、at max=2、readOnly=0、min==max=0、wrapping=3）。
 * @note        本文件全部断言按 Qt 6.8 口径（默认格式/初值/范围与
 *             stepBy 分段数字级语义均已对齐，见 XDateTimeEdit.c 注释）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON

#include "XDateTimeEdit.h"
#include "XAbstractSpinBox.h"
#include "XLineEdit.h"
#include "XDateTime.h"
#include "XWidget.h"
#include "XObject.h"
#include "XString.h"

#include <stdio.h>

/** @brief 三信号计数（用例内清零）。 */
static int xq_dt_dateTimeChanged = 0;
static int xq_dt_dateChanged = 0;
static int xq_dt_timeChanged = 0;

static void xq_dt_onDateTimeChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_dt_dateTimeChanged;
}

static void xq_dt_onDateChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_dt_dateChanged;
}

static void xq_dt_onTimeChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_dt_timeChanged;
}

/** @brief 取分段文本到调用方缓冲（XString 空串 toUtf8 返回 NULL，归一 ""）。 */
static void xq_dt_sectionText(const XDateTimeEdit* e, int section,
                              char* out, size_t cap)
{
    XString* s = XDateTimeEdit_sectionText(e, section);
    const char* u = s ? XString_toUtf8(s) : NULL;
    if (out && cap > 0) {
        size_t n = u ? strlen(u) : 0;
        if (n >= cap) n = cap - 1;
        if (n) memcpy(out, u, n);
        out[n] = '\0';
    }
    if (s) XClassDelete(s);
}

/** @brief 构造默认值（对齐 Qt 6.8 QDateTimeEdit 构造口径）。 */
XQ_TEST(XDateTimeEdit, constructorDefaultsAndSections)
{
    XDateTimeEdit* e = XDateTimeEdit_create(NULL, 0);
    const XDateTime* value;
    XQ_REQUIRE(e != NULL);

    /* 默认格式 = 缺省区域短日期 + 短时间（本机 Qt 6.8 探针实测
     * 'yyyy/M/d HH:mm'，displayedSections=0x718、sectionCount=5）。 */
    XQ_EQ_STR(XDateTimeEdit_displayFormat(e), "yyyy/M/d HH:mm");
    /* 默认范围 [1752-09-14, 9999-12-31]（Qt 6.8 探针实测）。 */
    {
        XDate dmin = XDateTimeEdit_minimumDate(e);
        XDate dmax = XDateTimeEdit_maximumDate(e);
        XTime tmin = XDateTimeEdit_minimumTime(e);
        XTime tmax = XDateTimeEdit_maximumTime(e);
        XQ_EQ_I64(XDate_year(&dmin), 1752);
        XQ_EQ_I64(XDate_month(&dmin), 9);
        XQ_EQ_I64(XDate_day(&dmin), 14);
        XQ_EQ_I64(XTime_hour(&tmin), 0);
        XQ_EQ_I64(XTime_msec(&tmin), 0);
        XQ_EQ_I64(XDate_year(&dmax), 9999);
        XQ_EQ_I64(XDate_month(&dmax), 12);
        XQ_EQ_I64(XDate_day(&dmax), 31);
        XQ_EQ_I64(XTime_hour(&tmax), 23);
        XQ_EQ_I64(XTime_second(&tmax), 59);
        XQ_EQ_I64(XTime_msec(&tmax), 999);
    }
    /* 默认值 2000-01-01 00:00:00.000（QDATETIMEEDIT_DATE_INITIAL）。 */
    value = XDateTimeEdit_dateTime(e);
    XQ_CHECK_PTR(value);
    if (value) {
        XQ_EQ_I64(XDate_year(&value->m_date), 2000);
        XQ_EQ_I64(XDate_month(&value->m_date), 1);
        XQ_EQ_I64(XDate_day(&value->m_date), 1);
        XQ_EQ_I64(XTime_hour(&value->m_time), 0);
        XQ_EQ_I64(XTime_second(&value->m_time), 0);
        XQ_EQ_I64(XTime_msec(&value->m_time), 0);
    }

    /* 分段：默认格式 5 段（年/月/日/时/分，无秒），掩码 0x718。 */
    XQ_EQ_I64(XDateTimeEdit_displayedSections(e), 0x718);
    XQ_EQ_I64(XDateTimeEdit_sectionCount(e), 5);
    XQ_EQ_I64(XDateTimeEdit_sectionAt(e, 0),
              XDateTimeEditSection_YearSection);
    XQ_EQ_I64(XDateTimeEdit_sectionAt(e, 1),
              XDateTimeEditSection_MonthSection);
    XQ_EQ_I64(XDateTimeEdit_sectionAt(e, 2),
              XDateTimeEditSection_DaySection);
    XQ_EQ_I64(XDateTimeEdit_sectionAt(e, 3),
              XDateTimeEditSection_HourSection);
    XQ_EQ_I64(XDateTimeEdit_sectionAt(e, 4),
              XDateTimeEditSection_MinuteSection);
    XQ_EQ_I64(XDateTimeEdit_sectionAt(e, 5),
              XDateTimeEditSection_NoSection);
    /* 当前分段缺省=年段（Qt 探针 currentSection=1024、index=0）。 */
    XQ_EQ_I64(XDateTimeEdit_currentSection(e),
              XDateTimeEditSection_YearSection);
    XQ_EQ_I64(XDateTimeEdit_currentSectionIndex(e), 0);
    /* calendarPopup 默认 false（Qt 探针 0）。 */
    XQ_CHECK(!XDateTimeEdit_calendarPopup(e));

    XClassDelete((XClass*)e);
}

/** @brief 分段步进、sectionText 渲染、setCurrentSection 有效性、
 *         setDisplayFormat 的纯时间段/纯日期段收窄。 */
XQ_TEST(XDateTimeEdit, sectionStepAndFormatNarrowing)
{
    XDateTimeEdit* e = XDateTimeEdit_create(NULL, 0);
    XDateTime value;
    XDate d;
    XTime t;
    char sec[16];
    XQ_REQUIRE(e != NULL);

    /* 本用例覆盖 6 段格式的渲染/分段查询：显式设 6 段格式（构造默认已是
     * Qt 的 5 段 'yyyy/M/d HH:mm'）。 */
    XDateTimeEdit_setDisplayFormat(e, "yyyy-MM-dd HH:mm:ss");
    value = XDateTime_create_datetime(XDate_create_date(2024, 3, 5),
                                      XTime_create_time(14, 25, 36, 789));
    XDateTimeEdit_setDateTime(e, &value);

    /* sectionText 按位宽渲染（Qt 实测 Y=2024/M=03/d=05/H=14/m=25/s=36）。 */
    xq_dt_sectionText(e, XDateTimeEditSection_YearSection, sec, sizeof(sec));
    XQ_EQ_STR(sec, "2024");
    xq_dt_sectionText(e, XDateTimeEditSection_MonthSection, sec, sizeof(sec));
    XQ_EQ_STR(sec, "03");
    xq_dt_sectionText(e, XDateTimeEditSection_DaySection, sec, sizeof(sec));
    XQ_EQ_STR(sec, "05");
    xq_dt_sectionText(e, XDateTimeEditSection_HourSection, sec, sizeof(sec));
    XQ_EQ_STR(sec, "14");
    xq_dt_sectionText(e, XDateTimeEditSection_MinuteSection, sec, sizeof(sec));
    XQ_EQ_STR(sec, "25");
    xq_dt_sectionText(e, XDateTimeEditSection_SecondSection, sec, sizeof(sec));
    XQ_EQ_STR(sec, "36");
    /* 格式中不存在的分段返回空串（Qt 实测 MSec/AmPm 为 ""）。 */
    xq_dt_sectionText(e, XDateTimeEditSection_MSecSection, sec, sizeof(sec));
    XQ_EQ_STR(sec, "");

    /* setCurrentSection 对不在格式中的分段不动作（Qt 实测仍 Year）。 */
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_MSecSection);
    XQ_EQ_I64(XDateTimeEdit_currentSection(e),
              XDateTimeEditSection_YearSection);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_NoSection);
    XQ_EQ_I64(XDateTimeEdit_currentSection(e),
              XDateTimeEditSection_YearSection);

    /* stepBy 按当前分段：月 +1（Qt 实测 2024/4）、年 -1。 */
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_MonthSection);
    XQ_EQ_I64(XDateTimeEdit_currentSectionIndex(e), 1);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_year(&d), 2024);
    XQ_EQ_I64(XDate_month(&d), 4);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_YearSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, -1);
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_year(&d), 2023);
    XQ_EQ_I64(XDate_month(&d), 4);

    /* 纯时间段格式：日期范围收到值日期、时间范围复位整天
     * （Qt 实测 displayedSections=0x18、minDate==maxDate）。 */
    XDateTimeEdit_setDateTime(e, &value);
    XDateTimeEdit_setDisplayFormat(e, "HH:mm");
    XQ_EQ_STR(XDateTimeEdit_displayFormat(e), "HH:mm");
    XQ_EQ_I64(XDateTimeEdit_displayedSections(e),
              (int)XDateTimeEditSection_HourSection |
              (int)XDateTimeEditSection_MinuteSection);
    XQ_EQ_I64(XDateTimeEdit_sectionCount(e), 2);
    {
        XDate dmin = XDateTimeEdit_minimumDate(e);
        XDate dmax = XDateTimeEdit_maximumDate(e);
        XQ_EQ_I64(XDate_year(&dmin), 2024);
        XQ_EQ_I64(XDate_month(&dmin), 3);
        XQ_EQ_I64(XDate_day(&dmin), 5);
        XQ_EQ_I64(XDate_compare(&dmin, &dmax), 0);
    }
    /* 当前分段随格式回正到首段（Qt setDisplayFormat 的
     * currentSectionIndex 回正；实测 HourSection）。 */
    XQ_EQ_I64(XDateTimeEdit_currentSection(e),
              XDateTimeEditSection_HourSection);

    /* 纯日期段格式：时间范围复位整天、值时间清零（Qt startOfDay 口径）。 */
    XDateTimeEdit_setDateTime(e, &value);
    XDateTimeEdit_setDisplayFormat(e, "dd.MM.yyyy");
    XQ_EQ_STR(XDateTimeEdit_displayFormat(e), "dd.MM.yyyy");
    XQ_EQ_I64(XDateTimeEdit_displayedSections(e),
              (int)XDateTimeEditSection_DaySection |
              (int)XDateTimeEditSection_MonthSection |
              (int)XDateTimeEditSection_YearSection);
    t = XDateTimeEdit_time(e);
    XQ_EQ_I64(XTime_hour(&t), 0);
    XQ_EQ_I64(XTime_minute(&t), 0);
    XQ_EQ_I64(XTime_msec(&t), 0);

    /* 单字母 'a' 是 AmPm 段 ⇒ "abc" 在 Qt 里是**合法**格式：Qt 6.8.0 实测
     * setDisplayFormat("abc") 被接受（displayFormat()=="abc"、
     * displayedSections()==AmPmSection(0x1)），不能当无效格式用。 */
    XDateTimeEdit_setDisplayFormat(e, "abc");
    XQ_EQ_STR(XDateTimeEdit_displayFormat(e), "abc");
    XQ_EQ_I64(XDateTimeEdit_displayedSections(e),
              (int)XDateTimeEditSection_AmPmSection);

    /* 无效格式（无任何可识别分段）不设置、原格式保持
     * （qdatetimeedit.cpp:929 parseFormat 守卫）。Qt 6.8.0 实测
     * setDisplayFormat("123")（以及 "$$$"/"-"/""）均被拒绝、格式保持
     * 上一次成功设置的 "abc"。 */
    XDateTimeEdit_setDisplayFormat(e, "123");
    XQ_EQ_STR(XDateTimeEdit_displayFormat(e), "abc");

    XClassDelete((XClass*)e);
}

/** @brief 范围钳位 + 三信号按实际变化发射（EmitIfChanged）+ 同值零信号。 */
XQ_TEST(XDateTimeEdit, rangeClampAndSignalEmission)
{
    XDateTimeEdit* e = XDateTimeEdit_create(NULL, 0);
    XDateTime value;
    XDate d;
    XTime t;
    XConnection* c1;
    XConnection* c2;
    XConnection* c3;
    XQ_REQUIRE(e != NULL);

    c1 = XObject_connect((XObject*)e,
                           (size_t)XDateTimeEdit_dateTimeChanged_signal(e, NULL),
                           (XObject*)e, xq_dt_onDateTimeChanged,
                           XConnectionType_Direct);
    c2 = XObject_connect((XObject*)e,
                           (size_t)XDateTimeEdit_dateChanged_signal(e, NULL),
                           (XObject*)e, xq_dt_onDateChanged,
                           XConnectionType_Direct);
    c3 = XObject_connect((XObject*)e,
                           (size_t)XDateTimeEdit_timeChanged_signal(e, NULL),
                           (XObject*)e, xq_dt_onTimeChanged,
                           XConnectionType_Direct);
    XQ_CHECK_PTR(c1);
    XQ_CHECK_PTR(c2);
    XQ_CHECK_PTR(c3);

    xq_dt_dateTimeChanged = 0;
    xq_dt_dateChanged = 0;
    xq_dt_timeChanged = 0;
    value = XDateTime_create_datetime(XDate_create_date(2024, 3, 5),
                                      XTime_create_time(14, 25, 36, 789));
    XDateTimeEdit_setDateTime(e, &value);
    XQ_EQ_I64(xq_dt_dateTimeChanged, 1);
    XQ_EQ_I64(xq_dt_dateChanged, 1);
    XQ_EQ_I64(xq_dt_timeChanged, 1);

    /* 同值 set 不再发射（Qt EmitIfChanged，实测零信号）。 */
    XDateTimeEdit_setDateTime(e, &value);
    XQ_EQ_I64(xq_dt_dateTimeChanged, 1);
    XQ_EQ_I64(xq_dt_dateChanged, 1);
    XQ_EQ_I64(xq_dt_timeChanged, 1);

    /* 只改时间：timeChanged +1，dateChanged 不动。 */
    t = XTime_create_time(15, 0, 0, 0);
    XDateTimeEdit_setTime(e, &t);
    XQ_EQ_I64(xq_dt_dateTimeChanged, 2);
    XQ_EQ_I64(xq_dt_dateChanged, 1);
    XQ_EQ_I64(xq_dt_timeChanged, 2);

    /* 范围钳位：上/下越界分别钳到 maximum/minimum（Qt 实测
     * setDate(2025) → 2024-12-31、setDate(2020) → 2024-01-01）。 */
    d = XDate_create_date(2024, 1, 1);
    XDateTimeEdit_setMinimumDate(e, &d);
    d = XDate_create_date(2024, 12, 31);
    XDateTimeEdit_setMaximumDate(e, &d);
    d = XDate_create_date(2025, 5, 5);
    XDateTimeEdit_setDate(e, &d);
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_year(&d), 2024);
    XQ_EQ_I64(XDate_month(&d), 12);
    XQ_EQ_I64(XDate_day(&d), 31);
    d = XDate_create_date(2020, 1, 1);
    XDateTimeEdit_setDate(e, &d);
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_year(&d), 2024);
    XQ_EQ_I64(XDate_month(&d), 1);
    XQ_EQ_I64(XDate_day(&d), 1);

    /* clear() 重载：只清当前分段（Qt 实测 clear 后年段变空格、其余
     * 段保留），值不变。 */
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_YearSection);
    XDateTimeEdit_clear(e);
    {
        const char* text = XAbstractSpinBox_text((XAbstractSpinBox*)e);
        XQ_CHECK(text != NULL);
        if (text) {
            /* 年段 4 位被空格替换 ⇒ 首字符是空格、不是数字。 */
            XQ_CHECK(text[0] == ' ');
        }
    }
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_year(&d), 2024);

    XClassDelete((XClass*)e);
}

/** @brief P0-68/P0-69/P0-20：范围写入口的 min≤max 不变量（Qt 口径）。
 *         - setMinimumDateTime 倒置：上界被抬到下界（qdatetimeedit.cpp
 *           :377 `setRange(m, (max > m ? max : m))`）；
 *         - setMaximumDateTime 倒置：下界被降到上界（:419
 *           `setRange((min < m ? min : m), m)`）；
 *         - setDateTimeRange 倒置：min 胜，终态 [min,min]（:447-454
 *           `maximum = (min > max ? minimum : max)`；旧实现终态 [max,max]
 *           上界胜，与 Qt 相反）；
 *         - setDateRange/setTimeRange 任一端无效 ⇒ 整调用不做
 *           （:637-644/:676-683 `if (min.isValid() && max.isValid())`），
 *           端点保留另一侧的时间/日期部分；
 *         - 钳位后当前值落在报告区间之内（倒置 bug 的可观察后果）。 */
XQ_TEST(XDateTimeEdit, rangeInvariantMinMaxOrdered)
{
    XDateTimeEdit* e = XDateTimeEdit_create(NULL, 0);
    const XDateTime* lo;
    const XDateTime* hi;
    XDateTime m;
    XDateTime value;
    XQ_REQUIRE(e != NULL);

    /* P0-68：setMinimumDateTime 正序：上界保持缺省 9999-12-31。 */
    m = XDateTime_create_datetime(XDate_create_date(2021, 1, 1),
                                  XTime_create_time(0, 0, 0, 0));
    XDateTimeEdit_setMinimumDateTime(e, &m);
    lo = XDateTimeEdit_minimumDateTime(e);
    hi = XDateTimeEdit_maximumDateTime(e);
    XQ_EQ_I64(XDate_year(&lo->m_date), 2021);
    XQ_EQ_I64(XDate_year(&hi->m_date), 9999);
    m = XDateTime_create_datetime(XDate_create_date(2021, 6, 1),
                                  XTime_create_time(12, 0, 0, 0));
    XDateTimeEdit_setMaximumDateTime(e, &m);
    /* P0-69：setMaximumDateTime 低于旧下界(2021-01-01) ⇒ min 被降到
     * 上界 2020-01-01（下界只降不抬，Qt :419）。 */
    m = XDateTime_create_datetime(XDate_create_date(2020, 1, 1),
                                  XTime_create_time(0, 0, 0, 0));
    XDateTimeEdit_setMaximumDateTime(e, &m);
    lo = XDateTimeEdit_minimumDateTime(e);
    hi = XDateTimeEdit_maximumDateTime(e);
    XQ_EQ_I64(XDate_year(&lo->m_date), 2020);
    XQ_EQ_I64(XDate_year(&hi->m_date), 2020);
    /* 不变量：恢复正序区间后钳位的当前值落在报告区间之内。 */
    m = XDateTime_create_datetime(XDate_create_date(2020, 12, 31),
                                  XTime_create_time(23, 59, 59, 999));
    XDateTimeEdit_setMaximumDateTime(e, &m);
    value = XDateTime_create_datetime(XDate_create_date(2020, 6, 1),
                                      XTime_create_time(6, 0, 0, 0));
    XDateTimeEdit_setDateTime(e, &value);
    value = *XDateTimeEdit_dateTime(e);
    lo = XDateTimeEdit_minimumDateTime(e);
    hi = XDateTimeEdit_maximumDateTime(e);
    XQ_CHECK(XDateTime_compare(&value, lo) >= 0);
    XQ_CHECK(XDateTime_compare(&value, hi) <= 0);

    /* P0-20：setDateTimeRange 倒置 ⇒ min 胜，终态 [min,min]。 */
    {
        XDateTime rmin = XDateTime_create_datetime(
            XDate_create_date(2022, 3, 1), XTime_create_time(1, 0, 0, 0));
        XDateTime rmax = XDateTime_create_datetime(
            XDate_create_date(2022, 2, 1), XTime_create_time(2, 0, 0, 0));
        XDateTimeEdit_setDateTimeRange(e, &rmin, &rmax);
        lo = XDateTimeEdit_minimumDateTime(e);
        hi = XDateTimeEdit_maximumDateTime(e);
        XQ_EQ_I64(XDate_year(&lo->m_date), 2022);
        XQ_EQ_I64(XDate_month(&lo->m_date), 3);
        XQ_EQ_I64(XDate_year(&hi->m_date), 2022);
        XQ_EQ_I64(XDate_month(&hi->m_date), 3); /* min 胜 ⇒ 上界同日。 */
        XQ_CHECK(XDateTime_compare(lo, hi) <= 0);
    }
    /* setDateTimeRange 单端无效 ⇒ 整调用不做。 */
    {
        XDateTime good = XDateTime_create_datetime(
            XDate_create_date(2023, 5, 1), XTime_create_time(3, 0, 0, 0));
        XDateTime bad = XDateTime_create(); /* 无效（date/time 均无效）。 */
        XQ_CHECK(!XDateTime_isValid(&bad));
        XDateTimeEdit_setDateTimeRange(e, &good, &bad);
        lo = XDateTimeEdit_minimumDateTime(e);
        XQ_EQ_I64(XDate_year(&lo->m_date), 2022); /* 保持上一轮范围。 */
        XDateTimeEdit_setDateTimeRange(e, &bad, &good);
        lo = XDateTimeEdit_minimumDateTime(e);
        XQ_EQ_I64(XDate_year(&lo->m_date), 2022);
    }
    /* setDateRange 两端有效：端点保留各自下/上界的时间部分。 */
    {
        XDate dmin = XDate_create_date(2024, 1, 10);
        XDate dmax = XDate_create_date(2024, 3, 20);
        XDate bad;
        XDateTime lo2;
        XDateTime hi2;
        /* 先立正序异时区间：下界 03:00、上界 09:00。 */
        lo2 = XDateTime_create_datetime(XDate_create_date(2023, 5, 1),
                                        XTime_create_time(3, 0, 0, 0));
        hi2 = XDateTime_create_datetime(XDate_create_date(2023, 8, 1),
                                        XTime_create_time(9, 0, 0, 0));
        XDateTimeEdit_setDateTimeRange(e, &lo2, &hi2);
        XDateTimeEdit_setDateRange(e, &dmin, &dmax);
        lo = XDateTimeEdit_minimumDateTime(e);
        hi = XDateTimeEdit_maximumDateTime(e);
        XQ_EQ_I64(XDate_year(&lo->m_date), 2024);
        XQ_EQ_I64(XDate_month(&lo->m_date), 1);
        XQ_EQ_I64(XTime_hour(&lo->m_time), 3); /* 下界原时间部分保留。 */
        XQ_EQ_I64(XDate_year(&hi->m_date), 2024);
        XQ_EQ_I64(XTime_hour(&hi->m_time), 9); /* 上界原时间部分保留。 */
        /* 任一端无效 ⇒ 整调用不做。 */
        bad = XDate_create(); /* m_jd=-1 无效。 */
        XQ_CHECK(!XDate_isValid(&bad));
        XDateTimeEdit_setDateRange(e, &dmax, &bad);
        XDateTimeEdit_setDateRange(e, &bad, &dmin);
        lo = XDateTimeEdit_minimumDateTime(e);
        hi = XDateTimeEdit_maximumDateTime(e);
        XQ_EQ_I64(XDate_year(&lo->m_date), 2024);
        XQ_EQ_I64(XDate_year(&hi->m_date), 2024);
    }
    /* setTimeRange 两端有效：端点保留各自下/上界的日期部分；
     * 任一端无效 ⇒ 整调用不做。 */
    {
        XTime tmin = XTime_create_time(9, 0, 0, 0);
        XTime tmax = XTime_create_time(18, 0, 0, 0);
        XTime tinv = XTime_create(); /* 无效时间。 */
        XDateTimeEdit_setTimeRange(e, &tmin, &tmax);
        lo = XDateTimeEdit_minimumDateTime(e);
        hi = XDateTimeEdit_maximumDateTime(e);
        XQ_EQ_I64(XTime_hour(&lo->m_time), 9);
        XQ_EQ_I64(XTime_hour(&hi->m_time), 18);
        XQ_EQ_I64(XDate_year(&lo->m_date), 2024); /* 日期部分保留。 */
        XQ_CHECK(!XTime_isValid(&tinv));
        XDateTimeEdit_setTimeRange(e, &tinv, &tmax);
        XDateTimeEdit_setTimeRange(e, &tmin, &tinv);
        lo = XDateTimeEdit_minimumDateTime(e);
        hi = XDateTimeEdit_maximumDateTime(e);
        XQ_EQ_I64(XTime_hour(&lo->m_time), 9);
        XQ_EQ_I64(XTime_hour(&hi->m_time), 18);
    }

    XClassDelete((XClass*)e);
}

/** @brief stepEnabled 四态 + 分段边界（Qt 探针实测值）。 */
XQ_TEST(XDateTimeEdit, stepEnabledBounds)
{
    XDateTimeEdit* e = XDateTimeEdit_create(NULL, 0);
    XAbstractSpinBox* base;
    XDateTime vmin;
    XDateTime vmax;
    XDateTime same;
    XQ_REQUIRE(e != NULL);
    base = (XAbstractSpinBox*)e;

    /* 默认（年段、值 2000 在 1752..9999 中间）⇒ 双向。 */
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
              XAbstractSpinBoxStepEnabledFlag_StepDownEnabled);

    /* 值==minimum ⇒ 只剩步上（Qt 实测 at min=1）。 */
    vmin = XDateTime_create_datetime(XDate_create_date(1752, 9, 14),
                                     XTime_create_time(0, 0, 0, 0));
    XDateTimeEdit_setDateTime(e, &vmin);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepUpEnabled);

    /* 值==maximum ⇒ 只剩步下（Qt 实测 at max=2）。 */
    vmax = XDateTime_create_datetime(XDate_create_date(9999, 12, 31),
                                     XTime_create_time(23, 59, 59, 999));
    XDateTimeEdit_setDateTime(e, &vmax);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepDownEnabled);

    /* readOnly ⇒ 全禁（Qt 实测 0）。 */
    XAbstractSpinBox_setReadOnly(base, true);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepNone);
    XAbstractSpinBox_setReadOnly(base, false);

    /* wrapping ⇒ 双向（Qt 实测 3）。 */
    XAbstractSpinBox_setWrapping(base, true);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
              XAbstractSpinBoxStepEnabledFlag_StepDownEnabled);
    XAbstractSpinBox_setWrapping(base, false);

    /* min==max ⇒ 全禁（Qt 实测 0）。 */
    same = XDateTime_create_datetime(XDate_create_date(2024, 1, 1),
                                     XTime_create_time(0, 0, 0, 0));
    XDateTimeEdit_setMinimumDateTime(e, &same);
    XDateTimeEdit_setMaximumDateTime(e, &same);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepNone);

    XClassDelete((XClass*)e);
}

/** @brief 分段数字级步进（对标 QDateTimeEditPrivate::stepBy，
 *         qdatetimeedit.cpp:2123-2284）：只改当前段数字并在该段绝对范围内
 *         钳/绕，**不跨段进位**。期望值全部来自本机 Qt 6.8 探针实测
 *         （干净状态下逐项 stepBy）：
 *         - 分钟 59 +1 → 59（不进位到下一小时）；
 *         - 小时 23 +1 → 23（非 wrapping 不跨日）；
 *         - 月 2024-12 +1 → 2024-12；2024-01-31 +1 → 2024-02-29；
 *         - 年 2024-02-29 +1 → 2025-02-28；日 2024-01-31 +1 → 2024-01-31；
 *         - wrapping：23 时 +1 → 00 时同日、59 分 +1 → 00 分同小时、
 *           2024-12 月 +1 → 2024-01 同年。
 *         此前本库走 epoch 毫秒加算，以上全部相左（gap 100）。 */
XQ_TEST(XDateTimeEdit, stepBySectionDigits)
{
    XDateTimeEdit* e = XDateTimeEdit_create(NULL, 0);
    XDateTime v;
    XDate d;
    XTime t;
    XQ_REQUIRE(e != NULL);
    XDateTimeEdit_setDisplayFormat(e, "yyyy-MM-dd HH:mm:ss");

    /* 分钟 59 +1 → 59（14:59:36 仍 14:59:36）。 */
    v = XDateTime_create_datetime(XDate_create_date(2024, 3, 5),
                                  XTime_create_time(14, 59, 36, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_MinuteSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    t = XDateTimeEdit_time(e);
    XQ_EQ_I64(XTime_hour(&t), 14);
    XQ_EQ_I64(XTime_minute(&t), 59);

    /* 小时 23 +1 → 23（23:30 仍 23:30，不跨日）。 */
    v = XDateTime_create_datetime(XDate_create_date(2024, 3, 5),
                                  XTime_create_time(23, 30, 0, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_HourSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    d = XDateTimeEdit_date(e);
    t = XDateTimeEdit_time(e);
    XQ_EQ_I64(XDate_day(&d), 5);
    XQ_EQ_I64(XTime_hour(&t), 23);
    XQ_EQ_I64(XTime_minute(&t), 30);

    /* 月 2024-12 +1 → 2024-12（非 wrapping 不换年）。 */
    v = XDateTime_create_datetime(XDate_create_date(2024, 12, 15),
                                  XTime_create_time(10, 0, 0, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_MonthSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_year(&d), 2024);
    XQ_EQ_I64(XDate_month(&d), 12);

    /* 月 2024-01-31 +1 → 2024-02-29（日按目标月长下调）。 */
    v = XDateTime_create_datetime(XDate_create_date(2024, 1, 31),
                                  XTime_create_time(10, 0, 0, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_MonthSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_year(&d), 2024);
    XQ_EQ_I64(XDate_month(&d), 2);
    XQ_EQ_I64(XDate_day(&d), 29);

    /* 年 2024-02-29 +1 → 2025-02-28（非闰年日下调）。 */
    v = XDateTime_create_datetime(XDate_create_date(2024, 2, 29),
                                  XTime_create_time(10, 0, 0, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_YearSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_year(&d), 2025);
    XQ_EQ_I64(XDate_month(&d), 2);
    XQ_EQ_I64(XDate_day(&d), 28);

    /* 日 2024-01-31 +1 → 2024-01-31（段内不跨月）。 */
    v = XDateTime_create_datetime(XDate_create_date(2024, 1, 31),
                                  XTime_create_time(10, 0, 0, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_DaySection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_month(&d), 1);
    XQ_EQ_I64(XDate_day(&d), 31);

    /* wrapping：23 时 +1 → 00 时同日。 */
    XAbstractSpinBox_setWrapping((XAbstractSpinBox*)e, true);
    v = XDateTime_create_datetime(XDate_create_date(2024, 3, 5),
                                  XTime_create_time(23, 30, 0, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_HourSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    d = XDateTimeEdit_date(e);
    t = XDateTimeEdit_time(e);
    XQ_EQ_I64(XDate_day(&d), 5);
    XQ_EQ_I64(XTime_hour(&t), 0);
    XQ_EQ_I64(XTime_minute(&t), 30);

    /* wrapping：59 分 +1 → 00 分（同小时 10 点）。 */
    v = XDateTime_create_datetime(XDate_create_date(2024, 3, 5),
                                  XTime_create_time(10, 59, 0, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_MinuteSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    t = XDateTimeEdit_time(e);
    XQ_EQ_I64(XTime_hour(&t), 10);
    XQ_EQ_I64(XTime_minute(&t), 0);

    /* wrapping：2024-12 月 +1 → 2024-01（同年）。 */
    v = XDateTime_create_datetime(XDate_create_date(2024, 12, 15),
                                  XTime_create_time(10, 0, 0, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_MonthSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)e, 1);
    d = XDateTimeEdit_date(e);
    XQ_EQ_I64(XDate_year(&d), 2024);
    XQ_EQ_I64(XDate_month(&d), 1);
    XQ_EQ_I64(XDate_day(&d), 15);
    XAbstractSpinBox_setWrapping((XAbstractSpinBox*)e, false);

    /* stepEnabled 的 test-mode 与 stepBy 同源（gap 112）：分钟 59 处
     * 非 wrapping 下 Up 已被段内钳位吃掉 ⇒ 只剩 Down；wrapping 下双向。 */
    v = XDateTime_create_datetime(XDate_create_date(2024, 3, 5),
                                  XTime_create_time(14, 59, 0, 0));
    XDateTimeEdit_setDateTime(e, &v);
    XDateTimeEdit_setCurrentSection(e, XDateTimeEditSection_MinuteSection);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base((XAbstractSpinBox*)e),
              XAbstractSpinBoxStepEnabledFlag_StepDownEnabled);
    XAbstractSpinBox_setWrapping((XAbstractSpinBox*)e, true);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base((XAbstractSpinBox*)e),
              XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
              XAbstractSpinBoxStepEnabledFlag_StepDownEnabled);
    XAbstractSpinBox_setWrapping((XAbstractSpinBox*)e, false);

    XClassDelete((XClass*)e);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */
