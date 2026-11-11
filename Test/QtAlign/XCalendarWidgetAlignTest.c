/**
 * @file       XCalendarWidgetAlignTest.c
 * @brief      XCalendarWidget × Qt 6.8 QCalendarWidget 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/
 *             qcalendarwidget.h 与 Qt 6.8 源码
 *             src/widgets/widgets/qcalendarwidget.cpp；关键行为用本机
 *             Qt 6.8.0（系统 Qt，offscreen）探针实测：
 *             - 构造默认：selectedDate=当前日期、firstDayOfWeek=1（周一）、
 *               gridVisible=false、navigationBarVisible=true、
 *               selectionMode=SingleSelection、horizontalHeaderFormat=2
 *               （ShortDayNames）、verticalHeaderFormat=1
 *               （ISOWeekNumbers）、dateEditEnabled=true、
 *               dateEditAcceptDelay=1500；
 *             - 日期范围默认 [儒略日 1, 9999-12-31]
 *               （qcalendarwidget.cpp:2531-2534/2587-2590，实测
 *               minimumDate().toJulianDay()==1）；
 *             - setSelectedDate：无效日期忽略；同值零信号；改值即翻到该
 *               月并发 selectionChanged（qcalendarwidget.cpp:2303-2320）；
 *             - setMinimumDate/setMaximumDate：无效日期忽略；最小值越过
 *               最大值时反向抬/降（QCalendarModel::setMinimumDate/
 *               setMaximumDate，qcalendarwidget.cpp:1200-1224；实测
 *               setMinimumDate(2021) 后 setMaximumDate(2020) 两者都变
 *               2020-05-05），选中越界即钳位并发 selectionChanged；
 *             - 翻页：setCurrentPage 同页零信号；showNextMonth/PrevMonth
 *               跨年进位（实测 2024-12 → 2025-1、2024-1 → 2023-12）；
 *               showSelectedDate/showToday 回对应月；
 *             - 点击网格：**按下不选、释放才选中并 clicked**
 *               （QCalendarView::mouseReleaseEvent 的 validDateClicked
 *               分支，qcalendarwidget.cpp:1523-1545）；NoSelection 下不
 *               选中、不发 clicked；
 *             - ISO 周号列：verticalHeaderFormat=ISOWeekNumbers 时左侧留出
 *               周号列（该列不可点选）；NoVerticalHeader 时日期网格从 0 起；
 *             - 月份下拉菜单：点导航条月份文本弹出 12 个月份条目，
 *               范围外月份置灰（Qt updateMonthMenu），选中条目翻页；
 *             - sizeHint 由字体度量推导且 sizeHint()==minimumSizeHint()
 *               （qcalendarwidget.cpp:2178-2184 实测 320x172）。
 * @note        [deferred] ① dateEditEnabled/dateEditAcceptDelay 对应的
 *             QCalendarTextNavigator（按键就地弹出日期文本编辑层）需要
 *             日期文本输入子系统，本轮只保留属性存取；② calendar()/
 *             setCalendar(QCalendar) 无 C 承载（本库无 XCalendar 类型）；
 *             ③ weekdayTextFormat/dateTextFormat/paintCell/updateCell 需要
 *             QTextCharFormat/QMap<QDate,…> 承载。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XCALENDARWIDGET_ON

#include "XCalendarWidget.h"
#include "XMenu.h"
#include "XAction.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XFont.h"

/** @brief selectionChanged / clicked / currentPageChanged 计数。 */
static int xq_cal_selection = 0;
static int xq_cal_clicked = 0;
static int xq_cal_page = 0;
/** @brief 最近一次 clicked 的载荷日期（校验释放点日期）。 */
static int xq_cal_clickedDay = 0;

static void xq_cal_onSelection(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_cal_selection;
}

static void xq_cal_onClicked(XObject* receiver, XVarList* args)
{
    XDate date;
    (void)receiver;
    if (args) {
        XVarList_args_1(args, XDate*, date);
        if (date) xq_cal_clickedDay = XDate_day(date);
    }
    ++xq_cal_clicked;
}

static void xq_cal_onPage(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_cal_page;
}

/** @brief 在控件本地坐标 (x,y) 注入一次左键按下。 */
static void xq_cal_press(XCalendarWidget* cal, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!cal) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton,
                     XKeyboardModifier_NoModifier, pos);
    XObject_event_base((XObject*)cal, (XEvent*)&me);
}

/** @brief 注入一次左键释放。 */
static void xq_cal_release(XCalendarWidget* cal, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!cal) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton,
                     XKeyboardModifier_NoModifier, pos);
    XObject_event_base((XObject*)cal, (XEvent*)&me);
}

/** @brief 完整点击（按下 + 释放）。 */
static void xq_cal_click(XCalendarWidget* cal, int x, int y)
{
    xq_cal_press(cal, x, y);
    xq_cal_release(cal, x, y);
}

/** @brief 构造默认值（Qt 6.8 探针逐项实测）。 */
XQ_TEST(XCalendarWidget, constructorDefaults)
{
    XCalendarWidget* cal = XCalendarWidget_create(NULL, 0);
    XDate today;
    XDate sel;
    XDate dmin;
    XDate dmax;
    XSize hint;
    XSize minHint;
    XQ_REQUIRE(cal != NULL);

    /* 选中日期与显示页=当前日期（qcalendarwidget.cpp init
     * setSelectedDate(QDate::currentDate())）。 */
    today = XDate_currentDate();
    sel = XCalendarWidget_selectedDate(cal);
    XQ_EQ_I64(XDate_compare(&sel, &today), 0);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), XDate_year(&today));
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), XDate_month(&today));

    /* 探针实测：firstDow=1、grid=0、nav=1、mode=1、hdr=2、vhdr=1、
     * dateEditEnabled=1、delay=1500。 */
    XQ_EQ_I64(XCalendarWidget_firstDayOfWeek(cal), 1);
    XQ_CHECK(!XCalendarWidget_isGridVisible(cal));
    XQ_CHECK(XCalendarWidget_isNavigationBarVisible(cal));
    XQ_EQ_I64(XCalendarWidget_selectionMode(cal),
              XCalendarSelectionMode_SingleSelection);
    XQ_EQ_I64(XCalendarWidget_horizontalHeaderFormat(cal),
              XCalendarHeaderFormat_ShortDayNames);
    /* 枚举数值对齐 Qt：0=NoVerticalHeader、1=ISOWeekNumbers。 */
    XQ_EQ_I64(XCalendarWidget_verticalHeaderFormat(cal),
              XCalendarVerticalHeaderFormat_ISOWeekNumbers);
    XQ_CHECK(XCalendarWidget_isDateEditEnabled(cal));
    XQ_EQ_I64(XCalendarWidget_dateEditAcceptDelay(cal), 1500);

    /* 日期范围默认 [儒略日 1, 9999-12-31]（Qt 6.8 探针
     * minimumDate().toJulianDay()==1）。 */
    dmin = XCalendarWidget_minimumDate(cal);
    dmax = XCalendarWidget_maximumDate(cal);
    /* XDate 的儒略日字段即 m_jd（XDate_toJulianDay 未在头文件导出）。 */
    XQ_EQ_I64(dmin.m_jd, 1);
    XQ_EQ_I64(XDate_year(&dmax), 9999);
    XQ_EQ_I64(XDate_month(&dmax), 12);
    XQ_EQ_I64(XDate_day(&dmax), 31);

    /* sizeHint == minimumSizeHint 且由字体度量推导（Qt 6.8
     * `QSize QCalendarWidget::sizeHint() const { return minimumSizeHint(); }`）。 */
    hint = XWidget_sizeHint((XWidget*)cal);
    minHint = XWidget_minimumSizeHint((XWidget*)cal);
    XQ_EQ_I64(hint.width, minHint.width);
    XQ_EQ_I64(hint.height, minHint.height);
    XQ_CHECK(hint.width > 7 * 10);
    XQ_CHECK(hint.height > 6 * 10);

    /* NULL getter 与默认自洽（避免 getter 空值/默认互相矛盾）。 */
    XQ_EQ_I64(XCalendarWidget_horizontalHeaderFormat(NULL),
              XCalendarHeaderFormat_ShortDayNames);
    XQ_EQ_I64(XCalendarWidget_verticalHeaderFormat(NULL),
              XCalendarVerticalHeaderFormat_ISOWeekNumbers);
    XQ_CHECK(XCalendarWidget_isDateEditEnabled(NULL));

    /* 简单存取对：每周首日/网格/导航条。 */
    XCalendarWidget_setFirstDayOfWeek(cal, 7);
    XQ_EQ_I64(XCalendarWidget_firstDayOfWeek(cal), 7);
    XCalendarWidget_setGridVisible(cal, true);
    XQ_CHECK(XCalendarWidget_isGridVisible(cal));
    XCalendarWidget_setNavigationBarVisible(cal, false);
    XQ_CHECK(!XCalendarWidget_isNavigationBarVisible(cal));
    XCalendarWidget_setVerticalHeaderFormat(cal,
        XCalendarVerticalHeaderFormat_NoVerticalHeader);
    XQ_EQ_I64(XCalendarWidget_verticalHeaderFormat(cal),
              XCalendarVerticalHeaderFormat_NoVerticalHeader);

    XClassDelete((XClass*)cal);
}

/** @brief 选中/范围钳位联动 + 范围互钳（QCalendarModel 语义）+ 无效日期守卫。 */
XQ_TEST(XCalendarWidget, selectionAndRangeCoercion)
{
    XCalendarWidget* cal = XCalendarWidget_create(NULL, 0);
    XDate d;
    XDate got;
    XDate before;
    XConnection* conn;
    XQ_REQUIRE(cal != NULL);
    conn = XObject_connect_1(
        (XObject*)cal, (size_t)XCalendarWidget_selectionChanged_signal(cal),
        (XObject*)cal, xq_cal_onSelection, XConnectionType_Direct);
    XQ_CHECK_PTR(conn);

    /* 改选中日期 ⇒ 翻到该月 + 发 selectionChanged（实测 1 次）。 */
    xq_cal_selection = 0;
    d = XDate_create_date(2020, 6, 15);
    XCalendarWidget_setSelectedDate(cal, &d);
    XQ_EQ_I64(xq_cal_selection, 1);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), 2020);
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), 6);
    /* 同值重设：零信号、不动页（qcalendarwidget.cpp:2303-2310 早退）。 */
    XCalendarWidget_setSelectedDate(cal, &d);
    XQ_EQ_I64(xq_cal_selection, 1);

    /* 无效日期忽略（qcalendarwidget.cpp `if (!date.isValid()) return;`）。 */
    before = XCalendarWidget_selectedDate(cal);
    {
        XDate bad = XDate_create();          /* m_jd == -1：无效 */
        XDate minBefore = XCalendarWidget_minimumDate(cal);
        XCalendarWidget_setSelectedDate(cal, &bad);
        got = XCalendarWidget_selectedDate(cal);
        XQ_EQ_I64(XDate_compare(&got, &before), 0);
        XCalendarWidget_setMinimumDate(cal, &bad);
        got = XCalendarWidget_minimumDate(cal);
        /* 无效下界被忽略：默认儒略日 1 保持不变。 */
        XQ_EQ_I64(XDate_compare(&got, &minBefore), 0);
        XQ_EQ_I64(got.m_jd, 1);
    }

    /* 下界钳位吃掉选中值：翻到新选中月 + 发一次 selectionChanged。 */
    xq_cal_selection = 0;
    d = XDate_create_date(2021, 1, 1);
    XCalendarWidget_setMinimumDate(cal, &d);
    got = XCalendarWidget_selectedDate(cal);
    XQ_EQ_I64(XDate_compare(&got, &d), 0);
    XQ_EQ_I64(xq_cal_selection, 1);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), 2021);
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), 1);
    /* 同值重设下界：零信号。 */
    XCalendarWidget_setMinimumDate(cal, &d);
    XQ_EQ_I64(xq_cal_selection, 1);

    /* 上界在选中值之上：零信号。 */
    d = XDate_create_date(2021, 3, 1);
    XCalendarWidget_setMaximumDate(cal, &d);
    XQ_EQ_I64(xq_cal_selection, 1);

    /* 上界降到下界之下 ⇒ 下界跟着降（QCalendarModel::setMaximumDate），
     * 选中钳到新上界 + 一次 selectionChanged。 */
    d = XDate_create_date(2020, 1, 1);
    XCalendarWidget_setMaximumDate(cal, &d);
    got = XCalendarWidget_selectedDate(cal);
    XQ_EQ_I64(XDate_compare(&got, &d), 0);
    XQ_EQ_I64(xq_cal_selection, 2);
    got = XCalendarWidget_minimumDate(cal);
    XQ_EQ_I64(XDate_compare(&got, &d), 0);
    XClassDelete((XClass*)cal);

    /* 反向：先给下界 2021-05-05，再给更早的上界 2020-05-05 ⇒ 两者都变
     * 2020-05-05（Qt 探针实测）。 */
    {
        XCalendarWidget* cal2 = XCalendarWidget_create(NULL, 0);
        XDate lo = XDate_create_date(2021, 5, 5);
        XDate hi = XDate_create_date(2020, 5, 5);
        XQ_REQUIRE(cal2 != NULL);
        XCalendarWidget_setMinimumDate(cal2, &lo);
        XCalendarWidget_setMaximumDate(cal2, &hi);
        got = XCalendarWidget_minimumDate(cal2);
        XQ_EQ_I64(XDate_year(&got), 2020);
        XQ_EQ_I64(XDate_month(&got), 5);
        XQ_EQ_I64(XDate_day(&got), 5);
        got = XCalendarWidget_maximumDate(cal2);
        XQ_EQ_I64(XDate_compare(&got, &hi), 0);
        got = XCalendarWidget_selectedDate(cal2);
        XQ_EQ_I64(XDate_compare(&got, &hi), 0);
        XClassDelete((XClass*)cal2);
    }

    /* clear 后回到 Qt 的默认边界（JD 1 / 9999-12-31）。 */
    {
        XCalendarWidget* cal3 = XCalendarWidget_create(NULL, 0);
        XDate lo = XDate_create_date(2021, 5, 5);
        XQ_REQUIRE(cal3 != NULL);
        XCalendarWidget_setMinimumDate(cal3, &lo);
        XCalendarWidget_clearMinimumDate(cal3);
        got = XCalendarWidget_minimumDate(cal3);
        XQ_EQ_I64(got.m_jd, 1);
        XCalendarWidget_clearMaximumDate(cal3);
        got = XCalendarWidget_maximumDate(cal3);
        XQ_EQ_I64(XDate_year(&got), 9999);
        XQ_EQ_I64(XDate_month(&got), 12);
        XQ_EQ_I64(XDate_day(&got), 31);
        XClassDelete((XClass*)cal3);
    }
}

/** @brief 翻页 API：同页零信号、跨年进位、showSelectedDate/showToday。 */
XQ_TEST(XCalendarWidget, pageNavigation)
{
    XCalendarWidget* cal = XCalendarWidget_create(NULL, 0);
    XDate d;
    XDate today;
    XConnection* conn;
    XQ_REQUIRE(cal != NULL);
    conn = XObject_connect_1(
        (XObject*)cal,
        (size_t)XCalendarWidget_currentPageChanged_signal(cal, 0, 0),
        (XObject*)cal, xq_cal_onPage, XConnectionType_Direct);
    XQ_CHECK_PTR(conn);

    xq_cal_page = 0;
    XCalendarWidget_setCurrentPage(cal, 2024, 1);
    XQ_EQ_I64(xq_cal_page, 1);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), 2024);
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), 1);
    /* 同页重设：零信号（Qt showMonth 的变化判据）。 */
    XCalendarWidget_setCurrentPage(cal, 2024, 1);
    XQ_EQ_I64(xq_cal_page, 1);

    /* 跨年进位（Qt 实测 2024-12 → showNextMonth → 2025-1）。 */
    XCalendarWidget_setCurrentPage(cal, 2024, 12);
    XCalendarWidget_showNextMonth(cal);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), 2025);
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), 1);
    /* 反向跨年（2024-1 → showPreviousMonth → 2023-12）。 */
    XCalendarWidget_setCurrentPage(cal, 2024, 1);
    XCalendarWidget_showPreviousMonth(cal);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), 2023);
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), 12);

    /* 年翻页保持月份。 */
    XCalendarWidget_setCurrentPage(cal, 2024, 3);
    XCalendarWidget_showNextYear(cal);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), 2025);
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), 3);
    XCalendarWidget_showPreviousYear(cal);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), 2024);
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), 3);

    /* showSelectedDate：从别处回到选中日期所在月。 */
    d = XDate_create_date(2023, 8, 9);
    XCalendarWidget_setSelectedDate(cal, &d);
    XCalendarWidget_setCurrentPage(cal, 2020, 2);
    XCalendarWidget_showSelectedDate(cal);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), 2023);
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), 8);

    /* showToday：回到今日所在月（不改选中）。 */
    today = XDate_currentDate();
    XCalendarWidget_setCurrentPage(cal, 2019, 5);
    XCalendarWidget_showToday(cal);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), XDate_year(&today));
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), XDate_month(&today));

    XClassDelete((XClass*)cal);
}

/** @brief 网格点击联动（2024-01 首格=2024-01-01）+ 释放才发 clicked
 *         + NoSelection 语义 + ISO 周号列不可点。
 * @details 列几何按黑盒扫描定位（先点第 1 行找「选中日==1」与「==2」的
 *          像素区间）——2024-01-01 是周一，第一行七格恰是 1..7 号，
 *          扫描不会触发邻月翻页。 */
XQ_TEST(XCalendarWidget, gridClickSelectionOnRelease)
{
    XCalendarWidget* cal = XCalendarWidget_create(NULL, 0);
    XDate sel;
    int rowY = 52;   /* 导航 22 + 表头 18 ⇒ 第一行中心。 */
    int day1Start = -1;
    int day2Start = -1;
    int x;
    XQ_REQUIRE(cal != NULL);
    {
        XConnection* c1 = XObject_connect_1(
            (XObject*)cal,
            (size_t)XCalendarWidget_selectionChanged_signal(cal),
            (XObject*)cal, xq_cal_onSelection, XConnectionType_Direct);
        XConnection* c2 = XObject_connect_1(
            (XObject*)cal,
            (size_t)XCalendarWidget_clicked_signal(cal, NULL),
            (XObject*)cal, xq_cal_onClicked, XConnectionType_Direct);
        XQ_CHECK_PTR(c1);
        XQ_CHECK_PTR(c2);
    }
    XCalendarWidget_setCurrentPage(cal, 2024, 1);

    /* ISO 周号列（默认开启）：最左侧条带不可点（QCalendarModel 的竖向
     * 表头不接收点击）。 */
    xq_cal_selection = 0;
    xq_cal_clicked = 0;
    xq_cal_click(cal, 2, rowY);
    XQ_EQ_I64(xq_cal_clicked, 0);
    XQ_EQ_I64(xq_cal_selection, 0);

    /* 黑盒扫描第一行：找「选中日==1」「==2」的起始像素列。 */
    for (x = 0; x < 200; ++x) {
        xq_cal_click(cal, x, rowY);
        sel = XCalendarWidget_selectedDate(cal);
        if (day1Start < 0 && XDate_day(&sel) == 1) day1Start = x;
        else if (day1Start >= 0 && day2Start < 0 && XDate_day(&sel) == 2)
            day2Start = x;
        if (day2Start >= 0) break;
    }
    XQ_CHECK(day1Start > 0);   /* 0..weekW-1 是周号列 ⇒ 首列必然不从 0 起 */
    XQ_CHECK(day2Start > day1Start);
    {
        int day1Center = (day1Start + day2Start) / 2;
        int day2Center = day2Start + (day2Start - day1Start) / 2;

        /* 按下不改选中、不发 clicked（Qt 在释放时才 changeDate+clicked）。 */
        xq_cal_selection = 0;
        xq_cal_clicked = 0;
        xq_cal_press(cal, day1Center, rowY);
        XQ_EQ_I64(xq_cal_selection, 0);
        XQ_EQ_I64(xq_cal_clicked, 0);
        sel = XCalendarWidget_selectedDate(cal);
        /* 扫描在「选中日==2」的首个像素列 break，故扫描结束时选中是
         * 2 号；按下不改变选中（Qt 只在释放时 changeDate+clicked）。 */
        XQ_EQ_I64(XDate_day(&sel), 2);

        /* 释放：选中生效 + clicked 一次（载荷=释放点日期）。 */
        xq_cal_release(cal, day1Center, rowY);
        XQ_EQ_I64(xq_cal_selection, 1);
        XQ_EQ_I64(xq_cal_clicked, 1);
        XQ_EQ_I64(xq_cal_clickedDay, 1);
        sel = XCalendarWidget_selectedDate(cal);
        XQ_EQ_I64(XDate_year(&sel), 2024);
        XQ_EQ_I64(XDate_month(&sel), 1);
        XQ_EQ_I64(XDate_day(&sel), 1);

        /* 第二列 ⇒ 2 号。 */
        xq_cal_click(cal, day2Center, rowY);
        sel = XCalendarWidget_selectedDate(cal);
        XQ_EQ_I64(XDate_day(&sel), 2);
        XQ_EQ_I64(xq_cal_clickedDay, 2);

        /* NoSelection：不选中、不发 clicked（Qt readOnly 分支）。 */
        XCalendarWidget_setSelectionMode(cal,
                                         XCalendarSelectionMode_NoSelection);
        xq_cal_clicked = 0;
        xq_cal_click(cal, day1Center, rowY);
        sel = XCalendarWidget_selectedDate(cal);
        XQ_EQ_I64(XDate_day(&sel), 2);
        XQ_EQ_I64(xq_cal_clicked, 0);

        /* 恢复单选后同格点击生效。 */
        XCalendarWidget_setSelectionMode(cal,
                                         XCalendarSelectionMode_SingleSelection);
        xq_cal_click(cal, day1Center, rowY);
        sel = XCalendarWidget_selectedDate(cal);
        XQ_EQ_I64(XDate_day(&sel), 1);
    }

    XClassDelete((XClass*)cal);
}

/** @brief 月份下拉菜单（对标 Qt monthButton+monthMenu）：点月份文本弹出
 *         12 个条目、范围外置灰、选中条目翻页（gap 104）。 */
XQ_TEST(XCalendarWidget, monthMenu)
{
    XCalendarWidget* cal = XCalendarWidget_create(NULL, 0);
    XDate lo;
    int i;
    int enabledCount;
    XQ_REQUIRE(cal != NULL);
    XQ_CHECK(cal->m_monthMenu == NULL);   /* 懒创建 */

    XCalendarWidget_setCurrentPage(cal, 2024, 6);
    /* 月份文本命中区：标题「yyyy年M月」整串居中，月份段在年份段右侧。
     * 逐像素扫导航条（年份段点击只会进年份就地编辑，无副作用），
     * 直到月份菜单被懒创建。 */
    {
        int w = XWidget_width((XWidget*)cal);
        int x;
        for (x = w / 2; x < w && !cal->m_monthMenu; ++x)
            xq_cal_press(cal, x, 10);
    }
    /* 点击后菜单已建（12 个条目）。 */
    XQ_CHECK_PTR(cal->m_monthMenu);
    if (!cal->m_monthMenu) {
        XClassDelete((XClass*)cal);
        return;
    }
    for (i = 0; i < 12; ++i) XQ_CHECK_PTR(cal->m_monthActions[i]);
    /* 无范围收窄：12 个条目全可用（Qt updateMonthMenu 的 beg=1/end=12）。 */
    enabledCount = 0;
    for (i = 0; i < 12; ++i)
        if (XAction_isEnabled(cal->m_monthActions[i])) ++enabledCount;
    XQ_EQ_I64(enabledCount, 12);
    /* 条目文本「N月」。 */
    {
        const XString* ts = XAction_text_const(cal->m_monthActions[0]);
        const char* text = ts ? XString_toUtf8(ts) : NULL;
        XQ_CHECK(text != NULL);
        if (text) XQ_EQ_STR(text, "1月");
    }

    /* 范围收窄到 2024-03 起：显示 2024 年时 1、2 月置灰、3 月可用
     * （Qt updateMonthMenu 的 beg/end 分支）。 */
    lo = XDate_create_date(2024, 3, 1);
    XCalendarWidget_setMinimumDate(cal, &lo);
    XCalendarWidget_setCurrentPage(cal, 2024, 6);
    enabledCount = 0;
    for (i = 0; i < 12; ++i)
        if (XAction_isEnabled(cal->m_monthActions[i])) ++enabledCount;
    XQ_EQ_I64(enabledCount, 10);
    XQ_CHECK(!XAction_isEnabled(cal->m_monthActions[0]));
    XQ_CHECK(!XAction_isEnabled(cal->m_monthActions[1]));
    XQ_CHECK(XAction_isEnabled(cal->m_monthActions[2]));

    /* 触发 5 月条目 ⇒ 翻到 2024-05（Qt _q_monthChanged）。 */
    XAction_trigger(cal->m_monthActions[4]);
    XQ_EQ_I64(XCalendarWidget_yearShown(cal), 2024);
    XQ_EQ_I64(XCalendarWidget_monthShown(cal), 5);

    XClassDelete((XClass*)cal);
}

/** @brief ISO 周号列存在性/不可点 + sizeHint 随字体/网格/导航条变化
 *         （gap 105/107）。 */
XQ_TEST(XCalendarWidget, weekColumnAndSizeHint)
{
    XCalendarWidget* cal = XCalendarWidget_create(NULL, 0);
    XDate sel;
    XSize withWeek;
    XSize noWeek;
    XSize bigger;
    XSize withGrid;
    XSize noNav;
    XQ_REQUIRE(cal != NULL);
    XCalendarWidget_setCurrentPage(cal, 2024, 1);

    /* 默认 ISO 周号列：第一行最左像素点击无效果（该列不可选）。 */
    xq_cal_selection = 0;
    xq_cal_click(cal, 1, 52);
    XQ_EQ_I64(xq_cal_selection, 0);

    /* 关掉周号列后同一像素就是 1 号（2024-01-01 是周一、首格即 1 号）。 */
    XCalendarWidget_setVerticalHeaderFormat(cal,
        XCalendarVerticalHeaderFormat_NoVerticalHeader);
    xq_cal_click(cal, 1, 52);
    sel = XCalendarWidget_selectedDate(cal);
    XQ_EQ_I64(XDate_year(&sel), 2024);
    XQ_EQ_I64(XDate_month(&sel), 1);
    XQ_EQ_I64(XDate_day(&sel), 1);
    noWeek = XWidget_sizeHint((XWidget*)cal);
    withWeek = noWeek;
    XCalendarWidget_setVerticalHeaderFormat(cal,
        XCalendarVerticalHeaderFormat_ISOWeekNumbers);
    withWeek = XWidget_sizeHint((XWidget*)cal);
    /* 周号列计入宽度（Qt minimumSizeHint 的 cols=8 分支）。 */
    XQ_CHECK(withWeek.width > noWeek.width);

    /* 网格显示 ⇒ 宽高各 +1（Qt qcalendarwidget.cpp:2225-2228）。 */
    withGrid = withWeek;
    XCalendarWidget_setGridVisible(cal, true);
    withGrid = XWidget_sizeHint((XWidget*)cal);
    XQ_EQ_I64(withGrid.width, withWeek.width + 1);
    XQ_EQ_I64(withGrid.height, withWeek.height + 1);
    XCalendarWidget_setGridVisible(cal, false);

    /* 导航条隐藏 ⇒ 高度少一条导航条。 */
    noNav = withGrid;
    XCalendarWidget_setNavigationBarVisible(cal, false);
    noNav = XWidget_sizeHint((XWidget*)cal);
    XQ_CHECK(noNav.height < withGrid.height);
    XCalendarWidget_setNavigationBarVisible(cal, true);

    /* 字体变大 ⇒ 提示尺寸变大（由字体度量推导，非固定 280x200）。 */
    {
        XSize baseHint = XWidget_sizeHint((XWidget*)cal);
        XFont* font = XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 22,
                                      -1, false);
        XQ_REQUIRE(font != NULL);
        XWidget_setFont((XWidget*)cal, font);
        bigger = XWidget_sizeHint((XWidget*)cal);
        XQ_CHECK(bigger.height > baseHint.height);
        XClassDelete((XClass*)font);
    }

    XClassDelete((XClass*)cal);
}

#endif /* XGUI_ON && XWIDGET_ON && XCALENDARWIDGET_ON */
