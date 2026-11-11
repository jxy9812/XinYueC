/******************************************************************************
 * @file       XGraphicsBlurEffect.h
 * @brief      XGraphicsBlurEffect 模糊效果（对标 Qt 6.8
 *             QGraphicsBlurEffect : QGraphicsEffect）。
 * @details    继承 XGraphicsEffect，对源快照做与 Qt 同算法的指数模糊：
 *             移植 Qt qt_blurImage/expblur（qimageeffects.cpp）的双侧指数
 *             冲激响应，水平+垂直各一趟（前向+后向），核展宽由 blurRadius
 *             决定——Qt 的绘制半径是 `2.5 × blurRadius`（qpixmapfilter.cpp
 *             的 radiusScale），本实现同；不再是固定 3x3 盒式核。
 *             ① blurRadius ≤ 1 时与 Qt 一样**不模糊**（直接绘源）：
 *                QPixmapBlurFilter::draw 的 `if (d->radius <= 1)` 短路；
 *                本机 Qt 6.8 实测 r=0/0.5/1 与无效果逐像素相同，r=1.25
 *                起可见扩散；
 *             ② blurRadius 负值**照存**（Qt setBlurRadius 不钳制：本机实测
 *                setBlurRadius(-3) 后 blurRadius()=-3）：boundingRectFor 的
 *                pad = 1 + 2.5r 随之变负 ⇒ 效果包围盒收缩、源被裁边（实测
 *                r=-2 时控件左右各裁 4px；半径极负到包围盒为空时整块不画）；
 *             ③ blurHints 的 QualityHint 参与渲染（Qt 以
 *                `hints & QualityHint` 作为 qt_blurImage 的 quality 参数：
 *                半半径跑两遍），Performance/Animation 不改变结果。
 *             boundingRectFor 按 Qt 源码公式外扩：pad = 1 + 2.5 × blurRadius
 *             （qpixmapfilter.cpp:550-555 的 `radiusScale * radius + 1`；
 *             四周同量，与 blurHints 无关）。缺省半径 5.0（Qt
 *             QPixmapBlurFilterPrivate 的 `radius(5)`，qgraphicseffect.cpp
 *             文档「By default, the blur radius is 5 pixels」）。
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
 * @details    m_blurRadius 为 blurRadius 属性（默认 5.0，Qt
 *             QPixmapBlurFilterPrivate `radius(5)`）；m_blurHints 为
 *             blurHints（默认 PerformanceHint，qgraphicseffect.cpp 文档
 *             口径），QualityHint 位改变渲染质量档（见文件头③）。
 */
typedef struct XGraphicsBlurEffect
{
    XGraphicsEffect m_base; /**< 基类成员；必须是第一个。 */
    float m_blurRadius;     /**< 模糊半径（对标 blurRadius；默认 5.0；
                                 绘制半径 = 2.5 × 本值，≤1 不模糊）。 */
    int m_blurHints;        /**< 模糊质量提示（XGraphicsBlurEffectBlurHint
                                 位或组合；默认 PerformanceHint；
                                 QualityHint 位 = 半半径两遍，其余位不改变
                                 渲染）。 */
} XGraphicsBlurEffect;

/**
 * @brief      XGraphicsBlurEffect 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XGraphicsBlurEffect_class_init(void);

/**
 * @brief      初始化 XGraphicsBlurEffect（对标 QGraphicsBlurEffect 构造；
 *             blurRadius 默认 5.0）。
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
 * @return     模糊半径；无效返回 5.0（Qt 构造缺省）。
 */
float XGraphicsBlurEffect_blurRadius(const XGraphicsBlurEffect* self);
/**
 * @brief      设置模糊半径（对标 QGraphicsBlurEffect::setBlurRadius）。
 * @details    与 Qt 一致**不钳制**：负值照存（本机 Qt 6.8 实测
 *             setBlurRadius(-3) → blurRadius() = -3），包围盒随
 *             pad = 1 + 2.5r 收缩并裁掉源边缘；渲染半径 ≤ 1（含负值）时
 *             直接绘源、不模糊（Qt QPixmapBlurFilter::draw 短路）。
 *             实际变化时经 updateBoundingRect() 请求重绘并发射
 *             blurRadiusChanged（对标 Qt 属性 setter）。
 * @param      self 目标效果。
 * @param      radius 模糊半径（任意实数；≤1 不模糊）。
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
 * @details    QualityHint 位改变渲染（半半径跑两遍，对标 Qt 把
 *             `hints & QualityHint` 传给 qt_blurImage）；其余位只存储。
 *             实际变化时发射 blurHintsChanged（对标 Qt setter；Qt 此处
 *             不触发重绘，本实现同）。
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
