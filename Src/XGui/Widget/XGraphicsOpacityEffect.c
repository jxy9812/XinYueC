/******************************************************************************
 * @file       XGraphicsOpacityEffect.c
 * @brief      不透明度效果实现（对标 Qt 6.8 QGraphicsOpacityEffect）。
 * @details    Draw 实现：输出画布上以 XPainter_setOpacity 设定透明度后
 *             整体绘制源快照——透明底上的 source-over 合成对预乘 ARGB
 *             恰为各通道按 opacity 缩放（对标 Qt draw() 的 p->setOpacity
 *             + drawPixmap 源管线）；opacityMask 非 NoBrush 时再按「画刷
 *             光栅化的掩码 alpha」逐像素相乘（对标 Qt 的 DestinationIn +
 *             fillRect）。不透明度/掩码变化经 update() 触发重绘。
 * @note       掩码坐标 = 控件局部坐标（Qt 的 sourceBoundingRect()），
 *             渐变画刷的几何按该矩形求值（与 XBrush_applyToPainter 的既有
 *             口径一致，XCommonStyle 同样用法）。
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

/** @brief 发射携带 XBrush 参数的信号（无连接时释放参数，防泄漏）。 */
static void xgraphicsopacityeffect_emitBrush(XGraphicsOpacityEffect* self,
                                             size_t signal, XBrush value)
{
    XVarList* args = XVarList_Create(XVar(XBrush, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief 把效果输出按掩码 alpha 相乘（对标 Qt 的 DestinationIn 合成）。
 * @details 掩码图在**画布坐标**上由画刷填充；画刷坐标按控件局部坐标
 *          （widgetRect = (0,0,w,h)）求值，故填充矩形平移 -destRect 后
 *          与画刷几何同源。逐像素 premultiplied 通道统一乘 maskAlpha/255
 *          （Qt DestinationIn：dst = dst × src.alpha）。
 */
static void xgraphicsopacityeffect_applyMask(
    XGraphicsOpacityEffect* self, XGraphicsEffectDrawContext* ctx)
{
    XImage* mask;
    XPainter painter;
    XRect brushRect;
    int dstStride;
    int maskStride;
    uint32_t* dstBits;
    uint32_t* maskBits;
    int width;
    int height;
    int x;
    int y;
    if (!self || !ctx || !ctx->m_dest || XImage_isNull(ctx->m_dest)) return;
    if (XBrush_style(&self->m_opacityMask) == XBrushStyle_NoBrush) return;
    mask = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!mask) return;
    if (!XImage_reinit_ex(mask, ctx->m_destRect.width, ctx->m_destRect.height,
                          XImageFormat_ARGB32_Premultiplied)) {
        XClassDelete(mask);
        return;
    }
    XImage_fillRect(mask, NULL, 0u);
    /* 画刷填充区域 = 控件局部坐标 (0,0,w,h) 平移到画布原点（掩码画布
       原点对应 destRect.topLeft）。负宽/高由 fillRect 按 Qt 交换几何边。 */
    brushRect.x = -ctx->m_destRect.x;
    brushRect.y = -ctx->m_destRect.y;
    brushRect.width = 0;
    brushRect.height = 0;
    if (self->m_base.m_source) {
        brushRect.width = XWidget_width(self->m_base.m_source);
        brushRect.height = XWidget_height(self->m_base.m_source);
    }
    if (brushRect.width <= 0 || brushRect.height <= 0) {
        /* 无源尺寸可依（理论上 Draw 期间必有源）：退回填满画布，
           把掩码视为画布范围的画刷。 */
        brushRect.x = 0;
        brushRect.y = 0;
        brushRect.width = ctx->m_destRect.width;
        brushRect.height = ctx->m_destRect.height;
    }
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, mask)) {
        XBrush_applyToPainter(&self->m_opacityMask, &painter, &brushRect);
        XPainter_fillRect_2(&painter, &brushRect);
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    dstBits = (uint32_t*)(void*)XImage_bits(ctx->m_dest);
    maskBits = (uint32_t*)(void*)XImage_bits(mask);
    if (!dstBits || !maskBits) {
        XClassDelete(mask);
        return;
    }
    dstStride = XImage_bytesPerLine(ctx->m_dest) / 4;
    maskStride = XImage_bytesPerLine(mask) / 4;
    width = XImage_width(mask);
    height = XImage_height(mask);
    for (y = 0; y < height; ++y) {
        uint32_t* dstRow = dstBits + (size_t)y * (size_t)dstStride;
        const uint32_t* maskRow = maskBits + (size_t)y * (size_t)maskStride;
        for (x = 0; x < width; ++x) {
            uint32_t alpha = (maskRow[x] >> 24) & 0xFFu;
            uint32_t pixel;
            if (alpha == 0u) {
                dstRow[x] = 0u;
                continue;
            }
            if (alpha == 255u) continue;
            /* 预乘通道 × maskAlpha/255（四舍五入）。 */
            pixel = dstRow[x];
            dstRow[x] =
                (((((pixel >> 24) & 0xFFu) * alpha + 127u) / 255u) << 24) |
                (((((pixel >> 16) & 0xFFu) * alpha + 127u) / 255u) << 16) |
                (((((pixel >> 8) & 0xFFu) * alpha + 127u) / 255u) << 8) |
                (((pixel & 0xFFu) * alpha + 127u) / 255u);
        }
    }
    XClassDelete(mask);
}

/** @brief Draw 槽位实现：源快照按 m_opacity 绘入输出画布，再叠掩码。 */
static void VXGraphicsOpacityEffect_draw(XGraphicsEffect* base,
                                         XGraphicsEffectDrawContext* ctx)
{
    XGraphicsOpacityEffect* self = (XGraphicsOpacityEffect*)base;
    XPainter painter;
    if (!self || !ctx || !ctx->m_source || !ctx->m_dest) return;
    if (XImage_isNull(ctx->m_dest)) return;
    /* 对标 Qt QGraphicsOpacityEffect::draw 首行：
       `if (qFuzzyIsNull(d->opacity) || opacity == 0) return;` —— 全透明
       时不产生任何绘制（画布保持全透明），省掉一次整幅合成。 */
    if (self->m_opacity <= 0.0f) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, ctx->m_dest)) {
        XPainter_deinit(&painter);
        return;
    }
    /* 对标 Qt QGraphicsOpacityEffect::draw：p->setOpacity(d->opacity) 后
       绘制 source。画布为透明底，source-over 合成结果 = 预乘源 × opacity。 */
    XPainter_setOpacity(&painter, self->m_opacity);
    XGraphicsEffect_drawSource(&self->m_base, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    /* 掩码在绘制之后按 alpha 相乘（Qt 先把掩码 DestinationIn 到源 pixmap，
       再以 opacity 绘制——乘法可交换，结果等价）。 */
    xgraphicsopacityeffect_applyMask(self, ctx);
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
    /* 掩码缺省 NoBrush（Qt 文档「There is no opacity mask by default」；
       本机 Qt 6.8 探针实测 opacityMask().style() == NoBrush(0)）。 */
    XBrush_init(&self->m_opacityMask);
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

XBrush XGraphicsOpacityEffect_opacityMask(
    const XGraphicsOpacityEffect* self)
{
    if (self) return self->m_opacityMask;
    return XBrush_create();
}

void XGraphicsOpacityEffect_setOpacityMask(XGraphicsOpacityEffect* self,
                                           XBrush mask)
{
    if (!self) return;
    /* 对标 Qt setOpacityMask（qgraphicseffect.cpp:1131-1141）：
     * 按 QBrush 值比较早退（本机实测同值不同实例不发射），实际变化才
     * 存值 + update + emit。 */
    if (XBrush_isEqual(&self->m_opacityMask, &mask)) return;
    self->m_opacityMask = mask;
    XGraphicsEffect_update(&self->m_base);
    xgraphicsopacityeffect_emitBrush(
        self, (size_t)XGraphicsOpacityEffect_opacityMaskChanged_signal, mask);
}

/* ==================== 信号 ==================== */

void* XGraphicsOpacityEffect_opacityChanged_signal(
    XGraphicsOpacityEffect* self, float opacity)
{
    (void)self; (void)opacity;
    return (void*)(size_t)XGraphicsOpacityEffect_opacityChanged_signal;
}

void* XGraphicsOpacityEffect_opacityMaskChanged_signal(
    XGraphicsOpacityEffect* self, XBrush mask)
{
    (void)self; (void)mask;
    return (void*)(size_t)XGraphicsOpacityEffect_opacityMaskChanged_signal;
}

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */
