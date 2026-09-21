/******************************************************************************
 * @file       XGraphicsDropShadowEffect.h
 * @brief      XGraphicsDropShadowEffect 投影效果（对标 Qt 6.8
 *             QGraphicsDropShadowEffect : QGraphicsEffect）。
 * @details    继承 XGraphicsEffect：按源快照 alpha 形状生成着色投影，
 *             经固定 3x3 盒式模糊（复用 XGraphicsEffect_blurBox3）后按
 *             offset 偏移绘入输出画布，再把源快照原样叠绘其上。与 Qt
 *             的差异：Qt 6 按 blurRadius 用与半径相关的高斯/指数近似核，
 *             本实现为固定盒式核（等效模糊半径约 2px），blurRadius 仅
 *             作 API 对齐保留。默认值对标 Qt 6.8：offset=(8,8)、
 *             blurRadius=1.0、color=QColor(63,63,63,180)。
 * @note       模块总开关 XWIDGET_ON 有效（效果由 XWidget 承载）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSDROPSHADOWEFFECT_H
#define XGRAPHICSDROPSHADOWEFFECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGraphicsEffect.h"
#include "XColor.h"

#if XWIDGET_ON

XCLASS_DEFINE_BEGING(XGraphicsDropShadowEffect)
XCLASS_DEFINE_EXTEND_END(XGraphicsDropShadowEffect, XGraphicsEffect)

/**
 * @brief      XGraphicsDropShadowEffect 投影效果对象；m_class 必须是
 *             第一个成员。
 */
typedef struct XGraphicsDropShadowEffect
{
    XGraphicsEffect m_base; /**< 基类成员；必须是第一个。 */
    XPointF m_offset;       /**< 投影偏移（对标 offset；默认 (8,8)）。 */
    float m_blurRadius;     /**< 模糊半径（对标 blurRadius；默认 1.0；
                                 当前实现不改变固定 3x3 盒式核）。 */
    XColor m_color;         /**< 投影颜色（对标 color；默认
                                 QColor(63,63,63,180)）。 */
} XGraphicsDropShadowEffect;

/**
 * @brief      XGraphicsDropShadowEffect 类虚函数表初始化（对标 Qt 同名
 *             接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XGraphicsDropShadowEffect_class_init(void);

/**
 * @brief      初始化 XGraphicsDropShadowEffect（对标
 *             QGraphicsDropShadowEffect 构造；offset=(8,8)、
 *             blurRadius=1.0、color=QColor(63,63,63,180)）。
 * @param      self 目标对象指针；不可为 NULL。
 * @return     无返回值。
 */
void XGraphicsDropShadowEffect_init(XGraphicsDropShadowEffect* self);
#define XGraphicsDropShadowEffect_create() \
    XGraphicsDropShadowEffect_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/**
 * @brief      使用指定内存类型创建 XGraphicsDropShadowEffect。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XGraphicsDropShadowEffect* XGraphicsDropShadowEffect_create_ex(
    XMemoryType memory);
#define XGraphicsDropShadowEffect_deinit_base(self) \
    XGraphicsEffect_deinit_base((XGraphicsEffect*)(self))
#define XGraphicsDropShadowEffect_delete_base(self) \
    XGraphicsEffect_delete_base((XGraphicsEffect*)(self))

/**
 * @brief      获取投影偏移（对标 QGraphicsDropShadowEffect::offset）。
 * @param      self 目标效果；可为 NULL。
 * @return     偏移；无效返回 (0,0)。
 */
XPointF XGraphicsDropShadowEffect_offset(const XGraphicsDropShadowEffect* self);
/**
 * @brief      设置投影偏移（对标 QGraphicsDropShadowEffect::setOffset）。
 * @details    实际变化时经 update() 请求重绘（对标 Qt 属性 setter）。
 * @param      self 目标效果。
 * @param      offset 偏移。
 * @return     无返回值。
 */
void XGraphicsDropShadowEffect_setOffset(XGraphicsDropShadowEffect* self,
                                         XPointF offset);
/**
 * @brief      获取模糊半径（对标 QGraphicsDropShadowEffect::blurRadius）。
 * @param      self 目标效果；可为 NULL。
 * @return     模糊半径；无效返回 1.0。
 */
float XGraphicsDropShadowEffect_blurRadius(
    const XGraphicsDropShadowEffect* self);
/**
 * @brief      设置模糊半径（对标 QGraphicsDropShadowEffect::setBlurRadius）。
 * @details    仅作 API 对齐保存（钳制非负），实际模糊核固定 3x3 盒式；
 *             实际变化时经 update() 请求重绘。
 * @param      self 目标效果。
 * @param      radius 模糊半径；非负。
 * @return     无返回值。
 */
void XGraphicsDropShadowEffect_setBlurRadius(
    XGraphicsDropShadowEffect* self, float radius);
/**
 * @brief      获取投影颜色（对标 QGraphicsDropShadowEffect::color）。
 * @param      self 目标效果；可为 NULL。
 * @return     颜色值拷贝；无效返回默认构造色。
 */
XColor XGraphicsDropShadowEffect_color(const XGraphicsDropShadowEffect* self);
/**
 * @brief      设置投影颜色（对标 QGraphicsDropShadowEffect::setColor）。
 * @details    实际变化时经 update() 请求重绘（对标 Qt 属性 setter）。
 * @param      self 目标效果。
 * @param      color 颜色值拷贝。
 * @return     无返回值。
 */
void XGraphicsDropShadowEffect_setColor(XGraphicsDropShadowEffect* self,
                                        XColor color);

#endif /* XWIDGET_ON */

#endif /* XGRAPHICSDROPSHADOWEFFECT_H */
