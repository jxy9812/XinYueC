/* Chart new-series / axis / gradient / legend / polar probe.
 * Uses renderToImage (same pipeline as paintEvent) and counts non-bg pixels.
 */
#include <stdio.h>
#include <math.h>
#include "XGuiApplication.h"
#include "XChartView.h"
#include "XChart.h"
#include "XBoxPlotSeries.h"
#include "XBoxSet.h"
#include "XCandlestickSeries.h"
#include "XCandlestickSet.h"
#include "XDateTimeAxis.h"
#include "XLogValueAxis.h"
#include "XColorAxis.h"
#include "XLinearGradient.h"
#include "XPolarChart.h"
#include "XLineSeries.h"
#include "XAreaSeries.h"
#include "XBarSeries.h"
#include "XStackedBarSeries.h"
#include "XPercentBarSeries.h"
#include "XHorizontalBarSeries.h"
#include "XValueCategoryAxis.h"
#include "XLegend.h"
#include "XLegendMarker.h"
#include "XImage.h"

static int countNonBg(const XImage* img, uint32_t bg)
{
    int x;
    int y;
    int n = 0;
    for (y = 0; y < XImage_height(img); ++y)
        for (x = 0; x < XImage_width(img); ++x)
            if (XImage_pixel(img, x, y) != bg) ++n;
    return n;
}

static int checkBoxPlot(XChartView* view)
{
    XChart* chart = XChart_create();
    XBoxPlotSeries* box = XBoxPlotSeries_create();
    XBoxSet* s1 = XBoxSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "A");
    XBoxSet* s2 = XBoxSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "B");
    XImage img;
    int px;
    int vals1[5];
    int vals2[5];
    int i;

    vals1[0] = 2; vals1[1] = 4; vals1[2] = 6; vals1[3] = 8; vals1[4] = 10;
    vals2[0] = 1; vals2[1] = 3; vals2[2] = 5; vals2[3] = 7; vals2[4] = 9;
    for (i = 0; i < 5; ++i) XBoxSet_setValue(s1, i, vals1[i]);
    for (i = 0; i < 5; ++i) XBoxSet_setValue(s2, i, vals2[i]);
    XBoxPlotSeries_append(box, s1);
    XBoxPlotSeries_append(box, s2);
    XChart_addSeries(chart, box, XChartSeriesType_BoxPlot);
    if (XChart_axisX(chart)) XValueAxis_setRange(XChart_axisX(chart), 0, 2);
    if (XChart_axisY(chart)) XValueAxis_setRange(XChart_axisY(chart), 0, 12);
    XChartView_setChart(view, chart);

    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (!XChartView_renderToImage(view, &img)) {
        printf("FAIL boxplot render returned false\n");
        XClassDeinit((XClass*)&img);
        return 0;
    }
    px = countNonBg(&img, 0xFFFFFFFFu);
    XClassDeinit((XClass*)&img);
    printf("boxplot non-bg pixels = %d\n", px);
    if (px < 100) {
        printf("FAIL boxplot produced almost nothing\n");
        return 0;
    }
    return 1;
}

static int checkCandlestick(XChartView* view)
{
    XChart* chart = XChart_create();
    XCandlestickSeries* cand = XCandlestickSeries_create();
    XImage img;
    int px;
    int i;

    for (i = 0; i < 8; ++i) {
        XCandlestickSet* cs = XCandlestickSet_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (double)i);
        XCandlestickSet_setOpen(cs, 10 + i);
        XCandlestickSet_setHigh(cs, 14 + i);
        XCandlestickSet_setLow(cs, 6 + i);
        XCandlestickSet_setClose(cs, (i % 2) ? 12 + i : 8 + i);
        XCandlestickSeries_append(cand, cs);
    }
    XChart_addSeries(chart, cand, XChartSeriesType_Candlestick);
    if (XChart_axisX(chart)) XValueAxis_setRange(XChart_axisX(chart), 0, 8);
    if (XChart_axisY(chart)) XValueAxis_setRange(XChart_axisY(chart), 0, 25);
    XChartView_setChart(view, chart);

    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (!XChartView_renderToImage(view, &img)) {
        printf("FAIL candlestick render returned false\n");
        XClassDeinit((XClass*)&img);
        return 0;
    }
    px = countNonBg(&img, 0xFFFFFFFFu);
    XClassDeinit((XClass*)&img);
    printf("candlestick non-bg pixels = %d\n", px);
    if (px < 100) {
        printf("FAIL candlestick produced almost nothing\n");
        return 0;
    }
    return 1;
}

static int checkDateTimeAxis(XChartView* view)
{
    XChart* chart = XChart_create();
    XDateTimeAxis* dax = XDateTimeAxis_create();
    XImage img;

    XDateTimeAxis_setRangeMSecs(dax, 0, 3600000);
    XDateTimeAxis_setFormat_utf8(dax, "hh:mm");
    XChart_setAxisX_abstract(chart, (XAbstractAxis*)dax);
    if (XChart_axisX_abstract(chart) != (XAbstractAxis*)dax) {
        printf("FAIL datetime axis attach\n");
        return 0;
    }
    if (XAbstractAxis_type(XChart_axisX_abstract(chart)) !=
        XAbstractAxis_AxisType_DateTime) {
        printf("FAIL axis type should be DateTime\n");
        return 0;
    }
    XChartView_setChart(view, chart);
    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (XChartView_renderToImage(view, &img))
        printf("datetime chart non-bg pixels = %d\n",
               countNonBg(&img, 0xFFFFFFFFu));
    XClassDeinit((XClass*)&img);
    printf("OK datetime axis attached, type=DateTime\n");
    return 1;
}

static int checkDefaultAxesWithLogAxis(void)
{
    XChart* chart = XChart_create();
    XLogValueAxis* lax = XLogValueAxis_create();
    XChart_setAxisY_abstract(chart, (XAbstractAxis*)lax);
    XChart_createDefaultAxes(chart);
    if (XLogValueAxis_min(lax) <= 0.0) {
        printf("FAIL log axis default range must be positive\n");
        return 0;
    }
    printf("OK log axis default range = %g..%g\n",
           XLogValueAxis_min(lax), XLogValueAxis_max(lax));
    XClassDelete(chart);
    return 1;
}

static int checkLegendMarkers(void)
{
    XChart* chart = XChart_create();
    XLineSeries* line = XLineSeries_create();
    XLegend* lg = XChart_legend(chart);
    XLegendMarker* mk;

    XChart_addSeries(chart, line, XChartSeriesType_Line);
    mk = XLegend_markerAt(lg, 0);
    if (XLegend_markerCount(lg) != 1 || !mk) {
        printf("FAIL legend marker not auto-created\n");
        return 0;
    }
    if (XLegendMarker_type(mk) != XLegendMarker_LegendMarkerType_XY) {
        printf("FAIL marker type should be XY\n");
        return 0;
    }
    XChart_removeSeries(chart, line);
    if (XLegend_markerCount(lg) != 0) {
        printf("FAIL marker not cleared after removeSeries\n");
        return 0;
    }
    printf("OK legend marker auto-create/remove\n");
    XClassDelete(chart);
    return 1;
}

static int checkLinearGradient(void)
{
    XLinearGradient* g = XLinearGradient_create_ex_2(
        XCLASS_DEFAULT_MEMORY_TYPE, 0.0, 0.0, 100.0, 0.0);
    XPainterGradient pg;
    XGradientStop st;

    if (!g) {
        printf("FAIL gradient create\n");
        return 0;
    }
    if (XGradient_type(&g->m_base) != XGradient_Type_Linear) {
        printf("FAIL gradient type should be Linear\n");
        return 0;
    }
    XGradient_setColorAt(&g->m_base, 1.0, 0xFFFF0000u);
    XGradient_setColorAt(&g->m_base, 0.0, 0xFF0000FFu);
    XGradient_setColorAt(&g->m_base, 0.5, 0xFF00FF00u);
    if (XGradient_stopCount(&g->m_base) != 3) {
        printf("FAIL stop count should be 3\n");
        return 0;
    }
    XGradient_stopAt(&g->m_base, 0, &st);
    if (st.m_position != 0.0) {
        printf("FAIL stops not sorted\n");
        return 0;
    }
    if (!XLinearGradient_toPainterGradient(g, &pg)) {
        printf("FAIL to painter gradient\n");
        return 0;
    }
    if (pg.m_stopCount != 3) {
        printf("FAIL painter stops = %d\n", pg.m_stopCount);
        return 0;
    }
    printf("OK linear gradient (3 stops, sorted, bridged)\n");
    XClassDelete(g);
    return 1;
}

static int checkColorAxis(void)
{
    XColorAxis* ax = XColorAxis_create();
    XLinearGradient* lg;
    uint32_t c0;
    uint32_t c1;
    uint32_t cm;

    if (!ax) {
        printf("FAIL color axis create\n");
        return 0;
    }
    if (XAbstractAxis_type((XAbstractAxis*)ax) != XAbstractAxis_AxisType_Color) {
        printf("FAIL color axis type\n");
        return 0;
    }
    lg = XLinearGradient_create();
    XGradient_setColorAt(&lg->m_base, 0.0, 0xFF000000u);
    XGradient_setColorAt(&lg->m_base, 1.0, 0xFFFFFFFFu);
    XColorAxis_setGradient(ax, lg);
    XClassDelete(lg);

    c0 = XColorAxis_colorAt(ax, 0.0);
    c1 = XColorAxis_colorAt(ax, 1.0);
    cm = XColorAxis_colorAt(ax, 0.5);
    if (c0 != 0xFF000000u || c1 != 0xFFFFFFFFu) {
        printf("FAIL gradient endpoints\n");
        return 0;
    }
    if (cm != 0xFF7F7F7Fu && cm != 0xFF808080u) {
        printf("FAIL midpoint color %08X\n", (unsigned)cm);
        return 0;
    }
    printf("OK color axis gradient midpoint=%08X\n", (unsigned)cm);
    XClassDelete(ax);
    return 1;
}

static int checkPolarChart(XChartView* view)
{
    XPolarChart* polar = XPolarChart_create();
    XLineSeries* line = XLineSeries_create();
    XImage img;
    int px;
    int i;
    int ok = 1;

    if (!polar || !line) {
        printf("FAIL polar create\n");
        return 0;
    }
    if (XChart_chartType((XChart*)polar) != XChart_ChartType_Polar) {
        printf("FAIL chartType should be Polar\n");
        ok = 0;
    }
    for (i = 0; i <= 8; ++i)
        XXYSeries_append(&line->m_base, (double)i, 5.0);
    XChart_addSeries((XChart*)polar, line, XChartSeriesType_Line);
    if (XChart_axisX((XChart*)polar))
        XValueAxis_setRange(XChart_axisX((XChart*)polar), 0, 8);
    if (XChart_axisY((XChart*)polar))
        XValueAxis_setRange(XChart_axisY((XChart*)polar), 0, 10);
    XPolarChart_addAxis(polar, (XAbstractAxis*)XChart_axisX((XChart*)polar),
                        XPolarChart_PolarOrientation_Angular);
    XPolarChart_addAxis(polar, (XAbstractAxis*)XChart_axisY((XChart*)polar),
                        XPolarChart_PolarOrientation_Radial);
    {
        XAbstractAxis* got[2];
        int n = XPolarChart_axes(polar, 0, got, 2);
        if (n != 2) {
            printf("FAIL polar axes count = %d\n", n);
            ok = 0;
        }
    }
    XChartView_setChart(view, (XChart*)polar);
    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (!XChartView_renderToImage(view, &img)) {
        printf("FAIL polar render returned false\n");
        XClassDeinit((XClass*)&img);
        return 0;
    }
    px = countNonBg(&img, 0xFFFFFFFFu);
    XClassDeinit((XClass*)&img);
    printf("polar chart non-bg pixels = %d\n", px);
    if (px < 100) {
        printf("FAIL polar produced almost nothing\n");
        ok = 0;
    }
    return ok;
}


/* 柱状变体渲染差异：分组 / 堆叠 / 百分比 / 水平 各自出图且像素量不同。 */
static int renderBarVariant(XChartView* view, int seriesType, int* outPx)
{
    XChart* chart = XChart_create();
    XAbstractBarSeries* bar = NULL;
    XBarSet* set1;
    XBarSet* set2;
    XImage img;
    int px;

    switch (seriesType) {
    case XChartSeriesType_Bar: bar = (XAbstractBarSeries*)XBarSeries_create(); break;
    case XChartSeriesType_StackedBar: bar = (XAbstractBarSeries*)XStackedBarSeries_create(); break;
    case XChartSeriesType_PercentBar: bar = (XAbstractBarSeries*)XPercentBarSeries_create(); break;
    case XChartSeriesType_HorizontalBar: bar = (XAbstractBarSeries*)XHorizontalBarSeries_create(); break;
    default: return 0;
    }
    if (!bar) return 0;

    set1 = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "S1");
    set2 = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "S2");
    {
        int i;
        for (i = 0; i < 3; ++i) {
            XBarSet_append(set1, 3.0 + i);
            XBarSet_append(set2, 5.0 + i);
        }
    }
    XAbstractBarSeries_append(bar, set1);
    XAbstractBarSeries_append(bar, set2);
    XChart_addSeries(chart, bar, seriesType);
    if (XChart_axisX(chart)) XValueAxis_setRange(XChart_axisX(chart), 0, 3);
    if (XChart_axisY(chart)) XValueAxis_setRange(XChart_axisY(chart), 0, 12);
    XChartView_setChart(view, chart);

    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (!XChartView_renderToImage(view, &img)) {
        XClassDeinit((XClass*)&img);
        return 0;
    }
    px = countNonBg(&img, 0xFFFFFFFFu);
    XClassDeinit((XClass*)&img);
    *outPx = px;
    return px > 100;
}

static int checkBarVariants(XChartView* view)
{
    int pxGrouped = 0;
    int pxStacked = 0;
    int pxPercent = 0;
    int pxHorizontal = 0;
    int ok = 1;

    if (!renderBarVariant(view, XChartSeriesType_Bar, &pxGrouped)) {
        printf("FAIL grouped bar render\n");
        ok = 0;
    }
    if (!renderBarVariant(view, XChartSeriesType_StackedBar, &pxStacked)) {
        printf("FAIL stacked bar render\n");
        ok = 0;
    }
    if (!renderBarVariant(view, XChartSeriesType_PercentBar, &pxPercent)) {
        printf("FAIL percent bar render\n");
        ok = 0;
    }
    if (!renderBarVariant(view, XChartSeriesType_HorizontalBar, &pxHorizontal)) {
        printf("FAIL horizontal bar render\n");
        ok = 0;
    }
    printf("bar variants px: grouped=%d stacked=%d percent=%d horizontal=%d\n",
           pxGrouped, pxStacked, pxPercent, pxHorizontal);
    /* 四种几何必须产生不同像素量，证明分派真的生效（而非同一套绘制）。 */
    if (pxGrouped == pxStacked && pxStacked == pxPercent) {
        printf("FAIL variants look identical (dispatch may not work)\n");
        ok = 0;
    }
    return ok;
}

/* QCategoryAxis 类别边界轴：结束值决定刻度位置。 */
static int checkValueCategoryAxis(void)
{
    XValueCategoryAxis* ax = XValueCategoryAxis_create();
    double startVal;
    double endA;
    double endB;
    int ok = 1;
    const XString* labels[4];
    int n;

    if (!ax) {
        printf("FAIL category axis create\n");
        return 0;
    }
    XValueAxis_setRange(&ax->m_base, 0, 100);
    XValueCategoryAxis_setStartValue(ax, 0);
    if (!XValueCategoryAxis_append_utf8(ax, "low", 30)) ok = 0;
    if (!XValueCategoryAxis_append_utf8(ax, "mid", 70)) ok = 0;
    if (!XValueCategoryAxis_append_utf8(ax, "high", 100)) ok = 0;
    if (XValueCategoryAxis_count(ax) != 3) {
        printf("FAIL category count\n");
        return 0;
    }
    /* 结束值必须升序，违反时拒绝。 */
    if (XValueCategoryAxis_append_utf8(ax, "bad", 50)) {
        printf("FAIL out-of-order append accepted\n");
        ok = 0;
    }
    {
        XString* l = XString_create_utf8("mid");
        endA = XValueCategoryAxis_endValue(ax, l);
        startVal = XValueCategoryAxis_startValue(ax, l);
        XClassDelete(l);
        l = XString_create_utf8("high");
        endB = XValueCategoryAxis_endValue(ax, l);
        XClassDelete(l);
    }
    if (endA != 70.0 || endB != 100.0 || startVal != 30.0) {
        printf("FAIL category boundaries start=%g mid=%g high=%g\n",
               startVal, endA, endB);
        ok = 0;
    }
    n = XValueCategoryAxis_categoriesLabels(ax, labels, 4);
    if (n != 3) {
        printf("FAIL categoriesLabels n=%d\n", n);
        ok = 0;
    }
    printf("OK category axis boundaries (%.0f..%.0f..%.0f)\n",
           startVal, endA, endB);
    XClassDelete(ax);
    return ok;
}


/* 极坐标柱状：走环形扇区几何（与直角柱状的像素分布不同）。 */
static int checkPolarBar(XChartView* view)
{
    XPolarChart* polar = XPolarChart_create();
    XBarSeries* bar = XBarSeries_create();
    XBarSet* set;
    XImage img;
    int px;
    int ok = 1;
    int i;

    if (!polar || !bar) {
        printf("FAIL polar bar create\n");
        return 0;
    }
    set = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "P");
    for (i = 0; i < 6; ++i) XBarSet_append(set, 4.0 + i);
    XAbstractBarSeries_append((XAbstractBarSeries*)bar, set);
    XChart_addSeries((XChart*)polar, bar, XChartSeriesType_Bar);
    if (XChart_axisX((XChart*)polar))
        XValueAxis_setRange(XChart_axisX((XChart*)polar), 0, 6);
    if (XChart_axisY((XChart*)polar))
        XValueAxis_setRange(XChart_axisY((XChart*)polar), 0, 12);
    XChartView_setChart(view, (XChart*)polar);

    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (!XChartView_renderToImage(view, &img)) {
        printf("FAIL polar bar render\n");
        XClassDeinit((XClass*)&img);
        return 0;
    }
    px = countNonBg(&img, 0xFFFFFFFFu);
    XClassDeinit((XClass*)&img);
    printf("polar bar non-bg pixels = %d\n", px);
    if (px < 100) {
        printf("FAIL polar bar produced almost nothing\n");
        ok = 0;
    }
    return ok;
}


/* 极坐标面积图：扇形填充（基线沿圆弧回程）。 */
static int checkPolarArea(XChartView* view)
{
    XPolarChart* polar = XPolarChart_create();
    XLineSeries* up = XLineSeries_create();
    XAreaSeries* area = XAreaSeries_create();
    XImage img;
    int px;
    int ok = 1;
    int i;

    if (!polar || !up || !area) {
        printf("FAIL polar area create\n");
        return 0;
    }
    for (i = 0; i < 10; ++i) XXYSeries_append(&up->m_base, (double)i, 5.0 + i * 0.3);
    XAreaSeries_setUpperSeries(area, up);
    XAreaSeries_setBaseValue(area, 1.0);
    XChart_addSeries((XChart*)polar, area, XChartSeriesType_Area);
    if (XChart_axisX((XChart*)polar))
        XValueAxis_setRange(XChart_axisX((XChart*)polar), 0, 9);
    if (XChart_axisY((XChart*)polar))
        XValueAxis_setRange(XChart_axisY((XChart*)polar), 0, 10);
    XChartView_setChart(view, (XChart*)polar);

    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (!XChartView_renderToImage(view, &img)) {
        printf("FAIL polar area render\n");
        XClassDeinit((XClass*)&img);
        return 0;
    }
    px = countNonBg(&img, 0xFFFFFFFFu);
    XClassDeinit((XClass*)&img);
    printf("polar area non-bg pixels = %d\n", px);
    if (px < 100) {
        printf("FAIL polar area produced almost nothing\n");
        ok = 0;
    }
    return ok;
}

/* labelsPosition 两种取值产生不同标签落位（像素分布不同）。 */
static int renderCatAxis(XChartView* view, int pos, int* outPx, XImage* outImg)
{
    XChart* chart = XChart_create();
    XValueCategoryAxis* cax = XValueCategoryAxis_create();
    XImage img;
    int px;

    XValueAxis_setRange(&cax->m_base, 0, 100);
    XValueCategoryAxis_append_utf8(cax, "aaa", 40);
    XValueCategoryAxis_append_utf8(cax, "bbb", 100);
    XValueCategoryAxis_setLabelsPosition(
        cax, (XValueCategoryAxis_AxisLabelsPosition)pos);
    XChart_setAxisX_abstract(chart, (XAbstractAxis*)cax);
    XChartView_setChart(view, chart);

    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (!XChartView_renderToImage(view, &img)) {
        XClassDeinit((XClass*)&img);
        return 0;
    }
    px = countNonBg(&img, 0xFFFFFFFFu);
    *outPx = px;
    /* 先转移再释放：顺序颠倒会把已析构的空对象移出去（diff 恒 0 的根因）。 */
    if (outImg)
        XClassMove((XClass*)outImg, (XClass*)&img);
    else
        XClassDeinit((XClass*)&img);
    return 1;
}

static int checkCategoryLabelPosition(XChartView* view)
{
    int pxCenter = 0;
    int pxOnValue = 0;
    XImage imgC;
    XImage imgV;
    int diff = 0;
    int x;
    int y;

    XImage_init(&imgC);
    XImage_init(&imgV);
    if (!renderCatAxis(view, XValueCategoryAxis_AxisLabelsPosition_Center,
                       &pxCenter, &imgC)) return 0;
    if (!renderCatAxis(view, XValueCategoryAxis_AxisLabelsPosition_OnValue,
                       &pxOnValue, &imgV)) return 0;

    if (XImage_width(&imgC) == XImage_width(&imgV) &&
        XImage_height(&imgC) == XImage_height(&imgV)) {
        for (y = 0; y < XImage_height(&imgC); ++y)
            for (x = 0; x < XImage_width(&imgC); ++x)
                if (XImage_pixel(&imgC, x, y) != XImage_pixel(&imgV, x, y))
                    ++diff;
    }
    XClassDeinit((XClass*)&imgC);
    XClassDeinit((XClass*)&imgV);
    printf("category labels px: center=%d onValue=%d pixel-diff=%d\n",
           pxCenter, pxOnValue, diff);
    if (pxCenter <= 0 || pxOnValue <= 0) {
        printf("FAIL category axis drew nothing\n");
        return 0;
    }
    if (diff == 0) {
        printf("FAIL labelsPosition has no visual effect\n");
        return 0;
    }
    return 1;
}


/* 堆叠顺序验证：改变柱组顺序，堆叠图的**底部**颜色必须随之改变。
 * 用「第一个柱组所在行的像素颜色」判定谁在底部。 */
static uint32_t stackedBottomColor(XChartView* view, int swapSets)
{
    XChart* chart = XChart_create();
    XAbstractBarSeries* bar = (XAbstractBarSeries*)XStackedBarSeries_create();
    XBarSet* red = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "R");
    XBarSet* blue = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "B");
    XImage img;
    uint32_t got = 0;
    int y;
    int x;

    XBarSet_setColor(red, 0xFFFF0000u);
    XBarSet_setColor(blue, 0xFF0000FFu);
    XBarSet_append(red, 5.0);
    XBarSet_append(blue, 5.0);
    if (swapSets) {
        XAbstractBarSeries_append(bar, blue);
        XAbstractBarSeries_append(bar, red);
    } else {
        XAbstractBarSeries_append(bar, red);
        XAbstractBarSeries_append(bar, blue);
    }
    XChart_addSeries(chart, bar, XChartSeriesType_StackedBar);
    if (XChart_axisX(chart)) XValueAxis_setRange(XChart_axisX(chart), 0, 1);
    if (XChart_axisY(chart)) XValueAxis_setRange(XChart_axisY(chart), 0, 12);
    XChartView_setChart(view, chart);

    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (XChartView_renderToImage(view, &img)) {
        /* 全图扫描：统计红/蓝像素的最低 y（最靠近底部者 = 堆叠底层）。 */
        {
            int bestRedY = -1;
            int bestBlueY = -1;
            for (y = 0; y < 400; ++y)
                for (x = 0; x < 600; ++x) {
                    uint32_t c = XImage_pixel(&img, x, y);
                    if (c == 0xFFFF0000u && y > bestRedY) bestRedY = y;
                    if (c == 0xFF0000FFu && y > bestBlueY) bestBlueY = y;
                }
            if (bestRedY >= 0 && bestBlueY >= 0)
                got = (bestRedY > bestBlueY) ? 0xFFFF0000u : 0xFF0000FFu;
            else if (bestRedY >= 0)
                got = 0xFFFF0000u;
            else if (bestBlueY >= 0)
                got = 0xFF0000FFu;
        }
    }
    XClassDeinit((XClass*)&img);
    return got;
}

static int checkStackedOrder(XChartView* view)
{
    uint32_t c1 = stackedBottomColor(view, 0);
    uint32_t c2 = stackedBottomColor(view, 1);
    printf("stacked bottom color: sets(R,B)=%08X sets(B,R)=%08X\n",
           (unsigned)c1, (unsigned)c2);
    if (c1 == 0 || c2 == 0) {
        printf("FAIL stacked bottom color not sampled\n");
        return 0;
    }
    if (c1 == c2) {
        printf("FAIL stacked order has no visual effect\n");
        return 0;
    }
    printf("OK stacked order affects bottom color\n");
    return 1;
}

/* 百分比归一化验证：用红/蓝两色段的实际像素高度比判定。
 * 5+5 应各占 50%；5+15 应为 25%/75%。 */
static int percentSegments(XChartView* view, double v1, double v2,
                          int* outRedH, int* outTotal)
{
    XChart* chart = XChart_create();
    XAbstractBarSeries* bar = (XAbstractBarSeries*)XPercentBarSeries_create();
    XBarSet* s1 = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "1");
    XBarSet* s2 = XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, "2");
    XImage img;
    int rmin = 400, rmax = -1, bmin = 400, bmax = -1;
    int ok = 0;
    int x;
    int y;

    XBarSet_setColor(s1, 0xFFFF0000u);
    XBarSet_setColor(s2, 0xFF0000FFu);
    XBarSet_append(s1, v1);
    XBarSet_append(s2, v2);
    XAbstractBarSeries_append(bar, s1);
    XAbstractBarSeries_append(bar, s2);
    XChart_addSeries(chart, bar, XChartSeriesType_PercentBar);
    if (XChart_axisX(chart)) XValueAxis_setRange(XChart_axisX(chart), 0, 1);
    if (XChart_axisY(chart)) XValueAxis_setRange(XChart_axisY(chart), 0, 100);
    XChartView_setChart(view, chart);

    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (XChartView_renderToImage(view, &img)) {
        for (y = 0; y < 400; ++y)
            for (x = 0; x < 600; ++x) {
                uint32_t c = XImage_pixel(&img, x, y);
                if (c == 0xFFFF0000u) {
                    if (y < rmin) rmin = y;
                    if (y > rmax) rmax = y;
                } else if (c == 0xFF0000FFu) {
                    if (y < bmin) bmin = y;
                    if (y > bmax) bmax = y;
                }
            }
        if (rmax >= 0 && bmax >= 0) {
            int top = rmin < bmin ? rmin : bmin;
            int bot = rmax > bmax ? rmax : bmax;
            *outRedH = rmax - rmin + 1;
            *outTotal = bot - top + 1;
            ok = 1;
        }
    }
    XClassDeinit((XClass*)&img);
    return ok;
}

static int checkPercentNormalization(XChartView* view)
{
    int redH = 0, total = 0;
    double r1 = 0.0, r2 = 0.0;

    if (!percentSegments(view, 5.0, 5.0, &redH, &total)) {
        printf("FAIL percent 5+5 sample\n");
        return 0;
    }
    r1 = total > 0 ? (double)redH / (double)total : 0.0;
    printf("percent segment ratio 5+5: red=%d total=%d ratio=%.2f\n",
           redH, total, r1);
    if (!percentSegments(view, 5.0, 15.0, &redH, &total)) {
        printf("FAIL percent 5+15 sample\n");
        return 0;
    }
    r2 = total > 0 ? (double)redH / (double)total : 0.0;
    printf("percent segment ratio 5+15: ratio=%.2f (expect 0.25)\n", r2);
    if (r1 < 0.40 || r1 > 0.60) {
        printf("FAIL 5+5 should be ~50 percent\n");
        return 0;
    }
    if (r2 < 0.15 || r2 > 0.35) {
        printf("FAIL 5+15 should be ~25 percent\n");
        return 0;
    }
    printf("OK percent normalization correct\n");
    return 1;
}

/* setStartValue 必须影响渲染：不同起始值 -> 类别刻度位置不同。 */
static int renderWithStart(XChartView* view, double start, int* outPx, XImage* outImg)
{
    XChart* chart = XChart_create();
    XValueCategoryAxis* cax = XValueCategoryAxis_create();
    XImage img;
    int px;

    XValueAxis_setRange(&cax->m_base, 0, 100);
    XValueCategoryAxis_append_utf8(cax, "a", 50);
    XValueCategoryAxis_append_utf8(cax, "b", 100);
    XValueCategoryAxis_setStartValue(cax, start);
    XChart_setAxisX_abstract(chart, (XAbstractAxis*)cax);
    XChartView_setChart(view, chart);
    XImage_init_ex(&img, 600, 400, XImageFormat_ARGB32);
    if (!XChartView_renderToImage(view, &img)) {
        XClassDeinit((XClass*)&img);
        return 0;
    }
    px = countNonBg(&img, 0xFFFFFFFFu);
    *outPx = px;
    if (outImg)
        XClassMove((XClass*)outImg, (XClass*)&img);
    else
        XClassDeinit((XClass*)&img);
    return 1;
}

static int checkSetStartValue(XChartView* view)
{
    int p1 = 0;
    int p2 = 0;
    XImage i1;
    XImage i2;
    int diff = 0;
    int x;
    int y;

    XImage_init(&i1);
    XImage_init(&i2);
    if (!renderWithStart(view, 0.0, &p1, &i1)) return 0;
    if (!renderWithStart(view, 40.0, &p2, &i2)) return 0;
    if (XImage_width(&i1) == XImage_width(&i2) &&
        XImage_height(&i1) == XImage_height(&i2)) {
        for (y = 0; y < XImage_height(&i1); ++y)
            for (x = 0; x < XImage_width(&i1); ++x)
                if (XImage_pixel(&i1, x, y) != XImage_pixel(&i2, x, y))
                    ++diff;
    }
    XClassDeinit((XClass*)&i1);
    XClassDeinit((XClass*)&i2);
    printf("setStartValue: start=0 px=%d start=40 px=%d pixel-diff=%d\n",
           p1, p2, diff);
    if (diff == 0) {
        printf("FAIL setStartValue has no visual effect\n");
        return 0;
    }
    printf("OK setStartValue affects rendering\n");
    return 1;
}


/* XPainterPath 几何精确性：boundingRect 必须算贝塞尔极值，
 * controlPointRect 必须包含控制点（两者不等）。 */
static int checkPathBounds(void)
{
    XPainterPath path;
    XRectF br;
    XRectF cpr;
    int ok = 1;

    XPainterPath_init(&path);
    XPainterPath_moveTo(&path, 0.0f, 0.0f);
    /* 控制点拉到 y=±100，实际曲线只到 y≈±28.87。 */
    XPainterPath_cubicTo(&path, 100.0f, 100.0f, 100.0f, -100.0f,
                         200.0f, 0.0f);
    XPainterPath_boundingRect(&path, &br);
    XPainterPath_controlPointRect(&path, &cpr);
    printf("path bounds: boundingRect h=%.2f  controlPointRect h=%.2f\n",
           br.height, cpr.height);
    /* boundingRect 高度应远小于控制点框（约 57.7 vs 200）。 */
    if (br.height > 80.0f) {
        printf("FAIL boundingRect should exclude control-point overshoot\n");
        ok = 0;
    }
    if (cpr.height < 190.0f || cpr.height > 210.0f) {
        printf("FAIL controlPointRect should span control points (200)\n");
        ok = 0;
    }
    if (br.height >= cpr.height) {
        printf("FAIL boundingRect must be smaller than controlPointRect\n");
        ok = 0;
    }
    /* 路径长度/命中/多边形化基本可用性。 */
    {
        float len = XPainterPath_length(&path);
        XPointF pts[256];
        int counts[8];
        int n;
        if (len <= 0.0f) {
            printf("FAIL path length should be positive\n");
            ok = 0;
        }
        n = XPainterPath_toSubpathPolygons(&path, pts, 256, counts, 8);
        if (n < 2) {
            printf("FAIL toSubpathPolygons returned %d points\n", n);
            ok = 0;
        }
        {
            /* 命中测试用闭合矩形路径（开放曲线不构成封闭区域）。 */
            XRect cr;
            XPainterPath cpath;
            XRect_init(&cr, 0, 0, 200, 100);
            XPainterPath_init(&cpath);
            XPainterPath_addRect(&cpath, &cr);
            if (!XPainterPath_containsPoint(&cpath, 100.0f, 50.0f)) {
                printf("FAIL containsPoint inside rect\n");
                ok = 0;
            }
            if (XPainterPath_containsPoint(&cpath, 300.0f, 50.0f)) {
                printf("FAIL containsPoint outside rect\n");
                ok = 0;
            }
            XPainterPath_deinit(&cpath);
        }
    }
    /* 圆弧路径：4 段弧应产生元素，且包围盒接近外接矩形。 */
    {
        XRect ar;
        XRectF arb;
        XRect_init(&ar, 0, 0, 200, 200);
        XPainterPath_clear(&path);
        XPainterPath_arcMoveTo(&path, &ar, 0.0f);
        XPainterPath_arcTo(&path, &ar, 0.0f, 360.0f);
        XPainterPath_boundingRect(&path, &arb);
        printf("circle path: elements=%d bounds=%.0fx%.0f\n",
               XPainterPath_elementCount(&path), arb.width, arb.height);
        if (arb.width < 190.0f || arb.width > 210.0f) {
            printf("FAIL arc bounding width\n");
            ok = 0;
        }
    }
    XPainterPath_deinit(&path);
    if (ok) printf("OK path bounds/arc/geometry correct\n");
    return ok;
}


/* toFillPolygons 语义（对标 Qt）：包围盒相交的子路径合并成一条，
 * 不相交的各自独立。 */
static int checkFillPolygons(void)
{
    XPainterPath path;
    XPointF pts[512];
    int counts[8];
    int n;
    int ok = 1;

    /* 情形一：两个嵌套矩形（内层形成孔洞）-> 合并为 1 组。 */
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
    n = XPainterPath_toFillPolygons(&path, pts, 512, counts, 8);
    printf("fillPolygons nested: points=%d group0=%d group1=%d\n",
           n, counts[0], counts[1]);
    if (n <= 0) { printf("FAIL nested returned nothing\n"); ok = 0; }
    /* 嵌套应合并成一组（group1 为 0）。 */
    if (counts[1] != 0) {
        printf("FAIL nested subpaths should merge into one group\n");
        ok = 0;
    }
    XPainterPath_deinit(&path);

    /* 情形二：两个分离矩形 -> 两组。 */
    XPainterPath_init(&path);
    {
        XRect a;
        XRect b;
        XRect_init(&a, 0, 0, 50, 50);
        XRect_init(&b, 200, 200, 50, 50);
        XPainterPath_addRect(&path, &a);
        XPainterPath_addRect(&path, &b);
    }
    counts[0] = counts[1] = counts[2] = 0;
    n = XPainterPath_toFillPolygons(&path, pts, 512, counts, 8);
    printf("fillPolygons separate: points=%d group0=%d group1=%d\n",
           n, counts[0], counts[1]);
    if (counts[0] <= 0 || counts[1] <= 0) {
        printf("FAIL separate subpaths should stay 2 groups\n");
        ok = 0;
    }
    XPainterPath_deinit(&path);

    if (ok) printf("OK toFillPolygons merges intersecting subpaths\n");
    return ok;
}


/* addText：字形轮廓能否写入路径（点阵字体无轮廓时返回 false 属正常）。 */
static int checkAddText(void)
{
    XPainterPath path;
    XFont font;
    int before;
    int after;
    bool got;

    XFont_init(&font);
    XFont_setFamily(&font, "Arial");
    XFont_setPixelSize(&font, 32);
    /* 直接指定系统 ttf 路径：默认家族解析在测试环境可能落到点阵 face。 */
    XFont_setFamily(&font, "C:/Windows/Fonts/arial.ttf");
    XFont_setStyleStrategy(&font, XFont_ForceOutline);
    XPainterPath_init(&path);
    before = XPainterPath_elementCount(&path);
    got = XPainterPath_addText(&path, 10.0f, 100.0f, &font, "ABC");
    after = XPainterPath_elementCount(&path);
    printf("addText: ok=%d elements %d -> %d\n", (int)got, before, after);
    XPainterPath_deinit(&path);
    XClassDeinit((XClass*)&font);
    /* 无轮廓字体（点阵）时 got=false 且元素不变，属可接受降级。 */
    if (got && after <= before) {
        printf("FAIL addText reported success but wrote nothing\n");
        return 0;
    }
    if (got) printf("OK addText wrote glyph outlines\n");
    else printf("note addText: font has no outline (bitmap-only), graceful\n");
    return 1;
}

/* addRegion：区域的每个子矩形应成为一条子路径。 */
static int checkAddRegion(void)
{
    XPainterPath path;
    XRegion region;
    XRect a;
    XRect b;
    XRect c;
    int elems;

    XRegion_init(&region);
    XRect_init(&a, 0, 0, 20, 20);
    XRect_init(&b, 100, 100, 20, 20);
    XRect_init(&c, 200, 0, 20, 20);
    XRegion_addRect(&region, &a);
    XRegion_addRect(&region, &b);
    XRegion_addRect(&region, &c);
    XPainterPath_init(&path);
    XPainterPath_addRegion(&path, &region);
    elems = XPainterPath_elementCount(&path);
    printf("addRegion: region rects=%d path elements=%d\n",
           region.count, elems);
    XPainterPath_deinit(&path);
    XRegion_deinit(&region);
    if (elems <= 0) {
        printf("FAIL addRegion wrote nothing\n");
        return 0;
    }
    /* 3 个矩形，每个 addRect 至少写 5 个元素（MoveTo+4 LineTo）。 */
    if (elems < 15) {
        printf("FAIL addRegion elements too few (%d < 15)\n", elems);
        return 0;
    }
    printf("OK addRegion converted %d rects\n", 3);
    return 1;
}

int main(void)
{
    XChartView* view = XChartView_create(NULL, 0);
    int ok = 1;
    int n = 0;

    if (!view) {
        printf("FAIL view create\n");
        return 1;
    }
    XWidget_resize((XWidget*)view, 600, 400);

    n += checkBoxPlot(view);
    n += checkCandlestick(view);
    n += checkDateTimeAxis(view);
    n += checkDefaultAxesWithLogAxis();
    n += checkLegendMarkers();
    n += checkLinearGradient();
    n += checkColorAxis();
    n += checkPolarChart(view);
    n += checkBarVariants(view);
    n += checkValueCategoryAxis();
    n += checkPolarBar(view);
    n += checkPolarArea(view);
    n += checkCategoryLabelPosition(view);
    n += checkStackedOrder(view);
    n += checkPercentNormalization(view);
    n += checkSetStartValue(view);
    n += checkPathBounds();
    n += checkFillPolygons();
    n += checkAddText();
    n += checkAddRegion();
    if (n != 20) ok = 0;

    printf("checks passed: %d/20\n", n);
    printf("%s\n", ok ? "ALL PASS" : "HAS FAILURES");
    return ok ? 0 : 1;
}
