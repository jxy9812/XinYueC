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

#if XCHARTS_ON

/** @brief 泛型序列容量上限（对标 QChart 可挂载序列数；实现为定长数组）。 */
#define XCHART_SERIES_CAPACITY 24

/* ==================== 主题色板（对标 Qt Charts ChartThemeManager） ==================== */

/** @brief 亮色主题色板（对标 ChartThemeLight）。 */
static const uint32_t g_themeLight[8] = {
    0xFF209ADFu, 0xFFEA8B20u, 0xFF219E38u, 0xFFD1294Bu,
    0xFF8B5AC7u, 0xFF16AFA9u, 0xFFC33E92u, 0xFF6E6E6Eu
};

/** @brief 蔚蓝主题色板（对标 ChartThemeBlueCerulean）。 */
static const uint32_t g_themeBlueCerulean[8] = {
    0xFF1F7CC0u, 0xFF3FA9F5u, 0xFF7ED1F0u, 0xFF0B4F7Fu,
    0xFF5BC0BEu, 0xFF9AD1D4u, 0xFF2E86ABu, 0xFFC4E7F5u
};

/** @brief 暗色主题色板（对标 ChartThemeDark）。 */
static const uint32_t g_themeDark[8] = {
    0xFF6FA8DCu, 0xFFE8B93Fu, 0xFF7BC96Fu, 0xFFE06C75u,
    0xFFB18BD8u, 0xFF56C8C1u, 0xFFE07FB4u, 0xFFB0B0B0u
};

/** @brief 棕沙主题色板（对标 ChartThemeBrownSand）。 */
static const uint32_t g_themeBrownSand[8] = {
    0xFFB58B5Au, 0xFFD9A45Bu, 0xFF8C6B47u, 0xFFC97C5Du,
    0xFFE0C08Cu, 0xFF6E5B45u, 0xFFA9835Bu, 0xFF8A7652u
};

/** @brief NCS 蓝主题色板（对标 ChartThemeBlueNcs）。 */
static const uint32_t g_themeBlueNcs[8] = {
    0xFF0F4C81u, 0xFF2C7BA8u, 0xFF5AA9D6u, 0xFF87C7E8u,
    0xFF1B6B93u, 0xFF4A90B8u, 0xFF74B3D0u, 0xFF0B3C5Du
};

/** @brief 高对比主题色板（对标 ChartThemeHighContrast）。 */
static const uint32_t g_themeHighContrast[8] = {
    0xFFFFFFFFu, 0xFFFFFF00u, 0xFF00FFFFu, 0xFF00FF00u,
    0xFFFF00FFu, 0xFFFF0000u, 0xFF8080FFu, 0xFFC0C0C0u
};

/** @brief 冰蓝主题色板（对标 ChartThemeBlueIcy）。 */
static const uint32_t g_themeBlueIcy[8] = {
    0xFFA8DAEFu, 0xFF6FC3DFu, 0xFF3D9BC4u, 0xFFCDE9F5u,
    0xFF9BC9DEu, 0xFF5EB0CEu, 0xFFBEE3F0u, 0xFF7FB9D4u
};

/** @brief Qt 经典主题色板（对标 ChartThemeQt）。 */
static const uint32_t g_themeQt[8] = {
    0xFF1D4F91u, 0xFFB22222u, 0xFF2E7D32u, 0xFFF9A825u,
    0xFF6A1B9Au, 0xFF00838Fu, 0xFFAD1457u, 0xFF546E7Au
};

/**
 * @brief 按主题 ID 取色板常量表。
 *
 * @param theme 主题 ID；越界按亮色处理。
 * @return 8 色 ARGB 色板常量数组。
 */
static const uint32_t* xchart_themeTable(XChart_ChartTheme theme)
{
    switch (theme) {
    case XChart_ChartTheme_BlueCerulean: return g_themeBlueCerulean;
    case XChart_ChartTheme_Dark:         return g_themeDark;
    case XChart_ChartTheme_BrownSand:    return g_themeBrownSand;
    case XChart_ChartTheme_BlueNcs:      return g_themeBlueNcs;
    case XChart_ChartTheme_HighContrast: return g_themeHighContrast;
    case XChart_ChartTheme_BlueIcy:      return g_themeBlueIcy;
    case XChart_ChartTheme_Qt:           return g_themeQt;
    case XChart_ChartTheme_Light:
    default:                             return g_themeLight;
    }
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
    XMemcpy(self->m_theme, g_themeLight, sizeof(g_themeLight));
    self->m_backgroundVisible = true;
    self->m_animationDuration = 1000;
    self->m_animationEasingCurve = XEasingCurve_Linear;
    self->m_defaultMinX = self->m_axisX ? self->m_axisX->m_min : 0.0;
    self->m_defaultMaxX = self->m_axisX ? self->m_axisX->m_max : 10.0;
    self->m_defaultMinY = self->m_axisY ? self->m_axisY->m_min : 0.0;
    self->m_defaultMaxY = self->m_axisY ? self->m_axisY->m_max : 10.0;
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
    if (self->m_axisX) XFree_System(self->m_axisX);
    if (self->m_axisY) XFree_System(self->m_axisY);
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
        XValueAxis_setRange(self->m_axisX, other->m_axisX->m_min,
                            other->m_axisX->m_max);
    if (self->m_axisY && other->m_axisY)
        XValueAxis_setRange(self->m_axisY, other->m_axisY->m_min,
                            other->m_axisY->m_max);
    self->m_defaultMinX = other->m_defaultMinX;
    self->m_defaultMaxX = other->m_defaultMaxX;
    self->m_defaultMinY = other->m_defaultMinY;
    self->m_defaultMaxY = other->m_defaultMaxY;
}

static void VXChart_move(XChart* self, XChart* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XChart_init(self);
    if (self->m_title) XString_delete_base(self->m_title);
    if (self->m_titleFamily) XString_delete_base(self->m_titleFamily);
    if (self->m_locale) XString_delete_base(self->m_locale);
    self->m_title = other->m_title;
    self->m_titleFamily = other->m_titleFamily;
    self->m_locale = other->m_locale;
    other->m_title = NULL;
    other->m_titleFamily = NULL;
    other->m_locale = NULL;
    XMemcpy(self, other, sizeof(XChart));
    XMemset(other, 0, sizeof(XChart));
    XChart_init(other);
}

/* ==================== 标题与图例 ==================== */

void XChart_setTitle(XChart* self, const char* title)
{
    if (!self) return;
    if (!self->m_title) self->m_title = XString_create();
    if (self->m_title)
        XString_assign_utf8(self->m_title, title ? title : "");
}

const char* XChart_title(const XChart* self)
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
    if (!self || !series) return;
    if (self->m_seriesCount >= XCHART_SERIES_CAPACITY) return;
    self->m_series[self->m_seriesCount] = series;
    self->m_seriesTypes[self->m_seriesCount] = type;
    ++self->m_seriesCount;
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
    int i;
    int found = 0;
    if (!self || !series) return;
    for (i = 0; i < self->m_seriesCount; ++i) {
        if (self->m_series[i] != series) continue;
        type = self->m_seriesTypes[i];
        found = 1;
        XMemmove(&self->m_series[i], &self->m_series[i + 1],
                (size_t)(self->m_seriesCount - i - 1) * sizeof(self->m_series[0]));
        XMemmove(&self->m_seriesTypes[i], &self->m_seriesTypes[i + 1],
                (size_t)(self->m_seriesCount - i - 1) * sizeof(self->m_seriesTypes[0]));
        --self->m_seriesCount;
        break;
    }
    xchart_unlinkSeries(self, series);
    if (found) xchart_deleteSeriesByType(series, type);
}

void XChart_removeAllSeries(XChart* self)
{
    if (!self) return;
    while (self->m_seriesCount > 0)
        XChart_removeSeries(self, self->m_series[0]);
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
    if (self->m_axisX) XFree_System(self->m_axisX);
    self->m_axisX = axis;
}

void XChart_setAxisY(XChart* self, XValueAxis* axis)
{
    if (!self || !axis || axis == self->m_axisY) return;
    if (self->m_axisY) XFree_System(self->m_axisY);
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

void XChart_setTheme(XChart* self, XChart_ChartTheme theme)
{
    if (!self) return;
    if ((int)theme < 0 || theme > XChart_ChartTheme_Qt)
        theme = XChart_ChartTheme_Light;
    self->m_themeId = theme;
    XMemcpy(self->m_theme, xchart_themeTable(theme), sizeof(self->m_theme));
}

XChart_ChartTheme XChart_theme(const XChart* self)
{ return self ? self->m_themeId : XChart_ChartTheme_Light; }

void XChart_setTitleFont(XChart* self, const char* family, int pixelSize)
{
    if (!self) return;
    if (!self->m_titleFamily) self->m_titleFamily = XString_create();
    if (self->m_titleFamily)
        XString_assign_utf8(self->m_titleFamily, family ? family : "");
    self->m_titlePixelSize = pixelSize;
}

const char* XChart_titleFontFamily(const XChart* self)
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
    entry.x = (float)self->m_axisX->m_min;
    entry.y = (float)self->m_axisY->m_min;
    entry.width = (float)(self->m_axisX->m_max - self->m_axisX->m_min);
    entry.height = (float)(self->m_axisY->m_max - self->m_axisY->m_min);
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
    if (self->m_axisX->m_max - self->m_axisX->m_min <= 0.0) return;
    if (self->m_axisY->m_max - self->m_axisY->m_min <= 0.0) return;
    if (!xchart_pushZoom(self)) return;
    cx = (self->m_axisX->m_min + self->m_axisX->m_max) / 2.0;
    cy = (self->m_axisY->m_min + self->m_axisY->m_max) / 2.0;
    hw = (self->m_axisX->m_max - self->m_axisX->m_min) / 2.0 / factor;
    hh = (self->m_axisY->m_max - self->m_axisY->m_min) / 2.0 / factor;
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
    sx = (self->m_axisX->m_max - self->m_axisX->m_min) * dx;
    sy = (self->m_axisY->m_max - self->m_axisY->m_min) * dy;
    if (sx == 0.0 && sy == 0.0) return;
    xchart_pushZoom(self);
    XValueAxis_setRange(self->m_axisX, self->m_axisX->m_min + sx,
                        self->m_axisX->m_max + sx);
    XValueAxis_setRange(self->m_axisY, self->m_axisY->m_min + sy,
                        self->m_axisY->m_max + sy);
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

void XChart_setLocale(XChart* self, const char* locale)
{
    if (!self) return;
    if (!self->m_locale) self->m_locale = XString_create();
    if (self->m_locale)
        XString_assign_utf8(self->m_locale, locale ? locale : "");
}

const char* XChart_locale(const XChart* self)
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
    rx = self->m_axisX->m_max - self->m_axisX->m_min;
    ry = self->m_axisY->m_max - self->m_axisY->m_min;
    if (outX)
        *outX = self->m_axisX->m_min +
                (double)((x - area->x) / area->width) * rx;
    if (outY)
        *outY = self->m_axisY->m_max -
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
    self->m_lineSeries[self->m_lineCount++] = series;
}

void XChart_setPieSeries(XChart* self, XPieSeries* series)
{
    if (!self) return;
    if (self->m_pieSeries && self->m_pieSeries != series) {
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
    self->m_barSeries[self->m_barCount++] = series;
}

void XChart_addScatterSeries(XChart* self, XScatterSeries* series)
{
    if (!self || !series || self->m_scatterCount >= 4) return;
    self->m_scatterSeries[self->m_scatterCount++] = series;
}

void XChart_addAreaSeries(XChart* self, XAreaSeries* series)
{
    if (!self || !series || self->m_areaCount >= 4) return;
    self->m_areaSeries[self->m_areaCount++] = series;
}

void XChart_addSplineSeries(XChart* self, XSplineSeries* series)
{
    if (!self || !series || self->m_splineCount >= 4) return;
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
    minX = self->m_axisX ? self->m_axisX->m_min : 0.0;
    maxX = self->m_axisX ? self->m_axisX->m_max : 10.0;
    minY = self->m_axisY ? self->m_axisY->m_min : 0.0;
    maxY = self->m_axisY ? self->m_axisY->m_max : 10.0;
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

const char* XChart_titleFont(const XChart* self)
{ return XChart_titleFontFamily(self); }

#endif /* XCHARTS_ON */
