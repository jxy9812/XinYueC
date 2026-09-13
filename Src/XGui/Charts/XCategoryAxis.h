/**
 * @file       XCategoryAxis.h
 * @brief      XCategoryAxis 类别轴（对标 Qt Charts 6.8 QCategoryAxis）。
 * @details    类别字符串列表（下标即刻度位置）；渲染由 XChart 完成。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCATEGORYAXIS_H
#define XCATEGORYAXIS_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"

#if XCHARTS_ON

/** @brief 类别轴（对标 QCategoryAxis）。 */
typedef struct XCategoryAxis
{
    char (*m_categories)[64]; /**< 类别标签数组（堆）。 */
    int m_count;              /**< 类别数。 */
    int m_capacity;           /**< 容量。 */
    bool m_visible;           /**< 轴可见（默认 true）。 */
    bool m_gridVisible;       /**< 网格线可见。 */
} XCategoryAxis;

void XCategoryAxis_init(XCategoryAxis* self);

/** @brief 追加类别。 @param self 目标轴指针。 @param label 类别标签。 @return 下标；失败 -1。 */
int XCategoryAxis_append(XCategoryAxis* self, const char* label);
/** @brief 查询类别数。 @param self 目标轴指针。 @return 类别数。 */
int XCategoryAxis_count(const XCategoryAxis* self);
/** @brief 按下标取类别。 @param self 目标轴指针。 @param index 下标。 @return 类别文本；越界空串。 */
const char* XCategoryAxis_category(const XCategoryAxis* self, int index);
/** @brief 设置轴可见性。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XCategoryAxis_setVisible(XCategoryAxis* self, bool visible);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XCATEGORYAXIS_H */