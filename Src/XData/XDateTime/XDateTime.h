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
 * @brief 获取自 Unix 纪元 (1970-01-01T00:00:00 UTC) 以来的当前毫秒数（墙钟口径）。
 * @return 当前的墙钟毫秒时间戳（含亚秒精度）。
 * @note  与 XDateTime_currentMSecsSinceEpoch（单调时钟）不同，本函数读的是
 *        系统实时时钟（POSIX CLOCK_REALTIME / Windows 系统时间），会随校时
 *        发生跳变；但精度高于秒级截断的 currentSecsSinceEpoch。
 * @note  供需要「真实 UTC 时刻 + 亚秒精度」的场景使用，典型如 NTP 客户端
 *        构造与测量时间戳。 durations/超时测量仍必须使用单调钟接口。
 */
int64_t XDateTime_currentWallMSecsSinceEpoch(void);

/**
 * @brief 设置系统墙钟时间（影响整个操作系统的时间基准）。
 * @param secs 目标 Unix 纪元秒（UTC 时区下的时刻）。
 * @return 设置成功返回 true；权限不足、平台不支持或参数非法返回 false，
 *         此时系统时间保持不变。
 * @note  这是写操作，与本模块其余只读接口语义不同，仅供时间同步（如 NTP
 *        校时）这类必须改动系统时钟的场景使用；常规时间测量请保持只读。
 * @note  权限要求：POSIX 需 root 或 CAP_SYS_TIME；Windows 需
 *        SE_SYSTEMTIME_NAME 特权。可先用 XDateTime_canSetSystemTime 探测。
 * @warning 调整系统时钟会使所有依赖墙钟的时间戳发生跳变。注意本模块的
 *          XDateTime_currentMSecsSinceEpoch 使用单调时钟，校时不会影响它，
 *          因此定时器、心跳、超时测量不受影响；但依赖墙钟的业务（日志、
 *          文件时间戳、证书有效期校验）会看到时间跳变。
 */
bool XDateTime_setSystemSecsSinceEpoch(int64_t secs);

/**
 * @brief 检测当前进程是否有权设置系统时间。
 * @return 有权设置返回 true，否则返回 false。
 * @note  供调用方在依赖校时前先做能力探测：返回 false 时应退化为「只读取
 *        偏移量、由应用自行决策」，而不是反复尝试写入。
 */
bool XDateTime_canSetSystemTime(void);

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
