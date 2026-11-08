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
 * @note  自 2026-11-05 起与 XDateTime_currentMSecsSinceEpoch **同锚同源**：
 *        桌面端由同一个单调源（QPC / CLOCK_MONOTONIC）推进，因此
 *        `currentNSecsSinceEpoch() / 1000000 == currentMSecsSinceEpoch()`
 *        严格成立（事件分发器把高精度定时器截止与时间轮截止合并比较，
 *        依赖这条恒等式）。裸机后端仍由 ms × 1e6 派生。
 * @return 当前的纳秒时间戳。
 */
int64_t XDateTime_currentNSecsSinceEpoch(void);
/**
 * @brief 获取"当前时刻"的毫秒读数（**Unix 纪元毫秒，单调推进**）。
 * @return 自 Unix 纪元 (1970-01-01T00:00:00 UTC) 以来的毫秒数。
 *
 * @note [口径 —— 四平台约定，用之前必读]
 *       自 2026-11-05 起四平台统一为 **Unix 纪元毫秒**，语义对齐 Qt 的
 *       QDateTime::currentMSecsSinceEpoch()：
 *         · Windows：QPC 推进 + 首调时的墙钟基线（取不到 QPC 退回
 *           GetTickCount64）；
 *         · POSIX/Linux：CLOCK_MONOTONIC 推进 + 首调时的 CLOCK_REALTIME
 *           基线；
 *         · ESP8266/STM32 等无 RTC 的裸机：仍是上电/调度启动起的单调
 *           毫秒（拿不到纪元基准，**零点为 0**），但框架时间轮已用
 *           冷启动对齐门吸收这个落差（见 XTimeWheelGroup handler 注记）。
 *
 *       [为什么这样定] 本函数是**全库统一的计时源**：绝大多数调用方拿它
 *       算**差值**（定时器轮、IO 超时、限流、设备数据新鲜度），所以必须
 *       单调（不能被 NTP 校时跳变）；同时又常被当成"某件事发生的时刻"直接
 *       打戳（XFlowMeter::m_lastChangeMs、XTempHum 的 m_lastUpdateMs、
 *       设备健康 lastOkMs…），所以零点不能是"进程/开机"而在 0 附近——
 *       取到 0 会被误判成"没有时刻"（xindustry-flowmeter 的「收数后有时刻」
 *       断言曾因此 1/5~1/3 概率间歇失败）。
 *
 *       [纪元锚只取一次] 锚点在首次调用时确定，之后纯单调推进。系统时钟
 *       若此后才被 NTP 校准，本函数的绝对值仍沿用旧锚（依然单调、依然远
 *       大于 0，但会与墙钟有固定偏差）。需要真实墙钟时刻请用
 *       XDateTime_currentWallMSecsSinceEpoch / currentDateTime。
 *
 *       [正确用法]
 *         · 测时长：两次调用相减（跨平台安全）；
 *         · 判"有没有收到过数据"：优先用业务对象自己的标志（如
 *           XFlowMeter_lastUpdateMs/XTempHum_lastUpdateMs 的 bool 返回值），
 *           桌面端现在 `ms > 0` 也成立，但裸机后端的零点仍是 0，别依赖它。
 */
int64_t XDateTime_currentMSecsSinceEpoch(void);

/**
 * @brief 获取自 Unix 纪元 (1970-01-01T00:00:00 UTC) 以来的当前毫秒数（墙钟口径）。
 * @return 当前的墙钟毫秒时间戳（含亚秒精度）。
 * @note  与 XDateTime_currentMSecsSinceEpoch 的区别**不在零点**（两者都落在
 *        Unix 纪元刻度上），而在**是否跟随校时**：本函数直读系统实时时钟
 *        （POSIX CLOCK_REALTIME / Windows 系统时间），NTP 校时后立刻反映；
 *        后者用单调源推进，校时不影响它。要"真实 UTC 时刻"用它，要"时长"
 *        用后者。
 * @note  精度高于秒级截断的 currentSecsSinceEpoch，供需要「真实 UTC 时刻 +
 *        亚秒精度」的场景使用，典型如 NTP 客户端构造与测量时间戳。
 *        durations/超时测量仍必须使用单调钟接口。
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
 * @warning 调整系统时钟会使依赖墙钟的时间戳发生跳变。注意本模块的
 *          XDateTime_currentMSecsSinceEpoch 用单调源推进（只在首调时取
 *          一次纪元锚），校时不会影响它，因此定时器、心跳、超时测量不受
 *          影响；但依赖墙钟的业务（日志、文件时间戳、证书有效期校验）会
 *          看到时间跳变。
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
 * @note  字段字母与 Qt 的 QDateTime::toString 同义：y/yy/yyyy（年）、
 *        M/MM（月）、d/dd（日）、H/HH（时）、m/mm（分）、s/ss（秒）。
 *        多字母 token 补前导零（"MM" ⇒ "05"），单字母 token 不补
 *        （"M" ⇒ "5"）——日期时间轴的刻度标签依赖这条定宽语义。
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
