#ifndef XXYSERIES_H
#define XXYSERIES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractSeries.h"
#include "XGeometry.h"
#include "XPixmap.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XXYSeries)
XCLASS_DEFINE_EXTEND_END(XXYSeries, XAbstractSeries)

/**
 * @brief XY 点序列基类（对标 Qt Charts 6.8 QXYSeries）。
 *
 *        承载点数组与增删改查、点外观（颜色/线宽/标记尺寸/点标签）、
 *        点选择以及点击/悬停等交互信号；QLineSeries/QScatterSeries/
 *        QSplineSeries 均继承本类。
 */
typedef struct XXYSeries
{
    XAbstractSeries m_base;      /**< 基类成员；必须是第一个。 */
    XPointF* m_points;           /**< 点数组（堆）。 */
    int m_count;                 /**< 点数。 */
    int m_capacity;              /**< 点容量。 */
    uint32_t m_color;            /**< 线/点色（0=使用主题色）。 */
    double m_width;              /**< 线宽（像素，默认 2）。 */
    double m_markerSize;         /**< 标记尺寸（默认 8）。 */
    bool m_pointsVisible;        /**< 点标记可见（默认 false）。 */
    XString* m_pointLabelsFormat; /**< 点标签格式（对象拥有；默认 "@xPoint, @yPoint"）。 */
    bool m_pointLabelsVisible;   /**< 点标签可见（默认 false）。 */
    uint32_t m_pointLabelsColor; /**< 点标签颜色（0=默认）。 */
    bool* m_selected;            /**< 点选择位图（堆；NULL=全未选）。 */
    uint32_t m_brush;            /**< 画刷颜色（0=透明；对标 brush）。 */
    uint32_t m_selectedColor;    /**< 选中点颜色（0=默认）。 */
    bool m_pointLabelsClipping;  /**< 点标签裁剪（默认 true）。 */
    XString* m_pointLabelsFontFamily; /**< 点标签字体族（对象拥有）。 */
    int m_pointLabelsFontSize;   /**< 点标签字号（磅；0=默认）。 */
    const XPixmap* m_lightMarker; /**< 普通点标记图像（借用）。 */
    const XPixmap* m_selectedLightMarker; /**< 选中点标记图像（借用）。 */
    uint32_t* m_pointColors;     /**< 点级颜色配置（堆；下标=点）。 */
    double* m_pointSizes;        /**< 点级尺寸配置（堆；下标=点）。 */
    int m_pointConfigCapacity;   /**< 点级配置容量。 */
    bool m_bestFitVisible;       /**< 最佳拟合线可见（默认 false）。 */
    uint32_t m_bestFitColor;     /**< 最佳拟合线颜色（0=默认）。 */
    double m_bestFitWidth;       /**< 最佳拟合线宽（默认 2）。 */
} XXYSeries;

XVtable* XXYSeries_class_init(void);

/**
 * @brief 初始化嵌入式 XY 序列。
 *
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XXYSeries_init(XXYSeries* self);

/**
 * @brief 堆上创建 XY 序列（一般由派生类创建）。
 *
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XXYSeries* XXYSeries_create_ex(XMemoryType memory);
#define XXYSeries_create() XXYSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XXYSeries_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XXYSeries_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 数据操作（对标 QXYSeries） ==================== */

/** @brief 追加一个点。 @param self 目标序列指针。 @param x X 坐标。 @param y Y 坐标。 @return 无返回值。 */
void XXYSeries_append(XXYSeries* self, double x, double y);
/** @brief 追加一个点（XPointF 形式）。 @param self 目标序列指针。 @param point 点。 @return 无返回值。 */
void XXYSeries_appendPoint(XXYSeries* self, const XPointF* point);
/** @brief 批量追加。 @param self 目标序列指针。 @param points 点数组。 @param count 数量。 @return 无返回值。 */
void XXYSeries_appendPoints(XXYSeries* self, const XPointF* points, int count);
/** @brief 按坐标替换旧点（对标 replace(oldX,oldY,newX,newY)）。 @param self 目标序列指针。 @param oldX 旧 X。 @param oldY 旧 Y。 @param newX 新 X。 @param newY 新 Y。 @return 替换成功返回 true。 */
bool XXYSeries_replace(XXYSeries* self, double oldX, double oldY,
                       double newX, double newY);
/** @brief 按下标替换点。 @param self 目标序列指针。 @param index 下标。 @param newX 新 X。 @param newY 新 Y。 @return 越界返回 false。 */
bool XXYSeries_replaceAt(XXYSeries* self, int index, double newX, double newY);
/** @brief 移除第一个匹配坐标的点（对标 remove(x,y)）。 @param self 目标序列指针。 @param x X 坐标。 @param y Y 坐标。 @return 移除成功返回 true。 */
bool XXYSeries_remove(XXYSeries* self, double x, double y);
/** @brief 按下标移除点。 @param self 目标序列指针。 @param index 下标。 @return 越界返回 false。 */
bool XXYSeries_removeAt(XXYSeries* self, int index);
/** @brief 移除一段连续点。 @param self 目标序列指针。 @param index 起始下标。 @param count 数量。 @return 无返回值。 */
void XXYSeries_removePoints(XXYSeries* self, int index, int count);
/** @brief 插入点。 @param self 目标序列指针。 @param index 插入位置。 @param point 点。 @return 越界返回 false。 */
bool XXYSeries_insert(XXYSeries* self, int index, const XPointF* point);
/** @brief 清空全部点。 @param self 目标序列指针。 @return 无返回值。 */
void XXYSeries_clear(XXYSeries* self);

/* ==================== 数据读取 ==================== */

/** @brief 查询点数。 @param self 目标序列指针。 @return 点数。 */
int XXYSeries_count(const XXYSeries* self);
/** @brief 读取下标点。 @param self 目标序列指针。 @param index 下标。 @return 点指针；越界返回 NULL。 */
const XPointF* XXYSeries_at(const XXYSeries* self, int index);

/* ==================== 外观（对标 QXYSeries） ==================== */

/** @brief 设置画笔（C 参数化：颜色+线宽，对标 setPen(QPen)）。 @param self 目标序列指针。 @param color 画笔颜色 ARGB。 @param width 线宽（像素）。 @return 无返回值。 */
void XXYSeries_setPen(XXYSeries* self, uint32_t color, double width);
/** @brief 读取画笔（参数化）。 @param self 目标序列指针。 @param color 输出画笔颜色。 @param width 输出线宽。 @return 无返回值。 */
void XXYSeries_pen(const XXYSeries* self, uint32_t* color, double* width);
/** @brief 设置画刷（C 参数化：颜色，对标 setBrush(QBrush)）。 @param self 目标序列指针。 @param color 画刷颜色 ARGB（0=透明）。 @return 无返回值。 */
void XXYSeries_setBrush(XXYSeries* self, uint32_t color);
/** @brief 读取画刷颜色。 @param self 目标序列指针。 @return 画刷颜色。 */
uint32_t XXYSeries_brush(const XXYSeries* self);
/** @brief 设置选中点颜色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XXYSeries_setSelectedColor(XXYSeries* self, uint32_t color);
/** @brief 查询选中点颜色。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XXYSeries_selectedColor(const XXYSeries* self);
/** @brief 设置点标签裁剪。 @param self 目标序列指针。 @param clip true 裁剪。 @return 无返回值。 */
void XXYSeries_setPointLabelsClipping(XXYSeries* self, bool clip);
/** @brief 查询点标签裁剪。 @param self 目标序列指针。 @return 裁剪返回 true。 */
bool XXYSeries_pointLabelsClipping(const XXYSeries* self);
/** @brief 设置点标签字体（C 参数化：字体族+字号）。 @param self 目标序列指针。 @param family 字体族（UTF-8；NULL 保持默认）。 @param pointSize 字号（磅；0 保持默认）。 @return 无返回值。 */
void XXYSeries_setPointLabelsFont(XXYSeries* self, const char* family,
                                  int pointSize);
/** @brief 读取点标签字体族。 @param self 目标序列指针。 @return 字体族；未设置空串。 */
const char* XXYSeries_pointLabelsFontFamily(const XXYSeries* self);
/** @brief 读取点标签字号。 @param self 目标序列指针。 @return 字号（磅）。 */
int XXYSeries_pointLabelsFontSize(const XXYSeries* self);
/** @brief 批量读取全部点（对标 points()）。 @param self 目标序列指针。 @param out 输出缓冲（至少 count 项）。 @param maxCount 缓冲容量。 @return 实际点数。 */
int XXYSeries_points(const XXYSeries* self, XPointF* out, int maxCount);
/** @brief 查询点数（pointsVector 便捷同义）。 @param self 目标序列指针。 @return 点数。 */
int XXYSeries_pointsVector(const XXYSeries* self, XPointF* out, int maxCount);

/** @brief 设置线/点颜色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XXYSeries_setColor(XXYSeries* self, uint32_t color);
/** @brief 查询线/点颜色。 @param self 目标序列指针。 @return 颜色。 */
uint32_t XXYSeries_color(const XXYSeries* self);
/** @brief 设置线宽。 @param self 目标序列指针。 @param width 宽度（像素）。 @return 无返回值。 */
void XXYSeries_setWidth(XXYSeries* self, double width);
/** @brief 查询线宽。 @param self 目标序列指针。 @return 线宽。 */
double XXYSeries_width(const XXYSeries* self);
/** @brief 设置标记尺寸。 @param self 目标序列指针。 @param size 尺寸。 @return 无返回值。 */
void XXYSeries_setMarkerSize(XXYSeries* self, double size);
/** @brief 查询标记尺寸。 @param self 目标序列指针。 @return 尺寸。 */
double XXYSeries_markerSize(const XXYSeries* self);
/** @brief 设置点标记可见。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XXYSeries_setPointsVisible(XXYSeries* self, bool visible);
/** @brief 查询点标记可见。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XXYSeries_pointsVisible(const XXYSeries* self);
/** @brief 设置点标签格式。 @param self 目标序列指针。 @param format 格式串（UTF-8）。 @return 无返回值。 */
void XXYSeries_setPointLabelsFormat(XXYSeries* self, const char* format);
/** @brief 查询点标签格式。 @param self 目标序列指针。 @return 格式串。 */
const char* XXYSeries_pointLabelsFormat(const XXYSeries* self);
/** @brief 设置点标签可见。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XXYSeries_setPointLabelsVisible(XXYSeries* self, bool visible);
/** @brief 查询点标签可见。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XXYSeries_pointLabelsVisible(const XXYSeries* self);
/** @brief 设置点标签颜色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XXYSeries_setPointLabelsColor(XXYSeries* self, uint32_t color);
/** @brief 查询点标签颜色。 @param self 目标序列指针。 @return 颜色。 */
uint32_t XXYSeries_pointLabelsColor(const XXYSeries* self);

/* ==================== 最佳拟合线（对标 QXYSeries） ==================== */

/** @brief 设置最佳拟合线可见。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XXYSeries_setBestFitLineVisible(XXYSeries* self, bool visible);
/** @brief 查询最佳拟合线可见。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XXYSeries_bestFitLineVisible(const XXYSeries* self);
/** @brief 计算最佳拟合线方程 y = a*x + b（最小二乘）。 @param self 目标序列指针。 @param slope 输出斜率 a。 @param intercept 输出截距 b。 @return 点数足够返回 true。 */
bool XXYSeries_bestFitLineEquation(const XXYSeries* self, double* slope,
                                   double* intercept);
/** @brief 设置最佳拟合线颜色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XXYSeries_setBestFitLineColor(XXYSeries* self, uint32_t color);
/** @brief 查询最佳拟合线颜色。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XXYSeries_bestFitLineColor(const XXYSeries* self);
/** @brief 设置最佳拟合线宽。 @param self 目标序列指针。 @param width 像素。 @return 无返回值。 */
void XXYSeries_setBestFitLineWidth(XXYSeries* self, double width);

/* ==================== 点选择（对标 QXYSeries） ==================== */

/** @brief 查询点是否选中。 @param self 目标序列指针。 @param index 下标。 @return 选中返回 true。 */
bool XXYSeries_isPointSelected(const XXYSeries* self, int index);
/** @brief 选中一个点。 @param self 目标序列指针。 @param index 下标。 @return 无返回值。 */
void XXYSeries_selectPoint(XXYSeries* self, int index);
/** @brief 取消选中一个点。 @param self 目标序列指针。 @param index 下标。 @return 无返回值。 */
void XXYSeries_deselectPoint(XXYSeries* self, int index);
/** @brief 设置点选中状态。 @param self 目标序列指针。 @param index 下标。 @param selected 是否选中。 @return 无返回值。 */
void XXYSeries_setPointSelected(XXYSeries* self, int index, bool selected);
/** @brief 全选。 @param self 目标序列指针。 @return 无返回值。 */
void XXYSeries_selectAllPoints(XXYSeries* self);
/** @brief 全不选。 @param self 目标序列指针。 @return 无返回值。 */
void XXYSeries_deselectAllPoints(XXYSeries* self);
/** @brief 批量选中。 @param self 目标序列指针。 @param indexes 下标数组。 @param count 数量。 @return 无返回值。 */
void XXYSeries_selectPoints(XXYSeries* self, const int* indexes, int count);
/** @brief 批量取消选中。 @param self 目标序列指针。 @param indexes 下标数组。 @param count 数量。 @return 无返回值。 */
void XXYSeries_deselectPoints(XXYSeries* self, const int* indexes, int count);
/** @brief 翻转选中状态。 @param self 目标序列指针。 @param indexes 下标数组。 @param count 数量。 @return 无返回值。 */
void XXYSeries_toggleSelection(XXYSeries* self, const int* indexes, int count);
/** @brief 读取全部选中下标。 @param self 目标序列指针。 @param out 输出缓冲。 @param maxCount 容量。 @return 选中数量。 */
int XXYSeries_selectedPoints(const XXYSeries* self, int* out, int maxCount);

/* ==================== 交互信号（对标 QXYSeries Q_SIGNALS） ==================== */

/** @brief clicked 信号地址（载荷：x, y）。 */
void* XXYSeries_clicked_signal(XXYSeries* self, double x, double y);
/** @brief hovered 信号地址（载荷：x, y, 状态）。 */
void* XXYSeries_hovered_signal(XXYSeries* self, double x, double y, bool state);
/** @brief pressed 信号地址（载荷：x, y）。 */
void* XXYSeries_pressed_signal(XXYSeries* self, double x, double y);
/** @brief released 信号地址（载荷：x, y）。 */
void* XXYSeries_released_signal(XXYSeries* self, double x, double y);
/** @brief doubleClicked 信号地址（载荷：x, y）。 */
void* XXYSeries_doubleClicked_signal(XXYSeries* self, double x, double y);
/** @brief pointReplaced 信号地址（载荷：index）。 */
void* XXYSeries_pointReplaced_signal(XXYSeries* self, int index);
/** @brief pointRemoved 信号地址（载荷：index）。 */
void* XXYSeries_pointRemoved_signal(XXYSeries* self, int index);
/** @brief pointAdded 信号地址（载荷：index）。 */
void* XXYSeries_pointAdded_signal(XXYSeries* self, int index);
/** @brief pointsReplaced 信号地址（无载荷）。 */
void* XXYSeries_pointsReplaced_signal(XXYSeries* self);
/** @brief colorChanged 信号地址（载荷：颜色）。 */
void* XXYSeries_colorChanged_signal(XXYSeries* self, uint32_t color);

/* ==================== 画笔/字体/标记/点配置（参数化补全） ==================== */

/** @brief 设置最佳拟合线画笔（颜色+线宽，对标 setBestFitLinePen(QPen)）。 @param self 目标序列指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XXYSeries_setBestFitLinePen(XXYSeries* self, uint32_t color,
                                 double width);
/** @brief 读取最佳拟合线画笔（参数化）。 @param self 目标序列指针。 @param color 输出颜色。 @param width 输出线宽。 @return 无返回值。 */
void XXYSeries_bestFitLinePen(const XXYSeries* self, uint32_t* color,
                              double* width);
/** @brief 读取点标签字体（别名：返回字体族）。 @param self 目标序列指针。 @return 字体族。 */
const char* XXYSeries_pointLabelsFont(const XXYSeries* self);
/** @brief 设置普通点标记图像（对标 setLightMarker(QImage)）。 @param self 目标序列指针。 @param marker 图像指针（可空=清除）。 @return 无返回值。 */
void XXYSeries_setLightMarker(XXYSeries* self, const XPixmap* marker);
/** @brief 读取普通点标记图像。 @param self 目标序列指针。 @return 图像指针；无标记返回 NULL。 */
const XPixmap* XXYSeries_lightMarker(const XXYSeries* self);
/** @brief 设置选中点标记图像（对标 setSelectedLightMarker）。 @param self 目标序列指针。 @param marker 图像指针（可空=清除）。 @return 无返回值。 */
void XXYSeries_setSelectedLightMarker(XXYSeries* self, const XPixmap* marker);
/** @brief 读取选中点标记图像。 @param self 目标序列指针。 @return 图像指针；无标记返回 NULL。 */
const XPixmap* XXYSeries_selectedLightMarker(const XXYSeries* self);
/** @brief 设置点级配置（颜色/尺寸/可见性，对标 setPointConfiguration）。 @param self 目标序列指针。 @param index 点下标。 @param color 颜色（0=不变）。 @param size 尺寸（0=不变）。 @return 无返回值。 */
void XXYSeries_setPointConfiguration(XXYSeries* self, int index,
                                     uint32_t color, double size);
/** @brief 读取点级颜色。 @param self 目标序列指针。 @param index 点下标。 @return 颜色；无配置 0。 */
uint32_t XXYSeries_pointColor(const XXYSeries* self, int index);
/** @brief 读取点级尺寸。 @param self 目标序列指针。 @param index 点下标。 @return 尺寸；无配置 0。 */
double XXYSeries_pointSize(const XXYSeries* self, int index);
/** @brief 清除点级配置。 @param self 目标序列指针。 @param index 点下标（-1=全部）。 @return 无返回值。 */
void XXYSeries_clearPointConfiguration(XXYSeries* self, int index);
/** @brief 按源数据设置点尺寸（对标 sizeBy）。 @param self 目标序列指针。 @param sourceData 源数据数组。 @param count 数量。 @param minSize 最小尺寸。 @param maxSize 最大尺寸。 @return 无返回值。 */
void XXYSeries_sizeBy(XXYSeries* self, const double* sourceData, int count,
                      double minSize, double maxSize);
/** @brief 按源数据设置点颜色（对标 colorBy，双端渐变）。 @param self 目标序列指针。 @param sourceData 源数据数组。 @param count 数量。 @param colorStart 起始颜色。 @param colorEnd 结束颜色。 @return 无返回值。 */
void XXYSeries_colorBy(XXYSeries* self, const double* sourceData, int count,
                       uint32_t colorStart, uint32_t colorEnd);

/** @brief 读取单点配置（对标 pointConfiguration(index)）。 @param self 目标序列指针。 @param index 点下标。 @param color 输出颜色（可空）。 @param size 输出尺寸（可空）。 @return 无返回值。 */
void XXYSeries_pointConfiguration(const XXYSeries* self, int index,
                                  uint32_t* color, double* size);
/** @brief 批量设置点配置（对标 setPointsConfiguration）。 @param self 目标序列指针。 @param colors 颜色数组（NULL=不变）。 @param sizes 尺寸数组（NULL=不变）。 @param count 数量。 @return 无返回值。 */
void XXYSeries_setPointsConfiguration(XXYSeries* self,
                                      const uint32_t* colors,
                                      const double* sizes, int count);
/** @brief 批量读取点配置（对标 pointsConfiguration）。 @param self 目标序列指针。 @param colors 输出颜色数组（可空）。 @param sizes 输出尺寸数组（可空）。 @param maxCount 容量。 @return 实际配置点数。 */
int XXYSeries_pointsConfiguration(const XXYSeries* self, uint32_t* colors,
                                  double* sizes, int maxCount);
/** @brief 清空全部点配置（对标 clearPointsConfiguration）。 @param self 目标序列指针。 @return 无返回值。 */
void XXYSeries_clearPointsConfiguration(XXYSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XXYSERIES_H */
