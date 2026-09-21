/******************************************************************************
 * @file       XGraphicsBlurEffect.c
 * @brief      模糊效果实现（对标 Qt 6.8 QGraphicsBlurEffect，简化盒式）。
 * @details    Draw 实现：先把源快照按控件局部坐标对齐绘入输出画布，再
 *             对处理区域执行 XGraphicsEffect_blurBox3（水平+垂直两趟
 *             3 点盒式均值）。与 Qt 的差异：Qt 6 按 blurRadius 用与半径
 *             相关的高斯/指数近似核并支持 blurHints 质量档位；本实现为
 *             固定 3x3 盒式核（等效模糊半径约 2px），radius/hints 不改
 *             变核尺寸。boundingRectFor 覆盖为源矩形四周外扩 2px（两趟
 *             各扩散 1px，供脏区放大采样源周边像素）。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XMemory.h"

#if XWIDGET_ON

#include "XGraphicsBlurEffect.h"
#include "XWidget.h"
#include "XImage.h"
#include "XPainter.h"

/** @brief 固定盒式核外扩量：水平+垂直两趟各扩散 1px。 */
#define XGRAPHICSBLUREFFECT_PAD 2.0f

/* ==================== 内部辅助 ==================== */

/** @brief BoundingRectFor 槽位实现：源矩形四周外扩固定核半径。 */
static XRectF VXGraphicsBlurEffect_boundingRectFor(
    const XGraphicsEffect* base, const XRectF* sourceRect)
{
    XRectF out;
    if (!sourceRect) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.width = 0.0f;
        out.height = 0.0f;
        return out;
    }
    (void)base;
    /* 对标 Qt boundingRectFor 覆盖语义：包围盒随模糊核扩散生长；外扩
       量取固定 3x3 两趟核的扩散半径（Qt 为 ceil(radius*3) 随半径变化）。 */
    out.x = sourceRect->x - XGRAPHICSBLUREFFECT_PAD;
    out.y = sourceRect->y - XGRAPHICSBLUREFFECT_PAD;
    out.width = sourceRect->width + 2.0f * XGRAPHICSBLUREFFECT_PAD;
    out.height = sourceRect->height + 2.0f * XGRAPHICSBLUREFFECT_PAD;
    return out;
}

/** @brief Draw 槽位实现：源对齐绘入画布后对处理区域做盒式模糊。 */
static void VXGraphicsBlurEffect_draw(XGraphicsEffect* base,
                                      XGraphicsEffectDrawContext* ctx)
{
    XRect blurRect;
    XPainter painter;
    if (!base || !ctx || !ctx->m_source || !ctx->m_dest) return;
    if (XImage_isNull(ctx->m_dest)) return;
    /* 1. 源快照按控件局部坐标对齐绘入输出画布（画布覆盖 boundingRect，
          越界自动裁剪；对标 Qt draw 内 source 绘制步）。 */
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, ctx->m_dest)) {
        XPainter_drawImage(&painter, ctx->m_source,
                           -ctx->m_destRect.x, -ctx->m_destRect.y);
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    /* 2. 对处理区域（画布内对应 m_sourceRect 的部分）做两趟盒式模糊；
          画布边缘外的像素按透明参与均值（与 Qt 源边界透明外推一致）。 */
    blurRect.x = ctx->m_sourceRect.x - ctx->m_destRect.x;
    blurRect.y = ctx->m_sourceRect.y - ctx->m_destRect.y;
    blurRect.width = ctx->m_sourceRect.width;
    blurRect.height = ctx->m_sourceRect.height;
    XGraphicsEffect_blurBox3(ctx->m_dest, &blurRect);
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
    self->m_blurRadius = 1.0f;
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
{ return self ? self->m_blurRadius : 1.0f; }

void XGraphicsBlurEffect_setBlurRadius(XGraphicsBlurEffect* self, float radius)
{
    if (!self) return;
    if (radius < 0.0f) radius = 0.0f;
    if (self->m_blurRadius == radius) return;
    self->m_blurRadius = radius;
    /* 对标 Qt：setBlurRadius 实际变化即触发源重绘（半径当前不改变
       固定核尺寸，见文件头"与 Qt 的差异"）。 */
    XGraphicsEffect_update(&self->m_base);
}

#endif /* XWIDGET_ON */
