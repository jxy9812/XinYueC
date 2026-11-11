/**
 * @file       XTimeEditAlignTest.c
 * @brief      XTimeEdit × Qt 6.8 QTimeEdit 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qdatetimeedit.h
 *             （Qt 6.8 中 QTimeEdit 声明于此）与 Qt 6.8 源码
 *             src/widgets/widgets/qdatetimeedit.cpp；关键行为用本机
 *             Qt 6.8.0（系统 Qt，offscreen）探针实测：
 *             - 默认：time=00:00:00.000（QDATETIMEEDIT_TIME_MIN）、
 *               minimumDate==maximumDate==2000-01-01（DATE_INITIAL 载体）、
 *               时间范围 00:00:00.000..23:59:59.999；默认 displayFormat 为
 *               缺省区域短时间格式（本机 C/zh_CN 实测 'HH:mm'，
 *               displayedSections=0x18，对齐 Qt 6.8 QTimeEdit 构造）；
 *             - QTimeEdit(QTime) 构造：有效时间作为初值（实测
 *               13:45:30），无效时间回退 00:00:00（QTime(25,0,0) 实测）；
 *             - 纯时间段格式收窄：日期范围收到值日期；
 *             - setDate 在纯时间段格式下先收窄日期范围到新日期
 *               （qdatetimeedit.cpp:266-278）：实测 11:00 setDate(2024-05-06)
 *               → '2024-05-06 00:00:00'、再 setDate(2000-01-01) →
 *               '2000-01-01 23:59:59'；
 *             - 分段步进按当前段（普通 ±1：08:30→09:30、10:20→10:21）；
 *             - userTimeChanged 由构造体内的
 *               connect(timeChanged, userTimeChanged) 转发：程序性
 *               setTime 同样触发，同值 setTime 不触发。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XTIMEEDIT_ON

#include "XTimeEdit.h"
#include "XDateTimeEdit.h"
#include "XAbstractSpinBox.h"
#include "XDateTime.h"
#include "XWidget.h"
#include "XObject.h"

/** @brief userTimeChanged 计数（用例内清零）。 */
static int xq_te_userTimeChanged = 0;

static void xq_te_onUserTimeChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_te_userTimeChanged;
}

/** @brief 构造默认值：时间/格式/纯时间段收窄（Qt 6.8 探针口径）。 */
XQ_TEST(XTimeEdit, constructorDefaults)
{
    XTimeEdit* te = XTimeEdit_create(NULL, 0);
    XDateTimeEdit* base;
    XQ_REQUIRE(te != NULL);
    base = (XDateTimeEdit*)te;

    /* QTimeEdit(QWidget*) → QDateTimeEdit(TIME_MIN, QMetaType::QTime)：
     * 本机 Qt 实测 time='00:00:00.000'、minDate==maxDate==2000-01-01、
     * 时间范围 00:00..23:59:59.999；默认格式 = 缺省区域短时间
     * 'HH:mm'（displayedSections=0x18）。 */
    {
        XTime t = XDateTimeEdit_time(base);
        XDate d = XDateTimeEdit_date(base);
        XQ_EQ_I64(XTime_hour(&t), 0);
        XQ_EQ_I64(XTime_minute(&t), 0);
        XQ_EQ_I64(XTime_second(&t), 0);
        XQ_EQ_I64(XTime_msec(&t), 0);
        XQ_EQ_I64(XDate_year(&d), 2000);
        XQ_EQ_I64(XDate_month(&d), 1);
        XQ_EQ_I64(XDate_day(&d), 1);
        XQ_EQ_STR(XDateTimeEdit_displayFormat(base), "HH:mm");
        d = XDateTimeEdit_minimumDate(base);
        XQ_EQ_I64(XDate_year(&d), 2000);
        XQ_EQ_I64(XDate_month(&d), 1);
        XQ_EQ_I64(XDate_day(&d), 1);
        d = XDateTimeEdit_maximumDate(base);
        XQ_EQ_I64(XDate_year(&d), 2000);
        XQ_EQ_I64(XDate_month(&d), 1);
        XQ_EQ_I64(XDate_day(&d), 1);
        t = XDateTimeEdit_minimumTime(base);
        XQ_EQ_I64(XTime_hour(&t), 0);
        XQ_EQ_I64(XTime_msec(&t), 0);
        t = XDateTimeEdit_maximumTime(base);
        XQ_EQ_I64(XTime_hour(&t), 23);
        XQ_EQ_I64(XTime_minute(&t), 59);
        XQ_EQ_I64(XTime_second(&t), 59);
        XQ_EQ_I64(XTime_msec(&t), 999);
    }

    /* 纯时间段：时/分两段（0x18，Qt 探针 displayedSections=0x18），当前段
     * 回正到时段首段（小时）。 */
    XQ_EQ_I64(XDateTimeEdit_sections(base),
              (int)XDateTimeEditSection_HourSection |
              (int)XDateTimeEditSection_MinuteSection);
    XQ_EQ_I64(XDateTimeEdit_sectionCount(base), 2);
    XQ_EQ_I64(XDateTimeEdit_currentSection(base),
              XDateTimeEditSection_HourSection);

    XClassDelete((XClass*)te);
}

/** @brief QTimeEdit(QTime) 构造重载 + 无效时间回退。 */
XQ_TEST(XTimeEdit, timeConstructorOverload)
{
    XTime t = XTime_create_time(13, 45, 30, 250);
    XTimeEdit* te = XTimeEdit_create_2(&t, NULL, 0);
    XDateTimeEdit* base;
    XTime got;
    XQ_REQUIRE(te != NULL);
    base = (XDateTimeEdit*)te;

    got = XDateTimeEdit_time(base);
    XQ_EQ_I64(XTime_hour(&got), 13);
    XQ_EQ_I64(XTime_minute(&got), 45);
    XQ_EQ_I64(XTime_second(&got), 30);
    XQ_EQ_I64(XTime_msec(&got), 250);
    XClassDelete((XClass*)te);

    /* 无效时间（QTime(25,0,0) 口径）回退 00:00:00.000。 */
    {
        XTime bad = XTime_create_time(25, 0, 0, 0);
        te = XTimeEdit_create_2(&bad, NULL, 0);
        XQ_REQUIRE(te != NULL);
        got = XDateTimeEdit_time((XDateTimeEdit*)te);
        XQ_EQ_I64(XTime_hour(&got), 0);
        XQ_EQ_I64(XTime_minute(&got), 0);
        XQ_EQ_I64(XTime_second(&got), 0);
        XClassDelete((XClass*)te);
    }
    /* NULL 时间同样回退默认。 */
    te = XTimeEdit_create_2(NULL, NULL, 0);
    XQ_REQUIRE(te != NULL);
    got = XDateTimeEdit_time((XDateTimeEdit*)te);
    XQ_EQ_I64(XTime_hour(&got), 0);
    XClassDelete((XClass*)te);
}

/** @brief 分段步进（非边界）+ 纯时间段日期迁移 + userTimeChanged 转发。
 * @note  边界处的跨段进位/回绕属已登记偏差（见文件尾），此处只用两种
 *        模型结果一致的普通步进（小时 08→09、分钟 20→21）。 */
XQ_TEST(XTimeEdit, stepAndUserTimeChanged)
{
    XTimeEdit* te = XTimeEdit_create(NULL, 0);
    XDateTimeEdit* base;
    XTime t;
    XConnection* conn;
    XQ_REQUIRE(te != NULL);
    base = (XDateTimeEdit*)te;

    conn = XObject_connect_1(
        (XObject*)te,
        (size_t)XTimeEdit_userTimeChanged_signal(te, NULL),
        (XObject*)te, xq_te_onUserTimeChanged, XConnectionType_Direct);
    XQ_CHECK_PTR(conn);

    /* 程序性 setTime 触发 userTimeChanged（Qt 构造体内 connect）。 */
    xq_te_userTimeChanged = 0;
    t = XTime_create_time(8, 30, 0, 0);
    XDateTimeEdit_setTime(base, &t);
    XQ_EQ_I64(xq_te_userTimeChanged, 1);
    XDateTimeEdit_setTime(base, &t);   /* 同值：零信号 */
    XQ_EQ_I64(xq_te_userTimeChanged, 1);

    /* 小时段步进：08:30 → 09:30（Qt 实测同值）。 */
    XDateTimeEdit_setCurrentSection(base, XDateTimeEditSection_HourSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)te, 1);
    t = XDateTimeEdit_time(base);
    XQ_EQ_I64(XTime_hour(&t), 9);
    XQ_EQ_I64(XTime_minute(&t), 30);
    XQ_EQ_I64(xq_te_userTimeChanged, 2);   /* 步进同样发 timeChanged */

    /* 分钟段步进：10:20 → 10:21（Qt 实测同值）。 */
    t = XTime_create_time(10, 20, 0, 0);
    XDateTimeEdit_setTime(base, &t);
    XDateTimeEdit_setCurrentSection(base, XDateTimeEditSection_MinuteSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)te, 1);
    t = XDateTimeEdit_time(base);
    XQ_EQ_I64(XTime_hour(&t), 10);
    XQ_EQ_I64(XTime_minute(&t), 21);

    /* 纯时间段格式的日期迁移（Qt 6.8 实测口径）：setDate 先把日期范围
     * 收窄到新日期（qdatetimeedit.cpp:266-278），跨日时时间按新一天的
     * 边界收口——实测 QTimeEdit 11:00 setDate(2024-05-06) →
     * '2024-05-06 00:00:00'，再 setDate(2000-01-01) →
     * '2000-01-01 23:59:59'。 */
    {
        XDate d = XDate_create_date(2024, 5, 6);
        XDateTimeEdit_setDate(base, &d);
        d = XDateTimeEdit_date(base);
        XQ_EQ_I64(XDate_year(&d), 2024);
        XQ_EQ_I64(XDate_month(&d), 5);
        XQ_EQ_I64(XDate_day(&d), 6);
        t = XDateTimeEdit_time(base);
        XQ_EQ_I64(XTime_hour(&t), 0);
        XQ_EQ_I64(XTime_minute(&t), 0);
        /* 日期范围随 setDate 收窄到同一天（Qt setDateRange(date,date)）。 */
        d = XDateTimeEdit_minimumDate(base);
        XQ_EQ_I64(XDate_year(&d), 2024);
        XQ_EQ_I64(XDate_month(&d), 5);
        XQ_EQ_I64(XDate_day(&d), 6);
        d = XDateTimeEdit_maximumDate(base);
        XQ_EQ_I64(XDate_day(&d), 6);
        /* 再迁回 2000-01-01：时间收口到当天最大 23:59:59.999。 */
        d = XDate_create_date(2000, 1, 1);
        XDateTimeEdit_setDate(base, &d);
        t = XDateTimeEdit_time(base);
        XQ_EQ_I64(XTime_hour(&t), 23);
        XQ_EQ_I64(XTime_minute(&t), 59);
        XQ_EQ_I64(XTime_second(&t), 59);
        XQ_EQ_I64(XTime_msec(&t), 999);
    }
    /* 同日期重设：日期/时间都不变 ⇒ userTimeChanged 不触发。 */
    {
        int before = xq_te_userTimeChanged;
        XDate d = XDate_create_date(2000, 1, 1);
        XDateTimeEdit_setDate(base, &d);
        XQ_EQ_I64(xq_te_userTimeChanged, before);
    }

    XClassDelete((XClass*)te);
}

/** @brief 边界段步进只在本段绝对范围内钳/绕（Qt 6.8 探针实测：23:30 时
 *         +1 仍 23:30、59 分 +1 仍 59 分；wrapping 下 23 时 +1 → 00 时
 *         同日、59 分 +1 → 00 分同小时）。此前本库按 epoch 加算会跨段
 *         进位（23:30 → 00:30），gap 100。 */
XQ_TEST(XTimeEdit, boundaryStepClampsWithinSection)
{
    XTimeEdit* te = XTimeEdit_create(NULL, 0);
    XDateTimeEdit* base;
    XTime t;
    XDate d;
    XQ_REQUIRE(te != NULL);
    base = (XDateTimeEdit*)te;

    /* 23:30 小时 +1 → 23:30（同日、不进位）。 */
    t = XTime_create_time(23, 30, 0, 0);
    XDateTimeEdit_setTime(base, &t);
    XDateTimeEdit_setCurrentSection(base, XDateTimeEditSection_HourSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)te, 1);
    t = XDateTimeEdit_time(base);
    d = XDateTimeEdit_date(base);
    XQ_EQ_I64(XTime_hour(&t), 23);
    XQ_EQ_I64(XTime_minute(&t), 30);
    XQ_EQ_I64(XDate_day(&d), 1);
    XQ_EQ_I64(XDate_month(&d), 1);

    /* 10:59 分钟 +1 → 10:59（不进位到 11:00）。 */
    t = XTime_create_time(10, 59, 0, 0);
    XDateTimeEdit_setTime(base, &t);
    XDateTimeEdit_setCurrentSection(base, XDateTimeEditSection_MinuteSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)te, 1);
    t = XDateTimeEdit_time(base);
    XQ_EQ_I64(XTime_hour(&t), 10);
    XQ_EQ_I64(XTime_minute(&t), 59);

    /* wrapping：23 时 +1 → 00 时同日；59 分 +1 → 00 分同小时。 */
    XAbstractSpinBox_setWrapping((XAbstractSpinBox*)te, true);
    t = XTime_create_time(23, 30, 0, 0);
    XDateTimeEdit_setTime(base, &t);
    XDateTimeEdit_setCurrentSection(base, XDateTimeEditSection_HourSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)te, 1);
    t = XDateTimeEdit_time(base);
    d = XDateTimeEdit_date(base);
    XQ_EQ_I64(XTime_hour(&t), 0);
    XQ_EQ_I64(XTime_minute(&t), 30);
    XQ_EQ_I64(XDate_day(&d), 1);
    t = XTime_create_time(10, 59, 0, 0);
    XDateTimeEdit_setTime(base, &t);
    XDateTimeEdit_setCurrentSection(base, XDateTimeEditSection_MinuteSection);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)te, 1);
    t = XDateTimeEdit_time(base);
    XQ_EQ_I64(XTime_hour(&t), 10);
    XQ_EQ_I64(XTime_minute(&t), 0);
    XAbstractSpinBox_setWrapping((XAbstractSpinBox*)te, false);

    XClassDelete((XClass*)te);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XTIMEEDIT_ON */
