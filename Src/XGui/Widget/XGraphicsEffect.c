/******************************************************************************
 * @file       XGraphicsEffect.c
 * @brief      图形效果基类实现（对标 Qt 6.8 QGraphicsEffect 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。setEnabled 在状态实际变化
 *             时发射 enabledChanged 并请求重绘；update() 把效果包围盒标
 *             记为脏区；sourceChanged/boundingRectFor/draw 为虚槽位（基
 *             类默认实现分别对应 Qt 的"转 update/原样返回/绘制源"语义）。
 *             XGraphicsEffect_drawWidget 承载 source→pixmap→draw 离屏
 *             管线的效果处理与回贴步。效果对象由 XWidget_setGraphicsEffect
 *             挂接（XWidget 拥有，Qt 语义）。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"

#if XWIDGET_ON

#include "XGraphicsEffect.h"
#include "XWidget.h"
#include "XWidget_Protected.h" /* XWidget_paintImage/paintOffset（框架内部
                                    绘制入口，效果回贴目标获取用）。 */
#include "XImage.h"
#include "XPainter.h"

#include <math.h>

/* ==================== 内部辅助 ==================== */

/** @brief 发射携带 bool 参数的信号。 */
static void xgraphicseffect_emitBool(XGraphicsEffect* self, size_t signal,
                                     bool enabled)
{
    XVarList* args = XVarList_Create(XVar(bool, enabled));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 整数矩形求交；不相交返回 false。 */
static bool xgraphicseffect_intersectRect(const XRect* a, const XRect* b,
                                          XRect* out)
{
    int x1, y1, x2, y2;
    if (!a || !b || !out) return false;
    x1 = a->x > b->x ? a->x : b->x;
    y1 = a->y > b->y ? a->y : b->y;
    x2 = (a->x + a->width) < (b->x + b->width) ? (a->x + a->width)
                                               : (b->x + b->width);
    y2 = (a->y + a->height) < (b->y + b->height) ? (a->y + a->height)
                                                 : (b->y + b->height);
    if (x2 <= x1 || y2 <= y1) return false;
    out->x = x1;
    out->y = y1;
    out->width = x2 - x1;
    out->height = y2 - y1;
    return true;
}

/** @brief XRectF → XRect：原点向下取整、尺寸向上取整（外扩不丢像素）。 */
static XRect xgraphicseffect_toRect(const XRectF* rect)
{
    XRect out;
    int x2;
    int y2;
    out.x = (int)floorf(rect->x);
    out.y = (int)floorf(rect->y);
    x2 = (int)ceilf(rect->x + rect->width);
    y2 = (int)ceilf(rect->y + rect->height);
    out.width = x2 - out.x;
    out.height = y2 - out.y;
    return out;
}

/* ==================== 虚槽位默认实现（对标 Qt 基类行为） ==================== */

/** @brief SourceChanged 默认实现：请求重绘（XGui 适配语义；Qt 基类为空，
 *         XGui 无独立脏区传播链，转 update() 保证源变更即时生效）。 */
static void VXGraphicsEffect_sourceChanged(XGraphicsEffect* self, int flags)
{
    (void)flags;
    XGraphicsEffect_update(self);
}

/** @brief BoundingRectFor 默认实现：原样返回（对标 Qt 基类 boundingRectFor）。 */
static XRectF VXGraphicsEffect_boundingRectFor(const XGraphicsEffect* self,
                                               const XRectF* sourceRect)
{
    XRectF out;
    (void)self;
    if (!sourceRect) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.width = 0.0f;
        out.height = 0.0f;
        return out;
    }
    return *sourceRect;
}

/** @brief Draw 默认实现：把源快照原样搬入输出画布（对标 Qt"无效果时
 *         source 原样输出"；源/画布按控件局部坐标对齐，越界自动裁剪）。 */
static void VXGraphicsEffect_draw(XGraphicsEffect* self,
                                  XGraphicsEffectDrawContext* ctx)
{
    XPainter painter;
    if (!self || !ctx || !ctx->m_source || !ctx->m_dest) return;
    if (XImage_isNull(ctx->m_dest)) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, ctx->m_dest)) {
        XPainter_deinit(&painter);
        return;
    }
    XPainter_drawImage(&painter, ctx->m_source,
                       -ctx->m_destRect.x, -ctx->m_destRect.y);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ==================== 类与实例生命周期 ==================== */

XVtable* XGraphicsEffect_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsEffect)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_SourceChanged,
                             VXGraphicsEffect_sourceChanged);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_BoundingRectFor,
                             VXGraphicsEffect_boundingRectFor);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_Draw, VXGraphicsEffect_draw);
    return XVTABLE_DEFAULT;
}

void XGraphicsEffect_init(XGraphicsEffect* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XGraphicsEffect);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_enabled = true;
}

XGraphicsEffect* XGraphicsEffect_create_ex(XMemoryType memory)
{
    XGraphicsEffect* self =
        (XGraphicsEffect*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGraphicsEffect_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

bool XGraphicsEffect_isEnabled(const XGraphicsEffect* self)
{ return self ? self->m_enabled : false; }

void XGraphicsEffect_setEnabled(XGraphicsEffect* self, bool enable)
{
    if (!self || self->m_enabled == enable) return;
    self->m_enabled = enable;
    xgraphicseffect_emitBool(self,
                             (size_t)XGraphicsEffect_enabledChanged_signal,
                             enable);
    /* 对标 Qt：启用状态变化即触发重绘——启用呈现效果、禁用恢复原样。 */
    XGraphicsEffect_update(self);
}

void XGraphicsEffect_update(XGraphicsEffect* self)
{
    XRectF boundsF;
    XRect bounds;
    if (!self || !self->m_source) return;
    /* 对标 QGraphicsEffect::update：把效果包围盒（含外扩）整体标脏。
       boundingRect 以父级相对原点构造，先平移回源控件局部坐标系
       （updateRect 语义为控件局部坐标）；超出控件矩形的部分由
       XWidget_addDirtyRegion 按 contentsRect 裁剪（简化项：效果外扩
       区随覆盖该带的父级重绘呈现，见 XWidget 脏区管线限制）。 */
    boundsF = XGraphicsEffect_boundingRect(self);
    boundsF.x -= (float)XWidget_x(self->m_source);
    boundsF.y -= (float)XWidget_y(self->m_source);
    bounds = xgraphicseffect_toRect(&boundsF);
    if (bounds.width <= 0 || bounds.height <= 0) return;
    XWidget_updateRect(self->m_source, &bounds);
}

/* ==================== 效果源与包围盒 ==================== */

XWidget* XGraphicsEffect_source(const XGraphicsEffect* self)
{ return self ? self->m_source : NULL; }

void XGraphicsEffect_setSource(XGraphicsEffect* self, XWidget* source)
{
    if (self) self->m_source = source;
}

void XGraphicsEffect_sourceChanged(XGraphicsEffect* self, int flags)
{
    XGraphicsEffect_SourceChangedSlot slot;
    if (!self || !XClassGetVtable(self)) return;
    slot = XClassGetVirtualFunc(self, EXGraphicsEffect_SourceChanged,
                                XGraphicsEffect_SourceChangedSlot);
    if (!slot) return;
    slot(self, flags);
}

XRectF XGraphicsEffect_boundingRectFor(const XGraphicsEffect* self,
                                       const XRectF* sourceRect)
{
    XGraphicsEffect_BoundingRectForSlot slot;
    if (!self || !XClassGetVtable(self))
        return VXGraphicsEffect_boundingRectFor(self, sourceRect);
    slot = XClassGetVirtualFunc(self, EXGraphicsEffect_BoundingRectFor,
                                XGraphicsEffect_BoundingRectForSlot);
    if (!slot)
        return VXGraphicsEffect_boundingRectFor(self, sourceRect);
    return slot(self, sourceRect);
}

XRectF XGraphicsEffect_boundingRect(const XGraphicsEffect* self)
{
    XRectF out;
    if (!self || !self->m_source) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.width = 0.0f;
        out.height = 0.0f;
        return out;
    }
    /* 对标 Qt：有源时对其源矩形求效果包围盒（XGui 源为承载控件几何）。 */
    out.x = (float)XWidget_x(self->m_source);
    out.y = (float)XWidget_y(self->m_source);
    out.width = (float)XWidget_width(self->m_source);
    out.height = (float)XWidget_height(self->m_source);
    return XGraphicsEffect_boundingRectFor(self, &out);
}

/* ==================== 效果绘制管线 ==================== */

void XGraphicsEffect_blurBox3(XImage* image, const XRect* rect)
{
    XRect area;
    XRect bounds;
    uint32_t* temp;
    size_t tempBytes;
    int stridePixels;
    uint32_t* bits;
    int x;
    int y;
    int ch;
    if (!image || XImage_isNull(image)) return;
    /* 简化项：仅处理 ARGB32 类 4 字节/像素格式（效果画布恒为
       ARGB32_Premultiplied），其余格式原样返回。 */
    stridePixels = XImage_bytesPerLine(image) / 4;
    if (stridePixels <= 0) return;
    bounds.x = 0;
    bounds.y = 0;
    bounds.width = XImage_width(image);
    bounds.height = XImage_height(image);
    area = (rect && rect->width > 0 && rect->height > 0) ? *rect : bounds;
    if (!xgraphicseffect_intersectRect(&area, &bounds, &area)) return;
    /* 对标 3x3 盒式卷积的水平+垂直两趟分离实现：水平趟结果写入临时
       缓冲，垂直趟从临时缓冲读回写图像，避免原地覆盖造成方向偏差。 */
    tempBytes = (size_t)area.width * (size_t)area.height * sizeof(uint32_t);
    temp = (uint32_t*)XMemory_malloc(tempBytes, XCLASS_DEFAULT_MEMORY_TYPE);
    if (!temp) return;
    bits = (uint32_t*)(void*)XImage_bits(image);
    if (!bits) {
        XMemory_free(temp, XCLASS_DEFAULT_MEMORY_TYPE);
        return;
    }
    /* 水平趟：out[x] = (in[x-1] + in[x] + in[x+1]) / 3，越界视为 0。 */
    for (y = 0; y < area.height; ++y) {
        const uint32_t* row =
            bits + (size_t)(area.y + y) * (size_t)stridePixels + area.x;
        uint32_t* outRow = temp + (size_t)y * (size_t)area.width;
        for (x = 0; x < area.width; ++x) {
            uint32_t out = 0;
            for (ch = 0; ch < 4; ++ch) {
                uint32_t acc = 0;
                int k;
                for (k = -1; k <= 1; ++k) {
                    int sx = x + k;
                    if (sx < 0 || sx >= area.width) continue;
                    acc += (row[sx] >> (ch * 8)) & 0xFFu;
                }
                out |= ((acc + 1u) / 3u) << (ch * 8);
            }
            outRow[x] = out;
        }
    }
    /* 垂直趟：从临时缓冲读，写回图像（覆盖原像素）。 */
    for (y = 0; y < area.height; ++y) {
        uint32_t* outRow =
            bits + (size_t)(area.y + y) * (size_t)stridePixels + area.x;
        for (x = 0; x < area.width; ++x) {
            uint32_t out = 0;
            for (ch = 0; ch < 4; ++ch) {
                uint32_t acc = 0;
                int k;
                for (k = -1; k <= 1; ++k) {
                    int sy = y + k;
                    if (sy < 0 || sy >= area.height) continue;
                    acc += (temp[(size_t)sy * (size_t)area.width + x] >>
                            (ch * 8)) & 0xFFu;
                }
                out |= ((acc + 1u) / 3u) << (ch * 8);
            }
            outRow[x] = out;
        }
    }
    XMemory_free(temp, XCLASS_DEFAULT_MEMORY_TYPE);
}

bool XGraphicsEffect_drawWidget(XGraphicsEffect* self, XWidget* widget,
                                XImage* sourceImage, const XRegion* paintRegion)
{
    XRect widgetRect;
    XRect dirtyRect;
    XRect srcRect;
    XRectF srcRectF;
    XRectF destRectF;
    XRect destRect;
    XRect srcRectX;
    XRectF srcRectXF;
    XRectF destRectXF;
    XGraphicsEffectDrawContext ctx;
    XImage* dest;
    XPainter painter;
    XPoint offset;
    XImage* target;
    int padL;
    int padT;
    int padR;
    int padB;
    XGraphicsEffect_DrawSlot slot;
    if (!self || !widget || !sourceImage || XImage_isNull(sourceImage) ||
        !paintRegion || paintRegion->count <= 0)
        return false;
    /* 绘制目标不可得时交回调用方常规路径（与 paintEvent 无目标时的
       "同步派发、不上屏"语义一致）。 */
    target = XWidget_paintImage(widget);
    if (!target) return false;
    offset = XWidget_paintOffset(widget);
    widgetRect.x = 0;
    widgetRect.y = 0;
    widgetRect.width = XWidget_width(widget);
    widgetRect.height = XWidget_height(widget);
    if (widgetRect.width <= 0 || widgetRect.height <= 0) return false;
    /* 性能边界（简化标注）：处理区域取"脏区∩控件区域"。效果控件区域
       小于视口时仅处理该区域；Qt 走 dirty region 与 effect boundingRect
       的完整求交，XGui 以外接矩形近似（多矩形脏区按外接框处理）。 */
    XRegion_boundingRect(paintRegion, &dirtyRect);
    if (!xgraphicseffect_intersectRect(&dirtyRect, &widgetRect, &srcRect))
        return false;
    /* 按效果包围盒语义求外扩量，让模糊/阴影采样到脏区周边的源像素。 */
    srcRectF.x = (float)srcRect.x;
    srcRectF.y = (float)srcRect.y;
    srcRectF.width = (float)srcRect.width;
    srcRectF.height = (float)srcRect.height;
    destRectF = XGraphicsEffect_boundingRectFor(self, &srcRectF);
    padL = (int)ceilf(srcRectF.x - destRectF.x);
    padT = (int)ceilf(srcRectF.y - destRectF.y);
    padR = (int)ceilf(destRectF.x + destRectF.width -
                      (srcRectF.x + srcRectF.width));
    padB = (int)ceilf(destRectF.y + destRectF.height -
                      (srcRectF.y + srcRectF.height));
    if (padL < 0) padL = 0;
    if (padT < 0) padT = 0;
    if (padR < 0) padR = 0;
    if (padB < 0) padB = 0;
    srcRectX = srcRect;
    srcRectX.x -= padL;
    srcRectX.y -= padT;
    srcRectX.width += padL + padR;
    srcRectX.height += padT + padB;
    if (!xgraphicseffect_intersectRect(&srcRectX, &widgetRect, &srcRectX))
        srcRectX = srcRect;
    /* 输出画布 = 外扩后源区域的效果包围盒（整数化，向下/向上取整）。 */
    srcRectXF.x = (float)srcRectX.x;
    srcRectXF.y = (float)srcRectX.y;
    srcRectXF.width = (float)srcRectX.width;
    srcRectXF.height = (float)srcRectX.height;
    destRectXF = XGraphicsEffect_boundingRectFor(self, &srcRectXF);
    if (destRectXF.width <= 0.0f || destRectXF.height <= 0.0f) return false;
    destRect = xgraphicseffect_toRect(&destRectXF);
    if (destRect.width <= 0 || destRect.height <= 0) return false;
    dest = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!dest) return false;
    if (!XImage_reinit_ex(dest, destRect.width, destRect.height,
                          XImageFormat_ARGB32_Premultiplied)) {
        XImage_delete_base(dest);
        return false;
    }
    /* 与 XWidget_grab 快照同语义：输出画布从全透明开始，Draw 实现负责
       填充全部内容，防止复用实现变化时残留旧像素。 */
    XImage_fillRect(dest, NULL, 0u);
    ctx.m_source = sourceImage;
    ctx.m_sourceRect = srcRectX;
    ctx.m_dest = dest;
    ctx.m_destRect = destRect;
    /* 对标 QGraphicsEffect::draw 虚调用：交由派生效果完成像素处理。 */
    slot = NULL;
    if (XClassGetVtable(self))
        slot = XClassGetVirtualFunc(self, EXGraphicsEffect_Draw,
                                    XGraphicsEffect_DrawSlot);
    if (slot) slot(self, &ctx);
    else VXGraphicsEffect_draw(self, &ctx);
    /* 回贴：画布按 destRect 平移到控件局部坐标，再叠加绘制偏移写入
       绘制目标。离屏段（xwidget_drawWithGraphicsEffect）已摘除设备
       坐标的表面裁剪，回贴以自身输出矩形自限：效果输出恰好覆盖
       boundingRectFor(脏区∩控件区域)，越界部分为效果外扩环（投影/
       模糊边带），其内容来自本轮全新快照，重绘是正确语义；自限同时
       防止越界写邻接控件区域（对标 Qt：效果结果绘制受绘制引擎裁剪
       限定在其包围盒内）。 */
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, target)) {
        XRect blitClip;
        blitClip.x = offset.x + destRect.x;
        blitClip.y = offset.y + destRect.y;
        blitClip.width = destRect.width;
        blitClip.height = destRect.height;
        XPainter_setClipRect(&painter, &blitClip,
                             XPainterClipOperation_ReplaceClip);
        XPainter_drawImage(&painter, dest,
                           offset.x + destRect.x, offset.y + destRect.y);
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    XImage_delete_base(dest);
    return true;
}

/* ==================== 信号 ==================== */

void* XGraphicsEffect_enabledChanged_signal(XGraphicsEffect* self, bool enabled)
{
    xgraphicseffect_emitBool(self,
                             (size_t)XGraphicsEffect_enabledChanged_signal,
                             enabled);
    return (void*)(size_t)XGraphicsEffect_enabledChanged_signal;
}

#endif /* XWIDGET_ON */
