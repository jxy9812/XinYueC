#include "XDate.h"
#include "XVariantTypeOps.h"
#include "XVariant.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/**
 * @brief （内部使用）获取儒略日数值。
 * @param date XDate 对象指针。
 * @return 儒略日数值。
 */
int64_t XDate_toJulianDay(const XDate* date);

/**
 * @brief （内部使用）从儒略日创建 XDate 对象。
 * @param jd 儒略日数值。
 * @return 对应的 XDate 对象。
 */
XDate XDate_fromJulianDay(int64_t jd);

// 儒略日转换常量
static const int64_t JD_EPOCH_OFFSET = 1721426LL; // 1 Jan 1 AD in Julian calendar

// 辅助函数：判断闰年
static bool is_leap_year(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
// 内部函数：验证日期
bool is_valid_date(int year, int month, int day) {
    if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1)
        return false;
    return day <= days_in_month(year, month);
}
// 公共静态方法实现
bool XDate_isLeapYear_static(int year) {
    if (year < 1 || year > 9999) return false;
    return is_leap_year(year);
}

// 公共静态方法实现
bool XDate_isValid_static(int year, int month, int day) {
    return is_valid_date(year, month, day);
}



// 内部函数：获取月份天数
int days_in_month(int year, int month) {
    static const int days_per_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 0;
    int days = days_per_month[month - 1];
    if (month == 2 && is_leap_year(year))
        days++;
    return days;
}

// 内部函数：格里高利历转儒略日
int64_t gregorian_to_julian_day(int year, int month, int day) {
    if (!is_valid_date(year, month, day)) return -1;

    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;
    int64_t jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    return jd;
}

// 内部函数：儒略日转格里高利历
void julian_day_to_gregorian(int64_t jd, int* year, int* month, int* day) {
    if (jd < 0) {
        *year = 0; *month = 0; *day = 0;
        return;
    }
    int64_t a = jd + 32044;
    int64_t b = (4 * a + 3) / 146097;
    int64_t c = a - (146097 * b) / 4;
    int64_t d = (4 * c + 3) / 1461;
    int64_t e = c - (1461 * d) / 4;
    int64_t m = (5 * e + 2) / 153;
    *day = (int)(e - (153 * m + 2) / 5 + 1);
    *month = (int)(m + 3 - 12 * (m / 10));
    *year = (int)(100 * b + d - 4800 + m / 10);
}

// 公共接口实现
XDate XDate_create(void) {
    XDate date;
    date.m_jd = -1;
    return date;
}

XDate XDate_create_date(int year, int month, int day) {
    if (!is_valid_date(year, month, day)) {
        return XDate_create();
    }
    XDate date;
    date.m_jd = gregorian_to_julian_day(year, month, day);
    return date;
}

//XDate XDate_currentDate(void) {
//    return XDate_currentDate_platform();
//}

bool XDate_isNull(const XDate* date) {
    return date == NULL || date->m_jd == -1;
}

bool XDate_isValid(const XDate* date) {
    return date != NULL && date->m_jd >= 0;
}

int XDate_year(const XDate* date) {
    if (XDate_isNull(date)) return 0;
    int year, month, day;
    julian_day_to_gregorian(date->m_jd, &year, &month, &day);
    return year;
}

int XDate_month(const XDate* date) {
    if (XDate_isNull(date)) return 0;
    int year, month, day;
    julian_day_to_gregorian(date->m_jd, &year, &month, &day);
    return month;
}

int XDate_day(const XDate* date) {
    if (XDate_isNull(date)) return 0;
    int year, month, day;
    julian_day_to_gregorian(date->m_jd, &year, &month, &day);
    return day;
}

int XDate_dayOfWeek(const XDate* date) {
    if (XDate_isNull(date)) return 0;
    // 1 Jan 1 AD was a Monday (Julian Day 1721426)
    // Monday = 1, Sunday = 7
    return (int)((date->m_jd + 1) % 7) + 1;
}

int XDate_dayOfYear(const XDate* date) {
    if (XDate_isNull(date)) return 0;
    int year = XDate_year(date);
    XDate start_of_year = XDate_create_date(year, 1, 1);
    return (int)(date->m_jd - start_of_year.m_jd + 1);
}

int XDate_daysInMonth(const XDate* date) {
    if (XDate_isNull(date)) return 0;
    int year = XDate_year(date);
    int month = XDate_month(date);
    return days_in_month(year, month);
}

int XDate_daysInYear(const XDate* date) {
    if (XDate_isNull(date)) return 0;
    int year = XDate_year(date);
    return is_leap_year(year) ? 366 : 365;
}

bool XDate_setDate(XDate* date, int year, int month, int day) {
    if (!date) return false;
    XDate new_date = XDate_create_date(year, month, day);
    if (XDate_isNull(&new_date)) return false;
    *date = new_date;
    return true;
}

XDate XDate_addDays(const XDate* date, int64_t days) {
    if (XDate_isNull(date)) return XDate_create();
    XDate result;
    result.m_jd = date->m_jd + days;
    return result;
}

XDate XDate_addMonths(const XDate* date, int months) {
    if (XDate_isNull(date)) return XDate_create();

    int year = XDate_year(date);
    int month = XDate_month(date);
    int day = XDate_day(date);

    int total_months = year * 12 + month - 1 + months;
    year = total_months / 12;
    month = total_months % 12 + 1;

    // 调整日期以适应目标月份的天数
    int max_day = days_in_month(year, month);
    if (day > max_day) {
        day = max_day;
    }

    return XDate_create_date(year, month, day);
}

XDate XDate_addYears(const XDate* date, int years) {
    if (XDate_isNull(date)) return XDate_create();
    int year = XDate_year(date);
    int month = XDate_month(date);
    int day = XDate_day(date);
    year += years;
    return XDate_create_date(year, month, day);
}

int64_t XDate_daysTo(const XDate* from, const XDate* to) {
    if (XDate_isNull(from) || XDate_isNull(to)) return 0;
    return to->m_jd - from->m_jd;
}

// 格式化辅助函数
static void format_date_component(XString* str, int value, int width, char fill) {
    XString_resize(str, width);
    char* buf = (char*)XString_data(str);
    for (int i = 0; i < width; i++) {
        buf[i] = fill;
    }
    int pos = width - 1;
    if (value == 0) {
        buf[pos] = '0';
    }
    else {
        while (value > 0 && pos >= 0) {
            buf[pos--] = '0' + (value % 10);
            value /= 10;
        }
    }
}

XString* XDate_toString_format(const XDate* date, const char* format) {
    if (XDate_isNull(date) || !format) return NULL;
    XString* result = XString_create();

    int year = XDate_year(date);
    int month = XDate_month(date);
    int day = XDate_day(date);

    size_t len = strlen(format);
    for (size_t i = 0; i < len; i++) {
        if (format[i] == 'y') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 'y') {
                count++;
                j++;
            }
            if (count >= 4) {
                XString_setNum_int(result, year, 10);
            }
            else {
                XString_setNum_int(result, year % 100, 10);
            }
            i = j - 1;
        }
        else if (format[i] == 'M') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 'M') {
                count++;
                j++;
            }
            if (count >= 3) {
                // 月份名称，此处简化为数字
                XString_setNum_int(result, month, 10);
            }
            else {
                XString_setNum_int(result, month, 10);
            }
            i = j - 1;
        }
        else if (format[i] == 'd') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 'd') {
                count++;
                j++;
            }
            XString_setNum_int(result, day, 10);
            i = j - 1;
        }
        else {
            XChar ch = (uint16_t)format[i];
            XString_push_back_base(result, ch);
        }
    }
    return result;
}
XString* XDate_toString_iso(const XDate* date) {
    return XDate_toString_format(date, "yyyy-MM-dd");
}

XDate XDate_fromString_format(const char* str, const char* format) {
    // 此处为简化实现，仅支持 "yyyy-MM-dd" 格式
    if (!str || !format) return XDate_create();
    return XDate_fromString_iso(str);
}

XDate XDate_fromString_iso(const char* str) {
    if (!str) return XDate_create();
    int year, month, day;
    if (sscanf(str, "%d-%d-%d", &year, &month, &day) != 3) {
        return XDate_create();
    }
    return XDate_create_date(year, month, day);
}

int64_t XDate_toJulianDay(const XDate* date) {
    if (XDate_isNull(date)) return -1;
    return date->m_jd;
}

XDate XDate_fromJulianDay(int64_t jd) {
    XDate date;
    date.m_jd = jd;
    if (jd < 0) {
        date.m_jd = -1;
    }
    return date;
}

void XDate_clear(XDate* date)
{
    if (date) *date = XDate_create();
}

int32_t XDate_compare(const XDate* lhs, const XDate* rhs)
{
    if (!lhs || !rhs) return XCompare_Other;
    return int64_t_compare(&((const XDate*)lhs)->m_jd,
                           &((const XDate*)rhs)->m_jd);
}

XVARIANT_TYPE_OPS_DEFINE(XDate, sizeof(XDate), NULL, NULL, XDate_clear, NULL,
	XDate_compare, "XDate");

XVariant* XDate_toVariant(const XDate* date)
{
    return date ? XVariant_create((void*)date, sizeof(XDate), XVariantType_Date) : NULL;
}

XDate XDate_fromVariant(const XVariant* variant)
{
    XDate date = XDate_create();
    XDate* source = XDate_fromVariant_ref(variant);
    if (source)
        date = *source;
    return date;
}

XDate* XDate_fromVariant_ref(const XVariant* variant)
{
    return (XDate*)XVariant_toRef(variant, XVariantType_Date);
}

void XDate_setVariant(XVariant* variant, const XDate* date)
{
    XDate invalid = XDate_create();
    if (!variant)
        return;
    if (variant->m_type != XVariantType_Date || !variant->m_data ||
        variant->m_dataSize != sizeof(XDate)) {
        if (variant->m_data)
            XVariant_deinit_base(variant);
        variant->m_data = XMalloc_System(sizeof(XDate));
        if (!variant->m_data)
            return;
        variant->m_dataSize = sizeof(XDate);
        variant->m_type = XVariantType_Date;
    }
    *(XDate*)variant->m_data = date ? *date : invalid;
}
