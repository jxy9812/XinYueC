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
 * @brief      抽象坐标轴对象；m_base 必须是第一个成员（嵌 XObject）。
 * @details    字段含义（对标 QAbstractAxis 同名属性）：
 *             - m_visible：轴可见（默认 true）；
 *             - m_gridLineVisible：网格线可见（默认 true）；
 *             - m_min/m_max：范围（默认 0/10；类别轴 max 随类别数推导）；
 *             - m_reverse：反转（默认 false）；
 *             - m_titleText：轴标题（对象拥有）；
 *             - m_labelsAngle：标签角度（度，默认 0）；
 *             - m_shadesVisible：阴影带可见（默认 false）；
 *             - m_linePenColor/m_labelsBrushColor：简化画笔/标签画刷颜色。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XAbstractAxis
{
    XObject   m_base;            /**< 基类成员；必须是第一个，由 XClass 管理。 */
    bool      m_visible;         /**< 轴可见（默认 true）。 */
    bool      m_gridLineVisible; /**< 网格线可见（默认 true）。 */
    double    m_min;             /**< 轴范围下限。 */
    double    m_max;             /**< 轴范围上限。 */
    bool      m_reverse;         /**< 反转方向（默认 false）。 */
    XString*  m_titleText;       /**< 轴标题（对象拥有）。 */
    int       m_labelsAngle;     /**< 标签角度（度，默认 0）。 */
    bool      m_shadesVisible;   /**< 阴影带可见（默认 false）。 */
    uint32_t  m_linePenColor;    /**< 简化线条颜色（ARGB；0=主题色）。 */
    uint32_t  m_labelsBrushColor; /**< 简化标签画刷颜色（ARGB；0=主题色）。 */
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
#define XAbstractAxis_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XAbstractAxis_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 属性（对标 QAbstractAxis） ==================== */

/** @brief 设置轴可见性。
 * @param self 目标轴指针。
 * @param visible true 显示。
 * @return 无返回值（变化时发射 visibleChanged）。
 */
void XAbstractAxis_setVisible(XAbstractAxis* self, bool visible);
/** @brief 查询轴可见性。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_isVisible(const XAbstractAxis* self);

/** @brief 设置网格线可见性。
 * @param self 目标轴指针。
 * @param visible true 显示。
 * @return 无返回值（变化时发射 gridLineVisibleChanged）。
 */
void XAbstractAxis_setGridLineVisible(XAbstractAxis* self, bool visible);
/** @brief 查询网格线可见性。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_isGridLineVisible(const XAbstractAxis* self);

/** @brief 设置轴范围。
 * @param self 目标轴指针。
 * @param min 下限。
 * @param max 上限（须 >= min）。
 * @return 无返回值（变化时发射 rangeChanged）。
 */
void XAbstractAxis_setRange(XAbstractAxis* self, double min, double max);
/** @brief 查询轴范围下限。 @param self 目标轴指针。 @return 下限。 */
double XAbstractAxis_min(const XAbstractAxis* self);
/** @brief 查询轴范围上限。 @param self 目标轴指针。 @return 上限。 */
double XAbstractAxis_max(const XAbstractAxis* self);

/** @brief 设置反转方向。
 * @param self 目标轴指针。
 * @param reverse true 反转。
 * @return 无返回值（变化时发射 reverseChanged）。
 */
void XAbstractAxis_setReverse(XAbstractAxis* self, bool reverse);
/** @brief 查询是否反转。 @param self 目标轴指针。 @return 反转返回 true。 */
bool XAbstractAxis_isReverse(const XAbstractAxis* self);

/** @brief 设置轴标题（XString 主版本；对标 setTitleText）。
 * @param self 目标轴指针。
 * @param title 借用 XString*；可为 NULL（清空）。
 * @return 无返回值（变化时发射 titleTextChanged）。
 */
void XAbstractAxis_setTitleText(XAbstractAxis* self, const XString* title);
/** @brief 设置轴标题（UTF-8 兼容重载，转发主版本）。 */
void XAbstractAxis_setTitleText_2(XAbstractAxis* self, const char* title);
/** @brief 读取轴标题（内部借用 XString*；不得释放）。 */
const XString* XAbstractAxis_titleText(const XAbstractAxis* self);
/** @brief 读取轴标题（UTF-8 借用；未设置返回空串）。 */
const char* XAbstractAxis_titleText_2(const XAbstractAxis* self);

/** @brief 设置标签角度。
 * @param self 目标轴指针。
 * @param angle 角度（度）。
 * @return 无返回值。
 */
void XAbstractAxis_setLabelsAngle(XAbstractAxis* self, int angle);
/** @brief 查询标签角度。 @param self 目标轴指针。 @return 角度（度）。 */
int XAbstractAxis_labelsAngle(const XAbstractAxis* self);

/** @brief 设置阴影带可见性。
 * @param self 目标轴指针。
 * @param visible true 显示。
 * @return 无返回值。
 */
void XAbstractAxis_setShadesVisible(XAbstractAxis* self, bool visible);
/** @brief 查询阴影带可见性。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XAbstractAxis_shadesVisible(const XAbstractAxis* self);

/** @brief 设置简化线条颜色（对标 setLinePenColor）。
 * @param self 目标轴指针。
 * @param color ARGB；0=主题色。
 * @return 无返回值。
 */
void XAbstractAxis_setLinePenColor(XAbstractAxis* self, uint32_t color);
/** @brief 查询简化线条颜色。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_linePenColor(const XAbstractAxis* self);

/** @brief 设置简化标签画刷颜色（对标 setLabelsBrush）。
 * @param self 目标轴指针。
 * @param color ARGB；0=主题色。
 * @return 无返回值。
 */
void XAbstractAxis_setLabelsBrushColor(XAbstractAxis* self, uint32_t color);
/** @brief 查询简化标签画刷颜色。 @param self 目标轴指针。 @return ARGB。 */
uint32_t XAbstractAxis_labelsBrushColor(const XAbstractAxis* self);

/* ==================== 信号（对标 QAbstractAxis） ==================== */

/** @brief visibleChanged 信号（载荷：bool 可见性）。 */
void* XAbstractAxis_visibleChanged_signal(XAbstractAxis* self, bool visible);
/** @brief gridLineVisibleChanged 信号（载荷：bool 可见性）。 */
void* XAbstractAxis_gridLineVisibleChanged_signal(XAbstractAxis* self, bool visible);
/** @brief titleTextChanged 信号（载荷：const XString* 标题）。 */
void* XAbstractAxis_titleTextChanged_signal(XAbstractAxis* self, const XString* title);
/** @brief rangeChanged 信号（载荷：double min, double max）。 */
void* XAbstractAxis_rangeChanged_signal(XAbstractAxis* self, double min, double max);
/** @brief reverseChanged 信号（载荷：bool 反转）。 */
void* XAbstractAxis_reverseChanged_signal(XAbstractAxis* self, bool reverse);

#ifdef __cplusplus
}
#endif
#endif /* XCHARTS_ON */
#endif /* XABSTRACT_AXIS_H */
