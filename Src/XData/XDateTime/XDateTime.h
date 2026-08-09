#ifndef XDATETIME_H
#define XDATETIME_H

#include <stdint.h>
typedef struct XVariant XVariant;
#include <stdbool.h>
#include "XDate.h"
#include "XTime.h"
#include "XString.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 日期时间结构体，组合了 XDate 和 XTime。
 */
typedef struct XDateTime {
    XDate m_date; /**< 日期部分 */
    XTime m_time; /**< 时间部分 */
} XDateTime;

/**
 * @brief 创建一个空的 XDateTime 对象。
 * @return 空的 XDateTime 对象（日期和时间均无效）。
 */
XDateTime XDateTime_create(void);

/**
 * @brief 创建一个指定日期和时间的 XDateTime 对象。
 * @param date XDate 对象。
 * @param time XTime 对象。
 * @return 如果日期和时间都有效则返回对应的 XDateTime 对象，否则返回无效对象。
 */
XDateTime XDateTime_create_datetime(XDate date, XTime time);

/**
 * @brief 获取当前本地日期和时间。
 * @return 表示当前日期时间的 XDateTime 对象，失败时返回无效对象。
 */
XDateTime XDateTime_currentDateTime(void);
/**
 * @brief 获取当前的 UTC 日期和时间。
 * @return 表示当前 UTC 日期时间的 XDateTime 对象，失败时返回无效对象。
 */
XDateTime XDateTime_currentDateTimeUtc(void);
/**
 * @brief 获取自 Unix 纪元 (1970-01-01T00:00:00 UTC) 以来的当前纳秒数。
 * @note  实际精度取决于硬件，通常为 100-纳秒级别，但返回值以纳秒为单位。
 * @return 当前的纳秒时间戳。
 */
int64_t XDateTime_currentNSecsSinceEpoch(void);
/**
 * @brief 获取自 Unix 纪元 (1970-01-01T00:00:00 UTC) 以来的当前毫秒数。
 * @return 当前的毫秒时间戳。
 */
int64_t XDateTime_currentMSecsSinceEpoch(void);

/**
 * @brief 获取自 Unix 纪元 (1970-01-01T00:00:00 UTC) 以来的当前秒数。
 * @return 当前的秒时间戳。
 */
int64_t XDateTime_currentSecsSinceEpoch(void);
/**
 * @brief 检查日期时间对象是否为空（未初始化）。
 * @param datetime 要检查的 XDateTime 对象。
 * @return 如果日期时间为空则返回 true，否则返回 false。
 */
bool XDateTime_isNull(const XDateTime* datetime);

/**
 * @brief 检查日期时间对象是否有效。
 * @param datetime 要检查的 XDateTime 对象。
 * @return 如果日期时间和时间都有效则返回 true，否则返回 false。
 */
bool XDateTime_isValid(const XDateTime* datetime);

/**
 * @brief 获取日期部分。
 * @param datetime XDateTime 对象指针。
 * @return 日期部分（XDate 对象）。
 */
XDate XDateTime_date(const XDateTime* datetime);

/**
 * @brief 获取时间部分。
 * @param datetime XDateTime 对象指针。
 * @return 时间部分（XTime 对象）。
 */
XTime XDateTime_time(const XDateTime* datetime);

/**
 * @brief 转换为自 Unix 纪元 (1970-01-01T00:00:00 UTC) 以来的毫秒数。
 * @param datetime XDateTime 对象指针。
 * @return 毫秒时间戳，如果日期时间无效则返回 0。
 */
int64_t XDateTime_toMSecsSinceEpoch(const XDateTime* datetime);

/**
 * @brief 转换为自 Unix 纪元 (1970-01-01T00:00:00 UTC) 以来的秒数。
 * @return 秒时间戳，如果日期时间无效则返回 0。
 */
int64_t XDateTime_toSecsSinceEpoch(const XDateTime* datetime);

/**
 * @brief 从毫秒时间戳设置日期时间。
 * @param datetime XDateTime 对象指针。
 * @param msecs 自 Unix 纪元以来的毫秒数。
 * @return 如果设置成功则返回 true，否则返回 false。
 */
bool XDateTime_setMSecsSinceEpoch(XDateTime* datetime, int64_t msecs);

/**
 * @brief 从秒时间戳设置日期时间。
 * @param datetime XDateTime 对象指针。
 * @param secs 自 Unix 纪元以来的秒数。
 * @return 如果设置成功则返回 true，否则返回 false。
 */
bool XDateTime_setSecsSinceEpoch(XDateTime* datetime, int64_t secs);

/**
 * @brief 设置日期部分。
 * @param datetime XDateTime 对象指针。
 * @param date XDate 对象。
 * @return 如果设置成功则返回 true，否则返回 false（日期无效）。
 */
bool XDateTime_setDate(XDateTime* datetime, XDate date);

/**
 * @brief 设置时间部分。
 * @param datetime XDateTime 对象指针。
 * @param time XTime 对象。
 * @return 如果设置成功则返回 true，否则返回 false（时间无效）。
 */
bool XDateTime_setTime(XDateTime* datetime, XTime time);

/**
 * @brief 在日期时间上增加指定天数。
 * @param datetime XDateTime 对象指针。
 * @param days 要增加的天数（可为负数）。
 * @return 新的 XDateTime 对象，如果输入无效则返回无效对象。
 */
XDateTime XDateTime_addDays(const XDateTime* datetime, int64_t days);

/**
 * @brief 在日期时间上增加指定月数。
 * @param datetime XDateTime 对象指针。
 * @param months 要增加的月数（可为负数）。
 * @return 新的 XDateTime 对象，如果输入无效则返回无效对象。
 */
XDateTime XDateTime_addMonths(const XDateTime* datetime, int months);

/**
 * @brief 在日期时间上增加指定年数。
 * @param datetime XDateTime 对象指针。
 * @param years 要增加的年数（可为负数）。
 * @return 新的 XDateTime 对象，如果输入无效则返回无效对象。
 */
XDateTime XDateTime_addYears(const XDateTime* datetime, int years);

/**
 * @brief 在日期时间上增加指定秒数。
 * @param datetime XDateTime 对象指针。
 * @param secs 要增加的秒数（可为负数）。
 * @return 新的 XDateTime 对象，如果输入无效则返回无效对象。
 */
XDateTime XDateTime_addSecs(const XDateTime* datetime, int64_t secs);

/**
 * @brief 在日期时间上增加指定毫秒数。
 * @param datetime XDateTime 对象指针。
 * @param msecs 要增加的毫秒数（可为负数）。
 * @return 新的 XDateTime 对象，如果输入无效则返回无效对象。
 */
XDateTime XDateTime_addMSecs(const XDateTime* datetime, int64_t msecs);

/**
 * @brief 将日期时间格式化为 C 字符串。
 * @param datetime XDateTime 对象指针。
 * @param format 格式字符串（如 "yyyy-MM-dd HH:mm:ss"）。
 * @return 指向新 XString 对象的指针，失败时返回 NULL。
 */
XString* XDateTime_toString_format(const XDateTime* datetime, const char* format);

/**
 * @brief 将日期时间格式化为 ISO 标准字符串 ("yyyy-MM-dd HH:mm:ss")。
 * @param datetime XDateTime 对象指针。
 * @return 指向新 XString 对象的指针，失败时返回 NULL。
 */
XString* XDateTime_toString_iso(const XDateTime* datetime);

/**
 * @brief 从格式化字符串解析日期时间。
 * @param str 要解析的字符串。
 * @param format 格式字符串（如 "yyyy-MM-dd HH:mm:ss"）。
 * @return 如果解析成功则返回对应的 XDateTime 对象，否则返回无效对象。
 */
XDateTime XDateTime_fromString_format(const char* str, const char* format);

/**
 * @brief 从 ISO 标准字符串 ("yyyy-MM-dd HH:mm:ss") 解析日期时间。
 * @param str 要解析的字符串。
 * @return 如果解析成功则返回对应的 XDateTime 对象，否则返回无效对象。
 */
XDateTime XDateTime_fromString_iso(const char* str);

/**
 * @brief 计算两个日期时间之间的天数差。
 * @param from 起始日期时间。
 * @param to 结束日期时间。
 * @return 从 from 到 to 的天数差（to - from）。
 */
int64_t XDateTime_daysTo(const XDateTime* from, const XDateTime* to);

/**
 * @brief 计算两个日期时间之间的秒数差。
 * @param from 起始日期时间。
 * @param to 结束日期时间。
 * @return 从 from 到 to 的秒数差（to - from）。
 */
int64_t XDateTime_secsTo(const XDateTime* from, const XDateTime* to);

/**
 * @brief 计算两个日期时间之间的毫秒数差。
 * @param from 起始日期时间。
 * @param to 结束日期时间。
 * @return 从 from 到 to 的毫秒数差（to - from）。
 */
int64_t XDateTime_msecsTo(const XDateTime* from, const XDateTime* to);

/**
 * @brief 将日期时间重置为无效日期时间。
 * @param datetime 待清理的日期时间对象，可为 NULL。
 */
void XDateTime_clear(XDateTime* datetime);

/**
 * @brief 比较两个日期时间对象。
 * @param lhs 左侧日期时间对象，可为 NULL。
 * @param rhs 右侧日期时间对象，可为 NULL。
 * @return 按 XCompare 约定返回比较结果。
 */
int32_t XDateTime_compare(const XDateTime* lhs, const XDateTime* rhs);

/** @brief 深复制创建存储 XDateTime 的 XVariant。 */
XVariant* XDateTime_toVariant(const XDateTime* datetime);
/** @brief 从同类型 XVariant 取得 XDateTime 值副本。 */
XDateTime XDateTime_fromVariant(const XVariant* variant);
/** @brief 从同类型 XVariant 借用取得 XDateTime 指针。 */
XDateTime* XDateTime_fromVariant_ref(const XVariant* variant);
/** @brief 设置 XVariant 的 XDateTime 值；datetime 为 NULL 时设置无效值。 */
void XDateTime_setVariant(XVariant* variant, const XDateTime* datetime);

/**
 * @brief 兼容旧的 XVariant XDateTime 扩展 API 名称。
 * @details 以下宏仅保留源代码兼容性，实际实现均归属 XDateTime。
 */
#define XVariant_create_DateTime    XDateTime_toVariant
#define XVariant_toDateTime          XDateTime_fromVariant
#define XVariant_toDateTime_ref      XDateTime_fromVariant_ref
#define XVariant_setValue_DateTime   XDateTime_setVariant

#ifdef __cplusplus
}
#endif

#endif // XDATETIME_H
