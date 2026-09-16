/**
 * @file       XCategoryAxis.h
 * @brief      XCategoryAxis 类别轴（对标 Qt Charts 6.8 QBarCategoryAxis）。
 * @details    以 m_base 组合继承 XAbstractAxis（可见性/网格/标题/范围在
 *             基类）；本类持有类别字符串列表。范围语义按 QBarCategoryAxis：
 *             min 固定 0、max 随类别数（append/remove/clear 后回写），
 *             setRange 仅允许裁剪 [0, count]。渲染由 XChart 完成。
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
#include "XAbstractAxis.h"

#if XCHARTS_ON

/** @brief 类别轴（m_base 必须是第一个成员；对标 QBarCategoryAxis）。 */
typedef struct XCategoryAxis
{
    XAbstractAxis m_base;     /**< 基类成员；必须是第一个（可见/范围/标题等）。 */
    XString** m_categories;   /**< 类别标签数组（对象拥有；堆）。 */
    int m_count;              /**< 类别数。 */
    int m_capacity;           /**< 容量。 */
} XCategoryAxis;

void XCategoryAxis_init(XCategoryAxis* self);
/** @brief 使用指定内存类型创建类别轴。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XCategoryAxis* XCategoryAxis_create_ex(XMemoryType memory);
#define XCategoryAxis_create() XCategoryAxis_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/** @brief 反初始化（释放基类资源与类别数组）。 */
/** @brief 反初始化（释放类别数组与基类资源）。 */
void XCategoryAxis_deinit_impl(XCategoryAxis* self);
#define XCategoryAxis_deinit_base(self) XCategoryAxis_deinit_impl(self)
/** @brief 删除堆上类别轴（反初始化并释放结构体）。 */
#define XCategoryAxis_delete_base(self) \
    XCategoryAxis_deinit_base(self), XFree_System(self)

/** @brief 追加类别（XString 主版本；对标 QBarCategoryAxis::append）。
 * @param self 目标轴指针。
 * @param label 借用 XString*；不能为 NULL。
 * @return 下标；失败 -1。 */
int XCategoryAxis_append(XCategoryAxis* self, const XString* label);
/** @brief 追加类别（UTF-8 兼容重载，转发主版本）。 */
int XCategoryAxis_append_2(XCategoryAxis* self, const char* label);
/** @brief 查询类别数。 @param self 目标轴指针。 @return 类别数。 */
int XCategoryAxis_count(const XCategoryAxis* self);
/** @brief 按下标取类别（内部借用 XString*；不得释放）。
 * @param self 目标轴指针。
 * @param index 下标。
 * @return 借用 XString*；越界 NULL。 */
const XString* XCategoryAxis_category(const XCategoryAxis* self, int index);
/** @brief 按下标取类别（UTF-8 借用；越界空串）。 */
const char* XCategoryAxis_category_2(const XCategoryAxis* self, int index);
/** @brief 设置轴可见性（转发基类）。 @param self 目标轴指针。 @param visible true 显示。 @return 无返回值。 */
void XCategoryAxis_setVisible(XCategoryAxis* self, bool visible);
/** @brief 查询轴范围下限（QBarCategoryAxis 语义：恒 0）。
 * @param self 目标轴指针。 @return 下限。 */
double XCategoryAxis_min(const XCategoryAxis* self);
/** @brief 查询轴范围上限（= 类别数）。
 * @param self 目标轴指针。 @return 上限。 */
double XCategoryAxis_max(const XCategoryAxis* self);
/** @brief 设置类别轴范围（仅允许 [0, count] 裁剪）。
 * @param self 目标轴指针。
 * @param min 下限（按 0 钳位）。
 * @param max 上限（按 count 钳位；须 >= min）。
 * @return 无返回值。
 */
void XCategoryAxis_setRange(XCategoryAxis* self, double min, double max);
/** @brief countChanged 信号地址（类别数变化时由本轴发射）。 */
void* XCategoryAxis_countChanged_signal(XCategoryAxis* self, int count);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XCATEGORYAXIS_H */