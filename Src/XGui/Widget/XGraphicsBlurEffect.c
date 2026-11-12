/******************************************************************************
 * @file       XGraphicsBlurEffect.c
 * @brief      模糊效果实现（对标 Qt 6.8 QGraphicsBlurEffect，指数滤波）。
 * @details    Draw 实现：先把源快照按控件局部坐标对齐绘入输出画布，再
 *             对整幅画布执行 XGraphicsEffect_blurImage（Qt expblur 移植，
 *             水平+垂直指数滤波）。核展宽 = 2.5 × blurRadius（Qt
 *             radiusScale），blurHints 的 QualityHint 走半半径两遍。
 *             radius ≤ 1 时直接绘源（Qt QPixmapBlurFilter::draw 短路）；
 *             负半径照存（包围盒随 pad=1+2.5r 收缩，源被裁边）。
 *             boundingRectFor 覆盖为 pad = 1 + 2.5 × blurRadius 的四周外扩
 *             （Qt qpixmapfilter.cpp:550-555 原文公式）。
 * @note       与 Qt 的差异：省略 qt_blurImage 在 radius ≥ 4 时的
 *             qt_halfScaled 半分辨率加速（XGui 恒全分辨率滤波：结果更精
 *             细、代价更高，属性能差异）。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XGraphicsBlurEffect.h"

#include "XGuiConfig.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XImage.h"
#include "XPainter.h"
#include "XVarList.h"

#if XWIDGET_ON

/** @brief boundingRectFor 的常数项（Qt `radiusScale * radius + 1` 的 +1）。 */
#define XGRAPHICSBLUREFFECT_BOUNDS_PAD 1.0f
/** @brief 半径缩放系数（Qt radiusScale：包围盒与绘制半径同用 2.5）。 */
#define XGRAPHICSBLUREFFECT_RADIUS_SCALE 2.5f
/** @brief 不模糊阈值（Qt QPixmapBlurFilter::draw：radius <= 1 直接绘源）。 */
#define XGRAPHICSBLUREFFECT_NO_BLUR_RADIUS 1.0f
/** @brief 缺省半径（Qt QPixmapBlurFilterPrivate `radius(5)`）。 */
#define XGRAPHICSBLUREFFECT_DEFAULT_RADIUS 5.0f

/* ==================== 内部辅助 ==================== */

/** @brief 发射携带 float 参数的信号（无连接时释放参数，防泄漏）。 */
static void xgraphicsblureffect_emitFloat(XGraphicsBlurEffect* self,
                                          size_t signal, float value)
{
    XVarList* args = XVarList_Create(XVar(float, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射携带 int 参数的信号（无连接时释放参数，防泄漏）。 */
static void xgraphicsblureffect_emitInt(XGraphicsBlurEffect* self,
                                        size_t signal, int value)
{
    XVarList* args = XVarList_Create(XVar(int, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 虚槽位实现（对标 Qt 基类/派生行为） ==================== */

/** @brief BoundingRectFor 槽位实现：源矩形四周按 Qt 公式外扩。 */
static XRectF VXGraphicsBlurEffect_boundingRectFor(
    const XGraphicsEffect* base, const XRectF* sourceRect)
{
    const XGraphicsBlurEffect* self = (const XGraphicsBlurEffect*)base;
    XRectF out;
    float pad;
    if (!sourceRect) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.width = 0.0f;
        out.height = 0.0f;
        return out;
    }
    /* 对标 QGraphicsBlurEffect::boundingRectFor → QPixmapBlurFilter::
     * boundingRectFor（qgraphicsefeffect 源码 qpixmapfilter.cpp:550-555）：
     *     delta = radiusScale(2.5) * radius + 1
     *     return rect.adjusted(-delta, -delta, delta, delta);
     * **不做**半径钳制：负半径时 delta 为负 ⇒ 包围盒收缩（Qt 实测
     * pad = 1 + 2.5r，含 r=-0.5 → 0.25 与 r=-3 → -6.5；本机 Qt 6.8
     * 逐点采样与源码常数完全一致）。与 blurHints 无关（三档实测相同）。 */
    pad = XGRAPHICSBLUREFFECT_BOUNDS_PAD +
          XGRAPHICSBLUREFFECT_RADIUS_SCALE * self->m_blurRadius;
    out.x = sourceRect->x - pad;
    out.y = sourceRect->y - pad;
    out.width = sourceRect->width + 2.0f * pad;
    out.height = sourceRect->height + 2.0f * pad;
    return out;
}

/** @brief Draw 槽位实现：源对齐绘入画布后整幅做指数模糊。 */
static void VXGraphicsBlurEffect_draw(XGraphicsEffect* base,
                                      XGraphicsEffectDrawContext* ctx)
{
    XGraphicsBlurEffect* self = (XGraphicsBlurEffect*)base;
    XPainter painter;
    if (!self || !ctx || !ctx->m_source || !ctx->m_dest) return;
    if (XImage_isNull(ctx->m_dest)) return;
    /* 1. 源快照按控件局部坐标对齐绘入输出画布（画布覆盖 boundingRect，
          越界自动裁剪；对标 Qt draw 内 source 绘制步）。 */
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, ctx->m_dest)) {
        XGraphicsEffect_drawSource(&self->m_base, &painter);
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    /* 2. radius ≤ 1 即不模糊（对标 QPixmapBlurFilter::draw：
          `if (d->radius <= 1) { painter->drawPixmap(...); return; }`；
          本机 Qt 6.8 实测 r=0/0.5/1 与无效果逐像素相同）。 */
    if (self->m_blurRadius <= XGRAPHICSBLUREFFECT_NO_BLUR_RADIUS) return;
    /* 3. 整幅画布模糊（画布 = 效果包围盒，含外扩环；Qt 的 qt_blurImage
          同样作用于 PadToEffectiveBoundingRect 的整张 pixmap，边界处按
          透明外推——指数滤波的首尾状态为 0）。
          绘制半径 = 2.5 × blurRadius（Qt `scaledRadius = radiusScale *
          d->radius`）；QualityHint 位作 quality 参数（半半径两遍）。 */
    XGraphicsEffect_blurImage(
        ctx->m_dest, XGRAPHICSBLUREFFECT_RADIUS_SCALE * self->m_blurRadius,
        (self->m_blurHints & (int)XGraphicsBlurEffectHint_Quality) != 0,
        false);
}

/* ==================== 类与实例生命周期 ==================== */

XVtable* XGraphicsBlurEffect_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsBlurEffect)
    XVTABLE_INHERIT_XCLASS(XGraphicsEffect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_BoundingRectFor,
                             VXGraphicsBlurEffect_boundingRectFor);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_Draw, VXGraphicsBlurEffect_draw);
    return XVTABLE_DEFAULT;
}

void XGraphicsBlurEffect_init(XGraphicsBlurEffect* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGraphicsEffect_init(&self->m_base);
    XClassSetVtable(self, XGraphicsBlurEffect);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 缺省半径 5.0（对标 Qt QPixmapBlurFilterPrivate 的 `radius(5)`，
       qpixmapfilter.cpp:465；本机 Qt 6.8 探针实测
       QGraphicsBlurEffect().blurRadius() == 5.0）。 */
    self->m_blurRadius = XGRAPHICSBLUREFFECT_DEFAULT_RADIUS;
    /* 对标 Qt 文档默认：blurHints=PerformanceHint（qgraphicseffect.cpp
       「By default, the blur hint is PerformanceHint」）。 */
    self->m_blurHints = (int)XGraphicsBlurEffectHint_Performance;
}

XGraphicsBlurEffect* XGraphicsBlurEffect_create_ex(XMemoryType memory)
{
    XGraphicsBlurEffect* self =
        (XGraphicsBlurEffect*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGraphicsBlurEffect_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

float XGraphicsBlurEffect_blurRadius(const XGraphicsBlurEffect* self)
{ return self ? self->m_blurRadius : XGRAPHICSBLUREFFECT_DEFAULT_RADIUS; }

void XGraphicsBlurEffect_setBlurRadius(XGraphicsBlurEffect* self, float radius)
{
    if (!self) return;
    /* 不钳制（对标 Qt：setBlurRadius 只做 qFuzzyCompare 早退 + 存值，
       qgraphicseffect.cpp:760-769；本机 Qt 6.8 实测负值照存，
       boundingRectFor 随之收缩、源被裁边）。
       [P0-60 2026-11-11] 早退判据由精确 == 改为 qFuzzyCompare 口径
       （XGraphicsEffect_fuzzyCompare）：Qt qgraphicseffect.cpp:763-764
       用 `qFuzzyCompare(d->filter->radius(), radius)`；精确比较会让
       5.0 与 5.000001f 这类近等值多发 blurRadiusChanged、并白跑一次
       updateBoundingRect() 重绘。 */
    if (XGraphicsEffect_fuzzyCompare(self->m_blurRadius, radius)) return;
    self->m_blurRadius = radius;
    /* 对标 Qt：setBlurRadius 实际变化即 updateBoundingRect()（包围盒随
       半径生长/收缩，收缩时旧外扩环也要覆写）并发射
     * blurRadiusChanged(radius)（qgraphicseffect.cpp:760-769）。 */
    XGraphicsEffect_updateBoundingRect(&self->m_base);
    xgraphicsblureffect_emitFloat(
        self, (size_t)XGraphicsBlurEffect_blurRadiusChanged_signal, radius);
}

XGraphicsBlurEffectBlurHints XGraphicsBlurEffect_blurHints(
    const XGraphicsBlurEffect* self)
{
    return self ? self->m_blurHints
                : (int)XGraphicsBlurEffectHint_Performance;
}

void XGraphicsBlurEffect_setBlurHints(XGraphicsBlurEffect* self,
                                      XGraphicsBlurEffectBlurHints hints)
{
    if (!self) return;
    if (self->m_blurHints == hints) return;
    self->m_blurHints = hints;
    /* 对标 Qt setBlurHints（qgraphicseffect.cpp:794-802）：实际变化发射
     * blurHintsChanged(hints)；Qt 此处不触发重绘（质量档只影响下一次
     * Draw 的滤波遍数），本实现同样不发重绘请求。 */
    xgraphicsblureffect_emitInt(
        self, (size_t)XGraphicsBlurEffect_blurHintsChanged_signal, hints);
}

/* ==================== 信号 ==================== */

void* XGraphicsBlurEffect_blurRadiusChanged_signal(
    XGraphicsBlurEffect* self, float radius)
{
    (void)self; (void)radius;
    return (void*)(size_t)XGraphicsBlurEffect_blurRadiusChanged_signal;
}

void* XGraphicsBlurEffect_blurHintsChanged_signal(
    XGraphicsBlurEffect* self, XGraphicsBlurEffectBlurHints hints)
{
    (void)self; (void)hints;
    return (void*)(size_t)XGraphicsBlurEffect_blurHintsChanged_signal;
}

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */
