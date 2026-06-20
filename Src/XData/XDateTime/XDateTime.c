#include "XDateTime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
/**
 * @brief （内部使用）获取自午夜以来的毫秒数。
 * @param time XTime 对象指针。
 * @return 自午夜以来的毫秒数。
 */
int XTime_msecsSinceStartOfDay(const XTime* time);

/**
 * @brief （内部使用）从自午夜以来的毫秒数创建 XTime 对象。
 * @param msecs 自午夜以来的毫秒数。
 * @return 对应的 XTime 对象。
 */
XTime XTime_fromMSecsSinceStartOfDay(int msecs);

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

// Unix epoch in Julian Day
#define UNIX_EPOCH_JD 2440588LL // 1970-01-01

// 公共接口实现
XDateTime XDateTime_create(void) {
    XDateTime dt;
    dt.m_date = XDate_create();
    dt.m_time = XTime_create();
    return dt;
}

XDateTime XDateTime_create_datetime(XDate date, XTime time) {
    if (XDate_isNull(&date) || XTime_isNull(&time)) {
        return XDateTime_create();
    }
    XDateTime dt;
    dt.m_date = date;
    dt.m_time = time;
    return dt;
}

//XDateTime XDateTime_currentDateTime(void) 
//{
//    XDateTime_create_datetime(XDate_currentDate(), XTime_currentTime());
//}
bool XDateTime_isNull(const XDateTime* datetime) {
    return datetime == NULL ||
        (XDate_isNull(&datetime->m_date) && XTime_isNull(&datetime->m_time));
}

bool XDateTime_isValid(const XDateTime* datetime) {
    return datetime != NULL &&
        XDate_isValid(&datetime->m_date) && XTime_isValid(&datetime->m_time);
}

XDate XDateTime_date(const XDateTime* datetime) {
    if (!datetime) return XDate_create();
    return datetime->m_date;
}

XTime XDateTime_time(const XDateTime* datetime) {
    if (!datetime) return XTime_create();
    return datetime->m_time;
}

int64_t XDateTime_toMSecsSinceEpoch(const XDateTime* datetime) {
    if (!XDateTime_isValid(datetime)) return 0;
    int64_t days_since_epoch = datetime->m_date.m_jd - UNIX_EPOCH_JD;
    return days_since_epoch * 86400000LL + datetime->m_time.m_msecs;
}

int64_t XDateTime_toSecsSinceEpoch(const XDateTime* datetime) {
    return XDateTime_toMSecsSinceEpoch(datetime) / 1000;
}

bool XDateTime_setMSecsSinceEpoch(XDateTime* datetime, int64_t msecs) {
    if (!datetime) return false;
    int64_t days = msecs / 86400000LL;
    int64_t ms_in_day = msecs % 86400000LL;
    if (ms_in_day < 0) {
        ms_in_day += 86400000LL;
        days--;
    }
    XDate date = XDate_fromJulianDay(UNIX_EPOCH_JD + days);
    XTime time = XTime_fromMSecsSinceStartOfDay((int)ms_in_day);
    if (XDate_isNull(&date) || XTime_isNull(&time)) {
        *datetime = XDateTime_create();
        return false;
    }
    datetime->m_date = date;
    datetime->m_time = time;
    return true;
}

bool XDateTime_setSecsSinceEpoch(XDateTime* datetime, int64_t secs) {
    return XDateTime_setMSecsSinceEpoch(datetime, secs * 1000);
}

bool XDateTime_setDate(XDateTime* datetime, XDate date) {
    if (!datetime || XDate_isNull(&date)) return false;
    datetime->m_date = date;
    return true;
}

bool XDateTime_setTime(XDateTime* datetime, XTime time) {
    if (!datetime || XTime_isNull(&time)) return false;
    datetime->m_time = time;
    return true;
}

XDateTime XDateTime_addDays(const XDateTime* datetime, int64_t days) {
    if (!XDateTime_isValid(datetime)) return XDateTime_create();
    XDateTime result;
    result.m_date = XDate_addDays(&datetime->m_date, days);
    result.m_time = datetime->m_time;
    return result;
}

XDateTime XDateTime_addMonths(const XDateTime* datetime, int months) {
    if (!XDateTime_isValid(datetime)) return XDateTime_create();
    XDateTime result;
    result.m_date = XDate_addMonths(&datetime->m_date, months);
    result.m_time = datetime->m_time;
    return result;
}

XDateTime XDateTime_addYears(const XDateTime* datetime, int years) {
    if (!XDateTime_isValid(datetime)) return XDateTime_create();
    XDateTime result;
    result.m_date = XDate_addYears(&datetime->m_date, years);
    result.m_time = datetime->m_time;
    return result;
}

XDateTime XDateTime_addSecs(const XDateTime* datetime, int64_t secs) {
    if (!XDateTime_isValid(datetime)) return XDateTime_create();
    int64_t total_msecs = (int64_t)datetime->m_time.m_msecs + secs * 1000;
    int64_t days_to_add = total_msecs / 86400000LL;
    int ms_in_day = (int)(total_msecs % 86400000LL);
    if (ms_in_day < 0) {
        ms_in_day += 86400000;
        days_to_add--;
    }
    XDateTime result;
    result.m_date = XDate_addDays(&datetime->m_date, days_to_add);
    result.m_time = XTime_fromMSecsSinceStartOfDay(ms_in_day);
    return result;
}

XDateTime XDateTime_addMSecs(const XDateTime* datetime, int64_t msecs) {
    if (!XDateTime_isValid(datetime)) return XDateTime_create();
    int64_t total_msecs = (int64_t)datetime->m_time.m_msecs + msecs;
    int64_t days_to_add = total_msecs / 86400000LL;
    int ms_in_day = (int)(total_msecs % 86400000LL);
    if (ms_in_day < 0) {
        ms_in_day += 86400000;
        days_to_add--;
    }
    XDateTime result;
    result.m_date = XDate_addDays(&datetime->m_date, days_to_add);
    result.m_time = XTime_fromMSecsSinceStartOfDay(ms_in_day);
    return result;
}

XString* XDateTime_toString_format(const XDateTime* datetime, const char* format) {
    if (!XDateTime_isValid(datetime) || !format)
        return NULL;

    XString* result = XString_create();
    if (!result) {
        return NULL;
    }
    // --- 优化：在循环外创建一次临时 XString ---
    XString* temp_num_str = XString_create();
    if (!temp_num_str) {
        XString_delete_base(result);
        return NULL;
    }
    int year = XDate_year(&datetime->m_date);
    int month = XDate_month(&datetime->m_date);
    int day = XDate_day(&datetime->m_date);
    int hour = XTime_hour(&datetime->m_time);
    int minute = XTime_minute(&datetime->m_time);
    int second = XTime_second(&datetime->m_time);

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
                XString_setNum_int(temp_num_str, year, 10);
            }
            else {
                XString_setNum_int(temp_num_str, year % 100, 10);
            }
            XString_append(result, temp_num_str);
            // -----------------------

            i = j - 1;
        }
        else if (format[i] == 'M') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 'M') {
                count++;
                j++;
            }

            XString_setNum_int(temp_num_str, month, 10);
            XString_append(result, temp_num_str);
            // -----------------------

            i = j - 1;
        }
        else if (format[i] == 'd') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 'd') {
                count++;
                j++;
            }

            XString_setNum_int(temp_num_str, day, 10);
            XString_append(result, temp_num_str);
            // -----------------------

            i = j - 1;
        }
        else if (format[i] == 'H') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 'H') {
                count++;
                j++;
            }

            XString_setNum_int(temp_num_str, hour, 10);
            XString_append(result, temp_num_str);
            // -----------------------

            i = j - 1;
        }
        else if (format[i] == 'm') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 'm') {
                count++;
                j++;
            }

            XString_setNum_int(temp_num_str, minute, 10);
            XString_append(result, temp_num_str);
            // -----------------------

            i = j - 1;
        }
        else if (format[i] == 's') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 's') {
                count++;
                j++;
            }

            XString_setNum_int(temp_num_str, second, 10);
           
            XString_append(result, temp_num_str);
            //XPrintf_2(result);
            // -----------------------

            i = j - 1;
        }
        else {
            // 普通字符，直接追加
            XChar ch = (uint16_t)format[i];
            //XString_push_back_base(result, ch);
            XString_append_char(result, ch);
        }
     /*   XPrintf_2(temp_num_str);
        printf("\n");
        XPrintf_2(result);
          printf("\n");*/
    }

    // --- 优化：在函数末尾统一销毁临时对象 ---
    XString_delete_base(temp_num_str);
    // -------------------------------------------

    return result;
}

XString* XDateTime_toString_iso(const XDateTime* datetime) {
    return XDateTime_toString_format(datetime, "yyyy-MM-dd HH:mm:ss");
}
XDateTime XDateTime_fromString_format(const char* str, const char* format) {
    if (!str || !format) return XDateTime_create();
    // 仅支持 ISO 格式
    return XDateTime_fromString_iso(str);
}

XDateTime XDateTime_fromString_iso(const char* str) {
    if (!str) return XDateTime_create();
    char date_part[11], time_part[9];
    if (sscanf(str, "%10s %8s", date_part, time_part) != 2) {
        return XDateTime_create();
    }
    XDate date = XDate_fromString_iso(date_part);
    XTime time = XTime_fromString_format(time_part, "HH:mm:ss");
    return XDateTime_create_datetime(date, time);
}

int64_t XDateTime_daysTo(const XDateTime* from, const XDateTime* to) {
    if (!XDateTime_isValid(from) || !XDateTime_isValid(to)) return 0;
    return XDate_daysTo(&from->m_date, &to->m_date);
}

int64_t XDateTime_secsTo(const XDateTime* from, const XDateTime* to) {
    if (!XDateTime_isValid(from) || !XDateTime_isValid(to)) return 0;
    int64_t days_diff = XDate_daysTo(&from->m_date, &to->m_date);
    int time_diff = XTime_secsTo(&from->m_time, &to->m_time);
    return days_diff * 86400LL + time_diff;
}

int64_t XDateTime_msecsTo(const XDateTime* from, const XDateTime* to) {
    if (!XDateTime_isValid(from) || !XDateTime_isValid(to)) return 0;
    int64_t days_diff = XDate_daysTo(&from->m_date, &to->m_date);
    int time_diff = XTime_msecsTo(&from->m_time, &to->m_time);
    return days_diff * 86400000LL + time_diff;
}