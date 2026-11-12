/******************************************************************************
 * @file       XGraphicsScene.c
 * @brief      XGraphicsScene 图形场景实现（对标 Qt 6.8 QGraphicsScene）。
 * @details    与同名头文件的公开 API 一一对应。注册/命中/事件/选中/焦点/
 *             渲染的逐条出处见 XGraphicsScene.h 文件头注；本文件注释只标
 *             函数级差异与内部约定。索引为线性遍历 + 每次查询现算排序
 *             （对标 Qt linear index + recursive_items_helper 的排序口径；
 *             Qt 缺省 BSP 属加速结构，语义与线性一致，W4 可选优化）。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XGraphicsScene.h"

#if XPAINTER_PATH_ON

#include "XGuiConfig.h"
#include "XMemory.h"
#include "XVector.h"
#include "XClass.h"
#include "XPrintf.h"      /* 护栏告警（对标 qWarning）。 */
#include "XGraphicsEffect.h" /* W4：图形项效果（effect 的 Draw/包围盒/绘制
                                上下文；XGraphicsEffect.h 只对整个图形层前向
                                声明，无包含环）。 */
#include "XImage.h"
#include "XPainter.h"
#include "XWindowEvent.h" /* XEnterEvent：悬停事件承载（复用框架既有，
                             不新造事件类型；type 用
                             XEVENT_TYPE_GRAPHICS_SCENE_HOVER_*）。 */
#include <math.h>

/* ==================== 内部常量与辅助 ==================== */

/** @brief 全透明阈值（Qt recursive_items_helper 的 isOpacityNull 用
 *  EPSILON=1e-12；float 存储下 1e-12 已沉底，取 float 可分辨量级）。 */
#define GSC_OPACITY_EPS 1e-6f

/** @brief _q_adjustRect 的零宽/高修补量（qgraphicsscene_p.h:312-318
 *  原值：QRectF 对零宽高矩形 intersects 恒 false 的修补）。 */
#define GSC_ADJUST_EPS 0.00001f

/** @brief 全场景脏时的查询返回载体（file-scope 单槽；isDirty/dirtyRect
 *  查询在主线程消费，W3/W4 帧合成同线程，无并发写读者）。 */
static XRectF gsc_wholeDirtyRect;

/** @brief 场景护栏告警（qWarning 口径：只报告、不中断）。 */
#define GSC_WARN(msg) XPrintf("[XGraphicsScene] %s\n", msg)

/** @brief float 矩形相交（QRectF::intersects：边界相触也算，空矩形恒否）。 */
static bool gsc_rectIntersects(const XRectF* a, const XRectF* b)
{
    float ax2;
    float ay2;
    float bx2;
    float by2;
    if (!a || !b) return false;
    if (a->width <= 0.0f || a->height <= 0.0f) return false;
    if (b->width <= 0.0f || b->height <= 0.0f) return false;
    ax2 = a->x + a->width;
    ay2 = a->y + a->height;
    bx2 = b->x + b->width;
    by2 = b->y + b->height;
    return (a->x <= bx2 && ax2 >= b->x && a->y <= by2 && ay2 >= b->y);
}

/** @brief float 矩形含点（QRectF::contains(QPointF)：left<=x<right）。 */
static bool gsc_rectContainsPoint(const XRectF* r, const XPointF* p)
{
    if (!r || !p) return false;
    if (r->width <= 0.0f || r->height <= 0.0f) return false;
    return (p->x >= r->x && p->x < r->x + r->width && p->y >= r->y &&
            p->y < r->y + r->height);
}

/** @brief float 矩形含矩形（QRectF::contains(QRectF)：包住且非退化）。 */
static bool gsc_rectContainsRect(const XRectF* outer, const XRectF* inner)
{
    if (!outer || !inner) return false;
    if (inner->width <= 0.0f || inner->height <= 0.0f) return false;
    if (outer->width <= 0.0f || outer->height <= 0.0f) return false;
    return (inner->x >= outer->x && inner->y >= outer->y &&
            inner->x + inner->width <= outer->x + outer->width &&
            inner->y + inner->height <= outer->y + outer->height);
}

/** @brief float 矩形相等（比较用；QRectF::operator== 精确口径）。 */
static bool gsc_rectEqual(const XRectF* a, const XRectF* b)
{
    if (!a || !b) return false;
    return a->x == b->x && a->y == b->y && a->width == b->width &&
           a->height == b->height;
}

/** @brief 浮点矩形并集（空矩形跳过；与 XGraphicsItem.c 的
 *  gitem_unitedRect 同式——文件本地副本，跨文件不可见）。 */
static void gsc_unitedRect(XRectF* rect, const XRectF* other)
{
    float x1;
    float y1;
    float x2;
    float y2;
    if (rect->width <= 0.0f || rect->height <= 0.0f) {
        *rect = *other;
        return;
    }
    if (other->width <= 0.0f || other->height <= 0.0f) return;
    x1 = rect->x < other->x ? rect->x : other->x;
    y1 = rect->y < other->y ? rect->y : other->y;
    x2 = (rect->x + rect->width) > (other->x + other->width)
             ? (rect->x + rect->width)
             : (other->x + other->width);
    y2 = (rect->y + rect->height) > (other->y + other->height)
             ? (rect->y + rect->height)
             : (other->y + other->height);
    rect->x = x1;
    rect->y = y1;
    rect->width = x2 - x1;
    rect->height = y2 - y1;
}

/** @brief _q_adjustRect（qgraphicsscene_p.h:312-318 原样：QRectF::intersects
 *  对零宽高恒 false 的修补）。 */
static void gsc_adjustRect(XRectF* rect)
{
    if (!rect) return;
    if (rect->width == 0.0f) {
        rect->x -= GSC_ADJUST_EPS;
        rect->width += 2.0f * GSC_ADJUST_EPS;
    }
    if (rect->height == 0.0f) {
        rect->y -= GSC_ADJUST_EPS;
        rect->height += 2.0f * GSC_ADJUST_EPS;
    }
}

/** @brief 浮点坐标 → 事件整数坐标（XPoint 承载；就近取整）。 */
static XPoint gsc_toPoint(float x, float y)
{
    XPoint p;
    p.x = (int)lrintf(x);
    p.y = (int)lrintf(y);
    return p;
}

/** @brief float 矩形 → int 矩形（路径 int 原语的入参转换；边向外扩半格
 *  取整，避免边界假阴性——XPainterPath 的矩形原语是 int 口径）。 */
static XRect gsc_toRect(const XRectF* r)
{
    XRect out;
    out.x = (int)floorf(r->x - 0.5f);
    out.y = (int)floorf(r->y - 0.5f);
    out.width = (int)ceilf(r->x + r->width + 0.5f) - out.x;
    out.height = (int)ceilf(r->y + r->height + 0.5f) - out.y;
    return out;
}

/** @brief item 的场景包围盒（boundingRect 经 sceneTransform 映射后做
 *  _q_adjustRect 修补——Qt intersect_* 的统一前置口径）。 */
static void gsc_sceneBRect(const XGraphicsItem* item, XRectF* out)
{
    *out = XGraphicsItem_sceneBoundingRect(item);
    gsc_adjustRect(out);
}

/* ==================== 生命周期 ==================== */

static void VXGraphicsScene_deinit(XGraphicsScene* self);

XVtable* XGraphicsScene_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsScene)
    XVTABLE_INHERIT_XCLASS(XObject);
    /* 本子集无新业务虚槽位（场景不是被派生重写的对象，事件/通知都在
     * item 侧）；Deinit 必须覆写（释放顶层表容器 + 父类清尾）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGraphicsScene_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief Deinit 槽位：先 clear 删全部在册顶层项（含子树），再释放顶层表
 *  容器与父类尾巴。**【复核修订 2026-11-16】**对齐 Qt ~QGraphicsScene
 *  （qgraphicsscene.cpp:1661：析构体调 clear()；:1648-1650 文档原文
 *  "Removes and deletes all items from the scene object before destroying
 *  the scene object"——scene 析构**删除**仍存在的 item，此前版本不删项
 *  是对 Qt 的偏离，已按原文收口）。 */
static void VXGraphicsScene_deinit(XGraphicsScene* self)
{
    if (!self) return;
    XGraphicsScene_clear(self); /* 删全部在册顶层项（先前 remove 的项不在
                                   其列——所有权已归调用方，Qt 同）。 */
    if (self->m_topLevelItems) {
        XClassDelete(self->m_topLevelItems);
        self->m_topLevelItems = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XGraphicsScene_init(XGraphicsScene* self)
{
    if (!self) return;
    /* AGENTS.md 铁律：init 全字段显式清零（MSVC /RTC1 栈垃圾防线）。 */
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XGraphicsScene);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_topLevelItems = NULL;
    self->m_sceneRect.x = 0.0f;
    self->m_sceneRect.y = 0.0f;
    self->m_sceneRect.width = 0.0f;
    self->m_sceneRect.height = 0.0f;
    self->m_focusItem = NULL;
    self->m_lastHoverItem = NULL;
    self->m_mouseGrabberItem = NULL;
    self->m_mouseDownScenePos.x = 0.0f;
    self->m_mouseDownScenePos.y = 0.0f;
    self->m_dragInitialPos.x = 0.0f;
    self->m_dragInitialPos.y = 0.0f;
    self->m_hasDragAnchor = false;
    self->m_lastEventScenePos.x = 0.0f;
    self->m_lastEventScenePos.y = 0.0f;
    self->m_selectionChanging = 0;
    self->m_stickyFocus = false;
    self->m_dirtyAll = false;
    self->m_dirtyRect.x = 0.0f;
    self->m_dirtyRect.y = 0.0f;
    self->m_dirtyRect.width = 0.0f;
    self->m_dirtyRect.height = 0.0f;
}

XGraphicsScene* XGraphicsScene_create_ex(XMemoryType memory)
{
    XGraphicsScene* self = (XGraphicsScene*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGraphicsScene_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 场景矩形 ==================== */

XRectF XGraphicsScene_sceneRect(const XGraphicsScene* self)
{
    if (!self) {
        XRectF empty;
        empty.x = 0.0f;
        empty.y = 0.0f;
        empty.width = 0.0f;
        empty.height = 0.0f;
        return empty;
    }
    if (self->m_sceneRect.width <= 0.0f || self->m_sceneRect.height <= 0.0f)
        return XGraphicsScene_itemsBoundingRect(self);
    return self->m_sceneRect;
}

void XGraphicsScene_setSceneRect(XGraphicsScene* self, const XRectF* rect)
{
    XRectF zero;
    const XRectF* r = rect;
    if (!self) return;
    if (!r) {
        zero.x = 0.0f;
        zero.y = 0.0f;
        zero.width = 0.0f;
        zero.height = 0.0f;
        r = &zero;
    }
    if (self->m_sceneRect.x == r->x && self->m_sceneRect.y == r->y &&
        self->m_sceneRect.width == r->width &&
        self->m_sceneRect.height == r->height)
        return; /* 同值早退（Qt setSceneRect 同）。 */
    self->m_sceneRect = *r;
    XGraphicsScene_updateRect(self, r); /* 场景矩形变化 ⇒ 标脏（Qt 同）。 */
}

XRectF XGraphicsScene_itemsBoundingRect(const XGraphicsScene* self)
{
    XRectF out;
    size_t i;
    if (!self) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.width = 0.0f;
        out.height = 0.0f;
        return out;
    }
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = 0.0f;
    out.height = 0.0f;
    if (!self->m_topLevelItems) return out;
    /* 全部项（含子项链）的并集：顶层自身包围盒 + 子树包围盒（子树矩形
     * 是项局部坐标，经项映射到场景）。对标 Qt index 的
     * growingItemsBoundingRect（不分可见性）。 */
    for (i = 0; i < XContainerSize(self->m_topLevelItems); ++i) {
        XGraphicsItem* item =
            *(XGraphicsItem**)XVector_at_base(self->m_topLevelItems,
                                              (int64_t)i);
        XRectF br;
        if (!item) continue;
        br = XGraphicsItem_sceneBoundingRect(item);
        gsc_unitedRect(&out, &br);
        br = XGraphicsItem_childrenBoundingRect(item);
        br = XGraphicsItem_mapRectToScene(item, &br);
        gsc_unitedRect(&out, &br);
    }
    return out;
}

/* ==================== 顶层登记 ==================== */

/** @brief 顶层登记（对标 registerTopLevelItem：表尾插入序）。 */
static bool gsc_registerTopLevel(XGraphicsScene* self, XGraphicsItem* item)
{
    if (!self->m_topLevelItems) {
        self->m_topLevelItems = XVector_Create(XGraphicsItem*);
        if (!self->m_topLevelItems) return false;
    }
    return XVector_insert_2(self->m_topLevelItems,
                            (int64_t)XContainerSize(self->m_topLevelItems),
                            &item);
}

/** @brief 顶层摘除（对标 unregisterTopLevelItem；找不到不动作）。 */
static void gsc_unregisterTopLevel(XGraphicsScene* self, XGraphicsItem* item)
{
    size_t i;
    size_t n;
    if (!self->m_topLevelItems || !item) return;
    n = XContainerSize(self->m_topLevelItems);
    for (i = 0; i < n; ++i) {
        if (*(XGraphicsItem**)XVector_at_base(self->m_topLevelItems,
                                              (int64_t)i) == item) {
            XVector_remove_base(self->m_topLevelItems, (int64_t)i, 1);
            return;
        }
    }
}

/** @brief item 是否在 target 的子树内（含相等）。 */
static bool gsc_inSubtree(const XGraphicsItem* target, const XGraphicsItem* item)
{
    const XGraphicsItem* p;
    if (!target || !item) return false;
    p = item;
    while (p) {
        if (p == target) return true;
        p = p->m_parentItem;
    }
    return false;
}

/** @brief 清理场景对该项子树的引用（焦点/抓取/悬停/拖动锚点）。 */
static void gsc_clearItemRefs(XGraphicsScene* self, XGraphicsItem* item,
                              bool sendFocusOut)
{
    if (gsc_inSubtree(item, self->m_focusItem)) {
        XGraphicsItem* old = self->m_focusItem;
        self->m_focusItem = NULL;
        if (sendFocusOut) {
            XEvent fo;
            XEvent_init(&fo, XEVENT_TYPE_FOCUS_OUT);
            (void)XGraphicsScene_sendEvent(self, old, &fo);
        }
    }
    if (gsc_inSubtree(item, self->m_mouseGrabberItem)) {
        self->m_mouseGrabberItem = NULL;
        self->m_hasDragAnchor = false;
    }
    if (gsc_inSubtree(item, self->m_lastHoverItem)) self->m_lastHoverItem = NULL;
}

/* ==================== 选中收口 ==================== */

void XGraphicsScene_itemSelectedChangedInternal(XGraphicsScene* scene,
                                                XGraphicsItem* item)
{
    /* 对标 QGraphicsItem::setSelected 中段（qgraphicsitem.cpp:2757-2767）：
     * 批量闸内静默，闸外即时发信号。 */
    (void)item;
    if (!scene || scene->m_selectionChanging > 0) return;
    XGraphicsScene_selectionChanged_signal(scene);
}

void* XGraphicsScene_selectionChanged_signal(XGraphicsScene* self)
{
    /* 无参数信号（Qt QGraphicsScene::selectionChanged 原样）。 */
    XEmitSignal(self, XGraphicsScene_selectionChanged_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

/* ==================== 查询遍历（对标 recursive_items_helper） ============ */

/** @brief 查询上下文。 */
typedef struct GscQuery
{
    int kind;       /* 0=全部 1=点 2=矩形 3=路径。 */
    XPointF point;  /* kind=1。 */
    XRectF rect;    /* kind=2（场景坐标，未修补）。 */
    const XPainterPath* path; /* kind=3（场景坐标）。 */
    XRectF pathBBox;          /* kind=3 预筛用（controlPointRect）。 */
    XGraphicsItemSelectionMode mode;
    XVector* out;
} GscQuery;

/** @brief 子项按堆叠序排序（z 升序 + siblingIndex 插入序；与
 *  XGraphicsItem_childItems 的排序键同式，就地排避免分配——Qt
 *  ensureSortedChildren 的懒排序同口径）。 */
static void gsc_sortChildren(XGraphicsItem* item)
{
    size_t n;
    size_t i;
    size_t j;
    XGraphicsItem** arr;
    if (!item || !item->m_childItems) return;
    n = XContainerSize(item->m_childItems);
    arr = (XGraphicsItem**)XVector_at_base(item->m_childItems, 0);
    for (i = 1; i < n; ++i) {
        XGraphicsItem* key = arr[i];
        int keyIdx = key->m_siblingIndex;
        j = i;
        while (j > 0) {
            XGraphicsItem* prev = arr[j - 1];
            bool prevGreater =
                (prev->m_z > key->m_z) ||
                (prev->m_z == key->m_z && prev->m_siblingIndex > keyIdx);
            if (!prevGreater) break;
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = key;
    }
}

/** @brief 单项精判（对标 QtPrivate::intersect_point/rect/path +
 *  QGraphicsItem::collidesWithPath）。可见性/透明度已由遍历剔除。 */
static bool gsc_refine(XGraphicsItem* item, const GscQuery* q)
{
    XRectF brect;
    switch (q->kind) {
    case 0: /* items()：无精判。 */
        return true;
    case 1: { /* 点（intersect_point：包围盒预筛 + 局部 contains）。 */
        XRectF probe;
        probe.x = q->point.x;
        probe.y = q->point.y;
        probe.width = 1.0f;
        probe.height = 1.0f;
        gsc_sceneBRect(item, &brect);
        if (!gsc_rectIntersects(&brect, &probe)) return false;
        if (q->mode == XGraphicsItemSelectionMode_ContainsItemBoundingRect ||
            q->mode == XGraphicsItemSelectionMode_IntersectsItemBoundingRect)
            return true; /* 包围盒模式：预筛即判。 */
        /* 形状模式：点映射到局部后经 contains_base 精判（Qt
         * intersect_point 的 keep = item->contains(p) 原式）。 */
        {
            XPointF lp = XGraphicsItem_mapFromScene(item, &q->point);
            return XGraphicsItem_contains_base(item, &lp);
        }
    }
    case 2: { /* 矩形（intersect_rect）。 */
        XRectF localB;
        gsc_sceneBRect(item, &brect);
        /* brect 的局部原值也要（Qt 与查询矩形比较的是局部 adjusted
         * boundingRect——原文 quirk 原样保留）。 */
        localB = XGraphicsItem_boundingRect_base(item);
        gsc_adjustRect(&localB);
        if (q->mode == XGraphicsItemSelectionMode_ContainsItemBoundingRect ||
            q->mode == XGraphicsItemSelectionMode_ContainsItemShape) {
            /* Qt：keep = sceneRect != brect && sceneRect.contains(sceneBRect)
             * （sceneRect 与**局部** brect 的不等比较是 Qt 原文口径）。 */
            if (!gsc_rectContainsRect(&q->rect, &brect)) return false;
            if (gsc_rectEqual(&q->rect, &localB)) return false;
            if (q->mode == XGraphicsItemSelectionMode_ContainsItemBoundingRect)
                return true;
            /* ContainsItemShape：查询矩形（局部坐标）须含项形状——矩形
             * 查询路径即矩形，退化为矩形含形状包围盒（Qt
             * collidesWithPath 的 path.contains(thisShape)；路径-路径
             * 布尔原语 XPainterPath 未提供，以包围盒承载，矩形图元精确）。 */
            {
                XRectF ql = XGraphicsItem_mapRectFromScene(item, &q->rect);
                XPainterPath shape;
                XRectF shapeBox;
                bool keep;
                XPainterPath_init(&shape);
                if (!XGraphicsItem_shape_base(item, &shape)) {
                    XPainterPath_deinit(&shape);
                    return false;
                }
                XPainterPath_boundingRect(&shape, &shapeBox);
                XPainterPath_deinit(&shape);
                keep = gsc_rectContainsRect(&ql, &shapeBox);
                return keep;
            }
        }
        /* Intersects 系：包围盒相交预筛。 */
        if (!gsc_rectIntersects(&q->rect, &brect)) return false;
        if (q->mode == XGraphicsItemSelectionMode_IntersectsItemBoundingRect)
            return true;
        /* IntersectsItemShape：项形状与查询矩形（局部坐标）相交——
         * XPainterPath_intersectsRect（int 口径，边外扩半格取整防边界
         * 假阴性；矩形图元精确，凹形图元可能多报，见头注 W4 收口）。 */
        {
            XRectF ql = XGraphicsItem_mapRectFromScene(item, &q->rect);
            XPainterPath shape;
            XRect intRect;
            bool keep;
            XPainterPath_init(&shape);
            if (!XGraphicsItem_shape_base(item, &shape)) {
                XPainterPath_deinit(&shape);
                return false;
            }
            intRect = gsc_toRect(&ql);
            keep = XPainterPath_intersectsRect(&shape, &intRect);
            XPainterPath_deinit(&shape);
            return keep;
        }
    }
    default: { /* 路径（intersect_path；包围盒承载近似，见头注）。 */
        XRectF pb;
        gsc_sceneBRect(item, &brect);
        if (!gsc_rectIntersects(&q->pathBBox, &brect)) return false;
        if (q->mode == XGraphicsItemSelectionMode_ContainsItemBoundingRect ||
            q->mode == XGraphicsItemSelectionMode_IntersectsItemBoundingRect) {
            /* Qt：path.contains(sceneBRect) / path.intersects(sceneBRect)。
             * XPainterPath 原语是 int 口径：containsRect 用项包围盒、
             * intersectsRect 同。 */
            XRect br = gsc_toRect(&brect);
            if (q->mode == XGraphicsItemSelectionMode_ContainsItemBoundingRect)
                return XPainterPath_containsRect(q->path, &br);
            return XPainterPath_intersectsRect(q->path, &br);
        }
        /* 形状模式：项形状包围盒映射场景坐标后与查询路径判定。 */
        {
            XRectF shapeBoxLocal;
            XRectF shapeBoxScene;
            XPainterPath shape;
            XRect br;
            XPainterPath_init(&shape);
            if (!XGraphicsItem_shape_base(item, &shape)) {
                XPainterPath_deinit(&shape);
                return false;
            }
            XPainterPath_boundingRect(&shape, &shapeBoxLocal);
            XPainterPath_deinit(&shape);
            shapeBoxScene = XGraphicsItem_mapRectToScene(item, &shapeBoxLocal);
            br = gsc_toRect(&shapeBoxScene);
            if (q->mode == XGraphicsItemSelectionMode_ContainsItemShape)
                return XPainterPath_containsRect(q->path, &br);
            return XPainterPath_intersectsRect(q->path, &br);
        }
    }
    }
}

/** @brief 升序遍历（对标 recursive_items_helper 的非裁剪支：项先子后；
 *  隐藏/全透明子树整枝剔除——子链透明度累乘、子项恒随父透明的子集
 *  口径，即 childrenCombineOpacity()=true 的 Qt 分支）。kind=0（纯索引
 *  口径，对标 index->items(order)）不剔除——Qt 的 items(SortOrder) 是
 *  索引全量（含隐藏/透明项；探针 P14 实证：items(order) 含隐藏 E）。 */
static void gsc_visit(XGraphicsItem* item, float parentOpacity, GscQuery* q)
{
    float opacity;
    size_t i;
    size_t n;
    if (!item || !q) return;
    if (q->kind != 0) {
        if (!item->m_visible) return; /* 有效隐藏（子链已同步）⇒ 整枝剔除。 */
        opacity = parentOpacity * item->m_opacity;
        if (opacity < GSC_OPACITY_EPS) return; /* 全透明 ⇒ 整枝剔除。 */
    } else {
        opacity = parentOpacity;
    }
    if (gsc_refine(item, q))
        XVector_insert_2(q->out, (int64_t)XContainerSize(q->out), &item);
    gsc_sortChildren(item);
    n = item->m_childItems ? XContainerSize(item->m_childItems) : 0;
    for (i = 0; i < n; ++i) {
        XGraphicsItem* child =
            *(XGraphicsItem**)XVector_at_base(item->m_childItems, (int64_t)i);
        gsc_visit(child, opacity, q);
    }
}

/** @brief 顶层按堆叠序排序（z 升序；同 z 由稳定插入排序保持**表内插入
 *  序**——对标 topLevelSiblingIndex 的排序键）。 */
static void gsc_sortTopLevels(XGraphicsScene* self, XGraphicsItem** arr,
                              size_t n)
{
    size_t i;
    size_t j;
    (void)self;
    for (i = 1; i < n; ++i) {
        XGraphicsItem* key = arr[i];
        j = i;
        while (j > 0) {
            XGraphicsItem* prev = arr[j - 1];
            if (!(prev->m_z > key->m_z)) break;
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = key;
    }
}

/** @brief 升序收集全部项（供选中/清选遍历；empty 时 out 保持空表）。 */
static void gsc_collectAll(XGraphicsScene* self, XVector* out)
{
    GscQuery q;
    size_t n;
    size_t i;
    if (!self || !out) return;
    XMemset(&q, 0, sizeof(q));
    q.kind = 0;
    q.out = out;
    if (!self->m_topLevelItems || XContainerSize(self->m_topLevelItems) == 0)
        return;
    n = XContainerSize(self->m_topLevelItems);
    {
        XGraphicsItem** tops =
            (XGraphicsItem**)XMemory_malloc(sizeof(XGraphicsItem*) * n,
                                            XCLASS_DEFAULT_MEMORY_TYPE);
        if (!tops) return;
        for (i = 0; i < n; ++i)
            tops[i] = *(XGraphicsItem**)XVector_at_base(self->m_topLevelItems,
                                                        (int64_t)i);
        gsc_sortTopLevels(self, tops, n);
        for (i = 0; i < n; ++i) gsc_visit(tops[i], 1.0f, &q);
        XMemory_free(tops, XCLASS_DEFAULT_MEMORY_TYPE);
    }
}

/** @brief 执行查询：收集升序集（必要时反转为降序）；空集返回 NULL
 *  （四个公开 items* 入口的头注同口径）。 */
static XVector* gsc_runQuery(XGraphicsScene* self, GscQuery* q,
                             XGraphicsSortOrder order)
{
    size_t n;
    size_t i;
    if (!self || !q) return NULL;
    q->out = XVector_Create(XGraphicsItem*);
    if (!q->out) return NULL;
    if (self->m_topLevelItems && XContainerSize(self->m_topLevelItems) > 0) {
        n = XContainerSize(self->m_topLevelItems);
        {
            XGraphicsItem** tops =
                (XGraphicsItem**)XMemory_malloc(sizeof(XGraphicsItem*) * n,
                                                XCLASS_DEFAULT_MEMORY_TYPE);
            if (!tops) {
                XClassDelete(q->out);
                q->out = NULL;
                return NULL;
            }
            for (i = 0; i < n; ++i)
                tops[i] = *(XGraphicsItem**)XVector_at_base(
                    self->m_topLevelItems, (int64_t)i);
            gsc_sortTopLevels(self, tops, n);
            for (i = 0; i < n; ++i) gsc_visit(tops[i], 1.0f, q);
            XMemory_free(tops, XCLASS_DEFAULT_MEMORY_TYPE);
        }
        if (order == XGraphicsSortOrder_DescendingOrder) {
            /* Qt items_helper：降序 = 升序整表反转（原式 swapItemsAt）。 */
            n = XContainerSize(q->out);
            for (i = 0; i < n / 2; ++i) {
                XGraphicsItem* a = *(XGraphicsItem**)XVector_at_base(
                    q->out, (int64_t)i);
                XGraphicsItem* b = *(XGraphicsItem**)XVector_at_base(
                    q->out, (int64_t)(n - 1 - i));
                XVector_replace_1(q->out, (int64_t)i, &b);
                XVector_replace_1(q->out, (int64_t)(n - 1 - i), &a);
            }
        }
    }
    if (XContainerSize(q->out) == 0) {
        XClassDelete(q->out);
        q->out = NULL;
        return NULL;
    }
    return q->out;
}

/* ==================== 索引与命中 ==================== */

XVector* XGraphicsScene_items(const XGraphicsScene* self,
                              XGraphicsSortOrder order)
{
    GscQuery q;
    XMemset(&q, 0, sizeof(q));
    q.kind = 0;
    if (!self) return NULL;
    return gsc_runQuery((XGraphicsScene*)self, &q, order);
}

XVector* XGraphicsScene_itemsAt(const XGraphicsScene* self, const XPointF* pos,
                                XGraphicsItemSelectionMode mode,
                                XGraphicsSortOrder order)
{
    GscQuery q;
    if (!self || !pos) return NULL;
    XMemset(&q, 0, sizeof(q));
    q.kind = 1;
    q.point = *pos;
    q.mode = mode;
    return gsc_runQuery((XGraphicsScene*)self, &q, order);
}

XVector* XGraphicsScene_itemsRect(const XGraphicsScene* self, const XRectF* rect,
                                  XGraphicsItemSelectionMode mode,
                                  XGraphicsSortOrder order)
{
    GscQuery q;
    if (!self || !rect) return NULL;
    XMemset(&q, 0, sizeof(q));
    q.kind = 2;
    q.rect = *rect;
    q.mode = mode;
    return gsc_runQuery((XGraphicsScene*)self, &q, order);
}

XVector* XGraphicsScene_itemsPath(const XGraphicsScene* self,
                                  const XPainterPath* path,
                                  XGraphicsItemSelectionMode mode,
                                  XGraphicsSortOrder order)
{
    GscQuery q;
    if (!self || !path) return NULL;
    XMemset(&q, 0, sizeof(q));
    q.kind = 3;
    q.path = path;
    q.mode = mode;
    XPainterPath_controlPointRect(path, &q.pathBBox);
    return gsc_runQuery((XGraphicsScene*)self, &q, order);
}

XGraphicsItem* XGraphicsScene_itemAt(const XGraphicsScene* self,
                                     const XPointF* pos)
{
    /* 对标 itemAt（qgraphicsscene.cpp:2100-2104）：items(point,
     * IntersectsItemShape, DescendingOrder).first()。 */
    XVector* list = XGraphicsScene_itemsAt(self, pos,
                                           XGraphicsItemSelectionMode_IntersectsItemShape,
                                           XGraphicsSortOrder_DescendingOrder);
    XGraphicsItem* top = NULL;
    if (!list) return NULL;
    if (XContainerSize(list) > 0)
        top = *(XGraphicsItem**)XVector_at_base(list, 0);
    XClassDelete(list);
    return top;
}

/* ==================== item 注册（所有权按 Qt） ==================== */

/** @brief 加入助手（内部递归）。
 *  @param outermost true=公开入口层（信号发射裁决在此）；false=子项层
 *         （批量闸并入外层——Qt 子项 addItem 在父批量闸内，其收尾比较
 *         被 selectionChanging>0 压住不发射）。
 *  @param joined  出参：本子树内**任一**节点入场景时已选中即置位。
 *         【复核修订 2026-11-16】对齐 Qt 选中记账口径
 *         （qgraphicsscene.cpp:2454 记 oldSize、子项各自 :2501
 *         `selectedItems << item`、:2526-2527 收尾比较 size）——子树
 *         节点加入前都不在选中集合中，故「任一节点选中」⇔「集合变大」；
 *         此前版本只按顶层项 isSelected 记账，漏「子树含已选中子项而
 *         顶层未选中」。 */
static void gsc_addItemInner(XGraphicsScene* self, XGraphicsItem* item,
                             bool outermost, bool* joined)
{
    XGraphicsItem* parent;
    const void* vret;
    XGraphicsScene* target;
    if (!item) {
        GSC_WARN("addItem: cannot add null item");
        return;
    }
    if (item->m_scene == self) {
        GSC_WARN("addItem: item has already been added to this scene");
        return;
    }
    /* ③ 属其它场景：先摘离（Qt 原文：Remove this item from its existing
     * scene；整棵子树随之摘离）。 */
    if (item->m_scene) XGraphicsScene_removeItem(item->m_scene, item);
    /* ④ 前置通知 ItemSceneChange：NULL=否决、它场景=改道（Qt 2412-2424）。 */
    vret = XGraphicsItem_itemChange_base(
        item, (int)XGraphicsItemChange_ItemSceneChange, self);
    target = (XGraphicsScene*)vret;
    if (target != self) {
        /* 改道走公开入口：目标场景侧按外层批量语义重新记账（Qt 2422-2424
         * 的 targetScene->addItem 同）。 */
        if (target && item->m_scene != target)
            XGraphicsScene_addItem(target, item);
        return;
    }
    /* ⑤ 跨场景父摘离（Qt 2436-2439：parent 的场景不同 ⇒ detach）。 */
    parent = XGraphicsItem_parentItem(item);
    if (parent && parent->m_scene && parent->m_scene != self)
        XGraphicsItem_setParentItem(item, NULL);
    /* 挂场景回指 + 顶层登记（Qt 2443-2450）。 */
    XGraphicsItem_setSceneInternal(item, self);
    parent = XGraphicsItem_parentItem(item);
    if (!parent && !gsc_registerTopLevel(self, item)) {
        XGraphicsItem_setSceneInternal(item, NULL);
        return;
    }
    /* 批量闸 + 选中记账（外层收尾统一裁决，见函数头注）。 */
    ++self->m_selectionChanging;
    /* 子项递归并入本批量（Qt 2541-2543：for child: addItem(child)——
     * 通知逐级、闸并入外层）。 */
    if (item->m_childItems) {
        size_t n = XContainerSize(item->m_childItems);
        size_t i;
        for (i = 0; i < n; ++i) {
            XGraphicsItem* child =
                *(XGraphicsItem**)XVector_at_base(item->m_childItems,
                                                  (int64_t)i);
            if (child) gsc_addItemInner(self, child, false, joined);
        }
    }
    if (XGraphicsItem_isSelected(item)) *joined = true;
    --self->m_selectionChanging;
    if (outermost && self->m_selectionChanging == 0 && *joined)
        XGraphicsScene_selectionChanged_signal(self);
    /* ⑦ 后置通知 ItemSceneHasChanged（Qt 2533；每节点各自派发）。 */
    (void)XGraphicsItem_itemChange_base(
        item, (int)XGraphicsItemChange_ItemSceneHasChanged, self);
    /* 标脏（Qt 2454 markDirty 同效）。 */
    {
        XRectF dirty = XGraphicsItem_sceneBoundingRect(item);
        XGraphicsScene_updateRect(self, &dirty);
    }
}

void XGraphicsScene_addItem(XGraphicsScene* self, XGraphicsItem* item)
{
    bool joined = false;
    if (!self) return;
    gsc_addItemInner(self, item, true, &joined);
}

/** @brief 摘离助手（对标 removeItemHelper；inDestructor=false 时递归
 *  子项、true 时只处理本项——析构路径子项已各自注销）。 */
static void gsc_removeItemHelper(XGraphicsScene* self, XGraphicsItem* item,
                                 bool inDestructor)
{
    bool wasSelected;
    /* 焦点清理（Qt item->clearFocus() + `if (item == focusItem)`：子树内
     * 焦点一并清——本子集无子焦点链，按子树口径覆盖悬空）。 */
    gsc_clearItemRefs(self, item, true);
    /* 场景回指先摘（Qt：oldScene = item->scene; item->scene = nullptr，
     * 先于子项递归——子项的父摘判定要读父的场景回指）。 */
    XGraphicsItem_setSceneInternal(item, NULL);
    /* 递归摘子（Qt：for child: q->removeItem(child)——公开路径、逐个带
     * 通知；析构路径子项已随级联析构注销，不再递归）。 */
    if (!inDestructor && item->m_childItems) {
        size_t n = XContainerSize(item->m_childItems);
        size_t i;
        for (i = 0; i < n; ++i) {
            XGraphicsItem* child =
                *(XGraphicsItem**)XVector_at_base(item->m_childItems,
                                                  (int64_t)i);
            if (child) XGraphicsScene_removeItem(self, child);
        }
    }
    /* 父摘离：父仍在场景内 ⇒ detach（Qt：if (parentItem->scene())
     * { Q_ASSERT(== q); item->setParentItem(nullptr); }；断言在发布版
     * 不生效，此处放宽为「父属任意场景即摘」，避免跨场景悬挂）。 */
    if (item->m_parentItem && item->m_parentItem->m_scene)
        XGraphicsItem_setParentItem(item, NULL);
    else if (!item->m_parentItem)
        gsc_unregisterTopLevel(self, item);
    /* 选中/悬停记账（Qt：++selectionChanging → selectedItems.remove(item)
     * → 收尾按 size 变化发信号；悬停表 removeAll 同口径）。 */
    ++self->m_selectionChanging;
    wasSelected = XGraphicsItem_isSelected(item);
    --self->m_selectionChanging;
    if (self->m_selectionChanging == 0 && wasSelected)
        XGraphicsScene_selectionChanged_signal(self);
    /* 标脏（Qt markDirty(removingItemFromScene=true) 同效）。 */
    {
        XRectF dirty = XGraphicsItem_sceneBoundingRect(item);
        XGraphicsScene_updateRect(self, &dirty);
    }
}

void XGraphicsScene_removeItem(XGraphicsScene* self, XGraphicsItem* item)
{
    const void* vret;
    XGraphicsScene* target;
    if (!self) return;
    if (!item) {
        GSC_WARN("removeItem: cannot remove 0-item");
        return;
    }
    if (item->m_scene != self) {
        XPrintf("[XGraphicsScene] removeItem: item %p's scene (%p) is "
                "different from this scene (%p)\n",
                (void*)item, (void*)item->m_scene, (void*)self);
        return;
    }
    /* 前置通知 ItemSceneChange（值=NULL）：NULL=继续摘离；它场景=改道加入
     * （Qt 2824-2830 原式）。 */
    vret = XGraphicsItem_itemChange_base(
        item, (int)XGraphicsItemChange_ItemSceneChange, NULL);
    target = (XGraphicsScene*)vret;
    if (target != NULL && target != self) {
        XGraphicsScene_addItem(target, item);
        return;
    }
    gsc_removeItemHelper(self, item, false);
    /* 后置通知 ItemSceneHasChanged（值=NULL；Qt 2834）。 */
    (void)XGraphicsItem_itemChange_base(
        item, (int)XGraphicsItemChange_ItemSceneHasChanged, NULL);
}

void XGraphicsScene_removeItemInternal(XGraphicsScene* scene,
                                       XGraphicsItem* item)
{
    if (!scene || !item || item->m_scene != scene) return;
    /* 析构路径（Qt removeItemHelper 的 inDestructor 分支）：不派发通知、
     * 不递归子项（已随级联析构各自注销）、不删项。 */
    gsc_removeItemHelper(scene, item, true);
}

void XGraphicsScene_clear(XGraphicsScene* self)
{
    if (!self) return;
    /* 对标 clear（qgraphicsscene.cpp:2282-2293）：自顶向下 delete 仍存在
     * 的顶层项（子项经 ~XGraphicsItem 级联析构并各自注销）；其余状态
     * 不动。 */
    while (self->m_topLevelItems && XContainerSize(self->m_topLevelItems) > 0) {
        XGraphicsItem* item =
            *(XGraphicsItem**)XVector_at_base(self->m_topLevelItems, 0);
        if (!item) {
            XVector_remove_base(self->m_topLevelItems, 0, 1);
            continue;
        }
        if (Class_IsHeap(item))
            XClassDelete(item);
        else
            XClassDeinit(item);
        /* 防御：非堆项/异类析构若未注销（XGraphicsItem 之外的表项），手动
         * 摘除防死循环（正常路径 deinit 已摘）。 */
        if (self->m_topLevelItems && XContainerSize(self->m_topLevelItems) > 0) {
            XGraphicsItem* head =
                *(XGraphicsItem**)XVector_at_base(self->m_topLevelItems, 0);
            if (head == item)
                gsc_unregisterTopLevel(self, item);
        }
    }
}

/* ==================== 选中 ==================== */

void XGraphicsScene_setSelectionAreaRect(XGraphicsScene* self,
                                         const XRectF* rect)
{
    XPainterPath path;
    if (!self) return;
    if (!rect) {
        XGraphicsScene_clearSelection(self);
        return;
    }
    /* 矩形→矩形路径（moveTo/lineTo 四点闭合——addRect 是 int 口径，
     * 浮点精度无损构造，与 XGraphicsItem.c 缺省 shape 同式）。 */
    XPainterPath_init(&path);
    XPainterPath_moveTo(&path, rect->x, rect->y);
    XPainterPath_lineTo(&path, rect->x + rect->width, rect->y);
    XPainterPath_lineTo(&path, rect->x + rect->width, rect->y + rect->height);
    XPainterPath_lineTo(&path, rect->x, rect->y + rect->height);
    XPainterPath_closeSubpath(&path);
    XGraphicsScene_setSelectionAreaPath(self, &path);
    XPainterPath_deinit(&path);
}

void XGraphicsScene_setSelectionAreaPath(XGraphicsScene* self,
                                         const XPainterPath* path)
{
    XVector* inPath;
    size_t i;
    size_t n;
    bool changed = false;
    if (!self) return;
    if (!path) {
        XGraphicsScene_clearSelection(self);
        return;
    }
    /* 对标 setSelectionArea（qgraphicsscene.cpp:2194-2245）：批量闸内
     * 路径内可选项 setSelected(true)、其余已选可选项 setSelected(false)、
     * 闸撤后按 changed 发一次信号。 */
    inPath = XGraphicsScene_itemsPath(self, path,
                                      XGraphicsItemSelectionMode_IntersectsItemShape,
                                      XGraphicsSortOrder_DescendingOrder);
    ++self->m_selectionChanging;
    if (inPath) {
        n = XContainerSize(inPath);
        for (i = 0; i < n; ++i) {
            XGraphicsItem* item =
                *(XGraphicsItem**)XVector_at_base(inPath, (int64_t)i);
            if (!item) continue;
            if (!(XGraphicsItem_flags(item) & XGraphicsItemFlag_ItemIsSelectable))
                continue; /* 不可选项不进选中集（Qt 在 setSelected 内强制）。 */
            if (!XGraphicsItem_isSelected(item)) changed = true;
            XGraphicsItem_setSelected(item, true);
        }
    }
    /* ReplaceSelection：路径外的已选项落选（Qt unselectItems 循环）。 */
    {
        XVector* all = XVector_Create(XGraphicsItem*);
        if (all) {
            gsc_collectAll(self, all);
            n = XContainerSize(all);
            for (i = 0; i < n; ++i) {
                XGraphicsItem* item =
                    *(XGraphicsItem**)XVector_at_base(all, (int64_t)i);
                bool inSet = false;
                size_t j;
                size_t m = inPath ? XContainerSize(inPath) : 0;
                if (!item) continue;
                if (!XGraphicsItem_isSelected(item)) continue;
                for (j = 0; j < m; ++j)
                    if (*(XGraphicsItem**)XVector_at_base(inPath,
                                                          (int64_t)j) == item) {
                        inSet = true;
                        break;
                    }
                if (!inSet) {
                    XGraphicsItem_setSelected(item, false);
                    changed = true;
                }
            }
            XClassDelete(all);
        }
    }
    --self->m_selectionChanging;
    if (inPath) XClassDelete(inPath);
    if (self->m_selectionChanging == 0 && changed)
        XGraphicsScene_selectionChanged_signal(self);
}

void XGraphicsScene_clearSelection(XGraphicsScene* self)
{
    XVector* snapshot;
    size_t i;
    size_t n;
    bool changed = false;
    if (!self) return;
    /* 对标 clearSelection（qgraphicsscene.cpp:2248-2276）：批量闸内在
     * **快照**上逐项落选（setSelected 可能经 itemChange 重入选中集合），
     * 被否决落选的项保留；闸撤后按 changed 发一次信号。 */
    ++self->m_selectionChanging;
    snapshot = XVector_Create(XGraphicsItem*);
    if (snapshot) {
        gsc_collectAll(self, snapshot);
        n = XContainerSize(snapshot);
        for (i = 0; i < n; ++i) {
            XGraphicsItem* item =
                *(XGraphicsItem**)XVector_at_base(snapshot, (int64_t)i);
            if (!item || !XGraphicsItem_isSelected(item)) continue;
            XGraphicsItem_setSelected(item, false);
            /* items 可能重入改选（itemChange 否决落选）；仍在选中态则记
             * stillSelected（Qt 同口径）。 */
            if (XGraphicsItem_isSelected(item)) continue;
            changed = true;
        }
        XClassDelete(snapshot);
    }
    --self->m_selectionChanging;
    if (self->m_selectionChanging == 0 && changed)
        XGraphicsScene_selectionChanged_signal(self);
}

XVector* XGraphicsScene_selectedItems(const XGraphicsScene* self)
{
    XVector* all;
    XVector* out;
    size_t i;
    size_t n;
    if (!self) return NULL;
    /* Qt selectedItems 无序；本实现按堆叠升序（确定性特例，头注同口径）。 */
    all = XGraphicsScene_items(self, XGraphicsSortOrder_AscendingOrder);
    if (!all) return NULL;
    out = XVector_Create(XGraphicsItem*);
    if (!out) {
        XClassDelete(all);
        return NULL;
    }
    n = XContainerSize(all);
    for (i = 0; i < n; ++i) {
        XGraphicsItem* item =
            *(XGraphicsItem**)XVector_at_base(all, (int64_t)i);
        if (item && XGraphicsItem_isSelected(item))
            XVector_insert_2(out, (int64_t)XContainerSize(out), &item);
    }
    XClassDelete(all);
    if (XContainerSize(out) == 0) {
        XClassDelete(out);
        return NULL;
    }
    return out;
}

/* ==================== 焦点 ==================== */

/** @brief 焦点收口（对标 setFocusItemHelper：旧焦 FocusOut → 摘位 →
 *  新焦挂位 → FocusIn；同项早退）。 */
static void gsc_setFocusHelper(XGraphicsScene* self, XGraphicsItem* item)
{
    XGraphicsItem* old;
    if (item == self->m_focusItem) return;
    old = self->m_focusItem;
    if (old) {
        XEvent fo;
        self->m_focusItem = NULL;
        XEvent_init(&fo, XEVENT_TYPE_FOCUS_OUT);
        (void)XGraphicsScene_sendEvent(self, old, &fo);
    }
    if (item) {
        XEvent fi;
        self->m_focusItem = item;
        XEvent_init(&fi, XEVENT_TYPE_FOCUS_IN);
        (void)XGraphicsScene_sendEvent(self, item, &fi);
    }
}

void XGraphicsScene_setFocusItem(XGraphicsScene* self, XGraphicsItem* item)
{
    if (!self) return;
    /* 对标 setFocusItem → setFocusItemHelper 首段（qgraphicsscene.cpp
     * 2912-2916）：不可聚焦/不可见 ⇒ 只清焦（enabled 恒真）。 */
    if (item && (!(XGraphicsItem_flags(item) & XGraphicsItemFlag_ItemIsFocusable) ||
                 !XGraphicsItem_isVisible(item)))
        item = NULL;
    gsc_setFocusHelper(self, item);
}

XGraphicsItem* XGraphicsScene_focusItem(const XGraphicsScene* self)
{
    if (!self) return NULL;
    return self->m_focusItem;
}

void XGraphicsScene_clearFocus(XGraphicsScene* self)
{
    if (!self) return;
    gsc_setFocusHelper(self, NULL);
}

void XGraphicsScene_setStickyFocus(XGraphicsScene* self, bool enabled)
{
    if (!self) return;
    self->m_stickyFocus = enabled;
}

bool XGraphicsScene_stickyFocus(const XGraphicsScene* self)
{
    if (!self) return false;
    return self->m_stickyFocus;
}

/* ==================== 事件分发 ==================== */

bool XGraphicsScene_sendEvent(XGraphicsScene* self, XGraphicsItem* item,
                              XEvent* event)
{
    /* 对标 QGraphicsScenePrivate::sendEvent（过滤/禁用不在本子集）：
     * 「递达」经 item 的 Event 虚槽。 */
    if (!self || !item || !event) return false;
    return XGraphicsItem_event_base(item, event);
}

/** @brief 悬停事件投递（XEnterEvent 承载：m_position=项局部、
 *  m_scenePosition=场景坐标；type=GRAPHICS_SCENE_HOVER_*）。 */
static void gsc_sendHover(XGraphicsScene* self, XEventType type,
                          XGraphicsItem* item, const XPointF* scenePos)
{
    XEnterEvent he;
    XPointF lp;
    XPoint l;
    XPoint s;
    XPoint g;
    if (!self || !item || !scenePos) return;
    lp = XGraphicsItem_mapFromScene(item, scenePos);
    l = gsc_toPoint(lp.x, lp.y);
    s = gsc_toPoint(scenePos->x, scenePos->y);
    g.x = 0;
    g.y = 0;
    XEnterEvent_init(&he, type, &l, &g);
    he.m_scenePosition = s;
    (void)XGraphicsScene_sendEvent(self, item, (XEvent*)&he);
}

/** @brief 悬停派发（对标 dispatchHoverEvent 的单项承载：目标切换 ⇒
 *  旧 Leave、新 Enter；随后目标 Move）。 */
static bool gsc_hoverDispatch(XGraphicsScene* self, const XPointF* scenePos)
{
    XVector* candidates;
    XGraphicsItem* target = NULL;
    size_t i;
    size_t n;
    if (!self || !scenePos) return false;
    candidates = XGraphicsScene_itemsAt(self, scenePos,
                                        XGraphicsItemSelectionMode_IntersectsItemShape,
                                        XGraphicsSortOrder_DescendingOrder);
    if (candidates) {
        n = XContainerSize(candidates);
        for (i = 0; i < n; ++i) {
            XGraphicsItem* item =
                *(XGraphicsItem**)XVector_at_base(candidates, (int64_t)i);
            if (item && XGraphicsItem_acceptHoverEvents(item)) {
                target = item;
                break;
            }
        }
        XClassDelete(candidates);
    }
    if (target != self->m_lastHoverItem) {
        if (self->m_lastHoverItem)
            gsc_sendHover(self, XEVENT_TYPE_GRAPHICS_SCENE_HOVER_LEAVE,
                          self->m_lastHoverItem, scenePos);
        self->m_lastHoverItem = target;
        if (target)
            gsc_sendHover(self, XEVENT_TYPE_GRAPHICS_SCENE_HOVER_ENTER,
                          target, scenePos);
    }
    if (target) {
        gsc_sendHover(self, XEVENT_TYPE_GRAPHICS_SCENE_HOVER_MOVE, target,
                      scenePos);
        return true;
    }
    return false;
}

bool XGraphicsScene_hoverMoveEvent(XGraphicsScene* self, const XPointF* scenePos)
{
    return gsc_hoverDispatch(self, scenePos);
}

void XGraphicsScene_hoverLeaveEvent(XGraphicsScene* self)
{
    XPointF last;
    if (!self || !self->m_lastHoverItem) return;
    /* 对标 leaveScene：指针离开视口 ⇒ 悬停项收 Leave、跟踪清空。 */
    last = self->m_lastEventScenePos;
    gsc_sendHover(self, XEVENT_TYPE_GRAPHICS_SCENE_HOVER_LEAVE,
                  self->m_lastHoverItem, &last);
    self->m_lastHoverItem = NULL;
}

void XGraphicsScene_mousePressEvent(XGraphicsScene* self, XMouseEvent* event)
{
    XPointF scenePos;
    XVector* candidates;
    bool setFocus = false;
    bool accepted = false;
    size_t i;
    size_t n;
    if (!self || !event) return;
    scenePos.x = (float)event->m_position.x;
    scenePos.y = (float)event->m_position.y;
    self->m_lastEventScenePos = scenePos;
    /* 按下先派悬停（Qt mousePressEvent 首段；有抓取项时不派）。 */
    if (!self->m_mouseGrabberItem)
        (void)gsc_hoverDispatch(self, &scenePos);
    if (self->m_mouseGrabberItem) {
        /* 已有抓取项：直送（Qt mousePressEventHandler 首支；抓取中的
         * accepted 状态不外泄——Qt 同口径 disregard）。 */
        XGraphicsItem* grabber = self->m_mouseGrabberItem;
        XPointF lp = XGraphicsItem_mapFromScene(grabber, &scenePos);
        event->m_position = gsc_toPoint(lp.x, lp.y);
        XEvent_setAccepted_base((XEvent*)event, true);
        (void)XGraphicsScene_sendEvent(self, grabber, (XEvent*)event);
        return;
    }
    /* 命中候选（Qt itemsAtPosition：IntersectsItemShape 降序）。 */
    candidates = XGraphicsScene_itemsAt(self, &scenePos,
                                        XGraphicsItemSelectionMode_IntersectsItemShape,
                                        XGraphicsSortOrder_DescendingOrder);
    /* 顶层首个可聚焦可见项随按下获焦（Qt 1344-1368：候选降序找
     * focusable；ItemStopsFocusHandling/panel 支不在子集）。 */
    if (candidates) {
        n = XContainerSize(candidates);
        for (i = 0; i < n; ++i) {
            XGraphicsItem* item =
                *(XGraphicsItem**)XVector_at_base(candidates, (int64_t)i);
            if (item && (XGraphicsItem_flags(item) &
                         XGraphicsItemFlag_ItemIsFocusable)) {
                setFocus = true;
                if (item != self->m_focusItem)
                    XGraphicsScene_setFocusItem(self, item);
                break;
            }
        }
    }
    /* 无人获焦且非粘性 ⇒ 清焦（Qt 1381-1382）。 */
    if (!self->m_stickyFocus && !setFocus)
        XGraphicsScene_setFocusItem(self, NULL);
    /* 隐式抓取循环：候选降序逐个抓取并发事件，accepted 即止（Qt
     * 1388-1453；acceptedMouseButtons 本子集恒全接受，无按钮过滤支）。 */
    if (candidates) {
        n = XContainerSize(candidates);
        for (i = 0; i < n; ++i) {
            XGraphicsItem* item =
                *(XGraphicsItem**)XVector_at_base(candidates, (int64_t)i);
            XPointF lp;
            if (!item) continue;
            self->m_mouseGrabberItem = item;
            self->m_mouseDownScenePos = scenePos;
            self->m_dragInitialPos = XGraphicsItem_pos(item);
            self->m_hasDragAnchor = true;
            XEvent_setAccepted_base((XEvent*)event, true);
            lp = XGraphicsItem_mapFromScene(item, &scenePos);
            event->m_position = gsc_toPoint(lp.x, lp.y);
            (void)XGraphicsScene_sendEvent(self, item, (XEvent*)event);
            if (XEvent_isAccepted((XEvent*)event)) {
                accepted = true;
                break;
            }
            /* 拒绝 ⇒ 撤抓取，尝试下一候选（Qt ungrabMouse 同支）。 */
            self->m_mouseGrabberItem = NULL;
            self->m_hasDragAnchor = false;
        }
        XClassDelete(candidates);
    }
    /* 无人接受 ⇒ 清抓取+清选中（Qt 尾段 1458-1476；ScrollHandDrag/
     * Ctrl 扩选例外属视图侧，不在子集）。 */
    if (!accepted) {
        self->m_mouseGrabberItem = NULL;
        self->m_hasDragAnchor = false;
        XGraphicsScene_clearSelection(self);
    }
}

void XGraphicsScene_mouseMoveEvent(XGraphicsScene* self, XMouseEvent* event)
{
    XPointF scenePos;
    if (!self || !event) return;
    scenePos.x = (float)event->m_position.x;
    scenePos.y = (float)event->m_position.y;
    self->m_lastEventScenePos = scenePos;
    if (!self->m_mouseGrabberItem) {
        /* 无抓取：有键按下静默返回；无键 ⇒ 悬停（Qt mouseMoveEvent）。 */
        if (event->m_buttons) return;
        (void)gsc_hoverDispatch(self, &scenePos);
        return;
    }
    {
        XGraphicsItem* grabber = self->m_mouseGrabberItem;
        XPointF lp = XGraphicsItem_mapFromScene(grabber, &scenePos);
        event->m_position = gsc_toPoint(lp.x, lp.y);
        (void)XGraphicsScene_sendEvent(self, grabber, (XEvent*)event);
        XEvent_setAccepted_base((XEvent*)event, true); /* 抓取即消费（Qt 同）。 */
    }
}

void XGraphicsScene_mouseReleaseEvent(XGraphicsScene* self, XMouseEvent* event)
{
    XPointF scenePos;
    XGraphicsItem* grabber;
    if (!self || !event) return;
    scenePos.x = (float)event->m_position.x;
    scenePos.y = (float)event->m_position.y;
    self->m_lastEventScenePos = scenePos;
    grabber = self->m_mouseGrabberItem;
    if (!grabber) {
        XEvent_setAccepted_base((XEvent*)event, false);
        return;
    }
    {
        XPointF lp = XGraphicsItem_mapFromScene(grabber, &scenePos);
        event->m_position = gsc_toPoint(lp.x, lp.y);
        (void)XGraphicsScene_sendEvent(self, grabber, (XEvent*)event);
        XEvent_setAccepted_base((XEvent*)event, true);
    }
    /* 全键松开 ⇒ 撤隐式抓取并按松开位置派一轮悬停（Qt 尾段；拖动锚点的
     * 清除在缺省 release handler——Qt 同口径，派生类必须链回缺省）。 */
    if (event->m_buttons == (XMouseButton)XMouseButton_NoButton) {
        self->m_mouseGrabberItem = NULL;
        (void)gsc_hoverDispatch(self, &scenePos);
    }
}

XGraphicsItem* XGraphicsScene_mouseGrabberItem(const XGraphicsScene* self)
{
    if (!self) return NULL;
    return self->m_mouseGrabberItem;
}

/** @brief 键事件传播（对标 keyPressEvent/keyReleaseEvent 同构 do-while：
 *  accept 置位 → 派发 → !accepted 且有父则上溯；面板支不在子集）。 */
static void gsc_keyEvent(XGraphicsScene* self, XKeyEvent* event)
{
    XGraphicsItem* item;
    XGraphicsItem* p;
    if (!self || !event) return;
    item = self->m_focusItem;
    if (!item) {
        XEvent_setAccepted_base((XEvent*)event, false);
        return;
    }
    p = item;
    do {
        XEvent_setAccepted_base((XEvent*)event, true);
        if (!XGraphicsScene_sendEvent(self, p, (XEvent*)event)) break;
    } while (!XEvent_isAccepted((XEvent*)event) &&
             (p = XGraphicsItem_parentItem(p)) != NULL);
}

void XGraphicsScene_keyPressEvent(XGraphicsScene* self, XKeyEvent* event)
{
    gsc_keyEvent(self, event);
}

void XGraphicsScene_keyReleaseEvent(XGraphicsScene* self, XKeyEvent* event)
{
    gsc_keyEvent(self, event);
}

void XGraphicsScene_contextMenuEvent(XGraphicsScene* self, XMouseEvent* event)
{
    XPointF scenePos;
    XVector* candidates;
    size_t i;
    size_t n;
    if (!self || !event) return;
    scenePos.x = (float)event->m_position.x;
    scenePos.y = (float)event->m_position.y;
    self->m_lastEventScenePos = scenePos;
    XEvent_setAccepted_base((XEvent*)event, false); /* 缺省忽略（Qt 同）。 */
    candidates = XGraphicsScene_itemsAt(self, &scenePos,
                                        XGraphicsItemSelectionMode_IntersectsItemShape,
                                        XGraphicsSortOrder_DescendingOrder);
    if (!candidates) return;
    n = XContainerSize(candidates);
    for (i = 0; i < n; ++i) {
        XGraphicsItem* item =
            *(XGraphicsItem**)XVector_at_base(candidates, (int64_t)i);
        XPointF lp;
        if (!item) continue;
        lp = XGraphicsItem_mapFromScene(item, &scenePos);
        event->m_position = gsc_toPoint(lp.x, lp.y);
        XEvent_setAccepted_base((XEvent*)event, true);
        if (!XGraphicsScene_sendEvent(self, item, (XEvent*)event)) break;
        if (XEvent_isAccepted((XEvent*)event)) break;
    }
    XClassDelete(candidates);
}

#if XWINDOWEVENT_ON
void XGraphicsScene_wheelEvent(XGraphicsScene* self, XWheelEvent* event)
{
    XPointF scenePos;
    XVector* candidates;
    size_t i;
    size_t n;
    if (!self || !event) return;
    scenePos.x = (float)event->m_position.x;
    scenePos.y = (float)event->m_position.y;
    self->m_lastEventScenePos = scenePos;
    candidates = XGraphicsScene_itemsAt(self, &scenePos,
                                        XGraphicsItemSelectionMode_IntersectsItemShape,
                                        XGraphicsSortOrder_DescendingOrder);
    if (!candidates) {
        XEvent_setAccepted_base((XEvent*)event, false);
        return;
    }
    n = XContainerSize(candidates);
    for (i = 0; i < n; ++i) {
        XGraphicsItem* item =
            *(XGraphicsItem**)XVector_at_base(candidates, (int64_t)i);
        XPointF lp;
        if (!item) continue;
        lp = XGraphicsItem_mapFromScene(item, &scenePos);
        event->m_position = gsc_toPoint(lp.x, lp.y);
        XEvent_setAccepted_base((XEvent*)event, true);
        (void)XGraphicsScene_sendEvent(self, item, (XEvent*)event);
        if (XEvent_isAccepted((XEvent*)event)) break;
    }
    XClassDelete(candidates);
}
#endif /* XWINDOWEVENT_ON */

/* ==================== 图形项效果渲染（W4） ==================== */

/** @brief 单次效果离屏位图的最大边长（防异常包围盒导致越界分配）。 */
#define GSC_EFFECT_MAX_PIXELS 16384

/** @brief 项是否挂着**启用中**的效果（禁用效果走常规绘制路径，Qt 同）。 */
static bool gsc_itemEffectEnabled(const XGraphicsItem* item)
{
    XGraphicsEffect* effect;
    if (!item) return false;
    effect = XGraphicsItem_graphicsEffect_const(item);
    return (effect != NULL) && XGraphicsEffect_isEnabled(effect);
}

/** @brief 祖先链（不含自身）上是否有启用效果。
 *  @details 对标 Qt：QGraphicsScenePrivate::drawItems 只遍历**顶层项**
 *           （qgraphicsscene.cpp:4610 的 drawItems 循环体，取项经
 *           estimateTopLevelItems :4624）再由 drawSubtreeRecursive（:4629）
 *           递归子树；本框架的项列表是扁平的（含子项），故由本判据跳过
 *           "已随祖先效果一起画过"的项，避免子项被画两遍。 */
static bool gsc_hasEffectAncestor(const XGraphicsItem* item)
{
    const XGraphicsItem* parent;
    if (!item) return false;
    parent = XGraphicsItem_parentItem(item);
    while (parent) {
        if (gsc_itemEffectEnabled(parent)) return true;
        parent = XGraphicsItem_parentItem(parent);
    }
    return false;
}

/** @brief 项源包围盒（**项本地坐标**）= boundingRect ∪ childrenBoundingRect。
 *  @details 对标 QGraphicsItemEffectSourcePrivate::boundingRect
 *           （qgraphicsitem.cpp:11002-11021；并集在 11013-11015）。 */
static XRectF gsc_itemSourceBounds(const XGraphicsItem* item)
{
    XRectF out;
    XRectF kids;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = 0.0f;
    out.height = 0.0f;
    if (!item) return out;
    out = XGraphicsItem_boundingRect_base(item);
    kids = XGraphicsItem_childrenBoundingRect(item);
    if (kids.width > 0.0f && kids.height > 0.0f) {
        if (out.width <= 0.0f || out.height <= 0.0f) {
            out = kids;
        } else {
            gsc_unitedRect(&out, &kids);
        }
    }
    return out;
}

/** @brief 按项→画布矩阵绘制**本项自身**（不递归子树；透明度取有效值）。
 *  @details 与 XGraphicsScene_render 的常规项完全同路径（save →
 *           setWorldTransform(combine) → setOpacity(effective) →
 *           XGraphicsItem_paint_base → restore），只是变换由调用方给定。 */
static void gsc_paintItemInCanvas(XGraphicsItem* item, XPainter* painter,
                                  const XImageTransform* itemToCanvas)
{
    if (!item || !painter || !itemToCanvas) return;
    XPainter_save(painter);
    XPainter_setWorldTransform(painter, itemToCanvas, true);
    XPainter_setOpacity(painter, XGraphicsItem_effectiveOpacity(item));
    XGraphicsItem_paint_base(item, painter);
    XPainter_restore(painter);
}

static void gsc_drawSubtree(XGraphicsItem* item, XPainter* painter,
                            const XImageTransform* itemToCanvas,
                            bool applyOwnEffect);

/** @brief 源重绘回调的私有数据（图形项源）：子树根 + 根项本地→**源位图像素**
 *         的矩阵。
 *  @details 关键点（复核修 2026-11-16）：sourcePixmap_2 回调 painter 的用户
 *          坐标是**源位图像素**（其世界变换 = 位图像素→设备，由
 *          `T(-effectRect.tl) ∘ deviceTransform` 承担），而 gsc_drawSubtree
 *          收到的矩阵是「项本地 → 外层画布」。两者差一个
 *          `T(-boundsCanvas.tl)`（画布原点 = 源包围盒左上角，见
 *          gsc_drawItemWithEffect 的 toImage）——少乘它会让源内容整体平移
 *          item 的场景位置。故本槽存**预复合好**的
 *          `itemToCanvas ∘ T(-boundsCanvas.tl)`。
 *          生命周期 = 一次 gsc_drawItemWithEffect 调用（栈上对象，
 *          Draw 派发期间有效）；sourcePixmap_2 只可能在 Draw 内回调它。 */
typedef struct GscEffectSourceSlot
{
    XGraphicsItem* m_item;              /**< 子树根（借用）。 */
    XImageTransform m_itemToSource;     /**< 根项本地 → 源位图像素。 */
} GscEffectSourceSlot;

/** @brief 源重绘回调（对标 Qt 在 sourcePixmap 内回环 scened->draw 的那一步）。 */
static void gsc_effectSourceProc(void* userData, XPainter* painter)
{
    GscEffectSourceSlot* slot = (GscEffectSourceSlot*)userData;
    if (!slot || !painter || !slot->m_item) return;
    gsc_drawSubtree(slot->m_item, painter, &slot->m_itemToSource, false);
}

/**
 * @brief 项挂启用效果时的离屏渲染 + 效果处理 + 回贴（对标
 *        QGraphicsScenePrivate::drawItemHelper 的 effect 分支，
 *        qgraphicsscene.cpp:4726-4761）。
 * @details 管线与 Qt 逐段对应：
 *          ① 源**子树**离屏渲染（含本项与全部后代）：画布坐标 = 本项本地
 *             坐标平移（原点 = 源包围盒左上角映射到画布的左上角）——
 *             等价 Qt `scened->draw(item, &pixmapPainter, ..., &effectTransform, ...)`
 *             把 item 重画进源位图；
 *          ② 输出画布 = boundingRectFor(源画布矩形) 取整（图元局部逻辑
 *             单位，1 单位 = 1 像素）；
 *          ③ `XGraphicsEffect_drawToContext` 派发 Draw 槽（内层可继续
 *             `sourcePixmap_2` 取设备分辨率源 / `drawSource` 取逻辑源）；
 *          ④ 画布回贴到外层画布（画布原点平移 + destRect 偏移），外层
 *             变换继续承载场景变换与 dpr。
 *          项变换（pos/transform/旋转）全程参与：内层绘制用
 *          XGraphicsItem_itemTransform 映射到根项本地坐标，回贴用
 *          「画布原点平移 ∘ 外层变换」——与 Qt 的
 *          `painter->setWorldTransform(transformPtr)` 同口径。
 * @param      item 目标项（含启用效果）；不可为 NULL。
 * @param      painter 外层绘制器（用户坐标 = 画布坐标系 = 场景坐标）。
 * @param      itemToCanvas 本项本地 → 外层画布的矩阵。
 * @param      effect 本项挂接的效果（借用）。
 * @return     true=已按效果路径完整处理（含"包围盒收缩到零面积⇒不落像素"
 *             的整块裁剪口径）；false=无法完成（调用方回退常规路径）。
 */
static bool gsc_drawItemWithEffect(XGraphicsItem* item, XPainter* painter,
                                   const XImageTransform* itemToCanvas,
                                   XGraphicsEffect* effect)
{
    XRectF bounds;        /* 项本地坐标：源包围盒 */
    XRectF boundsCanvas;  /* 外层画布坐标：源包围盒 */
    XRectF srcRectF;      /* 画布坐标（原点即源包围盒左上角） */
    XRectF destRectF;
    XRect srcRect;
    XRect destRect;
    XImage* source = NULL;
    XImage* dest = NULL;
    XPainter imgPainter;
    XGraphicsEffectDrawContext ctx;
    XImageTransform toImage;     /* 外层画布 → 源位图像素（平移 -boundsCanvas.tl） */
    XImageTransform toCanvas;    /* 源位图像素 → 外层画布（平移 +boundsCanvas.tl） */
    XImageTransform device;
    int width;
    int height;
    bool ok = false;

    if (!item || !painter || !itemToCanvas || !effect) return false;
    bounds = gsc_itemSourceBounds(item);
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return false;
    /* 源包围盒映射到外层画布（轴对齐取整），得到源位图的尺寸与位置。
     * 旋转项在这里被"摆正"到画布轴：与 Qt 的 Device 取源一致（位图本身
     * 总是轴对齐的，旋转由回贴时的世界变换承担）。 */
    XGraphicsItem_matrixMapRect(itemToCanvas, &bounds, &boundsCanvas);
    if (!isfinite((double)boundsCanvas.width) ||
        !isfinite((double)boundsCanvas.height))
        return false;
    if (boundsCanvas.width <= 0.0f || boundsCanvas.height <= 0.0f) return false;
    width = (int)ceilf(boundsCanvas.width);
    height = (int)ceilf(boundsCanvas.height);
    if (width <= 0 || height <= 0 || width > GSC_EFFECT_MAX_PIXELS ||
        height > GSC_EFFECT_MAX_PIXELS)
        return false;

    /* ① 源位图：尺寸 = 源包围盒的轴对齐映射尺寸，全透明起手。 */
    source = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!source) return false;
    if (!XImage_reinit_ex(source, width, height,
                          XImageFormat_ARGB32_Premultiplied)) {
        XClassDelete(source);
        return false;
    }
    XImage_fillRect(source, NULL, 0u);
    /* 源位图像素 (0,0) 对应外层画布坐标 boundsCanvas.topLeft ⇒
     * toImage = T(-boundsCanvas.topLeft)（先平移再套外层变换）。
     * 反方向的 toCanvas = T(+boundsCanvas.topLeft) 供设备变换与回贴用。 */
    XGraphicsItem_matrixFromTranslate(&toImage, -boundsCanvas.x, -boundsCanvas.y);
    XGraphicsItem_matrixFromTranslate(&toCanvas, boundsCanvas.x, boundsCanvas.y);
    XPainter_init(&imgPainter, NULL);
    if (!XPainter_begin_image(&imgPainter, source)) {
        XPainter_deinit(&imgPainter);
        XClassDelete(source);
        return false;
    }
    XPainter_setWorldTransform(&imgPainter, &toImage, false);
    /* 源重绘继承外层渲染提示（对标 Qt qgraphicsitem.cpp:11114：把
     * info->painter->renderHints() 传给 pixmapPainter）——否则视图开
     * Antialiasing/SmoothPixmapTransform 时，挂效果项的源内容与无效果
     * 路径渲染不一致（边缘锯齿/位图被点采样）。 */
    {
        XPainterRenderHints hints = XPainter_renderHints(painter);
        if (hints != 0u) {
            XPainter_setRenderHints(&imgPainter,
                                    (XPainterRenderHints)~0u, false);
            XPainter_setRenderHints(&imgPainter, hints, true);
        }
    }
    /* 源渲染**不**再对本项自身施效（否则无限套娃），后代效果照常。 */
    gsc_drawSubtree(item, &imgPainter, itemToCanvas, false);
    XPainter_end(&imgPainter);
    XPainter_deinit(&imgPainter);

    /* ② 输出画布：源画布矩形 (0,0,w,h) 经 boundingRectFor 生长（项本地
     *    逻辑单位；1 单位 = 1 像素，与 Qt 的 LogicalCoordinates 取源同口径）。 */
    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.width = width;
    srcRect.height = height;
    srcRectF.x = 0.0f;
    srcRectF.y = 0.0f;
    srcRectF.width = (float)width;
    srcRectF.height = (float)height;
    destRectF = XGraphicsEffect_boundingRectFor_base(effect, &srcRectF);
    if (!isfinite((double)destRectF.width) || !isfinite((double)destRectF.height)) {
        XClassDelete(source);
        return false;
    }
    if (destRectF.width <= 0.0f || destRectF.height <= 0.0f) {
        /* 效果把源收缩到零面积（如 blurRadius 极负）：整块裁剪，不落像素。
         * 对标 Qt（本机实测 QGraphicsBlurEffect(-20) 渲染后父级无像素）。 */
        XClassDelete(source);
        return true;
    }
    destRect.x = (int)floorf(destRectF.x);
    destRect.y = (int)floorf(destRectF.y);
    destRect.width = (int)ceilf(destRectF.x + destRectF.width) - destRect.x;
    destRect.height = (int)ceilf(destRectF.y + destRectF.height) - destRect.y;
    if (destRect.width <= 0 || destRect.height <= 0) {
        XClassDelete(source);
        return true;
    }
    if (destRect.width > GSC_EFFECT_MAX_PIXELS ||
        destRect.height > GSC_EFFECT_MAX_PIXELS) {
        XClassDelete(source);
        return false;
    }
    dest = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!dest) {
        XClassDelete(source);
        return false;
    }
    if (!XImage_reinit_ex(dest, destRect.width, destRect.height,
                          XImageFormat_ARGB32_Premultiplied)) {
        XClassDelete(dest);
        XClassDelete(source);
        return false;
    }
    XImage_fillRect(dest, NULL, 0u);

    /* ③ 上下文：新字段全填（旧路径只填四字段，本路径必须完整）。 */
    XGraphicsEffectDrawContext_init(&ctx);
    ctx.m_source = source;
    ctx.m_sourceRect = srcRect;
    ctx.m_dest = dest;
    ctx.m_destRect = destRect;
    ctx.m_sourceBounds.x = 0.0f;
    ctx.m_sourceBounds.y = 0.0f;
    ctx.m_sourceBounds.width = (float)width;
    ctx.m_sourceBounds.height = (float)height;
    /* 设备变换 = 源位图像素 → 设备像素（含 dpr）：把 painter 临时切到
     * 「位图像素坐标」（toCanvas 先作用，把位图像素搬回外层画布，再走原
     * world/view/dpr 链）再读 XPainter_deviceTransform（它与真实绘制路径
     * 同源，dpr 折在末端）。 */
    {
        XPainter_save(painter);
        XPainter_setWorldTransform(painter, &toCanvas, true);
        XPainter_deviceTransform(painter, &device);
        XPainter_restore(painter);
    }
    ctx.m_deviceTransform = device;
    ctx.m_hasDeviceTransform = true;
    /* 设备像素比：Qt 的 Device 取源用 `info->painter->device()->
     * devicePixelRatio()` 缩放补边后的位图（qgraphicsitem.cpp:11109-11110），
     * 而 XGui 的 deviceTransform 已把 dpr 折在末端（设备变换 =
     * world × view × dpr）⇒ 把同一个 dpr 交给上下文，sourcePixmap_2 才能
     * 先 ÷dpr 回"设备逻辑空间"求补边（**pad 不随视图缩放放大**，Qt 口径），
     * 再 ×dpr 回物理像素（dpr 的放大语义与 Qt 的 `size() * dpr` 一致）。
     * XPicture 录制器等无 dpr 层 ⇒ 1.0（旧几何）。 */
    ctx.m_devicePixelRatio = XPainter_devicePixelRatio(painter);
    /* 源重绘（sourcePixmap_2 内部）同样继承外层提示（Qt qgraphicsitem.cpp:
     * 11114 同款）：此处的 painter 就是本项的绘制器。 */
    ctx.m_renderHints = (uint32_t)XPainter_renderHints(painter);
    /* 源重绘回调：把子树按「项本地坐标」再画一遍（painter 的用户坐标已由
     * sourcePixmap_2 设成"画布 = 源位图像素"，订阅方只需按自己的画布坐标
     * 继续绘制）。 */
    {
        GscEffectSourceSlot slot;
        slot.m_item = item;
        /* 项本地 → 外层画布 → 源位图像素（toImage 后作用），与
         * gsc_drawSubtree 的 combine 语义（传入矩阵先作用）匹配。 */
        XGraphicsItem_matrixMultiply(&slot.m_itemToSource, itemToCanvas,
                                     &toImage);
        ctx.m_renderSource = gsc_effectSourceProc;
        ctx.m_renderSourceData = &slot;
        ok = XGraphicsEffect_drawToContext(effect, &ctx);
    }

    /* ④ 回贴：位图像素 → 外层画布用 toCanvas（平移 +boundsCanvas.tl），
     *    再按 destRect 偏移绘制（toCanvas 先作用，外层变换继续承载场景
     *    变换与 dpr）。 */
    if (ok) {
        XPainter_save(painter);
        XPainter_setWorldTransform(painter, &toCanvas, true);
        XPainter_setOpacity(painter, 1.0f);
        XPainter_drawImage(painter, dest, destRect.x, destRect.y);
        XPainter_restore(painter);
    }
    XClassDelete(dest);
    XClassDelete(source);
    return ok;
}

/** @brief 子树递归绘制（源离屏渲染专用；含后代与嵌套效果的完整语义）。
 *  @param applyOwnEffect 是否对本项**自身**应用效果。效果源重绘必须传
 *         false：对标 Qt 的 QGraphicsItemEffectSourcePrivate::pixmap →
 *         `scened->draw(item, ...)` 走的是**不带效果分支**的 draw（
 *         qgraphicsscene.cpp:4793 起的 draw()），只有后代的
 *         drawItemHelper 才会再判效果——否则源重绘会无限自我套娃
 *         （本机实测栈溢出段错误）。 */
static void gsc_drawSubtree(XGraphicsItem* item, XPainter* painter,
                            const XImageTransform* itemToCanvas,
                            bool applyOwnEffect)
{
    XGraphicsItem* child;
    XImageTransform childToCanvas;
    XImageTransform childToItem;
    XVector* kids;
    size_t i;
    size_t n;
    if (!item || !painter || !itemToCanvas) return;
    if (!XGraphicsItem_isVisible(item)) return;
    if (applyOwnEffect && gsc_itemEffectEnabled(item)) {
        XGraphicsEffect* effect = XGraphicsItem_graphicsEffect_const(item);
        if (gsc_drawItemWithEffect(item, painter, itemToCanvas, effect)) return;
        /* 效果路径不可用：回退常规绘制（仍是子树一致的结果）。 */
    }
    gsc_paintItemInCanvas(item, painter, itemToCanvas);
    kids = XGraphicsItem_childItems(item);
    if (!kids) return;
    n = XContainerSize(kids);
    for (i = 0; i < n; ++i) {
        child = *(XGraphicsItem**)XVector_at_base(kids, (int64_t)i);
        if (!child) continue;
        /* 子项本地 → 本项本地（XGraphicsItem_itemTransform 全支覆盖）再
         * 复合成子项本地 → 画布。 */
        if (!XGraphicsItem_itemTransform(child, item, &childToItem)) continue;
        XGraphicsItem_matrixMultiply(&childToCanvas, &childToItem, itemToCanvas);
        gsc_drawSubtree(child, painter, &childToCanvas, true);
    }
    XClassDelete(kids);
}


void XGraphicsScene_render(XGraphicsScene* self, XPainter* painter,
                           const XRectF* target)
{
    XRectF source;
    GscQuery q;
    XVector* list;
    XImageTransform base;
    size_t i;
    size_t n;
    if (!self || !painter) return;
    source = target ? *target : XGraphicsScene_sceneRect(self);
    /* 对标 render（qgraphicsscene.cpp:1746-1813）的 1:1 子集：无缩放、
     * 无背景/前景笔刷。收集=items(source, IntersectsItemBoundingRect)
     * 升序（Qt render 的 itemArray 反转前口径）。 */
    q.kind = 2;
    q.rect = source;
    q.mode = XGraphicsItemSelectionMode_IntersectsItemBoundingRect;
    list = gsc_runQuery(self, &q, XGraphicsSortOrder_AscendingOrder);
    /* 【复核修订 2026-11-16】入口 save + clip target + 出口 restore——
     * 对标 Qt render 全程 painter->save()/restore()（:1784/:1826）与
     * setClipRect(targetRect, IntersectClip)（:1786，target 为**设备
     * 坐标**裁剪、在 compose 世界变换**之前**设置）；此前版本把
     * T(-source 左上) compose 进调用方 painter 后不恢复 ⇒ 复用同一
     * painter 的调用方世界变换被永久污染、target 外区域无裁剪。 */
    XPainter_save(painter);
    if (target) {
        /* target 参数在 1:1 子集下兼任设备坐标裁剪矩形（Qt targetRect
         * 同位）；浮点→int 边外扩半格防边界假阴性。 */
        XRect clip = gsc_toRect(target);
        XPainter_setClipRect(painter, &clip,
                             XPainterClipOperation_IntersectClip);
    }
    /* 世界变换：1:1 子集的位移式 = T(target.topLeft − source.topLeft)
     * （Qt painterTransform 在 xratio=yratio=1 时的净效果——把 source
     * 左上摆到 target 左上）；本接口 target 兼任 source（同参 1:1）⇒
     * 恒为单位阵，场景区域**同位**落到设备；target=NULL 回落
     * T(−source 左上)（整个场景矩形画到设备原点起）。compose=true 与
     * 调用方既有变换叠加，出口 restore 全部还原。 */
    if (target)
        XGraphicsItem_matrixFromTranslate(&base, target->x - source.x,
                                          target->y - source.y);
    else
        XGraphicsItem_matrixFromTranslate(&base, -source.x, -source.y);
    XPainter_setWorldTransform(painter, &base, true);
    if (list) {
        n = XContainerSize(list);
        for (i = 0; i < n; ++i) {
            XGraphicsItem* item =
                *(XGraphicsItem**)XVector_at_base(list, (int64_t)i);
            XImageTransform st;
            if (!item) continue;
            /* 祖先挂了启用效果的项由祖先的子树渲染负责（见
             * gsc_hasEffectAncestor 的 Qt 出处）；跳过以防重复绘制。 */
            if (gsc_hasEffectAncestor(item)) continue;
            XGraphicsItem_sceneTransform(item, &st);
            {
                XGraphicsEffect* effect =
                    XGraphicsItem_graphicsEffect_const(item);
                if (effect && XGraphicsEffect_isEnabled(effect) &&
                    gsc_drawItemWithEffect(item, painter, &st, effect))
                    continue;
            }
            XPainter_save(painter);
            /* setWorldTransform(st, true) = st ∘ base（matrix 先作用）：
             * 项局部 → 场景 → 设备（Qt drawItemHelper 的
             * setWorldTransform(painter, transformPtr) 同口径）。 */
            XPainter_setWorldTransform(painter, &st, true);
            XPainter_setOpacity(painter, XGraphicsItem_effectiveOpacity(item));
            XGraphicsItem_paint_base(item, painter);
            XPainter_restore(painter);
        }
        XClassDelete(list);
    }
    XPainter_restore(painter);
}

void XGraphicsScene_update(XGraphicsScene* self)
{
    if (!self) return;
    self->m_dirtyAll = true;
    self->m_dirtyRect.x = 0.0f;
    self->m_dirtyRect.y = 0.0f;
    self->m_dirtyRect.width = 0.0f;
    self->m_dirtyRect.height = 0.0f;
}

void XGraphicsScene_updateRect(XGraphicsScene* self, const XRectF* rect)
{
    if (!self) return;
    if (!rect || rect->width <= 0.0f || rect->height <= 0.0f) {
        XGraphicsScene_update(self);
        return;
    }
    gsc_unitedRect(&self->m_dirtyRect, rect);
}

bool XGraphicsScene_isDirty(const XGraphicsScene* self)
{
    if (!self) return false;
    return self->m_dirtyAll ||
           (self->m_dirtyRect.width > 0.0f && self->m_dirtyRect.height > 0.0f);
}

const XRectF* XGraphicsScene_dirtyRect_const(const XGraphicsScene* self)
{
    if (!self) return NULL;
    if (self->m_dirtyAll) {
        /* 全场景脏：以场景矩形承载（未设置回落 itemsBoundingRect）。 */
        gsc_wholeDirtyRect = XGraphicsScene_sceneRect(self);
        return &gsc_wholeDirtyRect;
    }
    if (self->m_dirtyRect.width > 0.0f && self->m_dirtyRect.height > 0.0f)
        return &self->m_dirtyRect;
    return NULL;
}

void XGraphicsScene_clearDirty(XGraphicsScene* self)
{
    if (!self) return;
    self->m_dirtyAll = false;
    self->m_dirtyRect.x = 0.0f;
    self->m_dirtyRect.y = 0.0f;
    self->m_dirtyRect.width = 0.0f;
    self->m_dirtyRect.height = 0.0f;
}

#endif /* XPAINTER_PATH_ON */

#endif /* XGUI_ON */
