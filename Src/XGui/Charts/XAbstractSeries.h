#ifndef XABSTRACTSERIES_H
#define XABSTRACTSERIES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"

#if XCHARTS_ON

typedef struct XChart XChart;
typedef struct XValueAxis XValueAxis;

/**
 * @brief      序列类型（对标 Qt Charts 6.8 QAbstractSeries::SeriesType，
 *             数值一致）。
 */
typedef enum XChartSeriesType
{
    XChartSeriesType_Line = 0,                  /**< 折线（对标 SeriesTypeLine）。 */
    XChartSeriesType_Area = 1,                  /**< 面积（对标 SeriesTypeArea）。 */
    XChartSeriesType_Bar = 2,                   /**< 柱状（对标 SeriesTypeBar）。 */
    XChartSeriesType_StackedBar = 3,            /**< 堆叠柱（对标 SeriesTypeStackedBar）。 */
    XChartSeriesType_PercentBar = 4,            /**< 百分比柱（对标 SeriesTypePercentBar）。 */
    XChartSeriesType_Pie = 5,                   /**< 饼图（对标 SeriesTypePie）。 */
    XChartSeriesType_Scatter = 6,               /**< 散点（对标 SeriesTypeScatter）。 */
    XChartSeriesType_Spline = 7,                /**< 样条（对标 SeriesTypeSpline）。 */
    XChartSeriesType_HorizontalBar = 8,         /**< 水平柱（对标 SeriesTypeHorizontalBar）。 */
    XChartSeriesType_HorizontalStackedBar = 9,  /**< 水平堆叠柱（对标 SeriesTypeHorizontalStackedBar）。 */
    XChartSeriesType_HorizontalPercentBar = 10, /**< 水平百分比柱（对标 SeriesTypeHorizontalPercentBar）。 */
    XChartSeriesType_BoxPlot = 11,              /**< 盒须（对标 SeriesTypeBoxPlot）。 */
    XChartSeriesType_Candlestick = 12           /**< K 线（对标 SeriesTypeCandlestick）。 */
} XChartSeriesType;

XCLASS_DEFINE_BEGING(XAbstractSeries)
XCLASS_DEFINE_EXTEND_END(XAbstractSeries, XObject)

/**
 * @brief 抽象序列基类（对标 QAbstractSeries）。
 *
 *        name/visible/opacity/useOpenGL 为序列公共属性；m_chart 为
 *        所属图表（图表 addSeries 时回写，图表析构时清空，借用不拥有）。
 *        type() 由派生类以 X<Derived>_type() 提供（C 无虚函数）。
 */
typedef struct XAbstractSeries
{
    XObject m_base;           /**< 基类成员；必须是第一个。 */
    XString* m_name;          /**< 序列名（对象拥有）。 */
    bool m_visible;           /**< 可见（默认 true）。 */
    double m_opacity;         /**< 不透明度 0-1（默认 1）。 */
    bool m_useOpenGL;         /**< OpenGL 加速开关（属性存储）。 */
    XChart* m_chart;          /**< 所属图表（借用；不拥有）。 */
    int m_type;               /**< XChartSeriesType；派生类 init 设置。 */
    XValueAxis** m_axes;      /**< 挂接轴数组（借用；不拥有）。 */
    int m_axisCount;          /**< 挂接轴数。 */
    int m_axisCapacity;       /**< 挂接轴容量。 */
} XAbstractSeries;

XVtable* XAbstractSeries_class_init(void);

/**
 * @brief 初始化嵌入式抽象序列。
 *
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XAbstractSeries_init(XAbstractSeries* self);

/**
 * @brief 堆上创建抽象序列（直接构造不常用；对标基类受保护语义）。
 *
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XAbstractSeries* XAbstractSeries_create_ex(XMemoryType memory);
#define XAbstractSeries_create() \
    XAbstractSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XAbstractSeries_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XAbstractSeries_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 公共属性（对标 QAbstractSeries） ==================== */

/** @brief 设置序列名（XString 主版本；对标 QAbstractSeries::setName）。
 * @param self 目标序列指针。
 * @param name 借用 XString*；可为 NULL（清空）。
 * @return 无返回值。 */
void XAbstractSeries_setName(XAbstractSeries* self, const XString* name);
/** @brief 设置序列名（UTF-8 兼容重载，转发主版本）。 */
void XAbstractSeries_setName_2(XAbstractSeries* self, const char* name);
/** @brief 读取序列名（内部借用 XString*；对标 QAbstractSeries::name，不得释放）。 */
const XString* XAbstractSeries_name(const XAbstractSeries* self);
/** @brief 读取序列名（UTF-8 借用；未设置返回空串）。 */
const char* XAbstractSeries_name_2(const XAbstractSeries* self);
/** @brief 设置可见性。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractSeries_setVisible(XAbstractSeries* self, bool visible);
/** @brief 查询可见性。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XAbstractSeries_isVisible(const XAbstractSeries* self);
/** @brief 设置不透明度。 @param self 目标序列指针。 @param opacity 0-1。 @return 无返回值。 */
void XAbstractSeries_setOpacity(XAbstractSeries* self, double opacity);
/** @brief 查询不透明度。 @param self 目标序列指针。 @return 不透明度。 */
double XAbstractSeries_opacity(const XAbstractSeries* self);
/** @brief 设置 OpenGL 加速开关。 @param self 目标序列指针。 @param enable true 启用。 @return 无返回值。 */
void XAbstractSeries_setUseOpenGL(XAbstractSeries* self, bool enable);
/** @brief 查询 OpenGL 加速开关。 @param self 目标序列指针。 @return 启用返回 true。 */
bool XAbstractSeries_useOpenGL(const XAbstractSeries* self);
/** @brief 显示序列（对标 show()；等价 setVisible(true)）。 @param self 目标序列指针。 @return 无返回值。 */
void XAbstractSeries_show(XAbstractSeries* self);
/** @brief 隐藏序列（对标 hide()；等价 setVisible(false)）。 @param self 目标序列指针。 @return 无返回值。 */
void XAbstractSeries_hide(XAbstractSeries* self);
/** @brief 查询所属图表。 @param self 目标序列指针。 @return 图表指针；未挂载返回 NULL。 */
XChart* XAbstractSeries_chart(const XAbstractSeries* self);
/** @brief 查询序列类型（对标 QAbstractSeries::type）。 @param self 目标序列指针。 @return XChartSeriesType 枚举。 */
int XAbstractSeries_type(const XAbstractSeries* self);

/* ==================== 信号（对标 QAbstractSeries Q_SIGNALS） ==================== */

/** @brief nameChanged 信号地址（无载荷）。 */
void* XAbstractSeries_nameChanged_signal(XAbstractSeries* self);
/** @brief visibleChanged 信号地址（无载荷）。 */
void* XAbstractSeries_visibleChanged_signal(XAbstractSeries* self);
/** @brief opacityChanged 信号地址（无载荷）。 */
void* XAbstractSeries_opacityChanged_signal(XAbstractSeries* self);
/** @brief useOpenGLChanged 信号地址（无载荷）。 */
void* XAbstractSeries_useOpenGLChanged_signal(XAbstractSeries* self);

/* ==================== 轴挂接（对标 QAbstractSeries） ==================== */

/** @brief 挂接一个轴（重复挂接忽略）。 @param self 目标序列指针。 @param axis 轴指针。 @return 无返回值。 */
void XAbstractSeries_attachAxis(XAbstractSeries* self, XValueAxis* axis);
/** @brief 摘除一个轴。 @param self 目标序列指针。 @param axis 轴指针。 @return 无返回值。 */
void XAbstractSeries_detachAxis(XAbstractSeries* self, XValueAxis* axis);
/** @brief 查询已挂接轴数。 @param self 目标序列指针。 @return 轴数。 */
int XAbstractSeries_axisCount(const XAbstractSeries* self);
/** @brief 读取已挂接轴。 @param self 目标序列指针。 @param index 下标。 @return 轴指针；越界返回 NULL。 */
XValueAxis* XAbstractSeries_axisAt(const XAbstractSeries* self, int index);
/** @brief 批量读取已挂接轴（对标 attachedAxes()）。 @param self 目标序列指针。 @param out 输出缓冲。 @param maxCount 容量。 @return 实际轴数。 */
int XAbstractSeries_attachedAxes(const XAbstractSeries* self,
                                 XValueAxis** out, int maxCount);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTSERIES_H */
