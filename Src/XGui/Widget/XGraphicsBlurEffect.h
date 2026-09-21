/******************************************************************************
 * @file       XGraphicsBlurEffect.h
 * @brief      XGraphicsBlurEffect 模糊效果（对标 Qt 6.8
 *             QGraphicsBlurEffect : QGraphicsEffect）。
 * @details    继承 XGraphicsEffect，对源快照做简化盒式模糊：水平+垂直
 *             两趟 3 点盒式均值（等效 3x3 盒式卷积核）。与 Qt 的差异：
 *             Qt 6 按 blurRadius 采用与半径相关的高斯/指数近似核，本实
 *             现为固定 3x3 盒式核（等效模糊半径约 2px），blurRadius 属
 *             性仅作 API 对齐保留，不改变核尺寸；blurHints 未建模。
 *             boundingRectFor 按两趟各外扩 1px 生长源矩形。
 * @note       模块总开关 XWIDGET_ON 有效（效果由 XWidget 承载）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSBLUREFFECT_H
#define XGRAPHICSBLUREFFECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGraphicsEffect.h"

#if XWIDGET_ON

XCLASS_DEFINE_BEGING(XGraphicsBlurEffect)
XCLASS_DEFINE_EXTEND_END(XGraphicsBlurEffect, XGraphicsEffect)

/**
 * @brief      XGraphicsBlurEffect 模糊效果对象；m_class 必须是第一个成员。
 * @details    m_blurRadius 为 Qt API 对齐属性（对标 blurRadius，默认 1.0）；
 *             实际模糊核固定为 3x3 盒式（见文件头"与 Qt 的差异"）。
 */
typedef struct XGraphicsBlurEffect
{
    XGraphicsEffect m_base; /**< 基类成员；必须是第一个。 */
    float m_blurRadius;     /**< 模糊半径（对标 blurRadius；默认 1.0；
                                 当前实现不改变固定 3x3 盒式核）。 */
} XGraphicsBlurEffect;

/**
 * @brief      XGraphicsBlurEffect 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XGraphicsBlurEffect_class_init(void);

/**
 * @brief      初始化 XGraphicsBlurEffect（对标 QGraphicsBlurEffect 构造；
 *             blurRadius 默认 1.0）。
 * @param      self 目标对象指针；不可为 NULL。
 * @return     无返回值。
 */
void XGraphicsBlurEffect_init(XGraphicsBlurEffect* self);
#define XGraphicsBlurEffect_create() \
    XGraphicsBlurEffect_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/**
 * @brief      使用指定内存类型创建 XGraphicsBlurEffect。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XGraphicsBlurEffect* XGraphicsBlurEffect_create_ex(XMemoryType memory);
#define XGraphicsBlurEffect_deinit_base(self) \
    XGraphicsEffect_deinit_base((XGraphicsEffect*)(self))
#define XGraphicsBlurEffect_delete_base(self) \
    XGraphicsEffect_delete_base((XGraphicsEffect*)(self))

/**
 * @brief      获取模糊半径（对标 QGraphicsBlurEffect::blurRadius）。
 * @param      self 目标效果；可为 NULL。
 * @return     模糊半径；无效返回 1.0。
 */
float XGraphicsBlurEffect_blurRadius(const XGraphicsBlurEffect* self);
/**
 * @brief      设置模糊半径（对标 QGraphicsBlurEffect::setBlurRadius）。
 * @details    仅作 API 对齐保存（钳制非负），实际模糊核固定 3x3 盒式；
 *             实际变化时经 update() 请求重绘（对标 Qt 属性 setter）。
 * @param      self 目标效果。
 * @param      radius 模糊半径；非负。
 * @return     无返回值。
 */
void XGraphicsBlurEffect_setBlurRadius(XGraphicsBlurEffect* self,
                                       float radius);

#endif /* XWIDGET_ON */

#endif /* XGRAPHICSBLUREFFECT_H */
