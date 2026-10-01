/**
 * @file       XCalendarWidget.c
 * @brief      日历控件实现（对标 Qt 6.8 QCalendarWidget 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

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
    /* 显式 XClass 转换：XFont_deinit_base 为 XClass_deinit_base 裸别名
     * 宏，直呼指针类型不符（XChartView.c 同款注释）。 */
    XFont_deinit_base((XClass*)&font);
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
    int cell;
    char buf[16];
    XDate first;
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
     * 标题画在 setFont 之前，与表头/日期格字体不一致）。 */
    {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        XFont_deinit_base((XClass*)&font);
    }
    y = 0;
    if (cal->m_navBarVisible) {
        XRect titleMonth;
        int titleX = xcal_titleLayout(cal, NULL, &titleMonth);
        /* 月份子矩形当前仅随布局同出（月份下拉未实现，见 deferred）。 */
        (void)titleMonth;
#if XSTYLE_ON
        if (XStyle_defaultStyle() != NULL) {
            /* 对标 QCalendarWidget 导航条（qcalendarwidget.cpp:1784-1794
             * headerLayout）：左侧 prevMonth 箭头（CC_ToolButton，走
             * SP_ArrowLeft 口径）+ 中央「yyyy年M月」标题 + 右侧
             * nextMonth 箭头。Qt 6.8 无年箭头，年份导航由年份文本就地
             * 编辑承担。缺陷4：按钮选项不再带 AutoRaise（样式恒画按钮
             * 底），大三角由本控件直绘（样式链箭头仅 ~5px）。 */
            XStyle* style = XStyle_defaultStyle();
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
            int dx = cell * w / 7 + w / 14 - 4;
            XPainter_drawText(&painter, dx, y + 14, headerNames[idx], mid);
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
        for (cell = 0; cell < XCAL_ROWS * 7; ++cell) {
            /* 42 格全网格：越出本月的邻月日期以 Mid 弱化色渲染（对标
             * QCalendarModel 邻月格子），点击可选中邻月日期（见鼠标按下）。 */
            int dayNum = cell - firstCol + 1;
            int row = cell / 7;
            int col7 = cell % 7;
            int cx = col7 * w / 7 + w / 14 - 4;
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
                XRect_init(&bg, col7 * w / 7, y + row * XCAL_ROW_H,
                           w / 7, XCAL_ROW_H);
                XPainter_fillRect(&painter, &bg, highlight);
            }
            XPainter_drawText(&painter, cx, cy, buf,
                              inMonth ? windowText : mid);
            /* 今日标记：日期数字下加下划线（对标 Qt QCalendarModel 数据
             * 里 Today 角色的 Underline 文本格式，paintCell 呈现）。 */
            if (cal->m_showTodayDate) {
                XDate today = XDate_currentDate();
                if (XDate_compare(&d, &today) == 0) {
                    XRect ul;
                    XRect_init(&ul, col7 * w / 7 + w / 14 - 5,
                               cy + 3, (int)XStrlen(buf) * 8 + 2, 1);
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
        }
    }
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
static XConnection* xcal_kbReadyConn = NULL;

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
    if (xcal_kbReadyConn) {
        XObject_disconnect_2(xcal_kbReadyConn);
        xcal_kbReadyConn = NULL;
    }
    xcal_kbReadyConn = XObject_connect_1(
        (XObject*)kb, (size_t)XVirtualKeyboard_ready_signal(NULL),
        (XObject*)cal, xcal_yearEditCommitSlot, XConnectionType_Direct);
    /* 12 键数字布局定版先于 popup：首帧即按 Digits 渲染上屏（缺陷⑤）。 */
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
    if (xcal_kbReadyConn) {
        XObject_disconnect_2(xcal_kbReadyConn);
        xcal_kbReadyConn = NULL;
    }
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

static void VX_calendar_mousePressEvent(XWidget* self, XEvent* event)
{
    XCalendarWidget* cal = (XCalendarWidget*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    int navY;
    int dayOfWeek;
    XDate first;
    int firstCol;
    int cell;
    int dayNum;
    XDate d;
    if (!cal || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    navY = cal->m_navBarVisible ? XCAL_NAV_H : 0;
    /* 导航条命中（与绘制 xcal_arrowRect/xcal_yearRect 同源；对标 Qt
     * prevMonth/nextMonth 点击翻月、yearButton 点击进入 yearEdit）。 */
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
            /* 年份文本点击：进入就地编辑（对标 yearEdit 覆盖
             * yearButton，数字键入 + Enter 提交 / Esc 取消，见
             * keyPressEvent）。命中矩形=标题布局的年份子矩形（与绘制
             * 同源，xcal_titleLayout）。 */
            if (xcal_titleLayout(cal, &yearR, NULL) >= 0 &&
                XRect_contains(&yearR, pos.x, pos.y)) {
                xcal_beginYearEdit(cal);
            }
        }
        /* 月份文本区点击暂不动作（Qt 为月份下拉菜单，未实现，见 deferred）。 */
        XEvent_accept(event);
        return;
    }
    if (pos.y < navY + XCAL_HEADER_H) { XEvent_ignore(event); return; }
    {
        int w = XWidget_width(self);
        cell = ((pos.y - navY - XCAL_HEADER_H) / XCAL_ROW_H) * 7 +
               pos.x * 7 / w;
        /* 42 格网格之外（控件高于 6 行网格时的下方空白）不命中。 */
        if (cell < 0 || cell >= XCAL_ROWS * 7) {
            XEvent_ignore(event);
            return;
        }
    }
    XDate_setDate(&first, cal->m_shownYear, cal->m_shownMonth, 1);
    dayOfWeek = XDate_dayOfWeek(&first);
    firstCol = (dayOfWeek - 1 + 7) % 7;
    if (cal->m_firstDayOfWeek >= 1 && cal->m_firstDayOfWeek <= 7)
        firstCol = (dayOfWeek - cal->m_firstDayOfWeek + 7) % 7;
    dayNum = cell - firstCol + 1;
    /* 邻月格子（42 格网格的首尾补位）换算为实际日期（对标 Qt 邻月
     * 格可点选：点击即把选中移到该邻月日期并翻页）。 */
    d = XDate_addDays(&first, dayNum - 1);
    if (!xcal_inRange(cal, &d)) { XEvent_ignore(event); return; }
    if (cal->m_selectionMode == (int)XCalendarSelectionMode_NoSelection) {
        /* NoSelection 模式不选不发（对标 Qt selectionMode 语义）。 */
        XEvent_accept(event);
        return;
    }
    XCalendarWidget_setSelectedDate(cal, &d);
    xcal_emitDate(cal, (size_t)XCalendarWidget_clicked_signal, &d);
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
        XLineEdit_delete_base(self->m_yearEdit);
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
    /* 键盘导航/激活与滚轮翻页（对标 QCalendarView keyPressEvent/
     * wheelEvent：方向键移选中、PageUp/Down 翻月、Return 发 activated、
     * 滚轮翻页不改选中；年份就地编辑键入亦在此分派）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VX_calendar_keyPressEvent);
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
    XSize hint;
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
    XWidget_resize(self, 280, 200);
    hint.width = 280;
    hint.height = 200;
    XWidget_setSizeHint((XWidget*)self, &hint);

    self->m_dateEditEnabled = false;
    self->m_showTodayDate = true;
    self->m_verticalHeaderFormat = 0;
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
    /* 同值早退（对标 Qt QCalendarWidget::setSelectedDate 的同日期直接
     * return：重复设置同值不重发 selectionChanged、不重置显示页）——
     * 亦是日历弹层每次开启前同步选中态时保持零信号副作用的依赖点。 */
    if (XDate_compare(&self->m_selected, date) == 0) return;
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
    self->m_shownMonth = month;
    if (self->m_shownMonth < 1) self->m_shownMonth = 1;
    if (self->m_shownMonth > 12) self->m_shownMonth = 12;
    xcal_emitPageChanged(self);
    XWidget_update((XWidget*)self);
}

XDate XCalendarWidget_minimumDate(const XCalendarWidget* self)
{
    XDate d;
    XMemset(&d, 0, sizeof(d));
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
    XMemset(&d, 0, sizeof(d));
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
































/* ==================== Task 2.5：日历补充 API ==================== */

void XCalendarWidget_setDateEditEnabled(XCalendarWidget* self, bool enable)
{ if (self) self->m_dateEditEnabled = enable; }
bool XCalendarWidget_isDateEditEnabled(const XCalendarWidget* self)
{ return self ? self->m_dateEditEnabled : false; }

int XCalendarWidget_weekNumber(const XCalendarWidget* self,
                               const XDate* date)
{
    int doy;
    if (!self || !date) return -1;
    doy = XDate_dayOfYear(date);
    if (doy < 0) return -1;
    return (doy + 6) / 7; /* 简化 ISO 周估算。 */
}

void XCalendarWidget_setHeaderTextFormat(XCalendarWidget* self, int fmt)
{ if (self) self->m_headerTextFormat = fmt; }
void XCalendarWidget_setWeekdayTextFormat(XCalendarWidget* self, int fmt)
{ if (self) self->m_weekdayTextFormat = fmt; }

bool XCalendarWidget_isDateSelected(const XCalendarWidget* self,
                                    const XDate* date)
{
    if (!self || !date) return false;
    return XDate_compare(date, &self->m_selected) == 0;
}

void XCalendarWidget_setShowTodayDate(XCalendarWidget* self, bool show)
{
    if (self) {
        self->m_showTodayDate = show;
        XWidget_update((XWidget*)self);
    }
}
bool XCalendarWidget_isShowTodayDate(const XCalendarWidget* self)
{ return self ? self->m_showTodayDate : true; }

void XCalendarWidget_setVerticalHeaderFormat(XCalendarWidget* self,
                                             int format)
{
    if (self) {
        self->m_verticalHeaderFormat = format;
        XWidget_update((XWidget*)self);
    }
}
int XCalendarWidget_verticalHeaderFormat(const XCalendarWidget* self)
{ return self ? self->m_verticalHeaderFormat : 0; }

bool XCalendarWidget_todayDate(const XCalendarWidget* self, XDate* out)
{
    if (!self || !out) return false;
    *out = XDate_currentDate();
    return true;
}

void XCalendarWidget_showTodayPage(XCalendarWidget* self)
{
    XDate today;
    if (!self) return;
    today = XDate_currentDate();
    XCalendarWidget_setCurrentPage(self, XDate_year(&today),
                                   XDate_month(&today));
    XCalendarWidget_setSelectedDate(self, &today);
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
    if (self) self->m_horizontalHeaderFormat = format;
}

int XCalendarWidget_horizontalHeaderFormat(const XCalendarWidget* self)
{
    return self ? self->m_horizontalHeaderFormat : 1;
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
