/******************************************************************************
 * @file       XGraphicsBlurEffect.h
 * @brief      XGraphicsBlurEffect 模糊效果（对标 Qt 6.8
 *             QGraphicsBlurEffect : QGraphicsEffect）。
 * @details    继承 XGraphicsEffect，对源快照做简化盒式模糊：水平+垂直
 *             两趟 3 点盒式均值（等效 3x3 盒式卷积核）。与 Qt 的差异：
 *             Qt 6 按 blurRadius 采用与半径相关的高斯/指数近似核，本实
 *             现为固定 3x3 盒式核（等效模糊半径约 2px），blurRadius/
 *             blurHints 属性仅作 API 对齐保留（含变化信号），不改变核
 *             尺寸。boundingRectFor 按两趟各外扩 1px 生长源矩形。
 * @note       模块总开关 XWIDGET_ON 有效（效果由 XWidget 承载）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSBLUREFFECT_H
#define XGRAPHICSBLUREFFECT_H

#if XGUI_ON
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
 * @brief      模糊质量提示（对标 QGraphicsBlurEffect::BlurHint，位值一致：
 *             qgraphicseffect.h:125-129）。
 */
typedef enum XGraphicsBlurEffectBlurHint
{
    XGraphicsBlurEffectHint_Performance = 0x00, /**< 更快模糊（默认）。 */
    XGraphicsBlurEffectHint_Quality     = 0x01, /**< 更高质量模糊。 */
    XGraphicsBlurEffectHint_Animation   = 0x02 /**< 动画半径模糊。 */
} XGraphicsBlurEffectBlurHint;

/** @brief 模糊提示标志组合（对标 BlurHints QFlags；位或组合承载）。 */
typedef int XGraphicsBlurEffectBlurHints;

/**
 * @brief      XGraphicsBlurEffect 模糊效果对象；m_class 必须是第一个成员。
 * @details    m_blurRadius 为 Qt API 对齐属性（对标 blurRadius，默认 1.0）；
 *             m_blurHints 同为 API 对齐属性（对标 blurHints，默认
 *             PerformanceHint，qgraphicseffect.cpp 文档口径）；实际模糊核
 *             固定为 3x3 盒式（见文件头"与 Qt 的差异"）。
 */
typedef struct XGraphicsBlurEffect
{
    XGraphicsEffect m_base; /**< 基类成员；必须是第一个。 */
    float m_blurRadius;     /**< 模糊半径（对标 blurRadius；默认 1.0；
                                 当前实现不改变固定 3x3 盒式核）。 */
    int m_blurHints;        /**< 模糊质量提示（XGraphicsBlurEffectBlurHint
                                 位或组合；默认 PerformanceHint；固定核下
                                 不改变渲染，仅 API 对齐存储）。 */
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

/**
 * @brief      获取模糊质量提示（对标 QGraphicsBlurEffect::blurHints）。
 * @param      self 目标效果；可为 NULL。
 * @return     XGraphicsBlurEffectBlurHint 位或组合；无效返回
 *             PerformanceHint。
 */
XGraphicsBlurEffectBlurHints XGraphicsBlurEffect_blurHints(
    const XGraphicsBlurEffect* self);
/**
 * @brief      设置模糊质量提示（对标 QGraphicsBlurEffect::setBlurHints）。
 * @details    仅作 API 对齐存储（实际模糊核固定 3x3 盒式）；实际变化时
 *             发射 blurHintsChanged（对标 Qt setter；Qt 不触发重绘）。
 * @param      self 目标效果。
 * @param      hints XGraphicsBlurEffectBlurHint 位或组合。
 * @return     无返回值。
 */
void XGraphicsBlurEffect_setBlurHints(XGraphicsBlurEffect* self,
                                      XGraphicsBlurEffectBlurHints hints);

/* ==================== 信号（对标 QGraphicsBlurEffect signals） ==================== */

/**
 * @brief      模糊半径变化信号（对标 QGraphicsBlurEffect::
 *             blurRadiusChanged(qreal)；setBlurRadius 实际变化时发射）。
 * @param      self 目标效果；NULL 仅作信号标识占位。
 * @param      radius 新模糊半径。
 * @return     信号标识（函数自身地址，用作 XObject_connect 信号参数）。
 */
void* XGraphicsBlurEffect_blurRadiusChanged_signal(
    XGraphicsBlurEffect* self, float radius);
/**
 * @brief      模糊提示变化信号（对标 QGraphicsBlurEffect::
 *             blurHintsChanged(BlurHints)；setBlurHints 实际变化时发射）。
 * @param      self 目标效果；NULL 仅作信号标识占位。
 * @param      hints 新模糊提示组合。
 * @return     信号标识（函数自身地址，用作 XObject_connect 信号参数）。
 */
void* XGraphicsBlurEffect_blurHintsChanged_signal(
    XGraphicsBlurEffect* self, XGraphicsBlurEffectBlurHints hints);

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */

#endif /* XGRAPHICSBLUREFFECT_H */
