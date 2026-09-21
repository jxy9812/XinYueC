/******************************************************************************
 * @file       XGraphicsOpacityEffect.h
 * @brief      XGraphicsOpacityEffect 不透明度效果（对标 Qt 6.8
 *             QGraphicsOpacityEffect : QGraphicsEffect）。
 * @details    继承 XGraphicsEffect，把源快照以设定不透明度绘入输出画布
 *             （对标 Qt draw() 中 p->setOpacity 后绘制源）：透明画布上
 *             的 source-over 合成等价于预乘 ARGB 各通道按 opacity 线性
 *             缩放。默认 opacity=1.0（对标 Qt 默认值）。
 * @note       模块总开关 XWIDGET_ON 有效（效果由 XWidget 承载）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSOPACITYEFFECT_H
#define XGRAPHICSOPACITYEFFECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGraphicsEffect.h"

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
    float m_opacity;        /**< 不透明度 0~1（对标 opacity；默认 1.0）。 */
} XGraphicsOpacityEffect;

/**
 * @brief      XGraphicsOpacityEffect 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XGraphicsOpacityEffect_class_init(void);

/**
 * @brief      初始化 XGraphicsOpacityEffect（对标 QGraphicsOpacityEffect
 *             构造；opacity 默认 1.0）。
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
#define XGraphicsOpacityEffect_deinit_base(self) \
    XGraphicsEffect_deinit_base((XGraphicsEffect*)(self))
#define XGraphicsOpacityEffect_delete_base(self) \
    XGraphicsEffect_delete_base((XGraphicsEffect*)(self))

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

#endif /* XWIDGET_ON */

#endif /* XGRAPHICSOPACITYEFFECT_H */
