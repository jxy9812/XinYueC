/**
 * @file       XDateEditAlignTest.c
 * @brief      XDateEdit × Qt 6.8 QDateEdit 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qdatetimeedit.h
 *             （Qt 6.8 中 QDateEdit 声明于此）与 Qt 6.8 源码
 *             src/widgets/widgets/qdatetimeedit.cpp；关键行为用本机
 *             Qt 6.8.0（系统 Qt，offscreen）探针实测：
 *             - 默认：date=2000-01-01（QDATETIMEEDIT_DATE_INITIAL）、
 *               displayFormat='yyyy/M/d'（locale 短日期，本机实测）、
 *               minimumDate=1752-09-14、maximumDate=9999-12-31；
 *             - parserType 仅日期：setDisplayFormat("HH:mm") 被拒、
 *               原格式保持（Qt 实测 'dd.MM.yyyy' 不被改写）；
 *             - QDateEdit(QDate) 构造：有效日期作为初值、越界日期钳到
 *               范围（实测 QDateEdit(1000-1-1) → 1752-09-14）；
 *             - userDateChanged 由构造体内的
 *               connect(dateChanged, userDateChanged) 转发：程序性
 *               setDate 同样触发（实测计数 1），同值 setDate 不触发
 *               （dateChanged 自身是 EmitIfChanged）。
 * @note        displayFormat 取 Qt 在缺省区域（C/zh_CN）下的实测值
 *              'yyyy/M/d'；XGui 无 XLocale，短日期口径按该缺省值固定。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XDATEEDIT_ON

#include "XDateEdit.h"
#include "XDateTimeEdit.h"
#include "XAbstractSpinBox.h"
#include "XDateTime.h"
#include "XWidget.h"
#include "XObject.h"

/** @brief userDateChanged 计数（用例内清零）。 */
static int xq_de_userDateChanged = 0;

static void xq_de_onUserDateChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_de_userDateChanged;
}

/** @brief 构造默认值：日期/格式/范围/纯日期段收窄（Qt 6.8 探针口径）。 */
XQ_TEST(XDateEdit, constructorDefaults)
{
    XDateEdit* de = XDateEdit_create(NULL, 0);
    XDateTimeEdit* base;
    XQ_REQUIRE(de != NULL);
    base = (XDateTimeEdit*)de;

    /* QDateEdit(QWidget*) → QDateTimeEdit(DATE_INITIAL, QMetaType::QDate)：
     * 本机 Qt 实测 date='2000-01-01'、fmt='yyyy/M/d'、范围
     * 1752-09-14..9999-12-31。 */
    {
        XDate d = XDateTimeEdit_date(base);
        XQ_EQ_I64(XDate_year(&d), 2000);
        XQ_EQ_I64(XDate_month(&d), 1);
        XQ_EQ_I64(XDate_day(&d), 1);
        XQ_EQ_STR(XDateTimeEdit_displayFormat(base), "yyyy/M/d");

        d = XDateTimeEdit_minimumDate(base);
        XQ_EQ_I64(XDate_year(&d), 1752);
        XQ_EQ_I64(XDate_month(&d), 9);
        XQ_EQ_I64(XDate_day(&d), 14);
        d = XDateTimeEdit_maximumDate(base);
        XQ_EQ_I64(XDate_year(&d), 9999);
        XQ_EQ_I64(XDate_month(&d), 12);
        XQ_EQ_I64(XDate_day(&d), 31);
    }

    /* 纯日期段：只显示年月日三段（0x700），时间部分为 0、时间范围
     * 复位整天（qdatetimeedit.cpp:963-964）。 */
    XQ_EQ_I64(XDateTimeEdit_displayedSections(base), 0x700);
    XQ_EQ_I64(XDateTimeEdit_sectionCount(base), 3);
    {
        XTime t = XDateTimeEdit_time(base);
        XQ_EQ_I64(XTime_hour(&t), 0);
        XQ_EQ_I64(XTime_minute(&t), 0);
        XQ_EQ_I64(XTime_second(&t), 0);
        XQ_EQ_I64(XTime_msec(&t), 0);
        t = XDateTimeEdit_minimumTime(base);
        XQ_EQ_I64(XTime_hour(&t), 0);
        XQ_EQ_I64(XTime_msec(&t), 0);
        t = XDateTimeEdit_maximumTime(base);
        XQ_EQ_I64(XTime_hour(&t), 23);
        XQ_EQ_I64(XTime_minute(&t), 59);
        XQ_EQ_I64(XTime_second(&t), 59);
        XQ_EQ_I64(XTime_msec(&t), 999);
    }
    /* 当前分段落在年段（首段）。 */
    XQ_EQ_I64(XDateTimeEdit_currentSection(base),
              XDateTimeEditSection_YearSection);

    /* parserType=Date：时间段格式被拒、原格式保持（Qt 实测
     * setDisplayFormat("HH:mm") 后仍是日期格式）。 */
    XDateTimeEdit_setDisplayFormat(base, "HH:mm");
    XQ_EQ_STR(XDateTimeEdit_displayFormat(base), "yyyy/M/d");

    /* clear() 重载只清当前分段（Qt QDateTimeEdit::clear →
     * clearSection；值不变）。此时格式 "yyyy/M/d" 的首段是年段。 */
    XDateTimeEdit_setCurrentSection(base, XDateTimeEditSection_YearSection);
    XDateEdit_clear(de);
    {
        const char* text = XAbstractSpinBox_text((XAbstractSpinBox*)de);
        XQ_CHECK(text != NULL);
        if (text) XQ_CHECK(text[0] == ' ');
    }

    /* 纯日期段格式仍可换（Qt 实测 dd.MM.yyyy 生效）。 */
    XDateTimeEdit_setDisplayFormat(base, "dd.MM.yyyy");
    XQ_EQ_STR(XDateTimeEdit_displayFormat(base), "dd.MM.yyyy");

    XClassDelete((XClass*)de);
}

/** @brief QDateEdit(QDate) 构造重载：有效初值、无效/越界回退与钳位。 */
XQ_TEST(XDateEdit, dateConstructorOverload)
{
    XDate d;
    XDateEdit* de;
    XDateTimeEdit* base;
    XDate got;

    /* 有效日期：直接成为初值（Qt 实测 QDateEdit(2020-02-03)）。 */
    d = XDate_create_date(2020, 2, 3);
    de = XDateEdit_create_2(&d, NULL, 0);
    XQ_REQUIRE(de != NULL);
    base = (XDateTimeEdit*)de;
    got = XDateTimeEdit_date(base);
    XQ_EQ_I64(XDate_year(&got), 2020);
    XQ_EQ_I64(XDate_month(&got), 2);
    XQ_EQ_I64(XDate_day(&got), 3);
    XClassDelete((XClass*)de);

    /* NULL 日期：回退 QDATETIMEEDIT_DATE_INITIAL（2000-01-01）。 */
    de = XDateEdit_create_2(NULL, NULL, 0);
    XQ_REQUIRE(de != NULL);
    got = XDateTimeEdit_date((XDateTimeEdit*)de);
    XQ_EQ_I64(XDate_year(&got), 2000);
    XQ_EQ_I64(XDate_month(&got), 1);
    XQ_EQ_I64(XDate_day(&got), 1);
    XClassDelete((XClass*)de);

    /* 无效日期（m_jd=-1）：同 Qt 回退默认值。 */
    {
        XDate bad = XDate_create();
        de = XDateEdit_create_2(&bad, NULL, 0);
        XQ_REQUIRE(de != NULL);
        got = XDateTimeEdit_date((XDateTimeEdit*)de);
        XQ_EQ_I64(XDate_year(&got), 2000);
        XQ_EQ_I64(XDate_month(&got), 1);
        XQ_EQ_I64(XDate_day(&got), 1);
        XClassDelete((XClass*)de);
    }

    /* 越界日期：钳到范围下界（Qt 实测 QDateEdit(1000-1-1) →
     * 1752-09-14）。 */
    {
        XDate old = XDate_create_date(1000, 1, 1);
        de = XDateEdit_create_2(&old, NULL, 0);
        XQ_REQUIRE(de != NULL);
        got = XDateTimeEdit_date((XDateTimeEdit*)de);
        XQ_EQ_I64(XDate_year(&got), 1752);
        XQ_EQ_I64(XDate_month(&got), 9);
        XQ_EQ_I64(XDate_day(&got), 14);
        XClassDelete((XClass*)de);
    }
}

/** @brief userDateChanged 转发（程序性 setDate 也发）+ 父子析构级联。 */
XQ_TEST(XDateEdit, userDateChangedAndParent)
{
    XWidget* host = XWidget_create(NULL, 0);
    XDateEdit* de;
    XConnection* conn;
    XDate d;
    XQ_REQUIRE(host != NULL);
    de = XDateEdit_create(host, 0);
    XQ_REQUIRE(de != NULL);

    conn = XObject_connect(
        (XObject*)de,
        (size_t)XDateEdit_userDateChanged_signal(de, NULL),
        (XObject*)de, xq_de_onUserDateChanged, XConnectionType_Direct);
    XQ_CHECK_PTR(conn);

    /* 程序性 setDate 同样触发 userDateChanged（Qt 构造体内
     * connect(dateChanged, userDateChanged)，实测计数 1）。 */
    xq_de_userDateChanged = 0;
    d = XDate_create_date(2021, 7, 8);
    XDateTimeEdit_setDate((XDateTimeEdit*)de, &d);
    XQ_EQ_I64(xq_de_userDateChanged, 1);
    /* 同值 setDate：dateChanged 是 EmitIfChanged ⇒ 不再转发。 */
    XDateTimeEdit_setDate((XDateTimeEdit*)de, &d);
    XQ_EQ_I64(xq_de_userDateChanged, 1);
    /* 只改时间不改日期：userDateChanged 不触发。 */
    {
        XTime t = XTime_create_time(12, 0, 0, 0);
        XDateTimeEdit_setTime((XDateTimeEdit*)de, &t);
        XQ_EQ_I64(xq_de_userDateChanged, 1);
    }

    /* 父控件析构带走子控件（运行器堆计数判泄漏）。 */
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XDATEEDIT_ON */
