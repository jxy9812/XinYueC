/**
 * @file       XPolarChart.h
 * @brief      XPolarChart 极坐标图表（对标 Qt Charts 6.8 QPolarChart）。
 * @details    继承 XChart（对应 QPolarChart : QChart）；在直角坐标系之上
 *             增加「轴 → 极坐标角色」的绑定：径向轴（半径方向，通常是值轴）
 *             与角度轴（绕圆心扫掠，通常是类别/时间轴）。
 *
 *             Qt 中 QPolarChart::addAxis 接受 PolarOrientation 指定轴角色；
 *             本库以同语义入口承载。渲染由 XChartView 读取角色后走极坐标
 *             映射（网格为同心圆 + 角度射线），直角坐标图表行为不变。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPOLARCHART_H
#define XPOLARCHART_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XChart.h"

#if XCHARTS_ON

/**
 * @brief      极坐标轴角色（对标 Qt Charts 6.8 QPolarChart::PolarOrientation，
 *             数值一致；可按位组合用于 PolarOrientations）。
 */
typedef enum XPolarChart_PolarOrientation
{
    XPolarChart_PolarOrientation_Radial = 0x1,   /**< 径向（半径方向；对标 PolarOrientationRadial）。 */
    XPolarChart_PolarOrientation_Angular = 0x2   /**< 角度（绕圆心；对标 PolarOrientationAngular）。 */
} XPolarChart_PolarOrientation;

/** @brief 极坐标轴角色集合（可按位组合；对标 QPolarChart::PolarOrientations）。 */
typedef uint32_t XPolarChart_PolarOrientations;

XCLASS_DEFINE_BEGING(XPolarChart)
XCLASS_DEFINE_EXTEND_END(XPolarChart, XChart)

/** @brief 极坐标图表（m_base 必须是第一个成员；对标 QPolarChart : QChart）。 */
typedef struct XPolarChart
{
    XChart m_base;                        /**< 基类成员；必须是第一个。 */
    XAbstractAxis* m_radialAxis;          /**< 径向轴（借用；不拥有）。 */
    XAbstractAxis* m_angularAxis;         /**< 角度轴（借用；不拥有）。 */
} XPolarChart;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XPolarChart_class_init(void);

/**
 * @brief 初始化嵌入式极坐标图表（ChartType 置 Polar）。
 * @param self 目标图表指针，不能为空。
 * @return 无返回值。
 */
void XPolarChart_init(XPolarChart* self);

/**
 * @brief 堆上创建极坐标图表。
 * @param memory 内存类型。
 * @return 图表指针；分配失败返回 NULL。
 */
XPolarChart* XPolarChart_create_ex(XMemoryType memory);
#define XPolarChart_create() XPolarChart_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 轴角色绑定（对标 QPolarChart） ==================== */

/**
 * @brief 按角色挂接轴（对标 QPolarChart::addAxis(axis, polarOrientation)）。
 * @details 径向轴沿半径方向取值（通常是值轴），角度轴绕圆心扫掠
 *          （通常是类别轴/时间轴）。轴所有权仍归图表（转交基类登记），
 *          本入口只记录角色。
 * @param self 目标图表指针。
 * @param axis 轴指针（借用；生命周期由调用方或 XChart 管理）。
 * @param polarOrientation 轴角色（XPolarChart_PolarOrientation）。
 * @return 无返回值。
 */
void XPolarChart_addAxis(XPolarChart* self, XAbstractAxis* axis,
                         int polarOrientation);

/**
 * @brief 按角色读取轴（对标 QPolarChart::axes(polarOrientation)）。
 * @param self 目标图表指针。
 * @param polarOrientation 轴角色（可按位组合；0 表示全部）。
 * @param out 输出缓冲（调用方提供，至少 maxCount 项）。
 * @param maxCount 缓冲容量。
 * @return 实际写入的轴数。
 */
int XPolarChart_axes(const XPolarChart* self, int polarOrientation,
                     XAbstractAxis** out, int maxCount);

/**
 * @brief 查询某轴的极坐标角色（对标 QPolarChart::axisPolarOrientation）。
 * @param self 目标图表指针。
 * @param axis 轴指针。
 * @return 角色位值；轴未绑定返回 0。
 */
int XPolarChart_axisPolarOrientation(const XPolarChart* self,
                                     XAbstractAxis* axis);

/** @brief 查询图表类型（对标 QPolarChart::chartType；恒为 Polar）。 @param self 目标图表指针。 @return 固定返回 XChart_ChartType_Polar。 */
int XPolarChart_chartType(const XPolarChart* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XPOLARCHART_H */
