#include "XChartView.h"
#include "XValueAxis.h"
#include "XLineSeries.h"
#include "XPieSeries.h"
#include "XBarSeries.h"
#include "XScatterSeries.h"
#include "XAreaSeries.h"
#include "XSplineSeries.h"
#include "XMemory.h"
#include "XPainter.h"
#include "XWidget_Protected.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#if XCHARTS_ON

#define XCV_MARGIN_L   48
#define XCV_MARGIN_R   12
#define XCV_MARGIN_T   34
#define XCV_MARGIN_B   30

static uint32_t xcv_color(const XChartView* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self; (void)role;
    return 0xFF000000u;
#endif
}

/** @brief 布局：标题区/图例区/绘图区矩形。 */
static void xcv_layout(const XChartView* self, XRect* titleR,
                       XRect* plotR, XRect* legendR)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int top = 0;
    if (self->m_chart && self->m_chart->m_titleVisible)
        top = XCV_MARGIN_T;
    if (titleR) XRect_init(titleR, 0, 0, w, top);
    if (plotR) XRect_init(plotR, XCV_MARGIN_L, top + 4,
                          w - XCV_MARGIN_L - XCV_MARGIN_R,
                          h - top - 4 - XCV_MARGIN_B);
    if (legendR) XRect_init(legendR, w - 150, top + 8, 145, 20 * 4);
}

/** @brief 绘制标题（居中）。 */
static void xcv_paintTitle(XChartView* self, XPainter* painter,
                           const XRect* titleR)
{
    uint32_t text = xcv_color(self, XPaletteColorRole_WindowText);
    XFont font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    XPainter_drawText(painter, titleR->x + titleR->width / 2 -
                      (int)strlen(self->m_chart->m_title) * 4,
                      titleR->y + titleR->height - 8,
                      self->m_chart->m_title, text);
}

/** @brief 绘制数值轴网格 + 刻度标签。 */
static void xcv_paintAxes(XChartView* self, XPainter* painter,
                          const XRect* plotR)
{
    XValueAxis* ax = self->m_chart->m_axisX;
    XValueAxis* ay = self->m_chart->m_axisY;
    uint32_t dark = xcv_color(self, XPaletteColorRole_Dark);
    uint32_t mid = xcv_color(self, XPaletteColorRole_Mid);
    uint32_t text = xcv_color(self, XPaletteColorRole_WindowText);
    XFont font = XWidget_font((XWidget*)self);
    char buf[32];
    int i;
    int ticks;
    XPainter_setFont(painter, &font);
    if (!ax || !ay) return;
    /* 轴线。 */
    XPainter_setPen(painter, dark);
    XPainter_drawLine(painter, plotR->x, plotR->y,
                      plotR->x, plotR->y + plotR->height);
    XPainter_drawLine(painter, plotR->x, plotR->y + plotR->height,
                      plotR->x + plotR->width,
                      plotR->y + plotR->height);
    ticks = ay->m_tickCount > 1 ? ay->m_tickCount : 2;
    for (i = 0; i < ticks; ++i) {
        double t = (double)i / (ticks - 1);
        double v = ay->m_max - t * (ay->m_max - ay->m_min);
        int y = plotR->y + (int)(t * plotR->height);
        if (ay->m_gridVisible && i > 0)
            XPainter_drawLine(painter, plotR->x, y,
                              plotR->x + plotR->width, y);
        snprintf(buf, sizeof(buf), ay->m_labelFormat, v);
        XPainter_drawText(painter, plotR->x - 40, y + 6, buf, text);
    }
    ticks = ax->m_tickCount > 1 ? ax->m_tickCount : 2;
    for (i = 0; i < ticks; ++i) {
        double t = (double)i / (ticks - 1);
        double v = ax->m_min + t * (ax->m_max - ax->m_min);
        int x = plotR->x + (int)(t * plotR->width);
        if (ax->m_gridVisible && i > 0)
            XPainter_drawLine(painter, x, plotR->y, x,
                              plotR->y + plotR->height);
        snprintf(buf, sizeof(buf), ax->m_labelFormat, v);
        XPainter_drawText(painter, x - 12,
                          plotR->y + plotR->height + 16, buf, text);
    }
}

/** @brief 绘制折线序列（按轴范围缩放）。 */
static void xcv_paintLines(XChartView* self, XPainter* painter,
                           const XRect* plotR)
{
    int si;
    for (si = 0; si < self->m_chart->m_lineCount; ++si) {
        XLineSeries* s = self->m_chart->m_lineSeries[si];
        uint32_t color = s->m_color != 0
            ? s->m_color : XChart_themeColor(self->m_chart, si);
        int pi;
        if (!s->m_visible || s->m_count < 2) continue;
        XPainter_setPen(painter, color);
        for (pi = 0; pi < s->m_count - 1; ++pi) {
            const XPointF* p0 = &s->m_points[pi];
            const XPointF* p1 = &s->m_points[pi + 1];
            int x0; int y0; int x1; int y1;
            XValueAxis* ax = self->m_chart->m_axisX;
            XValueAxis* ay = self->m_chart->m_axisY;
            double rx = ax->m_max - ax->m_min;
            double ry = ay->m_max - ay->m_min;
            if (rx <= 0 || ry <= 0) continue;
            x0 = plotR->x + (int)((p0->x - ax->m_min) / rx * plotR->width);
            x1 = plotR->x + (int)((p1->x - ax->m_min) / rx * plotR->width);
            y0 = plotR->y + (int)((ay->m_max - p0->y) / ry * plotR->height);
            y1 = plotR->y + (int)((ay->m_max - p1->y) / ry * plotR->height);
            XPainter_drawLine(painter, x0, y0, x1, y1);
        }
    }
}

/** @brief 绘制饼图（切片占比扇形 + 标签）。 */
static void xcv_paintPie(XChartView* self, XPainter* painter,
                         const XRect* plotR)
{
    XPieSeries* pie = self->m_chart->m_pieSeries;
    double sum;
    double angle;
    int cx;
    int cy;
    int radius;
    int i;
    if (!pie || pie->m_count <= 0 || !pie->m_visible) return;
    sum = XPieSeries_sum(pie);
    if (sum <= 0) return;
    cx = plotR->x + plotR->width / 2;
    cy = plotR->y + plotR->height / 2;
    radius = (plotR->width < plotR->height ? plotR->width
             : plotR->height) / 2 - 8;
    angle = 90.0;
    for (i = 0; i < pie->m_count; ++i) {
        XPieSlice* slice = &pie->m_slices[i];
        double frac = slice->m_value / sum;
        double sweep = frac * 360.0;
        uint32_t color = slice->m_color != 0
            ? slice->m_color : XChart_themeColor(self->m_chart, i);
        double a0 = angle * 3.14159265358979323846 / 180.0;
        double a1 = (angle - sweep) * 3.14159265358979323846 / 180.0;
        XPoint tri[3];
        tri[0].x = cx; tri[0].y = cy;
        tri[1].x = cx + (int)(radius * cos(a0));
        tri[1].y = cy - (int)(radius * sin(a0));
        tri[2].x = cx + (int)(radius * cos(a1));
        tri[2].y = cy - (int)(radius * sin(a1));
        XPainter_setBrush(painter, color);
        XPainter_setPen(painter, color);
        XPainter_drawPie(painter,
            &(XRect){cx - radius, cy - radius, radius * 2, radius * 2},
            (int)(angle * 16), (int)(-sweep * 16));
        {
            double am = (angle - sweep / 2) * 3.14159265358979323846 / 180.0;
            XPainter_drawText(painter,
                cx + (int)(radius * 0.65 * cos(am)) - 12,
                cy - (int)(radius * 0.65 * sin(am)),
                slice->m_label, 0xFFFFFFFFu);
        }
        (void)tri;
        angle -= sweep;
    }
}

/** @brief 数据点 → 屏幕坐标（按轴范围缩放）。 */
static void xcv_mapPoint(const XChartView* self, const XRect* plotR,
                         double x, double y, int* sx, int* sy)
{
    XValueAxis* ax = self->m_chart->m_axisX;
    XValueAxis* ay = self->m_chart->m_axisY;
    double rx = ax->m_max - ax->m_min;
    double ry = ay->m_max - ay->m_min;
    if (rx <= 0) rx = 1;
    if (ry <= 0) ry = 1;
    *sx = plotR->x + (int)((x - ax->m_min) / rx * plotR->width);
    *sy = plotR->y + (int)((ay->m_max - y) / ry * plotR->height);
}

/** @brief 绘制柱状序列（组宽内矩形 + 数值刻度对齐）。 */
static void xcv_paintBars(XChartView* self, XPainter* painter,
                          const XRect* plotR)
{
    int si;
    for (si = 0; si < self->m_chart->m_barCount; ++si) {
        XBarSeries* s = self->m_chart->m_barSeries[si];
        uint32_t color = s->m_color != 0
            ? s->m_color : XChart_themeColor(self->m_chart, si);
        int i;
        if (!s->m_visible || s->m_count == 0) continue;
        XPainter_setPen(painter, color);
        for (i = 0; i < s->m_count; ++i) {
            double v = s->m_values[i];
            int sx; int sy0; int sy1;
            int bw = (int)(plotR->width / (s->m_count > 0 ? s->m_count : 1)
                           * s->m_barWidth);
            xcv_mapPoint(self, plotR, i + 0.5, v, &sx, &sy1);
            xcv_mapPoint(self, plotR, i + 0.5, 0, &sx, &sy0);
            if (bw < 4) bw = 4;
            {
                /* 柱体：零基线与柱顶间矩形（值可能为负）。 */
                int top = sy1 < sy0 ? sy1 : sy0;
                int hgt = sy1 > sy0 ? sy1 - sy0 : sy0 - sy1;
                if (hgt < 1) hgt = 1;
                XPainter_fillRect(painter,
                    &(XRect){sx - bw / 2, top, bw, hgt}, color);
            }
        }
    }
}

/** @brief 绘制散点序列（圆点标记）。 */
static void xcv_paintScatter(XChartView* self, XPainter* painter,
                             const XRect* plotR)
{
    int si;
    for (si = 0; si < self->m_chart->m_scatterCount; ++si) {
        XScatterSeries* s = self->m_chart->m_scatterSeries[si];
        uint32_t color = s->m_color != 0
            ? s->m_color : XChart_themeColor(self->m_chart, si);
        int pi;
        if (!s->m_visible) continue;
        XPainter_setPen(painter, color);
        for (pi = 0; pi < s->m_count; ++pi) {
            int sx; int sy;
            int r = s->m_markerSize / 2;
            xcv_mapPoint(self, plotR, s->m_points[pi].x, s->m_points[pi].y,
                         &sx, &sy);
            XPainter_fillRect(painter,
                &(XRect){sx - r, sy - r, s->m_markerSize, s->m_markerSize},
                color);
        }
    }
}

/** @brief 绘制面积序列（上边界折线 + 基线填充）。 */
static void xcv_paintArea(XChartView* self, XPainter* painter,
                          const XRect* plotR)
{
    int si;
    for (si = 0; si < self->m_chart->m_areaCount; ++si) {
        XAreaSeries* s = self->m_chart->m_areaSeries[si];
        XLineSeries* up = s->m_upper;
        uint32_t color = s->m_color != 0
            ? s->m_color : XChart_themeColor(self->m_chart, si);
        int pi;
        if (!s->m_visible || !up || up->m_count < 2) continue;
        XPainter_setBrush(painter, color);
        XPainter_setPen(painter, color);
        for (pi = 0; pi < up->m_count - 1; ++pi) {
            int ax0; int ay0; int ax1; int ay1; int bx0; int by1;
            xcv_mapPoint(self, plotR, up->m_points[pi].x, up->m_points[pi].y,
                         &ax0, &ay0);
            xcv_mapPoint(self, plotR, up->m_points[pi + 1].x,
                         up->m_points[pi + 1].y, &ax1, &ay1);
            xcv_mapPoint(self, plotR, up->m_points[pi + 1].x, s->m_baseValue,
                         &bx0, &by1);
            XPainter_drawPie(painter,
                &(XRect){ax0 - 1, ay0, ax1 - ax0 + 2, by1 - ay0 + 1},
                0, 0);
        }
        {
            int px; int py; int i;
            for (i = 0; i < up->m_count - 1; ++i) {
                xcv_mapPoint(self, plotR, up->m_points[i].x,
                             up->m_points[i].y, &px, &py);
                {
                    int qx; int qy;
                    xcv_mapPoint(self, plotR, up->m_points[i + 1].x,
                                 up->m_points[i + 1].y, &qx, &qy);
                    XPainter_drawLine(painter, px, py, qx, qy);
                }
            }
        }
    }
}

/** @brief 绘制样条序列（Catmull-Rom 插值，每段 8 细分）。 */
static void xcv_paintSpline(XChartView* self, XPainter* painter,
                            const XRect* plotR)
{
    int si;
    for (si = 0; si < self->m_chart->m_splineCount; ++si) {
        XSplineSeries* s = self->m_chart->m_splineSeries[si];
        uint32_t color = s->m_color != 0
            ? s->m_color : XChart_themeColor(self->m_chart, si);
        int pi;
        if (!s->m_visible || s->m_count < 2) continue;
        XPainter_setPen(painter, color);
        for (pi = 0; pi < s->m_count - 1; ++pi) {
            int sub;
            XPointF p0 = s->m_points[pi];
            XPointF p1 = s->m_points[pi + 1];
            XPointF pm1 = pi > 0 ? s->m_points[pi - 1] : p0;
            XPointF p2 = pi + 2 < s->m_count ? s->m_points[pi + 2] : p1;
            for (sub = 0; sub < 8; ++sub) {
                double t0 = (double)sub / 8;
                double t1 = (double)(sub + 1) / 8;
                double x0; double y0; double x1; double y1;
                int sx0; int sy0; int sx1; int sy1;
                x0 = 0.5 * ((2 * p1.x) + (-p0.x + p2.x) * t0 +
                     (2 * p0.x - 5 * p1.x + 4 * p2.x - pm1.x) * t0 * t0 +
                     (-p0.x + 3 * p1.x - 3 * p2.x + pm1.x) * t0 * t0 * t0);
                y0 = 0.5 * ((2 * p1.y) + (-p0.y + p2.y) * t0 +
                     (2 * p0.y - 5 * p1.y + 4 * p2.y - pm1.y) * t0 * t0 +
                     (-p0.y + 3 * p1.y - 3 * p2.y + pm1.y) * t0 * t0 * t0);
                x1 = 0.5 * ((2 * p1.x) + (-p0.x + p2.x) * t1 +
                     (2 * p0.x - 5 * p1.x + 4 * p2.x - pm1.x) * t1 * t1 +
                     (-p0.x + 3 * p1.x - 3 * p2.x + pm1.x) * t1 * t1 * t1);
                y1 = 0.5 * ((2 * p1.y) + (-p0.y + p2.y) * t1 +
                     (2 * p0.y - 5 * p1.y + 4 * p2.y - pm1.y) * t1 * t1 +
                     (-p0.y + 3 * p1.y - 3 * p2.y + pm1.y) * t1 * t1 * t1);
                xcv_mapPoint(self, plotR, x0, y0, &sx0, &sy0);
                xcv_mapPoint(self, plotR, x1, y1, &sx1, &sy1);
                XPainter_drawLine(painter, sx0, sy0, sx1, sy1);
            }
        }
    }
}

/** @brief 绘制图例（色块 + 序列名）。 *//** @brief 绘制图例（色块 + 序列名）。 */
static void xcv_paintLegend(XChartView* self, XPainter* painter,
                            const XRect* legendR)
{
    int i;
    uint32_t text = xcv_color(self, XPaletteColorRole_WindowText);
    XFont font = XWidget_font((XWidget*)self);
    int y = legendR->y;
    XPainter_setFont(painter, &font);
    for (i = 0; i < self->m_chart->m_lineCount && y < legendR->y + 80; ++i) {
        XLineSeries* s = self->m_chart->m_lineSeries[i];
        uint32_t color = s->m_color != 0
            ? s->m_color : XChart_themeColor(self->m_chart, i);
        XPainter_fillRect(painter,
            &(XRect){legendR->x, y, 12, 12}, color);
        XPainter_drawText(painter, legendR->x + 18, y + 10,
                          s->m_name, text);
        y += 20;
    }
    {   /* 柱状图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_barCount && y < legendR->y + 80; ++k) {
            XBarSeries* b = self->m_chart->m_barSeries[k];
            uint32_t color = b->m_color != 0
                ? b->m_color : XChart_themeColor(self->m_chart, k);
            XPainter_fillRect(painter,
                &(XRect){legendR->x, y, 12, 12}, color);
            XPainter_drawText(painter, legendR->x + 18, y + 10,
                              b->m_name, text);
            y += 20;
        }
    }
    {   /* 散点图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_scatterCount && y < legendR->y + 80; ++k) {
            XScatterSeries* sc = self->m_chart->m_scatterSeries[k];
            uint32_t color = sc->m_color != 0
                ? sc->m_color : XChart_themeColor(self->m_chart, k);
            XPainter_fillRect(painter,
                &(XRect){legendR->x, y, 12, 12}, color);
            XPainter_drawText(painter, legendR->x + 18, y + 10,
                              sc->m_name, text);
            y += 20;
        }
    }
    {   /* 面积图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_areaCount && y < legendR->y + 80; ++k) {
            XAreaSeries* ar = self->m_chart->m_areaSeries[k];
            uint32_t color = ar->m_color != 0
                ? ar->m_color : XChart_themeColor(self->m_chart, k);
            XPainter_fillRect(painter,
                &(XRect){legendR->x, y, 12, 12}, color);
            XPainter_drawText(painter, legendR->x + 18, y + 10,
                              ar->m_name, text);
            y += 20;
        }
    }
    {   /* 样条图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_splineCount && y < legendR->y + 80; ++k) {
            XSplineSeries* sp = self->m_chart->m_splineSeries[k];
            uint32_t color = sp->m_color != 0
                ? sp->m_color : XChart_themeColor(self->m_chart, k);
            XPainter_fillRect(painter,
                &(XRect){legendR->x, y, 12, 12}, color);
            XPainter_drawText(painter, legendR->x + 18, y + 10,
                              sp->m_name, text);
            y += 20;
        }
    }
    {   /* 饼图图例：逐切片。 */
        XPieSeries* pie = self->m_chart->m_pieSeries;
        int k;
        for (k = 0; pie && k < pie->m_count && y < legendR->y + 80; ++k) {
            uint32_t color = pie->m_slices[k].m_color != 0
                ? pie->m_slices[k].m_color
                : XChart_themeColor(self->m_chart, k);
            XPainter_fillRect(painter,
                &(XRect){legendR->x, y, 12, 12}, color);
            XPainter_drawText(painter, legendR->x + 18, y + 10,
                              pie->m_slices[k].m_label, text);
            y += 20;
        }
    }
}

/** @brief paintEvent：标题 → 网格轴 → 序列 → 图例。 */
static void VX_chartView_paintEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect titleR;
    XRect plotR;
    XRect legendR;
    if (!cv || !event || !cv->m_chart) return;
    image = XWidget_paintDevice(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    xcv_layout(cv, &titleR, &plotR, &legendR);
    {
        uint32_t base = xcv_color(cv, XPaletteColorRole_Base);
        XPainter_fillRect(&painter,
            &(XRect){0, 0, XWidget_width(self), XWidget_height(self)}, base);
    }
    if (cv->m_chart->m_titleVisible)
        xcv_paintTitle(cv, &painter, &titleR);
    xcv_paintAxes(cv, &painter, &plotR);
    /* 序列域裁剪：样条过冲/散点越界不得溢出绘图区（对标 Qt Charts
     * 的 domain 裁剪语义）。 */
    XPainter_setClipRect(&painter, &plotR, XPainterClipOperation_ReplaceClip);
    xcv_paintLines(cv, &painter, &plotR);
    xcv_paintSpline(cv, &painter, &plotR);
    xcv_paintArea(cv, &painter, &plotR);
    xcv_paintBars(cv, &painter, &plotR);
    xcv_paintScatter(cv, &painter, &plotR);
    XPainter_setClipRect(&painter, &plotR, XPainterClipOperation_NoClip);
    if (cv->m_chart->m_pieSeries)
        xcv_paintPie(cv, &painter, &plotR);
    if (cv->m_chart->m_legendVisible)
        xcv_paintLegend(cv, &painter, &legendR);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

XVtable* XChartView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XChartView)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_chartView_paintEvent);
    return XVTABLE_DEFAULT;
}

void XChartView_init(XChartView* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XChartView);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_chart = XChart_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XWidget_resize(self, 320, 240);
}

XChartView* XChartView_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XChartView* self = (XChartView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XChartView_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XChart* XChartView_chart(const XChartView* self)
{ return self ? self->m_chart : NULL; }

void XChartView_updateChart(XChartView* self)
{ if (self) XWidget_update((XWidget*)self); }

#endif /* XCHARTS_ON */