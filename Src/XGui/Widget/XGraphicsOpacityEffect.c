/******************************************************************************
 * @file       XGraphicsOpacityEffect.c
 * @brief      不透明度效果实现（对标 Qt 6.8 QGraphicsOpacityEffect）。
 * @details    Draw 实现：输出画布上以 XPainter_setOpacity 设定透明度后
 *             整体绘制源快照——透明底上的 source-over 合成对预乘 ARGB
 *             恰为各通道按 opacity 缩放（对标 Qt draw() 的 p->setOpacity
 *             + drawPixmap 源管线）；不透明度变化经 update() 触发重绘。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XMemory.h"

#if XWIDGET_ON

#include "XGraphicsOpacityEffect.h"
#include "XWidget.h"
#include "XImage.h"
#include "XPainter.h"

/* ==================== 内部辅助 ==================== */

/** @brief Draw 槽位实现：源快照按 m_opacity 绘入输出画布。 */
static void VXGraphicsOpacityEffect_draw(XGraphicsEffect* base,
                                         XGraphicsEffectDrawContext* ctx)
{
    XGraphicsOpacityEffect* self = (XGraphicsOpacityEffect*)base;
    XPainter painter;
    if (!self || !ctx || !ctx->m_source || !ctx->m_dest) return;
    if (XImage_isNull(ctx->m_dest)) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, ctx->m_dest)) {
        XPainter_deinit(&painter);
        return;
    }
    /* 对标 Qt QGraphicsOpacityEffect::draw：p->setOpacity(d->opacity) 后
       绘制 source。画布为透明底，source-over 合成结果 = 预乘源 × opacity。 */
    XPainter_setOpacity(&painter, self->m_opacity);
    XPainter_drawImage(&painter, ctx->m_source,
                       -ctx->m_destRect.x, -ctx->m_destRect.y);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ==================== 类与实例生命周期 ==================== */

XVtable* XGraphicsOpacityEffect_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsOpacityEffect)
    XVTABLE_INHERIT_XCLASS(XGraphicsEffect);
    /* 对标 Qt：opacity 效果覆盖 draw；boundingRectFor 沿用基类"原样
       返回"（不透明度不扩大包围盒），sourceChanged 转基类 update。 */
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_Draw, VXGraphicsOpacityEffect_draw);
    return XVTABLE_DEFAULT;
}

void XGraphicsOpacityEffect_init(XGraphicsOpacityEffect* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGraphicsEffect_init(&self->m_base);
    XClassSetVtable(self, XGraphicsOpacityEffect);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_opacity = 1.0f;
}

XGraphicsOpacityEffect* XGraphicsOpacityEffect_create_ex(XMemoryType memory)
{
    XGraphicsOpacityEffect* self =
        (XGraphicsOpacityEffect*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGraphicsOpacityEffect_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

float XGraphicsOpacityEffect_opacity(const XGraphicsOpacityEffect* self)
{ return self ? self->m_opacity : 1.0f; }

void XGraphicsOpacityEffect_setOpacity(XGraphicsOpacityEffect* self,
                                       float opacity)
{
    if (!self) return;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (self->m_opacity == opacity) return;
    self->m_opacity = opacity;
    /* 对标 Qt：setOpacity 实际变化即触发源重绘（opacityChanged 信号
       本仓库未建立，随基类 enabledChanged 模式后续扩展）。 */
    XGraphicsEffect_update(&self->m_base);
}

#endif /* XWIDGET_ON */
