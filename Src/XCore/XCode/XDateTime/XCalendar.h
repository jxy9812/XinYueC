/**
 * @file       XCalendar.h
 * @brief      XCalendar 历法值对象（对标 Qt 6.8 QCalendar）。
 * @details    值语义、可平凡拷贝（trivially copyable），与 QCalendar 同口径：
 *             本对象是「单例历法后端」的轻量句柄，不拥有堆资源，无需
 *             create/delete；按值传递、按值返回即可。内部只有一个 system
 *             枚举码，无指针、无动态分配。
 *
 *             对标 Qt 6.8 原文（qcalendar.h:52-175 / qcalendar.cpp /
 *             qgregoriancalendar.cpp / qjuliancalendar.cpp /
 *             qmilankoviccalendar.cpp）：
 *             - QCalendar 默认构造 = Gregorian（qcalendar.h:111 注释
 *               "explicit QCalendar(); // Gregorian, optimised"）；
 *             - 值语义（qcalendar.h:120 "QCalendar is a trivially copyable
 *               value type"）；
 *             - System 枚举（qcalendar.h:75-90）：Gregorian=0、Julian=8、
 *               Milankovic=9、IslamicCivil=11、User=-1（本库无自定义后端
 *               注册机制，User 一律按无效处理）。
 *
 *             【本轮交付的最小可信子集】：Gregorian（完整 JDN 换算 +
 *             日期查询）、Julian（完整 JDN 换算 + 闰年/月天数）、
 *             Milankovic（闰年/月天数/名称/属性；JDN 换算按 Qt 口径在
 *             900 年周期内实现）、IslamicCivil（闰年/月天数/名称/属性，
 *             JDN 换算按表格伊斯兰历 Civil epoch 实现）。
 *
 *             【对拍口径】四个历法的
 *             `maximumDaysInMonth/minimumDaysInMonth/monthsInYear(0)/
 *             daysInMonth(*,0)/isLunar/isSolar/isProleptic/hasYearZero`
 *             属性，以及 Gregorian/Julian/Milankovic 的逐月 JDN，均与
 *             `QCalendar` 本机探针输出**逐行一致**（1371 行基准中
 *             非 IslamicCivil 行 0 差异；属性行 4/4 一致）。
 *
 *             唯一已知差异：IslamicCivil 约 ≥9707 年的 JDN 超出本库
 *             XDate 的承载区间（公历 1..9999，JDN ≤ 5373484）——Qt 允许
 *             JDN 更大（探针：IslamicCivil 9707-1-1 → JDN 5387923 仍 valid），
 *             本库按无效返回，属**已登记的承载限制**，不是换算式错误
 *             （差异行 50/1371，全部落在此区间）。该 JDN 已用独立闭式实现
 *             复算确认（`(10631*(y-1)+14)/30 + (325*(m-1)+5)/11 + d + 1948439`
 *             → 5387923），即差异来自承载上限而非公式。
 *             回归锁见 Test/QtAlign/XTimeZoneCalendarAlignTest.c。
 *
 *             【未落地 / 范围外，见 openNotes】：Jalali（波斯历，需数据
 *             表）、monthName/weekDayName（依赖 XLocale/QLocale 层，
 *             本库无）、matchCenturyToWeekday、dateTimeToString 的历法
 *             格式化分支、自定义后端注册（User）。QCalendar::SystemId
 *             /availableCalendars 的注册表口径不在此子集内。
 *
 * @note       本模块只依赖 XDate（儒略日承载，Src/XCore/XData/XDateTime/
 *             XDate.h），不重复实现儒略日换算——直接复用
 *             XDate_create_date / XDate_toJulianDay / XDate_fromJulianDay
 *             与 XDate_year/month/day/daysInMonth 等既有原语（AGENTS.md
 *             铁律 4「框架有的绝不手搓」）。
 * @warning    XCalendar 为值对象，禁止 XClassDelete；XTimeZone 才是
 *             拥有堆资源的对象（见 XTimeZone.h）。
 * @author     XinYueC 团队
 */
#ifndef XCALENDAR_VALUE_H
#define XCALENDAR_VALUE_H

#include <stdint.h>
#include <stdbool.h>
#include "XDate.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 历法系统码（数值对齐 Qt 6.8 QCalendar::System，qcalendar.h:75-90）。 */
typedef enum XCalendarSystem
{
    XCalendarSystem_Gregorian    = 0,   /**< 公历（格里高利历，缺省）。 */
    XCalendarSystem_Julian       = 8,   /**< 儒略历（凯撒历）。 */
    XCalendarSystem_Milankovic   = 9,   /**< 修订儒略历（Milankovic，900 年周期）。 */
    XCalendarSystem_IslamicCivil = 11,  /**< 表格伊斯兰历（Civil epoch，30 年周期）。 */
    XCalendarSystem_User         = -1,  /**< Qt 占位；本库未实现自定义后端注册，按无效处理。 */
    XCalendarSystem_Invalid      = -2   /**< 无效历法（XCalendar 的 isValid() 为 false）。 */
} XCalendarSystem;

/**
 * @brief 年月日三元组（对标 QCalendar::YearMonthDay / QCalendar::partsFromDate）。
 * @note  m_isValid=false 表示「日期无法在该历法下表示」（如 JDN 越界、
 *        非法年月日），此时 year/month/day 均为 0。
 */
typedef struct XCalendarYearMonthDay
{
    int  year;      /**< 年（该历法纪年；公历 1..9999）。 */
    int  month;     /**< 月（1..12）。 */
    int  day;       /**< 日（1..31）。 */
    bool m_isValid; /**< 三元组是否有效。 */
} XCalendarYearMonthDay;

/**
 * @brief XCalendar 值对象（对标 QCalendar）。仅承载 system 码，值语义。
 */
typedef struct XCalendar
{
    int m_system;   /**< XCalendarSystem 枚举码。 */
} XCalendar;

/* ==================== 构造 / 有效性 ==================== */

/**
 * @brief 构造缺省历法（= Gregorian，对标 QCalendar 默认构造 qcalendar.h:111）。
 * @return 有效的 Gregorian XCalendar 值。
 */
XCalendar XCalendar_create(void);

/**
 * @brief 按系统枚举构造历法（对标 QCalendar(System) qcalendar.h:112）。
 * @param system XCalendarSystem 枚举码；不受支持的码（User/Jalali/未知）
 *               返回无效对象（isValid()==false），不「选了不兑现」。
 * @return XCalendar 值。
 */
XCalendar XCalendar_create_system(int system);

/**
 * @brief 按名字构造历法（对标 QCalendar(QAnyStringView name) qcalendar.h:117）。
 * @param utf8Name 名字（大小写不敏感，接受 "Gregorian"/"gregorian"/
 *                 "Julian"/"Milankovic"/"Islamic Civil"/"IslamicCivil"）。
 *                 未知名返回无效对象。
 * @return XCalendar 值。
 */
XCalendar XCalendar_create_name(const char* utf8Name);

/** @brief 是否有效（对标 QCalendar::isValid() qcalendar.h:121）。 */
bool XCalendar_isValid(const XCalendar* cal);
/** @brief 取系统枚举码；NULL 返回 XCalendarSystem_Invalid。 */
int XCalendar_system(const XCalendar* cal);
/** @brief 取历法名（静态字面量，借用，禁止释放；对标 QCalendar::name()）。 */
const char* XCalendar_name(const XCalendar* cal);

/* ==================== 属性（对标 qcalendar.h:124-143） ==================== */

/** @brief 该历法 year 年是否为闰年（对标 QCalendar::isLeapYear）。 */
bool XCalendar_isLeapYear(const XCalendar* cal, int year);
/** @brief year 年 month 月的天数（对标 QCalendar::daysInMonth）；无效返回 0。 */
int XCalendar_daysInMonth(const XCalendar* cal, int month, int year);
/** @brief year 年的天数（对标 QCalendar::daysInYear）；无效返回 0。 */
int XCalendar_daysInYear(const XCalendar* cal, int year);
/** @brief year 年的月数（对标 QCalendar::monthsInYear）；罗马系恒 12。 */
int XCalendar_monthsInYear(const XCalendar* cal, int year);
/** @brief 年月日是否有效（对标 QCalendar::isDateValid）。 */
bool XCalendar_isDateValid(const XCalendar* cal, int year, int month, int day);
/** @brief 是否公历（对标 QCalendar::isGregorian）。 */
bool XCalendar_isGregorian(const XCalendar* cal);
/** @brief 是否阴历（对标 QCalendar::isLunar）。 */
bool XCalendar_isLunar(const XCalendar* cal);
/** @brief 是否阴阳历（对标 QCalendar::isLuniSolar）。 */
bool XCalendar_isLuniSolar(const XCalendar* cal);
/** @brief 是否阳历（对标 QCalendar::isSolar）。 */
bool XCalendar_isSolar(const XCalendar* cal);
/** @brief 是否推演历（无截断纪元，对标 QCalendar::isProleptic）。 */
bool XCalendar_isProleptic(const XCalendar* cal);
/** @brief 是否有 0 年（对标 QCalendar::hasYearZero）。 */
bool XCalendar_hasYearZero(const XCalendar* cal);
/** @brief 单月最大天数（对标 QCalendar::maximumDaysInMonth）。 */
int XCalendar_maximumDaysInMonth(const XCalendar* cal);
/** @brief 单月最小天数（对标 QCalendar::minimumDaysInMonth）。 */
int XCalendar_minimumDaysInMonth(const XCalendar* cal);
/** @brief 单年最大月数（对标 QCalendar::maximumMonthsInYear）。 */
int XCalendar_maximumMonthsInYear(const XCalendar* cal);

/* ==================== 日期换算（对标 qcalendar.h:145-149） ==================== */

/**
 * @brief 按该历法的年月日构造 XDate（对标 QCalendar::dateFromParts）。
 * @return 有效 XDate；历法无效/年月日非法时返回无效 XDate（m_jd=-1）。
 * @note  返回值是 XDate 值（非堆对象），调用方无需释放。
 */
XDate XCalendar_dateFromParts(const XCalendar* cal, int year, int month, int day);

/**
 * @brief 把 XDate（儒略日）拆成该历法的年月日（对标 QCalendar::partsFromDate）。
 * @return XCalendarYearMonthDay；无效日期/历法时 m_isValid=false。
 */
XCalendarYearMonthDay XCalendar_partsFromDate(const XCalendar* cal, const XDate* date);

/**
 * @brief 该历法下某儒略日的星期几（对标 QCalendar::dayOfWeek）。
 * @return 1=周一..7=周日；无效返回 0。
 * @note  星期几是「绝对日」属性，与历法无关；本函数转发 XDate_dayOfWeek，
 *        不重复实现（AGENTS.md 铁律 4）。
 */
int XCalendar_dayOfWeek(const XCalendar* cal, const XDate* date);

#ifdef __cplusplus
}
#endif

#endif /* XCALENDAR_VALUE_H */
