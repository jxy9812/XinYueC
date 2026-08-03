#ifndef XTIME_H
#define XTIME_H

#include <stdint.h>
typedef struct XVariant XVariant;
#include <stdbool.h>
#include "XString.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 时间结构体，内部使用自午夜以来的毫秒数表示。
 */
typedef struct XTime {
    int m_msecs; /**< 自午夜以来的毫秒数 (-1 表示无效) */
} XTime;

/**
 * @brief 创建一个空的 XTime 对象。
 * @return 空的 XTime 对象（m_msecs = -1）。
 */
XTime XTime_create(void);

/**
 * @brief 创建一个指定时间的 XTime 对象。
 * @param hour 小时 (0-23)。
 * @param minute 分钟 (0-59)。
 * @param second 秒 (0-59)。
 * @param msec 毫秒 (0-999)。
 * @return 如果时间有效则返回对应的 XTime 对象，否则返回无效对象（m_msecs = -1）。
 */
XTime XTime_create_time(int hour, int minute, int second, int msec);

/**
 * @brief 获取当前本地时间。
 * @return 表示当前时间的 XTime 对象，失败时返回无效对象。
 */
XTime XTime_currentTime(void);

/**
 * @brief 检查时间对象是否为空（未初始化）。
 * @param time 要检查的 XTime 对象。
 * @return 如果时间为空则返回 true，否则返回 false。
 */
bool XTime_isNull(const XTime* time);

/**
 * @brief 检查时间对象是否有效。
 * @param time 要检查的 XTime 对象。
 * @return 如果时间有效则返回 true，否则返回 false。
 */
bool XTime_isValid(const XTime* time);

/**
 * @brief 获取小时。
 * @param time XTime 对象指针。
 * @return 小时 (0-23)，如果时间无效则返回 -1。
 */
int XTime_hour(const XTime* time);

/**
 * @brief 获取分钟。
 * @param time XTime 对象指针。
 * @return 分钟 (0-59)，如果时间无效则返回 -1。
 */
int XTime_minute(const XTime* time);

/**
 * @brief 获取秒。
 * @param time XTime 对象指针。
 * @return 秒 (0-59)，如果时间无效则返回 -1。
 */
int XTime_second(const XTime* time);

/**
 * @brief 获取毫秒。
 * @param time XTime 对象指针。
 * @return 毫秒 (0-999)，如果时间无效则返回 -1。
 */
int XTime_msec(const XTime* time);

/**
 * @brief 设置时间。
 * @param time XTime 对象指针。
 * @param hour 小时。
 * @param minute 分钟。
 * @param second 秒。
 * @param msec 毫秒。
 * @return 如果设置成功则返回 true，否则返回 false（时间无效）。
 */
bool XTime_setHMS(XTime* time, int hour, int minute, int second, int msec);

/**
 * @brief 在时间上增加指定秒数。
 * @param time XTime 对象指针。
 * @param secs 要增加的秒数（可为负数）。
 * @return 新的 XTime 对象，如果输入无效则返回无效对象。
 */
XTime XTime_addSecs(const XTime* time, int secs);

/**
 * @brief 在时间上增加指定毫秒数。
 * @param time XTime 对象指针。
 * @param msecs 要增加的毫秒数（可为负数）。
 * @return 新的 XTime 对象，如果输入无效则返回无效对象。
 */
XTime XTime_addMSecs(const XTime* time, int msecs);

/**
 * @brief 计算两个时间之间的秒数差。
 * @param from 起始时间。
 * @param to 结束时间。
 * @return 从 from 到 to 的秒数差（to - from）。
 */
int XTime_secsTo(const XTime* from, const XTime* to);

/**
 * @brief 计算两个时间之间的毫秒数差。
 * @param from 起始时间。
 * @param to 结束时间。
 * @return 从 from 到 to 的毫秒数差（to - from）。
 */
int XTime_msecsTo(const XTime* from, const XTime* to);

/**
 * @brief 将时间格式化为 C 字符串。
 * @param time XTime 对象指针。
 * @param format 格式字符串（如 "HH:mm:ss"）。
 * @return 指向新 XString 对象的指针，失败时返回 NULL。
 */
XString* XTime_toString_format(const XTime* time, const char* format);

/**
 * @brief 从格式化字符串解析时间。
 * @param str 要解析的字符串。
 * @param format 格式字符串（如 "HH:mm:ss"）。
 * @return 如果解析成功则返回对应的 XTime 对象，否则返回无效对象。
 */
XTime XTime_fromString_format(const char* str, const char* format);

/**
 * @brief 静态方法：检查给定的时、分、秒、毫秒是否构成有效时间。
 * @param hour 小时。
 * @param minute 分钟。
 * @param second 秒。
 * @param msec 毫秒。
 * @return 如果是有效时间则返回 true，否则返回 false。
 */
bool XTime_isValid_static(int hour, int minute, int second, int msec);

/**
 * @brief 将时间重置为无效时间。
 * @param time 待清理的时间对象，可为 NULL。
 */
void XTime_clear(XTime* time);

/**
 * @brief 比较两个时间对象。
 * @param lhs 左侧时间对象，可为 NULL。
 * @param rhs 右侧时间对象，可为 NULL。
 * @return 按 XCompare 约定返回比较结果。
 */
int32_t XTime_compare(const XTime* lhs, const XTime* rhs);

/** @brief 深复制创建存储 XTime 的 XVariant。 */
XVariant* XTime_toVariant(const XTime* time);
/** @brief 从同类型 XVariant 取得 XTime 值副本。 */
XTime XTime_fromVariant(const XVariant* variant);
/** @brief 从同类型 XVariant 借用取得 XTime 指针。 */
XTime* XTime_fromVariant_ref(const XVariant* variant);
/** @brief 设置 XVariant 的 XTime 值；time 为 NULL 时设置无效时间。 */
void XTime_setVariant(XVariant* variant, const XTime* time);

/**
 * @brief 兼容旧的 XVariant XTime 扩展 API 名称。
 * @details 以下宏仅保留源代码兼容性，实际实现均归属 XTime。
 */
#define XVariant_create_Time    XTime_toVariant
#define XVariant_toTime         XTime_fromVariant
#define XVariant_toTime_ref     XTime_fromVariant_ref
#define XVariant_setValue_Time  XTime_setVariant

#ifdef __cplusplus
}
#endif

#endif // XTIME_H
