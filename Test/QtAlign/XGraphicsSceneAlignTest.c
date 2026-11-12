/**
 * @file       XGraphicsSceneAlignTest.c
 * @brief      XGraphicsScene 与 Qt 6.8 QGraphicsScene 对齐契约测试。
 * @details    覆盖场景层十二项验收（对齐依据：Qt 6.8.3 原文 + 本机
 *             Qt 6.8.0 offscreen 探针 /tmp/w2g_probe/probe_scene.cpp
 *             逐点对拍，关键结果直接写进断言，锚点标 P<n>）：
 *             ① add/remove/clear 所有权与计数（remove 不删项、clear 删
 *                顶层项、跨场景 remove 拒绝）；
 *             ② item 跨 scene 移动（旧场景先摘、子树随迁）；
 *             ③ itemAt 按 z 降序命中（重叠项 C>A、子项 D 遮父 A、
 *                空点 NULL、隐藏项不命中——P4/P5/P6/P9）；
 *             ④ items(rect) 相交集合与顺序（升序 [A,D,C,B] 父先子后、
 *                降序整表反转、隐藏项被几何重载剔除、items() 纯索引
 *                口径含隐藏项——P2/P3/P7/P8/P10/P14）；
 *             ⑤ 选中（仅 ItemIsSelectable 响应、批量闸内
 *                selectionChanged 一次/批（P11 selChanged=1）、
 *                clearSelection 再发一次（P12 selChanged=2）、闸外
 *                setSelected 即时发）；
 *             ⑥ 焦点（仅 ItemIsFocusable+可见可获焦；不可聚焦项的
 *                setFocusItem=只清焦；FocusIn/FocusOut 派发）；
 *             ⑦ 鼠标 press/move/release 命中派发（隐式抓取、拒绝则
 *                下探候选）+ 键事件父链上溯（Qt keyPressEvent 的
 *                do-while 传播）+ 空点按下清选中与焦点；
 *             ⑧ 悬停 Enter/Leave 切换（m_lastHoverItem 单项跟踪、
 *                仅 acceptHoverEvents 的项收到、hoverLeaveEvent 清跟踪）；
 *             ⑨ render 出图像素级：不重叠=纯色、重叠=上层遮盖+
 *                effectiveOpacity 混色（0.5 蓝 over 红 = 半混）、隐藏
 *                即不画；paint 派发按可见集；
 *             ⑩ Qt 探针逐点对拍（同参几何见 buildProbeScene，P 系列锚点
 *                分布在 ③④⑤ 用例内）；
 *             ⑪ 泄漏零容忍（运行器缺省门；用例内全量释放——场景/项/
 *                items 容器）；
 *             ⑫ 既有 XGraphicsItem 13 用例全绿（跑全套 qt-align，
 *                XGraphicsItemAlignTest 在同场景语义下不回归）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGraphicsScene.h"
#include "XGraphicsItem.h"
#include "XObject.h"
#include "XClass.h"
#include "XMemory.h"
#include "XPainter.h"
#include "XImage.h"
#include "XVector.h"

/* ==================== 事件/绘制探针图元 ==================== */

/*
 * 事件探针：覆写 Event（记账 + 显式 accepted 决策，绕开缺省 handler 的
 * 选择/拖动语义以便精确断言派发次序）、Paint（填色 + 计数）、Deinit
 * （存活记账）。ItemChange 探针独立成类（计数各通知码 + 可配置否决/
 * 改道），同在文末用例消费。
 */
XCLASS_DEFINE_BEGING(XGraphicsSceneEventProbe)
XCLASS_DEFINE_EXTEND_END(XGraphicsSceneEventProbe, XGraphicsItem)

typedef struct XGraphicsSceneEventProbe
{
    XGraphicsItem m_base;   /**< 基类；必须第一个。 */
    float m_w;              /**< 包围盒 (0,0,m_w,m_h)。 */
    float m_h;              /**< 包围盒高。 */
    uint32_t m_brush;       /**< Paint 填色（0xAARRGGBB）。 */
    int m_press;            /**< GRAPHICS_SCENE_MOUSE_PRESS 次数。 */
    int m_move;             /**< GRAPHICS_SCENE_MOUSE_MOVE 次数。 */
    int m_release;          /**< GRAPHICS_SCENE_MOUSE_RELEASE 次数。 */
    int m_enter;            /**< GRAPHICS_SCENE_HOVER_ENTER 次数。 */
    int m_hoverMove;        /**< GRAPHICS_SCENE_HOVER_MOVE 次数。 */
    int m_leave;            /**< GRAPHICS_SCENE_HOVER_LEAVE 次数。 */
    int m_keyPress;         /**< KEY_PRESS 次数。 */
    int m_focusIn;          /**< FOCUS_IN 次数。 */
    int m_focusOut;         /**< FOCUS_OUT 次数。 */
    bool m_pressAccept;     /**< press 处理后 accepted 值（true=接受）。 */
    bool m_keyAccept;       /**< keyPress 处理后 accepted 值（缺省 false=
                                对标 Qt 缺省 keyPressEvent→ignore）。 */
} XGraphicsSceneEventProbe;

static int s_probeAlive = 0; /**< 存活探针数（泄漏/级联判据）。 */

static XRectF xq_probeBoundingRect(const XGraphicsItem* self)
{
    XGraphicsSceneEventProbe* probe = (XGraphicsSceneEventProbe*)self;
    XRectF out;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = probe ? probe->m_w : 0.0f;
    out.height = probe ? probe->m_h : 0.0f;
    return out;
}

static void xq_probePaint(XGraphicsItem* self, XPainter* painter)
{
    XGraphicsSceneEventProbe* probe = (XGraphicsSceneEventProbe*)self;
    XRect r;
    if (!probe || !painter) return;
    r.x = 0;
    r.y = 0;
    r.width = (int)probe->m_w;
    r.height = (int)probe->m_h;
    /* 纯填色（fillRect 不涉笔/刷状态；项局部坐标经场景变换落到设备）。 */
    XPainter_fillRect(painter, &r, probe->m_brush);
}

static bool xq_probeEvent(XGraphicsItem* base, XEvent* event)
{
    XGraphicsSceneEventProbe* probe = (XGraphicsSceneEventProbe*)base;
    if (!probe || !event) return false;
    switch (event->m_type) {
    case XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_PRESS:
        ++probe->m_press;
        XEvent_setAccepted_base(event, probe->m_pressAccept);
        break;
    case XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_MOVE:
        ++probe->m_move;
        break;
    case XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_RELEASE:
        ++probe->m_release;
        break;
    case XEVENT_TYPE_GRAPHICS_SCENE_HOVER_ENTER:
        ++probe->m_enter;
        break;
    case XEVENT_TYPE_GRAPHICS_SCENE_HOVER_MOVE:
        ++probe->m_hoverMove;
        break;
    case XEVENT_TYPE_GRAPHICS_SCENE_HOVER_LEAVE:
        ++probe->m_leave;
        break;
    case XEVENT_TYPE_KEY_PRESS:
        ++probe->m_keyPress;
        /* 对标 Qt：缺省 keyPressEvent 是 ignore；探针显式决策。 */
        XEvent_setAccepted_base(event, probe->m_keyAccept);
        break;
    case XEVENT_TYPE_FOCUS_IN:
        ++probe->m_focusIn;
        break;
    case XEVENT_TYPE_FOCUS_OUT:
        ++probe->m_focusOut;
        break;
    default:
        break;
    }
    return true;
}

static void xq_probeDeinit(XGraphicsItem* base)
{
    --s_probeAlive;
    /* 回落父类 Deinit：级联删子 + 场景注销 + 摘离父项 + XObject 清尾。 */
    XClass_Deinit_Parent(XGraphicsItem, base);
}

static XVtable* XGraphicsSceneEventProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsSceneEventProbe)
    XVTABLE_INHERIT_XCLASS(XGraphicsItem);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xq_probeDeinit);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_BoundingRect, xq_probeBoundingRect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Paint, xq_probePaint);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Event, xq_probeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 堆上建探针（调用方 XClassDelete 或经场景 clear 级联释放）。 */
static XGraphicsSceneEventProbe* xq_probeCreate(float w, float h, uint32_t brush)
{
    XGraphicsSceneEventProbe* p =
        (XGraphicsSceneEventProbe*)XMemory_malloc(sizeof(*p),
                                                  XCLASS_DEFAULT_MEMORY_TYPE);
    if (!p) return NULL;
    XMemset(p, 0, sizeof(*p));
    XGraphicsItem_init(&p->m_base);
    XClassSetVtable(p, XGraphicsSceneEventProbe);
    Set_Class_Memory(p, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(p, true);
    p->m_w = w;
    p->m_h = h;
    p->m_brush = brush;
    p->m_pressAccept = true;
    p->m_keyAccept = false;
    ++s_probeAlive;
    return p;
}

/* ==================== ItemChange 探针 ==================== */

XCLASS_DEFINE_BEGING(XGraphicsSceneChangeProbe)
XCLASS_DEFINE_EXTEND_END(XGraphicsSceneChangeProbe, XGraphicsItem)

typedef struct XGraphicsSceneChangeProbe
{
    XGraphicsItem m_base;      /**< 基类；必须第一个。 */
    float m_w;                 /**< 包围盒宽。 */
    float m_h;                 /**< 包围盒高。 */
    int m_sceneChange;         /**< ItemSceneChange 次数。 */
    int m_sceneHasChanged;     /**< ItemSceneHasChanged 次数。 */
    int m_selectedHasChanged;  /**< ItemSelectedHasChanged 次数。 */
    int m_visibleHasChanged;   /**< ItemVisibleHasChanged 次数。 */
    int m_positionHasChanged;  /**< ItemPositionHasChanged 次数。 */
    int m_parentHasChanged;    /**< ItemParentHasChanged 次数。 */
    int m_vetoSceneChange;     /**< 1=ItemSceneChange 回 NULL（否决）。 */
    XGraphicsScene* m_redirectTo; /**< 非 NULL=ItemSceneChange 回它（改道）。 */
} XGraphicsSceneChangeProbe;

static int s_changeAlive = 0;

static XRectF xq_changeProbeBoundingRect(const XGraphicsItem* self)
{
    XGraphicsSceneChangeProbe* probe = (XGraphicsSceneChangeProbe*)self;
    XRectF out;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = probe ? probe->m_w : 0.0f;
    out.height = probe ? probe->m_h : 0.0f;
    return out;
}

static const void* xq_changeProbeItemChange(XGraphicsItem* base, int change,
                                            const void* value)
{
    XGraphicsSceneChangeProbe* probe = (XGraphicsSceneChangeProbe*)base;
    if (!probe) return value;
    switch ((XGraphicsItemChange)change) {
    case XGraphicsItemChange_ItemSceneChange:
        ++probe->m_sceneChange;
        if (probe->m_vetoSceneChange) return NULL;
        if (probe->m_redirectTo) return probe->m_redirectTo;
        break;
    case XGraphicsItemChange_ItemSceneHasChanged:
        ++probe->m_sceneHasChanged;
        break;
    case XGraphicsItemChange_ItemSelectedHasChanged:
        ++probe->m_selectedHasChanged;
        break;
    case XGraphicsItemChange_ItemVisibleHasChanged:
        ++probe->m_visibleHasChanged;
        break;
    case XGraphicsItemChange_ItemPositionHasChanged:
        ++probe->m_positionHasChanged;
        break;
    case XGraphicsItemChange_ItemParentHasChanged:
        ++probe->m_parentHasChanged;
        break;
    default:
        break;
    }
    /* 回落父类缺省（_base 契约：原样回传）。 */
    return XClass_Parent(XGraphicsItem, EXGraphicsItem_ItemChange,
                         XGraphicsItem_ItemChangeSlot)(base, change, value);
}

static void xq_changeProbeDeinit(XGraphicsItem* base)
{
    --s_changeAlive;
    XClass_Deinit_Parent(XGraphicsItem, base);
}

static XVtable* XGraphicsSceneChangeProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsSceneChangeProbe)
    XVTABLE_INHERIT_XCLASS(XGraphicsItem);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xq_changeProbeDeinit);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_BoundingRect,
                             xq_changeProbeBoundingRect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_ItemChange, xq_changeProbeItemChange);
    return XVTABLE_DEFAULT;
}

static XGraphicsSceneChangeProbe* xq_changeProbeCreate(float w, float h)
{
    XGraphicsSceneChangeProbe* p =
        (XGraphicsSceneChangeProbe*)XMemory_malloc(sizeof(*p),
                                                   XCLASS_DEFAULT_MEMORY_TYPE);
    if (!p) return NULL;
    XMemset(p, 0, sizeof(*p));
    XGraphicsItem_init(&p->m_base);
    XClassSetVtable(p, XGraphicsSceneChangeProbe);
    Set_Class_Memory(p, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(p, true);
    p->m_w = w;
    p->m_h = h;
    ++s_changeAlive;
    return p;
}

/* ==================== 公共助手 ==================== */

/** @brief 对拍装配（与 Qt 探针 /tmp/w2g_probe/probe_scene.cpp 完全同参）：
 *  A(10,20,40x30,z0 可选可移可焦) / B(60,10,24x18,z5 不可选) /
 *  C(20,25,16x12,z2 可选) / D=A 的子项（局部 30,25，12x10，z1）。
 *  out: [0]=scene [1]=A [2]=B [3]=C [4]=D。全堆分配，用 XGraphicsScene_
 *  addItem 挂场景（B 后续用例可能迁走）。 */
static void xq_buildProbeScene(XGraphicsScene** outScene,
                               XGraphicsSceneEventProbe** outA,
                               XGraphicsSceneEventProbe** outB,
                               XGraphicsSceneEventProbe** outC,
                               XGraphicsSceneEventProbe** outD)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsSceneEventProbe* a;
    XGraphicsSceneEventProbe* b;
    XGraphicsSceneEventProbe* c;
    XGraphicsSceneEventProbe* d;
    XPointF pos;
    *outScene = scene;
    XQ_REQUIRE(scene != NULL);

    *outA = a = xq_probeCreate(40.0f, 30.0f, 0xFFCC3333u);
    *outB = b = xq_probeCreate(24.0f, 18.0f, 0xFF33CC33u);
    *outC = c = xq_probeCreate(16.0f, 12.0f, 0xFF3333CCu);
    *outD = d = xq_probeCreate(12.0f, 10.0f, 0xFFCCCC33u);
    XQ_REQUIRE(a && b && c && d);

    XGraphicsItem_setFlag(&a->m_base, XGraphicsItemFlag_ItemIsSelectable, true);
    XGraphicsItem_setFlag(&a->m_base, XGraphicsItemFlag_ItemIsMovable, true);
    XGraphicsItem_setFlag(&c->m_base, XGraphicsItemFlag_ItemIsSelectable, true);

    pos.x = 10.0f;
    pos.y = 20.0f;
    XGraphicsItem_setPos(&a->m_base, &pos);
    pos.x = 60.0f;
    pos.y = 10.0f;
    XGraphicsItem_setPos(&b->m_base, &pos);
    pos.x = 20.0f;
    pos.y = 25.0f;
    XGraphicsItem_setPos(&c->m_base, &pos);
    XGraphicsItem_setZValue(&b->m_base, 5.0f);
    XGraphicsItem_setZValue(&c->m_base, 2.0f);

    /* 子项先挂父再整体入场景（Qt addItem 会递归收编子树）。 */
    XGraphicsItem_setParentItem(&d->m_base, &a->m_base);
    pos.x = 30.0f;
    pos.y = 25.0f;
    XGraphicsItem_setPos(&d->m_base, &pos);
    XGraphicsItem_setZValue(&d->m_base, 1.0f);

    XGraphicsScene_addItem(scene, &a->m_base);
    XGraphicsScene_addItem(scene, &b->m_base);
    XGraphicsScene_addItem(scene, &c->m_base);
}

/** @brief 释放对拍装配（场景 clear 兜底 + 项/场景删除 + 泄漏门对账）。
 *  @warning d 是 a 的子项——删 a 即级联删 d，**不得**再显式删 d（UAF）；
 *           成员判定必须在 clear **之前**快照（clear 删掉的项不可再读）。 */
static void xq_freeProbeScene(XGraphicsScene* scene,
                              XGraphicsSceneEventProbe* a,
                              XGraphicsSceneEventProbe* b,
                              XGraphicsSceneEventProbe* c,
                              XGraphicsSceneEventProbe* d)
{
    bool aIn = (a && XGraphicsItem_scene(&a->m_base) != NULL);
    bool bIn = (b && XGraphicsItem_scene(&b->m_base) != NULL);
    bool cIn = (c && XGraphicsItem_scene(&c->m_base) != NULL);
    (void)d; /* 随 a 级联释放。 */
    /* clear 删「仍在场景中的顶层项」（含其子树）；先前 remove/迁走的
     * 顶层项由这里补删。 */
    XGraphicsScene_clear(scene);
    if (a && !aIn) XClassDelete(&a->m_base);
    if (b && !bIn) XClassDelete(&b->m_base);
    if (c && !cIn) XClassDelete(&c->m_base);
    XClassDelete((XClass*)scene);
}

/** @brief 项指针表断言（顺序敏感；name 打印用）。 */
static void xq_checkItems(const char* tag, XVector* list, void** want,
                          int wantCount)
{
    int i;
    int n = list ? (int)XContainerSize(list) : 0;
    bool ok = (n == wantCount);
    for (i = 0; ok && i < n; ++i)
        ok = (*(void**)XVector_at_base(list, (int64_t)i) == want[i]);
    xq_check_impl(ok, tag, __FILE__, __LINE__, "count=%d want=%d", n, wantCount);
    if (list) XClassDelete(list);
}

/* ==================== ① add/remove/clear 所有权与计数 ==================== */

XQ_TEST(XGraphicsScene, addRemoveClearOwnership)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsSceneEventProbe* a;
    XGraphicsSceneEventProbe* b;
    XGraphicsSceneEventProbe* c;
    XVector* list;
    XQ_REQUIRE(scene != NULL);
    XQ_REQUIRE(s_probeAlive >= 0);

    a = xq_probeCreate(10.0f, 10.0f, 0xFFFF0000u);
    b = xq_probeCreate(10.0f, 10.0f, 0xFF00FF00u);
    c = xq_probeCreate(10.0f, 10.0f, 0xFF0000FFu);
    XQ_REQUIRE(a && b && c);

    /* 加入：计数与回指。 */
    XGraphicsScene_addItem(scene, &a->m_base);
    XGraphicsScene_addItem(scene, &b->m_base);
    XGraphicsScene_addItem(scene, &c->m_base);
    XQ_CHECK(XGraphicsItem_scene(&a->m_base) == scene);
    XQ_CHECK(XGraphicsItem_scene(&b->m_base) == scene);
    XQ_CHECK(XGraphicsItem_scene(&c->m_base) == scene);
    list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("items count after add", list, (void*[]){a, b, c}, 3);
    XQ_CHECK(XGraphicsScene_itemsBoundingRect(scene).width > 0.0f);

    /* 重复加入同场景：告警 + 原状（Qt：already been added）。 */
    XGraphicsScene_addItem(scene, &a->m_base);
    list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("re-add keeps single entry", list, (void*[]){a, b, c}, 3);

    /* remove：不删项（所有权回归调用方）、回指清 NULL、集合缩水。 */
    XGraphicsScene_removeItem(scene, &a->m_base);
    XQ_CHECK(XGraphicsItem_scene(&a->m_base) == NULL);
    XQ_CHECK(s_probeAlive == 3); /* remove 不删项。 */
    list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("items after remove", list, (void*[]){b, c}, 2);

    /* remove 场景外项：告警 + 原状（Qt：scene differs）。 */
    XGraphicsScene_removeItem(scene, &a->m_base);
    list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("foreign remove is no-op", list, (void*[]){b, c}, 2);

    /* clear：删仍在场景中的顶层项（b、c），先前 remove 的 a 存活。 */
    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 1); /* 只剩 a。 */
    XQ_CHECK(XGraphicsItem_scene(&a->m_base) == NULL);
    list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("scene empty after clear", list, NULL, 0);
    /* remove 已摘离的项：告警 + 原状（Qt：scene differs 分支）。 */
    XGraphicsScene_removeItem(scene, &a->m_base);
    XQ_CHECK(s_probeAlive == 1);

    XGraphicsScene_removeItem(NULL, NULL); /* 空参安全。 */
    XGraphicsScene_clear(NULL);
    XClassDelete(&a->m_base);
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_probeAlive == 0);
}

/* ==================== ② item 跨 scene 移动 ==================== */

XQ_TEST(XGraphicsScene, crossSceneMove)
{
    XGraphicsScene* s1 = XGraphicsScene_create();
    XGraphicsScene* s2 = XGraphicsScene_create();
    XGraphicsSceneEventProbe* parent;
    XGraphicsSceneEventProbe* child;
    XVector* list;
    XQ_REQUIRE(s1 && s2);

    parent = xq_probeCreate(20.0f, 20.0f, 0xFFFF0000u);
    child = xq_probeCreate(10.0f, 10.0f, 0xFF00FF00u);
    XQ_REQUIRE(parent && child);
    XGraphicsItem_setParentItem(&child->m_base, &parent->m_base);
    XGraphicsScene_addItem(s1, &parent->m_base); /* 子树整棵入场景。 */
    XQ_CHECK(XGraphicsItem_scene(&child->m_base) == s1);

    /* 跨场景：旧场景先摘（含子树），子树随迁、父子结构不变。
     * items() 是树遍历口径（父先子后——探针 P14 同证：count 含子项 D）。 */
    XGraphicsScene_addItem(s2, &parent->m_base);
    XQ_CHECK(XGraphicsItem_scene(&parent->m_base) == s2);
    XQ_CHECK(XGraphicsItem_scene(&child->m_base) == s2);
    list = XGraphicsScene_items(s1, XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("s1 empty after move", list, NULL, 0);
    list = XGraphicsScene_items(s2, XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("s2 has moved subtree", list,
                  (void*[]){parent, child}, 2);

    XGraphicsScene_clear(s2);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)s1);
    XClassDelete((XClass*)s2);
}

/* ==================== ③ itemAt 按 z 降序命中（含重叠项/子项） ========== */

XQ_TEST(XGraphicsScene, itemAtZOrder)
{
    XGraphicsScene* scene;
    XGraphicsSceneEventProbe* a;
    XGraphicsSceneEventProbe* b;
    XGraphicsSceneEventProbe* c;
    XGraphicsSceneEventProbe* d;
    XGraphicsSceneEventProbe* hidden;
    XPointF p;
    xq_buildProbeScene(&scene, &a, &b, &c, &d);

    /* P4：itemAt(25,30)——A 与 C 重叠，C(z2) 在上。 */
    p.x = 25.0f;
    p.y = 30.0f;
    XQ_CHECK(XGraphicsScene_itemAt(scene, &p) == &c->m_base);
    /* P5：itemAt(45,50)——落在 A 的子项 D 上（子项遮父）。 */
    p.x = 45.0f;
    p.y = 50.0f;
    XQ_CHECK(XGraphicsScene_itemAt(scene, &p) == &d->m_base);
    /* P6：空点。 */
    p.x = 200.0f;
    p.y = 200.0f;
    XQ_CHECK(XGraphicsScene_itemAt(scene, &p) == NULL);
    /* P9：隐藏项不命中。 */
    hidden = xq_probeCreate(10.0f, 10.0f, 0xFF123456u);
    XQ_REQUIRE(hidden != NULL);
    XGraphicsScene_addItem(scene, &hidden->m_base);
    XGraphicsItem_setVisible(&hidden->m_base, false);
    p.x = 5.0f;
    p.y = 5.0f;
    XQ_CHECK(XGraphicsScene_itemAt(scene, &p) == NULL);
    /* 复现隐藏：恢复可见后命中恢复（可见位语义）。 */
    XGraphicsItem_setVisible(&hidden->m_base, true);
    XQ_CHECK(XGraphicsScene_itemAt(scene, &p) == &hidden->m_base);

    xq_freeProbeScene(scene, a, b, c, d);
}

/* ==================== ④ items(rect) 相交集合与顺序 ==================== */

XQ_TEST(XGraphicsScene, itemsRectSetAndOrder)
{
    XGraphicsScene* scene;
    XGraphicsSceneEventProbe* a;
    XGraphicsSceneEventProbe* b;
    XGraphicsSceneEventProbe* c;
    XGraphicsSceneEventProbe* d;
    XGraphicsSceneEventProbe* hidden;
    XRectF rect;
    XVector* list;
    xq_buildProbeScene(&scene, &a, &b, &c, &d);

    /* P2：大矩形升序 = [A, D, C, B]（父先子后、顶层按 z）。 */
    rect.x = 0.0f;
    rect.y = 0.0f;
    rect.width = 100.0f;
    rect.height = 100.0f;
    list = XGraphicsScene_itemsRect(scene, &rect,
                                    XGraphicsItemSelectionMode_IntersectsItemShape,
                                    XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("P2 itemsRect asc", list,
                  (void*[]){&a->m_base, &d->m_base, &c->m_base, &b->m_base}, 4);
    /* P3：降序 = 整表反转 [B, C, D, A]。 */
    list = XGraphicsScene_itemsRect(scene, &rect,
                                    XGraphicsItemSelectionMode_IntersectsItemShape,
                                    XGraphicsSortOrder_DescendingOrder);
    xq_checkItems("P3 itemsRect desc", list,
                  (void*[]){&b->m_base, &c->m_base, &d->m_base, &a->m_base}, 4);
    /* P7：小矩形降序 = [C, A]（C 在上层）。 */
    rect.x = 20.0f;
    rect.y = 25.0f;
    rect.width = 16.0f;
    rect.height = 12.0f;
    list = XGraphicsScene_itemsRect(scene, &rect,
                                    XGraphicsItemSelectionMode_IntersectsItemShape,
                                    XGraphicsSortOrder_DescendingOrder);
    xq_checkItems("P7 small rect desc", list,
                  (void*[]){&c->m_base, &a->m_base}, 2);
    /* P8：小矩形升序 = [A, C]。 */
    list = XGraphicsScene_itemsRect(scene, &rect,
                                    XGraphicsItemSelectionMode_IntersectsItemShape,
                                    XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("P8 small rect asc", list,
                  (void*[]){&a->m_base, &c->m_base}, 2);
    /* P10/P14 对照：几何重载剔除隐藏项，items() 纯索引口径含隐藏项。 */
    hidden = xq_probeCreate(10.0f, 10.0f, 0xFF123456u);
    XQ_REQUIRE(hidden != NULL);
    XGraphicsScene_addItem(scene, &hidden->m_base);
    XGraphicsItem_setVisible(&hidden->m_base, false);
    rect.x = 0.0f;
    rect.y = 0.0f;
    rect.width = 10.0f;
    rect.height = 10.0f;
    list = XGraphicsScene_itemsRect(scene, &rect,
                                    XGraphicsItemSelectionMode_IntersectsItemShape,
                                    XGraphicsSortOrder_AscendingOrder);
    xq_checkItems("P10 hidden excluded from itemsRect", list, NULL, 0);
    list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
    /* 树遍历含子项：[A, D, hidden, C, B]（D 是 A 的子项、探针 P14 的
     * count=4 同证「items() 含子项链」）。 */
    xq_checkItems("P14 items() keeps hidden", list,
                  (void*[]){&a->m_base, &d->m_base, &hidden->m_base, &c->m_base,
                            &b->m_base},
                  5);
    /* 命中模式：ContainsItemBoundingRect（查询矩形含包围盒）。 */
    rect.x = 58.0f;
    rect.y = 8.0f;
    rect.width = 28.0f;
    rect.height = 22.0f; /* 恰含 B 的包围盒 (60,10,24x18)。 */
    list = XGraphicsScene_itemsRect(scene, &rect,
                                    XGraphicsItemSelectionMode_ContainsItemBoundingRect,
                                    XGraphicsSortOrder_AscendingOrder);
    /* hidden 在 (0,0) 不被包含；B 被包含。 */
    xq_checkItems("contains bounding rect", list, (void*[]){&b->m_base}, 1);

    xq_freeProbeScene(scene, a, b, c, d);
}

/* ==================== ⑤ 选中：仅 Selectable、批量信号一次、清选 ========== */

static int s_selSignals = 0; /**< selectionChanged 计数（每用例清零）。 */

static void xq_onSelChanged(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++s_selSignals;
}

XQ_TEST(XGraphicsScene, selectionBatchSignal)
{
    XGraphicsScene* scene;
    XGraphicsSceneEventProbe* a;
    XGraphicsSceneEventProbe* b;
    XGraphicsSceneEventProbe* c;
    XGraphicsSceneEventProbe* d;
    XPainterPath path;
    XVector* sel;
    s_selSignals = 0;
    xq_buildProbeScene(&scene, &a, &b, &c, &d);
    XObject_connect((XObject*)scene,
                    (size_t)XGraphicsScene_selectionChanged_signal(NULL),
                    (XObject*)scene, xq_onSelChanged, XConnectionType_Direct);

    /* P11：路径覆盖 A、C（B 不可选不响应），批量发一次信号。 */
    XPainterPath_init(&path);
    XPainterPath_moveTo(&path, 15.0f, 15.0f);
    XPainterPath_lineTo(&path, 70.0f, 15.0f);
    XPainterPath_lineTo(&path, 70.0f, 45.0f);
    XPainterPath_lineTo(&path, 15.0f, 45.0f);
    XPainterPath_closeSubpath(&path);
    XGraphicsScene_setSelectionAreaPath(scene, &path);
    XPainterPath_deinit(&path);
    sel = XGraphicsScene_selectedItems(scene);
    xq_checkItems("P11 selected = [A, C]", sel,
                  (void*[]){&a->m_base, &c->m_base}, 2);
    XQ_EQ_I64(s_selSignals, 1);
    XQ_CHECK(XGraphicsItem_isSelected(&a->m_base));
    XQ_CHECK(XGraphicsItem_isSelected(&c->m_base));
    XQ_CHECK(!XGraphicsItem_isSelected(&b->m_base));

    /* P12：clearSelection 再发一次、集合清空。 */
    XGraphicsScene_clearSelection(scene);
    XQ_EQ_I64(s_selSignals, 2);
    sel = XGraphicsScene_selectedItems(scene);
    xq_checkItems("P12 cleared", sel, NULL, 0);

    /* 闸外单项 setSelected 即时发信号；不可选/隐藏项不触发。 */
    XGraphicsItem_setSelected(&a->m_base, true);
    XQ_EQ_I64(s_selSignals, 3);
    XGraphicsItem_setSelected(&b->m_base, true); /* 非 Selectable：无信号。 */
    XQ_EQ_I64(s_selSignals, 3);
    XQ_CHECK(!XGraphicsItem_isSelected(&b->m_base));
    XGraphicsItem_setVisible(&a->m_base, false); /* 隐藏落选 + 信号。 */
    XQ_EQ_I64(s_selSignals, 4);
    XQ_CHECK(!XGraphicsItem_isSelected(&a->m_base));
    XGraphicsItem_setVisible(&a->m_base, true);

    /* setSelectionAreaRect 等价路径形态（矩形→路径）。 */
    {
        XRectF r;
        r.x = 15.0f;
        r.y = 15.0f;
        r.width = 55.0f;
        r.height = 30.0f;
        XGraphicsScene_setSelectionAreaRect(scene, &r);
        XQ_EQ_I64(s_selSignals, 5);
        sel = XGraphicsScene_selectedItems(scene);
        xq_checkItems("rect selection = [A, C]", sel,
                      (void*[]){&a->m_base, &c->m_base}, 2);
    }
    xq_freeProbeScene(scene, a, b, c, d);
}

/** @brief 【复核修订】addItem 选中记账覆盖整棵子树：加入的顶层未选中、
 *  但子树内含已选中子项 ⇒ selectionChanged 照发一次（Qt
 *  qgraphicsscene.cpp:2454 记 oldSize、子项各自 :2501 `<< item`、
 *  :2526-2527 收尾比较 size）。 */
XQ_TEST(XGraphicsScene, addItemSubtreeSelectionSignal)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsSceneEventProbe* parent;
    XGraphicsSceneEventProbe* child;
    XVector* sel;
    s_selSignals = 0;
    XQ_REQUIRE(scene != NULL);
    parent = xq_probeCreate(20.0f, 20.0f, 0xFF445566u);
    child = xq_probeCreate(10.0f, 10.0f, 0xFF665544u);
    XQ_REQUIRE(parent && child);
    XGraphicsItem_setFlag(&child->m_base, XGraphicsItemFlag_ItemIsSelectable,
                          true);
    XGraphicsItem_setParentItem(&child->m_base, &parent->m_base);
    /* 子项在**入场景前**置选中（setSelected 只看 flags+visible，Qt 同）。 */
    XGraphicsItem_setSelected(&child->m_base, true);
    XQ_CHECK(XGraphicsItem_isSelected(&child->m_base));
    XObject_connect((XObject*)scene,
                    (size_t)XGraphicsScene_selectionChanged_signal(NULL),
                    (XObject*)scene, xq_onSelChanged, XConnectionType_Direct);
    XGraphicsScene_addItem(scene, &parent->m_base); /* 顶层未选中、子项选中。 */
    XQ_EQ_I64(s_selSignals, 1); /* 子树记账：照发一次。 */
    sel = XGraphicsScene_selectedItems(scene);
    xq_checkItems("subtree selected child counted", sel,
                  (void*[]){&child->m_base}, 1);
    /* 对照：整棵子树都未选中 ⇒ 不发。 */
    XGraphicsItem_setSelected(&child->m_base, false);
    XQ_EQ_I64(s_selSignals, 2); /* 上一行 setSelected(false) 即时发。 */
    {
        XGraphicsSceneEventProbe* p2 = xq_probeCreate(8.0f, 8.0f, 0xFF121212u);
        XQ_REQUIRE(p2 != NULL);
        XGraphicsItem_setParentItem(&p2->m_base, &parent->m_base);
        s_selSignals = 0;
        XGraphicsScene_addItem(scene, &p2->m_base); /* 无选中节点。 */
        XQ_EQ_I64(s_selSignals, 0);
        XGraphicsScene_clear(scene); /* p2 随 parent 级联删。 */
    }
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)scene);
}

/** @brief 【复核修订】场景**析构**删除全部在册项（Qt ~QGraphicsScene
 *  qgraphicsscene.cpp:1661 调 clear()、:1648-1650 文档原文）——直接删
 *  场景不留漏删项；先前 remove 的项不删（所有权已归调用方）。 */
XQ_TEST(XGraphicsScene, destructorDeletesRegisteredItems)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsSceneEventProbe* kept;
    XGraphicsSceneEventProbe* gone;
    XQ_REQUIRE(scene != NULL);
    kept = xq_probeCreate(10.0f, 10.0f, 0xFF112233u);
    gone = xq_probeCreate(10.0f, 10.0f, 0xFF332211u);
    XQ_REQUIRE(kept && gone);
    XGraphicsScene_addItem(scene, &kept->m_base);
    XGraphicsScene_addItem(scene, &gone->m_base);
    XGraphicsScene_removeItem(scene, &kept->m_base); /* 所有权回归调用方。 */
    XQ_CHECK(s_probeAlive == 2);
    XClassDelete((XClass*)scene); /* 析构：gone（含子树）被删、kept 不动。 */
    XQ_CHECK(s_probeAlive == 1);
    XClassDelete(&kept->m_base);
    XQ_CHECK(s_probeAlive == 0);
}

/* ==================== ⑥ 焦点：仅 Focusable、切换 ==================== */

XQ_TEST(XGraphicsScene, focusFocusableOnly)
{
    XGraphicsScene* scene;
    XGraphicsSceneEventProbe* a;
    XGraphicsSceneEventProbe* b;
    XGraphicsSceneEventProbe* c;
    XGraphicsSceneEventProbe* d;
    xq_buildProbeScene(&scene, &a, &b, &c, &d);
    XGraphicsItem_setFlag(&a->m_base, XGraphicsItemFlag_ItemIsFocusable, true);
    XGraphicsItem_setFlag(&c->m_base, XGraphicsItemFlag_ItemIsFocusable, true);

    /* 不可聚焦项：setFocusItem 只清焦不授焦（Qt setFocusItemHelper）。 */
    XGraphicsScene_setFocusItem(scene, &b->m_base);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == NULL);
    XQ_EQ_I64(b->m_focusIn, 0);

    /* 可聚焦项获焦：FocusIn 派发到项。 */
    XGraphicsScene_setFocusItem(scene, &a->m_base);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == &a->m_base);
    XQ_EQ_I64(a->m_focusIn, 1);
    XQ_EQ_I64(a->m_focusOut, 0);

    /* 切换焦点：旧焦 FocusOut、新焦 FocusIn。 */
    XGraphicsScene_setFocusItem(scene, &c->m_base);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == &c->m_base);
    XQ_EQ_I64(a->m_focusOut, 1);
    XQ_EQ_I64(c->m_focusIn, 1);

    /* 隐藏的可聚焦项不授焦（只清焦）。 */
    XGraphicsItem_setVisible(&a->m_base, false);
    XGraphicsScene_setFocusItem(scene, &a->m_base);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == NULL);
    XQ_EQ_I64(a->m_focusIn, 1); /* 未再 +1。 */
    XGraphicsItem_setVisible(&a->m_base, true);

    /* 【复核修订】隐藏**已聚焦**项 ⇒ 自动撤焦（Qt setVisibleHelper 隐藏支
     * qgraphicsitem.cpp:2420-2434 hasFocus 支：hasFocus && scene ⇒
     * clearFocusHelper）。c 本轮 focusOut 计数：前一清焦段 1 次 + 本段 1 次。 */
    XGraphicsScene_setFocusItem(scene, &c->m_base);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == &c->m_base);
    XGraphicsItem_setVisible(&c->m_base, false);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == NULL);
    XQ_EQ_I64(c->m_focusOut, 2);
    XGraphicsItem_setVisible(&c->m_base, true);

    /* clearFocus：FocusOut + 清位（c 本轮 focusOut 计数：前一清焦段 1 次 +
     * 隐藏撤焦段 1 次 + 此处显式 clearFocus 1 次 = 3）。 */
    XGraphicsScene_setFocusItem(scene, &c->m_base);
    XGraphicsScene_clearFocus(scene);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == NULL);
    XQ_EQ_I64(c->m_focusOut, 3);

    xq_freeProbeScene(scene, a, b, c, d);
}

/* ==================== ⑦ 鼠标 press/move/release 派发 + 键父链上溯 ====== */

XQ_TEST(XGraphicsScene, mouseDispatchAndKeyPropagation)
{
    XGraphicsScene* scene;
    XGraphicsSceneEventProbe* a;
    XGraphicsSceneEventProbe* b;
    XGraphicsSceneEventProbe* c;
    XGraphicsSceneEventProbe* d;
    XGraphicsSceneEventProbe* parent;
    XGraphicsSceneEventProbe* child;
    XMouseEvent me;
    XKeyEvent ke;
    XPoint pos;
    xq_buildProbeScene(&scene, &a, &b, &c, &d);
    /* C 拒绝按下 → 抓取落到下一候选 A（Qt：拒绝撤抓取、尝试下层候选）。 */
    c->m_pressAccept = false;
    /* 键上溯装配：child（可聚焦、忽略键）← parent（可聚焦、接受键）。 */
    parent = xq_probeCreate(30.0f, 30.0f, 0xFF224466u);
    child = xq_probeCreate(10.0f, 10.0f, 0xFF664422u);
    XQ_REQUIRE(parent && child);
    XGraphicsItem_setFlag(&parent->m_base, XGraphicsItemFlag_ItemIsFocusable, true);
    XGraphicsItem_setFlag(&child->m_base, XGraphicsItemFlag_ItemIsFocusable, true);
    parent->m_keyAccept = true; /* Qt 缺省 ignore；探针显式接受以判上溯。 */
    XGraphicsItem_setParentItem(&child->m_base, &parent->m_base);
    {
        XPointF pp;
        pp.x = 100.0f;
        pp.y = 100.0f;
        XGraphicsItem_setPos(&parent->m_base, &pp);
    }
    XGraphicsScene_addItem(scene, &parent->m_base);

    /* press(25,30)：候选降序 [C, A]；C 拒 → A 接受并抓取。 */
    pos.x = 25;
    pos.y = 30;
    XMouseEvent_init(&me, XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_PRESS,
                     (XMouseButton)XMouseButton_LeftButton,
                     (XKeyboardModifiers)XKeyboardModifier_NoModifier, pos);
    XGraphicsScene_mousePressEvent(scene, &me);
    XQ_CHECK(XGraphicsScene_mouseGrabberItem(scene) == &a->m_base);
    XQ_EQ_I64(c->m_press, 1);
    XQ_EQ_I64(a->m_press, 1);
    XQ_EQ_I64(b->m_press, 0);
    XQ_CHECK(XEvent_isAccepted((XEvent*)&me));

    /* move(30,35)：直送抓取项 A（Qt：无抓取时有键按下静默）。 */
    pos.x = 30;
    pos.y = 35;
    XMouseEvent_init(&me, XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_MOVE,
                     (XMouseButton)XMouseButton_NoButton,
                     (XKeyboardModifiers)XKeyboardModifier_NoModifier, pos);
    me.m_buttons = (XMouseButton)XMouseButton_LeftButton;
    XGraphicsScene_mouseMoveEvent(scene, &me);
    XQ_EQ_I64(a->m_move, 1);
    XQ_EQ_I64(c->m_move, 0);

    /* release(30,35)：直送 A；全键松开撤抓取。Qt 口径：release 的
     * buttons() 是**仍按住**的键集（松开的这颗不算）⇒ 最后一颗松开时
     * buttons()==0——由调用方（视图层/测试）按此约定填 m_buttons。 */
    pos.x = 30;
    pos.y = 35;
    XMouseEvent_init(&me, XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_RELEASE,
                     (XMouseButton)XMouseButton_LeftButton,
                     (XKeyboardModifiers)XKeyboardModifier_NoModifier, pos);
    me.m_buttons = (XMouseButton)XMouseButton_NoButton;
    XGraphicsScene_mouseReleaseEvent(scene, &me);
    XQ_EQ_I64(a->m_release, 1);
    XQ_CHECK(XGraphicsScene_mouseGrabberItem(scene) == NULL);

    /* 【复核修订】再抓取 A 后把它隐藏 ⇒ 抓取自动被撤（Qt setVisibleHelper
     * 隐藏支 qgraphicsitem.cpp:2415-2417 ungrabMouse），后续 move 不再
     * 直送隐藏项（此前版本：抓取项隐藏后 move/release 照送——缺口）。 */
    pos.x = 25;
    pos.y = 30;
    XMouseEvent_init(&me, XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_PRESS,
                     (XMouseButton)XMouseButton_LeftButton,
                     (XKeyboardModifiers)XKeyboardModifier_NoModifier, pos);
    XGraphicsScene_mousePressEvent(scene, &me);
    XQ_CHECK(XGraphicsScene_mouseGrabberItem(scene) == &a->m_base);
    XGraphicsItem_setVisible(&a->m_base, false);
    XQ_CHECK(XGraphicsScene_mouseGrabberItem(scene) == NULL);
    pos.x = 30;
    pos.y = 35;
    XMouseEvent_init(&me, XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_MOVE,
                     (XMouseButton)XMouseButton_NoButton,
                     (XKeyboardModifiers)XKeyboardModifier_NoModifier, pos);
    me.m_buttons = (XMouseButton)XMouseButton_LeftButton;
    XGraphicsScene_mouseMoveEvent(scene, &me);
    XQ_EQ_I64(a->m_move, 1); /* 隐藏后不再 +1（此前 release 段已 1 次）。 */
    XGraphicsItem_setVisible(&a->m_base, true);

    /* 键传播：焦点在 child，child 忽略 → 父链上溯到 parent 接受
     * （Qt keyPressEvent 的 do-while：accept 置位→派发→!accepted 且有父）。 */
    XGraphicsScene_setFocusItem(scene, &child->m_base);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == &child->m_base);
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, 'A',
                   (XKeyboardModifiers)XKeyboardModifier_NoModifier);
    XGraphicsScene_keyPressEvent(scene, &ke);
    XQ_EQ_I64(child->m_keyPress, 1);
    XQ_EQ_I64(parent->m_keyPress, 1);
    XQ_CHECK(XEvent_isAccepted((XEvent*)&ke));

    /* 焦点项接受键则不上溯：焦点给 parent，parent 一次接受即止。 */
    XGraphicsScene_setFocusItem(scene, &parent->m_base);
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, 'B',
                   (XKeyboardModifiers)XKeyboardModifier_NoModifier);
    XGraphicsScene_keyPressEvent(scene, &ke);
    XQ_EQ_I64(parent->m_keyPress, 2);
    XQ_CHECK(XEvent_isAccepted((XEvent*)&ke));

    /* 空点按下：无候选 ⇒ 清焦（非粘性）+ 清选中（Qt 尾段）。 */
    XGraphicsItem_setSelected(&a->m_base, true);
    XGraphicsScene_setFocusItem(scene, &parent->m_base);
    XGraphicsScene_setStickyFocus(scene, false);
    pos.x = 250;
    pos.y = 250;
    XMouseEvent_init(&me, XEVENT_TYPE_GRAPHICS_SCENE_MOUSE_PRESS,
                     (XMouseButton)XMouseButton_LeftButton,
                     (XKeyboardModifiers)XKeyboardModifier_NoModifier, pos);
    XGraphicsScene_mousePressEvent(scene, &me);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == NULL);
    XQ_CHECK(!XGraphicsItem_isSelected(&a->m_base));
    /* 粘性焦点：同点再按不清焦。 */
    XGraphicsScene_setFocusItem(scene, &parent->m_base);
    XGraphicsScene_setStickyFocus(scene, true);
    XGraphicsScene_mousePressEvent(scene, &me);
    XQ_CHECK(XGraphicsScene_focusItem(scene) == &parent->m_base);
    XGraphicsScene_setStickyFocus(scene, false);

    xq_freeProbeScene(scene, a, b, c, d);
}

/* ==================== ⑧ 悬停 Enter/Leave 切换 ==================== */

XQ_TEST(XGraphicsScene, hoverEnterLeaveSwitch)
{
    XGraphicsScene* scene;
    XGraphicsSceneEventProbe* a;
    XGraphicsSceneEventProbe* b;
    XGraphicsSceneEventProbe* c;
    XGraphicsSceneEventProbe* d;
    XGraphicsSceneEventProbe* noHover;
    XPointF p;
    xq_buildProbeScene(&scene, &a, &b, &c, &d);
    XGraphicsItem_setAcceptHoverEvents(&a->m_base, true);
    XGraphicsItem_setAcceptHoverEvents(&c->m_base, true);
    /* 不接受悬停的项：命中但不收悬停（Qt itemAcceptsHoverEvents_helper）。 */
    noHover = xq_probeCreate(10.0f, 10.0f, 0xFF112233u);
    XQ_REQUIRE(noHover != NULL);
    XGraphicsScene_addItem(scene, &noHover->m_base);
    {
        XPointF np;
        np.x = 90.0f;
        np.y = 90.0f;
        XGraphicsItem_setPos(&noHover->m_base, &np);
    }

    /* 悬停进 C（(25,30) 顶个是 C）：C Enter + Move。 */
    p.x = 25.0f;
    p.y = 30.0f;
    XQ_CHECK(XGraphicsScene_hoverMoveEvent(scene, &p));
    XQ_EQ_I64(c->m_enter, 1);
    XQ_EQ_I64(c->m_hoverMove, 1);
    XQ_EQ_I64(a->m_enter, 0);

    /* 跨项：移到 A 独占区（(15,25)）⇒ C Leave、A Enter。 */
    p.x = 15.0f;
    p.y = 25.0f;
    XQ_CHECK(XGraphicsScene_hoverMoveEvent(scene, &p));
    XQ_EQ_I64(c->m_leave, 1);
    XQ_EQ_I64(a->m_enter, 1);
    XQ_EQ_I64(a->m_hoverMove, 1);

    /* 移到不接受悬停的项上 ⇒ 旧项 Leave、无新目标（返回 false）。 */
    p.x = 95.0f;
    p.y = 95.0f;
    XQ_CHECK(!XGraphicsScene_hoverMoveEvent(scene, &p));
    XQ_EQ_I64(a->m_leave, 1);
    XQ_EQ_I64(noHover->m_enter, 0);

    /* hoverLeaveEvent：指针离场 ⇒ 跟踪清空（无旧项时无事件）。 */
    XGraphicsScene_hoverLeaveEvent(scene);
    XQ_EQ_I64(noHover->m_leave, 0);

    xq_freeProbeScene(scene, a, b, c, d);
}

/* ==================== ⑨ render 出图：遮盖 + opacity 混色 ==================== */

/** @brief 通道断言（±2 容差；0xAARRGGBB 布局）。 */
static void xq_checkPixel(const char* tag, const XImage* img, int x, int y,
                          int r, int g, int b)
{
    uint32_t px = XImage_pixel(img, x, y);
    int pr = (int)((px >> 16) & 0xFF);
    int pg = (int)((px >> 8) & 0xFF);
    int pb = (int)(px & 0xFF);
    bool ok = (pr >= r - 2 && pr <= r + 2) && (pg >= g - 2 && pg <= g + 2) &&
              (pb >= b - 2 && pb <= b + 2);
    xq_check_impl(ok, tag, __FILE__, __LINE__,
                  "pixel(%d,%d)=0x%08X got=(%d,%d,%d) want=(%d,%d,%d)", x, y,
                  px, pr, pg, pb, r, g, b);
}

XQ_TEST(XGraphicsScene, renderPixelBlend)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsSceneEventProbe* red;
    XGraphicsSceneEventProbe* blue;
    XImage image;
    XPainter painter;
    XRectF target;
    bool began;
    XQ_REQUIRE(scene != NULL);
    red = xq_probeCreate(32.0f, 32.0f, 0xFFFF0000u);
    blue = xq_probeCreate(32.0f, 32.0f, 0xFF0000FFu);
    XQ_REQUIRE(red && blue);
    XGraphicsScene_addItem(scene, &red->m_base);
    XGraphicsScene_addItem(scene, &blue->m_base);
    XGraphicsItem_setOpacity(&blue->m_base, 0.5f);
    {
        XPointF bp;
        bp.x = 16.0f;
        bp.y = 16.0f;
        XGraphicsItem_setPos(&blue->m_base, &bp); /* (16,16)-(48,48) 覆盖红。 */
    }

    XImage_init_ex(&image, 64, 64, XImageFormat_ARGB32);
    XQ_REQUIRE(!XImage_isNull(&image));
    XImage_fill(&image, 0u);
    XPainter_init(&painter, NULL);
    began = XPainter_begin_image(&painter, &image);
    XQ_REQUIRE(began);
    target.x = 0.0f;
    target.y = 0.0f;
    target.width = 64.0f;
    target.height = 64.0f;
    XGraphicsScene_render(scene, &painter, &target);
    /* 【复核修订】API 契约（painter 活跃期内断言）：① painter 状态不被
     * 污染（入口 save/出口 restore——Qt render :1784/:1826）：render 后
     * 世界变换回到入口值（begin_image 后为单位阵），再画一笔仍按入口
     * 坐标落墨；② target 外不落像素（clip target，Qt :1786）：渲染子
     * 区域 (8,8,20,20)，区域外的红块（场景 (0,0)-(32,32)）不落墨，
     * 区域内部分照画（同位 1:1——target 兼任场景源区域与设备目标区域）。 */
    {
        XImageTransform wt;
        XRect probe;
        XRectF sub;
        XPainter_worldTransform(&painter, &wt);
        XQ_CHECK(wt.m11 == 1.0f && wt.m22 == 1.0f && wt.m12 == 0.0f &&
                 wt.m21 == 0.0f && wt.dx == 0.0f && wt.dy == 0.0f);
        XImage_fill(&image, 0u);
        sub.x = 8.0f;
        sub.y = 8.0f;
        sub.width = 20.0f;
        sub.height = 20.0f;
        XGraphicsScene_render(scene, &painter, &sub);
        XPainter_worldTransform(&painter, &wt); /* 出口 restore：仍单位阵。 */
        XQ_CHECK(wt.m11 == 1.0f && wt.dx == 0.0f && wt.dy == 0.0f);
        /* 红块场景 (0,0)-(32,32) ∩ 子区域 (8,8)-(28,28)：设备 (8,8)-(28,28)
         * 应为红；区域外（如 (4,4)、(60,4)）不落墨。 */
        xq_checkPixel("sub-clip inside", &image, 12, 12, 255, 0, 0);
        xq_checkPixel("sub-clip outside left", &image, 4, 4, 0, 0, 0);
        xq_checkPixel("sub-clip outside far", &image, 60, 4, 0, 0, 0);
        XImage_fill(&image, 0u);
        probe.x = 2;
        probe.y = 2;
        probe.width = 4;
        probe.height = 4;
        XPainter_fillRect(&painter, &probe, 0xFF00FF00u); /* 入口态可继续画。 */
        xq_checkPixel("painter reusable after render", &image, 3, 3, 0, 255, 0);
        /* 重画整场景供后续像素断言（上一段 fill 清了底图）。 */
        XImage_fill(&image, 0u);
        XGraphicsScene_render(scene, &painter, &target);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);

    /* 纯红区：不重叠处只被红覆盖。 */
    xq_checkPixel("red zone", &image, 8, 8, 255, 0, 0);
    /* 蓝区（不重叠）：0.5 蓝 over 透明底。XImage ARGB32 是**直通 alpha**
     * 口径：源色不变、alpha 减半（像素实测 0x800000FF），B 通道仍 255。 */
    xq_checkPixel("blue zone half opacity", &image, 44, 44, 0, 0, 255);
    /* 重叠区：0.5 蓝 over 红 ⇒ (127, 0, 127)——上层遮盖且按有效不透明度
     * 混色（Qt drawItemHelper 的 painter->setOpacity(effectiveOpacity)）。 */
    xq_checkPixel("overlap blend", &image, 20, 20, 127, 0, 127);
    /* 空区：透明。 */
    xq_checkPixel("empty zone", &image, 60, 4, 0, 0, 0);

    /* 隐藏项不画（recursive_items_helper 的 !visible 整枝剔除）。 */
    XGraphicsItem_setVisible(&blue->m_base, false);
    XImage_fill(&image, 0u);
    XPainter_init(&painter, NULL);
    began = XPainter_begin_image(&painter, &image);
    XQ_REQUIRE(began);
    XGraphicsScene_render(scene, &painter, &target);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    xq_checkPixel("hidden not rendered", &image, 44, 44, 0, 0, 0);
    xq_checkPixel("red still there", &image, 8, 8, 255, 0, 0);

    XClassDeinit((XClass*)&image);
    XGraphicsScene_clear(scene); /* 删两探针。 */
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)scene);
}

/* ==================== ⑩ itemChange 链：Scene 前后置/否决/改道/通知闸 ==== */

XQ_TEST(XGraphicsScene, itemChangeChainAndGates)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsScene* scene2 = XGraphicsScene_create();
    XGraphicsSceneChangeProbe* p;
    XGraphicsSceneChangeProbe* veto;
    XGraphicsSceneChangeProbe* redirect;
    XPointF pos;
    XQ_REQUIRE(scene && scene2);
    XQ_REQUIRE(s_changeAlive >= 0);

    /* 正常加入：SceneChange（前置，值=scene）+ SceneHasChanged（后置）。 */
    p = xq_changeProbeCreate(10.0f, 10.0f);
    XQ_REQUIRE(p != NULL);
    XGraphicsScene_addItem(scene, &p->m_base);
    XQ_EQ_I64(p->m_sceneChange, 1);
    XQ_EQ_I64(p->m_sceneHasChanged, 1);
    XQ_CHECK(XGraphicsItem_scene(&p->m_base) == scene);

    /* 摘离：SceneChange（值=NULL）+ SceneHasChanged（值=NULL）。 */
    XGraphicsScene_removeItem(scene, &p->m_base);
    XQ_EQ_I64(p->m_sceneChange, 2);
    XQ_EQ_I64(p->m_sceneHasChanged, 2);
    XQ_CHECK(XGraphicsItem_scene(&p->m_base) == NULL);

    /* 否决（返回 NULL）：不入场景、不派后置（Qt addItem targetScene
     * 分支 return）。此刻场景内无项（p 已在上一步摘离）⇒ 全空。 */
    veto = xq_changeProbeCreate(10.0f, 10.0f);
    XQ_REQUIRE(veto != NULL);
    veto->m_vetoSceneChange = 1;
    XGraphicsScene_addItem(scene, &veto->m_base);
    XQ_CHECK(XGraphicsItem_scene(&veto->m_base) == NULL);
    XQ_EQ_I64(veto->m_sceneChange, 1);
    XQ_EQ_I64(veto->m_sceneHasChanged, 0);
    {
        XVector* list = XGraphicsScene_items(scene, XGraphicsSortOrder_AscendingOrder);
        xq_checkItems("veto keeps scene unchanged", list, NULL, 0);
    }

    /* 改道（返回 scene2）：落在 scene2（Qt：targetScene->addItem）。 */
    redirect = xq_changeProbeCreate(10.0f, 10.0f);
    XQ_REQUIRE(redirect != NULL);
    redirect->m_redirectTo = scene2;
    XGraphicsScene_addItem(scene, &redirect->m_base);
    XQ_CHECK(XGraphicsItem_scene(&redirect->m_base) == scene2);
    XQ_EQ_I64(redirect->m_sceneHasChanged, 1);
    {
        XVector* list = XGraphicsScene_items(scene2, XGraphicsSortOrder_AscendingOrder);
        xq_checkItems("redirected into scene2", list,
                      (void*[]){&redirect->m_base}, 1);
    }

    /* 通知闸（Qt setPos 3854：仅 ItemSendsGeometryChanges/
     * ItemSendsScenePositionChanges 派发 ItemPositionHasChanged）。 */
    XGraphicsScene_addItem(scene, &p->m_base);
    pos.x = 5.0f;
    pos.y = 5.0f;
    XGraphicsItem_setPos(&p->m_base, &pos);
    XQ_EQ_I64(p->m_positionHasChanged, 0); /* 无闸位：静默。 */
    XGraphicsItem_setFlag(&p->m_base, XGraphicsItemFlag_ItemSendsGeometryChanges,
                          true);
    pos.x = 8.0f;
    pos.y = 8.0f;
    XGraphicsItem_setPos(&p->m_base, &pos);
    XQ_EQ_I64(p->m_positionHasChanged, 1); /* 闸位开：派发。 */

    /* 选中/可见通知不设闸（Qt setSelected/setVisibleHelper 无条件派发）。 */
    XGraphicsItem_setFlag(&p->m_base, XGraphicsItemFlag_ItemIsSelectable, true);
    XGraphicsItem_setSelected(&p->m_base, true);
    XQ_EQ_I64(p->m_selectedHasChanged, 1);
    XGraphicsItem_setVisible(&p->m_base, false);
    XQ_EQ_I64(p->m_visibleHasChanged, 1);
    XGraphicsItem_setVisible(&p->m_base, true);
    XQ_EQ_I64(p->m_visibleHasChanged, 2);

    /* 父项通知**无条件派发**（【复核修订 2026-11-16】勘误：Qt setParentItem
     * :1798 前置通知无闸、setParentItemHelper :1375 `if (newParentVariant)`
     * 恒真 ⇒ ParentHasChanged 不受 ItemSendsGeometryChanges 闸控；
     * 对照组：setPos 仍受闸——veto 无闸位时 Position 通知静默）。 */
    pos.x = 1.0f;
    pos.y = 1.0f;
    XGraphicsItem_setPos(&veto->m_base, &pos); /* veto 无闸位：Position 静默。 */
    XQ_EQ_I64(veto->m_positionHasChanged, 0);
    XQ_EQ_I64(veto->m_parentHasChanged, 0);
    XGraphicsItem_setParentItem(&veto->m_base, &p->m_base);
    XQ_EQ_I64(veto->m_parentHasChanged, 1); /* 无闸：立即派发。 */
    XGraphicsItem_setParentItem(&veto->m_base, NULL);
    XQ_EQ_I64(veto->m_parentHasChanged, 2);
    /* 对照：同无闸位下 setPos 仍静默（Qt :3858 合取闸只在 setPos 生效）。 */
    pos.x = 2.0f;
    pos.y = 2.0f;
    XGraphicsItem_setPos(&veto->m_base, &pos);
    XQ_EQ_I64(veto->m_positionHasChanged, 0);

    XGraphicsScene_clear(scene);
    XGraphicsScene_clear(scene2);
    XClassDelete(&veto->m_base); /* 否决项从未入场景：clear 删不到，须手动删。 */
    XQ_CHECK(s_changeAlive == 0);
    XClassDelete((XClass*)scene);
    XClassDelete((XClass*)scene2);
}

/* ==================== ⑪ 场景矩形与脏区登记 ==================== */

XQ_TEST(XGraphicsScene, sceneRectAndDirty)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsSceneEventProbe* a;
    XRectF r;
    const XRectF* dirty;
    XQ_REQUIRE(scene != NULL);

    /* 未设置场景矩形 ⇒ sceneRect 回落 itemsBoundingRect（Qt 原文）。 */
    a = xq_probeCreate(20.0f, 10.0f, 0xFF445566u);
    XQ_REQUIRE(a != NULL);
    {
        XPointF pos;
        pos.x = 5.0f;
        pos.y = 7.0f;
        XGraphicsItem_setPos(&a->m_base, &pos);
    }
    XGraphicsScene_addItem(scene, &a->m_base);
    r = XGraphicsScene_sceneRect(scene);
    XQ_CHECK(r.x == 5.0f && r.y == 7.0f && r.width == 20.0f && r.height == 10.0f);

    /* 显式设置后按设置值。 */
    r.x = 0.0f;
    r.y = 0.0f;
    r.width = 100.0f;
    r.height = 80.0f;
    XGraphicsScene_setSceneRect(scene, &r);
    r = XGraphicsScene_sceneRect(scene);
    XQ_CHECK(r.width == 100.0f && r.height == 80.0f);

    /* 脏区：update 全场景脏；updateRect 区域累计；clearDirty 复位。 */
    XGraphicsScene_update(scene);
    XQ_CHECK(XGraphicsScene_isDirty(scene));
    dirty = XGraphicsScene_dirtyRect_const(scene);
    XQ_CHECK(dirty != NULL && dirty->width == 100.0f);
    XGraphicsScene_clearDirty(scene);
    XQ_CHECK(!XGraphicsScene_isDirty(scene));
    XQ_CHECK(XGraphicsScene_dirtyRect_const(scene) == NULL);
    r.x = 1.0f;
    r.y = 2.0f;
    r.width = 3.0f;
    r.height = 4.0f;
    XGraphicsScene_updateRect(scene, &r);
    r.x = 10.0f;
    r.y = 20.0f;
    r.width = 5.0f;
    r.height = 6.0f;
    XGraphicsScene_updateRect(scene, &r);
    dirty = XGraphicsScene_dirtyRect_const(scene);
    XQ_CHECK(dirty != NULL);
    /* 并集 = (1,2)-(15,26)。 */
    XQ_CHECK(dirty->x == 1.0f && dirty->y == 2.0f);
    XQ_CHECK(dirty->width == 14.0f && dirty->height == 24.0f);

    XGraphicsScene_clear(scene);
    XQ_CHECK(s_probeAlive == 0);
    XClassDelete((XClass*)scene);
}

/* ==================== ⑫ Qt 探针数值锚点（几何恒等） ==================== */

XQ_TEST(XGraphicsScene, qtProbeAnchors)
{
    /* 探针同参（P 系列）在此做数值级复认：probe_scene.cpp 的几何不变量
     * 已在 ③④ 用例以指针集断言；本用例补几何数值（itemsBoundingRect
     * 与各 sceneBoundingRect，全部为整数锚点，无浮点漂移）。 */
    XGraphicsScene* scene;
    XGraphicsSceneEventProbe* a;
    XGraphicsSceneEventProbe* b;
    XGraphicsSceneEventProbe* c;
    XGraphicsSceneEventProbe* d;
    XRectF r;
    xq_buildProbeScene(&scene, &a, &b, &c, &d);

    /* A=(10,20)+(40x30)。 */
    r = XGraphicsItem_sceneBoundingRect(&a->m_base);
    XQ_CHECK(r.x == 10.0f && r.y == 20.0f && r.width == 40.0f &&
             r.height == 30.0f);
    /* B=(60,10)+(24x18)。 */
    r = XGraphicsItem_sceneBoundingRect(&b->m_base);
    XQ_CHECK(r.x == 60.0f && r.y == 10.0f && r.width == 24.0f &&
             r.height == 18.0f);
    /* D = A 位置 + 局部 (30,25)+(12x10) = (40,45)+(12x10)（P5 命中的前提）。 */
    r = XGraphicsItem_sceneBoundingRect(&d->m_base);
    XQ_CHECK(r.x == 40.0f && r.y == 45.0f && r.width == 12.0f &&
             r.height == 10.0f);
    /* itemsBoundingRect = 并集 (10,10)-(84,55)。 */
    r = XGraphicsScene_itemsBoundingRect(scene);
    XQ_CHECK(r.x == 10.0f && r.y == 10.0f);
    XQ_CHECK(r.width == 74.0f && r.height == 45.0f);
    /* sceneRect 未设置 ⇒ 回落 itemsBoundingRect。 */
    r = XGraphicsScene_sceneRect(scene);
    XQ_CHECK(r.x == 10.0f && r.y == 10.0f && r.width == 74.0f &&
             r.height == 45.0f);

    xq_freeProbeScene(scene, a, b, c, d);
}
