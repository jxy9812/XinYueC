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
#include "XWindowEvent.h"
#if XCHARTVIEW_PROFILE
/* Phase A 五段计时依赖：环境门控（XCHARTVIEW_PROFILE 变量，语义同
 * getenv，对标 XGPU_PROFILE）与单调微秒时钟（XDateTime 纳秒换算）。 */
#include "XSystem.h"
#include "XDateTime.h"
#endif /* XCHARTVIEW_PROFILE */
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

#if XCHARTVIEW_PROFILE
/* ==================== Phase A 渲染剖析（§10.2 五段计时） ==================== */

/**
 * @brief 五段耗时采样窗（进程级累计；§10.2 Phase A）。
 *
 * 五段口径：fp=入口指纹计算；blit=静态层 blit；rebuild=静态层重建
 * （STATIC_LAYER=0 或层不可用回退直画时为五件套直画耗时）；series=
 * 绘图区五类序列现绘；legend=图例+其余（clip 恢复/饼图/橡皮筋）。
 */
typedef struct XChartViewProfile
{
    int gate;             /**< 环境门控缓存：-1 未探测，0 关，1 开。 */
    int64_t windowStart;  /**< 采样窗起点（微秒；满 1 秒输出并清零）。 */
    int64_t fpUs;         /**< 指纹计算累计。 */
    int64_t blitUs;       /**< 静态层 blit 累计。 */
    int64_t rebuildUs;    /**< 静态层重建（或直画回退）累计。 */
    int64_t seriesUs;     /**< 序列绘制累计。 */
    int64_t legendUs;     /**< 图例+其余累计。 */
    int frames;           /**< 采样窗内帧数。 */
} XChartViewProfile;

static XChartViewProfile g_xcvProfile = { -1, 0, 0, 0, 0, 0, 0, 0 };

/** @brief 运行期门控：XCHARTVIEW_PROFILE 环境变量非空且非 "0" 才计时
 *         输出（一次探测缓存；关闭时五段探针零输出、近零开销，不污染
 *         正常基准，§10.1 数据口径保持纯净）。 */
static bool xcv_profileEnabled(void)
{
    if (g_xcvProfile.gate < 0) {
        const char* env = XSystem_environment("XCHARTVIEW_PROFILE");
        g_xcvProfile.gate = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
    }
    return g_xcvProfile.gate > 0;
}

/** @brief 单调微秒时间戳（与 demo 基准/GPU 剖析同源：XDateTime 纳秒换算）。 */
static int64_t xcv_profileNowUs(void)
{ return XDateTime_currentNSecsSinceEpoch() / 1000LL; }

/** @brief 累计一帧五段耗时；采样窗满 1 秒经 XPrintf 输出各段均值并清零。 */
static void xcv_profileFrame(int64_t fpUs, int64_t blitUs, int64_t rebuildUs,
                             int64_t seriesUs, int64_t legendUs)
{
    XChartViewProfile* p = &g_xcvProfile;
    int64_t now;
    int64_t interval;
    if (!xcv_profileEnabled()) return;
    now = xcv_profileNowUs();
    if (p->windowStart <= 0) p->windowStart = now;
    p->fpUs += fpUs;
    p->blitUs += blitUs;
    p->rebuildUs += rebuildUs;
    p->seriesUs += seriesUs;
    p->legendUs += legendUs;
    ++p->frames;
    interval = now - p->windowStart;
    if (interval >= 1000000 && p->frames > 0) {
        XPrintf("XChartProfile: fp=%dus blit=%dus rebuild=%dus "
                "series=%dus legend=%dus\n",
                (int)(p->fpUs / p->frames), (int)(p->blitUs / p->frames),
                (int)(p->rebuildUs / p->frames),
                (int)(p->seriesUs / p->frames),
                (int)(p->legendUs / p->frames));
        p->windowStart = 0;
        p->fpUs = 0;
        p->blitUs = 0;
        p->rebuildUs = 0;
        p->seriesUs = 0;
        p->legendUs = 0;
        p->frames = 0;
    }
}
/* 五段计时成对探针（仅在 xcv_renderToImage 内配合局部 profT 使用；
 * 关闭编译开关时退化为空操作，保证裁剪路径零代码生成）。 */
#define XCV_PROF_BEGIN() \
    do { if (xcv_profileEnabled()) profT = xcv_profileNowUs(); } while (0)
#define XCV_PROF_END(field) \
    do { if (profT >= 0) { \
             field += xcv_profileNowUs() - profT; \
             profT = -1; \
         } } while (0)
#else /* XCHARTVIEW_PROFILE */
#define XCV_PROF_BEGIN() do { } while (0)
#define XCV_PROF_END(field) do { (void)0; } while (0)
#endif /* XCHARTVIEW_PROFILE */

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

/** @brief 把主题背景渐变（或纯色）应用到画刷并填充矩形。
 * @param anchor 渐变锚定矩形（整控件区域；渐变按绝对坐标采样，局部
 *               脏区刷新时色带与全图绘制保持一致）。
 * @param fill   实际填充矩形（脏区求交后的小块；无脏区时等于 anchor）。 */
static void xcv_fillChartBackground(XChartView* self, XPainter* painter,
                                    const XRect* anchor, const XRect* fill)
{
    XChart* chart = self->m_chart;
    if (!chart) return;
    if (chart->m_backgroundBrush != 0) {
        XPainter_fillRect(painter, fill, chart->m_backgroundBrush);
        return;
    }
#if XPAINTER_BRUSH_ON
    if (chart->m_themeBgStart != chart->m_themeBgEnd) {
        XPainterGradient gradient;
        XPainterGradient_initLinear(&gradient, 0.0f, (float)anchor->y,
                                    0.0f, (float)(anchor->y + anchor->height));
        XPainterGradient_addStop(&gradient, 0.0f, chart->m_themeBgStart);
        XPainterGradient_addStop(&gradient, 1.0f, chart->m_themeBgEnd);
        XPainter_setBrushGradient(painter, &gradient);
        XPainter_fillRect_2(painter, fill);
        return;
    }
#endif /* XPAINTER_BRUSH_ON */
    XPainter_fillRect(painter, fill, chart->m_themeBgStart);
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
    XFont_deinit_base((XClass*)&font);
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

/**
 * @brief 查询序列在图表中的全局下标（未登记返回 -1）。
 *
 * 视图侧本地副本，与 XChart.c xchart_seriesGlobalIndex 逐字同口径：
 * 泛型注册表直查；类型化路径按 line→spline→area→bar→scatter 固定顺序
 * 累计位置推全局序号（XChart.c 该函数为 static 跨文件不可见）。
 */
static int xcv_seriesGlobalIndex(const XChart* chart, const void* series)
{
    int i;
    int g;
    if (!chart || !series) return -1;
    for (i = 0; i < chart->m_seriesCount; ++i)
        if (chart->m_series[i] == series) return i;
    g = 0;
    for (i = 0; i < chart->m_lineCount; ++i) {
        if (chart->m_lineSeries[i] == series) return g;
        ++g;
    }
    for (i = 0; i < chart->m_splineCount; ++i) {
        if (chart->m_splineSeries[i] == series) return g;
        ++g;
    }
    for (i = 0; i < chart->m_areaCount; ++i) {
        if (chart->m_areaSeries[i] == series) return g;
        ++g;
    }
    for (i = 0; i < chart->m_barCount; ++i) {
        if (chart->m_barSeries[i] == series) return g;
        ++g;
    }
    for (i = 0; i < chart->m_scatterCount; ++i) {
        if (chart->m_scatterSeries[i] == series) return g;
        ++g;
    }
    return -1;
}

/**
 * @brief 主题色循环取色（下标回环）。
 *
 * 根因（R-40）：视图侧主题色回退此前按「类型内下标」取色，与 setTheme
 * 烘焙（xchart_applyThemeToSeries→xchart_seriesGlobalIndex 跨类型累计
 * 全局序）不一致——混合类型且未显式设色时，各类型首序列取到同一主题色。
 * 统一改为由序列指针反推全局下标后取色（与烘焙完全一致）；指针未登记
 * （防御）时退回调用方类型内下标。
 */
static uint32_t xcv_seriesColor(const XChartView* self, const void* series,
                                int localIndex)
{
    const XChart* chart;
    int gi;
    if (!self || !self->m_chart) return 0;
    chart = self->m_chart;
    gi = xcv_seriesGlobalIndex(chart, series);
    if (gi < 0) gi = localIndex;
    return XChart_themeColor(chart, gi);
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
    if (legendR) XRect_init(legendR, w - 150, top + 8, 145, 20 * 6);
}

/** @brief 估算文本包围盒是否与脏区相交（NULL 脏区恒可见）。
 * @details 对标 Qt systemClip 下光栅引擎按裁剪盒跳过整体在外的文本：
 *          稳态小脏区刷新不再为绘图区外的轴标签/图例/标题做字形光栅化。 */
static bool xcv_textVisible(const XRect* dirty, int x, int y, int textW)
{
    if (!dirty) return true;
    return x < dirty->x + dirty->width &&
           x + (textW > 0 ? textW : 1) > dirty->x &&
           y - 2 < dirty->y + dirty->height &&
           y + 18 > dirty->y;
}

/** @brief 绘制标题（居中；颜色=标题画刷或主题标签色）。 */
static void xcv_paintTitle(XChartView* self, XPainter* painter,
                           const XRect* titleR, const XRect* dirty)
{
    uint32_t text;
    XChart* chart = self->m_chart;
    XFont font = XWidget_font((XWidget*)self);
    const char* title;
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
        int tx;
        int ty;
        title = XChart_title_2(chart);
        tx = titleR->x + titleR->width / 2 - (int)XStrlen(title) * 4;
        ty = titleR->y + titleR->height - 8;
        if (xcv_textVisible(dirty, tx, ty, (int)XStrlen(title) * 8))
            XPainter_drawText(painter, tx, ty, title, text);
    }
    XFont_deinit_base((XClass*)&font);
}

/** @brief 绘制数值轴网格 + 刻度标签（颜色取自主题规格，轴级颜色可覆盖；
 *        轴 setVisible(false) 跳过该轴全部可视元素，reverse 翻转刻度值）。 */
static void xcv_paintAxes(XChartView* self, XPainter* painter,
                          const XRect* plotR, const XRect* dirty)
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
    bool visX;
    bool visY;
    XFont font = XWidget_font((XWidget*)self);
    char buf[32];
    int i;
    int ticks;
    if (!ax || !ay) return;
    /* 根因（R-106）：轴 m_visible 此前只存不用——setVisible(false) 后
     * 轴线/网格/标签照画。对标 QAbstractAxis::setVisible(false) 隐藏该轴
     * 全部可视元素（轴线、网格、次网格、标签、阴影带），双轴同隐时整段
     * 跳过（仅剩字体副本释放）。 */
    visX = ax->m_base.m_visible;
    visY = ay->m_base.m_visible;
    if (!visX && !visY) {
        /* 显式 XClass 转换：XFont_deinit_base 为裸别名宏，直呼会新增
         * 指针类型诊断（同 xcv_staticFingerprint 尾部注释约定）。 */
        XClass_deinit_base((XClass*)&font);
        return;
    }
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
    if (visY && ay->m_base.m_shadesVisible && chart->m_themeShadesBrush != 0) {
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
    /* 轴线（对标 theme axisLinePen）；随各轴可见性开关。 */
    if (visY) {
        XPainter_setPen(painter, axisPenY);
        XPainter_setPenWidth(painter, chart->m_themeAxisLineWidth);
        XPainter_drawLine(painter, plotR->x, plotR->y,
                          plotR->x, plotR->y + plotR->height);
    }
    if (visX) {
        XPainter_setPen(painter, axisPenX);
        XPainter_setPenWidth(painter, chart->m_themeAxisLineWidth);
        XPainter_drawLine(painter, plotR->x, plotR->y + plotR->height,
                          plotR->x + plotR->width,
                          plotR->y + plotR->height);
    }
    /* Y 轴刻度/网格/标签：reverse 时屏幕比例 t 处的值为 min+t*range
     *（默认向上为 max-t*range），与 xcv_mapPoint 的翻转映射对齐。 */
    ticks = ay->m_tickCount > 1 ? ay->m_tickCount : 2;
    for (i = 0; visY && i < ticks; ++i) {
        double t = (double)i / (ticks - 1);
        double v = ay->m_base.m_reverse
            ? ay->m_base.m_min + t * (ay->m_base.m_max - ay->m_base.m_min)
            : ay->m_base.m_max - t * (ay->m_base.m_max - ay->m_base.m_min);
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
        if (xcv_textVisible(dirty, plotR->x - 40, y + 6,
                            (int)XStrlen(buf) * 8))
            XPainter_drawText(painter, plotR->x - 40, y + 6, buf, textY);
    }
    /* X 轴刻度/网格/标签：reverse 时屏幕比例 t 处的值为 max-t*range。 */
    ticks = ax->m_tickCount > 1 ? ax->m_tickCount : 2;
    for (i = 0; visX && i < ticks; ++i) {
        double t = (double)i / (ticks - 1);
        double v = ax->m_base.m_reverse
            ? ax->m_base.m_max - t * (ax->m_base.m_max - ax->m_base.m_min)
            : ax->m_base.m_min + t * (ax->m_base.m_max - ax->m_base.m_min);
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
        if (xcv_textVisible(dirty, x - 12,
                            plotR->y + plotR->height + 16,
                            (int)XStrlen(buf) * 8))
            XPainter_drawText(painter, x - 12,
                              plotR->y + plotR->height + 16, buf, textX);
    }
    XFont_deinit_base((XClass*)&font);
}

#if XCHARTVIEW_STATIC_LAYER_ON
/* ==================== Phase B 静态层缓存（§10.2，对标 DeviceCoordinateCache） ==================== */

/** @brief 运行期层旁路（setStaticLayerBypass 的 A/B 开关；仅回归测试
 *  使用，生产恒 false）。GUI 主线程单写，与指纹命中判定同线程纪律。 */
static bool g_xcvLayerBypass;

void XChartView_setStaticLayerBypass(XChartView* self, bool bypass)
{
    (void)self;
    g_xcvLayerBypass = bypass;
}

/** @brief FNV-1a 64 位散列字节流增量（自行实现的静态指纹算法；
 *         offset basis 14695981039346656037，prime 1099511628211）。 */
static uint64_t xcv_fnv1aBytes(uint64_t hash, const void* data, size_t len)
{
    const uint8_t* bytes = (const uint8_t*)data;
    size_t i;
    for (i = 0; i < len; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/** @brief FNV-1a 混入 uint32（固定小端字节序展开，指纹跨平台一致）。 */
static uint64_t xcv_fnv1aU32(uint64_t hash, uint32_t value)
{
    uint8_t raw[4];
    raw[0] = (uint8_t)(value & 0xFFu);
    raw[1] = (uint8_t)((value >> 8) & 0xFFu);
    raw[2] = (uint8_t)((value >> 16) & 0xFFu);
    raw[3] = (uint8_t)((value >> 24) & 0xFFu);
    return xcv_fnv1aBytes(hash, raw, sizeof(raw));
}

/** @brief FNV-1a 混入 int。 */
static uint64_t xcv_fnv1aInt(uint64_t hash, int value)
{ return xcv_fnv1aU32(hash, (uint32_t)value); }

/** @brief FNV-1a 混入 bool。 */
static uint64_t xcv_fnv1aBool(uint64_t hash, bool value)
{ return xcv_fnv1aU32(hash, value ? 1u : 0u); }

/** @brief FNV-1a 混入 double 位型（轴范围值；按原始 8 字节比较）。 */
static uint64_t xcv_fnv1aDouble(uint64_t hash, double value)
{
    uint64_t bits = 0;
    XMemcpy(&bits, &value, sizeof(bits));
    return xcv_fnv1aBytes(hash, &bits, sizeof(bits));
}

/** @brief FNV-1a 混入 UTF-8 串（含结尾 0 以区分同前缀；NULL 单独混入标记）。 */
static uint64_t xcv_fnv1aStr(uint64_t hash, const char* text)
{
    if (!text) return xcv_fnv1aU32(hash, 0xFFFFFFFFu);
    return xcv_fnv1aBytes(hash, text, XStrlen(text) + 1);
}

/**
 * @brief 计算静态层内容指纹（FNV-1a 64 位；§10.2 指纹契约）。
 *
 * 覆盖：控件宽高/margins/titleVisible+标题文本 hash（另含标题字体族/
 * 字号/画刷，三者直接决定标题字形与颜色）/主题字段组（色板+背景渐变+
 * 标签+轴线+网格+次网格+轮廓+阴影带）/backgroundVisible+brush+pen/
 * plotArea 背景三态/双轴 range+tickCount+网格可见性（另含阴影带开关、
 * 轴级线条与标签颜色、标签格式串）/legendVisible/序列数量+颜色+名称
 * hash（折线/散点/面积/样条/柱状）。**不含序列数据点与饼图切片值**
 * （数据是动态部分，每帧现绘）。
 *
 * 图例取舍说明（§10.2 二选一）：图例【不入】静态层，随帧现绘——图例
 * 内容随序列颜色/名称变化且文本色走调色板回退链。指纹仍按 §10.2 契约
 * 混入 legendVisible 与序列颜色/名称 hash：这些字段变化会触发一次层
 * 重建（代价可忽略，且属外观变化场景），并为后续"图例入层"演进预留
 * 指纹兼容性（届时无需改指纹契约）。
 */
static uint64_t xcv_staticFingerprint(const XChartView* cv)
{
    const XChart* chart = cv ? cv->m_chart : NULL;
    const XValueAxis* axes[2];
    uint64_t h = 14695981039346656037ULL;
    XFont font;
    int i;
    int a;
    if (!chart) return h;
    /* 控件宽高（布局与层画布尺寸种子）。 */
    h = xcv_fnv1aInt(h, XWidget_width((const XWidget*)cv));
    h = xcv_fnv1aInt(h, XWidget_height((const XWidget*)cv));
    /* margins（<=0 走视图默认，原值混入即可）。 */
    h = xcv_fnv1aInt(h, chart->m_margins.left);
    h = xcv_fnv1aInt(h, chart->m_margins.top);
    h = xcv_fnv1aInt(h, chart->m_margins.right);
    h = xcv_fnv1aInt(h, chart->m_margins.bottom);
    /* 标题：可见性 + 文本/字体族/字号/画刷 hash。 */
    h = xcv_fnv1aBool(h, chart->m_titleVisible);
    h = xcv_fnv1aStr(h, XChart_title_2(chart));
    h = xcv_fnv1aStr(h, XChart_titleFontFamily_2(chart));
    h = xcv_fnv1aInt(h, chart->m_titlePixelSize);
    h = xcv_fnv1aU32(h, chart->m_titleBrush);
    /* 主题字段组（对标 ChartTheme 全量外观缓存字段）。 */
    h = xcv_fnv1aInt(h, (int)chart->m_themeId);
    for (i = 0; i < 8; ++i)
        h = xcv_fnv1aU32(h, chart->m_theme[i]);
    h = xcv_fnv1aU32(h, chart->m_themeBgStart);
    h = xcv_fnv1aU32(h, chart->m_themeBgEnd);
    h = xcv_fnv1aU32(h, chart->m_themeLabelBrush);
    h = xcv_fnv1aU32(h, chart->m_themeAxisLinePen);
    h = xcv_fnv1aInt(h, chart->m_themeAxisLineWidth);
    h = xcv_fnv1aU32(h, chart->m_themeGridPen);
    h = xcv_fnv1aInt(h, chart->m_themeGridLineWidth);
    h = xcv_fnv1aU32(h, chart->m_themeMinorGridPen);
    h = xcv_fnv1aInt(h, chart->m_themeMinorGridLineWidth);
    h = xcv_fnv1aU32(h, chart->m_themeOutlinePen);
    h = xcv_fnv1aInt(h, chart->m_themeOutlineWidth);
    h = xcv_fnv1aU32(h, chart->m_themeShadesBrush);
    h = xcv_fnv1aInt(h, chart->m_themeShadesMode);
    /* 背景：可见性 + 画刷 + 画笔。 */
    h = xcv_fnv1aBool(h, chart->m_backgroundVisible);
    h = xcv_fnv1aU32(h, chart->m_backgroundBrush);
    h = xcv_fnv1aU32(h, chart->m_backgroundPen);
    /* plotArea 背景三态：可见性 + 画刷 + 画笔。 */
    h = xcv_fnv1aBool(h, chart->m_plotAreaBackgroundVisible);
    h = xcv_fnv1aU32(h, chart->m_plotAreaBackgroundBrush);
    h = xcv_fnv1aU32(h, chart->m_plotAreaBackgroundPen);
    /* 双轴：range + tickCount + 网格/阴影可见性 + 轴可见性/reverse +
     * 轴级颜色 + 标签格式（R-106：两属性已被绘制消费，须入指纹驱动
     * 静态层重建，否则切换后回贴旧层）。 */
    axes[0] = chart->m_axisX;
    axes[1] = chart->m_axisY;
    for (a = 0; a < 2; ++a) {
        const XValueAxis* axis = axes[a];
        if (!axis) {
            h = xcv_fnv1aU32(h, 0u);
            continue;
        }
        h = xcv_fnv1aDouble(h, axis->m_base.m_min);
        h = xcv_fnv1aDouble(h, axis->m_base.m_max);
        h = xcv_fnv1aBool(h, axis->m_base.m_visible);
        h = xcv_fnv1aBool(h, axis->m_base.m_reverse);
        h = xcv_fnv1aInt(h, axis->m_tickCount);
        h = xcv_fnv1aBool(h, axis->m_base.m_gridLineVisible);
        h = xcv_fnv1aBool(h, axis->m_base.m_shadesVisible);
        h = xcv_fnv1aU32(h, axis->m_base.m_linePenColor);
        h = xcv_fnv1aU32(h, axis->m_base.m_labelsBrushColor);
        h = xcv_fnv1aStr(h, axis->m_labelFormat
                             ? XString_toUtf8(axis->m_labelFormat) : NULL);
    }
    /* 图例可见性（图例现绘不入层；见函数头取舍说明）。 */
    h = xcv_fnv1aBool(h, chart->m_legendVisible);
    /* 序列数量+颜色+名称 hash（不含数据点；名称/颜色属外观，驱动图例
     * 与序列主题回退色，按 §10.2 契约混入）。 */
    h = xcv_fnv1aInt(h, chart->m_lineCount);
    for (i = 0; i < chart->m_lineCount && chart->m_lineSeries[i]; ++i) {
        const XLineSeries* s = chart->m_lineSeries[i];
        h = xcv_fnv1aU32(h, s->m_base.m_color);
        h = xcv_fnv1aStr(h, XAbstractSeries_name_2(&s->m_base.m_base));
    }
    h = xcv_fnv1aInt(h, chart->m_scatterCount);
    for (i = 0; i < chart->m_scatterCount && chart->m_scatterSeries[i]; ++i) {
        const XScatterSeries* s = chart->m_scatterSeries[i];
        h = xcv_fnv1aU32(h, s->m_base.m_color);
        h = xcv_fnv1aStr(h, XAbstractSeries_name_2(&s->m_base.m_base));
    }
    h = xcv_fnv1aInt(h, chart->m_areaCount);
    for (i = 0; i < chart->m_areaCount && chart->m_areaSeries[i]; ++i) {
        const XAreaSeries* s = chart->m_areaSeries[i];
        h = xcv_fnv1aU32(h, s->m_color);
        h = xcv_fnv1aStr(h, XAreaSeries_name_2(s));
    }
    h = xcv_fnv1aInt(h, chart->m_splineCount);
    for (i = 0; i < chart->m_splineCount && chart->m_splineSeries[i]; ++i) {
        const XSplineSeries* s = chart->m_splineSeries[i];
        h = xcv_fnv1aU32(h, s->m_base.m_color);
        h = xcv_fnv1aStr(h, XAbstractSeries_name_2(&s->m_base.m_base));
    }
    h = xcv_fnv1aInt(h, chart->m_barCount);
    for (i = 0; i < chart->m_barCount && chart->m_barSeries[i]; ++i) {
        const XBarSeries* s = chart->m_barSeries[i];
        h = xcv_fnv1aU32(h, s->m_color);
        h = xcv_fnv1aStr(h, XAbstractSeries_name_2(&s->m_base.m_base));
    }
    /* 控件字体（标题/轴标签字形与步进随字体变化）。 */
    font = XWidget_font((const XWidget*)cv);
    h = xcv_fnv1aStr(h, XFont_family(&font));
    h = xcv_fnv1aInt(h, XFont_pointSize(&font));
    h = xcv_fnv1aInt(h, XFont_pixelSize(&font));
    h = xcv_fnv1aInt(h, XFont_weight(&font));
    h = xcv_fnv1aBool(h, XFont_bold(&font));
    h = xcv_fnv1aBool(h, XFont_italic(&font));
    h = xcv_fnv1aBool(h, XFont_underline(&font));
    h = xcv_fnv1aBool(h, XFont_strikeOut(&font));
    h = xcv_fnv1aBool(h, XFont_overline(&font));
    /* XFont_deinit_base 是 XClass_deinit_base 的裸别名宏（无 XClass
     * 转换）；此处显式转换既释放深拷贝字体又免新增指针类型诊断。 */
    XClass_deinit_base((XClass*)&font);
    /* 调色板窗口文本色（标题画刷为 0 时的回退色，随调色板变化）。 */
    h = xcv_fnv1aU32(h, xcv_color(cv, XPaletteColorRole_WindowText));
    return h;
}
#endif /* XCHARTVIEW_STATIC_LAYER_ON */

/**
 * @brief 静态五件套：背景+背景笔、绘图区背景（可见+画刷/画笔）、标题、
 *        坐标轴网格。直画路径与层重建路径共用（同代码保证逐位一致）。
 * @param dirty 直画时为脏区（填充矩形先与脏区求交、脏区外文本跳过，
 *              渐变仍按锚定矩形逐像素求值——裁剪只省合成不省求值）；
 *              层重建传 NULL（全量渲进层，脏区约束由 blit 承担）。
 * @note STATIC_LAYER=0 时直画路径调用本函数，操作序列与既有管线一致。
 */
static void xcv_paintStaticContent(XChartView* cv, XPainter* painter,
                                   const XRect* bounds, const XRect* titleR,
                                   const XRect* plotR, const XRect* dirty)
{
    XRect bgRect;
    XRect paRect;
    if (!cv->m_chart) return;
    bgRect = *bounds;
    if (dirty)
        bgRect = XRect_intersected(&bgRect, dirty);
    if (cv->m_chart->m_backgroundVisible) {
        xcv_fillChartBackground(cv, painter, bounds, &bgRect);
        if (cv->m_chart->m_backgroundPen != 0) {
            XPainter_setPen(painter, cv->m_chart->m_backgroundPen);
            XPainter_drawRect(painter, bounds);
        }
    }
    if (cv->m_chart->m_plotAreaBackgroundVisible) {
        paRect = *plotR;
        if (dirty)
            paRect = XRect_intersected(&paRect, dirty);
        if (cv->m_chart->m_plotAreaBackgroundBrush != 0)
            XPainter_fillRect(painter, &paRect,
                              cv->m_chart->m_plotAreaBackgroundBrush);
        if (cv->m_chart->m_plotAreaBackgroundPen != 0) {
            XPainter_setPen(painter, cv->m_chart->m_plotAreaBackgroundPen);
            XPainter_drawRect(painter, plotR);
        }
    }
    if (cv->m_chart->m_titleVisible)
        xcv_paintTitle(cv, painter, titleR, dirty);
    xcv_paintAxes(cv, painter, plotR, dirty);
}

#if XCHARTVIEW_STATIC_LAYER_ON
/**
 * @brief 把静态五件套渲进层画布（dirty=NULL 全量；对标 DeviceCoordinate
 *        Cache 的位图重建）。
 * @details 层画布尺寸/格式未变时直接复用（resize 失效挂钩只清
 *          m_staticValid，画布在此按需重分配）；每次重建先清为全透明
 *          （对标保留层/grab 快照画布语义）：半透明背景下层内合成 =
 *          五件套 over 0，回贴 = 五件套 over 目标既有内容，source-over
 *          结合律保证与直画逐位一致。
 * @return 重建成功返回 true；画布分配失败返回 false（调用方回退直画）。
 */
static bool xcv_staticLayerRebuild(XChartView* cv, const XRect* bounds,
                                   const XRect* titleR, const XRect* plotR,
                                   XImageFormat format)
{
    XPainter painter;
    if (!cv || !bounds || bounds->width <= 0 || bounds->height <= 0)
        return false;
    if (XImage_isNull(&cv->m_staticLayer) ||
        XImage_width(&cv->m_staticLayer) != bounds->width ||
        XImage_height(&cv->m_staticLayer) != bounds->height ||
        XImage_format(&cv->m_staticLayer) != format) {
        if (!XImage_reinit_ex(&cv->m_staticLayer, bounds->width,
                              bounds->height, format))
            return false;
    }
    XImage_fillRect(&cv->m_staticLayer, NULL, 0u);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, &cv->m_staticLayer)) {
        XPainter_deinit(&painter);
        return false;
    }
    /* 层画布不继承 paintOffset 平移与脏区裁剪：层像素坐标即控件本地
     * 坐标；blit 时经目标绘制器既有平移/裁剪落位。 */
    xcv_paintStaticContent(cv, &painter, bounds, titleR, plotR, NULL);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    return true;
}
#endif /* XCHARTVIEW_STATIC_LAYER_ON */

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

/** @brief XY 序列公共：点标记 + 点标签（线/样条/散点复用）。
 * @details baseColor 由调用方按序列全局序（R-40）与序列色解析后传入，
 *          此处不再重复取色。 */
static void xcv_drawXyPoints(XChartView* self, XPainter* painter,
                             const XRect* plotR, XXYSeries* xy,
                             uint32_t baseColor, bool scatter, int markerShape)
{
    uint32_t color;
    uint32_t labelColor;
    const char* fmt;
    int pi;
    if (!self->m_chart) return;
    if (!xy->m_pointsVisible && !xy->m_pointLabelsVisible) return;
    color = baseColor;
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
            ? xy->m_color : xcv_seriesColor(self, s, si);
        int pi;
        if (!XAbstractSeries_isVisible(&xy->m_base) || xy->m_count < 2) continue;
        /* 根因（R-106）配套：此处此前内联了一份「默认方向」的域映射，轴
         * reverse 时线段与点标记/命中检测（xcv_mapPoint，消费 reverse）
         * 映射不一致、线点错位。退化域（rx/ry<=0）整条跳过，与原逐段
         * continue 语义等价。 */
        {
            XValueAxis* ax = self->m_chart->m_axisX;
            XValueAxis* ay = self->m_chart->m_axisY;
            double rx = ax->m_base.m_max - ax->m_base.m_min;
            double ry = ay->m_base.m_max - ay->m_base.m_min;
            if (rx <= 0.0 || ry <= 0.0) continue;
        }
        XPainter_setPen(painter, color);
        XPainter_setPenWidth(painter,
            (int)(xy->m_width > 0 ? xy->m_width : 2));
        for (pi = 0; pi < xy->m_count - 1; ++pi) {
            const XPointF* p0 = &xy->m_points[pi];
            const XPointF* p1 = &xy->m_points[pi + 1];
            int x0; int y0; int x1; int y1;
            xcv_mapPoint(self, plotR, p0->x, p0->y, &x0, &y0);
            xcv_mapPoint(self, plotR, p1->x, p1->y, &x1, &y1);
            XPainter_drawLine(painter, x0, y0, x1, y1);
        }
        if (xy->m_bestFitVisible)
            xcv_drawBestFitLine(self, painter, plotR, xy);
        xcv_drawXyPoints(self, painter, plotR, xy, color, false, 0);
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

/** @brief 数据点 → 屏幕坐标（按轴范围缩放；轴 reverse 时翻转映射）。 */
static void xcv_mapPoint(const XChartView* self, const XRect* plotR,
                         double x, double y, int* sx, int* sy)
{
    XValueAxis* ax = self->m_chart->m_axisX;
    XValueAxis* ay = self->m_chart->m_axisY;
    double rx = ax->m_base.m_max - ax->m_base.m_min;
    double ry = ay->m_base.m_max - ay->m_base.m_min;
    if (rx <= 0) rx = 1;
    if (ry <= 0) ry = 1;
    /* 根因（R-106）：轴 m_reverse 此前只存不用。对标 QAbstractAxis
     * setReverse 后域方向翻转的语义：reverse X 屏幕比例取 (max-x)，
     * reverse Y 取 (y-min)（默认 Y 向上）。序列点/刻度/命中检测统一
     * 经本函数映射，翻转后保持一致。后续核对 Qt 6.8.3 源码推翻了
     * R-106 deferred 登记「setReverse 会把轴线/标签移至对侧」：
     * verticalaxis.cpp/horizontalaxis.cpp 中轴线（arrow）与刻度停点
     * 仅随 alignment() 定位，不读 isReverse()；reverse 只镜像网格/
     * 刻度/标签沿轴的排列（净效果=刻度值序翻转），轴不换边——故本
     * 实现不移轴，维持映射+刻度值翻转即为对标行为。 */
    *sx = plotR->x + (int)((ax->m_base.m_reverse
        ? ax->m_base.m_max - x
        : x - ax->m_base.m_min) / rx * plotR->width);
    *sy = plotR->y + (int)((ay->m_base.m_reverse
        ? y - ay->m_base.m_min
        : ay->m_base.m_max - y) / ry * plotR->height);
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
            ? xy->m_color : xcv_seriesColor(self, s, si);
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
        xcv_drawXyPoints(self, painter, plotR, xy, color, true,
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
            ? s->m_color : xcv_seriesColor(self, s, si);
        int pi;
        if (!XAbstractSeries_isVisible(&s->m_base) || !up ||
            up->m_base.m_count < 2) continue;
        XPainter_setBrush(painter, color);
        XPainter_setPen(painter, color);
        /* 对标 Qt QAreaSeries（areachartitem.cpp: painter->drawPath）：
           上边界折线 + 基线两步回程构成闭合多边形，一次填充。此前逐段
           矩形近似在下降段过填（填到线上方）、上升段欠填（楔形空隙）。 */
        {
            /* 根因（R-39）：填充此前用定长 poly[128] 并钳 126 点静默
             * 截断，描边却画全量——>126 点面积图填充残缺，违背 §3.2
             * 「无静默截断」声明。改为按点数动态容量：上边界全点 +
             * 基线 2 点，填充与描边同源（分配失败仅退化为不填充，
             * 不再产出残缺图形）。 */
            int count = up->m_base.m_count;
            XPoint* poly = (XPoint*)XMalloc_System(
                sizeof(XPoint) * (size_t)(count + 2));
            int used = 0;
            int bx;
            int by;
            int i;
            if (poly) {
                for (i = 0; i < count; ++i) {
                    int sx;
                    int sy;
                    xcv_mapPoint(self, plotR, up->m_base.m_points[i].x,
                                 up->m_base.m_points[i].y, &sx, &sy);
                    poly[used].x = sx;
                    poly[used].y = sy;
                    ++used;
                }
                xcv_mapPoint(self, plotR, up->m_base.m_points[count - 1].x,
                             s->m_baseValue, &bx, &by);
                poly[used].x = bx;
                poly[used].y = by;
                ++used;
                xcv_mapPoint(self, plotR, up->m_base.m_points[0].x,
                             s->m_baseValue, &bx, &by);
                poly[used].x = bx;
                poly[used].y = by;
                ++used;
                XPainter_drawPolygon(painter, poly, used,
                                     XPainterFillRule_OddEven);
                XFree_System(poly);
            }
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
            ? s->m_base.m_color : xcv_seriesColor(self, s, si);
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
        xcv_drawXyPoints(self, painter, plotR, &s->m_base, color, false, 0);
    }
}

/** @brief 绘制图例（色块 + 序列名；行包围盒与脏区不相交时跳过）。 */
static void xcv_paintLegend(XChartView* self, XPainter* painter,
                            const XRect* legendR, const XRect* dirty)
{
    int i;
    uint32_t text = xcv_color(self, XPaletteColorRole_WindowText);
    XFont font = XWidget_font((XWidget*)self);
    int y = legendR->y;
    XPainter_setFont(painter, &font);
    for (i = 0; i < self->m_chart->m_lineCount && y < legendR->y + legendR->height; ++i) {
        XLineSeries* s = self->m_chart->m_lineSeries[i];
        uint32_t color = s->m_base.m_color != 0
            ? s->m_base.m_color : xcv_seriesColor(self, s, i);
        if (xcv_textVisible(dirty, legendR->x, y, 140)) {
            XPainter_fillRect(painter,
                &(XRect){legendR->x, y, 12, 12}, color);
            XPainter_drawText(painter, legendR->x + 18, y + 10,
                              XAbstractSeries_name_2(&s->m_base.m_base), text);
        }
        y += 20;
    }
    {   /* 柱状图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_barCount && y < legendR->y + legendR->height; ++k) {
            XBarSeries* b = self->m_chart->m_barSeries[k];
            uint32_t color;
            /* 色板取第一个柱组的实际填充色（对标 Qt 图例展示柱组色）：
             * 柱色在 set 级渐变，序列级 m_color 通常为 0，回退
             * themeColor(k) 会与折线首序列同色。 */
            /* 色板回退链：序列色 → 首柱组色 → 全局序主题色（柱组画刷
               未烘焙时为 0，直用会画成透明）。 */
            {
                XBarSet* set0 = XAbstractBarSeries_barSetAt(&b->m_base, 0);
                uint32_t setColor = set0 ? XBarSet_color(set0) : 0;
                int gi = self->m_chart->m_lineCount +
                         self->m_chart->m_splineCount +
                         self->m_chart->m_areaCount + k;
                color = b->m_color != 0
                    ? b->m_color
                    : (setColor != 0
                           ? setColor
                           : XChart_themeColor(self->m_chart, gi));
            }
            if (xcv_textVisible(dirty, legendR->x, y, 140)) {
                XPainter_fillRect(painter,
                    &(XRect){legendR->x, y, 12, 12}, color);
                XPainter_drawText(painter, legendR->x + 18, y + 10,
                                  XAbstractSeries_name_2(&b->m_base.m_base), text);
            }
            y += 20;
        }
    }
    {   /* 散点图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_scatterCount && y < legendR->y + legendR->height; ++k) {
            XScatterSeries* sc = self->m_chart->m_scatterSeries[k];
            uint32_t color = sc->m_base.m_color != 0
                ? sc->m_base.m_color : xcv_seriesColor(self, sc, k);
            if (xcv_textVisible(dirty, legendR->x, y, 140)) {
                XPainter_fillRect(painter,
                    &(XRect){legendR->x, y, 12, 12}, color);
                XPainter_drawText(painter, legendR->x + 18, y + 10,
                                  XAbstractSeries_name_2(&sc->m_base.m_base), text);
            }
            y += 20;
        }
    }
    {   /* 面积图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_areaCount && y < legendR->y + legendR->height; ++k) {
            XAreaSeries* ar = self->m_chart->m_areaSeries[k];
            uint32_t color = ar->m_color != 0
                ? ar->m_color : xcv_seriesColor(self, ar, k);
            if (xcv_textVisible(dirty, legendR->x, y, 140)) {
                XPainter_fillRect(painter,
                    &(XRect){legendR->x, y, 12, 12}, color);
                XPainter_drawText(painter, legendR->x + 18, y + 10,
                                  XAreaSeries_name_2(ar), text);
            }
            y += 20;
        }
    }
    {   /* 样条图例。 */
        int k;
        for (k = 0; k < self->m_chart->m_splineCount && y < legendR->y + legendR->height; ++k) {
            XSplineSeries* sp = self->m_chart->m_splineSeries[k];
            uint32_t color = sp->m_base.m_color != 0
                ? sp->m_base.m_color : xcv_seriesColor(self, sp, k);
            if (xcv_textVisible(dirty, legendR->x, y, 140)) {
                XPainter_fillRect(painter,
                    &(XRect){legendR->x, y, 12, 12}, color);
                XPainter_drawText(painter, legendR->x + 18, y + 10,
                                  XAbstractSeries_name_2(&sp->m_base.m_base), text);
            }
            y += 20;
        }
    }
    {   /* 饼图图例：逐切片。 */
        XPieSeries* pie = self->m_chart->m_pieSeries;
        int k;
        for (k = 0; pie && k < pie->m_count && y < legendR->y + legendR->height; ++k) {
            XPieSlice* slice = pie->m_slices[k];
            uint32_t color = slice && XPieSlice_color(slice) != 0
                ? XPieSlice_color(slice)
                : XChart_themeColor(self->m_chart, k);
            if (xcv_textVisible(dirty, legendR->x, y, 140)) {
                XPainter_fillRect(painter,
                    &(XRect){legendR->x, y, 12, 12}, color);
                XPainter_drawText(painter, legendR->x + 18, y + 10,
                                  slice ? XPieSlice_label_2(slice) : "", text);
            }
            y += 20;
        }
    }
    /* 根因（R-37）：XFont_deinit_base 此前误放在折线图例循环体内——
     * m_lineCount==0（纯柱/饼图）时循环体永不执行，每次重绘泄漏一份
     * XWidget_font 深拷贝（动画逐帧累积）；且循环多轮时会对同一副本
     * 反复 deinit（二次释放）。对照 xcv_paintTitle/xcv_paintAxes 正确
     * 范式：setFont 持有的副本在函数尾恰好释放一次。 */
    XFont_deinit_base((XClass*)&font);
}

/** @brief 渲染整张图表到图像（paintEvent 与 renderToImage 共用管线）。
 * @param dirty paintEvent 脏区（控件本地坐标）；NULL 表示全图离屏渲染。
 * @details 对标 Qt QWidgetPrivate::drawWidget 在派发 paintEvent 前执行
 *          setSystemClip(toBePainted) 的语义：稳态小脏区刷新（如性能
 *          悬浮层重叠）只重绘脏区内容，不再全图重画。
 *          §10.2 第一期：STATIC_LAYER_ON 时入口先算指纹，命中静态层
 *          直接 blit（受 dirty/clip 约束），未命中重建层再 blit；序列
 *          （lines/spline/area/bars/scatter/pie）照旧现绘，plot 裁剪
 *          语义不变；renderToImage(dirty=NULL) 公共 API 逐位语义不变
 *          （层=五件套 over 全透明，回贴=over 目标既有内容，与直画
 *          同为 source-over 结合律同轮次同舍入）。 */
static bool xcv_renderToImage(XChartView* cv, XImage* image,
                              const XRect* dirty)
{
    XPainter painter;
    XPoint offset;
    XRect titleR;
    XRect plotR;
    XRect legendR;
    XRect bounds;
#if XCHARTVIEW_PROFILE
    int64_t profFpUs = 0;
    int64_t profBlitUs = 0;
    int64_t profRebuildUs = 0;
    int64_t profSeriesUs = 0;
    int64_t profLegendUs = 0;
    int64_t profT = -1;
#endif /* XCHARTVIEW_PROFILE */
#if XCHARTVIEW_STATIC_LAYER_ON
    uint64_t staticFp = 0;
    bool layerHit = false;
#endif /* XCHARTVIEW_STATIC_LAYER_ON */
    if (!cv || !image || !cv->m_chart) return false;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return false;
    }
    offset = XWidget_paintOffset((XWidget*)cv);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPAINTER_CLIP_ON
    if (dirty)
        XPainter_setClipRect(&painter, dirty,
                             XPainterClipOperation_ReplaceClip);
#endif /* XPAINTER_CLIP_ON */
    xcv_layout(cv, &titleR, &plotR, &legendR);
    /* 绘图区同步到模型（对标 QChart::plotArea；变化时发 plotAreaChanged）。 */
    XChart_setPlotArea(cv->m_chart, &(XRectF){ (float)plotR.x, (float)plotR.y,
                                               (float)plotR.width,
                                               (float)plotR.height });
    XRect_init(&bounds, 0, 0, XWidget_width((XWidget*)cv),
               XWidget_height((XWidget*)cv));
#if XCHARTVIEW_STATIC_LAYER_ON
    /* ===== 静态层：指纹命中 blit / 未命中重建（§10.2 Phase B）。 ===== */
    XCV_PROF_BEGIN();
    staticFp = xcv_staticFingerprint(cv);
    XCV_PROF_END(profFpUs);
    /* 层可用前提：背景可见（背景覆盖全控件时层内容与目标既有像素无关，
     * source-over 结合律保证回贴=直画）；背景关闭时直画结果依赖目标
     * 历史像素，层无法复现，保持直画（计时计入 rebuild 段：静态内容
     * 渲染本就是"无层重建"，两配置剖析口径一致）。 */
    layerHit = cv->m_chart->m_backgroundVisible &&
               bounds.width > 0 && bounds.height > 0 &&
               !g_xcvLayerBypass; /* A/B 旁路（回归位一致断言用）。 */
    if (layerHit) {
        /* 入口失效比对：resize 事件即时清 m_staticValid，此处再按指纹
         * 兜底（尺寸/格式变化的离屏场景、模型外观字段变化都在此命中
         * 重建）。 */
        layerHit = cv->m_staticValid && cv->m_staticFp == staticFp &&
                   !XImage_isNull(&cv->m_staticLayer) &&
                   XImage_width(&cv->m_staticLayer) == bounds.width &&
                   XImage_height(&cv->m_staticLayer) == bounds.height &&
                   XImage_format(&cv->m_staticLayer) ==
                       XImage_format(image);
        if (!layerHit) {
            XCV_PROF_BEGIN();
            layerHit = xcv_staticLayerRebuild(cv, &bounds, &titleR, &plotR,
                                              XImage_format(image));
            XCV_PROF_END(profRebuildUs);
            if (layerHit) {
                cv->m_staticFp = staticFp;
                cv->m_staticValid = true;
            }
        }
    }
    if (layerHit) {
        XCV_PROF_BEGIN();
        /* 层在绘制器坐标 (0,0) 整幅落位：绘制器已 translate 控件
         * paintOffset，裁剪处于脏区（受 dirty/clip 约束；全图渲染即
         * 无脏区裁剪整幅 blit）。光栅引擎对平移+矩形裁剪的 drawImage
         * 走按行 blit 快路径。 */
        XPainter_drawImage(&painter, &cv->m_staticLayer, 0, 0);
        XCV_PROF_END(profBlitUs);
    } else
#endif /* XCHARTVIEW_STATIC_LAYER_ON */
    {
        /* 直画回退：STATIC_LAYER=0 时与既有管线逐位一致（五件套提取为
         * 与层重建共用的 xcv_paintStaticContent，操作序列不变）；层
         * 不可用/重建失败同样到达此处。 */
        XCV_PROF_BEGIN();
        xcv_paintStaticContent(cv, &painter, &bounds, &titleR, &plotR,
                               dirty);
        XCV_PROF_END(profRebuildUs);
    }
    /* 序列域裁剪：样条过冲/散点越界不得溢出绘图区（对标 Qt Charts
     * 的 domain 裁剪语义）；与脏区取交集（无脏区时等效原 ReplaceClip）。
     * XPAINTER_CLIP_ON=0 的裁剪构建下不做裁剪。 */
#if XPAINTER_CLIP_ON
    XPainter_setClipRect(&painter, &plotR, XPainterClipOperation_IntersectClip);
#endif /* XPAINTER_CLIP_ON */
    /* 序列段：绘图区五类序列照旧现绘（plot 裁剪语义不变）。 */
    XCV_PROF_BEGIN();
    xcv_paintLines(cv, &painter, &plotR);
    xcv_paintSpline(cv, &painter, &plotR);
    xcv_paintArea(cv, &painter, &plotR);
    xcv_paintBars(cv, &painter, &plotR);
    xcv_paintScatter(cv, &painter, &plotR);
    XCV_PROF_END(profSeriesUs);
    /* 图例+其余段：clip 恢复/饼图/图例/橡皮筋（饼图是数据动态部分且
     * 需绘图域外自由度，归入本段计时）。 */
    XCV_PROF_BEGIN();
#if XPAINTER_CLIP_ON
    /* 恢复脏区裁剪（饼图/图例/橡皮筋不被绘图域限制）；全图渲染保持 NoClip。 */
    if (dirty)
        XPainter_setClipRect(&painter, dirty,
                             XPainterClipOperation_ReplaceClip);
    else
        XPainter_setClipRect(&painter, &plotR,
                             XPainterClipOperation_NoClip);
#endif /* XPAINTER_CLIP_ON */
    if (cv->m_chart->m_pieSeries)
        xcv_paintPie(cv, &painter, &plotR);
    if (cv->m_chart->m_legendVisible)
        xcv_paintLegend(cv, &painter, &legendR, dirty);
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
    XCV_PROF_END(profLegendUs);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#if XCHARTVIEW_PROFILE
    xcv_profileFrame(profFpUs, profBlitUs, profRebuildUs,
                     profSeriesUs, profLegendUs);
#endif /* XCHARTVIEW_PROFILE */
    return true;
}

/** @brief paintEvent：取事件脏区委托离屏渲染管线（脏区外不重绘）。 */
static void VX_chartView_paintEvent(XWidget* self, XEvent* event)
{
    XChartView* cv = (XChartView*)self;
    XImage* image;
    XRect dirty;
    if (!cv || !event) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    if (XEvent_type(event) == XEVENT_TYPE_PAINT) {
        dirty = XPaintEvent_rect((const XPaintEvent*)event);
        xcv_renderToImage(cv, image, &dirty);
    }
    else {
        xcv_renderToImage(cv, image, NULL);
    }
}

bool XChartView_renderToImage(XChartView* self, XImage* image)
{
    return xcv_renderToImage(self, image, NULL);
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
#if XCHARTVIEW_STATIC_LAYER_ON
/** @brief 析构处理（虚表入口，定义见下）。 */
static void VX_chartView_deinit(XChartView* self);
#endif /* XCHARTVIEW_STATIC_LAYER_ON */

/** @brief 尺寸变化：静态层失效（§10.2 失效挂钩；层画布在新尺寸首帧
 *         重建，尺寸/格式比对在 renderToImage 入口兜底）。 */
static void VX_chartView_resizeEvent(XWidget* self, XEvent* event)
{
#if XCHARTVIEW_STATIC_LAYER_ON
    XChartView* cv = (XChartView*)self;
    if (cv) cv->m_staticValid = false;
#else
    (void)self;
#endif /* XCHARTVIEW_STATIC_LAYER_ON */
    (void)event;
}

#if XCHARTVIEW_STATIC_LAYER_ON
/** @brief 析构：释放静态层画布（§8.0g 逐套 deinit 纪律）。 */
static void VX_chartView_deinit(XChartView* self)
{
    if (!self) return;
    XImage_deinit_base(&self->m_staticLayer);
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}
#endif /* XCHARTVIEW_STATIC_LAYER_ON */

XVtable* XChartView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XChartView)    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_chartView_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_chartView_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_chartView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VX_chartView_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VX_chartView_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent, VX_chartView_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VX_chartView_wheelEvent);
#if XCHARTVIEW_STATIC_LAYER_ON
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_chartView_deinit);
#endif /* XCHARTVIEW_STATIC_LAYER_ON */
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

/**
 * @brief 悬停状态在册校验 + 旧悬停点数据坐标恢复（R-102/R-103 合并修复）。
 *
 * 根因：
 * - R-102：悬停离开此前以硬编码 (0.0,0.0) 发射 hovered(point,false)，
 *   接收方无从得知离开点；同序列 A→B 换点也不给旧点发 false。视图
 *   结构已缓存 m_hoverSeries+m_hoverIndex，离开点坐标无需新增字段，
 *   在离开时机按「序列+下标」回查即得（对标 Qt Charts 逐点 hovered
 *   携带离开点坐标的载荷语义）。
 * - R-103：m_hoverSeries 是跨事件借用指针，序列被 removeSeries/
 *   removeAllSeries 删除后无失效联动（XChart 侧无回挂通知），下次
 *   mouseMove 会在已释放对象上发射信号（UAF）。本函数以「在册查找」
 *   兼作有效性自证：序列仍在图表三张 XY 表（line/spline/scatter，
 *   与 xcv_hitTest 同口径）中才返回坐标；查无即已删除，返回 false，
 *   调用方据此跳过离开发射并直接清悬停状态。
 *
 * @param chart  当前图表（悬停借用指针的属主）。
 * @param series 待校验的悬停序列借用指针。
 * @param index  悬停点下标。
 * @param outX   输出点 X（命中时有效）。
 * @param outY   输出点 Y（命中时有效）。
 * @return 序列在册且下标有效返回 true。
 */
static bool xcv_hoverPointData(const XChart* chart,
                               const XAbstractSeries* series, int index,
                               double* outX, double* outY)
{
    int i;
    if (!chart || !series || index < 0) return false;
    for (i = 0; i < chart->m_lineCount; ++i) {
        const XLineSeries* ls = chart->m_lineSeries[i];
        if ((const XAbstractSeries*)&ls->m_base.m_base == series) {
            if (index >= ls->m_base.m_count) return false;
            *outX = ls->m_base.m_points[index].x;
            *outY = ls->m_base.m_points[index].y;
            return true;
        }
    }
    for (i = 0; i < chart->m_splineCount; ++i) {
        const XSplineSeries* ss = chart->m_splineSeries[i];
        if ((const XAbstractSeries*)&ss->m_base.m_base == series) {
            if (index >= ss->m_base.m_count) return false;
            *outX = ss->m_base.m_points[index].x;
            *outY = ss->m_base.m_points[index].y;
            return true;
        }
    }
    for (i = 0; i < chart->m_scatterCount; ++i) {
        const XScatterSeries* cs = chart->m_scatterSeries[i];
        if ((const XAbstractSeries*)&cs->m_base.m_base == series) {
            if (index >= cs->m_base.m_count) return false;
            *outX = cs->m_base.m_points[index].x;
            *outY = cs->m_base.m_points[index].y;
            return true;
        }
    }
    return false;
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
                (size_t)XXYSeries_pressed_signal((XXYSeries*)hit, hx, hy),
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
                (size_t)XXYSeries_pressed_signal((XXYSeries*)hit, hx, hy),
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
        /* 离开载荷（R-102）：先按缓存的下标回查旧悬停点数据坐标；
         * 查询兼作在册校验（R-103）——旧序列已被删除时返回 false，
         * 下面跳过离开发射（不得再触已释放对象），仅清悬停状态。 */
        {
            double leaveX = 0.0;
            double leaveY = 0.0;
            bool haveOld = false;
            if (cv->m_hovering && cv->m_hoverSeries)
                haveOld = xcv_hoverPointData(cv->m_chart,
                                             cv->m_hoverSeries,
                                             cv->m_hoverIndex,
                                             &leaveX, &leaveY);
            if (hit && (hit != cv->m_hoverSeries ||
                        idx != cv->m_hoverIndex || !cv->m_hovering)) {
                /* 同序列 A→B 换点同样先给旧点发 false（此前仅跨序列
                 * 才发，旧点永远收不到离开通知）。 */
                if (haveOld)
                    xcv_emitHoverSignal(cv->m_hoverSeries,
                        (size_t)XXYSeries_hovered_signal((XXYSeries*)cv->m_hoverSeries,
                                                         leaveX, leaveY,
                                                         false),
                        leaveX, leaveY, false);
                cv->m_hoverSeries = hit;
                cv->m_hoverIndex = idx;
                cv->m_hovering = true;
                xcv_emitHoverSignal(hit, (size_t)XXYSeries_hovered_signal((XXYSeries*)hit, hx, hy, true), hx, hy, true);
            } else if (!hit && cv->m_hovering) {
                if (haveOld)
                    xcv_emitHoverSignal(cv->m_hoverSeries,
                        (size_t)XXYSeries_hovered_signal((XXYSeries*)cv->m_hoverSeries,
                                                         leaveX, leaveY,
                                                         false),
                        leaveX, leaveY, false);
                cv->m_hoverSeries = NULL;
                cv->m_hoverIndex = -1;
                cv->m_hovering = false;
            }
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
                (size_t)XXYSeries_clicked_signal((XXYSeries*)hit, hx, hy), hx, hy);
            xcv_emitXySignal(hit,
                (size_t)XXYSeries_released_signal((XXYSeries*)hit, hx, hy), hx, hy);
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
        /* 矩形端点 → 数据域（XChart::mapToValue 的布局内联实现）；
         * 轴 reverse 时按翻转映射取值（与 xcv_mapPoint 互逆），翻转后
         * 端点值序颠倒，setRange 拒绝 min>max，故先归一 min/max。 */
        {
            double v0x = ax->m_base.m_reverse
                ? maxX - (double)(r.x - plotR.x) /
                  (double)plotR.width * rx
                : minX + (double)(r.x - plotR.x) /
                  (double)plotR.width * rx;
            double v1x = ax->m_base.m_reverse
                ? maxX - (double)(r.x + r.width - plotR.x) /
                  (double)plotR.width * rx
                : minX + (double)(r.x + r.width - plotR.x) /
                  (double)plotR.width * rx;
            double v0y = ay->m_base.m_reverse
                ? minY + (double)(r.y - plotR.y) /
                  (double)plotR.height * ry
                : maxY - (double)(r.y - plotR.y) /
                  (double)plotR.height * ry;
            double v1y = ay->m_base.m_reverse
                ? minY + (double)(r.y + r.height - plotR.y) /
                  (double)plotR.height * ry
                : maxY - (double)(r.y + r.height - plotR.y) /
                  (double)plotR.height * ry;
            /* 压栈框选前的当前域（保持 zoomReset/zoomOut 语义一致）。 */
            xcv_pushCurrentDomain(cv);
            XValueAxis_setRange(ax, v0x < v1x ? v0x : v1x,
                                v0x < v1x ? v1x : v0x);
            XValueAxis_setRange(ay, v0y < v1y ? v0y : v1y,
                                v0y < v1y ? v1y : v0y);
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
                (size_t)XXYSeries_doubleClicked_signal((XXYSeries*)hit, hx, hy),
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
#if XCHARTVIEW_STATIC_LAYER_ON
    /* 静态层画布绑定 XImage 类元数据（空图像；首帧渲染按需分配）。 */
    XImage_init(&self->m_staticLayer);
#endif /* XCHARTVIEW_STATIC_LAYER_ON */
    XWidget_resize((XWidget*)self, 320, 240);
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
    /* 根因（R-103）：setChart 删除旧图表（连带全部序列）后，跨事件
     * 借用的 m_hoverSeries 即悬垂——对标 Qt Charts「交互项随序列删除
     * 即时销毁」，删除属主图表时同步清悬停状态；removeSeries 路径的
     * 失效由 mouseMove 侧 xcv_hoverPointData 在册校验兜底。 */
    self->m_hoverSeries = NULL;
    self->m_hoverIndex = -1;
    self->m_hovering = false;
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