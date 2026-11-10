/**
 * @file       HsfChartZoom.c
 * @brief      历史页「图表缩放取数」纯逻辑助手实现（见 HsfChartZoom.h）。
 * @note       零堆分配、零 GUI 依赖 ⇒ 可被回归锁单 TU 收录直接调用。
 */
#include "HsfChartZoom.h"

#include <stdio.h>

int HsfChartZoom_planBatches(int total, int chunk, bool windowMode,
                             int winFullRows, int wantPoints, int maxRows,
                             HsfChartZoomPlan* out)
{
    int batches;
    int step;

    if (out) {
        out->batches = 0;
        out->step = 0;
        out->sampled = false;
    }
    if (total <= 0) return 0;
    if (chunk <= 0) chunk = HSF_CHART_ZOOM_CHUNK_MAX;
    if (maxRows <= 0) maxRows = 1;

    if (windowMode && total <= winFullRows) {
        /* [窗口态·逐行取] 步进 = 批行数 ⇒ 窗内每一行都取回（不跳批、
         * 不丢段）⇒ 曲线是窗内全分辨率（用户要的"连续"）。 */
        batches = (total + chunk - 1) / chunk;
        if (batches < 1) batches = 1;
        if (out) {
            out->batches = batches;
            out->step = chunk;
            out->sampled = false;
        }
        return batches;
    }

    /* [窗口态·抽样取] 窗内行数超上限：**用满内存上限**做均匀跳批，而不是
     * 按"目标点数"只取十来个簇——簇与簇之间是空的，缩放到窗内看到的仍是
     * 断续线段（用户口径"不够连续"）。行数上限决定了最多能取多少行，
     * 就按它铺满整个窗（每批 chunk 行、步长均匀），交给绘制侧等距抽样。 */
    if (windowMode) {
        const int maxBatches = maxRows / chunk;
        batches = maxBatches > 0 ? maxBatches : 1;
        if (batches > (total + chunk - 1) / chunk)
            batches = (total + chunk - 1) / chunk;
        if (batches < 2 && total > chunk) batches = 2;
        if (batches < 1) batches = 1;
    } else {
        /* [全量模式 = 跨区间等距跳批] 每图例上限 N 点，筛选结果可达 20 万
         * 行：只连续取前若干行画的是"最近一小段"而不是"全部结果的平均
         * 取点"（用户口径要后者）。故按目标点数铺满全区间。 */
        const int maxBatches = maxRows / chunk;
        if (wantPoints < 1) wantPoints = 1;
        batches = (wantPoints + chunk - 1) / chunk;
        if (batches < 1) batches = 1;
        if (batches > maxBatches) batches = maxBatches;
        /* 库比"批数×chunk"还小时按实际行数算（避免空批）。 */
        if (batches > (total + chunk - 1) / chunk)
            batches = (total + chunk - 1) / chunk;
        /* [至少 2 批] 单批只能取连续 chunk 行；total>chunk 时一批画的是
         * "头部一小段"，不是覆盖面。 */
        if (batches < 2 && total > chunk) batches = 2;
        if (batches < 1) batches = 1;
    }
    if (batches <= 1) {
        step = chunk;
    } else {
        /* [步长让末批贴区尾] 按 ceil((total-chunk)/(batches-1)) 均分剩余区间，
         * 使第 batches-1 批起点 + chunk ≥ total（**向上取整** ⇒ 末批一定
         * 够到区尾，不会丢掉最近一段；向下取整会少取几行）。 */
        step = (total - chunk + (batches - 2)) / (batches - 1);
        if (step < chunk) step = chunk;
    }
    if (out) {
        out->batches = batches;
        out->step = step;
        /* 窗口态但窗内行数超上限 ⇒ 窗内抽样（曲线铺满视窗但仍尖角）。 */
        out->sampled = windowMode;
    }
    return batches;
}

bool HsfChartZoom_visibleWindow(int64_t baseTs, double xlo, double xhi,
                                int64_t* outLo, int64_t* outHi)
{
    if (outLo) *outLo = 0;
    if (outHi) *outHi = 0;
    if (baseTs <= 0) return false;
    if (!(xhi > xlo)) return false;
    if (outLo) *outLo = baseTs + (int64_t)(xlo * 1000.0);
    if (outHi) *outHi = baseTs + (int64_t)(xhi * 1000.0);
    if (outLo && outHi && *outHi <= *outLo) {
        *outLo = 0;
        *outHi = 0;
        return false;
    }
    return true;
}

bool HsfChartZoom_isFullView(int64_t visLo, int64_t visHi,
                             int64_t fullLo, int64_t fullHi)
{
    if (fullHi <= fullLo) return true;   /* 全量域未知 ⇒ 保守按全量视图 */
    if (visHi <= visLo) return false;
    return (visHi - visLo) * HSF_CHART_ZOOM_FULL_VIEW_DEN >=
           (fullHi - fullLo) * HSF_CHART_ZOOM_FULL_VIEW_NUM;
}

bool HsfChartZoom_windowCovers(int64_t winLo, int64_t winHi,
                               int64_t visLo, int64_t visHi)
{
    if (winLo <= 0 || winHi <= winLo) return false;
    return (visLo >= winLo && visHi <= winHi);
}

void HsfChartZoom_expandWindow(int64_t visLo, int64_t visHi,
                               int64_t* outLo, int64_t* outHi)
{
    int64_t margin;
    if (outLo) *outLo = visLo;
    if (outHi) *outHi = visHi;
    if (visHi <= visLo) return;
    margin = (visHi - visLo) / 100;   /* 1% */
    if (margin < 1000) margin = 1000; /* 最小 1s */
    if (outLo) *outLo = visLo - margin;
    if (outHi) *outHi = visHi + margin;
}

void HsfChartZoom_nextLimits(int seq, int* probe, int* chunk)
{
    const int chunkRange = HSF_CHART_ZOOM_CHUNK_MAX - HSF_CHART_ZOOM_CHUNK_MIN + 1;
    const int probeRange = HSF_CHART_ZOOM_PROBE_MAX - HSF_CHART_ZOOM_PROBE_MIN + 1;
    if (seq < 0) seq = 0;
    if (probe)
        *probe = HSF_CHART_ZOOM_PROBE_MIN + (seq % probeRange);
    if (chunk)
        *chunk = HSF_CHART_ZOOM_CHUNK_MAX - (seq % chunkRange);
}

/* ==================== 缩放视图（轴域 ⇄ 取数窗同源） ==================== */

bool HsfChartZoom_view(int64_t baseTs, double xlo, double xhi,
                       HsfChartZoomView* out)
{
    HsfChartZoomView v;
    v.valid = false;
    v.xLo = 0.0;
    v.xHi = 0.0;
    v.viewLo = 0;
    v.viewHi = 0;
    v.fetchLo = 0;
    v.fetchHi = 0;
    if (out) *out = v;
    if (!out) return false;
    /* [一次算齐] 轴域 → 绝对窗 → 取数窗（外扩）全在这里，页面不再另算：
     * 别处再算一遍就是"数据与轴域不同参"的入口（现场错位的根因）。 */
    if (!HsfChartZoom_visibleWindow(baseTs, xlo, xhi, &v.viewLo, &v.viewHi))
        return false;
    v.xLo = xlo;
    v.xHi = xhi;
    HsfChartZoom_expandWindow(v.viewLo, v.viewHi, &v.fetchLo, &v.fetchHi);
    v.valid = true;
    *out = v;
    return true;
}

int HsfChartZoom_countInView(const HsfChartZoomView* view,
                             HsfChartZoomTsAt at, const void* user, int n)
{
    int i;
    int count = 0;
    if (!view || !view->valid || !at || n <= 0) return 0;
    for (i = 0; i < n; ++i) {
        const int64_t ts = at(user, i);
        if (ts < view->viewLo) continue;
        if (ts > view->viewHi) break;   /* 升序：后面的都更晚 */
        ++count;
    }
    return count;
}

bool HsfChartZoom_viewFilled(const HsfChartZoomView* view,
                             HsfChartZoomTsAt at, const void* user, int n)
{
    int64_t first = 0;
    int64_t last = 0;
    int i;
    bool got = false;
    if (!view || !view->valid || !at || n <= 0) return false;
    if (view->viewHi <= view->viewLo) return false;
    for (i = 0; i < n; ++i) {
        const int64_t ts = at(user, i);
        if (ts < view->viewLo) continue;
        if (ts > view->viewHi) break;
        if (!got) { first = ts; got = true; }
        last = ts;
    }
    if (!got || last <= first) return false;
    /* 首末两点跨视口 ≥98% ⇒ 两端都铺到了（末端最多留一个采样间隔）。 */
    return (last - first) * 100 >= (view->viewHi - view->viewLo) * 98;
}

int HsfChartZoom_statusText(const char* whenLo, const char* whenHi,
                            const HsfChartZoomStatus* st, char* buf, int cap)
{
    if (!buf || cap <= 0) return 0;
    if (!whenLo) whenLo = "--";
    if (!whenHi) whenHi = "--";
    if (!st || st->rowsHeld <= 0) {
        /* [空窗不是"0 行筛选结果"] 缩放窗内没有记录是**窗口口径**的结论，
         * 不能说成"筛选结果为 0 行"（那会被读成全库没数据）。 */
        return snprintf(buf, (size_t)cap,
                        "图表数据：窗口 %s~%s · 该时间段无记录",
                        whenLo, whenHi);
    }
    return snprintf(buf, (size_t)cap,
                    "图表数据：窗口 %s~%s · 窗内 %d 行（视口 %d 行）%s%s",
                    whenLo, whenHi, st->rowsHeld, st->rowsInView,
                    st->sampled ? "，窗内抽样" : "，逐行取回",
                    st->filled ? "" : "（该时段数据不足，曲线两端可能留空）");
}
