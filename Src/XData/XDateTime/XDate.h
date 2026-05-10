#ifndef XDATE_H
#define XDATE_H

#include <stdint.h>
#include <stdbool.h>
#include "XString.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 日期结构体，内部使用儒略日(Julian Day)表示。
 */
typedef struct XDate {
    int64_t m_jd; /**< 儒略日 (Julian Day)，-1 表示无效 */
} XDate;

/**
 * @brief 创建一个空的 XDate 对象。
 * @return 空的 XDate 对象（m_jd = -1）。
 */
XDate XDate_create(void);

/**
 * @brief 创建一个指定日期的 XDate 对象。
 * @param year 年份 (1-9999)。
 * @param month 月份 (1-12)。
 * @param day 日期 (1-31)。
 * @return 如果日期有效则返回对应的 XDate 对象，否则返回无效对象（m_jd = -1）。
 */
XDate XDate_create_date(int year, int month, int day);

/**
 * @brief 获取当前本地日期。
 * @return 表示当前日期的 XDate 对象，失败时返回无效对象。
 */
XDate XDate_currentDate(void);

/**
 * @brief 检查日期对象是否为空（未初始化）。
 * @param date 要检查的 XDate 对象。
 * @return 如果日期为空则返回 true，否则返回 false。
 */
bool XDate_isNull(const XDate* date);

/**
 * @brief 检查日期对象是否有效。
 * @param date 要检查的 XDate 对象。
 * @return 如果日期有效则返回 true，否则返回 false。
 */
bool XDate_isValid(const XDate* date);

/**
 * @brief 获取年份。
 * @param date XDate 对象指针。
 * @return 年份 (1-9999)，如果日期无效则返回 0。
 */
int XDate_year(const XDate* date);

/**
 * @brief 获取月份。
 * @param date XDate 对象指针。
 * @return 月份 (1-12)，如果日期无效则返回 0。
 */
int XDate_month(const XDate* date);

/**
 * @brief 获取日期（月份中的第几天）。
 * @param date XDate 对象指针。
 * @return 日期 (1-31)，如果日期无效则返回 0。
 */
int XDate_day(const XDate* date);

/**
 * @brief 获取星期几。
 * @param date XDate 对象指针。
 * @return 星期几 (1=Monday, 7=Sunday)，如果日期无效则返回 0。
 */
int XDate_dayOfWeek(const XDate* date);

/**
 * @brief 获取一年中的第几天。
 * @param date XDate 对象指针。
 * @return 一年中的第几天 (1-366)，如果日期无效则返回 0。
 */
int XDate_dayOfYear(const XDate* date);

/**
 * @brief 获取该月的总天数。
 * @param date XDate 对象指针。
 * @return 该月的总天数，如果日期无效则返回 0。
 */
int XDate_daysInMonth(const XDate* date);

/**
 * @brief 获取该年的总天数。
 * @return 该年的总天数 (365 或 366)，如果日期无效则返回 0。
 */
int XDate_daysInYear(const XDate* date);

/**
 * @brief 设置日期。
 * @param date XDate 对象指针。
 * @param year 年份。
 * @param month 月份。
 * @param day 日期。
 * @return 如果设置成功则返回 true，否则返回 false（日期无效）。
 */
bool XDate_setDate(XDate* date, int year, int month, int day);

/**
 * @brief 在日期上增加指定天数。
 * @param date XDate 对象指针。
 * @param days 要增加的天数（可为负数）。
 * @return 新的 XDate 对象，如果输入无效则返回无效对象。
 */
XDate XDate_addDays(const XDate* date, int64_t days);

/**
 * @brief 在日期上增加指定月数。
 * @param date XDate 对象指针。
 * @param months 要增加的月数（可为负数）。
 * @return 新的 XDate 对象，如果输入无效则返回无效对象。
 */
XDate XDate_addMonths(const XDate* date, int months);

/**
 * @brief 在日期上增加指定年数。
 * @param date XDate 对象指针。
 * @param years 要增加的年数（可为负数）。
 * @return 新的 XDate 对象，如果输入无效则返回无效对象。
 */
XDate XDate_addYears(const XDate* date, int years);

/**
 * @brief 计算两个日期之间的天数差。
 * @param from 起始日期。
 * @param to 结束日期。
 * @return 从 from 到 to 的天数差（to - from）。
 */
int64_t XDate_daysTo(const XDate* from, const XDate* to);

/**
 * @brief 将日期格式化为 C 字符串。
 * @param date XDate 对象指针。
 * @param format 格式字符串（如 "yyyy-MM-dd"）。
 * @return 格式化后的 C 字符串，调用者负责释放内存，失败时返回 NULL。
 */
XString* XDate_toString_format(const XDate* date, const char* format);

/**
 * @brief 将日期格式化为 ISO 标准字符串 ("yyyy-MM-dd")。
 * @param date XDate 对象指针。
 * @return 指向新 XString 对象的指针，失败时返回 NULL。
 */
XString* XDate_toString_iso(const XDate* date);

/**
 * @brief 从格式化字符串解析日期。
 * @param str 要解析的字符串。
 * @param format 格式字符串（如 "yyyy-MM-dd"）。
 * @return 如果解析成功则返回对应的 XDate 对象，否则返回无效对象。
 */
XDate XDate_fromString_format(const char* str, const char* format);

/**
 * @brief 从 ISO 标准字符串 ("yyyy-MM-dd") 解析日期。
 * @param str 要解析的字符串。
 * @return 如果解析成功则返回对应的 XDate 对象，否则返回无效对象。
 */
XDate XDate_fromString_iso(const char* str);

/**
 * @brief 静态方法：检查给定的年、月、日是否构成有效日期。
 * @param year 年份。
 * @param month 月份。
 * @param day 日期。
 * @return 如果是有效日期则返回 true，否则返回 false。
 */
bool XDate_isValid_static(int year, int month, int day);

/**
 * @brief 静态方法：判断指定年份是否为闰年。
 * @param year 年份。
 * @return 如果是闰年则返回 true，否则返回 false。
 */
bool XDate_isLeapYear_static(int year);



#ifdef __cplusplus
}
#endif

#endif // XDATE_H