/**
 * @file       XGraphicsItemAlignTest.c
 * @brief      XGraphicsItem 与 Qt 6.8 QGraphicsItem 对齐契约测试。
 * @details    覆盖场景图基类十项验收（对齐依据：Qt 6.8.3 原文 +
 *             本机 Qt 6.8.0 offscreen 探针逐点对拍，探针源码/输出见
 *             /tmp/gitem_probe/probe1~4.cpp，关键数值直接写进断言）：
 *             ① 三层父子 mapTo/mapFromItem 往返恒等（旋转+缩放+平移）；
 *             ② sceneBoundingRect 与手工矩阵对拍（含 probe2 数值锚点）；
 *             ③ 父析构级联删子（~QGraphicsItem qgraphicsitem.cpp:1566）；
 *             ④ 循环父子拒绝（自身=Qt 警告返回；祖先→后代 Qt 6.8.0
 *                实测挂死=undefined，XGui 取安全分支拒绝）；
 *             ⑤ 重挂语义（**Qt 真语义**：pos 不变、scenePos 随新父链变——
 *                Qt 6.8.0 探针 §3 实测；任务书「保持 scenePos」与 Qt 相悖，
 *                详见 XGraphicsItem.h 文件头注）；
 *             ⑥ childItems z 升序、同 z 插入序、stackBefore 同级调整；
 *             ⑦ 隐藏父⇒子 effective 隐藏（visible+explicitlyHidden 双位
 *                模型，探针 §10 全分支）；
 *             ⑧ contains 经 shape/变换（缺省 shape=boundingRect 矩形，
 *                椭圆子类覆写 shape 后命中随形）；
 *             ⑨ 泄漏零容忍（运行器默认门，用例内全量释放）；
 *             ⑩ Qt 6.8.0 探针逐点对拍（≤1e-4 相对容差）：三层 mapToScene
 *                五点、itemTransform 双向、sceneTransform 六元、
 *                scenePos、sceneBoundingRect、childrenBoundingRect、
 *                effectiveOpacity 两态。
 * @note       对拍几何（与 probe2 完全同参）：G0 pos(12,-8) 无变换；
 *             G1 pos(6,9) rotate30·scale1.5；G2 pos(-4,7) rotate(-15)·
 *             scale0.8；G3 pos(3.5,-2.5) rotate45·scale1.2，包围盒
 *             40x30/24x18/16x12/12x10。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGraphicsItem.h"
#include "XObject.h"
#include "XClass.h"
#include "XMemory.h"
#include "XPainter.h"
#include "XVector.h"

/* ==================== 探针图元（矩形） ==================== */

/*
 * 具体图元探针：覆写 BoundingRect（返回 (0,0,w,h)）与 Paint（记账），
 * 另挂 EXClass_Deinit 记账存活数（级联删除判据）。
 */
XCLASS_DEFINE_BEGING(XGraphicsRectProbe)
XCLASS_DEFINE_EXTEND_END(XGraphicsRectProbe, XGraphicsItem)

typedef struct XGraphicsRectProbe
{
    XGraphicsItem m_base; /**< 基类；必须第一个。 */
    float m_w;            /**< 包围盒宽（局部 (0,0,m_w,m_h)）。 */
    float m_h;            /**< 包围盒高。 */
} XGraphicsRectProbe;

static int s_rectAlive = 0;  /**< 存活矩形探针数（deinit 记账）。 */
static int s_rectPaints = 0; /**< Paint 槽位派发次数（共享计数，用例内重置）。 */

static XRectF xq_rectProbeBoundingRect(const XGraphicsItem* self)
{
    XGraphicsRectProbe* probe = (XGraphicsRectProbe*)self;
    XRectF out;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = probe ? probe->m_w : 0.0f;
    out.height = probe ? probe->m_h : 0.0f;
    return out;
}

static void xq_rectProbePaint(XGraphicsItem* self, XPainter* painter)
{
    (void)painter;
    if (!self) return;
    ++s_rectPaints;
}

static void xq_rectProbeDeinit(XGraphicsItem* base)
{
    --s_rectAlive;
    /* 回落父类（XGraphicsItem）的 Deinit：级联删子 + 摘离父项 + XObject 清尾
     * （_base 契约：派生槽内回落父类必须 XClass_Parent）。 */
    XClass_Deinit_Parent(XGraphicsItem, base);
}

static XVtable* XGraphicsRectProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsRectProbe)
    XVTABLE_INHERIT_XCLASS(XGraphicsItem);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xq_rectProbeDeinit);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_BoundingRect, xq_rectProbeBoundingRect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Paint, xq_rectProbePaint);
    return XVTABLE_DEFAULT;
}

/** @brief 堆上创建矩形探针（调用方经根删除或 XClassDelete 释放）。 */
static XGraphicsRectProbe* xq_rectProbeCreate(float w, float h)
{
    XGraphicsRectProbe* p =
        (XGraphicsRectProbe*)XMemory_malloc(sizeof(XGraphicsRectProbe),
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!p) return NULL;
    XMemset(p, 0, sizeof(*p));
    XGraphicsItem_init(&p->m_base);
    XClassSetVtable(p, XGraphicsRectProbe);
    Set_Class_Memory(p, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(p, true);
    p->m_w = w;
    p->m_h = h;
    ++s_rectAlive;
    return p;
}

/* ==================== 探针图元（椭圆：覆写 Shape） ==================== */

XCLASS_DEFINE_BEGING(XGraphicsEllipseProbe)
XCLASS_DEFINE_EXTEND_END(XGraphicsEllipseProbe, XGraphicsItem)

typedef struct XGraphicsEllipseProbe
{
    XGraphicsItem m_base; /**< 基类；必须第一个。 */
    float m_w;            /**< 椭圆外接矩形宽。 */
    float m_h;            /**< 椭圆外接矩形高。 */
} XGraphicsEllipseProbe;

static int s_ellipseAlive = 0;

static XRectF xq_ellipseProbeBoundingRect(const XGraphicsItem* self)
{
    XGraphicsEllipseProbe* probe = (XGraphicsEllipseProbe*)self;
    XRectF out;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = probe ? probe->m_w : 0.0f;
    out.height = probe ? probe->m_h : 0.0f;
    return out;
}

static bool xq_ellipseProbeShape(const XGraphicsItem* self, XPainterPath* out)
{
    XGraphicsEllipseProbe* probe = (XGraphicsEllipseProbe*)self;
    XRect r;
    if (!probe || !out) return false;
    r.x = 0;
    r.y = 0;
    r.width = (int)probe->m_w;
    r.height = (int)probe->m_h;
    /* 对标 QGraphicsEllipseItem：shape = 椭圆路径（覆盖缺省「矩形形状」）。 */
    XPainterPath_addEllipse(out, &r);
    return true;
}

static void xq_ellipseProbeDeinit(XGraphicsItem* base)
{
    --s_ellipseAlive;
    XClass_Deinit_Parent(XGraphicsItem, base);
}

static XVtable* XGraphicsEllipseProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsEllipseProbe)
    XVTABLE_INHERIT_XCLASS(XGraphicsItem);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xq_ellipseProbeDeinit);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_BoundingRect, xq_ellipseProbeBoundingRect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Shape, xq_ellipseProbeShape);
    return XVTABLE_DEFAULT;
}

static XGraphicsEllipseProbe* xq_ellipseProbeCreate(float w, float h)
{
    XGraphicsEllipseProbe* p =
        (XGraphicsEllipseProbe*)XMemory_malloc(sizeof(XGraphicsEllipseProbe),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
    if (!p) return NULL;
    XMemset(p, 0, sizeof(*p));
    XGraphicsItem_init(&p->m_base);
    XClassSetVtable(p, XGraphicsEllipseProbe);
    Set_Class_Memory(p, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(p, true);
    p->m_w = w;
    p->m_h = h;
    ++s_ellipseAlive;
    return p;
}

/* ==================== 公共助手 ==================== */

/** @brief 相对容差比较（验收口径：差 ≤1e-4 相对；小值按 1 兜底）。 */
static bool xq_closeRel(double a, double b)
{
    double d = a - b;
    double m = b < 0 ? -b : b;
    if (d < 0) d = -d;
    if (m < 1.0) m = 1.0;
    return d <= 1e-4 * m;
}

/** @brief 点断言（对拍容差）。 */
static void xq_checkPointF(const char* tag, double ax, double ay, double bx,
                           double by)
{
    xq_check_impl(xq_closeRel(ax, bx) && xq_closeRel(ay, by), tag, __FILE__,
                  __LINE__, "got=(%.9f,%.9f) want=(%.9f,%.9f)", ax, ay, bx, by);
}

/** @brief 矩形断言（对拍容差，逐分量）。 */
static void xq_checkRectF(const char* tag, const XRectF* a, const XRectF* b)
{
    xq_check_impl(a && b && xq_closeRel(a->x, b->x) && xq_closeRel(a->y, b->y) &&
                      xq_closeRel(a->width, b->width) &&
                      xq_closeRel(a->height, b->height),
                  tag, __FILE__, __LINE__,
                  "got=(%.9f,%.9f,%.9f,%.9f) want=(%.9f,%.9f,%.9f,%.9f)",
                  a ? (double)a->x : 0.0, a ? (double)a->y : 0.0,
                  a ? (double)a->width : 0.0, a ? (double)a->height : 0.0,
                  b ? (double)b->x : 0.0, b ? (double)b->y : 0.0,
                  b ? (double)b->width : 0.0, b ? (double)b->height : 0.0);
}

/** @brief 按 probe2 同参构造 rotate(a)·scale(s) 矩阵（Qt：t.rotate 后
 *  t.scale——rotate 前乘、scale 后乘，probe4 实测）。 */
static void xq_makeRotateScale(XImageTransform* m, float deg, float scale)
{
    XGraphicsItem_matrixIdentity(m);
    XGraphicsItem_matrixRotate(m, deg);
    XGraphicsItem_matrixScale(m, scale, scale);
}

/** @brief 对拍树装配（与 Qt probe2 完全同参；out[0]=根 G0，删除根即级联
 *  G1..G3）。 */
static void xq_buildProbeTree(XGraphicsRectProbe** out)
{
    XGraphicsRectProbe* g0;
    XGraphicsRectProbe* g1;
    XGraphicsRectProbe* g2;
    XGraphicsRectProbe* g3;
    XPointF pos;
    out[0] = g0 = xq_rectProbeCreate(40.0f, 30.0f);
    out[1] = g1 = xq_rectProbeCreate(24.0f, 18.0f);
    out[2] = g2 = xq_rectProbeCreate(16.0f, 12.0f);
    out[3] = g3 = xq_rectProbeCreate(12.0f, 10.0f);
    XGraphicsItem_setParentItem(&g1->m_base, &g0->m_base);
    XGraphicsItem_setParentItem(&g2->m_base, &g1->m_base);
    XGraphicsItem_setParentItem(&g3->m_base, &g2->m_base);
    XPointF_init(&pos, 12.0f, -8.0f);
    XGraphicsItem_setPos(&g0->m_base, &pos);
    XPointF_init(&pos, 6.0f, 9.0f);
    XGraphicsItem_setPos(&g1->m_base, &pos);
    XPointF_init(&pos, -4.0f, 7.0f);
    XGraphicsItem_setPos(&g2->m_base, &pos);
    XPointF_init(&pos, 3.5f, -2.5f);
    XGraphicsItem_setPos(&g3->m_base, &pos);
    {
        XImageTransform t;
        xq_makeRotateScale(&t, 30.0f, 1.5f);
        XGraphicsItem_setTransform(&g1->m_base, &t, false);
        xq_makeRotateScale(&t, -15.0f, 0.8f);
        XGraphicsItem_setTransform(&g2->m_base, &t, false);
        xq_makeRotateScale(&t, 45.0f, 1.2f);
        XGraphicsItem_setTransform(&g3->m_base, &t, false);
    }
}

/* ==================== ⓪ 矩阵代数（QTransform 方向语义） ==================== */

XQ_TEST(XGraphicsItem, matrixAlgebraQtDirections)
{
    /* Qt probe4 实测锚点：rotate 前乘、scale 后乘、translate 前乘。 */
    XImageTransform m;
    XImageTransform t;
    XImageTransform r;
    XImageTransform tr;
    XImageTransform inv;
    XPointF p;
    XPointF q;

    /* rotate(30)·scale(1.5)：m11=1.299038106 m12=0.75 m21=-0.75 m22=1.299038106 */
    xq_makeRotateScale(&m, 30.0f, 1.5f);
    XQ_CHECK(xq_closeRel(m.m11, 1.299038106));
    XQ_CHECK(xq_closeRel(m.m12, 0.75));
    XQ_CHECK(xq_closeRel(m.m21, -0.75));
    XQ_CHECK(xq_closeRel(m.m22, 1.299038106));

    /* rotate(30) 后 translate(5,8)：前乘 → dx=0.330127019 dy=9.428203230
     * （= 5·cos30 + 8·(-sin30) / 5·sin30 + 8·cos30，probe4 实测）。 */
    xq_makeRotateScale(&m, 30.0f, 1.0f);
    XGraphicsItem_matrixTranslate(&m, 5.0f, 8.0f);
    XQ_CHECK(xq_closeRel(m.dx, 0.330127019));
    XQ_CHECK(xq_closeRel(m.dy, 9.428203230));
    XGraphicsItem_matrixMapPoint(&m, 1.0f, 0.0f, &p);
    XQ_CHECK(xq_closeRel(p.x, 1.196152423));
    XQ_CHECK(xq_closeRel(p.y, 9.928203230));

    /* 乘法顺序：(T·R) 先平移后旋转——(a*b).map(p)=b(a(p))。T(10,0)·R(90)
     * 作用于 (1,0)：先到 (11,0)，再旋转 90°（Qt 正角 y 向下呈顺时针）→
     * (0,11)。 */
    XGraphicsItem_matrixFromTranslate(&t, 10.0f, 0.0f);
    XGraphicsItem_matrixIdentity(&r);
    XGraphicsItem_matrixRotate(&r, 90.0f);
    XGraphicsItem_matrixMultiply(&tr, &t, &r);
    XGraphicsItem_matrixMapPoint(&tr, 1.0f, 0.0f, &p);
    XQ_CHECK(xq_closeRel(p.x, 0.0));
    XQ_CHECK(xq_closeRel(p.y, 11.0));

    /* 求逆往返：rot30·scale1.5·trans(6,9)（probe4 t4：dx=1.044228634
     * dy=16.191342951）——map(0,0)=(dx,dy)，逆映射应回原点；map(1,0) 后
     * 逆映射回 (1,0)。 */
    xq_makeRotateScale(&m, 30.0f, 1.5f);
    XGraphicsItem_matrixTranslate(&m, 6.0f, 9.0f);
    XQ_CHECK(xq_closeRel(m.dx, 1.044228634));
    XQ_CHECK(xq_closeRel(m.dy, 16.191342951));
    XQ_REQUIRE(XGraphicsItem_matrixInvert(&m, &inv));
    XGraphicsItem_matrixMapPoint(&inv, m.dx, m.dy, &p);
    XQ_CHECK(xq_closeRel(p.x, 0.0));
    XQ_CHECK(xq_closeRel(p.y, 0.0));
    XGraphicsItem_matrixMapPoint(&m, 1.0f, 0.0f, &q);
    XGraphicsItem_matrixMapPoint(&inv, q.x, q.y, &p);
    XQ_CHECK(xq_closeRel(p.x, 1.0));
    XQ_CHECK(xq_closeRel(p.y, 0.0));

    /* 奇异阵求逆失败并回单位阵（Qt inverted ok=false 同口径）。 */
    {
        XImageTransform sing;
        XGraphicsItem_matrixIdentity(&sing);
        sing.m11 = 0.0f;
        sing.m22 = 0.0f;
        XQ_CHECK(!XGraphicsItem_matrixInvert(&sing, &inv));
        XQ_CHECK(xq_closeRel(inv.m11, 1.0));
        XQ_CHECK(xq_closeRel(inv.m22, 1.0));
    }

    /* 【P1 修订锚点】带平移矩阵再 scale：平移分量**不参与缩放**
     * （Qt 6.8.0 probe5 实测 translate(5,8).scale(2,2) → m11=2 dx=5 dy=8，
     * map(3,1)=(11,10)；修订前误缩放为 dx=10/dy=16）。 */
    XGraphicsItem_matrixFromTranslate(&m, 5.0f, 8.0f);
    XGraphicsItem_matrixScale(&m, 2.0f, 2.0f);
    XQ_CHECK(xq_closeRel(m.m11, 2.0));
    XQ_CHECK(xq_closeRel(m.dx, 5.0));
    XQ_CHECK(xq_closeRel(m.dy, 8.0));
    XGraphicsItem_matrixMapPoint(&m, 3.0f, 1.0f, &p);
    XQ_CHECK(xq_closeRel(p.x, 11.0));
    XQ_CHECK(xq_closeRel(p.y, 10.0));

    /* 非均匀：rot30 后 scale(2,3) 线性项 m11×sx/m12×sx/m21×sy/m22×sy，
     * 再 translate(6,9) 前乘平移（probe5 实测三组锚点）。 */
    XGraphicsItem_matrixIdentity(&m);
    XGraphicsItem_matrixRotate(&m, 30.0f);
    XGraphicsItem_matrixScale(&m, 2.0f, 3.0f);
    XQ_CHECK(xq_closeRel(m.m11, 1.732050808));
    XQ_CHECK(xq_closeRel(m.m12, 1.0));
    XQ_CHECK(xq_closeRel(m.m21, -1.5));
    XQ_CHECK(xq_closeRel(m.m22, 2.598076211));
    XGraphicsItem_matrixTranslate(&m, 6.0f, 9.0f);
    XQ_CHECK(xq_closeRel(m.dx, -3.107695155));
    XQ_CHECK(xq_closeRel(m.dy, 29.382685902));
}

/* ==================== ① 三层 mapTo/mapFromItem 往返恒等 ==================== */

XQ_TEST(XGraphicsItem, mapRoundTripThreeLayer)
{
    XGraphicsRectProbe* tree[4];
    XPointF p;
    XPointF q;
    xq_buildProbeTree(tree);

    /* ①a mapToItem 往返恒等：G3→G1 再 mapFromItem(G1) 回原值。 */
    XPointF_init(&p, 5.0f, -5.0f);
    q = XGraphicsItem_mapToItem(&tree[3]->m_base, &tree[1]->m_base, &p);
    p = XGraphicsItem_mapFromItem(&tree[3]->m_base, &tree[1]->m_base, &q);
    XQ_CHECK(xq_closeRel(p.x, 5.0));
    XQ_CHECK(xq_closeRel(p.y, -5.0));

    /* ①b 跨两层：G1→G3（Qt probe2 数值 (2.740069789,-3.197905277)）。 */
    XPointF_init(&p, 2.0f, 3.0f);
    q = XGraphicsItem_mapToItem(&tree[1]->m_base, &tree[3]->m_base, &p);
    xq_checkPointF("G1->G3 (2,3)", q.x, q.y, 2.740069789, -3.197905277);

    /* ①c 反向：G3→G1（Qt probe2 数值 (-4.140122358,5.494069893)）。 */
    XPointF_init(&p, -1.5f, 2.25f);
    q = XGraphicsItem_mapToItem(&tree[3]->m_base, &tree[1]->m_base, &p);
    xq_checkPointF("G3->G1 (-1.5,2.25)", q.x, q.y, -4.140122358, 5.494069893);

    /* ①d mapToParent/mapFromParent：无变换项 = point+pos 快路径。 */
    XPointF_init(&p, 1.0f, 1.0f);
    q = XGraphicsItem_mapToParent(&tree[0]->m_base, &p);
    XQ_CHECK(xq_closeRel(q.x, 13.0)); /* (1,1)+pos(12,-8)。 */
    XQ_CHECK(xq_closeRel(q.y, -7.0));
    q = XGraphicsItem_mapFromParent(&tree[0]->m_base, &q);
    XQ_CHECK(xq_closeRel(q.x, 1.0));
    XQ_CHECK(xq_closeRel(q.y, 1.0));

    /* ①e mapToScene(NULL item) → mapToScene 口径、NULL 点安全。 */
    XPointF_init(&p, 7.0f, 7.0f);
    q = XGraphicsItem_mapToItem(&tree[3]->m_base, NULL, &p);
    XPointF_init(&p, 7.0f, 7.0f);
    {
        XPointF s;
        XPointF_init(&s, 7.0f, 7.0f);
        {
            XPointF want = XGraphicsItem_mapToScene(&tree[3]->m_base, &s);
            XQ_CHECK(xq_closeRel(q.x, want.x));
            XQ_CHECK(xq_closeRel(q.y, want.y));
        }
    }

    XClassDelete(tree[0]); /* 级联删 G1..G3。 */
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== ⑩+② sceneTransform/scenePos/包盒 逐点对拍 ==================== */

XQ_TEST(XGraphicsItem, qtProbeNumericCompare)
{
    XGraphicsRectProbe* tree[4];
    XGraphicsItem* g3;
    XPointF p;
    XPointF q;
    XRectF r;
    XImageTransform st;
    xq_buildProbeTree(tree);
    g3 = &tree[3]->m_base;

    /* ②a mapToScene 五点（Qt probe2 输出逐点；验收⑩ ≤1e-4 相对）。 */
    XPointF_init(&p, 0.0f, 0.0f);
    q = XGraphicsItem_mapToScene(g3, &p);
    xq_checkPointF("G3 mapToScene(0,0)", q.x, q.y, 12.387193183, 5.282529250);
    XPointF_init(&p, 1.0f, 2.0f);
    q = XGraphicsItem_mapToScene(g3, &p);
    xq_checkPointF("G3 mapToScene(1,2)", q.x, q.y, 10.613040020, 7.969605832);
    XPointF_init(&p, -4.0f, 6.0f);
    q = XGraphicsItem_mapToScene(g3, &p);
    xq_checkPointF("G3 mapToScene(-4,6)", q.x, q.y, 2.024733694, 4.614222925);
    XPointF_init(&p, 9.5f, -7.25f);
    q = XGraphicsItem_mapToScene(g3, &p);
    xq_checkPointF("G3 mapToScene(9.5,-7.25)", q.x, q.y, 28.268498399,
                   11.909756774);
    XPointF_init(&p, -6.25f, 8.75f);
    q = XGraphicsItem_mapToScene(g3, &p);
    xq_checkPointF("G3 mapToScene(-6.25,8.75)", q.x, q.y, -3.024726905,
                   3.788300616);

    /* ②b sceneTransform 六元（Qt probe2）。 */
    XGraphicsItem_sceneTransform(g3, &st);
    XQ_CHECK(xq_closeRel(st.m11, 0.720000000));
    XQ_CHECK(xq_closeRel(st.m12, 1.247076581));
    XQ_CHECK(xq_closeRel(st.m21, -1.247076581));
    XQ_CHECK(xq_closeRel(st.m22, 0.720000000));
    XQ_CHECK(xq_closeRel(st.dx, 12.387193183));
    XQ_CHECK(xq_closeRel(st.dy, 5.282529250));

    /* ②c scenePos = mapToScene(0,0)（qgraphicsitem.cpp:3798）。 */
    q = XGraphicsItem_scenePos(g3);
    xq_checkPointF("G3 scenePos", q.x, q.y, 12.387193183, 5.282529250);

    /* ②d sceneBoundingRect（Qt probe2）+ 与手工矩阵对拍（验收②）。 */
    r = XGraphicsItem_sceneBoundingRect(g3);
    {
        XRectF want;
        XRectF br;
        XRectF manual;
        want.x = -0.083572631;
        want.y = 5.282529250;
        want.width = 21.110765814;
        want.height = 22.164918977;
        xq_checkRectF("G3 sceneBoundingRect", &r, &want);
        br.x = 0.0f;
        br.y = 0.0f;
        br.width = 12.0f;
        br.height = 10.0f;
        XGraphicsItem_matrixMapRect(&st, &br, &manual);
        xq_checkRectF("sceneBoundingRect vs manual matrix", &r, &manual);
    }

    /* ②e mapRectToScene/mapRectFromScene：包盒口径经旋转「先缩后放」非恒等
     * （Qt mapRect 同——中间取包盒必外扩）；断言包含性 + 无变换项精确往返。 */
    {
        XRectF back;
        XRectF sceneBR = r; /* = sceneBoundingRect(G3)。 */
        back = XGraphicsItem_mapRectFromScene(g3, &sceneBR);
        r = XGraphicsItem_mapRectToScene(g3, &back);
        /* mapRectToScene(mapRectFromScene(R)) ⊇ R（外扩方向）。 */
        XQ_CHECK(r.x <= sceneBR.x + 1e-3f);
        XQ_CHECK(r.y <= sceneBR.y + 1e-3f);
        XQ_CHECK(r.x + r.width >= sceneBR.x + sceneBR.width - 1e-3f);
        XQ_CHECK(r.y + r.height >= sceneBR.y + sceneBR.height - 1e-3f);
        /* G0 仅平移：往返精确。 */
        {
            XRectF in;
            XRectF mid;
            in.x = 1.0f;
            in.y = 2.0f;
            in.width = 5.0f;
            in.height = 6.0f;
            mid = XGraphicsItem_mapRectToScene(&tree[0]->m_base, &in);
            XQ_CHECK(xq_closeRel(mid.x, 13.0)); /* (1,2)+pos(12,-8)。 */
            XQ_CHECK(xq_closeRel(mid.y, -6.0));
            in = XGraphicsItem_mapRectFromScene(&tree[0]->m_base, &mid);
            XQ_CHECK(xq_closeRel(in.x, 1.0) && xq_closeRel(in.width, 5.0));
        }
    }

    /* ②f childrenBoundingRect：全树（G3 在 G2 下）= 手推复合阵逐角解析值
     * （ownT3·ownT2 复合阵映射 br3 角 (0,10) 给出 x-min=-6.613045692）；
     * G3 重挂到 G0 后（Qt probe2 的取值状态）= 只含 G2 的包盒，与 Qt
     * 输出逐分量一致。 */
    {
        XRectF cbr = XGraphicsItem_childrenBoundingRect(&tree[1]->m_base);
        XRectF want;
        want.x = -6.613045692;
        want.y = 3.687116146;
        want.width = 17.461559296;
        want.height = 14.730182648;
        xq_checkRectF("G1 childrenBoundingRect (full tree)", &cbr, &want);
        cbr = XGraphicsItem_childrenBoundingRect(g3);
        XQ_CHECK(cbr.width <= 0.0f && cbr.height <= 0.0f); /* 叶子 → 空。 */
        /* 重挂到 probe2 的取值状态（G3 在 G0 下）再对拍。 */
        XGraphicsItem_setParentItem(g3, &tree[0]->m_base);
        cbr = XGraphicsItem_childrenBoundingRect(&tree[1]->m_base);
        want.x = -4.000000000;
        want.y = 3.687116223;
        want.width = 14.848513409;
        want.height = 12.585771710;
        xq_checkRectF("G1 childrenBoundingRect (Qt probe2 state)", &cbr, &want);
        XGraphicsItem_setParentItem(g3, &tree[2]->m_base); /* 恢复原树。 */
    }

    /* ②g deviceTransform（logical 版）= sceneTransform ∘ viewport
     * （Qt sceneTransform * viewportTransform 行向量顺序）。 */
    {
        XImageTransform vp;
        XImageTransform dt;
        XGraphicsItem_matrixIdentity(&vp);
        XGraphicsItem_matrixScale(&vp, 2.0f, 2.0f);
        XGraphicsItem_deviceTransform(g3, &vp, &dt);
        XQ_CHECK(xq_closeRel(dt.m11, 1.440000000)); /* 0.72·2。 */
        XQ_CHECK(xq_closeRel(dt.dx, 24.774386366)); /* 12.387193·2。 */
        XGraphicsItem_deviceTransform(g3, NULL, &dt); /* NULL 视口=单位。 */
        XQ_CHECK(xq_closeRel(dt.m11, 0.720000000));
        XQ_CHECK(xq_closeRel(dt.dy, 5.282529250));
    }

    /* ②h effectiveOpacity 两态（probe3 原链=0.04；probe2 重挂后=0.2）。 */
    XGraphicsItem_setOpacity(&tree[0]->m_base, 0.5f);
    XGraphicsItem_setOpacity(&tree[1]->m_base, 0.8f);
    XGraphicsItem_setOpacity(&tree[2]->m_base, 0.25f);
    XGraphicsItem_setOpacity(g3, 0.4f);
    XQ_CHECK(xq_closeRel(XGraphicsItem_effectiveOpacity(g3), 0.04));
    XGraphicsItem_setParentItem(g3, &tree[0]->m_base); /* G2→G0 重挂。 */
    XQ_CHECK(xq_closeRel(XGraphicsItem_effectiveOpacity(g3), 0.20));
    XGraphicsItem_setParentItem(g3, &tree[2]->m_base); /* 恢复原树。 */
    XQ_CHECK(xq_closeRel(XGraphicsItem_effectiveOpacity(g3), 0.04));

    XClassDelete(tree[0]);
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== ③ 父析构级联删子 ==================== */

XQ_TEST(XGraphicsItem, parentDestructorCascade)
{
    XGraphicsRectProbe* root = xq_rectProbeCreate(10.0f, 10.0f);
    XGraphicsRectProbe* c1 = xq_rectProbeCreate(5.0f, 5.0f);
    XGraphicsRectProbe* c2 = xq_rectProbeCreate(5.0f, 5.0f);
    XGraphicsRectProbe* gc = xq_rectProbeCreate(3.0f, 3.0f);
    XGraphicsEllipseProbe* ec = xq_ellipseProbeCreate(6.0f, 6.0f);
    XGraphicsItem_setParentItem(&c1->m_base, &root->m_base);
    XGraphicsItem_setParentItem(&c2->m_base, &root->m_base);
    XGraphicsItem_setParentItem(&gc->m_base, &c1->m_base);
    XGraphicsItem_setParentItem(&ec->m_base, &c2->m_base);
    XQ_EQ_I64(s_rectAlive, 4);
    XQ_EQ_I64(s_ellipseAlive, 1);
    /* 删根：子/孙/异类子全级联（~QGraphicsItem while(!children.isEmpty())
     * delete children.first()）。泄漏门（验收⑨）同时盯住少删/漏删。 */
    XClassDelete(root);
    XQ_EQ_I64(s_rectAlive, 0);
    XQ_EQ_I64(s_ellipseAlive, 0);
}

/* ==================== ④ 循环父子拒绝 ==================== */

XQ_TEST(XGraphicsItem, cycleRejection)
{
    XGraphicsRectProbe* a = xq_rectProbeCreate(5.0f, 5.0f);
    XGraphicsRectProbe* b = xq_rectProbeCreate(5.0f, 5.0f);
    XGraphicsRectProbe* c = xq_rectProbeCreate(5.0f, 5.0f);
    XGraphicsItem_setParentItem(&b->m_base, &a->m_base);
    XGraphicsItem_setParentItem(&c->m_base, &b->m_base);

    /* 自身为父：Qt qgraphicsitem.cpp:1791 警告+返回；关系不变。 */
    XQ_CHECK(!XGraphicsItem_setParentItem(&c->m_base, &c->m_base));
    XQ_CHECK(XGraphicsItem_parentItem(&c->m_base) == &b->m_base);

    /* 祖先→后代（成环）：Qt 文档明言 undefined（qgraphicsitem.cpp:1761）、
     * Qt 6.8.0 探针实测 setParentItem 挂死不返回——XGui 安全分支拒绝。 */
    XQ_CHECK(!XGraphicsItem_setParentItem(&a->m_base, &c->m_base));
    XQ_CHECK(XGraphicsItem_parentItem(&a->m_base) == NULL);
    XQ_CHECK(XGraphicsItem_parentItem(&c->m_base) == &b->m_base);

    /* 同值重挂：同值早退返回 true（Qt 静默 return）。 */
    XQ_CHECK(XGraphicsItem_setParentItem(&b->m_base, &a->m_base));
    XQ_CHECK(XGraphicsItem_parentItem(&b->m_base) == &a->m_base);

    /* 摘父成顶层。 */
    XQ_CHECK(XGraphicsItem_setParentItem(&c->m_base, NULL));
    XQ_CHECK(XGraphicsItem_parentItem(&c->m_base) == NULL);

    XClassDelete(a);
    XClassDelete(c);
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== ⑤ 重挂：pos 不变、scenePos 随父链（Qt 真语义） ====== */

XQ_TEST(XGraphicsItem, reparentKeepsPosMovesScenePos)
{
    XGraphicsRectProbe* tree[4];
    XGraphicsItem* g3;
    XPointF before;
    XPointF after;
    XPointF pos;
    xq_buildProbeTree(tree);
    g3 = &tree[3]->m_base;

    before = XGraphicsItem_scenePos(g3);
    xq_checkPointF("scenePos before reparent", before.x, before.y, 12.387193183,
                   5.282529250);

    /* Qt probe2 §重挂：G3 从 G2 摘下挂到 G0——pos 保持 (3.5,-2.5)，
     * scenePos 变为 G0 链下的 (15.5,-10.5)（= T(12,-8)∘(ownT3·T(3.5,-2.5))
     * 映射原点；Qt 6.8.0 setParentItemHelper 从不调整 pos——任务书
     * 「保持 scenePos」与 Qt 相悖，按 Qt 真语义断言）。 */
    XGraphicsItem_setParentItem(g3, &tree[0]->m_base);
    pos = XGraphicsItem_pos(g3);
    XQ_CHECK(xq_closeRel(pos.x, 3.5));
    XQ_CHECK(xq_closeRel(pos.y, -2.5));
    after = XGraphicsItem_scenePos(g3);
    xq_checkPointF("scenePos after reparent", after.x, after.y, 15.5, -10.5);

    /* 重挂回 G2：scenePos 还原（矩阵口径可逆）。 */
    XGraphicsItem_setParentItem(g3, &tree[2]->m_base);
    after = XGraphicsItem_scenePos(g3);
    xq_checkPointF("scenePos restored", after.x, after.y, 12.387193183,
                   5.282529250);

    /* mapToScene 一致性：scenePos == mapToScene(0,0)。 */
    XPointF_init(&pos, 0.0f, 0.0f);
    after = XGraphicsItem_mapToScene(g3, &pos);
    XQ_CHECK(xq_closeRel(after.x, 12.387193183));
    XQ_CHECK(xq_closeRel(after.y, 5.282529250));

    XClassDelete(tree[0]);
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== ⑥ z 升序 / stackBefore ==================== */

XQ_TEST(XGraphicsItem, childItemsZOrderAndStack)
{
    XGraphicsRectProbe* parent = xq_rectProbeCreate(10.0f, 10.0f);
    XGraphicsRectProbe* z5 = xq_rectProbeCreate(2.0f, 2.0f);
    XGraphicsRectProbe* zn1 = xq_rectProbeCreate(2.0f, 2.0f);
    XGraphicsRectProbe* z0a = xq_rectProbeCreate(2.0f, 2.0f);
    XGraphicsRectProbe* z0b = xq_rectProbeCreate(2.0f, 2.0f);
    XGraphicsRectProbe* top = xq_rectProbeCreate(2.0f, 2.0f);
    XVector* kids;
    XGraphicsItem_setParentItem(&z5->m_base, &parent->m_base);
    XGraphicsItem_setParentItem(&zn1->m_base, &parent->m_base);
    XGraphicsItem_setParentItem(&z0a->m_base, &parent->m_base);
    XGraphicsItem_setParentItem(&z0b->m_base, &parent->m_base);
    XGraphicsItem_setZValue(&z5->m_base, 5.0f);
    XGraphicsItem_setZValue(&zn1->m_base, -1.0f);
    /* z0a/z0b 缺省 z=0（插入序：z0a 先）。 */

    /* ⑥a z 升序（Qt childItems 文档：sorted by stacking order；探针 §5
     * 实测 [-1, 0, 0, 5]），同 z 保持插入序。 */
    kids = XGraphicsItem_childItems(&parent->m_base);
    XQ_REQUIRE(kids != NULL);
    XQ_EQ_SIZE(XContainerSize(kids), 4);
    XQ_CHECK(*(XGraphicsItem**)XVector_at_base(kids, 0) == &zn1->m_base);
    XQ_CHECK(*(XGraphicsItem**)XVector_at_base(kids, 1) == &z0a->m_base);
    XQ_CHECK(*(XGraphicsItem**)XVector_at_base(kids, 2) == &z0b->m_base);
    XQ_CHECK(*(XGraphicsItem**)XVector_at_base(kids, 3) == &z5->m_base);
    XClassDelete(kids); /* 容器归调用方（元素是借用指针）。 */

    /* ⑥b stackBefore（同 z）：z0b 挪到 z0a 前（Qt §8：e2->stackBefore(e1)
     * 后 e2 在前）。 */
    XQ_CHECK(XGraphicsItem_stackBefore(&z0b->m_base, &z0a->m_base));
    kids = XGraphicsItem_childItems(&parent->m_base);
    XQ_CHECK(*(XGraphicsItem**)XVector_at_base(kids, 1) == &z0b->m_base);
    XQ_CHECK(*(XGraphicsItem**)XVector_at_base(kids, 2) == &z0a->m_base);
    XClassDelete(kids);

    /* ⑥c 跨树 stackBefore 拒绝（Qt：cannot stack under ... must be a
     * sibling；探针 §8 实测）。 */
    XQ_CHECK(!XGraphicsItem_stackBefore(&z5->m_base, &top->m_base));
    /* NULL 兄弟拒绝。 */
    XQ_CHECK(!XGraphicsItem_stackBefore(&z5->m_base, NULL));
    /* 自身 stackBefore：无动作成功（Qt sibling==this 直接 return）。 */
    XQ_CHECK(XGraphicsItem_stackBefore(&z5->m_base, &z5->m_base));

    /* ⑥d 不同 z 的 stackBefore 不改变 childItems 的 z 决胜序（z 值优先）。 */
    XQ_CHECK(XGraphicsItem_stackBefore(&z5->m_base, &zn1->m_base));
    kids = XGraphicsItem_childItems(&parent->m_base);
    XQ_CHECK(*(XGraphicsItem**)XVector_at_base(kids, 0) == &zn1->m_base);
    XQ_CHECK(*(XGraphicsItem**)XVector_at_base(kids, 3) == &z5->m_base);
    XClassDelete(kids);

    /* ⑥e setZValue 缺省 0、可改。 */
    XQ_CHECK(XGraphicsItem_zValue(&top->m_base) == 0.0f);
    XGraphicsItem_setZValue(&top->m_base, 2.5f);
    XQ_CHECK(XGraphicsItem_zValue(&top->m_base) == 2.5f);

    XClassDelete(parent);
    XClassDelete(top);
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== ⑦ 可见性：父隐藏 ⇒ 子 effective 隐藏 ==================== */

XQ_TEST(XGraphicsItem, visibilityEffective)
{
    XGraphicsRectProbe* v1 = xq_rectProbeCreate(5.0f, 5.0f);
    XGraphicsRectProbe* v2 = xq_rectProbeCreate(5.0f, 5.0f);
    XGraphicsRectProbe* v3 = xq_rectProbeCreate(5.0f, 5.0f);
    XGraphicsItem_setParentItem(&v2->m_base, &v1->m_base);
    XGraphicsItem_setParentItem(&v3->m_base, &v2->m_base);

    /* 缺省可见（Qt：Items are visible by default）。 */
    XQ_CHECK(XGraphicsItem_isVisible(&v3->m_base));

    /* ⑦a 隐藏父 ⇒ 子孙 effective 隐藏（探针 §10；Qt 的 visible 位同步下沉）。 */
    XGraphicsItem_setVisible(&v1->m_base, false);
    XQ_CHECK(!XGraphicsItem_isVisible(&v2->m_base));
    XQ_CHECK(!XGraphicsItem_isVisible(&v3->m_base));
    XGraphicsItem_setVisible(&v1->m_base, true);
    XQ_CHECK(XGraphicsItem_isVisible(&v2->m_base));
    XQ_CHECK(XGraphicsItem_isVisible(&v3->m_base));

    /* ⑦b 显式隐藏的子在父隐/现循环后仍隐藏（explicitlyHidden）。 */
    XGraphicsItem_setVisible(&v3->m_base, false);
    XGraphicsItem_setVisible(&v1->m_base, false);
    XGraphicsItem_setVisible(&v1->m_base, true);
    XQ_CHECK(!XGraphicsItem_isVisible(&v3->m_base));
    XQ_CHECK(XGraphicsItem_isVisible(&v2->m_base));

    /* ⑦c 父隐藏时显式 show 子不生效（Qt：Don't show child if parent is not
     * visible）；父再现后生效（该子已非 explicitlyHidden）。 */
    XGraphicsItem_setVisible(&v1->m_base, false);
    XGraphicsItem_setVisible(&v3->m_base, true);
    XQ_CHECK(!XGraphicsItem_isVisible(&v3->m_base));
    XGraphicsItem_setVisible(&v1->m_base, true);
    XQ_CHECK(XGraphicsItem_isVisible(&v3->m_base));

    /* ⑦d 重挂随新父可见性（setParentItemHelper 口径）：隐藏父收养可见子
     * ⇒ 子隐藏；显式隐藏子被可见父收养 ⇒ 仍隐藏；摘父成顶层 ⇒ 非显式
     * 隐藏的隐藏项恢复可见、显式隐藏的保持隐藏（Qt 同）。 */
    {
        XGraphicsRectProbe* hidden = xq_rectProbeCreate(4.0f, 4.0f);
        XGraphicsRectProbe* child = xq_rectProbeCreate(4.0f, 4.0f);
        XGraphicsItem_setVisible(&hidden->m_base, false); /* 显式隐藏。 */
        XGraphicsItem_setParentItem(&child->m_base, &hidden->m_base);
        XQ_CHECK(!XGraphicsItem_isVisible(&child->m_base)); /* 随父隐藏。 */
        XGraphicsItem_setVisible(&v3->m_base, false); /* 显式隐藏 v3。 */
        XGraphicsItem_setParentItem(&v3->m_base, &v1->m_base); /* v1 可见。 */
        XQ_CHECK(!XGraphicsItem_isVisible(&v3->m_base)); /* 显式隐藏保持。 */
        XGraphicsItem_setParentItem(&child->m_base, &v1->m_base);
        XQ_CHECK(XGraphicsItem_isVisible(&child->m_base));
        XGraphicsItem_setVisible(&hidden->m_base, true);
        /* 摘父：显式隐藏 → 保持隐藏。 */
        XGraphicsItem_setVisible(&hidden->m_base, false);
        XGraphicsItem_setParentItem(&hidden->m_base, NULL);
        XQ_CHECK(!XGraphicsItem_isVisible(&hidden->m_base));
        /* 摘父：非显式（父链同步隐藏）→ 恢复可见。 */
        XGraphicsItem_setVisible(&v1->m_base, false); /* v2 同步隐藏。 */
        XGraphicsItem_setParentItem(&v2->m_base, NULL);
        XQ_CHECK(XGraphicsItem_isVisible(&v2->m_base));
        XGraphicsItem_setVisible(&v1->m_base, true);
        XGraphicsItem_setParentItem(&v2->m_base, &v1->m_base);
        XClassDelete(child);
        XClassDelete(hidden);
    }

    /* ⑦e isVisibleTo（qgraphicsitem.cpp:2355 原样口径；探针 §10：
     * isVisibleTo(parent) 只看 explicitHidden 位——父隐藏⇒「对父可见」
     * 仍为 true；但对场景（NULL）则 v1 显式隐藏⇒ false）。 */
    XGraphicsItem_setVisible(&v1->m_base, false);
    XQ_CHECK(XGraphicsItem_isVisibleTo(&v2->m_base, &v1->m_base));
    XQ_CHECK(!XGraphicsItem_isVisibleTo(&v3->m_base, NULL)); /* v3 显式隐藏。 */
    XQ_CHECK(!XGraphicsItem_isVisibleTo(&v2->m_base, NULL)); /* v1 显式隐藏。 */
    XGraphicsItem_setVisible(&v1->m_base, true);
    XQ_CHECK(XGraphicsItem_isVisibleTo(&v2->m_base, NULL)); /* v1 现身后可见。 */

    /* ⑦f 隐藏即落选（Qt setVisibleHelper 中段）。 */
    XGraphicsItem_setFlag(&v2->m_base, XGraphicsItemFlag_ItemIsSelectable, true);
    XGraphicsItem_setSelected(&v2->m_base, true);
    XQ_CHECK(XGraphicsItem_isSelected(&v2->m_base));
    XGraphicsItem_setVisible(&v1->m_base, false);
    XQ_CHECK(!XGraphicsItem_isSelected(&v2->m_base));
    XGraphicsItem_setVisible(&v1->m_base, true);

    XClassDelete(v1);
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== ⑧ contains 经 shape/变换 ==================== */

XQ_TEST(XGraphicsItem, containsShapeAndTransform)
{
    XGraphicsRectProbe* rect = xq_rectProbeCreate(10.0f, 10.0f);
    XGraphicsEllipseProbe* ellipse = xq_ellipseProbeCreate(10.0f, 10.0f);
    XGraphicsItem* plain =
        (XGraphicsItem*)XMemory_malloc(sizeof(XGraphicsItem),
                                       XCLASS_DEFAULT_MEMORY_TYPE);
    XPointF p;

    /* ⑧a 缺省 contains = shape = boundingRect 矩形（局部坐标）。 */
    XPointF_init(&p, 5.0f, 5.0f);
    XQ_CHECK(XGraphicsItem_contains_base(&rect->m_base, &p));
    XPointF_init(&p, -1.0f, 0.0f);
    XQ_CHECK(!XGraphicsItem_contains_base(&rect->m_base, &p));
    XPointF_init(&p, 9.99f, 9.99f);
    XQ_CHECK(XGraphicsItem_contains_base(&rect->m_base, &p));
    XPointF_init(&p, 10.01f, 5.0f);
    XQ_CHECK(!XGraphicsItem_contains_base(&rect->m_base, &p));

    /* ⑧b contains 不受自身 pos/transform 影响（局部坐标口径；Qt 探针 §7）。 */
    {
        XPointF pos;
        XImageTransform t;
        XPointF_init(&pos, 100.0f, 100.0f);
        XGraphicsItem_setPos(&rect->m_base, &pos);
        XGraphicsItem_matrixIdentity(&t);
        XGraphicsItem_matrixRotate(&t, 45.0f);
        XGraphicsItem_setTransform(&rect->m_base, &t, false);
        XPointF_init(&p, 5.0f, 5.0f);
        XQ_CHECK(XGraphicsItem_contains_base(&rect->m_base, &p));
        XPointF_init(&p, -1.0f, 0.0f);
        XQ_CHECK(!XGraphicsItem_contains_base(&rect->m_base, &p));
    }

    /* ⑧c 覆写 shape 后命中随形：椭圆外接矩形角点出局（QGraphicsEllipseItem
     * 口径）；boundingRect 仍是外接矩形（Shape 覆写不改包围盒）。 */
    XPointF_init(&p, 0.5f, 0.5f);
    XQ_CHECK(!XGraphicsItem_contains_base(&ellipse->m_base, &p));
    XPointF_init(&p, 5.0f, 5.0f);
    XQ_CHECK(XGraphicsItem_contains_base(&ellipse->m_base, &p));
    XPointF_init(&p, 0.5f, 5.0f);
    XQ_CHECK(XGraphicsItem_contains_base(&ellipse->m_base, &p));
    {
        XRectF br = XGraphicsItem_boundingRect_base(&ellipse->m_base);
        XQ_CHECK(xq_closeRel(br.width, 10.0));
        XQ_CHECK(xq_closeRel(br.height, 10.0));
    }

    /* ⑧d 基类缺省：boundingRect 空矩形（纯虚语义缺省+登记）、shape 空路径、
     * contains 恒 false、paint 空实现不崩。 */
    XQ_REQUIRE(plain != NULL);
    XGraphicsItem_init(plain);
    {
        XRectF br = XGraphicsItem_boundingRect_base(plain);
        XQ_CHECK(br.width <= 0.0f && br.height <= 0.0f);
        XPointF_init(&p, 0.0f, 0.0f);
        XQ_CHECK(!XGraphicsItem_contains_base(plain, &p));
        XGraphicsItem_paint_base(plain, NULL);
        {
            XPainterPath path;
            XPainterPath_init(&path);
            XQ_CHECK(XGraphicsItem_shape_base(plain, &path));
            XQ_CHECK(XPainterPath_isEmpty(&path));
            XPainterPath_deinit(&path);
        }
    }
    XMemory_free(plain, XCLASS_DEFAULT_MEMORY_TYPE);

    /* ⑧e 虚分派确实走派生槽：Paint 计数（椭圆未覆写 Paint → 基类空）。 */
    s_rectPaints = 0;
    XGraphicsItem_paint_base(&rect->m_base, NULL);
    XGraphicsItem_paint_base(&ellipse->m_base, NULL);
    XQ_EQ_I64(s_rectPaints, 1);

    XClassDelete(rect);
    XClassDelete(ellipse);
    XQ_EQ_I64(s_rectAlive, 0);
    XQ_EQ_I64(s_ellipseAlive, 0);
}

/* ==================== ⑩b itemTransform 各分支 / 树查询 ==================== */

XQ_TEST(XGraphicsItem, itemTransformVariants)
{
    XGraphicsRectProbe* tree[4];
    XGraphicsRectProbe* solo = xq_rectProbeCreate(4.0f, 4.0f);
    XImageTransform m;
    XPointF p;
    XPointF q;
    XPointF src;
    xq_buildProbeTree(tree);

    /* 父→子：itemTransform(G2,G3) = G2→G3 矩阵；与 itemTransform(G3,G2)
     * （G3→G2）互逆——复合后映射任意点应还原。 */
    {
        XImageTransform m2;
        XImageTransform comp;
        XPointF probe;
        XQ_REQUIRE(XGraphicsItem_itemTransform(&tree[2]->m_base, &tree[3]->m_base, &m));
        XQ_REQUIRE(XGraphicsItem_itemTransform(&tree[3]->m_base, &tree[2]->m_base, &m2));
        XGraphicsItem_matrixMultiply(&comp, &m, &m2);
        probe.x = 7.3f;
        probe.y = -12.1f;
        XGraphicsItem_matrixMapPoint(&comp, probe.x, probe.y, &p);
        XQ_CHECK(xq_closeRel(p.x, 7.3));
        XQ_CHECK(xq_closeRel(p.y, -12.1));
    }

    /* 子→父：itemTransform(G3,G2).map(0,0) = G3 原点在 G2 局部 = (3.5,-2.5)
     * （ownT3 先作用、再加位）。 */
    XQ_REQUIRE(XGraphicsItem_itemTransform(&tree[3]->m_base, &tree[2]->m_base, &m));
    XGraphicsItem_matrixMapPoint(&m, 0.0f, 0.0f, &p);
    XQ_CHECK(xq_closeRel(p.x, 3.5));
    XQ_CHECK(xq_closeRel(p.y, -2.5));

    /* 同项：单位阵（Qt 同支）。 */
    XQ_REQUIRE(XGraphicsItem_itemTransform(&tree[3]->m_base, &tree[3]->m_base, &m));
    XQ_CHECK(xq_closeRel(m.m11, 1.0));
    XQ_CHECK(xq_closeRel(m.dy, 0.0));

    /* 无共同祖先（两棵独立树）：各自根链坐标互映（Qt sceneTransform 组合支
     * 的 logical 口径）；solo 无变换 → 结果=G3 的场景坐标。 */
    XQ_REQUIRE(XGraphicsItem_itemTransform(&tree[3]->m_base, &solo->m_base, &m));
    XGraphicsItem_matrixMapPoint(&m, 0.0f, 0.0f, &p);
    xq_checkPointF("cross-tree itemTransform origin", p.x, p.y, 12.387193183,
                   5.282529250);

    /* NULL 出参/入参安全。 */
    XQ_CHECK(!XGraphicsItem_itemTransform(NULL, &solo->m_base, &m));
    XQ_CHECK(!XGraphicsItem_itemTransform(&tree[0]->m_base, NULL, &m));
    XQ_CHECK(!XGraphicsItem_itemTransform(&tree[0]->m_base, &solo->m_base, NULL));

    /* 树查询：isAncestorOf/commonAncestorItem/topLevelItem。 */
    XQ_CHECK(XGraphicsItem_isAncestorOf(&tree[0]->m_base, &tree[3]->m_base));
    XQ_CHECK(!XGraphicsItem_isAncestorOf(&tree[3]->m_base, &tree[0]->m_base));
    XQ_CHECK(!XGraphicsItem_isAncestorOf(&tree[3]->m_base, &tree[3]->m_base));
    XQ_CHECK(XGraphicsItem_commonAncestorItem(&tree[1]->m_base, &tree[3]->m_base) ==
             &tree[1]->m_base);
    XQ_CHECK(XGraphicsItem_commonAncestorItem(&tree[0]->m_base, &solo->m_base) ==
             NULL);
    XQ_CHECK(XGraphicsItem_topLevelItem(&tree[3]->m_base) == &tree[0]->m_base);
    XQ_CHECK(XGraphicsItem_topLevelItem(&solo->m_base) == &solo->m_base);

    XClassDelete(tree[0]);
    XClassDelete(solo);
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== 标志位与选中联动 ==================== */

XQ_TEST(XGraphicsItem, flagsAndSelection)
{
    XGraphicsRectProbe* it = xq_rectProbeCreate(5.0f, 5.0f);

    /* 缺省无标志（Qt：By default, no flags are enabled）。 */
    XQ_EQ_I64((int)XGraphicsItem_flags(&it->m_base), 0);

    /* 无 ItemIsSelectable 时 setSelected 强制落选（qgraphicsitem.cpp:2744）。 */
    XGraphicsItem_setSelected(&it->m_base, true);
    XQ_CHECK(!XGraphicsItem_isSelected(&it->m_base));

    /* 置位后可选（探针 §11：selected=1）。 */
    XGraphicsItem_setFlag(&it->m_base, XGraphicsItemFlag_ItemIsSelectable, true);
    XGraphicsItem_setSelected(&it->m_base, true);
    XQ_CHECK(XGraphicsItem_isSelected(&it->m_base));
    XQ_EQ_I64((int)XGraphicsItem_flags(&it->m_base),
              (int)XGraphicsItemFlag_ItemIsSelectable);

    /* setFlags 摘除 Selectable 自动落选（探针 §11：selected 1→0）。 */
    XGraphicsItem_setFlags(&it->m_base, 0);
    XQ_CHECK(!XGraphicsItem_isSelected(&it->m_base));

    /* 位值与 Qt 6.8 qgraphicsitem.h:50-52 逐位一致。 */
    XQ_EQ_I64((int)XGraphicsItemFlag_ItemIsMovable, 0x1);
    XQ_EQ_I64((int)XGraphicsItemFlag_ItemIsSelectable, 0x2);
    XQ_EQ_I64((int)XGraphicsItemFlag_ItemIsFocusable, 0x4);

    /* 组合位 + 单位开关。 */
    XGraphicsItem_setFlags(&it->m_base, (XGraphicsItemFlags)(
        XGraphicsItemFlag_ItemIsMovable | XGraphicsItemFlag_ItemIsFocusable));
    XGraphicsItem_setFlag(&it->m_base, XGraphicsItemFlag_ItemIsMovable, false);
    XQ_EQ_I64((int)XGraphicsItem_flags(&it->m_base),
              (int)XGraphicsItemFlag_ItemIsFocusable);

    /* 悬停开关（缺省 false）。 */
    XQ_CHECK(!XGraphicsItem_acceptHoverEvents(&it->m_base));
    XGraphicsItem_setAcceptHoverEvents(&it->m_base, true);
    XQ_CHECK(XGraphicsItem_acceptHoverEvents(&it->m_base));

    XClassDelete(it);
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== 纯借用查询与 NULL 容错 ==================== */

XQ_TEST(XGraphicsItem, nullSafetyAndBorrows)
{
    /* 全 API NULL 容错（框架惯例：NULL 进、安全出）。 */
    XQ_CHECK(XGraphicsItem_parentItem(NULL) == NULL);
    XQ_CHECK(!XGraphicsItem_isVisible(NULL));
    XQ_CHECK(!XGraphicsItem_isSelected(NULL));
    XQ_CHECK(!XGraphicsItem_contains_base(NULL, NULL));
    XQ_CHECK(XGraphicsItem_childItems(NULL) == NULL);
    XQ_CHECK(XGraphicsItem_commonAncestorItem(NULL, NULL) == NULL);
    XQ_CHECK(!XGraphicsItem_setParentItem(NULL, NULL));
    XGraphicsItem_setPos(NULL, NULL);
    XGraphicsItem_setVisible(NULL, true);
    XGraphicsItem_setZValue(NULL, 1.0f);
    XGraphicsItem_setTransform(NULL, NULL, false);
    XGraphicsItem_setOpacity(NULL, 0.5f);
    XGraphicsItem_setAcceptHoverEvents(NULL, true);
    XGraphicsItem_paint_base(NULL, NULL);
    {
        XImageTransform m;
        XPointF p;
        XGraphicsItem_sceneTransform(NULL, &m);
        XQ_CHECK(xq_closeRel(m.m11, 1.0));
        XGraphicsItem_matrixMapPoint(NULL, 1.0f, 2.0f, &p);
        XQ_CHECK(xq_closeRel(p.x, 1.0)); /* 无矩阵 → 原样。 */
        XGraphicsItem_matrixIdentity(NULL); /* 空指针安全。 */
        XGraphicsItem_matrixRotate(NULL, 30.0f);
        XGraphicsItem_matrixScale(NULL, 2.0f, 2.0f);
        XGraphicsItem_matrixTranslate(NULL, 1.0f, 1.0f);
        XGraphicsItem_matrixMultiply(NULL, NULL, NULL);
        {
            XImageTransform tmpinv;
            XQ_CHECK(!XGraphicsItem_matrixInvert(NULL, &tmpinv)); /* NULL 入参=失败。 */
            XQ_CHECK(xq_closeRel(tmpinv.m11, 1.0)); /* 回单位阵。 */
            XGraphicsItem_matrixMapRect(NULL, NULL, NULL); /* 空指针安全。 */
        }
    }

    /* transform_const 借用：返回内部缓冲指针、随 setTransform 内容更新
     * （_const 契约：禁止释放）。 */
    {
        XGraphicsRectProbe* it = xq_rectProbeCreate(5.0f, 5.0f);
        const XImageTransform* t = XGraphicsItem_transform_const(&it->m_base);
        XImageTransform nt;
        XQ_CHECK(t != NULL);
        XQ_CHECK(xq_closeRel(t->m11, 1.0)); /* 缺省单位阵。 */
        xq_makeRotateScale(&nt, 20.0f, 2.0f);
        XGraphicsItem_setTransform(&it->m_base, &nt, false);
        XQ_CHECK(xq_closeRel(t->m11, nt.m11)); /* 同一缓冲。 */
        /* combine=true：matrix 先作用、原矩阵后作用（Qt matrix * transform）。
         * scale3 ∘ (rot20·scale2) 的 m11 = 3·(2·cos20)。 */
        xq_makeRotateScale(&nt, 0.0f, 3.0f);
        XGraphicsItem_setTransform(&it->m_base, &nt, true);
        XQ_CHECK(xq_closeRel(t->m11, nt.m11 * 2.0f * 0.9396926207859084));
        XClassDelete(it);
    }
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== 几何 setter / opacity 边界 ==================== */

XQ_TEST(XGraphicsItem, settersAndClamps)
{
    XGraphicsRectProbe* it = xq_rectProbeCreate(5.0f, 5.0f);
    XPointF p;

    XPointF_init(&p, 3.0f, 4.0f);
    XGraphicsItem_setPos(&it->m_base, &p);
    p = XGraphicsItem_pos(&it->m_base);
    XQ_CHECK(xq_closeRel(p.x, 3.0));
    XQ_CHECK(xq_closeRel(p.y, 4.0));

    /* setPos(NULL) 按 (0,0)。 */
    XGraphicsItem_setPos(&it->m_base, NULL);
    p = XGraphicsItem_pos(&it->m_base);
    XQ_CHECK(xq_closeRel(p.x, 0.0));

    /* opacity 夹到 [0,1]（Qt setOpacity qFuzzyBound）；缺省 1.0。 */
    XGraphicsItem_setOpacity(&it->m_base, 2.0f);
    XQ_CHECK(xq_closeRel(XGraphicsItem_opacity(&it->m_base), 1.0));
    XGraphicsItem_setOpacity(&it->m_base, -0.5f);
    XQ_CHECK(xq_closeRel(XGraphicsItem_opacity(&it->m_base), 0.0));
    XGraphicsItem_setOpacity(&it->m_base, 0.75f);
    XQ_CHECK(xq_closeRel(XGraphicsItem_opacity(&it->m_base), 0.75));
    {
        XGraphicsRectProbe* fresh = xq_rectProbeCreate(1.0f, 1.0f);
        XQ_CHECK(xq_closeRel(XGraphicsItem_opacity(&fresh->m_base), 1.0));
        XClassDelete(fresh);
    }

    XClassDelete(it);
    XQ_EQ_I64(s_rectAlive, 0);
}

/* ==================== 【P2 修订】奇异变换回落=输入原样 ==================== */

XQ_TEST(XGraphicsItem, singularInvertFallbackReturnsInput)
{
    XGraphicsRectProbe* a = xq_rectProbeCreate(5.0f, 5.0f);
    XGraphicsRectProbe* b = xq_rectProbeCreate(5.0f, 5.0f);
    XImageTransform sing;
    XPointF p;
    XPointF out;
    XRectF r;
    XRectF rout;
    /* 奇异变换：单位阵但 m22=0（det = 1·0 - 0·0 = 0）。 */
    XGraphicsItem_matrixIdentity(&sing);
    sing.m22 = 0.0f;
    XGraphicsItem_setTransform(&a->m_base, &sing, false);
    XGraphicsItem_setTransform(&b->m_base, &sing, false);

    /* 求逆失败路径（mapFromScene/mapFromParent 走 invert；跨树
     * mapFromItem/mapToItem 走 itemTransform 内部 invert）→ 回落单位阵
     * 映射 = 输入原样（Qt inverted() 失败回单位阵口径；修订前返回
     * (0,0)/全 0 矩形）。 */
    XPointF_init(&p, 7.3f, -4.1f);
    out = XGraphicsItem_mapFromScene(&a->m_base, &p);
    XQ_CHECK(xq_closeRel(out.x, 7.3));
    XQ_CHECK(xq_closeRel(out.y, -4.1));
    out = XGraphicsItem_mapFromParent(&a->m_base, &p);
    XQ_CHECK(xq_closeRel(out.x, 7.3));
    XQ_CHECK(xq_closeRel(out.y, -4.1));
    out = XGraphicsItem_mapFromItem(&a->m_base, &b->m_base, &p);
    XQ_CHECK(xq_closeRel(out.x, 7.3));
    XQ_CHECK(xq_closeRel(out.y, -4.1));
    out = XGraphicsItem_mapToItem(&a->m_base, &b->m_base, &p);
    XQ_CHECK(xq_closeRel(out.x, 7.3));
    XQ_CHECK(xq_closeRel(out.y, -4.1));

    r.x = 1.0f;
    r.y = 2.0f;
    r.width = 5.0f;
    r.height = 6.0f;
    rout = XGraphicsItem_mapRectFromScene(&a->m_base, &r);
    XQ_CHECK(xq_closeRel(rout.x, 1.0));
    XQ_CHECK(xq_closeRel(rout.y, 2.0));
    XQ_CHECK(xq_closeRel(rout.width, 5.0));
    XQ_CHECK(xq_closeRel(rout.height, 6.0));

    /* 正向映射不受奇异影响（奇异阵仍是合法线性映射）：y' = m22·y = 0。 */
    out = XGraphicsItem_mapToScene(&a->m_base, &p);
    XQ_CHECK(xq_closeRel(out.x, 7.3));
    XQ_CHECK(xq_closeRel(out.y, 0.0));

    XClassDelete(a);
    XClassDelete(b);
    XQ_EQ_I64(s_rectAlive, 0);
}
