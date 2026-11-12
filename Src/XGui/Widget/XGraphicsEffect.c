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
#if XPAINTER_PATH_ON
#include "XGraphicsItem.h"     /* W4：图形项效果链（item 源包围盒/矩阵代数/
                                  析构摘槽）。XGraphicsItem.h 与
                                  XGraphicsEffect.h 只互相前向声明，无环。 */
#endif

#include <math.h>

#if XWIDGET_ON

/* ==================== 内部辅助 ==================== */

/* 图形项源的画布包围盒（定义在下方"坐标系无关的矩阵/矩形助手"段；此处前置
 * 声明供 sourcePixmap/sourcePixmap_2 使用）。 */
static bool xgraphicseffect_sourceCanvasBounds(const XGraphicsEffect* self,
                                               XRectF* out);

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
#if XPAINTER_PATH_ON
    if (self->m_itemSource) {
        /* 图形项源同款（对标 Qt ~QGraphicsEffect →
         * setGraphicsEffectSource(nullptr) → QGraphicsItemEffectSourcePrivate
         * 析构把 item->d_ptr->graphicsEffect 置空）：先清本侧再摘项槽，
         * 防重入看到半拆状态。 */
        XGraphicsItem* item = self->m_itemSource;
        self->m_itemSource = NULL;
        XGraphicsItem_graphicsEffectDetach(item, self);
    }
#endif /* XPAINTER_PATH_ON */
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

bool XGraphicsEffect_fuzzyCompare(float a, float b)
{
    /* 对标 Qt 6.8 qFuzzyCompare(float, float)（corelib/global/qnumeric.h:
       393-396）：`qAbs(p1-p2) * 100000.f <= min(qAbs(p1), qAbs(p2))`。
       容差取 1e5（Qt 的 **float** 重载；double 重载才是 1e12——X 的效果
       属性全用 float 存值，故用 float 重载口径）。
       置零保护同 Qt：min(|a|,|b|)==0 时相对容差无意义，只认精确相等
       （否则 0 与 1e-30 会被判"相等"，setOpacity(0→极小值) 会被误早退）。 */
    if (a == b) return true;
    if (a == 0.0f || b == 0.0f) return false;
    return fabsf(a - b) * 100000.0f <= fminf(fabsf(a), fabsf(b));
}

void XGraphicsEffect_update(XGraphicsEffect* self)
{
    XWidget* source;
    XWidget* parent;
    XRectF boundsF;
    XRect bounds;
    XRect sourceRect;
    XRect mark;
    if (!self || !self->m_source) return;
    source = self->m_source;
    parent = XWidget_parentWidget(source);
    /* boundingRect 为源本地坐标（Qt 口径）；脏区账本按控件自身坐标系记，
       故父级存在时平移到父坐标再上账，顶层控件直接用本地坐标。 */
    boundsF = XGraphicsEffect_boundingRect(self);
    bounds = xgraphicseffect_toRect(&boundsF);
    /* 源控件矩形（父坐标）：boundingRectFor 可以**收缩**到小于源矩形
       （负模糊半径/负偏移 ⇒ 效果包围盒比控件小甚至为负面积），此时四周
       边环既不在效果包围盒里、也不会被"上次包围盒"覆盖。Qt 的
       QGraphicsEffect::update 经 invalidateCache 让**整个源控件**失效，
       故源控件矩形恒进脏区。【P0-59 2026-11-11】 */
    sourceRect = XWidget_rect(source);
    if (parent) {
        sourceRect.x += XWidget_x(source);
        sourceRect.y += XWidget_y(source);
        bounds.x += XWidget_x(source);
        bounds.y += XWidget_y(source);
    }
    if (bounds.width <= 0 || bounds.height <= 0) {
        /* 包围盒为空/面积负：效果区无可标，但源控件矩形必须重绘（否则
           setBlurRadius(5→-20) 或 setEnabled(false) 后上一帧画面留在屏
           上）；同时作废旧包围盒缓存——下一轮有面积时不得再并入这份
           失效的旧矩形。旧实现在此直接 return，一行都不标。 */
        self->m_lastBoundsValid = false;
        if (parent) XWidget_updateRect(parent, &sourceRect);
        else XWidget_updateRect(source, &sourceRect);
        return;
    }
    /* 与上一轮标脏矩形取并集：效果参数收缩（模糊半径/偏移变小、甚至
       禁用）时，**旧**外扩环里的像素必须被覆写，只标新包围盒会留残影。
       对标 Qt updateBoundingRect 同时失效新旧矩形的语义。
       常态（正外扩）下源控件矩形 ⊆ 效果包围盒，并集不会扩大脏区。 */
    mark = bounds;
    xgraphicseffect_unionRect(&mark, &sourceRect, &mark);
    if (self->m_lastBoundsValid)
        xgraphicseffect_unionRect(&self->m_lastBounds, &mark, &mark);
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
#if XPAINTER_PATH_ON
    if (source && self->m_itemSource) {
        /* 一个效果只有一个源（Qt 的 QGraphicsEffectSource 是单一指针）：
        * 效果从图形项搬到控件时，图形项侧槽位必须摘空，否则两宿主各自
        * 认为拥有它 ⇒ 任一宿主析构 delete 后另一宿主悬垂 + 二次释放
        * （与 XWidget_setGraphicsEffect 的"夺走"分支同族）。
        * 断源通知（复核修 2026-11-16）：Qt QGraphicsEffectPrivate::
        * setGraphicsEffectSource（qgraphicseffect_p.h:121-134）在换源时先置
        * `flags |= SourceDetached`；此处按仓库控件路径的次序发——通知时
        * source() 仍指旧源（自定义效果据此清缓存/脱离源）。 */
        XGraphicsItem* item = self->m_itemSource;
        XGraphicsEffect_sourceChanged_base(self,
                                          XGraphicsEffectChange_SourceDetached);
        self->m_itemSource = NULL;
        XGraphicsItem_graphicsEffectDetach(item, self);
    }
#endif /* XPAINTER_PATH_ON */
}

#if XPAINTER_PATH_ON
XGraphicsItem* XGraphicsEffect_itemSource(const XGraphicsEffect* self)
{
    return self ? self->m_itemSource : NULL;
}

void XGraphicsEffect_setItemSource(XGraphicsEffect* self, XGraphicsItem* item)
{
    XGraphicsItem* previous;
    XWidget* widget;
    if (!self) return;
    if (self->m_itemSource == item) return;
    self->m_lastBoundsValid = false;
    previous = self->m_itemSource;
    widget = self->m_source;
    if (item && (previous || widget)) {
        /* 换宿主：先通知 Detached（此刻旧源仍可见），再摘旧宿主——对标 Qt
         * setGraphicsEffectSource 的 `flags |= SourceDetached`（旧源存在时）
         * 与仓库控件路径同序（Src/XGui/Widget/XWidget.c:14771-14820）。 */
        XGraphicsEffect_sourceChanged_base(self,
                                          XGraphicsEffectChange_SourceDetached);
    }
    self->m_itemSource = item;
    if (item && previous && previous != item) {
        /* 换到新图形项：旧项槽位摘空（它不再拥有本效果）。 */
        XGraphicsItem_graphicsEffectDetach(previous, self);
    }
    if (item && widget) {
        /* 控件源与图形项源互斥（见 XGraphicsEffect_setSource 同款理由）。 */
        self->m_source = NULL;
        XWidget_graphicsEffectDetach(widget, self);
    }
}
#endif /* XPAINTER_PATH_ON */

void XGraphicsEffect_sourceChanged_base(XGraphicsEffect* self, int flags)
{
    XGraphicsEffect_SourceChangedSlot slot;
    if (!self || !XClassGetVtable(self)) return;
    slot = XClassGetVirtualFunc(self, EXGraphicsEffect_SourceChanged,
                                XGraphicsEffect_SourceChangedSlot);
    if (!slot) return;
    slot(self, flags);
}

/* 用户 2026-11-14 定案：虚函数对外唯一入口只留 `_base`；槽位为空时
 * 回落基类默认实现（与 Qt「空槽 = 默认行为」同口径），不另设二级桥。 */
XRectF XGraphicsEffect_boundingRectFor_base(const XGraphicsEffect* self,
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
    if (!self) return out;
    if (self->m_source) {
        /* 对标 Qt QWidgetEffectSourcePrivate::boundingRect：控件作源时恒为
           (0,0,w,h)（源本地逻辑坐标，原点不随控件在父级中的位置漂移；本机
           Qt 6.8 实测 child.setGeometry(30,40,80,20) → (0,0,80,20)）。 */
        out.width = (float)XWidget_width(self->m_source);
        out.height = (float)XWidget_height(self->m_source);
        return out;
    }
#if XPAINTER_PATH_ON
    if (self->m_itemSource) {
        /* 对标 QGraphicsItemEffectSourcePrivate::boundingRect
         * （qgraphicsitem.cpp:11002-11021，其中 11013-11015 为并集）：
         *     QRectF rect = item->boundingRect();
         *     if (!item->d_ptr->children.isEmpty())
         *         rect |= item->childrenBoundingRect();
         * 图形项源恒为**项本地坐标**（未经 pos/transform）。 */
        out = XGraphicsItem_boundingRect_base(self->m_itemSource);
        {
            XRectF kids = XGraphicsItem_childrenBoundingRect(self->m_itemSource);
            if (kids.width > 0.0f && kids.height > 0.0f) {
                if (out.width <= 0.0f || out.height <= 0.0f) {
                    out = kids;
                } else {
                    float x1 = out.x < kids.x ? out.x : kids.x;
                    float y1 = out.y < kids.y ? out.y : kids.y;
                    float x2 = (out.x + out.width) > (kids.x + kids.width)
                                   ? (out.x + out.width)
                                   : (kids.x + kids.width);
                    float y2 = (out.y + out.height) > (kids.y + kids.height)
                                   ? (out.y + out.height)
                                   : (kids.y + kids.height);
                    out.x = x1;
                    out.y = y1;
                    out.width = x2 - x1;
                    out.height = y2 - y1;
                }
            }
        }
    }
#endif /* XPAINTER_PATH_ON */
    return out;
}

XRectF XGraphicsEffect_boundingRect(const XGraphicsEffect* self)
{
    XRectF src;
    if (!self || (!self->m_source
#if XPAINTER_PATH_ON
                  && !self->m_itemSource
#endif
                  )) {
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
    return XGraphicsEffect_boundingRectFor_base(self, &src);
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
    if (!self) return NULL;
    /* 控件源与图形项源都算"有源"（W4 起图形项也可挂效果）。 */
    if (!self->m_source
#if XPAINTER_PATH_ON
        && !self->m_itemSource
#endif
        )
        return NULL;
    /* 只在 Draw 派发期间可用（与 Qt 6.8 实际行为一致：QWidgetEffectSource
       的 pixmap 实现无条件解引用绘制上下文，本机探针在 draw() 之外调用
       直接段错误）。XGui 以 NULL 优雅返回取代崩溃，不另造离屏源
       （那会把已施效的画面当成源，递归施效）。 */
    if (!self->m_drawCtx || !self->m_drawCtx->m_source) return NULL;
    /* 对标 QWidgetEffectSourcePrivate::pixmap（qwidget.cpp:5823-5867）：
       源矩形 = 控件 rect()，再按 mode 决定补边量；offset = 补边后矩形
       左上角（可为负）。XGui 无设备像素比，恒按逻辑=设备 1:1。
       图形项源（W4）取绘制上下文给出的**画布包围盒**——画布坐标 = 项本地
       坐标平移，源快照按画布原点对齐；控件源保持 Qt 的 (0,0,w,h) 口径。 */
    if (!xgraphicseffect_sourceCanvasBounds(self, &rectF))
        rectF = XGraphicsEffect_sourceBoundingRect(self);
    switch (mode) {
    case XGraphicsEffectPadMode_PadToTransparentBorder:
        rectF.x -= 1.0f;
        rectF.y -= 1.0f;
        rectF.width += 2.0f;
        rectF.height += 2.0f;
        break;
    case XGraphicsEffectPadMode_PadToEffectiveBoundingRect:
        rectF = XGraphicsEffect_boundingRectFor_base(self, &rectF);
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

/* ==================== 坐标系无关的矩阵/矩形助手 ==================== */

/** @brief 单次效果位图的最大边长（防未初始化上下文/巨型源导致越界分配）。 */
#define XGRAPHICSEFFECT_MAX_PIXELS 16384

/** @brief 平移矩阵（XPAINTER_PATH_ON 时复用 W1 框架矩阵代数）。 */
static void xgraphicseffect_matrixFromTranslate(XImageTransform* m, float dx,
                                                float dy)
{
#if XPAINTER_PATH_ON
    XGraphicsItem_matrixFromTranslate(m, dx, dy);
#else
    XMemset(m, 0, sizeof(*m));
    m->m11 = 1.0f;
    m->m22 = 1.0f;
    m->m33 = 1.0f;
    m->dx = dx;
    m->dy = dy;
#endif
}

/** @brief 单位阵。 */
static void xgraphicseffect_matrixIdentity(XImageTransform* m)
{
#if XPAINTER_PATH_ON
    XGraphicsItem_matrixIdentity(m);
#else
    XMemset(m, 0, sizeof(*m));
    m->m11 = 1.0f;
    m->m22 = 1.0f;
    m->m33 = 1.0f;
#endif
}

/** @brief 矩阵乘法（out = a·b，a 先作用；XPAINTER_PATH_ON 时复用 W1）。 */
static void xgraphicseffect_matrixMultiply(XImageTransform* out,
                                           const XImageTransform* a,
                                           const XImageTransform* b)
{
#if XPAINTER_PATH_ON
    XGraphicsItem_matrixMultiply(out, a, b);
#else
    XImageTransform r;
    XMemset(&r, 0, sizeof(r));
    r.m11 = a->m11 * b->m11 + a->m12 * b->m21;
    r.m12 = a->m11 * b->m12 + a->m12 * b->m22;
    r.m21 = a->m21 * b->m11 + a->m22 * b->m21;
    r.m22 = a->m21 * b->m12 + a->m22 * b->m22;
    r.dx = a->dx * b->m11 + a->dy * b->m21 + b->dx;
    r.dy = a->dx * b->m12 + a->dy * b->m22 + b->dy;
    r.m33 = 1.0f;
    *out = r;
#endif
}

/** @brief 矩形映射包盒（XPAINTER_PATH_ON 时复用 W1 XGraphicsItem_matrixMapRect）。 */
static void xgraphicseffect_mapRect(const XImageTransform* m,
                                    const XRectF* rect, XRectF* out)
{
#if XPAINTER_PATH_ON
    XGraphicsItem_matrixMapRect(m, rect, out);
#else
    float px[4];
    float py[4];
    float x1;
    float y1;
    float x2;
    float y2;
    int i;
    if (!m || !rect || !out) return;
    px[0] = rect->x;
    py[0] = rect->y;
    px[1] = rect->x + rect->width;
    py[1] = rect->y;
    px[2] = rect->x;
    py[2] = rect->y + rect->height;
    px[3] = rect->x + rect->width;
    py[3] = rect->y + rect->height;
    x1 = x2 = m->m11 * px[0] + m->m21 * py[0] + m->dx;
    y1 = y2 = m->m12 * px[0] + m->m22 * py[0] + m->dy;
    for (i = 1; i < 4; ++i) {
        float x = m->m11 * px[i] + m->m21 * py[i] + m->dx;
        float y = m->m12 * px[i] + m->m22 * py[i] + m->dy;
        if (x < x1) x1 = x;
        if (x > x2) x2 = x;
        if (y < y1) y1 = y;
        if (y > y2) y2 = y;
    }
    out->x = x1;
    out->y = y1;
    out->width = x2 - x1;
    out->height = y2 - y1;
#endif
}

/**
 * @brief 取"源画布包围盒"（绘制上下文显式给出的那份；未给/不可信返回 false）。
 * @details 校验有限性与量级：既有测试会手工拼 XGraphicsEffectDrawContext
 *          而不填新字段（W4 之前的写法），此时 m_sourceBounds 是栈垃圾；
 *          这里用有限性 + [0, MAX] 区间把它挡掉，让调用方回落到 Qt 的
 *          sourceBoundingRect，绝不按垃圾尺寸分配位图（MSVC /RTC1 与
 *          QtAlign 旧用例双防线）。
 */
static bool xgraphicseffect_canvasBounds(const XGraphicsEffect* self,
                                         XRectF* out)
{
    const XGraphicsEffectDrawContext* ctx;
    if (!self || !out) return false;
    ctx = self->m_drawCtx;
    if (!ctx) return false;
    if (!isfinite((double)ctx->m_sourceBounds.x) ||
        !isfinite((double)ctx->m_sourceBounds.y) ||
        !isfinite((double)ctx->m_sourceBounds.width) ||
        !isfinite((double)ctx->m_sourceBounds.height))
        return false;
    if (ctx->m_sourceBounds.width <= 0.0f || ctx->m_sourceBounds.height <= 0.0f)
        return false;
    if (ctx->m_sourceBounds.width > (float)XGRAPHICSEFFECT_MAX_PIXELS ||
        ctx->m_sourceBounds.height > (float)XGRAPHICSEFFECT_MAX_PIXELS)
        return false;
    if (fabsf(ctx->m_sourceBounds.x) > 1.0e6f ||
        fabsf(ctx->m_sourceBounds.y) > 1.0e6f)
        return false;
    *out = ctx->m_sourceBounds;
    return true;
}

void XGraphicsEffectDrawContext_init(XGraphicsEffectDrawContext* ctx)
{
    if (!ctx) return;
    XMemset(ctx, 0, sizeof(*ctx));
    xgraphicseffect_matrixIdentity(&ctx->m_deviceTransform);
    /* dpr 缺省 1.0（"无 DPR 层"）：旧上下文/手拼上下文不填本字段时
       Device 取源与 W4 的唯一差异只剩"pad 在设备空间求"，物理几何在
       dpr=1 下逐像素不变。 */
    ctx->m_devicePixelRatio = 1.0f;
}

/**
 * @brief 取上下文里的设备像素比（dpr；未设置/非法回落 1.0）。
 * @details 对标 Qt `info->painter->device()->devicePixelRatio()`
 *          （qgraphicsitem.cpp:11109-11110 只在 Device 取源里读一次）。
 *          XGui 的上游（XGraphicsScene 的效果管线）从绘制器绑定的目标图像
 *          取 dpr 填进 ctx（XImage_devicePixelRatio）；0 / 非有限 / 负数
 *          一律按 1.0 —— 旧用例手拼上下文（W4 之前只填四字段）不会因此
 *          改变 dpr=1 的既有几何。 */
static float xgraphicseffect_devicePixelRatio(const XGraphicsEffect* self)
{
    const XGraphicsEffectDrawContext* ctx;
    float dpr;
    if (!self || !(ctx = self->m_drawCtx)) return 1.0f;
    dpr = ctx->m_devicePixelRatio;
    if (!isfinite((double)dpr) || dpr <= 0.0f) return 1.0f;
    return dpr;
}

/**
 * @brief 取"源画布包围盒"（图形项源专用；控件源返回 false）。
 * @details 图形项源在场景渲染里的画布坐标 = **项本地坐标平移**（画布原点
 *          = 项包围盒左上角），故源矩形必须取上下文给出的 m_sourceBounds
 *          而不是未经平移的 sourceBoundingRect。控件源（m_source 非 NULL）
 *          恒按 Qt 的 (0,0,w,h) 口径，**不读新字段**——W4 之前手拼
 *          XGraphicsEffectDrawContext 的既有用例因此零回归。
 */
static bool xgraphicseffect_sourceCanvasBounds(const XGraphicsEffect* self,
                                               XRectF* out)
{
    if (!self || self->m_source) return false;
    return xgraphicseffect_canvasBounds(self, out);
}

XRectF XGraphicsEffect_sourceBoundingRect_2(
    const XGraphicsEffect* self, const XImageTransform* deviceTransform,
    int coordinateSystem)
{
    XRectF out;
    XImageTransform identity;
    const XImageTransform* dt;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = 0.0f;
    out.height = 0.0f;
    if (!self) return out;
    if (coordinateSystem != XGraphicsEffectCoordinateSystem_Device)
        return XGraphicsEffect_sourceBoundingRect(self);
    /* Device 口径必须有绘制上下文（对标 QGraphicsItemEffectSourcePrivate::
     * boundingRect：`if (!info && deviceCoordinates) { qWarning(...); return
     * QRectF(); }`，qgraphicsitem.cpp:11002-11008）。 */
    if (!self->m_drawCtx) return out;
    /* 源包围盒：图形项源取画布框（项本地坐标平移后的那一份），控件源取
     * sourceBoundingRect（(0,0,w,h)）——与 sourcePixmap_2 的取源口径同源。 */
    if (!xgraphicseffect_sourceCanvasBounds(self, &out))
        out = XGraphicsEffect_sourceBoundingRect(self);
    if (out.width <= 0.0f || out.height <= 0.0f) return out;
    dt = deviceTransform;
    if (!dt) {
        if (self->m_drawCtx->m_hasDeviceTransform) {
            dt = &self->m_drawCtx->m_deviceTransform;
        } else {
            xgraphicseffect_matrixIdentity(&identity);
            dt = &identity;
        }
    }
    xgraphicseffect_mapRect(dt, &out, &out);
    return out;
}

XImage* XGraphicsEffect_sourcePixmap_2(const XGraphicsEffect* self,
                                       const XImageTransform* deviceTransform,
                                       XPoint* offset, int coordinateSystem)
{
    const XGraphicsEffectDrawContext* ctx;
    XImageTransform identity;
    XImageTransform xlate;
    XImageTransform map;
    const XImageTransform* dt;
    XRectF bounds;
    XRectF rectF;
    XRect rect;
    float dpr;
    XImage* out;
    XPainter painter;
    if (offset) {
        offset->x = 0;
        offset->y = 0;
    }
    if (!self) return NULL;
    if (coordinateSystem != XGraphicsEffectCoordinateSystem_Device)
        /* 对标 Qt 缺省形参：LogicalCoordinates + PadToEffectiveBoundingRect
         * （qgraphicseffect.h:68-71 `sourcePixmap(Qt::CoordinateSystem system =
         * Qt::LogicalCoordinates, QPoint *offset = nullptr, PixmapPadMode mode
         * = PadToEffectiveBoundingRect)`）——既有路径原样复用。 */
        return XGraphicsEffect_sourcePixmap(self, offset,
                                            XGraphicsEffectPadMode_PadToEffectiveBoundingRect);
    /* Device：必须有绘制上下文（对标 QGraphicsEffectSource::pixmap 的
     * `if (!info && deviceCoordinates) { qWarning(...); return QPixmap(); }`，
     * qgraphicsitem.cpp:11081-11085）。 */
    ctx = self->m_drawCtx;
    if (!ctx || !ctx->m_source || XImage_isNull(ctx->m_source)) return NULL;
    dt = deviceTransform;
    if (!dt) {
        if (ctx->m_hasDeviceTransform) {
            dt = &ctx->m_deviceTransform;
        } else {
            xgraphicseffect_matrixIdentity(&identity);
            dt = &identity;
        }
    }
    /* ① 补边在**设备空间**做（先映射、后补边）——Qt 口径：
     *    Qt 的 paddedEffectRect(Device, PadToEffectiveBoundingRect, src)
     *    收到的 src 已经是 `boundingRect(Device)` 的结果（内部就是
     *    `info->painter->worldTransform().mapRect(item->boundingRect())`，
     *    qgraphicsitem.cpp:11015-11018），随后直接
     *    `item->graphicsEffect()->boundingRectFor(deviceRect)`
     *    （qgraphicsitem.cpp:11054-11055）⇒ 补边量是**设备单位**，
     *    只随 dpr 放大（位图 `effectRectF.size() * dpr`，
     *    qgraphicsitem.cpp:11110-11111），**不随视图缩放放大**。
     *    旧实现先在源画布空间补边再整体乘 deviceTransform：deviceTransform
     *    含视图缩放 ⇒ pad 被一起放大（本机 Qt 6.8.0 offscreen 探针：
     *    QGraphicsView scale=2 下 Qt 的 `sourceBoundingRect(Device).topLeft −
     *    effectRect.topLeft` 恒为 8，旧 XGui 为 16）。
     *    XGui 的 deviceTransform 把 dpr 折在末端 ⇒ 先 ÷dpr 回"设备逻辑空间"
     *    求补边，再 ×dpr 回物理像素：dpr 的放大语义与 Qt 一致
     *    （dpr=2 物理 pad 仍 ×2，既有断言不回归），视图缩放不进 pad。 */
    if (!xgraphicseffect_sourceCanvasBounds(self, &bounds))
        bounds = XGraphicsEffect_sourceBoundingRect(self);
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return NULL;
    /* 不变量（复核阻断项，2026-11-17）：**补边与内容原点共用本 rect**。
     * 源内容（m_renderSource 的回调，或无回调时的 m_source 快照）以
     * bounds.topLeft 为原点绘制在源画布坐标系里；补边用的源矩形也由同一
     * bounds 映射而来，随后整体乘 deviceTransform 并减去 effectRect.topLeft。
     * 任何一侧单独叠上「项本地/场景平移」都会让内容相对补边错位——复核
     * gdb 实测的 (10,20) 就是项的场景位置被叠了第二次。 */
    if (!isfinite((double)bounds.x) || !isfinite((double)bounds.y))
        return NULL;
    if (!isfinite((double)bounds.width) || !isfinite((double)bounds.height))
        return NULL;
    if (bounds.width > (float)XGRAPHICSEFFECT_MAX_PIXELS ||
        bounds.height > (float)XGRAPHICSEFFECT_MAX_PIXELS)
        return NULL;
    dpr = xgraphicseffect_devicePixelRatio(self);
    xgraphicseffect_mapRect(dt, &bounds, &rectF); /* 源画布 → 设备（物理像素） */
    if (!isfinite((double)rectF.x) || !isfinite((double)rectF.y) ||
        !isfinite((double)rectF.width) || !isfinite((double)rectF.height))
        return NULL;
    if (dpr != 1.0f) {
        /* 物理像素 → 设备**逻辑**空间（Qt 的 boundingRectFor 入参空间）。 */
        rectF.x /= dpr;
        rectF.y /= dpr;
        rectF.width /= dpr;
        rectF.height /= dpr;
    }
    /* ② 设备逻辑空间补边（boundingRectFor 的 pad 是设备单位，与视图缩放无关）。 */
    rectF = XGraphicsEffect_boundingRectFor_base(self, &rectF);
    if (!isfinite((double)rectF.width) || !isfinite((double)rectF.height))
        return NULL;
    if (rectF.width <= 0.0f || rectF.height <= 0.0f) return NULL;
    if (dpr != 1.0f) {
        /* 设备逻辑 → 物理像素（= Qt 的 `effectRectF.size() * dpr`）。 */
        rectF.x *= dpr;
        rectF.y *= dpr;
        rectF.width *= dpr;
        rectF.height *= dpr;
    }
    if (!isfinite((double)rectF.x) || !isfinite((double)rectF.y))
        return NULL;
    rect = xgraphicseffect_toRect(&rectF);
    if (rect.width <= 0 || rect.height <= 0) return NULL;
    if (rect.width > XGRAPHICSEFFECT_MAX_PIXELS ||
        rect.height > XGRAPHICSEFFECT_MAX_PIXELS)
        return NULL;
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
    /* ③ 画布 → 图像像素：先套 deviceTransform、再平移到 effectRect 左上角
     *    （对标 qgraphicsitem.cpp:11116-11118
     *     `QTransform effectTransform = QTransform::fromTranslate(-effectRect.x(), -effectRect.y());
     *      if (deviceCoordinates && info->effectTransform) effectTransform *= *info->effectTransform;`
     *     ——Qt 的 effectTransform 在这里就是 painter 的世界变换，XGui 用
     *     显式 deviceTransform 承载）。 */
    xgraphicseffect_matrixFromTranslate(&xlate, -(float)rect.x, -(float)rect.y);
    xgraphicseffect_matrixMultiply(&map, dt, &xlate);
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, out)) {
        /* 继承外层渲染提示（对标 Qt qgraphicsitem.cpp:11114
         * `pixmapPainter.setRenderHints(info ? info->painter->renderHints()
         * : QPainter::TextAntialiasing)`）：0 = 不干预（留给 painter 缺省）。 */
        if (ctx->m_renderHints != 0u) {
            XPainter_setRenderHints(&painter,
                                    (XPainterRenderHints)~0u, false);
            XPainter_setRenderHints(&painter,
                                    (XPainterRenderHints)ctx->m_renderHints,
                                    true);
        }
        XPainter_setWorldTransform(&painter, &map, false);
        if (ctx->m_renderSource) {
            /* 源重绘（对标 Qt 的 scened->draw(item, &pixmapPainter, ...
             * &effectTransform ...)，qgraphicsitem.cpp:11129-11131）。 */
            ctx->m_renderSource(ctx->m_renderSourceData, &painter);
        } else if (ctx->m_source) {
            /* 无回调时 ctx->m_source 自身就是源内容：它的像素 (0,0) 对应
             * **源画布包围盒左上角** bounds.topLeft——必须与上面求补边用的
             * 是同一个 sourceBounds（复核阻断项「内容偏移项场景位置」的
             * 不变量）。内部调用点的 bounds 恒为 (0,0,w,h)（控件快照/
             * 图形项源位图均以包围盒原点起画），故这里是零回归；外部
             * 自造上下文若把 m_sourceBounds 设成非零画布坐标（Qt 的
             * `boundingRect(Device)` 就是 (10,20,40,30) 这种设备系矩形），
             * 源内容也随同一起对齐，不会只补边不搬内容。 */
            XPainter_drawImage(&painter, ctx->m_source, (int)bounds.x,
                               (int)bounds.y);
        }
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    return out;
}

bool XGraphicsEffect_drawToContext(XGraphicsEffect* self,
                                   XGraphicsEffectDrawContext* ctx)
{
    XGraphicsEffect_DrawSlot slot;
    const XRect* savedClip;
    XImage* savedTarget;
    XRect keep;
    if (!self || !ctx || !ctx->m_dest || XImage_isNull(ctx->m_dest)) return false;
    /* 效果处理段在效果自有画布上进行，必须脱离外层绘制的设备坐标表面裁剪
     * （理由见 XGraphicsEffect_drawWidget 的同款段：DIRECT 差带模式下裁剪
     * 是设备坐标，与画布本地坐标不相交时整帧裁空）。 */
    savedClip = XPainter_surfaceClipRect();
    savedTarget = XPainter_surfaceClipTarget();
    if (savedClip) keep = *savedClip;
    XPainter_clearSurfaceClipRect();
    slot = NULL;
    if (XClassGetVtable(self))
        slot = XClassGetVirtualFunc(self, EXGraphicsEffect_Draw,
                                    XGraphicsEffect_DrawSlot);
    /* 挂上本次绘制上下文：Draw 槽位内可用 XGraphicsEffect_drawSource /
     * sourcePixmap_2 直接取源（对标 Qt drawSource/sourcePixmap 依赖的
     * source 状态）。派发结束立即摘除。 */
    self->m_drawCtx = ctx;
    if (slot) slot(self, ctx);
    else VXGraphicsEffect_draw(self, ctx);
    self->m_drawCtx = NULL;
    if (savedClip)
        XPainter_setSurfaceClipRect(&keep, savedTarget);
    else
        XPainter_clearSurfaceClipRect();
    return true;
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
    destRectF = XGraphicsEffect_boundingRectFor_base(self, &srcRectF);
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
    destRectXF = XGraphicsEffect_boundingRectFor_base(self, &srcRectXF);
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
    /* 全字段清零起手（W4：上下文新增了设备系字段；不先清零会留栈垃圾）。 */
    XGraphicsEffectDrawContext_init(&ctx);
    ctx.m_source = sourceImage;
    ctx.m_sourceRect = srcRectX;
    ctx.m_dest = dest;
    ctx.m_destRect = destRect;
    /* 控件源的画布包围盒 = 控件全幅；本管线不做设备系取源，deviceTransform
     * 保持无效（m_hasDeviceTransform=false，sourcePixmap_2 的
     * deviceTransform 形参由调用方显式给）。 */
    ctx.m_sourceBounds.x = 0.0f;
    ctx.m_sourceBounds.y = 0.0f;
    ctx.m_sourceBounds.width = (float)widgetRect.width;
    ctx.m_sourceBounds.height = (float)widgetRect.height;
    /* 对标 QGraphicsEffect::draw 虚调用：交由派生效果完成像素处理。
       派发段（表面裁剪解除 + m_drawCtx 挂摘）收敛在 drawToContext，
       与场景渲染路径共用同一套语义。 */
    XGraphicsEffect_drawToContext(self, &ctx);
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
