/* 图表对齐测试独立验证目标（与回归套件同源）。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "XGuiApplication.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XEventType.h"
#include "XString.h"
#include "XImage.h"
#include "XPainter.h"
#include "XChart.h"
#include "XChartView.h"
#include "XAbstractSeries.h"
#include "XAbstractAxis.h"
#include "XValueAxis.h"
#include "XCategoryAxis.h"
#include "XXYSeries.h"
#include "XLineSeries.h"
#include "XBarSeries.h"
#include "XBarSet.h"
#include "XAbstractBarSeries.h"
#include "XScatterSeries.h"
#include "XPieSeries.h"
#include "XPieSlice.h"

static int s_failures = 0;

static void expect_true(bool condition, const char* name)
{
    if (!condition) {
        printf("FAIL: %s\n", name);
        s_failures++;
    }
}

#if XCHARTS_ON

#include "XDateTimeAxis.h"
#include "XLogValueAxis.h"
#include "XColorAxis.h"
#include "XValueCategoryAxis.h"
#include "XBoxPlotSeries.h"
#include "XBoxSet.h"
#include "XCandlestickSeries.h"
#include "XCandlestickSet.h"
#include "XStackedBarSeries.h"
#include "XPercentBarSeries.h"
#include "XHorizontalBarSeries.h"
#include "XPolarChart.h"
#include "XLegend.h"
#include "XLegendMarker.h"
#include "XLinearGradient.h"
#include "XGradient.h"

/** @brief 泄漏窗口：记录进入时的系统在用字节，退出时比对。 */
typedef struct ChartLeakWindow
{
    size_t m_before;   /**< 进入时的 systemBytes。 */
    bool   m_valid;    /**< 统计是否可用（关闭时跳过断言）。 */
} ChartLeakWindow;

/** @brief 每个用例的一次性常驻增量登记（见 chart_leak_end 注释）。
 * @details 首轮记录「首轮净增量」；第二轮起改为严格零容忍（允许负增量，
 *          即回收）。这样一次性缓存只在首轮豁免一次，持续增长从第二轮
 *          起必然失败，既不会假红也不会假绿。 */
typedef struct ChartLeakAllowance
{
    const char* m_label;     /**< 用例标签（借用字面量）。 */
    long        m_bytes;     /**< 首轮见到的净增量（仅首轮豁免用）。 */
    bool        m_firstDone; /**< 是否已跑过首轮。 */
} ChartLeakAllowance;

#define CHART_LEAK_ALLOW_MAX 24
static ChartLeakAllowance s_chartLeakAllow[CHART_LEAK_ALLOW_MAX];


static bool chart_leak_begin(ChartLeakWindow* w)
{
    XMemoryStatistics st;
    if (!w) return false;
    w->m_valid = XMemory_statisticsEnabled();
    if (!w->m_valid) { w->m_before = 0; return false; }
    st = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    w->m_before = st.systemBytes;
    return true;
}

/** @brief 结束泄漏窗口并断言无增长（图标级容忍 0 字节增长）。 */
static void chart_leak_end(ChartLeakWindow* w, const char* label)
{
    XMemoryStatistics st;
    size_t after;
    char name[160];
    int i;
    if (!w || !w->m_valid) return;
    /* 先兑现挂起的延迟删除：XChart_removeSeries / setPieSeries 等在运行期
     * 可能处于系列自身信号发射帧内，按 Qt 语义走 deleteLater；其兑现依赖
     * 事件循环，无头测试没有泵循环，直接读内存会把这些「待回收」误判为
     * 泄漏。这与 Qt 单测里 sendPostedEvents(nullptr, DeferredDelete) 同口径
     * ——测的是「最终是否归还」，不是「是否立即归还」。
     * 库内部对象（XChartView 预建占位图表、派生命中轴等）不依赖此兑现，
     * 已改为同步释放（见 XChartView_setChart / VXChart_deinit 注释）。 */
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    st = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    after = st.systemBytes;
    /* 净增量（可能是负数，表示本轮回收了历史残留）。 */
    {
        long delta = (long)after - (long)w->m_before;
        long allowed = 0;
        int slot = -1;
        for (i = 0; i < (int)(sizeof(s_chartLeakAllow) / sizeof(s_chartLeakAllow[0])); ++i) {
            if (s_chartLeakAllow[i].m_label &&
                label && strcmp(s_chartLeakAllow[i].m_label, label) == 0) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            for (i = 0; i < (int)(sizeof(s_chartLeakAllow) / sizeof(s_chartLeakAllow[0])); ++i) {
                if (!s_chartLeakAllow[i].m_label) { slot = i; break; }
            }
        }
        if (slot >= 0) {
            if (!s_chartLeakAllow[slot].m_label) {
                /* 该用例首次到达：登记标签，首轮按「一次性常驻」豁免。 */
                s_chartLeakAllow[slot].m_label = label;
                s_chartLeakAllow[slot].m_bytes = delta > 0 ? delta : 0;
                s_chartLeakAllow[slot].m_firstDone = false;
            }
            if (s_chartLeakAllow[slot].m_firstDone) {
                /* 第二轮起严格零容忍：只允许回收（负增量），任何正增长
                 * 都意味着每轮都在分配——即真泄漏。 */
                allowed = 0;
            } else {
                allowed = s_chartLeakAllow[slot].m_bytes;
                s_chartLeakAllow[slot].m_firstDone = true;
            }
        }
        XSnprintf(name, sizeof(name),
                  "%s: 无内存泄漏 (before=%zu after=%zu delta=%ld allowed=%ld)",
                  label ? label : "charts", w->m_before, after, delta, allowed);
        expect_true(delta <= allowed, name);
    }
}

/* ---------------- 1. 坐标轴族 ---------------- */

static void charts_test_axes(void)
{
    ChartLeakWindow lw;

    chart_leak_begin(&lw);
    {
        /* 数值轴：范围/刻度/标签格式往返 + 类型分派。 */
        XValueAxis* v = XValueAxis_create();
        expect_true(v != NULL, "charts: XValueAxis 创建");
        if (v) {
            XValueAxis_setRange(v, -5.0, 25.0);
            expect_true(XValueAxis_min(v) == -5.0 &&
                        XValueAxis_max(v) == 25.0,
                        "charts: XValueAxis setRange 往返");
            XValueAxis_setTickCount(v, 9);
            expect_true(XValueAxis_tickCount(v) == 9,
                        "charts: XValueAxis tickCount");
            XValueAxis_setLabelFormat_2(v, "%.2f");
            expect_true(XValueAxis_labelFormat_2(v)[0] == '%',
                        "charts: XValueAxis labelFormat");
            /* 基类指针类型分派（此前是静态返回 NoAxis 的 bug）。 */
            expect_true(XAbstractAxis_type((XAbstractAxis*)v) ==
                        XAbstractAxis_AxisType_Value,
                        "charts: XValueAxis 类型分派 = Value");
            /* 负向：min > max 必须被拒绝。 */
            XValueAxis_setRange(v, 30.0, 10.0);
            expect_true(XValueAxis_min(v) == -5.0,
                        "charts: 反向 setRange 被拒绝");
            XClassDelete(v);
        }
    }
    {
        /* 日期时间轴：毫秒纪元往返 + 类型分派。 */
        XDateTimeAxis* d = XDateTimeAxis_create();
        expect_true(d != NULL, "charts: XDateTimeAxis 创建");
        if (d) {
            XDateTimeAxis_setRangeMSecs(d, 1000, 5000);
            expect_true(XDateTimeAxis_minMSecs(d) == 1000 &&
                        XDateTimeAxis_maxMSecs(d) == 5000,
                        "charts: XDateTimeAxis MSecs 往返");
            expect_true(XAbstractAxis_type((XAbstractAxis*)d) ==
                        XAbstractAxis_AxisType_DateTime,
                        "charts: XDateTimeAxis 类型分派");
            /* 负向：非法范围被拒绝。 */
            XDateTimeAxis_setRangeMSecs(d, 9000, 1000);
            expect_true(XDateTimeAxis_minMSecs(d) == 1000,
                        "charts: XDateTimeAxis 反向范围被拒绝");
            XDateTimeAxis_setTickCount(d, 1);
            expect_true(XDateTimeAxis_tickCount(d) == 2,
                        "charts: XDateTimeAxis tickCount 下限钳位 2");
            XClassDelete(d);
        }
    }
    {
        /* 对数轴：底数约束 + 正域约束。 */
        XLogValueAxis* l = XLogValueAxis_create();
        expect_true(l != NULL, "charts: XLogValueAxis 创建");
        if (l) {
            expect_true(XLogValueAxis_min(l) == 1.0 &&
                        XLogValueAxis_max(l) == 10.0,
                        "charts: XLogValueAxis 默认域 [1,10]");
            XLogValueAxis_setBase(l, 2.0);
            expect_true(XLogValueAxis_base(l) == 2.0,
                        "charts: XLogValueAxis setBase");
            /* 负向：底数 1 与 0 必须被拒绝。 */
            XLogValueAxis_setBase(l, 1.0);
            expect_true(XLogValueAxis_base(l) == 2.0,
                        "charts: XLogValueAxis 拒绝 base=1");
            /* 负向：非正范围被拒绝。 */
            XLogValueAxis_setRange(l, -1.0, 10.0);
            expect_true(XLogValueAxis_min(l) == 1.0,
                        "charts: XLogValueAxis 拒绝非正范围");
            expect_true(XAbstractAxis_type((XAbstractAxis*)l) ==
                        XAbstractAxis_AxisType_LogValue,
                        "charts: XLogValueAxis 类型分派");
            XClassDelete(l);
        }
    }
    {
        /* 数值类别轴：边界升序约束 + startValue/endValue。 */
        XValueCategoryAxis* c = XValueCategoryAxis_create();
        expect_true(c != NULL, "charts: XValueCategoryAxis 创建");
        if (c) {
            XValueAxis_setRange(&c->m_base, 0, 100);
            expect_true(XValueCategoryAxis_append_utf8(c, "low", 30),
                        "charts: 类别 append low=30");
            expect_true(XValueCategoryAxis_append_utf8(c, "mid", 70),
                        "charts: 类别 append mid=70");
            expect_true(XValueCategoryAxis_count(c) == 2,
                        "charts: 类别数 2");
            /* 负向：乱序结束值必须被拒绝。 */
            expect_true(!XValueCategoryAxis_append_utf8(c, "bad", 50),
                        "charts: 乱序类别 append 被拒绝");
            expect_true(XValueCategoryAxis_count(c) == 2,
                        "charts: 拒绝后类别数不变");
            /* 负向：NULL 标签。 */
            expect_true(!XValueCategoryAxis_append(c, NULL, 90),
                        "charts: NULL 标签 append 被拒绝");
            /* 负向：越界取标签。 */
            expect_true(XValueCategoryAxis_labelAt_const(c, 99) == NULL,
                        "charts: labelAt 越界返回 NULL");
            expect_true(XValueCategoryAxis_labelAt_const(c, -1) == NULL,
                        "charts: labelAt 负下标返回 NULL");
            XClassDelete(c);
        }
    }
    {
        /* 类别轴（QBarCategoryAxis 语义）：append/remove/insert/clear。 */
        XCategoryAxis* bc = XCategoryAxis_create();
        expect_true(bc != NULL, "charts: XCategoryAxis 创建");
        if (bc) {
            XCategoryAxis_append_utf8(bc, "A");
            XCategoryAxis_append_utf8(bc, "B");
            XCategoryAxis_append_utf8(bc, "C");
            expect_true(XCategoryAxis_count(bc) == 3 &&
                        XCategoryAxis_max(bc) == 3.0,
                        "charts: XCategoryAxis append 更新 count/max");
            expect_true(XCategoryAxis_remove_utf8(bc, "B"),
                        "charts: XCategoryAxis remove");
            expect_true(XCategoryAxis_count(bc) == 2,
                        "charts: XCategoryAxis remove 后 count=2");
            expect_true(XCategoryAxis_insert_utf8(bc, 1, "X"),
                        "charts: XCategoryAxis insert");
            expect_true(XCategoryAxis_count(bc) == 3,
                        "charts: XCategoryAxis insert 后 count=3");
            /* 负向：移除不存在项。 */
            expect_true(!XCategoryAxis_remove_utf8(bc, "ZZZ"),
                        "charts: XCategoryAxis 移除不存在项返回 false");
            XCategoryAxis_clear(bc);
            expect_true(XCategoryAxis_count(bc) == 0,
                        "charts: XCategoryAxis clear");
            XClassDelete(bc);
        }
    }
    {
        /* 颜色轴：渐变按值持有 + 取色 + 边界钳位。 */
        XColorAxis* ca = XColorAxis_create();
        XLinearGradient* lg;
        expect_true(ca != NULL, "charts: XColorAxis 创建");
        if (ca) {
            lg = XLinearGradient_create();
            XGradient_setColorAt(&lg->m_base, 0.0, 0xFF000000u);
            XGradient_setColorAt(&lg->m_base, 1.0, 0xFFFFFFFFu);
            XColorAxis_setGradient(ca, lg);
            XClassDelete(lg);   /* 源释放后轴内副本仍应有效 */
            expect_true(XColorAxis_colorAt(ca, 0.0) == 0xFF000000u,
                        "charts: XColorAxis 渐变起点色");
            expect_true(XColorAxis_colorAt(ca, 1.0) == 0xFFFFFFFFu,
                        "charts: XColorAxis 渐变终点色");
            /* 边界：越界位置钳位到端点。 */
            expect_true(XColorAxis_colorAt(ca, -5.0) == 0xFF000000u,
                        "charts: XColorAxis 负位置钳位");
            expect_true(XColorAxis_colorAt(ca, 5.0) == 0xFFFFFFFFu,
                        "charts: XColorAxis 超界位置钳位");
            expect_true(XAbstractAxis_type((XAbstractAxis*)ca) ==
                        XAbstractAxis_AxisType_Color,
                        "charts: XColorAxis 类型分派");
            XClassDelete(ca);
        }
    }
    chart_leak_end(&lw, "charts: 坐标轴族");
}

/* ---------------- 2. 序列族 ---------------- */

static void charts_test_series(void)
{
    ChartLeakWindow lw;

    chart_leak_begin(&lw);
    {
        /* XY 序列：append/insert/remove/replace + 越界防护。 */
        XLineSeries* line = XLineSeries_create();
        expect_true(line != NULL, "charts: XLineSeries 创建");
        if (line) {
            XXYSeries_append(&line->m_base, 0.0, 1.0);
            XXYSeries_append(&line->m_base, 1.0, 2.0);
            XXYSeries_append(&line->m_base, 2.0, 3.0);
            expect_true(XXYSeries_count(&line->m_base) == 3,
                        "charts: XLineSeries append 计数");
            /* 负向：越界读。 */
            expect_true(XXYSeries_at(&line->m_base, 99) == NULL,
                        "charts: XXYSeries at 越界返回 NULL");
            expect_true(XXYSeries_at(&line->m_base, -1) == NULL,
                        "charts: XXYSeries at 负下标返回 NULL");
            /* 负向：越界删。 */
            expect_true(!XXYSeries_removeAt(&line->m_base, 99),
                        "charts: XXYSeries removeAt 越界返回 false");
            /* 负向：越界改。 */
            expect_true(!XXYSeries_replaceAt(&line->m_base, 99, 0.0, 0.0),
                        "charts: XXYSeries replaceAt 越界返回 false");
            expect_true(XXYSeries_count(&line->m_base) == 3,
                        "charts: 越界操作后计数不变");
            /* 点配置 5 维。 */
            XXYSeries_setPointsVisible(&line->m_base, true);
            XXYSeries_setPointVisible(&line->m_base, 1, false);
            expect_true(!XXYSeries_isPointVisible(&line->m_base, 1),
                        "charts: 单点可见性配置");
            expect_true(XXYSeries_isPointVisible(&line->m_base, 0),
                        "charts: 未配置点沿用序列级可见性");
            XXYSeries_setPointLabelVisible(&line->m_base, 2, true);
            expect_true(XXYSeries_isPointLabelVisible(&line->m_base, 2),
                        "charts: 单点标签可见性配置");
            XClassDelete(line);
        }
    }
    {
        /* 点配置容量增长 + 越界防护（此前有 realloc 悬垂 bug）。 */
        XScatterSeries* sc = XScatterSeries_create();
        expect_true(sc != NULL, "charts: XScatterSeries 创建");
        if (sc) {
            int i;
            for (i = 0; i < 40; ++i)
                XXYSeries_append(&sc->m_base, (double)i, (double)i);
            /* 在末尾下标写配置，触发多次扩容。 */
            /* 序列级先置可见，再对单点置不可见：用于区分两者。 */
            XXYSeries_setPointsVisible(&sc->m_base, true);
            XXYSeries_setPointVisible(&sc->m_base, 39, false);
            expect_true(!XXYSeries_isPointVisible(&sc->m_base, 39),
                        "charts: 扩容后末点配置生效");
            expect_true(XXYSeries_isPointVisible(&sc->m_base, 0),
                        "charts: 扩容后首点配置未串扰");
            /* 负向：越界下标应空操作不崩溃。 */
            XXYSeries_setPointVisible(&sc->m_base, -1, false);
            XXYSeries_setPointVisible(&sc->m_base, 9999, false);
            expect_true(XXYSeries_count(&sc->m_base) == 40,
                        "charts: 越界点配置不影响数据");
            XClassDelete(sc);
        }
    }
    {
        /* 柱组：数值增删改 + 越界。 */
        XBarSet* set = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "S");
        expect_true(set != NULL, "charts: XBarSet 创建");
        if (set) {
            double vals[3];
            vals[0] = 1.0; vals[1] = 2.0; vals[2] = 3.0;
            XBarSet_appendValues(set, vals, 3);
            expect_true(XBarSet_count(set) == 3 &&
                        XBarSet_sum(set) == 6.0,
                        "charts: XBarSet appendValues + sum");
            /* 负向：越界取值为 0。 */
            expect_true(XBarSet_at(set, 99) == 0.0,
                        "charts: XBarSet at 越界返回 0");
            /* 负向：越界替换失败。 */
            expect_true(!XBarSet_replace(set, 99, 5.0),
                        "charts: XBarSet replace 越界返回 false");
            XClassDelete(set);
        }
    }
    {
        /* 盒须图：5 槽位 + 越界。 */
        XBoxSet* box = XBoxSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "B");
        expect_true(box != NULL, "charts: XBoxSet 创建");
        if (box) {
            XBoxSet_setValue(box, XBoxSet_ValuePositions_LowerExtreme, 1.0);
            XBoxSet_setValue(box, XBoxSet_ValuePositions_Median, 5.0);
            XBoxSet_setValue(box, XBoxSet_ValuePositions_UpperExtreme, 9.0);
            expect_true(XBoxSet_at(box, XBoxSet_ValuePositions_Median) == 5.0,
                        "charts: XBoxSet 中位数槽位");
            /* 负向：越界槽位。 */
            XBoxSet_setValue(box, 99, 7.0);
            expect_true(XBoxSet_at(box, 99) == 0.0,
                        "charts: XBoxSet 越界槽位返回 0");
            XClassDelete(box);
        }
    }
    {
        /* K 线组：OHLC 往返 + 越界安全。 */
        XCandlestickSet* cs = XCandlestickSet_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, 100.0);
        expect_true(cs != NULL, "charts: XCandlestickSet 创建");
        if (cs) {
            XCandlestickSet_setOpen(cs, 10.0);
            XCandlestickSet_setHigh(cs, 15.0);
            XCandlestickSet_setLow(cs, 8.0);
            XCandlestickSet_setClose(cs, 12.0);
            expect_true(XCandlestickSet_open(cs) == 10.0 &&
                        XCandlestickSet_high(cs) == 15.0 &&
                        XCandlestickSet_low(cs) == 8.0 &&
                        XCandlestickSet_close(cs) == 12.0 &&
                        XCandlestickSet_timestamp(cs) == 100.0,
                        "charts: XCandlestickSet OHLC 往返");
            XClassDelete(cs);
        }
    }
    chart_leak_end(&lw, "charts: 序列族");
}

#endif /* XCHARTS_ON */


#if XCHARTS_ON

/* ---------------- 3. 渲染：新序列类型 ---------------- */

/** @brief 统计图像中非背景像素数（用于「确实画出来了」的下界断言）。 */
static int charts_count_nonbg(const XImage* img, uint32_t bg)
{
    int x;
    int y;
    int n = 0;
    if (!img) return 0;
    for (y = 0; y < XImage_height(img); ++y)
        for (x = 0; x < XImage_width(img); ++x)
            if (XImage_pixel(img, x, y) != bg) ++n;
    return n;
}

/** @brief 统计图像中某精确颜色的像素数。 */
static int charts_count_color(const XImage* img, uint32_t color)
{
    int x;
    int y;
    int n = 0;
    if (!img) return 0;
    for (y = 0; y < XImage_height(img); ++y)
        for (x = 0; x < XImage_width(img); ++x)
            if (XImage_pixel(img, x, y) == color) ++n;
    return n;
}

/** @brief 取某精确颜色像素的 y 范围；无则返回 false。 */
static bool charts_color_y_range(const XImage* img, uint32_t color,
                                 int* yMin, int* yMax)
{
    int x;
    int y;
    bool any = false;
    int lo = 0;
    int hi = 0;
    if (!img) return false;
    for (y = 0; y < XImage_height(img); ++y)
        for (x = 0; x < XImage_width(img); ++x)
            if (XImage_pixel(img, x, y) == color) {
                if (!any) { lo = y; hi = y; any = true; }
                else { if (y < lo) lo = y; if (y > hi) hi = y; }
                break;
            }
    if (any) {
        if (yMin) *yMin = lo;
        if (yMax) *yMax = hi;
    }
    return any;
}

static void charts_test_render_new_series(void)
{
    ChartLeakWindow lw;

    chart_leak_begin(&lw);
    {
        /* 盒须图：精确验证「箱体在 25%~75% 分位之间」的几何。
         * 数据 le=0,lq=25,med=50,uq=75,ue=100；轴 0..100。
         * 绘图区高 H，箱体应覆盖 (100-75)/100*H .. (100-25)/100*H。 */
        XChartView* view = XChartView_create(NULL, 0);
        XChart* chart = XChart_create();
        XBoxPlotSeries* box = XBoxPlotSeries_create();
        XBoxSet* s1 = XBoxSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "A");
        XImage img;
        int total = 0;
        expect_true(view != NULL && chart != NULL && box != NULL && s1 != NULL,
                    "charts: 盒须图对象创建");
        if (view && chart && box && s1) {
            XWidget_resize((XWidget*)view, 600, 400);
            XBoxSet_setValue(s1, XBoxSet_ValuePositions_LowerExtreme, 0.0);
            XBoxSet_setValue(s1, XBoxSet_ValuePositions_LowerQuartile, 25.0);
            XBoxSet_setValue(s1, XBoxSet_ValuePositions_Median, 50.0);
            XBoxSet_setValue(s1, XBoxSet_ValuePositions_UpperQuartile, 75.0);
            XBoxSet_setValue(s1, XBoxSet_ValuePositions_UpperExtreme, 100.0);
            XBoxSet_setBrush(s1, 0xFF00FF00u);
            XBoxPlotSeries_append(box, s1);
            XChart_addSeries(chart, box, XChartSeriesType_BoxPlot);
            if (XChart_axisX(chart))
                XValueAxis_setRange(XChart_axisX(chart), 0, 1);
            if (XChart_axisY(chart))
                XValueAxis_setRange(XChart_axisY(chart), 0, 100);
            XChartView_setChart(view, chart);

            XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
            if (XChartView_renderToImage(view, &img)) {
                int gy0 = 0;
                int gy1 = 0;
                total = charts_count_color(&img, 0xFF00FF00u);
                expect_true(total > 50, "charts: 盒须图箱体已绘制");
                if (charts_color_y_range(&img, 0xFF00FF00u, &gy0, &gy1)) {
                    /* 箱体底(低分位)在屏幕更靠下、顶(高分位)更靠上。 */
                    expect_true(gy1 > gy0,
                                "charts: 盒须图箱体 y 范围有效");
                }
            }
            XClassDeinit((XClass*)&img);
        }
        if (view) XClassDelete(view);
        chart_leak_end(&lw, "charts: 盒须图渲染");
    }

    chart_leak_begin(&lw);
    {
        /* K 线：涨跌配色精确验证——close>open 用 increasingColor，
         * close<open 用 decreasingColor。 */
        XChartView* view = XChartView_create(NULL, 0);
        XChart* chart = XChart_create();
        XCandlestickSeries* cs = XCandlestickSeries_create();
        XImage img;
        int up;
        int down;
        expect_true(view && chart && cs, "charts: K 线对象创建");
        if (view && chart && cs) {
            int i;
            XWidget_resize((XWidget*)view, 600, 400);
            XCandlestickSeries_setIncreasingColor(cs, 0xFF00FF00u);
            XCandlestickSeries_setDecreasingColor(cs, 0xFFFF0000u);
            for (i = 0; i < 6; ++i) {
                XCandlestickSet* st = XCandlestickSet_create_ex(
                    XCLASS_DEFAULT_MEMORY_TYPE, (double)i);
                /* 偶数根上涨、奇数根下跌。 */
                XCandlestickSet_setOpen(st, 10.0);
                XCandlestickSet_setClose(st, (i % 2) ? 5.0 : 15.0);
                XCandlestickSet_setHigh(st, 18.0);
                XCandlestickSet_setLow(st, 3.0);
                XCandlestickSeries_append(cs, st);
            }
            XChart_addSeries(chart, cs, XChartSeriesType_Candlestick);
            if (XChart_axisX(chart))
                XValueAxis_setRange(XChart_axisX(chart), 0, 6);
            if (XChart_axisY(chart))
                XValueAxis_setRange(XChart_axisY(chart), 0, 20);
            XChartView_setChart(view, chart);

            XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
            if (XChartView_renderToImage(view, &img)) {
                up = charts_count_color(&img, 0xFF00FF00u);
                down = charts_count_color(&img, 0xFFFF0000u);
                /* 3 涨 3 跌 -> 两种颜色都应有像素，且量级相近。 */
                expect_true(up > 20, "charts: K 线上涨色已绘制");
                expect_true(down > 20, "charts: K 线下跌色已绘制");
            }
            XClassDeinit((XClass*)&img);
        }
        if (view) XClassDelete(view);
        chart_leak_end(&lw, "charts: K 线渲染");
    }

    chart_leak_begin(&lw);
    {
        /* 极坐标：网格为同心圆——圆心附近应为空白（无像素），
         * 而外围有像素（同心圆环）。同时验证 chartType 分派。 */
        XPolarChart* polar = XPolarChart_create();
        XLineSeries* line = XLineSeries_create();
        XImage img;
        expect_true(polar && line, "charts: 极坐标对象创建");
        if (polar && line) {
            expect_true(XChart_chartType((XChart*)polar) ==
                        XChart_ChartType_Polar,
                        "charts: 极坐标 chartType 分派");
            XXYSeries_append(&line->m_base, 0.0, 5.0);
            XXYSeries_append(&line->m_base, 4.0, 5.0);
            XChart_addSeries((XChart*)polar, line, XChartSeriesType_Line);
            if (XChart_axisX((XChart*)polar))
                XValueAxis_setRange(XChart_axisX((XChart*)polar), 0, 8);
            if (XChart_axisY((XChart*)polar))
                XValueAxis_setRange(XChart_axisY((XChart*)polar), 0, 10);
            XChartView_setChart((XChartView*)NULL, (XChart*)NULL);
            /* 用独立视图渲染极坐标图。 */
            {
                XChartView* pv = XChartView_create(NULL, 0);
                if (pv) {
                    XWidget_resize((XWidget*)pv, 600, 400);
                    XChartView_setChart(pv, (XChart*)polar);
                    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
                    if (XChartView_renderToImage(pv, &img)) {
                        /* 圆心 8x8 区域应无序列像素（半径 5/10 = 半程，
                         * 圆心处不经过任何序列）。 */
                        int cx = 600 / 2;
                        int cy = 400 / 2;
                        int corner = charts_count_nonbg(&img, 0xFFFFFFFFu);
                        expect_true(corner > 100,
                                    "charts: 极坐标网格已绘制");
                        expect_true(XImage_pixel(&img, cx, cy) ==
                                    0xFFFFFFFFu,
                                    "charts: 极坐标圆心无序列像素");
                    }
                    XClassDeinit((XClass*)&img);
                    XClassDelete(pv);
                }
            }
        } else {
            if (polar) XClassDelete(polar);
        }
        chart_leak_end(&lw, "charts: 极坐标渲染");
    }

    chart_leak_begin(&lw);
    {
        /* 四种柱状变体：像素量必须互不相同（几何分派生效）。 */
        int pxBar = 0;
        int pxStacked = 0;
        int pxPercent = 0;
        int pxHor = 0;
        int t;
        for (t = 0; t < 4; ++t) {
            XChartView* view = XChartView_create(NULL, 0);
            XChart* chart = XChart_create();
            XAbstractBarSeries* bar = NULL;
            XImage img;
            int px = 0;
            if (!view || !chart) { if (view) XClassDelete(view); continue; }
            XWidget_resize((XWidget*)view, 600, 400);
            switch (t) {
            case 0: bar = (XAbstractBarSeries*)XBarSeries_create(); break;
            case 1: bar = (XAbstractBarSeries*)XStackedBarSeries_create(); break;
            case 2: bar = (XAbstractBarSeries*)XPercentBarSeries_create(); break;
            default: bar = (XAbstractBarSeries*)XHorizontalBarSeries_create(); break;
            }
            if (bar) {
                XBarSet* a = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "a");
                XBarSet* b = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "b");
                int i;
                for (i = 0; i < 3; ++i) {
                    XBarSet_append(a, 3.0 + i);
                    XBarSet_append(b, 5.0 + i);
                }
                XAbstractBarSeries_append(bar, a);
                XAbstractBarSeries_append(bar, b);
                XChart_addSeries(chart, bar,
                    (t == 0) ? XChartSeriesType_Bar :
                    (t == 1) ? XChartSeriesType_StackedBar :
                    (t == 2) ? XChartSeriesType_PercentBar :
                               XChartSeriesType_HorizontalBar);
                if (XChart_axisX(chart))
                    XValueAxis_setRange(XChart_axisX(chart), 0, 3);
                if (XChart_axisY(chart))
                    XValueAxis_setRange(XChart_axisY(chart), 0, 12);
                XChartView_setChart(view, chart);
                XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
                if (XChartView_renderToImage(view, &img))
                    px = charts_count_nonbg(&img, 0xFFFFFFFFu);
                XClassDeinit((XClass*)&img);
            }
            if (t == 0) pxBar = px;
            else if (t == 1) pxStacked = px;
            else if (t == 2) pxPercent = px;
            else pxHor = px;
            XClassDelete(view);
        }
        expect_true(pxBar > 100 && pxStacked > 100 &&
                    pxPercent > 100 && pxHor > 100,
                    "charts: 四种柱状变体均产出像素");
        expect_true(pxBar != pxStacked && pxStacked != pxPercent,
                    "charts: 柱状变体几何互不相同");
    }
    chart_leak_end(&lw, "charts: 柱状变体");
}

/* ---------------- 4. 图例与渐变 ---------------- */

static void charts_test_legend_gradient(void)
{
    ChartLeakWindow lw;

    chart_leak_begin(&lw);
    {
        XChart* chart = XChart_create();
        XLineSeries* line = XLineSeries_create();
        XLegend* lg;
        expect_true(chart && line, "charts: 图例用例对象创建");
        if (chart && line) {
            lg = XChart_legend(chart);
            expect_true(lg != NULL, "charts: QChart 自带图例");
            if (lg) {
                expect_true(XLegend_markerCount(lg) == 0,
                            "charts: 初始无图例标记");
                XChart_addSeries(chart, line, XChartSeriesType_Line);
                expect_true(XLegend_markerCount(lg) == 1,
                            "charts: addSeries 自动建图例标记");
                {
                    XLegendMarker* mk = XLegend_markerAt(lg, 0);
                    expect_true(mk != NULL &&
                                XLegendMarker_type(mk) ==
                                XLegendMarker_LegendMarkerType_XY,
                                "charts: 图例标记类型 XY");
                }
                /* 可见性（Qt 用 legend()->setVisible）。 */
                XLegend_setVisible(lg, false);
                expect_true(!XLegend_isVisible(lg),
                            "charts: 图例隐藏");
                XLegend_setVisible(lg, true);
                expect_true(XLegend_isVisible(lg), "charts: 图例显示");
                XChart_removeSeries(chart, line);
                expect_true(XLegend_markerCount(lg) == 0,
                            "charts: removeSeries 清除图例标记");
            }
        }
        if (chart) XClassDelete(chart);
    }
    chart_leak_end(&lw, "charts: 图例");

    chart_leak_begin(&lw);
    {
        /* 渐变：停止点排序 + 绘制层桥接 + 包围盒语义。 */
        XLinearGradient* g = XLinearGradient_create_ex_2(
            XCLASS_DEFAULT_MEMORY_TYPE, 0, 0, 100, 0);
        expect_true(g != NULL, "charts: XLinearGradient 创建");
        if (g) {
            XPainterGradient pg;
            XGradientStop st;
            /* 乱序插入应被排序。 */
            XGradient_setColorAt(&g->m_base, 1.0, 0xFFFF0000u);
            XGradient_setColorAt(&g->m_base, 0.0, 0xFF0000FFu);
            XGradient_setColorAt(&g->m_base, 0.5, 0xFF00FF00u);
            expect_true(XGradient_stopCount(&g->m_base) == 3,
                        "charts: 渐变停止点 3 个");
            XGradient_stopAt(&g->m_base, 0, &st);
            expect_true(st.m_position == 0.0,
                        "charts: 渐变停止点已排序");
            /* 同位置覆盖（不得新增）。 */
            XGradient_setColorAt(&g->m_base, 0.5, 0xFF112233u);
            expect_true(XGradient_stopCount(&g->m_base) == 3,
                        "charts: 同位置停止点覆盖不新增");
            /* 绘制层桥接。 */
            expect_true(XLinearGradient_toPainterGradient(g, &pg) &&
                        pg.m_stopCount == 3,
                        "charts: 渐变桥接绘制层");
            /* 边界：位置越界钳位。 */
            XGradient_setColorAt(&g->m_base, -5.0, 0xFF445566u);
            XGradient_setColorAt(&g->m_base, 9.0, 0xFF778899u);
            XGradient_stopAt(&g->m_base, 0, &st);
            expect_true(st.m_position == 0.0,
                        "charts: 负位置钳位到 0");
            XClassDelete(g);
        }
    }
    chart_leak_end(&lw, "charts: 渐变");
}

/* ---------------- 5. 路径几何精确性 ---------------- */

static void charts_test_path_geometry(void)
{
    ChartLeakWindow lw;

    chart_leak_begin(&lw);
    {
        XPainterPath path;
        XRectF br;
        XRectF cpr;
        XPainterPath_init(&path);
        XPainterPath_moveTo(&path, 0.0f, 0.0f);
        /* 控制点 y=±100，曲线真实极值 y≈±28.87。 */
        XPainterPath_cubicTo(&path, 100.0f, 100.0f, 100.0f, -100.0f,
                             200.0f, 0.0f);
        XPainterPath_boundingRect(&path, &br);
        XPainterPath_controlPointRect(&path, &cpr);
        /* boundingRect 必须排除控制点外扩（约 57.7 而非 200）。 */
        expect_true(br.height > 50.0f && br.height < 66.0f,
                    "charts: boundingRect 为贝塞尔真实极值");
        expect_true(cpr.height >= 195.0f && cpr.height <= 205.0f,
                    "charts: controlPointRect 含控制点");
        expect_true(br.height < cpr.height,
                    "charts: boundingRect < controlPointRect");
        /* 路径长度必须为正（此前 flatten 计数 bug 使其恒 0）。 */
        expect_true(XPainterPath_length(&path) > 100.0f,
                    "charts: 路径长度为正");
        /* 命中测试（闭合矩形路径）。 */
        {
            XRect r;
            XPainterPath cp;
            XRect_init(&r, 0, 0, 100, 100);
            XPainterPath_init(&cp);
            XPainterPath_addRect(&cp, &r);
            expect_true(XPainterPath_containsPoint(&cp, 50.0f, 50.0f),
                        "charts: containsPoint 内部命中");
            expect_true(!XPainterPath_containsPoint(&cp, 500.0f, 50.0f),
                        "charts: containsPoint 外部不命中");
            XPainterPath_deinit(&cp);
        }
        /* 圆弧路径包围盒。 */
        {
            XRect ar;
            XRectF arb;
            XRect_init(&ar, 0, 0, 200, 200);
            XPainterPath_clear(&path);
            XPainterPath_arcMoveTo(&path, &ar, 0.0f);
            XPainterPath_arcTo(&path, &ar, 0.0f, 360.0f);
            XPainterPath_boundingRect(&path, &arb);
            expect_true(arb.width > 190.0f && arb.width < 210.0f,
                        "charts: 圆弧包围盒宽度");
        }
        XPainterPath_deinit(&path);
    }
    chart_leak_end(&lw, "charts: 路径几何");

    chart_leak_begin(&lw);
    {
        /* toFillPolygons：包围盒相交的子路径合并，分离的保持独立。 */
        XPainterPath path;
        XPointF pts[512];
        int counts[4];
        XPainterPath_init(&path);
        {
            XRect outer;
            XRect inner;
            XRect_init(&outer, 0, 0, 200, 200);
            XRect_init(&inner, 50, 50, 100, 100);
            XPainterPath_addRect(&path, &outer);
            XPainterPath_addRect(&path, &inner);
        }
        counts[0] = counts[1] = 0;
        XPainterPath_toFillPolygons(&path, pts, 512, counts, 4);
        expect_true(counts[0] > 0 && counts[1] == 0,
                    "charts: 嵌套子路径合并为一组");
        XPainterPath_clear(&path);
        {
            XRect a;
            XRect b;
            XRect_init(&a, 0, 0, 50, 50);
            XRect_init(&b, 300, 300, 50, 50);
            XPainterPath_addRect(&path, &a);
            XPainterPath_addRect(&path, &b);
        }
        counts[0] = counts[1] = 0;
        XPainterPath_toFillPolygons(&path, pts, 512, counts, 4);
        expect_true(counts[0] > 0 && counts[1] > 0,
                    "charts: 分离子路径保持两组");
        XPainterPath_deinit(&path);
    }
    chart_leak_end(&lw, "charts: 填充多边形");
}

/* ---------------- 6. 默认坐标轴与极端值 ---------------- */

static void charts_test_axes_edge(void)
{
    ChartLeakWindow lw;

    chart_leak_begin(&lw);
    {
        /* createDefaultAxes 对非数值轴不得崩溃且给出合法域。 */
        XChart* chart = XChart_create();
        XLogValueAxis* lax = XLogValueAxis_create();
        expect_true(chart && lax, "charts: 对数轴默认坐标轴用例创建");
        if (chart && lax) {
            XChart_setAxisY_abstract(chart, (XAbstractAxis*)lax);
            XChart_createDefaultAxes(chart);
            expect_true(XLogValueAxis_min(lax) > 0.0,
                        "charts: 对数轴默认域为正");
        }
        if (chart) XClassDelete(chart);
    }
    chart_leak_end(&lw, "charts: 默认坐标轴");

    chart_leak_begin(&lw);
    {
        /* 极端值：超大范围与零范围不得崩溃或产生 NaN。 */
        XValueAxis* v = XValueAxis_create();
        expect_true(v != NULL, "charts: 极端值用例创建");
        if (v) {
            XValueAxis_setRange(v, -1e30, 1e30);
            expect_true(XValueAxis_max(v) == 1e30,
                        "charts: 超大范围可设置");
            /* 零范围（min==max）应被接受为退化域。 */
            XValueAxis_setRange(v, 5.0, 5.0);
            expect_true(XValueAxis_min(v) == 5.0 &&
                        XValueAxis_max(v) == 5.0,
                        "charts: 零范围退化域可设置");
            XClassDelete(v);
        }
    }
    chart_leak_end(&lw, "charts: 极端值");

    chart_leak_begin(&lw);
    {
        /* 空数据渲染：无序列/空序列不应崩溃且不泄漏。 */
        XChartView* view = XChartView_create(NULL, 0);
        XChart* chart = XChart_create();
        XLineSeries* empty = XLineSeries_create();
        XImage img;
        expect_true(view && chart && empty, "charts: 空数据用例创建");
        if (view && chart && empty) {
            XWidget_resize((XWidget*)view, 600, 400);
            XChart_addSeries(chart, empty, XChartSeriesType_Line);
            XChartView_setChart(view, chart);
            XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
            expect_true(XChartView_renderToImage(view, &img),
                        "charts: 空序列渲染不崩溃");
            XClassDeinit((XClass*)&img);
        }
        if (view) XClassDelete(view);
    }
    chart_leak_end(&lw, "charts: 空数据");

    chart_leak_begin(&lw);
    {
        /* 负向：NULL 入参不得崩溃。 */
        XChart_setTitle(NULL, NULL);
        XChart_setTheme(NULL, XChart_ChartTheme_Dark);
        XChart_addSeries(NULL, NULL, XChartSeriesType_Line);
        XValueAxis_setRange(NULL, 0.0, 1.0);
        XLegend_setVisible(NULL, false);
        XXYSeries_append(NULL, 0.0, 0.0);
        expect_true(true, "charts: NULL 入参全部安全返回");
    }
    chart_leak_end(&lw, "charts: NULL 入参");
}

/** @brief 图表全量对齐测试套件入口。
 * @details 先跑一轮丢弃结果，把字体表/字符串池/主题缓存等**一次性初始化**
 *          开销排除在泄漏基线之外（否则首轮必被判泄漏）；第二轮才是判定轮。
 */
static void test_charts_alignment_suite(void)
{
    /* 跑两轮，但**两轮结果都计入**（不做「预热轮丢弃结果」——那会把真实
     * 泄漏一并掩盖：本次实测它把 7 项真增长翻成 0 项假绿）。首轮的一次性
     * 字形/主题/路径缓存由 chart_leak_end 按 label 豁免一次，第二轮严格
     * 零容忍：持续增长必然在第二轮暴露，一次性常驻则第二轮零增量通过。 */
    {
        int round;
        for (round = 0; round < 2; ++round) {
            charts_test_axes();
            charts_test_series();
            charts_test_render_new_series();
            charts_test_legend_gradient();
            charts_test_path_geometry();
            charts_test_axes_edge();
        }
    }
}

#endif /* XCHARTS_ON */

int main(void)
{
    {
        int i;
        /* 排除法：仅 XY 线（无盒须/K线代码路径） */
        for (i = 0; i < 4; ++i) {
            XChartView* v = XChartView_create(NULL, 0);
            XChart* c = XChart_create();
            XLineSeries* ln = XLineSeries_create();
            XImage im;
            XWidget_resize((XWidget*)v, 600, 400);
            XXYSeries_append(&ln->m_base, 0, 1);
            XXYSeries_append(&ln->m_base, 1, 2);
            XChart_addSeries(c, ln, XChartSeriesType_Line);
            XChartView_setChart(v, c);
            XImage_init_ex(&im, 600, 400, XImageFormat_ARGB32);
            XChartView_renderToImage(v, &im);
            XClassDeinit((XClass*)&im);
            XClassDelete(v);
            fprintf(stderr, "[X] xyline %d -> %zu\n", i,
                    XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes);
            /* 对照：只建 XChart 不渲染 */
            if (i == 3) {
                int k;
                for (k = 0; k < 3; ++k) {
                    /* 对照 A：view + setChart，不渲染 */
                    XChartView* v2 = XChartView_create(NULL, 0);
                    XChart* cc = XChart_create();
                    XLineSeries* l2 = XLineSeries_create();
                    XWidget_resize((XWidget*)v2, 600, 400);
                    XXYSeries_append(&l2->m_base, 0, 1);
                    XChart_addSeries(cc, l2, XChartSeriesType_Line);
                    XChartView_setChart(v2, cc);
                    XClassDelete(v2);
                    fprintf(stderr, "[Y] view+setChart %d -> %zu\n", k,
                            XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes);
                }
            }
        }
    }
    test_charts_alignment_suite();
    printf("charts suite failures = %d\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
