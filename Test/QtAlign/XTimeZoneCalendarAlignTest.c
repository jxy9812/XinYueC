/**
 * @file       XTimeZoneCalendarAlignTest.c
 * @brief      XTimeZone / XCalendar × Qt 6.8 对齐契约测试。
 * @details    取证（本机 /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/corelib/time）：
 *             - qcalendar.h / qcalendar.cpp / qgregoriancalendar.cpp /
 *               qjuliancalendar.cpp / qmilankoviccalendar.cpp /
 *               qislamiccivilcalendar.cpp（四历法 JDN 换算与属性）；
 *             - qtimezone.cpp / qtimezoneprivate_tz.cpp（TZif 解析 + POSIX
 *               规则外推 + 偏移/缩写/DST 查询）。
 *
 *             预期值（expected）来源——不依赖「本机 Qt 6.8 运行时探针」，
 *             改用与 Qt 同口径、且独立实现的两套权威读数交叉验证：
 *             ① zoneinfo（Python 标准库，独立 TZif 解析器）在给定 UTC 毫秒
 *                处的 offset/abbr/isDst；② 公历/儒略历 JDN 由闭式公式独立
 *                复算。由此保证断言是「对真实世界时区文件的客观事实」，不是
 *                对自实现回声。
 *
 *             注：本测试覆盖 ① 值对象层（UTC/固定偏移/IANA TZif +
 *             Gregorian/Julian/Milankovic/IslamicCivil）；② 控件联动层
 *             （XDateTimeEdit::calendar/setCalendar/timeZone/setTimeZone 与
 *             XCalendarWidget::calendar/setCalendar，按
 *             XGUI_ON/XWIDGET_ON/XDATETIMEEDIT_ON/XCALENDARWIDGET_ON 门控，
 *             与既有 QtAlign 用例同款）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

/* 【宏可见性（2026-11-15 返工·B1R 修正）】XB1_WIDGET_LINKAGE_READY 定义在
 * XDateTimeEdit.h 内（声明落码即置 1），必须在下方第 ~590 行的 #if 判定
 * **之前** include 该头才能读到——原写法把 include 放在判定之后 ⇒ 判定时
 * 宏恒取缺省 0，三组联动用例永不启用（缺陷本体）。XCalendarWidget.h 同批
 * 提前（其对齐用例组同用该机制）。门控条件与既有控件测试同款。 */
#if XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON
#include "XDateTimeEdit.h"
#endif
#if XGUI_ON && XWIDGET_ON && XCALENDARWIDGET_ON
#include "XCalendarWidget.h"
#endif

#include "XCore/XCode/XDateTime/XCalendar.h"
#include "XCore/XCode/XDateTime/XTimeZone.h"
#include "XCore/XData/XDateTime/XDate.h" /* XDate_toJulianDay/fromJulianDay 已公开 */
#include "XCore/XData/XDateTime/XDateTime.h"
#include "XCore/XContainer/XString/XString.h"
#include "XCore/XCode/XAlgorithm/XStringUtils.h"

#include <string.h>

/* ---- 时区预期表（zoneinfo 独立读数，UTC 毫秒 → 偏移(秒)/isDst/缩写） ---- */
typedef struct TzExp
{
    const char* zone;
    int64_t     utcMs;
    int         offsetSec;  /* 相对 UTC 的总偏移（秒） */
    int         isDst;      /* 0/1 */
    const char* abbr;       /* 缩写 */
} TzExp;

static const TzExp kTzExps[] =
{
    /* America/New_York */
    { "America/New_York", 1705320000000LL, -18000, 0, "EST" }, /* 2024-01-15 */
    { "America/New_York", 1721044800000LL, -14400, 1, "EDT" }, /* 2024-07-15 */
    { "America/New_York", 2540289600000LL, -14400, 1, "EDT" }, /* 2050-07-01 */
    { "America/New_York", 4102488000000LL, -18000, 0, "EST" }, /* 2100-01-01 */
    /* Europe/London */
    { "Europe/London",    1705320000000LL,      0, 0, "GMT" }, /* 2024-01-15 */
    { "Europe/London",    1721044800000LL,   3600, 1, "BST" }, /* 2024-07-15 */
    { "Europe/London",    2540289600000LL,   3600, 1, "BST" }, /* 2050-07-01 */
    { "Europe/London",    4102488000000LL,      0, 0, "GMT" }, /* 2100-01-01 */
    /* Asia/Shanghai（无 DST，POSIX CST-8） */
    { "Asia/Shanghai",    1705320000000LL,  28800, 0, "CST" },
    { "Asia/Shanghai",    1721044800000LL,  28800, 0, "CST" },
    { "Asia/Shanghai",    2540289600000LL,  28800, 0, "CST" },
    { "Asia/Shanghai",    4102488000000LL,  28800, 0, "CST" },
    /* Australia/Sydney（南半球，1 月为夏令时） */
    { "Australia/Sydney", 1705320000000LL,  39600, 1, "AEDT" }, /* 2024-01-15 */
    { "Australia/Sydney", 1721044800000LL,  36000, 0, "AEST" }, /* 2024-07-15 */
    { "Australia/Sydney", 2540289600000LL,  36000, 0, "AEST" }, /* 2050-07-01 */
    { "Australia/Sydney", 4102488000000LL,  39600, 1, "AEDT" }, /* 2100-01-01 */
    /* Europe/Berlin */
    { "Europe/Berlin",    1705320000000LL,   3600, 0, "CET" },  /* 2024-01-15 */
    { "Europe/Berlin",    1721044800000LL,   7200, 1, "CEST" }, /* 2024-07-15 */
    { "Europe/Berlin",    2540289600000LL,   7200, 1, "CEST" }, /* 2050-07-01 */
    { "Europe/Berlin",    4102488000000LL,   3600, 0, "CET" },   /* 2100-01-01 */
    /* 三条 POSIX 规则边界用例（本轮修过、必须锁住）：
     * ① 尖括号名 + 非整小时偏移（Pacific/Chatham "<+1245>-12:45<+1345>"）；
     * ② 南半球跨年 DST + "M4.1.6/24" 的 24 时转换（America/Santiago）；
     * ③ 显式 0 偏移的 DST（Europe/Dublin "IST-1GMT0"，DST 偏移=0 而非缺省 +1h）。 */
    { "Pacific/Chatham",  2540289600000LL,  45900, 0, "+1245" }, /* 2050-07-01 标准时 */
    { "Pacific/Chatham",  4102488000000LL,  49500, 1, "+1345" }, /* 2100-01-01 夏令时 */
    { "America/Santiago", 2540289600000LL, -14400, 0, "-04" },   /* 2050-07-01 标准时 */
    { "America/Santiago", 4102488000000LL, -10800, 1, "-03" },   /* 2100-01-01 夏令时 */
    { "Europe/Dublin",    2540289600000LL,   3600, 0, "IST" },   /* 2050-07-01 标准时 */
    { "Europe/Dublin",    4102488000000LL,      0, 1, "GMT" },   /* 2100-01-01 夏令时(0 偏移) */
};

/* ==================== XTimeZone：UTC / 固定偏移 ==================== */

XQ_TEST(XTimeZone, UtcAndOffsetBasics)
{
    XTimeZone utc = XTimeZone_createUtc();
    XQ_CHECK(XTimeZone_isValid(&utc));
    XQ_CHECK(XTimeZone_spec(&utc) == (int)XTimeSpec_UTC);
    XQ_CHECK(XTimeZone_offsetFromUtc(&utc, 1700000000000LL) == 0);
    XQ_CHECK(XTimeZone_isDaylightTime(&utc, 1700000000000LL) == false);
    /* Qt 6.8 探针：QTimeZone::UTC.id() == "UTC"（原实现返回 NULL，是把偏差
     * 当口径的伪对齐锁，已按 Qt 改正）。 */
    XQ_CHECK_PTR(XTimeZone_id(&utc));
    XQ_CHECK_MSG(XStrcmp(XTimeZone_id(&utc), "UTC") == 0,
                 "UTC id got=%s want=UTC", XTimeZone_id(&utc) ? XTimeZone_id(&utc) : "(null)");

    XTimeZone off = XTimeZone_createOffset(8 * 3600); /* +8h */
    XQ_CHECK(XTimeZone_isValid(&off));
    XQ_CHECK(XTimeZone_spec(&off) == (int)XTimeSpec_OffsetFromUTC);
    XQ_CHECK(XTimeZone_offsetFromUtc(&off, 1700000000000LL) == 8 * 3600);
    XQ_CHECK(XTimeZone_standardTimeOffset(&off, 1700000000000LL) == 8 * 3600);
    XQ_CHECK(XTimeZone_daylightTimeOffset(&off, 1700000000000LL) == 0);
    /* Qt 6.8 探针：QTimeZone(8*3600).id() == abbreviation() == "UTC+08:00" */
    XQ_CHECK_MSG(XStrcmp(XTimeZone_id(&off), "UTC+08:00") == 0,
                 "offset id got=%s want=UTC+08:00", XTimeZone_id(&off));

    /* 越界（±16h 之外）返回无效 */
    XTimeZone bad = XTimeZone_createOffset(17 * 3600);
    XQ_CHECK(!XTimeZone_isValid(&bad));

    /* 本地↔UTC 换算：固定偏移直接加减 */
    XQ_EQ_I64(XTimeZone_utcMsToLocalMs(&off, 1000000LL), 1000000LL + 8LL * 3600 * 1000);
    XQ_EQ_I64(XTimeZone_localMsToUtcMs(&off, 1000000LL + 8LL * 3600 * 1000), 1000000LL);

    {
        XString* ab = XTimeZone_abbreviation(&off, 1700000000000LL);
        XQ_CHECK_PTR(ab);
        /* Qt 探针：固定偏移的缩写是 "UTC+08:00"（原实现恒 "UTC"） */
        XQ_CHECK_MSG(XStrcmp(XString_toUtf8(ab), "UTC+08:00") == 0,
                     "offset abbr got=%s want=UTC+08:00", XString_toUtf8(ab));
        XClassDelete(ab);
    }

    XTimeZone_release(&utc);
    XTimeZone_release(&off);
    XTimeZone_release(&bad);

    /* 无效输入的边界（复核 #6/#7）：
     * ① 任意可读的非 TZif 文件、截断的 TZif 都必须判无效（此前判 valid=1 且
     *    零偏移，等于把畸形数据伪装成「UTC」）；
     * ② 含路径穿越的 id 必须拒绝（此前可加载目录外文件并把该 id 暴露）。
     * 注：/etc/passwd 只在 POSIX 平台存在，Windows 上跳过该子项（其它两项
     * 与平台无关，仍然断言）。 */
#if !defined(_WIN32)
    {
        XTimeZone junk = XTimeZone_createIanaPath("/etc/passwd");
        XQ_CHECK_MSG(!XTimeZone_isValid(&junk),
                     "createIanaPath(/etc/passwd) must be invalid");
        XTimeZone_release(&junk);
    }
#endif
    {
        XTimeZone trav = XTimeZone_createIana("../../../../usr/share/zoneinfo/Europe/Berlin");
        XQ_CHECK_MSG(!XTimeZone_isValid(&trav),
                     "createIana with ../ must be invalid");
        XTimeZone_release(&trav);
    }
    {
        XTimeZone idot = XTimeZone_createIana("Europe/..");
        XQ_CHECK(!XTimeZone_isValid(&idot));
        XTimeZone_release(&idot);
    }
    {
        /* 非 TZif 内容但存在：用本测试源文件自身当「任意可读文件」 */
        XTimeZone nz = XTimeZone_createIanaPath(__FILE__);
        XQ_CHECK_MSG(!XTimeZone_isValid(&nz),
                     "createIanaPath(<a C source file>) must be invalid");
        XTimeZone_release(&nz);
    }
}

/* ==================== XTimeZone：IANA TZif 对拍 ==================== */

/* 平台可用性探测（复核 #19）：`XTimeZone_createIana` 读
 * "/usr/share/zoneinfo/"（Src/XCore/XCode/XDateTime/XTimeZone.c 的
 * XTZ_ZONEINFO_DIR）。Windows 上没有该目录 ⇒ 全部 IANA 断言必然全红，
 * 把「平台没有数据库」误报成「实现错了」。故先探测：**若该平台连
 * 最基础的 UTC 区都解析不出来，本组用例整体记一条说明并跳过**；
 * 只要能解析，就按精确值严格断言（不降低强度）。 */
static bool xq_tzDatabaseAvailable(void)
{
    XTimeZone tz = XTimeZone_createIana("UTC");
    bool ok = XTimeZone_isValid(&tz);
    XTimeZone_release(&tz);
    if (!ok) {
        XQ_CHECK_MSG(true,
                     "[skip] 本机无 /usr/share/zoneinfo 数据库（IANA 精确对拍用例跳过；"
                     "UTC/固定偏移用例仍生效）");
    }
    return ok;
}

XQ_TEST(XTimeZone, IanaOffsetAgainstZoneinfo)
{
    int n = (int)(sizeof(kTzExps) / sizeof(kTzExps[0]));
    int i;
    if (!xq_tzDatabaseAvailable()) return;
    for (i = 0; i < n; ++i) {
        const TzExp* e = &kTzExps[i];
        XTimeZone tz = XTimeZone_createIana(e->zone);
        XQ_CHECK_MSG(XTimeZone_isValid(&tz), "zone=%s should parse", e->zone);
        if (!XTimeZone_isValid(&tz)) { XTimeZone_release(&tz); continue; }

        XQ_CHECK_MSG(XTimeZone_offsetFromUtc(&tz, e->utcMs) == e->offsetSec,
                     "zone=%s utcMs=%lld off got=%d want=%d",
                     e->zone, (long long)e->utcMs,
                     XTimeZone_offsetFromUtc(&tz, e->utcMs), e->offsetSec);
        XQ_CHECK_MSG(XTimeZone_isDaylightTime(&tz, e->utcMs) == (e->isDst != 0),
                     "zone=%s utcMs=%lld isDst got=%d want=%d",
                     e->zone, (long long)e->utcMs,
                     (int)XTimeZone_isDaylightTime(&tz, e->utcMs), e->isDst);

        XString* ab = XTimeZone_abbreviation(&tz, e->utcMs);
        XQ_CHECK_MSG(XStrcmp(XString_toUtf8(ab), e->abbr) == 0,
                     "zone=%s utcMs=%lld abbr got=%s want=%s",
                     e->zone, (long long)e->utcMs,
                     XString_toUtf8(ab), e->abbr);
        XClassDelete(ab);

        XTimeZone_release(&tz);
    }
}

/* ---- 标准时/夏令时偏移精确对拍（Qt 6.8 本机探针实测值，逐条硬编码） ----
 * 这一组专门锁 standardTimeOffset/daylightTimeOffset：它们曾用「首个非 DST
 * 类型的 gmtOff」当全局基准，导致 LMT 期与部分 DST 段恒偏（复核 #5 实测
 * 40/135 组不符，例如 Europe/Dublin @2024 应 std=3600,dst=-3600 而实现给
 * std=-1521,dst=1521）。下面每条都来自 `QTimeZone::standardTimeOffset()`
 * 与 `daylightTimeOffset()` 的真实返回值。 */
typedef struct TzStdDst
{
    const char* zone;
    int64_t     utcMs;
    int         off;
    int         std;
    int         dst;
    int         isDst;
} TzStdDst;

static const TzStdDst kTzStdDst[] =
{
    /* Africa/Cairo 2045-10-26T02Z —— isDst=1、std=7200、dst=3600 */
    { "Africa/Cairo",   2392585200000LL, 10800,  7200, 3600, 1 },
    /* America/Santiago 2042-04-05T14Z */
    { "America/Santiago", 2280330000000LL, -10800, -14400, 3600, 1 },
    /* Australia/Sydney 2118-04-03T02Z（4678394400000）：南半球 DST 已结束 */
    { "Australia/Sydney", 4678394400000LL, 36000, 36000, 0, 0 },
    /* Europe/Berlin 2045-10-29T00Z */
    { "Europe/Berlin",  2383497600000LL,  7200,  3600, 3600, 1 },
    /* Asia/Shanghai 1940-06-01（历史段，无 DST） */
    { "Asia/Shanghai",  -933897600000LL, 28800, 28800,    0, 0 },
    /* America/New_York 1881（LMT 期，早于首条 transition） */
    { "America/New_York", -2800000000000LL, -17762, -17762, 0, 0 },
    /* Europe/Dublin 2024-01-15：反向 DST（冬季 std=3600、dst=-3600） */
    { "Europe/Dublin",  1705320000000LL,      0,  3600, -3600, 1 },
    /* Pacific/Chatham 2100-01-01 */
    { "Pacific/Chatham", 4102488000000LL, 49500, 45900, 3600, 1 },
};

XQ_TEST(XTimeZone, StdAndDstOffsetsMatchQt)
{
    int n = (int)(sizeof(kTzStdDst) / sizeof(kTzStdDst[0]));
    int i;
    if (!xq_tzDatabaseAvailable()) return;
    for (i = 0; i < n; ++i) {
        const TzStdDst* e = &kTzStdDst[i];
        XTimeZone tz = XTimeZone_createIana(e->zone);
        if (!XTimeZone_isValid(&tz)) { XTimeZone_release(&tz); continue; }
        XQ_CHECK_MSG(XTimeZone_offsetFromUtc(&tz, e->utcMs) == e->off,
                     "%s off got=%d want=%d", e->zone,
                     XTimeZone_offsetFromUtc(&tz, e->utcMs), e->off);
        XQ_CHECK_MSG(XTimeZone_standardTimeOffset(&tz, e->utcMs) == e->std,
                     "%s std got=%d want=%d", e->zone,
                     XTimeZone_standardTimeOffset(&tz, e->utcMs), e->std);
        XQ_CHECK_MSG(XTimeZone_daylightTimeOffset(&tz, e->utcMs) == e->dst,
                     "%s dst got=%d want=%d", e->zone,
                     XTimeZone_daylightTimeOffset(&tz, e->utcMs), e->dst);
        XQ_CHECK_MSG(XTimeZone_isDaylightTime(&tz, e->utcMs) == (e->isDst != 0),
                     "%s isDst got=%d want=%d", e->zone,
                     (int)XTimeZone_isDaylightTime(&tz, e->utcMs), e->isDst);
        XTimeZone_release(&tz);
    }
}

/* ---- POSIX /time 转换时刻与跨半球边界（复核 #3/#4 的三个实测失败点） ---- */
typedef struct TzBoundary
{
    const char* zone;
    int64_t     utcMs;
    int         off;
    int         isDst;
} TzBoundary;

static const TzBoundary kTzBoundary[] =
{
    /* Europe/London 2046-03-25T01Z：M3.5.0/1 的 /1 必须真的被解析，
     * 否则边界会晚到 02Z（原实现 /time 被吞，恒 7200） */
    { "Europe/London",   2405552400000LL,  3600, 1 },
    /* 同上，02Z 已在 BST 内 */
    { "Europe/London",   2405556000000LL,  3600, 1 },
    /* Australia/Sydney 2118-04-03T02Z：南半球跨年 DST 结束（M4.1.0/3） */
    { "Australia/Sydney", 4678394400000LL, 36000, 0 },
    /* Africa/Cairo 2045-10-26T21Z：M10.5.4/24 的 24 时转换 */
    { "Africa/Cairo",    2392664400000LL,  7200, 0 },
};

XQ_TEST(XTimeZone, PosixTransitionBoundaries)
{
    int n = (int)(sizeof(kTzBoundary) / sizeof(kTzBoundary[0]));
    int i;
    if (!xq_tzDatabaseAvailable()) return;
    for (i = 0; i < n; ++i) {
        const TzBoundary* e = &kTzBoundary[i];
        XTimeZone tz = XTimeZone_createIana(e->zone);
        if (!XTimeZone_isValid(&tz)) { XTimeZone_release(&tz); continue; }
        XQ_CHECK_MSG(XTimeZone_offsetFromUtc(&tz, e->utcMs) == e->off,
                     "%s @%lld off got=%d want=%d", e->zone,
                     (long long)e->utcMs,
                     XTimeZone_offsetFromUtc(&tz, e->utcMs), e->off);
        XQ_CHECK_MSG(XTimeZone_isDaylightTime(&tz, e->utcMs) == (e->isDst != 0),
                     "%s @%lld isDst got=%d want=%d", e->zone,
                     (long long)e->utcMs,
                     (int)XTimeZone_isDaylightTime(&tz, e->utcMs), e->isDst);
        XTimeZone_release(&tz);
    }
}

/* ==================== XTimeZone：offsetData 结构 ==================== */

XQ_TEST(XTimeZone, OffsetData)
{
    XTimeZone ny = XTimeZone_createIana("America/New_York");
    if (XTimeZone_isValid(&ny)) {
        XTimeZoneOffsetData d = XTimeZone_offsetData(&ny, 1721044800000LL); /* 夏季 EDT */
        XQ_CHECK(d.m_isValid);
        XQ_CHECK(d.m_offsetFromUtc == -14400);
        /* 精确值（Qt 探针：NY 夏季 std=-18000 dst=3600；原断言只查 "!=0"
         * 掩盖了 40/135 组 std/dst 偏差，见复核 #5）。 */
        XQ_CHECK_MSG(d.m_standardTimeOffset == -18000,
                     "NY std got=%d want=-18000", d.m_standardTimeOffset);
        XQ_CHECK_MSG(d.m_daylightTimeOffset == 3600,
                     "NY dst got=%d want=3600", d.m_daylightTimeOffset);
        /* 三者必须自洽：offset = std + dst（Qt qtimezoneprivate_tz.cpp:1049） */
        XQ_EQ_I64(d.m_offsetFromUtc, d.m_standardTimeOffset + d.m_daylightTimeOffset);
        XQ_CHECK(d.m_atUtcMs == 1721044800000LL);

        /* 冬季（EST）：std=-18000、dst=0 */
        {
            XTimeZoneOffsetData w = XTimeZone_offsetData(&ny, 1705320000000LL);
            XQ_CHECK(w.m_offsetFromUtc == -18000);
            XQ_CHECK(w.m_standardTimeOffset == -18000);
            XQ_CHECK(w.m_daylightTimeOffset == 0);
        }
    }
    XTimeZone_release(&ny);
}

/* ==================== XCalendar：构造 / 有效性 / 名 ==================== */

XQ_TEST(XCalendar, ConstructAndName)
{
    XCalendar g = XCalendar_create();             /* 默认 Gregorian */
    XQ_CHECK(XCalendar_isValid(&g));
    XQ_CHECK(XCalendar_system(&g) == (int)XCalendarSystem_Gregorian);
    XQ_CHECK(XStrcmp(XCalendar_name(&g), "Gregorian") == 0);

    XCalendar j = XCalendar_create_system((int)XCalendarSystem_Julian);
    XQ_CHECK(XCalendar_isValid(&j));
    XQ_CHECK(XStrcmp(XCalendar_name(&j), "Julian") == 0);

    XCalendar byName = XCalendar_create_name("Islamic Civil");
    XQ_CHECK(XCalendar_system(&byName) == (int)XCalendarSystem_IslamicCivil);

    XCalendar bad = XCalendar_create_name("Nonsense");
    XQ_CHECK(!XCalendar_isValid(&bad)); /* 未知名不伪造为有效 */

    XCalendar user = XCalendar_create_system((int)XCalendarSystem_User);
    XQ_CHECK(!XCalendar_isValid(&user)); /* 本库未实现自定义后端，按无效 */
}

/* ==================== XCalendar：JDN 换算（独立闭式公式对拍） ==================== */

XQ_TEST(XCalendar, JulianDayRoundTrip)
{
    /* 公历 JDN（独立复算：floor 除法闭式，并与 Python date.toordinal()+1721425
     * 逐值交叉一致；1970-01-01 = 2440588 与框架 XDateTime.c:92 的
     * UNIX_EPOCH_JD 同值） */
    struct { int y, m, d; int64_t jd; } greg[] = {
        { 2000, 1, 1, 2451545 },
        { 1970, 1, 1, 2440588 },
        { 1582, 10, 15, 2299161 },
        { 2024, 6, 15, 2460477 },
        { 1900, 2, 28, 2415079 },
        { 2025, 12, 31, 2461041 },
    };
    int i;
    for (i = 0; i < (int)(sizeof(greg)/sizeof(greg[0])); ++i) {
        XCalendar g = XCalendar_create();
        XDate d = XCalendar_dateFromParts(&g, greg[i].y, greg[i].m, greg[i].d);
        XQ_CHECK_MSG(XDate_isValid(&d), "greg date %d-%02d-%02d valid", greg[i].y, greg[i].m, greg[i].d);
        XQ_CHECK_MSG(XDate_toJulianDay(&d) == greg[i].jd,
                     "greg JDN %d-%02d-%02d got=%lld want=%lld",
                     greg[i].y, greg[i].m, greg[i].d,
                     (long long)XDate_toJulianDay(&d), (long long)greg[i].jd);
        /* 往返：JDN → 年月日 */
        XDate back = XDate_fromJulianDay(greg[i].jd);
        XQ_CHECK(XDate_year(&back) == greg[i].y);
        XQ_CHECK(XDate_month(&back) == greg[i].m);
        XQ_CHECK(XDate_day(&back) == greg[i].d);
    }

    /* 儒略历 JDN（独立复算：365*y + y/4 + 1721117 体系） */
    struct { int y, m, d; int64_t jd; } jul[] = {
        { 2024, 6, 15, 2460490 },
        { 2000, 1, 1, 2451558 },
        { 1900, 2, 28, 2415091 },
    };
    XCalendar jc = XCalendar_create_system((int)XCalendarSystem_Julian);
    for (i = 0; i < (int)(sizeof(jul)/sizeof(jul[0])); ++i) {
        XDate d = XCalendar_dateFromParts(&jc, jul[i].y, jul[i].m, jul[i].d);
        XQ_CHECK_MSG(XDate_toJulianDay(&d) == jul[i].jd,
                     "julian JDN %d-%02d-%02d got=%lld want=%lld",
                     jul[i].y, jul[i].m, jul[i].d,
                     (long long)XDate_toJulianDay(&d), (long long)jul[i].jd);
        /* 往返：儒略日 → 儒略历年月日（QJulianCalendar::julianDayToDate 同口径） */
        {
            XCalendarYearMonthDay p = XCalendar_partsFromDate(&jc, &d);
            XQ_CHECK_MSG(p.m_isValid, "julian parts valid %d-%02d-%02d", jul[i].y, jul[i].m, jul[i].d);
            XQ_CHECK(p.year == jul[i].y);
            XQ_CHECK(p.month == jul[i].m);
            XQ_CHECK(p.day == jul[i].d);
        }
    }
}

/* ==================== XCalendar：属性（闰年 / 月天数） ==================== */

XQ_TEST(XCalendar, LeapAndDays)
{
    XCalendar g = XCalendar_create();
    XQ_CHECK(XCalendar_isLeapYear(&g, 2000) == true);  /* 400 整除 */
    XQ_CHECK(XCalendar_isLeapYear(&g, 1900) == false); /* 100 整除但非 400 */
    XQ_CHECK(XCalendar_isLeapYear(&g, 2024) == true);
    XQ_CHECK(XCalendar_daysInMonth(&g, 2, 2024) == 29);
    XQ_CHECK(XCalendar_daysInMonth(&g, 2, 2023) == 28);
    XQ_CHECK(XCalendar_daysInMonth(&g, 12, 2024) == 31);
    XQ_CHECK(XCalendar_daysInYear(&g, 2024) == 366);
    XQ_CHECK(XCalendar_monthsInYear(&g, 2024) == 12);
    XQ_CHECK(XCalendar_isGregorian(&g) == true);
    XQ_CHECK(XCalendar_isSolar(&g) == true);

    XCalendar islamic = XCalendar_create_system((int)XCalendarSystem_IslamicCivil);
    /* Qt qislamiccivilcalendar.cpp:54-59：isLeapYear = mod(year*11+14,30) < 11。
     * 1445 ⇒ 闰年（month12 = 30 天）、1446 ⇒ 平年（29 天）。 */
    XQ_CHECK(XCalendar_isLeapYear(&islamic, 1445) == true);
    XQ_CHECK(XCalendar_isLeapYear(&islamic, 1446) == false);
    XQ_CHECK(XCalendar_daysInMonth(&islamic, 12, 1445) == 30);
    XQ_CHECK(XCalendar_daysInMonth(&islamic, 12, 1446) == 29);
    /* 前 11 月 30/29 交替（奇数月 30、偶数月 29），Qt 表格口径。 */
    XQ_CHECK(XCalendar_daysInMonth(&islamic, 1, 1445) == 30);
    XQ_CHECK(XCalendar_daysInMonth(&islamic, 2, 1445) == 29);
    /* 伊斯兰历为阴历（非阴阳历、非阳历）。 */
    XQ_CHECK(XCalendar_isLunar(&islamic) == true);
    XQ_CHECK(XCalendar_isLuniSolar(&islamic) == false);
    XQ_CHECK(XCalendar_isSolar(&islamic) == false);

    /* 儒略历：1900 能被 4 整除 ⇒ 闰年（公历 1900 不是闰年，两者在此分叉）。 */
    XCalendar ju = XCalendar_create_system((int)XCalendarSystem_Julian);
    XQ_CHECK(XCalendar_isLeapYear(&ju, 1900) == true);
    XQ_CHECK(XCalendar_isLeapYear(&g, 1900) == false);
    XQ_CHECK(XCalendar_daysInMonth(&ju, 2, 1900) == 29);
}

/* ==================== XCalendar：属性边界（Qt 6.8 探针逐值） ====================
 * 复核 #9 实测：maximumDaysInMonth/minimumDaysInMonth 原实现恒返回 31/28，
 * 而 Qt 对 IslamicCivil 是 30/29。另补 monthsInYear(0)/daysInMonth(*,0)/
 * dayOfWeek(无效历法) 三个 Qt 返回 0 的边界。 */
XQ_TEST(XCalendar, PropertyBoundsMatchQt)
{
    XCalendar g  = XCalendar_create();
    XCalendar ju = XCalendar_create_system((int)XCalendarSystem_Julian);
    XCalendar mi = XCalendar_create_system((int)XCalendarSystem_Milankovic);
    XCalendar ic = XCalendar_create_system((int)XCalendarSystem_IslamicCivil);

    /* Qt 探针：Gregorian/Julian/Milankovic = 31/28；IslamicCivil = 30/29 */
    XQ_CHECK(XCalendar_maximumDaysInMonth(&g) == 31);
    XQ_CHECK(XCalendar_minimumDaysInMonth(&g) == 28);
    XQ_CHECK(XCalendar_maximumDaysInMonth(&ju) == 31);
    XQ_CHECK(XCalendar_minimumDaysInMonth(&ju) == 28);
    XQ_CHECK(XCalendar_maximumDaysInMonth(&mi) == 31);
    XQ_CHECK(XCalendar_minimumDaysInMonth(&mi) == 28);
    XQ_CHECK_MSG(XCalendar_maximumDaysInMonth(&ic) == 30,
                 "Islamic maxDIM got=%d want=30", XCalendar_maximumDaysInMonth(&ic));
    XQ_CHECK_MSG(XCalendar_minimumDaysInMonth(&ic) == 29,
                 "Islamic minDIM got=%d want=29", XCalendar_minimumDaysInMonth(&ic));
    XQ_CHECK(XCalendar_maximumMonthsInYear(&ic) == 12);

    /* Qt 探针：monthsInYear(0) 四种历法全部 0 */
    XQ_CHECK(XCalendar_monthsInYear(&g, 0) == 0);
    XQ_CHECK(XCalendar_monthsInYear(&ju, 0) == 0);
    XQ_CHECK(XCalendar_monthsInYear(&ic, 0) == 0);
    XQ_CHECK(XCalendar_monthsInYear(&g, 2024) == 12);

    /* Qt 探针：daysInMonth(2,0) / daysInMonth(1,0) 均 0 */
    XQ_CHECK(XCalendar_daysInMonth(&g, 2, 0) == 0);
    XQ_CHECK(XCalendar_daysInMonth(&g, 1, 0) == 0);
    /* Qt 探针：isDateValid(0,1,1) = 0 */
    XQ_CHECK(XCalendar_isDateValid(&g, 0, 1, 1) == false);
    XQ_CHECK(XCalendar_isDateValid(&g, 1, 1, 1) == true);

    /* Qt 探针：无效历法下 dayOfWeek 返回 0（原实现无条件转发 XDate） */
    {
        XCalendar badCal = XCalendar_create_system((int)XCalendarSystem_User);
        XDate d = XDate_create_date(2024, 6, 15);
        XQ_CHECK_MSG(XCalendar_dayOfWeek(&badCal, &d) == 0,
                     "dayOfWeek(invalid) got=%d want=0",
                     XCalendar_dayOfWeek(&badCal, &d));
    }

    /* 上界自洽（复核 #11 的第二条）：isDateValid 与 dateFromParts 必须同口径。
     * Islamic 9700-1-1 的 JDN 已超 XDate 可承载上界（5373484），两处都要
     * 拒绝——原实现 isDateValid=1 而 dateFromParts 返回无效 XDate。 */
    {
        int y;
        for (y = 9700; y <= 9720; ++y) {
            bool valid = XCalendar_isDateValid(&ic, y, 1, 1);
            XDate d = XCalendar_dateFromParts(&ic, y, 1, 1);
            XQ_CHECK_MSG(valid == XDate_isValid(&d),
                         "islamic %d isDateValid=%d but dateFromParts valid=%d (must agree)",
                         y, (int)valid, (int)XDate_isValid(&d));
        }
        /* Islamic 1445 在承载范围内，必须两者都有效 */
        XQ_CHECK(XCalendar_isDateValid(&ic, 1445, 1, 1) == true);
        XQ_CHECK(XDate_isValid(&(XDate){XCalendar_dateFromParts(&ic, 1445, 1, 1).m_jd}));
    }

    /* 儒略历上界（复核 #13 的**更正版**）：公历 9999-12-31（JDN 5373484）
     * 在儒略历下**就是 9999-10-19**——这不是折叠 bug，而是正确答案。
     *
     * 三方独立取证（本轮实测）：
     * ① Qt 6.8 探针（qjuliancalendar）：
     *    `partsFromDate(QDate::fromJulianDay(5373484))` → 9999-10-19 valid=1；
     *    反向 `dateFromParts(9999,10,19).toJulianDay()` → 5373484。
     *    Qt 的 `dateFromParts(10000,1,2)` 是另一回事：JDN 5373559，比
     *    Gregorian 9999-12-31 **还晚 75 天**，与 JDN 5373484 无关。
     * ② 独立闭式逆变换（Fliegel–Van Flandern，非本库代码）：
     *    jd 5373484 → (9999, 10, 19)，与 Qt 逐条相同。
     * ③ 公历侧校验：`date(9999,12,31).toordinal()+1721425 == 5373484`。
     *
     * 原写法把**正确结果**当成「必须排除的 bug」（属测试自身错误）。
     * 现锁精确值 + 反向自洽。「上界不折叠」这条约束已由 XDate 承载上限
     * 与上面 Islamic 9700..9720 一致性覆盖，不需要也不应在此重复表达。 */
    {
        XDate lastGreg = XDate_create_date(9999, 12, 31);
        XCalendarYearMonthDay p = XCalendar_partsFromDate(&ju, &lastGreg);
        XQ_CHECK_MSG(p.m_isValid, "julian parts(9999-12-31) must be valid");
        XQ_CHECK_MSG(p.year == 9999 && p.month == 10 && p.day == 19,
                     "julian parts(greg 9999-12-31/jd 5373484) got=%d-%d-%d want=9999-10-19",
                     p.year, p.month, p.day);
        {
            /* 反向自洽：儒略 9999-10-19 必须回到同一 JDN（Qt 探针同值）。 */
            XDate back = XCalendar_dateFromParts(&ju, 9999, 10, 19);
            XQ_CHECK_MSG(XDate_toJulianDay(&back) == 5373484LL,
                         "julian 9999-10-19 jd got=%lld want=5373484",
                         (long long)XDate_toJulianDay(&back));
        }
        /* Qt 也接受更大的儒略年（10000-1-2 → JDN 5373559），但该 JDN 超出
         * XDate 承载上限（jdn <= 5373484）⇒ 本库按无效返回。
         * 这是**已登记的承载限制**，不是换算错误。 */
        XQ_CHECK(!XDate_isValid(&(XDate){XCalendar_dateFromParts(&ju, 10000, 1, 2).m_jd}));
    }
}

/* ==================== 控件联动（对标 qdatetimeedit.cpp:313/329/1055/1061
 *                     与 qcalendarwidget.cpp calendar()/setCalendar()） ====================
 *
 * 【门控说明】这两组用例断言的是 XDateTimeEdit/XCalendarWidget 新增的
 * calendar/setCalendar/timeZone/setTimeZone。该四个存取器落在
 * Src/XGui/Widget/XDateTimeEdit.{c,h} 与 XCalendarWidget.{c,h}——**不在本域
 * owned 清单内**（本域 owned 仅 Src/XCore/XCode/XDateTime/ 与本文测试），
 * 故以 DomainResult.sharedEdits 形式提交、由收口员统一应用。
 *
 * 门控**不需要人工置宏**：XDateTimeEdit.h 里 `#define
 * XB1_WIDGET_LINKAGE_READY 1`（上轮 sharedEdits 第 7 条；返工·B1R 已核
 * 实声明/实现均已落码并直接落宏，登记于本轮 sharedEdits 备案），与本头
 * include 同批生效 ⇒ 落码那一刻本组用例自动启用；未应用时宏为 0，整组
 * 剔除，QtAlign 套件照常编译（不会因符号缺失拖红他人）。 */
#ifndef XB1_WIDGET_LINKAGE_READY
#define XB1_WIDGET_LINKAGE_READY 0
#endif

#if XB1_WIDGET_LINKAGE_READY && XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON
/* XDateTimeEdit.h 已在文件头部提前 include（宏可见性需要）。 */
#include "XWidget.h"
#include "XClass.h"

XQ_TEST(XDateTimeEdit, CalendarAccessors)
{
    XDateTimeEdit* e = XDateTimeEdit_create(NULL, 0);
    XQ_CHECK_PTR(e);
    if (!e) return;

    /* 默认历法 = Gregorian（Qt: 构造即 QCalendar()） */
    {
        XCalendar c = XDateTimeEdit_calendar(e);
        XQ_CHECK(XCalendar_isValid(&c));
        XQ_CHECK(XCalendar_system(&c) == (int)XCalendarSystem_Gregorian);
    }

    /* setCalendar 落值并被 calendar() 读回 */
    {
        XCalendar j = XCalendar_create_system((int)XCalendarSystem_Julian);
        XCalendar back;
        XDateTimeEdit_setCalendar(e, &j);
        back = XDateTimeEdit_calendar(e);
        XQ_CHECK(XCalendar_system(&back) == (int)XCalendarSystem_Julian);

        /* 无效历法被忽略（保持原值，不「选了不兑现」成 Invalid） */
        {
            XCalendar bad = XCalendar_create_name("Nonsense");
            XDateTimeEdit_setCalendar(e, &bad);
            back = XDateTimeEdit_calendar(e);
            XQ_CHECK(XCalendar_system(&back) == (int)XCalendarSystem_Julian);
        }
        /* NULL 与无效历法同款（不崩、不改值） */
        XDateTimeEdit_setCalendar(e, NULL);
        back = XDateTimeEdit_calendar(e);
        XQ_CHECK(XCalendar_system(&back) == (int)XCalendarSystem_Julian);
    }
    XClassDelete((XClass*)e);
}

XQ_TEST(XDateTimeEdit, TimeZoneAccessors)
{
    XDateTimeEdit* e = XDateTimeEdit_create(NULL, 0);
    XQ_CHECK_PTR(e);
    if (!e) return;

    /* 默认 timeZone = 无效（Qt 默认 QTimeZone()），spec=LocalTime */
    XQ_CHECK(!XTimeZone_isValid(XDateTimeEdit_timeZone(e)));
    XQ_CHECK(XTimeZone_spec(XDateTimeEdit_timeZone(e)) == (int)XTimeSpec_LocalTime);

    /* setTimeZone(固定偏移)：值拷贝 + spec 联动 */
    {
        XTimeZone off = XTimeZone_createOffset(8 * 3600);
        XDateTimeEdit_setTimeZone(e, &off);
        XQ_CHECK(XTimeZone_spec(XDateTimeEdit_timeZone(e)) == (int)XTimeSpec_OffsetFromUTC);
        XQ_CHECK(XTimeZone_offsetFromUtc(XDateTimeEdit_timeZone(e), 0) == 8 * 3600);
        XQ_CHECK(XDateTimeEdit_timeSpec(e) == (int)XTimeSpec_OffsetFromUTC);
        /* 调用方持有的 off 仍有效（所有权未转移），由调用方释放 */
        XQ_CHECK(XTimeZone_isValid(&off));
        XTimeZone_release(&off);
        /* 控件内部的副本仍在（证明是内容拷贝而非接管） */
        XQ_CHECK(XTimeZone_offsetFromUtc(XDateTimeEdit_timeZone(e), 0) == 8 * 3600);
    }

    /* setTimeZone(UTC) 覆盖旧值，且旧的堆资源被正确释放（泄漏门由运行器判） */
    {
        XTimeZone utc = XTimeZone_createUtc();
        XDateTimeEdit_setTimeZone(e, &utc);
        XQ_CHECK(XTimeZone_spec(XDateTimeEdit_timeZone(e)) == (int)XTimeSpec_UTC);
        XQ_CHECK(XTimeZone_offsetFromUtc(XDateTimeEdit_timeZone(e), 0) == 0);
    }

    /* setTimeZone(NULL) 回落 LocalTime（对齐 Qt 的无效 QTimeZone） */
    XDateTimeEdit_setTimeZone(e, NULL);
    XQ_CHECK(XTimeZone_spec(XDateTimeEdit_timeZone(e)) == (int)XTimeSpec_LocalTime);
    XQ_CHECK(XDateTimeEdit_timeSpec(e) == (int)XTimeSpec_LocalTime);

    /* IANA：深拷贝（id 可读回、偏移与值对象一致），且重复设置同值应早退 */
    {
        XTimeZone ny = XTimeZone_createIana("America/New_York");
        if (XTimeZone_isValid(&ny)) {
            XDateTimeEdit_setTimeZone(e, &ny);
            XQ_CHECK(XTimeZone_spec(XDateTimeEdit_timeZone(e)) == (int)XTimeSpec_TimeZone);
            XQ_CHECK(XTimeZone_id(XDateTimeEdit_timeZone(e)) != NULL);
            XQ_CHECK(XStrcmp(XTimeZone_id(XDateTimeEdit_timeZone(e)), "America/New_York") == 0);
            XQ_CHECK(XTimeZone_offsetFromUtc(XDateTimeEdit_timeZone(e), 1705320000000LL) == -18000); /* EST */
            XQ_CHECK(XTimeZone_offsetFromUtc(XDateTimeEdit_timeZone(e), 1721044800000LL) == -14400); /* EDT */
            /* 同值再设：不崩、值不变 */
            XDateTimeEdit_setTimeZone(e, &ny);
            XQ_CHECK(XTimeZone_offsetFromUtc(XDateTimeEdit_timeZone(e), 1721044800000LL) == -14400);
        }
        XTimeZone_release(&ny);
    }

    XClassDelete((XClass*)e);
}
#endif /* XB1_WIDGET_LINKAGE_READY && XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */

#if XB1_WIDGET_LINKAGE_READY && XGUI_ON && XWIDGET_ON && XCALENDARWIDGET_ON
/* XCalendarWidget.h 已在文件头部提前 include（与 XDateTimeEdit.h 同批）。 */

XQ_TEST(XCalendarWidget, CalendarAccessors)
{
    XCalendarWidget* w = XCalendarWidget_create(NULL, 0);
    XQ_CHECK_PTR(w);
    if (!w) return;

    /* 默认 Gregorian */
    {
        XCalendar c = XCalendarWidget_calendar(w);
        XQ_CHECK(XCalendar_isValid(&c));
        XQ_CHECK(XCalendar_system(&c) == (int)XCalendarSystem_Gregorian);
    }
    /* setCalendar 落值可读回；无效忽略；NULL 不崩 */
    {
        XCalendar m = XCalendar_create_system((int)XCalendarSystem_Milankovic);
        XCalendar back;
        XCalendarWidget_setCalendar(w, &m);
        back = XCalendarWidget_calendar(w);
        XQ_CHECK(XCalendar_system(&back) == (int)XCalendarSystem_Milankovic);

        XCalendar bad = XCalendar_create_name("Nope");
        XCalendarWidget_setCalendar(w, &bad);
        back = XCalendarWidget_calendar(w);
        XQ_CHECK(XCalendar_system(&back) == (int)XCalendarSystem_Milankovic);

        XCalendarWidget_setCalendar(w, NULL);
        back = XCalendarWidget_calendar(w);
        XQ_CHECK(XCalendar_system(&back) == (int)XCalendarSystem_Milankovic);
    }
    /* NULL self 不崩：返回缺省 Gregorian 值 */
    {
        XCalendar nullCal = XCalendarWidget_calendar(NULL);
        XQ_CHECK(XCalendar_system(&nullCal) == (int)XCalendarSystem_Gregorian);
    }

    XClassDelete((XClass*)w);
}
#endif /* XB1_WIDGET_LINKAGE_READY && XGUI_ON && XWIDGET_ON && XCALENDARWIDGET_ON */
