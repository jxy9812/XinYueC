#include "XTime.h"
#include "XVariantTypeOps.h"
#include "XVariant.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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

// 内部函数：验证时间
bool is_valid_time(int hour, int minute, int second, int msec) {
    return (hour >= 0 && hour <= 23) &&
        (minute >= 0 && minute <= 59) &&
        (second >= 0 && second <= 59) &&
        (msec >= 0 && msec <= 999);
}

// 公共静态方法实现
bool XTime_isValid_static(int hour, int minute, int second, int msec) {
    return is_valid_time(hour, minute, second, msec);
}

// 公共接口实现
XTime XTime_create(void) {
    XTime time;
    time.m_msecs = -1;
    return time;
}

XTime XTime_create_time(int hour, int minute, int second, int msec) {
    if (!is_valid_time(hour, minute, second, msec)) {
        return XTime_create();
    }
    XTime time;
    time.m_msecs = hour * 3600000 + minute * 60000 + second * 1000 + msec;
    return time;
}

//XTime XTime_currentTime(void) {
//    return XTime_currentTime_platform();
//}

bool XTime_isNull(const XTime* time) {
    return time == NULL || time->m_msecs == -1;
}

bool XTime_isValid(const XTime* time) {
    return time != NULL && time->m_msecs >= 0;
}

int XTime_hour(const XTime* time) {
    if (XTime_isNull(time)) return -1;
    return time->m_msecs / 3600000;
}

int XTime_minute(const XTime* time) {
    if (XTime_isNull(time)) return -1;
    return (time->m_msecs % 3600000) / 60000;
}

int XTime_second(const XTime* time) {
    if (XTime_isNull(time)) return -1;
    return (time->m_msecs % 60000) / 1000;
}

int XTime_msec(const XTime* time) {
    if (XTime_isNull(time)) return -1;
    return time->m_msecs % 1000;
}

bool XTime_setHMS(XTime* time, int hour, int minute, int second, int msec) {
    if (!time) return false;
    XTime new_time = XTime_create_time(hour, minute, second, msec);
    if (XTime_isNull(&new_time)) return false;
    *time = new_time;
    return true;
}

XTime XTime_addSecs(const XTime* time, int secs) {
    if (XTime_isNull(time)) return XTime_create();
    int64_t total_msecs = (int64_t)time->m_msecs + (int64_t)secs * 1000;
    // 处理溢出，循环到一天内
    total_msecs %= 86400000; // 24*60*60*1000
    if (total_msecs < 0) total_msecs += 86400000;
    XTime result;
    result.m_msecs = (int)total_msecs;
    return result;
}

XTime XTime_addMSecs(const XTime* time, int msecs) {
    if (XTime_isNull(time)) return XTime_create();
    int64_t total_msecs = (int64_t)time->m_msecs + (int64_t)msecs;
    total_msecs %= 86400000;
    if (total_msecs < 0) total_msecs += 86400000;
    XTime result;
    result.m_msecs = (int)total_msecs;
    return result;
}

int XTime_secsTo(const XTime* from, const XTime* to) {
    if (XTime_isNull(from) || XTime_isNull(to)) return 0;
    return (to->m_msecs - from->m_msecs) / 1000;
}

int XTime_msecsTo(const XTime* from, const XTime* to) {
    if (XTime_isNull(from) || XTime_isNull(to)) return 0;
    return to->m_msecs - from->m_msecs;
}

// 格式化辅助函数
static void format_time_component(XString* str, int value, int width) {
    XString_resize(str, width);
    char* buf = (char*)XString_data(str);
    buf[0] = '0' + (value / 10);
    buf[1] = '0' + (value % 10);
}

XString* XTime_toString_format(const XTime* time, const char* format) {
    if (XTime_isNull(time) || !format) return NULL;

    XString* result = XString_create();

    int hour = XTime_hour(time);
    int minute = XTime_minute(time);
    int second = XTime_second(time);
    int msec = XTime_msec(time);

    size_t len = strlen(format);
    for (size_t i = 0; i < len; i++) {
        if (format[i] == 'H') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 'H') {
                count++;
                j++;
            }
            if (count >= 2) {
                XString_setNum_int(result, hour, 10);
            }
            else {
                XString_setNum_int(result, hour, 10);
            }
            i = j - 1;
        }
        else if (format[i] == 'm') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 'm') {
                count++;
                j++;
            }
            XString_setNum_int(result, minute, 10);
            i = j - 1;
        }
        else if (format[i] == 's') {
            int count = 0;
            size_t j = i;
            while (j < len && format[j] == 's') {
                count++;
                j++;
            }
            XString_setNum_int(result, second, 10);
            i = j - 1;
        }
        else if (format[i] == 'z') {
            // 毫秒
            XString_setNum_int(result, msec, 10);
        }
        else {
            //XChar ch = { .code = (uint16_t)format[i] };
            XChar ch = (uint16_t)format[i];
            XString_push_back_base(result, ch);
        }
    }
    return result;
}

XTime XTime_fromString_format(const char* str, const char* format) {
    const char* fraction;
    int hour, minute, second;
    int msec = 0;
    int digits = 0;
    int fractionValue = 0;
    if (!str || !format) return XTime_create();
    if (sscanf(str, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return XTime_create();
    }
    fraction = strchr(str, '.');
    if (fraction) {
        ++fraction;
        while (isdigit((unsigned char)fraction[digits]) && digits < 6) {
            fractionValue = fractionValue * 10 + fraction[digits] - '0';
            ++digits;
        }
        while (digits < 3) {
            fractionValue *= 10;
            ++digits;
        }
        msec = fractionValue / (digits > 3 ? 1000 : 1);
    }
    return XTime_create_time(hour, minute, second, msec);
}

int XTime_msecsSinceStartOfDay(const XTime* time) {
    if (XTime_isNull(time)) return -1;
    return time->m_msecs;
}

XTime XTime_fromMSecsSinceStartOfDay(int msecs) {
    // Normalize msecs to [0, 86399999]
    msecs %= 86400000;
    if (msecs < 0) msecs += 86400000;
    XTime time;
    time.m_msecs = msecs;
    return time;
}

void XTime_clear(XTime* time)
{
    if (time) *time = XTime_create();
}

int32_t XTime_compare(const XTime* lhs, const XTime* rhs)
{
    if (!lhs || !rhs) return XCompare_Other;
    return int_compare(&((const XTime*)lhs)->m_msecs,
                       &((const XTime*)rhs)->m_msecs);
}

XVARIANT_TYPE_OPS_DEFINE(XTime, sizeof(XTime), NULL, NULL, XTime_clear, NULL,
	XTime_compare, "XTime");

XVariant* XTime_toVariant(const XTime* time)
{
    return time ? XVariant_create((void*)time, sizeof(XTime), XVariantType_Time) : NULL;
}

XTime XTime_fromVariant(const XVariant* variant)
{
    XTime time = XTime_create();
    XTime* source = XTime_fromVariant_ref(variant);
    if (source)
        time = *source;
    return time;
}

XTime* XTime_fromVariant_ref(const XVariant* variant)
{
    return (XTime*)XVariant_toRef(variant, XVariantType_Time);
}

void XTime_setVariant(XVariant* variant, const XTime* time)
{
    XTime invalid = XTime_create();
    if (!variant)
        return;
    if (variant->m_type != XVariantType_Time || !variant->m_data ||
        variant->m_dataSize != sizeof(XTime)) {
        if (variant->m_data)
            XVariant_deinit_base(variant);
        variant->m_data = XMalloc_System(sizeof(XTime));
        if (!variant->m_data)
            return;
        variant->m_dataSize = sizeof(XTime);
        variant->m_type = XVariantType_Time;
    }
    *(XTime*)variant->m_data = time ? *time : invalid;
}
