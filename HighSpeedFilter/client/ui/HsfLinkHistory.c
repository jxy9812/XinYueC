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

/* 线上列数（HSF_HISTORY_WIRE_COLS）在这里，与两端服务端共用一处定义。 */
#include "XhsfLinkProto.h"

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
    XDate date;
    XTime time;
    XDateTime dt;
    int64_t tzOffsetMs;

    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (ts <= 0) {
        snprintf(out, (size_t)cap, "--");
        return;
    }
    /* [绝对时间换算 2026-11-10 根修] 旧实现是「现在 − 年龄」反推：
     *   age 整除出天数、remMs 只回绕 24h 的**时分秒**（XTime_addMSecs
     *   只做 % 86400000，**不向日期借位**）⇒ 跨零点前的行会算错：
     *   例如今天 08:00 看昨天 12:00 那一行，age 只有 20h ⇒ days=0 ⇒
     *   日期停在**今天** ⇒ **日期多一天**（实测 2026-11-09 12:00 显示成
     *   2026-11-10 12:00）。Python 端是 `time.localtime(ts/1000)`，绝对
     *   正确。改为按**绝对 epoch**换算（与历史页图表 X 轴 histXLabelText
     *   同一手法）：墙钟 epoch + 本地时区偏移 → 按日历展开成本地时间。 */
    {
        const XDateTime nowLocal = XDateTime_currentDateTime();
        tzOffsetMs = XDateTime_toMSecsSinceEpoch(&nowLocal)
                     - XDateTime_currentWallMSecsSinceEpoch();
    }
    if (!XDateTime_setMSecsSinceEpoch(&dt, ts + tzOffsetMs)) {
        snprintf(out, (size_t)cap, "--");
        return;
    }
    date = XDateTime_date(&dt);
    time = XDateTime_time(&dt);
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
            /* [格数跟协议走] 用 HSF_HISTORY_WIRE_COLS（XhsfLinkProto.h，19）
             * 而不是写死 18：2026-11-10 追加 water_temp 后，写死 18 会读不到
             * 水温那一格（且两处各写一个数字 ⇒ 改一端静默错列）。 */
            const XBsonValue* cell[HSF_HISTORY_WIRE_COLS];
            /** @brief 本行压差**越限说明**非空（异常行判据用；见下方注释）。
             *  @note  不用「显示格 ≠ --」判：压差有数值时显示格也有值，
             *         那会把「有压差读数」误判成「压差越限」。 */
            bool pressNote = false;
            int k;
            if (!row) continue;
            for (k = 0; k < HSF_HISTORY_WIRE_COLS; ++k)
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

            /* 纯数值列（缺数据 → "--"）。
             * [原始列 → 显示列] 2026-11-10 起显示列第 8 位是「水温」，
             * 浊度2 之后的所有显示列右移一位（瞬时流量 9 / 正累计 10 /
             * 压差 11 / 电箱温度 12 / 电箱湿度 13 / DI 14 / DO 15）。
             * 水温在**线上列序**里是最后一格（18）。 */
            {
                static const int kSrc[] = { 3, 4, 5, 6, 7, 18, 10, 12, 14, 15 };
                static const int kDst[] = { 3, 4, 5, 6, 7, 8, 10, 11, 12, 13 };
                static const char* const kFmt[] = {
                    "%.2f", "%.2f", "%.0f", "%.2f", "%.2f",
                    "%.1f", "%.2f", "%.3f", "%.1f", "%.1f"
                };
                for (k = 0; k < 10; ++k) {
                    if (hsfHistIsNull(cell[kSrc[k]]))
                        snprintf(dst->m_cells[kDst[k]],
                                 sizeof(dst->m_cells[kDst[k]]), "--");
                    else
                        snprintf(dst->m_cells[kDst[k]],
                                 sizeof(dst->m_cells[kDst[k]]), kFmt[k],
                                 hsfHistNum(cell[kSrc[k]], 0.0));
                }
                /* [图表原值旁路] 显示列 3..13 逐列对应服务端原始列
                 * （HsfServerStore 应答列序 ts,mode,vfd_state,freq_hz,
                 * current_a,rpm,turb1,turb2,flow,flow_unit,flow_total,
                 * total_unit,press_mpa,press_note,temp,hum,di,do,
                 * water_temp）：freq 3 / current 4 / rpm 5 / turb1 6 /
                 * turb2 7 / **water_temp 18** / flow 8 / flow_total 10 /
                 * press 12 / temp 14 / hum 15。
                 * 顺序按**显示列**排（图表序列下标 = 显示列 - 3）。
                 * 原值与显示格同源同一次解析，不另开反解析（显示格带
                 * 单位，"36.50 m3/h" 用 strtod 会停在空格，整列丢）。 */
                {
                    static const int kNumSrc[HSF_HISTORY_NUM_COUNT] = {
                        3, 4, 5, 6, 7, 18, 8, 10, 12, 14, 15
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
            /* 瞬时流量 / 正累计（带单位）。显示列 9/10（水温占了第 8 位）。 */
            {
                char unit[16];
                hsfHistText(cell[9], unit, (int)sizeof(unit));
                if (hsfHistIsNull(cell[8]))
                    snprintf(dst->m_cells[9], sizeof(dst->m_cells[9]), "--");
                else
                    snprintf(dst->m_cells[9], sizeof(dst->m_cells[9]), "%.2f %s",
                             hsfHistNum(cell[8], 0.0), unit);
                hsfHistText(cell[11], unit, (int)sizeof(unit));
                if (hsfHistIsNull(cell[10]))
                    snprintf(dst->m_cells[10], sizeof(dst->m_cells[10]), "--");
                else
                    snprintf(dst->m_cells[10], sizeof(dst->m_cells[10]), "%.2f %s",
                             hsfHistNum(cell[10], 0.0), unit);
            }
            /* 压差：有越限说明就显说明，否则显数值（显示列 11）。 */
            {
                char note[40];
                hsfHistText(cell[13], note, (int)sizeof(note));
                if (note[0]) {
                    pressNote = true;   /* 越限说明非空（异常判据用） */
                    snprintf(dst->m_cells[11], sizeof(dst->m_cells[11]),
                             "%s", note);
                } else if (hsfHistIsNull(cell[12]))
                    snprintf(dst->m_cells[11], sizeof(dst->m_cells[11]), "--");
                else
                    snprintf(dst->m_cells[11], sizeof(dst->m_cells[11]), "%.3f",
                             hsfHistNum(cell[12], 0.0));
            }
            /* DI / DO：32 位位图（十六进制）。显示列 14/15。 */
            for (k = 0; k < 2; ++k) {
                const int col = (k == 0) ? 16 : 17;
                if (hsfHistIsNull(cell[col]))
                    snprintf(dst->m_cells[14 + k], sizeof(dst->m_cells[14 + k]),
                             "--");
                else
                    snprintf(dst->m_cells[14 + k], sizeof(dst->m_cells[14 + k]),
                             "0x%08X",
                             (unsigned)(int32_t)hsfHistNum(cell[col], 0.0));
            }
            /* 异常行：变频器故障 / 浊度越限 / 压差有越限说明。
             * [压差判据 2026-11-10 根修] 旧实现判的是 `m_cells[10]`——
             * 那不是压差，是**正累计**（加「水温」列后显示列序整体右移一位，
             * 压差已移到 11）。正累计几乎恒有值 ⇒ **几乎每一行都被标成异常**
             * （整表粉红），而真正的压差越限反而不报。
             * 且判据改为「**越限说明文本非空**」而非「显示格 ≠ --」——
             * 后者把「有压差数值」也当异常。与 Python 的
             * `bool(press_note)`（pages_other.py 的 _server_row_texts）
             * 同源。 */
            dst->m_abnormal = (strstr(dst->m_cells[2], "\xe6\x95\x85\xe9\x9a\x9c") != NULL);
            if (!dst->m_abnormal) {
                const double t1 = hsfHistNum(cell[6], -1.0);
                const double t2 = hsfHistNum(cell[7], -1.0);
                if (t1 >= HSF_HISTORY_TURB_WARN_NTU ||
                    t2 >= HSF_HISTORY_TURB_WARN_NTU)
                    dst->m_abnormal = true;
            }
            if (!dst->m_abnormal && pressNote)
                dst->m_abnormal = true;   /* 压差有越限说明 */
            out->m_count++;
        }
    }
    out->m_valid = true;
    ++out->m_seq;
    XClassDelete((XClass*)doc);
    return true;
}
