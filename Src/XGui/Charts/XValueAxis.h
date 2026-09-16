/**
 * @file       XValueAxis.h
 * @brief      XValueAxis 数值轴（对标 Qt Charts 6.8 QValueAxis）。
 * @details    以 m_base 组合继承 XAbstractAxis（范围/可见性/标题等公共
 *             属性与信号在基类）；本类持有 tickCount/labelFormat 专用
 *             状态。渲染由 XChart 完成。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVALUEAXIS_H
#define XVALUEAXIS_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractAxis.h"

#if XCHARTS_ON

/** @brief 数值轴（m_base 必须是第一个成员；对标 QValueAxis : QAbstractAxis）。 */
typedef struct XValueAxis
{
    XAbstractAxis m_base;     /**< 基类成员；必须是第一个（范围/可见/标题等）。 */
    int m_tickCount;          /**< 刻度数（含端点，默认 6）。 */
    XString* m_labelFormat;   /**< 刻度标签格式（对象拥有；printf 风格）。 */
} XValueAxis;

void XValueAxis_init(XValueAxis* self);
/** @brief 反初始化（释放基类资源与标签格式；栈/堆对象统一入口）。 */
/** @brief 反初始化（释放标签格式与基类资源；栈/堆对象统一入口）。 */
void XValueAxis_deinit_impl(XValueAxis* self);
#define XValueAxis_deinit_base(self) XValueAxis_deinit_impl(self)
/** @brief 删除堆上数值轴（反初始化并释放结构体；结构体按 XChart 现有
 *         约定由 XFree_System 释放，本宏供独立创建/释放场景使用）。 */
#define XValueAxis_delete_base(self) \
    XValueAxis_deinit_base(self), XFree_System(self)

/** @brief 设置轴范围。 @param self 目标轴指针。 @param min 最小值。 @param max 最大值。 @return 无返回值。 */
void XValueAxis_setRange(XValueAxis* self, double min, double max);
/** @brief 查询轴最小值。 @param self 目标轴指针。 @return 最小值。 */
double XValueAxis_min(const XValueAxis* self);
/** @brief 查询轴最大值。 @param self 目标轴指针。 @return 最大值。 */
double XValueAxis_max(const XValueAxis* self);
/** @brief 设置刻度数。 @param self 目标轴指针。 @param count 刻度数（≥2）。 @return 无返回值。 */
void XValueAxis_setTickCount(XValueAxis* self, int count);
/** @brief 查询刻度数。 @param self 目标轴指针。 @return 刻度数。 */
int XValueAxis_tickCount(const XValueAxis* self);
/** @brief 设置刻度标签格式（XString 主版本；对标 setLabelFormat）。
 * @param self 目标轴指针。
 * @param fmt 借用 XString*；可为 NULL（恢复默认 "%g"）。
 * @return 无返回值。 */
void XValueAxis_setLabelFormat(XValueAxis* self, const XString* fmt);
/** @brief 设置刻度标签格式（UTF-8 兼容重载，转发主版本）。 */
void XValueAxis_setLabelFormat_2(XValueAxis* self, const char* fmt);
/** @brief 设置轴标题（XString 主版本；对标 setTitleText）。
 * @param self 目标轴指针。
 * @param title 借用 XString*；可为 NULL（清空）。
 * @return 无返回值。 */
void XValueAxis_setTitleText(XValueAxis* self, const XString* title);
/** @brief 设置轴标题（UTF-8 兼容重载，转发主版本）。 */
void XValueAxis_setTitleText_2(XValueAxis* self, const char* title);
/** @brief 设置轴可见性。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XValueAxis_setVisible(XValueAxis* self, bool visible);
/** @brief 查询轴可见性。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XValueAxis_isVisible(const XValueAxis* self);
/** @brief 查询网格线可见性。 @param self 目标轴指针。 @return 可见返回 true。 */
bool XValueAxis_isGridVisible(const XValueAxis* self);
/** @brief 设置网格线可见性。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XValueAxis_setGridVisible(XValueAxis* self, bool visible);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XVALUEAXIS_H */