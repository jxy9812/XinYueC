/**
 * @file       HsfLinkHistory.c
 * @brief      历史分页（0x0118）应答解析实现。
 * @details    [为什么格式化放在客户端] 服务端只发原始值（numeric + 模式文本
 *             + 单位文本），显示口径（"--"、小数位、单位拼接、位图十六进制）
 *             集中在界面这一处，将来改显示不用动协议与两端服务端。
 *             [口径对齐 Python] 与 Python 客户端 pages_other.py 的
 *             _server_row_texts 逐项对应（列序/格式/异常判据）。
 */
#include "HsfLinkHistory.h"

#include "XBson.h"
#include "XBsonArray.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XDate.h"
#include "XPrintf.h"
#include "XDateTime.h"
#include "XTime.h"
#include "XString.h"
#include <stdio.h>
#include <string.h>

/** @brief 取一格的文本值（NULL 变体 → 空串）。 */
static void hsfHistText(const XBsonValue* v, char* out, int cap)
{
    const XString* s;
    if (out && cap > 0) out[0] = '\0';
    if (!v) return;
    s = XBsonValue_toString(v);
    if (s) {
        const char* utf8 = XString_toUtf8(s);
        if (utf8 && out && cap > 0) snprintf(out, (size_t)cap, "%s", utf8);
    }
}

/** @brief 取一格的数值（无数据 → def）。 */
static double hsfHistNum(const XBsonValue* v, double def)
{
    if (!v) return def;
    if (XBsonValue_isNull(v)) return def;
    return XBsonValue_toDouble(v, def);
}

/** @brief 该格是否"没有数据"（NULL 变体或缺列）。 */
static bool hsfHistIsNull(const XBsonValue* v)
{
    return (!v || XBsonValue_isNull(v));
}

/**
 * @brief epoch 毫秒 → "yyyy-MM-dd HH:mm:ss" 文本（本地时区）。
 * @details [为什么绕这一圈] 框架的 XDate/XTime 是**日历型**（无
 *          "epoch → XDateTime" 的构造口），而历史行的 ts 是 epoch 毫秒。
 *          故用"当前本地时刻 − 该行年龄"反推：日期部分减整天数、时间部分
 *          减当天毫秒（XDate_addDays / XTime_addMSecs 均支持负值 ✓）。
 * @param ts epoch 毫秒（墙钟）。
 * @param out 输出缓冲（UTF-8；不能为 NULL）。
 * @param cap 缓冲容量。
 * @return 无。
 */
static void hsfHistFormatTs(int64_t ts, char* out, int cap)
{
    int64_t age;
    int64_t days;
    int remMs;
    XDate date;
    XTime time;
    XDateTime dt;
    XString* txt;

    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (ts <= 0) {
        snprintf(out, (size_t)cap, "--");
        return;
    }
    age = XDateTime_currentWallMSecsSinceEpoch() - ts;
    if (age < 0) age = 0;
    days = age / 86400000LL;
    remMs = (int)(age % 86400000LL);
    date = XDate_currentDate();
    time = XTime_currentTime();
    date = XDate_addDays(&date, -days);
    time = XTime_addMSecs(&time, -remMs);
    /* [自己拼格式] 框架的 XDateTime_toString_format **不补零**（"17:54:4"），
     * 与 Python 的 strftime（"17:54:04"）不一致 ⇒ 这里手工拼（口径对齐）。 */
    snprintf(out, (size_t)cap, "%04d-%02d-%02d %02d:%02d:%02d",
             XDate_year(&date), XDate_month(&date), XDate_day(&date),
             XTime_hour(&time), XTime_minute(&time), XTime_second(&time));
}

bool HsfHistoryPage_parse(const uint8_t* data, uint32_t len,
                          HsfHistoryPage* out)
{
    XBsonDocument* doc;
    XBsonValue* v;
    const XBsonArray* rows;
    int i;
    uint32_t seq;

    if (!out) return false;
    /* [m_seq 跨解析保留] 本函数整结构清零重填，序号必须单独拎出来
     * 递增（否则永远停在 1，"等下一跳应答"的导出流程会永远等不到）。 */
    seq = out->m_seq;
    memset(out, 0, sizeof(*out));
    out->m_seq = seq;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, len)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    v = XBsonDocument_value_keyUtf8(doc, "total");
    if (v) out->m_total = (int)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, "dbRows");
    if (v) out->m_dbRows = XBsonValue_toInt64(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, "offset");
    if (v) out->m_offset = (int)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, "limit");
    if (v) out->m_limit = (int)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, "latestTs");
    if (v) out->m_latestTs = XBsonValue_toInt64(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, "earliestTs");
    if (v) out->m_earliestTs = XBsonValue_toInt64(v, 0);

    v = XBsonDocument_value_keyUtf8(doc, "rows");
    rows = v ? XBsonValue_toArray(v) : NULL;
    if (rows) {
        const int64_t n = XBsonArray_count_base(rows);
        for (i = 0; i < (int)n && i < 64; ++i) {
            const XBsonValue* rowVal = XBsonArray_at_base(rows, (int64_t)i);
            const XBsonArray* row = rowVal ? XBsonValue_toArray(rowVal) : NULL;
            HsfHistoryRow* dst = &out->m_rows[i];
            const XBsonValue* cell[18];
            int k;
            if (!row) continue;
            for (k = 0; k < 18; ++k)
                cell[k] = XBsonArray_at_base(row, (int64_t)k);

            /* 时间戳：epoch 毫秒 → 文本（原值同时留存给图表当 X 轴）。 */
            {
                const int64_t ts = (int64_t)hsfHistNum(cell[0], 0.0);
                dst->m_ts = ts;
                hsfHistFormatTs(ts, dst->m_cells[0], (int)sizeof(dst->m_cells[0]));
            }
            /* 模式 / 搅拌状态（文本列）。 */
            hsfHistText(cell[1], dst->m_cells[1], (int)sizeof(dst->m_cells[1]));
            hsfHistText(cell[2], dst->m_cells[2], (int)sizeof(dst->m_cells[2]));
            if (!dst->m_cells[1][0])
                snprintf(dst->m_cells[1], sizeof(dst->m_cells[1]), "--");
            if (!dst->m_cells[2][0])
                snprintf(dst->m_cells[2], sizeof(dst->m_cells[2]), "--");

            /* 纯数值列（缺数据 → "--"）。 */
            {
                /* 原始列 → 显示列：freq/current/rpm/turb1/turb2/total/
                 * press/temp/hum。 */
                static const int kSrc[] = { 3, 4, 5, 6, 7, 10, 12, 14, 15 };
                static const int kDst[] = { 3, 4, 5, 6, 7, 8, 10, 11, 12 };
                static const char* const kFmt[] = {
                    "%.2f", "%.2f", "%.0f", "%.2f", "%.2f",
                    "%.2f", "%.3f", "%.1f", "%.1f"
                };
                for (k = 0; k < 9; ++k) {
                    if (hsfHistIsNull(cell[kSrc[k]]))
                        snprintf(dst->m_cells[kDst[k]],
                                 sizeof(dst->m_cells[kDst[k]]), "--");
                    else
                        snprintf(dst->m_cells[kDst[k]],
                                 sizeof(dst->m_cells[kDst[k]]), kFmt[k],
                                 hsfHistNum(cell[kSrc[k]], 0.0));
                }
                /* [图表原值旁路] 显示列 3..12 逐列对应服务端原始列
                 * （HsfServerStore 应答列序 ts,mode,vfd_state,freq_hz,
                 * current_a,rpm,turb1,turb2,flow,flow_unit,flow_total,
                 * total_unit,press_mpa,press_note,temp,hum,di,do）：
                 * freq 3 / current 4 / rpm 5 / turb1 6 / turb2 7 /
                 * flow 8 / flow_total 10 / press 12 / temp 14 / hum 15。
                 * 原值与显示格同源同一次解析，不另开反解析（显示格带
                 * 单位，"36.50 m3/h" 用 strtod 会停在空格，整列丢）。 */
                {
                    static const int kNumSrc[HSF_HISTORY_NUM_COUNT] = {
                        3, 4, 5, 6, 7, 8, 10, 12, 14, 15
                    };
                    int n;
                    for (n = 0; n < HSF_HISTORY_NUM_COUNT; ++n) {
                        const int src = kNumSrc[n];
                        if (hsfHistIsNull(cell[src])) {
                            dst->m_hasNum[n] = false;
                            dst->m_num[n] = 0.0;
                        } else {
                            dst->m_hasNum[n] = true;
                            dst->m_num[n] = hsfHistNum(cell[src], 0.0);
                        }
                    }
                }
            }
            /* 瞬时流量 / 正累计（带单位）。 */
            {
                char unit[16];
                hsfHistText(cell[9], unit, (int)sizeof(unit));
                if (hsfHistIsNull(cell[8]))
                    snprintf(dst->m_cells[8], sizeof(dst->m_cells[8]), "--");
                else
                    snprintf(dst->m_cells[8], sizeof(dst->m_cells[8]), "%.2f %s",
                             hsfHistNum(cell[8], 0.0), unit);
                hsfHistText(cell[11], unit, (int)sizeof(unit));
                if (hsfHistIsNull(cell[10]))
                    snprintf(dst->m_cells[9], sizeof(dst->m_cells[9]), "--");
                else
                    snprintf(dst->m_cells[9], sizeof(dst->m_cells[9]), "%.2f %s",
                             hsfHistNum(cell[10], 0.0), unit);
            }
            /* 压差：有越限说明就显说明，否则显数值。 */
            {
                char note[40];
                hsfHistText(cell[13], note, (int)sizeof(note));
                if (note[0])
                    snprintf(dst->m_cells[10], sizeof(dst->m_cells[10]), "%s", note);
                else if (hsfHistIsNull(cell[12]))
                    snprintf(dst->m_cells[10], sizeof(dst->m_cells[10]), "--");
                else
                    snprintf(dst->m_cells[10], sizeof(dst->m_cells[10]), "%.3f",
                             hsfHistNum(cell[12], 0.0));
            }
            /* DI / DO：32 位位图（十六进制）。 */
            for (k = 0; k < 2; ++k) {
                const int col = (k == 0) ? 16 : 17;
                if (hsfHistIsNull(cell[col]))
                    snprintf(dst->m_cells[13 + k], sizeof(dst->m_cells[13 + k]),
                             "--");
                else
                    snprintf(dst->m_cells[13 + k], sizeof(dst->m_cells[13 + k]),
                             "0x%08X",
                             (unsigned)(int32_t)hsfHistNum(cell[col], 0.0));
            }
            /* 异常行：变频器故障 / 浊度越限 / 压差有越限说明。 */
            dst->m_abnormal = (strstr(dst->m_cells[2], "\xe6\x95\x85\xe9\x9a\x9c") != NULL);
            if (!dst->m_abnormal) {
                const double t1 = hsfHistNum(cell[6], -1.0);
                const double t2 = hsfHistNum(cell[7], -1.0);
                if (t1 >= HSF_HISTORY_TURB_WARN_NTU ||
                    t2 >= HSF_HISTORY_TURB_WARN_NTU)
                    dst->m_abnormal = true;
            }
            if (!dst->m_abnormal && dst->m_cells[10][0] &&
                strcmp(dst->m_cells[10], "--") != 0)
                dst->m_abnormal = true;   /* 压差有越限说明 */
            out->m_count++;
        }
    }
    out->m_valid = true;
    ++out->m_seq;
    XClassDelete((XClass*)doc);
    return true;
}
