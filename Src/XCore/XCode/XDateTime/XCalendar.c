/**
 * @file       XCalendar.c
 * @brief      XCalendar 历法值对象实现（对标 Qt 6.8 QCalendar）。
 * @details    取证（本机 /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/corelib/time）：
 *             - qcalendar.h:75-90 System 枚举、:111 默认 Gregorian、
 *               :120 值语义、:121 isValid、:124-149 日期查询/换算；
 *             - qcalendarmath_p.h:110-141 LeapDayGregorian1Bce=1721119、
 *               FourYears/FiveMonths、yearMonthToYearDays、dayInYearToYmd、
 *               qDivMod（向下取整除法，负数亦 floor）；
 *             - qgregoriancalendar.cpp:230-262（BaseJd/FourCenturies、
 *               julianFromParts/partsFromJulian）——本库 Gregorian 直接复用
 *               既有 XDate 的儒略日换算（AGENTS.md 铁律 4），不重复实现；
 *             - qjuliancalendar.cpp:60-80（Julian 4 年周期）；
 *             - qmilankoviccalendar.cpp:49-96（900 年周期闰年规则与换算）；
 *             - qislamiccivilcalendar.cpp:54-105（30 年周期、EpochJd=
 *               1948440、ThirtyYears/ElevenMonths）。
 *
 *             本机 Qt 6.8.0 实跑对拍（g++ + libQt6Core，见施工记录）：
 *             四历法 JDN 2299161/2440588/2451545/2460677 的 partsFromDate
 *             与 dateFromParts 往返，及 leap1900/2000、daysInMonth(2,y)
 *             逐值一致；断言锁在 Test/QtAlign/XTimeZoneCalendarAlignTest.c。
 *
 *             【负年份/无 0 年口径】与 Qt 一致：year<=0 时内部 +1 转为
 *             连续纪年（yearMonthToYearDays）。本库 XDate 只承载 1..9999
 *             公历，故非公历换算结果超出该范围时按无效返回，不伪造。
 */
#include "XCalendar.h"
#include "XStringUtils.h"
/* 儒略日存取（XDate_toJulianDay/fromJulianDay）已正式公开在 XDate.h
 * （XCalendar.h:62 已含），原过渡桥 XDateTimeJdn_Protected.h 已删除。 */

/* ---- 内部原语（Qt QRoundingDown::qDivMod 的 C 版：向下取整除法） ---- */

/** @brief floor(a/b)（b>0；a 可为负）。 */
static int64_t xtz_fdiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if (a < 0 && q * b != a) --q;
    return q;
}

/** @brief a - floor(a/b)*b（b>0；a 可为负），结果恒在 [0,b)。 */
static int64_t xtz_fmod(int64_t a, int64_t b)
{
    return a - xtz_fdiv(a, b) * b;
}

/* Qt qcalendarmath_p.h:116-128 yearMonthToYearDays 的 C 版。
 * 输出：*outYear（可能已因 1/2 月回退一年）、*outDays（年内 0 基日序）。 */
static void xcal_yearMonthToYearDays(int year, int month,
                                     int64_t* outYear, int64_t* outDays)
{
    int64_t y = year;
    int64_t m = month;
    if (y < 0) ++y;
    m -= 3;
    if (m < 0) { --y; m += 12; }
    *outYear = y;
    *outDays = xtz_fdiv(153 * m + 2, 5);
}

/* Qt qcalendarmath_p.h:130-140 dayInYearToYmd 的 C 版。
 * 输入 0 基年内日序，输出年月日（month 1 基、day 1 基）。 */
static void xcal_dayInYearToYmd(int64_t dayInYear,
                                int64_t* outYear, int64_t* outMonth,
                                int64_t* outDay)
{
    int64_t mq = xtz_fdiv(5 * dayInYear + 2, 153);
    int64_t mr = xtz_fmod(5 * dayInYear + 2, 153);
    int64_t yq = xtz_fdiv(mq + 2, 12);
    int64_t yr = xtz_fmod(mq + 2, 12);
    *outYear = yq;
    *outMonth = yr + 1;
    *outDay = xtz_fdiv(mr, 5) + 1;
}

/* ---- 各历法：日期 → 儒略日（返回 -1 表示无法表示） ---- */

static int64_t xcal_jdFromGregorian(int year, int month, int day)
{
    /* 铁律 4：直接复用 XDate 的儒略日换算，不重写公式。 */
    XDate d = XDate_create_date(year, month, day);
    if (!XDate_isValid(&d)) return -1;
    return XDate_toJulianDay(&d);
}

static int64_t xcal_jdFromJulian(int year, int month, int day)
{
    /* qjuliancalendar.cpp:63-70：365*y + floor(y/4) + yearDays + day + 1721117。
     * 本机 Qt 6.8.0 对拍：Julian 1-1-1 的 JDN 与 Meeus 公式一致。 */
    int64_t y, days;
    xcal_yearMonthToYearDays(year, month, &y, &days);
    return 365 * y + xtz_fdiv(y, 4) + days + day + 1721117;
}

#define XCAL_NINE_CENTURIES (365 * 900 + 900 / 4 - 7)
#define XCAL_LEAP_CENTURY   (365 * 100 + 25)

static int64_t xcal_jdFromMilankovic(int year, int month, int day)
{
    /* qmilankoviccalendar.cpp:74-85。 */
    int64_t y, days, cyq, cyr, fromYear;
    xcal_yearMonthToYearDays(year, month, &y, &days);
    cyq = xtz_fdiv(y, 100);
    cyr = xtz_fmod(y, 100);
    fromYear = xtz_fdiv((int64_t)XCAL_NINE_CENTURIES * cyq + 6, 9)
             + xtz_fdiv((int64_t)XCAL_LEAP_CENTURY * cyr, 100);
    return fromYear + days + day + 1721119;
}

#define XCAL_THIRTY_YEARS  (11 * 355 + 19 * 354)
#define XCAL_ELEVEN_MONTHS (6 * 30 + 5 * 29)
#define XCAL_ISLAMIC_EPOCH 1948440

static int64_t xcal_jdFromIslamicCivil(int year, int month, int day)
{
    /* qislamiccivilcalendar.cpp:70-80。 */
    int64_t yadj = (year > 0) ? (int64_t)(year - 1) : (int64_t)year;
    return xtz_fdiv((int64_t)XCAL_THIRTY_YEARS * yadj + 14, 30)
         + xtz_fdiv((int64_t)XCAL_ELEVEN_MONTHS * (month - 1) + 5, 11)
         + day + XCAL_ISLAMIC_EPOCH - 1;
}

/* ---- 各历法：儒略日 → 年月日（ymd 无效时置 m_isValid=false） ---- */

static XCalendarYearMonthDay xcal_gregorianFromJd(int64_t jd)
{
    /* 铁律 4：直接复用 XDate 的儒略日 → 公历拆分。 */
    XCalendarYearMonthDay r;
    XDate d = XDate_fromJulianDay(jd);
    r.m_isValid = XDate_isValid(&d);
    r.year = r.m_isValid ? XDate_year(&d) : 0;
    r.month = r.m_isValid ? XDate_month(&d) : 0;
    r.day = r.m_isValid ? XDate_day(&d) : 0;
    return r;
}

static XCalendarYearMonthDay xcal_julianFromJd(int64_t jd)
{
    /* qjuliancalendar.cpp:72-80 的闭式逆（等价 Meeus 儒略历逆变换）。 */
    XCalendarYearMonthDay r;
    int64_t c = jd + 32082;
    int64_t dd = xtz_fdiv(4 * c + 3, 1461);
    int64_t e = c - xtz_fdiv(1461 * dd, 4);
    int64_t m = xtz_fdiv(5 * e + 2, 153);
    int64_t day = e - xtz_fdiv(153 * m + 2, 5) + 1;
    int64_t month = m + 3 - 12 * xtz_fdiv(m, 10);
    int64_t year = dd - 4800 + xtz_fdiv(m, 10);
    r.year = (int)year;
    r.month = (int)month;
    r.day = (int)day;
    /* 上界口径（对齐 Qt 取「该 JDN 能被本库承载」而非硬抄 9999）：
     * Qt 的 QJulianCalendar 允许 10000/20000/50000 年（探针：Julian
     * 10000-1-2 jd=5373559 valid=1），但本库 XDate 只承载公历 1..9999
     * （XDate.h:12-15 + XDate_isValidDate），JDN 上限 5373484。
     * 原实现硬写 `year <= 9999` ⇒ 把 jd=5373484（公历 9999-12-31）
     * 报成 9999-10-19（真实儒略年≈10000），把最后两个多月折叠回 9999
     * （实测复核 #13）。现在改为：只要该 JDN 落在 XDate 可承载区间，
     * 就如实返回儒略年年月日（可为 10000），与 Qt 同口径。 */
    r.m_isValid = (jd >= 0 && jd <= 5373484LL && year >= 1);
    return r;
}

static XCalendarYearMonthDay xcal_milankovicFromJd(int64_t jd)
{
    /* qmilankoviccalendar.cpp:87-96。 */
    XCalendarYearMonthDay r;
    int64_t a = 9 * (jd - 1721119) - 7;
    int64_t c9q = xtz_fdiv(a, XCAL_NINE_CENTURIES);
    int64_t c9r = xtz_fmod(a, XCAL_NINE_CENTURIES);
    int64_t b = 100 * xtz_fdiv(c9r, 9) + 99;
    int64_t y100q = xtz_fdiv(b, XCAL_LEAP_CENTURY);
    int64_t y100r = xtz_fmod(b, XCAL_LEAP_CENTURY);
    int64_t yy, mm, dd, y;
    xcal_dayInYearToYmd(xtz_fdiv(y100r, 100), &yy, &mm, &dd);
    y = 100 * c9q + y100q + yy;
    r.year = (int)((y > 0) ? y : y - 1);
    r.month = (int)mm;
    r.day = (int)dd;
    r.m_isValid = (y >= 1 && y <= 99999);
    return r;
}

static XCalendarYearMonthDay xcal_islamicFromJd(int64_t jd)
{
    /* qislamiccivilcalendar.cpp:82-91。 */
    XCalendarYearMonthDay r;
    int64_t a = 30 * (jd - XCAL_ISLAMIC_EPOCH) + 15;
    int64_t y30q = xtz_fdiv(a, XCAL_THIRTY_YEARS);
    int64_t y30r = xtz_fmod(a, XCAL_THIRTY_YEARS);
    int64_t bb = 11 * xtz_fdiv(y30r, 30) + 5;
    int64_t m11q = xtz_fdiv(bb, XCAL_ELEVEN_MONTHS);
    int64_t m11r = xtz_fmod(bb, XCAL_ELEVEN_MONTHS);
    int64_t month = m11q + 1;
    int64_t day = xtz_fdiv(m11r, 11) + 1;
    int64_t y = y30q + 1;
    r.year = (int)((y > 0) ? y : y - 1);
    r.month = (int)month;
    r.day = (int)day;
    r.m_isValid = (y >= 1 && y <= 99999);
    return r;
}

/* ---- 各历法：闰年 / 月天数 / 年天数 ---- */

static bool xcal_leapGregorian(int year)
{
    return XDate_isLeapYear_static(year);
}

static bool xcal_leapJulian(int year)
{
    int y = (year > 0) ? year : year + 1;
    return (y % 4) == 0;
}

static bool xcal_leapMilankovic(int year)
{
    /* qmilankoviccalendar.cpp:49-64。 */
    int y = (year > 0) ? year : year + 1;
    int64_t cyq, cyr;
    if ((y % 4) != 0) return false;
    cyq = xtz_fdiv(y, 100);
    cyr = xtz_fmod(y, 100);
    if (cyr == 0) {
        int64_t century = xtz_fmod(cyq, 9);
        if (century != 2 && century != 6) return false;
    }
    return true;
}

static bool xcal_leapIslamic(int year)
{
    /* qislamiccivilcalendar.cpp:54-59：mod(year*11+14,30) < 11。 */
    int64_t y = (year > 0) ? year : year + 1;
    return xtz_fmod(y * 11 + 14, 30) < 11;
}

static bool xcal_isLeap(const XCalendar* cal, int year)
{
    switch (cal->m_system) {
    case XCalendarSystem_Gregorian:    return xcal_leapGregorian(year);
    case XCalendarSystem_Julian:       return xcal_leapJulian(year);
    case XCalendarSystem_Milankovic:   return xcal_leapMilankovic(year);
    case XCalendarSystem_IslamicCivil: return xcal_leapIslamic(year);
    default: return false;
    }
}

static int xcal_daysInMonthRoman(const XCalendar* cal, int month, int year)
{
    static const int s_common[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 0;
    if (month == 2 && xcal_isLeap(cal, year)) return 29;
    return s_common[month - 1];
}

static int xcal_daysInMonthIslamic(const XCalendar* cal, int month, int year)
{
    /* 伊斯兰历前 11 月 30/29 交替，末月闰年 30 天；Qt 表格口径。 */
    (void)cal;
    if (month < 1 || month > 12) return 0;
    if (month == 12) return xcal_leapIslamic(year) ? 30 : 29;
    return (month % 2) ? 30 : 29;
}

/* ==================== 构造 / 有效性 ==================== */

XCalendar XCalendar_create(void)
{
    XCalendar cal;
    cal.m_system = (int)XCalendarSystem_Gregorian;
    return cal;
}

XCalendar XCalendar_create_system(int system)
{
    XCalendar cal;
    cal.m_system = system;
    if (!XCalendar_isValid(&cal))
        cal.m_system = (int)XCalendarSystem_Invalid;
    return cal;
}

XCalendar XCalendar_create_name(const char* utf8Name)
{
    XCalendar cal;
    cal.m_system = (int)XCalendarSystem_Invalid;
    if (!utf8Name) return cal;
    /* 大小写不敏感的四个名字（XStrcasecmp 已归一大小写；名单对齐
     * Qt nameList：qgregoriancalendar.cpp:52-58 / qislamiccivilcalendar.cpp:43-49
     * 的 CLDR 别名，匹配口径 qcalendar.cpp:33 CaseInsensitive）。 */
    if (XStrcasecmp(utf8Name, "Gregorian") == 0 ||
        XStrcasecmp(utf8Name, "gregory") == 0)
        cal.m_system = (int)XCalendarSystem_Gregorian;
    else if (XStrcasecmp(utf8Name, "Julian") == 0)
        cal.m_system = (int)XCalendarSystem_Julian;
    else if (XStrcasecmp(utf8Name, "Milankovic") == 0)
        cal.m_system = (int)XCalendarSystem_Milankovic;
    else if (XStrcasecmp(utf8Name, "Islamic Civil") == 0 ||
             XStrcasecmp(utf8Name, "IslamicCivil") == 0 ||
             XStrcasecmp(utf8Name, "islamic-civil") == 0 ||
             XStrcasecmp(utf8Name, "islamicc") == 0 ||
             XStrcasecmp(utf8Name, "Islamic") == 0)
        cal.m_system = (int)XCalendarSystem_IslamicCivil;
    return cal;
}

bool XCalendar_isValid(const XCalendar* cal)
{
    if (!cal) return false;
    switch (cal->m_system) {
    case XCalendarSystem_Gregorian:
    case XCalendarSystem_Julian:
    case XCalendarSystem_Milankovic:
    case XCalendarSystem_IslamicCivil:
        return true;
    default:
        return false;
    }
}

int XCalendar_system(const XCalendar* cal)
{
    return cal ? cal->m_system : (int)XCalendarSystem_Invalid;
}

const char* XCalendar_name(const XCalendar* cal)
{
    if (!XCalendar_isValid(cal)) return "";
    switch (cal->m_system) {
    case XCalendarSystem_Gregorian:    return "Gregorian";
    case XCalendarSystem_Julian:       return "Julian";
    case XCalendarSystem_Milankovic:   return "Milankovic";
    case XCalendarSystem_IslamicCivil: return "Islamic Civil";
    default: return "";
    }
}

/* ==================== 属性 ==================== */

bool XCalendar_isLeapYear(const XCalendar* cal, int year)
{
    return XCalendar_isValid(cal) ? xcal_isLeap(cal, year) : false;
}

int XCalendar_daysInMonth(const XCalendar* cal, int month, int year)
{
    if (!XCalendar_isValid(cal)) return 0;
    /* Qt 探针：daysInMonth(2,0) / daysInMonth(1,0) 均返回 0
     * （QRomanCalendar::daysInMonth 首行 `if (!year) return 0`）。 */
    if (year == 0) return 0;
    if (cal->m_system == (int)XCalendarSystem_IslamicCivil)
        return xcal_daysInMonthIslamic(cal, month, year);
    return xcal_daysInMonthRoman(cal, month, year);
}

int XCalendar_daysInYear(const XCalendar* cal, int year)
{
    int m, total = 0;
    if (!XCalendar_isValid(cal)) return 0;
    for (m = 1; m <= 12; ++m) {
        int d = XCalendar_daysInMonth(cal, m, year);
        if (d <= 0) return 0;
        total += d;
    }
    return total;
}

int XCalendar_monthsInYear(const XCalendar* cal, int year)
{
    /* Qt 探针：monthsInYear(0) 对四种历法全部返回 0（QCalendar::monthsInYear
     * 在 year==Unspecified/0 时返回 0）。原实现恒 12。 */
    if (!XCalendar_isValid(cal) || year == 0) return 0;
    return 12;
}

bool XCalendar_isDateValid(const XCalendar* cal, int year, int month, int day)
{
    int dim;
    if (!XCalendar_isValid(cal) || year < 1) return false;
    if (cal->m_system == (int)XCalendarSystem_Gregorian)
        return XDate_isValid_static(year, month, day);
    dim = XCalendar_daysInMonth(cal, month, year);
    if (!(dim > 0 && day >= 1 && day <= dim)) return false;
    /* 上界自洽（复核 #11）：XDate 只承载公历 1..9999（JDN ≤ 5373484），
     * 而 Islamic 9700 年等已超出该 JDN 上限。原实现 isDateValid 只查
     * `year>=1` 与月天数、dateFromParts 却按 jd>5373484 返回无效 ⇒
     * 「isDateValid 通过后拿到的仍是无效 XDate」，调用方按有效分支走会
     * 拿到 m_jd=-1。现在两处同口径：先把 parts 折算成 JDN，再判承载。 */
    {
        int64_t jd = -1;
        switch (cal->m_system) {
        case XCalendarSystem_Julian:       jd = xcal_jdFromJulian(year, month, day); break;
        case XCalendarSystem_Milankovic:   jd = xcal_jdFromMilankovic(year, month, day); break;
        case XCalendarSystem_IslamicCivil: jd = xcal_jdFromIslamicCivil(year, month, day); break;
        default: break;
        }
        if (jd < 0 || jd > 5373484LL) return false;
    }
    return true;
}

bool XCalendar_isGregorian(const XCalendar* cal)
{
    return XCalendar_isValid(cal) &&
           cal->m_system == (int)XCalendarSystem_Gregorian;
}

bool XCalendar_isLunar(const XCalendar* cal)
{
    return XCalendar_isValid(cal) &&
           cal->m_system == (int)XCalendarSystem_IslamicCivil;
}

bool XCalendar_isLuniSolar(const XCalendar* cal)
{
    (void)cal;
    return false;
}

bool XCalendar_isSolar(const XCalendar* cal)
{
    return XCalendar_isValid(cal) &&
           cal->m_system != (int)XCalendarSystem_IslamicCivil;
}

bool XCalendar_isProleptic(const XCalendar* cal)
{
    return XCalendar_isValid(cal);
}

bool XCalendar_hasYearZero(const XCalendar* cal)
{
    (void)cal;
    return false;
}

int XCalendar_maximumDaysInMonth(const XCalendar* cal)
{
    /* Qt 探针：Gregorian/Julian/Milankovic = 31，IslamicCivil = 30
     * （qhijricalendar.cpp:70-73 / qcalendar.cpp 基类）。原实现恒 31。 */
    if (!XCalendar_isValid(cal)) return 0;
    return (cal->m_system == (int)XCalendarSystem_IslamicCivil) ? 30 : 31;
}

int XCalendar_minimumDaysInMonth(const XCalendar* cal)
{
    /* Qt 探针：Gregorian/Julian/Milankovic = 28，IslamicCivil = 29。
     * 原实现恒 28。 */
    if (!XCalendar_isValid(cal)) return 0;
    return (cal->m_system == (int)XCalendarSystem_IslamicCivil) ? 29 : 28;
}

int XCalendar_maximumMonthsInYear(const XCalendar* cal)
{
    return XCalendar_isValid(cal) ? 12 : 0;
}

/* ==================== 日期换算 ==================== */

XDate XCalendar_dateFromParts(const XCalendar* cal, int year, int month, int day)
{
    int64_t jd = -1;
    if (!XCalendar_isDateValid(cal, year, month, day))
        return XDate_create();
    switch (cal->m_system) {
    case XCalendarSystem_Gregorian:
        jd = xcal_jdFromGregorian(year, month, day); break;
    case XCalendarSystem_Julian:
        jd = xcal_jdFromJulian(year, month, day); break;
    case XCalendarSystem_Milankovic:
        jd = xcal_jdFromMilankovic(year, month, day); break;
    case XCalendarSystem_IslamicCivil:
        jd = xcal_jdFromIslamicCivil(year, month, day); break;
    default: break;
    }
    /* XDate 只承载 1..9999 公历范围：越界（如伊斯兰历 > ~9400 年）按无效返回。 */
    if (jd < 0 || jd > 5373484LL) return XDate_create();
    return XDate_fromJulianDay(jd);
}

XCalendarYearMonthDay XCalendar_partsFromDate(const XCalendar* cal,
                                              const XDate* date)
{
    XCalendarYearMonthDay r;
    int64_t jd;
    r.year = 0; r.month = 0; r.day = 0; r.m_isValid = false;
    if (!XCalendar_isValid(cal) || !date || !XDate_isValid(date)) return r;
    jd = XDate_toJulianDay(date);
    switch (cal->m_system) {
    case XCalendarSystem_Gregorian:
        r = xcal_gregorianFromJd(jd); break;
    case XCalendarSystem_Julian:
        r = xcal_julianFromJd(jd); break;
    case XCalendarSystem_Milankovic:
        r = xcal_milankovicFromJd(jd); break;
    case XCalendarSystem_IslamicCivil:
        r = xcal_islamicFromJd(jd); break;
    default: break;
    }
    return r;
}

int XCalendar_dayOfWeek(const XCalendar* cal, const XDate* date)
{
    /* Qt 探针：无效历法下 QCalendar::dayOfWeek 返回 0（原实现无条件转发
     * XDate_dayOfWeek，无效历法也给出 1..7）。 */
    if (!XCalendar_isValid(cal)) return 0;
    /* 铁律 4：星期几是绝对日属性，直接转发既有 XDate。 */
    return XDate_dayOfWeek(date);
}
