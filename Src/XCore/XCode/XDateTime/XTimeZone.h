/**
 * @file       XTimeZone.h
 * @brief      XTimeZone 时区值对象（对标 Qt 6.8 QTimeZone）。
 * @details    对标 Qt 6.8 原文（qtimezone.h / qtimezone.cpp /
 *             qtimezoneprivate_tz.cpp / qtimezoneprivate.cpp）：
 *             - qtimezone.h:25-247 QTimeZone 公共 API（IANA 构造、固定偏移
 *               构造、offsetFromUtc/standardTimeOffset/daylightTimeOffset/
 *               isDaylightTime/abbreviation/id/isValid 等）；
 *             - qtimezone.cpp:418-470 默认构造 = 无效、QTimeZone(int) = 固定
 *               偏移（范围 ±16h）、QTimeZone(ianaId) 经系统后端解析；
 *             - qtimezoneprivate_tz.cpp:150-410 TZif v2/v3 解析（transitions/
 *               types/abbreviations + POSIX 规则外推）；
 *             - qtimezoneprivate.cpp:705-738 isValidId（IANA 命名规则）。
 *
 *             【所有权契约（AGENTS.md 铁律 4/8）】
 *             - UTC（spec=UTC）、固定偏移（spec=OffsetFromUTC）为**值对象**，
 *               内部无堆资源，按值传递/返回，无需 create/delete（与 Qt 的
 *               ShortData 语义一致，qtimezone.h:83 QTimeZone(ShortData)）；
 *             - IANA 时区（spec=TimeZone，读 /usr/share/zoneinfo 或内建 UTC
 *               表）为**拥有堆资源**的句柄：由 XTimeZone_create_iana /
 *               XTimeZone_create_iana_path 创建，**必须** XTimeZone_release
 *               释放（对应类型带 _ref/_move 三态）。LocalTime 本库无系统
 *               后端，降级为 UTC 同口径（零偏移、abbr="Local"，见 openNotes）。
 *
 *             【本轮交付的最小可信子集，且完整可用、有断言、不破坏既有
 *             XDateTime 行为】
 *             ① UTC 固定偏移：offsetFromUtc/standardTimeOffset/
 *                daylightTimeOffset/isDaylightTime/abbreviation/id/isValid；
 *             ② IANA TZif v2/v3 解析：transitions + types + abbreviations +
 *                POSIX 规则外推（覆盖 2037 之后 Qt 的重复口径，已与本机 Qt
 *                6.8.0 实跑对拍）；
 *             ③ 控件联动所需查询 API（offsetData/abbreviation 按 UTC 时刻）。
 *
 *             【未落地 / 范围外，见 openNotes】（不塞空壳）：hasTransitions/
 *             nextTransition/previousTransition/transitions 全表枚举、
 *             displayName(NameType/LongName/OffsetName 本地化文案)、
 *             comment/territory、asBackendZone、availableTimeZoneIds 全量枚举、
 *             fromSecondsAheadOfUtc 别名、systemTimeZoneId 系统后端、
 *             isTimeZoneIdAvailable 全库扫描（需遍历 zoneinfo 目录）。
 * @warning    spec=TimeZone 的对象由 XTimeZone_create_iana* 创建，调用方
 *             必须 XTimeZone_release 释放；UTC/固定偏移对象禁止 XTimeZone_release
 *             （无堆资源）。
 * @author     XinYueC 团队
 */
#ifndef XTIMEZONE_H
#define XTIMEZONE_H

#include <stdint.h>
#include <stdbool.h>
#include "XDateTime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 时区规格（数值对齐 Qt::TimeSpec，qnamespace.h:1239-1244）。 */
typedef enum XTimeSpec
{
    XTimeSpec_LocalTime    = 0, /**< 本地时区（本库无系统后端，降级为 UTC 同口径）。 */
    XTimeSpec_UTC          = 1, /**< UTC（零偏移）。 */
    XTimeSpec_OffsetFromUTC = 2,/**< 固定 UTC 偏移（秒）。 */
    XTimeSpec_TimeZone     = 3  /**< IANA 时区（读 TZif，拥有堆资源）。 */
} XTimeSpec;

/** @brief 偏移数据（对标 QTimeZone::OffsetData）。 */
typedef struct XTimeZoneOffsetData
{
    int64_t m_atUtcMs;        /**< 该偏移生效的 UTC 时刻（毫秒）。 */
    int     m_offsetFromUtc;  /**< 相对 UTC 的总偏移（秒）。 */
    int     m_standardTimeOffset; /**< 标准时偏移（秒）。 */
    int     m_daylightTimeOffset;  /**< 夏令时偏移（秒，0=非夏令时）。 */
    bool    m_isValid;        /**< 数据是否有效（IANA 解析成功）。 */
} XTimeZoneOffsetData;

/**
 * @brief XTimeZone 值对象（对标 QTimeZone）。
 * @note   UTC/固定偏移为纯值（无堆资源）；TimeZone 为拥有堆资源的句柄。
 *         详见文件头「所有权契约」。
 */
typedef struct XTimeZone
{
    int  m_spec;          /**< XTimeSpec 枚举码。 */
    int  m_offsetSeconds; /**< OffsetFromUTC 的固定偏移秒；其余口径忽略。 */
    char* m_id;           /**< IANA id（堆字符串，仅 TimeZone 拥有；UTC/Offset 为 NULL）。 */
    void* m_data;         /**< TimeZone 内部解析数据（XTimeZonePrivate*，仅 TimeZone 拥有；其余 NULL）。 */
} XTimeZone;

/* ==================== 构造 / 有效性 / 释放 ==================== */

/** @brief 构造无效时区（对标 QTimeZone() noexcept：short data，isValid=false）。 */
XTimeZone XTimeZone_createInvalid(void);
/** @brief 构造 UTC 时区（spec=UTC，零偏移，abbr="UTC"；对标 QTimeZone(QDateTime()).timeSpec()）。 */
XTimeZone XTimeZone_createUtc(void);
/** @brief 构造固定偏移时区（spec=OffsetFromUTC；范围 ±57600s = ±16h，越界返回无效；对标 QTimeZone(int)）。 */
XTimeZone XTimeZone_createOffset(int offsetSeconds);
/** @brief 按 IANA id 从系统 zoneinfo 构造（读 /usr/share/zoneinfo/<id>；失败返回无效；对标 QTimeZone(ianaId)）。 */
XTimeZone XTimeZone_createIana(const char* ianaId);
/** @brief 按显式 TZif 文件路径构造（无系统路径/板端裁剪时用；失败返回无效）。 */
XTimeZone XTimeZone_createIanaPath(const char* tzFilePath);
/** @brief 释放 IANA 时区拥有的堆资源（id + data）；UTC/固定偏移为 no-op（无堆资源）。 */
void XTimeZone_release(XTimeZone* tz);

/** @brief 是否为有效时区（对标 QTimeZone::isValid()）。 */
bool XTimeZone_isValid(const XTimeZone* tz);
/** @brief 取规格码；NULL 返回 XTimeSpec_LocalTime。 */
int XTimeZone_spec(const XTimeZone* tz);
/**
 * @brief 取时区 id（对标 QTimeZone::id()）。
 * @details 三者口径对齐 Qt 6.8（本机探针逐值）：
 *          - TimeZone：返回持有的 IANA id 借用指针（随 XTimeZone_release 失效）；
 *          - UTC：返回静态字面量 "UTC"（Qt: QTimeZone::UTC.id()=="UTC"）；
 *          - OffsetFromUTC：返回 "UTC±hh:mm[:ss]"，如 "UTC+08:00"。
 * @param tz 时区；NULL/无效返回 NULL。
 * @return **借用**指针，禁止释放、禁止跨线程长期持有。
 * @warning 固定偏移分支使用**函数内静态缓冲**（零分配换取）：连续两次调用会
 *          覆盖前一次结果，且**线程不安全**。需长期持有/跨线程时请调用方
 *          自行 `XString_create_utf8(...)` 复制。
 */
const char* XTimeZone_id(const XTimeZone* tz);
/* ==================== 偏移 / 夏令时查询（对标 qtimezone.h:186-193） ==================== */

/**
 * @brief 给定 UTC 时刻的总偏移（秒，= 标准时 + 夏令时；对标 offsetFromUtc）。
 * @param tz 时区（NULL/无效返回 0）。
 * @param atUtc 该时刻的 UTC 毫秒（XDateTime_toMSecsSinceEpoch 同锚口径）。
 * @return 相对 UTC 的秒偏移。
 */
int XTimeZone_offsetFromUtc(const XTimeZone* tz, int64_t atUtcMs);
/** @brief 给定 UTC 时刻的标准时偏移（秒；对标 standardTimeOffset）。 */
int XTimeZone_standardTimeOffset(const XTimeZone* tz, int64_t atUtcMs);
/** @brief 给定 UTC 时刻的夏令时偏移（秒；0=非夏令时；对标 daylightTimeOffset）。 */
int XTimeZone_daylightTimeOffset(const XTimeZone* tz, int64_t atUtcMs);
/** @brief 给定 UTC 时刻是否处于夏令时（对标 isDaylightTime）。 */
bool XTimeZone_isDaylightTime(const XTimeZone* tz, int64_t atUtcMs);
/**
 * @brief 给定 UTC 时刻的时区缩写（如 "CST"/"EST"/"EDT"；堆 XString*，调用方释放；对标 abbreviation）。
 * @return 新建 XString*（**调用方 XClassDelete**）。口径对齐 Qt 6.8 本机探针：
 *         - IANA：TZif 的缩写（"EST"/"EDT"/"+1245" 等，尖括号已剥离）；
 *         - UTC：\"UTC\"；
 *         - 固定偏移：\"UTC±hh:mm[:ss]\"（如 QTimeZone(8*3600).abbreviation()
 *           ==\"UTC+08:00\"）。
 * @note 返回的是**新对象**（非借用），与 XTimeZone_id 的借用语义不同。
 */
XString* XTimeZone_abbreviation(const XTimeZone* tz, int64_t atUtcMs);

/**
 * @brief 给定 UTC 时刻的完整偏移数据（对标 QTimeZone::offsetData）。
 * @return XTimeZoneOffsetData；无效/解析失败返回 m_isValid=false。
 */
XTimeZoneOffsetData XTimeZone_offsetData(const XTimeZone* tz, int64_t atUtcMs);

/* ==================== 便捷：本地时间 ↔ UTC 换算（控件联动核心） ==================== */

/**
 * @brief 把「该时区的本地时间」换算为 UTC 毫秒（对标 QDateTime.toUTC 的时区口径）。
 * @param tz 时区（NULL/无效按 UTC 零偏移）。
 * @param localMs 该时区本地时刻的 UTC 毫秒（即「墙上钟」对应的 UTC 瞬间）。
 * @return 真实 UTC 毫秒；IANA 下取「该本地时刻当时生效的偏移」求差。
 */
int64_t XTimeZone_localMsToUtcMs(const XTimeZone* tz, int64_t localMs);
/**
 * @brief 把 UTC 毫秒换算为「该时区的本地时间」毫秒（墙上钟；对标 QDateTime.toTimeZone）。
 * @return 本地时刻的 UTC 毫秒表示。
 */
int64_t XTimeZone_utcMsToLocalMs(const XTimeZone* tz, int64_t utcMs);

#ifdef __cplusplus
}
#endif

#endif /* XTIMEZONE_H */
