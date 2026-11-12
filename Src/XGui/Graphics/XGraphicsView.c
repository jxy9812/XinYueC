/******************************************************************************
 * @file       XGraphicsView.c
 * @brief      XGraphicsView 场景视图实现（对标 Qt 6.8 QGraphicsView）。
 * @details    与同名头文件的公开 API 一一对应；Qt 6.8.3 函数级出处见
 *             XGraphicsView.h 文件头注，本文件注释只标实现差异与内部约定。
 *             事件入口走 XAbstractScrollArea 的 ViewportEvent 保护槽（框架
 *             的滚动区事件模型：视口收到的每个事件先经私有过滤器转本槽，
 *             鼠标/滚轮在视口层不被冒泡——VXWidget_event 对输入事件恒
 *             return true，见 XWidget.c 分派器），与 Qt 的
 *             QAbstractScrollAreaFilter→视图处理器重映射同构。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XGraphicsView.h"

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && \
    XPAINTER_PATH_ON

#include "XAbstractScrollArea_Protected.h"
#include "XAlignment.h"      /* 对齐位（recalculateContentSize 的缩进支）。 */
#include "XGraphicsItem.h"   /* 矩阵代数 XGraphicsItem_matrix*（W1 公开 API）。 */
#include "XGraphicsScene.h"  /* sceneRect/itemsRect/render/鼠标滚轮派发。 */
#include "XGraphicsView.h"   /* 使 IDE 静读一致；宏守卫后重复包含无害。 */
#include "XScrollBar.h"
#include "XWidget.h"
#include "XWidget_Protected.h" /* XWidget_paintImage/paintOffset（绘制核）。 */
#include "XClass.h"
#include "XMemory.h"
#include "XGradient.h"       /* 渐变停止点（背景 Linear/Radial/Conical 分派）。 */
#include "XPixmap.h"         /* 纹理句柄 → XImage（背景纹理平铺）。 */
#include "XPainter.h"        /* XPainter_save/restore、drawTiledPixmap。 */
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"    /* XWheelEvent：滚轮事件承载；XEnterEvent：悬停位置。 */
#endif
#include <math.h>

/* ==================== 内部常量与辅助 ==================== */

/* ==================== 框架事件槽前置声明（class_init 挂表用） ========== */
static void VX_gview_paintEvent(XWidget* self, XEvent* event);
static void VX_gview_resizeEvent(XWidget* self, XEvent* event);
static void VX_gview_showEvent(XWidget* self, XEvent* event);
static void VX_gview_scrollContentsBy(XAbstractScrollArea* self, int dx,
                                      int dy);
static bool VX_gview_viewportEvent(XAbstractScrollArea* self, XEvent* event);
static void VX_gview_setupViewport(XAbstractScrollArea* self,
                                   XWidget* viewport);
static void VX_gview_deinit(XGraphicsView* self);

/** @brief int 夹取 + 就近取整（对标 q_round_bound，qgraphicsview.cpp:277-286：
 *  越界钳 INT_MIN/INT_MAX，否则**四舍五入远离零**（`d>=0 ? int(d+0.5) :
 *  int(d-1+…)` 等价式）。**不用 rint**——rint 是「就近取偶」（banker's
 *  rounding），.5 边界与 Qt 差 1（qnumeric.h:353-374 qRound =
 *  __builtin_round / `d + copysign(0.5, d)`，同为远离零）。 */
static int gview_roundBound(float v)
{
    double d = (double)v;
    if (d <= -2147483648.0) return (-2147483647 - 1);
    if (d >= 2147483647.0) return 2147483647;
    /* roundf 的语义正是 half-away-from-zero（C99 §7.12.9.6），与 qRound 同。 */
    return (int)roundf(v);
}

/** @brief 就近取整（Qt QPointF::toPoint 同式：qRound——远离零，非取偶）。 */
static int gview_round(float v)
{
    double d = (double)v;
    if (d <= -2147483648.0) return (-2147483647 - 1);
    if (d >= 2147483647.0) return 2147483647;
    return (int)roundf(v);
}

/** @brief 全视口标脏（Qt d->updateAll → viewport()->update()；本框架内容
 *  由滚动区本体自绘（XTextEdit 同款），标脏对象是视图本体）。 */
static void gview_updateAll(XGraphicsView* self)
{
    if (!self) return;
    XWidget_update((XWidget*)self);
}

/** @brief 滚动缓存重读（对标 QGraphicsViewPrivate::updateScroll，
 *  qgraphicsview.cpp:565-580：scrollX=-leftIndent+hbar->value()（LTR），
 *  scrollY=vbar->value()-topIndent）。 */
static void gview_updateScroll(XGraphicsView* self)
{
    if (!self) return;
    self->m_scrollX = -self->m_leftIndent +
                      XScrollBar_value(
                          XAbstractScrollArea_horizontalScrollBar(
                              &self->m_base));
    self->m_scrollY = XScrollBar_value(
                          XAbstractScrollArea_verticalScrollBar(
                              &self->m_base)) -
                      self->m_topIndent;
    self->m_dirtyScroll = false;
}

/** @brief 水平滚动量（dirtyScroll 时先重读；对标 horizontalScroll()）。 */
static int gview_horizontalScroll(XGraphicsView* self)
{
    if (!self) return 0;
    if (self->m_dirtyScroll) gview_updateScroll(self);
    return self->m_scrollX;
}

/** @brief 垂直滚动量（对标 verticalScroll()）。 */
static int gview_verticalScroll(XGraphicsView* self)
{
    if (!self) return 0;
    if (self->m_dirtyScroll) gview_updateScroll(self);
    return self->m_scrollY;
}

/** @brief 当前条厚（水平条高/垂直条宽；与 XAbstractScrollArea 内部
 *  xasa_barExtent 同源——条自身 sizeHint，非法回 16）。 */
static int gview_barExtent(XGraphicsView* self, bool horizontal)
{
    XWidget* bar;
    XSize hint;
    if (!self) return 16;
    bar = (XWidget*)(horizontal
                         ? XAbstractScrollArea_horizontalScrollBar(&self->m_base)
                         : XAbstractScrollArea_verticalScrollBar(&self->m_base));
    if (!bar) return 16;
    hint = XWidget_sizeHint_base(bar);
    if (horizontal) return hint.height > 0 ? hint.height : 16;
    return hint.width > 0 ? hint.width : 16;
}

/* ==================== 滚动范围与缩进（recalculateContentSize） ========== */

/**
 * @brief 重算滚动范围与对齐缩进（对标 QGraphicsViewPrivate::
 *        recalculateContentSize，qgraphicsview.cpp:344-486 逐支）。
 * @details 帧宽 OnlyAround styleHint 本框架无对应（SH_ScrollView_
 *          FrameOnlyAroundContents 未建模，主流样式该 hint=false），略去
 *          对应减法分支；scrollBarExtent=条自身 sizeHint（与框架
 *          xasa_barExtent 同源）。
 */
static void gview_recalculateContentSize(XGraphicsView* self)
{
    XSize maxSize;
    XRectF sceneR;
    XRectF viewRect;
    int width;
    int height;
    bool useHorizontalScrollBar;
    bool useVerticalScrollBar;
    int left;
    int right;
    int top;
    int bottom;
    int oldLeftIndent;
    int oldTopIndent;
    XPointF savedLastCenterPoint;
    XScrollBar* hbar;
    XScrollBar* vbar;
    if (!self) return;
    hbar = XAbstractScrollArea_horizontalScrollBar(&self->m_base);
    vbar = XAbstractScrollArea_verticalScrollBar(&self->m_base);
    if (!hbar || !vbar) return;
    maxSize = XAbstractScrollArea_maximumViewportSize(&self->m_base);
    width = maxSize.width;
    height = maxSize.height;
    sceneR = XGraphicsView_sceneRect(self);
    viewRect = sceneR;
    XGraphicsItem_matrixMapRect(&self->m_matrix, &viewRect, &viewRect);
    /* 条范围更新可能钳动 value → scrollContentsBy → lastCenterPoint 被改，
     * 先存后还（Qt :410-415 注释与 :505-510 的 savedLastCenterPoint 同款）。 */
    savedLastCenterPoint = self->m_lastCenterPoint;

    /* Qt 的 scrollBarExtent = PM_ScrollBarExtent（单条厚度，两轴同值）。
     * 本框架条厚按轴取（竖条宽/横条高，gview_barExtent），AsNeeded 联立
     * 判定与 Qt 同构：竖向放不下时若横向启用则竖条按「再扣横条厚」复判，
     * 反之亦然。 */
    useHorizontalScrollBar =
        (viewRect.width > (float)width) &&
        self->m_base.m_hPolicy == XScrollBarPolicy_AsNeeded;
    useVerticalScrollBar =
        (viewRect.height > (float)height) &&
        self->m_base.m_vPolicy == XScrollBarPolicy_AsNeeded;
    if (useHorizontalScrollBar &&
        self->m_base.m_vPolicy == XScrollBarPolicy_AsNeeded) {
        if (viewRect.height > (float)(height - gview_barExtent(self, true)))
            useVerticalScrollBar = true;
    }
    if (useVerticalScrollBar &&
        self->m_base.m_hPolicy == XScrollBarPolicy_AsNeeded) {
        if (viewRect.width > (float)(width - gview_barExtent(self, false)))
            useHorizontalScrollBar = true;
    }
    if (useHorizontalScrollBar) height -= gview_barExtent(self, true);
    if (useVerticalScrollBar) width -= gview_barExtent(self, false);

    oldLeftIndent = self->m_leftIndent;
    oldTopIndent = self->m_topIndent;

    /* 水平：整景放得下 ⇒ 按 alignment 定缩进 + 条 range(0,0)；放不下 ⇒
     * range=[round(left), round(right-width)]、pageStep=width、
     * singleStep=width/20（Qt :448-475）。 */
    left = gview_roundBound(viewRect.x);
    right = gview_roundBound(viewRect.x + viewRect.width - (float)width);
    if (left >= right) {
        if (self->m_alignment & (int)XAlignment_Left)
            self->m_leftIndent = gview_roundBound(-viewRect.x);
        else if (self->m_alignment & (int)XAlignment_Right)
            self->m_leftIndent =
                gview_roundBound((float)maxSize.width - viewRect.width -
                                 viewRect.x - 1.0f);
        else
            self->m_leftIndent =
                gview_roundBound((float)maxSize.width / 2.0f -
                                 (viewRect.x * 2.0f + viewRect.width) / 2.0f);
        XScrollBar_setRange(hbar, 0, 0);
    } else {
        self->m_leftIndent = 0;
        XScrollBar_setRange(hbar, left, right);
        XScrollBar_setPageStep(hbar, width);
        XScrollBar_setSingleStep(hbar, width / 20);
        if (oldLeftIndent != 0) XScrollBar_setValue(hbar, -oldLeftIndent);
    }

    /* 垂直：同构（Qt :477-504）。 */
    top = gview_roundBound(viewRect.y);
    bottom = gview_roundBound(viewRect.y + viewRect.height - (float)height);
    if (top >= bottom) {
        if (self->m_alignment & (int)XAlignment_Top)
            self->m_topIndent = gview_roundBound(-viewRect.y);
        else if (self->m_alignment & (int)XAlignment_Bottom)
            self->m_topIndent =
                gview_roundBound((float)maxSize.height - viewRect.height -
                                 viewRect.y - 1.0f);
        else
            self->m_topIndent =
                gview_roundBound((float)maxSize.height / 2.0f -
                                 (viewRect.y * 2.0f + viewRect.height) / 2.0f);
        XScrollBar_setRange(vbar, 0, 0);
    } else {
        self->m_topIndent = 0;
        XScrollBar_setRange(vbar, top, bottom);
        XScrollBar_setPageStep(vbar, height);
        XScrollBar_setSingleStep(vbar, height / 20);
        if (oldTopIndent != 0) XScrollBar_setValue(vbar, -oldTopIndent);
    }

    /* 还原中心点（范围变更期间的钳位不污染；Qt :505-510）。 */
    self->m_lastCenterPoint = savedLastCenterPoint;

    /* 缩进变化 ⇒ 全量重绘 + 滚动缓存失效（Qt :516-521 的 dirtyScroll/
     * updateAll 同口径；RTL 分支本框架无 RightToLeft 模式，不登）。 */
    if (oldLeftIndent != self->m_leftIndent ||
        oldTopIndent != self->m_topIndent) {
        self->m_dirtyScroll = true;
        gview_updateAll(self);
    } else {
        self->m_dirtyScroll = true;
    }
}

/* ==================== 坐标映射内部核（浮点） ==================== */

/** @brief 视口浮点坐标 → 场景（对标 mapToScene(QPoint) 的取整前口径：
 *  p += scroll 后 matrix.inverted().map）。
 *  @note self==NULL 按**恒等映射**决算（无视图=无视口变换/无滚动）——
 *  与头注「可为 NULL（原样返回）」一致；**必须写 out**，不得留未初始化
 *  栈值（AGENTS.md 未初始化局部量铁律，MSVC /RTC1 会触发运行期检查）。 */
static void gview_mapToSceneF(const XGraphicsView* self, float vx, float vy,
                              XPointF* out)
{
    XImageTransform inv;
    if (!out) return;
    if (!self) {
        out->x = vx;
        out->y = vy;
        return;
    }
    vx += (float)gview_horizontalScroll((XGraphicsView*)self);
    vy += (float)gview_verticalScroll((XGraphicsView*)self);
    if (self->m_identityMatrix) {
        out->x = vx;
        out->y = vy;
        return;
    }
    if (!XGraphicsItem_matrixInvert(&self->m_matrix, &inv)) {
        /* 奇异阵：Qt matrix.inverted() 失败回单位阵口径。 */
        XGraphicsItem_matrixIdentity(&inv);
    }
    XGraphicsItem_matrixMapPoint(&inv, vx, vy, out);
}

/** @brief 场景坐标 → 视口浮点坐标（对标 mapFromScene 的取整前口径）。
 *  @note self==NULL 同按恒等映射决算并写 out（理由同上）。 */
static void gview_mapFromSceneF(const XGraphicsView* self, float sx, float sy,
                                XPointF* out)
{
    if (!out) return;
    if (!self) {
        out->x = sx;
        out->y = sy;
        return;
    }
    if (self->m_identityMatrix) {
        out->x = sx - (float)gview_horizontalScroll((XGraphicsView*)self);
        out->y = sy - (float)gview_verticalScroll((XGraphicsView*)self);
        return;
    }
    XGraphicsItem_matrixMapPoint(&self->m_matrix, sx, sy, out);
    out->x -= (float)gview_horizontalScroll((XGraphicsView*)self);
    out->y -= (float)gview_verticalScroll((XGraphicsView*)self);
}

/* ==================== 鼠标跟踪（AnchorUnderMouse 的数据承载） ========== */

/**
 * @brief 记录最近鼠标位置：视口坐标与场景点**成对**写入。
 * @details 对标 QGraphicsViewPrivate 的两项承载——`lastMouseEvent`（`position`
 *          取其坐标；qgraphicsview_p.h:92）与 `lastMouseMoveScenePoint`
 *          （:78）：
 *          centerView 的 AnchorUnderMouse 分支（qgraphicsview.cpp:483-488）
 *          用「当前鼠标的视口位置」与「变换前鼠标下的场景点」做差分，
 *          两者必须是**同一次鼠标事件**的快照，否则差分公式自洽性破裂
 *          （Qt 的当前鼠标位置来自 QCursor::pos() 实时查询，本框架无可靠
 *          的实时光标入口，故以成对记录替代；见 m_lastMouseScenePoint 注）。
 * @param self 目标视图；NULL 无操作。
 * @param vpPos 鼠标的视口坐标；NULL 无操作。
 * @note 必须在**变换未变**时调用（记录的是当时的场景点）。
 */
static void gview_recordMousePos(XGraphicsView* self, const XPoint* vpPos)
{
    if (!self || !vpPos) return;
    self->m_lastMouseViewport = *vpPos;
    gview_mapToSceneF(self, (float)vpPos->x, (float)vpPos->y,
                      &self->m_lastMouseScenePoint);
}

/**
 * @brief 鼠标是否在视图上（Qt centerView 的 `q->underMouse()`，:483）。
 * @details 两个来源取并集：
 *          ① XWidget_underMouse：框架的悬停位属性（窗口级 ENTER/LEAVE
 *             冒泡维护，XWidget.c:10303；等价 Qt
 *             QApplicationPrivate::notify_helper 的 WA_UnderMouse 翻转，
 *             qapplication.cpp:3275-3282）；
 *          ② m_mouseInside：视口 ENTER/LEAVE 直接跟踪位（滚动区过滤器把
 *             视口事件先送 ViewportEvent，见 XAbstractScrollArea.c:32-64）。
 *             无窗口的离屏/嵌入式用法与合成事件测试只有这条路径能拿到
 *             悬停态，缺它则 AnchorUnderMouse 永远回落成 AnchorViewCenter。
 * @param self 目标视图；NULL 返回 false。
 * @return 鼠标在视图上返回 true。
 */
static bool gview_isMouseUnderView(const XGraphicsView* self)
{
    if (!self) return false;
    if (self->m_mouseInside) return true;
    return XWidget_underMouse((const XWidget*)self);
}

/* ==================== 背景（drawBackground 的样式分派） ==================== */

/** @brief 正模（瓦片网格锚点算术：负值回绕到 [0, m)）。 */
static int gview_positiveMod(int v, int m)
{
    int r;
    if (m <= 0) return 0;
    r = v % m;
    if (r < 0) r += m;
    return r;
}

/**
 * @brief 纹理平铺（XImage 纹理槽专用）：按 tile 尺寸循环 XPainter_drawImage。
 * @details Qt 侧 fillRect(rect, textureBrush) 的图案以**笔刷原点**（缺省 =
 *          逻辑原点 (0,0)）为网格起点、随 painter 变换落位。本函数把网格
 *          锚定逻辑原点：start = rect.tl − (rect.tl mod tile) ⇒ 滚动/平移
 *          使暴露矩形变化时纹理相位不漂移（若按暴露矩形左上角锚定，滚动
 *          时纹理会「爬」）。dpr>1 的图像按逻辑尺寸（物理/dpr）取 tile，
 *          与 XPainter_drawTiledPixmap 的 tileWidth 口径一致
 *          （XPainter.c:9568-9575）。
 * @param painter 绘制器；NULL 无操作。
 * @param image 纹理图像；NULL/零尺寸无操作。
 * @param rect 目标矩形（painter 逻辑坐标）；NULL 无操作。
 * @note 边缘 tile 不按 rect 裁剪——背景先于项绘制，且 painter 已有视口/
 *       目标裁剪，越界部分被光栅裁剪丢弃（不会覆盖任何项）。
 * @note 性能口径：代价 O(暴露面积 / tile 面积) 次贴图；tile 越碎代价越高，
 *       大暴露区建议用 XPixmap 纹理（走框架平铺原语）或增大 tile。
 */
static void gview_tileTexture(XPainter* painter, const XImage* image,
                              const XRect* rect)
{
    int tileW;
    int tileH;
    int startX;
    int startY;
    int x;
    int y;
    float dpr;
    if (!painter || !image || !rect) return;
    if (rect->width <= 0 || rect->height <= 0) return;
    tileW = XImage_width(image);
    tileH = XImage_height(image);
    if (tileW <= 0 || tileH <= 0) return;
    dpr = XImage_devicePixelRatio(image);
    if (!(dpr > 0.0f) || !isfinite((double)dpr)) dpr = 1.0f;
    if (dpr != 1.0f) {
        tileW = gview_roundBound((float)tileW / dpr);
        tileH = gview_roundBound((float)tileH / dpr);
    }
    if (tileW <= 0 || tileH <= 0) return;
    startX = rect->x - gview_positiveMod(rect->x, tileW);
    startY = rect->y - gview_positiveMod(rect->y, tileH);
    for (y = startY; y < rect->y + rect->height; y += tileH) {
        for (x = startX; x < rect->x + rect->width; x += tileW)
            XPainter_drawImage(painter, image, x, y);
    }
}

/**
 * @brief 背景纹理填充（XBrushStyle_TexturePattern）。
 * @details 优先走框架平铺原语 XPainter_drawTiledPixmap（= Qt
 *          QPainter::drawTiledPixmap，Qt 的纹理笔刷在光栅引擎里就是这张
 *          平铺图）；该开关被裁剪或原语失败时退回「XPixmap→XImage
 *          （引用计数浅拷贝，XPixmap.c:756-768）+ 循环 drawImage」同口径
 *          路径——不留「不支持就跳过」分支。只有纹理图像槽
 *          （XBrush_setTextureImage 的 XImage 借用槽，无平铺原语）时直接走
 *          循环 drawImage。
 * @param self 目标视图（读 m_backgroundBrush）；NULL 无操作。
 * @param painter 绘制器；NULL 无操作。
 * @param r 目标矩形（painter 逻辑坐标）；NULL 无操作。
 */
static void gview_fillTexture(XGraphicsView* self, XPainter* painter,
                              const XRect* r)
{
    const XPixmap* pixmap;
    const XImage* image;
    if (!self || !painter || !r) return;
    pixmap = XBrush_texture(&self->m_backgroundBrush);
    image = XBrush_textureImage(&self->m_backgroundBrush);
    if (pixmap) {
#if XPAINTER_TILED_PIXMAP_ON
        /* offset 取 rect.tl：drawTiledPixmap 对 offset 取模后从源偏置贴
         * 第一块（XPainter.c:9576-9616）⇒ 网格反推回逻辑原点。 */
        XPoint offset;
        offset.x = r->x;
        offset.y = r->y;
        if (XPainter_drawTiledPixmap(painter, r, pixmap, &offset)) return;
#endif /* XPAINTER_TILED_PIXMAP_ON */
        {
            XImage converted;
            XImage_init(&converted);
            XPixmap_toImage(pixmap, &converted);
            gview_tileTexture(painter, &converted, r);
            XClassDeinit((XClass*)&converted);
            return;
        }
    }
    if (image) gview_tileTexture(painter, image, r);
}

/**
 * @brief 画背景（对标 QGraphicsView::drawBackground，qgraphicsview.cpp:
 *        3709-3723）。
 * @details Qt 原文尾段就是 `painter->fillRect(rect, d->backgroundBrush)`
 *          ——纯色/渐变/标准图案/纹理全部由 QPainter 的笔刷机制承担。本框架
 *          按笔刷样式分派到同一效果的既有原语（**不告警跳过任何样式**）：
 *          - NoBrush：不画。Qt 此时回落 scene->drawBackground，本框架场景层
 *            无笔刷（W2）⇒ 无底可铺（与既有口径一致）。
 *          - SolidPattern：纯色填充（旧行为逐位不变，不触碰 painter 画刷
 *            状态）。
 *          - Linear/Radial/Conical + Dense1..DiagCross：复用框架既有画刷
 *            应用桥接 XBrush_applyToPainter（XGradient→XPainterGradient，
 *            几何按目标矩形——XGradient 基类不带几何）+ XPainter_fillRect_2
 *            按样式分派：渐变走 painterFillPolygonShape 的渐变光栅，标准
 *            图案走 painterPatternBit 的 Qt 8×8 位表（qt_patternForBrush
 *            同源）。渐变样式缺渐变载荷时 applyToPainter 回落纯色（Qt 的
 *            QBrush(渐变样式) 无载荷同样退化为纯色填充）。
 *          - TexturePattern：gview_fillTexture（平铺，见上）。
 *          渐变/图案分支把画刷写进 painter，包 save/restore ⇒ 后续
 *          XGraphicsScene_render 的项绘制不受污染（项若用 fillRect 显式
 *          颜色，XPainter 会套用当前图案样式，必须隔离）。
 * @param self 目标视图；NULL 无操作。
 * @param painter 绘制器；NULL 无操作。
 * @param sceneRect 暴露场景矩形；NULL 无操作。
 */
static void gview_drawBackground(XGraphicsView* self, XPainter* painter,
                                 const XRectF* sceneRect)
{
    XBrushStyle style;
    XColor c;
    XRect r;
    if (!self || !painter || !sceneRect) return;
    style = XBrush_style(&self->m_backgroundBrush);
    if (style == XBrushStyle_NoBrush) return;
    /* 场景坐标 → 整数填充矩形（Qt fillRect(QRectF) 的四舍五入口径；
     * 旧实现同式，保持不变）。 */
    r.x = gview_roundBound(sceneRect->x);
    r.y = gview_roundBound(sceneRect->y);
    r.width = gview_roundBound(sceneRect->width);
    r.height = gview_roundBound(sceneRect->height);
    if (r.width <= 0 || r.height <= 0) return;
    if (style == XBrushStyle_TexturePattern) {
        gview_fillTexture(self, painter, &r);
        return;
    }
    if (style == XBrushStyle_SolidPattern) {
        c = XBrush_color(&self->m_backgroundBrush);
        XPainter_fillRect(painter, &r, XColor_rgba(&c));
        return;
    }
    if (!XPainter_save(painter)) return;
    XBrush_applyToPainter(&self->m_backgroundBrush, painter, &r);
    XPainter_fillRect_2(painter, &r);
    XPainter_restore(painter);
}

/* ==================== 拖动与场景事件（视口坐标入口） ==================== */

/** @brief 把视口鼠标事件复制成场景坐标事件（对标 mousePress/Move/Release
 *  里构造 QGraphicsSceneMouseEvent 的一支：**事件类型换成场景域**
 *  XEVENT_TYPE_GRAPHICS_SCENE_*——Qt 的 QGraphicsSceneMouseEvent(QEvent::
 *  GraphicsSceneMousePress) 与视口 QMouseEvent 是两个事件；坐标换场景，
 *  载荷复用 XMouseEvent）。 */
static void gview_sceneMouseEvent(XGraphicsView* self, const XMouseEvent* src,
                                  const XPointF* scenePos, int sceneType,
                                  XMouseEvent* out)
{
    *out = *src;
    out->m_class.m_type = sceneType;
    out->m_position.x = gview_round(scenePos->x);
    out->m_position.y = gview_round(scenePos->y);
    (void)self;
}

/** @brief 鼠标按下（对标 QGraphicsView::mousePressEvent，
 *  qgraphicsview.cpp:3192-3277 逐支：先存事件→交互允许时派场景→
 *  场景收下即返回→RubberBand 起带（Ctrl=追加选、否则先清选）→
 *  ScrollHandDrag 左键起手拖）。返回=视口事件是否被消费。 */
static bool gview_mousePressEvent(XGraphicsView* self, XMouseEvent* event)
{
    XPointF scenePos;
    bool consumed = true;
    if (!self || !event) return false;
    gview_recordMousePos(self, &event->m_position);
    self->m_rubberBandStart = event->m_position;
    XEvent_setAccepted_base((XEvent*)event, false);
    gview_mapToSceneF(self, (float)event->m_position.x,
                      (float)event->m_position.y, &scenePos);
    if (self->m_interactive && self->m_scene) {
        XMouseEvent copy;
        gview_sceneMouseEvent(self, event, &scenePos,
                              XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_PRESS, &copy);
        XEvent_setAccepted_base((XEvent*)&copy, false);
        XGraphicsScene_mousePressEvent(self->m_scene, &copy);
        /* 记录最近一次场景派发的接受态（对标 d->lastMouseEvent.setAccepted，
         * Qt :3260-3261）——手拖 release 的「点击清选」守卫消费。 */
        self->m_lastEventAccepted = XEvent_isAccepted((XEvent*)&copy);
        if (self->m_lastEventAccepted) {
            XEvent_setAccepted_base((XEvent*)event, true);
            return consumed;
        }
    }
    if (self->m_dragMode == XGraphicsViewDragMode_RubberBandDrag &&
        !self->m_rubberBanding) {
        if (self->m_interactive) {
            XEvent_setAccepted_base((XEvent*)event, true);
            self->m_rubberBanding = true;
            XRect_init(&self->m_rubberBandRect, 0, 0, 0, 0);
            if (self->m_scene) {
                /* Ctrl=追加选（本轮场景层 AddToSelection 按替换承载——
                 * W2 已登记的口径），否则先清选（Qt 同）。 */
                bool extend = (event->m_modifiers &
                               (XKeyboardModifiers)
                                   XKeyboardModifier_ControlModifier) != 0;
                if (!extend) XGraphicsScene_clearSelection(self->m_scene);
            }
        }
    } else if (self->m_dragMode == XGraphicsViewDragMode_ScrollHandDrag &&
               event->m_button == XMouseButton_LeftButton) {
        XEvent_setAccepted_base((XEvent*)event, true);
        self->m_handScrolling = true;
        self->m_handScrollMotions = 0;
    }
    return consumed;
}

/** @brief 鼠标移动（对标 mouseMoveEvent，qgraphicsview.cpp:3279-3301 +
 *  mouseMoveEventHandler：手型拖拽调条 → 橡皮筋更新+实时选区 → 场景
 *  move 派发）。返回=视口事件是否被消费。 */
static bool gview_mouseMoveEvent(XGraphicsView* self, XMouseEvent* event)
{
    XPointF scenePos;
    if (!self || !event) return false;
    /* 手型拖拽：条值随帧间差分反向移动（Qt :3286-3294）。 */
    if (self->m_handScrolling &&
        self->m_dragMode == XGraphicsViewDragMode_ScrollHandDrag) {
        XScrollBar* hbar =
            XAbstractScrollArea_horizontalScrollBar(&self->m_base);
        XScrollBar* vbar =
            XAbstractScrollArea_verticalScrollBar(&self->m_base);
        int dx = event->m_position.x - self->m_lastMouseViewport.x;
        int dy = event->m_position.y - self->m_lastMouseViewport.y;
        if (hbar) XScrollBar_setValue(hbar, XScrollBar_value(hbar) - dx);
        if (vbar) XScrollBar_setValue(vbar, XScrollBar_value(vbar) - dy);
        ++self->m_handScrollMotions;
    }
    /* 与场景点成对记录（同时写 m_lastMouseViewport；AnchorUnderMouse 的
     * centerView 消费）。放在手拖分支之后：手拖已改条值，此时重算的场景点
     * 与**当前**视图状态一致。 */
    gview_recordMousePos(self, &event->m_position);
    gview_mapToSceneF(self, (float)event->m_position.x,
                      (float)event->m_position.y, &scenePos);
    /* 橡皮筋：更新矩形 + 实时把带内场景矩形设为选区（Qt updateRubberBand
     * :750-776：addPolygon(mapToScene(band)) 后 setSelectionArea）。 */
    if (self->m_rubberBanding) {
        int x1 = self->m_rubberBandStart.x;
        int y1 = self->m_rubberBandStart.y;
        int x2 = event->m_position.x;
        int y2 = event->m_position.y;
        XRect band;
        XRect_init(&band, x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2,
                   x1 < x2 ? x2 - x1 : x1 - x2, y1 < y2 ? y2 - y1 : y1 - y2);
        if (band.width != self->m_rubberBandRect.width ||
            band.height != self->m_rubberBandRect.height ||
            band.x != self->m_rubberBandRect.x ||
            band.y != self->m_rubberBandRect.y) {
            self->m_rubberBandRect = band;
            gview_updateAll(self);
        }
        if (self->m_scene && self->m_rubberBandRect.width > 0 &&
            self->m_rubberBandRect.height > 0) {
            XRectF bandScene = XGraphicsView_mapToSceneRect(
                self, &self->m_rubberBandRect);
            XGraphicsScene_setSelectionAreaRect(self->m_scene, &bandScene);
        }
    }
    /* 场景 move 派发（手型拖拽中 Qt 提前返回——mouseMoveEventHandler 的
     * `if (handScrolling) return;`）。 */
    if (self->m_interactive && self->m_scene && !self->m_handScrolling) {
        XMouseEvent copy;
        gview_sceneMouseEvent(self, event, &scenePos,
                              XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_MOVE, &copy);
        XGraphicsScene_mouseMoveEvent(self->m_scene, &copy);
        /* 同 press：记录最近场景事件的接受态（Qt mouseMoveEventHandler
         * 尾段 d->lastMouseEvent.setAccepted）。 */
        self->m_lastEventAccepted = XEvent_isAccepted((XEvent*)&copy);
    }
    return true;
}

/** @brief 鼠标松开（对标 mouseReleaseEvent，qgraphicsview.cpp:3303-3363：
 *  橡皮筋收带 / 手型松手（微动判点击清选）→ 场景 release 派发）。
 *  返回=视口事件是否被消费。 */
static bool gview_mouseReleaseEvent(XGraphicsView* self, XMouseEvent* event)
{
    XPointF scenePos;
    if (!self || !event) return false;
    /* 成对记录（含 m_lastMouseViewport）——release 位置也必须是锚点可用的
     * 一致快照（Qt 的当前鼠标位置取 QCursor::pos() 实时值，本框架无该入口
     * ⇒ 任何改变「最近鼠标位置」的路径都要同拍写场景点）。 */
    gview_recordMousePos(self, &event->m_position);
    gview_mapToSceneF(self, (float)event->m_position.x,
                      (float)event->m_position.y, &scenePos);
    if (self->m_dragMode == XGraphicsViewDragMode_RubberBandDrag &&
        self->m_interactive && event->m_buttons == XMouseButton_NoButton) {
        self->m_rubberBanding = false;
        XRect_init(&self->m_rubberBandRect, 0, 0, 0, 0);
        gview_updateAll(self);
    } else if (self->m_dragMode == XGraphicsViewDragMode_ScrollHandDrag &&
               event->m_button == XMouseButton_LeftButton) {
        self->m_handScrolling = false;
        /* 微动（≤6 次移动）**且最近场景事件未被接受** ⇒ 判点击、清选
         * （Qt :3318-3323：`!d->lastMouseEvent.isAccepted() &&
         * d->handScrollMotions <= 6`）。【复核根修 2026-11-16】此前缺
         * accepted 守卫——场景项收过 move（拖动中）也会被清选，
         * m_lastEventAccepted 即该守卫的承载（Qt 存整份 lastMouseEvent，
         * 本框架只留判定位）。 */
        if (self->m_scene && self->m_interactive &&
            !self->m_lastEventAccepted && self->m_handScrollMotions <= 6) {
            XGraphicsScene_clearSelection(self->m_scene);
        }
    }
    /* 场景 release 派发（Qt :3331-3356 无模式分支、恒派发）。 */
    if (self->m_interactive && self->m_scene) {
        XMouseEvent copy;
        gview_sceneMouseEvent(self, event, &scenePos,
                              XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_RELEASE, &copy);
        XEvent_setAccepted_base((XEvent*)&copy, false);
        XGraphicsScene_mouseReleaseEvent(self->m_scene, &copy);
        self->m_lastEventAccepted = XEvent_isAccepted((XEvent*)&copy);
    }
    return true;
}

#if XWINDOWEVENT_ON
/** @brief 滚轮（对标 wheelEvent，qgraphicsview.cpp:3372-3402：无修饰键
 *  分支——场景优先（GraphicsSceneWheel），场景不收回落 QAbstractScrollArea
 *  滚条步进。任务书的 Ctrl 缩放档不是 Qt 行为，见头注 ②）。
 *  返回=视口事件是否被消费。 */
static bool gview_wheelEvent(XGraphicsView* self, XWheelEvent* event)
{
    if (!self || !event) return false;
    if (!self->m_scene || !self->m_interactive) {
        /* Qt :3375-3377：无场景/非交互直接走 QAbstractScrollArea。 */
        XClass_Parent(XAbstractScrollArea, EXWidget_WheelEvent,
                      XWidgetEventSlot)((XWidget*)&self->m_base,
                                        (XEvent*)event);
        return true;
    }
    {
        XWheelEvent copy = *event;
        XPointF scenePos;
        gview_mapToSceneF(self, (float)event->m_position.x,
                          (float)event->m_position.y, &scenePos);
        /* 场景域事件类型（Qt 的 QGraphicsSceneWheelEvent(QEvent::
         * GraphicsSceneWheel)——与视口 QWheelEvent 是两个事件）。 */
        copy.m_class.m_type = XEVENT_TYPE_GRAPHICS_SCENE_WHEEL;
        copy.m_position.x = gview_round(scenePos.x);
        copy.m_position.y = gview_round(scenePos.y);
        XEvent_setAccepted_base((XEvent*)&copy, false);
        XGraphicsScene_wheelEvent(self->m_scene, &copy);
        XEvent_setAccepted_base((XEvent*)event,
                                XEvent_isAccepted((XEvent*)&copy));
        if (!XEvent_isAccepted((XEvent*)event)) {
            XClass_Parent(XAbstractScrollArea, EXWidget_WheelEvent,
                          XWidgetEventSlot)((XWidget*)&self->m_base,
                                            (XEvent*)event);
        }
    }
    return true;
}
#endif /* XWINDOWEVENT_ON */

/** @brief 视口事件入口（ViewportEvent 保护槽覆写；框架的滚动区事件模型：
 *  视口收到的每个事件先经本入口——与 Qt 的 QAbstractScrollAreaFilter 重
 *  映射同构。鼠标/滚轮/悬停在此处理；其余回落基类缺省（恒 false 不消费，
 *  XAbstractScrollArea_Protected.h 的契约）。
 *  @note ENTER/LEAVE 只**记录**悬停态与鼠标位置，返回 false 让事件继续走
 *  视口常规路径（视口自身与父链照常收 ENTER/LEAVE：控件悬停位、
 *  工具提示、悬停样式刷新都由框架缺省路径维护，不能在这里吞掉）。 */
static bool VX_gview_viewportEvent(XAbstractScrollArea* self, XEvent* event)
{
    XGraphicsView* view;
    if (!self || !event) return false;
    view = (XGraphicsView*)self;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
        return gview_mousePressEvent(
            view, (XMouseEvent*)event);
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        return gview_mouseReleaseEvent(view, (XMouseEvent*)event);
    case XEVENT_TYPE_MOUSE_MOVE:
        return gview_mouseMoveEvent(view, (XMouseEvent*)event);
#if XWINDOWEVENT_ON
    case XEVENT_TYPE_WHEEL:
        return gview_wheelEvent(view, (XWheelEvent*)event);
    /* 悬停跟踪（AnchorUnderMouse 的鼠标位置来源；Qt 用
     * QCursor::pos() 实时查询，本框架以最近的 ENTER/MOVE 位置承载）。 */
    case XEVENT_TYPE_ENTER: {
        const XEnterEvent* ee = (const XEnterEvent*)event;
        view->m_mouseInside = true;
        gview_recordMousePos(view, &ee->m_position);
        return false;
    }
    case XEVENT_TYPE_LEAVE:
        view->m_mouseInside = false;
        return false;
#endif /* XWINDOWEVENT_ON */
    default:
        return XClass_Parent(XAbstractScrollArea,
                             EXAbstractScrollArea_ViewportEvent,
                             bool (*)(XAbstractScrollArea*, XEvent*))(
            self, event);
    }
}

/**
 * @brief 视口换装钩子（对标 QGraphicsView::setupViewport 的鼠标跟踪支，
 *        qgraphicsview.cpp:2718-2750 中的 :2743-2749）。
 * @details Qt 原文：`if ((scene && …) || d->transformationAnchor ==
 *          AnchorUnderMouse || d->resizeAnchor == AnchorUnderMouse)
 *          widget->setMouseTracking(true);`——换装出的**新视口**不带旧视口
 *          的跟踪位，不补则 AnchorUnderMouse 在悬停期收不到移动事件（锚点
 *          位置会停在最后一次按键拖动处）。
 * @note Qt 同函数里的 focusPolicy/autoFillBackground/触摸/手势各支本子集
 *       不建模（框架视口排布与输入模型见 XAbstractScrollArea.c）；
 *       基类口径经 XClass_Parent 回落（本类不吞基类行为）。
 * @param self 目标滚动区（XGraphicsView）；NULL 无操作。
 * @param viewport 新视口（借用）；NULL 无操作。
 */
static void VX_gview_setupViewport(XAbstractScrollArea* self,
                                   XWidget* viewport)
{
    XGraphicsView* view;
    if (!self || !viewport) return;
    view = (XGraphicsView*)self;
    XClass_Parent(XAbstractScrollArea, EXAbstractScrollArea_SetupViewport,
                  void (*)(XAbstractScrollArea*, XWidget*))(self, viewport);
    if (view->m_transformationAnchor ==
            (uint8_t)XGraphicsViewViewportAnchor_AnchorUnderMouse ||
        view->m_resizeAnchor ==
            (uint8_t)XGraphicsViewViewportAnchor_AnchorUnderMouse)
        XWidget_setMouseTracking(viewport, true);
}

/* ==================== 变换后的重居中 ==================== */

/**
 * @brief 按锚点重居中（对标 QGraphicsViewPrivate::centerView，
 *        qgraphicsview.cpp:478-500 逐支）。
 * @details 三分支的唯一事实源是 Qt 原文：
 *          - AnchorUnderMouse（:482-491）：
 *              if (q->underMouse()) {
 *                  QPointF transformationDiff =
 *                      mapToScene(viewport->rect().center())
 *                      - mapToScene(viewport->mapFromGlobal(QCursor::pos()));
 *                  q->centerOn(lastMouseMoveScenePoint + transformationDiff);
 *              } else {
 *                  q->centerOn(lastCenterPoint);
 *              }
 *            两个 mapToScene 都在**新矩阵**下求值（matrix 已在 setTransform
 *            里赋值，centerView 在其后调用），lastMouseMoveScenePoint 是
 *            **旧矩阵**下记录的鼠标处场景点 ⇒ 代进 centerOn 后
 *            `mapToScene(鼠标视口位置) == lastMouseMoveScenePoint`：缩放前后
 *            鼠标下的场景点不动。本框架的「当前鼠标视口位置」取最近鼠标
 *            事件的成对快照（无 QCursor 查询入口，见 gview_recordMousePos）。
 *            underMouse 判定见 gview_isMouseUnderView；为假时与
 *            AnchorViewCenter 同路径（Qt 同）。
 *          - AnchorViewCenter：centerOn(lastCenterPoint)。
 *          - NoAnchor：不动。
 * @param self 目标视图；NULL 无操作。
 * @param anchor 锚点枚举。
 */
static void gview_centerView(XGraphicsView* self,
                             XGraphicsViewViewportAnchor anchor)
{
    if (!self) return;
    switch (anchor) {
    case XGraphicsViewViewportAnchor_AnchorUnderMouse: {
        XWidget* viewport;
        float width;
        float height;
        if (gview_isMouseUnderView(self)) {
            XPointF centerScene;
            XPointF mouseScene;
            XPointF target;
            viewport = XAbstractScrollArea_viewport(&self->m_base);
            width = viewport ? (float)XWidget_width(viewport) : 0.0f;
            height = viewport ? (float)XWidget_height(viewport) : 0.0f;
            gview_mapToSceneF(self, width / 2.0f, height / 2.0f,
                              &centerScene);
            gview_mapToSceneF(self, (float)self->m_lastMouseViewport.x,
                              (float)self->m_lastMouseViewport.y, &mouseScene);
            /* lastMouseMoveScenePoint + (中心场景点 − 鼠标处场景点)。 */
            target.x = self->m_lastMouseScenePoint.x +
                       (centerScene.x - mouseScene.x);
            target.y = self->m_lastMouseScenePoint.y +
                       (centerScene.y - mouseScene.y);
            XGraphicsView_centerOn(self, &target);
        } else {
            /* 无鼠标事件/已离开视图 ⇒ 回落中心锚（Qt 同）。 */
            XGraphicsView_centerOn(self, &self->m_lastCenterPoint);
        }
        break;
    }
    case XGraphicsViewViewportAnchor_AnchorViewCenter:
    default:
        XGraphicsView_centerOn(self, &self->m_lastCenterPoint);
        break;
    case XGraphicsViewViewportAnchor_NoAnchor:
        break;
    }
}

/* ==================== 生命周期与虚表 ==================== */

XVtable* XGraphicsView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsView)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_gview_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_gview_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VX_gview_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy,
                             VX_gview_scrollContentsBy);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ViewportEvent,
                             VX_gview_viewportEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_SetupViewport,
                             VX_gview_setupViewport);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_gview_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief Deinit：场景是借用回指（不拥有、不置 NULL 的必要只在防悬挂
 *  访问——本类 deinit 后无成员访问路径，保留原值无害；Qt 的
 *  ~QGraphicsView 同样不通知场景）。回落父类清尾。 */
static void VX_gview_deinit(XGraphicsView* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractScrollArea,
                         (XAbstractScrollArea*)self);
}

void XGraphicsView_init(XGraphicsView* self, XWidget* parent,
                        XWidgetFlags flags)
{
    if (!self) return;
    /* AGENTS.md 铁律：init 全字段显式清零（MSVC /RTC1 栈垃圾防线）。 */
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XGraphicsView);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XGraphicsItem_matrixIdentity(&self->m_matrix);
    self->m_identityMatrix = true;
    self->m_alignment =
        (int)XAlignment_HCenter | (int)XAlignment_VCenter;
    self->m_dragMode = (uint8_t)XGraphicsViewDragMode_NoDrag;
    self->m_transformationAnchor =
        (uint8_t)XGraphicsViewViewportAnchor_AnchorViewCenter;
    self->m_resizeAnchor =
        (uint8_t)XGraphicsViewViewportAnchor_AnchorViewCenter;
    self->m_interactive = true;
    self->m_dirtyScroll = true;
    /* 新增字段显式清零（XMemset 已清，这里逐字段点名守 MSVC /RTC1 与
     * 「新增字段必须进 init」的可读性）。 */
    self->m_lastEventAccepted = false;
    self->m_keepLastCenterPoint = false;
    self->m_rubberBandStart.x = 0;
    self->m_rubberBandStart.y = 0;
    /* 鼠标跟踪两项（AnchorUnderMouse 的成对快照）显式清零。 */
    self->m_lastMouseScenePoint.x = 0.0f;
    self->m_lastMouseScenePoint.y = 0.0f;
    self->m_mouseInside = false;
    XBrush_init(&self->m_backgroundBrush); /* NoBrush=未设背景。 */
    /* 场景矩形未设（m_hasSceneRect=false）；对齐缩进 0；无带、无手拖。 */
}

XGraphicsView* XGraphicsView_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags)
{
    XGraphicsView* self = (XGraphicsView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGraphicsView_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 场景关联 ==================== */

void XGraphicsView_setScene(XGraphicsView* self, XGraphicsScene* scene)
{
    if (!self) return;
    if (self->m_scene == scene) return; /* 同场景早退（Qt 同）。 */
    /* 换场全视口标脏（Qt :1721 d->updateAll()）。 */
    gview_updateAll(self);
    self->m_scene = scene;
    /* 重算滚动范围；**挂接**新场景时复位中心点并置一次性保留位
     * （Qt :1746-1750：recalculateContentSize、
     * lastCenterPoint=sceneRect().center()、keepLastCenterPoint=true）。
     * 摘离（scene==NULL）只重算范围——Qt :1747-1752 的 else 支不碰
     * lastCenterPoint/keepLastCenterPoint（【复核根修 2026-11-17】此前
     * 摘离也重置，随后 resize/setTransform 的 anchor 重居中行为与 Qt 不同）。 */
    gview_recalculateContentSize(self);
    if (scene) {
        XRectF r = XGraphicsView_sceneRect(self);
        self->m_lastCenterPoint.x = r.x + r.width / 2.0f;
        self->m_lastCenterPoint.y = r.y + r.height / 2.0f;
        self->m_keepLastCenterPoint = true;
    }
    self->m_dirtyScroll = true;
}

XGraphicsScene* XGraphicsView_scene(const XGraphicsView* self)
{
    if (!self) return NULL;
    return self->m_scene;
}

void XGraphicsView_setInteractive(XGraphicsView* self, bool allowed)
{
    if (!self) return;
    self->m_interactive = allowed;
}

bool XGraphicsView_isInteractive(const XGraphicsView* self)
{
    if (!self) return false;
    return self->m_interactive;
}

/* ==================== 场景矩形与对齐 ==================== */

XRectF XGraphicsView_sceneRect(const XGraphicsView* self)
{
    if (!self) {
        XRectF empty;
        empty.x = 0.0f;
        empty.y = 0.0f;
        empty.width = 0.0f;
        empty.height = 0.0f;
        return empty;
    }
    if (self->m_hasSceneRect) return self->m_sceneRect;
    if (self->m_scene) return XGraphicsScene_sceneRect(self->m_scene);
    {
        XRectF empty;
        empty.x = 0.0f;
        empty.y = 0.0f;
        empty.width = 0.0f;
        empty.height = 0.0f;
        return empty;
    }
}

void XGraphicsView_setSceneRect(XGraphicsView* self, const XRectF* rect)
{
    if (!self) return;
    /* 空矩形=清除显式设置（Qt setSceneRect 的 hasSceneRect=!isNull）。 */
    self->m_hasSceneRect = rect && !(rect->width <= 0.0f && rect->height <= 0.0f);
    self->m_sceneRect = rect ? *rect : XGraphicsView_sceneRect(self);
    if (!self->m_hasSceneRect) {
        self->m_sceneRect.x = 0.0f;
        self->m_sceneRect.y = 0.0f;
        self->m_sceneRect.width = 0.0f;
        self->m_sceneRect.height = 0.0f;
    }
    gview_recalculateContentSize(self);
}

void XGraphicsView_setAlignment(XGraphicsView* self, int alignment)
{
    if (!self) return;
    if (self->m_alignment == alignment) return;
    self->m_alignment = alignment;
    gview_recalculateContentSize(self);
}

int XGraphicsView_alignment(const XGraphicsView* self)
{
    if (!self) return 0;
    return self->m_alignment;
}

void XGraphicsView_setBackgroundBrush(XGraphicsView* self, const XBrush* brush)
{
    if (!self) return;
    if (brush)
        self->m_backgroundBrush = *brush;
    else
        XBrush_init(&self->m_backgroundBrush);
    gview_updateAll(self);
}

const XBrush* XGraphicsView_backgroundBrush_const(const XGraphicsView* self)
{
    if (!self) return NULL;
    return &self->m_backgroundBrush;
}

/* ==================== 变换 ==================== */

const XImageTransform* XGraphicsView_transform_const(const XGraphicsView* self)
{
    if (!self) return NULL;
    return &self->m_matrix;
}

void XGraphicsView_viewportTransform(const XGraphicsView* self,
                                     XImageTransform* out)
{
    XImageTransform move;
    if (!out) return;
    if (!self) {
        XGraphicsItem_matrixIdentity(out);
        return;
    }
    /* matrix * T(-scroll)：矩阵先作用（scene→matrix 后平移 -scroll）。
     * 对标 qgraphicsview.cpp:3798-3805。 */
    XGraphicsItem_matrixFromTranslate(
        &move, -(float)gview_horizontalScroll((XGraphicsView*)self),
        -(float)gview_verticalScroll((XGraphicsView*)self));
    if (self->m_identityMatrix) {
        *out = move;
        return;
    }
    XGraphicsItem_matrixMultiply(out, &self->m_matrix, &move);
}

void XGraphicsView_setTransform(XGraphicsView* self,
                                const XImageTransform* matrix, bool combine)
{
    XImageTransform oldMatrix;
    XImageTransform identity;
    /* matrix==NULL 按**单位阵**处理（头注承诺；对标 Qt
     * `setTransform(QTransform())`——缺省构造的 QTransform 就是单位阵）。
     * 【复核根修 2026-11-16】此前 `matrix ? *matrix : oldMatrix` 让 NULL
     * 静默保持旧阵，与文档相反、NULL 复位失效。 */
    XGraphicsItem_matrixIdentity(&identity);
    if (!self) return;
    oldMatrix = self->m_matrix;
    if (!combine)
        self->m_matrix = matrix ? *matrix : identity;
    else if (matrix)
        XGraphicsItem_matrixMultiply(&self->m_matrix, matrix, &oldMatrix);
    else
        /* combine=true 且 NULL：单位阵先作用=原阵（显式写回，不做魔法）。 */
        self->m_matrix = oldMatrix;
    /* 同值早退（Qt :3868 oldMatrix == d->matrix；QTransform 逐字段精确
     * 相等，这里同式逐字段比——XMemcmp 对含 float 的结构有填充字节差异
     * 风险，不用）。 */
    if (oldMatrix.m11 == self->m_matrix.m11 &&
        oldMatrix.m12 == self->m_matrix.m12 &&
        oldMatrix.m21 == self->m_matrix.m21 &&
        oldMatrix.m22 == self->m_matrix.m22 &&
        oldMatrix.dx == self->m_matrix.dx &&
        oldMatrix.dy == self->m_matrix.dy &&
        oldMatrix.m13 == self->m_matrix.m13 &&
        oldMatrix.m23 == self->m_matrix.m23 &&
        oldMatrix.m33 == self->m_matrix.m33)
        return;
    /* 恒等快门（Qt :3871 d->identityMatrix = d->matrix.isIdentity()）。 */
    self->m_identityMatrix =
        self->m_matrix.m11 == 1.0f && self->m_matrix.m12 == 0.0f &&
        self->m_matrix.m21 == 0.0f && self->m_matrix.m22 == 1.0f &&
        self->m_matrix.dx == 0.0f && self->m_matrix.dy == 0.0f &&
        self->m_matrix.m13 == 0.0f && self->m_matrix.m23 == 0.0f &&
        self->m_matrix.m33 == 1.0f;
    gview_recalculateContentSize(self);
    gview_centerView(self,
                     (XGraphicsViewViewportAnchor)self->m_transformationAnchor);
    /* 任意矩阵变更全量重绘（Qt :3892 d->updateAll()）。 */
    gview_updateAll(self);
}

void XGraphicsView_resetTransform(XGraphicsView* self)
{
    XImageTransform identity;
    XGraphicsItem_matrixIdentity(&identity);
    XGraphicsView_setTransform(self, &identity, false);
}

void XGraphicsView_rotate(XGraphicsView* self, float deg)
{
    XImageTransform matrix;
    if (!self) return;
    matrix = self->m_matrix;
    XGraphicsItem_matrixRotate(&matrix, deg);
    XGraphicsView_setTransform(self, &matrix, false);
}

void XGraphicsView_scale(XGraphicsView* self, float sx, float sy)
{
    XImageTransform matrix;
    if (!self) return;
    matrix = self->m_matrix;
    XGraphicsItem_matrixScale(&matrix, sx, sy);
    XGraphicsView_setTransform(self, &matrix, false);
}

void XGraphicsView_translate(XGraphicsView* self, float dx, float dy)
{
    XImageTransform matrix;
    if (!self) return;
    matrix = self->m_matrix;
    XGraphicsItem_matrixTranslate(&matrix, dx, dy);
    XGraphicsView_setTransform(self, &matrix, false);
}

float XGraphicsView_devicePixelRatio(const XGraphicsView* self)
{
    XWindow* handle;
    float dpr;
    if (!self) return 1.0f;
    handle = XWidget_windowHandle(XWidget_window((const XWidget*)self));
    dpr = XWindow_devicePixelRatio(handle);
    /* 非法兜底（XWindow_devicePixelRatio 空句柄已回 1.0；双保险口径同
     * XWidget.c:14670 的 isfinite 兜底）。 */
    if (!(dpr > 0.0f) || !isfinite((double)dpr)) return 1.0f;
    return dpr;
}

/* ==================== 坐标映射 ==================== */

XPointF XGraphicsView_mapToScene(const XGraphicsView* self,
                                 const XPoint* viewportPoint)
{
    XPointF out;
    float vx = viewportPoint ? (float)viewportPoint->x : 0.0f;
    float vy = viewportPoint ? (float)viewportPoint->y : 0.0f;
    /* 显式清零（helpers 的 NULL 支已写，此处是双保险：任何路径都不返回
     * 未初始化栈值）。 */
    out.x = 0.0f;
    out.y = 0.0f;
    gview_mapToSceneF(self, vx, vy, &out);
    return out;
}

XPoint XGraphicsView_mapFromScene(const XGraphicsView* self,
                                  const XPointF* scenePoint)
{
    XPointF out;
    XPoint p;
    out.x = 0.0f;
    out.y = 0.0f;
    gview_mapFromSceneF(self, scenePoint ? scenePoint->x : 0.0f,
                        scenePoint ? scenePoint->y : 0.0f, &out);
    p.x = gview_round(out.x);
    p.y = gview_round(out.y);
    return p;
}

XRectF XGraphicsView_mapToSceneRect(const XGraphicsView* self,
                                    const XRect* viewportRect)
{
    XRectF out;
    XPointF corners[4];
    float minX;
    float minY;
    float maxX;
    float maxY;
    int i;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = 0.0f;
    out.height = 0.0f;
    if (!self || !viewportRect) return out;
    /* 四角映射取包盒（Qt mapToScene(QRect) 返回四角 QPolygonF，包盒为
     * 合法近似；adjusted(0,0,1,1) 的像素闭区间口径随 int 域忽略）。 */
    gview_mapToSceneF(self, (float)viewportRect->x,
                      (float)viewportRect->y, &corners[0]);
    gview_mapToSceneF(self, (float)(viewportRect->x + viewportRect->width),
                      (float)viewportRect->y, &corners[1]);
    gview_mapToSceneF(self, (float)(viewportRect->x + viewportRect->width),
                      (float)(viewportRect->y + viewportRect->height),
                      &corners[2]);
    gview_mapToSceneF(self, (float)viewportRect->x,
                      (float)(viewportRect->y + viewportRect->height),
                      &corners[3]);
    minX = maxX = corners[0].x;
    minY = maxY = corners[0].y;
    for (i = 1; i < 4; ++i) {
        if (corners[i].x < minX) minX = corners[i].x;
        if (corners[i].x > maxX) maxX = corners[i].x;
        if (corners[i].y < minY) minY = corners[i].y;
        if (corners[i].y > maxY) maxY = corners[i].y;
    }
    out.x = minX;
    out.y = minY;
    out.width = maxX - minX;
    out.height = maxY - minY;
    return out;
}

XRect XGraphicsView_mapFromSceneRect(const XGraphicsView* self,
                                     const XRectF* sceneRect)
{
    XRect out;
    XPointF corners[4];
    float minX;
    float minY;
    float maxX;
    float maxY;
    int i;
    XRect_init(&out, 0, 0, 0, 0);
    if (!self || !sceneRect) return out;
    gview_mapFromSceneF(self, sceneRect->x, sceneRect->y, &corners[0]);
    gview_mapFromSceneF(self, sceneRect->x + sceneRect->width, sceneRect->y,
                        &corners[1]);
    gview_mapFromSceneF(self, sceneRect->x + sceneRect->width,
                        sceneRect->y + sceneRect->height, &corners[2]);
    gview_mapFromSceneF(self, sceneRect->x, sceneRect->y + sceneRect->height,
                        &corners[3]);
    minX = maxX = corners[0].x;
    minY = maxY = corners[0].y;
    for (i = 1; i < 4; ++i) {
        if (corners[i].x < minX) minX = corners[i].x;
        if (corners[i].x > maxX) maxX = corners[i].x;
        if (corners[i].y < minY) minY = corners[i].y;
        if (corners[i].y > maxY) maxY = corners[i].y;
    }
    out.x = gview_round(minX);
    out.y = gview_round(minY);
    out.width = gview_round(maxX) - out.x;
    out.height = gview_round(maxY) - out.y;
    return out;
}

/* ==================== 导航 ==================== */

void XGraphicsView_centerOn(XGraphicsView* self, const XPointF* pos)
{
    XPointF point;
    XPointF viewPoint;
    XWidget* viewport;
    float width;
    float height;
    if (!self) return;
    point.x = pos ? pos->x : 0.0f;
    point.y = pos ? pos->y : 0.0f;
    viewport = XAbstractScrollArea_viewport(&self->m_base);
    width = viewport ? (float)XWidget_width(viewport) : 0.0f;
    height = viewport ? (float)XWidget_height(viewport) : 0.0f;
    XGraphicsItem_matrixMapPoint(&self->m_matrix, point.x, point.y, &viewPoint);
    /* 有缩进的轴不动（Qt 的 !d->leftIndent / !d->topIndent 守卫）。 */
    if (!self->m_leftIndent) {
        XScrollBar* hbar =
            XAbstractScrollArea_horizontalScrollBar(&self->m_base);
        if (hbar)
            XScrollBar_setValue(
                hbar, gview_roundBound(viewPoint.x - width / 2.0f));
    }
    if (!self->m_topIndent) {
        XScrollBar* vbar =
            XAbstractScrollArea_verticalScrollBar(&self->m_base);
        if (vbar)
            XScrollBar_setValue(
                vbar, gview_roundBound(viewPoint.y - height / 2.0f));
    }
    self->m_lastCenterPoint = point;
}

void XGraphicsView_centerOnItem(XGraphicsView* self, XGraphicsItem* item)
{
    XRectF br;
    XPointF center;
    if (!self || !item) return;
    br = XGraphicsItem_sceneBoundingRect(item);
    center.x = br.x + br.width / 2.0f;
    center.y = br.y + br.height / 2.0f;
    XGraphicsView_centerOn(self, &center);
}

/* ==================== 设备变换 ==================== */

void XGraphicsView_deviceTransform(const XGraphicsView* self,
                                   const XGraphicsItem* item,
                                   XImageTransform* out)
{
    XImageTransform vp;
    if (!out) return;
    if (!self) {
        XGraphicsItem_matrixIdentity(out);
        return;
    }
    XGraphicsView_viewportTransform(self, &vp);
    /* = item.deviceTransform(viewportTransform)：Qt 的 sceneTransform *
     * viewportTransform（qgraphicsitem.cpp:4287-4294），组合捷径经
     * W1 公开入口。dpr 不在矩阵内（头注 ①）。 */
    XGraphicsItem_deviceTransform(item, &vp, out);
}

/* ==================== 拖动模式 ==================== */

void XGraphicsView_setDragMode(XGraphicsView* self, XGraphicsViewDragMode mode)
{
    uint8_t oldMode;
    if (!self) return;
    /* 同值早退（Qt :1505 `if (d->dragMode == mode) return;`）——必须位于
     * clearRubberBand 之前：重复设置同一模式时正在拉的带要保留。
     * 【复核根修 2026-11-17】此前无早退，同模式重复调用会清掉进行中的
     * 橡皮筋（Qt 不清）。 */
    if (self->m_dragMode == (uint8_t)mode) return;
    oldMode = self->m_dragMode;
    /* 换模式清带（Qt :1508 d->clearRubberBand() 位于模式赋值之前）：
     * 否则从 RubberBandDrag 切走时 rubberBanding/rubberBandRect 残留到
     * release，带会「挂」在屏上不消失。 */
    if (self->m_rubberBanding ||
        self->m_rubberBandRect.width != 0 || self->m_rubberBandRect.height != 0) {
        self->m_rubberBanding = false;
        XRect_init(&self->m_rubberBandRect, 0, 0, 0, 0);
        gview_updateAll(self);
    }
    self->m_dragMode = (uint8_t)mode;
    /* ScrollHandDrag→NoDrag 撤手拖态（Qt :1516-1519 的旧值==ScrollHandDrag
     * 条件，不是「新模式是 NoDrag」就撤）。 */
    if (oldMode == (uint8_t)XGraphicsViewDragMode_ScrollHandDrag &&
        mode == XGraphicsViewDragMode_NoDrag && self->m_handScrolling)
        self->m_handScrolling = false;
}

XGraphicsViewDragMode XGraphicsView_dragMode(const XGraphicsView* self)
{
    if (!self) return XGraphicsViewDragMode_NoDrag;
    return (XGraphicsViewDragMode)self->m_dragMode;
}

void XGraphicsView_setTransformationAnchor(
    XGraphicsView* self, XGraphicsViewViewportAnchor anchor)
{
    XWidget* viewport;
    if (!self) return;
    self->m_transformationAnchor = (uint8_t)anchor;
    /* 对标 setTransformationAnchor，qgraphicsview.cpp:1363-1372：
     * `if (d->transformationAnchor == AnchorUnderMouse)
     *      d->viewport->setMouseTracking(true);`（:1370-1371）
     * ——AnchorUnderMouse 需要视口持续收悬停期 move 才有「当前鼠标位置」
     * （否则只剩按键拖动期的事件）。 */
    if (anchor == XGraphicsViewViewportAnchor_AnchorUnderMouse) {
        viewport = XAbstractScrollArea_viewport(&self->m_base);
        if (viewport) XWidget_setMouseTracking(viewport, true);
    }
}

XGraphicsViewViewportAnchor XGraphicsView_transformationAnchor(
    const XGraphicsView* self)
{
    if (!self)
        return XGraphicsViewViewportAnchor_AnchorViewCenter;
    return (XGraphicsViewViewportAnchor)self->m_transformationAnchor;
}

const XRect* XGraphicsView_rubberBandRect_const(const XGraphicsView* self)
{
    if (!self) return NULL;
    if (self->m_dragMode != XGraphicsViewDragMode_RubberBandDrag ||
        !self->m_interactive || !self->m_rubberBanding)
        return NULL;
    return &self->m_rubberBandRect;
}

/* ==================== 与 scene 联动 ==================== */

void XGraphicsView_invalidateScene(XGraphicsView* self, const XRectF* sceneRect)
{
    XRect vpRect;
    if (!self) return;
    if (sceneRect) {
        /* 映射进视口后标脏（Qt invalidateScene 的 mapFromScene(rect)
         * 与视口相交判定最小版：框架标脏按控件全幅/矩形两档，直接把
         * 映射矩形给 XWidget_updateRect）。 */
        vpRect = XGraphicsView_mapFromSceneRect(self, sceneRect);
        XWidget_updateRect((XWidget*)self, &vpRect);
    } else {
        gview_updateAll(self);
    }
}

void XGraphicsView_updateSceneRect(XGraphicsView* self, const XRectF* rect)
{
    if (!self) return;
    /* NULL=无操作（头注契约）：Qt 的 updateSceneRect(QRectF) 是场景
     * sceneRectChanged 信号槽，参数恒有值；本框架以 NULL 表示「调用方未
     * 给新矩形」。【复核根修 2026-11-17】此前仍会走重算，与文档
     * 「保留现值、无操作」不符。 */
    if (!rect) return;
    if (self->m_hasSceneRect) return; /* 显式设置时不跟随（Qt 同）。 */
    self->m_sceneRect = *rect;
    gview_recalculateContentSize(self);
}

/* ==================== 渲染 ==================== */

void XGraphicsView_renderViewport(XGraphicsView* self, XPainter* painter,
                                  const XRect* viewportRect)
{
    XRectF exposed;
    XImageTransform vp;
    const XRect* vpRect = viewportRect;
    XRect defaultRect;
    XRect localVpRect;
    if (!self || !painter) return;
    if (!vpRect) {
        /* 缺省=整幅即视口（不裁剪不平移；painter 坐标系即视口坐标系）。 */
        XRect_init(&defaultRect, 0, 0, XWidget_width((XWidget*)self),
                   XWidget_height((XWidget*)self));
        vpRect = &defaultRect;
    }
    /* vpRect 是**视口在 painter 坐标系里的位置**；视口显示的**内容**恒为
     * 视口局部 (0,0,w,h)。暴露场景矩形必须由局部矩形映射得出——用带原点
     * 的 vpRect 会把「视口在 painter 里的偏移」当成滚动量再叠一次
     * （[复核根修 2026-11-16]：旧写法在 vpRect=(30,30,30,30) 下使
     * XGraphicsScene_render 的 target 裁剪落到 (60,60,30,30)，内容被
     * 自己的裁剪整幅裁空）。 */
    XRect_init(&localVpRect, 0, 0, vpRect->width, vpRect->height);
    /* 暴露场景矩形 = 视口局部矩形映射到场景的四角包盒（对标 paintEvent 的
     * exposedSceneRect = mapToScene(exposedRegion.boundingRect()).boundingRect()）。
     * 复用本类公开的 mapToSceneRect——同一套四角映射只留一份实现（项目
     * 「框架有的绝不手搓」铁律）。 */
    exposed = XGraphicsView_mapToSceneRect(self, &localVpRect);
    XPainter_save(painter);
    /* ② 视口裁剪（vpRect 在**调用方逻辑坐标系**里；setClipRect 在设置
     * 当时按当前有效变换映射——所以必须放在后续变换变更之前）。 */
    XPainter_setClipRect(painter, vpRect, XPainterClipOperation_IntersectClip);
    /* ③ 平移到视口左上（调用方逻辑坐标 → 视口坐标）。 */
    if (vpRect->x != 0 || vpRect->y != 0)
        XPainter_translate(painter, (float)vpRect->x, (float)vpRect->y);
    /* ④ 视口变换**组合**进当前变换——不是替换。
     * 【复核根修 2026-11-16】setWorldTransform(combine=false) 是整体替换
     * m_transform（XPainter.c:18602-18612 `m_state.m_transform = *matrix`），
     * 会把 ③ 的平移与**调用方既有变换**（paintEvent 的 XWidget_paintOffset、
     * 离屏重定向、子控件坐标系）一起擦掉 ⇒ 裁剪仍按旧变换落在 (60,0) 而
     * 内容画到 (0,0)，整幅被自己的裁剪裁空（复核探针实跑：px(5,5)=0 且
     * px(65,5)=0；不 translate 的对照 px(5,5)=ffff0000）。
     * 对标 Qt render 的 `painter->setTransform(painterMatrix, true)`
     * （qgraphicsview.cpp:2180 combine=true）：vp 先作用（scene→视口）、
     * 调用方变换后作用（视口→设备）。 */
    XGraphicsView_viewportTransform(self, &vp);
    XPainter_setWorldTransform(painter, &vp, true);
    /* ⑤ 背景（drawBackground：按笔刷样式分派，见 gview_drawBackground；
     * 渐变/图案分支自带 save/restore，不污染 ⑥ 的项绘制）。 */
    gview_drawBackground(self, painter, &exposed);
    /* ⑥ 各项：XGraphicsScene_render 在当前变换上组合（target==source
     * ⇒ 位移式=单位阵；裁剪=暴露场景矩形经当前变换映射的交集）；逐项
     * sceneTransform 由 render 内部叠放——与 Qt drawItemHelper 同构。 */
    if (self->m_scene)
        XGraphicsScene_render(self->m_scene, painter, &exposed);
    /* ⑦ restore（前景层本子集无笔刷建模，不登）。 */
    XPainter_restore(painter);
}

void XGraphicsView_render(XGraphicsView* self, XPainter* painter,
                          const XRectF* target, const XRect* source)
{
    XRect sourceRect;
    XRect adjacentRect;
    XRectF targetRect;
    float xratio;
    float yratio;
    XImageTransform painterMatrix;
    XImageTransform step;
    XRectF sourceSceneRect;
    /* 无场景=空操作（Qt qgraphicsview.cpp:2110
     * `if (!d->scene || !(painter && painter->isActive())) return;`）。
     * 【复核根修 2026-11-17】此前无场景仍会铺视图背景笔刷，与 Qt 的
     * 「render 对空场景不碰 painter」不符。 */
    if (!self || !painter || !self->m_scene) return;
    /* 缺省 source=**视口矩形**（Qt :2113-2116 `viewport()->rect()`——视口
     * 子控件自身的 (0,0,w,h)，不含滚动条/框架；【复核根修 2026-11-16】
     * 此前用整个控件尺寸，带滚动条时会多画不可见带）。 */
    if (source) {
        sourceRect = *source;
    } else {
        XWidget* vp = XAbstractScrollArea_viewport(&self->m_base);
        XRect_init(&sourceRect, 0, 0,
                   vp ? XWidget_width(vp) : XWidget_width((XWidget*)self),
                   vp ? XWidget_height(vp) : XWidget_height((XWidget*)self));
    }
    /* 缺省 target=设备矩形（Qt :2118-2125：Picture 设备取 source 尺寸，
     * 其它设备取 device 全幅）。本框架 painter 无设备尺寸查询入口，退化为
     * source 尺寸（=Picture 支；调用方显式给 target 更精确，文档已注）。 */
    if (target) {
        targetRect = *target;
    } else {
        targetRect.x = 0.0f;
        targetRect.y = 0.0f;
        targetRect.width = (float)sourceRect.width;
        targetRect.height = (float)sourceRect.height;
    }
    if (sourceRect.width <= 0 || sourceRect.height <= 0 ||
        targetRect.width <= 0.0f || targetRect.height <= 0.0f)
        return;
    xratio = targetRect.width / (float)sourceRect.width;
    yratio = targetRect.height / (float)sourceRect.height;
    /* IgnoreAspectRatio（本接口无比例枚举，Qt 缺省分支同）。
     * 作用顺序 = matrix → T(-scroll) → T(-source.tl) → S(xratio,yratio)
     * → T(target.tl)。Qt 原文 qgraphicsview.cpp:2160-2166：
     *   painterMatrix = matrix * T(-scroll);
     *   painterMatrix *= T(target).scale(...).translate(-source);
     * 其中最后一段是**方法链构造**的 QTransform（translate/scale 前乘：
     * 映射 p → ratio·(p − source.tl) + target.tl），故整条链按作用顺序
     * 展开正是上式。【复核根修 2026-11-17】原实现把 T(target) 放在缩放
     * 之前、T(-source) 放在最后：matrixScale 是**前乘**（S 先作用），
     * 得 S → matrix → -scroll → +target → -source——source 平移被放到
     * 设备端且未缩放，非零 source.tl 时整幅错位（本轮 Qt 6.8.0 offscreen
     * 探针同参实测：source=(10,10,80,80)、target=(20,10,40,40)、项视口盒
     * (10..30,10..30) 时 Qt 落位 (20..30,10..20)=红，旧式落位在画布外）。 */
    painterMatrix = self->m_matrix;
    XGraphicsItem_matrixFromTranslate(
        &step, -(float)gview_horizontalScroll(self),
        -(float)gview_verticalScroll(self));
    XGraphicsItem_matrixMultiply(&painterMatrix, &painterMatrix, &step);
    XGraphicsItem_matrixFromTranslate(&step, -(float)sourceRect.x,
                                      -(float)sourceRect.y);
    XGraphicsItem_matrixMultiply(&painterMatrix, &painterMatrix, &step);
    /* 缩放必须**后乘**（先前的 matrixScale 是前乘，会把比例套到场景
     * 坐标上）；先用 identity 造出纯 S，再按「先前矩阵 → S」组合。 */
    XGraphicsItem_matrixIdentity(&step);
    XGraphicsItem_matrixScale(&step, xratio, yratio);
    XGraphicsItem_matrixMultiply(&painterMatrix, &painterMatrix, &step);
    XGraphicsItem_matrixFromTranslate(&step, targetRect.x, targetRect.y);
    XGraphicsItem_matrixMultiply(&painterMatrix, &painterMatrix, &step);
    /* 源区域 → 场景包盒（Qt :2146 `mapToScene(sourceRect.adjusted(-1,-1,1,1))`
     * ——**四向外扩 1px** 防半像素/旋转时边缘项漏画）。 */
    adjacentRect.x = sourceRect.x - 1;
    adjacentRect.y = sourceRect.y - 1;
    adjacentRect.width = sourceRect.width + 2;
    adjacentRect.height = sourceRect.height + 2;
    sourceSceneRect = XGraphicsView_mapToSceneRect(self, &adjacentRect);
    XPainter_save(painter);
    /* 裁剪 target（调用方逻辑坐标；Qt :2174 setClipRect(targetRect)）。 */
    {
        XRect clip;
        clip.x = gview_roundBound(targetRect.x);
        clip.y = gview_roundBound(targetRect.y);
        clip.width = gview_roundBound(targetRect.width);
        clip.height = gview_roundBound(targetRect.height);
        XPainter_setClipRect(painter, &clip,
                             XPainterClipOperation_IntersectClip);
    }
    XPainter_setWorldTransform(painter, &painterMatrix, true);
    gview_drawBackground(self, painter, &sourceSceneRect);
    if (self->m_scene)
        XGraphicsScene_render(self->m_scene, painter, &sourceSceneRect);
    XPainter_restore(painter);
}

/* ==================== 框架事件槽（paint/resize/show/scroll） ============ */

/** @brief 绘制槽：painter 绑定后备存储 → 平移绘制偏移 → 渲染视口核
 *  （视口在控件本体系里的矩形=子控件几何；对标 paintEvent 的
 *  painter(viewport()) 模型在「本体自绘」框架下的等价承载）。 */
static void VX_gview_paintEvent(XWidget* self, XEvent* event)
{
    XGraphicsView* view = (XGraphicsView*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect vpRect;
    if (!view || !event) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    /* 视口矩形（本体坐标=视口子控件几何；无视口回落内容矩形）。 */
    {
        XWidget* vp = XAbstractScrollArea_viewport(&view->m_base);
        if (vp)
            vpRect = XWidget_geometry(vp);
        else {
            XRect cr = XWidget_contentsRect((const XWidget*)self);
            XRect_init(&vpRect, cr.x, cr.y, cr.width, cr.height);
        }
    }
    XGraphicsView_renderViewport(view, &painter, &vpRect);
    XPainter_deinit(&painter);
}

/** @brief 内容滚动：滚动缓存失效 + 全量标脏（对标 scrollContentsBy 的
 *  dirtyScroll=true + 视口重绘；加速 blit 属平台优化，最小闭环全量）。 */
static void VX_gview_scrollContentsBy(XAbstractScrollArea* self, int dx,
                                      int dy)
{
    XGraphicsView* view = (XGraphicsView*)self;
    (void)dx;
    (void)dy;
    if (!view) return;
    view->m_dirtyScroll = true;
    gview_updateAll(view);
    /* 滚动后中心点跟随视口（Qt updateLastCenterPoint，
     * qgraphicsview.cpp:526-530：mapToScene(viewport rect 中心点)）。 */
    {
        XWidget* viewport = XAbstractScrollArea_viewport(&view->m_base);
        if (viewport) {
            XPointF center;
            gview_mapToSceneF(view, (float)XWidget_width(viewport) / 2.0f,
                              (float)XWidget_height(viewport) / 2.0f, &center);
            view->m_lastCenterPoint = center;
        }
    }
}

/** @brief 尺寸变化：按 Qt resizeEvent 逐支（qgraphicsview.cpp:3587-3610）。 */
static void VX_gview_resizeEvent(XWidget* self, XEvent* event)
{
    XGraphicsView* view = (XGraphicsView*)self;
    XPointF oldLastCenterPoint;
    if (!view) return;
    /* ① 存中心点——resize 可能滚动视图从而改变中心点（Qt :3591-3593）。 */
    oldLastCenterPoint = view->m_lastCenterPoint;
    /* ② 基类（视口/条排布）+ 场景口径范围重算（Qt :3595-3596）。 */
    XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    gview_recalculateContentSize(view);
    /* ③ 还原或更新中心点（Qt :3598-3603）：
     *    NoAnchor 且 keepLastCenterPoint 未置位 ⇒ 按新视口重取中心点
     *    （updateLastCenterPoint）；否则还原存档。 */
    if (view->m_resizeAnchor ==
            (uint8_t)XGraphicsViewViewportAnchor_NoAnchor &&
        !view->m_keepLastCenterPoint) {
        XWidget* vp = XAbstractScrollArea_viewport(&view->m_base);
        if (vp) {
            XPointF center;
            gview_mapToSceneF(view, (float)XWidget_width(vp) / 2.0f,
                              (float)XWidget_height(vp) / 2.0f, &center);
            view->m_lastCenterPoint = center;
        }
    } else {
        view->m_lastCenterPoint = oldLastCenterPoint;
    }
    /* ④ 按 resize 锚点重居中；keepLastCenterPoint 一次性消费（Qt :3604-3605）。 */
    gview_centerView(view, (XGraphicsViewViewportAnchor)view->m_resizeAnchor);
    view->m_keepLastCenterPoint = false;
}

/** @brief 首次显示：几何就绪后补一次范围重算（XScrollArea 同款套路：
 *  隐藏期的 resize 已排布，show 再收敛一轮保证范围与视口终值一致）。 */
static void VX_gview_showEvent(XWidget* self, XEvent* event)
{
    XGraphicsView* view = (XGraphicsView*)self;
    if (!view) return;
    XClass_Parent(XAbstractScrollArea, EXWidget_ShowEvent, XWidgetEventSlot)(
        self, event);
    VX_gview_resizeEvent(self, event);
}

#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && \
          XPAINTER_PATH_ON */

#endif /* XGUI_ON */
