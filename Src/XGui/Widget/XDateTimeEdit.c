/**
 * @file       XDateTimeEdit.c
 * @brief      日期时间编辑控件实现（对标 Qt 6.8 QDateTimeEdit 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XDateTimeEdit.h"
#include "XStringUtils.h"
#include "XPrintf.h"         /* 弹层收层路径诊断日志（缺陷④定位） */
#include "XMemory.h"
#include "XEvent.h"
#include "XWindowEvent.h"    /* 焦点事件原因（focusIn 分段选区分派） */
#include "XVarList.h"
#include "XLineEdit.h"
#include "XCalendarWidget.h"
#if XVIRTUALKEYBOARD_ON
#include "XVirtualKeyboard.h" /* 箭头开层同拍确定性关键盘（缺陷 A） */
#include "XVirtualKeyboardSettings.h" /* 年份/时间行编辑器键盘总开关 */
#endif
#include "XGuiConfig.h"
#include "XWindow.h"
#include "XCoreApplication.h"
#include "XGuiApplication.h"   /* 主屏查询/焦点窗口（弹层翻转与焦点回交） */
#include "XScreen.h"           /* 屏幕几何（弹层贴边超屏翻转） */
#include "XPainter.h"          /* 弹层时间设定行文本绘制/量宽 */
#include "XStyle.h"
#include "XStyleOption.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON

/* ==================== 内部工具 ==================== */

/** @brief 分段记号容量上限（默认格式 6 段；含毫秒/星期/上下午的
 *         "yyyy-MM-dd HH:mm:ss.zzz dddd AP" 记 9 段，取 16 覆盖常用格式）。 */
#define XDT_SECTION_MAX 16

/** @brief 日期段/时间段掩码（parserType 滤段、setDisplayFormat 收窄、
 *         弹层三态判定共用；对标 Qt DateSections_Mask/TimeSections_Mask）。 */
#define XDT_DATE_SECTION_MASK                                \
    ((int)XDateTimeEditSection_YearSection |                 \
     (int)XDateTimeEditSection_MonthSection |                \
     (int)XDateTimeEditSection_DaySection)
#define XDT_TIME_SECTION_MASK                                \
    ((int)XDateTimeEditSection_HourSection |                 \
     (int)XDateTimeEditSection_MinuteSection |               \
     (int)XDateTimeEditSection_SecondSection |               \
     (int)XDateTimeEditSection_MSecSection |                 \
     (int)XDateTimeEditSection_AmPmSection)

/** @brief 分段记号（内部解析产物，渲染/掩码/查询共用一份解析）。 */
typedef struct XdtSectionTok
{
    int  code;  /**< 分段枚举值（XDateTimeEditSection）。 */
    int  width; /**< 数字位数/档位：yyyy=4；MM=2/M=1；dd=2/d=1；
                     HH/mm/ss=2；h=1|2；z=1|2|3；ddd=3/dddd=4（区分
                     星期文案档）；AP/A=1|2。现有记号全为 ASCII，宽度
                     恰等于格式串字节长度。 */
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
 *        M/dd/dd/d/dddd/ddd/HH/h/hh/mm/ss/zzz/zz/z/AP（A、ap、a 同 AP，
 *        Qt 对上下午记号大小写不敏感）；单字母 H/m/s 维持字面输出。 */
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
        } else if (c == 'M') {
            /* 单 'M'：月段无补零档（对标 Qt countRepeat=1 的
             * MonthSection，"yyyy/M/d" 的 zh_CN 短格式口径）。 */
            out[n].code = (int)XDateTimeEditSection_MonthSection;
            out[n].width = 1;
            out[n].spec = 'M';
            out[n++].pos = i++;
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
        } else if (c == 'd') {
            /* 单 'd'：日段无补零档（对标 Qt countRepeat=1 的
             * DaySection，"yyyy/M/d" 的 zh_CN 短格式口径）。 */
            out[n].code = (int)XDateTimeEditSection_DaySection;
            out[n].width = 1;
            out[n].spec = 'd';
            out[n++].pos = i++;
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

/** @brief 分段枚举码对解析类型是否允许（对标 qdatetimeparser.cpp
 *         parseFormat 的 parserType 守卫：457-515 时间段仅非 QDate、
 *         518-553 日期段仅非 QTime）。 */
static bool xdt_codeAllowed(int code, int parserType)
{
    if (parserType == (int)XDateTimeEditParserType_Date)
        return (code & XDT_DATE_SECTION_MASK) != 0;
    if (parserType == (int)XDateTimeEditParserType_Time)
        return (code & XDT_TIME_SECTION_MASK) != 0;
    return true;
}

/** @brief 控件生效的解析类型（NULL 控件按基类 DateTime 口径）。 */
static int xdt_tokenParserType(const XDateTimeEdit* self)
{
    if (!self) return (int)XDateTimeEditParserType_DateTime;
    return self->m_parserType;
}

/** @brief 带解析类型过滤的分段解析（全文件渲染/掩码/查询/键入解析
 *         的统一入口）：按 parserType 剔除对方侧记号，被剔记号在渲染
 *         与文本解析中退化为字面字符（对标 Qt parseFormat 不生成
 *         sectionNode 后记号并入 separators 字面文本的呈现口径）。 */
static int xdt_tokenizeTyped(const char* fmt, XdtSectionTok* out, int max,
                             int parserType)
{
    int n = xdt_tokenize(fmt, out, max);
    int i;
    int j = 0;
    if (parserType == (int)XDateTimeEditParserType_DateTime) return n;
    for (i = 0; i < n; ++i) {
        if (xdt_codeAllowed(out[i].code, parserType)) out[j++] = out[i];
    }
    return j;
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

/** @brief 星期/上下午文案表：渲染（xdt_renderToken）与键入解析
 *         （xdt_parseEditText）共用同一份，保证 round-trip 一致。
 *         星期下标对应 XDate_dayOfWeek()-1（1=周一..7=周日），对标
 *         zh_CN 的 dayName；上下午对标 zh_CN 的 amText/pmText。 */
static const char* const xdt_dayShortNames[7] = {
    "周一", "周二", "周三", "周四", "周五", "周六", "周日" };
static const char* const xdt_dayLongNames[7] = {
    "星期一", "星期二", "星期三", "星期四", "星期五",
    "星期六", "星期日" };
static const char* const xdt_amText = "上午";
static const char* const xdt_pmText = "下午";

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
        if (tok->width < 2)
            return XSnprintf(buf, cap, "%d",
                             XDate_month(&self->m_dateTime.m_date));
        return XSnprintf(buf, cap, "%02d",
                         XDate_month(&self->m_dateTime.m_date));
    case 'd':
        if (tok->width >= 3) {
            /* XDate_dayOfWeek 1=周一..7=周日；无星期文案起始日偏移。 */
            int dow = XDate_dayOfWeek(&self->m_dateTime.m_date);
            int idx = (dow >= 1 && dow <= 7) ? dow - 1 : 0;
            return XSnprintf(buf, cap, "%s",
                             (tok->width >= 4 ? xdt_dayLongNames
                                              : xdt_dayShortNames)[idx]);
        }
        if (tok->width < 2)
            return XSnprintf(buf, cap, "%d",
                             XDate_day(&self->m_dateTime.m_date));
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
        return XSnprintf(buf, cap, "%s", (h < 12) ? xdt_amText : xdt_pmText);
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
    n = xdt_tokenizeTyped(fmt, toks, XDT_SECTION_MAX,
                          xdt_tokenParserType(self));
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
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
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

/** @brief 发射日期变化信号 dateChanged(XDate*)（P1-4：此前全文件无发射
 *         点；对标 QDateTimeEdit::dateChanged——setDate/setDateTime/
 *         键入提交/stepBy/范围钳位等任何日期部分变化路径都发射）。 */
static void xdt_emitDate(XDateTimeEdit* self)
{
    XDate* ptr = &self->m_dateTime.m_date;
    XVarList* args = XVarList_Create(XVar(XDate*, ptr));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XDateTimeEdit_dateChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射时间变化信号 timeChanged(XTime*)（P1-4：对标
 *         QDateTimeEdit::timeChanged，时间部分变化的各路径均发射）。 */
static void xdt_emitTime(XDateTimeEdit* self)
{
    XTime* ptr = &self->m_dateTime.m_time;
    XVarList* args = XVarList_Create(XVar(XTime*, ptr));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XDateTimeEdit_timeChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

#if XCALENDARWIDGET_ON
static void xdt_syncPopupValue(XDateTimeEdit* self); /* 弹层随值同步（缺陷 F；实现见「日历弹层」节）。 */
#endif

/** @brief 统一提交发射：dateTimeChanged 恒发射（保持既有 API 语义），
 *         dateChanged/timeChanged 按日期/时间部分是否实际变化分别发射
 *         （P1-4，对标 QDateTimeEdit 三信号随部分变化齐发）。值变化
 *         落点同时驱动弹层随值同步（缺陷 F：弹层开着时物理键入/滚轮/
 *         箭头步进改值后，日历选中页与时间行跟随刷新——信号发射是本
 *         控件全部交互改值路径的汇聚点：xdt_commitTyping/
 *         XDateTimeEdit_stepBy/XDateTimeEdit_interpret 与 setDate/
 *         setTime/setDateTime 公共 API 都经此处）。 */
static void xdt_emitPartChanged(XDateTimeEdit* self, const XDateTime* old)
{
    xdt_emitChanged(self);
    if (XDate_compare(&old->m_date, &self->m_dateTime.m_date) != 0)
        xdt_emitDate(self);
    if (XTime_compare(&old->m_time, &self->m_dateTime.m_time) != 0)
        xdt_emitTime(self);
#if XCALENDARWIDGET_ON
    xdt_syncPopupValue(self);
#endif
}

/* ==================== 分段键入模型（F3：对标 QDateTimeEdit 分段编辑） ==================== */

static void xdt_commitTyping(XDateTimeEdit* self, bool advance);

/** @brief 复位数字键入累积态（换段提交后/提交完成后/清理路径共用）。 */
static void xdt_resetTyping(XDateTimeEdit* self)
{
    if (!self) return;
    self->m_typingSection = -1;
    self->m_typingValue = 0;
    self->m_typingDigits = 0;
}

/** @brief 分段数字键入的自然上限（重启判据，对标 Qt 段 parse 的
 *         "候选超上限即以新数字重启"）。spec 与记号宽对齐：yyyy=9999、
 *         MM=12、dd=31、HH=23、h=12、mm/ss=59、z 按位宽 10^w-1。
 *         上下午/星期文案段不接受数字键入，返回 0。 */
static int xdt_sectionDigitMax(char spec, int width)
{
    switch (spec) {
    case 'y': return 9999;
    case 'M': return 12;
    case 'd': return 31;
    case 'H': return 23;
    case 'h': return 12;
    case 'm': return 59;
    case 's': return 59;
    case 'z': {
        int i;
        int m = 1;
        for (i = 0; i < width; ++i) m *= 10;
        return m - 1;
    }
    default:
        return 0;
    }
}

/** @brief 分段键入容量=段自然上限的十进制位数（缺陷⑦ 2026-10-02 对齐
 *         Qt 6.8.3：QDateTimeParser::sectionMaxSize，qdatetimeparser.cpp
 *         :646-704——M/d 段 maxSize=2 与记号位宽 1 无关；显示宽由记号
 *         位宽决定、键入容量由段可取值域决定，二者分离。yyyy=4、
 *         M/MM=2、d/dd=2、HH/h=2、mm/ss=2、z=位宽）。上下午/星期文案
 *         段不接受数字键入，容量 0。 */
static int xdt_sectionTypeCapacity(char spec, int width)
{
    int m = xdt_sectionDigitMax(spec, width);
    int c = 1;
    if (m <= 0) return 0;
    while (m >= 10) {
        ++c;
        m /= 10;
    }
    return c;
}

/** @brief 键入前缀可扩展性（缺陷⑦ 2026-10-02 对齐 Qt 6.8.3
 *         QDateTimeParser::potentialValue 的逆否判定，qdatetimeparser.cpp
 *         :2102-2134）：已键入 used 位的前缀 value 补 1..(cap-used) 位后
 *         能否落进段值域 [minv,maxv]。不可扩展即 Qt skipToNextSection
 *         （:2139-2172，注释原文口径「M 段打 1 不跳——还有 [012] 可续；
 *         打 3 即跳」）。本控件键入恒发生在整段反选后的段尾累积替换
 *         （xdt_typeDigit），只需后缀补位，无需 potentialValue 的中位
 *         插入分支。 */
static bool xdt_typingCanExtend(int value, int used, int cap,
                                int minv, int maxv)
{
    int scale = 1; /* 10^k（k=补位个数）。 */
    int k;
    for (k = 1; used + k <= cap; ++k) {
        int low;
        int high;
        scale *= 10;
        low = value * scale;          /* 补 k 位后的最小补全值。 */
        if (low > maxv) return false; /* 补位只会更大→任何长度都不可达。 */
        high = low + scale - 1;       /* 补 k 位后的最大补全值。 */
        if (high >= minv) return true; /* [low,high]∩[minv,maxv] 非空。 */
    }
    return false;
}

/** @brief 取字符串第 charIndex 个字符的字节偏移（段显示替换的
 *         字符→字节换算；UTF-8 前缀可能含多字节字面字符）。 */
static int xdt_charToByteOffset(const char* s, int charIndex)
{
    int chars = 0;
    int b = 0;
    if (!s) return 0;
    while (s[b] != '\0' && chars < charIndex) {
        if (((unsigned char)s[b] & 0xC0) != 0x80) ++chars;
        ++b;
    }
    return b;
}

/** @brief 把当前段显示整段替换为键入累积值的补零文本（显示层手术：
 *         只改内嵌行编辑文本，不动 m_dateTime——未满位值为中间态，
 *         提交点见 xdt_commitTyping。替换宽度=max(记号位宽, 键入位数)：
 *         单记号 M/d 段多位键入（10/11/12，缺陷⑦ 后可续打）按位数撑宽
 *         显示（对标 Qt 中间态补零/扩位显示），提交后 refreshText 恢复
 *         规范渲染；选区随宽保持整段反选）。 */
static void xdt_showTypingBuffer(XDateTimeEdit* self)
{
    char buf[128];
    char out[256];
    char piece[16];
    XdtSectionRange ranges[XDT_SECTION_MAX];
    XdtSectionTok toks[XDT_SECTION_MAX];
    XLineEdit* edit;
    int count = 0;
    int n;
    int index = -1;
    int i;
    int selW;
    int bStart;
    int o;
    if (!self) return;
    xdt_render(self, buf, sizeof(buf), ranges, XDT_SECTION_MAX, &count);
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
    for (i = 0; i < n; ++i) {
        if (toks[i].code == self->m_currentSection) { index = i; break; }
    }
    edit = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (!edit) return;
    if (index < 0 || index >= count || ranges[index].len <= 0) {
        xdt_refreshText(self);
        return;
    }
    selW = toks[index].width;
    if (self->m_typingDigits > selW) selW = self->m_typingDigits;
    XSnprintf(piece, sizeof(piece), "%0*d", selW, self->m_typingValue);
    bStart = xdt_charToByteOffset(buf, ranges[index].start);
    /* 数字段渲染全为 ASCII：段字符数=段字节数（ranges[index].len）。 */
    o = 0;
    XStrncpy(out + o, buf, (size_t)bStart);
    o += bStart;
    {
        size_t pl = XStrlen(piece);
        size_t j;
        for (j = 0; j < pl; ++j) out[o++] = piece[j];
    }
    XStrncpy(out + o, buf + bStart + ranges[index].len, sizeof(out) - o - 1);
    out[sizeof(out) - 1] = '\0';
    XLineEdit_setText(edit, out);
    /* 保持整段反选（宽度=补位后的段显示宽，字符坐标稳定）。 */
    XLineEdit_setSelection(edit, ranges[index].start, selW);
}

/** @brief 把键入累积值写回当前值的一个分段（提交落账）。日期段经
 *         XDate_setDate 合法性校验，月切换致天数溢出时日逐位回退
 *         （如 2-29 换非闰年→2-28）；时间段经 XTime_setHMS 校验；
 *         'z' 段按位宽折算毫秒（z 键入 5 → 500ms）。 */
static bool xdt_applySectionValue(XDateTimeEdit* self,
                                  const XdtSectionTok* tok, int value)
{
    int y = XDate_year(&self->m_dateTime.m_date);
    int m = XDate_month(&self->m_dateTime.m_date);
    int d = XDate_day(&self->m_dateTime.m_date);
    int h = XTime_hour(&self->m_dateTime.m_time);
    int mi = XTime_minute(&self->m_dateTime.m_time);
    int s = XTime_second(&self->m_dateTime.m_time);
    int ms = XTime_msec(&self->m_dateTime.m_time);
    switch (tok->spec) {
    case 'y':
        y = (value < 1) ? 1 : value;
        break;
    case 'M':
        m = (value < 1) ? 1 : ((value > 12) ? 12 : value);
        break;
    case 'd':
        d = (value < 1) ? 1 : value;
        break;
    case 'H':
        h = value;
        break;
    case 'h':
        h = value;
        break;
    case 'm':
        mi = value;
        break;
    case 's':
        s = value;
        break;
    case 'z': {
        int i;
        ms = value;
        for (i = tok->width; i < 3; ++i) ms *= 10;
        if (ms > 999) ms = 999;
        break;
    }
    default:
        return false;
    }
    if (tok->spec == 'y' || tok->spec == 'M' || tok->spec == 'd') {
        XDate probe;
        int tryDay = d;
        while (tryDay >= 1 && !XDate_setDate(&probe, y, m, tryDay)) --tryDay;
        if (tryDay < 1) return false;
        self->m_dateTime.m_date = probe;
    } else {
        XTime tm;
        if (!XTime_setHMS(&tm, h, mi, s, ms)) return false;
        self->m_dateTime.m_time = tm;
    }
    return true;
}

/** @brief 取日期时间指定段的现值（键入撤销快照读数，G3 退格回归；
 *         上下午/星期文案段不接受数字键入无需承载，返回 -1）。 */
static int xdt_sectionValueOf(const XDateTime* dt, int code)
{
    if (!dt) return -1;
    switch (code) {
    case (int)XDateTimeEditSection_YearSection:
        return XDate_year(&dt->m_date);
    case (int)XDateTimeEditSection_MonthSection:
        return XDate_month(&dt->m_date);
    case (int)XDateTimeEditSection_DaySection:
        return XDate_day(&dt->m_date);
    case (int)XDateTimeEditSection_HourSection:
        return XTime_hour(&dt->m_time);
    case (int)XDateTimeEditSection_MinuteSection:
        return XTime_minute(&dt->m_time);
    case (int)XDateTimeEditSection_SecondSection:
        return XTime_second(&dt->m_time);
    case (int)XDateTimeEditSection_MSecSection:
        return XTime_msec(&dt->m_time);
    default:
        return -1;
    }
}

/** @brief 落账键入累积态：未满位输入在换段/步进/提交/失焦前结算，
 *         满位输入经此提交。写回成功后钳位+重渲染+按部分发射三信号；
 *         advance 时跳下一段并整段选中（对标 Qt 满位自动跳段），
 *         同时记录键入撤销快照（被提交段+落账前值，供退格回退）。 */
static void xdt_commitTyping(XDateTimeEdit* self, bool advance)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int index;
    XDateTime old;
    bool changed;
    int committedCode = (int)XDateTimeEditSection_NoSection;
    int committedPrev = -1;
    if (!self) return;
    if (self->m_typingDigits <= 0) {
        xdt_resetTyping(self);
        return;
    }
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
    index = self->m_typingSection;
    if (index < 0 || index >= n ||
        toks[index].code != self->m_currentSection) {
        xdt_resetTyping(self);
        return;
    }
    {
        XdtSectionTok tok = toks[index];
        int value = self->m_typingValue;
        xdt_resetTyping(self);
        old = self->m_dateTime;
        committedCode = tok.code;
        committedPrev = xdt_sectionValueOf(&old, tok.code);
        if (!xdt_applySectionValue(self, &tok, value)) {
            /* 非法组合：放弃中间态，重渲染回当前值。 */
            xdt_refreshText(self);
            xdt_selectCurrentSection(self);
            return;
        }
    }
    xdt_clamp(self);
    changed = XDateTime_compare(&old, &self->m_dateTime) != 0;
    xdt_refreshText(self);
    if (changed) xdt_emitPartChanged(self, &old);
    if (advance) {
        int target = (index + 1 < n) ? index + 1 : n - 1;
        /* 键入撤销快照（G3）：满位提交+进位跳段路径记录被提交段与其
         * 落账前值，供退格单步回退（值回前值+整段重选回退段）。 */
        self->m_undoSection = committedCode;
        self->m_undoValue = committedPrev;
        self->m_currentSection = toks[target].code;
        XWidget_update((XWidget*)self);
    }
    xdt_selectCurrentSection(self);
}

/** @brief 退格回退最近一次键入落账（G3 退格回归，2026-10-02 定版）：
 *         段值恢复到快照值并整段重选回退段（判据「值回前值且不跳
 *         段」——退格=撤销本段键入，不触发进位跳段；快照单步消费即
 *         失效）。原值经 xdt_applySectionValue 既有校验/钳位链写回；
 *         z 段先按记号位宽逆折回键入域（落账 ms=value*10^(3-width)，
 *         提交后 actual=typed*10^(3-width) 恒成立故整除无损）。段码
 *         不在格式中（格式中途变更）时仅重渲染恢复显示。 */
static void xdt_undoTypingCommit(XDateTimeEdit* self)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int i;
    int index = -1;
    int value;
    XdtSectionTok tok;
    XDateTime old;
    bool changed;
    if (!self ||
        self->m_undoSection <= (int)XDateTimeEditSection_NoSection)
        return;
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
    for (i = 0; i < n; ++i) {
        if (toks[i].code == self->m_undoSection) { index = i; break; }
    }
    value = self->m_undoValue;
    self->m_undoSection = (int)XDateTimeEditSection_NoSection;
    if (index < 0) {
        xdt_refreshText(self);
        xdt_selectCurrentSection(self);
        return;
    }
    tok = toks[index];
    if (tok.spec == 'z') {
        for (i = tok.width; i < 3; ++i) value /= 10;
    }
    old = self->m_dateTime;
    if (!xdt_applySectionValue(self, &tok, value)) {
        xdt_refreshText(self);
        xdt_selectCurrentSection(self);
        return;
    }
    self->m_currentSection = tok.code;
    xdt_clamp(self);
    changed = XDateTime_compare(&old, &self->m_dateTime) != 0;
    xdt_refreshText(self);
    if (changed) xdt_emitPartChanged(self, &old);
    xdt_selectCurrentSection(self);
    XWidget_update((XWidget*)self);
}

/** @brief 键入一位数字到当前段（F3-①：段内累积替换而非行编辑纯文本
 *         插入）。候选超段自然上限时以本位数字重启（对标 Qt 高位重启，
 *         如月份键入 1、3 → 13 不可达 → 重启为 3）；提交+跳段判据对齐
 *         Qt 6.8.3（qdatetimeedit.cpp:1271-1290 进位门 + skipToNextSection
 *         的 potentialValue 口径，缺陷⑦ 2026-10-02 定版）：满键入容量
 *         （段自然上限位数，见 xdt_sectionTypeCapacity）或前缀不可扩展
 *         （无任何补位能落进段值域，如 M 段打 3、HH 段打 9）才提交并
 *         跳下一段——单记号 M/d 段打 1 位不再立即跳段（月份 10/11/12、
 *         日 10..31 可续打）；HH/mm/ss 段单数字不可扩展时由「等满位」
 *         提前为「即打即提交+跳段」（Qt 一致口径）。星期/上下午文案段
 *         不接受数字（对标 Qt 展示档）。 */
static void xdt_typeDigit(XDateTimeEdit* edit, int digit)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int index;
    int width;
    int maxv;
    int minv;
    int cap;
    int candidate;
    int digits;
    if (!edit) return;
    /* 只读门禁（缺陷 E）：XAbstractSpinBox_setReadOnly 此前仅拦内嵌行
       编辑，分段键入走壳层累积无门禁——setReadOnly(true) 后数字键入
       照常改值。只读时键入直接拒绝（对标 Qt readOnly 拦键盘编辑）。 */
    if (XAbstractSpinBox_isReadOnly((XAbstractSpinBox*)edit)) return;
    n = xdt_tokenizeTyped(xdt_effectiveFormat(edit), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(edit));
    if (n <= 0) return;
    index = XDateTimeEdit_currentSectionIndex(edit);
    if (index < 0 || index >= n) return;
    width = toks[index].width;
    if ((toks[index].spec == 'd' && width >= 3) ||
        toks[index].spec == 'A') {
        return;
    }
    maxv = xdt_sectionDigitMax(toks[index].spec, width);
    /* 段值域下界（Qt absoluteMin 口径）：日期段 1、时间段 0。 */
    minv = (toks[index].spec == 'y' || toks[index].spec == 'M' ||
            toks[index].spec == 'd') ? 1 : 0;
    cap = xdt_sectionTypeCapacity(toks[index].spec, width);
    if (edit->m_typingSection != index) xdt_resetTyping(edit);
    edit->m_typingSection = index;
    candidate = edit->m_typingValue * 10 + digit;
    digits = edit->m_typingDigits + 1;
    if (candidate > maxv) {
        candidate = digit;
        digits = 1;
    }
    edit->m_typingValue = candidate;
    edit->m_typingDigits = digits;
    xdt_showTypingBuffer(edit);
    XWidget_update((XWidget*)edit);
    /* 进位门（缺陷⑦）：满键入容量或前缀不可扩展→提交+跳段；否则留段
       续打（10/11/12 等多位值可达）。原判据 digits>=width 把格式记号
       位宽当键入容量，单记号 M/d 段打 1 位即提前跳段——月份 10/11/12
       永不可键入；数字盘连拍与物理键入同经本函数（VK 合成数字键直发
       壳 keyPressEvent 数字分支），一处修复三路同治。 */
    if (digits >= cap ||
        !xdt_typingCanExtend(candidate, digits, cap, minv, maxv))
        xdt_commitTyping(edit, true);
}

/** @brief 聚焦指定段序号：先落账旧段未满位输入（换段才提交，同段
 *         续打保持累积），再落地段码+整段选中（点击/方向键/API 共用，
 *         F3-②）。 */
static void xdt_focusSectionIndex(XDateTimeEdit* self, int index)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    if (!self) return;
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
    if (index < 0 || index >= n) return;
    if (self->m_typingDigits > 0 && self->m_typingSection != index)
        xdt_commitTyping(self, false);
    self->m_currentSection = toks[index].code;
    self->m_typingSection = index;
    /* 显式换段（点击/方向键/Tab/API）即弃键入撤销快照：退格回退只服
       务最近一次键入落账，编辑锚点已迁移不再回跳（进位跳段走
       commitTyping 尾部直写段码不经此，G3 快照保留）。 */
    self->m_undoSection = (int)XDateTimeEditSection_NoSection;
    XWidget_update((XWidget*)self);
    xdt_selectCurrentSection(self);
}

/** @brief 按分段枚举码聚焦段（API 路径用；码不在格式中时不动）。 */
static void xdt_focusSectionCode(XDateTimeEdit* self, int code)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int i;
    if (!self) return;
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
    for (i = 0; i < n; ++i) {
        if (toks[i].code == code) {
            xdt_focusSectionIndex(self, i);
            return;
        }
    }
}

/** @brief 点击字符位置→段序号（F3-② 命中测试；段右边界归本段——
 *         点击段末位右半仍落在该段，对标 Qt sectionAt(pos) 的
 *         pos ∈ [start, end] 归属）。无分段返回 -1。 */
static int xdt_sectionIndexAt(XDateTimeEdit* self, int charPos)
{
    char buf[128];
    XdtSectionRange ranges[XDT_SECTION_MAX];
    XdtSectionTok toks[XDT_SECTION_MAX];
    int count = 0;
    int n;
    int i;
    int hit = -1;
    if (!self) return -1;
    xdt_render(self, buf, sizeof(buf), ranges, XDT_SECTION_MAX, &count);
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
    for (i = 0; i < n && i < count; ++i) {
        if (ranges[i].len <= 0) continue;
        if (charPos <= ranges[i].start + ranges[i].len) return i;
        hit = i;
    }
    return hit;
}

/* 用户变体信号发射器已按 Qt 归属迁往子类（XDateEdit.c/XTimeEdit.c
 * 构造连接 dateChanged/timeChanged 转发发射），基类不再持有。 */

/* ==================== calendarPopup 弹层前向声明（弹层机器实现见
 *                     「日历弹层」节；键入/点击路径先于此引用） ==================== */
#if XCALENDARWIDGET_ON
static void xdt_popupShow(XDateTimeEdit* self);
static void xdt_popupHide(XDateTimeEdit* self);
static void xdt_syncPopupValue(XDateTimeEdit* self); /* 弹层随值同步（缺陷 F）。 */
static void xdt_fieldKeyboardArm(XDateTimeEdit* edit); /* 缺陷⑧ 字段路径键盘确认接线（实现见「日历弹层」节 xdt_kbReadyConn 声明旁；mousePressEvent 先于此引用）。 */
#endif
static XRect xdt_arrowRect(XDateTimeEdit* edit);

/* ==================== 虚槽重载 ==================== */

/** @brief 纯时间编辑（parserType=Time）时段步进的日内回绕。
 * @details XTimeEdit 载体为单日 datetime（纯时间格式收窄后日期范围=当
 *         天，XTimeEdit_init 收窄注释），时段步进走 epoch 加算跨过 24h
 *         边界时日期侧越界，xdt_clamp 会把值钳成 23:59:59.999——Qt 口
 *         径（QTimeEdit 的值即 time-of-day，QTime::addSecs 午夜回绕）
 *         应回到 00 并保留分秒（与分/秒 59→00 带进位的既有 epoch 模型
 *         同源，仅放开单日日期侧钳制）。仅时间段分支且 parserType=Time
 *         时启用；日期/混合格式路径行为不变。 */
static void xdt_stepWrapTimeOfDay(XDateTimeEdit* edit, int64_t deltaMs)
{
    const int64_t dayMs = 86400000;
    int64_t ms;
    int64_t tod;
    if (!edit || deltaMs == 0) return;
    ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
    tod = ms % dayMs;
    if (tod < 0) tod += dayMs;
    ms -= tod;
    tod = (tod + deltaMs) % dayMs;
    if (tod < 0) tod += dayMs;
    XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms + tod);
}

static void XDateTimeEdit_stepBy(XAbstractSpinBox* self, int steps)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XDateTime old;
    int section;
    if (!edit || steps == 0) return;
    /* 只读门禁（缺陷 E）：虚槽入口是全部步进通道的公共汇聚点——方向
       键 Up/Down/PageUp/Home/End 与滚轮经基类 stepBy_base 虚派发到此，
       日历/时间弹层的 ▲▼ 按钮与滚轮（xdtp_timeRowPress/
       VXDateTimePopup_wheelEvent）同走虚派发，stepUp/stepDown 公共槽
       亦然。只读时一律拒绝（滚轮同路径天然被拦；对标 Qt readOnly
       拦键盘与滚轮编辑、值仅程序性 API 可变）。 */
    if (XAbstractSpinBox_isReadOnly((XAbstractSpinBox*)edit)) return;
    /* F3-①：未满位键入先落账再步进（对标 Qt 步进前结算当前段键入）。 */
    xdt_commitTyping(edit, false);
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
        if (edit->m_parserType == (int)XDateTimeEditParserType_Time)
            xdt_stepWrapTimeOfDay(edit, (int64_t)steps * 3600000);
        else
            XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    } else if (section == (int)XDateTimeEditSection_MinuteSection) {
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps * 60000;
        if (edit->m_parserType == (int)XDateTimeEditParserType_Time)
            xdt_stepWrapTimeOfDay(edit, (int64_t)steps * 60000);
        else
            XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    } else if (section == (int)XDateTimeEditSection_MSecSection) {
        /* 毫秒段步进：1 毫秒/步（跨秒/分/时的进位由 epoch 换算天然承担）。 */
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps;
        if (edit->m_parserType == (int)XDateTimeEditParserType_Time)
            xdt_stepWrapTimeOfDay(edit, (int64_t)steps);
        else
            XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    } else if (section == (int)XDateTimeEditSection_AmPmSection) {
        /* 上下午段步进 = 翻转上午/下午（±12 小时，对标 Qt 步进 AmPm 段）。 */
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps * 43200000;
        if (edit->m_parserType == (int)XDateTimeEditParserType_Time)
            xdt_stepWrapTimeOfDay(edit, (int64_t)steps * 43200000);
        else
            XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    } else {
        int64_t ms = XDateTime_toMSecsSinceEpoch(&edit->m_dateTime);
        ms += (int64_t)steps * 1000;
        if (edit->m_parserType == (int)XDateTimeEditParserType_Time)
            xdt_stepWrapTimeOfDay(edit, (int64_t)steps * 1000);
        else
            XDateTime_setMSecsSinceEpoch(&edit->m_dateTime, ms);
    }
    xdt_clamp(edit);
    if (XDateTime_compare(&old, &edit->m_dateTime) != 0) {
        xdt_refreshText(edit);
        /* 步进后保持当前分段整体反选（对标 Qt 步进不清除段落选区）。 */
        xdt_selectCurrentSection(edit);
        /* P1-4：步进路径同步发射 dateChanged/timeChanged（对标
         * QDateTimeEdit：方向键/箭头步进改变日期或时间部分时对应部分
         * 信号同样发射）。用户变体信号（userDateChanged/userTimeChanged）
         * 由子类构造的 dateChanged→user*Changed 连接转发发射——与 Qt
         * QDateEdit/QTimeEdit 构造函数的 connect 同构，步进/键入提交/
         * 程序性 setDate 等一切 dateChanged 发射点均随之触发（Qt 全变更
         * 口径，qdatetimeedit.cpp:2289-2312 emitSignals）。
         * 缺陷 F：发射收敛到 xdt_emitPartChanged 单点（此前此处为
         * emitChanged+条件 emitDate/emitTime 的手写展开，语义与汇聚点
         * 完全一致），值变化后弹层开着时日历选中页/时间行随动刷新。 */
        xdt_emitPartChanged(edit, &old);
    }
}

static int XDateTimeEdit_stepEnabled(const XAbstractSpinBox* self)
{
    (void)self;
    return (int)XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
           (int)XAbstractSpinBoxStepEnabledFlag_StepDownEnabled;
}

/** @brief 从 text[*pos] 起跳过非数字并截取数字串（P1-5 键入解析的取数
 *         原语，对标 Qt 分节 parse 的按位取数字）。
 * @param text      编辑文本。
 * @param pos       入/出字节游标；截到数字时推进越过数字串。
 * @param maxDigits 最多截取位数（yyyy=4/MM..ss=2/z=3）：与记号位宽
 *                  对齐可避免字面字符被删除后数字串串位。
 * @param outValue  截到的数值（无数字时不写）。
 * @return 是否截到数字。 */
static bool xdt_takeDigits(const char* text, int* pos, int maxDigits,
                           int* outValue)
{
    int p = *pos;
    int value = 0;
    int digits = 0;
    if (!text) return false;
    while (text[p] != '\0' && (text[p] < '0' || text[p] > '9')) ++p;
    while (text[p] >= '0' && text[p] <= '9' && digits < maxDigits) {
        value = value * 10 + (text[p] - '0');
        ++p;
        ++digits;
    }
    if (digits == 0) return false;
    *pos = p;
    *outValue = value;
    return true;
}

/** @brief 在 text[pos] 起 16 字节窗口内查找文案（AmPm 中文文案定位用）。
 * @return 命中字节偏移；未命中返回 -1。 */
static int xdt_findText(const char* text, int pos, const char* needle)
{
    size_t n = XStrlen(needle);
    int i;
    for (i = pos; i <= pos + 16 && text[i] != '\0'; ++i) {
        if (XStrncmp(&text[i], needle, n) == 0) return i;
    }
    return -1;
}

/** @brief 尝试在 text[pos] 精确匹配星期文案（长名优先）；命中返回字节
 *         长度，否则 0。星期节为展示档，仅用于解析游标跳过。 */
static int xdt_matchDayName(const char* text, int pos)
{
    int i;
    for (i = 0; i < 7; ++i) {
        size_t len = XStrlen(xdt_dayLongNames[i]);
        if (XStrncmp(&text[pos], xdt_dayLongNames[i], len) == 0)
            return (int)len;
    }
    for (i = 0; i < 7; ++i) {
        size_t len = XStrlen(xdt_dayShortNames[i]);
        if (XStrncmp(&text[pos], xdt_dayShortNames[i], len) == 0)
            return (int)len;
    }
    return 0;
}

/** @brief 按显示格式把编辑文本分节解析回日期时间候选值（P1-5 键入提交
 *         的解析核心，对标 QDateTimeEdit 私有 parse/interpret 路径：
 *         格式串与编辑文本并行游走——数字节截数字串、AmPm 节识别
 *         「上午/下午」、星期节为展示档跳过、字面字符宽松对齐）。
 * @details 各节初值取自当前值，未键入的节保持不变（对标 Qt 分段独立
 *          编辑语义）；12 小时制 'h' 节配合上下午文案折算回 24 小时制
 *          （13→下午1，0→上午12）；最终经 XDate_setDate/XTime_setHMS
 *          合法性校验。
 * @retval true  out 写入解析结果。
 * @retval false 非法输入（越界/非法组合），调用方回退旧值。 */
static bool xdt_parseEditText(const XDateTimeEdit* self, const char* text,
                              XDateTime* out)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    const char* fmt;
    int textLen;
    int ti = 0;
    size_t i = 0; /* 格式串字节游标（size_t 对齐记号 pos，避免符号比较）。 */
    int t = 0;    /* 编辑文本字节游标。 */
    int n;
    int year, month, day, hour, minute, second, msec;
    bool amSeen = false;
    bool pmSeen = false;
    bool has12h = false;
    if (!self || !text || !out) return false;
    fmt = xdt_effectiveFormat(self);
    n = xdt_tokenizeTyped(fmt, toks, XDT_SECTION_MAX,
                          xdt_tokenParserType(self));
    textLen = (int)XStrlen(text);
    year = XDate_year(&out->m_date);
    month = XDate_month(&out->m_date);
    day = XDate_day(&out->m_date);
    hour = XTime_hour(&out->m_time);
    minute = XTime_minute(&out->m_time);
    second = XTime_second(&out->m_time);
    msec = XTime_msec(&out->m_time);
    while (fmt[i] != '\0' && t < textLen) {
        if (ti < n && i == toks[ti].pos) {
            const XdtSectionTok* tok = &toks[ti];
            ++ti;
            switch (tok->spec) {
            case 'y':
                (void)xdt_takeDigits(text, &t, 4, &year);
                break;
            case 'M':
                (void)xdt_takeDigits(text, &t, 2, &month);
                break;
            case 'd':
                if (tok->width >= 3) {
                    /* 星期节：展示档不带数据，跳过文案即可（对标 Qt）。 */
                    int len = xdt_matchDayName(text, t);
                    if (len > 0) t += len;
                } else {
                    (void)xdt_takeDigits(text, &t, 2, &day);
                }
                break;
            case 'H':
                (void)xdt_takeDigits(text, &t, 2, &hour);
                break;
            case 'h':
                has12h = true;
                (void)xdt_takeDigits(text, &t, 2, &hour);
                break;
            case 'm':
                (void)xdt_takeDigits(text, &t, 2, &minute);
                break;
            case 's':
                (void)xdt_takeDigits(text, &t, 2, &second);
                break;
            case 'z':
                (void)xdt_takeDigits(text, &t, 3, &msec);
                break;
            case 'A': {
                /* 上下午节：定位「上午/下午」文案记录极性（对标 Qt
                 * AmPm 节 parse；未找到时保持 24 小时直读）。 */
                int am = xdt_findText(text, t, xdt_amText);
                int pm = xdt_findText(text, t, xdt_pmText);
                if (am >= 0 && (pm < 0 || am <= pm)) {
                    amSeen = true;
                    t = am + (int)XStrlen(xdt_amText);
                } else if (pm >= 0) {
                    pmSeen = true;
                    t = pm + (int)XStrlen(xdt_pmText);
                }
                break;
            }
            default:
                break;
            }
            i += tok->width; /* 记号均为 ASCII：宽度=格式字节长。 */
            continue;
        }
        /* 字面字符：优先逐字节精确对齐；不一致（用户编辑导致错位）时
         * 在 16 字节窗口内找同一字面再对齐，找不到则只推进格式游标。 */
        if (text[t] == fmt[i]) {
            ++t;
            ++i;
            continue;
        }
        {
            int k = t + 1;
            while (k <= t + 16 && text[k] != '\0' && text[k] != fmt[i]) ++k;
            if (k <= t + 16 && text[k] == fmt[i]) t = k + 1;
        }
        ++i;
    }
    if (has12h && (amSeen || pmSeen)) {
        /* 12 小时制 + 上下午文案合并回 24 小时制（对标 Qt：12AM→0、
         * 12PM→12、PM 其余 +12）；无 AP 文案时按 24 小时直读。 */
        if (pmSeen) hour = (hour % 12) + 12;
        else hour = hour % 12;
    }
    {
        XDate d;
        XTime tm;
        /* 合法性终检（对标 QDateTimeEdit 非法文本拒绝提交）：日期经
         * 闰年/月天数校验，时间经时分秒毫秒范围校验。 */
        if (!XDate_setDate(&d, year, month, day)) return false;
        if (!XTime_setHMS(&tm, hour, minute, second, msec)) return false;
        out->m_date = d;
        out->m_time = tm;
    }
    return true;
}

/** @brief 提交虚槽重载（P1-5：基类默认空操作导致键入不生效；对标
 *         QDateTimeEdit::interpretText / 私有 interpret）：把编辑框
 *         键入文本按当前格式分节解析回值并提交。基类 Enter/失焦/
 *         隐藏/关闭路径（XAbstractSpinBox interpretText 分派）随之
 *         生效。@details 解析失败或基类 cleared 待解释态时回退旧值
 *         重渲染（对标 correctionMode=CorrectToPreviousValue）；成功
 *         且值变化时刷新显示并发射 dateTimeChanged/dateChanged/
 *         timeChanged。 */
static void XDateTimeEdit_interpret(XAbstractSpinBox* self)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XLineEdit* line;
    const char* text;
    XDateTime old;
    XDateTime parsed;
    if (!edit) return;
    if (self->m_cleared) {
        /* 基类 clear() 后的待解释态：不回填值（对标 Qt 私有 cleared）。 */
        self->m_cleared = false;
        xdt_resetTyping(edit);
        return;
    }
    /* F3-①：失焦/隐藏/关闭/Enter 的解释路径先落账未满位键入，避免
     * 中间态显示文本（如 "0020-…"）被按字面解析。 */
    xdt_commitTyping(edit, false);
    line = XAbstractSpinBox_lineEdit(self);
    if (!line) return;
    text = XLineEdit_text(line);
    if (!text) return;
    old = edit->m_dateTime;
    parsed = old;
    if (!xdt_parseEditText(edit, text, &parsed)) {
        /* 非法键入：回退旧值并按当前节重渲染（对标 Qt 回退上次有效值）。 */
        xdt_refreshText(edit);
        xdt_selectCurrentSection(edit);
        return;
    }
    edit->m_dateTime = parsed;
    xdt_clamp(edit);
    /* 值未变也重渲染：把键入的非规范文本（如缺前导零）规整回显示格式。 */
    xdt_refreshText(edit);
    xdt_selectCurrentSection(edit);
    if (XDateTime_compare(&old, &edit->m_dateTime) != 0)
        xdt_emitPartChanged(edit, &old);
}

/** @brief 键盘按下：数字键在当前段内累积替换（F3-①，满位自动跳段，
 *         不再转发内嵌行编辑做纯文本插入）；Left/Right 在分段间移动并
 *         整段选中当前段（对标 QDateTimeEdit 方向键跨段导航，段间不
 *         循环）；Tab/Backtab（免 Shift/带 Shift）在分段间前进/后退，
 *         首/末段越界时交回基类做焦点遍历迁出（对标 Qt keyPressEvent
 *         Tab 分支 → focusNextPrevChild 的段内拦截语义）；Home/End 拦
 *         截为光标到当前节首/节尾、值不动（P1-6，对标 Qt 行编辑语义，
 *         覆写基类的 min/max 值跳转）；其余按键交基类（Up/Down/
 *         PageUp/PageDown 步进、Return 提交、其余转发内嵌编辑框）。 */
static void XDateTimeEdit_keyPressEvent(XWidget* self, XEvent* event)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XKeyEvent* ke;
    int key;
    int mods;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = XKeyEvent_key(ke);
    mods = (int)XKeyEvent_modifiers(ke);
    if (key >= XKey_0 && key <= XKey_9 &&
        (mods & (XKeyboardModifier_ControlModifier |
                 XKeyboardModifier_AltModifier)) == 0) {
        /* F3-①：数字键入=当前段累积替换（段内满位自动跳下一段）。
         * 事件在此终结，不再转发内嵌行编辑——此前 "2020" 键入被行编辑
         * 当纯文本插进年段（撑成 8 位），根因即此转发。 */
        xdt_typeDigit(edit, key - XKey_0);
        XEvent_accept(event);
        return;
    }
    /* 缺陷⑧ 同族观察项：Esc 收层不冲账（对标收起键只弃 IME 草稿不碰
     * 编辑缓冲，未满位中间态留待失焦 interpret 结算）。常态弹层抓取下
     * Esc 先行由弹层 keyPressEvent 收层（幂等），此处壳分支为兜底；字
     * 段路径收键盘置 m_userCollapsed 闩锁（守护轮询不重弹）。无键盘会
     * 话不 accept 不 return，事件照旧落基类转发行编辑（零回归）。 */
    if (key == XKey_Escape) {
        bool closed = false;
#if XCALENDARWIDGET_ON
        if (edit->m_popupVisible) {
            XPrintf("[XDateTimeEdit] popupHide<-shell-esc\n");
            xdt_popupHide(edit);
            closed = true;
        }
#endif
#if XVIRTUALKEYBOARD_ON
        if (!closed) {
            XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
            if (kb && XVirtualKeyboard_popupVisible(kb) &&
                kb->m_target == (XWidget*)edit) {
                /* 会话守卫同确认槽：只收本壳的字段键盘，他会话不动。 */
                XVirtualKeyboard_closePopup(kb);
                closed = true;
            }
        }
#endif
        if (closed) {
            XEvent_accept(event);
            return;
        }
    }
    /* 缺陷⑨（G3 退格回归，2026-10-02 定版）：壳层拦截退格按段模型
     * 结算，绝不交基类转发行编辑——缺陷⑦满位提交+advance 后整段选中
     * 已前移到下一段，行编辑退格删除的是合成渲染串中的「当前选中段
     * 文本」（G3 实测把「时」段从 '2030-10-23 03:41:57' 抹成
     * ' :41:57' 且不自愈；Qt 虽同样基类→行编辑 qdatetimeedit.cpp:1270，
     * 但 Qt 行编辑持有完整文本+interpret 兜底，XGui 渲染串为壳层合成
     * 件，自由删除即破坏显示）。三级语义：①有未满位缓冲→削一位
     * （末位放弃中间态恢复已落账值），保持段位不跳段；②无缓冲有键
     * 入撤销快照（满位提交+进位跳段场景）→段值回退到本次落账前值并
     * 整段重选回退段（判据「值回 02 且不跳段」）；③皆无→幂等重选当
     * 前段。退格属编辑动作，键盘会话不收层（G3 判据无收层要求）。 */
    if (key == XKey_Backspace) {
        if (edit->m_typingDigits > 0) {
            if (edit->m_typingDigits > 1) {
                edit->m_typingValue /= 10;
                edit->m_typingDigits--;
                xdt_showTypingBuffer(edit);
            } else {
                xdt_resetTyping(edit);
                xdt_refreshText(edit);
                xdt_selectCurrentSection(edit);
            }
            XWidget_update((XWidget*)edit);
            XEvent_accept(event);
            return;
        }
        if (edit->m_undoSection != (int)XDateTimeEditSection_NoSection) {
            xdt_undoTypingCommit(edit);
            XEvent_accept(event);
            return;
        }
        xdt_selectCurrentSection(edit);
        XEvent_accept(event);
        return;
    }
    /* 其余按键先落账未满位键入（换段/步进/提交前结算，满位已在键入
     * 路径自动提交，这里兜底 1~位宽-1 位的中间态）。 */
    xdt_commitTyping(edit, false);
    if (key == XKey_Tab || key == XKey_Backtab) {
        /* Tab/Backtab 段间导航（对标 Qt：forward=非 Backtab 且 Tab 不带
         * Shift；段内可走则移段并整段选中，首/末段越界交基类忽略后
         * 走焦点遍历迁出控件）。 */
        bool forward = (key != XKey_Backtab) &&
                       !(key == XKey_Tab &&
                         (mods & XKeyboardModifier_ShiftModifier) != 0);
        XdtSectionTok toks[XDT_SECTION_MAX];
        int count = xdt_tokenizeTyped(xdt_effectiveFormat(edit), toks,
                                      XDT_SECTION_MAX,
                                      xdt_tokenParserType(edit));
        int index = XDateTimeEdit_currentSectionIndex(edit);
        int target = forward ? index + 1 : index - 1;
        if (count > 0 && target >= 0 && target < count) {
            xdt_focusSectionIndex(edit, target);
            XEvent_accept(event);
            return;
        }
        /* 段外（进入/离开控件）：落到基类——Tab/Backtab 显式忽略交焦
         * 点遍历。 */
    }
    if (key == XKey_Left || key == XKey_Right) {
        XdtSectionTok toks[XDT_SECTION_MAX];
        int count = xdt_tokenizeTyped(xdt_effectiveFormat(edit), toks,
                                      XDT_SECTION_MAX,
                                      xdt_tokenParserType(edit));
        if (count > 0) {
            int index = XDateTimeEdit_currentSectionIndex(edit);
            int target = (key == XKey_Left)
                ? (index > 0 ? index - 1 : 0)
                : (index + 1 < count ? index + 1 : count - 1);
            /* 段序号落地统一走 focus：段码+整段选中（与点击同口径）。 */
            xdt_focusSectionIndex(edit, target);
            XEvent_accept(event);
            return;
        }
        /* 无分段格式：退回基类，让方向键回到编辑框光标移动。 */
    }
    if (key == XKey_Home || key == XKey_End) {
        /* P1-6：基类（XAbstractSpinBox keyPressEvent）把 Home/End 实现
         * 为 min/max 值跳转（大步数 stepBy），对日期时间控件属数据破坏；
         * Qt 6.8.3 的 QDateTimeEdit::keyPressEvent 不消费 Home/End，
         * 事件落到 QAbstractSpinBox 行编辑做光标移动。此处子类拦截：
         * Home/End = 光标到当前节首/节尾，值不动（对标行编辑语义）。 */
        XdtSectionRange ranges[XDT_SECTION_MAX];
        XdtSectionTok toks[XDT_SECTION_MAX];
        XLineEdit* line =
            XAbstractSpinBox_lineEdit((XAbstractSpinBox*)edit);
        char buf[128];
        int count = 0;
        int n;
        int index = -1;
        int i;
        xdt_render(edit, buf, sizeof(buf), ranges, XDT_SECTION_MAX, &count);
        n = xdt_tokenizeTyped(xdt_effectiveFormat(edit), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(edit));
        for (i = 0; i < n; ++i) {
            if (toks[i].code == edit->m_currentSection) { index = i; break; }
        }
        if (!line) {
            XEvent_ignore(event);
            return;
        }
        if (index >= 0 && index < count && ranges[index].len > 0) {
            XLineEdit_setCursorPosition(line,
                (key == XKey_Home) ? ranges[index].start
                                   : ranges[index].start + ranges[index].len);
        } else {
            /* 当前分段不在格式中：退回行编辑整文本 home/end。 */
            int total = xdt_utf8Chars(buf, (int)XStrlen(buf));
            XLineEdit_setCursorPosition(line, (key == XKey_Home) ? 0 : total);
        }
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XAbstractSpinBox, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 鼠标按下（F3-②/③）：行编辑已设鼠标穿透，点击命中本控件。
 *         编辑区内左键：像素→字符命中测试定位段 → 落账旧段输入 →
 *         setCurrentSection+整段选中 → 焦点移回本控件（键盘主权回收，
 *         后续按键直达分段导航/键入，不再落入行编辑文本光标模式）。
 *         编辑区外（上下按钮条）与非左键不接管，保持既有行为。对标
 *         QDateTimeEdit 点击定段的分段编辑模型。 */
static void XDateTimeEdit_mousePressEvent(XWidget* self, XEvent* event)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XLineEdit* line;
    XMouseEvent* me;
    XRect leGeo;
    XPoint local;
    int charPos;
    int index;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        if (self && event)
            XClass_Parent(XWidget, EXWidget_MousePressEvent,
                          void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    me = (XMouseEvent*)event;
    line = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)edit);
    if (XMouseEvent_button(me) != XMouseButton_LeftButton || !line) {
        /* 非左键不接管：父链传播/右键上下文菜单合成照旧。 */
        XEvent_ignore(event);
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
#if XCALENDARWIDGET_ON
    if (edit->m_calendarPopup) {
        /* 日历态：点下拉箭头开/收弹层（对标 Qt QDateTimeEdit 日历态
           命中 SC_ComboBoxArrow 才弹层；弹层存活时此点击落在弹层抓取
           域外——收层走弹层越界判定，不会抵达本分支）。 */
        XRect arrow = xdt_arrowRect(edit);
        XPoint wpos = XMouseEvent_position(me);
        if (XRect_contains(&arrow, wpos.x, wpos.y)) {
            /* 先落焦再开/收层（顺序即语义）：守护轮询 ② 以焦点边沿触发
               重弹——若先关键盘后落焦，closePopup 的 prevFocus 采样记的
               还是旧焦点，随后焦点迁入 WA14 壳会构成边沿、约 200ms 后
               把键盘重新弹出（缺陷 A 复发）。先 setFocus 再 closePopup
               则采样已同步为壳，无边沿可依。 */
            XWidget_setFocus(self);
            if (edit->m_popupVisible) {
                XPrintf("[XDateTimeEdit] popupHide<-arrow\n");
                xdt_popupHide(edit);
            }
            else xdt_popupShow(edit);
#if XVIRTUALKEYBOARD_ON
            /* 缺陷 A（箭头误触发屏幕键盘）消除：同拍 notifyPress（命中
               WA14 壳，XWidget.c:1520 汇聚点）已先于本 mousePressEvent
               把屏幕键盘弹出，此处确定性关掉——show 未出帧即收，无闪现
               且无字符注入。语义=「下箭头只开日历/时间弹层，不弹键盘」
               （用户 2026-10-01 二次修正口径）；开/收弹层两分支都关，
               收层拍同样不允许键盘残留。 */
            {
                XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
                if (kb) XVirtualKeyboard_closePopup(kb);
            }
#endif
            XEvent_accept(event);
            return;
        }
        /* 箭头之外按既有分段点击路径继续（含编辑区内/外分支）。 */
    }
#endif
    leGeo = XWidget_geometry((XWidget*)line);
    local.x = XMouseEvent_position(me).x - leGeo.x;
    local.y = XMouseEvent_position(me).y - leGeo.y;
    if (local.x < 0 || local.x >= leGeo.width ||
        local.y < 0 || local.y >= leGeo.height) {
        /* 编辑区外（步进按钮条）：不接管，保持既有无点击步进行为。 */
        XEvent_ignore(event);
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    charPos = XLineEdit_cursorPositionAt(line, &local);
    index = xdt_sectionIndexAt(edit, charPos);
    if (index < 0) {
        XEvent_ignore(event);
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    xdt_focusSectionIndex(edit, index);
#if XVIRTUALKEYBOARD_ON
    /* 缺陷⑧：同拍 notifyPress（XWidget.c 汇聚点先于本 handler）已以本
     * 壳为 target 弹出 Digits 键盘，此处补 ready→确认槽消费者（先断旧
     * 后连新，与时间行接线互斥替换）——否则确认键零消费者无效果。 */
    xdt_fieldKeyboardArm(edit);
#endif
    /* 键盘主权回收：焦点从行编辑移回本控件，键事件改投本控件
     * keyPressEvent（方向键=跨段导航而非行编辑光标移动）。 */
    XWidget_setFocus(self);
    XEvent_accept(event);
}

/* ==================== 日历弹层（calendarPopup=true 接线，对标
 *                     QDateTimeEditPrivate::QDateTimePopup 容器弹层） ==================== */

#if XCALENDARWIDGET_ON

XCLASS_DEFINE_BEGING(XDateTimePopup)
XCLASS_DEFINE_EXTEND_END(XDateTimePopup, XWidget)

/** @brief 弹层内容三态（按当前 displayFormat 解析出的分段构成自动
 *         判定，一处机制三态生效，对标 Qt QDateTimeEditPrivate::
 *         QDateTimePopup 承载内容随编辑语义变化的思路）：
 *         仅日期段→纯日历；仅时间段→纯时间设定行；两者都有→日历在
 *         上+时间设定行在下。 */
typedef enum XdtPopupMode
{
    XdtPopupMode_DateOnly = 0, /**< 仅日期段：弹层=纯日历。 */
    XdtPopupMode_TimeOnly = 1, /**< 仅时间段：弹层=纯时间设定行。 */
    XdtPopupMode_DateTime = 2  /**< 日期+时间段：日历在上+时间行在下。 */
} XdtPopupMode;

/* 时间设定行（水平一排「[时] ▲ 18 ▼」三组）布局常量；绘制语言与
 * XCalendarWidget 导航条一致（按钮底凸起斜面+实心三角箭头）。 */
#define XDT_TIMEROW_H 34   /**< 时间设定行总高。 */
#define XDT_TIMEROW_W 280  /**< 纯时间弹层宽（与日历控件同宽对齐观感）。 */
#define XDT_TG_LABEL_W 16  /**< 组段标签宽（单个汉字）。 */
#define XDT_TG_BTN_W 16    /**< 组上/下箭头按钮宽。 */
#define XDT_TG_BTN_H 16    /**< 组上/下箭头按钮高。 */
#define XDT_TG_VALUE_W 32  /**< 组段值框宽（两位数字居中）。 */
#define XDT_TG_VALUE_H 18  /**< 组段值框高。 */
#define XDT_TG_GAP 2       /**< 组内元素间距。 */
/** @brief 组内容总宽（标签+▲+值+▼ 水平相邻，与命中/绘制同源）。 */
#define XDT_TG_CONTENT_W                                 \
    (XDT_TG_LABEL_W + XDT_TG_GAP + XDT_TG_BTN_W + XDT_TG_GAP + \
     XDT_TG_VALUE_W + XDT_TG_GAP + XDT_TG_BTN_W)

/** @brief 日历弹层容器对象；m_base 必须是第一个成员。容器为顶层
 *         Popup 窗口（XWindowType_Popup），承载内容按分段构成三态：
 *         日历作为子件铺在其 1px 边框带内；时间设定行由容器自绘于
 *         日历下方（或独占弹层）；外部点击收层与 Esc 收层在容器层
 *         拦截。缺陷 G 终版口径（用户 2026-10-01 二次裁定）：弹层只
 *         承担日历点选与 ▲▼ 步进，不承载任何屏幕键盘界面——时间的
 *         键入途径=点击主窗口字段分段弹数字键盘（修复 A 链路）；弹
 *         层打开/交互全程不触发 XVirtualKeyboard_popup。 */
typedef struct XDateTimePopup
{
    XWidget m_base;      /**< 基类成员（嵌 XWidget）；必须是第一个。 */
    XDateTimeEdit* m_owner; /**< 属主日期时间控件（借用；可为 NULL）。 */
    bool m_hasCalendar;  /**< 弹层是否承载日历（分段构成含日期段）。 */
    bool m_hasTimeRow;   /**< 弹层是否承载时间设定行（含时间段）。 */
    int m_rowY;          /**< 时间行顶 y（弹层本地坐标；无行时 0）。 */
} XDateTimePopup;

static void VXDateTimePopup_paintEvent(XWidget* self, XEvent* event);
static void VXDateTimePopup_mousePressEvent(XWidget* self, XEvent* event);
static void VXDateTimePopup_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VXDateTimePopup_wheelEvent(XWidget* self, XEvent* event);
static void VXDateTimePopup_keyPressEvent(XWidget* self, XEvent* event);
/* 时间行绘制/命中（实现见「弹层时间设定行」节；paintEvent 先于此引用）。 */
static int xdtp_groupAt(const XDateTimePopup* popup, int x, int y, int* part);
static void xdtp_drawTimeRow(XDateTimePopup* popup, XPainter* painter,
                             const XFont* font);
/* 时间行真实编辑器（修复 G：三段值=XLineEdit 子件，DigitsOnly 数字键盘；
 * 懒创建/摆放/同步与提交槽实现见「弹层时间设定行」节）。 */
static void xdtp_timeRowSyncEditors(XDateTimePopup* popup);
static void xdt_timeRowCommitSlot(XObject* receiver, XVarList* args);
static void xdt_timeRowLiveSlot0(XObject* receiver, XVarList* args);
static void xdt_timeRowLiveSlot1(XObject* receiver, XVarList* args);
static void xdt_timeRowLiveSlot2(XObject* receiver, XVarList* args);
static bool VXDateTimeEdit_eventFilter(XObject* self, XObject* watched,
                                       XEvent* event);

static void XDateTimeEdit_paintEvent(XWidget* self, XEvent* event);
static void XDateTimeEdit_resizeEvent(XWidget* self, XEvent* event);
static void XDateTimeEdit_hideEvent(XWidget* self, XEvent* event);
static void XDateTimeEdit_focusInEvent(XWidget* self, XEvent* event);
static void XDateTimeEdit_timerEvent(XObject* object, XTimerEvent* event);
static void VXDateTimeEdit_clear(XAbstractSpinBox* self);
static void xdt_syncEditGeometry(XDateTimeEdit* self);

/** @brief 弹层容器绘制：Base 底 + 1px Mid 边框带（对标 QDateTimePopup
 *         的 QFrame::StyledPanel|Plain 细边框；日历子件铺在边框带内）。 */
static void VXDateTimePopup_paintEvent(XWidget* self, XEvent* event)
{
    XDateTimePopup* popup = (XDateTimePopup*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t base;
    uint32_t mid;
    int w;
    int h;
    if (!popup || !event ||
        XEvent_type(event) != XEVENT_TYPE_PAINT) {
        XClass_Parent(XWidget, EXWidget_PaintEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
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
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Base);
        base = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    base = 0xFFFFFFFFu; mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    XRect_init(&r, 0, 0, w, h);
    XPainter_fillRect(&painter, &r, base);
    /* 四边 1px 边框带（日历子件从 (1,1) 铺设，边框带不被覆盖）。 */
    XRect_init(&r, 0, 0, w, 1);
    XPainter_fillRect(&painter, &r, mid);
    XRect_init(&r, 0, h - 1, w, 1);
    XPainter_fillRect(&painter, &r, mid);
    XRect_init(&r, 0, 0, 1, h);
    XPainter_fillRect(&painter, &r, mid);
    XRect_init(&r, w - 1, 0, 1, h);
    XPainter_fillRect(&painter, &r, mid);
    /* 时间设定行承载态：组合态先画日历/行之间 1px 分隔线，再整排
     * 绘制「[段] ▲ 值 ▼」组（控件字体与日历文字同源）。 */
    if (popup->m_hasTimeRow) {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        if (popup->m_hasCalendar) {
            XRect sr;
            XRect_init(&sr, 0, popup->m_rowY - 1, w, 1);
            XPainter_fillRect(&painter, &sr, mid);
        }
        xdtp_drawTimeRow(popup, &painter, &font);
        XClassDeinit((XClass*)&font);
    }
    XPainter_deinit(&painter);
}

/** @brief 弹层内坐标判定（对标 XComboPopupView 越界判据：抓取直投
 *         容器的本地坐标与容器尺寸比较）。 */
static bool xdtp_contains(const XDateTimePopup* popup, int x, int y)
{
    if (!popup) return false;
    return x >= 0 && y >= 0 &&
           x < XWidget_width((const XWidget*)popup) &&
           y < XWidget_height((const XWidget*)popup);
}

/** @brief 把容器本地坐标事件转投日历子件（抓取直投容器、不再做子
 *         命中——XWidget 派发层抓取分支语义；转投前把事件位置换算
 *         到日历局部坐标，对标 QWidget::grabMouse 下的手工转发）。 */
static void xdtp_forwardToCalendar(XDateTimePopup* popup, XEvent* event)
{
    XDateTimeEdit* owner = popup ? popup->m_owner : NULL;
    XCalendarWidget* cal = owner ? owner->m_calendar : NULL;
    XMouseEvent* me = (XMouseEvent*)event;
    XRect cg;
    XPoint pos;
    XPoint local;
    if (!cal || !XWidget_isVisible((XWidget*)cal)) return;
    pos = XMouseEvent_position(me);
    cg = XWidget_geometry((XWidget*)cal);
    local.x = pos.x - cg.x;
    local.y = pos.y - cg.y;
    me->m_position = local;
    XObject_event_base((XObject*)cal, event);
}

/* ==================== 弹层时间设定行（分段构成含时间段时承载；
 *                     水平一排「[时] ▲ 18 ▼」组，绘制语言与
 *                     XCalendarWidget 导航条一致：按钮底凸起斜面
 *                     +实心三角箭头） ==================== */

/** @brief 组段标签表（下标 0=时/1=分/2=秒；与 xdtp_timeRowGroups
 *         的固定顺序对齐）。 */
static const char* const xdtp_groupLabels[3] = { "时", "分", "秒" };

/** @brief 按 displayFormat 分段掩码收集时间行组（只显示格式实际包含
 *         的段，固定顺序 时→分→秒；无秒不画秒组）。返回组个数。 */
static int xdtp_timeRowGroups(const XDateTimeEdit* owner, int* codes, int max)
{
    int mask = owner ? XDateTimeEdit_sections(owner) : 0;
    static const int order[3] = {
        (int)XDateTimeEditSection_HourSection,
        (int)XDateTimeEditSection_MinuteSection,
        (int)XDateTimeEditSection_SecondSection
    };
    int i;
    int n = 0;
    for (i = 0; i < 3 && n < max; ++i) {
        if (mask & order[i]) codes[n++] = order[i];
    }
    return n;
}

/** @brief 第 gi 组（共 nGroups 组均分行宽）的四个子矩形（弹层本地
 *         坐标；布局与命中/绘制共用本函数，同源不漂移）。 */
static void xdtp_groupRects(const XDateTimePopup* popup, int nGroups, int gi,
                            XRect* label, XRect* up, XRect* value,
                            XRect* down)
{
    int w = XWidget_width((XWidget*)popup);
    int usable = w - 8 > 0 ? w - 8 : 1;
    int slotX = 4 + usable * gi / nGroups;
    int slotW = usable * (gi + 1) / nGroups - usable * gi / nGroups;
    int x = slotX + (slotW - XDT_TG_CONTENT_W) / 2;
    int yBtn = popup->m_rowY + (XDT_TIMEROW_H - XDT_TG_BTN_H) / 2;
    int yVal = popup->m_rowY + (XDT_TIMEROW_H - XDT_TG_VALUE_H) / 2;
    XRect_init(label, x, yVal, XDT_TG_LABEL_W, XDT_TG_VALUE_H);
    x += XDT_TG_LABEL_W + XDT_TG_GAP;
    XRect_init(up, x, yBtn, XDT_TG_BTN_W, XDT_TG_BTN_H);
    x += XDT_TG_BTN_W + XDT_TG_GAP;
    XRect_init(value, x, yVal, XDT_TG_VALUE_W, XDT_TG_VALUE_H);
    x += XDT_TG_VALUE_W + XDT_TG_GAP;
    XRect_init(down, x, yBtn, XDT_TG_BTN_W, XDT_TG_BTN_H);
}

/** @brief 命中测试：弹层本地坐标→组序号；part 出参 0=▲ / 1=段值 /
 *         2=▼。未落在任一组返回 -1（part 置 -1）。 */
static int xdtp_groupAt(const XDateTimePopup* popup, int x, int y, int* part)
{
    int codes[3];
    int n;
    int i;
    *part = -1;
    if (!popup->m_hasTimeRow || !popup->m_rowY) return -1;
    if (y < popup->m_rowY || y >= popup->m_rowY + XDT_TIMEROW_H) return -1;
    n = xdtp_timeRowGroups(popup->m_owner, codes, 3);
    for (i = 0; i < n; ++i) {
        XRect label;
        XRect up;
        XRect value;
        XRect down;
        xdtp_groupRects(popup, n, i, &label, &up, &value, &down);
        if (XRect_contains(&up, x, y)) { *part = 0; return i; }
        if (XRect_contains(&value, x, y)) { *part = 1; return i; }
        if (XRect_contains(&down, x, y)) { *part = 2; return i; }
    }
    return -1;
}

/** @brief 组当前段值（时/分/秒 24 小时制原始值）。 */
static int xdtp_groupValue(const XDateTimeEdit* owner, int code)
{
    if (!owner) return 0;
    if (code == (int)XDateTimeEditSection_HourSection)
        return XTime_hour(&owner->m_dateTime.m_time);
    if (code == (int)XDateTimeEditSection_MinuteSection)
        return XTime_minute(&owner->m_dateTime.m_time);
    return XTime_second(&owner->m_dateTime.m_time);
}

/** @brief 时间行提交槽（Return/editingFinished → 三段文本解析写回值）。
 *  @details 每段独立解析：纯数字才生效，越界（时>23、分/秒>59）整段
 *           丢弃保留原值（对标 QTime 编辑语义）；空段保留当前值；全部
 *           合法才经 XDateTimeEdit_setTime 落值（范围钳制+信号由其承
 *           担）。毫秒位不经时间行（保留现值）。 */
static void xdt_timeRowCommitSlot(XObject* receiver, XVarList* args)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)receiver;
    XTime nt;
    int codes[3];
    int n;
    int i;
    int h = -1;
    int mi = -1;
    int s = -1;
    (void)args;
    if (!edit) return;
    n = xdtp_timeRowGroups(edit, codes, 3);
    nt = XDateTimeEdit_time(edit);
    for (i = 0; i < n; ++i) {
        XLineEdit* e = edit->m_timeEdits[i];
        const char* text = e ? XLineEdit_text(e) : NULL;
        int maxV = (codes[i] == (int)XDateTimeEditSection_HourSection)
                       ? 23 : 59;
        if (text && text[0]) {
            int len = (int)XStrlen(text);
            int acc = 0;
            bool ok = len > 0;
            int k;
            for (k = 0; k < len && ok; ++k) {
                if (text[k] < '0' || text[k] > '9') ok = false;
                else acc = acc * 10 + (text[k] - '0');
            }
            if (ok && acc <= maxV) {
                if (codes[i] == (int)XDateTimeEditSection_HourSection)
                    h = acc;
                else if (codes[i] ==
                         (int)XDateTimeEditSection_MinuteSection)
                    mi = acc;
                else s = acc;
            }
        }
    }
    if (h < 0 && mi < 0 && s < 0) return;
    /* 毫秒实参取当日毫秒总数的低 3 位（XTime.m_msecs=当日总毫秒，直传
     * 即越界、setHMS 校验拒绝静默失改——实测探针 S7d 组合弹层确认后
     * 值不落的根因；初始值=墙钟的字段必现，初始 00:00:00 的纯时间字
     * 段恰好合法故先前仅组合弹层暴露）。时间行不承载毫秒段，取低位
     * 保真即可。 */
    XTime_setHMS(&nt,
                 h >= 0 ? h : XTime_hour(&nt),
                 mi >= 0 ? mi : XTime_minute(&nt),
                 s >= 0 ? s : XTime_second(&nt),
                 nt.m_msecs % 1000);
    XDateTimeEdit_setTime(edit, &nt);
    /* 确认/回车提交后连带收起键盘（用户裁定：确认=应用并收起）。 */
    {
        XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
        if (kb) XVirtualKeyboard_closePopup(kb);
    }
}

/** @brief 键盘确认键→时间行提交 接线句柄（年编 xcal_kbReadyConn 同款：
 *         键盘为应用单例、同一时刻至多一会话有效，句柄不随控件实例携
 *         带；popupHide/deinit 成对断开防跨会话残留双发）。 */
static XConnection* xdt_kbReadyConn = NULL;

/** @brief 字段路径键盘确认键（ready）→冲账半截输入并收键盘（缺陷⑧）。
 *  @details 字段路径（点壳编辑区）同拍 notifyPress 以本壳为 target 弹出
 *           Digits 键盘（穿透上溯收敛到壳，XVirtualKeyboard.c:3102-3122），
 *           数字键经合成键直达壳 keyPressEvent，而确认键唯一载荷=ready
 *           信号（XVirtualKeyboard.c:1584-1597，无参不自动收层、不产任
 *           何合成键）——字段会话此前零消费者，按确认无任何效果（缺陷⑧
 *           根因）。本槽即该消费者：会话守卫=键盘 m_target 仍是本壳，字
 *           段会话终结后的残留连接在 QWERTY 等他会话按确认时就此失活，
 *           不得误收他键盘。冲账对标 Qt Key_Return 的确认提交语义
 *           （interpret+editingFinished，qabstractspinbox.cpp:1045-1053）：
 *           xdt_commitTyping(edit,false) 把未满位中间态按前缀补位落账
 *           （如月段打 1→月=1）且对空缓冲幂等（advance=false 保持当前
 *           段）；收层=「确认=应用并收起」（弹层路径
 *           xdt_timeRowKeyboardConfirmSlot 同裁定），popupVisible 守卫
 *           +closePopup 幂等（置 m_userCollapsed 闩锁，守护不重弹）。
 *           ready 发射帧内同步收层与弹层路径先例同型。 */
static void xdt_fieldKeyboardConfirmSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* kb;
    XDateTimeEdit* edit = (XDateTimeEdit*)receiver;
    (void)args;
    if (!edit) return;
    kb = XGuiApplication_virtualKeyboard();
    if (!kb) return;
    /* 会话错配丢弃：m_target 公开成员（XVirtualKeyboard.h），年编等
     * 他会话的确认不由本槽处理（年编自有 xcal_kbReadyConn 消费）。 */
    if (kb->m_target != (XWidget*)edit) return;
    xdt_commitTyping(edit, false); /* 冲账未满位缓冲（空缓冲仅复位，幂等）。 */
    if (XVirtualKeyboard_popupVisible(kb))
        XVirtualKeyboard_closePopup(kb);
}

/** @brief 字段路径会话接线（缺陷⑧）：先断旧后连新（xdt_timeRowPopup
 *           Keyboard 同纪律），复用同一单例句柄 xdt_kbReadyConn，与时间
 *           行接线互斥替换——ready 单消费者恒成立，一次确认至多一个槽
 *           运行。键盘缺席（总开关关/未创建）惰性跳过；连接存续到下一
 *           次 Arm/popupHide/deinit，期间无本壳会话时由确认槽 m_target
 *           守卫失活。 */
static void xdt_fieldKeyboardArm(XDateTimeEdit* edit)
{
    XVirtualKeyboard* kb;
    if (!edit) return;
    kb = XGuiApplication_virtualKeyboard();
    if (!kb) return;
    if (xdt_kbReadyConn) {
        XObject_disconnect_2(xdt_kbReadyConn);
        xdt_kbReadyConn = NULL;
    }
    xdt_kbReadyConn = XObject_connect_1(
        (XObject*)kb, (size_t)XVirtualKeyboard_ready_signal(NULL),
        (XObject*)edit, xdt_fieldKeyboardConfirmSlot,
        XConnectionType_Direct);
}

/** @brief 键盘确认键（ready）→提交三段并收键盘；提交早退（三段全非
 *  法时提交槽 return 不收层）时兜底收起，保证「确认=提交并收起」无条
 *  件成立。提交槽自身尾部已带 closePopup（幂等），此处为早退路径补收。 */
static void xdt_timeRowKeyboardConfirmSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* kb;
    xdt_timeRowCommitSlot(receiver, args);
    kb = XGuiApplication_virtualKeyboard();
    if (kb && XVirtualKeyboard_popupVisible(kb))
        XVirtualKeyboard_closePopup(kb);
}

/** @brief 时间行公共弹键盘 helper（三站点收敛：容器兜底按下/编辑器
 *  直点/编辑器获焦）：布锚主窗口顶层→先断旧后连新确认键接线（防前
 *  会话残留双发，XCalendarWidget.c 527-530 纪律）→数字布局显式定版
 *  先于 popup——setMode 只写读数+重建键表+标脏，无 show 态依赖；
 *  popup 内 flush 即按 Digits 出帧（不依赖上下文 hints 传播时序，
 *  首帧即终帧，年编同款）。编辑器缺席先自愈（见函数体内注）。 */
static void xdt_timeRowPopupKeyboard(XDateTimeEdit* edit, int gi)
{
    XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
    bool healed = false;
    if (!kb || gi < 0 || gi >= 3) return;
    /* 编辑器缺席自愈（F4 回归加固，2026-10-01）：键盘弹出不再依赖
     * 「popupShow 必已跑 xdtp_timeRowSyncEditors」的跨函数时序不变量
     * ——该同步被跳过/弹层经非常规路径重建时，行值框点击在
     * !m_timeEdits[gi] 处静默早退：只定段、键盘永不出场、绘制层裸露
     * 手绘值框（实测 F4 症状全套吻合：分框定段正常/秒框同码路、
     * ▲▼步进正常、值框无插入符焦痕、30+帧零键盘）。按需补跑
     * SyncEditors（幂等：已建仅重摆几何+回填段值+show，EnsureEditors
     * 对已建段 continue 不重复接线；回填 setText 经 textChanged→实时
     * 槽同值早退，无信号副作用）。 */
    if (!edit->m_timeEdits[gi] && edit->m_popup) {
        xdtp_timeRowSyncEditors(edit->m_popup);
        healed = edit->m_timeEdits[gi] != NULL;
    }
    if (!edit->m_timeEdits[gi]) return;
    if (healed) {
        /* 自愈补齐直点语义（对齐容器兜底路径 xdtp_timeRowPress 与事件
         * 过滤器的 setFocus+selectAll）：自愈前的那次定段点击没走到这
         * 两步，不补则编辑器持有回填的当前段值且无选区——合成数字键
         * 尾部拼接被 maxLength=2 截断，键入看似无效。 */
        XWidget_setFocus((XWidget*)edit->m_timeEdits[gi]);
        XLineEdit_selectAll(edit->m_timeEdits[gi]);
    }
    XVirtualKeyboard_setHostWindow(kb,
                                   XWidget_topLevelWidget((XWidget*)edit));
    if (xdt_kbReadyConn) {
        XObject_disconnect_2(xdt_kbReadyConn);
        xdt_kbReadyConn = NULL;
    }
    xdt_kbReadyConn = XObject_connect_1(
        (XObject*)kb, (size_t)XVirtualKeyboard_ready_signal(NULL),
        (XObject*)edit, xdt_timeRowKeyboardConfirmSlot,
        XConnectionType_Direct);
    /* 数字布局显式定版先于 popup：首帧按 Digits 渲染上屏（缺陷⑤）。 */
    XVirtualKeyboard_setMode(kb, XKeyboardMode_Digits);
    XVirtualKeyboard_popup(kb, (XWidget*)edit->m_timeEdits[gi]);
}

/** @brief 时间行编辑器 textChanged 联动主体（确定性路由版）：信号不携
 *  发送者，段序号 gi 由连接期绑定的蹦床传入——原实现经 appFocusWidget
 *  焦点探测定位段（悬浮键盘模态双抓取下焦点上报不可靠即整链静默，
 *  键入不落值的根因，故删除）。落值门槛原样保留：空文本/非数字/越界
 *  （时>23、分/秒>59）不落值；落值前补同值早退——setTime 恒经
 *  xdt_emitPartChanged 发 dateTimeChanged 并驱动弹层随值同步（开层同
 *  步/▲▼回写/syncPopupValue 回写的 setText→textChanged 回路），无早
 *  退则形成信号风暴/空转回路（早退在 setTime 之前，递归不可能）。
 *  累加器初值必须为 0：初值 -1 时首位数字即得负累积（如 "2"→-8）恒
 *  被 v<0 门槛拒收，实时落值整链静默（实测探针 S5/S7d 键入不落值根
 *  因之二，2026-10-01 修复）。 */
static void xdt_timeRowLiveOnIndex(XObject* receiver, XVarList* args, int gi)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)receiver;
    XLineEdit* editor;
    XTime nt;
    int codes[3];
    int n;
    int v = 0;
    int maxV = 59;
    const char* text;
    (void)args;
    if (!edit || gi < 0 || gi >= 3) return;
    editor = edit->m_timeEdits[gi];
    if (!editor) return;
    text = XLineEdit_text(editor);
    if (!text || !text[0]) return;
    n = xdtp_timeRowGroups(edit, codes, 3);
    if (gi >= n) return;
    if (codes[gi] == (int)XDateTimeEditSection_HourSection) maxV = 23;
    {
        int len = (int)XStrlen(text);
        int k;
        for (k = 0; k < len; ++k) {
            if (text[k] < '0' || text[k] > '9') return;
            v = v * 10 + (text[k] - '0');
        }
    }
    if (v < 0 || v > maxV) return;
    if (v == xdtp_groupValue(edit, codes[gi])) return; /* 同值早退（见函数头）。 */
    nt = XDateTimeEdit_time(edit);
    /* 毫秒实参取当日毫秒总数的低 3 位（XTime.m_msecs=当日总毫秒，直传
     * 即越界、setHMS 校验拒绝静默失改，实测探针 S7d 根因之一）；
     * 时间行不承载毫秒段，保留现值防回绕。 */
    if (codes[gi] == (int)XDateTimeEditSection_HourSection)
        XTime_setHMS(&nt, v, XTime_minute(&nt), XTime_second(&nt),
                     nt.m_msecs % 1000);
    else if (codes[gi] == (int)XDateTimeEditSection_MinuteSection)
        XTime_setHMS(&nt, XTime_hour(&nt), v, XTime_second(&nt),
                     nt.m_msecs % 1000);
    else
        XTime_setHMS(&nt, XTime_hour(&nt), XTime_minute(&nt), v,
                     nt.m_msecs % 1000);
    XDateTimeEdit_setTime(edit, &nt);
}

/** @brief textChanged 蹦床（时/分/秒 三段各连一支：连接期绑定段序号，
 *  取代焦点探测；信号 id 两种写法等价，沿用既有 accessor 现形）。 */
static void xdt_timeRowLiveSlot0(XObject* receiver, XVarList* args)
{
    xdt_timeRowLiveOnIndex(receiver, args, 0);
}

static void xdt_timeRowLiveSlot1(XObject* receiver, XVarList* args)
{
    xdt_timeRowLiveOnIndex(receiver, args, 1);
}

static void xdt_timeRowLiveSlot2(XObject* receiver, XVarList* args)
{
    xdt_timeRowLiveOnIndex(receiver, args, 2);
}

/** @brief 懒创建时间行三段编辑器（弹层容器子件；DigitsOnly 数字键盘、
 *         maxLength=段宽两位；Return/editingFinished 连提交槽）。 */
static void xdtp_timeRowEnsureEditors(XDateTimePopup* popup)
{
    XDateTimeEdit* owner = popup->m_owner;
    int i;
    if (!owner) return;
    for (i = 0; i < 3; ++i) {
        if (owner->m_timeEdits[i]) continue;
        owner->m_timeEdits[i] = XLineEdit_create((XWidget*)popup, 0);
        if (!owner->m_timeEdits[i]) continue;
        /* 数字键盘：DigitsOnly 硬提示 → 12 键数字布局（悬浮顶层形态，
         * 宿主由 popupShow 锚定主窗口顶层）。 */
        XWidget_setInputMethodHints((XWidget*)owner->m_timeEdits[i],
                                    XInputMethodHint_DigitsOnly);
        XLineEdit_setMaxLength(owner->m_timeEdits[i], 2);
        XObject_connect_1((XObject*)owner->m_timeEdits[i],
            (size_t)XLineEdit_returnPressed_signal,
            (XObject*)owner, xdt_timeRowCommitSlot,
            XConnectionType_Direct);
        XObject_connect_1((XObject*)owner->m_timeEdits[i],
            (size_t)XLineEdit_editingFinished_signal,
            (XObject*)owner, xdt_timeRowCommitSlot,
            XConnectionType_Direct);
        /* 单段键入实时落值（输一位改一段——用户裁定实时反馈）；
         * textChanged 不携发送者，按创建序 i 连对应蹦床绑定段序号
         * （确定性路由，见 xdt_timeRowLiveOnIndex 函数头）。 */
        XObject_connect_1((XObject*)owner->m_timeEdits[i],
            (size_t)XLineEdit_textChanged_signal,
            (XObject*)owner,
            (i == 0) ? xdt_timeRowLiveSlot0 :
            ((i == 1) ? xdt_timeRowLiveSlot1 : xdt_timeRowLiveSlot2),
            XConnectionType_Direct);
        /* 获焦即弹数字键盘（显式 popup；见 VXDateTimeEdit_eventFilter）。 */
        XObject_installEventFilter((XObject*)owner->m_timeEdits[i],
                                   (XObject*)owner);
    }
}

/** @brief 摆放并同步时间行三段编辑器（几何=组值框矩形，文本=当前段
 *         值；popupShow 与步进后调用，同源 xdtp_groupRects 不漂移）。 */
static void xdtp_timeRowSyncEditors(XDateTimePopup* popup)
{
    XDateTimeEdit* owner = popup->m_owner;
    int codes[3];
    int n;
    int i;
    if (!popup->m_hasTimeRow || !owner) return;
    xdtp_timeRowEnsureEditors(popup);
    n = xdtp_timeRowGroups(owner, codes, 3);
    for (i = 0; i < n; ++i) {
        XLineEdit* e = owner->m_timeEdits[i];
        XRect label;
        XRect up;
        XRect value;
        XRect down;
        char buf[8];
        if (!e) continue;
        xdtp_groupRects(popup, n, i, &label, &up, &value, &down);
        XWidget_setGeometry((XWidget*)e, value.x, value.y,
                            value.width, value.height);
        XSnprintf(buf, sizeof(buf), "%02d", xdtp_groupValue(owner, codes[i]));
        XLineEdit_setText(e, buf);
        XWidget_show((XWidget*)e);
        XWidget_raise((XWidget*)e);
    }
}

/** @brief 绘制按钮底凸起斜面（左/上 Light、右/下 Dark；XCalendarWidget
 *         xcal_drawNavBevel 同款手法）。 */
static void xdtp_drawBevel(XPainter* painter, const XRect* r,
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

/** @brief 绘制实心三角箭头（up=true 上三角/step+，false 下三角/
 *         step-；行填充近似，xcal_drawNavArrow 的上下变体）。 */
static void xdtp_drawTri(XPainter* painter, const XRect* r, bool up,
                         uint32_t color)
{
    int cx = r->x + r->width / 2;
    int cy = r->y + r->height / 2;
    int hw = 5; /* 半宽 5 → 全宽 10px。 */
    int hh = 4; /* 半高 4 → 全高 8px。 */
    int k;
    for (k = 0; k < hh * 2; ++k) {
        int halfW = (k + 1) * hw / (hh * 2);
        int y = up ? (cy - hh + k) : (cy + hh - k - 1);
        XRect row;
        if (halfW <= 0) continue;
        XRect_init(&row, cx - halfW, y, halfW * 2, 1);
        XPainter_fillRect(painter, &row, color);
    }
}

/* ==================== 弹层时间设定行（定版口径=真实编辑器方案，与
 *                     XDateTimeEdit.h m_timeEdits 字段注一致）：三段值
 *                     框为弹层容器子件 XLineEdit（懒创建、DigitsOnly
 *                     硬提示、maxLength=段宽两位），点值框=点编辑器本
 *                     尊——setFocus+selectAll 后显式弹悬浮数字键盘
 *                     （Digits 布局定版），键入经编辑器 textChanged 实
 *                     时落值、Return/editingFinished 提交（含键盘确认
 *                     键 ready 接线）；▲▼ 仍走 stepBy 步进（容器兜底
 *                     路径保点击可达）。日历点选/翻页与年份就地编辑的
 *                     弹层内嵌键盘链路不受本节影响。 ==== */
/** @brief 绘制整排时间设定行（paintEvent 专用；段值实时取自属主当前
 *         值，属主当前编辑段对应的段值框以 Highlight 描边提示）。 */
static void xdtp_drawTimeRow(XDateTimePopup* popup, XPainter* painter,
                             const XFont* font)
{
    XWidget* self = (XWidget*)popup;
    XDateTimeEdit* owner = popup->m_owner;
    int codes[3];
    int n;
    int i;
    uint32_t base;
    uint32_t mid;
    uint32_t light;
    uint32_t dark;
    uint32_t windowText;
    uint32_t highlight;
#if XPALETTE_ON
    XPalette palette = XWidget_palette(self);
    XColor c;
    c = XPalette_color(&palette, XPaletteColorGroup_Current,
                       XPaletteColorRole_Base);
    base = XColor_rgba(&c);
    c = XPalette_color(&palette, XPaletteColorGroup_Current,
                       XPaletteColorRole_Mid);
    mid = XColor_rgba(&c);
    c = XPalette_color(&palette, XPaletteColorGroup_Current,
                       XPaletteColorRole_Light);
    light = XColor_rgba(&c);
    c = XPalette_color(&palette, XPaletteColorGroup_Current,
                       XPaletteColorRole_Dark);
    dark = XColor_rgba(&c);
    c = XPalette_color(&palette, XPaletteColorGroup_Current,
                       XPaletteColorRole_WindowText);
    windowText = XColor_rgba(&c);
    c = XPalette_color(&palette, XPaletteColorGroup_Current,
                       XPaletteColorRole_Highlight);
    highlight = XColor_rgba(&c);
#else
    base = 0xFFFFFFFFu; mid = 0xFF808080u; light = 0xFFE0E0E0u;
    dark = 0xFF606060u; windowText = 0xFF000000u;
    highlight = 0xFF3080C0u;
#endif /* XPALETTE_ON */
    n = xdtp_timeRowGroups(owner, codes, 3);
    for (i = 0; i < n; ++i) {
        XRect label;
        XRect up;
        XRect value;
        XRect down;
        char buf[8];
        int v = xdtp_groupValue(owner, codes[i]);
        int tw;
        xdtp_groupRects(popup, n, i, &label, &up, &value, &down);
        /* 段标签（时/分/秒）。 */
        XPainter_drawText(painter, label.x + 1, label.y + 13,
                          xdtp_groupLabels[i], windowText);
        /* 上箭头按钮：按钮底+实心上三角。 */
        xdtp_drawBevel(painter, &up, base, light, dark);
        xdtp_drawTri(painter, &up, true, windowText);
        /* 段值框：Base 底+1px 边框（当前编辑段用 Highlight 提示），
         * 两位数字居中。定版=真实编辑器方案（本文件「弹层时间设定行」
         * 节）：编辑器就位时被其精确覆盖（同源 xdtp_groupRects 几何）
         * 不可见，本手绘值框仅为其底层兜底；编辑器缺席/未生效时裸露
         * 呈现（旧「无行编辑器子件、无屏幕键盘界面」口径已废，两套
         * 表述并存曾误导演练排障——F4 回归根因排查耗时点）。 */
        XPainter_fillRect(painter, &value, base);
        {
            XRect e;
            uint32_t border =
                (owner && owner->m_currentSection == codes[i])
                    ? highlight : mid;
            XRect_init(&e, value.x, value.y, value.width, 1);
            XPainter_fillRect(painter, &e, border);
            XRect_init(&e, value.x, value.y + value.height - 1,
                       value.width, 1);
            XPainter_fillRect(painter, &e, border);
            XRect_init(&e, value.x, value.y, 1, value.height);
            XPainter_fillRect(painter, &e, border);
            XRect_init(&e, value.x + value.width - 1, value.y, 1,
                       value.height);
            XPainter_fillRect(painter, &e, border);
        }
        XSnprintf(buf, sizeof(buf), "%02d", v);
        tw = XPainter_textWidth(font, buf);
        XPainter_drawText(painter,
                          value.x + (value.width - (tw > 0 ? tw : 0)) / 2,
                          value.y + 13, buf, windowText);
        /* 下箭头按钮：按钮底+实心下三角。 */
        xdtp_drawBevel(painter, &down, base, light, dark);
        xdtp_drawTri(painter, &down, false, windowText);
    }
}

/** @brief 时间行按下分派：▲/▼ 复用属主既有分段步进入口（先聚焦该段
 *         再 stepBy(±1)，跨段进位由 XDateTimeEdit_stepBy 的 epoch 换算
 *         天然承担，步进后经值变化汇聚点 xdt_syncPopupValue 随动刷新
 *         日历选中页与时间行）；点段值=聚焦该段真实编辑器并全选+显式
 *         弹悬浮数字键盘（定版=真实编辑器方案；值框已被编辑器覆盖，
 *         点击正常直达编辑器本尊——此处为容器兜底路径：编辑器缺席/
 *         隐藏时按组定段，语义与编辑器直点路径一致）。时间调整一律不
 *         关弹层（可连调）。 */
static void xdtp_timeRowPress(XDateTimePopup* popup, const XMouseEvent* me)
{
    XDateTimeEdit* owner = popup->m_owner;
    XPoint pos;
    int part;
    int gi;
    int codes[3];
    int n;
    if (!owner) return;
    pos = XMouseEvent_position(me);
    gi = xdtp_groupAt(popup, pos.x, pos.y, &part);
    if (gi < 0) return;
    n = xdtp_timeRowGroups(owner, codes, 3);
    if (gi >= n) return;
    if (part == 1) {
        /* 值框已被真实编辑器覆盖（点击直达编辑器，正常不达此处）；
         * 兜底聚焦编辑器并全选（编辑器缺席/隐藏时按组定段）。 */
        xdt_focusSectionCode(owner, codes[gi]);
        if (owner->m_timeEdits[gi] &&
            XWidget_isVisible((XWidget*)owner->m_timeEdits[gi])) {
            XWidget_setFocus((XWidget*)owner->m_timeEdits[gi]);
            XLineEdit_selectAll(owner->m_timeEdits[gi]);
        }
#if XVIRTUALKEYBOARD_ON
        /* 显式弹数字键盘（三站点公共 helper：布锚+确认键接线+popup+
         * 数字布局定版，见 xdt_timeRowPopupKeyboard 函数头）。 */
        xdt_timeRowPopupKeyboard(owner, gi);
#endif /* XVIRTUALKEYBOARD_ON */
        XWidget_update((XWidget*)owner);
        XWidget_update((XWidget*)popup);
        return;
    }
    xdt_focusSectionCode(owner, codes[gi]);
    XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)owner,
                                 (part == 0) ? 1 : -1);
    /* 步进后同步该组编辑器文本（▲▼ 点在容器层、编辑器未持焦——文本
     * 随新值刷新；持焦编辑器的键入文本不经此处，无覆盖风险）。 */
    if (owner->m_timeEdits[gi] &&
        XWidget_isVisible((XWidget*)owner->m_timeEdits[gi])) {
        char buf[8];
        XSnprintf(buf, sizeof(buf), "%02d",
                  xdtp_groupValue(owner, codes[gi]));
        XLineEdit_setText(owner->m_timeEdits[gi], buf);
    }
    XWidget_update((XWidget*)owner);
    XWidget_update((XWidget*)popup);
}

/** @brief 时间行编辑器事件过滤：直点（press，吞事件+全选+弹键盘）与
 *  获焦（FocusIn，总开关门内全选+弹键盘）双路显式弹出数字键盘（悬浮
 *  顶层形态，宿主=主窗口顶层；对标年份编辑器
 *  xcal_yearEditPopupKeyboard 的显式 popup——弹层窗口的焦点上报在
 *  模态抓取下不驱动 VK 自动链，必须显式弹）。Esc 交回容器收层。 */
static bool VXDateTimeEdit_eventFilter(XObject* self, XObject* watched,
                                       XEvent* event)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    int gi;
    if (!edit || !watched || !event) return false;
    for (gi = 0; gi < 3; ++gi)
        if (watched == (XObject*)edit->m_timeEdits[gi]) break;
    if (gi >= 3) return false;
    /* 按下时间行编辑器：setFocus+selectAll 后显式弹悬浮数字键盘，并吞
     * 掉事件（return true）——过滤器先于目标事件槽运行，放行会被编辑
     * 器本尊 mousePressEvent 的 moveCursor 按点收缩选区（与容器兜底路
     * 径 xdtp_timeRowPress 的 setFocus+selectAll 语义对齐：直点即整段
     * 全选重输）。键盘经三站点公共 helper（布锚+确认键接线+popup+数
     * 字布局定版；不依赖焦点自动链路——弹层窗口的焦点上报在模态抓取
     * 下不可靠，按下即弹确定性最高）。 */
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        XLineEdit* e = edit->m_timeEdits[gi];
        if (e) {
            XWidget_setFocus((XWidget*)e);
            XLineEdit_selectAll(e);
        }
        xdt_timeRowPopupKeyboard(edit, gi);
        return true; /* 吞掉：直点路径不再进入编辑器本尊按压处理。 */
    }
    /* 获焦（程序性/Tab 路径）：总开关门内全选+弹数字键盘（不 setFocus
     * ——获焦即本函数触发的前提；不吞事件，焦点事件语义保持直通）。 */
    if (XEvent_type(event) == XEVENT_TYPE_FOCUS_IN) {
        if (XVirtualKeyboardSettings_keyboardEnabled(
                XVirtualKeyboardSettings_instance())) {
            if (edit->m_timeEdits[gi])
                XLineEdit_selectAll(edit->m_timeEdits[gi]);
            xdt_timeRowPopupKeyboard(edit, gi);
        }
    }
    return false;
}

/** @brief 按下：越界（弹窗外部）点击收层并吞掉；时间行区域先于日历
 *         命中（▲/▼ 步进对应段、点段值切编辑焦点，均不关层）；其余
 *         界内转投日历子件（日期选择/翻页导航，点选日期收层），无日
 *         历承载（纯时间弹层）或日历未接住时由容器吸收（边框带/分隔
 *         线/行呼吸边距）。 */
static void VXDateTimePopup_mousePressEvent(XWidget* self, XEvent* event)
{
    XDateTimePopup* popup = (XDateTimePopup*)self;
    XMouseEvent* me;
    XPoint pos;
    int part;
    if (!popup || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (!xdtp_contains(popup, pos.x, pos.y)) {
        /* 模态抓取期间转发到本窗口的弹窗外按下：一律收层并吞掉。 */
        if (popup->m_owner) {
            XPrintf("[XDateTimeEdit] popupHide<-outside\n");
            xdt_popupHide(popup->m_owner);
        }
        XEvent_accept(event);
        return;
    }
    if (popup->m_hasTimeRow && pos.y >= popup->m_rowY) {
        /* 时间行整行归容器：命中组则分派步进/聚焦，行内其余（呼吸
         * 边距）吸收。 */
        if (xdtp_groupAt(popup, pos.x, pos.y, &part) >= 0)
            xdtp_timeRowPress(popup, me);
        XEvent_accept(event);
        return;
    }
    if (popup->m_hasCalendar) xdtp_forwardToCalendar(popup, event);
    XEvent_accept(event);
}

/** @brief 释放：越界吞掉（防止后续合成误判）；时间行区域吸收（步进
 *         已在按下分派）；其余界内转投日历子件（与按下同口径，保持
 *         按下/释放成对到达）。 */
static void VXDateTimePopup_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XDateTimePopup* popup = (XDateTimePopup*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!popup || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (!xdtp_contains(popup, pos.x, pos.y)) {
        XEvent_accept(event);
        return;
    }
    if (popup->m_hasTimeRow && pos.y >= popup->m_rowY) {
        /* 时间行整行吸收（定版=真实编辑器方案：步进/定段/聚焦已在按
         * 下分派完成，持焦编辑器的键入文本不经释放路径，无事可做）。 */
        XEvent_accept(event);
        return;
    }
    if (popup->m_hasCalendar) xdtp_forwardToCalendar(popup, event);
    XEvent_accept(event);
}

/** @brief 滚轮：时间行组上滚动=步进该组段（先聚焦该段再逐格
 *         stepBy(±1)，正角度增/负角度减，对标 QAbstractSpinBox 滚轮
 *         步进方向）；日历区域转发日历（翻页）；无日历承载时吸收。 */
static void VXDateTimePopup_wheelEvent(XWidget* self, XEvent* event)
{
    XDateTimePopup* popup = (XDateTimePopup*)self;
    XWheelEvent* we;
    XPoint pos;
    int part;
    int gi;
    if (!popup || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) {
        XClass_Parent(XWidget, EXWidget_WheelEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    we = (XWheelEvent*)event;
    pos = XWheelEvent_position(we);
    if (popup->m_hasTimeRow &&
        (gi = xdtp_groupAt(popup, pos.x, pos.y, &part)) >= 0) {
        XDateTimeEdit* owner = popup->m_owner;
        int codes[3];
        int n;
        XPoint delta;
        int steps;
        if (owner &&
            (n = xdtp_timeRowGroups(owner, codes, 3)) > 0 && gi < n) {
            xdt_focusSectionCode(owner, codes[gi]);
            delta = XWheelEvent_angleDelta(we);
            steps = delta.y / 120;
            if (steps == 0)
                steps = (delta.y > 0) ? 1 : ((delta.y < 0) ? -1 : 0);
            while (steps > 0) {
                XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)owner, 1);
                --steps;
            }
            while (steps < 0) {
                XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)owner, -1);
                ++steps;
            }
            XWidget_update((XWidget*)owner);
            XWidget_update((XWidget*)popup);
        }
        XEvent_accept(event);
        return;
    }
    if (popup->m_hasCalendar) {
        /* 日历区域滚轮转发（换算日历局部坐标后直投，翻页不改选中）。 */
        XDateTimeEdit* owner = popup->m_owner;
        XCalendarWidget* cal = owner ? owner->m_calendar : NULL;
        if (cal && XWidget_isVisible((XWidget*)cal)) {
            XRect cg = XWidget_geometry((XWidget*)cal);
            we->m_position.x = pos.x - cg.x;
            we->m_position.y = pos.y - cg.y;
            XObject_event_base((XObject*)cal, event);
        }
    }
    XEvent_accept(event);
}

/** @brief 按键：Esc 收层（对标 Qt 日历弹层 Esc 关闭并焦点回交）；
 *         其余按键直投日历子件（对标 Qt QCalendarPopup 内日历持有
 *         焦点接收键盘：方向键移选中、PageUp/Down 翻月、Return 发
 *         activated、年份就地编辑键入），日历未消费亦由本层吸收。 */
static void VXDateTimePopup_keyPressEvent(XWidget* self, XEvent* event)
{
    XDateTimePopup* popup = (XDateTimePopup*)self;
    XKeyEvent* ke;
    if (!popup || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) {
        XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    ke = (XKeyEvent*)event;
    if (XKeyEvent_key(ke) == XKey_Escape && popup->m_owner) {
        XPrintf("[XDateTimeEdit] popupHide<-esc\n");
        xdt_popupHide(popup->m_owner);
        XEvent_accept(event);
        return;
    }
    /* 非收层键转投日历（抓取直投容器、手工转发，同鼠标路径口径）；
     * 纯时间弹层无日历承载，按键由本层吸收（Esc 已在上方收层）。 */
    if (popup->m_hasCalendar) {
        XDateTimeEdit* owner = popup->m_owner;
        XCalendarWidget* cal = owner ? owner->m_calendar : NULL;
        if (cal && XWidget_isVisible((XWidget*)cal))
            XObject_event_base((XObject*)cal, event);
    }
    XEvent_accept(event);
}

static XVtable* XDateTimePopup_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDateTimePopup)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXDateTimePopup_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXDateTimePopup_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXDateTimePopup_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent,
                             VXDateTimePopup_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXDateTimePopup_keyPressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建弹层容器（NULL 父控件；窗口类型在显形前置 Popup）。 */
static XDateTimePopup* xdtPopup_create(XDateTimeEdit* owner)
{
    XDateTimePopup* popup = (XDateTimePopup*)XMemory_malloc(
        sizeof(XDateTimePopup), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!popup) return NULL;
    XMemset(popup, 0, sizeof(*popup));
    XWidget_init(&popup->m_base, NULL, 0);
    XClassSetVtable(popup, XDateTimePopup);
    popup->m_owner = owner;
    Set_Class_Memory(popup, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(popup, true);
    return popup;
}

/** @brief 确保日历与弹层容器就位并挂接父子关系（日历经既有懒创建
 *         语义产出后收编为弹层子件；已收编则幂等）。 */
static void xdt_ensureCalendarPopup(XDateTimeEdit* self)
{
    XCalendarWidget* cal;
    if (!self) return;
    cal = XDateTimeEdit_calendarWidget(self);
    if (!cal) return;
    if (!self->m_popup) {
        self->m_popup = xdtPopup_create(self);
        if (!self->m_popup) return;
    }
    if (XWidget_parentWidget((XWidget*)cal) != (XWidget*)self->m_popup) {
        /* 日历收编进弹层容器（容器析构级联；本控件 deinit 先显式删
         * 日历再删容器，次序安全）。日历此前从未显式 show（懒创建
         * 语义），换父后须显式 show 置 explicitShow 才会随弹层显形
         * （propagateVisibility 只带出未显式隐藏的子控件）。 */
        XWidget_setParent((XWidget*)cal, (XWidget*)self->m_popup, 0);
        XWidget_setGeometry((XWidget*)cal, 1, 1,
                            XWidget_width((XWidget*)cal),
                            XWidget_height((XWidget*)cal));
        XWidget_show((XWidget*)cal);
    }
}

/** @brief 日历点击槽：选中日期已由 selectionChanged 槽写回编辑框，
 *         此处只负责收层（对标 Qt 点日期即关闭弹层）。 */
static void xdt_calendarClickedSlot(XObject* receiver, XVarList* args)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)receiver;
    (void)args;
    if (edit && edit->m_popupVisible) {
        XPrintf("[XDateTimeEdit] popupHide<-calClick\n");
        xdt_popupHide(edit);
    }
}

/** @brief 按当前 displayFormat 的分段构成判定弹层内容三态（一处机制
 *         三态生效）：有日期段无时间段→纯日历；有时间段无日期段→纯
 *         时间设定行；两者都有→日历在上+时间行在下。 */
static XdtPopupMode xdt_popupMode(const XDateTimeEdit* self)
{
    int mask = XDateTimeEdit_sections(self);
    bool hasDate = (mask & XDT_DATE_SECTION_MASK) != 0;
    bool hasTime = (mask & XDT_TIME_SECTION_MASK) != 0;
    if (hasDate && hasTime) return XdtPopupMode_DateTime;
    if (hasTime) return XdtPopupMode_TimeOnly;
    return XdtPopupMode_DateOnly;
}

/** @brief 弹层几何：宽高按分段构成三态定尺寸——纯日历=日历+2px 边框
 *         带（现状）；组合=日历高+1px 分隔+时间行高；纯时间=时间行
 *         高（宽与日历对齐）。默认控件左下缘起（对标
 *         QDateTimeEditPrivate::positionCalendarPopup 的 LTR 锚点：
 *         弹层左缘贴控件左缘）、横向钳入主屏（对标 QComboBox 弹层的
 *         屏幕边界处理）、超下缘翻转到控件上方。 */
static void xdt_popupReposition(XDateTimeEdit* self)
{
    XWidget* popup;
    XDateTimePopup* impl;
    XCalendarWidget* cal;
    XPoint origin;
    XPoint g;
    XRect r;
    XRect sg;
    bool haveScreen = false;
    int selfH;
    int pw;
    int ph;
    int rowY;
    int x;
    int y;
    if (!self || !self->m_popup) return;
    popup = (XWidget*)self->m_popup;
    impl = self->m_popup;
    cal = self->m_calendar;
    if (impl->m_hasCalendar && !cal) return;
    XWidget_setWindowFlags(popup, (XWidgetFlags)XWindowType_Popup);
    selfH = XWidget_height((XWidget*)self);
    pw = (impl->m_hasCalendar ? XWidget_width((XWidget*)cal)
                              : XDT_TIMEROW_W) + 2;
    rowY = 1 + (impl->m_hasCalendar
                    ? XWidget_height((XWidget*)cal) + 1 : 0);
    ph = 1 + (impl->m_hasCalendar ? XWidget_height((XWidget*)cal) : 0)
       + ((impl->m_hasCalendar && impl->m_hasTimeRow) ? 1 : 0)
       + (impl->m_hasTimeRow ? XDT_TIMEROW_H : 0) + 1;
    impl->m_rowY = impl->m_hasTimeRow ? rowY : 0;
    origin.x = 0;
    origin.y = selfH;
    g = XWidget_mapToGlobal((XWidget*)self, &origin);
    XMemset(&sg, 0, sizeof(sg));
    {
        XScreen* screen = XGuiApplication_primaryScreen();
        if (screen) {
            sg = XScreen_geometry(screen);
            haveScreen = sg.width > 0 && sg.height > 0;
        }
    }
    x = g.x; /* 对标 Qt LTR 锚点：弹层左缘贴控件左缘（箭头贴右时由
                横向钳位保证不超屏）。 */
    y = g.y;
    if (haveScreen) {
        if (x < sg.x) x = sg.x;
        if (x + pw > sg.x + sg.width) x = sg.x + sg.width - pw;
    }
    if (haveScreen && y + ph > sg.y + sg.height) {
        /* 贴下缘超屏：翻转到控件上方（仍超上缘则钳回屏顶）。 */
        y = g.y - selfH - ph;
        if (y < sg.y) y = sg.y;
    }
    XRect_init(&r, x, y, pw, ph);
    XWidget_setGeometryRect(popup, &r);
    if (self->m_popupVisible)
        XWidget_flushBackingStore(popup, NULL);
}

/** @brief 把本控件日期上下界同步进日历（对标 QDateTimeEditPrivate::
 *         syncCalendarWidget 的 monthCalendar->setDateRange(q->minimumDate,
 *         q->maximumDate)：日历可点选范围跟随编辑框范围）。 */
static void xdt_syncCalendarRange(XDateTimeEdit* self)
{
    if (!self || !self->m_calendar) return;
    XCalendarWidget_setMinimumDate(self->m_calendar,
                                   &self->m_minimum.m_date);
    XCalendarWidget_setMaximumDate(self->m_calendar,
                                   &self->m_maximum.m_date);
}

/** @brief 弹出弹层（分段构成定内容+选中同步+几何+show/raise/首帧/
 *         模态双抓取全流程；对标 QDateTimeEdit 弹出日历：弹层以编辑框
 *         当前日期初始化选中态与显示页。内容三态：仅日期段=纯日历、
 *         仅时间段=纯时间设定行、两者都有=日历在上+时间行在下）。 */
static void xdt_popupShow(XDateTimeEdit* self)
{
    XCalendarWidget* cal;
    XdtPopupMode mode;
    if (!self || self->m_popupVisible) return;
    /* 弹层承载内容按当前 displayFormat 分段构成自动判定（一处机制）。 */
    mode = xdt_popupMode(self);
    if (!self->m_popup) {
        self->m_popup = xdtPopup_create(self);
        if (!self->m_popup) return;
    }
    self->m_popup->m_hasCalendar = (mode != XdtPopupMode_TimeOnly);
    self->m_popup->m_hasTimeRow = (mode != XdtPopupMode_DateOnly);
    cal = NULL;
    if (self->m_popup->m_hasCalendar) {
        xdt_ensureCalendarPopup(self);
        cal = self->m_calendar;
        if (!cal) return;
        /* 防残留（r2 项4）：重开弹层先终结上轮未收尾的年编会话（幂
         * 等，无会话零操作）——否则旧缓冲编辑器随弹层显形覆在标题年
         * 份上，开层同步翻页被盖读成「2000年1月」。 */
        XCalendarWidget_endYearEdit(cal);
    } else if (self->m_calendar &&
               XWidget_isVisible((XWidget*)self->m_calendar)) {
        /* 纯时间弹层不承载日历：历史遗留的日历子件（先日期/组合格式
         * 打开过弹层、后改纯时间格式）隐藏，避免盖住时间行。 */
        XWidget_hide((XWidget*)self->m_calendar);
    }
    if (cal) {
        /* 日期范围先同步（对标 syncCalendarWidget：initCalendarPopup
         * 与 setRange 两路都会推送 min/max 到日历）。 */
        xdt_syncCalendarRange(self);
        /* 以当前值同步日历选中态并翻到所在页：仅差异时推送（配合日历
         * setSelectedDate 同值早退与联动槽幂等，开弹层零信号副作用——
         * 对标 Qt 开弹层不触发 value 信号）；showSelectedDate 只对齐
         * 显示页不携带选中信号。 */
        {
            XDate sel = XCalendarWidget_selectedDate(cal);
            if (XDate_compare(&sel, &self->m_dateTime.m_date) != 0)
                XCalendarWidget_setSelectedDate(cal,
                                                &self->m_dateTime.m_date);
            XCalendarWidget_showSelectedDate(cal);
        }
    }
    xdt_popupReposition(self);
#if XVIRTUALKEYBOARD_ON
    /* 屏幕键盘宿主锚定主窗口（悬浮全宽底栏、弹层之上）：时间行真实
     * 编辑器获焦时数字键盘弹出即为此形态；closePopup 自动清锚。 */
    {
        XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
        if (kb)
            XVirtualKeyboard_setHostWindow(
                kb, XWidget_topLevelWidget((XWidget*)self));
    }
    /* 时间行真实编辑器：按段位摆放、同步段值并显示（点击值框=点击
     * 编辑器本尊，获焦即弹数字键盘；键入经编辑器文本，失焦/回车提交
     * 见 xdt_timeRowCommitSlot）。 */
    xdtp_timeRowSyncEditors(self->m_popup);
#endif /* XVIRTUALKEYBOARD_ON */
    self->m_popupVisible = true;
    XWidget_show((XWidget*)self->m_popup);
    /* 对标 Qt QWidgetPrivate::show_helper 的 Popup 分支（qwidget.cpp:
       8038-8043「new popups and tools need to be raised」）：弹层每次
       show 都必须置顶。平台唯一置顶通道是 requestActivate→
       XRaiseWindow；未映射窗口的激活在平台层挂起（MapNotify 后补做）。
       先例：XComboBox xcombo_popupShow/XMenu XMenu_popup。 */
    XWidget_activateWindow((XWidget*)self->m_popup);
    XWidget_raise((XWidget*)self->m_popup);
    /* 独立顶层窗口无宿主帧泵：主动完成首帧绘制上屏（含日历子件）。 */
    XWidget_flushBackingStore((XWidget*)self->m_popup, NULL);
    /* 模态鼠标+键盘抓取（对标 Qt grabForPopup 成对抓取）：公共层立即
       设置直投目标使弹窗外点击也路由到弹层（越界收层判定依赖此）；
       平台抓取需窗口完成映射，延迟到 1ms 精确定时器执行。 */
    XWidget_grabMouse((XWidget*)self->m_popup);
    XWidget_grabKeyboard((XWidget*)self->m_popup);
    if (self->m_grabTimer == XTIMER_INVALID_ID) {
        self->m_grabTimer = XObject_startTimer_ms(
            (XObject*)self, 1u, XTimerType_PreciseTimer);
    }
#if XVIRTUALKEYBOARD_ON
    /* 缺陷 A（箭头误触发屏幕键盘）第二道闭环：同拍 notifyPress 命中
     * WA14 壳已先弹屏幕键盘（按下汇聚点先于本函数的任何调用路径），
     * 弹层开启即确定性关掉——任何开层路径（箭头分支/后续扩展）都不
     * 允许「弹层+键盘」叠加显形。与箭头分支的关层互为冗余（双保险），
     * closePopup 幂等，键盘未弹时空操作。 */
    {
        XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
        if (kb) XVirtualKeyboard_closePopup(kb);
    }
#endif
    XWidget_update((XWidget*)self);
}

/** @brief 收起弹层（解抓取+隐藏+焦点回交+控件重绘；幂等）。 */
static void xdt_popupHide(XDateTimeEdit* self)
{
    XWindow* handle;
    if (!self || !self->m_popupVisible) return;
    self->m_popupVisible = false;
    if (self->m_grabTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_grabTimer);
        self->m_grabTimer = XTIMER_INVALID_ID;
    }
    /* 年编会话随弹层终结（r2 项4；幂等）：显式显形的 m_yearEdit 不被
       隐藏传播收回，残留会在重开弹层时复显旧缓冲覆盖标题年份。此时
       弹层尚可见，endYearEdit 内部的抓取恢复随后被本函数解抓成对抵
       消。 */
    if (self->m_calendar) XCalendarWidget_endYearEdit(self->m_calendar);
    if (self->m_popup) {
        XWidget_releaseMouse((XWidget*)self->m_popup);
        /* 对标 Qt closePopup：鼠标/键盘成对解抓（漏解键盘抓取会使弹层
           收起后全局按键仍被劫持，XComboBox 同款纪律）。 */
        XWidget_releaseKeyboard((XWidget*)self->m_popup);
        handle = XWidget_windowHandle((XWidget*)self->m_popup);
        if (handle) {
            XWindow_setMouseGrabEnabled(handle, false);
            XWindow_setKeyboardGrabEnabled(handle, false);
        }
        XWidget_hide((XWidget*)self->m_popup);
    }
    /* 焦点回交（对标 Qt closePopup 的焦点还原）：弹出时 activateWindow
       曾把应用焦点窗口指向弹层；收起后重新激活宿主顶层恢复按键链。 */
    {
        XWidget* host = XWidget_topLevelWidget((XWidget*)self);
        if (host && host->m_isWindow && self->m_popup &&
            XGuiApplication_focusWindow() ==
                (XWindow*)XWidget_windowHandle((XWidget*)self->m_popup))
            XWidget_activateWindow(host);
    }
#if XVIRTUALKEYBOARD_ON
    /* 悬浮键盘随弹层收起并归还抓取（防死锁：悬浮键盘接管的双抓取若不
     * 归还，弹层关闭后全应用点击仍被键盘截留——强制结束程序才解）。
     * 确认键接线先于收层断开（popupHide/deinit 成对纪律；先断后收，
     * closePopup 尾随信号不再有悬空消费者）。 */
    {
        XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
        if (xdt_kbReadyConn) {
            XObject_disconnect_2(xdt_kbReadyConn);
            xdt_kbReadyConn = NULL;
        }
        if (kb) XVirtualKeyboard_closePopup(kb);
    }
#endif /* XVIRTUALKEYBOARD_ON */
    XWidget_update((XWidget*)self);
}

/** @brief 弹层随值同步（缺陷 F）：弹层开着时值变化落点把日历选中页与
 *         时间行刷新到当前值——弹层此前是打开瞬间的快照，物理键入/
 *         滚轮/箭头步进改值后显示滞留旧值。
 * @details 日历侧：setSelectedDate 仅差异时推送（同值早退，零信号副
 *          作用——日历→编辑框回写槽 xdt_calendarSelectionSlot 对同值
 *          亦幂等，无回环）；showSelectedDate 把显示页翻到选中日期所
 *          在页（同页早退，setCurrentPage 不重发）。时间行侧：段值绘
 *          制实时取自 m_dateTime（xdtp_drawTimeRow），重绘即同步——
 *          XWidget_update(弹层容器) 与既有 ▲▼ 路径（xdtp_timeRowPress）
 *          同口径。调用点=xdt_emitPartChanged 汇聚点（信号发射处），
 *          弹层不可见时零操作。 */
static void xdt_syncPopupValue(XDateTimeEdit* self)
{
    XCalendarWidget* cal;
    int codes[3];
    int n;
    int i;
    if (!self || !self->m_popupVisible || !self->m_popup) return;
    cal = self->m_calendar;
    if (cal && self->m_popup->m_hasCalendar &&
        XWidget_isVisible((XWidget*)cal)) {
        XDate sel = XCalendarWidget_selectedDate(cal);
        if (XDate_compare(&sel, &self->m_dateTime.m_date) != 0)
            XCalendarWidget_setSelectedDate(cal,
                                            &self->m_dateTime.m_date);
        XCalendarWidget_showSelectedDate(cal);
    }
    /* 时间行编辑器文本随值同步（修复 F；跳过持焦编辑器——正在键入的
     * 文本不被值回写覆盖，提交经 editingFinished/Return 走本函数后
     * 再统一刷新）。 */
    n = xdtp_timeRowGroups(self, codes, 3);
    for (i = 0; i < n; ++i) {
        XLineEdit* e = self->m_timeEdits[i];
        char buf[8];
        if (!e || !XWidget_isVisible((XWidget*)e) ||
            (XWidget*)e == XWidget_appFocusWidget()) continue;
        XSnprintf(buf, sizeof(buf), "%02d",
                  xdtp_groupValue(self, codes[i]));
        XLineEdit_setText(e, buf);
    }
    XWidget_update((XWidget*)self->m_popup);
}

#endif /* XCALENDARWIDGET_ON */

/** @brief 延迟平台双抓取（1ms 精确定时器，窗口已映射；XComboBox
 *         同款时序——先杀定时器记账再启用鼠标+键盘平台抓取）。 */
static void XDateTimeEdit_timerEvent(XObject* object, XTimerEvent* event)
{
    XDateTimeEdit* self = (XDateTimeEdit*)object;
    XTimerId id;
    XWindow* handle;
    if (self && event &&
        XTimerEvent_timerId(event) == self->m_grabTimer) {
        id = self->m_grabTimer;
        self->m_grabTimer = XTIMER_INVALID_ID;
        XObject_killTimer(object, id);
        if (self->m_popupVisible && self->m_popup) {
            handle = XWidget_windowHandle((XWidget*)self->m_popup);
            if (handle) {
                XWindow_setMouseGrabEnabled(handle, true);
                XWindow_setKeyboardGrabEnabled(handle, true);
            }
        }
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XAbstractSpinBox, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*)) (object, event);
}

/** @brief 弹层态绘制（calendarPopup=true）：可编辑下拉形态——单枚下箭头
 *         ▽（对标 Qt 6.8.3 QDateTimeEdit::paintEvent qdatetimeedit.cpp:
 *         2446-2468：calendarPopup 态以 CC_ComboBox 呈现 optCombo.editable
 *         =true，仅普通态走 QAbstractSpinBox::paintEvent→CC_SpinBox 的上
 *         下双箭头；两态绘制分支均按 Qt 原样——calendarPopup=Combo 观感、
 *         普通态=SpinBox 观感，命中判定按 Qt 同款借用 ComboBox 箭头矩形，
 *         见 xdt_arrowRect）。文本由内嵌行编辑子件自绘。
 *         calendarPopup=false 走默认绘制路径（与未引入本功能前逐像素
 *         一致）。 */
static void XDateTimeEdit_paintEvent(XWidget* self, XEvent* event)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_PAINT || !edit->m_calendarPopup) {
        XClass_Parent(XWidget, EXWidget_PaintEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    {
        XPainter painter;
        XImage* image;
        XPoint offset;
        XRect r;
        int w = XWidget_width(self);
        int h = XWidget_height(self);
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
        XRect_init(&r, 0, 0, w, h);
#if XSTYLE_ON
        if (XStyle_defaultStyle() != NULL) {
            XStyle* style = XStyle_defaultStyle();
            XStyleOption opt;
            XStyleOption_init(&opt, XStyleCC_ComboBox);
            opt.m_rect = r;
            opt.m_state = XWidget_isEnabled(self)
                ? XStyleState_Enabled | XStyleState_Raised : 0;
            if (edit->m_popupVisible)
                opt.m_state |= XStyleState_Sunken | XStyleState_On;
            if (XWidget_hasFocus(self))
                opt.m_state |= XStyleState_HasFocus;
            if (XWidget_underMouse(self) && XWidget_isEnabled(self))
                opt.m_state |= XStyleState_MouseOver;
            opt.m_text = "";
#if XPALETTE_ON
            opt.m_palette = XWidget_palette(self);
#endif
            XStyle_drawComplexControl(style, XStyleCC_ComboBox, &opt,
                                      &painter, self);
        } else
#endif /* XSTYLE_ON */
        {
            /* 无样式回退：边框+右缘 16px 箭头列（与回退命中/几何同源）。
             * 单枚下箭头（combo 口径，见函数头 Qt 6.8.3 对齐说明）。 */
            uint32_t base = 0xFFFFFFFFu;
            uint32_t dark = 0xFF606060u;
#if XPALETTE_ON
            XPalette palette = XWidget_palette(self);
            XColor c;
            c = XPalette_color(&palette, XPaletteColorGroup_Current,
                               XPaletteColorRole_Base);
            base = XColor_rgba(&c);
            c = XPalette_color(&palette, XPaletteColorGroup_Current,
                               XPaletteColorRole_Dark);
            dark = XColor_rgba(&c);
#endif
            XPainter_fillRect(&painter, &r, base);
            XRect_init(&r, 0, 0, w, 1);
            XPainter_fillRect(&painter, &r, dark);
            XRect_init(&r, 0, h - 1, w, 1);
            XPainter_fillRect(&painter, &r, dark);
            XRect_init(&r, 0, 0, 1, h);
            XPainter_fillRect(&painter, &r, dark);
            XRect_init(&r, w - 1, 0, 1, h);
            XPainter_fillRect(&painter, &r, dark);
            XRect_init(&r, w - 16, 0, 16, h);
            XPainter_fillRect(&painter, &r, base);
            {
                int cx = w - 8;
                int cy = h / 2;
                XPainter_setPen(&painter, dark);
                XPainter_drawLine(&painter, cx - 4, cy - 2, cx + 4, cy - 2);
                XPainter_drawLine(&painter, cx - 4, cy - 2, cx, cy + 3);
                XPainter_drawLine(&painter, cx + 4, cy - 2, cx, cy + 3);
            }
            XRect_init(&r, 0, 0, w, h);
        }
        XPainter_deinit(&painter);
    }
}

/** @brief 弹层态几何：箭头列之外全归编辑区（SC_ComboBoxEditField 口
 *         径，与绘制同源）；非弹层态交回基类（CC_SpinBox 口径不变）。 */
static void XDateTimeEdit_resizeEvent(XWidget* self, XEvent* event)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_RESIZE || !edit->m_calendarPopup) {
        XClass_Parent(XAbstractSpinBox, EXWidget_ResizeEvent,
                      void (*)(XWidget*, XEvent*)) ((XWidget*)self, event);
        return;
    }
    xdt_syncEditGeometry(edit);
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void (*)(XWidget*, XEvent*)) ((XWidget*)self, event);
}

/** @brief 隐藏事件：控件随页签切换/顶层隐藏而隐藏时收起弹层并解抓
 *         取（对标 Qt 弹层随宿主隐藏关闭），再链基类提交路径。 */
static void XDateTimeEdit_hideEvent(XWidget* self, XEvent* event)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    if (edit && event && XEvent_type(event) == XEVENT_TYPE_HIDE) {
#if XCALENDARWIDGET_ON
        if (edit->m_popupVisible) {
            XPrintf("[XDateTimeEdit] popupHide<-hideEvent\n");
            xdt_popupHide(edit);
        }
#endif
    }
    XClass_Parent(XAbstractSpinBox, EXWidget_HideEvent,
                  void (*)(XWidget*, XEvent*)) ((XWidget*)self, event);
}

/** @brief 聚焦进入（对标 QDateTimeEdit::focusInEvent qdatetimeedit.cpp:
 *         1311-1359 的分段选区语义）：Mouse/Popup 原因不动选区；其余
 *         原因（Tab/Shortcut/默认=首段、Backtab=末段）聚焦后整段选中
 *         对应分段——覆写基类「Tab 进入全选文本」的 QAbstractSpinBox
 *         语义（Qt 中该 selectAll 被 QDateTimeEdit 分段选区取代）。 */
static void XDateTimeEdit_focusInEvent(XWidget* self, XEvent* event)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XFocusReason reason;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_FOCUS_IN) {
        XClass_Parent(XAbstractSpinBox, EXWidget_FocusInEvent,
                      void (*)(XWidget*, XEvent*)) (self, event);
        return;
    }
    reason = XFocusEvent_reason((XFocusEvent*)event);
    /* 先链基类（保留基类聚焦路径的带焦重绘处理）；基类按 Tab 原因会
     * 全选文本，随后分段选区覆盖之（终态=整段反选，对标 Qt）。 */
    XClass_Parent(XAbstractSpinBox, EXWidget_FocusInEvent,
                  void (*)(XWidget*, XEvent*)) (self, event);
#if XVIRTUALKEYBOARD_ON
    /* 缺陷⑧：不经鼠标的弹屏（守护边沿 guardTick ≤200ms 后 popup）在
     * 此补 ready→确认槽消费者；连接对无键盘场景惰性无害（槽内
     * m_target 会话守卫，他会话确认不误收，deinit 成对断开兜底）。 */
    xdt_fieldKeyboardArm(edit);
#endif
    if (reason == XFocusReason_Mouse || reason == XFocusReason_Popup)
        return;
    {
        XdtSectionTok toks[XDT_SECTION_MAX];
        int n = xdt_tokenizeTyped(xdt_effectiveFormat(edit), toks,
                                  XDT_SECTION_MAX,
                                  xdt_tokenParserType(edit));
        if (n > 0)
            xdt_focusSectionIndex(edit,
                                  (reason == XFocusReason_Backtab) ? n - 1
                                                                   : 0);
    }
}

/** @brief 清空虚槽重载（对标 QDateTimeEdit::clear() → QDateTimeEditPrivate::
 *         clearSection qdatetimeedit.cpp:1975-1991）：仅把当前分段的显示
 *         文本替换为等宽空格（值不动、光标回段首），非基类的「清空全部
 *         文本+cleared 待解释」语义。分段不在格式中时不动作。 */
static void VXDateTimeEdit_clear(XAbstractSpinBox* self)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XLineEdit* line;
    char buf[128];
    XdtSectionRange ranges[XDT_SECTION_MAX];
    XdtSectionTok toks[XDT_SECTION_MAX];
    int count = 0;
    int n;
    int index = -1;
    int i;
    int bStart;
    if (!edit) return;
    xdt_commitTyping(edit, false);
    line = XAbstractSpinBox_lineEdit(self);
    if (!line) return;
    xdt_render(edit, buf, sizeof(buf), ranges, XDT_SECTION_MAX, &count);
    n = xdt_tokenizeTyped(xdt_effectiveFormat(edit), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(edit));
    for (i = 0; i < n; ++i) {
        if (toks[i].code == edit->m_currentSection) { index = i; break; }
    }
    if (index < 0 || index >= count || ranges[index].len <= 0) return;
    bStart = xdt_charToByteOffset(buf, ranges[index].start);
    /* 数字段渲染全 ASCII：段字节数=字符数；整段替换为空格（对标
     * t.replace(pos, size, spaces)）。 */
    for (i = 0; i < ranges[index].len; ++i) buf[bStart + i] = ' ';
    XLineEdit_setText(line, buf);
    XLineEdit_setCursorPosition(line, ranges[index].start);
}

/** @brief 按样式取下拉箭头子矩形（无样式回退右缘 16px 列，与回退
 *         绘制同源）。 */
static XRect xdt_arrowRect(XDateTimeEdit* edit)
{
    XRect r;
    int w = XWidget_width((XWidget*)edit);
    int h = XWidget_height((XWidget*)edit);
#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCC_ComboBox);
        XRect_init(&opt.m_rect, 0, 0, w, h);
        opt.m_state = XWidget_isEnabled((XWidget*)edit)
            ? (uint32_t)XStyleState_Enabled : 0;
        r = XStyle_subControlRect(XStyle_defaultStyle(), XStyleCC_ComboBox,
                                  &opt, XStyleSC_ComboBoxArrow,
                                  (XWidget*)edit);
        if (r.width > 0 && r.height > 0) return r;
    }
#endif /* XSTYLE_ON */
    XRect_init(&r, w - 16, 0, 16, h);
    return r;
}

/** @brief 弹层态编辑区几何（SC_ComboBoxEditField 口径，与 combo 绘制
 *         同源——弹层态按 CC_ComboBox 呈现单下箭头，见
 *         XDateTimeEdit_paintEvent 函数头 Qt 6.8.3 对齐说明；行编辑子
 *         件落在箭头列之外的编辑区，不盖箭头列。无样式回退右缘留
 *         16px 箭头列）。setCalendarPopup 开关与 resize 共用。 */
static void xdt_syncEditGeometry(XDateTimeEdit* self)
{
    XLineEdit* line;
    int w;
    int h;
    if (!self) return;
    line = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (!line) return;
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        XStyleOption opt;
        XRect field;
        XStyleOption_init(&opt, XStyleCC_ComboBox);
        XRect_init(&opt.m_rect, 0, 0, w, h);
        opt.m_state = XWidget_isEnabled((XWidget*)self)
            ? (uint32_t)XStyleState_Enabled : 0;
        field = XStyle_subControlRect(XStyle_defaultStyle(),
                                      XStyleCC_ComboBox, &opt,
                                      XStyleSC_ComboBoxEditField,
                                      (XWidget*)self);
        if (field.width > 0 && field.height > 0) {
            XWidget_setGeometry((XWidget*)line, field.x, field.y,
                                field.width, field.height);
            return;
        }
    }
#endif /* XSTYLE_ON */
    XWidget_setGeometry((XWidget*)line, 0, 0, w - 16 > 1 ? w - 16 : 1, h);
}

/* ==================== 生命周期与虚表 ==================== */

static void VXDateTimeEdit_deinit(XDateTimeEdit* self)
{
    int i;
    if (!self) return;
    /* 键盘确认键接线无条件断开（防非弹层路径残留会话连接；键盘为应
     * 用单例、句柄文件级静态，任一实例终结即清）。 */
    if (xdt_kbReadyConn) {
        XObject_disconnect_2(xdt_kbReadyConn);
        xdt_kbReadyConn = NULL;
    }
#if XCALENDARWIDGET_ON
    /* 弹层存活先收层（解抓取+杀延迟定时器，须先于对象删除）。 */
    if (self->m_popupVisible) xdt_popupHide(self);
    /* 时间行编辑器先于弹层容器删除（显式摘离子链，防级联重复释放）。 */
    for (i = 0; i < 3; ++i) {
        if (self->m_timeEdits[i]) {
            XClassDelete(self->m_timeEdits[i]);
            self->m_timeEdits[i] = NULL;
        }
    }
    if (self->m_displayFormat) {
        XClassDelete((XClass*)self->m_displayFormat);
        self->m_displayFormat = NULL;
    }
    /* 释放内置/接管的日历控件（先删日历：显式删除已将其摘离弹层
     * 子链，随后删弹层容器不会级联重复释放）。 */
    if (self->m_calendar) {
        XClassDelete((XClass*)self->m_calendar);
        self->m_calendar = NULL;
    }
    if (self->m_popup) {
        XClassDelete((XClass*)self->m_popup);
        self->m_popup = NULL;
    }
#else
    if (self->m_displayFormat) {
        XClassDelete((XClass*)self->m_displayFormat);
        self->m_displayFormat = NULL;
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
    /* P1-5：覆写 Interpret——基类默认空操作导致键入数字既不改值也不
     * 重渲染；覆写后基类 Enter/失焦/隐藏/关闭的 interpretText 分派
     * 路径均把键入文本提交回值（对标 QDateTimeEdit::interpretText）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Interpret,
                             XDateTimeEdit_interpret);
    /* 对标 QDateTimeEdit::clear()=清当前分段（非基类全文本清空）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Clear, VXDateTimeEdit_clear);
    /* 时间行编辑器事件过滤（FocusIn 显式弹数字键盘）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, VXDateTimeEdit_eventFilter);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             XDateTimeEdit_keyPressEvent);
    /* F3-②：覆写鼠标按下——点击命中测试定段+整段选中+焦点主权回收
     * （行编辑已设鼠标穿透，点击直达本控件）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             XDateTimeEdit_mousePressEvent);
    /* 对标 QDateTimeEdit::focusInEvent 的分段选区语义（Tab 进首段）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent,
                             XDateTimeEdit_focusInEvent);
    /* calendarPopup 弹层接线：弹层态绘制/几何/收层/延迟抓取四虚槽。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, XDateTimeEdit_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, XDateTimeEdit_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, XDateTimeEdit_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, XDateTimeEdit_timerEvent);
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
    /* 默认值=当前日期时间：必须用墙钟（currentDateTime）。
     * XDateTime_currentMSecsSinceEpoch 已切换为单调时钟（开机时长），
     * 供计时/超时测量使用，不代表纪元时刻——用它会让编辑框显示
     * 1970-01-01 加开机时长。 */
    self->m_dateTime = XDateTime_currentDateTime();
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
    /* F3 分段键入态（year 段从无键入起步）。 */
    xdt_resetTyping(self);
    /* 键入撤销快照（G3 退格回归）从无撤销起步（XMemset 已清零，显式
       置位自文档）。 */
    self->m_undoSection = (int)XDateTimeEditSection_NoSection;
    self->m_undoValue = -1;
    /* F3-②/③：行编辑鼠标穿透（WA_TransparentForMouseEvents）——点击
     * 经 childAt 落到行编辑后在派发层被属性跳过、改投本控件
     * mousePressEvent（定段+整选+焦点回收），行编辑退化为显示件；
     * 对标 QDateTimeEdit 行编辑仅作编辑缓存、分段交互由控件层接管。 */
    {
        XLineEdit* lineEdit = XAbstractSpinBox_lineEdit(
            (XAbstractSpinBox*)self);
        if (lineEdit) {
            XWidget_setAttribute((XWidget*)lineEdit,
                                 XWidgetAttribute_TransparentForMouseEvents,
                                 true);
            /* 焦点策略 NoFocus + 焦点代理取消（合并回归修复）：行编辑的
               StrongFocus + deepestFocusProxy 会把键盘/程序性焦点落进行
               编辑，数字/方向键被 XLineControl_processKeyEvent 当纯文本
               消费，分段键入整条饿死（GUI 表现为裸文本拼接）。F3 的
               「焦点回收至壳本体」只覆盖鼠标路径——键盘与程序性聚焦
               （XWidget_setFocusReason 经 deepestFocusProxy）必须在此
               掐断，分段键入（xdt_typeDigit/commitTyping）才接得到键。 */
            XWidget_setFocusPolicy((XWidget*)lineEdit,
                                   XWidgetFocusPolicy_NoFocus);
        }
    }
    XWidget_setFocusProxy((XWidget*)self, NULL);
    /* 段高亮/光标可见性（缺陷 D）：行编辑焦点代理反向指向壳本体——
       XWidget_hasFocus(行编辑) 经最深焦点代理链解析到壳，壳持焦期间
       行编辑绘制门禁（XLineEdit.c paintEvent：cursorVisible=hasFocus
       && ...）随之成立：当前编辑段恒有整段高亮底色（Highlight 角色
       蓝底，XLineControl Selections 分支），段内键入位置有可见光标条
       （SH_BlinkCursorWhenTextSelected=1 时选区+光标同屏，光标钉在
       段尾=键入落点，随 xdt_focusSectionIndex/xdt_showTypingBuffer 的
       setSelection 换段移动）。键事件路由不受影响：dispatchKeyEvent
       按 g_focusWidget 投递，setFocus(壳) 经壳自身最深代理（NULL）仍
       落壳本体，行编辑 NoFocus+鼠标穿透不变，分段键入不被行编辑吃掉
       （F3 契约保持）。注册后焦点进退经代理注册表补 update(行编辑)，
       高亮+光标随聚焦/失焦即时出现/消失。 */
    {
        XLineEdit* lineEdit = XAbstractSpinBox_lineEdit(
            (XAbstractSpinBox*)self);
        if (lineEdit)
            XWidget_setFocusProxy((XWidget*)lineEdit, (XWidget*)self);
    }
    /* 输入法/屏幕键盘接入（缺陷 A 定版：恢复壳 WA14，覆盖此前「删除壳
       WA14」的过杀方案——该方案使点字段也不弹键盘）。壳持
       WA_InputMethodEnabled+ImhDigitsOnly 两行（对标 qdatetimeedit.cpp:
       2570/1607 的 setAttribute(WA_InputMethodEnabled)+setImh 口径）：
       点字段文本分段 → 按下汇聚点 notifyPress（XWidget.c:1520）→ 穿透
       归属上溯（点编辑区时 childAt 命中的是 F3-② 置穿透的内嵌行编辑，
       其 ImhNone 会误弹 QWERTY；XVirtualKeyboard_notifyPress 沿父链收
       敛到实际接收按下事件的 WA14 壳，xkb_supportedTarget 的 WA14 放行
       回退过判）→ 虚拟键盘以 Digits 12 键数字盘弹出（缺陷⑥ 2026-10-01
       定版 DigitsOnly→Digits，Qt 原版 setImh(ImhPreferNumbers) 的
       Number 布局在壳目标下「+/.」为死键/污键，Digits 全键有效，且与
       弹层时间行值框、日历年份编辑既有 Digits 统一，xdt_typeDigit 只吃
       0-9 语义最准）；键入经 xkb_sendKey 合成键注入焦点控件（类型
       无关）→ 本控件 keyPressEvent 分段消化（xdt_typeDigit，与无头
       apitest 注入同路，不直写行编辑裸拼接）。箭头误触发的另一半在
       弹层路径闭环：箭头命中分支与 xdt_popupShow 尾部对虚拟键盘显式
       closePopup——notifyPress 先于控件 mousePressEvent（同拍先弹后
       关，面板未出帧即收，无闪现），见 XDateTimeEdit_mousePressEvent
       箭头分支注。年份编辑器是独立 XLineEdit（XCalendarWidget 自带
       WA14+DigitsOnly），不受影响；XDateEdit/XTimeEdit 经共享 init
       继承本两行。 */
    XWidget_setAttribute((XWidget*)self,
                         XWidgetAttribute_InputMethodEnabled, true);
    XWidget_setInputMethodHints((XWidget*)self,
                                XInputMethodHint_DigitsOnly);
    /* 以当前时间刷新编辑框文本（无信号）。 */
    (void)now;
    xdt_refreshText(self);

    /* P2：calendarPopup 默认 false（对标 QDateTimeEdit::calendarPopup
     * 默认值）。true 时弹出接线见「日历弹层」节：箭头命中开层、
     * 贴边超屏翻转、外部点击/Esc 收层、选中日期写回编辑框。 */
    self->m_calendarPopup = false;
    self->m_timeSpec = 0;
    /* 解析类型默认 DateTime（基类全段可用；XDateEdit/XTimeEdit 构造
     * 时分别改置 Date/Time，对标 QDateTimeParser::parserType）。 */
    self->m_parserType = (int)XDateTimeEditParserType_DateTime;
#if XCALENDARWIDGET_ON
    self->m_popup = NULL;
    self->m_popupVisible = false;
    self->m_grabTimer = XTIMER_INVALID_ID;
#endif
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
    XDateTime old;
    if (!self || !dateTime) return;
    old = self->m_dateTime;
    self->m_dateTime = *dateTime;
    xdt_clamp(self);
    xdt_refreshText(self);
    /* P1-4：dateChanged/timeChanged 随对应部分实际变化发射（对标
     * QDateTimeEdit::setDateTime 三信号齐发语义）。 */
    xdt_emitPartChanged(self, &old);
}

const XDateTime* XDateTimeEdit_dateTime(const XDateTimeEdit* self)
{
    return self ? &self->m_dateTime : NULL;
}

void XDateTimeEdit_setDate(XDateTimeEdit* self, const XDate* date)
{
    XDateTime old;
    if (!self || !date) return;
    old = self->m_dateTime;
    XDateTime_setDate(&self->m_dateTime, *date);
    xdt_clamp(self);
    xdt_refreshText(self);
    /* P1-4：setDate 路径发射 dateChanged（对标 QDateTimeEdit::setDate，
     * 同时经 emitPartChanged 保持 dateTimeChanged 语义）。 */
    xdt_emitPartChanged(self, &old);
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
    XDateTime old;
    if (!self || !time) return;
    old = self->m_dateTime;
    XDateTime_setTime(&self->m_dateTime, *time);
    xdt_clamp(self);
    xdt_refreshText(self);
    /* P1-4：setTime 路径发射 timeChanged（对标 QDateTimeEdit::setTime，
     * 同时经 emitPartChanged 保持 dateTimeChanged 语义）。 */
    xdt_emitPartChanged(self, &old);
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
    XDateTime old;
    if (!self || !dateTime) return;
    old = self->m_dateTime;
    self->m_minimum = *dateTime;
    xdt_clamp(self);
#if XCALENDARWIDGET_ON
    /* 对标 QAbstractSpinBoxPrivate::setRange → syncCalendarWidget：范围
     * 变化推送日历可点选范围。 */
    xdt_syncCalendarRange(self);
#endif
    /* P1-4：下界钳位实际改动当前值时补发三信号（对标
     * QAbstractSpinBoxPrivate::setRange 的值变化发射语义）。 */
    if (XDateTime_compare(&old, &self->m_dateTime) != 0)
        xdt_emitPartChanged(self, &old);
}

const XDateTime* XDateTimeEdit_maximumDateTime(const XDateTimeEdit* self)
{
    return self ? &self->m_maximum : NULL;
}

void XDateTimeEdit_setMaximumDateTime(XDateTimeEdit* self,
                                      const XDateTime* dateTime)
{
    XDateTime old;
    if (!self || !dateTime) return;
    old = self->m_dateTime;
    self->m_maximum = *dateTime;
    xdt_clamp(self);
#if XCALENDARWIDGET_ON
    /* 对标 QAbstractSpinBoxPrivate::setRange → syncCalendarWidget。 */
    xdt_syncCalendarRange(self);
#endif
    /* P1-4：上界钳位实际改动当前值时补发三信号（对标
     * QAbstractSpinBoxPrivate::setRange 的值变化发射语义）。 */
    if (XDateTime_compare(&old, &self->m_dateTime) != 0)
        xdt_emitPartChanged(self, &old);
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
    /* 对标 Qt setDisplayFormat 的 parseFormat 守卫（qdatetimeedit.cpp:929
     * `if (d->parseFormat(format))`）：不含任何可识别分段的格式不设置、
     * 原格式保持。 */
    {
        XdtSectionTok toks[XDT_SECTION_MAX];
        if (xdt_tokenizeTyped(utf8 ? utf8 : "", toks, XDT_SECTION_MAX,
                              xdt_tokenParserType(self)) == 0)
            return;
    }
    if (!self->m_displayFormat) self->m_displayFormat = XString_create();
    if (self->m_displayFormat)
        XString_assign_utf8(self->m_displayFormat, utf8 ? utf8 : "");
    /* 格式变化后键入累积段的序号/位宽失配：丢弃中间态（下次键入按
     * 新格式重启）。 */
    xdt_resetTyping(self);
    /* 对标 qdatetimeedit.cpp:950：currentSectionIndex 越段回正——当前
     * 分段不在新格式中时落到首段（否则点击/步进定位错段）。 */
    {
        XdtSectionTok toks[XDT_SECTION_MAX];
        int n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks,
                                  XDT_SECTION_MAX,
                                  xdt_tokenParserType(self));
        if (n > 0) {
            int i;
            bool found = false;
            for (i = 0; i < n; ++i) {
                if (toks[i].code == self->m_currentSection) {
                    found = true;
                    break;
                }
            }
            if (!found) self->m_currentSection = toks[0].code;
        }
    }
    /* 对标 qdatetimeedit.cpp:951-965：按新格式的分段构成收窄范围与值。
     * 纯时间段→日期范围收窄到当前值日期（收窄后时间范围非法时复位整
     * 天范围并还原时间，958-960）；纯日期段→复位时间范围并把值的时
     * 间部分清零（963-964，startOfDay 口径；值清零为私有直赋语义，
     * 不经发射路径）。 */
    {
        int mask = XDateTimeEdit_sections(self);
        bool timeShown = (mask & XDT_TIME_SECTION_MASK) != 0;
        bool dateShown = (mask & XDT_DATE_SECTION_MASK) != 0;
        if (timeShown && !dateShown) {
            XTime saved = self->m_dateTime.m_time;
            XDate cur = self->m_dateTime.m_date;
            XDateTimeEdit_setDateRange(self, &cur, &cur);
            if (XTime_compare(&self->m_minimum.m_time,
                              &self->m_maximum.m_time) >= 0) {
                XTime tmin;
                XTime tmax;
                XTime_setHMS(&tmin, 0, 0, 0, 0);
                XTime_setHMS(&tmax, 23, 59, 59, 999);
                XDateTimeEdit_setTimeRange(self, &tmin, &tmax);
                XDateTimeEdit_setTime(self, &saved);
            }
        } else if (dateShown && !timeShown) {
            XTime tmin;
            XTime tmax;
            XTime_setHMS(&tmin, 0, 0, 0, 0);
            XTime_setHMS(&tmax, 23, 59, 59, 999);
            XDateTimeEdit_setTimeRange(self, &tmin, &tmax);
            XTime_setHMS(&self->m_dateTime.m_time, 0, 0, 0, 0);
            xdt_clamp(self);
        }
    }
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
    /* 对标 Qt setCurrentSection 的有效性检查（qdatetimeedit.cpp:717-721）：
     * NoSection 或分段不在显示格式中时不动作（修正此前「码不在格式中
     * 仅记录」的宽松语义——它会让后续 currentSectionIndex/步进定位错段）。 */
    if (section == (int)XDateTimeEditSection_NoSection ||
        !(section & XDateTimeEdit_sections(self)))
        return;
    /* F3-②：API 换段与点击/方向键同口径——先落账旧段输入，再落地+
     * 整段选中。 */
    xdt_commitTyping(self, false);
    xdt_focusSectionCode(self, section);
}

int XDateTimeEdit_sections(const XDateTimeEdit* self)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int i;
    int mask = 0;
    if (!self) return 0;
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
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

/* 用户变体信号桩随声明迁往子类：见 XDateEdit.c 的
 * XDateEdit_userDateChanged_signal 与 XTimeEdit.c 的
 * XTimeEdit_userTimeChanged_signal。 */



















/* ==================== Task 2.5：日历弹出/时区/分段 ==================== */

void XDateTimeEdit_setCalendarPopup(XDateTimeEdit* self, bool popup)
{
    if (!self || self->m_calendarPopup == popup) return;
    self->m_calendarPopup = popup;
#if XCALENDARWIDGET_ON
    /* 关闭时弹层存活先收层（对标 Qt 禁用 calendarPopup 即隐藏弹层）。 */
    if (!popup && self->m_popupVisible) xdt_popupHide(self);
    /* 切换瞬间编辑区几何随形态换轨（弹层态=SC_ComboBoxEditField 留
     * 箭头列；普通态回基类 CC_SpinBox 口径——经 update 后重排或直接
     * 依基类 resize 路径收口，此处主动同步弹层态）。 */
    if (popup) xdt_syncEditGeometry(self);
#endif
    XWidget_update((XWidget*)self);
}
/* P2：NULL 回退值随默认值改为 false（对标 QDateTimeEdit 默认）。 */
bool XDateTimeEdit_calendarPopup(const XDateTimeEdit* self)
{ return self ? self->m_calendarPopup : false; }

#if XCALENDARWIDGET_ON

/* ==================== calendarWidget 族（对标 QDateTimeEdit::
 *                     calendarWidget / setCalendarWidget；聚合思路
 *                     与 QComboBox::view/setView 一致） ==================== */

/** @brief 日历选中变化联动槽：把日历选中日期回填到编辑框（单向同步，
 *         setDate 不回写日历，故无回环）。同值不重复提交（幂等：弹层
 *         开启时的选中同步、重复点同一天均不产生多余信号）。 */
static void xdt_calendarSelectionSlot(XObject* receiver, XVarList* args)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)receiver;
    XDate d;
    XDate cur;
    (void)args;
    if (!edit || !edit->m_calendar) return;
    /* 只读门禁（缺陷 E）：日历选日回写槽此前无门禁——setReadOnly(true)
       后点选日期照常改值。按 Qt 口径：readOnly 的 calendarPopup 仍可
       打开浏览、翻页、年份编辑，但选择不落值（本槽直接返回；弹层收起
       槽 xdt_calendarClickedSlot 不受影响，点选后弹层照常关闭，重开时
       xdt_popupShow 以编辑框当前值重新同步选中态）。 */
    if (XAbstractSpinBox_isReadOnly((XAbstractSpinBox*)edit)) return;
    d = XCalendarWidget_selectedDate(edit->m_calendar);
    cur = XDateTimeEdit_date(edit);
    if (XDate_compare(&cur, &d) != 0)
        XDateTimeEdit_setDate(edit, &d);
}

XCalendarWidget* XDateTimeEdit_calendarWidget(const XDateTimeEdit* self)
{
    XDateTimeEdit* edit = (XDateTimeEdit*)self;
    XCalendarWidget* cal;
    if (!edit) return NULL;
    if (edit->m_calendar) return edit->m_calendar;
    /* 懒创建：保持 NULL 父控件（对象由本控件持有，deinit 释放；
     * 弹出路径再把日历收编进弹层容器子链）。 */
    cal = XCalendarWidget_create(NULL, 0);
    if (!cal) return NULL;
    edit->m_calendar = cal;
    /* 以当前日期初始化日历选中态（不触发编辑框信号：先置值后连接）。 */
    XCalendarWidget_setSelectedDate(cal, &edit->m_dateTime.m_date);
    /* 连接日历选中信号 → setDate 联动槽。 */
    XObject_connect_1((XObject*)cal,
        (size_t)XCalendarWidget_selectionChanged_signal(cal),
        (XObject*)edit, xdt_calendarSelectionSlot,
        XConnectionType_Direct);
    /* 连接点击信号 → 收层槽（对标 Qt 点日期即选中并收起弹层）。 */
    XObject_connect_1((XObject*)cal,
        (size_t)XCalendarWidget_clicked_signal(cal, NULL),
        (XObject*)edit, xdt_calendarClickedSlot,
        XConnectionType_Direct);
    return cal;
}

void XDateTimeEdit_setCalendarWidget(XDateTimeEdit* self,
                                     XCalendarWidget* calendar)
{
    if (!self || calendar == self->m_calendar) return;
    /* 取得所有权：先释放旧的内置/接管日历。 */
    if (self->m_calendar) {
        XClassDelete((XClass*)self->m_calendar);
        self->m_calendar = NULL;
    }
    self->m_calendar = calendar;
    if (calendar) {
        /* 以外部日历的选中日期回填本控件（触发 dateChanged 等信号）。 */
        XDate d = XCalendarWidget_selectedDate(calendar);
        XDateTimeEdit_setDate(self, &d);
        /* 连接选中/点击信号 → 联动槽（写回+收层）。 */
        XObject_connect_1((XObject*)calendar,
            (size_t)XCalendarWidget_selectionChanged_signal(calendar),
            (XObject*)self, xdt_calendarSelectionSlot,
            XConnectionType_Direct);
        XObject_connect_1((XObject*)calendar,
            (size_t)XCalendarWidget_clicked_signal(calendar, NULL),
            (XObject*)self, xdt_calendarClickedSlot,
            XConnectionType_Direct);
        /* 对标 initCalendarPopup → syncCalendarWidget：外部日历接管后
         * 立即同步本控件日期上下界。 */
        xdt_syncCalendarRange(self);
        /* 弹层容器已存在时把外部日历收编进容器子链（与内置日历同
         * 承载；几何在弹层显形路径统一收口；显式 show 同
         * xdt_ensureCalendarPopup——外部日历若从未 show 过，换父后
         * 不会随弹层显形）。 */
        if (self->m_popup &&
            XWidget_parentWidget((XWidget*)calendar) !=
                (XWidget*)self->m_popup) {
            XWidget_setParent((XWidget*)calendar,
                              (XWidget*)self->m_popup, 0);
            XWidget_setGeometry((XWidget*)calendar, 1, 1,
                                XWidget_width((XWidget*)calendar),
                                XWidget_height((XWidget*)calendar));
            XWidget_show((XWidget*)calendar);
        }
    }
}

#endif /* XCALENDARWIDGET_ON */

void XDateTimeEdit_setTimeSpec(XDateTimeEdit* self, int spec)
{ if (self) self->m_timeSpec = spec; }
int XDateTimeEdit_timeSpec(const XDateTimeEdit* self)
{ return self ? self->m_timeSpec : 0; }

void XDateTimeEdit_setCurrentSectionIndex(XDateTimeEdit* self, int index)
{
    if (!self || index < 0) return;
    /* 分段序号与分段枚举码此前共用同一字段：传普通序号会落入非法
     * 分段码，步进定位错段（14.124 扫描 中 项）。现统一走 focus 落地：
     * 序号映射为对应分段码+离段落账+整段选中（F3-②，与点击/方向键
     * 同口径）。 */
    xdt_focusSectionIndex(self, index);
}

/* ==================== 分段查询族 ==================== */

int XDateTimeEdit_sectionCount(const XDateTimeEdit* self)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    if (!self) return 0;
    return xdt_tokenizeTyped(xdt_effectiveFormat(self), toks,
                             XDT_SECTION_MAX, xdt_tokenParserType(self));
}

int XDateTimeEdit_currentSectionIndex(const XDateTimeEdit* self)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    int i;
    if (!self) return 0;
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
    for (i = 0; i < n; ++i)
        if (toks[i].code == self->m_currentSection) return i;
    return 0;
}

int XDateTimeEdit_sectionAt(const XDateTimeEdit* self, int index)
{
    XdtSectionTok toks[XDT_SECTION_MAX];
    int n;
    if (!self || index < 0) return (int)XDateTimeEditSection_NoSection;
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
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
    n = xdt_tokenizeTyped(xdt_effectiveFormat(self), toks, XDT_SECTION_MAX,
                      xdt_tokenParserType(self));
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
    /* 对标 Qt setSelectedSection（qdatetimeedit.cpp:864-874）：NoSection
     * 反选全部文本；其余分段仅当确实出现在显示格式中才生效（有效性
     * 检查），生效即整段选中；API 换段前先落账旧段键入。 */
    if (!self) return;
    if (section == (int)XDateTimeEditSection_NoSection) {
        XLineEdit* line = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
        if (line) XLineEdit_deselect(line);
        return;
    }
    xdt_focusSectionCode(self, section);
}


#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */