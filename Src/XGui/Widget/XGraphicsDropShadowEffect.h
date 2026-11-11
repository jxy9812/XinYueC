/******************************************************************************
 * @file       XGraphicsDropShadowEffect.h
 * @brief      XGraphicsDropShadowEffect 投影效果（对标 Qt 6.8
 *             QGraphicsDropShadowEffect : QGraphicsEffect）。
 * @details    继承 XGraphicsEffect：按源快照 alpha 形状生成投影，经与 Qt
 *             同算法的指数模糊（XGraphicsEffect_blurImage 的 alphaOnly
 *             模式，核展宽 = blurRadius）后按 offset 偏移绘入输出画布，
 *             再以投影颜色着色（Qt 的 SourceIn 着色步），最后把源快照原样
 *             叠绘其上。半径语义对齐 Qt（qpixmapfilter.cpp:965-1010）：
 *             ① blurRadius 直接作为指数滤波半径（不乘 2.5——那是
 *                QGraphicsBlurEffect 的口径，投影用 `d->radius` 原值）；
 *             ② blurRadius ≤ 0 且 offset 为零 ⇒ 直接绘源（Qt
 *                qgraphicseffect.cpp:1017 的短路）；
 *             ③ blurRadius 负值照存（Qt setBlurRadius 不钳制），包围盒
 *                随 `rect.translated(offset).adjusted(-r,-r,r,r)` 变负而
 *                收缩、投影被裁边；滤波半径按 0 处理（Qt 的负 alpha 在
 *                定点式里只会得到"不模糊"的可观测结果，本机实测 r=-1/-2/-4
 *                投影内部恒为实色硬边）。
 *             默认值对标 Qt 6.8：offset=(8,8)、blurRadius=1.0、
 *             color=QColor(63,63,63,180)。
 *             boundingRectFor 对标 Qt 源码公式（qpixmapfilter.cpp:965-970）：
 *             `rect ∪ (rect 平移 offset 后四周外扩 blurRadius)`。
 * @note       模块总开关 XWIDGET_ON 有效（效果由 XWidget 承载）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSDROPSHADOWEFFECT_H
#define XGRAPHICSDROPSHADOWEFFECT_H

#if XGUI_ON
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
                                 指数滤波半径取本值原样，≤0 不模糊）。 */
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

/**
 * @brief      获取投影偏移（对标 QGraphicsDropShadowEffect::offset）。
 * @param      self 目标效果；可为 NULL。
 * @return     偏移；无效返回 (0,0)。
 */
XPointF XGraphicsDropShadowEffect_offset(const XGraphicsDropShadowEffect* self);
/**
 * @brief      设置投影偏移（对标 QGraphicsDropShadowEffect::setOffset）。
 * @details    实际变化时经 updateBoundingRect() 请求重绘（偏移同时改变
 *             效果包围盒）并发射 offsetChanged(offset)；同值早退。
 * @param      self 目标效果。
 * @param      offset 偏移。
 * @return     无返回值。
 */
void XGraphicsDropShadowEffect_setOffset(XGraphicsDropShadowEffect* self,
                                         XPointF offset);
/**
 * @brief      按分量设置投影偏移（对标 QGraphicsDropShadowEffect::
 *             setOffset(qreal dx, qreal dy) 重载；C 无重载，按仓库既有
 *             `<名字>_2` 约定给第二个重载）。
 * @details    与 setOffset(XPointF) 同语义（同值早退 + 发射 offsetChanged）。
 * @param      self 目标效果。
 * @param      dx 水平偏移。
 * @param      dy 垂直偏移。
 * @return     无返回值。
 */
void XGraphicsDropShadowEffect_setOffset_2(XGraphicsDropShadowEffect* self,
                                           float dx, float dy);
/**
 * @brief      两个方向设同一偏移（对标 QGraphicsDropShadowEffect::
 *             setOffset(qreal d) 重载；`_3` = 第三个重载）。
 * @param      self 目标效果。
 * @param      d 水平与垂直偏移（同值）。
 * @return     无返回值。
 */
void XGraphicsDropShadowEffect_setOffset_3(XGraphicsDropShadowEffect* self,
                                           float d);
/**
 * @brief      只改水平偏移，垂直保持（对标 QGraphicsDropShadowEffect::
 *             setXOffset；Qt 内联实现为 setOffset(QPointF(dx, yOffset()))）。
 * @param      self 目标效果。
 * @param      dx 新的水平偏移。
 * @return     无返回值。
 */
void XGraphicsDropShadowEffect_setXOffset(XGraphicsDropShadowEffect* self,
                                          float dx);
/**
 * @brief      只改垂直偏移，水平保持（对标 setYOffset）。
 * @param      self 目标效果。
 * @param      dy 新的垂直偏移。
 * @return     无返回值。
 */
void XGraphicsDropShadowEffect_setYOffset(XGraphicsDropShadowEffect* self,
                                          float dy);
/**
 * @brief      取水平偏移（对标 QGraphicsDropShadowEffect::xOffset）。
 * @param      self 目标效果；可为 NULL。
 * @return     水平偏移；无效返回 0。
 */
float XGraphicsDropShadowEffect_xOffset(const XGraphicsDropShadowEffect* self);
/**
 * @brief      取垂直偏移（对标 QGraphicsDropShadowEffect::yOffset）。
 * @param      self 目标效果；可为 NULL。
 * @return     垂直偏移；无效返回 0。
 */
float XGraphicsDropShadowEffect_yOffset(const XGraphicsDropShadowEffect* self);
/**
 * @brief      获取模糊半径（对标 QGraphicsDropShadowEffect::blurRadius）。
 * @param      self 目标效果；可为 NULL。
 * @return     模糊半径；无效返回 1.0。
 */
float XGraphicsDropShadowEffect_blurRadius(
    const XGraphicsDropShadowEffect* self);
/**
 * @brief      设置模糊半径（对标 QGraphicsDropShadowEffect::setBlurRadius）。
 * @details    与 Qt 一致**不钳制**：负值照存（本机 Qt 6.8 实测负半径被接受
 *             且投影被裁边、内部保持硬边）；滤波半径按 max(0, radius) 取，
 *             即负半径 = 不模糊（见文件头③）。
 *             实际变化时经 updateBoundingRect() 请求重绘（半径同时改变
 *             包围盒外扩量，收缩时旧外扩环一并失效）。
 * @param      self 目标效果。
 * @param      radius 模糊半径（任意实数；≤0 不模糊）。
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

/* ==================== 信号（对标 QGraphicsDropShadowEffect signals） ==================== */

/**
 * @brief      投影偏移变化信号（对标 QGraphicsDropShadowEffect::
 *             offsetChanged(QPointF)；setOffset 实际变化时发射）。
 * @param      self 目标效果；NULL 仅作信号标识占位。
 * @param      offset 新偏移。
 * @return     信号标识（函数自身地址，用作 XObject_connect 信号参数）。
 */
void* XGraphicsDropShadowEffect_offsetChanged_signal(
    XGraphicsDropShadowEffect* self, XPointF offset);
/**
 * @brief      模糊半径变化信号（对标 QGraphicsDropShadowEffect::
 *             blurRadiusChanged(qreal)；setBlurRadius 实际变化时发射）。
 * @param      self 目标效果；NULL 仅作信号标识占位。
 * @param      radius 新模糊半径。
 * @return     信号标识（函数自身地址，用作 XObject_connect 信号参数）。
 */
void* XGraphicsDropShadowEffect_blurRadiusChanged_signal(
    XGraphicsDropShadowEffect* self, float radius);
/**
 * @brief      投影颜色变化信号（对标 QGraphicsDropShadowEffect::
 *             colorChanged(QColor)；setColor 实际变化时发射）。
 * @param      self 目标效果；NULL 仅作信号标识占位。
 * @param      color 新颜色值拷贝。
 * @return     信号标识（函数自身地址，用作 XObject_connect 信号参数）。
 */
void* XGraphicsDropShadowEffect_colorChanged_signal(
    XGraphicsDropShadowEffect* self, XColor color);

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */

#endif /* XGRAPHICSDROPSHADOWEFFECT_H */
