/**
 * @file       XAbstractAxis.h
 * @brief      XAbstractAxis 抽象坐标轴基类（对标 Qt Charts 6.8 QAbstractAxis）。
 * @details    提供坐标轴公共属性与信号：可见性、网格线可见性、范围
 *             （min/max/setRange）、反转、标题、标签角度、阴影带、简化
 *             线条/标签颜色。XValueAxis（QValueAxis）与 XCategoryAxis
 *             （QBarCategoryAxis 语义）以 m_base 组合继承本类。
 * @note       模块总开关 XCHARTS_ON；XAbstractAxis 为 XObject 派生，支持
 *             信号槽（m_signalSlot）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XABSTRACT_AXIS_H
#define XABSTRACT_AXIS_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"

#if XCHARTS_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XAbstractAxis)
XCLASS_DEFINE_EXTEND_END(XAbstractAxis, XObject)

/**
 * @brief      坐标轴类型（对标 Qt Charts 6.8 QAbstractAxis::AxisType，
 *             数值一致；可按位组合用于 AxisTypes）。
 */
typedef enum XAbstractAxis_AxisType
{
    XAbstractAxis_AxisType_NoAxis = 0x0,      /**< 无轴（对标 AxisTypeNoAxis）。 */
    XAbstractAxis_AxisType_Value = 0x1,       /**< 数值轴（对标 AxisTypeValue）。 */
    XAbstractAxis_AxisType_BarCategory = 0x2, /**< 柱状类别轴（对标 AxisTypeBarCategory）。 */
    XAbstractAxis_AxisType_Category = 0x4,    /**< 类别轴（对标 AxisTypeCategory）。 */
    XAbstractAxis_AxisType_DateTime = 0x8,    /**< 日期时间轴（对标 AxisTypeDateTime）。 */
    XAbstractAxis_AxisType_LogValue = 0x10,   /**< 对数数值轴（对标 AxisTypeLogValue）。 */
    XAbstractAxis_AxisType_Color = 0x20       /**< 颜色轴（对标 AxisTypeColor）。 */
} XAbstractAxis_AxisType;

/** @brief 坐标轴类型集合（可按位组合；对标 QAbstractAxis::AxisTypes）。 */
typedef uint32_t XAbstractAxis_AxisTypes;

/**
 * @brief      抽象坐标轴对象；m_base 必须是第一个成员（嵌 XObject）。
 * @details    字段含义（对标 QAbstractAxis 同名属性）：
 *             - m_visible：轴可见（默认 true）；
 *             - m_lineVisible：轴线可见（默认 true）；
 *             - m_gridLineVisible：主网格线可见（默认 true）；
 *             - m_minorGridLineVisible：次网格线可见（默认 false）；
 *             - m_labelsVisible：刻度标签可见（默认 true）；
 *             - m_titleVisible：轴标题可见（默认 true）；
 *             - m_labelsEditable / m_truncateLabels：标签可编辑/截断（默认 false）；
 *             - m_min/m_max：范围（默认 0/10；类别轴 max 随类别数推导）；
 *             - m_reverse：反转（默认 false）；
 *             - m_titleText：轴标题（对象拥有）；
 *             - m_labelsAngle：标签角度（度，默认 0）；
 *             - m_shadesVisible：阴影带可见（默认 false）；
 *             - m_linePen/labelsBrush/titleBrush/shades*：C 参数化的
 *               QPen/QBrush 等价物（颜色 + 线宽；0=主题色）。
 *             Qt 的 QPen/QBrush/QFont 在本库以「颜色 + 线宽 / 字体族 + 字号」
 *             参数化承载；QColor→uint32_t ARGB，QRectF→XRectF。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XAbstractAxis
{
    XObject   m_base;            /**< 基类成员；必须是第一个，由 XClass 管理。 */

    /* ---- 8 字节对齐组（对齐分组前移，消除原布局 0x72/0x89/0x9d 空洞；
     *      成员访问一律按名，无位置初始化/offsetof 依赖） ---- */
    XString*  m_titleText;       /**< 轴标题（对象拥有）。 */
    XString*  m_labelsFontFamily;/**< 标签字体族（对象拥有；对标 labelsFont）。 */
    XString*  m_titleFontFamily; /**< 标题字体族（对象拥有；对标 titleFont）。 */
    double    m_min;             /**< 轴范围下限。 */
    double    m_max;             /**< 轴范围上限。 */
    double    m_linePenWidth;    /**< 轴线画笔宽（像素；对标 linePen）。 */
    double    m_gridLinePenWidth;/**< 主网格线画笔宽（像素）。 */
    double    m_minorGridLinePenWidth; /**< 次网格线画笔宽（像素）。 */
    double    m_shadesPenWidth;  /**< 阴影带画笔宽（像素）。 */
    double    m_titlePenWidth;   /**< 标题画笔宽占位（保持 8 字节分组）。 */

    /* ---- 4 字节对齐组 ---- */
    int       m_labelsAngle;     /**< 标签角度（度，默认 0）。 */
    int       m_labelsFontSize;  /**< 标签字号（磅；0=默认）。 */
    int       m_titleFontSize;   /**< 标题字号（磅；0=默认）。 */
    uint32_t  m_linePenColor;    /**< 轴线画笔颜色（ARGB；对标 linePen）。 */
    uint32_t  m_gridLinePenColor;/**< 主网格线画笔颜色（ARGB；对标 gridLinePen）。 */
    uint32_t  m_minorGridLinePenColor; /**< 次网格线画笔颜色（ARGB）。 */
    uint32_t  m_labelsBrushColor;/**< 标签画刷颜色（ARGB；对标 labelsBrush）。 */
    uint32_t  m_labelsColor;     /**< 标签文字颜色（ARGB；对标 labelsColor）。 */
    uint32_t  m_titleBrushColor; /**< 标题画刷颜色（ARGB；对标 titleBrush）。 */
    uint32_t  m_shadesPenColor;  /**< 阴影带画笔颜色（ARGB；对标 shadesPen）。 */
    uint32_t  m_shadesBrushColor;/**< 阴影带画刷颜色（ARGB；对标 shadesBrush）。 */

    /* ---- 1 字节标志组（尾部填充；保持 bool 不并位域） ---- */
    bool      m_visible;         /**< 轴可见（默认 true）。 */
    bool      m_lineVisible;     /**< 轴线可见（默认 true）。 */
    bool      m_gridLineVisible; /**< 主网格线可见（默认 true）。 */
    bool      m_minorGridLineVisible; /**< 次网格线可见（默认 false）。 */
    bool      m_labelsVisible;   /**< 刻度标签可见（默认 true）。 */
    bool      m_titleVisible;    /**< 轴标题可见（默认 true）。 */
    bool      m_labelsEditable;  /**< 标签可编辑（默认 false；Qt 6.0+）。 */
    bool      m_truncateLabels;  /**< 标签截断（默认 false；Qt 6.2+）。 */
    bool      m_reverse;         /**< 反转方向（默认 false）。 */
    bool      m_shadesVisible;   /**< 阴影带可见（默认 false）。 */

    /* ---- 轴类型（对标 QAbstractAxis::type() 纯虚；C 版以字段承载） ---- */
    uint8_t   m_axisType;        /**< XAbstractAxis_AxisType 位值；派生类 init 设置。 */
    uint8_t   m_axisAlign;       /**< 对齐位（XLegend_Alignment 值域；0=未定）。 */
    uint8_t   m_axisOrientation; /**< 朝向（0=水平 1=垂直；0=未定）。 */
} XAbstractAxis;

/* ==================== 生命周期 ==================== */

/** @brief 初始化 XAbstractAxis 类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XAbstractAxis_class_init(void);
/** @brief 初始化抽象轴（对标 QAbstractAxis 构造）。
 * @param self 目标轴对象；不可为 NULL。
 * @return 无返回值。
 */
void XAbstractAxis_init(XAbstractAxis* self);
/** @brief 使用指定内存类型创建抽象轴。
 * @param memory 内存类型。
 * @return 新建对象；失败返回 NULL。
 */
XAbstractAxis* XAbstractAxis_create_ex(XMemoryType memory);
#define XAbstractAxis_create() XAbstractAxis_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 可见性与轴线（对标 QAbstractAxis） ==================== */

/** @brief 设置轴可见性（对标 setVisible）。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractAxis_setVisible(XAbstractAxis* self, bool visible);
/** @brief 查询轴可见性（对标 isVisible）。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_isVisible(const XAbstractAxis* self);
/** @brief 显示轴（对标 show；等价 setVisible(true)）。 @param self 目标轴指针。 @return 无返回值。 */
void XAbstractAxis_show(XAbstractAxis* self);
/** @brief 隐藏轴（对标 hide；等价 setVisible(false)）。 @param self 目标轴指针。 @return 无返回值。 */
void XAbstractAxis_hide(XAbstractAxis* self);

/** @brief 设置轴线可见（对标 setLineVisible）。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractAxis_setLineVisible(XAbstractAxis* self, bool visible);
/** @brief 查询轴线可见（对标 isLineVisible）。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_isLineVisible(const XAbstractAxis* self);
/** @brief 设置轴线画笔（颜色 + 线宽；对标 setLinePen）。
 * @param self 目标轴指针。
 * @param color ARGB 颜色；0=主题色。
 * @param width 线宽（像素）。
 * @return 无返回值。 */
void XAbstractAxis_setLinePen(XAbstractAxis* self, uint32_t color, double width);
/** @brief 读取轴线画笔（参数化；对标 linePen）。
 * @param self 目标轴指针。
 * @param color 输出颜色；可空。
 * @param width 输出线宽；可空。
 * @return 无返回值。 */
void XAbstractAxis_linePen(const XAbstractAxis* self, uint32_t* color, double* width);
/** @brief 设置轴线颜色（对标 setLinePenColor）。 @param self 目标轴指针。 @param color ARGB。 @return 无返回值。 */
void XAbstractAxis_setLinePenColor(XAbstractAxis* self, uint32_t color);
/** @brief 读取轴线颜色（对标 linePenColor）。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_linePenColor(const XAbstractAxis* self);

/* ==================== 网格线（对标 QAbstractAxis） ==================== */

/** @brief 设置主网格线可见（对标 setGridLineVisible）。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractAxis_setGridLineVisible(XAbstractAxis* self, bool visible);
/** @brief 查询主网格线可见（对标 isGridLineVisible）。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_isGridLineVisible(const XAbstractAxis* self);
/** @brief 设置主网格线画笔（对标 setGridLinePen）。 @param self 目标轴指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XAbstractAxis_setGridLinePen(XAbstractAxis* self, uint32_t color, double width);
/** @brief 读取主网格线画笔（对标 gridLinePen）。 @param self 目标轴指针。 @param color 输出颜色；可空。 @param width 输出线宽；可空。 @return 无返回值。 */
void XAbstractAxis_gridLinePen(const XAbstractAxis* self, uint32_t* color, double* width);
/** @brief 设置主网格线颜色（对标 setGridLineColor）。 @param self 目标轴指针。 @param color ARGB。 @return 无返回值。 */
void XAbstractAxis_setGridLineColor(XAbstractAxis* self, uint32_t color);
/** @brief 读取主网格线颜色（对标 gridLineColor）。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_gridLineColor(const XAbstractAxis* self);

/** @brief 设置次网格线可见（对标 setMinorGridLineVisible）。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractAxis_setMinorGridLineVisible(XAbstractAxis* self, bool visible);
/** @brief 查询次网格线可见（对标 isMinorGridLineVisible）。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_isMinorGridLineVisible(const XAbstractAxis* self);
/** @brief 设置次网格线画笔（对标 setMinorGridLinePen）。 @param self 目标轴指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XAbstractAxis_setMinorGridLinePen(XAbstractAxis* self, uint32_t color, double width);
/** @brief 读取次网格线画笔（对标 minorGridLinePen）。 @param self 目标轴指针。 @param color 输出颜色；可空。 @param width 输出线宽；可空。 @return 无返回值。 */
void XAbstractAxis_minorGridLinePen(const XAbstractAxis* self, uint32_t* color, double* width);
/** @brief 设置次网格线颜色（对标 setMinorGridLineColor）。 @param self 目标轴指针。 @param color ARGB。 @return 无返回值。 */
void XAbstractAxis_setMinorGridLineColor(XAbstractAxis* self, uint32_t color);
/** @brief 读取次网格线颜色（对标 minorGridLineColor）。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_minorGridLineColor(const XAbstractAxis* self);

/* ==================== 刻度标签（对标 QAbstractAxis） ==================== */

/** @brief 设置标签可见（对标 setLabelsVisible）。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractAxis_setLabelsVisible(XAbstractAxis* self, bool visible);
/** @brief 查询标签可见（对标 labelsVisible）。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_labelsVisible(const XAbstractAxis* self);
/** @brief 设置标签画刷颜色（对标 setLabelsBrush）。 @param self 目标轴指针。 @param color ARGB。 @return 无返回值。 */
void XAbstractAxis_setLabelsBrush(XAbstractAxis* self, uint32_t color);
/** @brief 读取标签画刷颜色（对标 labelsBrush）。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_labelsBrush(const XAbstractAxis* self);
/** @brief 设置标签字体（对标 setLabelsFont）。
 * @param self 目标轴指针。
 * @param family 借用 XString*；可为 NULL（保持默认）。
 * @param pointSize 字号（磅；0=保持默认）。
 * @return 无返回值。 */
void XAbstractAxis_setLabelsFont(XAbstractAxis* self, const XString* family, int pointSize);
/** @brief 设置标签字体（UTF-8 编码版本；对标 setLabelsFont）。
 * @param self 目标轴指针。
 * @param family UTF-8 字体族（借用，不转移所有权）；NULL 表示清空字体族。
 * @param pointSize 字号（磅；0=保持默认）。
 * @return 无返回值。 */
void XAbstractAxis_setLabelsFont_utf8(XAbstractAxis* self, const char* family,
                                      int pointSize);
/** @brief 设置标签字体（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param family UTF-8 字体族（借用）；NULL 表示清空。
 * @param pointSize 字号（磅；0=保持默认）。
 * @return 无返回值。 */
void XAbstractAxis_setLabelsFont_2(XAbstractAxis* self, const char* family,
                                   int pointSize);
/** @brief 读取标签字体族（借用；对标 labelsFont().family）。
 * @param self 目标轴指针。
 * @return 借用 XString*；`@warning` 归对象所有，随下次 setLabelsFont 或析构失效，调用者不得释放。
 */
const XString* XAbstractAxis_labelsFontFamily_const(const XAbstractAxis* self);
/** @brief 读取标签字体族（兼容别名，转发 _const 借用版）。
 * @param self 目标轴指针。
 * @return 借用 XString*；归对象所有，调用者不得释放。 */
const XString* XAbstractAxis_labelsFontFamily(const XAbstractAxis* self);
/** @brief 读取标签字体族（UTF-8 借用；未设置空串）。 @param self 目标轴指针。 @return UTF-8 串。 */
const char* XAbstractAxis_labelsFontFamily_2(const XAbstractAxis* self);
/** @brief 读取标签字号（对标 labelsFont().pointSize）。 @param self 目标轴指针。 @return 字号（磅；0=默认）。 */
int XAbstractAxis_labelsFontSize(const XAbstractAxis* self);
/** @brief 读取标签字体（对标 labelsFont()；等价 labelsFontFamily + labelsFontSize）。
 * @param self 目标轴指针。
 * @param family 输出字体族借用指针；可空。
 * @param pointSize 输出字号；可空。
 * @return 无返回值。 */
void XAbstractAxis_labelsFont(const XAbstractAxis* self,
                              const XString** family, int* pointSize);
/** @brief 设置标签角度（对标 setLabelsAngle）。 @param self 目标轴指针。 @param angle 角度（度）。 @return 无返回值。 */
void XAbstractAxis_setLabelsAngle(XAbstractAxis* self, int angle);
/** @brief 查询标签角度（对标 labelsAngle）。 @param self 目标轴指针。 @return 角度（度）。 */
int XAbstractAxis_labelsAngle(const XAbstractAxis* self);
/** @brief 设置标签文字颜色（对标 setLabelsColor）。 @param self 目标轴指针。 @param color ARGB。 @return 无返回值。 */
void XAbstractAxis_setLabelsColor(XAbstractAxis* self, uint32_t color);
/** @brief 读取标签文字颜色（对标 labelsColor）。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_labelsColor(const XAbstractAxis* self);
/** @brief 设置标签可编辑（对标 setLabelsEditable）。 @param self 目标轴指针。 @param editable true 可编辑。 @return 无返回值。 */
void XAbstractAxis_setLabelsEditable(XAbstractAxis* self, bool editable);
/** @brief 查询标签可编辑（对标 labelsEditable）。 @param self 目标轴指针。 @return 可编辑返回 true。 */
bool XAbstractAxis_labelsEditable(const XAbstractAxis* self);
/** @brief 查询标签是否被截断（对标 labelsTruncated；只读派生）。 @param self 目标轴指针。 @return 截断返回 true。 */
bool XAbstractAxis_labelsTruncated(const XAbstractAxis* self);
/** @brief 设置标签截断（对标 setTruncateLabels；Qt 6.2+）。 @param self 目标轴指针。 @param truncate true 截断。 @return 无返回值。 */
void XAbstractAxis_setTruncateLabels(XAbstractAxis* self, bool truncate);
/** @brief 查询标签截断（对标 truncateLabels；Qt 6.2+）。 @param self 目标轴指针。 @return 截断返回 true。 */
bool XAbstractAxis_truncateLabels(const XAbstractAxis* self);

/* ==================== 轴标题（对标 QAbstractAxis） ==================== */

/** @brief 设置标题可见（对标 setTitleVisible）。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractAxis_setTitleVisible(XAbstractAxis* self, bool visible);
/** @brief 查询标题可见（对标 isTitleVisible）。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_isTitleVisible(const XAbstractAxis* self);
/** @brief 设置标题画刷颜色（对标 setTitleBrush）。 @param self 目标轴指针。 @param color ARGB。 @return 无返回值。 */
void XAbstractAxis_setTitleBrush(XAbstractAxis* self, uint32_t color);
/** @brief 读取标题画刷颜色（对标 titleBrush）。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_titleBrush(const XAbstractAxis* self);
/** @brief 设置标题字体（对标 setTitleFont）。
 * @param self 目标轴指针。
 * @param family 借用 XString*；可为 NULL（保持默认）。
 * @param pointSize 字号（磅；0=保持默认）。
 * @return 无返回值。 */
void XAbstractAxis_setTitleFont(XAbstractAxis* self, const XString* family, int pointSize);
/** @brief 设置标题字体（UTF-8 编码版本；对标 setTitleFont）。
 * @param self 目标轴指针。
 * @param family UTF-8 字体族（借用，不转移所有权）；NULL 表示清空字体族。
 * @param pointSize 字号（磅；0=保持默认）。
 * @return 无返回值。 */
void XAbstractAxis_setTitleFont_utf8(XAbstractAxis* self, const char* family,
                                     int pointSize);
/** @brief 设置标题字体（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param family UTF-8 字体族（借用）；NULL 表示清空。
 * @param pointSize 字号（磅；0=保持默认）。
 * @return 无返回值。 */
void XAbstractAxis_setTitleFont_2(XAbstractAxis* self, const char* family,
                                  int pointSize);
/** @brief 读取标题字体族（借用；对标 titleFont().family）。
 * @param self 目标轴指针。
 * @return 借用 XString*；归对象所有，随下次 setTitleFont 或析构失效，调用者不得释放。
 */
const XString* XAbstractAxis_titleFontFamily_const(const XAbstractAxis* self);
/** @brief 读取标题字体族（兼容别名，转发 _const 借用版）。
 * @param self 目标轴指针。
 * @return 借用 XString*；归对象所有，调用者不得释放。 */
const XString* XAbstractAxis_titleFontFamily(const XAbstractAxis* self);
/** @brief 读取标题字体族（UTF-8 借用；未设置空串）。 @param self 目标轴指针。 @return UTF-8 串。 */
const char* XAbstractAxis_titleFontFamily_2(const XAbstractAxis* self);
/** @brief 读取标题字号（对标 titleFont().pointSize）。 @param self 目标轴指针。 @return 字号（磅；0=默认）。 */
int XAbstractAxis_titleFontSize(const XAbstractAxis* self);
/** @brief 读取标题字体（对标 titleFont()；等价 titleFontFamily + titleFontSize）。
 * @param self 目标轴指针。
 * @param family 输出字体族借用指针；可空。
 * @param pointSize 输出字号；可空。
 * @return 无返回值。 */
void XAbstractAxis_titleFont(const XAbstractAxis* self,
                             const XString** family, int* pointSize);
/** @brief 设置轴标题（XString 主版本；对标 setTitleText）。
 * @param self 目标轴指针。
 * @param title 借用 XString*；可为 NULL（清空）。
 * @return 无返回值（变化时发射 titleTextChanged）。 */
void XAbstractAxis_setTitleText(XAbstractAxis* self, const XString* title);
/** @brief 设置轴标题（UTF-8 编码版本；对标 setTitleText）。
 * @param self 目标轴指针。
 * @param title UTF-8 标题串（借用，不转移所有权）；NULL 表示清空。
 * @return 无返回值（内容变化时发射 titleTextChanged）。 */
void XAbstractAxis_setTitleText_utf8(XAbstractAxis* self, const char* title);
/** @brief 设置轴标题（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param title UTF-8 标题串（借用）；NULL 表示清空。
 * @return 无返回值。 */
void XAbstractAxis_setTitleText_2(XAbstractAxis* self, const char* title);
/** @brief 读取轴标题（借用；对标 titleText）。
 * @param self 目标轴指针。
 * @return 借用 XString*；归对象所有，随下次 setTitleText 或析构失效，调用者不得释放。
 */
const XString* XAbstractAxis_titleText_const(const XAbstractAxis* self);
/** @brief 读取轴标题（兼容别名，转发 _const 借用版）。
 * @param self 目标轴指针。
 * @return 借用 XString*；归对象所有，调用者不得释放。 */
const XString* XAbstractAxis_titleText(const XAbstractAxis* self);
/** @brief 读取轴标题（UTF-8 借用；未设置返回空串）。 @param self 目标轴指针。 @return UTF-8 串。 */
const char* XAbstractAxis_titleText_2(const XAbstractAxis* self);

/* ==================== 阴影带（对标 QAbstractAxis） ==================== */

/** @brief 设置阴影带可见（对标 setShadesVisible）。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractAxis_setShadesVisible(XAbstractAxis* self, bool visible);
/** @brief 查询阴影带可见（对标 shadesVisible）。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_shadesVisible(const XAbstractAxis* self);
/** @brief 设置阴影带画笔（对标 setShadesPen）。 @param self 目标轴指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XAbstractAxis_setShadesPen(XAbstractAxis* self, uint32_t color, double width);
/** @brief 读取阴影带画笔（对标 shadesPen）。 @param self 目标轴指针。 @param color 输出颜色；可空。 @param width 输出线宽；可空。 @return 无返回值。 */
void XAbstractAxis_shadesPen(const XAbstractAxis* self, uint32_t* color, double* width);
/** @brief 设置阴影带画刷颜色（对标 setShadesBrush）。 @param self 目标轴指针。 @param color ARGB。 @return 无返回值。 */
void XAbstractAxis_setShadesBrush(XAbstractAxis* self, uint32_t color);
/** @brief 读取阴影带画刷颜色（对标 shadesBrush）。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_shadesBrush(const XAbstractAxis* self);
/** @brief 设置阴影带颜色（对标 setShadesColor；等价设画刷色）。 @param self 目标轴指针。 @param color ARGB。 @return 无返回值。 */
void XAbstractAxis_setShadesColor(XAbstractAxis* self, uint32_t color);
/** @brief 读取阴影带颜色（对标 shadesColor）。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_shadesColor(const XAbstractAxis* self);
/** @brief 设置阴影带边框颜色（对标 setShadesBorderColor；等价设画笔色）。 @param self 目标轴指针。 @param color ARGB。 @return 无返回值。 */
void XAbstractAxis_setShadesBorderColor(XAbstractAxis* self, uint32_t color);
/** @brief 读取阴影带边框颜色（对标 shadesBorderColor）。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_shadesBorderColor(const XAbstractAxis* self);

/* ==================== 方向 / 范围 / 反转（对标 QAbstractAxis） ==================== */

/** @brief 查询轴朝向（对标 orientation；0=水平 1=垂直，见 XGuiOrientation）。 @param self 目标轴指针。 @return 朝向。 */
int XAbstractAxis_orientation(const XAbstractAxis* self);
/** @brief 查询轴对齐（对标 alignment；Qt::Alignment 位集合）。 @param self 目标轴指针。 @return 对齐位集合。 */
uint32_t XAbstractAxis_alignment(const XAbstractAxis* self);

/** @brief 设置轴范围下限（对标 setMin；非数值轴忽略）。
 * @param self 目标轴指针。 @param min 下限。 @return 无返回值。 */
void XAbstractAxis_setMin(XAbstractAxis* self, double min);
/** @brief 设置轴范围上限（对标 setMax；非数值轴忽略）。
 * @param self 目标轴指针。 @param max 上限。 @return 无返回值。 */
void XAbstractAxis_setMax(XAbstractAxis* self, double max);
/** @brief 设置轴范围（对标 setRange）。
 * @param self 目标轴指针。 @param min 下限。 @param max 上限（须 >= min）。
 * @return 无返回值（变化时发射 rangeChanged）。 */
void XAbstractAxis_setRange(XAbstractAxis* self, double min, double max);
/** @brief 查询轴范围下限（对标 min）。 @param self 目标轴指针。 @return 下限。 */
double XAbstractAxis_min(const XAbstractAxis* self);
/** @brief 查询轴范围上限（对标 max）。 @param self 目标轴指针。 @return 上限。 */
double XAbstractAxis_max(const XAbstractAxis* self);

/** @brief 设置反转方向（对标 setReverse）。 @param self 目标轴指针。 @param reverse true 反转。 @return 无返回值。 */
void XAbstractAxis_setReverse(XAbstractAxis* self, bool reverse);
/** @brief 查询是否反转（对标 isReverse）。 @param self 目标轴指针。 @return 反转返回 true。 */
bool XAbstractAxis_isReverse(const XAbstractAxis* self);

/* ==================== 类型查询 ==================== */

/**
 * @brief 查询轴类型（对标 QAbstractAxis::type）。
 * @param self 目标轴指针。
 * @return XAbstractAxis_AxisType 位值；未知轴返回 AxisTypeNoAxis。
 */
int XAbstractAxis_type(const XAbstractAxis* self);

/* ==================== 信号（对标 QAbstractAxis Q_SIGNALS） ==================== */

/** @brief visibleChanged 信号（载荷：bool 可见性）。 */
void* XAbstractAxis_visibleChanged_signal(XAbstractAxis* self, bool visible);
/** @brief lineVisibleChanged 信号（载荷：bool 可见性）。 */
void* XAbstractAxis_lineVisibleChanged_signal(XAbstractAxis* self, bool visible);
/** @brief linePenChanged 信号（载荷：颜色, 线宽）。 */
void* XAbstractAxis_linePenChanged_signal(XAbstractAxis* self, uint32_t color, double width);
/** @brief colorChanged 信号（载荷：颜色）。 */
void* XAbstractAxis_colorChanged_signal(XAbstractAxis* self, uint32_t color);
/** @brief labelsVisibleChanged 信号（载荷：bool 可见性）。 */
void* XAbstractAxis_labelsVisibleChanged_signal(XAbstractAxis* self, bool visible);
/** @brief labelsBrushChanged 信号（载荷：颜色）。 */
void* XAbstractAxis_labelsBrushChanged_signal(XAbstractAxis* self, uint32_t color);
/** @brief labelsFontChanged 信号（载荷：字体族, 字号）。 */
void* XAbstractAxis_labelsFontChanged_signal(XAbstractAxis* self, const char* family, int pointSize);
/** @brief labelsAngleChanged 信号（载荷：角度）。 */
void* XAbstractAxis_labelsAngleChanged_signal(XAbstractAxis* self, int angle);
/** @brief labelsColorChanged 信号（载荷：颜色）。 */
void* XAbstractAxis_labelsColorChanged_signal(XAbstractAxis* self, uint32_t color);
/** @brief labelsEditableChanged 信号（载荷：可编辑）。 */
void* XAbstractAxis_labelsEditableChanged_signal(XAbstractAxis* self, bool editable);
/** @brief labelsTruncatedChanged 信号（载荷：截断；Qt 6.2+）。 */
void* XAbstractAxis_labelsTruncatedChanged_signal(XAbstractAxis* self, bool truncated);
/** @brief truncateLabelsChanged 信号（载荷：截断；Qt 6.2+）。 */
void* XAbstractAxis_truncateLabelsChanged_signal(XAbstractAxis* self, bool truncate);
/** @brief gridVisibleChanged 信号（载荷：可见性）。 */
void* XAbstractAxis_gridVisibleChanged_signal(XAbstractAxis* self, bool visible);
/** @brief gridLinePenChanged 信号（载荷：颜色, 线宽）。 */
void* XAbstractAxis_gridLinePenChanged_signal(XAbstractAxis* self, uint32_t color, double width);
/** @brief gridLineColorChanged 信号（载荷：颜色）。 */
void* XAbstractAxis_gridLineColorChanged_signal(XAbstractAxis* self, uint32_t color);
/** @brief minorGridVisibleChanged 信号（载荷：可见性）。 */
void* XAbstractAxis_minorGridVisibleChanged_signal(XAbstractAxis* self, bool visible);
/** @brief minorGridLinePenChanged 信号（载荷：颜色, 线宽）。 */
void* XAbstractAxis_minorGridLinePenChanged_signal(XAbstractAxis* self, uint32_t color, double width);
/** @brief minorGridLineColorChanged 信号（载荷：颜色）。 */
void* XAbstractAxis_minorGridLineColorChanged_signal(XAbstractAxis* self, uint32_t color);
/** @brief shadesVisibleChanged 信号（载荷：可见性）。 */
void* XAbstractAxis_shadesVisibleChanged_signal(XAbstractAxis* self, bool visible);
/** @brief shadesPenChanged 信号（载荷：颜色, 线宽）。 */
void* XAbstractAxis_shadesPenChanged_signal(XAbstractAxis* self, uint32_t color, double width);
/** @brief shadesBrushChanged 信号（载荷：颜色）。 */
void* XAbstractAxis_shadesBrushChanged_signal(XAbstractAxis* self, uint32_t color);
/** @brief shadesColorChanged 信号（载荷：颜色）。 */
void* XAbstractAxis_shadesColorChanged_signal(XAbstractAxis* self, uint32_t color);
/** @brief shadesBorderColorChanged 信号（载荷：颜色）。 */
void* XAbstractAxis_shadesBorderColorChanged_signal(XAbstractAxis* self, uint32_t color);
/** @brief titleTextChanged 信号（载荷：const XString* 标题）。 */
void* XAbstractAxis_titleTextChanged_signal(XAbstractAxis* self, const XString* title);
/** @brief titleBrushChanged 信号（载荷：颜色）。 */
void* XAbstractAxis_titleBrushChanged_signal(XAbstractAxis* self, uint32_t color);
/** @brief titleVisibleChanged 信号（载荷：可见性）。 */
void* XAbstractAxis_titleVisibleChanged_signal(XAbstractAxis* self, bool visible);
/** @brief titleFontChanged 信号（载荷：字体族, 字号）。 */
void* XAbstractAxis_titleFontChanged_signal(XAbstractAxis* self, const char* family, int pointSize);
/** @brief rangeChanged 信号（载荷：double min, double max）。 */
void* XAbstractAxis_rangeChanged_signal(XAbstractAxis* self, double min, double max);
/** @brief reverseChanged 信号（载荷：bool 反转）。 */
void* XAbstractAxis_reverseChanged_signal(XAbstractAxis* self, bool reverse);

#ifdef __cplusplus
}
#endif
#endif /* XCHARTS_ON */

#endif /* XGUI_ON */

#endif /* XABSTRACT_AXIS_H */
