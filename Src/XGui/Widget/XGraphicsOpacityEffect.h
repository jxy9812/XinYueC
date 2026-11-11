/******************************************************************************
 * @file       XGraphicsOpacityEffect.h
 * @brief      XGraphicsOpacityEffect 不透明度效果（对标 Qt 6.8
 *             QGraphicsOpacityEffect : QGraphicsEffect）。
 * @details    继承 XGraphicsEffect，把源快照以设定不透明度绘入输出画布
 *             （对标 Qt draw() 中 p->setOpacity 后绘制源）：透明画布上
 *             的 source-over 合成等价于预乘 ARGB 各通道按 opacity 线性
 *             缩放。默认 opacity=0.7（对标 Qt qgraphicseffect_p.h 构造
 *             缺省，Qt 6.8.3 源码实证）。
 *             opacityMask（QBrush 掩码）按 Qt 口径实现：用画刷在
 *             **控件局部坐标**（Qt 的 sourceBoundingRect()）上填充一张
 *             画布同尺寸的掩码图，再按掩码 **alpha** 逐像素乘到效果输出
 *             （Qt 的 `CompositionMode_DestinationIn` + fillRect；掩码的
 *             RGB 不参与——本机 Qt 6.8 实测：不透明绿色掩码对红色源毫无
 *             影响，半透明掩码只按 alpha 衰减）。画刷经
 *             Graphics/XBrush.h 的 XBrush_applyToPainter + XPainter_fillRect_2
 *             光栅化（纯色/渐变由软件后端完整支持；Dense/Hor/Ver/Diag
 *             图案在后端按纯色近似——XPainter 既有声明，见
 *             XPainter_setBrushStyle 文档；TexturePattern 后端未实现）。
 *             默认掩码 = NoBrush（Qt `there is no opacity mask by default`）。
 * @note       模块总开关 XWIDGET_ON 有效（效果由 XWidget 承载）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSOPACITYEFFECT_H
#define XGRAPHICSOPACITYEFFECT_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGraphicsEffect.h"
#include "XBrush.h"

#if XWIDGET_ON

XCLASS_DEFINE_BEGING(XGraphicsOpacityEffect)
XCLASS_DEFINE_EXTEND_END(XGraphicsOpacityEffect, XGraphicsEffect)

/**
 * @brief      XGraphicsOpacityEffect 不透明度效果对象；m_class 必须是
 *             第一个成员。
 */
typedef struct XGraphicsOpacityEffect
{
    XGraphicsEffect m_base; /**< 基类成员；必须是第一个。 */
    float m_opacity;        /**< 不透明度 0~1（对标 opacity；Qt 缺省 0.7）。 */
    XBrush m_opacityMask;   /**< 不透明度掩码（对标 opacityMask；Qt 缺省
                                 QBrush()=NoBrush）。值为 8 字节驻留池句柄，
                                 按值拷贝安全、无需释放；仅其 alpha 参与
                                 合成（RGB 被忽略）。 */
} XGraphicsOpacityEffect;

/**
 * @brief      XGraphicsOpacityEffect 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XGraphicsOpacityEffect_class_init(void);

/**
 * @brief      初始化 XGraphicsOpacityEffect（对标 QGraphicsOpacityEffect
 *             构造；opacity 默认 0.7）。
 * @param      self 目标对象指针；不可为 NULL。
 * @return     无返回值。
 */
void XGraphicsOpacityEffect_init(XGraphicsOpacityEffect* self);
#define XGraphicsOpacityEffect_create() \
    XGraphicsOpacityEffect_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/**
 * @brief      使用指定内存类型创建 XGraphicsOpacityEffect。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XGraphicsOpacityEffect* XGraphicsOpacityEffect_create_ex(XMemoryType memory);

/**
 * @brief      获取不透明度（对标 QGraphicsOpacityEffect::opacity）。
 * @param      self 目标效果；可为 NULL。
 * @return     不透明度 0~1；无效返回 1.0。
 */
float XGraphicsOpacityEffect_opacity(const XGraphicsOpacityEffect* self);
/**
 * @brief      设置不透明度（对标 QGraphicsOpacityEffect::setOpacity）。
 * @details    数值钳制到 0~1；实际变化时经 update() 请求重绘（对标 Qt
 *             属性 setter 触发源重绘并发射 opacityChanged）。
 * @param      self 目标效果。
 * @param      opacity 不透明度 0~1。
 * @return     无返回值。
 */
void XGraphicsOpacityEffect_setOpacity(XGraphicsOpacityEffect* self,
                                       float opacity);

/**
 * @brief      获取不透明度掩码（对标 QGraphicsOpacityEffect::opacityMask）。
 * @param      self 目标效果；可为 NULL。
 * @return     掩码画刷值拷贝；无效返回 NoBrush（XBrush_create()）。
 */
XBrush XGraphicsOpacityEffect_opacityMask(
    const XGraphicsOpacityEffect* self);
/**
 * @brief      设置不透明度掩码（对标 QGraphicsOpacityEffect::
 *             setOpacityMask(const QBrush&)）。
 * @details    Qt 语义（qgraphicseffect.cpp:1131-1141）：
 *               if (d->opacityMask == mask) return;
 *               d->opacityMask = mask;
 *               d->hasOpacityMask = (mask.style() != Qt::NoBrush);
 *               update();
 *               emit opacityMaskChanged(mask);
 *             画刷按值比较（XBrush_isEqual：样式/颜色/渐变/纹理）——本机
 *             Qt 6.8 实测「同值不同实例」不发射、NoBrush 清除掩码后恢复
 *             不透明；实际变化即请求源重绘并发射 opacityMaskChanged。
 *             NoBrush 表示**无掩码**（不是全透明）：绘制退化为纯 opacity。
 * @param      self 目标效果。
 * @param      mask 掩码画刷（值拷贝；XBrush_create() 即清除）。
 * @return     无返回值。
 */
void XGraphicsOpacityEffect_setOpacityMask(XGraphicsOpacityEffect* self,
                                           XBrush mask);

/* ==================== 信号（对标 QGraphicsOpacityEffect signals） ==================== */

/**
 * @brief      不透明度变化信号（对标 QGraphicsOpacityEffect::
 *             opacityChanged(qreal)；setOpacity 实际变化时发射）。
 * @param      self 目标效果；NULL 仅作信号标识占位。
 * @param      opacity 新不透明度（钳位后 0~1）。
 * @return     信号标识（函数自身地址，用作 XObject_connect 信号参数）。
 */
void* XGraphicsOpacityEffect_opacityChanged_signal(
    XGraphicsOpacityEffect* self, float opacity);

/**
 * @brief      掩码变化信号（对标 QGraphicsOpacityEffect::
 *             opacityMaskChanged(const QBrush&)；setOpacityMask 实际变化时
 *             发射）。
 * @param      self 目标效果；NULL 仅作信号标识占位。
 * @param      mask 新掩码画刷（值拷贝）。
 * @return     信号标识（函数自身地址，用作 XObject_connect 信号参数）。
 */
void* XGraphicsOpacityEffect_opacityMaskChanged_signal(
    XGraphicsOpacityEffect* self, XBrush mask);

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */

#endif /* XGRAPHICSOPACITYEFFECT_H */
