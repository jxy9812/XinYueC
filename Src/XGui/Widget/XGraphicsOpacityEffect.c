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

#if XGUI_ON

#include "XGraphicsOpacityEffect.h"

#include "XGuiConfig.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XImage.h"
#include "XPainter.h"
#include "XVarList.h"

#if XWIDGET_ON

/* ==================== 内部辅助 ==================== */

/** @brief 发射携带 float 参数的信号（无连接时释放参数，防泄漏）。 */
static void xgraphicsopacityeffect_emitFloat(XGraphicsOpacityEffect* self,
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
    /* [缺省对齐 2026-10-07] 对标 Qt 6.8.3 qgraphicseffect_p.h:183 构造
     * opacity(0.7)：「挂接即 70% 半透明呈现」；原 1.0 缺省使挂接无
     * 视觉效果、且 setOpacity 前 setOpacityEnabled 才生效的语义失真。 */
    self->m_opacity = 0.7f;
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
    /* 对标 Qt：setOpacity 实际变化即触发源重绘并发射
     * opacityChanged(opacity)（qgraphicseffect.cpp:1086-1101，update()
     * 之后 emit；载荷为钳位后新值）。 */
    XGraphicsEffect_update(&self->m_base);
    xgraphicsopacityeffect_emitFloat(
        self, (size_t)XGraphicsOpacityEffect_opacityChanged_signal, opacity);
}

/* ==================== 信号 ==================== */

void* XGraphicsOpacityEffect_opacityChanged_signal(
    XGraphicsOpacityEffect* self, float opacity)
{
    (void)self; (void)opacity;
    return (void*)(size_t)XGraphicsOpacityEffect_opacityChanged_signal;
}

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */
