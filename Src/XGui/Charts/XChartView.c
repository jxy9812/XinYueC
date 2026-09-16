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

/** @brief 布局（前向声明，定义见下）。 */
static void xcv_layout(const XChartView* self, XRect* titleR,
                       XRect* plotR, XRect* legendR);

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

/** @brief 图表背景纯色（m_backgroundBrush 显式色，否则主题背景起点）。 */
static uint32_t xcv_backgroundColor(const XChartView* self)
{
    XChart* chart = self->m_chart;
    if (!chart) return 0xFFFFFFFFu;
    if (chart->m_backgroundBrush != 0)
        return chart->m_backgroundBrush;
    return chart->m_themeBgStart;
}

/** @brief 把主题背景渐变（或纯色）应用到画刷并填充矩形。 */
static void xcv_fillChartBackground(XChartView* self, XPainter* painter,
                                    const XRect* rect)
{
    XChart* chart = self->m_chart;
    if (!chart) return;
    if (chart->m_backgroundBrush != 0) {
        XPainter_fillRect(painter, rect, chart->m_backgroundBrush);
        return;
    }
#if XPAINTER_BRUSH_ON
    if (chart->m_themeBgStart != chart->m_themeBgEnd) {
        XPainterGradient gradient;
        XPainterGradient_initLinear(&gradient, 0.0f, (float)rect->y,
                                    0.0f, (float)(rect->y + rect->height));
        XPainterGradient_addStop(&gradient, 0.0f, chart->m_themeBgStart);
        XPainterGradient_addStop(&gradient, 1.0f, chart->m_themeBgEnd);
        XPainter_setBrushGradient(painter, &gradient);
        XPainter_fillRect_2(painter, rect);
        return;
    }
#endif /* XPAINTER_BRUSH_ON */
    XPainter_fillRect(painter, rect, chart->m_themeBgStart);
}

/** @brief 数字格式化（对标 presenter numberToString 默认 'g' 精度 6）。 */
static void xcv_formatNumber(char* buf, size_t size, double value)
{
    XSnprintf(buf, size, "%g", value);
}

/** @brief 应用标签字体（家族+磅字号；对标点标签/柱标签/饼标签字体）。 */
static void xcv_applyLabelFont(XPainter* painter, const XWidget* widget,
                               const char* family, int sizePts)
{
    XFont font = XWidget_font(widget);
    if (family && family[0] != '\0')
        XFont_setFamily(&font, family);
    if (sizePts > 0)
        XFont_setPointSize(&font, sizePts);
    XPainter_setFont(painter, &font);
    XFont_deinit_base(&font);
}

/**
 * @brief 点标签格式化：替换 @xPoint/@yPoint（对标 QXYSeries 点标签）。
 *
 * @param buf  输出缓冲。
 * @param size 缓冲容量。
 * @param fmt  格式串；NULL 用默认 "@xPoint, @yPoint"。
 * @param x    X 坐标。
 * @param y    Y 坐标。
 * @return 无返回值。
 */
static void xcv_formatPointLabel(char* buf, size_t size, const char* fmt,
                                 double x, double y)
{
    char xs[64];
    char ys[64];
    const char* src;
    char* dst;
    char* end;
    if (!buf || size == 0) return;
    if (!fmt || fmt[0] == '\0') fmt = "@xPoint, @yPoint";
    xcv_formatNumber(xs, sizeof(xs), x);
    xcv_formatNumber(ys, sizeof(ys), y);
    src = fmt;
    dst = buf;
    end = buf + size - 1;
    while (*src && dst < end) {
        if (src[0] == '@' && XStrncmp(src, "@xPoint", 7) == 0) {
            size_t n = XStrlen(xs);
            size_t i;
            for (i = 0; i < n && dst < end; ++i) *dst++ = xs[i];
            src += 7;
        } else if (src[0] == '@' && XStrncmp(src, "@yPoint", 7) == 0) {
            size_t n = XStrlen(ys);
            size_t i;
            for (i = 0; i < n && dst < end; ++i) *dst++ = ys[i];
            src += 7;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/**
 * @brief 柱标签格式化：替换 @value（对标 AbstractBarChartItem::generateLabelText）。
 *
 * @param buf       输出缓冲。
 * @param size      缓冲容量。
 * @param fmt       格式串；NULL/空用值本身。
 * @param value     柱值。
 * @param precision 有效位数。
 * @return 无返回值。
 */
static void xcv_formatBarLabel(char* buf, size_t size, const char* fmt,
                               double value, int precision)
{
    char vs[64];
    const char* src;
    char* dst;
    char* end;
    if (!buf || size == 0) return;
    if (precision <= 0) precision = 6;
    XSnprintf(vs, sizeof(vs), "%.*g", precision, value);
    if (!fmt || fmt[0] == '\0') {
        XSnprintf(buf, size, "%s", vs);
        return;
    }
    src = fmt;
    dst = buf;
    end = buf + size - 1;
    while (*src && dst < end) {
        if (src[0] == '@' && XStrncmp(src, "@value", 6) == 0) {
            size_t n = XStrlen(vs);
            size_t i;
            for (i = 0; i < n && dst < end; ++i) *dst++ = vs[i];
            src += 6;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/** @brief 命中测试是否在绘图区内（对标 QChartView plotArea.contains）。 */
static bool xcv_inPlotArea(const XChartView* self, int px, int py)
{
    XRect plotR;
    if (!self) return false;
    xcv_layout(self, NULL, &plotR, NULL);
    return px >= plotR.x && px < plotR.x + plotR.width &&
           py >= plotR.y && py < plotR.y + plotR.height;
}

/** @brief 主题色循环取色（下标回环）。 */
static uint32_t xcv_seriesColor(const XChartView* self, int index)
{
    return XChart_themeColor(self->m_chart, index);
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

/** @brief 绘制标题（居中；颜色=标题画刷或主题标签色）。 */
static void xcv_paintTitle(XChartView* self, XPainter* painter,
                           const XRect* titleR)
{
    uint32_t text;
    XChart* chart = self->m_chart;
    XFont font = XWidget_font((XWidget*)self);
    if (!chart) return;
    text = chart->m_titleBrush != 0
        ? chart->m_titleBrush : xcv_color(self, XPaletteColorRole_WindowText);
    if (chart->m_titleFamily && XString_toUtf8(chart->m_titleFamily) &&
        XStrlen(XString_toUtf8(chart->m_titleFamily)) > 0)
        XFont_setFamily(&font, XString_toUtf8(chart->m_titleFamily));
    if (chart->m_titlePixelSize > 0)
        XFont_setPixelSize(&font, chart->m_titlePixelSize);
    XPainter_setFont(painter, &font);
    {
        const char* title = XChart_title_2(chart);
        XPainter_drawText(painter, titleR->x + titleR->width / 2 -
                          (int)XStrlen(title) * 4,
                          titleR->y + titleR->height - 8, title, text);
    XFont_deinit_base(&font);
    }
}

/** @brief 绘制数值轴网格 + 刻度标签（颜色取自主题规格，轴级颜色可覆盖）。 */
static void xcv_paintAxes(XChartView* self, XPainter* painter,
                          const XRect* plotR)
{
    XValueAxis* ax = self->m_chart->m_axisX;
    XValueAxis* ay = self->m_chart->m_axisY;
    XChart* chart = self->m_chart;
    uint32_t axisPenX;
    uint32_t axisPenY;
    uint32_t gridPen;
    uint32_t minorPen;
    uint32_t textX;
    uint32_t textY;
    XFont font = XWidget_font((XWidget*)self);
    char buf[32];
    int i;
    int ticks;
    if (!ax || !ay) return;
    axisPenX = ax->m_base.m_linePenColor != 0
        ? ax->m_base.m_linePenColor : chart->m_themeAxisLinePen;
    axisPenY = ay->m_base.m_linePenColor != 0
        ? ay->m_base.m_linePenColor : chart->m_themeAxisLinePen;
    gridPen = chart->m_themeGridPen;
    minorPen = chart->m_themeMinorGridPen;
    textX = ax->m_base.m_labelsBrushColor != 0
        ? ax->m_base.m_labelsBrushColor : chart->m_themeLabelBrush;
    textY = ay->m_base.m_labelsBrushColor != 0
        ? ay->m_base.m_labelsBrushColor : chart->m_themeLabelBrush;
    XPainter_setFont(painter, &font);
    /* 主题阴影带（对标 ChartTheme::backgroundShades；HighContrast 主题生效）。 */
    if (ay->m_base.m_shadesVisible && chart->m_themeShadesBrush != 0) {
        int shadeTicks = ay->m_tickCount > 1 ? ay->m_tickCount : 2;
        for (i = 1; i < shadeTicks; i += 2) {
            int y0 = plotR->y + (int)((double)(i - 1) /
                     (double)(shadeTicks - 1) * plotR->height);
            int y1 = plotR->y + (int)((double)i /
                     (double)(shadeTicks - 1) * plotR->height);
            if (y1 - y0 < 1) y1 = y0 + 1;
            XPainter_fillRect(painter,
                &(XRect){plotR->x, y0, plotR->width, y1 - y0},
                chart->m_themeShadesBrush);
        }
    }
    /* 轴线（对标 theme axisLinePen）。 */
    XPainter_setPen(painter, axisPenY);
    XPainter_setPenWidth(painter, chart->m_themeAxisLineWidth);
    XPainter_drawLine(painter, plotR->x, plotR->y,
                      plotR->x, plotR->y + plotR->height);
    XPainter_setPen(painter, axisPenX);
    XPainter_drawLine(painter, plotR->x, plotR->y + plotR->height,
                      plotR->x + plotR->width,
                      plotR->y + plotR->height);
    ticks = ay->m_tickCount > 1 ? ay->m_tickCount : 2;
    for (i = 0; i < ticks; ++i) {
        double t = (double)i / (ticks - 1);
        double v = ay->m_base.m_max - t * (ay->m_base.m_max - ay->m_base.m_min);
        int y = plotR->y + (int)(t * plotR->height);
        if (ay->m_base.m_gridLineVisible) {
            if (i > 0) {
                XPainter_setPen(painter, gridPen);
                XPainter_setPenWidth(painter, chart->m_themeGridLineWidth);
                XPainter_drawLine(painter, plotR->x, y,
                                  plotR->x + plotR->width, y);
            }
            /* 次网格线（对标 minorGridLinePen，虚线）。 */
#if XPAINTER_PENSTYLE_ON
            if (i > 0) {
                double tm = t - 0.5 / (ticks - 1);
                int ym = plotR->y + (int)(tm * plotR->height);
                XPainter_setPen(painter, minorPen);
                XPainter_setPenWidth(painter,
                                     chart->m_themeMinorGridLineWidth);
                XPainter_setPenStyle(painter, XPainterPenStyle_DashLine);
                XPainter_drawLine(painter, plotR->x, ym,
                                  plotR->x + plotR->width, ym);
                XPainter_setPenStyle(painter, XPainterPenStyle_SolidLine);
            }
#endif /* XPAINTER_PENSTYLE_ON */
        }
        XSnprintf(buf, sizeof(buf), XString_toUtf8(ay->m_labelFormat), v);
        XPainter_drawText(painter, plotR->x - 40, y + 6, buf, textY);
    }
    ticks = ax->m_tickCount > 1 ? ax->m_tickCount : 2;
    for (i = 0; i < ticks; ++i) {
        double t = (double)i / (ticks - 1);
        double v = ax->m_base.m_min + t * (ax->m_base.m_max - ax->m_base.m_min);
        int x = plotR->x + (int)(t * plotR->width);
        if (ax->m_base.m_gridLineVisible) {
            if (i > 0) {
                XPainter_setPen(painter, gridPen);
                XPainter_setPenWidth(painter, chart->m_themeGridLineWidth);
                XPainter_drawLine(painter, x, plotR->y, x,
                                  plotR->y + plotR->height);
            }
#if XPAINTER_PENSTYLE_ON
            if (i > 0) {
                double tm = t - 0.5 / (ticks - 1);
                int xm = plotR->x + (int)(tm * plotR->width);
                XPainter_setPen(painter, minorPen);
                XPainter_setPenWidth(painter,
                                     chart->m_themeMinorGridLineWidth);
                XPainter_setPenStyle(painter, XPainterPenStyle_DashLine);
                XPainter_drawLine(painter, xm, plotR->y, xm,
                                  plotR->y + plotR->height);
                XPainter_setPenStyle(painter, XPainterPenStyle_SolidLine);
            }
#endif /* XPAINTER_PENSTYLE_ON */
        }
        XSnprintf(buf, sizeof(buf), XString_toUtf8(ax->m_labelFormat), v);
        XPainter_drawText(painter, x - 12,
                          plotR->y + plotR->height + 16, buf, textX);
    }
    XFont_deinit_base(&font);
}

/** @brief 数据点 → 屏幕坐标（按轴范围缩放；前向声明，定义见后）。 */
static void xcv_mapPoint(const XChartView* self, const XRect* plotR,
                         double x, double y, int* sx, int* sy);

/** @brief 绘制单个点标记（圆/方；选中色；对标 Qt 的 ChartMarker）。 */
static void xcv_drawMarker(XPainter* painter, int sx, int sy, double size,
                           uint32_t fill, uint32_t border, bool circle)
{
    int r = (int)(size / 2);
    XRect rect;
    if (r < 1) r = 1;
    XRect_init(&rect, sx - r, sy - r, r * 2, r * 2);
    XPainter_setBrush(painter, fill);
    XPainter_setPen(painter, border);
    XPainter_setPenWidth(painter, 1);
#if XPAINTER_SHAPE_ON
    if (circle) {
        XPainter_drawEllipse(painter, &rect);
        return;
    }
#endif /* XPAINTER_SHAPE_ON */
    XPainter_fillRect(painter, &rect, fill);
    XPainter_setBrush(painter, border);
    XPainter_drawRect(painter, &rect);
}

/** @brief 绘制单个点标签（居中于点上方；对标 Qt 点标签落位）。 */
static void xcv_drawPointLabel(XPainter* painter, int sx, int sy,
                               const char* fmt, double x, double y,
                               uint32_t color)
{
    char buf[160];
    int w;
    xcv_formatPointLabel(buf, sizeof(buf), fmt, x, y);
    if (buf[0] == '\0') return;
    w = (int)XStrlen(buf) * 4;
    XPainter_drawText(painter, sx - w / 2, sy - 3, buf, color);
}

/** @brief 绘制最佳拟合线（对标 QXYSeriesPrivate::drawBestFitLine）。 */
static void xcv_drawBestFitLine(XChartView* self, XPainter* painter,
                                const XRect* plotR, XXYSeries* xy)
{
    double slope;
    double intercept;
    double minX;
    double maxX;
    int x0;
    int y0;
    int x1;
    int y1;
    uint32_t color;
    if (!self->m_chart || !self->m_chart->m_axisX) return;
    if (!XXYSeries_bestFitLineEquation(xy, &slope, &intercept)) return;
    minX = self->m_chart->m_axisX->m_base.m_min;
    maxX = self->m_chart->m_axisX->m_base.m_max;
    xcv_mapPoint(self, plotR, minX, slope * minX + intercept, &x0, &y0);
    xcv_mapPoint(self, plotR, maxX, slope * maxX + intercept, &x1, &y1);
    color = xy->m_bestFitColor != 0 ? xy->m_bestFitColor : xy->m_color;
    XPainter_setPen(painter, color);
    XPainter_setPenWidth(painter,
        (int)(xy->m_bestFitWidth > 0 ? xy->m_bestFitWidth : 2));
    XPainter_drawLine(painter, x0, y0, x1, y1);
}

/** @brief 取 XY 点级颜色（配置优先；选中用选中色；前向声明）。 */
static uint32_t xcv_pointColor(const XXYSeries* xy, int index,
                               uint32_t base, bool selected);
/** @brief 取 XY 点级尺寸（配置优先；前向声明）。 */
static double xcv_pointSize(const XXYSeries* xy, int index, double base);

/** @brief XY 序列公共：点标记 + 点标签（线/样条/散点复用）。 */
static void xcv_drawXyPoints(XChartView* self, XPainter* painter,
                             const XRect* plotR, XXYSeries* xy,
                             int seriesIndex, bool scatter, int markerShape)
{
    uint32_t color;
    uint32_t labelColor;
    const char* fmt;
    int pi;
    if (!self->m_chart) return;
    if (!xy->m_pointsVisible && !xy->m_pointLabelsVisible) return;
    color = xy->m_color != 0 ? xy->m_color : xcv_seriesColor(self, seriesIndex);
    labelColor = xy->m_pointLabelsColor != 0
        ? xy->m_pointLabelsColor : self->m_chart->m_themeLabelBrush;
    fmt = XXYSeries_pointLabelsFormat_2(xy);
    if (xy->m_pointLabelsVisible)
        xcv_applyLabelFont(painter, (const XWidget*)self,
            XXYSeries_pointLabelsFontFamily_2(xy),
            XXYSeries_pointLabelsFontSize(xy));
    for (pi = 0; pi < xy->m_count; ++pi) {
        int sx;
        int sy;
        double ms;
        uint32_t pointColor;
        bool selected;
        bool circle;
        xcv_mapPoint(self, plotR, xy->m_points[pi].x,
                     xy->m_points[pi].y, &sx, &sy);
        selected = XXYSeries_isPointSelected(xy, pi);
        pointColor = xcv_pointColor(xy, pi, color, selected);
        ms = xcv_pointSize(xy, pi, xy->m_markerSize);
        if (ms <= 0) ms = xy->m_markerSize > 0 ? xy->m_markerSize : 8.0;
        circle = !scatter || markerShape == XScatterSeriesMarkerShape_Circle;
        /* 散点序列的标记由 xcv_paintScatter 绘制，这里只负责标签。 */
        if (xy->m_pointsVisible && !scatter) {
            xcv_drawMarker(painter, sx, sy, ms, pointColor, pointColor,
                           circle);
        }
        if (xy->m_pointLabelsVisible) {
#if XPAINTER_CLIP_ON
            if (!xy->m_pointLabelsClipping)
                XPainter_setClipRect(painter, plotR,
                                     XPainterClipOperation_NoClip);
#endif /* XPAINTER_CLIP_ON */
            xcv_drawPointLabel(painter, sx, sy, fmt,
                               xy->m_points[pi].x, xy->m_points[pi].y,
                               labelColor);
#if XPAINTER_CLIP_ON
            if (!xy->m_pointLabelsClipping)
                XPainter_setClipRect(painter, plotR,
                                     XPainterClipOperation_ReplaceClip);
#endif /* XPAINTER_CLIP_ON */
        }
    }
}

/** @brief 取 XY 点级颜色（配置优先；选中用选中色）。 */
static uint32_t xcv_pointColor(const XXYSeries* xy, int index,
                               uint32_t base, bool selected)
{
    uint32_t cfg = XXYSeries_pointColor(xy, index);
    if (selected) {
        uint32_t sc = xy->m_selectedColor != 0 ? xy->m_selectedColor : base;
        return cfg != 0 ? cfg : sc;
    }
    return cfg != 0 ? cfg : base;
}

/** @brief 取 XY 点级尺寸（配置优先）。 */
static double xcv_pointSize(const XXYSeries* xy, int index, double base)
{
    double cfg = XXYSeries_pointSize(xy, index);
    return cfg > 0 ? cfg : base;
}

/** @brief 绘制折线序列（按轴范围缩放 + 点标记/点标签/最佳拟合线）。 */
static void xcv_paintLines(XChartView* self, XPainter* painter,
                           const XRect* plotR)
{
    int si;
    for (si = 0; si < self->m_chart->m_lineCount; ++si) {
        XLineSeries* s = self->m_chart->m_lineSeries[si];
        XXYSeries* xy = &s->m_base;
        uint32_t color = xy->m_color != 0
            ? xy->m_color : xcv_seriesColor(self, si);
        int pi;
        if (!XAbstractSeries_isVisible(&xy->m_base) || xy->m_count < 2) continue;
        XPainter_setPen(painter, color);
        XPainter_setPenWidth(painter,
            (int)(xy->m_width > 0 ? xy->m_width : 2));
        for (pi = 0; pi < xy->m_count - 1; ++pi) {
            const XPointF* p0 = &xy->m_points[pi];
            const XPointF* p1 = &xy->m_points[pi + 1];
            int x0; int y0; int x1; int y1;
            XValueAxis* ax = self->m_chart->m_axisX;
            XValueAxis* ay = self->m_chart->m_axisY;
            double rx = ax->m_base.m_max - ax->m_base.m_min;
            double ry = ay->m_base.m_max - ay->m_base.m_min;
            if (rx <= 0 || ry <= 0) continue;
            x0 = plotR->x + (int)((p0->x - ax->m_base.m_min) / rx * plotR->width);
            x1 = plotR->x + (int)((p1->x - ax->m_base.m_min) / rx * plotR->width);
            y0 = plotR->y + (int)((ay->m_base.m_max - p0->y) / ry * plotR->height);
            y1 = plotR->y + (int)((ay->m_base.m_max - p1->y) / ry * plotR->height);
            XPainter_drawLine(painter, x0, y0, x1, y1);
        }
        if (xy->m_bestFitVisible)
            xcv_drawBestFitLine(self, painter, plotR, xy);
        xcv_drawXyPoints(self, painter, plotR, xy, si, false, 0);
    }
}

/** @brief 绘制饼图（占比扇形 + 标签落位 + holeSize 挖洞成环图）。 */
static void xcv_paintPie(XChartView* self, XPainter* painter,
                         const XRect* plotR)
{
#if XPAINTER_SHAPE_ON
    XPieSeries* pie = self->m_chart->m_pieSeries;
    double sum;
    double angle;
    int cx;
    int cy;
    double base;
    double outer;
    double hole;
    int i;
    int gi;
    if (!pie || pie->m_count <= 0 || !XAbstractSeries_isVisible(&pie->m_base)) return;
    sum = XPieSeries_sum(pie);
    if (sum <= 0) return;
    gi = 0;
    base = (plotR->width < plotR->height ? plotR->width
           : plotR->height) / 2.0;
    outer = base * XPieSeries_pieSize(pie);
    hole = outer * XPieSeries_holeSize(pie);
    cx = plotR->x + (int)(XPieSeries_horizontalPosition(pie) * plotR->width);
    cy = plotR->y + (int)(XPieSeries_verticalPosition(pie) * plotR->height);
    angle = XPieSeries_pieStartAngle(pie);
    for (i = 0; i < pie->m_count; ++i) {
        XPieSlice* slice = pie->m_slices[i];
        double value;
        double frac;
        double sweep;
        uint32_t color;
        uint32_t penColor;
        double penWidth;
        int penWidthPx;
        if (!slice) continue;
        value = XPieSlice_value(slice);
        frac = value / sum;
        sweep = frac * (XPieSeries_pieEndAngle(pie) - XPieSeries_pieStartAngle(pie));
        color = XPieSlice_brush(slice) != 0
            ? XPieSlice_brush(slice)
            : XChart_themeGradientColor(self->m_chart, gi,
                (double)(i + 1) / (double)pie->m_count);
        penWidth = 0;
        XPieSlice_pen(slice, &penColor, &penWidth);
        if (penColor == 0)
            penColor = color;
        penWidthPx = (int)(penWidth > 0 ? penWidth : 1);
        if (penWidthPx < 1) penWidthPx = 1;
        {
            /* 分离突出：沿扇区中线把圆心外移 distance（对标 exploded）。 */
            int scx = cx;
            int scy = cy;
            double a0;
            double a1;
            if (XPieSlice_isExploded(slice)) {
                double am = (angle - sweep / 2) * 3.14159265358979323846 / 180.0;
                double dist = outer * XPieSlice_explodeDistanceFactor(slice);
                scx = cx + (int)(dist * cos(am));
                scy = cy - (int)(dist * sin(am));
            }
            a0 = angle * 3.14159265358979323846 / 180.0;
            a1 = (angle - sweep) * 3.14159265358979323846 / 180.0;
            XPainter_setBrush(painter, color);
            XPainter_setPen(painter, penColor);
            XPainter_setPenWidth(painter, penWidthPx);
            XPainter_drawPie(painter,
                &(XRect){scx - (int)outer, scy - (int)outer,
                         (int)(outer * 2), (int)(outer * 2)},
                (int)(angle * 16), (int)(-sweep * 16));
            if (XPieSlice_isLabelVisible(slice)) {
                double am = (angle - sweep / 2) * 3.14159265358979323846 / 180.0;
                double pos;
                uint32_t lc = XPieSlice_labelColor(slice) != 0
                    ? XPieSlice_labelColor(slice) : 0xFFFFFFFFu;
                switch (XPieSlice_labelPosition(slice)) {
                case XPieSlice_LabelPosition_InsideHorizontal:
                case XPieSlice_LabelPosition_InsideTangential:
                case XPieSlice_LabelPosition_InsideNormal:
                    pos = (outer + hole) * 0.5;
                    break;
                case XPieSlice_LabelPosition_Outside:
                default:
                    pos = outer + outer * XPieSlice_labelArmLengthFactor(slice);
                    break;
                }
                if (pos < hole) pos = hole;
                xcv_applyLabelFont(painter, (const XWidget*)self,
                    XPieSlice_labelFont_2(slice), slice->m_labelFontSize);
                XPainter_drawText(painter,
                    scx + (int)(pos * cos(am)) - 12,
                    scy - (int)(pos * sin(am)),
                    XPieSlice_label_2(slice), lc);
            }
            (void)a0; (void)a1;
        }
        angle -= sweep;
    }
    /* holeSize>0：用背景色填充中心洞（对标环图；渐变背景逐像素一致）。 */
    if (hole > 2.0) {
        XRect holeRect;
        XRect_init(&holeRect, cx - (int)hole, cy - (int)hole,
                   (int)(hole * 2), (int)(hole * 2));
#if XPAINTER_BRUSH_ON
        if (self->m_chart->m_backgroundBrush == 0 &&
            self->m_chart->m_themeBgStart != self->m_chart->m_themeBgEnd) {
            XPainterGradient gradient;
            XPainterGradient_initLinear(&gradient, 0.0f, (float)plotR->y,
                                        0.0f,
                                        (float)(plotR->y + plotR->height));
            XPainterGradient_addStop(&gradient, 0.0f,
                                     self->m_chart->m_themeBgStart);
            XPainterGradient_addStop(&gradient, 1.0f,
                                     self->m_chart->m_themeBgEnd);
            XPainter_setBrushGradient(painter, &gradient);
            XPainter_setPen_2(painter, XPainterPenStyle_NoPen);
            XPainter_drawEllipse(painter, &holeRect);
        } else
#endif /* XPAINTER_BRUSH_ON */
        {
            XPainter_setBrush(painter, xcv_backgroundColor(self));
            XPainter_setPen_2(painter, XPainterPenStyle_NoPen);
            XPainter_drawEllipse(painter, &holeRect);
        }
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
    double rx = ax->m_base.m_max - ax->m_base.m_min;
    double ry = ay->m_base.m_max - ay->m_base.m_min;
    if (rx <= 0) rx = 1;
    if (ry <= 0) ry = 1;
    *sx = plotR->x + (int)((x - ax->m_base.m_min) / rx * plotR->width);
    *sy = plotR->y + (int)((ay->m_base.m_max - y) / ry * plotR->height);
}

/** @brief 绘制柱状序列（按 XBarSet 集合分组成组；柱色=柱组画刷，0=主题默认）。 */
static void xcv_paintBars(XChartView* self, XPainter* painter,
                          const XRect* plotR)
{
    int si;
    for (si = 0; si < self->m_chart->m_barCount; ++si) {
        XBarSeries* s = self->m_chart->m_barSeries[si];
        XAbstractBarSeries* abs = &s->m_base;
        int setCount;
        int catCount;
        int i;
        int j;
        if (!XAbstractSeries_isVisible(&abs->m_base) || abs->m_barSetCount == 0)
            continue;
        setCount = abs->m_barSetCount;
        catCount = 0;
        for (j = 0; j < setCount; ++j) {
            int n = XBarSet_count(abs->m_barSets[j]);
            if (n > catCount) catCount = n;
        }
        if (catCount <= 0) continue;
        for (i = 0; i < catCount; ++i) {
            double slot = (double)plotR->width / (double)catCount;
            double group = slot * abs->m_barWidth;
            double barW = setCount > 0 ? group / (double)setCount : group;
            double gx = plotR->x + (double)i * slot + (slot - group) / 2.0;
            for (j = 0; j < setCount; ++j) {
                XBarSet* set = abs->m_barSets[j];
                double v;
                double bx;
                uint32_t color;
                uint32_t penColor;
                double penWidth;
                int sx0; int sy0; int sy1;
                int top;
                int hgt;
                XRect barRect;
                if (!set) continue;
                v = XBarSet_at(set, i);
                color = XBarSet_brush(set) != 0
                    ? XBarSet_brush(set)
                    : XChart_themeGradientColor(self->m_chart, si + j, 0.5);
                if (XBarSet_isBarSelected(set, i)) {
                    uint32_t sc = XBarSet_selectedColor(set);
                    if (sc != 0) color = sc;
                }
                penWidth = 0;
                XBarSet_pen(set, &penColor, &penWidth);
                if (penColor == 0) penColor = color;
                bx = gx + (double)j * barW;
                xcv_mapPoint(self, plotR, i + 0.5, v, &sx0, &sy1);
                xcv_mapPoint(self, plotR, i + 0.5, 0, &sx0, &sy0);
                top = sy1 < sy0 ? sy1 : sy0;
                hgt = sy1 > sy0 ? sy1 - sy0 : sy0 - sy1;
                if (hgt < 1) hgt = 1;
                XRect_init(&barRect, (int)bx, top, (int)barW, hgt);
                if (barRect.width < 1) barRect.width = 1;
                XPainter_setPen(painter, penColor);
                XPainter_setPenWidth(painter,
                    (int)(penWidth > 0 ? penWidth : 2));
                XPainter_fillRect(painter, &barRect, color);
                /* 柱标签（对标 generateLabelText + labelsPosition 落位）。 */
                if (abs->m_labelsVisible) {
                    char lbuf[96];
                    int lx;
                    int ly;
                    const char* fmt = XAbstractBarSeries_labelsFormat_2(abs);
                    xcv_formatBarLabel(lbuf, sizeof(lbuf), fmt, v,
                                       abs->m_labelsPrecision);
                    lx = barRect.x + barRect.width / 2 -
                         (int)XStrlen(lbuf) * 4;
                    switch (abs->m_labelsPosition) {
                    case XAbstractBarSeries_LabelsInsideEnd:
                        ly = v >= 0 ? top + 12 : top + hgt - 4;
                        break;
                    case XAbstractBarSeries_LabelsInsideBase:
                        ly = v >= 0 ? top + hgt - 4 : top + 12;
                        break;
                    case XAbstractBarSeries_LabelsOutsideEnd:
                        ly = v >= 0 ? top - 4 : top + hgt + 12;
                        break;
                    case XAbstractBarSeries_LabelsCenter:
                    default:
                        ly = top + hgt / 2;
                        break;
                    }
                    /* m_labelsAngle 旋转：XPainter 无文本旋转能力，仅落位（角度属性已存储）。 */
                    (void)abs->m_labelsAngle;
                    xcv_applyLabelFont(painter, (const XWidget*)self,
                        XBarSet_labelFont_2(set),
                        XBarSet_labelFontSize(set));
                    XPainter_drawText(painter, lx, ly, lbuf,
                        XBarSet_labelBrush(set) != 0
                            ? XBarSet_labelBrush(set)
                            : self->m_chart->m_themeLabelBrush);
                }
            }
        }
    }
}

/** @brief 绘制散点序列（形状标记 + 边框 + 选中色 + 点标签）。 */
static void xcv_paintScatter(XChartView* self, XPainter* painter,
                             const XRect* plotR)
{
    int si;
    for (si = 0; si < self->m_chart->m_scatterCount; ++si) {
        XScatterSeries* s = self->m_chart->m_scatterSeries[si];
        XXYSeries* xy = &s->m_base;
        uint32_t color = xy->m_color != 0
            ? xy->m_color : xcv_seriesColor(self, si);
        if (!XAbstractSeries_isVisible(&xy->m_base)) continue;
        {
            int pi;
            for (pi = 0; pi < xy->m_count; ++pi) {
                int sx; int sy;
                double ms;
                uint32_t fill;
                uint32_t border;
                bool selected;
                xcv_mapPoint(self, plotR, xy->m_points[pi].x,
                             xy->m_points[pi].y, &sx, &sy);
                selected = XXYSeries_isPointSelected(xy, pi);
                fill = xcv_pointColor(xy, pi, color, selected);
                border = s->m_borderColor != 0 ? s->m_borderColor : fill;
                ms = xcv_pointSize(xy, pi, xy->m_markerSize);
                if (ms <= 0) ms = 8.0;
                xcv_drawMarker(painter, sx, sy, ms, fill, border,
                    s->m_markerShape != XScatterSeriesMarkerShape_Rectangle);
            }
        }
        xcv_drawXyPoints(self, painter, plotR, xy, si, true,
                         s->m_markerShape);
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
        /* 面积上边界点标记 + 点标签（对标 QAreaSeries 点标签）。 */
        if (s->m_pointsVisible || s->m_pointLabelsVisible) {
            int pi;
            uint32_t border = s->m_borderColor != 0
                ? s->m_borderColor : color;
            for (pi = 0; pi < up->m_base.m_count; ++pi) {
                int sx; int sy;
                xcv_mapPoint(self, plotR, up->m_base.m_points[pi].x,
                             up->m_base.m_points[pi].y, &sx, &sy);
                if (s->m_pointsVisible) {
                    xcv_drawMarker(painter, sx, sy,
                        up->m_base.m_markerSize > 0
                            ? up->m_base.m_markerSize : 8.0,
                        color, border, true);
                }
                if (s->m_pointLabelsVisible) {
#if XPAINTER_CLIP_ON
                    if (!s->m_pointLabelsClipping)
                        XPainter_setClipRect(painter, plotR,
                                             XPainterClipOperation_NoClip);
#endif /* XPAINTER_CLIP_ON */
                    xcv_applyLabelFont(painter, (const XWidget*)self,
                        XAreaSeries_pointLabelsFontFamily_2(s),
                        XAreaSeries_pointLabelsFontSize(s));
                    xcv_drawPointLabel(painter, sx, sy,
                        XAreaSeries_pointLabelsFormat_2(s),
                        up->m_base.m_points[pi].x,
                        up->m_base.m_points[pi].y,
                        s->m_pointLabelsColor != 0
                            ? s->m_pointLabelsColor
                            : self->m_chart->m_themeLabelBrush);
#if XPAINTER_CLIP_ON
                    if (!s->m_pointLabelsClipping)
                        XPainter_setClipRect(painter, plotR,
                                             XPainterClipOperation_ReplaceClip);
#endif /* XPAINTER_CLIP_ON */
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
        if (s->m_base.m_bestFitVisible)
            xcv_drawBestFitLine(self, painter, plotR, &s->m_base);
        xcv_drawXyPoints(self, painter, plotR, &s->m_base, si, false, 0);
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
                          XAbstractSeries_name_2(&s->m_base.m_base), text);
        y += 20;
    XFont_deinit_base(&font);
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
                              XAbstractSeries_name_2(&b->m_base.m_base), text);
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
                              XAbstractSeries_name_2(&sc->m_base.m_base), text);
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
                              XAreaSeries_name_2(ar), text);
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
                              XAbstractSeries_name_2(&sp->m_base.m_base), text);
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
                              slice ? XPieSlice_label_2(slice) : "", text);
            y += 20;
        }
    }
}

/** @brief 渲染整张图表到图像（paintEvent 与 renderToImage 共用管线）。 */
static bool xcv_renderToImage(XChartView* cv, XImage* image)
{
    XPainter painter;
    XPoint offset;
    XRect titleR;
    XRect plotR;
    XRect legendR;
    if (!cv || !image || !cv->m_chart) return false;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return false;
    }
    offset = XWidget_paintOffset((XWidget*)cv);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    xcv_layout(cv, &titleR, &plotR, &legendR);
    /* 绘图区同步到模型（对标 QChart::plotArea；变化时发 plotAreaChanged）。 */
    XChart_setPlotArea(cv->m_chart, &(XRectF){ (float)plotR.x, (float)plotR.y,
                                               (float)plotR.width,
                                               (float)plotR.height });
    /* 背景：backgroundVisible + 主题渐变（或显式背景画刷）+ plotAreaBackground。 */
    if (cv->m_chart->m_backgroundVisible) {
        xcv_fillChartBackground(cv, &painter,
            &(XRect){0, 0, XWidget_width((XWidget*)cv),
                     XWidget_height((XWidget*)cv)});
        if (cv->m_chart->m_backgroundPen != 0) {
            XPainter_setPen(&painter, cv->m_chart->m_backgroundPen);
            XPainter_drawRect(&painter,
                &(XRect){0, 0, XWidget_width((XWidget*)cv),
                         XWidget_height((XWidget*)cv)});
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
    return true;
}

/** @brief paintEvent：委托离屏渲染管线。 */
static void VX_chartView_paintEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XImage* image;
    if (!cv || !event) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    xcv_renderToImage(cv, image);
}

bool XChartView_renderToImage(XChartView* self, XImage* image)
{
    return xcv_renderToImage(self, image);
}

/* ==================== 框选缩放交互（对标 QChartView 鼠标语义） ==================== */

/** @brief 按下处理（虚表入口，定义见下）。 */
static void VX_chartView_mousePressEvent(XWidget* self, XEvent* event);
/** @brief 移动处理（虚表入口，定义见下）。 */
static void VX_chartView_mouseMoveEvent(XWidget* self, XEvent* event);
/** @brief 抬起处理（虚表入口，定义见下）。 */
static void VX_chartView_mouseReleaseEvent(XWidget* self, XEvent* event);
/** @brief 双击处理（虚表入口，定义见下）。 */
static void VX_chartView_mouseDoubleClickEvent(XWidget* self, XEvent* event);
/** @brief 滚轮处理（虚表入口，定义见下）。 */
static void VX_chartView_wheelEvent(XWidget* self, XEvent* event);

XVtable* XChartView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XChartView)    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_chartView_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_chartView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VX_chartView_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VX_chartView_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent, VX_chartView_mouseDoubleClickEvent);
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
    chart->m_zoomStack[chart->m_zoomCount].x = (float)chart->m_axisX->m_base.m_min;
    chart->m_zoomStack[chart->m_zoomCount].y = (float)chart->m_axisY->m_base.m_min;
    chart->m_zoomStack[chart->m_zoomCount].width =
        (float)(chart->m_axisX->m_base.m_max - chart->m_axisX->m_base.m_min);
    chart->m_zoomStack[chart->m_zoomCount].height =
        (float)(chart->m_axisY->m_base.m_max - chart->m_axisY->m_base.m_min);
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

/** @brief 橡皮筋是否启用（对标 QChartView 去掉 ClickThrough 位后的模式非空）。 */
static bool xcv_rubberBandActive(const XChartView* cv)
{
    if (!cv) return false;
    return (cv->m_rubberBand & (XChartView_RubberBand_VerticalRubberBand |
                                XChartView_RubberBand_HorizontalRubberBand |
                                XChartView_RubberBand_RectangleRubberBand)) != 0;
}

/** @brief 按下：橡皮筋模式下（且按点在绘图区内、无点击穿透命中）记录起点并抓取鼠标。 */
static void VX_chartView_mousePressEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!cv || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    pos = XMouseEvent_position(me);
    if (!xcv_rubberBandActive(cv) ||
        !xcv_inPlotArea(cv, pos.x, pos.y)) {
        int idx;
        double hx;
        double hy;
        XAbstractSeries* hit = xcv_hitTest(cv, pos.x, pos.y,
            &idx, &hx, &hy);
        if (hit)
            xcv_emitXySignal(hit,
                (size_t)XXYSeries_pressed_signal(hit, hx, hy),
                hx, hy);
        XEvent_accept(event);
        return;
    }
    /* ClickThrough：命中可点击序列时穿透到图表（对标 Qt 6.2+）。 */
    if (cv->m_rubberBand & XChartView_RubberBand_ClickThroughRubberBand) {
        int idx;
        double hx;
        double hy;
        XAbstractSeries* hit = xcv_hitTest(cv, pos.x, pos.y,
            &idx, &hx, &hy);
        if (hit) {
            xcv_emitXySignal(hit,
                (size_t)XXYSeries_pressed_signal(hit, hx, hy),
                hx, hy);
            XEvent_accept(event);
            return;
        }
    }
    cv->m_dragging = true;
    cv->m_dragStart = pos;
    XRect_init(&cv->m_dragRect, cv->m_dragStart.x, cv->m_dragStart.y, 0, 0);
    XWidget_grabMouse(self);
    XEvent_accept(event);
}

/** @brief 移动：更新橡皮筋矩形（Vertical/Horizontal 锁轴）并重绘。 */
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
    {
        /* 锁轴语义（对标 QChartView::mouseMoveEvent）：
           Vertical 位缺失 → 起点 Y 锁到绘图区顶、高度=绘图区高；
           Horizontal 位缺失 → 起点 X 锁到绘图区左、宽度=绘图区宽。 */
        XRect plotR;
        int x0;
        int y0;
        int w;
        int h;
        int x1;
        int y1;
        xcv_layout(cv, NULL, &plotR, NULL);
        x0 = cv->m_dragStart.x;
        y0 = cv->m_dragStart.y;
        w = pos.x - x0;
        h = pos.y - y0;
        if (!(cv->m_rubberBand & XChartView_RubberBand_VerticalRubberBand)) {
            y0 = plotR.y;
            h = plotR.height;
        }
        if (!(cv->m_rubberBand & XChartView_RubberBand_HorizontalRubberBand)) {
            x0 = plotR.x;
            w = plotR.width;
        }
        x1 = x0 + w;
        y1 = y0 + h;
        cv->m_dragRect.x = x0 < x1 ? x0 : x1;
        cv->m_dragRect.y = y0 < y1 ? y0 : y1;
        cv->m_dragRect.width = x0 < x1 ? x1 - x0 : x0 - x1;
        cv->m_dragRect.height = y0 < y1 ? y1 - y0 : y0 - y1;
    }
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 抬起：左键把橡皮筋矩形映射为数据域并放大；右键缩小。 */
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
    if (XMouseEvent_button(me) != XMouseButton_LeftButton &&
        XMouseEvent_button(me) != XMouseButton_RightButton)
        return;
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
    ax = cv->m_chart ? cv->m_chart->m_axisX : NULL;
    ay = cv->m_chart ? cv->m_chart->m_axisY : NULL;
    if (!ax || !ay) return;
    xcv_layout(cv, NULL, &plotR, NULL);
    minX = ax->m_base.m_min;
    maxX = ax->m_base.m_max;
    minY = ay->m_base.m_min;
    maxY = ay->m_base.m_max;
    rx = maxX - minX;
    ry = maxY - minY;
    if (rx <= 0.0 || ry <= 0.0 || plotR.width <= 0 || plotR.height <= 0)
        return;
    if (XMouseEvent_button(me) == XMouseButton_LeftButton) {
        XRect r = cv->m_dragRect;
        if (r.width < 8 || r.height < 8) {
            XWidget_update(self);
            return;
        }
        /* 锁轴模式：矩形补齐到绘图区尺寸（对标 QChartView::mouseReleaseEvent）。 */
        if (!(cv->m_rubberBand & XChartView_RubberBand_RectangleRubberBand)) {
            if (cv->m_rubberBand & XChartView_RubberBand_VerticalRubberBand) {
                r.x = plotR.x;
                r.width = plotR.width;
            } else if (cv->m_rubberBand & XChartView_RubberBand_HorizontalRubberBand) {
                r.y = plotR.y;
                r.height = plotR.height;
            }
        }
        /* 矩形端点 → 数据域（XChart::mapToValue 的布局内联实现）。 */
        {
            double v0x = minX + (double)(r.x - plotR.x) /
                         (double)plotR.width * rx;
            double v1x = minX + (double)(r.x + r.width - plotR.x) /
                         (double)plotR.width * rx;
            double v0y = maxY - (double)(r.y - plotR.y) /
                         (double)plotR.height * ry;
            double v1y = maxY - (double)(r.y + r.height - plotR.y) /
                         (double)plotR.height * ry;
            /* 压栈框选前的当前域（保持 zoomReset/zoomOut 语义一致）。 */
            xcv_pushCurrentDomain(cv);
            XValueAxis_setRange(ax, v0x, v1x);
            XValueAxis_setRange(ay, v0y, v1y);
        }
    } else if (XMouseEvent_button(me) == XMouseButton_RightButton) {
        /* 右键：缩小（锁轴模式下按半档扩展对应轴）。 */
        if (cv->m_rubberBand & XChartView_RubberBand_VerticalRubberBand) {
            double expand = ry * 0.5;
            xcv_pushCurrentDomain(cv);
            XValueAxis_setRange(ay, minY - expand, maxY + expand);
        } else if (cv->m_rubberBand & XChartView_RubberBand_HorizontalRubberBand) {
            double expand = rx * 0.5;
            xcv_pushCurrentDomain(cv);
            XValueAxis_setRange(ax, minX - expand, maxX + expand);
        } else {
            XChart_zoomOut(cv->m_chart);
        }
    }
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 双击：左键放大一档，Shift+左键复位（对标 Qt Charts 交互语义扩展）。 */
static void VX_chartView_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!cv || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK)
        return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_accept(event);
        return;
    }
    pos = XMouseEvent_position(me);
    if (XMouseEvent_modifiers(me) & XKeyboardModifier_ShiftModifier)
        XChart_zoomReset(cv->m_chart);
    else
        XChart_zoomIn(cv->m_chart);
    {
        int idx;
        double hx;
        double hy;
        XAbstractSeries* hit = xcv_hitTest(cv, pos.x, pos.y,
            &idx, &hx, &hy);
        if (hit)
            xcv_emitXySignal(hit,
                (size_t)XXYSeries_doubleClicked_signal(hit, hx, hy),
                hx, hy);
    }
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 滚轮：普通滚动映射为域滚动（120 角度 = 5% 域宽）；Ctrl+滚轮缩放。 */
static void VX_chartView_wheelEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XWheelEvent* we;
    if (!cv || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
    we = (XWheelEvent*)event;
    {
        XPoint delta = XWheelEvent_angleDelta(we);
        XKeyboardModifiers mods = XWheelEvent_modifiers(we);
        if (mods & XKeyboardModifier_ControlModifier) {
            /* Ctrl+滚轮：以绘图区中心缩放（向上放大一档，向下缩小）。 */
            if (delta.y > 0)
                XChart_zoomIn(cv->m_chart);
            else if (delta.y < 0)
                XChart_zoomOut(cv->m_chart);
        } else {
            double dx = (double)delta.x / (120.0 * 20.0);
            double dy = (delta.y != 0) ? (double)delta.y : (double)delta.x;
            XChart_scroll(cv->m_chart, dx, dy / (120.0 * 20.0));
        }
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