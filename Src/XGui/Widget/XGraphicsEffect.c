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

#if XGUI_ON

#include "XGraphicsEffect.h"

#include "XGuiConfig.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XWidget.h"
#include "XWidget_Protected.h" /* XWidget_paintImage/paintOffset（框架内部
                                    绘制入口，效果回贴目标获取用）。 */
#include "XImage.h"
#include "XPainter.h"

#include <math.h>

#if XWIDGET_ON

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

/** @brief 整数矩形并集（就地写 out；两者都为空时 out 为空）。 */
static void xgraphicseffect_unionRect(const XRect* a, const XRect* b,
                                      XRect* out)
{
    int x1, y1, x2, y2;
    if (!a || !b || !out) return;
    if (a->width <= 0 || a->height <= 0) {
        *out = *b;
        return;
    }
    if (b->width <= 0 || b->height <= 0) {
        *out = *a;
        return;
    }
    x1 = a->x < b->x ? a->x : b->x;
    y1 = a->y < b->y ? a->y : b->y;
    x2 = (a->x + a->width) > (b->x + b->width) ? (a->x + a->width)
                                               : (b->x + b->width);
    y2 = (a->y + a->height) > (b->y + b->height) ? (a->y + a->height)
                                                 : (b->y + b->height);
    out->x = x1;
    out->y = y1;
    out->width = x2 - x1;
    out->height = y2 - y1;
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

/**
 * @brief Deinit 槽位：把控件侧的 graphicsEffect 槽摘空。
 *
 * @details 对标 Qt 的 `QGraphicsEffect::~QGraphicsEffect →
 *          d->setGraphicsEffectSource(nullptr)`，后者经
 *          QWidgetEffectSourcePrivate::detach 把 `widget->d_func()
 *          ->graphicsEffect` 置空。本机 Qt 6.8 实测：`delete effect` 后
 *          `w.graphicsEffect()` 返回 NULL。
 *          控件析构/换装路径会先 XGraphicsEffect_setSource(NULL) 再删效果，
 *          故本槽实际只覆盖「外部直接删除效果」这一种情形——不这样兜底，
 *          控件会留下悬垂指针并在下次绘制时读已释放对象。
 */
static void VXGraphicsEffect_deinit(XGraphicsEffect* self)
{
    if (!self) return;
    if (self->m_source) {
        XWidget* source = self->m_source;
        /* 先断本侧关联再通知控件：控件侧的 Detach 会调用
         * XWidget_graphicsEffectDetach，其判据是「控件槽 == 本效果」，
         * 与 m_source 无关；先清 m_source 可让重入（如 Detach 内的标脏
         * 触发同步绘制）不再看到半拆状态。 */
        self->m_source = NULL;
        XWidget_graphicsEffectDetach(source, self);
    }
    self->m_drawCtx = NULL;
    self->m_lastBoundsValid = false;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

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
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGraphicsEffect_deinit);
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
    /* 对标 Qt QGraphicsEffect::setEnabled：
     *   if (d->isEnabled == enable) return;
     *   d->isEnabled = enable;
     *   if (d->source) d->source->update();
     *   emit enabledChanged(enable);
     * 顺序 = 先请求源重绘、后发信号（信号槽里读到的脏区已是新状态）。 */
    XGraphicsEffect_update(self);
    xgraphicseffect_emitBool(self,
                             (size_t)XGraphicsEffect_enabledChanged_signal,
                             enable);
}

void XGraphicsEffect_update(XGraphicsEffect* self)
{
    XWidget* source;
    XWidget* parent;
    XRectF boundsF;
    XRect bounds;
    XRect mark;
    if (!self || !self->m_source) return;
    source = self->m_source;
    /* boundingRect 为源本地坐标（Qt 口径）；脏区账本按控件自身坐标系记，
       故父级存在时平移到父坐标再上账，顶层控件直接用本地坐标。 */
    boundsF = XGraphicsEffect_boundingRect(self);
    bounds = xgraphicseffect_toRect(&boundsF);
    if (bounds.width <= 0 || bounds.height <= 0) return;
    parent = XWidget_parentWidget(source);
    if (parent) {
        bounds.x += XWidget_x(source);
        bounds.y += XWidget_y(source);
    }
    /* 与上一轮标脏矩形取并集：效果参数收缩（模糊半径/偏移变小、甚至
       禁用）时，**旧**外扩环里的像素必须被覆写，只标新包围盒会留残影。
       对标 Qt updateBoundingRect 同时失效新旧矩形的语义。 */
    mark = bounds;
    if (self->m_lastBoundsValid)
        xgraphicseffect_unionRect(&self->m_lastBounds, &bounds, &mark);
    self->m_lastBounds = bounds;
    self->m_lastBoundsValid = true;
    if (parent) {
        /* 外扩环可越出源控件矩形，必须由父控件承载（父的标脏区域覆盖
           兄弟区间；经源控件标脏会被其 contentsRect 裁剪，环出账本 →
           DIRECT 差带模式下残影，昆仑通态投影残留实修点）。 */
        XWidget_updateRect(parent, &mark);
        return;
    }
    XWidget_updateRect(source, &mark);
}

void XGraphicsEffect_updateBoundingRect(XGraphicsEffect* self)
{
    /* Qt 语义：包围盒变化后新旧矩形都失效。旧矩形的失效由 update() 内的
       「上次标脏 ∪ 本次包围盒」承担，故此处直接走同一入口。 */
    XGraphicsEffect_update(self);
}

/* ==================== 效果源与包围盒 ==================== */

XWidget* XGraphicsEffect_source(const XGraphicsEffect* self)
{ return self ? self->m_source : NULL; }

void XGraphicsEffect_setSource(XGraphicsEffect* self, XWidget* source)
{
    if (!self) return;
    if (self->m_source != source) {
        /* 换源/摘除即作废"上次标脏矩形"缓存：它按旧源的坐标系记账
           （父坐标或顶层本地坐标），跨源复用会把脏区标到别处。 */
        self->m_lastBoundsValid = false;
    }
    self->m_source = source;
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

XRectF XGraphicsEffect_sourceBoundingRect(const XGraphicsEffect* self)
{
    XRectF out;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = 0.0f;
    out.height = 0.0f;
    if (!self || !self->m_source) return out;
    /* 对标 Qt QWidgetEffectSourcePrivate::boundingRect：控件作源时恒为
       (0,0,w,h)（源本地逻辑坐标，原点不随控件在父级中的位置漂移；本机
       Qt 6.8 实测 child.setGeometry(30,40,80,20) → (0,0,80,20)）。 */
    out.width = (float)XWidget_width(self->m_source);
    out.height = (float)XWidget_height(self->m_source);
    return out;
}

XRectF XGraphicsEffect_boundingRect(const XGraphicsEffect* self)
{
    XRectF src;
    if (!self || !self->m_source) {
        src.x = 0.0f;
        src.y = 0.0f;
        src.width = 0.0f;
        src.height = 0.0f;
        return src;
    }
    /* 对标 Qt：
     *   if (d->source) return boundingRectFor(d->source->boundingRect());
     *   return QRectF();
     * 坐标系 = 源本地坐标（不是父坐标）；脏区折算由 update() 负责。 */
    src = XGraphicsEffect_sourceBoundingRect(self);
    return XGraphicsEffect_boundingRectFor(self, &src);
}

bool XGraphicsEffect_sourceIsPixmap(const XGraphicsEffect* self)
{
    /* 对标 Qt：`return source() ? source()->isPixmap() : false;`。XGui 的
       效果源恒为控件（无 QGraphicsItem/QGraphicsPixmapItem 世界），
       QWidgetEffectSourcePrivate::isPixmap 恒 false ⇒ 本函数恒 false。
       本机 Qt 6.8 探针：挂在 QWidget 上的效果 draw() 内实测 false。 */
    (void)self;
    return false;
}

XImage* XGraphicsEffect_sourcePixmap(const XGraphicsEffect* self,
                                     XPoint* offset,
                                     XGraphicsEffectPixmapPadMode mode)
{
    XRectF rectF;
    XRect rect;
    XImage* out;
    XImage* snapshot;
    XPainter painter;
    if (offset) {
        offset->x = 0;
        offset->y = 0;
    }
    if (!self || !self->m_source) return NULL;
    /* 只在 Draw 派发期间可用（与 Qt 6.8 实际行为一致：QWidgetEffectSource
       的 pixmap 实现无条件解引用绘制上下文，本机探针在 draw() 之外调用
       直接段错误）。XGui 以 NULL 优雅返回取代崩溃，不另造离屏源
       （那会把已施效的画面当成源，递归施效）。 */
    if (!self->m_drawCtx || !self->m_drawCtx->m_source) return NULL;
    /* 对标 QWidgetEffectSourcePrivate::pixmap（qwidget.cpp:5823-5867）：
       源矩形 = 控件 rect()，再按 mode 决定补边量；offset = 补边后矩形
       左上角（可为负）。XGui 无设备像素比，恒按逻辑=设备 1:1。 */
    rectF = XGraphicsEffect_sourceBoundingRect(self);
    switch (mode) {
    case XGraphicsEffectPadMode_PadToTransparentBorder:
        rectF.x -= 1.0f;
        rectF.y -= 1.0f;
        rectF.width += 2.0f;
        rectF.height += 2.0f;
        break;
    case XGraphicsEffectPadMode_PadToEffectiveBoundingRect:
        rectF = XGraphicsEffect_boundingRectFor(self, &rectF);
        break;
    case XGraphicsEffectPadMode_NoPad:
    default:
        break;
    }
    rect = xgraphicseffect_toRect(&rectF);
    if (rect.width <= 0 || rect.height <= 0) return NULL;
    if (offset) {
        offset->x = rect.x;
        offset->y = rect.y;
    }
    out = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!out) return NULL;
    if (!XImage_reinit_ex(out, rect.width, rect.height,
                          XImageFormat_ARGB32_Premultiplied)) {
        XClassDelete(out);
        return NULL;
    }
    XImage_fillRect(out, NULL, 0u);
    /* 管线快照（控件局部坐标、未施效）——等价 Qt 在 draw() 内命中的源缓存。 */
    snapshot = self->m_drawCtx->m_source;
    if (snapshot && !XImage_isNull(snapshot)) {
        XPainter_init(&painter, NULL);
        if (XPainter_begin_image(&painter, out)) {
            /* 源快照的 (0,0) 就是源本地原点；补边后图像原点在
               rect.topLeft ⇒ 源平移到 -rect.topLeft 对齐。 */
            XPainter_drawImage(&painter, snapshot, -rect.x, -rect.y);
            XPainter_end(&painter);
        }
        XPainter_deinit(&painter);
    }
    return out;
}

void XGraphicsEffect_drawSource(XGraphicsEffect* self, XPainter* painter)
{
    XGraphicsEffectDrawContext* ctx;
    if (!self || !painter) return;
    ctx = self->m_drawCtx;
    if (!ctx || !ctx->m_source || !ctx->m_dest) return;
    if (XImage_isNull(ctx->m_dest)) return;
    /* 源快照与输出画布同处控件局部坐标系：画布原点对应 destRect 左上角，
       故平移到 -destRect 即把源按画布原点对齐（越界由绘制器裁剪）。 */
    XPainter_drawImage(painter, ctx->m_source,
                       -ctx->m_destRect.x, -ctx->m_destRect.y);
}

/* ==================== 效果绘制管线 ==================== */

/* 指数模糊定标：与 Qt qimageeffects.cpp 的 expblur<aprec, zprec, ...>
 * 逐项一致（aprec=12 = 定点精度 0.12，zprec=10 = 状态小数位）。 */
#define XGRAPHICSEFFECT_BLUR_APREC 12
#define XGRAPHICSEFFECT_BLUR_ZPREC 10
/** 输出收敛点：z 的整数部分位于第 (zprec+aprec) 位之后（Qt 的 ZA_MASK 起点）。 */
#define XGRAPHICSEFFECT_BLUR_SHIFT (XGRAPHICSEFFECT_BLUR_APREC + \
                                    XGRAPHICSEFFECT_BLUR_ZPREC)

/**
 * @brief 一维指数模糊单步（对标 Qt qt_blurinner，四通道版本）。
 * @details Qt 原式（qimageeffects.cpp:27-58）把 0xAARRGGBB 的每个通道左移
 *          zprec 位做定点状态，前向趟用 alpha 作步长向当前像素收敛；本函数
 *          是该式的逐句转写，移位常量按 aprec/zprec 展开。
 * @param pixel 当前像素（读通道、写回滤波结果）。
 * @param zR/zG/zB/zA 四通道定点状态（跨像素保持）。
 * @param alpha 收敛步长（0..4095）。
 * @return 滤波后的像素。
 */
static uint32_t xgraphicseffect_blurInner(uint32_t pixel, int* zR, int* zG,
                                          int* zB, int* zA, int alpha)
{
    int Az = ((int)((pixel >> 24) & 0xFFu)) << XGRAPHICSEFFECT_BLUR_ZPREC;
    int Rz = ((int)((pixel >> 16) & 0xFFu)) << XGRAPHICSEFFECT_BLUR_ZPREC;
    int Gz = ((int)((pixel >> 8) & 0xFFu)) << XGRAPHICSEFFECT_BLUR_ZPREC;
    int Bz = ((int)(pixel & 0xFFu)) << XGRAPHICSEFFECT_BLUR_ZPREC;
    *zR += alpha * (Rz - (*zR >> XGRAPHICSEFFECT_BLUR_APREC));
    *zG += alpha * (Gz - (*zG >> XGRAPHICSEFFECT_BLUR_APREC));
    *zB += alpha * (Bz - (*zB >> XGRAPHICSEFFECT_BLUR_APREC));
    *zA += alpha * (Az - (*zA >> XGRAPHICSEFFECT_BLUR_APREC));
    return ((((uint32_t)(*zA >> XGRAPHICSEFFECT_BLUR_SHIFT)) & 0xFFu) << 24) |
           ((((uint32_t)(*zR >> XGRAPHICSEFFECT_BLUR_SHIFT)) & 0xFFu) << 16) |
           ((((uint32_t)(*zG >> XGRAPHICSEFFECT_BLUR_SHIFT)) & 0xFFu) << 8) |
           (((uint32_t)(*zB >> XGRAPHICSEFFECT_BLUR_SHIFT)) & 0xFFu);
}

/**
 * @brief 一维指数模糊单步（只滤 alpha 通道；对标 Qt qt_blurinner_alphaOnly）。
 * @param pixel 当前像素（只改高 8 位 alpha，其余位原样保留）。
 * @param z alpha 定点状态。
 * @param alpha 收敛步长。
 * @return 滤波后的像素。
 */
static uint32_t xgraphicseffect_blurInnerAlpha(uint32_t pixel, int* z,
                                               int alpha)
{
    int Az = ((int)((pixel >> 24) & 0xFFu)) << XGRAPHICSEFFECT_BLUR_ZPREC;
    *z += alpha * (Az - (*z >> XGRAPHICSEFFECT_BLUR_APREC));
    return (pixel & 0x00FFFFFFu) |
           ((((uint32_t)(*z >> XGRAPHICSEFFECT_BLUR_SHIFT)) & 0xFFu) << 24);
}

/** @brief 行趟：前向 + 后向（对标 Qt qt_blurrow 的两段循环）。 */
static void xgraphicseffect_blurRow(uint32_t* bits, int stridePixels, int width,
                                    int y, int alpha, bool alphaOnly)
{
    int x;
    int zR = 0;
    int zG = 0;
    int zB = 0;
    int zA = 0;
    uint32_t* row = bits + (size_t)y * (size_t)stridePixels;
    for (x = 0; x < width; ++x) {
        if (alphaOnly)
            row[x] = xgraphicseffect_blurInnerAlpha(row[x], &zA, alpha);
        else
            row[x] = xgraphicseffect_blurInner(row[x], &zR, &zG, &zB, &zA,
                                               alpha);
    }
    for (x = width - 2; x >= 0; --x) {
        if (alphaOnly)
            row[x] = xgraphicseffect_blurInnerAlpha(row[x], &zA, alpha);
        else
            row[x] = xgraphicseffect_blurInner(row[x], &zR, &zG, &zB, &zA,
                                               alpha);
    }
}

/** @brief 列趟：前向 + 后向（Qt 用 qmemrotate 转置后复用 qt_blurrow，
 *         本实现直接按列递推——指数滤波可分离，结果等价）。 */
static void xgraphicseffect_blurColumn(uint32_t* bits, int stridePixels,
                                       int height, int x, int alpha,
                                       bool alphaOnly)
{
    int y;
    int zR = 0;
    int zG = 0;
    int zB = 0;
    int zA = 0;
    for (y = 0; y < height; ++y) {
        uint32_t* pixel = bits + (size_t)y * (size_t)stridePixels + x;
        if (alphaOnly)
            *pixel = xgraphicseffect_blurInnerAlpha(*pixel, &zA, alpha);
        else
            *pixel = xgraphicseffect_blurInner(*pixel, &zR, &zG, &zB, &zA,
                                               alpha);
    }
    for (y = height - 2; y >= 0; --y) {
        uint32_t* pixel = bits + (size_t)y * (size_t)stridePixels + x;
        if (alphaOnly)
            *pixel = xgraphicseffect_blurInnerAlpha(*pixel, &zA, alpha);
        else
            *pixel = xgraphicseffect_blurInner(*pixel, &zR, &zG, &zB, &zA,
                                               alpha);
    }
}

/**
 * @brief 收敛步长（对标 Qt expblur 的 alpha 计算）。
 * @details `alpha = qRound(2^aprec · (1 − (cutOffIntensity/255)^(1/radius)))`，
 *          cutOffIntensity = 2：半径处强度衰减到 2/255；radius ≤ 1e-5 时取
 *          最大值（近似恒等，Qt 原式同）。
 */
static int xgraphicseffect_blurAlpha(float radius)
{
    double value;
    if (radius <= 1e-5f) return (1 << XGRAPHICSEFFECT_BLUR_APREC) - 1;
    value = 1.0 - pow(2.0 / 255.0, 1.0 / (double)radius);
    value *= (double)(1 << XGRAPHICSEFFECT_BLUR_APREC);
    /* qRound：四舍五入（正数即 floor(x+0.5)）。 */
    return (int)floor(value + 0.5);
}

void XGraphicsEffect_blurImage(XImage* image, float radius, bool quality,
                               bool alphaOnly)
{
    uint32_t* bits;
    int width;
    int height;
    int stridePixels;
    int alpha;
    int passes;
    int pass;
    int i;
    if (!image || XImage_isNull(image)) return;
    /* 仅处理 ARGB32 类 4 字节/像素格式（效果画布恒为
       ARGB32_Premultiplied），其余格式原样返回。 */
    if (XImage_format(image) != XImageFormat_ARGB32_Premultiplied) return;
    stridePixels = XImage_bytesPerLine(image) / 4;
    if (stridePixels <= 0) return;
    width = XImage_width(image);
    height = XImage_height(image);
    if (width <= 0 || height <= 0) return;
    bits = (uint32_t*)(void*)XImage_bits(image);
    if (!bits) return;
    /* 对标 Qt QPixmapBlurFilter::draw 的 `radius <= 1` 短路与 expblur 的
       负半径行为：半径 ≤ 0 时 alpha 取最大值（近似恒等），不产生发散。 */
    if (radius <= 0.0f) return;
    /* improvedQuality：Qt 把半径减半后跑两遍（qimageeffects.cpp:121-123），
       高频细节保留更好。 */
    if (quality) radius *= 0.5f;
    alpha = xgraphicseffect_blurAlpha(radius);
    passes = quality ? 2 : 1;
    for (pass = 0; pass < passes; ++pass) {
        for (i = 0; i < height; ++i)
            xgraphicseffect_blurRow(bits, stridePixels, width, i, alpha,
                                    alphaOnly);
        for (i = 0; i < width; ++i)
            xgraphicseffect_blurColumn(bits, stridePixels, height, i, alpha,
                                       alphaOnly);
    }
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
    /* 空包围盒 = 效果把源收缩到零面积（如 blurRadius 极负时
       pad = 1 + 2.5r 已经吃掉整个控件）：Qt 的口径是**整块裁剪**——
       本机 Qt 6.8 实测 30x20 控件 + QGraphicsBlurEffect(-20)
       （boundingRectFor = (49,49,-68,-78)）渲染后父级无任何像素；
       故这里返回 true 表示"本控件已由效果路径处理"（一个像素都不落），
       不能返回 false 让调用方回退到常规绘制（那样控件反而整块可见）。 */
    if (destRectXF.width <= 0.0f || destRectXF.height <= 0.0f) return true;
    destRect = xgraphicseffect_toRect(&destRectXF);
    if (destRect.width <= 0 || destRect.height <= 0) return true;
    dest = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!dest) return false;
    if (!XImage_reinit_ex(dest, destRect.width, destRect.height,
                          XImageFormat_ARGB32_Premultiplied)) {
        XClassDelete(dest);
        return false;
    }
    /* 与 XWidget_grab 快照同语义：输出画布从全透明开始，Draw 实现负责
       填充全部内容，防止复用实现变化时残留旧像素。 */
    XImage_fillRect(dest, NULL, 0u);
    ctx.m_source = sourceImage;
    ctx.m_sourceRect = srcRectX;
    ctx.m_dest = dest;
    ctx.m_destRect = destRect;
    /* 对标 QGraphicsEffect::draw 虚调用：交由派生效果完成像素处理。
       效果处理段在效果自有画布（dest）上进行，必须脱离外层绘制的
       设备坐标表面裁剪：DIRECT 模式脏区裁剪是设备坐标，与画布本地
       坐标不相交时源绘制整帧被裁空、画布保持透明，回贴后控件呈背景
       平灰（桌面+昆仑通态 DIRECT 实测：启用透明度/模糊/投影后样例
       内容丢失；FULL 模式裁剪恒整窗故不触发）。处理完恢复外层裁剪，
       回贴段写真实表面仍受其约束。 */
    {
        const XRect* savedClip = XPainter_surfaceClipRect();
        XImage* savedTarget = XPainter_surfaceClipTarget();
        XRect keep;
        if (savedClip) keep = *savedClip;
        XPainter_clearSurfaceClipRect();
        slot = NULL;
        if (XClassGetVtable(self))
            slot = XClassGetVirtualFunc(self, EXGraphicsEffect_Draw,
                                        XGraphicsEffect_DrawSlot);
        /* 挂上本次绘制上下文：Draw 槽位内可用 XGraphicsEffect_drawSource
           直接取源快照（对标 Qt drawSource 依赖的 source 状态）。派发结束
           立即摘除，效果对外承诺 m_drawCtx 仅在 Draw 期间有效。 */
        self->m_drawCtx = &ctx;
        if (slot) slot(self, &ctx);
        else VXGraphicsEffect_draw(self, &ctx);
        self->m_drawCtx = NULL;
        if (savedClip)
            XPainter_setSurfaceClipRect(&keep, savedTarget);
        else
            XPainter_clearSurfaceClipRect();
    }
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
    XClassDelete(dest);
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

#endif /* XGUI_ON */
