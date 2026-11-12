/******************************************************************************
 * @file       XGraphicsItem.c
 * @brief      XGraphicsItem 图形项基类实现（对标 Qt 6.8 QGraphicsItem）。
 * @details    与同名头文件的公开 API 一一对应。四条虚槽位（BoundingRect/
 *             Paint/Shape/Contains）按 _base 契约分派（槽位空时回落 VX*
 *             缺省实现）；几何/变换沿父链累积（scene = 根项链坐标，不依赖
 *             scene 对象）；父项拥有子项（析构级联删子）；可见性按 Qt 的
 *             visible + explicitlyHidden 双位模型同步下沉。矩阵代数
 *             （XGraphicsItem_matrix* 族）为本模块补齐的 QTransform 最小
 *             对应物，语义逐条对齐 Qt 6.8 qtransform.cpp（乘法行向量口径、
 *             rotate 前乘 / scale 后乘 / translate 前乘）。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XGraphicsItem.h"

#if XPAINTER_PATH_ON

#include "XGuiConfig.h"
#include "XGraphicsEffect.h" /* W4：图形效果挂载（所有权/断源/效果包围盒）。
                                XGraphicsEffect.h 与 XGraphicsItem.h 只互相
                                前向声明，无包含环。 */
#include "XGraphicsScene.h" /* 场景域内部协作：缺省 mousePress/Move 的
                               清选/拖动锚点（XGraphicsItem.h 只前向声明
                               场景类型，不互含——include 方向恒
                               XGraphicsItem.c → XGraphicsScene.h 单向）。 */
#include "XMemory.h"
#include "XVector.h"
#include "XClass.h"
#include <math.h>
#include <stdlib.h>

/* ==================== 矩阵代数（QTransform 对应物） ==================== */

void XGraphicsItem_matrixIdentity(XImageTransform* m)
{
    if (!m) return;
    m->m11 = 1.0f;
    m->m12 = 0.0f;
    m->m21 = 0.0f;
    m->m22 = 1.0f;
    m->dx = 0.0f;
    m->dy = 0.0f;
    m->m13 = 0.0f;
    m->m23 = 0.0f;
    m->m33 = 1.0f;
}

void XGraphicsItem_matrixFromTranslate(XImageTransform* m, float dx, float dy)
{
    if (!m) return;
    XGraphicsItem_matrixIdentity(m);
    m->dx = dx;
    m->dy = dy;
}

void XGraphicsItem_matrixTranslate(XImageTransform* m, float dx, float dy)
{
    /* 对标 QTransform::translate（qtransform.cpp:352）：对 TxScale/TxRotate
     * 均为前乘 T·M（"先平移当前坐标系再套原矩阵"）。仿射域的前乘展开：
     *   (T·M).m11 = T.m11·M.m11 + T.m21·M.m12 = M.m11（T 无旋转剪切项）
     *   (T·M).dx  = T.dx·M.m11 + T.dy·M.m21 + M.dx = M.dx + dx·M.m11
     *   (T·M).dy  = T.dx·M.m12 + T.dy·M.m22 + M.dy = M.dy + dy·M.m22
     * 透视项（m13/m23）同型前乘：T 为纯平移无投影，m13/m23 不变、
     * m33 分母不变（Qt translate 的 TxProject 分支同式）。 */
    if (!m) return;
    m->dx += dx * m->m11 + dy * m->m21;
    m->dy += dx * m->m12 + dy * m->m22;
}

void XGraphicsItem_matrixScale(XImageTransform* m, float sx, float sy)
{
    /* 对标 QTransform::scale（qtransform.cpp:420-448）：**S·M 前乘**——
     * 「先缩放坐标系（点坐标先乘缩放）再套原矩阵」，行向量映射
     * (S·M).map(p) = M(S(p))。仿射域展开：m11/m12 乘 sx、m21/m22 乘 sy，
     * **dx/dy 不动**——Qt 各分支均不改平移分量（【P1 修订 2026-11-16】
     * 原实现误缩放 dx/dy，那是后乘 M·S 的行为；本机 Qt 6.8.0 probe5
     * 实测 translate(5,8).scale(2,2) → m11=2 m22=2 dx=5 dy=8，
     * rotate(30).scale(2,3) → m12×sx/m21×sy 线性项同）。 */
    if (!m) return;
    m->m11 *= sx;
    m->m12 *= sx;
    m->m21 *= sy;
    m->m22 *= sy;
}

void XGraphicsItem_matrixRotate(XImageTransform* m, float deg)
{
    /* 对标 QTransform::rotateRadians（qtransform.cpp:678，Z 轴）：
     * 前乘 R·M，R = [cos, sin; -sin, cos]（正角逆时针；y 向下坐标系呈
     * 顺时针，与 Qt 文档同口径）。仿射域前乘展开（R 无平移项）：
     *   (R·M).m11 = cos·M.m11 + sin·M.m21
     *   (R·M).m12 = cos·M.m12 + sin·M.m22
     *   (R·M).m21 = -sin·M.m11 + cos·M.m21
     *   (R·M).m22 = -sin·M.m12 + cos·M.m22
     * 平移分量 (R·M).dx = M.dx、(R·M).dy = M.dy（R.dx=R.dy=0，Qt 同）。 */
    float rad;
    float sina;
    float cosa;
    float tm11;
    float tm12;
    float tm21;
    float tm22;
    if (!m || deg == 0.0f) return;
    rad = deg * 3.14159265358979323846f / 180.0f;
    sina = sinf(rad);
    cosa = cosf(rad);
    tm11 = cosa * m->m11 + sina * m->m21;
    tm12 = cosa * m->m12 + sina * m->m22;
    tm21 = -sina * m->m11 + cosa * m->m21;
    tm22 = -sina * m->m12 + cosa * m->m22;
    m->m11 = tm11;
    m->m12 = tm12;
    m->m21 = tm21;
    m->m22 = tm22;
}

void XGraphicsItem_matrixMultiply(XImageTransform* out, const XImageTransform* a,
                                  const XImageTransform* b)
{
    /* 对标 QTransform::operator*（qtransform.cpp:907）：(a*b) 的展开式
     * （行向量口径，(a*b).map(p) = b.map(a.map(p))——a 先 b 后）。
     * 内部双精度累加，回写 float（对拍容差 1e-4 相对，float 存储误差
     * ~5e-8 相对，余量充足）。 */
    double am11;
    double am12;
    double am21;
    double am22;
    double adx;
    double ady;
    double bm11;
    double bm12;
    double bm21;
    double bm22;
    double bdx;
    double bdy;
    double rm11;
    double rm12;
    double rm21;
    double rm22;
    double rdx;
    double rdy;
    if (!out || !a || !b) return;
    am11 = a->m11;
    am12 = a->m12;
    am21 = a->m21;
    am22 = a->m22;
    adx = a->dx;
    ady = a->dy;
    bm11 = b->m11;
    bm12 = b->m12;
    bm21 = b->m21;
    bm22 = b->m22;
    bdx = b->dx;
    bdy = b->dy;
    rm11 = am11 * bm11 + am12 * bm21;
    rm12 = am11 * bm12 + am12 * bm22;
    rm21 = am21 * bm11 + am22 * bm21;
    rm22 = am21 * bm12 + am22 * bm22;
    rdx = adx * bm11 + ady * bm21 + bdx;
    rdy = adx * bm12 + ady * bm22 + bdy;
    out->m11 = (float)rm11;
    out->m12 = (float)rm12;
    out->m21 = (float)rm21;
    out->m22 = (float)rm22;
    out->dx = (float)rdx;
    out->dy = (float)rdy;
    /* 透视项：双方均为仿射（m13=m23=0、m33=1）时结果仿射（Qt 仿射乘
     * 仿射同式）；非仿射入参本模块不支持（QGraphicsItem 变换恒仿射）。 */
    out->m13 = 0.0f;
    out->m23 = 0.0f;
    out->m33 = 1.0f;
}

bool XGraphicsItem_matrixInvert(const XImageTransform* in, XImageTransform* out)
{
    /* 对标 QTransform::inverted（qtransform.cpp 仿射支 adjoint/det）：
     * det = m11·m22 - m12·m21；det==0（含近奇异按 Qt 口径不夹）时失败，
     * out 写单位阵、返回 false（Qt 返回 identity 并置 ok=false）。 */
    double det;
    double d11;
    double d12;
    double d21;
    double d22;
    double dx;
    double dy;
    if (!out) return false;
    if (!in) {
        XGraphicsItem_matrixIdentity(out);
        return false;
    }
    det = (double)in->m11 * (double)in->m22 - (double)in->m12 * (double)in->m21;
    if (det == 0.0) {
        XGraphicsItem_matrixIdentity(out);
        return false;
    }
    d11 = (double)in->m22 / det;
    d12 = -(double)in->m12 / det;
    d21 = -(double)in->m21 / det;
    d22 = (double)in->m11 / det;
    dx = ((double)in->m21 * (double)in->dy - (double)in->m22 * (double)in->dx) / det;
    dy = ((double)in->m12 * (double)in->dx - (double)in->m11 * (double)in->dy) / det;
    out->m11 = (float)d11;
    out->m12 = (float)d12;
    out->m21 = (float)d21;
    out->m22 = (float)d22;
    out->dx = (float)dx;
    out->dy = (float)dy;
    out->m13 = 0.0f;
    out->m23 = 0.0f;
    out->m33 = 1.0f;
    return true;
}

void XGraphicsItem_matrixMapPoint(const XImageTransform* m, float x, float y,
                                  XPointF* out)
{
    /* 对标 QTransform::map(QPointF)：x' = m11·x + m21·y + dx（仿射支）。 */
    double dx;
    double dy;
    if (!out) return;
    if (!m) {
        out->x = x;
        out->y = y;
        return;
    }
    dx = (double)m->m11 * (double)x + (double)m->m21 * (double)y + (double)m->dx;
    dy = (double)m->m12 * (double)x + (double)m->m22 * (double)y + (double)m->dy;
    out->x = (float)dx;
    out->y = (float)dy;
}

void XGraphicsItem_matrixMapRect(const XImageTransform* m, const XRectF* rect,
                                 XRectF* out)
{
    /* 对标 QTransform::mapRect(QRectF)：映射四角后取轴对齐包盒
     * （Qt 仿射支把矩形边映射后取 min/max，等价四角包盒）。 */
    float x1;
    float y1;
    float x2;
    float y2;
    float tx1;
    float ty1;
    float tx2;
    float ty2;
    XPointF p;
    if (!out) return;
    if (!m || !rect) {
        out->x = 0.0f;
        out->y = 0.0f;
        out->width = 0.0f;
        out->height = 0.0f;
        return;
    }
    XGraphicsItem_matrixMapPoint(m, rect->x, rect->y, &p);
    x1 = x2 = p.x;
    y1 = y2 = p.y;
    XGraphicsItem_matrixMapPoint(m, rect->x + rect->width, rect->y, &p);
    x1 = p.x < x1 ? p.x : x1;
    y1 = p.y < y1 ? p.y : y1;
    x2 = p.x > x2 ? p.x : x2;
    y2 = p.y > y2 ? p.y : y2;
    XGraphicsItem_matrixMapPoint(m, rect->x, rect->y + rect->height, &p);
    x1 = p.x < x1 ? p.x : x1;
    y1 = p.y < y1 ? p.y : y1;
    x2 = p.x > x2 ? p.x : x2;
    y2 = p.y > y2 ? p.y : y2;
    XGraphicsItem_matrixMapPoint(m, rect->x + rect->width, rect->y + rect->height, &p);
    x1 = p.x < x1 ? p.x : x1;
    y1 = p.y < y1 ? p.y : y1;
    x2 = p.x > x2 ? p.x : x2;
    y2 = p.y > y2 ? p.y : y2;
    tx1 = x1;
    ty1 = y1;
    tx2 = x2;
    ty2 = y2;
    out->x = tx1;
    out->y = ty1;
    out->width = tx2 - tx1;
    out->height = ty2 - ty1;
}

/* ==================== 内部辅助（矩阵/矩形，file-local） ==================== */

static void VXGraphicsItem_deinit(XGraphicsItem* self);

/** @brief item→parent 矩阵：本项变换 ∘ T(pos)（变换先作用、再加位）。
 *  对标 QGraphicsItemPrivate::combineTransformToParent（qgraphicsitem.cpp:
 *  1124-1134）：x *= computedFullTransform(); x *= fromTranslate(pos)——
 *  Qt operator* 口径的后乘（ownT 先作用、T(pos) 后作用），**不是**
 *  QTransform::translate 的前乘（那是「先平移坐标系」语义，方向相反）。 */
static void gitem_itemToParent(const XGraphicsItem* item, XImageTransform* out)
{
    XImageTransform trans;
    XGraphicsItem_matrixFromTranslate(&trans, item->m_pos.x, item->m_pos.y);
    XGraphicsItem_matrixMultiply(out, &item->m_transform, &trans);
}

/** @brief 浮点矩形并集（QRectF::united 口径：空矩形（w/h≤0）跳过）。 */
static void gitem_unitedRect(XRectF* rect, const XRectF* other)
{
    float x1;
    float y1;
    float x2;
    float y2;
    if (rect->width <= 0.0f || rect->height <= 0.0f) {
        *rect = *other;
        return;
    }
    if (other->width <= 0.0f || other->height <= 0.0f) {
        return;
    }
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

/** @brief 父链深度（顶层 0；对标 QGraphicsItemPrivate::depth）。 */
static int gitem_depth(const XGraphicsItem* item)
{
    int depth = 0;
    const XGraphicsItem* p = item->m_parentItem;
    while (p) {
        ++depth;
        p = p->m_parentItem;
    }
    return depth;
}

/** @brief 后代包围盒递归累加（对标 childrenBoundingRectHelper，
 *  qgraphicsitem.cpp:1392-1429：每个后代的 boundingRect 经「后代→本项」
 *  复合矩阵映射进同一累加矩形后取并集；无裁剪标志分支——本子集未含
 *  ItemClipsChildrenToShape，W2 扩标志时补）。
 *  @param toTop item（含其子树基点）到最顶层调用者的复合矩阵；最外层传
 *         NULL（此时 item 即顶层调用者）。 */
static void gitem_childrenRectHelper(const XGraphicsItem* item,
                                     const XImageTransform* toTop, XRectF* rect)
{
    size_t i;
    size_t n = item->m_childItems ? XContainerSize(item->m_childItems) : 0;
    for (i = 0; i < n; ++i) {
        XGraphicsItem* child =
            *(XGraphicsItem**)XVector_at_base(item->m_childItems, (int64_t)i);
        XImageTransform childToItem;
        XImageTransform childToTop;
        XRectF br;
        XRectF mapped;
        if (!child) continue;
        gitem_itemToParent(child, &childToItem);
        if (toTop)
            XGraphicsItem_matrixMultiply(&childToTop, &childToItem, toTop);
        else
            childToTop = childToItem;
        br = XGraphicsItem_boundingRect_base(child);
        XGraphicsItem_matrixMapRect(&childToTop, &br, &mapped);
        gitem_unitedRect(rect, &mapped);
        gitem_childrenRectHelper(child, &childToTop, rect);
    }
}

/* ==================== 虚槽位缺省实现（VX*，对标 Qt 基类行为） ==================== */

/** @brief BoundingRect 缺省：空矩形。Qt 中 boundingRect 为**纯虚**；基类
 *  没有缺省可仿——XGui 以空矩形承载（空项无包围盒），并由 W2 的具体图元
 *  （矩形/椭圆/文本…）提供各自缺省。已登记：派生类必须覆写本槽。 */
static XRectF VXGraphicsItem_boundingRect(const XGraphicsItem* self)
{
    XRectF out;
    (void)self;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = 0.0f;
    out.height = 0.0f;
    return out;
}

/** @brief Paint 缺省：空（对标「无内容图元」——基类不做任何绘制）。 */
static void VXGraphicsItem_paint(XGraphicsItem* self, XPainter* painter)
{
    (void)self;
    (void)painter;
}

/** @brief Shape 缺省：boundingRect 的矩形路径（对标 shape 原文：The
 *  default implementation calls boundingRect() to return a simple
 *  rectangular shape；qgraphicsitem.cpp:4857-4861）。 */
static bool VXGraphicsItem_shape(const XGraphicsItem* self, XPainterPath* out)
{
    XRectF br;
    if (!self || !out) return false;
    br = XGraphicsItem_boundingRect_base(self);
    if (br.width <= 0.0f || br.height <= 0.0f) {
        /* 空包围盒 → 空路径（QPainterPath::addRect(QRectF()) 同效：
         * contains 恒 false）。 */
        return true;
    }
    /* addRect 收整数矩形；局部包围盒是浮点——用 moveTo/lineTo 四点
     * 闭合构造，精度无损（Qt addRect(QRectF) 浮点语义）。 */
    XPainterPath_moveTo(out, br.x, br.y);
    XPainterPath_lineTo(out, br.x + br.width, br.y);
    XPainterPath_lineTo(out, br.x + br.width, br.y + br.height);
    XPainterPath_lineTo(out, br.x, br.y + br.height);
    XPainterPath_closeSubpath(out);
    return true;
}

/** @brief Contains 缺省：shape 命中（对标 contains 原文：By default, this
 *  function calls shape()；qgraphicsitem.cpp:4913-4916。本轮无裁剪路径
 *  分支——isClipped 相关标志不在本子集）。 */
static bool VXGraphicsItem_contains(const XGraphicsItem* self, const XPointF* point)
{
    XPainterPath path;
    bool hit;
    if (!self || !point) return false;
    XPainterPath_init(&path);
    hit = XGraphicsItem_shape_base(self, &path) &&
          XPainterPath_containsPoint(&path, point->x, point->y);
    XPainterPath_deinit(&path);
    return hit;
}

/* ==================== 事件缺省 handler（对标 Qt 各缺省虚函数） ====================
 * Qt 的 QGraphicsItem 为每个事件类型各设一个虚函数（mousePressEvent 等），
 * sceneEvent 按事件类型转发。本类把分派收敛进 Event 单槽，缺省实现按
 * Qt 6.8 对应缺省 handler 逐条对齐（出处见各分支注释）。 */

/** @brief GRAPHICS_SCENE_MOUSE_PRESS 缺省（qgraphicsitem.cpp
 *  QGraphicsItem::mousePressEvent 原文）：左键+可选中⇒（无 Ctrl）场景清选
 *  后选中本项；否则非可移动项忽略（accepted=false）。 */
static void VXGraphicsItem_mousePressDefault(XGraphicsItem* self,
                                             XMouseEvent* event)
{
    if (!self || !event) return;
    if ((event->m_button & (XMouseButton)XMouseButton_LeftButton) &&
        (self->m_flags & XGraphicsItemFlag_ItemIsSelectable)) {
        bool multiSelect =
            (event->m_modifiers & (XKeyboardModifiers)XKeyboardModifier_ControlModifier) != 0;
        if (!multiSelect && !XGraphicsItem_isSelected(self)) {
            XGraphicsScene* scene = self->m_scene;
            if (scene) XGraphicsScene_clearSelection(scene);
            XGraphicsItem_setSelected(self, true);
        }
    } else if (!(self->m_flags & XGraphicsItemFlag_ItemIsMovable)) {
        XEvent_setAccepted_base((XEvent*)event, false);
    }
}

/** @brief GRAPHICS_SCENE_MOUSE_RELEASE 缺省（QGraphicsItem::
 *  mouseReleaseEvent 原文）：左键+可选中且未移动（按下=松开同点）⇒
 *  无 Ctrl 清选后保持选中本项、有 Ctrl 翻转选中；全键松开清拖动锚点。 */
static void VXGraphicsItem_mouseReleaseDefault(XGraphicsItem* self,
                                               XMouseEvent* event)
{
    if (!self || !event) return;
    if ((event->m_button & (XMouseButton)XMouseButton_LeftButton) &&
        (self->m_flags & XGraphicsItemFlag_ItemIsSelectable)) {
        bool multiSelect =
            (event->m_modifiers & (XKeyboardModifiers)XKeyboardModifier_ControlModifier) != 0;
        XGraphicsScene* scene = self->m_scene;
        bool sameAsDown = false;
        if (scene) {
            XPointF cur = scene->m_lastEventScenePos;
            sameAsDown = (cur.x == scene->m_mouseDownScenePos.x &&
                          cur.y == scene->m_mouseDownScenePos.y);
        }
        if (sameAsDown) {
            if (multiSelect) {
                XGraphicsItem_setSelected(self, !XGraphicsItem_isSelected(self));
            } else {
                if (scene) XGraphicsScene_clearSelection(scene);
                XGraphicsItem_setSelected(self, true);
            }
        }
    }
    if (self->m_scene && event->m_buttons == (XMouseButton)XMouseButton_NoButton)
        self->m_scene->m_hasDragAnchor = false; /* 全键松开清 initialPositions
                                                   （Qt mouseReleaseEvent 尾段）。 */
}

/** @brief GRAPHICS_SCENE_MOUSE_MOVE 缺省（QGraphicsItem::mouseMoveEvent
 *  原文的「本项」分支）：左键+可移动⇒按下锚点差值映射回父坐标拖动；
 *  可选中顺带置选（Qt 同）。 */
static void VXGraphicsItem_mouseMoveDefault(XGraphicsItem* self,
                                            XMouseEvent* event)
{
    XGraphicsScene* scene;
    if (!self || !event) return;
    if (!((event->m_buttons & (XMouseButton)XMouseButton_LeftButton) &&
          (self->m_flags & XGraphicsItemFlag_ItemIsMovable)))
        return;
    scene = self->m_scene;
    if (!scene || scene->m_mouseGrabberItem != self || !scene->m_hasDragAnchor)
        return;
    {
        XPointF curItem;
        XPointF curParent;
        XPointF downItem;
        XPointF downParent;
        XPointF np;
        /* currentParentPos = mapToParent(mapFromScene(scenePos))，
         * buttonDownParentPos = mapToParent(mapFromScene(buttonDownPos))，
         * newPos = initialPos + 前者 − 后者（qgraphicsitem.cpp
         * mouseMoveEvent 缺省的无 view 分支原式）。 */
        curItem = XGraphicsItem_mapFromScene(self, &scene->m_lastEventScenePos);
        curParent = XGraphicsItem_mapToParent(self, &curItem);
        downItem = XGraphicsItem_mapFromScene(self, &scene->m_mouseDownScenePos);
        downParent = XGraphicsItem_mapToParent(self, &downItem);
        np.x = scene->m_dragInitialPos.x + curParent.x - downParent.x;
        np.y = scene->m_dragInitialPos.y + curParent.y - downParent.y;
        XGraphicsItem_setPos(self, &np);
        if (self->m_flags & XGraphicsItemFlag_ItemIsSelectable)
            XGraphicsItem_setSelected(self, true);
    }
}

/** @brief Event 缺省实现：按事件类型分派（对标 Qt sceneEvent 的 switch +
 *  各缺省 handler）。KEY_PRESS/RELEASE/WHEEL 缺省 handler 是
 *  event->ignore()（qgraphicsitem.cpp 同名缺省），XEvent_init 缺省已接受
 *  ⇒ 显式置 false；HOVER_* 缺省空操作（Qt hoverMoveEvent 同）。 */
static bool VXGraphicsItem_event(XGraphicsItem* self, XEvent* event)
{
    if (!self || !event) return false;
    switch (event->m_type) {
    case XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_PRESS:
        VXGraphicsItem_mousePressDefault(self, (XMouseEvent*)event);
        break;
    case XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_MOVE:
        VXGraphicsItem_mouseMoveDefault(self, (XMouseEvent*)event);
        break;
    case XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_RELEASE:
        VXGraphicsItem_mouseReleaseDefault(self, (XMouseEvent*)event);
        break;
    case XEVENT_TYPE_KEY_PRESS:
    case XEVENT_TYPE_KEY_RELEASE:
    case XEVENT_TYPE_GRAPHICS_SCENE_WHEEL:
    case XEVENT_TYPE_GRAPHICS_SCENE_CONTEXT_MENU:
        /* Qt keyPressEvent/keyReleaseEvent/wheelEvent/contextMenuEvent
         * 缺省均为 ignore（qgraphicsitem.cpp）。 */
        XEvent_setAccepted_base(event, false);
        break;
    default:
        /* 未知类型：QObject::event 对未识别类型恒 false，场景事件经
         * sceneEvent 转发后按 Qt 口径不置 accepted（事件缺省已接受，
         * 分派器各自显式管理）。空操作即可。 */
        break;
    }
    return true;
}

/** @brief ItemChange 缺省实现：原样回传（Qt QGraphicsItem::itemChange
 *  qgraphicsitem.cpp:7363-7366 原文：Q_UNUSED(change); return value;）。 */
static const void* VXGraphicsItem_itemChange(XGraphicsItem* self, int change,
                                             const void* value)
{
    (void)self;
    (void)change;
    return value;
}

/* ==================== 生命周期 ==================== */

XVtable* XGraphicsItem_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsItem)
    XVTABLE_INHERIT_XCLASS(XObject);
    /* Deinit 必须覆写（对标 QGraphicsEffect 的 VXGraphicsEffect_deinit 挂法）：
     * 不挂则 XClassDeinit 走 XObject 缺省 → 不级联删子、不摘父链、子表容器
     * 泄漏（gdb 断点实证：漏挂时 gitem_deleteChildren 全程未命中）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGraphicsItem_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_BoundingRect,
                             VXGraphicsItem_boundingRect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Paint, VXGraphicsItem_paint);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Shape, VXGraphicsItem_shape);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Contains, VXGraphicsItem_contains);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_ItemChange, VXGraphicsItem_itemChange);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Event, VXGraphicsItem_event);
    return XVTABLE_DEFAULT;
}

void XGraphicsItem_init(XGraphicsItem* self)
{
    if (!self) return;
    /* AGENTS.md 铁律：init 全字段显式清零（MSVC /RTC1 栈垃圾防线），
     * 再按 Qt 构造缺省补非零值（qgraphicsitem.cpp QGraphicsItem 构造：
     * visible=1、opacity=1、z=0、单位变换、无父）。 */
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XGraphicsItem);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_flags = 0;
    self->m_parentItem = NULL;
    self->m_childItems = NULL;
    self->m_siblingIndex = 0;
    self->m_pos.x = 0.0f;
    self->m_pos.y = 0.0f;
    XGraphicsItem_matrixIdentity(&self->m_transform);
    self->m_z = 0.0f;
    self->m_opacity = 1.0f;
    self->m_visible = true;
    self->m_explicitlyHidden = false;
    self->m_selected = false;
    self->m_acceptHoverEvents = false;
    self->m_scene = NULL;
    self->m_graphicsEffect = NULL; /* W4：缺省无效果（Qt graphicsEffect(nullptr)）。 */
}

XGraphicsItem* XGraphicsItem_create_ex(XMemoryType memory)
{
    XGraphicsItem* self = (XGraphicsItem*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGraphicsItem_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/** @brief 子表挂接（对标 QGraphicsItemPrivate::addChild，qgraphicsitem.cpp:
 *  5403-5413：兄弟序号=表尾、needSort 置位——XGui 的 childItems() 每次
 *  现算排序，无需 needSort 位）。 */
static bool gitem_addChild(XGraphicsItem* parent, XGraphicsItem* child)
{
    if (!parent->m_childItems) {
        parent->m_childItems = XVector_Create(XGraphicsItem*);
        if (!parent->m_childItems) return false;
    }
    if (!XVector_insert_2(parent->m_childItems,
                          (int64_t)XContainerSize(parent->m_childItems), &child)) {
        return false;
    }
    child->m_siblingIndex = (int)XContainerSize(parent->m_childItems) - 1;
    return true;
}

/** @brief 子表摘除并重排兄弟序号（对标 removeChild + 兄弟序号连续化；
 *  Qt 用洞位懒重排，XGui 量级小直接重排，语义等价：siblingIndex 恒为
 *  「父表中的插入序」）。 */
static void gitem_removeChild(XGraphicsItem* parent, XGraphicsItem* child)
{
    size_t i;
    size_t n;
    if (!parent || !parent->m_childItems || !child) return;
    n = XContainerSize(parent->m_childItems);
    for (i = 0; i < n; ++i) {
        if (*(XGraphicsItem**)XVector_at_base(parent->m_childItems, (int64_t)i) ==
            child) {
            XVector_remove_base(parent->m_childItems, (int64_t)i, 1);
            break;
        }
    }
    n = XContainerSize(parent->m_childItems);
    for (i = 0; i < n; ++i) {
        XGraphicsItem* sib = *(XGraphicsItem**)XVector_at_base(parent->m_childItems,
                                                               (int64_t)i);
        sib->m_siblingIndex = (int)i;
    }
}

/** @brief 可见性下沉（对标 QGraphicsItemPrivate::setVisibleHelper，
 *  qgraphicsitem.cpp：显式位更新→同值早退→父隐藏时子不得 show→翻转
 *  visible→隐藏即落选→后代同步（explicit=false，显式隐藏的子不复活）→
 *  末尾派发 ItemVisibleHasChanged（2525）+ 标脏）。 */
static void gitem_setVisibleHelper(XGraphicsItem* self, bool newVisible,
                                   bool explicitly)
{
    size_t i;
    size_t n;
    if (explicitly) self->m_explicitlyHidden = !newVisible;
    if (self->m_visible == newVisible) return;
    /* Don't show child if parent is not visible（Qt 原文注释同款）。 */
    if (self->m_parentItem && newVisible && !self->m_parentItem->m_visible) return;
    self->m_visible = newVisible;
    if (!newVisible) {
        /* Certain properties are dropped as an item becomes invisible（Qt
         * setVisibleHelper 隐藏支 qgraphicsitem.cpp:2413-2436 全支对齐）：
         * ① 撤鼠标抓取（2415-2417 `if (mouseGrabberItems.contains(q))
         *    q->ungrabMouse()`——本项或其子树内节点是场景抓取项即清，
         *    拖动锚点一并失效；【复核修订 2026-11-16】此前版本漏此支，
         *    隐藏的抓取项仍会收后续 move/release）；
         * ② 自身持有场景焦点则清焦（2420-2434 hasFocus 支——本子集无
         *    widget/panel 祖先链分支，非 widget 项 Qt 即 clear=true 直清）；
         * ③ 落选（2435-2436 `if (isSelected()) setSelected(false);`——经
         *    setSelected 走完整通知链：itemChange(ItemSelectedHasChanged)
         *    + 场景 selectionChanged）。 */
        if (self->m_scene && self->m_scene->m_mouseGrabberItem &&
            (self->m_scene->m_mouseGrabberItem == self ||
             XGraphicsItem_isAncestorOf(self, self->m_scene->m_mouseGrabberItem))) {
            self->m_scene->m_mouseGrabberItem = NULL;
            self->m_scene->m_hasDragAnchor = false;
        }
        if (self->m_scene && self->m_scene->m_focusItem == self)
            XGraphicsScene_clearFocus(self->m_scene);
        if (XGraphicsItem_isSelected(self))
            XGraphicsItem_setSelected(self, false);
    }
    /* 标脏（Qt setVisible 的 markDirty 同效：本项包围盒随有效可见位
     * 翻转进出渲染集）。 */
    if (self->m_scene) {
        XRectF dirty = XGraphicsItem_sceneBoundingRect(self);
        XGraphicsScene_updateRect(self->m_scene, &dirty);
    }
    /* Update children with explicitly = false（Qt 原文注释同款）。 */
    n = self->m_childItems ? XContainerSize(self->m_childItems) : 0;
    for (i = 0; i < n; ++i) {
        XGraphicsItem* child =
            *(XGraphicsItem**)XVector_at_base(self->m_childItems, (int64_t)i);
        if (!child) continue;
        if (!newVisible || !child->m_explicitlyHidden)
            gitem_setVisibleHelper(child, newVisible, false);
    }
    /* Deliver post-change notification（Qt setVisibleHelper 尾段
     * qgraphicsitem.cpp:2525；值=新有效可见位，不设通知闸）。 */
    {
        bool v = self->m_visible;
        (void)XGraphicsItem_itemChange_base(
            self, (int)XGraphicsItemChange_ItemVisibleHasChanged, &v);
    }
}

/** @brief 级联删子（对标 ~QGraphicsItem，qgraphicsitem.cpp:1566-1569：
 *  `while (!d_ptr->children.isEmpty()) delete d_ptr->children.first()`；
 *  栈上子项只反初始化。先断链再删，防子项 deinit 经 setParentItem(NULL)
 *  重入本表）。 */
static void gitem_deleteChildren(XGraphicsItem* self)
{
    while (self->m_childItems && XContainerSize(self->m_childItems) > 0) {
        XGraphicsItem* child =
            *(XGraphicsItem**)XVector_at_base(self->m_childItems, 0);
        if (!child) {
            XVector_remove_base(self->m_childItems, 0, 1);
            continue;
        }
        child->m_parentItem = NULL;
        XVector_remove_base(self->m_childItems, 0, 1);
        if (Class_IsHeap(child))
            XClassDelete(child);
        else
            XClassDeinit(child);
    }
}

/** @brief Deinit 槽位：级联删子 → 场景注销 → 摘离父项 → 释放子表容器 →
 *  父类清尾。顺序对标 ~QGraphicsItem（qgraphicsitem.cpp:1528 起的析构序：
 *  先删子（每个子项析构各自向场景注销自己），随后 removeItemHelper(this)
 *  摘离场景并经 setParentItem(nullptr) 摘离仍存活的父项）。 */
static void VXGraphicsItem_deinit(XGraphicsItem* self)
{
    if (!self) return;
    gitem_deleteChildren(self);
    if (self->m_scene) {
        /* 析构内注销路径（Qt removeItemHelper 的 inDestructor 分支）：从
         * 场景索引/顶层表摘除、清场景回指与焦点/抓取/悬停引用；不派发
         * ItemSceneChange/HasChanged（Qt 析构路径同样不派发）。 */
        XGraphicsScene_removeItemInternal(self->m_scene, self);
        self->m_scene = NULL;
    }
    if (self->m_parentItem) {
        gitem_removeChild(self->m_parentItem, self);
        self->m_parentItem = NULL;
    }
    if (self->m_childItems) {
        XClassDelete(self->m_childItems);
        self->m_childItems = NULL;
    }
    /* 图形效果由本项拥有（对标 ~QGraphicsItem 的 `delete d_ptr->graphicsEffect`
     * ——qgraphicsitem.cpp:1583-1585，排在场景注销之后）。先断反向关联再删：
     * 效果 Deinit 槽会反查 m_itemSource 摘本项槽位，本路径自己删效果时不
     * 需要那一步。 */
    if (self->m_graphicsEffect) {
        XGraphicsEffect* effect = self->m_graphicsEffect;
        self->m_graphicsEffect = NULL;
        XGraphicsEffect_setItemSource(effect, NULL);
        XClassDelete(effect);
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/* ==================== 虚槽位唯一公开入口（_base 契约） ==================== */

XRectF XGraphicsItem_boundingRect_base(const XGraphicsItem* self)
{
    XGraphicsItem_BoundingRectSlot slot;
    if (!self || !XClassGetVtable(self))
        return VXGraphicsItem_boundingRect(self);
    slot = XClassGetVirtualFunc(self, EXGraphicsItem_BoundingRect,
                                XGraphicsItem_BoundingRectSlot);
    if (!slot) return VXGraphicsItem_boundingRect(self);
    return slot(self);
}

void XGraphicsItem_paint_base(XGraphicsItem* self, XPainter* painter)
{
    XGraphicsItem_PaintSlot slot;
    if (!self || !XClassGetVtable(self)) return;
    slot = XClassGetVirtualFunc(self, EXGraphicsItem_Paint,
                                XGraphicsItem_PaintSlot);
    if (!slot) return;
    slot(self, painter);
}

bool XGraphicsItem_shape_base(const XGraphicsItem* self, XPainterPath* out)
{
    XGraphicsItem_ShapeSlot slot;
    if (!out) return false;
    if (!self || !XClassGetVtable(self))
        return VXGraphicsItem_shape(self, out);
    slot = XClassGetVirtualFunc(self, EXGraphicsItem_Shape, XGraphicsItem_ShapeSlot);
    if (!slot) return VXGraphicsItem_shape(self, out);
    return slot(self, out);
}

bool XGraphicsItem_contains_base(const XGraphicsItem* self, const XPointF* point)
{
    XGraphicsItem_ContainsSlot slot;
    if (!self || !XClassGetVtable(self))
        return VXGraphicsItem_contains(self, point);
    slot = XClassGetVirtualFunc(self, EXGraphicsItem_Contains,
                                XGraphicsItem_ContainsSlot);
    if (!slot) return VXGraphicsItem_contains(self, point);
    return slot(self, point);
}

const void* XGraphicsItem_itemChange_base(XGraphicsItem* self, int change,
                                          const void* value)
{
    XGraphicsItem_ItemChangeSlot slot;
    if (!self || !XClassGetVtable(self))
        return VXGraphicsItem_itemChange(self, change, value);
    slot = XClassGetVirtualFunc(self, EXGraphicsItem_ItemChange,
                                XGraphicsItem_ItemChangeSlot);
    if (!slot) return VXGraphicsItem_itemChange(self, change, value);
    return slot(self, change, value);
}

bool XGraphicsItem_event_base(XGraphicsItem* self, XEvent* event)
{
    XGraphicsItem_EventSlot slot;
    if (!self || !XClassGetVtable(self)) return false;
    slot = XClassGetVirtualFunc(self, EXGraphicsItem_Event,
                                XGraphicsItem_EventSlot);
    if (!slot) return VXGraphicsItem_event(self, event);
    return slot(self, event);
}

/* ==================== 几何与变换 ==================== */

XPointF XGraphicsItem_pos(const XGraphicsItem* self)
{
    XPointF out;
    if (!self) {
        out.x = 0.0f;
        out.y = 0.0f;
        return out;
    }
    return self->m_pos;
}

void XGraphicsItem_setPos(XGraphicsItem* self, const XPointF* position)
{
    XPointF zero;
    XRectF dirty;
    XRectF dirtyNew;
    const XPointF* pos = position;
    bool sendsGeometry;
    if (!self) return;
    if (!pos) {
        zero.x = 0.0f;
        zero.y = 0.0f;
        pos = &zero;
    }
    if (self->m_pos.x == pos->x && self->m_pos.y == pos->y) return;
    sendsGeometry =
        (self->m_flags & (XGraphicsItemFlag_ItemSendsGeometryChanges |
                          XGraphicsItemFlag_ItemSendsScenePositionChanges)) != 0;
    /* 脏区/通知按 Qt setPos 顺序：先摆位，后通知（qgraphicsitem.cpp:3854
     * setPosHelper → 3877 ItemPositionHasChanged）。旧位置包围盒在摆位前
     * 取出，与新位置一并标脏（Qt markDirty 同效）。 */
    dirty = XGraphicsItem_sceneBoundingRect(self);
    self->m_pos = *pos;
    if (self->m_scene) {
        dirtyNew = XGraphicsItem_sceneBoundingRect(self);
        gitem_unitedRect(&dirty, &dirtyNew);
        XGraphicsScene_updateRect(self->m_scene, &dirty);
    }
    if (sendsGeometry) {
        XPointF newPos = *pos;
        (void)XGraphicsItem_itemChange_base(
            self, (int)XGraphicsItemChange_ItemPositionHasChanged, &newPos);
    }
}

const XImageTransform* XGraphicsItem_transform_const(const XGraphicsItem* self)
{
    if (!self) return NULL;
    return &self->m_transform;
}

void XGraphicsItem_setTransform(XGraphicsItem* self, const XImageTransform* matrix,
                                bool combine)
{
    XImageTransform identity;
    XImageTransform newTransform;
    const XImageTransform* src = matrix;
    if (!self) return;
    if (!src) {
        XGraphicsItem_matrixIdentity(&identity);
        src = &identity;
    }
    /* 对标 QGraphicsItem::setTransform（qgraphicsitem.cpp:4473）：
     * newTransform(combine ? matrix * transform : matrix)。 */
    if (combine)
        XGraphicsItem_matrixMultiply(&newTransform, src, &self->m_transform);
    else
        newTransform = *src;
    /* 同值早退（Qt `if (transform == newTransform) return;`；XGui 按位精确
     * 比较——比 Qt 的 qFuzzy 相等更严，只可能少早退、不可能漏变更）。 */
    if (XMemcmp(&self->m_transform, &newTransform, sizeof(XImageTransform)) == 0)
        return;
    self->m_transform = newTransform;
}

void XGraphicsItem_sceneTransform(const XGraphicsItem* self, XImageTransform* out)
{
    XImageTransform tmp;
    const XGraphicsItem* p;
    int depth = 0;
    if (!out) return;
    if (!self) {
        XGraphicsItem_matrixIdentity(out);
        return;
    }
    /* 深度防御（成环已被 setParentItem 拒绝；异常链按单位阵返回）。 */
    depth = gitem_depth(self);
    XGraphicsItem_matrixIdentity(out);
    if (depth >= 4096) return;
    /* 从 self 出发逐级**后乘**「本级 itemToParent」（Qt 行向量口径 a*b =
     * a 先作用：res = itemToParent(self) · itemToParent(parent) · …——
     * 点先过本项变换、再逐级上溯到根；探针 §1 与 Qt sceneTransform
     * 手工链精确一致 maxRel=3.5e-16）。注意不能写成 multiply(&tmp, out)：
     * 那会变成「先父链后本项」，平移分量全部错位（旋转×均匀缩放的线性
     * 部分恰好可交换、掩盖方向错误的实证教训）。 */
    for (p = self; p; p = p->m_parentItem) {
        gitem_itemToParent(p, &tmp);
        XGraphicsItem_matrixMultiply(out, out, &tmp);
    }
}

XPointF XGraphicsItem_scenePos(const XGraphicsItem* self)
{
    XImageTransform st;
    XPointF out;
    if (!self) {
        out.x = 0.0f;
        out.y = 0.0f;
        return out;
    }
    XGraphicsItem_sceneTransform(self, &st);
    XGraphicsItem_matrixMapPoint(&st, 0.0f, 0.0f, &out);
    return out;
}

XPointF XGraphicsItem_mapToParent(const XGraphicsItem* self, const XPointF* point)
{
    XImageTransform m;
    XPointF out;
    if (!point) {
        out.x = 0.0f;
        out.y = 0.0f;
        return out;
    }
    if (!self) return *point;
    /* 对标 mapToParent（qgraphicsitem.cpp:5820-5824）：无变换时
     * point + pos 快路径；有变换走 transformToParent().map(point)。 */
    gitem_itemToParent(self, &m);
    XGraphicsItem_matrixMapPoint(&m, point->x, point->y, &out);
    return out;
}

XPointF XGraphicsItem_mapFromParent(const XGraphicsItem* self, const XPointF* point)
{
    XImageTransform m;
    XImageTransform inv;
    XPointF out;
    if (!point) {
        out.x = 0.0f;
        out.y = 0.0f;
        return out;
    }
    if (!self) return *point;
    gitem_itemToParent(self, &m);
    if (!XGraphicsItem_matrixInvert(&m, &inv)) {
        /* 奇异回落=单位阵映射=输入原样（【P2 修订 2026-11-16】对齐 Qt
         * inverted() 失败回单位阵的口径，与本文件 matrixInvert 一致；
         * 原实现返回 (0,0) 是无出处口径）。 */
        return *point;
    }
    XGraphicsItem_matrixMapPoint(&inv, point->x, point->y, &out);
    return out;
}

XPointF XGraphicsItem_mapToItem(const XGraphicsItem* self, const XGraphicsItem* item,
                                const XPointF* point)
{
    /* 对标 mapToItem（qgraphicsitem.cpp:5785-5787）：item 非空走
     * itemTransform(item).map(point)，空走 mapToScene。 */
    if (!point) {
        XPointF zero;
        zero.x = 0.0f;
        zero.y = 0.0f;
        return zero;
    }
    if (!item) return XGraphicsItem_mapToScene(self, point);
    {
        XImageTransform m;
        XPointF out;
        if (!XGraphicsItem_itemTransform(self, item, &m)) {
            /* Qt 的 itemTransform 失败回单位阵（ok=false），mapToItem
             * 不检查继续映射 = 输入原样（【P2 修订】原实现返回 (0,0)）。 */
            return *point;
        }
        XGraphicsItem_matrixMapPoint(&m, point->x, point->y, &out);
        return out;
    }
}

XPointF XGraphicsItem_mapFromItem(const XGraphicsItem* self, const XGraphicsItem* item,
                                  const XPointF* point)
{
    if (!point) {
        XPointF zero;
        zero.x = 0.0f;
        zero.y = 0.0f;
        return zero;
    }
    if (!item) return XGraphicsItem_mapFromScene(self, point);
    {
        XImageTransform m;
        XPointF out;
        /* mapFromItem(item, p) = item->mapToItem(self, p) 的口径：
         * 直接取 item→self 矩阵映射（Qt 文档：equivalent to
         * item->mapToItem(this, point)）。 */
        if (!XGraphicsItem_itemTransform(item, self, &m)) {
            /* 失败=单位阵映射=输入原样（【P2 修订】同 mapToItem 口径）。 */
            return *point;
        }
        XGraphicsItem_matrixMapPoint(&m, point->x, point->y, &out);
        return out;
    }
}

XPointF XGraphicsItem_mapToScene(const XGraphicsItem* self, const XPointF* point)
{
    XImageTransform st;
    XPointF out;
    if (!point) {
        out.x = 0.0f;
        out.y = 0.0f;
        return out;
    }
    if (!self) return *point;
    XGraphicsItem_sceneTransform(self, &st);
    XGraphicsItem_matrixMapPoint(&st, point->x, point->y, &out);
    return out;
}

XPointF XGraphicsItem_mapFromScene(const XGraphicsItem* self, const XPointF* point)
{
    XImageTransform st;
    XImageTransform inv;
    XPointF out;
    if (!point) {
        out.x = 0.0f;
        out.y = 0.0f;
        return out;
    }
    if (!self) return *point;
    XGraphicsItem_sceneTransform(self, &st);
    if (!XGraphicsItem_matrixInvert(&st, &inv)) {
        /* 奇异回落=单位阵映射=输入原样（【P2 修订】对齐 Qt inverted()
         * 失败回单位阵口径；原实现返回 (0,0)）。 */
        return *point;
    }
    XGraphicsItem_matrixMapPoint(&inv, point->x, point->y, &out);
    return out;
}

XRectF XGraphicsItem_mapRectToScene(const XGraphicsItem* self, const XRectF* rect)
{
    XImageTransform st;
    XRectF out;
    if (!rect) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.width = 0.0f;
        out.height = 0.0f;
        return out;
    }
    if (!self) return *rect;
    XGraphicsItem_sceneTransform(self, &st);
    XGraphicsItem_matrixMapRect(&st, rect, &out);
    return out;
}

XRectF XGraphicsItem_mapRectFromScene(const XGraphicsItem* self, const XRectF* rect)
{
    XImageTransform st;
    XImageTransform inv;
    XRectF out;
    if (!rect) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.width = 0.0f;
        out.height = 0.0f;
        return out;
    }
    if (!self) return *rect;
    XGraphicsItem_sceneTransform(self, &st);
    if (!XGraphicsItem_matrixInvert(&st, &inv)) {
        /* 奇异回落=单位阵映射=输入矩形原样（【P2 修订】对齐 Qt inverted()
         * 失败回单位阵口径；原实现返回全 0 矩形）。 */
        return *rect;
    }
    XGraphicsItem_matrixMapRect(&inv, rect, &out);
    return out;
}

XGraphicsItem* XGraphicsItem_parentItem(const XGraphicsItem* self)
{
    if (!self) return NULL;
    return self->m_parentItem;
}

XGraphicsItem* XGraphicsItem_topLevelItem(const XGraphicsItem* self)
{
    XGraphicsItem* p;
    if (!self) return NULL;
    p = self->m_parentItem;
    while (p && p->m_parentItem) p = p->m_parentItem;
    if (p) return p;
    return (XGraphicsItem*)self;
}

bool XGraphicsItem_isAncestorOf(const XGraphicsItem* self, const XGraphicsItem* child)
{
    const XGraphicsItem* ancestor;
    if (!self || !child || child == self) return false;
    ancestor = child->m_parentItem;
    while (ancestor) {
        if (ancestor == self) return true;
        ancestor = ancestor->m_parentItem;
    }
    return false;
}

XGraphicsItem* XGraphicsItem_commonAncestorItem(const XGraphicsItem* self,
                                                const XGraphicsItem* other)
{
    const XGraphicsItem* a;
    const XGraphicsItem* b;
    int da;
    int db;
    if (!other) return NULL;
    if (!self) return NULL;
    if (other == self) return (XGraphicsItem*)self;
    /* 对标 commonAncestorItem（qgraphicsitem.cpp:6421-6443）：按深度对齐
     * 后同步上溯。 */
    a = self;
    b = other;
    da = gitem_depth(a);
    db = gitem_depth(b);
    while (da > db) {
        a = a->m_parentItem;
        --da;
        if (!a) return NULL;
    }
    while (db > da) {
        b = b->m_parentItem;
        --db;
        if (!b) return NULL;
    }
    while (a && a != b) {
        a = a->m_parentItem;
        b = b->m_parentItem;
    }
    return (XGraphicsItem*)a;
}

bool XGraphicsItem_setParentItem(XGraphicsItem* self, XGraphicsItem* newParent)
{
    XGraphicsItem* oldParent;
    if (!self) return false;
    if (newParent == self) {
        /* 对标 qgraphicsitem.cpp:1791-1794（cannot assign as a parent of
         * itself；探针 §4：警告+返回、父子关系不变）。 */
        return false;
    }
    oldParent = self->m_parentItem;
    if (newParent == oldParent) return true; /* 同值早退（Qt 同）。 */
    if (newParent && XGraphicsItem_isAncestorOf(self, newParent)) {
        /* 祖先→后代成环：Qt 文档（qgraphicsitem.cpp:1761-1763）明言
         * undefined、6.8.0 探针实测 setParentItem 内部链行走死循环挂死。
         * XGui 取安全分支：拒绝并保持树原状（不可学 Qt 挂死）。 */
        return false;
    }
    if (oldParent) gitem_removeChild(oldParent, self);
    self->m_parentItem = newParent;
    if (newParent) {
        if (!gitem_addChild(newParent, self)) {
            self->m_parentItem = NULL;
            return false;
        }
        /* 可见性随新父（对标 setParentItemHelper：
         *   if (parent->visible != visible)
         *       if (!parent->visible || !explicitlyHidden)
         *           setVisibleHelper(parent->visible, explicit=false)） */
        if (newParent->m_visible != self->m_visible) {
            if (!newParent->m_visible || !self->m_explicitlyHidden)
                gitem_setVisibleHelper(self, newParent->m_visible, false);
        }
    } else {
        /* 摘父成顶层：非显式隐藏的隐藏项恢复可见（Qt 同：
         * if (!visible && !explicitlyHidden) setVisibleHelper(true, false)）。 */
        if (!self->m_visible && !self->m_explicitlyHidden)
            gitem_setVisibleHelper(self, true, false);
    }
    /* 重挂改变 scene 几何 ⇒ 标脏 + ItemParentHasChanged。
     * **【复核修订 2026-11-16】**通知**无条件派发**（此前版本误闸在
     * ItemSendsGeometryChanges 之后并引 1798/1375 称「Qt 同口径」——
     * 核实 Qt 6.8.3 原文：setParentItem（qgraphicsitem.cpp:1798-1805）
     * 的前置 ItemParentChange 无任何闸、无条件派发；setParentItemHelper
     * :1375 的 `if (newParentVariant)` 恒真（setParentItem:1805 恒传
     * 非空变体）⇒ 后置 ItemParentHasChanged 亦无条件。该闸在 Qt 只属
     * Transform/Rotation/Scale 系（3993-4224）与 setPos（:3858）。 */
    if (self->m_scene) {
        XRectF dirty = XGraphicsItem_sceneBoundingRect(self);
        XGraphicsScene_updateRect(self->m_scene, &dirty);
    }
    (void)XGraphicsItem_itemChange_base(
        self, (int)XGraphicsItemChange_ItemParentHasChanged, newParent);
    return true;
}

XVector* XGraphicsItem_childItems(const XGraphicsItem* self)
{
    size_t n;
    size_t i;
    size_t j;
    XVector* out;
    XGraphicsItem** items;
    if (!self || !self->m_childItems) return NULL;
    n = XContainerSize(self->m_childItems);
    if (n == 0) return NULL;
    items = (XGraphicsItem**)XMemory_malloc(sizeof(XGraphicsItem*) * n,
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!items) return NULL;
    for (i = 0; i < n; ++i)
        items[i] = *(XGraphicsItem**)XVector_at_base(self->m_childItems, (int64_t)i);
    /* 稳定排序：z 升序，同 z 按 siblingIndex（插入序）升序——qt_closestLeaf
     * 的比较键（qgraphicsitem_p.h:622-633）。插入排序 O(n²)，场景树量级
     * 小；W2 若需大批量子项可换归并。 */
    for (i = 1; i < n; ++i) {
        XGraphicsItem* key = items[i];
        int keyIdx = key->m_siblingIndex;
        j = i;
        while (j > 0) {
            XGraphicsItem* prev = items[j - 1];
            bool prevGreater = (prev->m_z > key->m_z) ||
                               (prev->m_z == key->m_z &&
                                prev->m_siblingIndex > keyIdx);
            if (!prevGreater) break;
            items[j] = items[j - 1];
            --j;
        }
        items[j] = key;
    }
    out = XVector_Create(XGraphicsItem*);
    if (!out) {
        XMemory_free(items, XCLASS_DEFAULT_MEMORY_TYPE);
        return NULL;
    }
    for (i = 0; i < n; ++i)
        XVector_insert_2(out, (int64_t)i, &items[i]);
    XMemory_free(items, XCLASS_DEFAULT_MEMORY_TYPE);
    return out;
}

float XGraphicsItem_zValue(const XGraphicsItem* self)
{
    if (!self) return 0.0f;
    return self->m_z;
}

void XGraphicsItem_setZValue(XGraphicsItem* self, float z)
{
    if (!self) return;
    if (self->m_z == z) return; /* 同值早退（Qt setZValue 同）。 */
    self->m_z = z;
    /* 排序在 childItems() 现算（对标 needSortChildren 置位的懒排序语义）。 */
}

bool XGraphicsItem_stackBefore(XGraphicsItem* self, const XGraphicsItem* sibling)
{
    XGraphicsItem* parent;
    size_t i;
    size_t n;
    int myPos = -1;
    int sibPos = -1;
    XGraphicsItem* moved;
    if (!self || !sibling) return false;
    if (sibling == self) return true; /* Qt：sibling==this 直接 return（无动作）。 */
    parent = self->m_parentItem;
    if (parent != sibling->m_parentItem || !parent || !parent->m_childItems) {
        /* 非兄弟拒绝（Qt qgraphicsitem.cpp:4657：cannot stack under ...，
         * 探针 §8 实测跨树告警+不动）。 */
        return false;
    }
    n = XContainerSize(parent->m_childItems);
    for (i = 0; i < n; ++i) {
        XGraphicsItem* it = *(XGraphicsItem**)XVector_at_base(parent->m_childItems,
                                                              (int64_t)i);
        if (it == self) myPos = (int)i;
        if (it == (XGraphicsItem*)sibling) sibPos = (int)i;
    }
    if (myPos < 0 || sibPos < 0) return false;
    /* Qt：只在 myIndex >= siblingIndex 时移动（已在前面则无事）。 */
    if (myPos >= sibPos) {
        moved = *(XGraphicsItem**)XVector_at_base(parent->m_childItems,
                                                  (int64_t)myPos);
        XVector_remove_base(parent->m_childItems, myPos, 1);
        XVector_insert_2(parent->m_childItems, sibPos, &moved);
        /* 重排兄弟序号（保证 siblingIndex 恒连续）。 */
        n = XContainerSize(parent->m_childItems);
        for (i = 0; i < n; ++i) {
            XGraphicsItem* sib = *(XGraphicsItem**)XVector_at_base(
                parent->m_childItems, (int64_t)i);
            sib->m_siblingIndex = (int)i;
        }
    }
    return true;
}

/* ==================== 标志位 / 可见性 / 选中 / 透明度 / 悬停 ==================== */

XGraphicsItemFlags XGraphicsItem_flags(const XGraphicsItem* self)
{
    if (!self) return 0;
    return self->m_flags;
}

void XGraphicsItem_setFlags(XGraphicsItem* self, XGraphicsItemFlags flags)
{
    if (!self) return;
    self->m_flags = flags;
    /* 摘除 ItemIsSelectable 自动落选（Qt setFlags 原文 + 探针 §11）。
     * 经 setSelected 走完整通知链（itemChange + 场景 selectionChanged），
     * 与 Qt 的 setSelected(false) 路径一致；隐藏项同样落选。 */
    if (!(self->m_flags & XGraphicsItemFlag_ItemIsSelectable))
        XGraphicsItem_setSelected(self, false);
    else if (self->m_selected && !self->m_visible)
        XGraphicsItem_setSelected(self, false);
}

void XGraphicsItem_setFlag(XGraphicsItem* self, XGraphicsItemFlag flag, bool enabled)
{
    /* 对标 setFlag（qgraphicsitem.cpp:1868-1873）：置位/清位后走 setFlags。 */
    if (!self) return;
    if (enabled)
        XGraphicsItem_setFlags(self, self->m_flags | (XGraphicsItemFlags)flag);
    else
        XGraphicsItem_setFlags(self, self->m_flags & ~(XGraphicsItemFlags)flag);
}

bool XGraphicsItem_isVisible(const XGraphicsItem* self)
{
    if (!self) return false;
    return self->m_visible;
}

bool XGraphicsItem_isVisibleTo(const XGraphicsItem* self, const XGraphicsItem* ancestor)
{
    /* 原样移植 isVisibleTo（qgraphicsitem.cpp:2355-2365）。 */
    const XGraphicsItem* p;
    if (!self) return false;
    if (self->m_explicitlyHidden) return false;
    p = self;
    do {
        if (p == ancestor) return true;
        if (p->m_explicitlyHidden) return false;
    } while ((p = p->m_parentItem) != NULL);
    return ancestor == NULL;
}

void XGraphicsItem_setVisible(XGraphicsItem* self, bool visible)
{
    if (!self) return;
    /* 对标 setVisible：显式调用（explicit=true）。 */
    gitem_setVisibleHelper(self, visible, true);
}

bool XGraphicsItem_isSelected(const XGraphicsItem* self)
{
    if (!self) return false;
    return self->m_selected;
}

void XGraphicsItem_setSelected(XGraphicsItem* self, bool selected)
{
    bool newSelected;
    if (!self) return;
    /* 对标 setSelected（qgraphicsitem.cpp:2737-2752）：无 ItemIsSelectable
     * 或不可见时强制 false（enabled 恒真——本子集无 enabled 概念）。 */
    if (!(self->m_flags & XGraphicsItemFlag_ItemIsSelectable) || !self->m_visible)
        selected = false;
    if (self->m_selected == selected) return;
    self->m_selected = selected;
    newSelected = self->m_selected;
    /* Qt setSelected 后段（qgraphicsitem.cpp:2755-2768）：update() 标脏；
     * 场景在非批量期即时发 selectionChanged；末尾派发
     * ItemSelectedHasChanged（值=新选中位，不设通知闸）。 */
    if (self->m_scene) {
        XRectF dirty = XGraphicsItem_sceneBoundingRect(self);
        XGraphicsScene_updateRect(self->m_scene, &dirty);
        XGraphicsScene_itemSelectedChangedInternal(self->m_scene, self);
    }
    (void)XGraphicsItem_itemChange_base(
        self, (int)XGraphicsItemChange_ItemSelectedHasChanged, &newSelected);
}

float XGraphicsItem_opacity(const XGraphicsItem* self)
{
    if (!self) return 1.0f;
    return self->m_opacity;
}

void XGraphicsItem_setOpacity(XGraphicsItem* self, float opacity)
{
    if (!self) return;
    /* 夹到 [0,1]（对标 setOpacity 的 qFuzzyBound(0., opacity, 1.)）。 */
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (self->m_opacity == opacity) return;
    self->m_opacity = opacity;
}

float XGraphicsItem_effectiveOpacity(const XGraphicsItem* self)
{
    /* 对标 calcEffectiveOpacity（qgraphicsitem_p.h:236-260）：本项 opacity
     * 起底，沿父链逐级乘父项 opacity。本子集标志位不含
     * ItemIgnoresParentOpacity / ItemDoesntPropagateOpacityToChildren，
     * 即恒全链累乘（Qt 探针 §6：0.5×0.5×0.5=0.125）。 */
    const XGraphicsItem* p;
    float o;
    if (!self) return 1.0f;
    o = self->m_opacity;
    p = self->m_parentItem;
    while (p) {
        o *= p->m_opacity;
        p = p->m_parentItem;
    }
    return o;
}

bool XGraphicsItem_acceptHoverEvents(const XGraphicsItem* self)
{
    if (!self) return false;
    return self->m_acceptHoverEvents;
}

void XGraphicsItem_setAcceptHoverEvents(XGraphicsItem* self, bool accept)
{
    if (!self) return;
    self->m_acceptHoverEvents = accept;
}

/* ==================== 场景关联（W2 域） ==================== */

XGraphicsScene* XGraphicsItem_scene(const XGraphicsItem* self)
{
    if (!self) return NULL;
    return self->m_scene;
}

bool XGraphicsItem_setSceneInternal(XGraphicsItem* self, XGraphicsScene* scene)
{
    /* 内部协作入口：XGraphicsScene 模块专用（头注 @warning 同口径）；
     * 不派发通知、不早退——加入/摘除的幂等由 addItem/removeItem 保证。 */
    if (!self) return false;
    self->m_scene = scene;
    return true;
}

/* ==================== 剩余几何查询（依赖上面的实现） ==================== */

bool XGraphicsItem_itemTransform(const XGraphicsItem* self, const XGraphicsItem* other,
                                 XImageTransform* out)
{
    const XGraphicsItem* parent;
    const XGraphicsItem* otherParent;
    XImageTransform a;
    XImageTransform b;
    XImageTransform bi;
    XGraphicsItem* common;
    if (!out) return false;
    XGraphicsItem_matrixIdentity(out);
    if (!self || !other) return false;
    if (other == self) return true; /* 同项：单位阵（Qt 同）。 */
    parent = self->m_parentItem;
    otherParent = other->m_parentItem;
    if (parent == other) {
        /* self 是 other 的孩子：self→parent 矩阵（Qt 同支）。 */
        gitem_itemToParent(self, out);
        return true;
    }
    if (otherParent == self) {
        /* self 是 other 的父：other→parent 的逆（Qt 同支）。 */
        gitem_itemToParent(other, &a);
        return XGraphicsItem_matrixInvert(&a, out);
    }
    if (parent == otherParent) {
        /* 兄弟：self→parent ∘ parent→other（= other→parent 的逆）（Qt 同支）。 */
        gitem_itemToParent(self, &a);
        gitem_itemToParent(other, &b);
        if (!XGraphicsItem_matrixInvert(&b, &bi)) return false;
        XGraphicsItem_matrixMultiply(out, &a, &bi);
        return true;
    }
    common = XGraphicsItem_commonAncestorItem(self, other);
    if (!common) {
        /* 无共同祖先（两棵独立树）：各自根链坐标互映（对标 Qt 的
         * sceneTransform * other->sceneTransform.inverted 支）。 */
        XImageTransform stSelf;
        XImageTransform stOther;
        XImageTransform stOtherInv;
        XGraphicsItem_sceneTransform(self, &stSelf);
        XGraphicsItem_sceneTransform(other, &stOther);
        if (!XGraphicsItem_matrixInvert(&stOther, &stOtherInv)) return false;
        XGraphicsItem_matrixMultiply(out, &stSelf, &stOtherInv);
        return true;
    }
    if (common != other && common != self) {
        /* 表亲：双双映射到共同祖先再组合（Qt cousins 支，
         * qgraphicsitem.cpp:4407-4419——守卫必不可少：common==self/other
         * 时双递归会无限套娃）。 */
        XImageTransform thisToCommon;
        XImageTransform otherToCommon;
        XImageTransform otherCommonInv;
        if (!XGraphicsItem_itemTransform(self, common, &thisToCommon)) return false;
        if (!XGraphicsItem_itemTransform(other, common, &otherToCommon)) return false;
        if (!XGraphicsItem_matrixInvert(&otherToCommon, &otherCommonInv)) return false;
        XGraphicsItem_matrixMultiply(out, &thisToCommon, &otherCommonInv);
        return true;
    }
    {
        /* 祖孙：沿链复合（Qt walk-the-chain 支，qgraphicsitem.cpp:4421-4442）：
         * 从较深一项逐级 combineTransformToParent 复合到 root（不含 root），
         * self 是祖先时取逆（x = other→self → self→other）。 */
        bool parentOfOther = XGraphicsItem_isAncestorOf(self, other);
        const XGraphicsItem* child = parentOfOther ? other : self;
        const XGraphicsItem* root = parentOfOther ? self : other;
        const XGraphicsItem* p = child;
        XImageTransform x;
        XGraphicsItem_matrixIdentity(&x);
        do {
            XImageTransform step;
            gitem_itemToParent(p, &step);
            XGraphicsItem_matrixMultiply(&x, &x, &step);
        } while ((p = p->m_parentItem) != NULL && p != root);
        if (parentOfOther)
            return XGraphicsItem_matrixInvert(&x, out);
        *out = x;
        return true;
    }
}

void XGraphicsItem_deviceTransform(const XGraphicsItem* self,
                                   const XImageTransform* viewportTransform,
                                   XImageTransform* out)
{
    XImageTransform st;
    XImageTransform identity;
    const XImageTransform* vp = viewportTransform;
    if (!out) return;
    if (!vp) {
        XGraphicsItem_matrixIdentity(&identity);
        vp = &identity;
    }
    if (!self) {
        *out = *vp;
        return;
    }
    /* 对标 deviceTransform（qgraphicsitem.cpp:4287-4294）：可变换项
     * sceneTransform * viewportTransform，全程逻辑坐标。**W3-c 终版
     * 勘误收口**：W1 注的「dpr=1、W4 接 dpr」经 Qt 6.8.3 原文复核为误
     * 记——Qt 的 deviceTransform 不含 dpr（dpr 在 QPainterPrivate::
     * updateMatrix 的 hidpiScaleTransform，qpainter.cpp:626-643；本框架
     * XPainter.c:8312/2351 同链），缺的只是 ItemIgnoresTransformations
     * 支（本子集无该标志位，W4 扩标志位时补）。 */
    XGraphicsItem_sceneTransform(self, &st);
    XGraphicsItem_matrixMultiply(out, &st, vp);
}

XRectF XGraphicsItem_sceneBoundingRect(const XGraphicsItem* self)
{
    XImageTransform st;
    XRectF br;
    if (!self) {
        br.x = 0.0f;
        br.y = 0.0f;
        br.width = 0.0f;
        br.height = 0.0f;
        return br;
    }
    /* 对标 sceneBoundingRect（qgraphicsitem.cpp:4764-4788）：boundingRect
     * 经 sceneTransform 映射取包盒（Qt 的 translate-only 快路径结果同）。 */
    XGraphicsItem_sceneTransform(self, &st);
    br = XGraphicsItem_boundingRect_base(self);
    XGraphicsItem_matrixMapRect(&st, &br, &br);
    return br;
}

XRectF XGraphicsItem_childrenBoundingRect(const XGraphicsItem* self)
{
    XRectF out;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = 0.0f;
    out.height = 0.0f;
    if (!self) return out;
    gitem_childrenRectHelper(self, NULL, &out);
    return out;
}

/* ==================== 图形效果（W4） ==================== */

XGraphicsEffect* XGraphicsItem_graphicsEffect_const(const XGraphicsItem* self)
{
    return self ? self->m_graphicsEffect : NULL;
}

/**
 * @brief 本项（含子孙与效果外扩环）的场景坐标包围盒。
 * @details 用于效果换装/卸载的标脏（对标 prepareGeometryChange +
 *          adjustedItemEffectiveBoundingRect：Qt 的脏区按「项包围盒 ∪
 *          子项包围盒 ∪ 效果包围盒」的场景映射算，qgraphicsscene.cpp:4682/
 *          5117）。effect 参数只在本函数内借用，调用方保证其存活。
 */
static XRectF gitem_effectiveSceneBounds(const XGraphicsItem* item,
                                         const XGraphicsEffect* effect)
{
    XRectF local;
    XRectF out;
    XImageTransform st;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = 0.0f;
    out.height = 0.0f;
    if (!item) return out;
    local = XGraphicsItem_boundingRect_base(item);
    {
        XRectF kids = XGraphicsItem_childrenBoundingRect(item);
        if (kids.width > 0.0f && kids.height > 0.0f) {
            if (local.width <= 0.0f || local.height <= 0.0f) {
                local = kids;
            } else {
                float x1 = local.x < kids.x ? local.x : kids.x;
                float y1 = local.y < kids.y ? local.y : kids.y;
                float x2 = (local.x + local.width) > (kids.x + kids.width)
                               ? (local.x + local.width)
                               : (kids.x + kids.width);
                float y2 = (local.y + local.height) > (kids.y + kids.height)
                               ? (local.y + local.height)
                               : (kids.y + kids.height);
                local.x = x1;
                local.y = y1;
                local.width = x2 - x1;
                local.height = y2 - y1;
            }
        }
    }
    if (local.width <= 0.0f || local.height <= 0.0f) return out;
    if (effect)
        local = XGraphicsEffect_boundingRectFor_base(effect, &local);
    if (local.width <= 0.0f || local.height <= 0.0f) return out;
    XGraphicsItem_sceneTransform(item, &st);
    XGraphicsItem_matrixMapRect(&st, &local, &out);
    return out;
}

/** @brief 标脏本项的有效场景包围盒（有场景才记；无场景是空操作）。 */
static void gitem_markEffectDirty(const XGraphicsItem* item,
                                  const XGraphicsEffect* effect,
                                  const XRectF* alsoUnion)
{
    XRectF bounds;
    if (!item || !item->m_scene) return;
    bounds = gitem_effectiveSceneBounds(item, effect);
    if (alsoUnion && alsoUnion->width > 0.0f && alsoUnion->height > 0.0f) {
        if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
            bounds = *alsoUnion;
        } else {
            float x1 = bounds.x < alsoUnion->x ? bounds.x : alsoUnion->x;
            float y1 = bounds.y < alsoUnion->y ? bounds.y : alsoUnion->y;
            float x2 = (bounds.x + bounds.width) >
                               (alsoUnion->x + alsoUnion->width)
                           ? (bounds.x + bounds.width)
                           : (alsoUnion->x + alsoUnion->width);
            float y2 = (bounds.y + bounds.height) >
                               (alsoUnion->y + alsoUnion->height)
                           ? (bounds.y + bounds.height)
                           : (alsoUnion->y + alsoUnion->height);
            bounds.x = x1;
            bounds.y = y1;
            bounds.width = x2 - x1;
            bounds.height = y2 - y1;
        }
    }
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;
    XGraphicsScene_updateRect(item->m_scene, &bounds);
}

bool XGraphicsItem_graphicsEffectDetach(XGraphicsItem* self,
                                        XGraphicsEffect* effect)
{
    if (!self || !effect || self->m_graphicsEffect != effect) return false;
    /* 只摘槽位，不释放 effect（调用方正在销毁它）。标脏按**不含效果环**的
     * 项包围盒记：此刻效果可能在析构途中，再调其 boundingRectFor 虚槽会
     * 读到派生类已拆字段（Qt 的对应路径由 scene 的失效重算兜住）。 */
    self->m_graphicsEffect = NULL;
    gitem_markEffectDirty(self, NULL, NULL);
    return true;
}

void XGraphicsItem_setGraphicsEffect(XGraphicsItem* self,
                                     XGraphicsEffect* effect)
{
    XRectF oldBounds;
    bool hadOld;
    if (!self || self->m_graphicsEffect == effect) return;
    hadOld = (self->m_graphicsEffect != NULL);
    if (hadOld) oldBounds = gitem_effectiveSceneBounds(self, self->m_graphicsEffect);
    if (self->m_graphicsEffect) {
        /* ① 换装/卸载先删旧（对标 `delete d_ptr->graphicsEffect;
         *    d_ptr->graphicsEffect = nullptr;`，qgraphicsitem.cpp:2905-2908）。 */
        XGraphicsEffect* old = self->m_graphicsEffect;
        self->m_graphicsEffect = NULL;
        /* 断源通知（复核补强，2026-11-17）：换装/卸载（effect==NULL）都必须
         * 发 SourceDetached。Qt 侧由 ~QGraphicsEffect 的
         * `d->setGraphicsEffectSource(nullptr)` 统一发（qgraphicseffect.cpp:
         * 379-383，旧源存在 ⇒ `flags |= SourceDetached`），自定义效果据此
         * 清缓存；此前本路径直接 setItemSource(NULL) 静默断开，派生效果
         * 收不到 Detached（项→项/控件→项夺源路径已由 setItemSource 发，
         * 唯独「摘掉/换掉」缺失）。顺序与控件路径一致：通知时 source()
         * 仍指旧源，随后才断关联并删效果。 */
        XGraphicsEffect_sourceChanged_base(old,
                                           XGraphicsEffectChange_SourceDetached);
        /* 先断反向关联再删（防效果 Deinit 槽反查已换掉的槽位）。 */
        XGraphicsEffect_setItemSource(old, NULL);
        XClassDelete(old);
    }
    if (effect) {
        /* ② 同一效果只能有一个宿主：挂在别的图形项/控件上时先摘下
         *    （Qt：If effect is the installed effect on a different item,
         *    setGraphicsEffect() will remove the effect from the item）。 */
        XGraphicsItem* previous = XGraphicsEffect_itemSource(effect);
        if (previous && previous != self) {
            XRectF prevBounds = gitem_effectiveSceneBounds(previous, effect);
            if (previous->m_graphicsEffect == effect)
                previous->m_graphicsEffect = NULL;
            gitem_markEffectDirty(previous, NULL, &prevBounds);
        }
        /* 断源通知由效果层统一承担（XGraphicsEffect_setItemSource：旧图形项 /
         * 旧控件存在时先发 SourceDetached，对标 Qt QGraphicsEffectPrivate::
         * setGraphicsEffectSource，qgraphicseffect_p.h:121-134；控件侧夺源
         * 走 XGraphicsEffect_setSource 的同一套通知）——此处不重复发。 */
        self->m_graphicsEffect = effect;
        /* 摘旧宿主（控件源由 setItemSource 内部经 XWidget_graphicsEffectDetach
         * 清槽；另一图形项上面已清）并建立新关联，其间发出 SourceDetached。 */
        XGraphicsEffect_setItemSource(effect, self);
        /* 挂接语义 = 源附着（Qt 由 setGraphicsEffectSource 触发一次
         * sourceChanged(SourceAttached)）。 */
        XGraphicsEffect_sourceChanged_base(effect,
                                           XGraphicsEffectChange_SourceAttached);
    }
    /* ③ 标脏：旧（含旧效果环）∪ 新（含新效果环）。 */
    gitem_markEffectDirty(self, effect, hadOld ? &oldBounds : NULL);
}

#endif /* XPAINTER_PATH_ON */

#endif /* XGUI_ON */
