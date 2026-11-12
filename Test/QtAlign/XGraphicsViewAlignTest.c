/**
 * @file       XGraphicsViewAlignTest.c
 * @brief      XGraphicsView 与 Qt 6.8 QGraphicsView 对齐契约测试。
 * @details    覆盖 §B17/W3-c 验收十项（对齐依据：Qt 6.8.3 原文 + 本机
 *             Qt 6.8.0 offscreen 探针 /tmp/w3gview_probe/probe_view.cpp
 *             与 probe_dpr_paint.cpp 逐点对拍，关键结论直接写进断言，
 *             锚点标 A~E / DPR）：
 *             ① 默认值 + 恒等映射往返（探针 A：视口变换=平移、
 *                mapToScene/mapFromScene 互逆）；
 *             ② scale(2,2)+rotate(30)+translate(5,8) 后矩阵对拍（探针 B：
 *                viewportTransform==matrix*T(-scroll)、映射与手算
 *                0.00e+00、deviceTransform==sceneTransform∘vp——≤1e-4）；
 *             ③ dpr 完整链（探针 DPR：QT_SCALE_FACTOR=2 下 Qt 的
 *                viewportTransform/deviceTransform/mapToScene 与 dpr=1
 *                **逐字节相同**——dpr 不进矩阵；本用例断言框架侧同构：
 *                矩阵与 dpr 无关 + dpr=2 目标图像物理像素 2 倍覆盖
 *                （probe_dpr_paint 的框架镜像，XPainter hidpi 链
 *                XPainter.c:2351/8312 ← Qt qpainter.cpp:626-643））；
 *             ④ 滚动条驱动平移（翻条后 mapToScene 偏移=−Δvalue）+
 *                滚动范围（recalculateContentSize：range=[round(left),
 *                round(right−w)]、pageStep=视口、singleStep=w/20）；
 *             ⑤ 滚轮（探针 D：普通滚轮滚垂直条、**transform 不变**；
 *                Ctrl 档同样不缩放——Qt QGraphicsView 无内置滚轮缩放，
 *                任务书口径按 Qt 真语义纠正，见 XGraphicsView.h 头注 ②）；
 *             ⑥ renderViewport 像素断言（两项场景：可见位置/被视口边缘
 *                裁剪正确；背景笔刷铺底）+ render(target,source) 的
 *                IgnoreAspectRatio 缩放落位；
 *             ⑦ centerOn（探针 C：条值=round(matrix.map(center)−
 *                viewport/2)）+ centerOnItem；
 *             ⑧ 拖动模式（ScrollHandDrag 手型拖条、RubberBandDrag 实时
 *                选区、NoDrag 不起带）；
 *             ⑨ 泄漏零容忍（运行器缺省门；用例内全量释放）；
 *             ⑩ 既有 XGraphicsScene/XGraphicsItem/XAbstractScrollArea
 *                套件全绿（跑过滤套件）。
 *             ⑪ AnchorUnderMouse（Qt 6.8.3 qgraphicsview.cpp:478-500 的
 *                centerView 分支）：`scale` 前后**鼠标处场景点不变**、
 *                视口中心场景点改变（判别性）；无鼠标事件 ⇒ 回落
 *                AnchorViewCenter（中心点不变）；LEAVE 后同样回落；
 *                setTransformationAnchor(AnchorUnderMouse) 把视口鼠标
 *                跟踪打开（Qt :1370-1371）。
 *             ⑫ 背景笔刷样式分派（Qt drawBackground 原文就是
 *                fillRect(rect, backgroundBrush)，:3709-3723）：Solid 纯色、
 *                LinearGradient 渐变两端/中点像素、TexturePattern 瓦片
 *                周期（2D 棋盘 tile 的 4×4 周期）、HorPattern 标准图案
 *                空行——**没有**「不支持就告警跳过」的样式。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGraphicsView.h"
#include "XGraphicsScene.h"
#include "XGraphicsItem.h"
#include "XAbstractScrollArea_Protected.h"
#include "XObject.h"
#include "XClass.h"
#include "XMemory.h"
#include "XPainter.h"
#include "XImage.h"
#include "XBrush.h"
#include "XGradient.h"       /* XGradient_setColorAt（背景渐变用例）。 */
#include "XLinearGradient.h" /* XLinearGradient_init_ex（背景渐变用例）。 */
#include "XVector.h"
#include "XAlignment.h"
#include "XWidget.h"
#include "XScrollBar.h"
#include "XCoreApplication.h"

#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif

#include <math.h>

/* ==================== 探针图元（填色矩形，可配滚轮接受） ==================== */

XCLASS_DEFINE_BEGING(XGraphicsViewProbe)
XCLASS_DEFINE_EXTEND_END(XGraphicsViewProbe, XGraphicsItem)

typedef struct XGraphicsViewProbe
{
    XGraphicsItem m_base;   /**< 基类；必须第一个。 */
    float m_w;              /**< 包围盒 (0,0,m_w,m_h)。 */
    float m_h;
    uint32_t m_brush;       /**< Paint 填色（0xAARRGGBB）。 */
    int m_wheel;            /**< GRAPHICS_SCENE_WHEEL 次数。 */
    int m_press;            /**< GRAPHICS_SCENE_MOUSE_PRESS 次数。 */
    float m_wheelSceneX;    /**< 最近滚轮事件的场景坐标（断言视图换算）。 */
    float m_wheelSceneY;
    bool m_acceptWheel;     /**< wheel 事件是否 accept。 */
} XGraphicsViewProbe;

static int s_probeAlive = 0;

/** @brief 用例入口归零存活探针计数（复核 2026-11-17 测试卫生项）。
 *  @details 失败路径上 `XQ_REQUIRE` 会立即 return，跳过用例尾部的
 *  `XGraphicsScene_clear` ⇒ 本文件全局的 s_probeAlive 残留，进而让后续
 *  用例的 `s_probeAlive == 0` 断言级联失败（假失败污染）。每例开头归零
 *  使各例互相独立；**用例尾部仍断言 0**——真正的探针泄漏由该断言 +
 *  运行器堆计数双门兜底，不会被掩盖。 */
static void xqv_caseBegin(void)
{
    s_probeAlive = 0;
}

static XRectF xqv_probeBoundingRect(const XGraphicsItem* self)
{
    XGraphicsViewProbe* probe = (XGraphicsViewProbe*)self;
    XRectF out;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = probe ? probe->m_w : 0.0f;
    out.height = probe ? probe->m_h : 0.0f;
    return out;
}

static void xqv_probePaint(XGraphicsItem* self, XPainter* painter)
{
    XGraphicsViewProbe* probe = (XGraphicsViewProbe*)self;
    XRect r;
    if (!probe || !painter) return;
    r.x = 0;
    r.y = 0;
    r.width = (int)probe->m_w;
    r.height = (int)probe->m_h;
    XPainter_fillRect(painter, &r, probe->m_brush);
}

static bool xqv_probeEvent(XGraphicsItem* base, XEvent* event)
{
    XGraphicsViewProbe* probe = (XGraphicsViewProbe*)base;
    if (!probe || !event) return false;
    if (event->m_type == XEVENT_TYPE_GRAPHICS_SCENE_WHEEL) {
        const XWheelEvent* we = (const XWheelEvent*)event;
        ++probe->m_wheel;
        /* 事件 m_position 已由场景/视图换算到项局部坐标（XWheelEvent
         * 承载；场景域事件复用框架既有事件对象）。 */
        probe->m_wheelSceneX = (float)we->m_position.x;
        probe->m_wheelSceneY = (float)we->m_position.y;
        XEvent_setAccepted_base(event, probe->m_acceptWheel);
        return true;
    }
    if (event->m_type == XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_PRESS) {
        ++probe->m_press;
        /* 接受按下（可移动语义的替身：让「场景收下 press」分支可断言）。 */
        XEvent_setAccepted_base(event, probe->m_acceptWheel);
        return true;
    }
    return XClass_Parent(XGraphicsItem, EXGraphicsItem_Event,
                         XGraphicsItem_EventSlot)(base, event);
}

static void xqv_probeDeinit(XGraphicsItem* base)
{
    --s_probeAlive;
    XClass_Deinit_Parent(XGraphicsItem, base);
}

static XVtable* XGraphicsViewProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsViewProbe)
    XVTABLE_INHERIT_XCLASS(XGraphicsItem);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xqv_probeDeinit);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_BoundingRect, xqv_probeBoundingRect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Paint, xqv_probePaint);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Event, xqv_probeEvent);
    return XVTABLE_DEFAULT;
}

static XGraphicsViewProbe* xqv_probeCreate(float w, float h, uint32_t brush)
{
    XGraphicsViewProbe* p =
        (XGraphicsViewProbe*)XMemory_malloc(sizeof(*p),
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!p) return NULL;
    XMemset(p, 0, sizeof(*p));
    XGraphicsItem_init(&p->m_base);
    XClassSetVtable(p, XGraphicsViewProbe);
    Set_Class_Memory(p, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(p, true);
    p->m_w = w;
    p->m_h = h;
    p->m_brush = brush;
    p->m_acceptWheel = true;
    ++s_probeAlive;
    return p;
}

/* ==================== 公共助手 ==================== */

/** @brief 浮点近等（验收容差 1e-4）。 */
static void xq_nearF(const char* tag, float got, float expect)
{
    xq_check_impl(fabsf(got - expect) <= 1.0e-4f, "near", __FILE__, __LINE__,
                  "%s got=%.6f expect=%.6f", tag, got, expect);
}

/** @brief 矩阵近等（逐字段 ≤1e-4）。 */
static void xq_nearMatrix(const char* tag, const XImageTransform* got,
                          const XImageTransform* expect)
{
    xq_nearF(tag, got->m11, expect->m11);
    xq_nearF(tag, got->m12, expect->m12);
    xq_nearF(tag, got->m21, expect->m21);
    xq_nearF(tag, got->m22, expect->m22);
    xq_nearF(tag, got->dx, expect->dx);
    xq_nearF(tag, got->dy, expect->dy);
    xq_nearF(tag, got->m13, expect->m13);
    xq_nearF(tag, got->m23, expect->m23);
    xq_nearF(tag, got->m33, expect->m33);
}

/** @brief 构造探针场景 (0,0,200,150) + 视图 300x200 + 项 A(10,20)+(40x30)
 *  位置 (5,7)——与 Qt 探针 probe_view.cpp 同参。
 *  @note  **不用 XQ_REQUIRE**：本函数返回指针，而 XQ_REQUIRE 展开为
 *  `return;`（xq_align.h:148）⇒ 返回值未定义 + -Wreturn-mismatch 告警
 *  （复核记录项）。失败返回 NULL，由调用方 XQ_REQUIRE 判。 */
static XGraphicsScene* xq_buildScene(void)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsViewProbe* a;
    XRectF r;
    XPointF pos;
    if (!scene) return NULL;
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 200.0f;
    r.height = 150.0f;
    XGraphicsScene_setSceneRect(scene, &r);
    a = xqv_probeCreate(40.0f, 30.0f, 0xFFCC3344u);
    if (!a) {
        XClassDelete((XClass*)scene);
        return NULL;
    }
    pos.x = 5.0f;
    pos.y = 7.0f;
    XGraphicsItem_setPos(&a->m_base, &pos);
    XGraphicsScene_addItem(scene, &a->m_base);
    return scene;
}

/** @brief 手算矩阵：scale(2,2) → rotate(30) → translate(5,8)（探针 B 同
 *  参；构造顺序=QTransform 构造顺序，matrixScale/Rotate/Translate 均
 *  前乘——与 QTransform::scale/rotate/translate 同构）。 */
static void xq_buildBMatrix(XImageTransform* out)
{
    XGraphicsItem_matrixIdentity(out);
    XGraphicsItem_matrixScale(out, 2.0f, 2.0f);
    XGraphicsItem_matrixRotate(out, 30.0f);
    XGraphicsItem_matrixTranslate(out, 5.0f, 8.0f);
}

/* ==================== ① 默认值 + 恒等映射往返（探针 A） ==================== */

XQ_TEST(XGraphicsView, defaultsAndIdentityMapping)
{
    XGraphicsScene* scene;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XVector* list;
    XPointF scenePos;
    XPoint vpPos;
    XImageTransform vp;
    XImageTransform expectT;
    XPoint p;

    xqv_caseBegin();
    scene = xq_buildScene(); /* 探针在归零之后创建（计数只含本例）。 */
    XQ_REQUIRE(view != NULL && scene != NULL);
    /* 默认值（Qt 构造口径）。 */
    XQ_CHECK(XGraphicsView_scene(view) == NULL);
    XQ_EQ_I64(XGraphicsView_dragMode(view), XGraphicsViewDragMode_NoDrag);
    XQ_CHECK(XGraphicsView_isInteractive(view));
    XQ_EQ_I64(XGraphicsView_alignment(view),
              (int)XAlignment_HCenter | (int)XAlignment_VCenter);
    XQ_EQ_I64(XGraphicsView_transformationAnchor(view),
              XGraphicsViewViewportAnchor_AnchorViewCenter);
    XQ_CHECK(XBrush_style(XGraphicsView_backgroundBrush_const(view)) ==
             XBrushStyle_NoBrush);
    /* 无窗口宿主 ⇒ dpr 兜底 1.0（XWindow_devicePixelRatio 空句柄口径）。 */
    xq_nearF("dpr-no-window", XGraphicsView_devicePixelRatio(view), 1.0f);

    XGraphicsView_setScene(view, scene);
    XQ_CHECK(XGraphicsView_scene(view) == scene);
    XGraphicsView_setScene(view, scene); /* 同场景早退（Qt 同）。 */
    XWidget_resize((XWidget*)view, 300, 200);

    /* 场景在册项=1（挂接成功）。 */
    list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
    XQ_REQUIRE(list != NULL);
    XQ_EQ_SIZE(XContainerSize(list), 1);
    XClassDelete(list);

    /* 视口 300x200、场景 200x150 ⇒ 放得下：缩进左 50/上 25（AlignHCenter：
     * leftIndent = 300/2 − (0+200)/2 = 50；topIndent = 100 − 75 = 25）；
     * scroll = 条值 − 缩进 = −50/−25 ⇒ viewportTransform = T(50,25)。 */
    XGraphicsView_viewportTransform(view, &vp);
    XGraphicsItem_matrixFromTranslate(&expectT, 50.0f, 25.0f);
    xq_nearMatrix("vp-identity", &vp, &expectT);

    /* 探针 A 锚点（Qt 视口 298x198 有 1px 帧 ⇒ 其值 49/24；本框架
     * NoFrame 视口 300x200 ⇒ 50/25，公式同源）。 */
    p.x = 50;
    p.y = 40;
    scenePos = XGraphicsView_mapToScene(view, &p);
    xq_nearF("mapToScene.x", scenePos.x, 0.0f);
    xq_nearF("mapToScene.y", scenePos.y, 15.0f);
    scenePos.x = 50.0f;
    scenePos.y = 40.0f;
    vpPos = XGraphicsView_mapFromScene(view, &scenePos);
    XQ_EQ_I64(vpPos.x, 100);
    XQ_EQ_I64(vpPos.y, 65);

    /* 恒等下往返恒等（整数点）。 */
    {
        int ix;
        int iy;
        for (iy = 0; iy <= 200; iy += 50) {
            for (ix = 0; ix <= 300; ix += 75) {
                XPointF back;
                p.x = ix;
                p.y = iy;
                back = XGraphicsView_mapToScene(view, &p);
                scenePos.x = back.x;
                scenePos.y = back.y;
                vpPos = XGraphicsView_mapFromScene(view, &scenePos);
                XQ_EQ_I64(vpPos.x, ix);
                XQ_EQ_I64(vpPos.y, iy);
            }
        }
    }

    /* 条范围 (0,0)：放得下（Qt recalculateContentSize 的 hbar->setRange(0,0)）。 */
    XQ_EQ_I64(XScrollBar_maximum(
                  XAbstractScrollArea_horizontalScrollBar(&view->m_base)), 0);
    XQ_EQ_I64(XScrollBar_maximum(
                  XAbstractScrollArea_verticalScrollBar(&view->m_base)), 0);

    /* 对齐变体（探针 E）：AlignLeft|AlignTop ⇒ 缩进 0、mapToScene(0,0)=(0,0)。 */
    XGraphicsView_setAlignment(view, (int)XAlignment_Left | (int)XAlignment_Top);
    XGraphicsView_viewportTransform(view, &vp);
    XGraphicsItem_matrixFromTranslate(&expectT, 0.0f, 0.0f);
    xq_nearMatrix("vp-alignLeftTop", &vp, &expectT);
    p.x = 0;
    p.y = 0;
    scenePos = XGraphicsView_mapToScene(view, &p);
    xq_nearF("alignLeftTop.x", scenePos.x, 0.0f);
    xq_nearF("alignLeftTop.y", scenePos.y, 0.0f);

    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}

/* ==================== NULL 契约（复核 ② 全量锁定） ======================
 * 两个被复核点名的入口：
 *   ① `render` 在 scene==NULL 时必须是空操作（Qt qgraphicsview.cpp:2110
 *      `if (!d->scene || ...) return;`）——旧实现仍按视图背景笔刷铺底。
 *   ② `updateSceneRect(self, NULL)` 头注承诺「无操作（保留现值）」，旧实现
 *      仍走 recalculateContentSize ⇒ 会把外部改过的条范围重置。
 * 其余 NULL 面（mapToScene/mapFromScene/setScene/centerOn/deviceTransform/
 * 各 getter）按头注逐条断言，防止再退化成未初始化栈值或野指针访问。
 * 【复核根修 2026-11-17】 */
XQ_TEST(XGraphicsView, nullContract)
{
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XGraphicsView* bare = XGraphicsView_create(NULL, 0);
    XGraphicsScene* scene;
    XPointF p;
    XPointF sp;
    XPoint vp;
    XImageTransform expect;
    XImageTransform expectI;
    XImageTransform got;
    XImage img;
    XPainter painter;
    XScrollBar* hbar;
    XScrollBar* vbar;
    XBrush brush;
    XRectF sr;
    uint32_t px;

    xqv_caseBegin();
    scene = xq_buildScene(); /* 探针在归零之后创建。 */
    XQ_REQUIRE(view != NULL && bare != NULL && scene != NULL);

    /* ---- ① 无 self 的恒等映射 + 越界写防护（写 out，不留栈垃圾） ---- */
    p.x = 12.0f;
    p.y = -7.0f;
    sp = XGraphicsView_mapToScene(NULL, &(XPoint){ 12, -7 });
    xq_nearF("null mapToScene.x", sp.x, 12.0f);
    xq_nearF("null mapToScene.y", sp.y, -7.0f);
    sp = XGraphicsView_mapToScene(NULL, NULL); /* 点也 NULL ⇒ (0,0)。 */
    xq_nearF("null mapToScene nil.x", sp.x, 0.0f);
    xq_nearF("null mapToScene nil.y", sp.y, 0.0f);
    vp = XGraphicsView_mapFromScene(NULL, &p);
    XQ_EQ_I64(vp.x, 12);
    XQ_EQ_I64(vp.y, -7);
    vp = XGraphicsView_mapFromScene(NULL, NULL);
    XQ_EQ_I64(vp.x, 0);
    XQ_EQ_I64(vp.y, 0);
    /* 取整口径：2.5 → 3（qRound 远离零），-2.5 → -3（rint 会给 2 / -2）。 */
    p.x = 2.5f;
    p.y = -2.5f;
    vp = XGraphicsView_mapFromScene(NULL, &p);
    XQ_EQ_I64(vp.x, 3);
    XQ_EQ_I64(vp.y, -3);

    /* ---- getter 的 self==NULL 返回值（头注逐条） ---- */
    XQ_CHECK(XGraphicsView_scene(NULL) == NULL);
    XQ_CHECK(!XGraphicsView_isInteractive(NULL));
    XQ_EQ_I64(XGraphicsView_alignment(NULL), 0);
    XQ_EQ_I64(XGraphicsView_dragMode(NULL), XGraphicsViewDragMode_NoDrag);
    XQ_EQ_I64(XGraphicsView_transformationAnchor(NULL),
              XGraphicsViewViewportAnchor_AnchorViewCenter);
    xq_nearF("null dpr", XGraphicsView_devicePixelRatio(NULL), 1.0f);
    XQ_CHECK(XGraphicsView_backgroundBrush_const(NULL) == NULL);
    XQ_CHECK(XGraphicsView_transform_const(NULL) == NULL);
    XQ_CHECK(XGraphicsView_rubberBandRect_const(NULL) == NULL);
    sr = XGraphicsView_sceneRect(NULL);
    XQ_CHECK(sr.width == 0.0f && sr.height == 0.0f);
    XGraphicsView_viewportTransform(NULL, &got);
    XGraphicsItem_matrixIdentity(&expectI);
    xq_nearMatrix("null viewportTransform", &got, &expectI);
    XGraphicsView_viewportTransform(NULL, NULL); /* out NULL=静默返回，不崩。 */

    /* ---- ② setTransform(NULL, combine) 必须回/保持单位阵 ---- */
    xq_buildBMatrix(&expect); /* 先弄成非单位阵。 */
    XGraphicsView_setTransform(view, &expect, false);
    xq_nearMatrix("setTransform non-identity",
                  XGraphicsView_transform_const(view), &expect);
    XGraphicsView_setTransform(view, NULL, false);
    xq_nearMatrix("setTransform NULL ⇒ identity",
                  XGraphicsView_transform_const(view), &expectI);
    /* combine=true 且 NULL：单位阵先作用=原阵（保持在单位阵也一致）。 */
    XGraphicsView_setTransform(view, NULL, true);
    xq_nearMatrix("setTransform NULL combine ⇒ identity",
                  XGraphicsView_transform_const(view), &expectI);

    /* ---- setter 的 self==NULL 空操作（不崩、不产生状态） ---- */
    XGraphicsView_setScene(NULL, scene);
    XGraphicsView_setInteractive(NULL, true);
    XGraphicsView_setAlignment(NULL, (int)XAlignment_Left);
    XGraphicsView_setBackgroundBrush(NULL, NULL);
    XGraphicsView_setDragMode(NULL, XGraphicsViewDragMode_RubberBandDrag);
    XGraphicsView_setTransformationAnchor(NULL,
                                          XGraphicsViewViewportAnchor_NoAnchor);
    XGraphicsView_setSceneRect(NULL, NULL);
    XGraphicsView_resetTransform(NULL);
    XGraphicsView_rotate(NULL, 10.0f);
    XGraphicsView_scale(NULL, 2.0f, 2.0f);
    XGraphicsView_translate(NULL, 1.0f, 1.0f);
    XGraphicsView_centerOn(NULL, NULL);
    XGraphicsView_centerOnItem(NULL, NULL);
    XGraphicsView_invalidateScene(NULL, NULL);
    XGraphicsView_updateSceneRect(NULL, NULL);
    XGraphicsView_renderViewport(NULL, NULL, NULL);
    XGraphicsView_render(NULL, NULL, NULL, NULL);

    /* ---- 有场景视图的 NULL 次参（与「按 (0,0)/(0,0) 处理」头注对拍） --- */
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 300, 200);
    p.x = 0.0f;
    p.y = 0.0f;
    {
        XPoint origin;
        origin.x = 0;
        origin.y = 0;
        sp = XGraphicsView_mapToScene(view, &origin);
    }
    {
        XPointF nilPoint = XGraphicsView_mapToScene(view, NULL);
        xq_nearF("view mapToScene nil.x", nilPoint.x, sp.x);
        xq_nearF("view mapToScene nil.y", nilPoint.y, sp.y);
    }
    vp = XGraphicsView_mapFromScene(view, &p);
    {
        XPoint nilScene = XGraphicsView_mapFromScene(view, NULL);
        XQ_EQ_I64(nilScene.x, vp.x);
        XQ_EQ_I64(nilScene.y, vp.y);
        /* 场景 (0,0) 在居中缩进下映射到 (50,25)——NULL 点确实走了映射。 */
        XQ_EQ_I64(nilScene.x, 50);
        XQ_EQ_I64(nilScene.y, 25);
    }
    /* 矩形映射：矩形 NULL/self NULL ⇒ 空矩形。 */
    {
        XRect vr;
        XRectF rr;
        XRect_init(&vr, 0, 0, 10, 10);
        rr = XGraphicsView_mapToSceneRect(NULL, &vr);
        XQ_CHECK(rr.width == 0.0f && rr.height == 0.0f);
        rr = XGraphicsView_mapToSceneRect(view, NULL);
        XQ_CHECK(rr.width == 0.0f && rr.height == 0.0f);
        rr.x = 0.0f;
        rr.y = 0.0f;
        rr.width = 10.0f;
        rr.height = 10.0f;
        vr = XGraphicsView_mapFromSceneRect(NULL, &rr);
        XQ_CHECK(vr.width == 0 && vr.height == 0);
        vr = XGraphicsView_mapFromSceneRect(view, NULL);
        XQ_CHECK(vr.width == 0 && vr.height == 0);
    }
    /* centerOn(view, NULL) = centerOn((0,0))（探针公式：条值=round(map(0,0)−视口/2)；
     * 场景 200x150 放得下 ⇒ 有缩进、条值不动，断言 lastCenterPoint 落 (0,0)）。 */
    p.x = 100.0f;
    p.y = 75.0f;
    XGraphicsView_centerOn(view, &p);
    XGraphicsView_centerOn(view, NULL);
    xq_nearF("centerOn nil last.x", view->m_lastCenterPoint.x, 0.0f);
    xq_nearF("centerOn nil last.y", view->m_lastCenterPoint.y, 0.0f);
    XGraphicsView_centerOnItem(view, NULL); /* item NULL=空操作。 */

    /* setScene(view, NULL)=摘离：只重算范围，**不**重置 lastCenterPoint /
     * keepLastCenterPoint（Qt qgraphicsview.cpp:1747-1752 的 else 支）。
     * 旧实现无条件复位中心点并置 keep 位，与 Qt 不符。 */
    p.x = 123.0f;
    p.y = 45.0f;
    XGraphicsView_centerOn(view, &p);
    view->m_keepLastCenterPoint = false;
    XGraphicsView_setScene(view, NULL);
    XQ_CHECK(XGraphicsView_scene(view) == NULL);
    xq_nearF("detach keeps last.x", view->m_lastCenterPoint.x, 123.0f);
    xq_nearF("detach keeps last.y", view->m_lastCenterPoint.y, 45.0f);
    XQ_CHECK(!view->m_keepLastCenterPoint);
    XGraphicsView_setScene(view, scene); /* 复位挂接供后续断言。 */
    XQ_CHECK(XGraphicsView_scene(view) == scene);

    /* deviceTransform：self NULL=单位阵；item NULL=viewportTransform 原样。 */
    XGraphicsView_viewportTransform(view, &expect);
    XGraphicsView_deviceTransform(view, NULL, &got);
    xq_nearMatrix("deviceTransform nil item", &got, &expect);
    XGraphicsView_deviceTransform(NULL, NULL, &got);
    XGraphicsItem_matrixIdentity(&expectI);
    xq_nearMatrix("deviceTransform nil self", &got, &expectI);
    XGraphicsView_deviceTransform(view, NULL, NULL); /* out NULL=静默。 */

    /* ---- ②a render 对空场景必须空操作（Qt :2110） ---- */
    brush = XBrush_create_color(XColor_create_rgba(0xFF112233u),
                                XBrushStyle_SolidPattern);
    XGraphicsView_setBackgroundBrush(bare, &brush);
    XQ_CHECK(XGraphicsView_scene(bare) == NULL);
    XImage_init_ex(&img, 40, 40, XImageFormat_ARGB32);
    XQ_REQUIRE(!XImage_isNull(&img));
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    XQ_REQUIRE(XPainter_begin_image(&painter, &img));
    XGraphicsView_render(bare, &painter, NULL, NULL);
    XPainter_deinit(&painter);
    px = XImage_pixel(&img, 20, 20);
    XQ_EQ_I64(px, 0u); /* 无场景=一笔不落（旧实现会铺 0xFF112233 底色）。 */
    /* 同一视图 renderViewport 无场景时按文档仍铺视图背景（对照，证明
     * 上面不是「背景笔刷没生效」的假绿）。 */
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    XQ_REQUIRE(XPainter_begin_image(&painter, &img));
    XGraphicsView_renderViewport(bare, &painter, NULL);
    XPainter_deinit(&painter);
    px = XImage_pixel(&img, 20, 20);
    XQ_EQ_I64(px, 0xFF112233u);
    XClassDeinit((XClass*)&img);

    /* ---- ②b updateSceneRect(NULL)=无操作（不重算条范围） ---- */
    hbar = XAbstractScrollArea_horizontalScrollBar(&view->m_base);
    vbar = XAbstractScrollArea_verticalScrollBar(&view->m_base);
    XQ_REQUIRE(hbar != NULL && vbar != NULL);
    XScrollBar_setRange(hbar, 111, 222);
    XScrollBar_setRange(vbar, 333, 444);
    XGraphicsView_updateSceneRect(view, NULL);
    XQ_EQ_I64(XScrollBar_minimum(hbar), 111);
    XQ_EQ_I64(XScrollBar_maximum(hbar), 222);
    XQ_EQ_I64(XScrollBar_minimum(vbar), 333);
    XQ_EQ_I64(XScrollBar_maximum(vbar), 444);
    /* 非 NULL 才重算（恢复场景口径范围）。 */
    sr.x = 0.0f;
    sr.y = 0.0f;
    sr.width = 200.0f;
    sr.height = 150.0f;
    XGraphicsView_updateSceneRect(view, &sr);
    XQ_CHECK(XScrollBar_minimum(hbar) != 111 ||
             XScrollBar_maximum(hbar) != 222);

    XGraphicsView_setBackgroundBrush(bare, NULL);
    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)bare);
    XClassDelete((XClass*)scene);
}

/* ==================== ② 变换矩阵对拍（探针 B） ==================== */

XQ_TEST(XGraphicsView, transformMapping)
{
    XGraphicsScene* scene;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XGraphicsViewProbe* a;
    XVector* list;
    XImageTransform m;
    XImageTransform vp;
    XImageTransform expectVp;
    XImageTransform step;
    XImageTransform inv;
    XImageTransform dt;
    XImageTransform expectDt;
    XScrollBar* hbar;
    XScrollBar* vbar;
    XPointF sp;
    XPoint p;
    int scrollX;
    int scrollY;

    xqv_caseBegin();
    scene = xq_buildScene(); /* 探针在归零之后创建。 */
    XQ_REQUIRE(view != NULL && scene != NULL);
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 300, 200);

    list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
    XQ_REQUIRE(list != NULL);
    a = *(XGraphicsViewProbe**)XVector_at_base(list, 0);
    XClassDelete(list);

    xq_buildBMatrix(&m);
    XGraphicsView_setTransform(view, &m, false);
    /* 同值早退：重复 set 同阵不翻动（断言矩阵不变即可）。 */
    XGraphicsView_setTransform(view, &m, false);

    hbar = XAbstractScrollArea_horizontalScrollBar(&view->m_base);
    vbar = XAbstractScrollArea_verticalScrollBar(&view->m_base);
    scrollX = XScrollBar_value(hbar);
    scrollY = XScrollBar_value(vbar);

    /* viewportTransform == matrix * T(-scroll)（探针 B 断言同款）。 */
    XGraphicsView_viewportTransform(view, &vp);
    XGraphicsItem_matrixFromTranslate(&step, -(float)scrollX, -(float)scrollY);
    XGraphicsItem_matrixMultiply(&expectVp, &m, &step);
    xq_nearMatrix("vp-transformed", &vp, &expectVp);

    /* mapToScene(QPoint) = inv(matrix).map(p + scroll)（探针 B 手算式，
     * Qt 6.8.0 实测绝对差 0.00e+00）。 */
    p.x = 100;
    p.y = 60;
    sp = XGraphicsView_mapToScene(view, &p);
    XQ_CHECK(XGraphicsItem_matrixInvert(&m, &inv));
    {
        XPointF expect;
        XGraphicsItem_matrixMapPoint(&inv, (float)(p.x + scrollX),
                                     (float)(p.y + scrollY), &expect);
        xq_nearF("mapToScene.x", sp.x, expect.x);
        xq_nearF("mapToScene.y", sp.y, expect.y);
    }
    /* 浮点往返：mapToScene 后再手算映回，落回原视口点（±取整 0.5+1e-4）。 */
    {
        XPointF back;
        XGraphicsItem_matrixMapPoint(&m, sp.x, sp.y, &back);
        back.x -= (float)scrollX;
        back.y -= (float)scrollY;
        xq_nearF("roundtrip.x", back.x, 100.0f);
        xq_nearF("roundtrip.y", back.y, 60.0f);
    }
    /* mapFromScene(QPointF) = round(matrix.map(p) − scroll)（探针 B：
     * 93.7390→94、−56.1581→−56，qRound 口径）。 */
    {
        XPointF fp;
        XPoint vpPos;
        sp.x = 37.4f;
        sp.y = 21.7f;
        XGraphicsItem_matrixMapPoint(&m, sp.x, sp.y, &fp);
        fp.x -= (float)scrollX;
        fp.y -= (float)scrollY;
        vpPos = XGraphicsView_mapFromScene(view, &sp);
        XQ_EQ_I64(vpPos.x, (int)roundf(fp.x));
        XQ_EQ_I64(vpPos.y, (int)roundf(fp.y));
    }

    /* deviceTransform == item.sceneTransform ∘ viewportTransform（探针 B
     * 断言同款；Qt 6.8.0 在 dpr=1/2 下均成立）。 */
    XGraphicsView_deviceTransform(view, &a->m_base, &dt);
    XGraphicsItem_sceneTransform(&a->m_base, &step); /* pos(5,7) ⇒ T(5,7)。 */
    XGraphicsItem_matrixMultiply(&expectDt, &step, &vp);
    xq_nearMatrix("deviceTransform", &dt, &expectDt);

    /* 滚动范围（recalculateContentSize）：viewRect=m.mapRect(0,0,200,150)；
     * 双条 AsNeeded 都启用 ⇒ 算宽各扣对侧条厚（Qt :377-387；探针 B 的
     * 284/14 与本框架逐值同构——探针 298 视口条厚 14，本框架 300 视口
     * 条厚=sizeHint 16 ⇒ 宽 284、高 184）。 */
    {
        XRectF viewRect;
        XRectF sceneR;
        int vsbExt = XWidget_sizeHint_base(
            (XWidget*)XAbstractScrollArea_verticalScrollBar(&view->m_base))
                         .width;
        int hsbExt = XWidget_sizeHint_base(
            (XWidget*)XAbstractScrollArea_horizontalScrollBar(&view->m_base))
                         .height;
        int widthUsed = 300 - vsbExt;
        int heightUsed = 200 - hsbExt;
        int left;
        int right;
        XQ_REQUIRE(vsbExt > 0);
        XQ_REQUIRE(hsbExt > 0);
        sceneR.x = 0.0f;
        sceneR.y = 0.0f;
        sceneR.width = 200.0f;
        sceneR.height = 150.0f;
        XGraphicsItem_matrixMapRect(&m, &sceneR, &viewRect);
        /* 期望取整口径=Qt q_round_bound（**远离零**，非 rintf 的就近取偶）。 */
        left = (int)roundf(viewRect.x);
        right = (int)roundf(viewRect.x + viewRect.width - (float)widthUsed);
        XQ_EQ_I64(XScrollBar_minimum(hbar), left);
        XQ_EQ_I64(XScrollBar_maximum(hbar), right);
        XQ_EQ_I64(XScrollBar_pageStep(hbar), widthUsed);
        XQ_EQ_I64(XScrollBar_singleStep(hbar), widthUsed / 20);
        left = (int)roundf(viewRect.y);
        right = (int)roundf(viewRect.y + viewRect.height - (float)heightUsed);
        XQ_EQ_I64(XScrollBar_minimum(vbar), left);
        XQ_EQ_I64(XScrollBar_maximum(vbar), right);
        XQ_EQ_I64(XScrollBar_pageStep(vbar), heightUsed);
    }

    /* rotate/scale/translate API：前乘组合（Qt :1819/:1832/:1858 的
     * matrix.rotate(...) 后 setTransform(matrix) 同式）。 */
    {
        XImageTransform rotated;
        XImageTransform expectRot;
        rotated = *XGraphicsView_transform_const(view);
        XGraphicsItem_matrixRotate(&rotated, 10.0f);
        XGraphicsView_rotate(view, 10.0f);
        xq_nearMatrix("rotate-compose", XGraphicsView_transform_const(view),
                      &rotated);
        (void)expectRot;
    }
    {
        XImageTransform scaled;
        scaled = *XGraphicsView_transform_const(view);
        XGraphicsItem_matrixScale(&scaled, 1.5f, 0.5f);
        XGraphicsView_scale(view, 1.5f, 0.5f);
        xq_nearMatrix("scale-compose", XGraphicsView_transform_const(view),
                      &scaled);
    }
    {
        XImageTransform translated;
        translated = *XGraphicsView_transform_const(view);
        XGraphicsItem_matrixTranslate(&translated, 3.0f, 4.0f);
        XGraphicsView_translate(view, 3.0f, 4.0f);
        xq_nearMatrix("translate-compose",
                      XGraphicsView_transform_const(view), &translated);
    }
    /* combine=true：新阵先作用（Qt :3860 d->matrix = matrix * d->matrix）。 */
    {
        XImageTransform replace;
        XImageTransform expectC;
        XGraphicsItem_matrixFromTranslate(&replace, 7.0f, 9.0f);
        XGraphicsItem_matrixMultiply(&expectC, &replace,
                                     XGraphicsView_transform_const(view));
        XGraphicsView_setTransform(view, &replace, true);
        xq_nearMatrix("combine", XGraphicsView_transform_const(view), &expectC);
    }
    /* resetTransform 回恒等。 */
    XGraphicsView_resetTransform(view);
    XGraphicsItem_matrixIdentity(&expectVp);
    xq_nearMatrix("reset", XGraphicsView_transform_const(view), &expectVp);
    XQ_CHECK(XGraphicsView_transform_const(view)->m11 == 1.0f);

    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}

/* ==================== ③ dpr 完整链（探针 DPR） ==================== */

/* 渲染视图到目标图像（renderViewport 全视口；painter 生命周期在此收口）。 */
static void xq_renderView(XGraphicsView* view, XImage* img)
{
    XPainter painter;
    bool began;
    XQ_REQUIRE(XGraphicsView_scene(view) != NULL);
    XPainter_init(&painter, NULL);
    began = XPainter_begin_image(&painter, img);
    XQ_REQUIRE(began);
    XGraphicsView_renderViewport(view, &painter, NULL);
    XPainter_deinit(&painter);
}

XQ_TEST(XGraphicsView, deviceTransformDpr)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsViewProbe* red;
    XPointF pos;
    XRectF r;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XImage dpr1;
    XImage dpr2;
    XImageTransform vp1;
    XImageTransform vp2;
    uint32_t px;

    xqv_caseBegin();
    XQ_REQUIRE(scene != NULL && view != NULL);
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 60.0f;
    r.height = 60.0f;
    XGraphicsScene_setSceneRect(scene, &r);
    red = xqv_probeCreate(20.0f, 20.0f, 0xFFFF2222u);
    XQ_REQUIRE(red != NULL);
    pos.x = 10.0f;
    pos.y = 20.0f;
    XGraphicsItem_setPos(&red->m_base, &pos);
    XGraphicsScene_addItem(scene, &red->m_base);

    /* 场景恰同视口（60x60）：缩进 0、scroll 0、viewportTransform=恒等。 */
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 60, 60);
    XGraphicsView_viewportTransform(view, &vp1);
    XGraphicsItem_matrixIdentity(&vp2);
    xq_nearMatrix("vp-60", &vp1, &vp2);

    /* dpr=1：项局部 (10..30,20..40) → 逻辑=物理 (10..30,20..40)。 */
    XImage_init_ex(&dpr1, 60, 60, XImageFormat_ARGB32);
    XQ_REQUIRE(!XImage_isNull(&dpr1));
    XImage_fill(&dpr1, 0u);
    xq_renderView(view, &dpr1);
    px = XImage_pixel(&dpr1, 20, 30);
    XQ_CHECK(((px >> 24) & 0xFF) != 0);
    px = XImage_pixel(&dpr1, 9, 30);
    XQ_EQ_I64((px >> 24) & 0xFF, 0);
    px = XImage_pixel(&dpr1, 31, 30);
    XQ_EQ_I64((px >> 24) & 0xFF, 0);

    /* dpr=2（DPR 探针框架镜像）：图像 120x120 物理、dpr 标签 2 ⇒ 逻辑
     * 60x60；渲染核仍在逻辑域给世界变换，hidpi 链（XPainter.c:2351，
     * Qt qpainter.cpp:626-643 同构）把内容放大到物理 2 倍：
     * 逻辑 (10..30,20..40) → 物理 (20..60,40..80)。 */
    XImage_init_ex(&dpr2, 120, 120, XImageFormat_ARGB32);
    XQ_REQUIRE(!XImage_isNull(&dpr2));
    XImage_setDevicePixelRatio(&dpr2, 2.0f);
    XImage_fill(&dpr2, 0u);
    xq_nearF("dpr2-tag", XImage_devicePixelRatio(&dpr2), 2.0f);
    xq_renderView(view, &dpr2);
    /* 物理锚点：红块中心 (40,60) 有内容；块外 (40,38)/(40,82) 透明。 */
    px = XImage_pixel(&dpr2, 40, 60);
    XQ_CHECK(((px >> 24) & 0xFF) != 0);
    px = XImage_pixel(&dpr2, 40, 38);
    XQ_EQ_I64((px >> 24) & 0xFF, 0);
    px = XImage_pixel(&dpr2, 40, 82);
    XQ_EQ_I64((px >> 24) & 0xFF, 0);
    /* **矩阵与 dpr 无关**（Qt 探针：QT_SCALE_FACTOR=2 下 viewportTransform/
     * deviceTransform/mapToScene 与 dpr=1 逐字节相同——见文件头注 ③）：
     * 渲染前后视图矩阵未动。 */
    XGraphicsView_viewportTransform(view, &vp1);
    xq_nearMatrix("vp-after-dpr2", &vp1, &vp2);

    /* 视图 dpr 访问器：无窗口=1.0；矩阵不因它分叉（头注 ① 的口径落点）。 */
    xq_nearF("view-dpr", XGraphicsView_devicePixelRatio(view), 1.0f);

    XClassDeinit((XClass*)&dpr1); /* 栈上图像：只反初始化（unref 数据）。 */
    XClassDeinit((XClass*)&dpr2);
    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}

/* ==================== ④⑦ 滚动条驱动平移 + centerOn ==================== */

XQ_TEST(XGraphicsView, scrollAndCenterOn)
{
    XGraphicsScene* scene;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XImageTransform m;
    XImageTransform expectVp;
    XImageTransform step;
    XImageTransform vp1;
    XScrollBar* hbar;
    XScrollBar* vbar;
    XPointF center;
    XPointF sp1;
    XPointF sp2;
    XPoint p;
    int scrollX;
    int scrollY;

    xqv_caseBegin();
    scene = xq_buildScene(); /* 探针在归零之后创建。 */
    XQ_REQUIRE(view != NULL && scene != NULL);
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 300, 200);
    xq_buildBMatrix(&m);
    XGraphicsView_setTransform(view, &m, false);

    hbar = XAbstractScrollArea_horizontalScrollBar(&view->m_base);
    vbar = XAbstractScrollArea_verticalScrollBar(&view->m_base);
    scrollX = XScrollBar_value(hbar);
    scrollY = XScrollBar_value(vbar);

    /* centerOn(100,75)（场景中心）：条值 = round(matrix.map(c) − 视口/2)
     * （探针 C：Qt hbar=-50 vbar=150 与手算逐值一致；本框架视口 300x200）。 */
    center.x = 100.0f;
    center.y = 75.0f;
    XGraphicsView_centerOn(view, &center);
    {
        XPointF mapped;
        int expectH;
        int expectV;
        XGraphicsItem_matrixMapPoint(&m, center.x, center.y, &mapped);
        expectH = (int)roundf(mapped.x - 300.0f / 2.0f);
        expectV = (int)roundf(mapped.y - 200.0f / 2.0f);
        XQ_EQ_I64(XScrollBar_value(hbar), expectH);
        XQ_EQ_I64(XScrollBar_value(vbar), expectV);
    }
    /* centerOn 后 viewportTransform 同步（矩阵口径断言在翻条后统一做）。 */
    scrollX = XScrollBar_value(hbar);
    scrollY = XScrollBar_value(vbar);

    /* 翻条驱动平移：Δscene = inv(m).map(Δscroll)（mapToScene 公式的差分
     * 形式；验收④）。 */
    p.x = 150;
    p.y = 100;
    sp1 = XGraphicsView_mapToScene(view, &p);
    XScrollBar_setValue(hbar, XScrollBar_value(hbar) + 10);
    XScrollBar_setValue(vbar, XScrollBar_value(vbar) + 5);
    sp2 = XGraphicsView_mapToScene(view, &p);
    {
        XImageTransform invM;
        /* Δscene = inv(m) 的**线性部分**·Δscroll（delta 映射不含平移项——
         * inv.map 差分中仿射平移相消）。 */
        XQ_CHECK(XGraphicsItem_matrixInvert(&m, &invM));
        xq_nearF("hbar-shift.x", sp2.x - sp1.x,
                 invM.m11 * 10.0f + invM.m21 * 5.0f);
        xq_nearF("vbar-shift.y", sp2.y - sp1.y,
                 invM.m12 * 10.0f + invM.m22 * 5.0f);
    }
    /* viewportTransform 平移项同步（= m * T(-new scroll)）。 */
    XGraphicsView_viewportTransform(view, &vp1);
    XGraphicsItem_matrixFromTranslate(
        &step, -(float)XScrollBar_value(hbar), -(float)XScrollBar_value(vbar));
    XGraphicsItem_matrixMultiply(&expectVp, &m, &step);
    xq_nearMatrix("vp-after-scroll", &vp1, &expectVp);

    /* centerOnItem：条值 = 对 item.sceneBoundingRect 中心的 centerOn
     * （Qt :1918-1922）。 */
    {
        XVector* list =
            XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
        XGraphicsViewProbe* a;
        XRectF br;
        XPointF itemCenter;
        XQ_REQUIRE(list != NULL);
        a = *(XGraphicsViewProbe**)XVector_at_base(list, 0);
        XClassDelete(list);
        br = XGraphicsItem_sceneBoundingRect(&a->m_base);
        itemCenter.x = br.x + br.width / 2.0f;
        itemCenter.y = br.y + br.height / 2.0f;
        XGraphicsView_centerOnItem(view, &a->m_base);
        {
            XPointF mapped;
            int expectH;
            int expectV;
            /* setValue 钳到条值域（Qt centerOn 的条 setValue 同钳）。 */
            int hmin = XScrollBar_minimum(hbar);
            int hmax = XScrollBar_maximum(hbar);
            int vmin = XScrollBar_minimum(vbar);
            int vmax = XScrollBar_maximum(vbar);
            XGraphicsItem_matrixMapPoint(&m, itemCenter.x, itemCenter.y,
                                         &mapped);
            expectH = (int)roundf(mapped.x - 150.0f);
            expectV = (int)roundf(mapped.y - 100.0f);
            if (expectH < hmin) expectH = hmin;
            if (expectH > hmax) expectH = hmax;
            if (expectV < vmin) expectV = vmin;
            if (expectV > vmax) expectV = vmax;
            XQ_EQ_I64(XScrollBar_value(hbar), expectH);
            XQ_EQ_I64(XScrollBar_value(vbar), expectV);
        }
    }

    /* 显式 sceneRect 与回退：setSceneRect 后 sceneRect() 用设置值；
     * NULL=清除（回退场景/空）。 */
    {
        XRectF sr;
        sr.x = -50.0f;
        sr.y = -25.0f;
        sr.width = 400.0f;
        sr.height = 300.0f;
        XGraphicsView_setSceneRect(view, &sr);
        sr = XGraphicsView_sceneRect(view);
        XQ_CHECK(sr.x == -50.0f && sr.width == 400.0f && sr.height == 300.0f);
        XGraphicsView_setSceneRect(view, NULL);
        sr = XGraphicsView_sceneRect(view);
        /* 回退场景矩形 (0,0,200,150)。 */
        XQ_CHECK(sr.width == 200.0f && sr.height == 150.0f);
    }
    /* updateSceneRect：未设显式值时跟随（Qt :2702-2712 只重算范围——
     * sceneRect() 属性仍回落场景，此处只断言不崩+不跟随路径）。 */
    {
        XRectF big;
        big.x = 0.0f;
        big.y = 0.0f;
        big.width = 800.0f;
        big.height = 600.0f;
        XGraphicsView_updateSceneRect(view, &big);
        {
            XRectF sr = XGraphicsView_sceneRect(view);
            /* 属性口径不变（仍回落场景 (0,0,200,150)）。 */
            XQ_CHECK(sr.width == 200.0f && sr.height == 150.0f);
        }
    }

    (void)scrollX;
    (void)scrollY;
    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}

/* ==================== ⑤ 滚轮（探针 D） ==================== */

#if XWINDOWEVENT_ON
/** @brief 最近一次 xqv_sendWheel 的接受态（避免把栈事件带出作用域）。 */
static bool xqv_lastWheelAccepted = false;

/** @brief 构造滚轮事件并发给视口（**必须走 XWheelEvent_init**：手写
 *  XMemset 的事件没有 vtable，XEvent_setAccepted_base 首行
 *  `ISNULL(XClassGetVtable(event))` 直接返回（XEvent.c:236-241）⇒
 *  accepted 断言恒真 + 每次调用一条 XError。【复核根修 2026-11-16】 */
static void xqv_sendWheel(XGraphicsView* view, int vx, int vy, int deltaY,
                          XKeyboardModifiers modifiers)
{
    XWheelEvent we;
    XPoint position;
    XPoint globalPos;
    XPoint angle;
    position.x = vx;
    position.y = vy;
    globalPos.x = vx;
    globalPos.y = vy;
    angle.x = 0;
    angle.y = deltaY;
    XWheelEvent_init(&we, XEVENT_TYPE_WHEEL, &position, &globalPos, &angle,
                     XMouseButton_NoButton, modifiers);
    /* vtable 前置条件（复核 ③）：没有 vtable 时 XEvent_setAccepted_base
     * 首行即返回 ⇒ 接受态恒为 XEvent_init 的缺省 true，用例全成假绿。
     * 这里直接锁住「事件确实装了虚表」。 */
    XQ_CHECK(XClassGetVtable(&we) != NULL);
    (void)XCoreApplication_sendEvent(
        (XObject*)XAbstractScrollArea_viewport(&view->m_base), (XEvent*)&we);
    /* 把接受态回写给调用方用（栈对象出函数即失效）。 */
    xqv_lastWheelAccepted = XEvent_isAccepted((XEvent*)&we);
}

XQ_TEST(XGraphicsView, wheelEvents)
{
    XGraphicsScene* scene;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XGraphicsViewProbe* a;
    XVector* list;
    XImageTransform m;
    XScrollBar* vbar;
    int before;

    xqv_caseBegin();
    scene = xq_buildScene(); /* 探针在归零之后创建。 */
    XQ_REQUIRE(view != NULL && scene != NULL);
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 300, 200);
    vbar = XAbstractScrollArea_verticalScrollBar(&view->m_base);

    list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
    XQ_REQUIRE(list != NULL);
    a = *(XGraphicsViewProbe**)XVector_at_base(list, 0);
    XClassDelete(list);

    /* ⑤a 场景优先（判别性设计）：**让垂直条有非零范围**，这样「场景收下
     * ⇒ 条不动」与「回落 ⇒ 条动」才可区分。【复核根修 2026-11-16】原用例
     * 在条 range=0..0 下发滚轮，回落也改不了条值 ⇒ 无判别力。
     * 做法：先放大变换使场景超出视口（条有范围），再把项放回命中点。 */
    xq_buildBMatrix(&m);
    XGraphicsView_setTransform(view, &m, false);
    XQ_REQUIRE(XScrollBar_maximum(vbar) > XScrollBar_minimum(vbar));

    {
        /* 项 A 场景盒 (5,7)+(40x30)；把滚轮落点取成它的场景中心，再反算
         * 视口坐标（当前变换下）。 */
        XPointF center;
        XPoint vp;
        center.x = 5.0f + 20.0f;
        center.y = 7.0f + 15.0f;
        vp = XGraphicsView_mapFromScene(view, &center);
        before = XScrollBar_value(vbar);
        a->m_wheel = 0;
        xqv_sendWheel(view, vp.x, vp.y, 120,
                      (XKeyboardModifiers)XKeyboardModifier_NoModifier);
        XQ_EQ_I64(a->m_wheel, 1); /* 项收到场景滚轮。 */
        XQ_CHECK(xqv_lastWheelAccepted); /* 项 accept ⇒ 视图不再回落。 */
        XQ_EQ_I64(XScrollBar_value(vbar), before); /* **条不动**（会判别）。 */
        /* 事件坐标换算：项局部中心应为 (20,15)。 */
        xq_nearF("wheel-local.x", a->m_wheelSceneX, 20.0f);
        xq_nearF("wheel-local.y", a->m_wheelSceneY, 15.0f);
    }

    /* ⑤a2 **项拒绝**也必须真实传播（复核 ③ 的反证支）：项把 accepted 置
     * false ⇒ 场景不收 ⇒ 视图回落滚动条（条动）。若事件没有 vtable，
     * XEvent_setAccepted_base 是空操作、accepted 保持场景派发前的 true，
     * 这里条就不会动（旧用例只有「接受」正向断言 ⇒ 假绿）。 */
    {
        XPointF center;
        XPoint vp;
        center.x = 5.0f + 20.0f;
        center.y = 7.0f + 15.0f;
        vp = XGraphicsView_mapFromScene(view, &center);
        a->m_acceptWheel = false;
        before = XScrollBar_value(vbar);
        xqv_sendWheel(view, vp.x, vp.y, 120,
                      (XKeyboardModifiers)XKeyboardModifier_NoModifier);
        XQ_EQ_I64(a->m_wheel, 2); /* 项确实收到（拒绝≠收不到）。 */
        XQ_EQ_I64(XScrollBar_value(vbar), before - 3); /* 拒绝 ⇒ 回落条。 */
        XQ_CHECK(xqv_lastWheelAccepted); /* 滚动条兜底后 accept。 */
        a->m_acceptWheel = true;
    }

    /* ⑤b 无人命中 → 回落 QAbstractScrollArea 滚条（探针 D：transform
     * 不变、vbar 变化）。隐藏项保证空白命中；条有范围故步进可见。 */
    XGraphicsItem_setVisible(&a->m_base, false);
    before = XScrollBar_value(vbar);
    xqv_sendWheel(view, 10, 10, 120,
                  (XKeyboardModifiers)XKeyboardModifier_NoModifier);
    XQ_EQ_I64(a->m_wheel, 2); /* 无人命中：不再进项（⑤a2 后累计 2）。 */
    /* ASA 滚轮口径：竖滚 120 ⇒ stepBy(−3)（负=值减；VX_asa_wheelEvent
     * 既定步进），滚后 accept（滚动条消费）。 */
    XQ_EQ_I64(XScrollBar_value(vbar), before - 3);
    XQ_CHECK(xqv_lastWheelAccepted);
    /* 变换未被滚轮改动（探针 D transform_changed=0）。 */
    xq_nearF("wheel-no-zoom",
             XGraphicsView_transform_const(view)->m11, m.m11);
    /* Ctrl 档同款（Qt wheelEvent 无修饰键分支——探针 D 实测：普通与
     * Ctrl 滚轮都只滚条、都不缩放）。 */
    before = XScrollBar_value(vbar);
    xqv_sendWheel(view, 10, 10, 120,
                  (XKeyboardModifiers)XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(XScrollBar_value(vbar), before - 3);
    xq_nearF("ctrl-no-zoom", XGraphicsView_transform_const(view)->m11,
             m.m11);

    /* 非交互视图：滚轮不进场景、直接走滚动条（Qt :3375-3377）。 */
    XGraphicsView_setInteractive(view, false);
    before = XScrollBar_value(vbar);
    {
        XPointF center;
        XPoint vp;
        center.x = 25.0f;
        center.y = 22.0f;
        vp = XGraphicsView_mapFromScene(view, &center);
        xqv_sendWheel(view, vp.x, vp.y, 120,
                      (XKeyboardModifiers)XKeyboardModifier_NoModifier);
    }
    XQ_EQ_I64(a->m_wheel, 2); /* 场景未再收（交互闸拦下）。 */
    XQ_EQ_I64(XScrollBar_value(vbar), before - 3);
    XGraphicsView_setInteractive(view, true);

    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}
#endif /* XWINDOWEVENT_ON */

/* ==================== ⑧ 拖动模式 ==================== */

#if XWINDOWEVENT_ON
/** @brief 构造视口坐标鼠标事件（发给视口 → ASA 过滤器 → 视图入口）。
 *  @note  **[复核根修 2026-11-16] 必须走框架 XMouseEvent_init**：手写
 *  XMemset 的事件**没有 vtable**，而 XEvent_setAccepted_base 首行
 *  `ISNULL(XClassGetVtable(event))` 直接返回（XEvent.c:236-241）⇒ 事件的
 *  accept/ignore 全成空操作，accepted 断言恒真、且每次调用各打一条 XError
 *  （实跑 42 条）。init 后事件带 XMouseEvent 虚表，接受态才真实可断言。 */
static void xqv_sendMouse(XGraphicsView* view, int type, int vx, int vy,
                          XMouseButton button, XMouseButton buttons)
{
    XMouseEvent me;
    XPoint position;
    position.x = vx;
    position.y = vy;
    XMouseEvent_init(&me, (XEventType)type, button,
                     (XKeyboardModifiers)XKeyboardModifier_NoModifier,
                     position);
    me.m_buttons = buttons;
    /* vtable 前置条件（复核 ③）：见 xqv_sendWheel 同注。 */
    XQ_CHECK(XClassGetVtable(&me) != NULL);
    (void)XCoreApplication_sendEvent(
        (XObject*)XAbstractScrollArea_viewport(&view->m_base), (XEvent*)&me);
}

XQ_TEST(XGraphicsView, dragModes)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsViewProbe* sel;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XScrollBar* hbar;
    XScrollBar* vbar;
    XRectF r;
    XPointF pos;

    xqv_caseBegin();
    XQ_REQUIRE(scene != NULL && view != NULL);
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 800.0f;
    r.height = 600.0f;
    XGraphicsScene_setSceneRect(scene, &r);
    sel = xqv_probeCreate(100.0f, 80.0f, 0xFF336699u);
    XQ_REQUIRE(sel != NULL);
    XGraphicsItem_setFlag(&sel->m_base, XGraphicsItemFlag_ItemIsSelectable,
                          true);
    pos.x = 40.0f;
    pos.y = 50.0f;
    XGraphicsItem_setPos(&sel->m_base, &pos);
    XGraphicsScene_addItem(scene, &sel->m_base);
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 300, 200);
    hbar = XAbstractScrollArea_horizontalScrollBar(&view->m_base);
    vbar = XAbstractScrollArea_verticalScrollBar(&view->m_base);

    /* NoDrag（缺省）：空白按下不起带、条不动。 */
    XQ_EQ_I64(XGraphicsView_dragMode(view), XGraphicsViewDragMode_NoDrag);
    xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 200, 100,
                  XMouseButton_LeftButton, XMouseButton_LeftButton);
    XQ_CHECK(XGraphicsView_rubberBandRect_const(view) == NULL);
    xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 210, 110,
                  XMouseButton_LeftButton, XMouseButton_NoButton);

    /* ScrollHandDrag：左键起手 → 移动拖条（Δ=(15,−8) ⇒ hbar −15、
     * vbar +8，Qt :3286-3294）；先给条一个可滚起点。 */
    XGraphicsView_setDragMode(view, XGraphicsViewDragMode_ScrollHandDrag);
    XScrollBar_setValue(hbar, 50);
    XScrollBar_setValue(vbar, 50);
    xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 150, 100,
                  XMouseButton_LeftButton, XMouseButton_LeftButton);
    xqv_sendMouse(view, XEVENT_TYPE_MOUSE_MOVE, 165, 92,
                  XMouseButton_NoButton, XMouseButton_LeftButton);
    XQ_EQ_I64(XScrollBar_value(hbar), 50 - 15);
    XQ_EQ_I64(XScrollBar_value(vbar), 50 + 8);
    xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 165, 92,
                  XMouseButton_LeftButton, XMouseButton_LeftButton);

    /* setDragMode 的两条 Qt 语义（复核 ④ 顺带核实项）：
     *   a) 同模式早退（qgraphicsview.cpp:1505）——重复设置同一模式不得清
     *      进行中的状态；
     *   b) handScrolling 只在 **旧模式 ScrollHandDrag → 新模式 NoDrag** 时
     *      撤（:1516-1519），不是「新模式是 NoDrag」就撤。 */
    xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 150, 100,
                  XMouseButton_LeftButton, XMouseButton_LeftButton);
    XQ_CHECK(view->m_handScrolling); /* 手拖进行中（不松手）。 */
    XGraphicsView_setDragMode(view, XGraphicsViewDragMode_RubberBandDrag);
    XQ_CHECK(view->m_handScrolling); /* 旧模式≠NoDrag ⇒ 不清。 */
    XGraphicsView_setDragMode(view, XGraphicsViewDragMode_NoDrag);
    XQ_CHECK(view->m_handScrolling); /* 旧模式=RubberBand ⇒ 仍不清（Qt 同）。 */
    /* 收尾（走真正的 ScrollHandDrag→NoDrag 路径才清）。 */
    XGraphicsView_setDragMode(view, XGraphicsViewDragMode_ScrollHandDrag);
    XGraphicsView_setDragMode(view, XGraphicsViewDragMode_NoDrag);
    XQ_CHECK(!view->m_handScrolling);

    /* 「微动 ⇒ 点击清选」的**守卫**（Qt :3318-3323
     * `!lastMouseEvent.isAccepted() && handScrollMotions <= 6`）：
     * 【复核根修 2026-11-16】原断言「此前无选中 ⇒ 仍无选中」是恒真，
     * 换成判别性用例：先选中项，再让项**接受**按下/移动（m_acceptWheel
     * 同时作 press 接受位），微动后松手 ⇒ 守卫因 accepted=true 命中，
     * **选中必须保住**。 */
    XGraphicsItem_setSelected(&sel->m_base, true);
    {
        XVector* selItems = XGraphicsScene_selectedItems(scene);
        XQ_CHECK(selItems != NULL);
        if (selItems) {
            XQ_EQ_SIZE(XContainerSize(selItems), 1);
            XClassDelete(selItems);
        }
    }
    /* 项场景盒 (40..140,50..130)；视口 (50,32) 处 scroll 已被拖到
     * hbar=35/vbar=58 ⇒ 命中需按当前映射：直接用 centerOn 复位。 */
    XGraphicsView_centerOnItem(view, &sel->m_base);
    sel->m_acceptWheel = true; /* press 也接受（复用的接受位）。 */
    {
        XPointF c;
        XPoint vp;
        XRectF br = XGraphicsItem_sceneBoundingRect(&sel->m_base);
        c.x = br.x + br.width / 2.0f;
        c.y = br.y + br.height / 2.0f;
        vp = XGraphicsView_mapFromScene(view, &c);
        sel->m_press = 0;
        xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_PRESS, vp.x, vp.y,
                      XMouseButton_LeftButton, XMouseButton_LeftButton);
        XQ_EQ_I64(sel->m_press, 1); /* 项确实收到 press（接受=真）。 */
        xqv_sendMouse(view, XEVENT_TYPE_MOUSE_MOVE, vp.x + 3, vp.y + 2,
                      XMouseButton_NoButton, XMouseButton_LeftButton);
        xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, vp.x + 3,
                      vp.y + 2, XMouseButton_LeftButton,
                      XMouseButton_NoButton);
    }
    {
        XVector* selItems = XGraphicsScene_selectedItems(scene);
        XQ_CHECK(selItems != NULL); /* accepted ⇒ 不清选。 */
        if (selItems) XClassDelete(selItems);
    }
    /* 守卫的否定支**不可观测**，故不设断言：无项接受按下时场景
     * mousePressEventHandler 自己就清选（W2 口径），到 release 时选中集
     * 已空，视图侧那颗 clearSelection 只是幂等兜底。判别性只在上面的
     * 肯定支（accepted=true ⇒ 保住选中）。 */

    /* RubberBandDrag：空白按下起带 → 移动实时选区 → 松开收带。 */
    XGraphicsView_setDragMode(view, XGraphicsViewDragMode_RubberBandDrag);
    {
        /* setDragMode 应已清带（Qt :1500-1502：clearRubberBand 在函数首部）。
         * 切模式清带是复核项，这里顺带断言。 */
        XQ_CHECK(XGraphicsView_rubberBandRect_const(view) == NULL);
    }

    /* 「场景收下 press ⇒ 不起带」/「项拒绝 ⇒ 起带」的判别性反证
     * （复核 ③）：事件若没有 vtable，项的 `accepted=false` 是空操作、
     * 接受态保持场景派发前的 true ⇒ 拒绝支不会起带，下面的
     * `band != NULL` 必红（这正是旧用例断言空转的形态）。 */
    XGraphicsView_centerOnItem(view, &sel->m_base);
    {
        XPointF c;
        XPoint vp;
        XRectF br = XGraphicsItem_sceneBoundingRect(&sel->m_base);
        c.x = br.x + br.width / 2.0f;
        c.y = br.y + br.height / 2.0f;
        vp = XGraphicsView_mapFromScene(view, &c);
        /* 接受支：场景在视图进 RubberBand 分支之前就返回（无带）。 */
        sel->m_acceptWheel = true;
        sel->m_press = 0;
        xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_PRESS, vp.x, vp.y,
                      XMouseButton_LeftButton, XMouseButton_LeftButton);
        XQ_EQ_I64(sel->m_press, 1);
        XQ_CHECK(XGraphicsView_rubberBandRect_const(view) == NULL);
        xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, vp.x, vp.y,
                      XMouseButton_LeftButton, XMouseButton_NoButton);
        /* 拒绝支：accepted=false 真传播 ⇒ 落到 RubberBandDrag 起带。 */
        sel->m_acceptWheel = false;
        xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_PRESS, vp.x, vp.y,
                      XMouseButton_LeftButton, XMouseButton_LeftButton);
        XQ_EQ_I64(sel->m_press, 2); /* 项收到了，但拒绝。 */
        {
            const XRect* band = XGraphicsView_rubberBandRect_const(view);
            XQ_CHECK(band != NULL);
            if (band) {
                XQ_EQ_I64(band->width, 0);
                XQ_EQ_I64(band->height, 0);
            }
        }
        xqv_sendMouse(view, XEVENT_TYPE_MOUSE_MOVE, vp.x + 20, vp.y + 10,
                      XMouseButton_NoButton, XMouseButton_LeftButton);
        {
            const XRect* band = XGraphicsView_rubberBandRect_const(view);
            XQ_CHECK(band != NULL);
            if (band) {
                XQ_EQ_I64(band->x, vp.x);
                XQ_EQ_I64(band->y, vp.y);
                XQ_EQ_I64(band->width, 20);
                XQ_EQ_I64(band->height, 10);
            }
        }
        xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, vp.x + 20,
                      vp.y + 10, XMouseButton_LeftButton,
                      XMouseButton_NoButton);
        XQ_CHECK(XGraphicsView_rubberBandRect_const(view) == NULL);
        sel->m_acceptWheel = true;
    }

    xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 10, 10,
                  XMouseButton_LeftButton, XMouseButton_LeftButton);
    {
        const XRect* band = XGraphicsView_rubberBandRect_const(view);
        /* 空带也要有对象；NULL 会让后续解引用崩。失败路径不允许
         * `XQ_REQUIRE` 早退——否则跳过 `XGraphicsScene_clear`，全局探针
         * 计数残留并级联污染后续用例（复核 2026-11-17 测试卫生项）。 */
        XQ_CHECK(band != NULL);
        if (band) {
            XQ_EQ_I64(band->width, 0);
            XQ_EQ_I64(band->height, 0);
        }
    }
    /* 同模式重复设置必须早退（Qt :1505 `if (d->dragMode == mode) return;`
     * 在 clearRubberBand 之前）——进行中的带必须保留。 */
    XGraphicsView_setDragMode(view, XGraphicsViewDragMode_RubberBandDrag);
    {
        const XRect* band = XGraphicsView_rubberBandRect_const(view);
        XQ_CHECK(band != NULL);
        if (band) {
            XQ_EQ_I64(band->width, 0);
            XQ_EQ_I64(band->height, 0);
        }
    }
    /* 移到 (250,150)：带内含项 A 场景包围盒 (40,50)+(100x80) 的视口投影
     * （恒等变换、scroll=0：项在视口 (40..140,50..130)，带 (10..250,
     * 10..150) 覆盖之）。 */
    xqv_sendMouse(view, XEVENT_TYPE_MOUSE_MOVE, 250, 150,
                  XMouseButton_NoButton, XMouseButton_LeftButton);
    {
        const XRect* band = XGraphicsView_rubberBandRect_const(view);
        XQ_CHECK(band != NULL);
        if (band) {
            XQ_EQ_I64(band->x, 10);
            XQ_EQ_I64(band->y, 10);
            XQ_EQ_I64(band->width, 240);
            XQ_EQ_I64(band->height, 140);
        }
    }
    {
        XVector* selItems = XGraphicsScene_selectedItems(scene);
        XQ_CHECK(selItems != NULL);
        if (selItems) {
            XQ_EQ_SIZE(XContainerSize(selItems), 1);
            XQ_CHECK(*(XGraphicsItem**)XVector_at_base(selItems, 0) ==
                     &sel->m_base);
            XClassDelete(selItems);
        }
    }
    xqv_sendMouse(view, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 250, 150,
                  XMouseButton_LeftButton, XMouseButton_NoButton);
    XQ_CHECK(XGraphicsView_rubberBandRect_const(view) == NULL);
    /* 回 NoDrag 撤手拖态（Qt :1513-1514）。 */
    XGraphicsView_setDragMode(view, XGraphicsViewDragMode_NoDrag);
    XQ_EQ_I64(XGraphicsView_dragMode(view), XGraphicsViewDragMode_NoDrag);

    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}
#endif /* XWINDOWEVENT_ON */

/* ==================== ⑥ 渲染像素断言 ==================== */

XQ_TEST(XGraphicsView, renderPixel)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsViewProbe* red;
    XGraphicsViewProbe* blue;
    XPointF pos;
    XRectF r;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XBrush brush;
    XImage img;
    XPainter painter;
    uint32_t px;

    xqv_caseBegin();
    XQ_REQUIRE(scene != NULL && view != NULL);
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 60.0f;
    r.height = 60.0f;
    XGraphicsScene_setSceneRect(scene, &r);
    red = xqv_probeCreate(20.0f, 20.0f, 0xFFFF2222u);
    blue = xqv_probeCreate(20.0f, 20.0f, 0xFF2244FFu);
    XQ_REQUIRE(red != NULL && blue != NULL);
    pos.x = 10.0f;
    pos.y = 20.0f;
    XGraphicsItem_setPos(&red->m_base, &pos);
    pos.x = 25.0f;
    pos.y = 25.0f;
    XGraphicsItem_setPos(&blue->m_base, &pos);
    /* z 序：blue 后插入（同 z 按插入序）⇒ 叠在 red 上层。 */
    XGraphicsScene_addItem(scene, &red->m_base);
    XGraphicsScene_addItem(scene, &blue->m_base);
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 60, 60);

    /* renderViewport：视口即整幅（60x60）。红 (10..30,20..40)；蓝
     * (25..45,25..45)，重叠区 (25..30,25..40) 蓝在上层。 */
    XImage_init_ex(&img, 60, 60, XImageFormat_ARGB32);
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    XQ_REQUIRE(XPainter_begin_image(&painter, &img));
    XGraphicsView_renderViewport(view, &painter, NULL);
    XPainter_deinit(&painter);
    px = XImage_pixel(&img, 20, 25);
    XQ_EQ_I64(px & 0xFF, 0x22); /* 红块：R 通道。 */
    px = XImage_pixel(&img, 40, 40);
    XQ_EQ_I64((px >> 8) & 0xFF, 0x44); /* 蓝块独占区：G 通道=0x44。 */
    px = XImage_pixel(&img, 55, 10);
    XQ_EQ_I64(px, 0u); /* 空白区（无背景笔刷=不铺底）。 */
    /* 蓝盖红：重叠格 (27,30) 应为蓝系（插入序后者在上）。 */
    px = XImage_pixel(&img, 27, 30);
    XQ_EQ_I64((px >> 8) & 0xFF, 0x44);

    /* 背景笔刷铺底（drawBackground 最小版）：空白区=笔刷色。setter 值拷贝
     * 入（头注口径），getter 回内部副本——比对样式与颜色而非指针。 */
    brush = XBrush_create_color(XColor_create_rgba(0xFF181818u),
                                XBrushStyle_SolidPattern);
    XGraphicsView_setBackgroundBrush(view, &brush);
    XQ_CHECK(XBrush_style(XGraphicsView_backgroundBrush_const(view)) ==
             XBrushStyle_SolidPattern);
    {
        XColor gotC = XBrush_color(XGraphicsView_backgroundBrush_const(view));
        XQ_CHECK(XColor_rgba(&gotC) == 0xFF181818u);
    }
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    XQ_REQUIRE(XPainter_begin_image(&painter, &img));
    XGraphicsView_renderViewport(view, &painter, NULL);
    XPainter_deinit(&painter);
    px = XImage_pixel(&img, 55, 10);
    XQ_EQ_I64(px & 0xFF, 0x18); /* 背景色 B 通道落位。 */
    px = XImage_pixel(&img, 20, 25);
    XQ_EQ_I64(px & 0xFF, 0x22); /* 项不被盖。 */
    /* NoBrush=清除背景（回不铺底）。 */
    XGraphicsView_setBackgroundBrush(view, NULL);
    XQ_CHECK(XBrush_style(XGraphicsView_backgroundBrush_const(view)) ==
             XBrushStyle_NoBrush);

    /* render(target, source)：整视口 → 半幅目标（xratio=yratio=0.5，
     * IgnoreAspectRatio）：红 (10..30,20..40) → (5..15,10..20)。 */
    {
        XRectF target;
        target.x = 0.0f;
        target.y = 0.0f;
        target.width = 30.0f;
        target.height = 30.0f;
        XClassDeinit((XClass*)&img); /* 换规格重 init 前先反初始化（旧数据 unref）。 */
        XImage_init_ex(&img, 30, 30, XImageFormat_ARGB32);
        XImage_fill(&img, 0u);
        XPainter_init(&painter, NULL);
        XQ_REQUIRE(XPainter_begin_image(&painter, &img));
        XGraphicsView_render(view, &painter, &target, NULL);
        XPainter_deinit(&painter);
        px = XImage_pixel(&img, 10, 15);
        XQ_EQ_I64(px & 0xFF, 0x22); /* 红 (5..15,10..20)。 */
        px = XImage_pixel(&img, 4, 15);
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&img, 16, 15);
        XQ_EQ_I64((px >> 8) & 0xFF, 0x44); /* 蓝 (12.5..22.5,12.5..22.5)。 */
        px = XImage_pixel(&img, 2, 2);
        XQ_EQ_I64(px, 0u); /* 空白（背景笔刷已清）。 */
        px = XImage_pixel(&img, 25, 25);
        XQ_EQ_I64(px, 0u);
    }

    /* renderViewport 的视口子矩形（**平移必须生效**，[复核根修 2026-11-16]
     * 原用例的期望值恰建立在「平移被 setWorldTransform 擦掉」的错误行为
     * 上）。语义：vpRect=(30,30,30,30) 表示视口落在 painter 逻辑坐标
     * (30,30)，场景点 p → 设备 (30,30)+p，裁剪也按该矩形：
     *   红 (10..30,20..40) → 设备 (40..60,50..70)，裁剪后只留 (40..60,50..60)；
     *   蓝 (25..45,25..45) → 设备 (55..75,55..75)，裁剪后只留 (55..60,55..60)
     *   （蓝在上层）。
     * 图像放大到 90x90 使「裁剪带」在内可寻址。 */
    {
        XRect vpRect;
        XRect_init(&vpRect, 30, 30, 30, 30);
        XClassDeinit((XClass*)&img); /* 换规格重 init 前先反初始化。 */
        XImage_init_ex(&img, 90, 90, XImageFormat_ARGB32);
        XImage_fill(&img, 0u);
        XPainter_init(&painter, NULL);
        XQ_REQUIRE(XPainter_begin_image(&painter, &img));
        XGraphicsView_renderViewport(view, &painter, &vpRect);
        XPainter_deinit(&painter);
        /* 平移生效的正证据：红块落到 (40..60) 而非 (10..30)。 */
        px = XImage_pixel(&img, 20, 25);
        XQ_EQ_I64(px, 0u); /* 不平移的话这里会是红——平移已被保留。 */
        px = XImage_pixel(&img, 45, 55);
        XQ_EQ_I64(px & 0xFF, 0x22); /* 红（场景 (15,25)）。 */
        px = XImage_pixel(&img, 57, 57);
        XQ_EQ_I64((px >> 8) & 0xFF, 0x44); /* 蓝在上层（场景 (27,27)）。 */
        /* 裁剪生效的正证据：设备 (50,65) 对应场景 (20,35)——红在场景
         * (10..30,20..40) 内，但设备 y=65 超出裁剪 (30..60) ⇒ 不落像素。 */
        px = XImage_pixel(&img, 50, 65);
        XQ_EQ_I64(px, 0u);
        /* 裁剪外（左侧 x<30）与右下（>60）都不落像素。 */
        px = XImage_pixel(&img, 20, 40);
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&img, 70, 70);
        XQ_EQ_I64(px, 0u);
    }
    XClassDeinit((XClass*)&img); /* 栈上图像：只反初始化（unref 数据）。 */

    /* **调用方变换必须保留**（[复核根修 2026-11-16] 的等价回归：
     * 复核探针 /tmp/gvprobe_off/view_probe2.cpp —— painter 先
     * translate(60,0) 再 renderViewport(NULL)，旧实现 px(5,5)=0 且
     * px(65,5)=0（内容被自己的裁剪整体裁掉），对照不 translate 的
     * view_probe3b px(5,5)=ffff0000）。修后：内容右移 60，裁剪也随调用方
     * 变换落在 (60,0,60,60)。 */
    {
        XImage wide;
        XPainter p2;
        XClassDeinit((XClass*)&img); /* 换规格重 init 前先反初始化。 */
        XImage_init_ex(&wide, 120, 60, XImageFormat_ARGB32);
        XImage_fill(&wide, 0u);
        XPainter_init(&p2, NULL);
        XQ_REQUIRE(XPainter_begin_image(&p2, &wide));
        XPainter_translate(&p2, 60.0f, 0.0f);
        XGraphicsView_renderViewport(view, &p2, NULL);
        XPainter_deinit(&p2);
        px = XImage_pixel(&wide, 5, 25);
        XQ_EQ_I64(px, 0u); /* 左半幅空（内容不在原点）。 */
        /* 红场景 (10..30,20..40) + 平移 (60,0) ⇒ 设备 (70..90,20..40)。 */
        px = XImage_pixel(&wide, 65, 25);
        XQ_EQ_I64(px, 0u); /* 红块未落在旧位置（平移确实生效）。 */
        px = XImage_pixel(&wide, 75, 25);
        XQ_EQ_I64(px & 0xFF, 0x22); /* 红块右移到 70+（平移被保留）。 */
        /* 蓝场景 (25..45,25..45) + 平移 ⇒ 设备 (85..105,25..45)。 */
        px = XImage_pixel(&wide, 95, 35);
        XQ_EQ_I64((px >> 8) & 0xFF, 0x44); /* 蓝块同样右移。 */
        XClassDeinit((XClass*)&wide);
    }

    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}

/* ============ ⑥b 缩放+平移下的渲染落位（review ① 的像素级锁定） ========
 * 对齐依据：Qt 6.8.0 offscreen 探针（本轮实跑）——scene (0,0,200,200)、
 * 视口 100x100、项 scene(30,30)+(10x10)、matrix=[2 0;0 2;dx=5,dy=7]、
 * 条值 (55,57)：
 *   · viewportTransform: 场景 p → 视口 2p+(5,7)−(55,57) = 2p−(50,50)，
 *     项视口盒 = (10..30,10..30)（Qt `render(&p)` 实测 (11,11)/(29,29) 有红、
 *     (9,9)/(31,31) 空）；
 *   · render(target=(20,10,40,40), source=(10,10,80,80))：项设备盒
 *     (20..30,10..20)（Qt 实测 (25,15)=ffff2222、(19,15)/(31,15)/(25,9)/
 *     (25,21)/(2,2)=00000000）。
 * 这两个数就是 painterMatrix 作用顺序（matrix → −scroll → −source →
 * S(ratio) → +target）的判别证据：旧式「S 前乘 + −source 最后」会把项
 * 画到画布外（source.tl 非零时整幅错位）。 */
XQ_TEST(XGraphicsView, renderScaledTransform)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsViewProbe* red;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XImageTransform m;
    XImageTransform vp;
    XImageTransform expectVp;
    XImageTransform step;
    XScrollBar* hbar;
    XScrollBar* vbar;
    XImage img;
    XPainter painter;
    XPointF pos;
    XPointF sp;
    XPoint vpp;
    XRectF r;
    uint32_t px;

    xqv_caseBegin();
    XQ_REQUIRE(scene != NULL && view != NULL);
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 200.0f;
    r.height = 200.0f;
    XGraphicsScene_setSceneRect(scene, &r);
    red = xqv_probeCreate(10.0f, 10.0f, 0xFFFF2222u);
    XQ_REQUIRE(red != NULL);
    pos.x = 30.0f;
    pos.y = 30.0f;
    XGraphicsItem_setPos(&red->m_base, &pos);
    XGraphicsScene_addItem(scene, &red->m_base);
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 100, 100);

    /* 显式矩阵 [m11=2 m22=2 dx=5 dy=7]：p → 2p+(5,7)（XImageTransform
     * 的 map 公式 x'=m11·x+m21·y+dx，探针同参）。 */
    XGraphicsItem_matrixIdentity(&m);
    m.m11 = 2.0f;
    m.m22 = 2.0f;
    m.dx = 5.0f;
    m.dy = 7.0f;
    XGraphicsView_setTransform(view, &m, false);
    XQ_CHECK(XGraphicsView_transform_const(view)->m11 == 2.0f);

    hbar = XAbstractScrollArea_horizontalScrollBar(&view->m_base);
    vbar = XAbstractScrollArea_verticalScrollBar(&view->m_base);
    XQ_REQUIRE(hbar != NULL && vbar != NULL);
    /* 场景放大后远大于视口 ⇒ 无对齐缩进（滚动量=条值）。 */
    XQ_EQ_I64(view->m_leftIndent, 0);
    XQ_EQ_I64(view->m_topIndent, 0);
    XQ_CHECK(XScrollBar_maximum(hbar) > XScrollBar_minimum(hbar));
    XQ_CHECK(XScrollBar_maximum(vbar) > XScrollBar_minimum(vbar));
    /* 固定滚动量（探针同参；落在条值域内，setValue 不钳）。 */
    XScrollBar_setValue(hbar, 55);
    XScrollBar_setValue(vbar, 57);
    XQ_EQ_I64(XScrollBar_value(hbar), 55);
    XQ_EQ_I64(XScrollBar_value(vbar), 57);

    /* viewportTransform = matrix*T(-55,-57) ⇒ dx=-50 dy=-50 m11=m22=2。 */
    XGraphicsView_viewportTransform(view, &vp);
    XGraphicsItem_matrixFromTranslate(&step, -55.0f, -57.0f);
    XGraphicsItem_matrixMultiply(&expectVp, &m, &step);
    xq_nearMatrix("scaled-vp", &vp, &expectVp);
    /* 映射校验：场景 (30,30) ↔ 视口 (10,10)。 */
    sp.x = 30.0f;
    sp.y = 30.0f;
    vpp = XGraphicsView_mapFromScene(view, &sp);
    XQ_EQ_I64(vpp.x, 10);
    XQ_EQ_I64(vpp.y, 10);
    {
        XPoint back;
        XPointF backScene;
        back.x = 10;
        back.y = 10;
        backScene = XGraphicsView_mapToScene(view, &back);
        xq_nearF("scaled-mapToScene.x", backScene.x, 30.0f);
        xq_nearF("scaled-mapToScene.y", backScene.y, 30.0f);
    }

    /* renderViewport：项视口盒 (10..30,10..30)。 */
    XImage_init_ex(&img, 100, 100, XImageFormat_ARGB32);
    XQ_REQUIRE(!XImage_isNull(&img));
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    XQ_REQUIRE(XPainter_begin_image(&painter, &img));
    XGraphicsView_renderViewport(view, &painter, NULL);
    XPainter_deinit(&painter);
    px = XImage_pixel(&img, 11, 11);
    XQ_EQ_I64((px >> 8) & 0xFF, 0x22); /* 项内。 */
    px = XImage_pixel(&img, 29, 29);
    XQ_EQ_I64((px >> 8) & 0xFF, 0x22);
    px = XImage_pixel(&img, 9, 9);
    XQ_EQ_I64(px, 0u); /* 项左上外。 */
    px = XImage_pixel(&img, 31, 31);
    XQ_EQ_I64(px, 0u); /* 项右下外。 */

    /* 调用方变换（translate 40,20）必须与缩放矩阵**叠加**：
     * 项设备盒 (50..70,30..50)。 */
    {
        XImage wide;
        XPainter p2;
        XClassDeinit((XClass*)&img);
        XImage_init_ex(&wide, 140, 100, XImageFormat_ARGB32);
        XImage_fill(&wide, 0u);
        XPainter_init(&p2, NULL);
        XQ_REQUIRE(XPainter_begin_image(&p2, &wide));
        XPainter_translate(&p2, 40.0f, 20.0f);
        XGraphicsView_renderViewport(view, &p2, NULL);
        XPainter_deinit(&p2);
        px = XImage_pixel(&wide, 49, 29);
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&wide, 51, 31);
        XQ_EQ_I64((px >> 8) & 0xFF, 0x22);
        px = XImage_pixel(&wide, 69, 49);
        XQ_EQ_I64((px >> 8) & 0xFF, 0x22);
        px = XImage_pixel(&wide, 71, 51);
        XQ_EQ_I64(px, 0u);
        XClassDeinit((XClass*)&wide);
    }

    /* vpRect 非零原点 + 缩放矩阵：视口局部 (0,0) 落到逻辑 (10,20)，
     * 项设备盒 = (10..30,10..30)+(10,20) = (20..40,30..50)。 */
    {
        XRect vpRect;
        XRect_init(&vpRect, 10, 20, 60, 60);
        XClassDeinit((XClass*)&img);
        XImage_init_ex(&img, 100, 100, XImageFormat_ARGB32);
        XImage_fill(&img, 0u);
        XPainter_init(&painter, NULL);
        XQ_REQUIRE(XPainter_begin_image(&painter, &img));
        XGraphicsView_renderViewport(view, &painter, &vpRect);
        XPainter_deinit(&painter);
        px = XImage_pixel(&img, 25, 35);
        XQ_EQ_I64((px >> 8) & 0xFF, 0x22);
        px = XImage_pixel(&img, 35, 55); /* 项下外（y>50）。 */
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&img, 15, 25); /* 视口左外（x<20）。 */
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&img, 65, 45); /* 裁剪带右外（x>60）。 */
        XQ_EQ_I64(px, 0u);
    }

    /* 组合分支（复核 ① 的「某个分支」）：vpRect 非零原点 + 调用方变换同时
     * 生效——顺序必须是 vp → T(vpRect) → 调用方 C（C 最后作用）。
     * 项设备盒 = (10..30,10..30) + (10,20) + (40,20) = (60..80,50..70)。 */
    {
        XRect vpRect;
        XImage wide;
        XPainter p3;
        XRect_init(&vpRect, 10, 20, 60, 60);
        XImage_init_ex(&wide, 120, 100, XImageFormat_ARGB32);
        XImage_fill(&wide, 0u);
        XPainter_init(&p3, NULL);
        XQ_REQUIRE(XPainter_begin_image(&p3, &wide));
        XPainter_translate(&p3, 40.0f, 20.0f);
        XGraphicsView_renderViewport(view, &p3, &vpRect);
        XPainter_deinit(&p3);
        px = XImage_pixel(&wide, 65, 55);
        XQ_EQ_I64((px >> 8) & 0xFF, 0x22);
        px = XImage_pixel(&wide, 55, 55); /* 项左外（x<60）。 */
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&wide, 85, 55); /* 项右外（x>80）。 */
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&wide, 65, 45); /* 项上外（y<50）。 */
        XQ_EQ_I64(px, 0u);
        XClassDeinit((XClass*)&wide);
    }

    /* render(target, source)：source 非零原点必须**先减源再缩放**再摆到
     * target（Qt 探针逐点同参：项设备盒 (20..30,10..20)）。 */
    {
        XRectF target;
        XRect source;
        XClassDeinit((XClass*)&img);
        XImage_init_ex(&img, 100, 100, XImageFormat_ARGB32);
        XImage_fill(&img, 0u);
        target.x = 20.0f;
        target.y = 10.0f;
        target.width = 40.0f;
        target.height = 40.0f;
        XRect_init(&source, 10, 10, 80, 80);
        XPainter_init(&painter, NULL);
        XQ_REQUIRE(XPainter_begin_image(&painter, &img));
        XGraphicsView_render(view, &painter, &target, &source);
        XPainter_deinit(&painter);
        px = XImage_pixel(&img, 25, 15); /* 项中心落位（Qt 实测同像素）。 */
        XQ_EQ_I64((px >> 8) & 0xFF, 0x22);
        px = XImage_pixel(&img, 19, 15);
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&img, 31, 15);
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&img, 25, 9);
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&img, 25, 21);
        XQ_EQ_I64(px, 0u);
        px = XImage_pixel(&img, 2, 2);
        XQ_EQ_I64(px, 0u); /* target 裁剪外。 */
        XClassDeinit((XClass*)&img);
    }

    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}

/* ============ ⑥c paintEvent 整链（XWidget_grab 真驱动，复核 ① 分支） ====
 * renderViewport 的像素断言走的是「调用方直接给 painter」；复核 ① 还点名
 * paintEvent 分支（XWidget_paintImage/paintOffset → 视口几何 → renderViewport）。
 * 本用例用 XWidget_grab 真驱动控件绘制路径（与 XHeaderViewPaint 同范式），
 * 锁「缩放+平移矩阵经 paintEvent 后仍落在同一设备矩形」。
 * 期望值同 ⑥b：matrix=[2 0;0 2;5,7]、条值 (55,57) ⇒ 项视口盒 (10..30)。 */
XQ_TEST(XGraphicsView, paintEventGrab)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsViewProbe* red;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XImageTransform m;
    XScrollBar* hbar;
    XScrollBar* vbar;
    XImage* shot;
    XPointF pos;
    XRectF r;
    uint32_t px;

    xqv_caseBegin();
    XQ_REQUIRE(scene != NULL && view != NULL);
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 200.0f;
    r.height = 200.0f;
    XGraphicsScene_setSceneRect(scene, &r);
    red = xqv_probeCreate(10.0f, 10.0f, 0xFFFF2222u);
    XQ_REQUIRE(red != NULL);
    pos.x = 30.0f;
    pos.y = 30.0f;
    XGraphicsItem_setPos(&red->m_base, &pos);
    XGraphicsScene_addItem(scene, &red->m_base);
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 100, 100);

    XGraphicsItem_matrixIdentity(&m);
    m.m11 = 2.0f;
    m.m22 = 2.0f;
    m.dx = 5.0f;
    m.dy = 7.0f;
    XGraphicsView_setTransform(view, &m, false);
    hbar = XAbstractScrollArea_horizontalScrollBar(&view->m_base);
    vbar = XAbstractScrollArea_verticalScrollBar(&view->m_base);
    XQ_REQUIRE(hbar != NULL && vbar != NULL);
    XScrollBar_setValue(hbar, 55);
    XScrollBar_setValue(vbar, 57);

    shot = XWidget_grab((const XWidget*)view);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(XImage_width(shot), 100);
    XQ_EQ_I64(XImage_height(shot), 100);
    /* 项设备盒 (10..30,10..30)（同 ⑥b 的映射）；四邻域对照证明落位。 */
    px = XImage_pixel(shot, 11, 11);
    XQ_EQ_I64((px >> 8) & 0xFF, 0x22);
    px = XImage_pixel(shot, 29, 29);
    XQ_EQ_I64((px >> 8) & 0xFF, 0x22);
    px = XImage_pixel(shot, 9, 9);
    XQ_EQ_I64(px, 0u);
    px = XImage_pixel(shot, 31, 31);
    XQ_EQ_I64(px, 0u);
    XClassDelete((XClass*)shot);

    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}

/* ==================== 与 scene 联动（invalidate/updateSceneRect） ======== */

XQ_TEST(XGraphicsView, sceneLinkage)
{
    XGraphicsScene* scene;
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XRectF r;

    xqv_caseBegin();
    scene = xq_buildScene(); /* 探针在归零之后创建。 */
    XQ_REQUIRE(view != NULL && scene != NULL);
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 300, 200);

    /* invalidateScene(NULL)：全视口标脏（不崩即可——脏位在控件层）。 */
    XGraphicsView_invalidateScene(view, NULL);
    /* invalidateScene(rect)：映射后标脏。 */
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 100.0f;
    r.height = 80.0f;
    XGraphicsView_invalidateScene(view, &r);

    /* 场景标脏 → 视图渲染核按当前状态出图（联动最小闭环：渲染读实时
     * 状态，不依赖脏区快照）。 */
    {
        XGraphicsViewProbe* a;
        XVector* list =
            XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
        XPointF pos;
        XQ_REQUIRE(list != NULL);
        a = *(XGraphicsViewProbe**)XVector_at_base(list, 0);
        XClassDelete(list);
        /* 项移动后 invalidate，渲染应反映新位置（恒等段：A 场景盒移到
         * (100,7)+(40x30)，视口 (100..140,7..37) 才有内容）。 */
        XImage img;
        XPainter painter;
        pos.x = 100.0f;
        pos.y = 7.0f;
        XGraphicsItem_setPos(&a->m_base, &pos);
        XGraphicsView_invalidateScene(view, NULL);
        XImage_init_ex(&img, 300, 200, XImageFormat_ARGB32);
        XImage_fill(&img, 0u);
        XPainter_init(&painter, NULL);
        XQ_REQUIRE(XPainter_begin_image(&painter, &img));
        XGraphicsView_renderViewport(view, &painter, NULL);
        XPainter_deinit(&painter);
        XQ_CHECK(XImage_pixel(&img, 170, 45) != 0u); /* 新位 (100..140,7..37) → 视口 +(50,25)。 */
        XQ_EQ_I64(XImage_pixel(&img, 60, 40), 0u); /* 旧位视口 (55..95,32..62) 已空。 */
        XClassDeinit((XClass*)&img); /* 栈上图像：只反初始化。 */
    }

    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}

/* ======================================================================== */
/*        ⑪ AnchorUnderMouse（Qt 6.8.3 qgraphicsview.cpp:478-500）          */
/* ======================================================================== */

#if XWINDOWEVENT_ON
/** @brief 场景坐标近等（锚点/居中的整数取整误差 ≤ 1 场景单位）。
 *  @details 不能用 xq_nearF 的 1e-4：centerOn 把条值取整（qRound），
 *           一次缩放 + 重居中的往返误差上限约 0.5 视口像素 / 缩放比。 */
static void xq_nearScene(const char* tag, float got, float expect)
{
    xq_check_impl(fabsf(got - expect) <= 1.0f, "nearScene", __FILE__, __LINE__,
                  "%s got=%.4f expect=%.4f", tag, (double)got,
                  (double)expect);
}

/** @brief 构造并投递视口 ENTER 事件（AnchorUnderMouse 的鼠标位置来源）。
 *  @note  走框架 XEnterEvent_init（带虚表；同 xqv_sendMouse 的复核注）。
 *         事件经滚动区私有过滤器先到 XGraphicsView 的 ViewportEvent，由
 *         ENTER 分支成对记录（视口位置, 场景点）。 */
static void xqv_sendEnter(XGraphicsView* view, int vx, int vy)
{
    XEnterEvent enter;
    XPoint position;
    XPoint globalPos;
    position.x = vx;
    position.y = vy;
    globalPos.x = vx;
    globalPos.y = vy;
    XEnterEvent_init(&enter, XEVENT_TYPE_ENTER, &position, &globalPos);
    XQ_CHECK(XClassGetVtable(&enter) != NULL);
    (void)XCoreApplication_sendEvent(
        (XObject*)XAbstractScrollArea_viewport(&view->m_base),
        (XEvent*)&enter);
}

/** @brief 构造并投递视口 LEAVE 事件（清 ViewportEvent 的悬停跟踪位）。 */
static void xqv_sendLeave(XGraphicsView* view)
{
    XEvent leave;
    XEvent_init(&leave, XEVENT_TYPE_LEAVE);
    XQ_CHECK(XClassGetVtable(&leave) != NULL);
    (void)XCoreApplication_sendEvent(
        (XObject*)XAbstractScrollArea_viewport(&view->m_base), &leave);
}

/** @brief 视口中心处的场景点（AnchorViewCenter 的判别量）。
 *  @note  公开 mapToScene 只收整数点 ⇒ 中心取整后查询；与 centerOn 的
 *         w/2.0f 口径最多差 0.5 视口像素 ⇒ 用 xq_nearScene 的 1 场景
 *         单位容差覆盖。 */
static XPointF xqv_viewportCenterScene(const XGraphicsView* view)
{
    XWidget* vp = XAbstractScrollArea_viewport(
        &((XGraphicsView*)view)->m_base);
    XPoint center;
    center.x = vp ? XWidget_width(vp) / 2 : 0;
    center.y = vp ? XWidget_height(vp) / 2 : 0;
    return XGraphicsView_mapToScene(view, &center);
}

XQ_TEST(XGraphicsView, anchorUnderMouse)
{
    XGraphicsScene* centerScene = XGraphicsScene_create();
    XGraphicsScene* mouseScene = XGraphicsScene_create();
    XGraphicsView* centerView = XGraphicsView_create(NULL, 0);
    XGraphicsView* mouseView = XGraphicsView_create(NULL, 0);
    XScrollBar* hbar;
    XScrollBar* vbar;
    XWidget* vp;
    XRectF r;
    XPointF mouseBefore;
    XPointF mouseAfter;
    XPointF centerBefore;
    XPointF centerAfter;
    XPoint mousePoint;
    int sx0;
    int sy0;
    int vw0;
    int vh0;

    xqv_caseBegin();
    XQ_REQUIRE(centerScene != NULL && mouseScene != NULL);
    XQ_REQUIRE(centerView != NULL && mouseView != NULL);
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 400.0f;
    r.height = 400.0f;
    XGraphicsScene_setSceneRect(centerScene, &r);
    XGraphicsScene_setSceneRect(mouseScene, &r);

    /* ---- ① 基线（AnchorViewCenter）：缩放前后视口中心场景点不变 ----
     * 场景 400×400 / 视图 200×200 ⇒ scale(2,2) 前后都放不下（条恒在），
     * 视口尺寸在缩放期间不变——否则「中心点不变」的判据会被视口尺寸
     * 变化污染。 */
    XGraphicsView_setScene(centerView, centerScene);
    XWidget_resize((XWidget*)centerView, 200, 200);
    XGraphicsView_setTransformationAnchor(
        centerView, XGraphicsViewViewportAnchor_AnchorViewCenter);
    hbar = XAbstractScrollArea_horizontalScrollBar(&centerView->m_base);
    vbar = XAbstractScrollArea_verticalScrollBar(&centerView->m_base);
    XQ_REQUIRE(hbar != NULL && vbar != NULL);
    XScrollBar_setValue(hbar, 100);
    XScrollBar_setValue(vbar, 100);
    vp = XAbstractScrollArea_viewport(&centerView->m_base);
    XQ_REQUIRE(vp != NULL);
    vw0 = XWidget_width(vp);
    vh0 = XWidget_height(vp);
    centerBefore = xqv_viewportCenterScene(centerView);
    XGraphicsView_scale(centerView, 2.0f, 2.0f);
    /* 前提复核：缩放期间视口尺寸不变（否则中心判据被尺寸变化污染）。 */
    XQ_EQ_I64(XWidget_width(XAbstractScrollArea_viewport(&centerView->m_base)),
              vw0);
    XQ_EQ_I64(XWidget_height(XAbstractScrollArea_viewport(&centerView->m_base)),
              vh0);
    centerAfter = xqv_viewportCenterScene(centerView);
    xq_nearScene("viewCenter.x", centerAfter.x, centerBefore.x);
    xq_nearScene("viewCenter.y", centerAfter.y, centerBefore.y);

    /* ---- ② AnchorUnderMouse + 悬停：缩放前后**鼠标处**场景点不变 ----
     * Qt :482-491：underMouse 时 centerOn(lastMouseMoveScenePoint +
     * (mapToScene(视口中心) − mapToScene(鼠标视口位置)))。 */
    XGraphicsView_setScene(mouseView, mouseScene);
    XWidget_resize((XWidget*)mouseView, 200, 200);
    XGraphicsView_setTransformationAnchor(
        mouseView, XGraphicsViewViewportAnchor_AnchorUnderMouse);
    XQ_EQ_I64(XGraphicsView_transformationAnchor(mouseView),
              XGraphicsViewViewportAnchor_AnchorUnderMouse);
    /* Qt :1370-1371：置 AnchorUnderMouse 必须把视口鼠标跟踪打开。 */
    vp = XAbstractScrollArea_viewport(&mouseView->m_base);
    XQ_REQUIRE(vp != NULL);
    XQ_CHECK(XWidget_hasMouseTracking(vp));
    hbar = XAbstractScrollArea_horizontalScrollBar(&mouseView->m_base);
    vbar = XAbstractScrollArea_verticalScrollBar(&mouseView->m_base);
    XQ_REQUIRE(hbar != NULL && vbar != NULL);
    XScrollBar_setValue(hbar, 100);
    XScrollBar_setValue(vbar, 100);
    /* 悬停点取偏中心处（若取中心，锚点与中心锚不可判别）。 */
    mousePoint.x = 150;
    mousePoint.y = 40;
    xqv_sendEnter(mouseView, mousePoint.x, mousePoint.y);
    /* **Qt 6.8.0 offscreen 探针逐值对拍**（/tmp/b7qtprobe/probe_anchor.cpp，
     * 同参：scene 400×400 / view 200×200 / scale(2,2) / 条值 (100,100) /
     * 鼠标视口 (150,40)；探针输出）：
     *   viewport=184x184 hbar=100 vbar=100
     *   B mouseTracking=1 underMouse=1 cursorPos(mapped)=150,40
     *   B mouse: before=250.000,140.000 after=250.000,140.000
     *   B center: before=192.000,192.000 after=221.000,166.000
     *   C noMouse center: before=192.000,192.000 after=192.000,192.000
     * 鼠标处场景点 (250,140) 与视口尺寸无关（= 视口 (150,40)+条值
     * (100,100)、恒等矩阵）⇒ 逐值断言；中心点随视口尺寸变——Qt 探针
     * 扣条厚得 184×184，本框架 AsNeeded 下 maximumViewportSize 不扣条厚
     * （由 XAbstractScrollAreaAlignTest.defaultsAndGeometry 锁定）⇒ 视口
     * 200×200、中心 200/225，按 centerView 原式用本视图实测视口尺寸重算
     * （见下）。 */
    mouseBefore = XGraphicsView_mapToScene(mouseView, &mousePoint);
    centerBefore = xqv_viewportCenterScene(mouseView);
    sx0 = XScrollBar_value(hbar);
    sy0 = XScrollBar_value(vbar);
    XGraphicsView_scale(mouseView, 2.0f, 2.0f);
    mouseAfter = XGraphicsView_mapToScene(mouseView, &mousePoint);
    xq_nearScene("underMouse.x", mouseAfter.x, mouseBefore.x);
    xq_nearScene("underMouse.y", mouseAfter.y, mouseBefore.y);
    /* 判别性：中心**必须**动（鼠标不在中心 ⇒ 锚点不是中心）。 */
    centerAfter = xqv_viewportCenterScene(mouseView);
    XQ_CHECK_MSG(fabsf(centerAfter.x - centerBefore.x) > 5.0f,
                 "center moved? before=%.3f after=%.3f",
                 (double)centerBefore.x, (double)centerAfter.x);
    /* **Qt 探针逐值对拍**：鼠标处场景点与视口尺寸无关 ⇒ 与 Qt 完全相等
     * （探针 B mouse before/after = 250,140）。 */
    xq_nearScene("qt.mouseBefore.x", mouseBefore.x, 250.0f);
    xq_nearScene("qt.mouseBefore.y", mouseBefore.y, 140.0f);
    xq_nearScene("qt.mouseAfter.x", mouseAfter.x, 250.0f);
    xq_nearScene("qt.mouseAfter.y", mouseAfter.y, 140.0f);
    /* 中心点按 centerView 原式 + **本视图实测视口尺寸**重算：Qt 探针
     * 视口 184×184 ⇒ 192/221,166；本框架视口 200×200 ⇒ 200/225,170。
     * 代数：centerOn(target) 之后 target 必然成为新中心点，而
     *   target = lastMouseMoveScenePoint
     *          + mapToScene(视口中心) − mapToScene(鼠标视口位置)
     * （矩阵 scale 2、scroll 在 recalc 前后不变 = (sx0, sy0)）。 */
    {
        float vw = (float)XWidget_width(vp);
        float vh = (float)XWidget_height(vp);
        float mx = (float)mousePoint.x + (float)sx0;
        float my = (float)mousePoint.y + (float)sy0;
        float expectX = mx + (((vw / 2.0f) + (float)sx0) / 2.0f - mx / 2.0f);
        float expectY = my + (((vh / 2.0f) + (float)sy0) / 2.0f - my / 2.0f);
        xq_nearScene("centerBefore.x", centerBefore.x,
                     vw / 2.0f + (float)sx0);
        xq_nearScene("centerBefore.y", centerBefore.y,
                     vh / 2.0f + (float)sy0);
        xq_nearScene("centerAfter.x", centerAfter.x, expectX);
        xq_nearScene("centerAfter.y", centerAfter.y, expectY);
    }

    /* ---- ③ 无鼠标事件 ⇒ 回落 AnchorViewCenter（Qt :489-491）----
     * 全新视图：AnchorUnderMouse 但从未收到 ENTER/MOVE ⇒ underMouse 为假
     * ⇒ centerOn(lastCenterPoint) ⇒ 视口中心场景点不变。 */
    {
        XGraphicsScene* bareScene = XGraphicsScene_create();
        XGraphicsView* bareView = XGraphicsView_create(NULL, 0);
        XQ_REQUIRE(bareScene != NULL && bareView != NULL);
        XGraphicsScene_setSceneRect(bareScene, &r);
        XGraphicsView_setScene(bareView, bareScene);
        XWidget_resize((XWidget*)bareView, 200, 200);
        XGraphicsView_setTransformationAnchor(
            bareView, XGraphicsViewViewportAnchor_AnchorUnderMouse);
        hbar = XAbstractScrollArea_horizontalScrollBar(&bareView->m_base);
        vbar = XAbstractScrollArea_verticalScrollBar(&bareView->m_base);
        XQ_REQUIRE(hbar != NULL && vbar != NULL);
        XScrollBar_setValue(hbar, 100);
        XScrollBar_setValue(vbar, 100);
        /* 前提断言：控件悬停位也是假（无窗口 ⇒ 无窗口级 ENTER 冒泡）。 */
        XQ_CHECK(!XWidget_underMouse((XWidget*)bareView));
        centerBefore = xqv_viewportCenterScene(bareView);
        XGraphicsView_scale(bareView, 2.0f, 2.0f);
        centerAfter = xqv_viewportCenterScene(bareView);
        xq_nearScene("noMouseCenter.x", centerAfter.x, centerBefore.x);
        xq_nearScene("noMouseCenter.y", centerAfter.y, centerBefore.y);
        XClassDelete((XClass*)bareView);
        XClassDelete((XClass*)bareScene);
    }

    /* ---- ④ LEAVE 之后同样回落（m_mouseInside 清零）----
     * ② 的视图此时矩阵为 scale(2)；LEAVE 后 scale(0.5) 回单位阵：中心点
     * 不动、鼠标处场景点**必然改变**（不再按鼠标锚定）。 */
    xqv_sendLeave(mouseView);
    mouseBefore = XGraphicsView_mapToScene(mouseView, &mousePoint);
    centerBefore = xqv_viewportCenterScene(mouseView);
    XGraphicsView_scale(mouseView, 0.5f, 0.5f);
    centerAfter = xqv_viewportCenterScene(mouseView);
    xq_nearScene("afterLeaveCenter.x", centerAfter.x, centerBefore.x);
    xq_nearScene("afterLeaveCenter.y", centerAfter.y, centerBefore.y);
    mouseAfter = XGraphicsView_mapToScene(mouseView, &mousePoint);
    XQ_CHECK_MSG(fabsf(mouseAfter.x - mouseBefore.x) > 5.0f,
                 "mouse anchor must be off after leave: before=%.3f after=%.3f",
                 (double)mouseBefore.x, (double)mouseAfter.x);

    XClassDelete((XClass*)centerView);
    XClassDelete((XClass*)mouseView);
    XClassDelete((XClass*)centerScene);
    XClassDelete((XClass*)mouseScene);
}
#endif /* XWINDOWEVENT_ON */

/* ======================================================================== */
/*      ⑫ 背景笔刷样式分派（Solid / LinearGradient / Texture / Pattern）     */
/* ======================================================================== */

XQ_TEST(XGraphicsView, backgroundBrushes)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsView* view = XGraphicsView_create(NULL, 0);
    XLinearGradient gradient;
    XBrush brush;
    XImage tile;
    XImage img;
    XPainter painter;
    XRectF r;
    uint32_t px;

    xqv_caseBegin();
    XQ_REQUIRE(scene != NULL && view != NULL);
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 64.0f;
    r.height = 64.0f;
    XGraphicsScene_setSceneRect(scene, &r);
    XGraphicsView_setScene(view, scene);
    XWidget_resize((XWidget*)view, 64, 64);
    /* 视口 = 整幅、恒等映射（场景 64×64 与视图同尺寸 ⇒ 无滚动、缩进 0）：
     * 背景填充矩形与设备像素一一对应，像素断言可直接给期望值。 */
    XImage_init_ex(&img, 64, 64, XImageFormat_ARGB32);

    /* ---- ① Solid：纯色填充（既有行为，逐位不变） ---- */
    brush = XBrush_create_color(XColor_create_rgba(0xFF112233u),
                                XBrushStyle_SolidPattern);
    XGraphicsView_setBackgroundBrush(view, &brush);
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    XQ_REQUIRE(XPainter_begin_image(&painter, &img));
    XGraphicsView_renderViewport(view, &painter, NULL);
    XPainter_deinit(&painter);
    px = XImage_pixel(&img, 10, 10);
    XQ_EQ_I64(px, 0xFF112233u);
    px = XImage_pixel(&img, 63, 63);
    XQ_EQ_I64(px, 0xFF112233u);

    /* ---- ② LinearGradient：红→蓝，垂直铺满暴露矩形（0,0,64,64）---- */
    XLinearGradient_init_ex(&gradient, 0.0, 0.0, 0.0, 64.0);
    XGradient_setColorAt(&gradient.m_base, 0.0, 0xFFFF0000u);
    XGradient_setColorAt(&gradient.m_base, 1.0, 0xFF0000FFu);
    brush = XBrush_create_color(XColor_create_rgba(0xFF000000u),
                               XBrushStyle_SolidPattern);
    XBrush_setGradient(&brush, &gradient.m_base);
    XQ_EQ_I64(XBrush_style(&brush), XBrushStyle_LinearGradientPattern);
    XQ_CHECK_PTR(XBrush_gradient(&brush));
    XGraphicsView_setBackgroundBrush(view, &brush);
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    XQ_REQUIRE(XPainter_begin_image(&painter, &img));
    XGraphicsView_renderViewport(view, &painter, NULL);
    XPainter_deinit(&painter);
    /* 顶行 t≈0.008 ⇒ 近纯红；底行 t≈0.992 ⇒ 近纯蓝；中行两者并存。 */
    px = XImage_pixel(&img, 32, 0);
    XQ_CHECK_MSG(((px >> 16) & 0xFF) >= 240u && (px & 0xFF) <= 8u,
                 "gradient top=0x%08X", px);
    px = XImage_pixel(&img, 32, 63);
    XQ_CHECK_MSG((px & 0xFF) >= 240u && ((px >> 16) & 0xFF) <= 8u,
                 "gradient bottom=0x%08X", px);
    px = XImage_pixel(&img, 32, 32);
    {
        uint32_t rr = (px >> 16) & 0xFFu;
        uint32_t bb = px & 0xFFu;
        XQ_CHECK_MSG(rr >= 96u && rr <= 160u && bb >= 96u && bb <= 160u,
                     "gradient mid=0x%08X", px);
    }

    /* ---- ③ TexturePattern（XImage 纹理槽）：4×4 棋盘 tile 的周期 ----
     * tile = 4×4，左上/右下 2×2 红、右上/左下 2×2 蓝 ⇒ 列/行周期都是 4。 */
    XImage_init_ex(&tile, 4, 4, XImageFormat_ARGB32);
    XImage_fill(&tile, 0xFFFF0000u);
    XImage_setPixel(&tile, 2, 0, 0xFF0000FFu);
    XImage_setPixel(&tile, 3, 0, 0xFF0000FFu);
    XImage_setPixel(&tile, 2, 1, 0xFF0000FFu);
    XImage_setPixel(&tile, 3, 1, 0xFF0000FFu);
    XImage_setPixel(&tile, 0, 2, 0xFF0000FFu);
    XImage_setPixel(&tile, 1, 2, 0xFF0000FFu);
    XImage_setPixel(&tile, 0, 3, 0xFF0000FFu);
    XImage_setPixel(&tile, 1, 3, 0xFF0000FFu);
    XBrush_init(&brush);
    XBrush_setTextureImage(&brush, &tile); /* 借用：tile 保活到本用例末尾。 */
    XQ_EQ_I64(XBrush_style(&brush), XBrushStyle_TexturePattern);
    XGraphicsView_setBackgroundBrush(view, &brush);
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    XQ_REQUIRE(XPainter_begin_image(&painter, &img));
    XGraphicsView_renderViewport(view, &painter, NULL);
    XPainter_deinit(&painter);
    XQ_EQ_I64(XImage_pixel(&img, 0, 0), 0xFFFF0000u); /* tile(0,0) 红。 */
    XQ_EQ_I64(XImage_pixel(&img, 1, 1), 0xFFFF0000u);
    XQ_EQ_I64(XImage_pixel(&img, 2, 0), 0xFF0000FFu); /* tile(2,0) 蓝。 */
    XQ_EQ_I64(XImage_pixel(&img, 3, 1), 0xFF0000FFu);
    XQ_EQ_I64(XImage_pixel(&img, 0, 2), 0xFF0000FFu); /* tile(0,2) 蓝。 */
    XQ_EQ_I64(XImage_pixel(&img, 2, 2), 0xFFFF0000u); /* tile(2,2) 红。 */
    /* 周期 4：第 2 个 tile 与第 1 个逐格同相（列/行都验）。 */
    XQ_EQ_I64(XImage_pixel(&img, 4, 0), XImage_pixel(&img, 0, 0));
    XQ_EQ_I64(XImage_pixel(&img, 6, 2), XImage_pixel(&img, 2, 2));
    XQ_EQ_I64(XImage_pixel(&img, 0, 4), XImage_pixel(&img, 0, 0));
    XQ_EQ_I64(XImage_pixel(&img, 1, 6), XImage_pixel(&img, 1, 2));
    XQ_EQ_I64(XImage_pixel(&img, 5, 5), XImage_pixel(&img, 1, 1));
    XQ_EQ_I64(XImage_pixel(&img, 63, 63), XImage_pixel(&img, 3, 3));

    /* 注：背景纹理的 **XPixmap 纹理槽**（XBrush_setTexture → 走框架平铺
     * 原语 XPainter_drawTiledPixmap）不并入本零泄漏用例——XBrush_setTexture
     * 每次调用都无条件在画刷驻留池堆拷一份 XPixmap（XBrush.c:64-72
     * xbrush_own_texture；池去重按**指针**比 texture，XBrush.c:131-151
     * xbrush_intern）⇒ 每次调用永久 +约 10.7KB，与 --leak-tolerance=0
     * 冲突。该路径实测可用（临时并入时 35 项断言全绿，像素相位与本用例
     * ③ 逐格一致），缺口已登记 openNotes。 */

    /* ---- ④ 标准图案（HorPattern）：Qt 8×8 位表的空行必须真空 ----
     * 图案位表（XPainter.c kBrushPatterns）：HorPattern = 行 3 全 0 ⇒
     * y&7==3 的行不落像素，其余行落笔刷色。若「图案被静默跳过」，
     * 行 0 也不会落色（判别性双向断言）。 */
    brush = XBrush_create_color(XColor_create_rgba(0xFF00FF00u),
                                XBrushStyle_HorPattern);
    XGraphicsView_setBackgroundBrush(view, &brush);
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    XQ_REQUIRE(XPainter_begin_image(&painter, &img));
    XGraphicsView_renderViewport(view, &painter, NULL);
    XPainter_deinit(&painter);
    XQ_EQ_I64(XImage_pixel(&img, 10, 0), 0xFF00FF00u);
    XQ_EQ_I64(XImage_pixel(&img, 10, 3), 0u);
    XQ_EQ_I64(XImage_pixel(&img, 10, 8), 0xFF00FF00u);
    XQ_EQ_I64(XImage_pixel(&img, 10, 11), 0u);

    /* ---- ⑤ 纹理相位锚定**场景原点**（滚动不漂移）----
     * 换 128×128 场景 / 64×64 视图并把横条拖到 2：视口像素 0 显示场景
     * x=2 ⇒ 瓦片列相位 = (2 − 0) mod 4 = 2（蓝）；像素 2 显示场景 x=4
     * ⇒ 相位 0（红）。若按「暴露矩形左上角」锚定（错误口径），相位会
     * 恒从 0 起 ⇒ 像素 0 仍是红，本断言可判别。 */
    {
        XGraphicsScene* wideScene = XGraphicsScene_create();
        XGraphicsView* wideView = XGraphicsView_create(NULL, 0);
        XScrollBar* whbar;
        XQ_REQUIRE(wideScene != NULL && wideView != NULL);
        r.x = 0.0f;
        r.y = 0.0f;
        r.width = 128.0f;
        r.height = 128.0f;
        XGraphicsScene_setSceneRect(wideScene, &r);
        XGraphicsView_setScene(wideView, wideScene);
        XWidget_resize((XWidget*)wideView, 64, 64);
        whbar = XAbstractScrollArea_horizontalScrollBar(&wideView->m_base);
        XQ_REQUIRE(whbar != NULL);
        XQ_CHECK(XScrollBar_maximum(whbar) >= 2);
        XScrollBar_setValue(whbar, 2);
        /* 复用 ③ 的棋盘纹理（tile 仍为借用、仍存活）。 */
        XBrush_init(&brush);
        XBrush_setTextureImage(&brush, &tile);
        XGraphicsView_setBackgroundBrush(wideView, &brush);
        XImage_fill(&img, 0u);
        XPainter_init(&painter, NULL);
        XQ_REQUIRE(XPainter_begin_image(&painter, &img));
        XGraphicsView_renderViewport(wideView, &painter, NULL);
        XPainter_deinit(&painter);
        XQ_EQ_I64(XImage_pixel(&img, 0, 0), 0xFF0000FFu); /* 场景 x=2 ⇒ 蓝。 */
        XQ_EQ_I64(XImage_pixel(&img, 2, 0), 0xFFFF0000u); /* 场景 x=4 ⇒ 红。 */
        XQ_EQ_I64(XImage_pixel(&img, 4, 0), 0xFF0000FFu); /* 场景 x=6 ⇒ 蓝。 */
        XClassDelete((XClass*)wideView);
        XClassDelete((XClass*)wideScene);
    }

    /* 收尾：清背景笔刷（NoBrush=不铺底），纹理图像解除借用前先放开画刷。 */
    XGraphicsView_setBackgroundBrush(view, NULL);
    XQ_EQ_I64(XBrush_style(XGraphicsView_backgroundBrush_const(view)),
              XBrushStyle_NoBrush);
    XClassDeinit((XClass*)&tile);
    XClassDeinit((XClass*)&img);
    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)view);
    XClassDelete((XClass*)scene);
}
