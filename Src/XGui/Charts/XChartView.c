#include "XChartView.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#include "XAbstractSeries.h"
#include "XString.h"
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

/** @brief 布局：标题区/图例区/绘图区矩形（边距读图表 m_margins）。 */
static void xcv_layout(const XChartView* self, XRect* titleR,
                       XRect* plotR, XRect* legendR)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int left = XCV_MARGIN_L;
    int right = XCV_MARGIN_R;
    int top = XCV_MARGIN_T;
    int bottom = XCV_MARGIN_B;
    const XMargins* m;
    if (self->m_chart) {
        m = &self->m_chart->m_margins;
        if (m->left > 0) left = m->left;
        if (m->top > 0) top = m->top;
        if (m->right > 0) right = m->right;
        if (m->bottom > 0) bottom = m->bottom;
    }
    if (self->m_chart && !self->m_chart->m_titleVisible)
        top = 8;
    if (titleR) XRect_init(titleR, 0, 0, w, top);
    if (plotR) XRect_init(plotR, left, top + 4,
                          w - left - right, h - top - 4 - bottom);
    if (legendR) XRect_init(legendR, w - 150, top + 8, 145, 20 * 4);
}

/** @brief 绘制标题（居中）。 */
static void xcv_paintTitle(XChartView* self, XPainter* painter,
                           const XRect* titleR)
{
    uint32_t text = xcv_color(self, XPaletteColorRole_WindowText);
    XFont font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    {
        const char* title = XChart_title(self->m_chart);
        XPainter_drawText(painter, titleR->x + titleR->width / 2 -
                          (int)XStrlen(title) * 4,
                          titleR->y + titleR->height - 8, title, text);
    }
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
        XSnprintf(buf, sizeof(buf), XString_toUtf8(ay->m_labelFormat), v);
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
        XSnprintf(buf, sizeof(buf), XString_toUtf8(ax->m_labelFormat), v);
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
        uint32_t color = s->m_base.m_color != 0
            ? s->m_base.m_color : XChart_themeColor(self->m_chart, si);
        int pi;
        if (!XAbstractSeries_isVisible(&s->m_base.m_base) || s->m_base.m_count < 2) continue;
        XPainter_setPen(painter, color);
        for (pi = 0; pi < s->m_base.m_count - 1; ++pi) {
            const XPointF* p0 = &s->m_base.m_points[pi];
            const XPointF* p1 = &s->m_base.m_points[pi + 1];
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

/** @brief 绘制饼图（切片占比扇形 + 标签）；SHAPE 裁剪关闭时跳过扇形绘制。 */
static void xcv_paintPie(XChartView* self, XPainter* painter,
                         const XRect* plotR)
{
#if XPAINTER_SHAPE_ON
    XPieSeries* pie = self->m_chart->m_pieSeries;
    double sum;
    double angle;
    int cx;
    int cy;
    int radius;
    int i;
    if (!pie || pie->m_count <= 0 || !XAbstractSeries_isVisible(&pie->m_base.m_base)) return;
    sum = XPieSeries_sum(pie);
    if (sum <= 0) return;
    cx = plotR->x + plotR->width / 2;
    cy = plotR->y + plotR->height / 2;
    radius = (plotR->width < plotR->height ? plotR->width
             : plotR->height) / 2 - 8;
    angle = XPieSeries_pieStartAngle(pie);
    for (i = 0; i < pie->m_count; ++i) {
        XPieSlice* slice = pie->m_slices[i];
        double value;
        double frac;
        double sweep;
        uint32_t color;
        if (!slice) continue;
        value = XPieSlice_value(slice);
        frac = value / sum;
        sweep = frac * 360.0;
        color = XPieSlice_color(slice) != 0
            ? XPieSlice_color(slice) : XChart_themeColor(self->m_chart, i);
        double a0 = angle * 3.14159265358979323846 / 180.0;
        double a1 = (angle - sweep) * 3.14159265358979323846 / 180.0;
        XPoint tri[3];
        tri[0].x = cx; tri[0].y = cy;
        tri[1].x = cx + (int)(radius * cos(a0));
        tri[1].y = cy - (int)(radius * sin(a0));
        tri[2].x = cx + (int)(radius * cos(a1));
        tri[2].y = cy - (int)(radius * sin(a1));
        {
            /* 分离突出：沿扇区中线把圆心外移 distance（对标 exploded）。 */
            int scx = cx;
            int scy = cy;
            if (XPieSlice_isExploded(slice)) {
                double am = (angle - sweep / 2) * 3.14159265358979323846 / 180.0;
                double dist = radius * XPieSlice_explodeDistanceFactor(slice);
                scx = cx + (int)(dist * cos(am));
                scy = cy - (int)(dist * sin(am));
            }
            XPainter_setBrush(painter, color);
            XPainter_setPen(painter, XPieSlice_borderColor(slice) != 0
                            ? XPieSlice_borderColor(slice) : color);
            XPainter_drawPie(painter,
                &(XRect){scx - radius, scy - radius, radius * 2, radius * 2},
                (int)(angle * 16), (int)(-sweep * 16));
            if (XPieSlice_isLabelVisible(slice)) {
                double am = (angle - sweep / 2) * 3.14159265358979323846 / 180.0;
                XPainter_drawText(painter,
                    scx + (int)(radius * 0.65 * cos(am)) - 12,
                    scy - (int)(radius * 0.65 * sin(am)),
                    XPieSlice_label(slice), XPieSlice_labelColor(slice));
            }
        }
        (void)tri;
        angle -= sweep;
    }
#else
    /* XPAINTER_SHAPE_ON=0：无扇形绘制能力，饼图在裁剪构建下不渲染。 */
    (void)self;
    (void)painter;
    (void)plotR;
#endif /* XPAINTER_SHAPE_ON */
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
        if (!XAbstractSeries_isVisible(&s->m_base.m_base.m_base) ||
            s->m_base.m_count == 0) continue;
        XPainter_setPen(painter, color);
        for (i = 0; i < s->m_base.m_count; ++i) {
            double v = s->m_base.m_values[i];
            int sx; int sy0; int sy1;
            int bw = (int)(plotR->width / (s->m_base.m_count > 0 ? s->m_base.m_count : 1)
                           * s->m_base.m_barWidth);
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
        uint32_t color = s->m_base.m_color != 0
            ? s->m_base.m_color : XChart_themeColor(self->m_chart, si);
        int pi;
        if (!XAbstractSeries_isVisible(&s->m_base.m_base)) continue;
        XPainter_setPen(painter, color);
        for (pi = 0; pi < s->m_base.m_count; ++pi) {
            int sx; int sy;
            int r = s->m_base.m_markerSize / 2;
            xcv_mapPoint(self, plotR, s->m_base.m_points[pi].x, s->m_base.m_points[pi].y,
                         &sx, &sy);
            XPainter_fillRect(painter,
                &(XRect){sx - r, sy - r, s->m_base.m_markerSize, s->m_base.m_markerSize},
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
        if (!XAbstractSeries_isVisible(&s->m_base) || !up ||
            up->m_base.m_count < 2) continue;
        XPainter_setBrush(painter, color);
        XPainter_setPen(painter, color);
        for (pi = 0; pi < up->m_base.m_count - 1; ++pi) {
            int ax0; int ay0; int ax1; int ay1; int bx0; int by1;
            xcv_mapPoint(self, plotR, up->m_base.m_points[pi].x, up->m_base.m_points[pi].y,
                         &ax0, &ay0);
            xcv_mapPoint(self, plotR, up->m_base.m_points[pi + 1].x,
                         up->m_base.m_points[pi + 1].y, &ax1, &ay1);
            xcv_mapPoint(self, plotR, up->m_base.m_points[pi + 1].x, s->m_baseValue,
                         &bx0, &by1);
            /* 每段梯形以基线与上边界间的矩形填充近似（轴对齐数据）。 */
            {
                int top = ay0 < by1 ? ay0 : by1;
                int hgt = ay0 < by1 ? by1 - ay0 : ay0 - by1;
                int left = ax0 < ax1 ? ax0 : ax1;
                int w = ax0 < ax1 ? ax1 - ax0 : ax0 - ax1;
                if (hgt < 1) hgt = 1;
                if (w < 1) w = 1;
                XPainter_fillRect(painter,
                    &(XRect){left, top, w, hgt}, color);
            }
            (void)bx0;
        }
        {
            int px; int py; int i;
            for (i = 0; i < up->m_base.m_count - 1; ++i) {
                xcv_mapPoint(self, plotR, up->m_base.m_points[i].x,
                             up->m_base.m_points[i].y, &px, &py);
                {
                    int qx; int qy;
                    xcv_mapPoint(self, plotR, up->m_base.m_points[i + 1].x,
                                 up->m_base.m_points[i + 1].y, &qx, &qy);
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
        uint32_t color = s->m_base.m_color != 0
            ? s->m_base.m_color : XChart_themeColor(self->m_chart, si);
        int pi;
        if (!XAbstractSeries_isVisible(&s->m_base.m_base) || s->m_base.m_count < 2) continue;
        XPainter_setPen(painter, color);
        for (pi = 0; pi < s->m_base.m_count - 1; ++pi) {
            int sub;
            XPointF p0 = s->m_base.m_points[pi];
            XPointF p1 = s->m_base.m_points[pi + 1];
            XPointF pm1 = pi > 0 ? s->m_base.m_points[pi - 1] : p0;
            XPointF p2 = pi + 2 < s->m_base.m_count ? s->m_base.m_points[pi + 2] : p1;
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
        uint32_t color = s->m_base.m_color != 0
            ? s->m_base.m_color : XChart_themeColor(self->m_chart, i);
        XPainter_fillRect(painter,
            &(XRect){legendR->x, y, 12, 12}, color);
        XPainter_drawText(painter, legendR->x + 18, y + 10,
                          XAbstractSeries_name(&s->m_base.m_base), text);
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
                              XAbstractSeries_name(&b->m_base.m_base), text);
            y += 20;
        }
    }
    {   /* 散点图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_scatterCount && y < legendR->y + 80; ++k) {
            XScatterSeries* sc = self->m_chart->m_scatterSeries[k];
            uint32_t color = sc->m_base.m_color != 0
                ? sc->m_base.m_color : XChart_themeColor(self->m_chart, k);
            XPainter_fillRect(painter,
                &(XRect){legendR->x, y, 12, 12}, color);
            XPainter_drawText(painter, legendR->x + 18, y + 10,
                              XAbstractSeries_name(&sc->m_base.m_base), text);
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
                              XAreaSeries_name(ar), text);
            y += 20;
        }
    }
    {   /* 样条图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_splineCount && y < legendR->y + 80; ++k) {
            XSplineSeries* sp = self->m_chart->m_splineSeries[k];
            uint32_t color = sp->m_base.m_color != 0
                ? sp->m_base.m_color : XChart_themeColor(self->m_chart, k);
            XPainter_fillRect(painter,
                &(XRect){legendR->x, y, 12, 12}, color);
            XPainter_drawText(painter, legendR->x + 18, y + 10,
                              XAbstractSeries_name(&sp->m_base.m_base), text);
            y += 20;
        }
    }
    {   /* 饼图图例：逐切片。 */
        XPieSeries* pie = self->m_chart->m_pieSeries;
        int k;
        for (k = 0; pie && k < pie->m_count && y < legendR->y + 80; ++k) {
            XPieSlice* slice = pie->m_slices[k];
            uint32_t color = slice && XPieSlice_color(slice) != 0
                ? XPieSlice_color(slice)
                : XChart_themeColor(self->m_chart, k);
            XPainter_fillRect(painter,
                &(XRect){legendR->x, y, 12, 12}, color);
            XPainter_drawText(painter, legendR->x + 18, y + 10,
                              slice ? XPieSlice_label(slice) : "", text);
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
    /* 绘图区同步到模型（对标 QChart::plotArea；变化时发 plotAreaChanged）。 */
    XChart_setPlotArea(cv->m_chart, &(XRectF){ (float)plotR.x, (float)plotR.y,
                                               (float)plotR.width,
                                               (float)plotR.height });
    /* 背景：backgroundVisible + backgroundBrush + plotAreaBackground。 */
    if (cv->m_chart->m_backgroundVisible) {
        uint32_t base = cv->m_chart->m_backgroundBrush != 0
            ? cv->m_chart->m_backgroundBrush
            : xcv_color(cv, XPaletteColorRole_Base);
        XPainter_fillRect(&painter,
            &(XRect){0, 0, XWidget_width(self), XWidget_height(self)}, base);
        if (cv->m_chart->m_backgroundPen != 0) {
            XPainter_setPen(&painter, cv->m_chart->m_backgroundPen);
            XPainter_drawRect(&painter,
                &(XRect){0, 0, XWidget_width(self), XWidget_height(self)});
        }
    }
    if (cv->m_chart->m_plotAreaBackgroundVisible) {
        if (cv->m_chart->m_plotAreaBackgroundBrush != 0)
            XPainter_fillRect(&painter, &plotR,
                              cv->m_chart->m_plotAreaBackgroundBrush);
        if (cv->m_chart->m_plotAreaBackgroundPen != 0) {
            XPainter_setPen(&painter, cv->m_chart->m_plotAreaBackgroundPen);
            XPainter_drawRect(&painter, &plotR);
        }
    }
    if (cv->m_chart->m_titleVisible)
        xcv_paintTitle(cv, &painter, &titleR);
    xcv_paintAxes(cv, &painter, &plotR);
    /* 序列域裁剪：样条过冲/散点越界不得溢出绘图区（对标 Qt Charts
     * 的 domain 裁剪语义）；XPAINTER_CLIP_ON=0 的裁剪构建下不做裁剪。 */
#if XPAINTER_CLIP_ON
    XPainter_setClipRect(&painter, &plotR, XPainterClipOperation_ReplaceClip);
#endif /* XPAINTER_CLIP_ON */
    xcv_paintLines(cv, &painter, &plotR);
    xcv_paintSpline(cv, &painter, &plotR);
    xcv_paintArea(cv, &painter, &plotR);
    xcv_paintBars(cv, &painter, &plotR);
    xcv_paintScatter(cv, &painter, &plotR);
#if XPAINTER_CLIP_ON
    XPainter_setClipRect(&painter, &plotR, XPainterClipOperation_NoClip);
#endif /* XPAINTER_CLIP_ON */
    if (cv->m_chart->m_pieSeries)
        xcv_paintPie(cv, &painter, &plotR);
    if (cv->m_chart->m_legendVisible)
        xcv_paintLegend(cv, &painter, &legendR);
    /* 框选橡皮筋：拖拽中叠加半透明矩形 + 实线边框（对标 Qt 橡皮筋观感）。 */
    if (cv->m_dragging) {
        XRect* r = &cv->m_dragRect;
        if (r->width > 0 && r->height > 0) {
            XPainter_setBrush(&painter, 0x402090FFu);
            XPainter_fillRect(&painter, r, 0x402090FFu);
            XPainter_setPen(&painter, 0xFF2090FFu);
            XPainter_drawRect(&painter, r);
        }
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ==================== 框选缩放交互（对标 QChartView 鼠标语义） ==================== */

/** @brief 按下处理（虚表入口，定义见下）。 */
static void VX_chartView_mousePressEvent(XWidget* self, XEvent* event);
/** @brief 移动处理（虚表入口，定义见下）。 */
static void VX_chartView_mouseMoveEvent(XWidget* self, XEvent* event);
/** @brief 抬起处理（虚表入口，定义见下）。 */
static void VX_chartView_mouseReleaseEvent(XWidget* self, XEvent* event);
/** @brief 滚轮处理（虚表入口，定义见下）。 */
static void VX_chartView_wheelEvent(XWidget* self, XEvent* event);

XVtable* XChartView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XChartView)    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_chartView_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_chartView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VX_chartView_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VX_chartView_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VX_chartView_wheelEvent);
    return XVTABLE_DEFAULT;
}

/* ==================== 框选缩放交互（对标 QChartView 鼠标语义） ==================== */

/**
 * @brief 把当前两轴域压入缩放栈（供框选缩放复用 zoomOut/zoomReset 语义）。
 *
 * @param self 目标视图指针。
 * @return 无返回值。
 */
static void xcv_pushCurrentDomain(XChartView* self)
{
    XChart* chart;
    XRectF* grown;
    int newCap;
    if (!self || !self->m_chart) return;
    chart = self->m_chart;
    if (!chart->m_axisX || !chart->m_axisY) return;
    if (chart->m_zoomCount >= chart->m_zoomCapacity) {
        newCap = chart->m_zoomCapacity > 0 ? chart->m_zoomCapacity * 2 : 8;
        grown = (XRectF*)XMalloc_System(sizeof(XRectF) * (size_t)newCap);
        if (!grown) return;
        if (chart->m_zoomStack) {
            XMemcpy(grown, chart->m_zoomStack,
                   sizeof(XRectF) * (size_t)chart->m_zoomCapacity);
            XFree_System(chart->m_zoomStack);
        }
        chart->m_zoomStack = grown;
        chart->m_zoomCapacity = newCap;
    }
    chart->m_zoomStack[chart->m_zoomCount].x = (float)chart->m_axisX->m_min;
    chart->m_zoomStack[chart->m_zoomCount].y = (float)chart->m_axisY->m_min;
    chart->m_zoomStack[chart->m_zoomCount].width =
        (float)(chart->m_axisX->m_max - chart->m_axisX->m_min);
    chart->m_zoomStack[chart->m_zoomCount].height =
        (float)(chart->m_axisY->m_max - chart->m_axisY->m_min);
    ++chart->m_zoomCount;
}

/** @brief 命中检测：找距像素点最近的 XY 序列点（阈值 10px）。 */
static XAbstractSeries* xcv_hitTest(XChartView* self, int px, int py,
                                    int* outIndex, double* outX, double* outY)
{
    XChart* chart;
    int si;
    int pi;
    int best = -1;
    XAbstractSeries* bestS = NULL;
    int bestDist = 10 * 10;
    double bestX = 0.0;
    double bestY = 0.0;
    XRect plotR;
    if (!self || !self->m_chart) return NULL;
    chart = self->m_chart;
    XRect_init(&plotR, 0, 0, XWidget_width((XWidget*)self),
               XWidget_height((XWidget*)self));
    for (si = 0; si < chart->m_lineCount; ++si) {
        XLineSeries* ls = chart->m_lineSeries[si];
        if (!XAbstractSeries_isVisible((XAbstractSeries*)&ls->m_base.m_base))
            continue;
        for (pi = 0; pi < ls->m_base.m_count; ++pi) {
            int sx; int sy; int dx; int dy; int d2;
            xcv_mapPoint(self, &plotR, ls->m_base.m_points[pi].x,
                         ls->m_base.m_points[pi].y, &sx, &sy);
            dx = sx - px; dy = sy - py;
            d2 = dx * dx + dy * dy;
            if (d2 < bestDist) {
                bestDist = d2;
                best = pi;
                bestS = (XAbstractSeries*)&ls->m_base.m_base;
                bestX = ls->m_base.m_points[pi].x;
                bestY = ls->m_base.m_points[pi].y;
            }
        }
    }
    for (si = 0; si < chart->m_scatterCount; ++si) {
        XScatterSeries* ss = chart->m_scatterSeries[si];
        if (!XAbstractSeries_isVisible((XAbstractSeries*)&ss->m_base.m_base))
            continue;
        for (pi = 0; pi < ss->m_base.m_count; ++pi) {
            int sx; int sy; int dx; int dy; int d2;
            xcv_mapPoint(self, &plotR, ss->m_base.m_points[pi].x,
                         ss->m_base.m_points[pi].y, &sx, &sy);
            dx = sx - px; dy = sy - py;
            d2 = dx * dx + dy * dy;
            if (d2 < bestDist) {
                bestDist = d2;
                best = pi;
                bestS = (XAbstractSeries*)&ss->m_base.m_base;
                bestX = ss->m_base.m_points[pi].x;
                bestY = ss->m_base.m_points[pi].y;
            }
        }
    }
    for (si = 0; si < chart->m_splineCount; ++si) {
        XSplineSeries* ss = chart->m_splineSeries[si];
        if (!XAbstractSeries_isVisible((XAbstractSeries*)&ss->m_base.m_base))
            continue;
        for (pi = 0; pi < ss->m_base.m_count; ++pi) {
            int sx; int sy; int dx; int dy; int d2;
            xcv_mapPoint(self, &plotR, ss->m_base.m_points[pi].x,
                         ss->m_base.m_points[pi].y, &sx, &sy);
            dx = sx - px; dy = sy - py;
            d2 = dx * dx + dy * dy;
            if (d2 < bestDist) {
                bestDist = d2;
                best = pi;
                bestS = (XAbstractSeries*)&ss->m_base.m_base;
                bestX = ss->m_base.m_points[pi].x;
                bestY = ss->m_base.m_points[pi].y;
            }
        }
    }
    if (bestS) {
        *outIndex = best;
        *outX = bestX;
        *outY = bestY;
    }
    return bestS;
}

/** @brief 发射 XY 序列双 double 信号（点击/按压/释放）。 */
static void xcv_emitXySignal(XAbstractSeries* series, size_t signal,
                             double x, double y)
{
    double vx = x;
    double vy = y;
    XVarList* args = XVarList_Create(XVar(double, vx), XVar(double, vy));
    if (!args) return;
    if (series && ((XObject*)series)->m_signalSlot)
        XObject_emitSignal((XObject*)series, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射 XY 序列 hovered 信号（x, y, state）。 */
static void xcv_emitHoverSignal(XAbstractSeries* series, size_t signal,
                                double x, double y, bool state)
{
    double vx = x;
    double vy = y;
    int vs = state ? 1 : 0;
    XVarList* args = XVarList_Create(XVar(double, vx), XVar(double, vy),
                                     XVar(int, vs));
    if (!args) return;
    if (series && ((XObject*)series)->m_signalSlot)
        XObject_emitSignal((XObject*)series, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 按下：橡皮筋模式下记录起点并抓取鼠标。 */
static void VX_chartView_mousePressEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XMouseEvent* me;
    if (!cv || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    if (cv->m_rubberBand == XChartView_RubberBand_NoRubberBand) {
        int idx;
        double hx;
        double hy;
        XAbstractSeries* hit = xcv_hitTest(cv,
            XMouseEvent_position(me).x, XMouseEvent_position(me).y,
            &idx, &hx, &hy);
        if (hit)
            xcv_emitXySignal(hit,
                (size_t)XXYSeries_pressed_signal(hit, hx, hy),
                hx, hy);
        XEvent_accept(event);
        return;
    }
    cv->m_dragging = true;
    cv->m_dragStart = XMouseEvent_position(me);
    XRect_init(&cv->m_dragRect, cv->m_dragStart.x, cv->m_dragStart.y, 0, 0);
    XWidget_grabMouse(self);
    XEvent_accept(event);
}

/** @brief 移动：更新橡皮筋矩形并重绘。 */
static void VX_chartView_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!cv || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    me = (XMouseEvent*)event;
    if (!cv->m_dragging) {
        /* 悬停命中：进入/离开发射 hovered。 */
        int idx;
        double hx;
        double hy;
        XAbstractSeries* hit = xcv_hitTest(cv,
            XMouseEvent_position(me).x, XMouseEvent_position(me).y,
            &idx, &hx, &hy);
        if (hit && (hit != cv->m_hoverSeries || idx != cv->m_hoverIndex ||
                    !cv->m_hovering)) {
            if (cv->m_hovering && cv->m_hoverSeries &&
                cv->m_hoverSeries != hit)
                xcv_emitHoverSignal(cv->m_hoverSeries, (size_t)XXYSeries_hovered_signal(cv->m_hoverSeries, hx, hy, false), hx, hy, false);
            cv->m_hoverSeries = hit;
            cv->m_hoverIndex = idx;
            cv->m_hovering = true;
            xcv_emitHoverSignal(hit, (size_t)XXYSeries_hovered_signal(hit, hx, hy, true), hx, hy, true);
        } else if (!hit && cv->m_hovering) {
            xcv_emitHoverSignal(cv->m_hoverSeries, (size_t)XXYSeries_hovered_signal(cv->m_hoverSeries, 0.0, 0.0, false), 0.0, 0.0, false);
            cv->m_hoverSeries = NULL;
            cv->m_hoverIndex = -1;
            cv->m_hovering = false;
        }
        XEvent_accept(event);
        return;
    }
    pos = XMouseEvent_position(me);
    cv->m_dragRect.x = pos.x < cv->m_dragStart.x ? pos.x : cv->m_dragStart.x;
    cv->m_dragRect.y = pos.y < cv->m_dragStart.y ? pos.y : cv->m_dragStart.y;
    cv->m_dragRect.width = pos.x > cv->m_dragStart.x
        ? pos.x - cv->m_dragStart.x : cv->m_dragStart.x - pos.x;
    cv->m_dragRect.height = pos.y > cv->m_dragStart.y
        ? pos.y - cv->m_dragStart.y : cv->m_dragStart.y - pos.y;
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 抬起：拖出阈值后把橡皮筋矩形映射为数据域并放大。 */
static void VX_chartView_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XMouseEvent* me;
    XValueAxis* ax;
    XValueAxis* ay;
    XRect plotR;
    double minX;
    double maxX;
    double minY;
    double maxY;
    double rx;
    double ry;
    if (!cv || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE)
        return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    if (!cv->m_dragging) {
        int idx;
        double hx;
        double hy;
        XAbstractSeries* hit = xcv_hitTest(cv,
            XMouseEvent_position(me).x, XMouseEvent_position(me).y,
            &idx, &hx, &hy);
        if (hit) {
            xcv_emitXySignal(hit,
                (size_t)XXYSeries_clicked_signal(hit, hx, hy), hx, hy);
            xcv_emitXySignal(hit,
                (size_t)XXYSeries_released_signal(hit, hx, hy), hx, hy);
        }
        XEvent_accept(event);
        return;
    }
    cv->m_dragging = false;
    XWidget_releaseMouse(self);
    if (cv->m_dragRect.width < 8 || cv->m_dragRect.height < 8) {
        XWidget_update(self);
        return;
    }
    ax = cv->m_chart ? cv->m_chart->m_axisX : NULL;
    ay = cv->m_chart ? cv->m_chart->m_axisY : NULL;
    if (!ax || !ay) return;
    xcv_layout(cv, NULL, &plotR, NULL);
    minX = ax->m_min;
    maxX = ax->m_max;
    minY = ay->m_min;
    maxY = ay->m_max;
    rx = maxX - minX;
    ry = maxY - minY;
    if (rx <= 0.0 || ry <= 0.0 || plotR.width <= 0 || plotR.height <= 0)
        return;
    /* 矩形端点 → 数据域（XChart::mapToValue 的布局内联实现）。 */
    {
        double v0x = minX + (double)(cv->m_dragRect.x - plotR.x) /
                     (double)plotR.width * rx;
        double v1x = minX + (double)(cv->m_dragRect.x + cv->m_dragRect.width -
                                     plotR.x) / (double)plotR.width * rx;
        double v0y = maxY - (double)(cv->m_dragRect.y - plotR.y) /
                     (double)plotR.height * ry;
        double v1y = maxY - (double)(cv->m_dragRect.y + cv->m_dragRect.height -
                                     plotR.y) / (double)plotR.height * ry;
        /* 压栈框选前的当前域（保持 zoomReset/zoomOut 语义一致）。 */
        xcv_pushCurrentDomain(cv);
        XValueAxis_setRange(ax, v0x, v1x);
        XValueAxis_setRange(ay, v0y, v1y);
    }
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 滚轮：垂直滚动映射为 Y 域滚动（120 角度 = 5% 域宽）。 */
static void VX_chartView_wheelEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XWheelEvent* we;
    if (!cv || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
    we = (XWheelEvent*)event;
    {
        XPoint delta = XWheelEvent_angleDelta(we);
        int dy = (delta.y != 0) ? delta.y : delta.x;
        XChart_scroll(cv->m_chart, 0.0, (double)dy / (120.0 * 20.0));
    }
    XWidget_update(self);
    XEvent_accept(event);
}

void XChartView_init(XChartView* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XChartView);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_chart = XChart_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    self->m_rubberBand = XChartView_RubberBand_NoRubberBand;
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

void XChartView_setChart(XChartView* self, XChart* chart)
{
    if (!self || chart == self->m_chart) return;
    if (self->m_chart) XChart_delete_base(self->m_chart);
    self->m_chart = chart;
    XWidget_update((XWidget*)self);
}

void XChartView_setRubberBand(XChartView* self, XChartView_RubberBands rubberBands)
{
    if (!self) return;
    self->m_rubberBand = rubberBands;
    self->m_dragging = false;
}

XChartView_RubberBands XChartView_rubberBand(const XChartView* self)
{
    return self ? self->m_rubberBand
                : XChartView_RubberBand_NoRubberBand;
}

void XChartView_updateChart(XChartView* self)
{ if (self) XWidget_update((XWidget*)self); }

#endif /* XCHARTS_ON */