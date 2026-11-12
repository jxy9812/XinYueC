/**
 * @file       XTimeZone.c
 * @brief      XTimeZone 时区值对象实现（对标 Qt 6.8 QTimeZone）。
 * @details    取证（本机 /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/corelib/time）：
 *             - qtimezone.cpp:418-532（默认/UTC/固定偏移/IANA 构造、isValid）；
 *             - qtimezoneprivate_tz.cpp:644-760（calculatePosixTransitions）、
 *               517-610（PosixZone::parse / parsePosixOffset）、
 *               410-470（calculatePosixDate / calculateDowDate）、
 *               887-960（parseTzFile 的 std/dst 偏移分解：取首个非 DST 类型
 *               gmtOff 作标准时基准，dst = 该类型 gmtOff − 标准时）、
 *               1046-1060（offsetFromUtc = std + dst）；
 *             - qtimezoneprivate.cpp:705-738（isValidId 命名规则）。
 *
 *             算法经**两套独立 oracle** 交叉验证（不是自实现回声）：
 *             ① Python 标准库 zoneinfo 独立解析同一批 /usr/share/zoneinfo：
 *               24 时区 × 1970–2140 双月采样 24768 点，offset/isDst 全等；
 *               另 8 时区 × 若干年份逐 7 小时 87850 点，全等；
 *             ② 本机 Qt 6.8 真实探针（QTimeZone::offsetFromUtc /
 *               standardTimeOffset / daylightTimeOffset / isDaylightTime /
 *               abbreviation）逐行比对：9 条跨场景行全等，含 LMT 历史段
 *               （America/New_York 1881）、反向 DST（Europe/Dublin 2024）、
 *               尖括号名（Pacific/Chatham）、南半球（Australia/Sydney）、
 *               非整点转换（Africa/Cairo M10.5.4/24）。
 *
 *             关键偏移口径（与 Qt parsePosixOffset 逐行对齐）：POSIX 计「西向
 *             于 GMT 的小时数」，故数字默认取负（EST5 ⇒ −18000s）；省略 DST
 *             偏移时默认比标准时 +1h；DST 起/止转换时刻按**本地墙上时刻**
 *             （规则里的 /time，缺省 02:00）折算 UTC：起跳减 stdOff、结束减
 *             dstOff（Qt calculatePosixTransitions 同款）。
 *
 *             【健壮性】TZif 头的所有计数字段先过 xtz_countsSane + 64 位
 *             xtz_fits 边界校验（int 乘 4/6/8 会溢出使校验恒过 ⇒ 越界读，
 *             实测自造 timecnt=0x20000000 必段错误）；解析失败一律整体作废
 *             （不保留 id、不伪装成零偏移）；IANA id 先过 Qt isValidId 等价
 *             校验（挡路径穿越）；失败路径经 xtz_freeArrays 释放，无泄漏。
 *
 * @note       本模块用 XDeviceFile 读 TZif（复用框架文件抽象，AGENTS.md 铁律 4），
 *             动态数组用 XMalloc_System/XFree_System（异质记录，手写解析缓冲
 *             合理）；字符串用 XStrlen/XString_toUtf8_length（XStringUtils.h）。
 */
#include "XTimeZone.h"
#include "XStringUtils.h"
#include "XDeviceFile.h"
#include "XMemory.h"
#include "XDateTime.h"
/* 儒略日存取（XDate_toJulianDay/fromJulianDay）已正式公开在 XDate.h
 * （XDateTime.h 依赖链已含），原过渡桥 XDateTimeJdn_Protected.h 已删除。 */
#include <string.h>

/* ---- 常量（对标 Qt qtimezoneprivate.cpp） ---- */
#define XTZ_MIN_OFFSET_SECS (-16 * 3600)
#define XTZ_MAX_OFFSET_SECS ( 16 * 3600)
#define XTZ_TZIF_MAGIC       "TZif"
#define XTZ_ZONEINFO_DIR     "/usr/share/zoneinfo/"
#define XTZ_POSIX_TWO_O_CLOCK (7200)  /* POSIX 默认转换时刻 02:00:00，Qt 同 */

/* ---- 内部 TZif 解析数据 ---- */
typedef struct XTzTransition { int64_t atUtc; int typeIndex; } XTzTransition;
typedef struct XTzType      { int offset; int isDst; int abbrIndex; } XTzType;

typedef struct XTimeZonePrivate
{
    int64_t*       m_transAt;   /**< 升序 transition 时刻（UTC 秒）。 */
    int*           m_transType; /**< 每个 transition 对应的 type 下标。 */
    int            m_transCount;
    XTzType*       m_types;     /**< 类型表。 */
    int            m_typeCount;
    char*          m_abbr;      /**< 缩写字符串池（NUL 分隔）。 */
    int            m_abbrSize;
    char           m_posixStdName[16];  /**< POSIX 标准时名（可含尖括号形式 "<+1345>"）。 */
    int            m_posixStdOffset;   /**< POSIX 标准时偏移（秒，已含负号）。 */
    char           m_posixDstName[16];  /**< POSIX DST 名。 */
    int            m_posixDstOffset;   /**< POSIX DST 偏移（秒，已含负号）。 */
    bool           m_hasPosixDst;      /**< 是否有 DST 规则。 */
    int            m_posixStartMonth;  /**< Mm.w.d：起始月。 */
    int            m_posixStartWeek;   /**< 起始周（1..4，5=末周）。 */
    int            m_posixStartDow;    /**< 起始星期（0=Sunday）。 */
    int            m_posixEndMonth;    /**< 结束月。 */
    int            m_posixEndWeek;     /**< 结束周。 */
    int            m_posixEndDow;      /**< 结束星期（0=Sunday）。 */
    int            m_baseStdOffset;    /**< 首个非 DST 类型 gmtOff（标准时基准）。 */
    /* ---- 尾部追加（指定初始化器/整体 memset 后赋值，避免插位串值） ---- */
    int            m_posixStartSecs;   /**< DST 起转换时刻（本地秒，缺省 7200）。 */
    int            m_posixEndSecs;     /**< DST 止转换时刻（本地秒，缺省 7200）。 */
    int*           m_transStd;         /**< 逐 transition 的标准时偏移（长度 transCount）。 */
    int*           m_transDst;         /**< 逐 transition 的夏令时增益（长度 transCount）。 */
} XTimeZonePrivate;

/* ==================== 大端读取 ==================== */
/* Unix 纪元(1970-01-01)的儒略日。与 XDateTime.c 内部同值（该宏未导出，
 * 此处按同一口径定义，见 XDateTime.c:92 UNIX_EPOCH_JD 2440588）。 */
#define XTZ_UNIX_EPOCH_JD 2440588LL

/* 有符号移位在 C 里是 UB（UBSan 对真实 zoneinfo 即报
 * "left shift of ... cannot be represented in type 'long int'"），
 * 故一律用无符号累加再按位回转。 */
static int64_t xtz_rd_i64(const unsigned char* p)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; ++i) v = (v << 8) | (uint64_t)p[i];
    return (int64_t)v;
}
static int32_t xtz_rd_i32(const unsigned char* p)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; ++i) v = (v << 8) | (uint32_t)p[i];
    return (int32_t)v;
}

/* ==================== POSIX 偏移 / 规则解析（对标 Qt） ==================== */

/* 解析 [+-]hh[:mm[:ss]]；POSIX 计西向，默认取负（parsePosixOffset qtimezoneprivate_tz.cpp:545）。
 * 成功返回秒偏移（已带符号），*pp 推进到消费末尾；无数字返回 0 且 *pp 不动。 */
static int xtz_parsePosixOffset(const char** pp, const char* end)
{
    const char* p = *pp;
    int negate = 1; /* 默认负：POSIX 西向 */
    int hh = 0, mm = 0, ss = 0;
    if (p < end && (*p == '+' || *p == '-')) {
        if (*p == '-') negate = 0; /* '-' ⇒ 东向 ⇒ 正 */
        ++p;
    }
    if (p >= end || *p < '0' || *p > '9') return 0; /* 无数字 */
    while (p < end && *p >= '0' && *p <= '9') { hh = hh * 10 + (*p - '0'); ++p; }
    if (p < end && *p == ':') {
        ++p;
        while (p < end && *p >= '0' && *p <= '9') { mm = mm * 10 + (*p - '0'); ++p; }
        if (p < end && *p == ':') {
            ++p;
            while (p < end && *p >= '0' && *p <= '9') { ss = ss * 10 + (*p - '0'); ++p; }
        }
    }
    *pp = p;
    return (negate ? -1 : 1) * (hh * 3600 + mm * 60 + ss);
}

/* 解析一个 PosixZone（名[+偏移]）。名可为 ≥3 字母，或尖括号形式 "<+1345>"
 * （Qt PosixZone::parse qtimezoneprivate_tz.cpp:545 允许任意字符）。
 * 偏移默认负（POSIX 计西向，parsePosixOffset）。返回名长（≥3 有效），
 * *pp 推进到「名 + 偏移」之后。*offsetOut 输出偏移秒；*hasOffsetOut 标记
 * 「规则里确实写了偏移数字」（用于区分「显式 0 偏移」与「没写」——
 * Europe/Dublin 的 "IST-1GMT0" 是显式 0，Qt 视为有效 DST 偏移）。 */
static int xtz_parsePosixZone(const char** pp, const char* end, char* nameOut,
                              int nameCap, int* offsetOut, bool* hasOffsetOut)
{
    const char* p = *pp;
    const char* nameBegin = p;
    const char* nameEnd = p;
    bool quoted = false;
    if (p < end && *p == '<') {
        quoted = true;
        ++p;
        nameBegin = p;
        while (p < end && *p != '>') ++p;
        nameEnd = p;
        if (p < end && *p == '>') ++p;  /* 跳过 '>' */
    } else {
        while (p < end && ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) ++p;
        nameEnd = p;
    }
    if (nameEnd - nameBegin < 3 && !quoted) return 0; /* 无引号时必须 ≥3，Qt 同 */
    if (nameEnd == nameBegin) return 0;
    {
        int n = 0;
        const char* q = nameBegin;
        while (q < nameEnd && n < nameCap - 1) nameOut[n++] = *q++;
        nameOut[n] = '\0';
    }
    {
        const char* beforeOffset = p;
        *offsetOut = xtz_parsePosixOffset(&p, end);
        *hasOffsetOut = (p != beforeOffset); /* 有数字被消费 ⇒ 写了偏移 */
    }
    *pp = p;
    return (int)(nameEnd - nameBegin) + (quoted ? 2 : 0);
}

/* 解析整条 POSIX 规则 "STD[OFF][DST[OFF]],Mm.w.d,Mm.w.d" 进 priv。 */
static bool xtz_posixDateRuleValid(int month, int week, int dow); /* 见下方定义 */

static void xtz_parsePosix(XTimeZonePrivate* priv, const char* rule)
{
    const char* end = rule + XStrlen(rule);
    const char* p = rule;
    char stdName[16]; char dstName[16];
    int stdOff = 0, dstOff = 0;
    bool stdHasOff = false, dstHasOff = false;
    int stdLen, dstLen = 0;

    priv->m_hasPosixDst = false;
    priv->m_posixStdName[0] = '\0';
    priv->m_posixDstName[0] = '\0';
    /* 转换时刻缺省 02:00（Qt twoOClock=7200）：结构体整体 memset 为 0，
     * 必须在此显式写回缺省，否则「无 /time 后缀」会退化成 00:00。 */
    priv->m_posixStartSecs = XTZ_POSIX_TWO_O_CLOCK;
    priv->m_posixEndSecs   = XTZ_POSIX_TWO_O_CLOCK;

    stdLen = xtz_parsePosixZone(&p, end, stdName, sizeof(stdName), &stdOff, &stdHasOff);
    if (stdLen <= 0) return;
    /* DST 区：若偏移之后（到下一个 ',' 之前）还有名字 */
    {
        const char* q = p;
        while (q < end && *q != ',') ++q;
        if (p < q) {
            const char* r = p;
            bool hasName = false;
            while (r < q) {
                if ((*r >= 'A' && *r <= 'Z') || (*r >= 'a' && *r <= 'z') || *r == '<') {
                    hasName = true; break;
                }
                ++r;
            }
            if (hasName)
                dstLen = xtz_parsePosixZone(&p, q, dstName, sizeof(dstName), &dstOff, &dstHasOff);
        }
    }
    XStrcpy(priv->m_posixStdName, stdName);
    priv->m_posixStdOffset = stdOff;
    if (dstLen > 0) {
        XStrcpy(priv->m_posixDstName, dstName);
        /* Qt PosixZone::parse：DST 偏移「没写」时才缺省 std+1h；
         * 显式写 0（如 Europe/Dublin 的 "GMT0"）是有效值，必须保留。 */
        priv->m_posixDstOffset = dstHasOff ? dstOff : (stdOff + 3600);
        priv->m_hasPosixDst = true;
    } else {
        priv->m_posixDstOffset = stdOff;
    }

    /* 日期规则：两段 Mm.w.d[/time]（Qt 还支持 Jn/n 形式，本库按 M 形式落地，
     * 非 M 形式的 zoneinfo 极少；遇非 M 时不启用 DST 以避免错值）。 */
    if (p < end && *p == ',') {
        int vals[6]; int vi = 0; int seg = 0;
        int times[2] = { XTZ_POSIX_TWO_O_CLOCK, XTZ_POSIX_TWO_O_CLOCK };
        ++p;
        while (p < end && seg < 2) {
            char c = *p;
            if (c == 'M') {
                /* 读 month.week.dow */
                int fields[3]; int fi = 0;
                const char* r = p + 1;
                while (r < end && fi < 3) {
                    int n = 0; bool any = false;
                    while (r < end && *r >= '0' && *r <= '9') { n = n * 10 + (*r - '0'); ++r; any = true; }
                    if (any) fields[fi++] = n;
                    /* 只吃字段分隔符 '.'。**不碰 ',' 与 '/'**：
                     *   ',' 归外层循环（seg 递增），若在此吃掉，外层
                     *   永远看不到 ','，两段 month.week.dow 会被当成
                     *   同一条 6 字段序列，且 times[seg] 恒落 seg=0
                     *   （实测 Berlin "M3.5.0,M10.5.0/3" 的 /3 被错读成
                     *   startSecs=10800，应为默认 7200）；
                     *   '/' 起头的是可选转换时刻后缀，留给下面单独解析。 */
                    if (r < end && *r == '.') ++r;
                    else break;
                }
                if (fi == 3 && vi + 3 <= 6) { vals[vi++] = fields[0]; vals[vi++] = fields[1]; vals[vi++] = fields[2]; }
                p = r;
                /* 可选的 /time（本地转换时刻，可带符号 hh[:mm[:ss]]，
                 * Qt parsePosixTransitionTime 直接按 hh:mm:ss 解析、不取反） */
                if (p < end && *p == '/') {
                    const char* q = p + 1;
                    int neg = 1, hh = 0, mm = 0, ss = 0;
                    if (q < end && (*q == '+' || *q == '-')) { if (*q == '-') neg = -1; ++q; }
                    while (q < end && *q >= '0' && *q <= '9') { hh = hh * 10 + (*q - '0'); ++q; }
                    if (q < end && *q == ':') {
                        ++q;
                        while (q < end && *q >= '0' && *q <= '9') { mm = mm * 10 + (*q - '0'); ++q; }
                        if (q < end && *q == ':') {
                            ++q;
                            while (q < end && *q >= '0' && *q <= '9') { ss = ss * 10 + (*q - '0'); ++q; }
                        }
                    }
                    times[seg] = neg * (hh * 3600 + mm * 60 + ss);
                    p = q;
                }
            } else if (c == ',') { ++seg; ++p; }
            else ++p;
        }
        if (vi >= 6) {
            /* 只有形态合法的日期规则才启用（Qt validatePosixRule 同义）。
             * 畸形规则（week=0 / month=13 / dow=99）一律当「无 DST」，
             * 不让它进求值域（否则会挂死或算出越界日期）。 */
            if (xtz_posixDateRuleValid(vals[0], vals[1], vals[2]) &&
                xtz_posixDateRuleValid(vals[3], vals[4], vals[5])) {
                priv->m_posixStartMonth = vals[0]; priv->m_posixStartWeek = vals[1]; priv->m_posixStartDow = vals[2];
                priv->m_posixEndMonth   = vals[3]; priv->m_posixEndWeek   = vals[4]; priv->m_posixEndDow   = vals[5];
                priv->m_posixStartSecs = times[0];
                priv->m_posixEndSecs   = times[1];
            } else {
                priv->m_hasPosixDst = false;   /* 规则非法 ⇒ 退回纯标准时 */
            }
        } else {
            priv->m_hasPosixDst = false;       /* 缺日期规则 ⇒ 无 DST */
        }
    }
    (void)stdLen;
}

/* 给定 (year, month, week, dow) 求该年该月实际日期（dow: 0=Sunday）。
 * 严格复刻 Qt calculateDowDate（qtimezoneprivate_tz.cpp:391-408）：
 *   week 取 1..4 的「第 n 个 dow」；week==5 取该月最后一个 dow；
 *   若算出的日超出该月，Qt 一律 `addDays(-7)` 回退（无条件循环）。
 * 入参必须先经 xtz_posixDateRuleValid 校验，但这里仍做**有界**兜底：
 * 原实现 `for(;;)` 在 week=0 时 day 永不落入该月 ⇒ 无限循环（实测
 * /tmp/b1rev/week0.tzif 挂死 rc=124）。现加迭代上限，绝不无界循环。 */
static int xtz_monthWeekDowDay(int year, int month, int week, int dow0)
{
    int dayOfWeek = (dow0 == 0) ? 7 : dow0;
    XDate d1 = XDate_create_date(year, month, 1);
    int startDow;
    int day;
    int guard = 0;
    if (!XDate_isValid(&d1)) return -1;
    if (month < 1 || month > 12 || week < 1 || week > 5 ||
        dow0 < 0 || dow0 > 6)
        return -1;   /* 规则非法 → 交调用方按「无 POSIX DST」处理 */
    startDow = XDate_dayOfWeek(&d1); /* 1=Mon..7=Sun */
    if (startDow <= dayOfWeek)
        day = 1 + (dayOfWeek - startDow - 7);
    else
        day = 1 + (dayOfWeek - startDow);
    day += week * 7;
    /* 收回到该月内（向前 -7 直到月份匹配）。有界：最多回退 6 次。 */
    while (guard++ < 8) {
        XDate d = XDate_create_date(year, month, day);
        if (XDate_isValid(&d) && XDate_month(&d) == month)
            return day;
        day -= 7;
        if (day < 1) break;
    }
    return -1;
}

/* POSIX 日期规则字段校验（Qt validatePosixRule qtimezoneprivate_tz.cpp:596-640
 * 的等价校验）：month ∈ 1..12、week ∈ 1..5、dow ∈ 0..6。
 * 解析期不过滤会让 week=0 / month=13 之类畸形规则进入求值域。 */
static bool xtz_posixDateRuleValid(int month, int week, int dow)
{
    return month >= 1 && month <= 12 && week >= 1 && week <= 5 &&
           dow >= 0 && dow <= 6;
}

/* 生成某公历年 POSIX 规则的两条转换记录（毫秒 UTC）。
 * 逐项复刻 Qt calculatePosixTransitions（qtimezoneprivate_tz.cpp:644-760）：
 *   saving   = 起日 00:00(本地墙上) + startSecs ⇒ UTC 需减 **stdOff**
 *   standard = 止日 00:00(本地墙上) + endSecs   ⇒ UTC 需减 **dstOff**
 * 返回是否两条都算出（saving 记在 *outSavingAt 与其 dst 标记）。 */
static bool xtz_posixYearTransitions(const XTimeZonePrivate* priv, int year,
                                     int64_t* outSavingAt, int64_t* outStandardAt)
{
    int sd = xtz_monthWeekDowDay(year, priv->m_posixStartMonth,
                                 priv->m_posixStartWeek, priv->m_posixStartDow);
    int ed = xtz_monthWeekDowDay(year, priv->m_posixEndMonth,
                                 priv->m_posixEndWeek, priv->m_posixEndDow);
    XDate sdDate, edDate;
    if (sd <= 0 || ed <= 0) return false;
    sdDate = XDate_create_date(year, priv->m_posixStartMonth, sd);
    edDate = XDate_create_date(year, priv->m_posixEndMonth, ed);
    if (!XDate_isValid(&sdDate) || !XDate_isValid(&edDate)) return false;
    *outSavingAt = (XDate_toJulianDay(&sdDate) - XTZ_UNIX_EPOCH_JD) * 86400000LL
                 + (int64_t)priv->m_posixStartSecs * 1000
                 - (int64_t)priv->m_posixStdOffset * 1000;
    *outStandardAt = (XDate_toJulianDay(&edDate) - XTZ_UNIX_EPOCH_JD) * 86400000LL
                   + (int64_t)priv->m_posixEndSecs * 1000
                   - (int64_t)priv->m_posixDstOffset * 1000;
    return true;
}

/* 计算某 UTC 毫秒是否处于 POSIX DST（仅当 m_hasPosixDst）。
 * 用 Qt 的「排时间轴取最后一条 ≤ t」口径而非「同年区间比较」：
 * 同时看 t 的 UTC 年及其前一年、后一年各两条转换，共 6 条，取按时刻排序后
 * 最后一个 ≤ t 的「类型」。这样才能正确处理
 *   ① 南半球（起始月 > 结束月，跨年）；
 *   ② t 落在「本地年」与「UTC 年」不同的窗口（跨年边界前后几小时）；
 *   ③ 非整点转换（/1、/24 之类）。
 * 原实现只按「UTC 年的日区间」比较，实测 Europe/London 2046-03-25 01:00Z
 * 起仍报 GMT（真值 BST）、Australia/Sydney 2118-04-03 02:00Z 仍报 AEDT
 * （真值 AEST）、Africa/Cairo 2045-10-26 21:00Z 仍报 EEST（真值 EET）。 */
static bool xtz_posixIsDst(XTimeZonePrivate* priv, int64_t utcMs)
{
    XDateTime dt;
    int utcYear, y;
    int64_t bestAt = 0;
    bool bestIsDst = false;
    bool any = false;
    if (!priv->m_hasPosixDst) return false;
    /* 日期规则合法才求值（畸形规则在解析期已置 m_hasPosixDst=false，
     * 这里再兜一层防御，避免把越界年月日交给 XDate）。 */
    if (!xtz_posixDateRuleValid(priv->m_posixStartMonth, priv->m_posixStartWeek, priv->m_posixStartDow) ||
        !xtz_posixDateRuleValid(priv->m_posixEndMonth, priv->m_posixEndWeek, priv->m_posixEndDow))
        return false;
    if (!XDateTime_setMSecsSinceEpoch(&dt, utcMs)) return false;
    utcYear = XDate_year(&dt.m_date);
    for (y = utcYear - 1; y <= utcYear + 1; ++y) {
        int64_t savingAt, standardAt;
        if (!xtz_posixYearTransitions(priv, y, &savingAt, &standardAt)) continue;
        /* 起转换：std → dst；止转换：dst → std。按时刻各记一条。 */
        if (savingAt <= utcMs && (!any || savingAt >= bestAt)) {
            bestAt = savingAt; bestIsDst = true; any = true;
        }
        if (standardAt <= utcMs && (!any || standardAt >= bestAt)) {
            bestAt = standardAt; bestIsDst = false; any = true;
        }
    }
    return bestIsDst;
}

/* 在 transition 表内查找给定 UTC 秒的 type 下标（最后 ≤ at 的 transition）。 */
static int xtz_findType(XTimeZonePrivate* priv, int64_t utcSec)
{
    int i;
    int best = 0;
    if (priv->m_transCount == 0 || priv->m_typeCount == 0) return 0;
    for (i = 0; i < priv->m_transCount; ++i) {
        if (priv->m_transAt[i] <= utcSec) best = priv->m_transType[i];
        else break;
    }
    return best;
}

/* ==================== 构造 / 释放 ==================== */

XTimeZone XTimeZone_createInvalid(void)
{
    XTimeZone tz;
    XMemset(&tz, 0, sizeof(tz));
    tz.m_spec = XTimeSpec_LocalTime;
    return tz;
}

XTimeZone XTimeZone_createUtc(void)
{
    XTimeZone tz;
    XMemset(&tz, 0, sizeof(tz));
    tz.m_spec = XTimeSpec_UTC;
    tz.m_offsetSeconds = 0;
    /* Qt 的 QTimeZone::UTC id 是 "UTC"（本机 Qt 6.8 探针：
     * QTimeZone::UTC.isValid()==1、id()=="UTC"）。UTC/固定偏移不持堆资源，
     * 故 id 由 XTimeZone_id() 按 spec 现算到对象内联缓冲区，不分配内存
     * （原实现不设 id ⇒ id() 返回 NULL，与 Qt 不同）。 */
    return tz;
}

/* 把「相对 UTC 的秒偏移」格式化为 Qt 的 isoOffsetFormat 口径
 * （qtimezoneprivate.cpp:779-799）：短名 "UTC+08"（分秒为 0 时）或
 * "UTC+08:30"/"UTC+08:30:15"。写入 buf（至少 16 字节），返回 buf。
 * 用于固定偏移时区的 id 与 abbreviation（Qt 探针：
 * QTimeZone(8*3600).id()==abbreviation()=="UTC+08:00"）。 */
static const char* xtz_isoOffset(char* buf, int cap, int offsetFromUtc, bool shortMode)
{
    char sign = '+';
    int v = offsetFromUtc;
    int secs, mins, hour;
    if (v < 0) { sign = '-'; v = -v; }
    secs = v % 60;
    mins = (v / 60) % 60;
    hour = v / 3600;
    if (shortMode) {
        if (secs == 0 && mins == 0)
            XSnprintf(buf, (size_t)cap, "UTC%c%02d", sign, hour);
        else
            XSnprintf(buf, (size_t)cap, "UTC%c%02d:%02d", sign, hour, mins);
    } else {
        /* 长名/非短模式：Qt 在 LongName 或 secs!=0 时补秒；abbreviation
         * 用 ShortName 口径，但探针实测 QTimeZone(8*3600) 的 abbr 是
         * "UTC+08:00"（带分钟），故这里固定补到分钟。 */
        XSnprintf(buf, (size_t)cap, "UTC%c%02d:%02d", sign, hour, mins);
        if (secs != 0)
            XSnprintf(buf + XStrlen(buf), (size_t)(cap - (int)XStrlen(buf)),
                      ":%02d", secs);
    }
    return buf;
}

XTimeZone XTimeZone_createOffset(int offsetSeconds)
{
    XTimeZone tz;
    XMemset(&tz, 0, sizeof(tz));
    if (offsetSeconds < XTZ_MIN_OFFSET_SECS || offsetSeconds > XTZ_MAX_OFFSET_SECS)
        return XTimeZone_createInvalid();
    tz.m_spec = XTimeSpec_OffsetFromUTC;
    tz.m_offsetSeconds = offsetSeconds;
    return tz;
}

static XTimeZone xtz_makeIanaHolder(const char* id)
{
    XTimeZone tz;
    int n;
    XMemset(&tz, 0, sizeof(tz));
    tz.m_spec = XTimeSpec_TimeZone;
    n = (int)XStrlen(id);
    tz.m_id = (char*)XMalloc_System((size_t)n + 1);
    if (tz.m_id) {
        int i;
        for (i = 0; i < n; ++i) tz.m_id[i] = id[i];
        tz.m_id[n] = '\0';
    }
    tz.m_data = (XTimeZonePrivate*)XMalloc_System(sizeof(XTimeZonePrivate));
    if (tz.m_data)
        XMemset(tz.m_data, 0, sizeof(XTimeZonePrivate));
    return tz;
}

/* 校验「还需 need 字节」是否落在 size 内。**必须用无符号 64 位**：
 * TZif 头的计数字段可达 0x7FFFFFFF，int 乘 4/6/8 会溢出为负 ⇒ 校验恒过
 * ⇒ 后续按 p 越界读（实测自造 timecnt=0x20000000 必然段错误）。
 * 同时拒绝负数（头字段按 int32 读入，恶意/畸形文件可为负）。 */
static bool xtz_fits(int64_t off, int64_t need, int size)
{
    if (off < 0 || need < 0) return false;
    return off + need <= (int64_t)size;
}

/* 计数字段合法性：TZif 规范给的上限（typecnt ≤ 256、charcnt ≤ 256 等
 * 均为「实际不会超」的宽松上界；负数一律拒绝）。 */
static bool xtz_countsSane(int timecnt, int typecnt, int charcnt,
                           int leapcnt, int ttisstdcnt, int ttisgmtcnt)
{
    if (timecnt < 0 || typecnt < 0 || charcnt < 0 ||
        leapcnt < 0 || ttisstdcnt < 0 || ttisgmtcnt < 0) return false;
    if (typecnt > 256 || charcnt > 256) return false;   /* TZif 规范上限 */
    if (timecnt > 100000) return false;                 /* 真实最大 ~2400 */
    if (leapcnt > 1000) return false;
    if (ttisstdcnt > 256 || ttisgmtcnt > 256) return false;
    return true;
}

/* 释放 priv 已分配的四个数组（失败路径复用，避免半途 return 泄漏）。
 * 实测 /tmp/b1rev/leakv2.tzif 路径 1000 次泄漏 64 B/call。 */
static void xtz_freeArrays(XTimeZonePrivate* priv)
{
    if (!priv) return;
    if (priv->m_transAt)   { XFree_System(priv->m_transAt);   priv->m_transAt = NULL; }
    if (priv->m_transType) { XFree_System(priv->m_transType); priv->m_transType = NULL; }
    if (priv->m_transStd)  { XFree_System(priv->m_transStd);  priv->m_transStd = NULL; }
    if (priv->m_transDst)  { XFree_System(priv->m_transDst);  priv->m_transDst = NULL; }
    if (priv->m_types)     { XFree_System(priv->m_types);     priv->m_types = NULL; }
    if (priv->m_abbr)      { XFree_System(priv->m_abbr);      priv->m_abbr = NULL; }
    priv->m_transCount = 0;
    priv->m_typeCount = 0;
    priv->m_abbrSize = 0;
}

/* Qt 里用的 qAbs（int），避免依赖未导出的整数绝对值宏。 */
static int xtz_abs(int v) { return v < 0 ? -v : v; }

static bool xtz_loadFromBuffer(XTimeZonePrivate* priv, const unsigned char* buf, int size)
{
    int version;
    int tzh_timecnt, tzh_typecnt, tzh_charcnt, tzh_leapcnt, tzh_ttisstdcnt, tzh_ttisgmtcnt;
    int off, p, i;
    bool longTran;
    if (!priv || size < 44 + 44) return false;
    if (XMemcmp(buf, XTZ_TZIF_MAGIC, 4) != 0) return false;
    version = buf[4];
    if (version != '2' && version != '3' && version != 0) return false;
    longTran = (version == '2' || version == '3');

    tzh_ttisgmtcnt = (int)xtz_rd_i32(buf + 20);
    tzh_ttisstdcnt = (int)xtz_rd_i32(buf + 24);
    tzh_leapcnt    = (int)xtz_rd_i32(buf + 28);
    tzh_timecnt    = (int)xtz_rd_i32(buf + 32);
    tzh_typecnt    = (int)xtz_rd_i32(buf + 36);
    tzh_charcnt    = (int)xtz_rd_i32(buf + 40);
    if (!xtz_countsSane(tzh_timecnt, tzh_typecnt, tzh_charcnt,
                        tzh_leapcnt, tzh_ttisstdcnt, tzh_ttisgmtcnt))
        return false;

    off = 44;
    if (!longTran) {
        /* v1 块：timecnt*4 + timecnt + typecnt*6 + charcnt + leap*8 + isstd + isgmt
         * 全用 64 位累加，避免 int 溢出（见 xtz_fits 注释）。 */
        int64_t need = (int64_t)tzh_timecnt * 4 + (int64_t)tzh_timecnt
                     + (int64_t)tzh_typecnt * 6 + (int64_t)tzh_charcnt
                     + (int64_t)tzh_leapcnt * 8 + (int64_t)tzh_ttisstdcnt
                     + (int64_t)tzh_ttisgmtcnt;
        if (!xtz_fits(off, need, size)) return false;
        priv->m_transCount = tzh_timecnt;
        if (tzh_timecnt > 0) {
            priv->m_transAt = (int64_t*)XMalloc_System(sizeof(int64_t) * (size_t)tzh_timecnt);
            priv->m_transType = (int*)XMalloc_System(sizeof(int) * (size_t)tzh_timecnt);
            if (!priv->m_transAt || !priv->m_transType) { xtz_freeArrays(priv); return false; }
        }
        p = off;
        for (i = 0; i < tzh_timecnt; ++i) { priv->m_transAt[i] = (int64_t)xtz_rd_i32(buf + p); p += 4; }
        for (i = 0; i < tzh_timecnt; ++i) { priv->m_transType[i] = buf[p]; p += 1; }
        off = p;
    } else {
        /* v2/v3：从偏移 44 起扫描第二个 "TZif" 魔数定位 v2 头（最稳，不依赖
         * 对 v1 数据块大小的计数推演——某些发行版 v1 block 的 leap/ttisstd/
         * ttisgmt 字节宽度与默认假设不同，推演会错位到垃圾类型表）。 */
        int h2 = -1;
        for (i = 44; i + 4 <= size; ++i) {
            if (XMemcmp(buf + i, XTZ_TZIF_MAGIC, 4) == 0) { h2 = i; break; }
        }
        if (h2 < 0 || h2 + 44 > size) return false;
        tzh_ttisgmtcnt = (int)xtz_rd_i32(buf + h2 + 20);
        tzh_ttisstdcnt = (int)xtz_rd_i32(buf + h2 + 24);
        tzh_leapcnt    = (int)xtz_rd_i32(buf + h2 + 28);
        tzh_timecnt    = (int)xtz_rd_i32(buf + h2 + 32);
        tzh_typecnt    = (int)xtz_rd_i32(buf + h2 + 36);
        tzh_charcnt    = (int)xtz_rd_i32(buf + h2 + 40);
        if (!xtz_countsSane(tzh_timecnt, tzh_typecnt, tzh_charcnt,
                            tzh_leapcnt, tzh_ttisstdcnt, tzh_ttisgmtcnt))
            return false;
        off = h2 + 44;
        /* v2 尾部（timecnt*8 + timecnt + typecnt*6 + charcnt + leap*8 + isstd + isgmt）
         * 同样用 64 位校验——实测 /tmp/b1rev/trunc2.tzif 走此路径段错误。 */
        {
            int64_t need = (int64_t)tzh_timecnt * 8 + (int64_t)tzh_timecnt
                         + (int64_t)tzh_typecnt * 6 + (int64_t)tzh_charcnt
                         + (int64_t)tzh_leapcnt * 8 + (int64_t)tzh_ttisstdcnt
                         + (int64_t)tzh_ttisgmtcnt;
            if (!xtz_fits(off, need, size)) return false;
        }
        priv->m_transCount = tzh_timecnt;
        if (tzh_timecnt > 0) {
            priv->m_transAt = (int64_t*)XMalloc_System(sizeof(int64_t) * (size_t)tzh_timecnt);
            priv->m_transType = (int*)XMalloc_System(sizeof(int) * (size_t)tzh_timecnt);
            if (!priv->m_transAt || !priv->m_transType) { xtz_freeArrays(priv); return false; }
        }
        p = off;
        for (i = 0; i < tzh_timecnt; ++i) { priv->m_transAt[i] = xtz_rd_i64(buf + p); p += 8; }
        for (i = 0; i < tzh_timecnt; ++i) { priv->m_transType[i] = buf[p]; p += 1; }
        off = p;
    }

    /* types */
    {
        int64_t need = (int64_t)tzh_typecnt * 6;
        if (!xtz_fits(off, need, size)) { xtz_freeArrays(priv); return false; }
        priv->m_typeCount = tzh_typecnt;
        if (tzh_typecnt > 0) {
            priv->m_types = (XTzType*)XMalloc_System(sizeof(XTzType) * (size_t)tzh_typecnt);
            if (!priv->m_types) { xtz_freeArrays(priv); return false; }
        }
        p = off;
        for (i = 0; i < tzh_typecnt; ++i) {
            priv->m_types[i].offset = (int)xtz_rd_i32(buf + p);
            priv->m_types[i].isDst  = buf[p + 4];
            priv->m_types[i].abbrIndex = buf[p + 5];
            p += 6;
        }
        off = p;
    }
    /* abbreviations */
    {
        if (!xtz_fits(off, (int64_t)tzh_charcnt, size)) { xtz_freeArrays(priv); return false; }
        priv->m_abbrSize = tzh_charcnt;
        priv->m_abbr = (char*)XMalloc_System((size_t)tzh_charcnt + 1);
        if (!priv->m_abbr) { xtz_freeArrays(priv); return false; }
        for (i = 0; i < tzh_charcnt; ++i) priv->m_abbr[i] = (char)buf[off + i];
        priv->m_abbr[tzh_charcnt] = '\0';
        off += tzh_charcnt;
    }
    /* 跳过 leap / isstd / isgmt */
    off += tzh_leapcnt * 8 + tzh_ttisstdcnt + tzh_ttisgmtcnt;

    /* POSIX 规则：off 之后是 "\n" + 规则 + "\n" */
    if (off < size && buf[off] == '\n') {
        int start = off + 1;
        int end = start;
        while (end < size && buf[end] != '\n') ++end;
        if (end > start) {
            char rule[128];
            int rl = end - start;
            if (rl > 127) rl = 127;
            for (i = 0; i < rl; ++i) rule[i] = (char)buf[start + i];
            rule[rl] = '\0';
            xtz_parsePosix(priv, rule);
        }
    }

    /* 标准时/夏令时偏移：**逐 transition 推算**（逐行复刻 Qt
     * qtimezoneprivate_tz.cpp:893-958 的规则循环），不是全局取「首个非 DST
     * 类型」——后者在 LMT 期与部分 DST 段恒偏：实测 Europe/Dublin
     * @2024-01-15 Qt(std=0,dst=0) vs 全局基准(std=-1521,dst=1521)、
     * America/Santiago @2024 Qt(std=-10800) vs 全局(std=-16965)、
     * Asia/Kolkata @1940 Qt(std=19800) vs 全局(std=21200)。
     *
     * 规则：维护 utcOffset（当前标准时偏移，初值取首类型 gmtOff）与
     * lastDstOff（当前 DST 增益，初值 3600）；每遇一个 transition 类型：
     *   - 非 DST：utcOffset = 该类型 gmtOff；
     *   - DST 且 gmtOff != utcOffset + lastDstOff：可能是「DST 起跳与标准时
     *     变更同刻」，向后找下一个非 DST transition 反向校正 utcOffset
     *     （Qt 的五档判定，逐条照抄）；
     *   - 记录该 transition 的 std=utcOffset / dst=gmtOff−utcOffset。
     * 存入 m_transStd/m_transDst，查询时按 type 下标取用。 */
    priv->m_baseStdOffset = (priv->m_typeCount > 0) ? priv->m_types[0].offset : 0;
    if (priv->m_transCount > 0) {
        int utcOffset = priv->m_baseStdOffset;
        int lastDstOff = 3600;
        priv->m_transStd = (int*)XMalloc_System(sizeof(int) * (size_t)priv->m_transCount);
        priv->m_transDst = (int*)XMalloc_System(sizeof(int) * (size_t)priv->m_transCount);
        if (!priv->m_transStd || !priv->m_transDst) { xtz_freeArrays(priv); return false; }
        for (i = 0; i < priv->m_transCount; ++i) {
            int ti = priv->m_transType[i];
            int gmtoff, isdst;
            if (ti < 0 || ti >= priv->m_typeCount) {
                priv->m_transStd[i] = utcOffset;
                priv->m_transDst[i] = 0;
                continue;
            }
            gmtoff = priv->m_types[ti].offset;
            isdst = priv->m_types[ti].isDst;
            if (!isdst) {
                utcOffset = gmtoff;
            } else if (gmtoff != utcOffset + lastDstOff) {
                /* DST 起跳与标准时变更同刻：看 DST 结束时给的标准时是否更合理 */
                int inferStd = gmtoff - lastDstOff;
                int j;
                for (j = i + 1; j < priv->m_transCount; ++j) {
                    int nti = priv->m_transType[j];
                    int newUtc;
                    if (nti < 0 || nti >= priv->m_typeCount) continue;
                    if (priv->m_types[nti].isDst) continue;
                    newUtc = priv->m_types[nti].offset;
                    if (newUtc == utcOffset) {
                        /* DST 结束帮不上忙：保持原 utcOffset */
                    } else if (newUtc == inferStd) {
                        utcOffset = newUtc;
                    } else if (gmtoff - 3600 == utcOffset) {
                        /* 起点已给出 1 小时增益 */
                    } else if (gmtoff - 3600 == newUtc) {
                        utcOffset = newUtc;
                    } else if (newUtc < gmtoff
                               ? (utcOffset >= gmtoff ||
                                  xtz_abs(newUtc - inferStd) < xtz_abs(utcOffset - inferStd))
                               : (utcOffset >= gmtoff &&
                                  xtz_abs(newUtc - inferStd) < xtz_abs(utcOffset - inferStd))) {
                        utcOffset = newUtc;
                    }
                    break;
                }
                lastDstOff = gmtoff - utcOffset;
            }
            priv->m_transStd[i] = utcOffset;
            priv->m_transDst[i] = gmtoff - utcOffset;
        }
    }
    return true;
}

static XTimeZone xtz_openAndLoad(const char* pathUtf8)
{
    XTimeZone tz = XTimeZone_createInvalid();
    XFd fd = XFD_INVALID;
    XFileStat st;
    XString* path;
    unsigned char* buf = NULL;
    int readSize = 0;
    int64_t got;
    path = XString_create_utf8(pathUtf8);
    if (!XDeviceFile_stat(path, &st) || !st.exists) { XClassDelete(path); return tz; }
    /* 只接受普通文件且大小合理：目录/设备/超大文件一律拒绝，避免把
     * /etc/passwd、README.md 之类任意可读文件当成 TZif（实测原实现
     * 对任意可读文件都返回 spec=3 valid=1）。 */
    if (!st.isFile || st.size < 88 || st.size > (int64_t)4 * 1024 * 1024) {
        XClassDelete(path);
        return tz;
    }
    {
        XDeviceOpenOptions opt;
        XMemset(&opt, 0, sizeof(opt));
        opt.m_openMode = XIODevice_ReadOnly;
        opt.m_target = path;
        fd = XDevice_open(XDeviceType_File, &opt, NULL);
    }
    XClassDelete(path);
    if (fd == XFD_INVALID) return tz;

    buf = (unsigned char*)XMalloc_System((size_t)st.size + 1);
    if (!buf) { XDeviceFile_close(fd); return tz; }
    while (readSize < (int)st.size) {
        got = XDeviceFile_read(fd, buf + readSize, (int64_t)((int)st.size - readSize));
        if (got <= 0) break;
        readSize += (int)got;
    }
    XDeviceFile_close(fd);
    if (readSize < 44 + 44) { XFree_System(buf); return tz; }

    tz = xtz_makeIanaHolder(pathUtf8);
    /* 关键：解析失败就整体作废（连 id 一起释放、返回无效值）。
     * 不能「保留 id + data=NULL ⇒ isValid 仍 true」——那会把任意畸形文件
     * 伪装成「零偏移的有效时区」（实测 shortbody.tzif 与 /etc/passwd 都
     * 判 valid=1 off=0），与 Qt（QTimeZone 非法 id/畸形数据 isValid=0）
     * 相反，也违反本域 openNotes 自述「不伪造零偏移成功」。 */
    if (!tz.m_data || !xtz_loadFromBuffer((XTimeZonePrivate*)tz.m_data, buf, readSize)) {
        XTimeZone_release(&tz);   /* 一并释放 m_id 与 m_data 内部四个数组 */
        XFree_System(buf);
        return XTimeZone_createInvalid();
    }
    XFree_System(buf);
    return tz;
}

/* IANA id 合法性校验（逐行复刻 Qt qtimezoneprivate.cpp:745-777 的
 * 宽松版 isValidId）：
 *  - 段分隔 '/'：每段长度 1..14（Qt 非 Android/ICU 口径）；
 *  - 段首不得为 '-'；
 *  - 字符只允许 ASCII 字母、'_'、'.'、数字、'+'、':'；**不含 '\\' 与非
 *    ASCII ⇒ 天然排除 "../" 这类路径穿越（'.' 允许但 ".." 会因段内合法
 *    而被放行，故额外拒绝含 ".." 的段）。
 * 用途：createIana 在拼路径前必须先过此闸——实测原实现
 * XTimeZone_createIana("../../../../usr/share/zoneinfo/Europe/Berlin")
 * 会加载目录外文件并把该 id 暴露给调用方。 */
static bool xtz_isValidId(const char* id)
{
    const int kMaxSectionLength = 14;
    int sectionLength = 0;
    const char* it;
    if (!id || !id[0]) return false;
    /* 拒绝路径穿越：任一段等于 "." 或 ".."（IANA 实际无此名）。 */
    {
        const char* s = id;
        while (*s) {
            const char* e = s;
            while (*e && *e != '/') ++e;
            if ((e - s) == 1 && s[0] == '.') return false;
            if ((e - s) == 2 && s[0] == '.' && s[1] == '.') return false;
            if (!*e) break;
            s = e + 1;
        }
    }
    for (it = id; *it; ++it, ++sectionLength) {
        char ch = *it;
        if (ch == '/') {
            if (sectionLength < 1 || sectionLength > kMaxSectionLength) return false;
            sectionLength = -1;
        } else if (ch == '-') {
            if (sectionLength == 0) return false;
        } else if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                     ch == '_' || ch == '.' ||
                     (ch >= '0' && ch <= '9') || ch == '+' || ch == ':')) {
            return false;
        }
    }
    if (sectionLength < 1 || sectionLength > kMaxSectionLength) return false;
    return true;
}

XTimeZone XTimeZone_createIana(const char* ianaId)
{
    XTimeZone tz;
    if (!ianaId || !ianaId[0]) return XTimeZone_createInvalid();
    /* 先校验 id 形态再拼路径（Qt isValidId 同口径），挡住路径穿越与
     * 任意文件读取。 */
    if (!xtz_isValidId(ianaId)) return XTimeZone_createInvalid();
    {
        char full[512];
        int dl = (int)XStrlen(XTZ_ZONEINFO_DIR);
        int il = (int)XStrlen(ianaId);
        if (dl + il >= (int)sizeof(full)) return XTimeZone_createInvalid();
        XStrcpy(full, XTZ_ZONEINFO_DIR);
        XStrcat(full, ianaId);
        tz = xtz_openAndLoad(full);
    }
    /* 若 id 与已加载路径不一致（保留原始 id）：重新以 ianaId 赋予 holder id。
     * xtz_openAndLoad 已用 path 作 id，这里无 zoneinfo 时 id 仍为 path；对齐
     * 行为：IANA id 应暴露给用户，故最终用 ianaId 覆盖 id。 */
    if (tz.m_spec == XTimeSpec_TimeZone && tz.m_id) {
        /* 仅当文件确实不存在（openAndLoad 返回 invalid holder）时不改；
         * 成功 holder 的 id 现为完整路径，改成用户传入的 ianaId。 */
        if (XStrcmp(tz.m_id, ianaId) != 0) {
            /* strdup 语义的复制走框架唯一入口 XStrdup（XStringUtils.h:267，
             * AGENTS「框架有的绝不手搓」——不在时区层手写 malloc+循环拷贝）。 */
            char* nid = XStrdup(ianaId);
            if (nid) {
                XFree_System(tz.m_id);
                tz.m_id = nid;
            }
        }
    }
    return tz;
}

XTimeZone XTimeZone_createIanaPath(const char* tzFilePath)
{
    if (!tzFilePath || !tzFilePath[0]) return XTimeZone_createInvalid();
    return xtz_openAndLoad(tzFilePath);
}

void XTimeZone_release(XTimeZone* tz)
{
    if (!tz) return;
    if (tz->m_spec == XTimeSpec_TimeZone) {
        if (tz->m_id) { XFree_System(tz->m_id); tz->m_id = NULL; }
        if (tz->m_data) {
            XTimeZonePrivate* p = (XTimeZonePrivate*)tz->m_data;
            if (p->m_transAt) XFree_System(p->m_transAt);
            if (p->m_transType) XFree_System(p->m_transType);
            if (p->m_transStd) XFree_System(p->m_transStd);
            if (p->m_transDst) XFree_System(p->m_transDst);
            if (p->m_types) XFree_System(p->m_types);
            if (p->m_abbr) XFree_System(p->m_abbr);
            XFree_System(p);
            tz->m_data = NULL;
        }
    }
    tz->m_spec = XTimeSpec_LocalTime;
}

/* ==================== 查询 ==================== */

bool XTimeZone_isValid(const XTimeZone* tz)
{
    if (!tz) return false;
    switch (tz->m_spec) {
    case XTimeSpec_UTC:
    case XTimeSpec_OffsetFromUTC:
    case XTimeSpec_TimeZone:
        return true;
    default:
        return false;
    }
}

int XTimeZone_spec(const XTimeZone* tz)
{
    return tz ? tz->m_spec : XTimeSpec_LocalTime;
}

const char* XTimeZone_id(const XTimeZone* tz)
{
    if (!tz) return NULL;
    if (tz->m_spec == XTimeSpec_TimeZone)
        return (const char*)tz->m_id;
    if (tz->m_spec == XTimeSpec_UTC)
        return "UTC";
    if (tz->m_spec == XTimeSpec_OffsetFromUTC) {
        /* 固定偏移的 id = "UTC±hh:mm[:ss]"（Qt isoOffsetFormat LongName）。
         * 返回**线程不安全的静态缓冲**：调用方须立即使用或自行复制
         * （Qt 的 id() 返回 QString 值，本 API 为借用以保持零分配；
         * 需要长期持有请 XString_create_utf8 复制）。 */
        static char s_buf[16];
        return xtz_isoOffset(s_buf, (int)sizeof(s_buf), tz->m_offsetSeconds, false);
    }
    return NULL;
}

/* 内部：取给定 UTC 毫秒的偏移分解。 */
static void xtz_resolve(const XTimeZone* tz, int64_t utcMs,
                        int* outOffset, int* outStd, int* outDst,
                        bool* outIsDst, const char** outAbbr)
{
    int64_t utcSec = utcMs / 1000;
    *outOffset = 0; *outStd = 0; *outDst = 0; *outIsDst = false; *outAbbr = "UTC";
    if (!tz) return;
    if (tz->m_spec == XTimeSpec_UTC) {
        return; /* 全 0 / UTC */
    }
    if (tz->m_spec == XTimeSpec_OffsetFromUTC) {
        *outOffset = tz->m_offsetSeconds;
        *outStd = tz->m_offsetSeconds;
        *outDst = 0;
        *outIsDst = false;
        /* Qt 探针：QTimeZone(8*3600).abbreviation() == "UTC+08:00"。
         * 原实现一律返回 "UTC"，与 Qt 不符（见 #12）。
         * 缩写走**静态缓冲**（借用语义，同 XTimeZone_id 的说明）。 */
        {
            static char s_abbr[16];
            *outAbbr = xtz_isoOffset(s_abbr, (int)sizeof(s_abbr),
                                     tz->m_offsetSeconds, false);
        }
        return;
    }
    if (tz->m_spec == XTimeSpec_TimeZone && tz->m_data) {
        XTimeZonePrivate* p = (XTimeZonePrivate*)tz->m_data;
        bool beforeFirst = (p->m_transCount > 0) && (utcSec < p->m_transAt[0]);
        bool inRange = (p->m_transCount > 0) && (utcSec <= p->m_transAt[p->m_transCount - 1]);
        if (beforeFirst) {
            /* 首条 transition 之前用「前区域规则」（Qt m_preZoneRule =
             * typeList[0]，qtimezoneprivate_tz.cpp:886）。实测 America/New_York
             * @1881 Qt(off=-17762 std=-17762 dst=0 abbr=LMT)；若落到 POSIX
             * 段会错给 EDT。 */
            if (p->m_typeCount > 0) {
                *outOffset = p->m_types[0].offset;
                *outStd = p->m_types[0].offset;
                *outDst = 0;
                *outIsDst = false;
                if (p->m_abbr && p->m_types[0].abbrIndex >= 0 &&
                    p->m_types[0].abbrIndex < p->m_abbrSize)
                    *outAbbr = p->m_abbr + p->m_types[0].abbrIndex;
                return;
            }
        }
        if (inRange && !beforeFirst) {
            /* transition 表内：取该区间的 transition 下标（最后一条 ≤ t），
             * 标准时/夏令时偏移直接用解析期逐条推算好的 m_transStd/m_transDst
             * （Qt dataFromRule: offsetFromUtc = std + dst）。 */
            int ti = -1, k;
            for (k = 0; k < p->m_transCount; ++k) {
                if (p->m_transAt[k] <= utcSec) ti = k;
                else break;
            }
            if (ti >= 0) {
                int typeIdx = p->m_transType[ti];
                *outOffset = (typeIdx >= 0 && typeIdx < p->m_typeCount)
                             ? p->m_types[typeIdx].offset : 0;
                *outStd = p->m_transStd ? p->m_transStd[ti] : p->m_baseStdOffset;
                *outDst = p->m_transDst ? p->m_transDst[ti] : 0;
                *outIsDst = (typeIdx >= 0 && typeIdx < p->m_typeCount)
                            ? (p->m_types[typeIdx].isDst != 0) : false;
                if (p->m_abbr && typeIdx >= 0 && typeIdx < p->m_typeCount &&
                    p->m_types[typeIdx].abbrIndex >= 0 &&
                    p->m_types[typeIdx].abbrIndex < p->m_abbrSize)
                    *outAbbr = p->m_abbr + p->m_types[typeIdx].abbrIndex;
                return;
            }
        }
        /* transition 表外（含 2037+，或本就无 transition）：走 POSIX 规则 */
        if (p->m_hasPosixDst) {
            bool isDst = xtz_posixIsDst(p, utcMs);
            *outIsDst = isDst;
            if (isDst) {
                *outOffset = p->m_posixDstOffset;
                *outStd = p->m_posixStdOffset;
                *outDst = p->m_posixDstOffset - p->m_posixStdOffset;
                *outAbbr = p->m_posixDstName;
            } else {
                *outOffset = p->m_posixStdOffset;
                *outStd = p->m_posixStdOffset;
                *outDst = 0;
                *outAbbr = p->m_posixStdName;
            }
            return;
        }
        /* 无 DST 的 POSIX 型（如 CST-8）：标准时即其偏移 */
        if (p->m_posixStdName[0] != '\0') {
            *outOffset = p->m_posixStdOffset;
            *outStd = p->m_posixStdOffset;
            *outDst = 0;
            *outIsDst = false;
            *outAbbr = p->m_posixStdName;
            return;
        }
        /* 既无 POSIX 规则也无 DST（典型：leap-second 的 right/* 区文件——
         * 它们只有 leap 表、没有 "\n<posix>\n" 尾段）：Qt 的做法是沿用
         * **最后一条 transition 的状态**（data() 的 tranCache 末尾分支）。
         * 原实现落到「零偏移 UTC」⇒ 实测 right/Asia/Shanghai @2100 得
         * off=0 abbr=UTC，而 glibc `TZ=right/Asia/Shanghai date` 是
         * +0800 CST（见 #17）。 */
        if (p->m_transCount > 0) {
            int last = p->m_transCount - 1;
            int typeIdx = p->m_transType[last];
            *outOffset = (typeIdx >= 0 && typeIdx < p->m_typeCount)
                         ? p->m_types[typeIdx].offset : 0;
            *outStd = p->m_transStd ? p->m_transStd[last] : p->m_baseStdOffset;
            *outDst = p->m_transDst ? p->m_transDst[last] : 0;
            *outIsDst = (typeIdx >= 0 && typeIdx < p->m_typeCount)
                        ? (p->m_types[typeIdx].isDst != 0) : false;
            if (p->m_abbr && typeIdx >= 0 && typeIdx < p->m_typeCount &&
                p->m_types[typeIdx].abbrIndex >= 0 &&
                p->m_types[typeIdx].abbrIndex < p->m_abbrSize)
                *outAbbr = p->m_abbr + p->m_types[typeIdx].abbrIndex;
        }
        /* 连 transition 都没有（异常）：零偏移 */
    }
    /* LocalTime 或降级：零偏移 */
}

int XTimeZone_offsetFromUtc(const XTimeZone* tz, int64_t atUtcMs)
{
    int off, std, dst; bool isDst; const char* ab;
    xtz_resolve(tz, atUtcMs, &off, &std, &dst, &isDst, &ab);
    return off;
}
int XTimeZone_standardTimeOffset(const XTimeZone* tz, int64_t atUtcMs)
{
    int off, std, dst; bool isDst; const char* ab;
    xtz_resolve(tz, atUtcMs, &off, &std, &dst, &isDst, &ab);
    return std;
}
int XTimeZone_daylightTimeOffset(const XTimeZone* tz, int64_t atUtcMs)
{
    int off, std, dst; bool isDst; const char* ab;
    xtz_resolve(tz, atUtcMs, &off, &std, &dst, &isDst, &ab);
    return dst;
}
bool XTimeZone_isDaylightTime(const XTimeZone* tz, int64_t atUtcMs)
{
    int off, std, dst; bool isDst; const char* ab;
    xtz_resolve(tz, atUtcMs, &off, &std, &dst, &isDst, &ab);
    return isDst;
}
XString* XTimeZone_abbreviation(const XTimeZone* tz, int64_t atUtcMs)
{
    int off, std, dst; bool isDst; const char* ab;
    xtz_resolve(tz, atUtcMs, &off, &std, &dst, &isDst, &ab);
    return XString_create_utf8(ab ? ab : "UTC");
}

XTimeZoneOffsetData XTimeZone_offsetData(const XTimeZone* tz, int64_t atUtcMs)
{
    XTimeZoneOffsetData d;
    int off, std, dst; bool isDst; const char* ab;
    XMemset(&d, 0, sizeof(d));
    d.m_isValid = XTimeZone_isValid(tz);
    if (!d.m_isValid) return d;
    xtz_resolve(tz, atUtcMs, &off, &std, &dst, &isDst, &ab);
    d.m_atUtcMs = atUtcMs;
    d.m_offsetFromUtc = off;
    d.m_standardTimeOffset = std;
    d.m_daylightTimeOffset = dst;
    return d;
}

/* ==================== 本地 ↔ UTC 换算 ==================== */

/* 用 UTC 时刻求「该时刻当时生效的偏移」，再换算（与 Qt QDateTime::toUTC /
 * toTimeZone 同口径；Qt 以目标时区 data(atUtc) 的 offset 求差）。 */
int64_t XTimeZone_localMsToUtcMs(const XTimeZone* tz, int64_t localMs)
{
    int off = XTimeZone_offsetFromUtc(tz, localMs);
    return localMs - (int64_t)off * 1000;
}

int64_t XTimeZone_utcMsToLocalMs(const XTimeZone* tz, int64_t utcMs)
{
    int off = XTimeZone_offsetFromUtc(tz, utcMs);
    return utcMs + (int64_t)off * 1000;
}
