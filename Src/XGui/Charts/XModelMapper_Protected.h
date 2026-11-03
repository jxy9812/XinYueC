/**
 * @file       XModelMapper_Protected.h
 * @brief      模型映射器族内部共享助手（对标 Qt 的 *ModelMapperPrivate）。
 * @details    仅供 XModelMapper 族内部实现使用，不对外公开。集中提供
 *             「单元格取值」「按方向定位单元格」「可映射项数」三类原语，
 *             避免 H/V 子类各自重复实现。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XMODELMAPPER_PROTECTED_H
#define XMODELMAPPER_PROTECTED_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemModel.h"

#if XCHARTS_ON

/**
 * @brief      映射方向（对标 Qt::Orientation，数值一致）。
 * @details    整个映射器族共用。横向按列推进、纵向按行推进。
 */
typedef enum XModelMapper_Orientation
{
    XModelMapper_Orientation_Horizontal = 0, /**< 横向：按列推进（对标 Qt::Horizontal）。 */
    XModelMapper_Orientation_Vertical = 1    /**< 纵向：按行推进（对标 Qt::Vertical）。 */
} XModelMapper_Orientation;

/** @brief 把单元格文本解析成 double；空串或非法返回 0。
 * @param model 数据模型（借用）。
 * @param row 行下标。
 * @param col 列下标。
 * @return 解析结果。
 */
double xmodelmapper_cellToDouble(const XAbstractItemModel* model, int row,
                                 int col);

/** @brief 按方向把「区间首项 + 偏移 idx」映射到 (row,col)。
 * @param orientation 0=横向（按列推进），1=纵向（按行推进）。
 * @param firstIdx 起始行/列。
 * @param idx 推进下标。
 * @param row 输出行；调用方提供存储。
 * @param col 输出列；调用方提供存储。
 * @return 无返回值。
 */
void xmodelmapper_stepCell(int orientation, int firstIdx, int idx,
                           int* row, int* col);

/** @brief 按方向取「指定 section 在某项处的单元格」。
 * @param orientation 0=横向，1=纵向。
 * @param firstIdx 起始行/列（横向时为基准行，纵向时为基准列）。
 * @param section 行/列号（横向=行，纵向=列）。
 * @param idx 推进下标。
 * @param row 输出行；调用方提供存储。
 * @param col 输出列；调用方提供存储。
 * @return 无返回值。
 */
void xmodelmapper_sectionCell(int orientation, int firstIdx, int section,
                              int idx, int* row, int* col);

/** @brief 按方向计算可映射项数。
 * @param model 数据模型（借用）。
 * @param orientation 0=横向，1=纵向。
 * @param first 起始行/列。
 * @param count 请求数量（-1=按模型维度推导）。
 * @return 可映射项数；无模型返回 0。
 */
int xmodelmapper_availableCount(const XAbstractItemModel* model,
                                int orientation, int first, int count);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XMODELMAPPER_PROTECTED_H */
