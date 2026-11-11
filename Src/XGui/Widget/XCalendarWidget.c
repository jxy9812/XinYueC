/**
 * @file       XCalendarWidget.c
 * @brief      日历控件实现（对标 Qt 6.8 QCalendarWidget 公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 *             [已登记偏差] ① 月份下拉菜单未实现（qcalendarwidget.cpp
 *             monthMenu 分支无对应实现，月份文本区点击不响应）；
 *             ② ISO 周数列未绘制（网格固定 7 列，verticalHeaderFormat
 *             仅存取）；③ calendar()/setCalendar(QCalendar) 无 C 承载
 *             （本库无 XCalendar 类型）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XCalendarWidget.h"
#include "XStringUtils.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XLineEdit.h" /* 年份就地编辑器：真实编辑控件接入输入法/屏幕键盘链路 */
#include "XMenu.h"     /* 月份下拉菜单（对标 Qt monthBeaner+monthMenu） */
#include "XAction.h"   /* 月份条目 */
#include "XWindow.h" /* 弹层窗口句柄平台抓取（年份编辑期解/复抓，XDateTimeEdit.c 同款） */
#if XVIRTUALKEYBOARD_ON
#include "XGuiApplication.h" /* virtualKeyboard()：年份编辑数字键盘单例 */
#include "XVirtualKeyboard.h"
#endif
#include "XWidget_Protected.h"

#if XWIDGET_ON && XCALENDARWIDGET_ON

/* ==================== 内部工具 ==================== */

static void xcal_endYearEdit(XCalendarWidget* cal, bool commit);
static bool VXCalendarWidget_eventFilter(XObject* self, XObject* watched,
                                         XEvent* event);
static void VXCalendarWidget_deinit(XCalendarWidget* self);

/** @brief 星期文案表（列下标 0=周一..6=周日，对应 XDate_dayOfWeek()-1）：
 *         单字/短名/长名三档，渲染表头按 HorizontalHeaderFormat 取用。 */
static const char* const xcal_daySingleNames[7] = {
    "\xE4\xB8\x80", "\xE4\xBA\x8C", "\xE4\xB8\x89", "\xE5\x9B\x9B",
    "\xE4\xBA\x94", "\xE5\x85\xAD", "\xE6\x97\xA5" };
static const char* const xcal_dayShortNames[7] = {
    "\xE5\x91\xA8\xE4\xB8\x80", "\xE5\x91\xA8\xE4\xBA\x8C",
    "\xE5\x91\xA8\xE4\xB8\x89", "\xE5\x91\xA8\xE5\x9B\x9B",
    "\xE5\x91\xA8\xE4\xBA\x94", "\xE5\x91\xA8\xE5\x85\xAD",
    "\xE5\x91\xA8\xE6\x97\xA5" };
static const char* const xcal_dayLongNames[7] = {
    "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xB8\x80", "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xBA\x8C",
    "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xB8\x89", "\xE6\x98\x9F\xE6\x9C\x9F\xE5\x9B\x9B",
    "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xBA\x94", "\xE6\x98\x9F\xE6\x9C\x9F\xE5\x85\xAD",
    "\xE6\x98\x9F\xE6\x9C\x9F\xE6\x97\xA5" };

/** @brief 导航条几何常量（对标 QCalendarWidgetPrivate::createNavigationBar
 *         的 [< 月份 年份 >] 结构：左右各一枚月份箭头，中央年份/月份文本；
 *         Qt 6.8 无年箭头，年份导航经年份文本就地编辑承担）。 */
#define XCAL_NAV_H 22          /**< 导航条高度。 */
#define XCAL_HEADER_H 18       /**< 星期表头高度。 */
#define XCAL_ROW_H 24          /**< 日期行高。 */
#define XCAL_ROWS 6            /**< 网格行数（对标 Qt 6 行网格）。 */
#define XCAL_ARROW_W 18        /**< 箭头按钮宽（缺陷4：14→18，命中与
                                    绘制经 xcal_arrowRect 同源放大）。 */

/** @brief 导航箭头矩形（side<0=左侧上一月箭头，side>0=右侧下一月箭头）。 */
static XRect xcal_arrowRect(const XCalendarWidget* cal, int side)
{
    XRect r;
    int w = XWidget_width((XWidget*)cal);
    XRect_init(&r, (side < 0) ? 4 : w - XCAL_ARROW_W - 4, 2,
               XCAL_ARROW_W, XCAL_NAV_H - 4);
    return r;
}

/** @brief 导航标题「yyyy年M月」布局（缺陷3：绘制与命中同源）。
 * @details 整串以控件字体经 XPainter_textWidth 精确量宽后水平居中；
 *          年份子矩形=标题起点..「年」字末（点击进就地编辑），月份
 *          子矩形=其后至标题末。对标 Qt updateMonthMenu 的单一
 *          month/year 合成标签（qcalendarwidget.cpp:1820-1845）——
 *          旧实现两段文本按估算点位独立摆放，年份串实宽溢出估算位与
 *          月份串重叠（月份数字被「年」字形盖住，实测呈「2026年月」）。
 * @param cal       目标日历。
 * @param yearOut   出：年份命中/覆盖子矩形（可为 NULL）。
 * @param monthOut  出：月份命中子矩形（可为 NULL）。
 * @return 标题左缘 x；布局失效（量宽为 0）返回 -1（出矩形为零矩形）。 */
static int xcal_titleLayout(const XCalendarWidget* cal,
                            XRect* yearOut, XRect* monthOut)
{
    char title[32];
    char yearPart[16];
    XFont font;
    int w;
    int tw;
    int yw;
    int tx;
    if (yearOut) XRect_init(yearOut, 0, 0, 0, 0);
    if (monthOut) XRect_init(monthOut, 0, 0, 0, 0);
    if (!cal) return -1;
    w = XWidget_width((XWidget*)cal);
    XSnprintf(title, sizeof(title), "%04d\xE5\xB9\xB4%d\xE6\x9C\x88",
              cal->m_shownYear, cal->m_shownMonth);
    XSnprintf(yearPart, sizeof(yearPart), "%04d\xE5\xB9\xB4",
              cal->m_shownYear);
    font = XWidget_font((XWidget*)cal);
    tw = XPainter_textWidth(&font, title);
    yw = XPainter_textWidth(&font, yearPart);
    /* 显式 XClass 转换：XClassDeinit 为 XClass_deinit_base 裸别名
     * 宏，直呼指针类型不符（XChartView.c 同款注释）。 */
    XClassDeinit((XClass*)&font);
    if (tw <= 0) return -1;
    tx = (w - tw) / 2;
    if (tx < 0) tx = 0;
    if (yw < 0) yw = 0;
    if (yw > tw) yw = tw;
    if (yearOut)
        XRect_init(yearOut, tx, 2, yw > 0 ? yw : 1, XCAL_NAV_H - 4);
    if (monthOut)
        XRect_init(monthOut, tx + yw, 2, tw - yw > 0 ? tw - yw : 1,
                   XCAL_NAV_H - 4);
    return tx;
}

/** @brief 绘制导航月箭头的实心三角（缺陷4：宽 12px、高 10px，远超旧
 *         样式链 ToolButton 箭头的 ~5px——其箭头固定 8x8 rect 内取
 *         1/3 半径）。填充行近似（逐列半高递增，与 XCommonStyle
 *         xcs_drawArrow 无 POLYGON 回退同一手法）。
 * @param painter 目标画笔。
 * @param r       箭头按钮矩形（三角形取中心）。
 * @param left    true=左三角（上一月），false=右三角（下一月）。
 * @param color   三角颜色（ARGB32）。 */
static void xcal_drawNavArrow(XPainter* painter, const XRect* r,
                              bool left, uint32_t color)
{
    int cx = r->x + r->width / 2;
    int cy = r->y + r->height / 2;
    int hw = 6; /* 半宽 6 → 全宽 12px。 */
    int hh = 5; /* 半高 5 → 全高 10px。 */
    int k;
    for (k = 0; k < hw * 2; ++k) {
        int halfH = (k + 1) * hh / (hw * 2);
        int x = left ? (cx - hw + k) : (cx + hw - k - 1);
        XRect row;
        if (halfH <= 0) continue;
        XRect_init(&row, x, cy - halfH, 1, halfH * 2);
        XPainter_fillRect(painter, &row, color);
    }
}

/** @brief 绘制导航箭头按钮底（凸起 1px 斜面：左/上 Light、右/下
 *         Dark；无样式回退路径用，样式路径由 CC_ToolButton 供给）。 */
static void xcal_drawNavBevel(XPainter* painter, const XRect* r,
                              uint32_t base, uint32_t light, uint32_t dark)
{
    XRect f;
    XPainter_fillRect(painter, r, base);
    XRect_init(&f, r->x, r->y, r->width, 1);
    XPainter_fillRect(painter, &f, light);
    XRect_init(&f, r->x, r->y, 1, r->height);
    XPainter_fillRect(painter, &f, light);
    XRect_init(&f, r->x, r->y + r->height - 1, r->width, 1);
    XPainter_fillRect(painter, &f, dark);
    XRect_init(&f, r->x + r->width - 1, r->y, 1, r->height);
    XPainter_fillRect(painter, &f, dark);
}

/** @brief 查询页 (year,month) 是否落在允许范围内（对标 Qt updateMonthMenu
 *         的 prev/next 使能判据：翻页目标月份起点越界即禁止）。 */
static bool xcal_pageInRange(const XCalendarWidget* cal, int year, int month)
{
    if (cal->m_minSet) {
        if (year < XDate_year(&cal->m_min)) return false;
        if (year == XDate_year(&cal->m_min) &&
            month < XDate_month(&cal->m_min)) return false;
    }
    if (cal->m_maxSet) {
        if (year > XDate_year(&cal->m_max)) return false;
        if (year == XDate_year(&cal->m_max) &&
            month > XDate_month(&cal->m_max)) return false;
    }
    return true;
}

/** @brief 按儒略日造日期（XDate 的 m_jd 直存语义，XDate.c 的
 *         XDate_fromJulianDay 未在头文件导出——本文件自带同义构造，
 *         仅用于「儒略日 1」这一不可由年月日表达的默认下界）。 */
static XDate xcal_dateFromJulianDay(int64_t jd)
{
    XDate d;
    d.m_jd = jd;
    return d;
}

/** @brief 某公历年是否有 ISO 第 53 周（该年 1 月 1 日是周四，或闰年且
 *         1 月 1 日是周三）。 */
static int xcal_isoWeeksInYear(int year)
{
    XDate jan1 = XDate_create_date(year, 1, 1);
    int dow;
    if (!XDate_isValid(&jan1)) return 52;
    dow = XDate_dayOfWeek(&jan1); /* 1=Mon..7=Sun */
    if (dow == 4) return 53;
    if (dow == 3 && XDate_isLeapYear_static(year)) return 53;
    return 52;
}

/** @brief ISO 8601 周号（对标 QDate::weekNumber）。
 *  @details 周 1 = 含 1 月 4 日的那一周（周一为首日）：
 *           第 o 天（1 起）、星期 d（周一=1）⇒ week=(o-d+10)/7；
 *           week<1 ⇒ 上一年最后一周；week>本年周数 ⇒ 第 1 周。
 *           与 QDate::weekNumber() 逐值对齐（测试用 2021-01-01=53、
 *           2023-01-01=52、2024-01-01=1、2024-12-30=1 校验）。 */
static int xcal_isoWeekNumber(const XDate* date)
{
    int doy;
    int dow;
    int week;
    int year;
    if (!date || !XDate_isValid(date)) return 0;
    doy = XDate_dayOfYear(date);
    dow = XDate_dayOfWeek(date);
    year = XDate_year(date);
    week = (doy - dow + 10) / 7;
    if (week < 1) {
        week = xcal_isoWeeksInYear(year - 1);
    } else if (week > 52) {
        int maxWeek = xcal_isoWeeksInYear(year);
        if (week > maxWeek) week = 1;
    }
    return week;
}

/** @brief ISO 周数列宽（0=不显示；按字体实测「53」宽 + 左右留白，不低于
 *         18px——与绘制/命中同源，字号变化自动跟随）。 */
static int xcal_weekColumnWidth(const XCalendarWidget* cal)
{
    XFont font;
    int tw;
    if (!cal || cal->m_verticalHeaderFormat !=
                (int)XCalendarVerticalHeaderFormat_ISOWeekNumbers)
        return 0;
    font = XWidget_font((XWidget*)cal);
    tw = XPainter_textWidth(&font, "53") + 10;
    XClassDeinit((XClass*)&font);
    if (tw < 18) tw = 18;
    return tw;
}

/** @brief 日期网格左缘（周号列宽；无周号列时为 0）。 */
static int xcal_gridLeft(const XCalendarWidget* cal)
{
    return xcal_weekColumnWidth(cal);
}

/** @brief 单列日宽（剩余宽度 7 等分；容器过窄时退化为 1px 防除零）。 */
static int xcal_dayColWidth(const XCalendarWidget* cal)
{
    int w = XWidget_width((XWidget*)cal) - xcal_gridLeft(cal);
    return w > 7 ? w / 7 : (w > 0 ? w : 1);
}

/** @brief 网格首列（cell 列号 0 的本地 x）。 */
static int xcal_dayColX(const XCalendarWidget* cal, int col)
{
    return xcal_gridLeft(cal) + col * xcal_dayColWidth(cal);
}

/** @brief 计算 sizeHint/minimumSizeHint（对标 Qt QCalendarWidget::
 *         minimumSizeHint，qcalendarwidget.cpp:2186-2264，`sizeHint()`
 *         直接返回它）：按字体实测表头/日期/周号文本宽定列宽，行高按
 *         表头与日期文本高 + 行距，再乘行/列数并加导航条高。
 *         本控件绘制几何用固定行高 XCAL_ROW_H/表头 XCAL_HEADER_H/导航
 *         XCAL_NAV_H ⇒ 提示值与该几何同源（不小于常量），字号变大时
 *         自动增高，不再固定 280x200。 */
static void xcal_updateSizeHint(XCalendarWidget* cal)
{
    XSize hint;
    XFont font;
    int colW = 0;
    int rowH = 0;
    int textH;
    int weekW;
    int i;
    int rows;
    if (!cal) return;
    font = XWidget_font((XWidget*)cal);
    textH = XPainter_textHeight(&font);
    weekW = xcal_weekColumnWidth(cal);
    if (cal->m_horizontalHeaderFormat != (int)XCalendarHeaderFormat_NoHorizontalHeader) {
        const char* const* names = xcal_dayShortNames;
        if (cal->m_horizontalHeaderFormat ==
            (int)XCalendarHeaderFormat_SingleLetterDayNames)
            names = xcal_daySingleNames;
        else if (cal->m_horizontalHeaderFormat ==
                 (int)XCalendarHeaderFormat_LongDayNames)
            names = xcal_dayLongNames;
        for (i = 0; i < 7; ++i) {
            int tw = XPainter_textWidth(&font, names[i]) + 10;
            if (tw > colW) colW = tw;
        }
    }
    for (i = 1; i <= 31; ++i) {
        char buf[8];
        int tw;
        XSnprintf(buf, sizeof(buf), "%d", i);
        tw = XPainter_textWidth(&font, buf) + 10;
        if (tw > colW) colW = tw;
    }
    XClassDeinit((XClass*)&font);
    if (colW < 20) colW = 20;
    if (textH < XCAL_ROW_H) textH = XCAL_ROW_H;
    rowH = textH;
    rows = (cal->m_horizontalHeaderFormat !=
            (int)XCalendarHeaderFormat_NoHorizontalHeader) ? 7 : 6;
    hint.width = weekW + colW * 7;
    hint.height = (cal->m_navBarVisible ? XCAL_NAV_H : 0) + rowH * rows;
    if (cal->m_gridVisible) {
        /* Qt 在网格显示时给 cell 宽高各 +1（qcalendarwidget.cpp:2225-2228
         * `// hardcoded in tableview`）。 */
        hint.width += 1;
        hint.height += 1;
    }
    XWidget_setSizeHint((XWidget*)cal, &hint);
    /* Qt 6.8：sizeHint() 直接返回 minimumSizeHint()（两者恒等）。 */
    XWidget_setMinimumSizeHint((XWidget*)cal, &hint);
}

/* ==================== 月份下拉菜单（对标 monthButton+monthMenu） ==================== */

static void xcal_updateMonthMenu(XCalendarWidget* cal);
static void xcal_monthTriggeredSlot(XObject* receiver, XVarList* args);

/** @brief 懒创建 12 个月份条目的下拉菜单（对标 Qt 的
 *         `monthMenu = new QMenu(monthButton)` + 12 个 addAction）。 */
static void xcal_ensureMonthMenu(XCalendarWidget* cal)
{
    int i;
    if (!cal || cal->m_monthMenu) return;
    cal->m_monthMenu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       (XWidget*)cal, "qt_calendar_monthmenu");
    if (!cal->m_monthMenu) return;
    for (i = 0; i < 12; ++i) {
        char name[16];
        XSnprintf(name, sizeof(name), "%d\xE6\x9C\x88", i + 1); /* "%d月" */
        cal->m_monthActions[i] = XMenu_addAction_2(cal->m_monthMenu, name);
    }
    XObject_connect_1((XObject*)cal->m_monthMenu,
                      (size_t)XMenu_triggered_signal(cal->m_monthMenu, NULL),
                      (XObject*)cal, xcal_monthTriggeredSlot,
                      XConnectionType_Direct);
}

/** @brief 按当前页/范围刷新月份条目使能态（对标 Qt updateMonthMenu：
 *         仅当显示年恰为范围最小/最大年时按月份收窄，界外月份置灰）。 */
static void xcal_updateMonthMenu(XCalendarWidget* cal)
{
    int beg = 1;
    int end = 12;
    int i;
    if (!cal || !cal->m_monthMenu) return;
    if (cal->m_minSet && cal->m_shownYear == XDate_year(&cal->m_min))
        beg = XDate_month(&cal->m_min);
    if (cal->m_maxSet && cal->m_shownYear == XDate_year(&cal->m_max))
        end = XDate_month(&cal->m_max);
    for (i = 0; i < 12; ++i) {
        if (!cal->m_monthActions[i]) continue;
        XAction_setEnabled(cal->m_monthActions[i],
                           (i + 1) >= beg && (i + 1) <= end);
    }
}

/** @brief 月份条目被触发：翻到该月（对标 Qt _q_monthChanged →
 *         updateCurrentPage；条目已按范围置灰，直接 setCurrentPage）。 */
static void xcal_monthTriggeredSlot(XObject* receiver, XVarList* args)
{
    XCalendarWidget* cal = (XCalendarWidget*)receiver;
    int i;
    if (!cal || !args) return;
    XVarList_args_1(args, XAction*, act);
    if (!act) return;
    for (i = 0; i < 12; ++i) {
        if (cal->m_monthActions[i] == act) {
            XCalendarWidget_setCurrentPage(cal, cal->m_shownYear, i + 1);
            return;
        }
    }
}

/** @brief 在月份文本下方弹出月份菜单（对标 Qt monthButton 的菜单弹出）。 */
static void xcal_showMonthMenu(XCalendarWidget* cal)
{
    XRect monthR;
    XPoint local;
    XPoint global;
    if (!cal) return;
    xcal_ensureMonthMenu(cal);
    xcal_updateMonthMenu(cal);
    if (!cal->m_monthMenu) return;
    if (xcal_titleLayout(cal, NULL, &monthR) < 0) return;
    local.x = monthR.x;
    local.y = monthR.y + monthR.height;
    global = XWidget_mapToGlobal((XWidget*)cal, &local);
    XMenu_popup(cal->m_monthMenu, &global);
}

static void xcal_emitDate(XCalendarWidget* self, size_t signal, const XDate* d)
{
    /* 载荷按**借用指针**承载：Qt 的 clicked/activated 形参是
     * `const QDate&`，本项目日期/时间信号同口径（XDateTimeEdit
     * dateChanged、XDateEdit userDateChanged、XTimeEdit timeChanged
     * 均为 `XVar(XDate*, ptr)`）。指针指向调用方的日期存储，仅保证
     * 同步发射期间有效；队列连接需要跨线程时序时调用方自行复制。 */
    XDate copy = *d;
    XDate* ptr = &copy;
    XVarList* args = XVarList_Create(XVar(XDate*, ptr));
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

/** @brief 字体/样式变化：按新字体度量重算 sizeHint（对标 Qt
 *         ensurePolished + cachedSizeHint 失效口径）。 */
static void VX_calendar_changeEvent(XWidget* self, XEvent* event)
{
    XCalendarWidget* cal = (XCalendarWidget*)self;
    if (!cal || !event) return;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_FONT_CHANGE:
    case XEVENT_TYPE_STYLE_CHANGE:
        xcal_updateSizeHint(cal);
        break;
    default:
        break;
    }
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

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
    int cell;
    char buf[16];
    XDate first;
    XFont font;
    const char* const* headerNames;
    if (!cal || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    image = XWidget_paintImage(self);
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
    /* 控件字体先于导航条生效（缺陷3 配套）：标题宽度经
     * XPainter_textWidth 以同一字体精确量取并居中（量/画同源；旧实现
     * 标题画在 setFont 之前，与表头/日期格字体不一致）。字体对象在整帧
     * 绘制期复用（逐格新建/释放字体代价随格数线性堆，AGENTS.md 重绘
     * 红线口径）。 */
    {
        font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
    }
    y = 0;
    if (cal->m_navBarVisible) {
        XRect titleMonth;
        int titleX = xcal_titleLayout(cal, NULL, &titleMonth);
        /* 月份子矩形当前仅随布局同出（月份下拉未实现，见 deferred）。 */
        (void)titleMonth;
#if XSTYLE_ON
        if (XWidget_style((const XWidget*)self) != NULL) {
            /* 对标 QCalendarWidget 导航条（qcalendarwidget.cpp:1784-1794
             * headerLayout）：左侧 prevMonth 箭头（CC_ToolButton，走
             * SP_ArrowLeft 口径）+ 中央「yyyy年M月」标题 + 右侧
             * nextMonth 箭头。Qt 6.8 无年箭头，年份导航由年份文本就地
             * 编辑承担。缺陷4：按钮选项不再带 AutoRaise（样式恒画按钮
             * 底），大三角由本控件直绘（样式链箭头仅 ~5px）。 */
            XStyle* style = XWidget_style((const XWidget*)self);
            XStyleOption bar;
            XStyleOption_init(&bar, XStyleCC_ToolButton);
            {
                XRect nr;
                XRect_init(&nr, 0, 0, w, XCAL_NAV_H);
                bar.m_rect = nr;
            }
            bar.m_state = XWidget_isEnabled((XWidget*)cal)
                ? XStyleState_Enabled | XStyleState_AutoRaise : 0;
#if XPALETTE_ON
            bar.m_palette = XWidget_palette((XWidget*)cal);
#endif
            XStyle_drawComplexControl(style, XStyleCC_ToolButton, &bar,
                                      &painter, (XWidget*)cal);
            {
                int side;
                for (side = 0; side < 2; ++side) {
                    XStyleOption tb;
                    XStyleOption_init(&tb, XStyleCC_ToolButton);
                    tb.m_rect = xcal_arrowRect(cal, (side == 0) ? -1 : 1);
                    tb.m_state = XWidget_isEnabled((XWidget*)cal)
                        ? XStyleState_Enabled : 0;
#if XPALETTE_ON
                    tb.m_palette = XWidget_palette((XWidget*)cal);
#endif
                    XStyle_drawComplexControl(style, XStyleCC_ToolButton,
                                              &tb, &painter,
                                              (XWidget*)cal);
                    xcal_drawNavArrow(&painter, &tb.m_rect, (side == 0),
                                      windowText);
                }
            }
            y = XCAL_NAV_H;
        } else
#endif /* XSTYLE_ON */
        {
            uint32_t light = 0xFFE0E0E0u;
            uint32_t dark = 0xFF606060u;
#if XPALETTE_ON
            {
                XPalette palette = XWidget_palette(self);
                XColor c;
                c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                   XPaletteColorRole_Light);
                light = XColor_rgba(&c);
                c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                   XPaletteColorRole_Dark);
                dark = XColor_rgba(&c);
            }
#endif
            XRect_init(&r, 0, 0, w, XCAL_NAV_H);
            XPainter_fillRect(&painter, &r, mid);
            /* 导航箭头：<（上一月）在左、>（下一月）在右，与命中测试
             * xcal_arrowRect 同源。缺陷4：按钮底（凸起斜面）+ 大三角。 */
            {
                int side;
                for (side = 0; side < 2; ++side) {
                    XRect ar = xcal_arrowRect(cal, (side == 0) ? -1 : 1);
                    xcal_drawNavBevel(&painter, &ar, base, light, dark);
                    xcal_drawNavArrow(&painter, &ar, (side == 0),
                                      windowText);
                }
            }
            y = XCAL_NAV_H;
        }
        /* 中央「yyyy年M月」合成标题（缺陷3）：整串一次量宽居中绘制，
         * 年份/月份命中子矩形与绘制同源（xcal_titleLayout）——旧实现
         * 两段按估算点位独立摆放致重叠（月份数字被「年」字形盖住）。 */
        {
            char title[32];
            XSnprintf(title, sizeof(title), "%04d\xE5\xB9\xB4%d\xE6\x9C\x88",
                      cal->m_shownYear, cal->m_shownMonth);
            XPainter_drawText(&painter, titleX >= 0 ? titleX : 0, 15,
                              title, windowText);
        }
        /* 年份就地编辑态由真实编辑控件（m_yearEdit 子件）覆盖渲染，
         * 无需手绘缓冲/光标。 */
    }
    /* 星期表头：列序随 firstDayOfWeek 旋转、文案按
     * horizontalHeaderFormat 三档取用（0=不显示；对标 Qt 表头语义，
     * 修复此前格式仅存储、表头恒为单字且不随周首日旋转）。 */
    headerNames = NULL;
    if (cal->m_horizontalHeaderFormat ==
        (int)XCalendarHeaderFormat_SingleLetterDayNames)
        headerNames = xcal_daySingleNames;
    else if (cal->m_horizontalHeaderFormat ==
             (int)XCalendarHeaderFormat_ShortDayNames)
        headerNames = xcal_dayShortNames;
    else if (cal->m_horizontalHeaderFormat ==
             (int)XCalendarHeaderFormat_LongDayNames)
        headerNames = xcal_dayLongNames;
    if (headerNames != NULL) {
        for (cell = 0; cell < 7; ++cell) {
            /* 列 cell 的星期号：周首日 + 列序（1=周一..7=周日 基）。 */
            int dow1 = ((cal->m_firstDayOfWeek >= 1 &&
                         cal->m_firstDayOfWeek <= 7)
                            ? cal->m_firstDayOfWeek : 1) + cell;
            int idx = (dow1 - 1) % 7;
            int colW = xcal_dayColWidth(cal);
            int tw;
            int dx;
            tw = XPainter_textWidth(&font, headerNames[idx]);
            dx = xcal_dayColX(cal, cell) + (colW - tw) / 2;
            XPainter_drawText(&painter, dx >= 0 ? dx : 0, y + 14,
                              headerNames[idx], mid);
        }
    }
    y += XCAL_HEADER_H;
    XDate_setDate(&first, cal->m_shownYear, cal->m_shownMonth, 1);
    dayOfWeek = XDate_dayOfWeek(&first);
    firstCol = (dayOfWeek - 1 + 7) % 7;
    if (cal->m_firstDayOfWeek >= 1 && cal->m_firstDayOfWeek <= 7)
        firstCol = (dayOfWeek - cal->m_firstDayOfWeek + 7) % 7;
    {
        int dim = XDate_daysInMonth(&first);
        int colW = xcal_dayColWidth(cal);
        int weekW = xcal_gridLeft(cal);
        int mondayCol = 0;
        if (weekW > 0 && cal->m_firstDayOfWeek >= 1 &&
            cal->m_firstDayOfWeek <= 7)
            mondayCol = (1 - cal->m_firstDayOfWeek + 7) % 7;
        if (weekW > 0) {
            /* ISO 周号列（对齐 QCalendarModel::data 的
             * `dateForCell(row, columnForDayOfWeek(Monday)).weekNumber()`）。 */
            int row;
            for (row = 0; row < XCAL_ROWS; ++row) {
                int dayOffset = row * 7 + mondayCol - firstCol;
                XDate monday = XDate_addDays(&first, dayOffset);
                int wk = xcal_isoWeekNumber(&monday);
                char wbuf[8];
                int tw;
                int dx;
                if (wk <= 0) continue;
                XSnprintf(wbuf, sizeof(wbuf), "%d", wk);
                tw = XPainter_textWidth(&font, wbuf);
                dx = (weekW - tw) / 2;
                if (dx < 0) dx = 0;
                XPainter_drawText(&painter, dx,
                                  y + row * XCAL_ROW_H + 14, wbuf, mid);
            }
        }
        for (cell = 0; cell < XCAL_ROWS * 7; ++cell) {
            /* 42 格全网格：越出本月的邻月日期以 Mid 弱化色渲染（对标
             * QCalendarModel 邻月格子），点击可选中邻月日期（见鼠标按下）。 */
            int dayNum = cell - firstCol + 1;
            int row = cell / 7;
            int col7 = cell % 7;
            int tw;
            int cx;
            int cy = y + row * XCAL_ROW_H + 14;
            XDate d;
            bool inMonth = (dayNum >= 1 && dayNum <= dim);
            if (inMonth) {
                XDate_setDate(&d, cal->m_shownYear, cal->m_shownMonth, dayNum);
                XSnprintf(buf, sizeof(buf), "%d", dayNum);
            } else {
                d = XDate_addDays(&first, dayNum - 1);
                XSnprintf(buf, sizeof(buf), "%d", XDate_day(&d));
            }
            if (cal->m_selectionMode ==
                    (int)XCalendarSelectionMode_SingleSelection &&
                XDate_compare(&d, &cal->m_selected) == 0) {
                XRect bg;
                XRect_init(&bg, xcal_dayColX(cal, col7),
                           y + row * XCAL_ROW_H, colW, XCAL_ROW_H);
                XPainter_fillRect(&painter, &bg, highlight);
            }
            {
                tw = XPainter_textWidth(&font, buf);
            }
            cx = xcal_dayColX(cal, col7) + (colW - tw) / 2;
            if (cx < 0) cx = 0;
            XPainter_drawText(&painter, cx, cy, buf,
                              inMonth ? windowText : mid);
            /* 今日标记：日期数字下加下划线（对标 Qt QCalendarModel 数据
             * 里 Today 角色的 Underline 文本格式，paintCell 呈现；恒开
             * ——原 setShowTodayDate 开关属自造 API 已删除）。 */
            {
                XDate today = XDate_currentDate();
                if (XDate_compare(&d, &today) == 0) {
                    XRect ul;
                    XRect_init(&ul, cx, cy + 3, tw > 0 ? tw : 1, 1);
                    XPainter_fillRect(&painter, &ul, windowText);
                }
            }
        }
        if (cal->m_gridVisible) {
            int row;
            for (row = 0; row <= XCAL_ROWS; ++row) {
                XRect hr;
                XRect_init(&hr, 0, y + row * XCAL_ROW_H, w, 1);
                XPainter_fillRect(&painter, &hr, mid);
            }
            if (weekW > 0) {
                XRect vr;
                XRect_init(&vr, weekW - 1, y, 1, XCAL_ROWS * XCAL_ROW_H);
                XPainter_fillRect(&painter, &vr, mid);
            }
        }
    }
    XClassDeinit((XClass*)&font);
    XPainter_deinit(&painter);
}

/** @brief 年份编辑提交槽（Return/editingFinished → 提交并收起）。 */
static void xcal_yearEditCommitSlot(XObject* receiver, XVarList* args)
{
    XCalendarWidget* cal = (XCalendarWidget*)receiver;
    (void)args;
    if (cal) xcal_endYearEdit(cal, true);
}

#if XVIRTUALKEYBOARD_ON
/** @brief 年份编辑期键盘确认键接线句柄（begin 连接 / end 断开配对记账；
 *         文件级静态：键盘为应用单例，同一时刻至多一份年份编辑会话有
 *         效，句柄不随控件实例携带）。 */
/* 确认键接线断开按「信号+接收者+槽」身份检索（XObject_disconnect_1），
 * 不缓存 XConnection 裸指针：缓存指针在连接对象被其他通路销毁/重建后
 * 成为悬垂，disconnect_2 首解引 conn->signal 即 AV（实机 4 次同偏移
 * 0x36c26c 崩溃实证，皆发生在年份编辑会话序列）；检索式断开按值匹配、
 * 幂等，连接已不存在时安全返回 false。 */
static void xcal_disconnectKbReady(XCalendarWidget* cal)
{
    XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
    if (!cal || !kb) return;
    XObject_disconnect_1((XObject*)kb,
                         (size_t)XVirtualKeyboard_ready_signal(NULL),
                         (XObject*)cal, xcal_yearEditCommitSlot);
}

/** @brief 落焦前布锚：把日历当前顶层设为键盘悬浮锚。
 *  @details 弹层内编辑时日历当前顶层=日历弹层容器（Popup 型独立顶层，
 *           widget 父链与桥接窗 transient parent 均无主窗口反向引用），
 *           键盘侧 popup 时自动从应用顶层表解析主窗口为实际锚——键盘
 *           保持独立顶层窗口、悬浮锚定主窗口底部全宽并 raise 压过日历
 *           弹层（对标 QVirtualKeyboardInputPanel 独立顶层形态）。须在
 *           setFocus 之前调用：焦点驱动的自动弹出路径（守护边沿/面板
 *           show）都发生在锚生效期，弹出必为悬浮形态。 */
static void xcal_yearEditAnchorKeyboard(XCalendarWidget* cal)
{
    XVirtualKeyboard* kb;
    if (!cal) return;
    kb = XGuiApplication_virtualKeyboard();
    if (!kb) return;
    XVirtualKeyboard_setHostWindow(kb,
                                   XWidget_topLevelWidget((XWidget*)cal));
}

/** @brief 屏幕键盘（数字布局）随编辑器弹出（顶层悬浮形态）。
 *  @details 锚已在布锚步就位（xcal_yearEditAnchorKeyboard）：popup 后
 *           键盘=独立顶层窗口、锚定主窗口底部全宽（外观/尺寸/位置与
 *           普通编辑框弹出的内嵌形态完全一致），raise 压过日历弹层。
 *           popup 前显式 setMode(Digits)：年份编辑器 construction 期已
 *           带 DigitsOnly 硬提示，常规路径经 hints→布局映射切 12 键数
 *           字布局——实测 hints 依赖输入上下文焦点传播（context 生效
 *           hints 为查询缓存），布锚→落焦→popup 同步序列下映射读到
 *           的仍是前一编辑器的缓存（round-4 实证 mode=TextLower），年
 *           份编辑布局由本处按编辑器已知语义定版（12 键数字保持）；
 *           定版先于 popup 使首帧即按 Digits 渲染（setMode 只写读数+
 *           重建键表+标脏，无 show 态依赖，不依赖 hints 传播时序）。
 *           旧内嵌形态两难已免：挂弹层底部要弹层增高容纳、挂主窗口子
 *           控件又被弹层遮挡+弹层抓取劫持（实测三连败）——悬浮形态弹
 *           层几何不动，弹层模态抓取由键盘 popup 悬浮分支自行夺取
 *           （键面点击直达、物理键入经抓取 topLevel 失配回退焦点链照
 *           常到编辑框）。确认键（ready）接年份提交：键盘确认键只发
 *           信号不收层，无此接线键入年份无法经键盘落定（先断旧后连
 *           新，防前会话残留叠加双发）。 */
static void xcal_yearEditPopupKeyboard(XCalendarWidget* cal)
{
    XVirtualKeyboard* kb;
    if (!cal) return;
    kb = XGuiApplication_virtualKeyboard();
    if (!kb) return;
    XObject_connect_1(
        (XObject*)kb, (size_t)XVirtualKeyboard_ready_signal(NULL),
        (XObject*)cal, xcal_yearEditCommitSlot, XConnectionType_Direct);
    XVirtualKeyboard_setMode(kb, XKeyboardMode_Digits);
    XVirtualKeyboard_popup(kb, (XWidget*)cal->m_yearEdit);
}

/** @brief 收起屏幕键盘并解除确认键接线（endYearEdit 专用配对）。
 *  @details 悬浮面板 closePopup 幂等：隐藏+释放抓取+原生捕获+保持顶
 *           层归属+清悬浮锚，日历弹层几何不受影响（悬浮模式从不改弹
 *           层尺寸，收层无残影）。收起无条件执行（键盘可能已被关闭
 *           键/守护提前收层），接线解除先行，保证无悬空 ready 消费
 *           者。 */
static void xcal_yearEditDismissKeyboard(XCalendarWidget* cal)
{
    XVirtualKeyboard* kb;
    if (!cal) return;
    xcal_disconnectKbReady(cal);
    kb = XGuiApplication_virtualKeyboard();
    if (kb) XVirtualKeyboard_closePopup(kb);
}
#endif /* XVIRTUALKEYBOARD_ON */

/** @brief 进入年份就地编辑（对标 Qt 点击 yearButton 弹出 yearEdit——
 *         真实编辑控件而非手绘缓冲：XLineEdit 子件自带 WA14+焦点，落焦
 *         即接入输入法/屏幕键盘链路（XVirtualKeyboardPlatformInputContext
 *         按焦点对象 WA14+编辑控件识别弹面板），实体与屏显键盘同一入口）。 */
static void xcal_beginYearEdit(XCalendarWidget* cal)
{
    XRect yearR;
    char buf[16];
    if (!cal) return;
    /* 已在编辑态：仅重新全选，防重复增高弹层（幂等）。 */
    if (cal->m_yearEdit &&
        XWidget_isVisible((XWidget*)cal->m_yearEdit)) {
        XLineEdit_selectAll(cal->m_yearEdit);
        return;
    }
    if (!cal->m_yearEdit) {
        cal->m_yearEdit = XLineEdit_create((XWidget*)cal, 0);
        if (!cal->m_yearEdit) return;
        /* 数字键盘：DigitsOnly 硬提示 → 虚拟键盘弹 12 键数字布局
         * （XVirtualKeyboard hints→mode 映射；年份只接受数字，软提示
         * PreferNumbers 亦映射 Number，但 DigitsOnly 语义最准）。 */
        XWidget_setInputMethodHints((XWidget*)cal->m_yearEdit,
                                    XInputMethodHint_DigitsOnly);
        XObject_connect_1((XObject*)cal->m_yearEdit,
                          (size_t)XLineEdit_returnPressed_signal,
                          (XObject*)cal, xcal_yearEditCommitSlot,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)cal->m_yearEdit,
                          (size_t)XLineEdit_editingFinished_signal,
                          (XObject*)cal, xcal_yearEditCommitSlot,
                          XConnectionType_Direct);
        /* Esc 取消经事件过滤器截获（XLineEdit 不消费 Esc 时才会传播到
           本控件，过滤器保证取消语义必然生效）。 */
        XObject_installEventFilter((XObject*)cal->m_yearEdit,
                                   (XObject*)cal);
    }
    if (xcal_titleLayout(cal, &yearR, NULL) < 0) return;
    XWidget_setGeometry((XWidget*)cal->m_yearEdit,
                        yearR.x, yearR.y, yearR.width, yearR.height);
    XSnprintf(buf, sizeof(buf), "%04d", cal->m_shownYear);
    XLineEdit_setText(cal->m_yearEdit, buf);
    XLineEdit_selectAll(cal->m_yearEdit);
    XWidget_show((XWidget*)cal->m_yearEdit);
    XWidget_raise((XWidget*)cal->m_yearEdit);
#if XVIRTUALKEYBOARD_ON
    /* 布锚先行（焦点驱动的自动弹出/守护边沿均在锚生效期发生），落焦
       后显式弹出（悬浮形态+确认键接线+hints 数字布局在焦点就位后生
       效）。 */
    xcal_yearEditAnchorKeyboard(cal);
    XWidget_setFocus((XWidget*)cal->m_yearEdit);
    xcal_yearEditPopupKeyboard(cal);
#else
    XWidget_setFocus((XWidget*)cal->m_yearEdit);
#endif
    /* 解除弹层模态抓取——仅键盘缺席的兜底路径（键盘 popup 悬浮分支已
     * 自行夺取双抓取+原生捕获：此处的释放是空操作，而尾部补激活
     * activateWindow=SetForegroundWindow 带置顶，会把弹层抬回悬浮键
     * 盘之上遮挡键垫，故键盘弹出时整体跳过）。键盘缺席（类型拒绝/无
     * 宿主）时沿用原语义：抓取持有期间物理键入是死的——dispatchKeyEvent
     * 键盘抓取者优先（XWidget.c:1715-1716），数字键全进弹层容器
     * keyPress→非 Esc 转投日历（XDateTimeEdit.c:1854-1862）
     * →VX_calendar_keyPressEvent 数字键 XEvent_ignore，永远到不了已
     * 持焦点的 yearEdit；鼠标同抓使屏幕键盘面板按键也被直投弹层容器。
     * 解除=框架标志（releaseMouse/releaseKeyboard 仅当前抓取者匹配时
     * 清位）+平台双通道（win32 ReleaseCapture/SetFocus(NULL)）成对执
     * 行；副作用补救：平台解键盘抓取=OS 焦点悬空（win32 SetFocus(NULL)），
     * 解除后显式补激活弹层窗口恢复 OS 焦点链；框架焦点仍在 yearEdit，
     * 物理键入自此经 dispatchKeyEvent 焦点回退直达 yearEdit。仅弹层形
     * 态（容器 windowType=Popup，即 popupShow 抓过）才解除——嵌入式日
     * 历（宿主页常规子件）无模态抓取，盲解只会造成焦点空抖。 */
#if !XVIRTUALKEYBOARD_ON
    {
        XWidget* popupHost = XWidget_parentWidget((XWidget*)cal);
        if (popupHost &&
            XWidget_windowType(popupHost) == XWindowType_Popup) {
            XWindow* hostHandle = XWidget_windowHandle(popupHost);
            XWidget_releaseMouse(popupHost);
            XWidget_releaseKeyboard(popupHost);
            if (hostHandle) {
                XWindow_setMouseGrabEnabled(hostHandle, false);
                XWindow_setKeyboardGrabEnabled(hostHandle, false);
            }
            XWidget_activateWindow(popupHost);
        }
    }
#else
    {
        XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
        if (!kb || !XVirtualKeyboard_popupVisible(kb)) {
            XWidget* popupHost = XWidget_parentWidget((XWidget*)cal);
            if (popupHost &&
                XWidget_windowType(popupHost) == XWindowType_Popup) {
                XWindow* hostHandle = XWidget_windowHandle(popupHost);
                XWidget_releaseMouse(popupHost);
                XWidget_releaseKeyboard(popupHost);
                if (hostHandle) {
                    XWindow_setMouseGrabEnabled(hostHandle, false);
                    XWindow_setKeyboardGrabEnabled(hostHandle, false);
                }
                XWidget_activateWindow(popupHost);
            }
        }
    }
#endif
    XWidget_update((XWidget*)cal);
}

/** @brief 结束年份就地编辑（对标 yearEdit editingFinished/Escape）。
 * @param commit true=提交编辑器年份（合法且在范围内则翻页）；false=取消。
 * @note 幂等：编辑器已隐藏时早退（editingFinished 与 hide 引发的
 *       focusOut 二次进入安全）。 */
static void xcal_endYearEdit(XCalendarWidget* cal, bool commit)
{
    int year;
    if (!cal || !cal->m_yearEdit) return;
    if (!XWidget_isVisible((XWidget*)cal->m_yearEdit)) return;
    year = 0;
    if (commit) {
        const char* text = XLineEdit_text(cal->m_yearEdit);
        if (text) {
            int i;
            int len = (int)XStrlen(text);
            bool allDigits = len > 0;
            for (i = 0; i < len && allDigits; ++i) {
                if (text[i] < '0' || text[i] > '9') allDigits = false;
                else year = year * 10 + (text[i] - '0');
            }
            if (allDigits && year >= 1 && year <= 9999 &&
                xcal_pageInRange(cal, year, cal->m_shownMonth))
                XCalendarWidget_setCurrentPage(cal, year, cal->m_shownMonth);
        }
    }
#if XVIRTUALKEYBOARD_ON
    xcal_yearEditDismissKeyboard(cal);
#endif
    XWidget_hide((XWidget*)cal->m_yearEdit);
    XWidget_setFocus((XWidget*)cal);
    /* 恢复弹层模态抓取（beginYearEdit 尾部解除的对称逆操作，popupShow
     * 同款双抓取对：框架 grabMouse/grabKeyboard 标志位+平台 SetCapture/
     * SetFocus(hwnd)；XMenu 子菜单关闭后父菜单原样重抓先例 XMenu.c:
     * 638-644）。越界收层/Esc 拦截重新回到抓取重路由承载。双门限：仅
     * 弹层形态（容器 windowType=Popup，与解除侧同判据——嵌入式日历无
     * 抓取可复）且日历弹层仍可见时恢复：随弹层收起进入本函数时
     * popupHide 已成对解抓（XDateTimeEdit.c:2100-2108），再抓无主；
     * yearEdit 已隐藏则本函数早退，此段不可达。注意：popupShow 的 1ms
     * 延迟平台抓取定时器此刻必然已触发清账（能进入年份编辑即弹层映射
     * 完成，远超 1ms），无残留定时器重新启用平台抓取的竞态（popupHide
     * 纪律 XDateTimeEdit.c:2095-2098 对应面）。 */
    {
        XWidget* popupHost = XWidget_parentWidget((XWidget*)cal);
        if (popupHost &&
            XWidget_windowType(popupHost) == XWindowType_Popup &&
            XWidget_isVisible(popupHost) &&
            XWidget_isVisible((XWidget*)cal)) {
            XWindow* hostHandle = XWidget_windowHandle(popupHost);
            XWidget_grabMouse(popupHost);
            XWidget_grabKeyboard(popupHost);
            if (hostHandle) {
                XWindow_setMouseGrabEnabled(hostHandle, true);
                XWindow_setKeyboardGrabEnabled(hostHandle, true);
            }
        }
    }
    XWidget_update((XWidget*)cal);
}

void XCalendarWidget_endYearEdit(XCalendarWidget* self)
{
    /* 公共收口（r2 项4）：不提交、幂等——编辑器未进入编辑态时
     * xcal_endYearEdit 早退。随日历弹层收起/重开调用，终结残留年编
     * 会话：m_yearEdit 以 explicitShow 显形，弹层隐藏/重现的可见性传
     * 播不收回它，残留会把旧缓冲文本（如 "2000"）覆在标题年份上并吞
     * 掉后续物理键入。 */
    if (!self) return;
    xcal_endYearEdit(self, false);
}

/** @brief 命中测试：本地坐标 → 网格日期（越出日网格/周号列/42 格返回
 *         false）。与绘制同源（xcal_gridLeft/xcal_dayColWidth）。 */
static bool xcal_dateAt(const XCalendarWidget* cal, const XPoint* pos,
                        XDate* out)
{
    int navY;
    int weekW;
    int dayAreaW;
    int col;
    int cell;
    int dayNum;
    int dow;
    int firstCol;
    XDate first;
    if (!cal || !pos || !out) return false;
    navY = cal->m_navBarVisible ? XCAL_NAV_H : 0;
    if (pos->y < navY + XCAL_HEADER_H) return false;
    weekW = xcal_gridLeft(cal);
    dayAreaW = XWidget_width((XWidget*)cal) - weekW;
    if (dayAreaW <= 0 || pos->x < weekW || pos->x >= weekW + dayAreaW)
        return false;
    col = (pos->x - weekW) * 7 / dayAreaW;
    if (col < 0) col = 0;
    if (col > 6) col = 6;
    cell = ((pos->y - navY - XCAL_HEADER_H) / XCAL_ROW_H) * 7 + col;
    /* 42 格网格之外（控件高于 6 行网格时的下方空白）不命中。 */
    if (cell < 0 || cell >= XCAL_ROWS * 7) return false;
    XDate_setDate(&first, cal->m_shownYear, cal->m_shownMonth, 1);
    dow = XDate_dayOfWeek(&first);
    firstCol = (dow - 1 + 7) % 7;
    if (cal->m_firstDayOfWeek >= 1 && cal->m_firstDayOfWeek <= 7)
        firstCol = (dow - cal->m_firstDayOfWeek + 7) % 7;
    dayNum = cell - firstCol + 1;
    /* 邻月格子（42 格网格的首尾补位）换算为实际日期（对标 Qt 邻月格可点选）。 */
    *out = XDate_addDays(&first, dayNum - 1);
    return true;
}

/** @brief 鼠标按下（对标 QCalendarView::mousePressEvent）：
 *         - 导航条：左/右箭头翻月、年份文本进就地编辑、**月份文本弹月份
 *           菜单**（Qt monthButton→monthMenu）；
 *         - 日期格：本库只在按下时记候选（m_pressValid/m_pressDate），
 *           选中与 clicked 都留到**释放**时（Qt 在 mouseReleaseEvent 发
 *           changeDate+clicked，gap 108）——NoSelection 模式不发 clicked。 */
static void VX_calendar_mousePressEvent(XWidget* self, XEvent* event)
{
    XCalendarWidget* cal = (XCalendarWidget*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    int navY;
    XDate d;
    if (!cal || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    navY = cal->m_navBarVisible ? XCAL_NAV_H : 0;
    /* 导航条命中（与绘制 xcal_arrowRect/xcal_titleLayout 同源；对标 Qt
     * prevMonth/nextMonth 点击翻月、yearButton 点击进入 yearEdit、
     * monthButton 点击弹 monthMenu）。 */
    if (cal->m_navBarVisible && pos.y <= navY) {
        XRect arrowPrev;
        XRect arrowNext;
        arrowPrev = xcal_arrowRect(cal, -1);
        arrowNext = xcal_arrowRect(cal, 1);
        if (XRect_contains(&arrowPrev, pos.x, pos.y)) {
            /* 上一月（越下界不再翻页，对标 Qt updateMonthMenu 的
             * prevEnabled 判据）。 */
            int m = cal->m_shownMonth - 1;
            int y2 = cal->m_shownYear;
            if (m < 1) { m = 12; --y2; }
            if (xcal_pageInRange(cal, y2, m))
                XCalendarWidget_setCurrentPage(cal, y2, m);
        } else if (XRect_contains(&arrowNext, pos.x, pos.y)) {
            /* 下一月（越上界不再翻页）。 */
            int m = cal->m_shownMonth + 1;
            int y2 = cal->m_shownYear;
            if (m > 12) { m = 1; ++y2; }
            if (xcal_pageInRange(cal, y2, m))
                XCalendarWidget_setCurrentPage(cal, y2, m);
        } else {
            XRect yearR;
            XRect monthR;
            /* 年份文本点击：进入就地编辑（对标 yearEdit 覆盖
             * yearButton，数字键入 + Enter 提交 / Esc 取消，见
             * keyPressEvent）。命中矩形=标题布局的年份子矩形（与绘制
             * 同源，xcal_titleLayout）。 */
            if (xcal_titleLayout(cal, &yearR, &monthR) >= 0 &&
                XRect_contains(&yearR, pos.x, pos.y)) {
                xcal_beginYearEdit(cal);
            } else if (XRect_contains(&monthR, pos.x, pos.y)) {
                /* 月份文本点击：弹 12 个月份条目（gap 104）。 */
                xcal_showMonthMenu(cal);
            }
        }
        XEvent_accept(event);
        return;
    }
    if (!xcal_dateAt(cal, &pos, &d)) { XEvent_ignore(event); return; }
    if (!xcal_inRange(cal, &d)) { XEvent_ignore(event); return; }
    if (cal->m_selectionMode == (int)XCalendarSelectionMode_NoSelection) {
        /* NoSelection 模式不选不发（对标 Qt readOnly 分支）。 */
        cal->m_pressValid = false;
        XEvent_accept(event);
        return;
    }
    /* 只记候选，选中/clicked 留到释放（Qt validDateClicked）。 */
    cal->m_pressDate = d;
    cal->m_pressValid = true;
    XEvent_accept(event);
}

/** @brief 鼠标释放（对标 QCalendarView::mouseReleaseEvent）：按下命中过
 *         有效日期且释放点仍是有效日期时，先改选中再发 clicked。
 *         此前本库在按下即选中并发 clicked——点击了却「松手前就触发」，
 *         按住拖出也无法取消（gap 108）。 */
static void VX_calendar_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XCalendarWidget* cal = (XCalendarWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    XDate d;
    if (!cal || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (cal->m_pressValid) {
        pos = XMouseEvent_position(me);
        if (xcal_dateAt(cal, &pos, &d) && xcal_inRange(cal, &d)) {
            if (cal->m_selectionMode !=
                (int)XCalendarSelectionMode_NoSelection) {
                XCalendarWidget_setSelectedDate(cal, &d);
                xcal_emitDate(cal, (size_t)XCalendarWidget_clicked_signal,
                              &d);
            }
        }
        cal->m_pressValid = false;
    }
    XEvent_accept(event);
}

/** @brief 键盘按下（对标 QCalendarView::keyPressEvent 与 QTableView 光标
 *         语义）：方向键移选中（左/右 ±1 天、上/下 ∓/±7 天）、Home/End
 *         周首/周末、PageUp/PageDown 翻月（不改选中，对标 showPreviousMonth
 *         /showNextMonth）、Return/Enter 发 activated（QCalendarView 发
 *         editingFinished → QCalendarWidget 转 activated）；NoSelection
 *         模式下移动选区不生效。年份就地编辑态优先吸收数字/退格/回车/
 *         Esc。其余按键忽略。 */
static void VX_calendar_keyPressEvent(XWidget* self, XEvent* event)
{
    XCalendarWidget* cal = (XCalendarWidget*)self;
    XKeyEvent* ke;
    int key;
    if (!cal || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) {
        XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    ke = (XKeyEvent*)event;
    key = XKeyEvent_key(ke);
    /* 年份就地编辑键入由编辑控件自身处理（数字/退格/光标），Return/
     * editingFinished 经信号提交、Esc 经事件过滤器取消——本控件不再
     * 手绘捕获（真实编辑控件接入了输入法/屏幕键盘链路）。 */
    if (key == XKey_Left || key == XKey_Right || key == XKey_Up ||
        key == XKey_Down) {
        /* 方向键移动选中（NoSelection 不移动）；越界由日期算法与范围
         * 钳位共同承担。 */
        if (cal->m_selectionMode == (int)XCalendarSelectionMode_NoSelection) {
            XEvent_accept(event);
            return;
        }
        {
            int days = (key == XKey_Left) ? -1
                     : (key == XKey_Right) ? 1
                     : (key == XKey_Up) ? -7 : 7;
            XDate d = XDate_addDays(&cal->m_selected, days);
            if (xcal_inRange(cal, &d))
                XCalendarWidget_setSelectedDate(cal, &d);
        }
        XEvent_accept(event);
        return;
    }
    if (key == XKey_Home || key == XKey_End) {
        if (cal->m_selectionMode != (int)XCalendarSelectionMode_NoSelection) {
            /* 周首/周末（按每周首日折算，对标 QTableView 行内首末列）。 */
            int dow = XDate_dayOfWeek(&cal->m_selected);
            int anchor = (cal->m_firstDayOfWeek >= 1 &&
                          cal->m_firstDayOfWeek <= 7)
                             ? cal->m_firstDayOfWeek : 1;
            int off = (dow - anchor + 7) % 7;
            XDate d = XDate_addDays(&cal->m_selected,
                                    (key == XKey_Home) ? -off : (6 - off));
            if (xcal_inRange(cal, &d))
                XCalendarWidget_setSelectedDate(cal, &d);
        }
        XEvent_accept(event);
        return;
    }
    if (key == XKey_PageUp || key == XKey_PageDown) {
        /* 翻月不改选中（对标 Qt PageUp/Down 翻页；API 路径同
         * showPreviousMonth/showNextMonth，含范围判据）。 */
        if (key == XKey_PageUp) XCalendarWidget_showPreviousMonth(cal);
        else XCalendarWidget_showNextMonth(cal);
        XEvent_accept(event);
        return;
    }
    if (key == XKey_Return || key == XKey_Enter) {
        /* 激活当前选中（对标 QCalendarView Return → editingFinished →
         * QCalendarWidget 发 activated(currentDate)）。 */
        if (cal->m_selectionMode != (int)XCalendarSelectionMode_NoSelection) {
            XDate d = cal->m_selected;
            xcal_emitDate(cal, (size_t)XCalendarWidget_activated_signal, &d);
        }
        XEvent_accept(event);
        return;
    }
    XEvent_ignore(event);
}

/** @brief 年份编辑器事件过滤（Esc=取消就地编辑；其余键交编辑控件——
 *         数字/退格/光标由其自身处理，虚拟键盘输入经输入法管线进同一路径）。 */
static bool VXCalendarWidget_eventFilter(XObject* self, XObject* watched,
                                         XEvent* event)
{
    XCalendarWidget* cal = (XCalendarWidget*)self;
    if (!cal || !watched || watched != (XObject*)cal->m_yearEdit) return false;
    if (event && XEvent_type(event) == XEVENT_TYPE_KEY_PRESS &&
        XKeyEvent_key((XKeyEvent*)event) == XKey_Escape) {
        xcal_endYearEdit(cal, false);
        return true;
    }
    return false;
}

/** @brief 反初始化：释放年份编辑器后调父类（日历经 delete_base 释放，
 *         弹层关闭/控件销毁路径均可达）。 */
static void VXCalendarWidget_deinit(XCalendarWidget* self)
{
    if (!self) return;
    if (self->m_yearEdit) {
        XClassDelete(self->m_yearEdit);
        self->m_yearEdit = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}
/** @brief 滚轮翻页（对标 QCalendarView::wheelEvent：angleDelta.y 每 15°
 *         一步、正角度翻上一月（addMonths(-numSteps) 负向）、不改选中）。 */
static void VX_calendar_wheelEvent(XWidget* self, XEvent* event)
{
    XCalendarWidget* cal = (XCalendarWidget*)self;
    XWheelEvent* we;
    XPoint delta;
    int steps;
    if (!cal || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) {
        XClass_Parent(XWidget, EXWidget_WheelEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    we = (XWheelEvent*)event;
    delta = XWheelEvent_angleDelta(we);
    steps = (delta.y / 8) / 15;
    if (steps == 0) steps = (delta.y > 0) ? 1 : ((delta.y < 0) ? -1 : 0);
    while (steps > 0) {
        XCalendarWidget_showPreviousMonth(cal);
        --steps;
    }
    while (steps < 0) {
        XCalendarWidget_showNextMonth(cal);
        ++steps;
    }
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
    /* 释放才是「选定 + clicked」的时点（对标 QCalendarView::mouseReleaseEvent）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_calendar_mouseReleaseEvent);
    /* 键盘导航/激活与滚轮翻页（对标 QCalendarView keyPressEvent/
     * wheelEvent：方向键移选中、PageUp/Down 翻月、Return 发 activated、
     * 滚轮翻页不改选中；年份就地编辑键入亦在此分派）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VX_calendar_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent,
                             VX_calendar_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent,
                             VX_calendar_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter,
                             VXCalendarWidget_eventFilter);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCalendarWidget_deinit);
    return XVTABLE_DEFAULT;
}

void XCalendarWidget_init(XCalendarWidget* self, XWidget* parent,
                          XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XCalendarWidget);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XMemset(&self->m_selected, 0, sizeof(XDate));
    /* 对标 QCalendarWidgetPrivate::init 的 setSelectedDate(QDate::
     * currentDate())：初始选中与展示页为今天——修复 night #67：此前
     * 硬编码 2026-09-09，既非今日也无从体现"今日/选中"语义。 */
    self->m_selected = XDate_currentDate();
    self->m_shownYear = XDate_year(&self->m_selected);
    self->m_shownMonth = XDate_month(&self->m_selected);
    self->m_firstDayOfWeek = 1;
    self->m_gridVisible = false;
    self->m_navBarVisible = true;
    self->m_selectionMode = (int)XCalendarSelectionMode_SingleSelection;
    /* [范围默认对齐 Qt 6.8 2026-11-11，gap 106] QCalendarWidgetPrivate
     * 的 m_minimumDate/m_maximumDate = QDate::fromJulianDay(1) /
     * 9999-12-31（qcalendarwidget.cpp:2531-2534/2587-2590；本机探针
     * minimumDate() 的 julianDay()==1）。此前本库用「未设置」表达无界，
     * 与 Qt 的可查询默认值不同。 */
    self->m_min = xcal_dateFromJulianDay(1);
    self->m_max = XDate_create_date(9999, 12, 31);
    self->m_minSet = true;
    self->m_maxSet = true;
    self->m_pressValid = false;
    self->m_monthMenu = NULL;
    /* 尺寸由字体度量推导（sizeHint==minimumSizeHint，gap 107）；初始
     * 尺寸取提示值（此前固定 280x200）。 */
    xcal_updateSizeHint(self);
    {
        XSize hint = XWidget_sizeHint((XWidget*)self);
        XWidget_resize((XWidget*)self, hint.width, hint.height);
    }

    /* 对标 Qt 6.8 探针：isDateEditEnabled() 缺省 true。 */
    self->m_dateEditEnabled = true;
    /* 对标 Qt 6.8 探针：verticalHeaderFormat() 缺省 1=ISOWeekNumbers
     * （枚举数值按 Qt：0=NoVerticalHeader、1=ISOWeekNumbers）。 */
    self->m_verticalHeaderFormat =
        (int)XCalendarVerticalHeaderFormat_ISOWeekNumbers;
    self->m_headerTextFormat = 0;
    self->m_weekdayTextFormat = 0;
    /* 对标 QCalendarWidget::horizontalHeaderFormat 默认 ShortDayNames。 */
    self->m_horizontalHeaderFormat =
        (int)XCalendarHeaderFormat_ShortDayNames;
    self->m_dateEditAcceptDelay = 1500; /* Qt 默认 1500ms */
    /* 年份就地编辑器懒创建（beginYearEdit 首次进入时构造）。 */
    self->m_yearEdit = NULL;
    /* 对标 QCalendarWidget 焦点策略 StrongFocus（键盘导航需要可达焦点；
     * 弹层内经键盘抓取直投本控件，不依赖焦点链）。 */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
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
    XMemset(&d, 0, sizeof(d));
    if (self) d = self->m_selected;
    return d;
}

void XCalendarWidget_setSelectedDate(XCalendarWidget* self, const XDate* date)
{
    if (!self || !date) return;
    /* 对标 QCalendarWidget::setSelectedDate（qcalendarwidget.cpp:2303-
     * 2320）：无效日期直接忽略（`if (!date.isValid()) return;`）。 */
    if (!XDate_isValid(date)) return;
    /* 同值早退（对标 Qt QCalendarWidget::setSelectedDate 的同日期直接
     * return：重复设置同值不重发 selectionChanged、不重置显示页）——
     * 亦是日历弹层每次开启前同步选中态时保持零信号副作用的依赖点。 */
    if (XDate_compare(&self->m_selected, date) == 0) return;
    self->m_selected = *date;
    xcal_clampToRange(self);
    self->m_shownYear = XDate_year(&self->m_selected);
    self->m_shownMonth = XDate_month(&self->m_selected);
    xcal_updateMonthMenu(self);
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
    /* 同页早退：currentPageChanged 仅随实际翻页发射、不重复重绘
     * （对标 Qt showMonth 的变化判据）。 */
    if (self->m_shownYear == year && self->m_shownMonth == month) return;
    /* 翻页前收起年份编辑器（不提交——页已按新值/箭头翻动，编辑缓冲
     * 作废；XDateEdit/XTimeEdit 弹层点选日期路径经此收起编辑器）。 */
    if (self->m_yearEdit &&
        XWidget_isVisible((XWidget*)self->m_yearEdit)) {
        XWidget_hide((XWidget*)self->m_yearEdit);
        XWidget_setFocus((XWidget*)self);
    }
    self->m_shownYear = year;
    /* 按 1..12 钳位后再写入 m_shownMonth。 */
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    self->m_shownMonth = month;
    /* 翻页后月份菜单的可用条目随显示年/范围变化（Qt updateMonthMenu）。 */
    xcal_updateMonthMenu(self);
    xcal_emitPageChanged(self);
    XWidget_update((XWidget*)self);
}

XDate XCalendarWidget_minimumDate(const XCalendarWidget* self)
{
    /* 构造即已设置（儒略日 1，对齐 Qt）；未设置分支保留给防御性调用。 */
    if (self && self->m_minSet) return self->m_min;
    return XDate_create();
}

void XCalendarWidget_setMinimumDate(XCalendarWidget* self, const XDate* date)
{
    XDate oldDate;
    if (!self || !date) return;
    /* 对标 QCalendarWidget::setMinimumDate（qcalendarwidget.cpp:2512-
     * 2531）：无效日期直接忽略（Qt `if (!date.isValid() || ...) return;`）。 */
    if (!XDate_isValid(date)) return;
    oldDate = self->m_selected;
    self->m_min = *date;
    self->m_minSet = true;
    /* 对标 QCalendarModel::setMinimumDate（qcalendarwidget.cpp:1200-1211）：
     * 最小值越过最大值时把最大值抬到最小值，范围保持合法。 */
    if (self->m_maxSet && XDate_compare(&self->m_max, &self->m_min) < 0)
        self->m_max = self->m_min;
    xcal_clampToRange(self);
    /* 对标 Qt 6.8.3 QCalendarWidget::setMinimumDate
     * （qcalendarwidget.cpp:2512-2531）：钳位吃掉选中日期（oldDate !=
     * newDate）时 update + showMonth + emit selectionChanged——范围
     * 收紧改写选中值时订阅方同步收到通知。 */
    if (XDate_compare(&oldDate, &self->m_selected) != 0) {
        self->m_shownYear = XDate_year(&self->m_selected);
        self->m_shownMonth = XDate_month(&self->m_selected);
        xcal_emitSelectionChanged(self);
        XWidget_update((XWidget*)self);
    }
    xcal_updateMonthMenu(self);
}

void XCalendarWidget_clearMinimumDate(XCalendarWidget* self)
{
    XDate d;
    if (!self) return;
    /* 对标 QCalendarWidget::clearMinimumDate（qcalendarwidget.cpp:
     * 2531-2534）：复位为儒略日 1 并走 setMinimumDate 的钳位/通知路径。 */
    d = xcal_dateFromJulianDay(1);
    XCalendarWidget_setMinimumDate(self, &d);
}

XDate XCalendarWidget_maximumDate(const XCalendarWidget* self)
{
    /* 同 minimumDate：构造即已设置为 9999-12-31（对齐 Qt）。 */
    if (self && self->m_maxSet) return self->m_max;
    return XDate_create();
}

void XCalendarWidget_setMaximumDate(XCalendarWidget* self, const XDate* date)
{
    XDate oldDate;
    if (!self || !date) return;
    /* 对标 QCalendarWidget::setMaximumDate（qcalendarwidget.cpp:2568-
     * 2587）：无效日期直接忽略。 */
    if (!XDate_isValid(date)) return;
    oldDate = self->m_selected;
    self->m_max = *date;
    self->m_maxSet = true;
    /* 对标 QCalendarModel::setMaximumDate（qcalendarwidget.cpp:1213-1224）：
     * 最大值低于最小值时把最小值降到最大值，范围保持合法。 */
    if (self->m_minSet && XDate_compare(&self->m_min, &self->m_max) > 0)
        self->m_min = self->m_max;
    xcal_clampToRange(self);
    /* 对标 QCalendarWidget::setMaximumDate（qcalendarwidget.cpp:2568-2587）：
     * 钳位改写选中日期时 update + showMonth + emit selectionChanged
     * （同 setMinimumDate 口径）。 */
    if (XDate_compare(&oldDate, &self->m_selected) != 0) {
        self->m_shownYear = XDate_year(&self->m_selected);
        self->m_shownMonth = XDate_month(&self->m_selected);
        xcal_emitSelectionChanged(self);
        XWidget_update((XWidget*)self);
    }
    xcal_updateMonthMenu(self);
}

void XCalendarWidget_clearMaximumDate(XCalendarWidget* self)
{
    XDate d;
    if (!self) return;
    /* 对标 QCalendarWidget::clearMaximumDate（qcalendarwidget.cpp:
     * 2587-2590）：复位为 9999-12-31。 */
    d = XDate_create_date(9999, 12, 31);
    XCalendarWidget_setMaximumDate(self, &d);
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
    /* Qt 在网格显示时给 cell 宽高各 +1（qcalendarwidget.cpp:2225-2228）。 */
    xcal_updateSizeHint(self);
    XWidget_update((XWidget*)self);
}

bool XCalendarWidget_isNavigationBarVisible(const XCalendarWidget* self)
{
    /* NULL 回退与 init 默认一致（导航栏默认 true），避免 getter 空值
     * 与构造默认互相矛盾。 */
    return self ? self->m_navBarVisible : true;
}

void XCalendarWidget_setNavigationBarVisible(XCalendarWidget* self, bool visible)
{
    if (!self) return;
    self->m_navBarVisible = visible;
    /* 导航条高计入 sizeHint（对齐 Qt minimumSizeHint 的 headerSize.height）。 */
    xcal_updateSizeHint(self);
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
































/* ==================== Task 2.5：日历补充 API ====================
 * 注：weekNumber/isDateSelected/setShowTodayDate/isShowTodayDate/
 * todayDate/showTodayPage 六个自造 API 已删除（冗余清理）：QCalendarWidget
 * 无对应公开符号，且全仓（Src/Test/App）无消费者；今日标记行为恒开
 * （对标 Qt 日历恒下划线标今日）。 */

void XCalendarWidget_setDateEditEnabled(XCalendarWidget* self, bool enable)
{ if (self) self->m_dateEditEnabled = enable; }
bool XCalendarWidget_isDateEditEnabled(const XCalendarWidget* self)
{ return self ? self->m_dateEditEnabled : true; }

void XCalendarWidget_setHeaderTextFormat(XCalendarWidget* self, int fmt)
{ if (self) self->m_headerTextFormat = fmt; }
void XCalendarWidget_setWeekdayTextFormat(XCalendarWidget* self, int fmt)
{ if (self) self->m_weekdayTextFormat = fmt; }

void XCalendarWidget_setVerticalHeaderFormat(XCalendarWidget* self,
                                             int format)
{
    if (self) {
        self->m_verticalHeaderFormat = format;
        /* 周号列宽计入 sizeHint（Qt cols=8 分支）。 */
        xcal_updateSizeHint(self);
        XWidget_update((XWidget*)self);
    }
}
int XCalendarWidget_verticalHeaderFormat(const XCalendarWidget* self)
{
    /* NULL 回退与 init 默认一致（ISOWeekNumbers=1）。 */
    return self ? self->m_verticalHeaderFormat
                : (int)XCalendarVerticalHeaderFormat_ISOWeekNumbers;
}

int XCalendarWidget_headerTextFormat(const XCalendarWidget* self)
{
    return self ? self->m_headerTextFormat : 0;
}

int XCalendarWidget_weekdayTextFormat(const XCalendarWidget* self)
{
    return self ? self->m_weekdayTextFormat : 0;
}

void XCalendarWidget_setHorizontalHeaderFormat(XCalendarWidget* self,
                                               int format)
{
    if (self) {
        self->m_horizontalHeaderFormat = format;
        /* 表头行/列宽计入 sizeHint（Qt rows=6/7 与字体宽分支）。 */
        xcal_updateSizeHint(self);
        XWidget_update((XWidget*)self);
    }
}

int XCalendarWidget_horizontalHeaderFormat(const XCalendarWidget* self)
{
    /* NULL 回退与 init 默认一致（ShortDayNames=2），避免 getter 空值
     * 与构造默认互相矛盾（此前误返回 1=SingleLetterDayNames）。 */
    return self ? self->m_horizontalHeaderFormat
                : (int)XCalendarHeaderFormat_ShortDayNames;
}

void XCalendarWidget_setDateEditAcceptDelay(XCalendarWidget* self, int delay)
{
    if (self) self->m_dateEditAcceptDelay = delay;
}

int XCalendarWidget_dateEditAcceptDelay(const XCalendarWidget* self)
{
    return self ? self->m_dateEditAcceptDelay : 0;
}

void XCalendarWidget_setDateRange(XCalendarWidget* self, const XDate* min,
                                  const XDate* max)
{
    if (min) XCalendarWidget_setMinimumDate(self, min);
    if (max) XCalendarWidget_setMaximumDate(self, max);
}

void XCalendarWidget_showNextMonth(XCalendarWidget* self)
{
    int year, month;
    if (!self) return;
    year = XCalendarWidget_yearShown(self);
    month = XCalendarWidget_monthShown(self);
    if (month == 12) XCalendarWidget_setCurrentPage(self, year + 1, 1);
    else XCalendarWidget_setCurrentPage(self, year, month + 1);
}

void XCalendarWidget_showPreviousMonth(XCalendarWidget* self)
{
    int year, month;
    if (!self) return;
    year = XCalendarWidget_yearShown(self);
    month = XCalendarWidget_monthShown(self);
    if (month == 1) XCalendarWidget_setCurrentPage(self, year - 1, 12);
    else XCalendarWidget_setCurrentPage(self, year, month - 1);
}

void XCalendarWidget_showNextYear(XCalendarWidget* self)
{
    if (!self) return;
    XCalendarWidget_setCurrentPage(self,
                                   XCalendarWidget_yearShown(self) + 1,
                                   XCalendarWidget_monthShown(self));
}

void XCalendarWidget_showPreviousYear(XCalendarWidget* self)
{
    if (!self) return;
    XCalendarWidget_setCurrentPage(self,
                                   XCalendarWidget_yearShown(self) - 1,
                                   XCalendarWidget_monthShown(self));
}

void XCalendarWidget_showSelectedDate(XCalendarWidget* self)
{
    XDate d;
    if (!self) return;
    d = XCalendarWidget_selectedDate(self);
    XCalendarWidget_setCurrentPage(self, XDate_year(&d), XDate_month(&d));
}

void XCalendarWidget_showToday(XCalendarWidget* self)
{
    XDate today;
    if (!self) return;
    today = XDate_currentDate();
    XCalendarWidget_setCurrentPage(self, XDate_year(&today),
                                   XDate_month(&today));
}

#endif /* XWIDGET_ON && XCALENDARWIDGET_ON */

#endif /* XGUI_ON */
