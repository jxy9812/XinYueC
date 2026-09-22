#include "XChart.h"

#include "XAlgorithm.h"
#include "XString.h"
#include "XValueAxis.h"
#include "XLineSeries.h"
#include "XPieSeries.h"
#include "XBarSeries.h"
#include "XScatterSeries.h"
#include "XAreaSeries.h"
#include "XSplineSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include <math.h>

#if XCHARTS_ON

/** @brief 泛型序列容量上限（对标 QChart 可挂载序列数；实现为定长数组）。 */
#define XCHART_SERIES_CAPACITY 24

/* ==================== 主题规格（逐字复刻 Qt 6.8.3 qtcharts/src/charts/themes/*.cpp） ==================== */

/** @brief 亮色主题系列色（ChartThemeLight::seriesColors）。 */
static const uint32_t g_themeLight[8] = {
    0xFF209FDFu, 0xFF99CA53u, 0xFFF6A625u, 0xFF6D5FD5u,
    0xFFBF593Eu, 0xFF209FDFu, 0xFF99CA53u, 0xFFF6A625u
};

/** @brief 蔚蓝主题系列色（ChartThemeBlueCerulean::seriesColors）。 */
static const uint32_t g_themeBlueCerulean[8] = {
    0xFFC7E85Bu, 0xFF1CB54Fu, 0xFF5CBF9Bu, 0xFF009FBFu,
    0xFFEE7392u, 0xFFC7E85Bu, 0xFF1CB54Fu, 0xFF5CBF9Bu
};

/** @brief 暗色主题系列色（ChartThemeDark::seriesColors）。 */
static const uint32_t g_themeDark[8] = {
    0xFF38AD6Bu, 0xFF3C84A7u, 0xFFEB8817u, 0xFF7B7F8Cu,
    0xFFBF593Eu, 0xFF38AD6Bu, 0xFF3C84A7u, 0xFFEB8817u
};

/** @brief 棕沙主题系列色（ChartThemeBrownSand::seriesColors）。 */
static const uint32_t g_themeBrownSand[8] = {
    0xFFB39B72u, 0xFFB3B376u, 0xFFC35660u, 0xFF536780u,
    0xFF494345u, 0xFFB39B72u, 0xFFB3B376u, 0xFFC35660u
};

/** @brief NCS 蓝主题系列色（ChartThemeBlueNcs::seriesColors）。 */
static const uint32_t g_themeBlueNcs[8] = {
    0xFF1DB0DAu, 0xFF1341A6u, 0xFF88D41Eu, 0xFFFF8E1Au,
    0xFF398CA3u, 0xFF1DB0DAu, 0xFF1341A6u, 0xFF88D41Eu
};

/** @brief 高对比主题系列色（ChartThemeHighContrast::seriesColors）。 */
static const uint32_t g_themeHighContrast[8] = {
    0xFF202020u, 0xFF596A74u, 0xFFFFAB03u, 0xFF038E9Bu,
    0xFFFF4A41u, 0xFF202020u, 0xFF596A74u, 0xFFFFAB03u
};

/** @brief 冰蓝主题系列色（ChartThemeBlueIcy::seriesColors）。 */
static const uint32_t g_themeBlueIcy[8] = {
    0xFF3DAEDAu, 0xFF2685BFu, 0xFF0C2673u, 0xFF5F3DBAu,
    0xFF2FA3B4u, 0xFF3DAEDAu, 0xFF2685BFu, 0xFF0C2673u
};

/** @brief Qt 经典主题系列色（ChartThemeQt::seriesColors，共 8 色）。 */
static const uint32_t g_themeQt[8] = {
    0xFF80C342u, 0xFF328930u, 0xFF006325u, 0xFF35322Fu,
    0xFF5D5B59u, 0xFF868482u, 0xFFAEADACu, 0xFFD7D6D5u
};

/** @brief 主题规格（对标 ChartTheme 各主题类构造器逐字段）。 */
typedef struct XChartThemeSpec
{
    const uint32_t* series;      /**< 系列默认色循环（对标 seriesColors）。 */
    int seriesCount;             /**< 系列色数量。 */
    uint32_t bgStart;            /**< 背景渐变起点（对标 backgroundGradient stop 0）。 */
    uint32_t bgEnd;              /**< 背景渐变终点（对标 stop 1）。 */
    uint32_t labelBrush;         /**< 标签画刷（对标 labelBrush）。 */
    uint32_t axisLinePen;        /**< 轴线画笔色（对标 axisLinePen）。 */
    int axisLineWidth;           /**< 轴线画笔宽（对标 axisLinePen.width()）。 */
    uint32_t gridPen;            /**< 网格线画笔色（对标 gridLinePen）。 */
    int gridLineWidth;           /**< 网格线画笔宽。 */
    uint32_t minorGridPen;       /**< 次网格线画笔色（对标 minorGridLinePen；虚线）。 */
    int minorGridLineWidth;      /**< 次网格线画笔宽。 */
    uint32_t outlinePen;         /**< 轮廓画笔色（对标 outlinePen）。 */
    int outlineWidth;            /**< 轮廓画笔宽。 */
    uint32_t shadesBrush;        /**< 阴影带画刷色（0=无；对标 backgroundShadesBrush）。 */
    int shadesMode;              /**< 阴影带模式（BackgroundShadesMode）。 */
    bool dropShadow;             /**< 背景投影（对标 isBackgroundDropShadowEnabled）。 */
} XChartThemeSpec;

/** @brief 亮色主题（ChartThemeLight）。 */
static const XChartThemeSpec kThemeLight = {
    g_themeLight, 5,
    0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFF404044u,
    0xFFD6D6D6u, 1,
    0xFFE2E2E2u, 1,
    0xFFE2E2E2u, 1,
    0xFF4D4D4Du, 2,
    0u, 0,
    false
};

/** @brief 蔚蓝主题（ChartThemeBlueCerulean）。 */
static const XChartThemeSpec kThemeBlueCerulean = {
    g_themeBlueCerulean, 5,
    0xFF056189u, 0xFF101A31u,
    0xFFFFFFFFu,
    0xFFD6D6D6u, 2,
    0xFF84A2B0u, 1,
    0xFF84A2B0u, 1,
    0xFFEBEBEBu, 2,
    0u, 0,
    false
};

/** @brief 暗色主题（ChartThemeDark）。 */
static const XChartThemeSpec kThemeDark = {
    g_themeDark, 5,
    0xFF2E303Au, 0xFF121218u,
    0xFFFFFFFFu,
    0xFF86878Cu, 2,
    0xFF86878Cu, 1,
    0xFF86878Cu, 1,
    0xFFD6D6D6u, 2,
    0u, 0,
    false
};

/** @brief 棕沙主题（ChartThemeBrownSand）。 */
static const XChartThemeSpec kThemeBrownSand = {
    g_themeBrownSand, 5,
    0xFFF3ECE0u, 0xFFF3ECE0u,
    0xFF404044u,
    0xFFB5B0A7u, 2,
    0xFFD4CEC3u, 1,
    0xFFD4CEC3u, 1,
    0xFF222222u, 2,
    0u, 0,
    false
};

/** @brief NCS 蓝主题（ChartThemeBlueNcs）。 */
static const XChartThemeSpec kThemeBlueNcs = {
    g_themeBlueNcs, 5,
    0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFF404044u,
    0xFFD6D6D6u, 2,
    0xFFE2E2E2u, 1,
    0xFFE2E2E2u, 1,
    0xFF474747u, 2,
    0u, 0,
    false
};

/** @brief 高对比主题（ChartThemeHighContrast）。 */
static const XChartThemeSpec kThemeHighContrast = {
    g_themeHighContrast, 5,
    0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFF181818u,
    0xFF8C8C8Cu, 2,
    0xFF8C8C8Cu, 1,
    0xFF8C8C8Cu, 1,
    0xFF000000u, 2,
    0xFFFFEECDu, 2,
    true
};

/** @brief 冰蓝主题（ChartThemeBlueIcy）。 */
static const XChartThemeSpec kThemeBlueIcy = {
    g_themeBlueIcy, 5,
    0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFF404044u,
    0xFFD6D6D6u, 2,
    0xFFE2E2E2u, 1,
    0xFFE2E2E2u, 1,
    0xFF474747u, 2,
    0u, 0,
    true
};

/** @brief Qt 经典主题（ChartThemeQt）。 */
static const XChartThemeSpec kThemeQt = {
    g_themeQt, 8,
    0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFF35322Fu,
    0xFFD7D6D5u, 1,
    0xFFD7D6D5u, 1,
    0xFFD7D6D5u, 1,
    0xFF35322Fu, 2,
    0u, 0,
    false
};

/**
 * @brief 按主题 ID 取规格。
 *
 * @param theme 主题 ID；越界按亮色处理。
 * @return 主题规格常量指针。
 */
static const XChartThemeSpec* xchart_themeSpec(XChart_ChartTheme theme)
{
    switch (theme) {
    case XChart_ChartTheme_BlueCerulean: return &kThemeBlueCerulean;
    case XChart_ChartTheme_Dark:         return &kThemeDark;
    case XChart_ChartTheme_BrownSand:    return &kThemeBrownSand;
    case XChart_ChartTheme_BlueNcs:      return &kThemeBlueNcs;
    case XChart_ChartTheme_HighContrast: return &kThemeHighContrast;
    case XChart_ChartTheme_BlueIcy:      return &kThemeBlueIcy;
    case XChart_ChartTheme_Qt:           return &kThemeQt;
    case XChart_ChartTheme_Light:
    default:                             return &kThemeLight;
    }
}

/** @brief 主题背景阴影带模式（对标 ChartTheme::BackgroundShadesMode）。 */
enum XChart_BackgroundShadesMode
{
    XChart_BackgroundShadesNone = 0,       /**< 无阴影带。 */
    XChart_BackgroundShadesVertical = 1,   /**< 垂直阴影带。 */
    XChart_BackgroundShadesHorizontal = 2, /**< 水平阴影带。 */
    XChart_BackgroundShadesBoth = 3        /**< 双向阴影带。 */
};

/**
 * @brief RGB(0-255) → HSV(0-1) 转换（对标 QColor::hsvHueF/hsvSaturationF）。
 *
 * @param r 红分量。
 * @param g 绿分量。
 * @param b 蓝分量。
 * @param h 输出色相（0-1；无色相输出 0）。
 * @param s 输出饱和度（0-1）。
 * @param v 输出明度（0-1）。
 * @return 无返回值。
 */
static void xchart_rgbToHsv(int r, int g, int b, double* h, double* s,
                            double* v)
{
    double rd = (double)r / 255.0;
    double gd = (double)g / 255.0;
    double bd = (double)b / 255.0;
    double max = rd;
    double min = rd;
    double delta;
    if (gd > max) max = gd;
    if (bd > max) max = bd;
    if (gd < min) min = gd;
    if (bd < min) min = bd;
    delta = max - min;
    *v = max;
    if (delta < 1e-12) {
        *h = 0.0;
        *s = 0.0;
        return;
    }
    *s = delta / max;
    if (max == rd)
        *h = 60.0 * fmod((gd - bd) / delta, 6.0);
    else if (max == gd)
        *h = 60.0 * ((bd - rd) / delta + 2.0);
    else
        *h = 60.0 * ((rd - gd) / delta + 4.0);
    if (*h < 0.0) *h += 360.0;
    *h /= 360.0;
}

/**
 * @brief HSV(0-1) → RGB(0-255) 转换（对标 QColor::setHsvF）。
 *
 * @param h 色相（0-1）。
 * @param s 饱和度（0-1）。
 * @param v 明度（0-1）。
 * @param r 输出红。
 * @param g 输出绿。
 * @param b 输出蓝。
 * @return 无返回值。
 */
static void xchart_hsvToRgb(double h, double s, double v, int* r, int* g,
                            int* b)
{
    double hp;
    double c;
    double x;
    double m;
    double rp;
    double gp;
    double bp;
    if (h < 0.0) h = 0.0;
    if (h > 1.0) h = 1.0;
    hp = h * 6.0;
    c = v * s;
    x = c * (1.0 - fabs(fmod(hp, 2.0) - 1.0));
    m = v - c;
    if (hp < 1.0)      { rp = c; gp = x; bp = 0.0; }
    else if (hp < 2.0) { rp = x; gp = c; bp = 0.0; }
    else if (hp < 3.0) { rp = 0.0; gp = c; bp = x; }
    else if (hp < 4.0) { rp = 0.0; gp = x; bp = c; }
    else if (hp < 5.0) { rp = x; gp = 0.0; bp = c; }
    else               { rp = c; gp = 0.0; bp = x; }
    *r = (int)((rp + m) * 255.0 + 0.5);
    *g = (int)((gp + m) * 255.0 + 0.5);
    *b = (int)((bp + m) * 255.0 + 0.5);
}

/**
 * @brief 线性插值取色（对标 ChartThemeManager::colorAt(start, end, pos)）。
 *
 * @param start 起始色 ARGB。
 * @param end   结束色 ARGB。
 * @param pos   插值位置 0-1。
 * @return ARGB 插值色。
 */
static uint32_t xchart_lerpColor(uint32_t start, uint32_t end, double pos)
{
    int sr = (int)((start >> 16) & 0xFFu);
    int sg = (int)((start >> 8) & 0xFFu);
    int sb = (int)(start & 0xFFu);
    int er = (int)((end >> 16) & 0xFFu);
    int eg = (int)((end >> 8) & 0xFFu);
    int eb = (int)(end & 0xFFu);
    int r = (int)((double)sr + ((double)er - (double)sr) * pos + 0.5);
    int g = (int)((double)sg + ((double)eg - (double)sg) * pos + 0.5);
    int b = (int)((double)sb + ((double)eb - (double)sb) * pos + 0.5);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) |
           (uint32_t)b;
}

/**
 * @brief 按主题序列渐变取色（对标 ChartThemeManager::generateSeriesGradients
 *        后 colorAt(gradient, pos)）。
 *
 * @details 渐变三停：0.0=(h,0,1) 白、0.5=基准色、1.0=(h,s,0.25)。
 *
 * @param base 基准色 ARGB。
 * @param pos  渐变位置 0-1。
 * @return ARGB 渐变插值色。
 */
static uint32_t xchart_gradientColor(uint32_t base, double pos)
{
    int r;
    int g;
    int b;
    double h;
    double s;
    double v;
    int wr;
    int wg;
    int wb;
    int dr;
    int dg;
    int db;
    int sr;
    int sg;
    int sb;
    uint32_t white;
    uint32_t dark;
    r = (int)((base >> 16) & 0xFFu);
    g = (int)((base >> 8) & 0xFFu);
    b = (int)(base & 0xFFu);
    xchart_rgbToHsv(r, g, b, &h, &s, &v);
    xchart_hsvToRgb(h, 0.0, 1.0, &wr, &wg, &wb);
    xchart_hsvToRgb(h, s, 0.25, &dr, &dg, &db);
    white = 0xFF000000u | ((uint32_t)wr << 16) | ((uint32_t)wg << 8) |
            (uint32_t)wb;
    dark = 0xFF000000u | ((uint32_t)dr << 16) | ((uint32_t)dg << 8) |
           (uint32_t)db;
    if (pos <= 0.5) {
        return xchart_lerpColor(white, base, pos / 0.5);
    }
    return xchart_lerpColor(base, dark, (pos - 0.5) / 0.5);
}

/**
 * @brief 把主题规格应用到图表模型（色板/背景/标题/投影/绘图区背景）。
 *
 * @param self  目标图表指针。
 * @param theme 主题 ID（已钳位）。
 * @return 无返回值。
 */
static void xchart_applyTheme(XChart* self, XChart_ChartTheme theme)
{
    const XChartThemeSpec* spec;
    int i;
    if (!self) return;
    spec = xchart_themeSpec(theme);
    XMemcpy(self->m_theme, spec->series,
            sizeof(uint32_t) * (size_t)spec->seriesCount);
    for (i = spec->seriesCount; i < 8; ++i)
        self->m_theme[i] = spec->series[i % spec->seriesCount];
    self->m_themeBgStart = spec->bgStart;
    self->m_themeBgEnd = spec->bgEnd;
    self->m_themeLabelBrush = spec->labelBrush;
    self->m_themeAxisLinePen = spec->axisLinePen;
    self->m_themeAxisLineWidth = spec->axisLineWidth;
    self->m_themeGridPen = spec->gridPen;
    self->m_themeGridLineWidth = spec->gridLineWidth;
    self->m_themeMinorGridPen = spec->minorGridPen;
    self->m_themeMinorGridLineWidth = spec->minorGridLineWidth;
    self->m_themeOutlinePen = spec->outlinePen;
    self->m_themeOutlineWidth = spec->outlineWidth;
    self->m_themeShadesBrush = spec->shadesBrush;
    self->m_themeShadesMode = spec->shadesMode;
    self->m_backgroundBrush = 0u;   /* 0=跟随主题渐变（对标 setBackgroundBrush(主题渐变)）。 */
    self->m_backgroundPen = 0u;
    self->m_titleBrush = spec->labelBrush;
    self->m_dropShadowEnabled = spec->dropShadow;
    self->m_plotAreaBackgroundBrush = 0u;
    self->m_plotAreaBackgroundPen = 0u;
    self->m_plotAreaBackgroundVisible = false;
    /* 阴影带可见性（对标 QAbstractAxisPrivate::initializeTheme 的 forced 分支）。 */
    if (self->m_axisX)
        self->m_axisX->m_base.m_shadesVisible =
            (spec->shadesMode == XChart_BackgroundShadesBoth ||
             spec->shadesMode == XChart_BackgroundShadesVertical);
    if (self->m_axisY)
        self->m_axisY->m_base.m_shadesVisible =
            (spec->shadesMode == XChart_BackgroundShadesBoth ||
             spec->shadesMode == XChart_BackgroundShadesHorizontal);
}

/* ==================== 生命周期 ==================== */

static void VXChart_deinit(XChart* self);
static void VXChart_copy(XChart* self, const XChart* other);
static void VXChart_move(XChart* self, XChart* other);

XVtable* XChart_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XChart)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXChart_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXChart_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXChart_move);
    return XVTABLE_DEFAULT;
}

/**
 * @brief 按泛型表释放所有序列（不区分类型）。
 *
 * @param self 目标图表指针。
 * @return 无返回值。
 */
static void xchart_deleteSeriesByType(void* series, XChartSeriesType type)
{
    if (!series) return;
    switch (type) {
    case XChartSeriesType_Line:
        XLineSeries_delete_base((XLineSeries*)series);
        break;
    case XChartSeriesType_Area:
        XAreaSeries_delete_base((XAreaSeries*)series);
        break;
    case XChartSeriesType_Bar:
    case XChartSeriesType_StackedBar:
    case XChartSeriesType_PercentBar:
    case XChartSeriesType_HorizontalBar:
    case XChartSeriesType_HorizontalStackedBar:
    case XChartSeriesType_HorizontalPercentBar:
        XBarSeries_delete_base((XBarSeries*)series);
        break;
    case XChartSeriesType_Pie:
        XPieSeries_delete_base((XPieSeries*)series);
        break;
    case XChartSeriesType_Scatter:
        XScatterSeries_delete_base((XScatterSeries*)series);
        break;
    case XChartSeriesType_Spline:
        XSplineSeries_delete_base((XSplineSeries*)series);
        break;
    default:
        break;
    }
}

void XChart_init(XChart* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    XClassSetVtable(self, XChart);
    self->m_title = XString_create_utf8("Chart");
    self->m_titleFamily = XString_create();
    self->m_locale = XString_create();
    self->m_legendVisible = true;
    self->m_titleVisible = true;
    self->m_axisX = (XValueAxis*)XMalloc_System(sizeof(XValueAxis));
    self->m_axisY = (XValueAxis*)XMalloc_System(sizeof(XValueAxis));
    if (self->m_axisX) XValueAxis_init(self->m_axisX);
    if (self->m_axisY) XValueAxis_init(self->m_axisY);
    self->m_themeId = XChart_ChartTheme_Light;
    xchart_applyTheme(self, XChart_ChartTheme_Light);
    self->m_backgroundVisible = true;
    self->m_animationDuration = 1000;
    self->m_animationEasingCurve = XEasingCurve_Linear;
    self->m_defaultMinX = self->m_axisX ? self->m_axisX->m_base.m_min : 0.0;
    self->m_defaultMaxX = self->m_axisX ? self->m_axisX->m_base.m_max : 10.0;
    self->m_defaultMinY = self->m_axisY ? self->m_axisY->m_base.m_min : 0.0;
    self->m_defaultMaxY = self->m_axisY ? self->m_axisY->m_base.m_max : 10.0;
}

XChart* XChart_create_ex(XMemoryType memory)
{
    XChart* self = (XChart*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XChart_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXChart_deinit(XChart* self)
{
    int i;
    if (!self) return;
    if (self->m_title) {
        XString_delete_base(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_titleFamily) {
        XString_delete_base(self->m_titleFamily);
        self->m_titleFamily = NULL;
    }
    if (self->m_locale) {
        XString_delete_base(self->m_locale);
        self->m_locale = NULL;
    }
    for (i = 0; i < self->m_lineCount; ++i)
        if (self->m_lineSeries[i]) XLineSeries_delete_base(self->m_lineSeries[i]);
    if (self->m_pieSeries) XPieSeries_delete_base(self->m_pieSeries);
    for (i = 0; i < self->m_barCount; ++i)
        if (self->m_barSeries[i]) XBarSeries_delete_base(self->m_barSeries[i]);
    for (i = 0; i < self->m_scatterCount; ++i)
        if (self->m_scatterSeries[i]) XScatterSeries_delete_base(self->m_scatterSeries[i]);
    for (i = 0; i < self->m_areaCount; ++i)
        if (self->m_areaSeries[i]) XAreaSeries_delete_base(self->m_areaSeries[i]);
    for (i = 0; i < self->m_splineCount; ++i)
        if (self->m_splineSeries[i]) XSplineSeries_delete_base(self->m_splineSeries[i]);
    if (self->m_axisX) { XValueAxis_deinit_base(self->m_axisX); XFree_System(self->m_axisX); }
    if (self->m_axisY) { XValueAxis_deinit_base(self->m_axisY); XFree_System(self->m_axisY); }
    if (self->m_zoomStack) XFree_System(self->m_zoomStack);
    self->m_lineCount = 0;
    self->m_barCount = 0;
    self->m_scatterCount = 0;
    self->m_areaCount = 0;
    self->m_splineCount = 0;
    self->m_pieSeries = NULL;
    self->m_axisX = NULL;
    self->m_axisY = NULL;
    self->m_zoomStack = NULL;
    self->m_zoomCount = 0;
    self->m_zoomCapacity = 0;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XChart_deinit(XChart* self)
{
    if (!self) return;
    XChart_deinit_base(self);
}

static void VXChart_copy(XChart* self, const XChart* other)
{
    if (!self || !other || self == other) return;
    /* 序列与缩放栈不可复制（Qt 同语义：QChart 禁用拷贝构造）。 */
    XChart_deinit(self);
    if (XClassIsVtableNull(self)) XChart_init(self);
    XChart_init(self);
    XChart_setTitle(self, XChart_title(other));
    self->m_legendVisible = other->m_legendVisible;
    self->m_titleVisible = other->m_titleVisible;
    XChart_setTheme(self, other->m_themeId);
    XChart_setTitleBrush(self, other->m_titleBrush);
    XChart_setBackgroundBrush(self, other->m_backgroundBrush);
    XChart_setBackgroundPen(self, other->m_backgroundPen);
    self->m_backgroundVisible = other->m_backgroundVisible;
    self->m_dropShadowEnabled = other->m_dropShadowEnabled;
    self->m_backgroundRoundness = other->m_backgroundRoundness;
    self->m_animationOptions = other->m_animationOptions;
    self->m_animationDuration = other->m_animationDuration;
    self->m_animationEasingCurve = other->m_animationEasingCurve;
    self->m_margins = other->m_margins;
    self->m_plotArea = other->m_plotArea;
    self->m_plotAreaBackgroundVisible = other->m_plotAreaBackgroundVisible;
    self->m_plotAreaBackgroundBrush = other->m_plotAreaBackgroundBrush;
    self->m_plotAreaBackgroundPen = other->m_plotAreaBackgroundPen;
    self->m_localizeNumbers = other->m_localizeNumbers;
    if (self->m_locale && other->m_locale)
        XString_assign(self->m_locale, other->m_locale);
    if (self->m_titleFamily && other->m_titleFamily)
        XString_assign(self->m_titleFamily, other->m_titleFamily);
    self->m_titlePixelSize = other->m_titlePixelSize;
    if (self->m_axisX && other->m_axisX)
        XValueAxis_setRange(self->m_axisX, other->m_axisX->m_base.m_min,
                            other->m_axisX->m_base.m_max);
    if (self->m_axisY && other->m_axisY)
        XValueAxis_setRange(self->m_axisY, other->m_axisY->m_base.m_min,
                            other->m_axisY->m_base.m_max);
    self->m_defaultMinX = other->m_defaultMinX;
    self->m_defaultMaxX = other->m_defaultMaxX;
    self->m_defaultMinY = other->m_defaultMinY;
    self->m_defaultMaxY = other->m_defaultMaxY;
}

static void VXChart_move(XChart* self, XChart* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XChart_init(self);
    /* 释放 self 原有动态资源；vtable 与内存属性保留。 */
    VXChart_deinit(self);
    /* 结构体赋值转移全部字段（对象指针所有权移交）。 */
    *self = *other;
    /* other 重置为全新默认对象（与 XChart_init 语义一致）。 */
    XMemset(other, 0, sizeof(XChart));
    XChart_init(other);
    XClassSetVtable(self, XChart);
}

/* ==================== 标题与图例 ==================== */

void XChart_setTitle(XChart* self, const XString* title)
{
    if (!self) return;
    if (!self->m_title) self->m_title = XString_create();
    if (!self->m_title) return;
    if (title)
        XString_assign(self->m_title, title);
    else
        XString_assign_utf8(self->m_title, "");
}
void XChart_setTitle_2(XChart* self, const char* title)
{
    XString* tmp = NULL;
    if (title) {
        tmp = XString_create_utf8(title);
        if (!tmp) return;
    }
    XChart_setTitle(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

const XString* XChart_title(const XChart* self)
{
    return (self && self->m_title) ? self->m_title : NULL;
}
const char* XChart_title_2(const XChart* self)
{
    const char* text;
    if (!self || !self->m_title) return "";
    text = XString_toUtf8(self->m_title);
    return text ? text : "";
}

void XChart_setTitleVisible(XChart* self, bool visible)
{ if (self) self->m_titleVisible = visible; }

bool XChart_isTitleVisible(const XChart* self)
{ return self ? self->m_titleVisible : false; }

void XChart_setLegendVisible(XChart* self, bool visible)
{ if (self) self->m_legendVisible = visible; }

bool XChart_isLegendVisible(const XChart* self)
{ return self ? self->m_legendVisible : false; }

/* ==================== 泛型序列管理 ==================== */

/**
 * @brief 在泛型表中登记序列（去重）。
 *
 * @param self   目标图表指针。
 * @param series 序列指针。
 * @param type   序列类型。
 * @return 无返回值。
 */
static void xchart_registerSeries(XChart* self, void* series,
                                  XChartSeriesType type)
{
    int i;
    if (!self || !series) return;
    /* 防御性边界检查：兑现“去重”契约。同一指针重复入表会使
     * removeSeries/removeAllSeries 对其释放两次（双重释放）。 */
    for (i = 0; i < self->m_seriesCount; ++i)
        if (self->m_series[i] == series) return;
    if (self->m_seriesCount >= XCHART_SERIES_CAPACITY) return;
    self->m_series[self->m_seriesCount] = series;
    self->m_seriesTypes[self->m_seriesCount] = type;
    ++self->m_seriesCount;
}

/**
 * @brief 从泛型注册表摘除序列（memmove 收缩；不释放对象）。
 *
 * P0-2：注册表摘除的唯一路径，供 removeSeries 与 setPieSeries（删除旧
 * 饼图前）共用，保证注册表与被管对象生命周期一致，杜绝悬空登记项。
 *
 * @param self    目标图表指针。
 * @param series  序列指针。
 * @param typeOut 非空时输出被摘除项的类型。
 * @return 摘除成功返回 true；未登记返回 false。
 */
static bool xchart_unregisterSeries(XChart* self, const void* series,
                                    XChartSeriesType* typeOut)
{
    int i;
    if (!self || !series) return false;
    for (i = 0; i < self->m_seriesCount; ++i) {
        if (self->m_series[i] != series) continue;
        if (typeOut) *typeOut = self->m_seriesTypes[i];
        XMemmove(&self->m_series[i], &self->m_series[i + 1],
                 (size_t)(self->m_seriesCount - i - 1) * sizeof(self->m_series[0]));
        XMemmove(&self->m_seriesTypes[i], &self->m_seriesTypes[i + 1],
                 (size_t)(self->m_seriesCount - i - 1) * sizeof(self->m_seriesTypes[0]));
        --self->m_seriesCount;
        /* 防御性边界检查：收缩后清空尾部槽位，注册表不留悬空指针。 */
        self->m_series[self->m_seriesCount] = NULL;
        return true;
    }
    return false;
}

/**
 * @brief 类型化数组去重守卫：同指针已在对应类型数组中时拒绝重复加入。
 *
 * P0-2 随批审查发现的存量缺口：泛型注册表（m_series）已在
 * xchart_registerSeries 去重，但类型化直加入口（addLineSeries 等）对同
 * 指针重复加入不去重——xchart_unlinkSeries 每次只摘每类数组的一个副本，
 * removeSeries 会残留悬空项。各类型化入口在本守卫后追加。
 *
 * @return true=可继续加入；false=同指针已存在（跳过）。
 */
static bool xchart_typeArrayContains(const void* const* array, int count,
                                     const void* series)
{
    int i;
    for (i = 0; i < count; ++i)
        if (array[i] == series) return true;
    return false;
}

void XChart_addSeries(XChart* self, void* series, XChartSeriesType type)
{
    if (!self || !series) return;
    switch (type) {
    case XChartSeriesType_Line:
        XChart_addLineSeries(self, (XLineSeries*)series);
        break;
    case XChartSeriesType_Area:
        XChart_addAreaSeries(self, (XAreaSeries*)series);
        break;
    case XChartSeriesType_Bar:
    case XChartSeriesType_StackedBar:
    case XChartSeriesType_PercentBar:
    case XChartSeriesType_HorizontalBar:
    case XChartSeriesType_HorizontalStackedBar:
    case XChartSeriesType_HorizontalPercentBar:
        XChart_addBarSeries(self, (XBarSeries*)series);
        break;
    case XChartSeriesType_Pie:
        XChart_setPieSeries(self, (XPieSeries*)series);
        break;
    case XChartSeriesType_Scatter:
        XChart_addScatterSeries(self, (XScatterSeries*)series);
        break;
    case XChartSeriesType_Spline:
        XChart_addSplineSeries(self, (XSplineSeries*)series);
        break;
    default:
        break;
    }
    xchart_registerSeries(self, series, type);
}

/**
 * @brief 从按类集合中摘除序列指针（不释放）。
 *
 * @param self   目标图表指针。
 * @param series 序列指针。
 * @return 无返回值。
 */
static void xchart_unlinkSeries(XChart* self, void* series)
{
    int i;
    if (!self || !series) return;
    for (i = 0; i < self->m_lineCount; ++i) {
        if ((void*)self->m_lineSeries[i] != series) continue;
        XMemmove(&self->m_lineSeries[i], &self->m_lineSeries[i + 1],
                (size_t)(self->m_lineCount - i - 1) * sizeof(self->m_lineSeries[0]));
        self->m_lineSeries[--self->m_lineCount] = NULL;
        return;
    }
    for (i = 0; i < self->m_barCount; ++i) {
        if ((void*)self->m_barSeries[i] != series) continue;
        XMemmove(&self->m_barSeries[i], &self->m_barSeries[i + 1],
                (size_t)(self->m_barCount - i - 1) * sizeof(self->m_barSeries[0]));
        self->m_barSeries[--self->m_barCount] = NULL;
        return;
    }
    for (i = 0; i < self->m_scatterCount; ++i) {
        if ((void*)self->m_scatterSeries[i] != series) continue;
        XMemmove(&self->m_scatterSeries[i], &self->m_scatterSeries[i + 1],
                (size_t)(self->m_scatterCount - i - 1) * sizeof(self->m_scatterSeries[0]));
        self->m_scatterSeries[--self->m_scatterCount] = NULL;
        return;
    }
    for (i = 0; i < self->m_areaCount; ++i) {
        if ((void*)self->m_areaSeries[i] != series) continue;
        XMemmove(&self->m_areaSeries[i], &self->m_areaSeries[i + 1],
                (size_t)(self->m_areaCount - i - 1) * sizeof(self->m_areaSeries[0]));
        self->m_areaSeries[--self->m_areaCount] = NULL;
        return;
    }
    for (i = 0; i < self->m_splineCount; ++i) {
        if ((void*)self->m_splineSeries[i] != series) continue;
        XMemmove(&self->m_splineSeries[i], &self->m_splineSeries[i + 1],
                (size_t)(self->m_splineCount - i - 1) * sizeof(self->m_splineSeries[0]));
        self->m_splineSeries[--self->m_splineCount] = NULL;
        return;
    }
    if ((void*)self->m_pieSeries == series)
        self->m_pieSeries = NULL;
}

void XChart_removeSeries(XChart* self, void* series)
{
    XChartSeriesType type = XChartSeriesType_Line;
    int found;
    if (!self || !series) return;
    /* 先从泛型注册表摘除（P0-2：唯一摘除路径，memmove 收缩 + 尾部清空）。 */
    found = xchart_unregisterSeries(self, series, &type) ? 1 : 0;
    xchart_unlinkSeries(self, series);
    if (found) xchart_deleteSeriesByType(series, type);
}

void XChart_removeAllSeries(XChart* self)
{
    if (!self) return;
    while (self->m_seriesCount > 0) {
        void* s = self->m_series[0];
        if (!s) {
            /* 防御性边界检查：空槽位直接收缩注册表，避免 removeSeries(NULL)
             * 空转导致死循环。 */
            XMemmove(&self->m_series[0], &self->m_series[1],
                     (size_t)(self->m_seriesCount - 1) * sizeof(self->m_series[0]));
            XMemmove(&self->m_seriesTypes[0], &self->m_seriesTypes[1],
                     (size_t)(self->m_seriesCount - 1) * sizeof(self->m_seriesTypes[0]));
            --self->m_seriesCount;
            self->m_series[self->m_seriesCount] = NULL;
            continue;
        }
        XChart_removeSeries(self, s);
    }
}

int XChart_seriesCount(const XChart* self)
{ return self ? self->m_seriesCount : 0; }

void* XChart_seriesAt(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_seriesCount) return NULL;
    return self->m_series[index];
}

XChartSeriesType XChart_seriesTypeAt(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_seriesCount)
        return XChartSeriesType_Line;
    return self->m_seriesTypes[index];
}

/* ==================== 坐标轴 ==================== */

void XChart_setAxisX(XChart* self, XValueAxis* axis)
{
    if (!self || !axis || axis == self->m_axisX) return;
    if (self->m_axisX) { XValueAxis_deinit_base(self->m_axisX); XFree_System(self->m_axisX); }
    self->m_axisX = axis;
}

void XChart_setAxisY(XChart* self, XValueAxis* axis)
{
    if (!self || !axis || axis == self->m_axisY) return;
    if (self->m_axisY) { XValueAxis_deinit_base(self->m_axisY); XFree_System(self->m_axisY); }
    self->m_axisY = axis;
}

XValueAxis* XChart_axisX(const XChart* self) { return self ? self->m_axisX : NULL; }
XValueAxis* XChart_axisY(const XChart* self) { return self ? self->m_axisY : NULL; }

void XChart_createDefaultAxes(XChart* self)
{
    if (!self) return;
    if (self->m_axisX)
        XValueAxis_setRange(self->m_axisX, self->m_defaultMinX,
                            self->m_defaultMaxX);
    else
        XValueAxis_setRange(self->m_axisX, 0.0, 10.0);
    if (self->m_axisY)
        XValueAxis_setRange(self->m_axisY, self->m_defaultMinY,
                            self->m_defaultMaxY);
    else
        XValueAxis_setRange(self->m_axisY, 0.0, 10.0);
    self->m_zoomCount = 0;
}

/* ==================== 主题与外观 ==================== */

/**
 * @brief 查询序列在泛型表中的全局下标（对标 ThemeManager::createIndexKey）。
 *
 * @param self   目标图表指针。
 * @param series 序列指针。
 * @return 全局下标；未登记返回 -1。
 */
static int xchart_seriesGlobalIndex(const XChart* self, const void* series)
{
    int i;
    int g;
    if (!self || !series) return -1;
    /* 泛型 addSeries 路径：直接查注册表。 */
    for (i = 0; i < self->m_seriesCount; ++i)
        if (self->m_series[i] == series) return i;
    /* 类型化 addXxxSeries 路径（不入注册表，C1 契约）：按固定类型顺序
     * 累计位置推全局序号，保证主题色跨类型连续分配（对标 Qt Charts
     * 按 series 全局序取主题色，修"每类型首序列同色"）。 */
    g = 0;
    for (i = 0; i < self->m_lineCount; ++i) {
        if (self->m_lineSeries[i] == series) return g;
        ++g;
    }
    for (i = 0; i < self->m_splineCount; ++i) {
        if (self->m_splineSeries[i] == series) return g;
        ++g;
    }
    for (i = 0; i < self->m_areaCount; ++i) {
        if (self->m_areaSeries[i] == series) return g;
        ++g;
    }
    for (i = 0; i < self->m_barCount; ++i) {
        if (self->m_barSeries[i] == series) return g;
        ++g;
    }
    for (i = 0; i < self->m_scatterCount; ++i) {
        if (self->m_scatterSeries[i] == series) return g;
        ++g;
    }
    return -1;
}

/**
 * @brief 柱组主题色计算（逐字复刻 QAbstractBarSeriesPrivate::initializeTheme
 *        的取色/取位算法）。
 *
 * @param self       目标图表指针。
 * @param series     目标柱状序列。
 * @param seriesIndex 序列全局下标。
 * @param setIndex   柱组下标。
 * @param takeAtPos  输出当前渐变取位。
 * @return ARGB 柱组画刷色。
 */
static uint32_t xchart_barSetThemeColor(XChart* self,
                                        XAbstractBarSeries* series,
                                        int seriesIndex, int setIndex,
                                        double* takeAtPos)
{
    const XChartThemeSpec* spec = xchart_themeSpec(
        self ? self->m_themeId : XChart_ChartTheme_Light);
    int gradientCount = spec->seriesCount;
    int setCount = series ? series->m_barSetCount : 0;
    int actualIndex = 0;
    int firstSeriesSetCount = setCount;
    int lowestSeries = seriesIndex;
    double step;
    int colorIndex;
    int si;
    /* 其它柱状序列对本序列取色的影响（对标 Qt 的系列计数补偿）。 */
    if (self) {
        for (si = 0; si < self->m_barCount; ++si) {
            XBarSeries* other = self->m_barSeries[si];
            int otherIndex;
            if (!other || (XAbstractBarSeries*)other == series) continue;
            otherIndex = xchart_seriesGlobalIndex(self, other);
            if (otherIndex < 0) continue;
            if (otherIndex == seriesIndex) continue;
            actualIndex += XAbstractBarSeries_count(
                (XAbstractBarSeries*)&other->m_base);
            if (otherIndex < lowestSeries) {
                firstSeriesSetCount =
                    XAbstractBarSeries_count((XAbstractBarSeries*)&other->m_base);
                if (firstSeriesSetCount < gradientCount)
                    firstSeriesSetCount = gradientCount;
                lowestSeries = otherIndex;
            }
        }
    }
    if (takeAtPos) *takeAtPos = 0.5;
    step = 0.2;
    if (firstSeriesSetCount > 1) {
        step = 1.0 / (double)firstSeriesSetCount;
        if (firstSeriesSetCount % gradientCount)
            step *= (double)gradientCount;
        else
            step *= (double)(gradientCount - 1);
        if (seriesIndex > 0) {
            int initialStepper = actualIndex;
            while (initialStepper > gradientCount) {
                initialStepper -= gradientCount;
                if (takeAtPos) *takeAtPos += step;
                if (takeAtPos && *takeAtPos == 1.0) *takeAtPos += step;
                if (takeAtPos) *takeAtPos -= (int)*takeAtPos;
            }
        }
    }
    colorIndex = (actualIndex + setIndex) % gradientCount;
    if ((actualIndex + setIndex) > 0 &&
        (actualIndex + setIndex) % gradientCount == 0) {
        if (takeAtPos) *takeAtPos += step;
        if (takeAtPos && *takeAtPos == 1.0) *takeAtPos += step;
        if (takeAtPos) *takeAtPos -= (int)*takeAtPos;
    }
    {
        uint32_t base = spec->series[colorIndex];
        double pos = takeAtPos ? *takeAtPos : 0.5;
        return xchart_gradientColor(base, pos);
    }
}

/**
 * @brief 把当前主题应用到已有序列（对标 ChartThemeManager::setTheme 中
 *        对 series 逐个 initializeTheme(index, theme, forced=true)）。
 *
 * @param self 目标图表指针。
 * @return 无返回值。
 */
static void xchart_applyThemeToSeries(XChart* self)
{
    int si;
    int gi;
    if (!self) return;
    /* 折线：颜色=色板，点标签色=主题标签画刷。 */
    for (si = 0; si < self->m_lineCount; ++si) {
        XLineSeries* s = self->m_lineSeries[si];
        gi = xchart_seriesGlobalIndex(self, s);
        if (gi < 0) gi = si;
        if (s->m_base.m_color == 0)
            XXYSeries_setColor(&s->m_base,
                XChart_themeColor(self, gi));
        if (s->m_base.m_pointLabelsColor == 0)
            XXYSeries_setPointLabelsColor(&s->m_base,
                self->m_themeLabelBrush);
    }
    /* 样条：同折线。 */
    for (si = 0; si < self->m_splineCount; ++si) {
        XSplineSeries* s = self->m_splineSeries[si];
        gi = xchart_seriesGlobalIndex(self, s);
        if (gi < 0) gi = si;
        if (s->m_base.m_color == 0)
            XXYSeries_setColor(&s->m_base,
                XChart_themeColor(self, gi));
        if (s->m_base.m_pointLabelsColor == 0)
            XXYSeries_setPointLabelsColor(&s->m_base,
                self->m_themeLabelBrush);
    }
    /* 散点：画笔=渐变起点（白），画刷=色板；点标签色=主题标签画刷。 */
    for (si = 0; si < self->m_scatterCount; ++si) {
        XScatterSeries* s = self->m_scatterSeries[si];
        gi = xchart_seriesGlobalIndex(self, s);
        if (gi < 0) gi = si;
        if (s->m_base.m_color == 0)
            XXYSeries_setColor(&s->m_base,
                XChart_themeColor(self, gi));
        if (s->m_base.m_brush == 0)
            XXYSeries_setBrush(&s->m_base,
                XChart_themeColor(self, gi));
        if (s->m_base.m_pointLabelsColor == 0)
            XXYSeries_setPointLabelsColor(&s->m_base,
                self->m_themeLabelBrush);
    }
    /* 面积：画刷=色板，边框=渐变起点，点标签色=主题标签画刷。 */
    for (si = 0; si < self->m_areaCount; ++si) {
        XAreaSeries* s = self->m_areaSeries[si];
        gi = xchart_seriesGlobalIndex(self, s);
        if (gi < 0) gi = si;
        if (s->m_color == 0)
            XAreaSeries_setColor(s, XChart_themeColor(self, gi));
        if (s->m_brushColor == 0)
            XAreaSeries_setBrush(s, XChart_themeColor(self, gi));
        if (s->m_borderColor == 0)
            XAreaSeries_setBorderColor(s,
                XChart_themeGradientColor(self, gi, 0.0));
        if (s->m_pointLabelsColor == 0)
            XAreaSeries_setPointLabelsColor(s, self->m_themeLabelBrush);
    }
    /* 柱状：逐柱组按 Qt 算法取色（画刷/标签画刷/画笔）。 */
    for (si = 0; si < self->m_barCount; ++si) {
        XBarSeries* s = self->m_barSeries[si];
        XAbstractBarSeries* abs = (XAbstractBarSeries*)&s->m_base;
        int setIndex;
        gi = xchart_seriesGlobalIndex(self, s);
        if (gi < 0) gi = si;
        for (setIndex = 0; setIndex < abs->m_barSetCount; ++setIndex) {
            XBarSet* set = abs->m_barSets[setIndex];
            double takeAtPos = 0.5;
            uint32_t brushColor;
            uint32_t penColor;
            uint32_t labelColor;
            if (!set) continue;
            brushColor = xchart_barSetThemeColor(self, abs, gi, setIndex,
                                                 &takeAtPos);
            penColor = xchart_gradientColor(
                XChart_themeColor(self, gi % 8), 0.0);
            if (takeAtPos < 0.3)
                labelColor = xchart_gradientColor(
                    XChart_themeColor(self, gi % 8), 1.0);
            else
                labelColor = xchart_gradientColor(
                    XChart_themeColor(self, gi % 8), 0.0);
            if (XBarSet_brush(set) == 0)
                XBarSet_setBrush(set, brushColor);
            if (XBarSet_labelBrush(set) == 0)
                XBarSet_setLabelBrush(set, labelColor);
            {
                uint32_t pc = 0;
                double pw = 0;
                XBarSet_pen(set, &pc, &pw);
                if (pc == 0)
                    XBarSet_setPen(set, penColor,
                        pw > 0 ? pw : 2.0);
            }
        }
        if (s->m_color == 0 && abs->m_barSetCount > 0 &&
            abs->m_barSets[0])
            s->m_color = XBarSet_brush(abs->m_barSets[0]);
    }
    /* 饼图：画笔=渐变起点，画刷按切片序号取渐变，标签画刷=主题标签。 */
    if (self->m_pieSeries) {
        XPieSeries* pie = self->m_pieSeries;
        int sliceIndex;
        gi = xchart_seriesGlobalIndex(self, pie);
        if (gi < 0) gi = 0;
        for (sliceIndex = 0; sliceIndex < pie->m_count; ++sliceIndex) {
            XPieSlice* slice = pie->m_slices[sliceIndex];
            double pos;
            uint32_t pc = 0;
            double pw = 0;
            if (!slice) continue;
            pos = (double)(sliceIndex + 1) / (double)pie->m_count;
            XPieSlice_pen(slice, &pc, &pw);
            if (pc == 0)
                XPieSlice_setPen(slice,
                    XChart_themeGradientColor(self, gi, 0.0),
                    pw > 0 ? pw : 1.0);
            if (XPieSlice_brush(slice) == 0)
                XPieSlice_setBrush(slice,
                    XChart_themeGradientColor(self, gi, pos));
            if (XPieSlice_labelBrush(slice) == 0)
                XPieSlice_setLabelBrush(slice,
                    self->m_themeLabelBrush);
        }
    }
}

void XChart_setTheme(XChart* self, XChart_ChartTheme theme)
{
    if (!self) return;
    if ((int)theme < 0 || theme > XChart_ChartTheme_Qt)
        theme = XChart_ChartTheme_Light;
    self->m_themeId = theme;
    xchart_applyTheme(self, theme);
    xchart_applyThemeToSeries(self);
}

XChart_ChartTheme XChart_theme(const XChart* self)
{ return self ? self->m_themeId : XChart_ChartTheme_Light; }

void XChart_setTitleFont(XChart* self, const XString* family, int pixelSize)
{
    if (!self) return;
    if (!self->m_titleFamily) self->m_titleFamily = XString_create();
    if (!self->m_titleFamily) return;
    if (family)
        XString_assign(self->m_titleFamily, family);
    else
        XString_assign_utf8(self->m_titleFamily, "");
    self->m_titlePixelSize = pixelSize;
}
void XChart_setTitleFont_2(XChart* self, const char* family, int pixelSize)
{
    XString* tmp = NULL;
    if (family) {
        tmp = XString_create_utf8(family);
        if (!tmp) return;
    }
    XChart_setTitleFont(self, tmp, pixelSize);
    if (tmp) XString_delete_base(tmp);
}

const XString* XChart_titleFontFamily(const XChart* self)
{
    return (self && self->m_titleFamily) ? self->m_titleFamily : NULL;
}
const char* XChart_titleFontFamily_2(const XChart* self)
{
    const char* text;
    if (!self || !self->m_titleFamily) return "";
    text = XString_toUtf8(self->m_titleFamily);
    return text ? text : "";
}

int XChart_titlePixelSize(const XChart* self)
{ return self ? self->m_titlePixelSize : 0; }

void XChart_setTitleBrush(XChart* self, uint32_t brush)
{ if (self) self->m_titleBrush = brush; }

uint32_t XChart_titleBrush(const XChart* self)
{ return self ? self->m_titleBrush : 0u; }

void XChart_setBackgroundBrush(XChart* self, uint32_t brush)
{ if (self) self->m_backgroundBrush = brush; }

uint32_t XChart_backgroundBrush(const XChart* self)
{ return self ? self->m_backgroundBrush : 0u; }

void XChart_setBackgroundPen(XChart* self, uint32_t pen)
{ if (self) self->m_backgroundPen = pen; }

uint32_t XChart_backgroundPen(const XChart* self)
{ return self ? self->m_backgroundPen : 0u; }

void XChart_setBackgroundVisible(XChart* self, bool visible)
{ if (self) self->m_backgroundVisible = visible; }

bool XChart_isBackgroundVisible(const XChart* self)
{ return self ? self->m_backgroundVisible : false; }

void XChart_setDropShadowEnabled(XChart* self, bool enabled)
{ if (self) self->m_dropShadowEnabled = enabled; }

bool XChart_isDropShadowEnabled(const XChart* self)
{ return self ? self->m_dropShadowEnabled : false; }

void XChart_setBackgroundRoundness(XChart* self, float diameter)
{ if (self) self->m_backgroundRoundness = diameter; }

float XChart_backgroundRoundness(const XChart* self)
{ return self ? self->m_backgroundRoundness : 0.0f; }

/* ==================== 动画 ==================== */

void XChart_setAnimationOptions(XChart* self, XChart_AnimationOptions options)
{ if (self) self->m_animationOptions = options; }

XChart_AnimationOptions XChart_animationOptions(const XChart* self)
{ return self ? self->m_animationOptions : 0u; }

void XChart_setAnimationDuration(XChart* self, int msecs)
{ if (self && msecs >= 0) self->m_animationDuration = msecs; }

int XChart_animationDuration(const XChart* self)
{ return self ? self->m_animationDuration : 0; }

void XChart_setAnimationEasingCurve(XChart* self, XEasingCurve_Type type)
{ if (self) self->m_animationEasingCurve = (int)type; }

XEasingCurve_Type XChart_animationEasingCurve(const XChart* self)
{
    if (!self) return XEasingCurve_Linear;
    if (self->m_animationEasingCurve < 0 ||
        self->m_animationEasingCurve > (int)XEasingCurve_CosineCurve)
        return XEasingCurve_Linear;
    return (XEasingCurve_Type)self->m_animationEasingCurve;
}

/* ==================== 缩放与滚动 ==================== */

/**
 * @brief 把当前两轴域推入缩放栈。
 *
 * @param self 目标图表指针。
 * @return 压栈成功返回 true；容量分配失败返回 false。
 */
static bool xchart_pushZoom(XChart* self)
{
    XRectF entry;
    if (!self || !self->m_axisX || !self->m_axisY) return false;
    if (self->m_zoomCount >= self->m_zoomCapacity) {
        int newCap = self->m_zoomCapacity > 0 ? self->m_zoomCapacity * 2 : 8;
        XRectF* grown = (XRectF*)XMalloc_System(sizeof(XRectF) * (size_t)newCap);
        if (!grown) return false;
        if (self->m_zoomStack) {
            XMemcpy(grown, self->m_zoomStack,
                   sizeof(XRectF) * (size_t)self->m_zoomCapacity);
            XFree_System(self->m_zoomStack);
        }
        self->m_zoomStack = grown;
        self->m_zoomCapacity = newCap;
    }
    entry.x = (float)self->m_axisX->m_base.m_min;
    entry.y = (float)self->m_axisY->m_base.m_min;
    entry.width = (float)(self->m_axisX->m_base.m_max - self->m_axisX->m_base.m_min);
    entry.height = (float)(self->m_axisY->m_base.m_max - self->m_axisY->m_base.m_min);
    self->m_zoomStack[self->m_zoomCount++] = entry;
    return true;
}

void XChart_zoomIn(XChart* self)
{
    XChart_zoom(self, 2.0);
}

void XChart_zoom(XChart* self, double factor)
{
    double cx;
    double cy;
    double hw;
    double hh;
    if (!self || !self->m_axisX || !self->m_axisY) return;
    if (factor <= 0.0) return;
    if (self->m_axisX->m_base.m_max - self->m_axisX->m_base.m_min <= 0.0) return;
    if (self->m_axisY->m_base.m_max - self->m_axisY->m_base.m_min <= 0.0) return;
    if (!xchart_pushZoom(self)) return;
    cx = (self->m_axisX->m_base.m_min + self->m_axisX->m_base.m_max) / 2.0;
    cy = (self->m_axisY->m_base.m_min + self->m_axisY->m_base.m_max) / 2.0;
    hw = (self->m_axisX->m_base.m_max - self->m_axisX->m_base.m_min) / 2.0 / factor;
    hh = (self->m_axisY->m_base.m_max - self->m_axisY->m_base.m_min) / 2.0 / factor;
    XValueAxis_setRange(self->m_axisX, cx - hw, cx + hw);
    XValueAxis_setRange(self->m_axisY, cy - hh, cy + hh);
}

void XChart_zoomOut(XChart* self)
{
    XRectF entry;
    if (!self || self->m_zoomCount <= 0) return;
    entry = self->m_zoomStack[--self->m_zoomCount];
    if (self->m_axisX)
        XValueAxis_setRange(self->m_axisX, entry.x, entry.x + entry.width);
    if (self->m_axisY)
        XValueAxis_setRange(self->m_axisY, entry.y, entry.y + entry.height);
}

void XChart_zoomReset(XChart* self)
{
    if (!self) return;
    self->m_zoomCount = 0;
    if (self->m_axisX)
        XValueAxis_setRange(self->m_axisX, self->m_defaultMinX,
                            self->m_defaultMaxX);
    if (self->m_axisY)
        XValueAxis_setRange(self->m_axisY, self->m_defaultMinY,
                            self->m_defaultMaxY);
}

bool XChart_isZoomed(const XChart* self)
{ return self && self->m_zoomCount > 0; }

void XChart_scroll(XChart* self, double dx, double dy)
{
    double sx;
    double sy;
    if (!self || !self->m_axisX || !self->m_axisY) return;
    sx = (self->m_axisX->m_base.m_max - self->m_axisX->m_base.m_min) * dx;
    sy = (self->m_axisY->m_base.m_max - self->m_axisY->m_base.m_min) * dy;
    if (sx == 0.0 && sy == 0.0) return;
    xchart_pushZoom(self);
    XValueAxis_setRange(self->m_axisX, self->m_axisX->m_base.m_min + sx,
                        self->m_axisX->m_base.m_max + sx);
    XValueAxis_setRange(self->m_axisY, self->m_axisY->m_base.m_min + sy,
                        self->m_axisY->m_base.m_max + sy);
}

/* ==================== 边距与绘图区 ==================== */

void XChart_setMargins(XChart* self, int left, int top, int right, int bottom)
{
    if (!self) return;
    self->m_margins.left = left;
    self->m_margins.top = top;
    self->m_margins.right = right;
    self->m_margins.bottom = bottom;
}

XMargins XChart_margins(const XChart* self)
{
    XMargins m;
    if (!self) {
        XMargins_init(&m, 0, 0, 0, 0);
        return m;
    }
    return self->m_margins;
}

XRectF XChart_plotArea(const XChart* self)
{
    XRectF r;
    XMemset(&r, 0, sizeof(r));
    return self ? self->m_plotArea : r;
}

void XChart_setPlotArea(XChart* self, const XRectF* rect)
{
    if (!self || !rect) return;
    if (self->m_plotArea.x == rect->x && self->m_plotArea.y == rect->y &&
        self->m_plotArea.width == rect->width &&
        self->m_plotArea.height == rect->height)
        return;
    self->m_plotArea = *rect;
    XChart_plotAreaChanged_signal(self);
}

void XChart_setPlotAreaBackgroundBrush(XChart* self, uint32_t brush)
{ if (self) self->m_plotAreaBackgroundBrush = brush; }

uint32_t XChart_plotAreaBackgroundBrush(const XChart* self)
{ return self ? self->m_plotAreaBackgroundBrush : 0u; }

void XChart_setPlotAreaBackgroundPen(XChart* self, uint32_t pen)
{ if (self) self->m_plotAreaBackgroundPen = pen; }

uint32_t XChart_plotAreaBackgroundPen(const XChart* self)
{ return self ? self->m_plotAreaBackgroundPen : 0u; }

void XChart_setPlotAreaBackgroundVisible(XChart* self, bool visible)
{ if (self) self->m_plotAreaBackgroundVisible = visible; }

bool XChart_isPlotAreaBackgroundVisible(const XChart* self)
{ return self ? self->m_plotAreaBackgroundVisible : false; }

/* ==================== 本地化与区域 ==================== */

void XChart_setLocalizeNumbers(XChart* self, bool localize)
{ if (self) self->m_localizeNumbers = localize; }

bool XChart_localizeNumbers(const XChart* self)
{ return self ? self->m_localizeNumbers : false; }

void XChart_setLocale(XChart* self, const XString* locale)
{
    if (!self) return;
    if (!self->m_locale) self->m_locale = XString_create();
    if (!self->m_locale) return;
    if (locale)
        XString_assign(self->m_locale, locale);
    else
        XString_assign_utf8(self->m_locale, "");
}
void XChart_setLocale_2(XChart* self, const char* locale)
{
    XString* tmp = NULL;
    if (locale) {
        tmp = XString_create_utf8(locale);
        if (!tmp) return;
    }
    XChart_setLocale(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

const XString* XChart_locale(const XChart* self)
{
    return (self && self->m_locale) ? self->m_locale : NULL;
}
const char* XChart_locale_2(const XChart* self)
{
    const char* text;
    if (!self || !self->m_locale) return "";
    text = XString_toUtf8(self->m_locale);
    return text ? text : "";
}

/* ==================== 坐标映射与类型 ==================== */

void XChart_mapToValue(const XChart* self, float x, float y,
                       double* outX, double* outY)
{
    const XRectF* area;
    double rx;
    double ry;
    if (outX) *outX = 0.0;
    if (outY) *outY = 0.0;
    if (!self || !self->m_axisX || !self->m_axisY) return;
    area = &self->m_plotArea;
    if (area->width <= 0.0f || area->height <= 0.0f) return;
    rx = self->m_axisX->m_base.m_max - self->m_axisX->m_base.m_min;
    ry = self->m_axisY->m_base.m_max - self->m_axisY->m_base.m_min;
    if (outX)
        *outX = self->m_axisX->m_base.m_min +
                (double)((x - area->x) / area->width) * rx;
    if (outY)
        *outY = self->m_axisY->m_base.m_max -
                (double)((y - area->y) / area->height) * ry;
}

XChart_ChartType XChart_chartType(const XChart* self)
{
    (void)self;
    return XChart_ChartType_Cartesian;
}

/* ==================== 信号 ==================== */

void* XChart_plotAreaChanged_signal(XChart* self)
{
    if (!self)
        return (void*)(size_t)XChart_plotAreaChanged_signal;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XChart_plotAreaChanged_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XChart_plotAreaChanged_signal;
}

/* ==================== 兼容层 ==================== */

void XChart_addLineSeries(XChart* self, XLineSeries* series)
{
    if (!self || !series || self->m_lineCount >= 8) return;
    if (xchart_typeArrayContains((const void* const*)self->m_lineSeries,
                                 self->m_lineCount, series)) return;
    self->m_lineSeries[self->m_lineCount++] = series;
}

void XChart_setPieSeries(XChart* self, XPieSeries* series)
{
    if (!self) return;
    if (self->m_pieSeries && self->m_pieSeries != series) {
        /* P0-2 根因：泛型 addSeries(Pie) 会把旧饼图登记进 m_series 注册表，
         * 此处删除旧饼图前必须同步摘除注册项（参照 removeSeries 的 memmove
         * 收缩逻辑），否则注册表残留悬空指针，removeSeries/removeAllSeries
         * 会对已释放对象二次释放。类型化直设路径未登记，摘除为无操作，
         * 语义不变。 */
        xchart_unregisterSeries(self, self->m_pieSeries, NULL);
        XPieSeries_delete_base(self->m_pieSeries);
    }
    self->m_pieSeries = series;
}

int XChart_lineSeriesCount(const XChart* self)
{ return self ? self->m_lineCount : 0; }

XLineSeries* XChart_lineSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_lineCount) return NULL;
    return self->m_lineSeries[index];
}

XPieSeries* XChart_pieSeries(const XChart* self)
{ return self ? self->m_pieSeries : NULL; }

void XChart_addBarSeries(XChart* self, XBarSeries* series)
{
    if (!self || !series || self->m_barCount >= 4) return;
    if (xchart_typeArrayContains((const void* const*)self->m_barSeries,
                                 self->m_barCount, series)) return;
    self->m_barSeries[self->m_barCount++] = series;
}

void XChart_addScatterSeries(XChart* self, XScatterSeries* series)
{
    if (!self || !series || self->m_scatterCount >= 4) return;
    if (xchart_typeArrayContains((const void* const*)self->m_scatterSeries,
                                 self->m_scatterCount, series)) return;
    self->m_scatterSeries[self->m_scatterCount++] = series;
}

void XChart_addAreaSeries(XChart* self, XAreaSeries* series)
{
    if (!self || !series || self->m_areaCount >= 4) return;
    if (xchart_typeArrayContains((const void* const*)self->m_areaSeries,
                                 self->m_areaCount, series)) return;
    self->m_areaSeries[self->m_areaCount++] = series;
}

void XChart_addSplineSeries(XChart* self, XSplineSeries* series)
{
    if (!self || !series || self->m_splineCount >= 4) return;
    if (xchart_typeArrayContains((const void* const*)self->m_splineSeries,
                                 self->m_splineCount, series)) return;
    self->m_splineSeries[self->m_splineCount++] = series;
}

int XChart_barSeriesCount(const XChart* self) { return self ? self->m_barCount : 0; }
XBarSeries* XChart_barSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_barCount) return NULL;
    return self->m_barSeries[index];
}
int XChart_scatterSeriesCount(const XChart* self) { return self ? self->m_scatterCount : 0; }
XScatterSeries* XChart_scatterSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_scatterCount) return NULL;
    return self->m_scatterSeries[index];
}
int XChart_areaSeriesCount(const XChart* self) { return self ? self->m_areaCount : 0; }
XAreaSeries* XChart_areaSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_areaCount) return NULL;
    return self->m_areaSeries[index];
}
int XChart_splineSeriesCount(const XChart* self) { return self ? self->m_splineCount : 0; }
XSplineSeries* XChart_splineSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_splineCount) return NULL;
    return self->m_splineSeries[index];
}

uint32_t XChart_themeColor(const XChart* self, int index)
{
    if (!self) return 0xFF000000u;
    index %= 8;
    if (index < 0) index += 8;
    return self->m_theme[index];
}

uint32_t XChart_themeGradientColor(const XChart* self, int index,
                                   double pos)
{
    uint32_t base;
    if (!self) return 0xFF000000u;
    if (pos < 0.0) pos = 0.0;
    if (pos > 1.0) pos = 1.0;
    base = XChart_themeColor(self, index);
    return xchart_gradientColor(base, pos);
}

uint32_t XChart_themeBackgroundStart(const XChart* self)
{ return self ? self->m_themeBgStart : 0xFFFFFFFFu; }

uint32_t XChart_themeBackgroundEnd(const XChart* self)
{ return self ? self->m_themeBgEnd : 0xFFFFFFFFu; }

void* XChart_series(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_seriesCount) return NULL;
    return self->m_series[index];
}

void XChart_addAxis(XChart* self, XValueAxis* axis)
{
    if (!self || !axis) return;
    if (!self->m_axisX) self->m_axisX = axis;
    else if (!self->m_axisY) self->m_axisY = axis;
}

bool XChart_removeAxis(XChart* self, XValueAxis* axis)
{
    if (!self || !axis) return false;
    if (self->m_axisX == axis) {
        self->m_axisX = NULL;
        return true;
    }
    if (self->m_axisY == axis) {
        self->m_axisY = NULL;
        return true;
    }
    return false;
}

int XChart_axes(const XChart* self, XValueAxis** out, int maxCount)
{
    int n = 0;
    if (!self || !out || maxCount <= 0) return 0;
    if (self->m_axisX && n < maxCount) out[n++] = self->m_axisX;
    if (self->m_axisY && n < maxCount) out[n++] = self->m_axisY;
    return n;
}

void XChart_mapToPosition(const XChart* self, double valueX, double valueY,
                          int* outX, int* outY)
{
    double minX;
    double maxX;
    double minY;
    double maxY;
    const XRectF* pa;
    if (!self) {
        if (outX) *outX = 0;
        if (outY) *outY = 0;
        return;
    }
    minX = self->m_axisX ? self->m_axisX->m_base.m_min : 0.0;
    maxX = self->m_axisX ? self->m_axisX->m_base.m_max : 10.0;
    minY = self->m_axisY ? self->m_axisY->m_base.m_min : 0.0;
    maxY = self->m_axisY ? self->m_axisY->m_base.m_max : 10.0;
    /* 数据域映射到绘图区（plotArea；对标 QChart::mapToPosition）。 */
    pa = &self->m_plotArea;
    if (outX) {
        double fx = (maxX > minX)
            ? (valueX - minX) / (maxX - minX) : 0.0;
        *outX = (int)(pa->x + fx * pa->width);
    }
    if (outY) {
        double fy = (maxY > minY)
            ? (valueY - minY) / (maxY - minY) : 0.0;
        *outY = (int)(pa->y + (1.0 - fy) * pa->height);
    }
}

const XString* XChart_titleFont(const XChart* self)
{ return XChart_titleFontFamily(self); }
const char* XChart_titleFont_2(const XChart* self)
{ return XChart_titleFontFamily_2(self); }

#endif /* XCHARTS_ON */
