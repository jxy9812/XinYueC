/**
 * @file       XValueAxis.h
 * @brief      XValueAxis 数值轴（对标 Qt Charts 6.8 QValueAxis）。
 * @details    持有 min/max/tickCount/labelFormat；渲染由 XChart 完成。
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
#include "XString.h"

#if XCHARTS_ON

/** @brief 数值轴（对标 QValueAxis）。 */
typedef struct XValueAxis
{
    double m_min;             /**< 轴最小值。 */
    double m_max;             /**< 轴最大值。 */
    int m_tickCount;          /**< 刻度数（含端点，默认 6）。 */
    XString* m_labelFormat;   /**< 刻度标签格式（对象拥有；printf 风格）。 */
    XString* m_titleText;     /**< 轴标题（对象拥有）。 */
    bool m_visible;           /**< 轴可见（默认 true）。 */
    bool m_gridVisible;       /**< 网格线可见（默认 true）。 */
} XValueAxis;

void XValueAxis_init(XValueAxis* self);

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
/** @brief 设置刻度标签格式。 @param self 目标轴指针。 @param fmt printf 风格格式。 @return 无返回值。 */
void XValueAxis_setLabelFormat(XValueAxis* self, const char* fmt);
/** @brief 设置轴标题。 @param self 目标轴指针。 @param title UTF-8 标题。 @return 无返回值。 */
void XValueAxis_setTitleText(XValueAxis* self, const char* title);
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