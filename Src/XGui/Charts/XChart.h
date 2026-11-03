#ifndef XCHART_H
#define XCHART_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XGeometry.h"
#include "XValueAxis.h"
#include "XAbstractSeries.h"

#if XCHARTS_ON

typedef struct XValueAxis   XValueAxis;
typedef struct XLineSeries  XLineSeries;
typedef struct XBarSeries   XBarSeries;
typedef struct XScatterSeries XScatterSeries;
typedef struct XAreaSeries  XAreaSeries;
typedef struct XSplineSeries XSplineSeries;
typedef struct XPieSeries   XPieSeries;
typedef struct XLegend      XLegend;
typedef struct XBoxPlotSeries XBoxPlotSeries;
typedef struct XCandlestickSeries XCandlestickSeries;
typedef struct XAbstractAxis XAbstractAxis;
typedef struct XAbstractBarSeries XAbstractBarSeries;

/**
 * @brief      图表类型（对标 Qt Charts 6.8 QChart::ChartType，数值一致）。
 * @details    直角坐标（XChart）与极坐标（XPolarChart）均已实现；
 *             极坐标经 XChartView 的同心圆/角度射线网格与极坐标映射渲染。
 */
typedef enum XChart_ChartType
{
    XChart_ChartType_Undefined = 0,   /**< 未定义（对标 QChart::ChartTypeUndefined）。 */
    XChart_ChartType_Cartesian = 1,   /**< 直角坐标（对标 QChart::ChartTypeCartesian）。 */
    XChart_ChartType_Polar = 2        /**< 极坐标（对标 QChart::ChartTypePolar；见 XPolarChart）。 */
} XChart_ChartType;

/**
 * @brief      图表主题（对标 Qt Charts 6.8 QChart::ChartTheme，数值一致）。
 * @details    setTheme 按主题重建序列色板与背景色；渲染层读取色板缓存。
 */
typedef enum XChart_ChartTheme
{
    XChart_ChartTheme_Light = 0,        /**< 亮色（对标 QChart::ChartThemeLight）。 */
    XChart_ChartTheme_BlueCerulean = 1, /**< 蔚蓝（对标 QChart::ChartThemeBlueCerulean）。 */
    XChart_ChartTheme_Dark = 2,         /**< 暗色（对标 QChart::ChartThemeDark）。 */
    XChart_ChartTheme_BrownSand = 3,    /**< 棕沙（对标 QChart::ChartThemeBrownSand）。 */
    XChart_ChartTheme_BlueNcs = 4,      /**< NCS 蓝（对标 QChart::ChartThemeBlueNcs）。 */
    XChart_ChartTheme_HighContrast = 5, /**< 高对比（对标 QChart::ChartThemeHighContrast）。 */
    XChart_ChartTheme_BlueIcy = 6,      /**< 冰蓝（对标 QChart::ChartThemeBlueIcy）。 */
    XChart_ChartTheme_Qt = 7            /**< Qt 经典（对标 QChart::ChartThemeQt）。 */
} XChart_ChartTheme;

/**
 * @brief      动画选项（对标 Qt Charts 6.8 QChart::AnimationOption，数值一致）。
 * @details    可按位组合；AllAnimations = GridAxisAnimations|SeriesAnimations。
 */
typedef enum XChart_AnimationOption
{
    XChart_Animation_NoAnimation = 0x0,      /**< 无动画（对标 QChart::NoAnimation）。 */
    XChart_Animation_GridAxisAnimations = 0x1, /**< 网格轴动画（对标 QChart::GridAxisAnimations）。 */
    XChart_Animation_SeriesAnimations = 0x2, /**< 序列动画（对标 QChart::SeriesAnimations）。 */
    XChart_Animation_AllAnimations = 0x3     /**< 全部动画（对标 QChart::AllAnimations）。 */
} XChart_AnimationOption;

/** @brief 动画选项集合（可按位组合；对标 QChart::AnimationOptions）。 */
typedef uint32_t XChart_AnimationOptions;

/**
 * @brief      缓动曲线类型（对标 Qt 6.8 QEasingCurve::Type，数值一致）。
 * @details    动画插值使用；本批次动画落地前仅作为属性存储。
 */
typedef enum XEasingCurve_Type
{
    XEasingCurve_Linear = 0,   /**< 线性（默认，对标 QEasingCurve::Linear）。 */
    XEasingCurve_InQuad = 1,        /**< 二次入（对标 InQuad）。 */
    XEasingCurve_OutQuad = 2,       /**< 二次出（对标 OutQuad）。 */
    XEasingCurve_InOutQuad = 3,     /**< 二次入出（对标 InOutQuad）。 */
    XEasingCurve_OutInQuad = 4,     /**< 二次出入（对标 OutInQuad）。 */
    XEasingCurve_InCubic = 5,       /**< 三次入（对标 InCubic）。 */
    XEasingCurve_OutCubic = 6,      /**< 三次出（对标 OutCubic）。 */
    XEasingCurve_InOutCubic = 7,    /**< 三次入出（对标 InOutCubic）。 */
    XEasingCurve_OutInCubic = 8,    /**< 三次出入（对标 OutInCubic）。 */
    XEasingCurve_InQuart = 9,       /**< 四次入（对标 InQuart）。 */
    XEasingCurve_OutQuart = 10,     /**< 四次出（对标 OutQuart）。 */
    XEasingCurve_InOutQuart = 11,   /**< 四次入出（对标 InOutQuart）。 */
    XEasingCurve_OutInQuart = 12,   /**< 四次出入（对标 OutInQuart）。 */
    XEasingCurve_InQuint = 13,      /**< 五次入（对标 InQuint）。 */
    XEasingCurve_OutQuint = 14,     /**< 五次出（对标 OutQuint）。 */
    XEasingCurve_InOutQuint = 15,   /**< 五次入出（对标 InOutQuint）。 */
    XEasingCurve_OutInQuint = 16,   /**< 五次出入（对标 OutInQuint）。 */
    XEasingCurve_InSine = 17,       /**< 正弦入（对标 InSine）。 */
    XEasingCurve_OutSine = 18,      /**< 正弦出（对标 OutSine）。 */
    XEasingCurve_InOutSine = 19,    /**< 正弦入出（对标 InOutSine）。 */
    XEasingCurve_OutInSine = 20,    /**< 正弦出入（对标 OutInSine）。 */
    XEasingCurve_InExpo = 21,       /**< 指数入（对标 InExpo）。 */
    XEasingCurve_OutExpo = 22,      /**< 指数出（对标 OutExpo）。 */
    XEasingCurve_InOutExpo = 23,    /**< 指数入出（对标 InOutExpo）。 */
    XEasingCurve_OutInExpo = 24,    /**< 指数出入（对标 OutInExpo）。 */
    XEasingCurve_InCirc = 25,       /**< 圆弧入（对标 InCirc）。 */
    XEasingCurve_OutCirc = 26,      /**< 圆弧出（对标 OutCirc）。 */
    XEasingCurve_InOutCirc = 27,    /**< 圆弧入出（对标 InOutCirc）。 */
    XEasingCurve_OutInCirc = 28,    /**< 圆弧出入（对标 OutInCirc）。 */
    XEasingCurve_InElastic = 29,    /**< 弹性入（对标 InElastic）。 */
    XEasingCurve_OutElastic = 30,   /**< 弹性出（对标 OutElastic）。 */
    XEasingCurve_InOutElastic = 31, /**< 弹性入出（对标 InOutElastic）。 */
    XEasingCurve_OutInElastic = 32, /**< 弹性出入（对标 OutInElastic）。 */
    XEasingCurve_InBack = 33,       /**< 回弹入（对标 InBack）。 */
    XEasingCurve_OutBack = 34,      /**< 回弹出（对标 OutBack）。 */
    XEasingCurve_InOutBack = 35,    /**< 回弹入出（对标 InOutBack）。 */
    XEasingCurve_OutInBack = 36,    /**< 回弹出入（对标 OutInBack）。 */
    XEasingCurve_InBounce = 37,     /**< 弹跳入（对标 InBounce）。 */
    XEasingCurve_OutBounce = 38,    /**< 弹跳出（对标 OutBounce）。 */
    XEasingCurve_InOutBounce = 39,  /**< 弹跳入出（对标 InOutBounce）。 */
    XEasingCurve_OutInBounce = 40,  /**< 弹跳出入（对标 OutInBounce）。 */
    XEasingCurve_InCurve = 41,      /**< 自定义曲线入（对标 InCurve）。 */
    XEasingCurve_OutCurve = 42,     /**< 自定义曲线出（对标 OutCurve）。 */
    XEasingCurve_SineCurve = 43,    /**< 正弦曲线（对标 SineCurve）。 */
    XEasingCurve_CosineCurve = 44   /**< 余弦曲线（对标 CosineCurve）。 */
} XEasingCurve_Type;

/**
 * @brief      序列类型（对标 Qt Charts 6.8 QAbstractSeries::SeriesType，
 *             数值一致）。
 * @details    泛型序列管理使用；当前实现覆盖 Line/Area/Bar/Pie/Scatter/
 *             Spline 等全类型均已实现（含 BoxPlot/Candlestick 与 5 个
 *             柱状变体）；枚举值与 Qt 保持一致以支持跨端对照。
 */

XCLASS_DEFINE_BEGING(XChart)
XCLASS_DEFINE_EXTEND_END(XChart, XObject)

/**
 * @brief 图表模型（对标 QChart）。
 *
 *        继承 XObject 以支持 plotAreaChanged 信号与对象生命周期管理；
 *        持有标题/图例/坐标轴/主题/缩放状态与序列集合。序列按加入顺序
 *        渲染；zoom/scroll 通过坐标轴域矩形栈实现（渲染层读取轴范围）。
 */
typedef struct XChart
{
    XObject m_base;           /**< 基类成员；必须是第一个，由 XClass 管理。 */

    /* ---- 8 字节对齐组：指针数组 / 双精度域（对齐分组前移，消除原布局
     *      0x7a/0xd4/0x104/0x12c/0x154/0x1d4/0x1f2/0x225/0x231/0x384 等
     *      空洞；成员访问一律按名，无位置初始化/offsetof 依赖） ---- */
    XString* m_title;         /**< 图表标题（对象拥有）。 */
    XValueAxis* m_axisX;      /**< X 数值轴（内部拥有；新轴非数值轴时为 NULL）。 */
    XValueAxis* m_axisY;      /**< Y 数值轴（内部拥有；新轴非数值轴时为 NULL）。 */
    XAbstractAxis* m_axisXAny; /**< X 轴基类视图（内部拥有；恒指向实际 X 轴）。 */
    XAbstractAxis* m_axisYAny; /**< Y 轴基类视图（内部拥有；恒指向实际 Y 轴）。 */
    XLineSeries* m_lineSeries[8];   /**< 折线序列集合（内部拥有）。 */
    XPieSeries* m_pieSeries;  /**< 饼图序列（内部拥有；单例）。 */
    XAbstractBarSeries* m_barSeries[4]; /**< 柱状序列集合（内部拥有；含全部 bar 变体）。 */
    XScatterSeries* m_scatterSeries[4]; /**< 散点序列集合。 */
    XAreaSeries* m_areaSeries[4];   /**< 面积序列集合。 */
    XSplineSeries* m_splineSeries[4]; /**< 样条序列集合。 */
    XBoxPlotSeries* m_boxPlotSeries[4]; /**< 盒须序列集合（内部拥有）。 */
    XCandlestickSeries* m_candlestickSeries[4]; /**< K 线序列集合（内部拥有）。 */
    XString* m_titleFamily;   /**< 标题字体家族（对象拥有；对标 setTitleFont().family()）。 */
    XLegend* m_legend;        /**< 图例对象（内部拥有；对标 QChart::legend()）。 */
    XString* m_locale;        /**< 区域名称（对象拥有；对标 locale）。 */
    double m_defaultMinX;     /**< 初始 X 域下界（zoomReset 恢复用）。 */
    double m_defaultMaxX;     /**< 初始 X 域上界（zoomReset 恢复用）。 */
    double m_defaultMinY;     /**< 初始 Y 域下界（zoomReset 恢复用）。 */
    double m_defaultMaxY;     /**< 初始 Y 域上界（zoomReset 恢复用）。 */
    void* m_series[24];       /**< 泛型序列表（加入顺序；内部拥有）。 */
    XRectF* m_zoomStack;      /**< 缩放域栈（数据域矩形；内部拥有）。 */

    /* ---- 4 字节对齐组：主题色板 / 几何与动画（收窄后按对齐降序重排） ---- */
    uint32_t m_theme[8];      /**< 当前主题序列色板缓存（ARGB；对标 seriesColors）。 */
    uint32_t m_themeBgStart;  /**< 主题背景渐变起点色（ARGB；对标 chartBackgroundGradient stop 0）。 */
    uint32_t m_themeBgEnd;    /**< 主题背景渐变终点色（ARGB；对标 chartBackgroundGradient stop 1）。 */
    uint32_t m_themeLabelBrush; /**< 主题标签画刷色（ARGB；对标 labelBrush）。 */
    uint32_t m_themeAxisLinePen; /**< 主题轴线画笔色（ARGB；对标 axisLinePen）。 */
    uint32_t m_themeGridPen;  /**< 主题网格线画笔色（ARGB；对标 gridLinePen）。 */
    uint32_t m_themeMinorGridPen; /**< 主题次网格线画笔色（ARGB；对标 minorGridLinePen）。 */
    uint32_t m_themeOutlinePen; /**< 主题轮廓画笔色（ARGB；对标 outlinePen）。 */
    uint32_t m_themeShadesBrush; /**< 主题阴影带画刷色（ARGB；0=无；对标 backgroundShadesBrush）。 */
    uint32_t m_titleBrush;    /**< 标题画刷色（ARGB；0=跟随窗口文本色）。 */
    uint32_t m_backgroundBrush; /**< 背景画刷色（ARGB；0=调色板 Base）。 */
    uint32_t m_backgroundPen;   /**< 背景画笔色（ARGB；0=无描边）。 */
    uint32_t m_plotAreaBackgroundBrush; /**< 绘图区背景画刷色（ARGB）。 */
    uint32_t m_plotAreaBackgroundPen;   /**< 绘图区背景画笔色（ARGB）。 */
    XChart_AnimationOptions m_animationOptions; /**< 动画选项（对标 animationOptions；契约字段保持 uint32 往返）。 */
    float m_backgroundRoundness; /**< 背景圆角直径（对标 backgroundRoundness）。 */
    int m_animationDuration;  /**< 动画时长（毫秒；公开 setter 原样往返）。 */
    XMargins m_margins;       /**< 图表边距（对标 margins；<=0 用视图默认）。 */
    XRectF m_plotArea;        /**< 绘图区矩形（视图坐标；对标 plotArea）。 */

    /* ---- 1 字节窄整型组（计数受数组容量约束；主题线宽 0-255 像素；
     *      全部无取址使用、无 -1 哨兵） ---- */
    uint8_t m_seriesTypes[24]; /**< 泛型序列类型表（XChartSeriesType，0-12）。 */
    int16_t m_titlePixelSize; /**< 标题字号（像素；<=0 用视图默认，有符号保留哨兵）。 */
    uint8_t m_lineCount;      /**< 折线序列数（<=8）。 */
    uint8_t m_barCount;       /**< 柱状序列数（<=4）。 */
    uint8_t m_scatterCount;   /**< 散点序列数（<=4）。 */
    uint8_t m_areaCount;      /**< 面积序列数（<=4）。 */
    uint8_t m_splineCount;    /**< 样条序列数（<=4）。 */
    uint8_t m_boxPlotCount;   /**< 盒须序列数（<=4）。 */
    uint8_t m_candlestickCount; /**< K 线序列数（<=4）。 */
    uint8_t m_seriesCount;    /**< 泛型序列数（<=24）。 */
    uint8_t m_themeId;        /**< 当前主题 ID（XChart_ChartTheme，0-7）。 */
    uint8_t m_chartType;      /**< XChart_ChartType；派生类 init 设置（对标 chartType()）。 */
    uint8_t m_themeAxisLineWidth; /**< 主题轴线画笔宽（像素，<=255）。 */
    uint8_t m_themeGridLineWidth; /**< 主题网格线画笔宽（像素，<=255）。 */
    uint8_t m_themeMinorGridLineWidth; /**< 主题次网格线画笔宽（像素，<=255）。 */
    uint8_t m_themeOutlineWidth; /**< 主题轮廓画笔宽（像素，<=255）。 */
    uint8_t m_themeShadesMode; /**< 主题阴影带模式（0 无/1 垂直/2 水平/3 双向）。 */
    uint8_t m_animationEasingCurve; /**< 缓动类型（XEasingCurve_Type，0-44）。 */
    uint16_t m_zoomCount;     /**< 缩放栈深度。 */
    uint16_t m_zoomCapacity;  /**< 缩放栈容量。 */

    /* ---- 1 字节标志组（位域压缩；全部无取址使用） ---- */
    bool m_backgroundVisible : 1; /**< 背景可见（默认 true）。 */
    bool m_dropShadowEnabled : 1; /**< 背景投影（属性存储；渲染待后续批次）。 */
    bool m_plotAreaBackgroundVisible : 1; /**< 绘图区背景可见（对标 isPlotAreaBackgroundVisible）。 */
    bool m_localizeNumbers : 1;   /**< 本地化数字（对标 localizeNumbers；属性存储）。 */
} XChart;

/** @brief 析构入口（查表分派父类析构；对标 C++ 虚析构语义）。 */

/** @brief 删除堆上图表对象（查表分派析构并释放内存）。 */

XVtable* XChart_class_init(void);

/**
 * @brief 初始化图表模型（栈/嵌入使用）。
 *
 * @param self 目标图表指针，不能为空。
 * @return 无返回值。
 */
void XChart_init(XChart* self);

/**
 * @brief 堆上创建图表模型。
 *
 * @param memory 内存类型。
 * @return 图表指针；分配失败返回 NULL。
 */
XChart* XChart_create_ex(XMemoryType memory);
#define XChart_create() XChart_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 设置标题文本（XString 主版本；对标 QChart::setTitle）。
 * @param self 目标图表指针。
 * @param title 借用 XString*；可为 NULL（清空）。
 * @return 无返回值。 */
void XChart_setTitle(XChart* self, const XString* title);
/** @brief 设置标题文本（UTF-8 兼容重载，转发主版本）。 */
void XChart_setTitle_2(XChart* self, const char* title);
/** @brief 读取标题文本（内部借用 XString*；对标 QChart::title，不得释放）。 */
const XString* XChart_title(const XChart* self);
/** @brief 读取标题文本（UTF-8 借用；未设置返回空串）。 */
const char* XChart_title_2(const XChart* self);
/** @brief 读取图例对象（借用；对标 QChart::legend()）。
 * @param self 目标图表指针。
 * @return 借用 XLegend*；归图表所有，随图表析构失效，调用者不得释放。
 */
XLegend* XChart_legend(const XChart* self);

/* ==================== 泛型序列管理（对标 addSeries/removeSeries/series） ==================== */

/**
 * @brief 追加任意序列（对标 QChart::addSeries；接管所有权）。
 *
 * @param self   目标图表指针。
 * @param series 序列指针（XLineSeries/XAreaSeries/XBarSeries/XPieSeries/
 *               XScatterSeries/XSplineSeries 之一）。
 * @param type   序列类型（XChartSeriesType）。
 * @return 无返回值。
 */
void XChart_addSeries(XChart* self, void* series, XChartSeriesType type);

/**
 * @brief 移除并释放序列（对标 QChart::removeSeries）。
 *
 * @param self   目标图表指针。
 * @param series 序列指针；未注册时为空操作。
 * @return 无返回值。
 */
void XChart_removeSeries(XChart* self, void* series);

/** @brief 移除并释放全部序列（对标 QChart::removeAllSeries）。 @param self 目标图表指针。 @return 无返回值。 */
void XChart_removeAllSeries(XChart* self);

/** @brief 查询泛型序列数（对标 series().size()）。 @param self 目标图表指针。 @return 序列数。 */
int XChart_seriesCount(const XChart* self);

/** @brief 按下标取泛型序列（对标 series().at()）。 @param self 目标图表指针。 @param index 下标。 @return 序列指针；越界返回 NULL。 */
void* XChart_seriesAt(const XChart* self, int index);

/** @brief 按下标取泛型序列类型。 @param self 目标图表指针。 @param index 下标。 @return 序列类型；越界返回 XChartSeriesType_Line。 */
XChartSeriesType XChart_seriesTypeAt(const XChart* self, int index);

/* ==================== 坐标轴（对标 addAxis/removeAxis/axes/createDefaultAxes） ==================== */

/**
 * @brief 设置 X 数值轴（对标 addAxis(axis, Horizontal)；接管所有权）。
 *
 * @param self 目标图表指针。
 * @param axis X 轴指针；NULL 恢复内部默认轴。
 * @return 无返回值。
 */
void XChart_setAxisX(XChart* self, XValueAxis* axis);

/**
 * @brief 设置 Y 数值轴（对标 addAxis(axis, Vertical)；接管所有权）。
 *
 * @param self 目标图表指针。
 * @param axis Y 轴指针；NULL 恢复内部默认轴。
 * @return 无返回值。
 */
void XChart_setAxisY(XChart* self, XValueAxis* axis);

/** @brief 取 X 数值轴。 @param self 目标图表指针。 @return X 轴指针（内部拥有）。 */
XValueAxis* XChart_axisX(const XChart* self);
/** @brief 取 Y 数值轴。 @param self 目标图表指针。 @return Y 轴指针（内部拥有）。 */
XValueAxis* XChart_axisY(const XChart* self);

/** @brief 按轴范围复位默认域并清空缩放栈（对标 createDefaultAxes）。 @param self 目标图表指针。 @return 无返回值。 */
void XChart_createDefaultAxes(XChart* self);

/* ==================== 泛化轴挂接（支持任意 XAbstractAxis 派生轴） ==================== */

/**
 * @brief 挂接 X 轴（基类指针泛化入口；对标 QChart::addAxis(axis, Qt::AlignBottom)）。
 * @details 与 XChart_setAxisX 的差别：本入口接受任意 XAbstractAxis 派生轴
 *          （XValueAxis / XDateTimeAxis / XLogValueAxis / XCategoryAxis 等），
 *          使日期时间轴与对数轴也能挂到图表上。所有权转移给图表。
 * @param self 目标图表指针。
 * @param axis 轴指针（XAbstractAxis 派生）；NULL 表示恢复内部默认数值轴。
 * @return 无返回值。
 */
void XChart_setAxisX_abstract(XChart* self, XAbstractAxis* axis);

/**
 * @brief 挂接 Y 轴（基类指针泛化入口；对标 QChart::addAxis(axis, Qt::AlignLeft)）。
 * @details 语义同 XChart_setAxisX_abstract，作用于 Y 轴。
 * @param self 目标图表指针。
 * @param axis 轴指针（XAbstractAxis 派生）；NULL 表示恢复内部默认数值轴。
 * @return 无返回值。
 */
void XChart_setAxisY_abstract(XChart* self, XAbstractAxis* axis);

/**
 * @brief 读取 X 轴的基类视图（对标 QChart::axes 的 X 轴项）。
 * @param self 目标图表指针。
 * @return 借用 XAbstractAxis*；图表无 X 轴返回 NULL。调用者不得释放。
 */
XAbstractAxis* XChart_axisX_abstract(const XChart* self);

/**
 * @brief 读取 Y 轴的基类视图（对标 QChart::axes 的 Y 轴项）。
 * @param self 目标图表指针。
 * @return 借用 XAbstractAxis*；图表无 Y 轴返回 NULL。调用者不得释放。
 */
XAbstractAxis* XChart_axisY_abstract(const XChart* self);

/* ==================== 主题与外观 ==================== */

/** @brief 设置主题并重建色板。 @param self 目标图表指针。 @param theme 主题枚举。 @return 无返回值。 */
void XChart_setTheme(XChart* self, XChart_ChartTheme theme);
/** @brief 查询当前主题。 @param self 目标图表指针。 @return 主题枚举。 */
XChart_ChartTheme XChart_theme(const XChart* self);
/** @brief 设置标题字体（XString 主版本；对标 setTitleFont；家族 + 像素字号子集）。
 * @param self 目标图表指针。
 * @param family 借用 XString*；可为 NULL（清空）。
 * @param pixelSize 像素字号；<=0 用默认。
 * @return 无返回值。 */
void XChart_setTitleFont(XChart* self, const XString* family, int pixelSize);
/** @brief 设置标题字体（UTF-8 兼容重载，转发主版本）。 */
void XChart_setTitleFont_2(XChart* self, const char* family, int pixelSize);
/** @brief 读取标题字体家族（内部借用 XString*；不得释放）。 */
const XString* XChart_titleFontFamily(const XChart* self);
/** @brief 读取标题字体家族（UTF-8 借用；空串表示默认）。 */
const char* XChart_titleFontFamily_2(const XChart* self);
/** @brief 读取标题字号。 @param self 目标图表指针。 @return 像素字号；<=0 表示默认。 */
int XChart_titlePixelSize(const XChart* self);
/** @brief 设置标题画刷色。 @param self 目标图表指针。 @param brush ARGB 颜色；0 跟随窗口文本色。 @return 无返回值。 */
void XChart_setTitleBrush(XChart* self, uint32_t brush);
/** @brief 读取标题画刷色。 @param self 目标图表指针。 @return ARGB 颜色。 */
uint32_t XChart_titleBrush(const XChart* self);
/** @brief 设置背景画刷色。 @param self 目标图表指针。 @param brush ARGB 颜色；0 跟随调色板。 @return 无返回值。 */
void XChart_setBackgroundBrush(XChart* self, uint32_t brush);
/** @brief 读取背景画刷色。 @param self 目标图表指针。 @return ARGB 颜色。 */
uint32_t XChart_backgroundBrush(const XChart* self);
/** @brief 设置背景画笔色。 @param self 目标图表指针。 @param pen ARGB 颜色；0 无描边。 @return 无返回值。 */
void XChart_setBackgroundPen(XChart* self, uint32_t pen);
/** @brief 读取背景画笔色。 @param self 目标图表指针。 @return ARGB 颜色。 */
uint32_t XChart_backgroundPen(const XChart* self);
/** @brief 设置背景可见。 @param self 目标图表指针。 @param visible true 显示。 @return 无返回值。 */
void XChart_setBackgroundVisible(XChart* self, bool visible);
/** @brief 查询背景可见。 @param self 目标图表指针。 @return 可见返回 true。 */
bool XChart_isBackgroundVisible(const XChart* self);
/** @brief 设置背景投影开关。 @param self 目标图表指针。 @param enabled true 启用。 @return 无返回值。 */
void XChart_setDropShadowEnabled(XChart* self, bool enabled);
/** @brief 查询背景投影开关。 @param self 目标图表指针。 @return 启用返回 true。 */
bool XChart_isDropShadowEnabled(const XChart* self);
/** @brief 设置背景圆角直径。 @param self 目标图表指针。 @param diameter 圆角直径（像素）。 @return 无返回值。 */
void XChart_setBackgroundRoundness(XChart* self, float diameter);
/** @brief 查询背景圆角直径。 @param self 目标图表指针。 @return 圆角直径。 */
float XChart_backgroundRoundness(const XChart* self);

/* ==================== 动画 ==================== */

/** @brief 设置动画选项。 @param self 目标图表指针。 @param options 选项位集合。 @return 无返回值。 */
void XChart_setAnimationOptions(XChart* self, XChart_AnimationOptions options);
/** @brief 查询动画选项。 @param self 目标图表指针。 @return 选项位集合。 */
XChart_AnimationOptions XChart_animationOptions(const XChart* self);
/** @brief 设置动画时长。 @param self 目标图表指针。 @param msecs 时长（毫秒）。 @return 无返回值。 */
void XChart_setAnimationDuration(XChart* self, int msecs);
/** @brief 查询动画时长。 @param self 目标图表指针。 @return 时长（毫秒）。 */
int XChart_animationDuration(const XChart* self);
/** @brief 设置缓动曲线类型。 @param self 目标图表指针。 @param type 缓动枚举。 @return 无返回值。 */
void XChart_setAnimationEasingCurve(XChart* self, XEasingCurve_Type type);
/** @brief 查询缓动曲线类型。 @param self 目标图表指针。 @return 缓动枚举。 */
XEasingCurve_Type XChart_animationEasingCurve(const XChart* self);

/* ==================== 缩放与滚动（对标 zoom/zoomReset/scroll/isZoomed） ==================== */

/** @brief 放大一档（以绘图区中心为焦点，域各缩半；对标 zoomIn()）。 @param self 目标图表指针。 @return 无返回值。 */
void XChart_zoomIn(XChart* self);
/** @brief 缩小一档（回弹最近一档；栈空为空操作；对标 zoomOut()）。 @param self 目标图表指针。 @return 无返回值。 */
void XChart_zoomOut(XChart* self);
/** @brief 按因子缩放（>1 放大，<1 缩小；对标 zoom(factor)）。 @param self 目标图表指针。 @param factor 缩放因子。 @return 无返回值。 */
void XChart_zoom(XChart* self, double factor);
/** @brief 复位到初始域并清空缩放栈（对标 zoomReset）。 @param self 目标图表指针。 @return 无返回值。 */
void XChart_zoomReset(XChart* self);
/** @brief 查询是否处于缩放状态（对标 isZoomed）。 @param self 目标图表指针。 @return 缩放栈非空返回 true。 */
bool XChart_isZoomed(const XChart* self);
/**
 * @brief 按数据域**绝对单位**滚动（对标 QChart::scroll(dx, dy)）。
 * @details Qt 口径：偏移量以**图表域单位**计（Qt 原文「The offset is in
 *          units of the chart's domain」），直接加到两轴域的 min/max 上，
 *          **不是占比**——如 dx=2.0 表示沿 X 轴右移 2 个域单位（秒）。
 *          正值右/下。此处此前实现为「占比×域宽」，与自身注释声称的
 *          Qt 口径不符（且使调用方传入绝对偏移时被二次放大），已按 Qt
 *          改正为绝对域单位。
 * @param self 目标图表指针。
 * @param dx X 域偏移量（域单位；正值右移）。
 * @param dy Y 域偏移量（域单位；正值下移）。
 * @return 无返回值。
 */
void XChart_scroll(XChart* self, double dx, double dy);
/** @brief 仅缩放 X 轴（以 X 域中心为焦点；Y 域不动——时间轴趋势图配
 *         纵轴自动量程的标准搭配；对标 Qt Charts 单轴 zoom）。 @param self 目标图表指针。 @param factor 缩放因子（>1 放大）。 @return 无返回值。 */
void XChart_zoomX(XChart* self, double factor);
/**
 * @brief 以锚点缩放 X 轴（锚点数据值在缩放前后占据同一相对位置）。
 * @details 趋势图滚轮缩放的口径：光标所指处的那段时间在缩放前后停在
 *          同一像素位置（缩放围绕光标，而非域中心）。锚点相对位置取
 *          [0,1]（0=左缘，1=右缘），越界按 0.5（居中）处理。Y 域不动。
 * @param self 目标图表指针。 @param factor 缩放因子（>1 放大）。 @param anchor 锚点数据值。 @param ratio 锚点相对位置（0..1）。 @return 无返回值。
 */
void XChart_zoomXAt(XChart* self, double factor, double anchor, double ratio);
/**
 * @brief 仅滚动 X 轴（Y 域不动）。
 * @details 单轴扩展（Qt QChart::scroll 无单轴版）；入参口径同
 *          XChart_scroll：**域绝对单位**（正值右移）。
 * @param self 目标图表指针。
 * @param dx X 域偏移量（域单位；正值右移）。
 * @return 无返回值。
 */
void XChart_scrollX(XChart* self, double dx);

/* ==================== 边距与绘图区 ==================== */

/** @brief 设置图表边距。 @param self 目标图表指针。 @param left 左。 @param top 上。 @param right 右。 @param bottom 下。 @return 无返回值。 */
void XChart_setMargins(XChart* self, int left, int top, int right, int bottom);
/** @brief 读取图表边距。 @param self 目标图表指针。 @return 边距结构。 */
XMargins XChart_margins(const XChart* self);
/** @brief 读取绘图区矩形（视图坐标；未布局时为空矩形）。 @param self 目标图表指针。 @return 绘图区矩形。 */
XRectF XChart_plotArea(const XChart* self);
/** @brief 设置绘图区矩形（布局覆盖由视图决定；对标 setPlotArea）。 @param self 目标图表指针。 @param rect 目标矩形。 @return 无返回值。 */
void XChart_setPlotArea(XChart* self, const XRectF* rect);
/** @brief 设置绘图区背景画刷色。 @param self 目标图表指针。 @param brush ARGB 颜色。 @return 无返回值。 */
void XChart_setPlotAreaBackgroundBrush(XChart* self, uint32_t brush);
/** @brief 读取绘图区背景画刷色。 @param self 目标图表指针。 @return ARGB 颜色。 */
uint32_t XChart_plotAreaBackgroundBrush(const XChart* self);
/** @brief 设置绘图区背景画笔色。 @param self 目标图表指针。 @param pen ARGB 颜色。 @return 无返回值。 */
void XChart_setPlotAreaBackgroundPen(XChart* self, uint32_t pen);
/** @brief 读取绘图区背景画笔色。 @param self 目标图表指针。 @return ARGB 颜色。 */
uint32_t XChart_plotAreaBackgroundPen(const XChart* self);
/** @brief 设置绘图区背景可见。 @param self 目标图表指针。 @param visible true 显示。 @return 无返回值。 */
void XChart_setPlotAreaBackgroundVisible(XChart* self, bool visible);
/** @brief 查询绘图区背景可见。 @param self 目标图表指针。 @return 可见返回 true。 */
bool XChart_isPlotAreaBackgroundVisible(const XChart* self);

/* ==================== 本地化与区域 ==================== */

/** @brief 设置数字本地化开关。 @param self 目标图表指针。 @param localize true 本地化。 @return 无返回值。 */
void XChart_setLocalizeNumbers(XChart* self, bool localize);
/** @brief 查询数字本地化开关。 @param self 目标图表指针。 @return 本地化返回 true。 */
bool XChart_localizeNumbers(const XChart* self);
/** @brief 设置区域名称（XString 主版本；对标 QChart::setLocale）。
 * @param self 目标图表指针。
 * @param locale 借用 XString*；可为 NULL（清空）。
 * @return 无返回值。 */
void XChart_setLocale(XChart* self, const XString* locale);
/** @brief 设置区域名称（UTF-8 兼容重载，转发主版本）。 */
void XChart_setLocale_2(XChart* self, const char* locale);
/** @brief 读取区域名称（内部借用 XString*；不得释放）。 */
const XString* XChart_locale(const XChart* self);
/** @brief 读取区域名称（UTF-8 借用）。 */
const char* XChart_locale_2(const XChart* self);

/* ==================== 坐标映射（对标 mapToValue/mapToPosition） ==================== */

/**
 * @brief 视图坐标 → 数据坐标（对标 QChart::mapToValue）。
 *
 * @param self   目标图表指针。
 * @param x      视图 X。
 * @param y      视图 Y。
 * @param outX   输出数据 X；可为 NULL。
 * @param outY   输出数据 Y；可为 NULL。
 * @return 无返回值。
 */
void XChart_mapToValue(const XChart* self, float x, float y,
                       double* outX, double* outY);

/* ==================== 类型查询 ==================== */

/** @brief 查询图表类型（当前固定 Cartesian；对标 chartType()）。 @param self 目标图表指针。 @return 图表类型枚举。 */
XChart_ChartType XChart_chartType(const XChart* self);

/* ==================== 信号 ==================== */

/**
 * @brief 发射绘图区变化信号（对标 QChart::plotAreaChanged）。
 *
 * @param self 图表指针；NULL 时返回信号标识（用于连接）。
 * @return 信号标识指针。
 */
void* XChart_plotAreaChanged_signal(XChart* self);

/* ==================== 扩展（Qt 无对应；本库内部渲染与交互所需） ==================== */

/**
 * @brief 读取主题系列色（对标 ChartThemeManager::colorAt(seriesColors)，Qt 无公开等价）。
 *
 * @details 渲染层与回归断言需要按序列下标取主题色板；Qt 把该信息藏在
 *          QChartPrivate/ChartThemeManager 中（无公共访问器）。本库将其
 *          公开为扩展 API，仅供渲染与测试使用。
 * @param self  目标图表指针。
 * @param index 序列下标（越界回环）。
 * @return ARGB 颜色。
 */
uint32_t XChart_themeColor(const XChart* self, int index);

/** @brief 按主题序列渐变取色（对标 ChartThemeManager 渐变，Qt 无公开等价）。
 * @param self 目标图表指针。
 * @param index 序列下标（越界回环）。
 * @param pos 渐变位置 0-1（0=渐变起点，1=渐变终点；越界钳位）。
 * @return ARGB 颜色。 */
uint32_t XChart_themeGradientColor(const XChart* self, int index, double pos);

/** @brief 读取主题背景渐变起点色（Qt 无公开等价）。 @param self 目标图表指针。 @return ARGB 颜色。 */
uint32_t XChart_themeBackgroundStart(const XChart* self);
/** @brief 读取主题背景渐变终点色（Qt 无公开等价）。 @param self 目标图表指针。 @return ARGB 颜色。 */
uint32_t XChart_themeBackgroundEnd(const XChart* self);

/* ==================== 坐标轴（对标 addAxis/removeAxis/axes） ==================== */

/**
 * @brief 添加一个轴（对标 QChart::addAxis(axis, alignment)）。
 * @details 本库轴与图表的位置绑定由 setAxisX/setAxisY 表达；此入口按
 *          轴指针登记，X 轴与 Y 轴各自由 setAxisX/setAxisY 指定。
 * @param self 目标图表指针。
 * @param axis 轴指针（借用；生命周期仍由调用方或 setAxisX/Y 管理）。
 * @return 无返回值。
 */
void XChart_addAxis(XChart* self, XValueAxis* axis);

/**
 * @brief 移除一个轴（对标 QChart::removeAxis）。
 * @param self 目标图表指针。
 * @param axis 轴指针。
 * @return 移除成功返回 true；轴未登记返回 false 且图表不变。
 */
bool XChart_removeAxis(XChart* self, XValueAxis* axis);

/**
 * @brief 读取轴列表（对标 QChart::axes(orientation, series)）。
 * @param self     目标图表指针。
 * @param out      输出缓冲（调用方提供，至少 maxCount 项）。
 * @param maxCount 缓冲容量。
 * @return 实际写入的轴数（0 表示无轴或缓冲为 NULL）。
 */
int XChart_axes(const XChart* self, XValueAxis** out, int maxCount);

/** @brief 读取标题字体族（借用；对标 titleFont().family()；等价 titleFontFamily）。
 * @param self 目标图表指针。
 * @return 借用 XString*；归对象所有，随析构失效，调用者不得释放。
 */
const XString* XChart_titleFont_const(const XChart* self);
/** @brief 读取标题字体族（兼容别名，转发 _const 借用版）。 */
const XString* XChart_titleFont(const XChart* self);
/** @brief 读取标题字体族（UTF-8 借用视图）。 @param self 目标图表指针。 @return UTF-8 串。 */
const char* XChart_titleFont_toUtf8(const XChart* self);
/** @brief 读取标题字体族（UTF-8 兼容别名）。 */
const char* XChart_titleFont_2(const XChart* self);
/** @brief 读取标题字号（对标 titleFont().pixelSize()）。 @param self 目标图表指针。 @return 像素字号；<=0 表示默认。 */
int XChart_titlePixelSize(const XChart* self);

/** @brief 数据域坐标映射到视图坐标（对标 mapToPosition 的便捷形态）。 @param self 目标图表指针。 @param valueX 数据 X。 @param valueY 数据 Y。 @param outX 输出视图 X。 @param outY 输出视图 Y。 @return 无返回值。 */
void XChart_mapToPosition(const XChart* self, double valueX, double valueY,
                          int* outX, int* outY);

/**
 * @brief 仅缩放 X 轴（以 X 域中心为焦点；Y 域不动）。
 * @details **本库扩展**：Qt 的 zoom(factor) 同时缩放两轴，没有单轴版本；
 *          时间轴趋势图需要「只缩横向、纵轴交给自动量程」，故提供此扩展。
 * @param self 目标图表指针。
 * @param factor 缩放因子（>1 放大）。
 * @return 无返回值。
 */
void XChart_zoomX(XChart* self, double factor);

/**
 * @brief 以锚点缩放 X 轴（锚点数据值缩放前后停在同一点）。
 * @details **本库扩展**：滚轮缩放围绕光标而非域中心，Qt 无对应 API。
 * @param self 目标图表指针。
 * @param factor 缩放因子（>1 放大）。
 * @param anchor 锚点数据值。
 * @param ratio 锚点相对位置（0..1）。
 * @return 无返回值。
 */
void XChart_zoomXAt(XChart* self, double factor, double anchor, double ratio);

/**
 * @brief 仅滚动 X 轴（Y 域不动；入参为域绝对单位，正值右移）。
 * @details **本库扩展**：Qt 的 scroll(dx,dy) 没有单轴版本。
 * @param self 目标图表指针。
 * @param dx X 域偏移量（域单位）。
 * @return 无返回值。
 */
void XChart_scrollX(XChart* self, double dx);

#endif /* XCHARTS_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCHART_H */
