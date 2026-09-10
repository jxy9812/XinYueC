/**
 * @file       XRubberBand.h
 * @brief      XRubberBand 橡皮筋选择框（对标 Qt 6.8 QRubberBand 全部
 *             公共 API）。
 * @details    形状 Line/Rectangle（数值对齐）；setGeometry/move/resize
 *             沿用 XWidget 基类；绘制半透明填充+边框（第一版绘制为
 *             边框线）。继承 XWidget。
 * @note       模块总开关 XRUBBERBAND_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XRUBBERBAND_H
#define XRUBBERBAND_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XRUBBERBAND_ON

/** @brief 橡皮筋形状（对标 QRubberBand::Shape，数值一致）。 */
typedef enum XRubberBandShape
{
    XRubberBandShape_Line = 0,      /**< 线形（水平/垂直分隔参考线）。 */
    XRubberBandShape_Rectangle = 1  /**< 矩形框选。 */
} XRubberBandShape;

XCLASS_DEFINE_BEGING(XRubberBand)
XCLASS_DEFINE_EXTEND_END(XRubberBand, XWidget)

typedef struct XRubberBand
{
    XWidget m_base;          /**< 基类成员；必须是第一个。 */
    int m_shape;             /**< 形状（XRubberBandShape）。 */
} XRubberBand;

XVtable* XRubberBand_class_init(void);
void XRubberBand_init(XRubberBand* self, XRubberBandShape shape,
                      XWidget* parent);
#define XRubberBand_create(shape, parent) XRubberBand_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (shape), (parent))
XRubberBand* XRubberBand_create_ex(XMemoryType memory,
                                   XRubberBandShape shape, XWidget* parent);
#define XRubberBand_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XRubberBand_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief 查询形状（对标 shape()）。 */
XRubberBandShape XRubberBand_shape(const XRubberBand* self);

#endif /* XWIDGET_ON && XRUBBERBAND_ON */

#ifdef __cplusplus
}
#endif
#endif /* XRUBBERBAND_H */
