/******************************************************************************
 * @file       XFrame.c
 * @brief      XFrame 框架控件实现（对标 Qt 6.8 QFrame 全部公开 API 的 C 适配）。
 * @details    实现要点：
 *             - 生命周期走 XClass 体系：XFrame_init/create_ex 挂 XFrame
 *               虚表，重载 EXObject_Event / EXWidget_PaintEvent /
 *               EXWidget_ChangeEvent / EXClass_Copy / EXClass_Move；
 *             - 样式与几何：m_frameStyle 保存 Shape|Shadow 组合值；
 *               setFrameStyle/Shape/Shadow 统一刷新尺寸策略（未显式调用
 *               XWidget_setSizePolicy 时按形状接管）、sizeHint 与几何，
 *               并重算边框宽度（对标 Qt QFrame::setFrameStyle 语义）；
 *             - frameWidth 计算按 Qt SE_ShapedFrameContents 规则：
 *               NoFrame=0；Box/HLine/VLine Plain=lineWidth、Raised/Sunken=
 *               2*lineWidth+midLineWidth；WinPanel=2；Panel=lineWidth；
 *               StyledPanel=PM_DefaultFrameWidth（=2）；四周宽度一致；
 *             - 绘制：XFrame_initStyleOption 填充与 QStyleOptionFrame
 *               等价的 XFrameStyleOption，再按 Qt QCommonStyle::
 *               CE_ShapedFrame 分发给内部移植的 qDrawPlainRect /
 *               qDrawShadeRect / qDrawShadePanel / qDrawShadeLine /
 *               qDrawWinPanel，全部经 XPainter 软件光栅输出，不引入
 *               任何平台/后端 API；XPALETTE_ON=0 时绘制退化为无操作，
 *               但 frameWidth 计算与几何仍可用；
 *             - 事件：ParentChange（事件前）与 Polish（事件后）重算边框
 *               宽度；ChangeEvent 对 StyleChange 重算边框宽度并忽略事件
 *               （与 XWidget 默认 changeEvent 槽忽略行为一致，且避免
 *               经虚表调用基类入口造成递归）；
 *             - paintEvent：在 paintDevice 图像上先按 paintOffset 平移，
 *               再把事件矩形平移后设为裁剪区，最后调用 XFrame_drawFrame，
 *               与 XWidget 默认绘制路径保持一致。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XFrame.h"
#include "XMemory.h"
#include "XEventType.h"
#include "XEvent.h"
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif
#include <string.h>

#if XWIDGET_ON && XFRAME_ON

/* ==================== 内部函数声明 ==================== */

static void XFrame_updateStyledFrameWidths(XFrame* self);
static void XFrame_updateFrameWidth(XFrame* self);

/* ==================== 调色板前景角色解析（对标 QWidget::foregroundRole） ==================== */

/**
 * @brief      返回绘制用的前景颜色角色。
 * @details    显式前景角色优先（NoRole 之外的任何角色）；未设置时按 Qt
 *             QWidget::foregroundRole 的规则由背景角色推断：
 *             Button→ButtonText、Base→Text、Dark/Shadow→Light、
 *             Highlight→HighlightedText、ToolTipBase→ToolTipText、
 *             其余（默认 Window）→WindowText。
 */
static XPaletteColorRole frame_foregroundRole(const XFrame* self)
{
    XPaletteColorRole role = XWidget_foregroundRole(&self->m_base);
    if (role != XPaletteColorRole_NoRole)
        return role;
    switch (XWidget_backgroundRole(&self->m_base)) {
    case XPaletteColorRole_Button:
        return XPaletteColorRole_ButtonText;
    case XPaletteColorRole_Base:
        return XPaletteColorRole_Text;
    case XPaletteColorRole_Dark:
    case XPaletteColorRole_Shadow:
        return XPaletteColorRole_Light;
    case XPaletteColorRole_Highlight:
        return XPaletteColorRole_HighlightedText;
    case XPaletteColorRole_ToolTipBase:
        return XPaletteColorRole_ToolTipText;
    default:
        return XPaletteColorRole_WindowText;
    }
}

#if XPALETTE_ON
/* ==================== qdrawutil 软件绘制内核（逐行对照 Qt 6.8 源码） ==================== */

/**
 * @brief 取当前颜色组下指定角色颜色并转为 ARGB32。内部小工具。
 */
static uint32_t frame_paletteColor(const XPalette* palette,
                                   XPaletteColorRole role)
{
    XColor c = XPalette_color(palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
}

/**
 * @brief      单色矩形边框（对标 qDrawPlainRect，无内部填充）。
 * @details    逐层画 lineWidth 个同心矩形：第 i 层
 *             (x+i, y+i, w-2i-1, h-2i-1)，宽度/高度不足时由
 *             XPainter_drawRect 自然跳过。
 */
static void frame_drawPlainRect(XPainter* painter, const XRect* r,
                                uint32_t color, int lineWidth)
{
    int i;
    if (!painter || !r || r->width <= 0 || r->height <= 0 || lineWidth <= 0)
        return;
    XPainter_setPen(painter, color);
    for (i = 0; i < lineWidth; ++i) {
        XRect one = { r->x + i, r->y + i,
                      r->width - i * 2 - 1, r->height - i * 2 - 1 };
        XPainter_drawRect(painter, &one);
    }
}

/**
 * @brief      立体盒状边框（对标 qDrawShadeRect，无内部填充）。
 * @details    实现与 Qt 完全一致：
 *             - lw==1 && mlw==0 走标准分支：整框用 pen1 画外圈，再用 pen2
 *               补内侧三边；
 *             - 复杂分支：pen1 画上/左/右下三侧，Mid 画中间过渡环，
 *               pen2 画下/右剩余阴影。坐标全部按 Qt 源码逐行推导。
 */
static void frame_drawShadeRect(XPainter* painter, const XRect* r,
                                const XPalette* palette, bool sunken,
                                int lineWidth, int midLineWidth)
{
    int x1, y1, x2, y2;
    int m, k, j, i;
    uint32_t pen1, pen2;
    if (!painter || !r || !palette) return;
    if (r->width == 0 || r->height == 0) return;
    if (r->width < 0 || r->height < 0 || lineWidth < 0 || midLineWidth < 0)
        return;
    x1 = r->x;
    y1 = r->y;
    x2 = r->x + r->width - 1;
    y2 = r->y + r->height - 1;
    pen1 = sunken ? frame_paletteColor(palette, XPaletteColorRole_Dark)
                  : frame_paletteColor(palette, XPaletteColorRole_Light);
    pen2 = sunken ? frame_paletteColor(palette, XPaletteColorRole_Light)
                  : frame_paletteColor(palette, XPaletteColorRole_Dark);

    if (lineWidth == 1 && midLineWidth == 0) {
        XRect one = { x1, y1, r->width - 2, r->height - 2 };
        XPainter_setPen(painter, pen1);
        XPainter_drawRect(painter, &one);
        XPainter_setPen(painter, pen2);
        XPainter_drawLine(painter, x1 + 1, y1 + 1, x2 - 2, y1 + 1);
        XPainter_drawLine(painter, x1 + 1, y1 + 2, x1 + 1, y2 - 2);
        XPainter_drawLine(painter, x1, y2, x2, y2);
        XPainter_drawLine(painter, x2, y1, x2, y2 - 1);
        return;
    }

    /* 第一组（pen1）：上、左与内侧右下阴影。 */
    m = lineWidth + midLineWidth;
    k = m;
    XPainter_setPen(painter, pen1);
    for (i = 0; i < lineWidth; ++i) {
        XPainter_drawLine(painter, x1 + i, y2 - i, x1 + i, y1 + i);
        XPainter_drawLine(painter, x1 + i, y1 + i, x2 - i, y1 + i);
        XPainter_drawLine(painter, x1 + k, y2 - k, x2 - k, y2 - k);
        XPainter_drawLine(painter, x2 - k, y2 - k, x2 - k, y1 + k);
        ++k;
    }
    /* 中间过渡环（Mid 角色）。 */
    if (midLineWidth > 0) {
        XPainter_setPen(painter,
                        frame_paletteColor(palette, XPaletteColorRole_Mid));
        j = lineWidth * 2;
        for (i = 0; i < midLineWidth; ++i) {
            XRect mid = { x1 + lineWidth + i, y1 + lineWidth + i,
                          r->width - j - 1, r->height - j - 1 };
            XPainter_drawRect(painter, &mid);
            j += 2;
        }
    }
    /* 第二组（pen2）：下、右阴影。 */
    XPainter_setPen(painter, pen2);
    k = m;
    for (i = 0; i < lineWidth; ++i) {
        XPainter_drawLine(painter, x1 + 1 + i, y2 - i, x2 - i, y2 - i);
        XPainter_drawLine(painter, x2 - i, y2 - i, x2 - i, y1 + i + 1);
        XPainter_drawLine(painter, x1 + k, y2 - k, x1 + k, y1 + k);
        XPainter_drawLine(painter, x1 + k, y1 + k, x2 - k, y1 + k);
        ++k;
    }
}

/**
 * @brief      立体面板边框（对标 qDrawShadePanel，无内部填充）。
 * @details    pen1（sunken?Dark:Light）画上边与左边；pen2
 *             （sunken?Light:Dark）画下边与右边；左边起点带 +lineWidth
 *             横向偏移（Qt 源码左线公式），右边终点收在
 *             y+h-lineWidth-1。
 */
static void frame_drawShadePanel(XPainter* painter, const XRect* r,
                                 const XPalette* palette, bool sunken,
                                 int lineWidth)
{
    int x, y, w, h, i;
    uint32_t pen1, pen2;
    if (!painter || !r || !palette) return;
    if (r->width == 0 || r->height == 0) return;
    if (r->width < 0 || r->height < 0 || lineWidth < 0) return;
    x = r->x;
    y = r->y;
    w = r->width;
    h = r->height;
    pen1 = sunken ? frame_paletteColor(palette, XPaletteColorRole_Dark)
                  : frame_paletteColor(palette, XPaletteColorRole_Light);
    pen2 = sunken ? frame_paletteColor(palette, XPaletteColorRole_Light)
                  : frame_paletteColor(palette, XPaletteColorRole_Dark);
    XPainter_setPen(painter, pen1);
    for (i = 0; i < lineWidth; ++i) {
        /* 上边：(x, y+i) -> (x+w-2-i, y+i) */
        XPainter_drawLine(painter, x, y + i, x + w - 2 - i, y + i);
        /* 左边：(x+lw+i, y+h-2) -> (x+lw+i, y+lw-i) */
        XPainter_drawLine(painter, x + lineWidth + i, y + h - 2,
                          x + lineWidth + i, y + lineWidth - i);
    }
    XPainter_setPen(painter, pen2);
    for (i = 0; i < lineWidth; ++i) {
        /* 下边：(x+i, y+h-1-i) -> (x+w-1, y+h-1-i) */
        XPainter_drawLine(painter, x + i, y + h - 1 - i,
                          x + w - 1, y + h - 1 - i);
        /* 右边：(x+w-1-i, y+i) -> (x+w-1-i, y+h-lw-1) */
        XPainter_drawLine(painter, x + w - 1 - i, y + i,
                          x + w - 1 - i, y + h - lineWidth - 1);
    }
}

/**
 * @brief      Windows 风格四色边框（对标 qDrawWinShades）。
 * @details    c1/c2 各是一条两段折线；矩形大于 4x4 时再补 c3/c4 内环，
 *             参数顺序与 qDrawWinPanel 的 sunken/raised 颜色映射一致。
 */
static void frame_drawWinShades(XPainter* painter, const XRect* r,
                                uint32_t c1, uint32_t c2,
                                uint32_t c3, uint32_t c4)
{
    int x, y, w, h;
    if (!painter || !r) return;
    x = r->x;
    y = r->y;
    w = r->width;
    h = r->height;
    if (w < 2 || h < 2) return;
    XPainter_setPen(painter, c1);
    XPainter_drawLine(painter, x, y + h - 2, x, y);
    XPainter_drawLine(painter, x, y, x + w - 2, y);
    XPainter_setPen(painter, c2);
    XPainter_drawLine(painter, x, y + h - 1, x + w - 1, y + h - 1);
    XPainter_drawLine(painter, x + w - 1, y + h - 1, x + w - 1, y);
    if (w > 4 && h > 4) {
        XPainter_setPen(painter, c3);
        XPainter_drawLine(painter, x + 1, y + h - 3, x + 1, y + 1);
        XPainter_drawLine(painter, x + 1, y + 1, x + w - 3, y + 1);
        XPainter_setPen(painter, c4);
        XPainter_drawLine(painter, x + 1, y + h - 2, x + w - 2, y + h - 2);
        XPainter_drawLine(painter, x + w - 2, y + h - 2, x + w - 2, y + 1);
    }
}

/**
 * @brief      Windows 经典面板（对标 qDrawWinPanel）。
 * @details    线宽固定 2：sunken 用 Dark/Light/Shadow/Midlight，
 *             raised 用 Light/Shadow/Midlight/Dark（顺序同 Qt）。
 */
static void frame_drawWinPanel(XPainter* painter, const XRect* r,
                               const XPalette* palette, bool sunken)
{
    uint32_t c1, c2, c3, c4;
    if (!painter || !r || !palette) return;
    if (sunken) {
        c1 = frame_paletteColor(palette, XPaletteColorRole_Dark);
        c2 = frame_paletteColor(palette, XPaletteColorRole_Light);
        c3 = frame_paletteColor(palette, XPaletteColorRole_Shadow);
        c4 = frame_paletteColor(palette, XPaletteColorRole_Midlight);
    } else {
        c1 = frame_paletteColor(palette, XPaletteColorRole_Light);
        c2 = frame_paletteColor(palette, XPaletteColorRole_Shadow);
        c3 = frame_paletteColor(palette, XPaletteColorRole_Midlight);
        c4 = frame_paletteColor(palette, XPaletteColorRole_Dark);
    }
    frame_drawWinShades(painter, r, c1, c2, c3, c4);
}

/**
 * @brief      立体分隔线（对标 qDrawShadeLine）。
 * @details    水平线或垂直线二选一；总宽度 tlw=2*lw+mlw，逐层画
 *             pen1（sunken?Dark:Light）/ Mid / pen2（sunken?Light:Dark）
 *             三段，坐标公式与 Qt 源码一致（含 x2--/y2-- 收边）。
 */
static void frame_drawShadeLine(XPainter* painter,
                                int x1, int y1, int x2, int y2,
                                const XPalette* palette, bool sunken,
                                int lineWidth, int midLineWidth)
{
    int tlw, i, t;
    uint32_t pen1, pen2;
    if (!painter || !palette) return;
    if (lineWidth < 0 || midLineWidth < 0) return;
    tlw = lineWidth * 2 + midLineWidth;
    pen1 = sunken ? frame_paletteColor(palette, XPaletteColorRole_Dark)
                  : frame_paletteColor(palette, XPaletteColorRole_Light);
    pen2 = sunken ? frame_paletteColor(palette, XPaletteColorRole_Light)
                  : frame_paletteColor(palette, XPaletteColorRole_Dark);

    if (y1 == y2) {
        int y = y1 - tlw / 2;
        if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
        --x2;
        XPainter_setPen(painter, pen1);
        for (i = 0; i < lineWidth; ++i) {
            XPainter_drawLine(painter, x1 + i, y + tlw - 1 - i,
                              x1 + i, y + i);
            XPainter_drawLine(painter, x1 + i, y + i, x2 - i, y + i);
        }
        if (midLineWidth > 0) {
            XPainter_setPen(painter,
                            frame_paletteColor(palette, XPaletteColorRole_Mid));
            for (i = 0; i < midLineWidth; ++i)
                XPainter_drawLine(painter, x1 + lineWidth, y + lineWidth + i,
                                  x2 - lineWidth, y + lineWidth + i);
        }
        XPainter_setPen(painter, pen2);
        for (i = 0; i < lineWidth; ++i) {
            XPainter_drawLine(painter, x1 + i, y + tlw - i - 1,
                              x2 - i, y + tlw - i - 1);
            XPainter_drawLine(painter, x2 - i, y + tlw - i - 1,
                              x2 - i, y + i + 1);
        }
    } else if (x1 == x2) {
        int x = x1 - tlw / 2;
        if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
        --y2;
        XPainter_setPen(painter, pen1);
        for (i = 0; i < lineWidth; ++i) {
            XPainter_drawLine(painter, x + i, y2, x + i, y1 + i);
            XPainter_drawLine(painter, x + i, y1 + i, x + tlw - 1, y1 + i);
        }
        if (midLineWidth > 0) {
            XPainter_setPen(painter,
                            frame_paletteColor(palette, XPaletteColorRole_Mid));
            for (i = 0; i < midLineWidth; ++i)
                XPainter_drawLine(painter, x + lineWidth + i, y1 + lineWidth,
                                  x + lineWidth + i, y2);
        }
        XPainter_setPen(painter, pen2);
        for (i = 0; i < lineWidth; ++i) {
            XPainter_drawLine(painter, x + lineWidth, y2 - i,
                              x + tlw - i - 1, y2 - i);
            XPainter_drawLine(painter, x + tlw - i - 1, y2 - i,
                              x + tlw - i - 1, y1 + lineWidth);
        }
    }
}

/**
 * @brief      CE_ShapedFrame 形状分发（对标 QCommonStyle::CE_ShapedFrame）。
 * @details    形状/阴影完全按 Qt 分支：Box/StyledPanel/Panel/WinPanel/
 *             HLine/VLine，Plain 走单色（前景角色），立体走对应 shade
 *             内核；StyledPanel 的 Plain 分支按 PE_Frame 语义固定使用
 *             WindowText；NoFrame 与其他非法形状不绘制。
 */
static void frame_drawShaped(XFrame* self, XPainter* painter,
                             const XFrameStyleOption* opt)
{
    int shape, shadow;
    uint32_t fg;
    if (!self || !painter || !opt) return;
    shape = (int)(opt->m_frameShape & XFrameStyleMask_Shape);
    shadow = (int)(opt->m_frameShadow & XFrameStyleMask_Shadow);
    fg = frame_paletteColor(&opt->m_palette, frame_foregroundRole(self));
    switch (shape) {
    case XFrameShape_Box:
        if (shadow == XFrameShadow_Plain)
            frame_drawPlainRect(painter, &opt->m_rect, fg, opt->m_lineWidth);
        else
            frame_drawShadeRect(painter, &opt->m_rect, &opt->m_palette,
                                shadow == XFrameShadow_Sunken,
                                opt->m_lineWidth, opt->m_midLineWidth);
        break;
    case XFrameShape_StyledPanel:
        if (shadow == XFrameShadow_Plain) {
            uint32_t wt = frame_paletteColor(&opt->m_palette,
                                             XPaletteColorRole_WindowText);
            frame_drawPlainRect(painter, &opt->m_rect, wt, opt->m_lineWidth);
        } else {
            frame_drawShadePanel(painter, &opt->m_rect, &opt->m_palette,
                                 shadow == XFrameShadow_Sunken,
                                 opt->m_lineWidth);
        }
        break;
    case XFrameShape_Panel:
        if (shadow == XFrameShadow_Plain)
            frame_drawPlainRect(painter, &opt->m_rect, fg, opt->m_lineWidth);
        else
            frame_drawShadePanel(painter, &opt->m_rect, &opt->m_palette,
                                 shadow == XFrameShadow_Sunken,
                                 opt->m_lineWidth);
        break;
    case XFrameShape_WinPanel:
        if (shadow == XFrameShadow_Plain)
            frame_drawPlainRect(painter, &opt->m_rect, fg, opt->m_lineWidth);
        else
            frame_drawWinPanel(painter, &opt->m_rect, &opt->m_palette,
                               shadow == XFrameShadow_Sunken);
        break;
    case XFrameShape_HLine:
    case XFrameShape_VLine: {
        int p1x, p1y, p2x, p2y;
        if (shape == XFrameShape_HLine) {
            p1x = opt->m_rect.x;
            p1y = opt->m_rect.y + opt->m_rect.height / 2;
            p2x = opt->m_rect.x + opt->m_rect.width;
            p2y = p1y;
        } else {
            p1x = opt->m_rect.x + opt->m_rect.width / 2;
            p1y = opt->m_rect.y;
            p2x = p1x;
            p2y = opt->m_rect.y + opt->m_rect.height;
        }
        if (shadow == XFrameShadow_Plain) {
            XPainter_setPen(painter, fg);
            XPainter_setPenWidth(painter, opt->m_lineWidth);
            XPainter_drawLine(painter, p1x, p1y, p2x, p2y);
        } else {
            frame_drawShadeLine(painter, p1x, p1y, p2x, p2y,
                                &opt->m_palette, shadow == XFrameShadow_Sunken,
                                opt->m_lineWidth, opt->m_midLineWidth);
        }
        break;
    }
    default:
        /* NoFrame 及未知形状：不绘制任何内容。 */
        break;
    }
}
#endif /* XPALETTE_ON */

/* ==================== 虚槽实现（对标 QFrame 事件/复制/移动） ==================== */

/** @brief 控件事件入口：ParentChange 先重算边框宽度，Polish 后重算（Qt 顺序）。 */
static bool VXFrame_event(XWidget* self, XEvent* event)
{
    bool result;
    XEventType type;
    if (!self || !event) return false;
    type = XEvent_type(event);
    if (type == XEVENT_TYPE_PARENT_CHANGE)
        XFrame_updateFrameWidth((XFrame*)self);
    result = XWidget_event_base(self, event);
    if (type == XEVENT_TYPE_POLISH)
        XFrame_updateFrameWidth((XFrame*)self);
    return result;
}

/** @brief 变更事件：StyleChange 重算边框宽度，随后忽略事件（同 XWidget 默认）。 */
static void VXFrame_changeEvent(XWidget* self, XEvent* event)
{
    XEventType type = event ? XEvent_type(event) : XEVENT_TYPE_NONE;
    if (type == XEVENT_TYPE_STYLE_CHANGE)
        XFrame_updateFrameWidth((XFrame*)self);
    XEvent_ignore(event);
}

/** @brief 绘制事件：在 paintDevice 上平移裁剪后调用 drawFrame。 */
static void VXFrame_paintEvent(XWidget* self, XEvent* event)
{
#if !XPALETTE_ON || !XWINDOWEVENT_ON
    (void)self;
    (void)event;
    return;
#else
    XImage* image;
    XPoint offset;
    XPainter painter;
#if XPAINTER_CLIP_ON
    XPaintEvent* pe;
    XRect clip;
#endif
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
#if XPAINTER_CLIP_ON
    pe = (XPaintEvent*)event;
#endif
    image = XWidget_paintDevice(self);
    if (!image) return; /* 未上屏/未创建后备存储时无绘制目标。 */
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPAINTER_CLIP_ON
    clip = XPaintEvent_rect(pe);
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_ReplaceClip);
#endif
    XFrame_drawFrame((XFrame*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XPALETTE_ON && XWINDOWEVENT_ON */
}

/** @brief 深拷贝：基类 XWidget 深拷贝后复制全部边框标量字段。 */
static void VXFrame_copy(XFrame* self, const XFrame* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XFrame_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_frameStyle = other->m_frameStyle;
    self->m_lineWidth = other->m_lineWidth;
    self->m_midLineWidth = other->m_midLineWidth;
    self->m_frameWidth = other->m_frameWidth;
    self->m_leftFrameWidth = other->m_leftFrameWidth;
    self->m_topFrameWidth = other->m_topFrameWidth;
    self->m_rightFrameWidth = other->m_rightFrameWidth;
    self->m_bottomFrameWidth = other->m_bottomFrameWidth;
}

/** @brief 移动语义：基类移动后转移边框字段，源对象边框字段归构造默认值。 */
static void VXFrame_move(XFrame* self, XFrame* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XFrame_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self, (XWidget*)other);
    self->m_frameStyle = other->m_frameStyle;
    self->m_lineWidth = other->m_lineWidth;
    self->m_midLineWidth = other->m_midLineWidth;
    self->m_frameWidth = other->m_frameWidth;
    self->m_leftFrameWidth = other->m_leftFrameWidth;
    self->m_topFrameWidth = other->m_topFrameWidth;
    self->m_rightFrameWidth = other->m_rightFrameWidth;
    self->m_bottomFrameWidth = other->m_bottomFrameWidth;
    /* 源对象归默认：NoFrame|Plain、lineWidth=1、其余 0。 */
    other->m_frameStyle = XFrameShape_NoFrame | XFrameShadow_Plain;
    other->m_lineWidth = 1;
    other->m_midLineWidth = 0;
    other->m_frameWidth = 0;
    other->m_leftFrameWidth = 0;
    other->m_topFrameWidth = 0;
    other->m_rightFrameWidth = 0;
    other->m_bottomFrameWidth = 0;
}

/* ==================== 生命周期（对标 QFrame 构造/析构） ==================== */

XVtable* XFrame_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFrame)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXFrame_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXFrame_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXFrame_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXFrame_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXFrame_move);
    return XVTABLE_DEFAULT;
}

void XFrame_init(XFrame* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(XFrame));
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XFrame);
    self->m_frameStyle = XFrameShape_NoFrame | XFrameShadow_Plain;
    self->m_lineWidth = 1;
    self->m_midLineWidth = 0;
    self->m_frameWidth = 0;
    self->m_leftFrameWidth = 0;
    self->m_topFrameWidth = 0;
    self->m_rightFrameWidth = 0;
    self->m_bottomFrameWidth = 0;
}

XFrame* XFrame_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XFrame* self = (XFrame*)XMemory_malloc(sizeof(XFrame), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(XFrame));
    XFrame_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 样式属性（对标 QFrame public API） ==================== */

int XFrame_frameStyle(const XFrame* self)
{
    return self ? self->m_frameStyle
                : (XFrameShape_NoFrame | XFrameShadow_Plain);
}

XFrameShape XFrame_frameShape(const XFrame* self)
{
    return self ? (XFrameShape)(self->m_frameStyle & XFrameStyleMask_Shape)
                : XFrameShape_NoFrame;
}

XFrameShadow XFrame_frameShadow(const XFrame* self)
{
    return self ? (XFrameShadow)(self->m_frameStyle & XFrameStyleMask_Shadow)
                : XFrameShadow_Plain;
}

void XFrame_setFrameStyle(XFrame* self, int style)
{
    XWidgetSizePolicy policy;
    XSize hint;
    int shape;
    if (!self) return;
    /* 未显式设定过尺寸策略时按形状接管（对标 Qt WA_WState_OwnSizePolicy）。 */
    if (!XWidget_testAttribute(&self->m_base,
                               XWidgetAttribute_WState_OwnSizePolicy)) {
        shape = style & XFrameStyleMask_Shape;
        switch (shape) {
        case XFrameShape_HLine:
            policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Minimum,
                                                 XWidgetSizePolicy_Fixed,
                                                 XWidgetSizePolicyControl_Line);
            break;
        case XFrameShape_VLine:
            policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Fixed,
                                                 XWidgetSizePolicy_Minimum,
                                                 XWidgetSizePolicyControl_Line);
            break;
        default:
            policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Preferred,
                                                 XWidgetSizePolicy_Preferred,
                                                 XWidgetSizePolicyControl_Frame);
            break;
        }
        XWidget_setSizePolicyFull(&self->m_base, &policy);
        XWidget_setAttribute(&self->m_base,
                             XWidgetAttribute_WState_OwnSizePolicy, false);
    }
    self->m_frameStyle = style & (XFrameStyleMask_Shape | XFrameStyleMask_Shadow);
    /* sizeHint：HLine=(-1,3)、VLine=(3,-1)、其余保持基类（-1,-1 默认）。 */
    shape = self->m_frameStyle & XFrameStyleMask_Shape;
    if (shape == XFrameShape_HLine)
        XSize_init(&hint, -1, 3);
    else if (shape == XFrameShape_VLine)
        XSize_init(&hint, 3, -1);
    else
        XSize_init(&hint, -1, -1);
    XWidget_setSizeHint(&self->m_base, &hint);
    XWidget_updateGeometry(&self->m_base);
    XWidget_update(&self->m_base);
    XFrame_updateFrameWidth(self);
}

void XFrame_setFrameShape(XFrame* self, XFrameShape shape)
{
    if (self)
        XFrame_setFrameStyle(self,
            (self->m_frameStyle & XFrameStyleMask_Shadow) | (int)shape);
}

void XFrame_setFrameShadow(XFrame* self, XFrameShadow shadow)
{
    if (self)
        XFrame_setFrameStyle(self,
            (self->m_frameStyle & XFrameStyleMask_Shape) | (int)shadow);
}

int XFrame_lineWidth(const XFrame* self)
{
    return self ? self->m_lineWidth : 1;
}

void XFrame_setLineWidth(XFrame* self, int width)
{
    if (!self) return;
    if (width < 0) width = 0;
    if (width > 255) width = 255;
    if ((short)width == self->m_lineWidth) return;
    self->m_lineWidth = (short)width;
    XFrame_updateFrameWidth(self);
}

int XFrame_midLineWidth(const XFrame* self)
{
    return self ? self->m_midLineWidth : 0;
}

void XFrame_setMidLineWidth(XFrame* self, int width)
{
    if (!self) return;
    if (width < 0) width = 0;
    if (width > 255) width = 255;
    if ((short)width == self->m_midLineWidth) return;
    self->m_midLineWidth = (short)width;
    XFrame_updateFrameWidth(self);
}

int XFrame_frameWidth(const XFrame* self)
{
    return self ? self->m_frameWidth : 0;
}

XRect XFrame_frameRect(const XFrame* self)
{
    XRect fr;
    XRect out = { 0, 0, 0, 0 };
    if (!self) return out;
    fr = XWidget_contentsRect(&self->m_base);
    return XRect_adjusted(&fr,
                          -self->m_leftFrameWidth,
                          -self->m_topFrameWidth,
                          self->m_rightFrameWidth,
                          self->m_bottomFrameWidth);
}

void XFrame_setFrameRect(XFrame* self, const XRect* rect)
{
    XRect cr;
    XRect adj;
    int rightMargin, bottomMargin;
    if (!self) return;
    cr = (rect && rect->width > 0 && rect->height > 0)
             ? *rect
             : XWidget_rect(&self->m_base);
    adj = XRect_adjusted(&cr,
                         self->m_leftFrameWidth, self->m_topFrameWidth,
                         -self->m_rightFrameWidth, -self->m_bottomFrameWidth);
    rightMargin = XRect_right(&cr) - XRect_right(&adj);
    bottomMargin = XRect_bottom(&cr) - XRect_bottom(&adj);
    XWidget_setContentsMargins(&self->m_base,
                              adj.x, adj.y, rightMargin, bottomMargin);
}

XSize XFrame_sizeHint(const XFrame* self)
{
    XSize out;
    int shape;
    if (!self) { XSize_init(&out, -1, -1); return out; }
    shape = self->m_frameStyle & XFrameStyleMask_Shape;
    if (shape == XFrameShape_HLine) { XSize_init(&out, -1, 3); return out; }
    if (shape == XFrameShape_VLine) { XSize_init(&out, 3, -1); return out; }
    return XWidget_sizeHint(&self->m_base);
}

void XFrame_drawFrame(XFrame* self, XPainter* painter)
{
#if !XPALETTE_ON
    (void)self;
    (void)painter;
    return;
#else
    XFrameStyleOption opt;
    if (!self || !painter) return;
    XFrame_initStyleOption(self, &opt);
    if (XPainter_save(painter)) {
        frame_drawShaped(self, painter, &opt);
        XPainter_restore(painter);
    }
#endif /* XPALETTE_ON */
}

void XFrame_initStyleOption(XFrame* self, XFrameStyleOption* option)
{
    int shape, shadow;
    if (!self || !option) return;
    memset(option, 0, sizeof(*option));
    option->m_rect = XFrame_frameRect(self);
    shape = self->m_frameStyle & XFrameStyleMask_Shape;
    shadow = self->m_frameStyle & XFrameStyleMask_Shadow;
    option->m_frameShape = (XFrameShape)shape;
    option->m_frameShadow = (XFrameShadow)shadow;
    switch (shape) {
    case XFrameShape_Box:
    case XFrameShape_HLine:
    case XFrameShape_VLine:
    case XFrameShape_StyledPanel:
    case XFrameShape_Panel:
        option->m_lineWidth = self->m_lineWidth;
        option->m_midLineWidth = self->m_midLineWidth;
        break;
    default:
        /* WinPanel/NoFrame 等不响应自定义线宽：使用综合 frameWidth。 */
        option->m_lineWidth = self->m_frameWidth;
        option->m_midLineWidth = 0;
        break;
    }
    option->m_raised = (shadow == XFrameShadow_Raised);
    option->m_sunken = (shadow == XFrameShadow_Sunken);
#if XPALETTE_ON
    option->m_palette = XWidget_palette(&self->m_base);
#endif /* XPALETTE_ON */
}

/* ==================== 边框宽度计算（对标 QFramePrivate::updateFrameWidth） ==================== */

/** @brief 按形状/阴影/线宽刷新四周与综合边框宽度（SE_ShapedFrameContents 规则）。 */
static void XFrame_updateStyledFrameWidths(XFrame* self)
{
    int fw;
    int shape;
    int shadow;
    if (!self) return;
    shape = self->m_frameStyle & XFrameStyleMask_Shape;
    shadow = self->m_frameStyle & XFrameStyleMask_Shadow;
    switch (shape) {
    case XFrameShape_NoFrame:
        fw = 0;
        break;
    case XFrameShape_Box:
    case XFrameShape_HLine:
    case XFrameShape_VLine:
        fw = (shadow == XFrameShadow_Plain)
                 ? self->m_lineWidth
                 : (short)(self->m_lineWidth * 2 + self->m_midLineWidth);
        break;
    case XFrameShape_WinPanel:
        fw = 2;
        break;
    case XFrameShape_Panel:
        fw = self->m_lineWidth;
        break;
    case XFrameShape_StyledPanel:
        fw = 2; /* PM_DefaultFrameWidth */
        break;
    default:
        fw = 0;
        break;
    }
    self->m_leftFrameWidth = (short)fw;
    self->m_topFrameWidth = (short)fw;
    self->m_rightFrameWidth = (short)fw;
    self->m_bottomFrameWidth = (short)fw;
    self->m_frameWidth = (short)fw;
}

/** @brief 保持 frameRect 稳定：先取当前外形矩形，刷新宽度后写回内容边距。 */
static void XFrame_updateFrameWidth(XFrame* self)
{
    XRect fr;
    if (!self) return;
    fr = XFrame_frameRect(self);
    XFrame_updateStyledFrameWidths(self);
    XFrame_setFrameRect(self, &fr);
}

#endif /* XWIDGET_ON && XFRAME_ON */
