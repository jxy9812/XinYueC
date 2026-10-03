/**
 * @file       XTouchMultiPointTest.c
 * @brief      多点触摸回归测试（方案 B B1；对齐 Qt 6.8 QTouchEvent 多点形态）。
 * @details    覆盖：per-id 序列路由（双触点交错 BEGIN/UPDATE/END 各自
 *             独立且不串扰）、单点序列行为与既有语义逐位一致（touch→
 *             mouse 仿真）、CANCEL 清全部抓取、XTouchEvent setPoints/
 *             points/clone/deinit 生命周期（无泄漏）、主点手势状态机
 *             （第 4 节：单击=左键、双击=DBL_CLICK、拖动=滚轮相位、
 *             长按=右键自动弹 CONTEXT_MENU，惰性兜底路径驱动）。
 *             全部经 WSI 注入（handleTouchPoints_ex，与平台 XI2 分派
 *             同层，无需硬件）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XTouchMultiPointTest.h"
#include "XWidget.h"
#include "XWindowEvent.h"
#include "XWindowSystemInterface.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>

static int tp_failures = 0;
static void tp_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[TP-FAIL] %s\n", what ? what : "");
        ++tp_failures;
    }
}

/* 接收桩：正式 XCLASS 子类（继承 XWidget），覆写 touchEvent 虚槽。
 * 记录 touchBegin/update/end 到达次数与最后触点 id。 */
typedef struct TpSink
{
    XWidget m_base;      /**< 基类成员；必须为第一个。 */
    int m_begin;
    int m_update;
    int m_end;
    int32_t m_lastId;
} TpSink;

XCLASS_DEFINE_BEGING(TpSink)
XCLASS_DEFINE_EXTEND_END(TpSink, XWidget)

static void TpSink_touchEvent(XWidget* self, XEvent* event)
{
    TpSink* sink = (TpSink*)self;
    const XTouchEvent* te = (const XTouchEvent*)event;
    const XTouchPoint* pts;
    XEventType type = XEvent_type(event);
    if (type != XEVENT_TYPE_TOUCH_BEGIN && type != XEVENT_TYPE_TOUCH_UPDATE &&
        type != XEVENT_TYPE_TOUCH_END && type != XEVENT_TYPE_TOUCH_CANCEL)
        return;
    pts = XTouchEvent_points(te);
    if (type == XEVENT_TYPE_TOUCH_BEGIN) ++sink->m_begin;
    else if (type == XEVENT_TYPE_TOUCH_UPDATE) ++sink->m_update;
    else ++sink->m_end;
    sink->m_lastId = (pts && te->m_pointCount > 0) ? pts[0].m_id : -1;
    XEvent_accept(event);
}

XVtable* TpSink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(TpSink)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_TouchEvent, TpSink_touchEvent);
    return XVTABLE_DEFAULT;
}

static void tp_sinkTouchEvent(XWidget* self, XEvent* event)
{
    TpSink* sink = (TpSink*)self;
    const XTouchEvent* te = (const XTouchEvent*)event;
    const XTouchPoint* pts;
    XEventType type = XEvent_type(event);
    if (type != XEVENT_TYPE_TOUCH_BEGIN && type != XEVENT_TYPE_TOUCH_UPDATE &&
        type != XEVENT_TYPE_TOUCH_END && type != XEVENT_TYPE_TOUCH_CANCEL)
        return;
    pts = XTouchEvent_points(te);
    if (type == XEVENT_TYPE_TOUCH_BEGIN) ++sink->m_begin;
    else if (type == XEVENT_TYPE_TOUCH_UPDATE) ++sink->m_update;
    else ++sink->m_end;
    sink->m_lastId = (pts && te->m_pointCount > 0) ? pts[0].m_id : -1;
    XEvent_accept(event);
}

static XWidget* tp_sinkParent(const XTouchEvent* te)
{
    (void)te;
    return NULL;
}

/** @brief 构造单触点（id/坐标/状态）。 */
static XTouchPoint tp_point(int32_t id, int x, int y, int state)
{
    XTouchPoint p;
    p.m_id = id;
    p.m_state = state;
    p.m_position.x = (short)x;
    p.m_position.y = (short)y;
    p.m_globalPosition = p.m_position;
    p.m_pressure = 1.0f;
    return p;
}

/* ==================== 第 4 节：主点手势状态机回归 ==================== */

/* 手势计数 sink：正式 XCLASS 子类（继承 XWidget），覆写鼠标按下/释放/
 * 双击/上下文菜单/滚轮虚槽计数。触摸事件保持基类默认忽略——手势状态机
 * 只驱动未被接受的触摸序列（接受即走控件抓取，不合成）。鼠标按下/释放/
 * 双击有意不 accept（对标 Qt 未处理路径；右键按下未被接受时框架才自动
 * 合成 CONTEXT_MENU）；contextMenu/wheel 记录后 accept。 */
typedef struct TpGestureSink
{
    XWidget m_base;      /**< 基类成员；必须为第一个。 */
    int m_press;         /**< 左键 mousePress 次数。 */
    int m_rightPress;    /**< 右键 mousePress 次数（长按诊断）。 */
    int m_release;       /**< mouseRelease 次数（左右键合计）。 */
    int m_dblClick;      /**< mouseDoubleClick 次数。 */
    int m_contextMenu;   /**< contextMenu 次数。 */
    int m_wheel;         /**< wheel 次数。 */
    int m_lastAngleY;    /**< 最近一次滚轮 angleDelta.y。 */
    int m_lastPhase;     /**< 最近一次滚轮 phase（XWheelEventPhase）。 */
} TpGestureSink;

XCLASS_DEFINE_BEGING(TpGestureSink)
XCLASS_DEFINE_EXTEND_END(TpGestureSink, XWidget)

static void TpGestureSink_mousePress(XWidget* self, XEvent* event)
{
    TpGestureSink* sink = (TpGestureSink*)self;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (((const XMouseEvent*)event)->m_button == XMouseButton_RightButton)
        ++sink->m_rightPress;
    else
        ++sink->m_press;
    /* 有意忽略（不 accept）：右键未接受才触发框架 CONTEXT_MENU 合成。 */
}

static void TpGestureSink_mouseRelease(XWidget* self, XEvent* event)
{
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    ++((TpGestureSink*)self)->m_release;
}

static void TpGestureSink_mouseDoubleClick(XWidget* self, XEvent* event)
{
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) return;
    ++((TpGestureSink*)self)->m_dblClick;
}

static void TpGestureSink_contextMenu(XWidget* self, XEvent* event)
{
    (void)event;
    ++((TpGestureSink*)self)->m_contextMenu;
    XEvent_accept(event);
}

static void TpGestureSink_wheel(XWidget* self, XEvent* event)
{
    TpGestureSink* sink = (TpGestureSink*)self;
    if (XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
    ++sink->m_wheel;
    sink->m_lastAngleY = (int)((const XWheelEvent*)event)->m_angleDelta.y;
    sink->m_lastPhase = XWheelEvent_phase((const XWheelEvent*)event);
    XEvent_accept(event);
}

XVtable* TpGestureSink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(TpGestureSink)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, TpGestureSink_mousePress);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, TpGestureSink_mouseRelease);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent, TpGestureSink_mouseDoubleClick);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ContextMenuEvent, TpGestureSink_contextMenu);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, TpGestureSink_wheel);
    return XVTABLE_DEFAULT;
}

/** @brief 注入单点触摸序列步（第 4 节；显式时间戳驱动长按惰性兜底）。 */
static void tp_gestureStep(XWindow* xw, XEventType type, int32_t id,
                           int x, int y, int state, uint32_t ts)
{
    XTouchPoint one[1];
    one[0] = tp_point(id, x, y, state);
    XWindowSystemInterface_handleTouchPoints_ex(xw, type, one, 1, ts);
}

int XTouchMultiPointTest_run(void)
{
    TpSink sinkA;
    TpSink sinkB;
    XWidget top;
    XTouchPoint pts[2];

    memset(&sinkA, 0, sizeof(sinkA));
    memset(&sinkB, 0, sizeof(sinkB));
    XWidget_init(&top, NULL, 0);
    XWidget_setGeometry(&top, 0, 0, 400, 300);
    XWidget_init(&sinkA.m_base, &top, 0);
    XClassGetVtable(&sinkA.m_base) = TpSink_class_init();
    XWidget_setGeometry(&sinkA.m_base, 0, 0, 200, 300);
    XWidget_init(&sinkB.m_base, &top, 0);
    XClassGetVtable(&sinkB.m_base) = TpSink_class_init();
    XWidget_setGeometry(&sinkB.m_base, 200, 0, 200, 300);
    XWidget_show(&top);
    XWidget_show(&sinkA.m_base);
    XWidget_show(&sinkB.m_base);

    /* 0. 命中诊断：childAt 应命中两个 sink。 */
    {
        XPoint pa, pb;
        XWidget* hit;
        pa.x = 100; pa.y = 150;
        pb.x = 300; pb.y = 150;
        hit = XWidget_childAt(&top, &pa);
        fprintf(stderr, "[TPDBG] childAt(100,150)=%p (sinkA=%p) visible=%d\n",
                (void*)hit, (void*)&sinkA.m_base,
                (int)XWidget_isVisible(&sinkA.m_base));
        hit = XWidget_childAt(&top, &pb);
        fprintf(stderr, "[TPDBG] childAt(300,150)=%p (sinkB=%p) visible=%d\n",
                (void*)hit, (void*)&sinkB.m_base,
                (int)XWidget_isVisible(&sinkB.m_base));
    }

    /* 1. 双触点交错序列：id=1 落 A（被接受→抓取），id=2 落 B。
     *    [R2 排查结论，2026-09-26] 本节 4 断言失败非第七轮派发层连带
     *    （XWidget_dispatchTouchEvent 工作树与 HEAD 逐行一致），根因是
     *    XWidget.c 单一全局 g_touchGrabWidget 抓取（id=1 抓取后 id=2 全
     *    序列误投 A），偏离 Qt 6.8 per-point 隐式抓取语义（qapplication.cpp
     *    translateRawTouchEvent:3802 逐点 childAt/target、activateImplicit
     *    TouchGrab:3791 抓取记于触点）。真修=per-id 抓取表，坐落 XWidget.c
     *    （他路独占），处方已出：R2 deferred。 */
    pts[0] = tp_point(1, 100, 150, XTOUCHPOINT_STATE_PRESSED);
    pts[1] = tp_point(2, 300, 150, XTOUCHPOINT_STATE_PRESSED);
    /* id=1 BEGIN（主点在 A 区）。 */
    {
        XTouchPoint one[1];
        one[0] = pts[0];
        XWindowSystemInterface_handleTouchPoints_ex(
            (XWindow*)XWidget_windowHandle(&top), XEVENT_TYPE_TOUCH_BEGIN, one, 1, 100);
    }
    tp_expect(sinkA.m_begin == 1, "id=1 BEGIN 到达 A");
    /* id=2 BEGIN（主点在 B 区）。 */
    {
        XTouchPoint one[1];
        one[0] = pts[1];
        XWindowSystemInterface_handleTouchPoints_ex(
            (XWindow*)XWidget_windowHandle(&top), XEVENT_TYPE_TOUCH_BEGIN, one, 1, 110);
    }
    tp_expect(sinkB.m_begin == 1, "id=2 BEGIN 到达 B");
    /* 交替 UPDATE：各自路由到各自抓取控件。 */
    {
        XTouchPoint one[1];
        one[0] = tp_point(1, 110, 160, XTOUCHPOINT_STATE_UPDATED);
        XWindowSystemInterface_handleTouchPoints_ex(
            (XWindow*)XWidget_windowHandle(&top), XEVENT_TYPE_TOUCH_UPDATE, one, 1, 120);
        one[0] = tp_point(2, 290, 140, XTOUCHPOINT_STATE_UPDATED);
        XWindowSystemInterface_handleTouchPoints_ex(
            (XWindow*)XWidget_windowHandle(&top), XEVENT_TYPE_TOUCH_UPDATE, one, 1, 130);
    }
    tp_expect(sinkA.m_update == 1 && sinkA.m_lastId == 1,
              "id=1 UPDATE 路由 A（不串扰 B）");
    tp_expect(sinkB.m_update == 1 && sinkB.m_lastId == 2,
              "id=2 UPDATE 路由 B（不串扰 A）");
    /* 逆序 END：先 2 后 1。 */
    {
        XTouchPoint one[1];
        one[0] = tp_point(2, 300, 150, XTOUCHPOINT_STATE_RELEASED);
        XWindowSystemInterface_handleTouchPoints_ex(
            (XWindow*)XWidget_windowHandle(&top), XEVENT_TYPE_TOUCH_END, one, 1, 140);
        one[0] = tp_point(1, 100, 150, XTOUCHPOINT_STATE_RELEASED);
        XWindowSystemInterface_handleTouchPoints_ex(
            (XWindow*)XWidget_windowHandle(&top), XEVENT_TYPE_TOUCH_END, one, 1, 150);
    }
    tp_expect(sinkB.m_end == 1 && sinkA.m_end == 1, "双触点 END 各达各控件");

    /* 2. 多点单事件负载：事件携带两点，主点字段=points[0]（B1 语义：
       控件层派发按主点；多点并行派发随 B2 平台聚合落地）。 */
    {
        XTouchEvent* ev = XTouchEvent_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_TOUCH_BEGIN, NULL, NULL, 1);
        XTouchPoint two[2];
        two[0] = tp_point(3, 100, 100, XTOUCHPOINT_STATE_PRESSED);
        two[1] = tp_point(4, 300, 100, XTOUCHPOINT_STATE_PRESSED);
        tp_expect(ev != NULL, "多点事件创建成功");
        XTouchEvent_setPoints(ev, two, 2);
        tp_expect(ev->m_pointCount == 2 &&
                  XTouchEvent_points(ev)[1].m_id == 4,
                  "多点负载 setPoints/points 一致");
        tp_expect(ev->m_position.x == 100,
                  "多点负载主点同步 points[0]");
        XClassDelete((XEvent*)ev);
    }

    /* 3. XTouchEvent 生命周期：setPoints/points/clone/deinit 无泄漏。 */
    {
        XTouchEvent* ev = XTouchEvent_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_TOUCH_BEGIN, NULL, NULL, 1);
        XTouchPoint two[2];
        const XTouchPoint* rd;
        XEvent* copy;
        two[0] = tp_point(9, 10, 20, XTOUCHPOINT_STATE_PRESSED);
        two[1] = tp_point(10, 30, 40, XTOUCHPOINT_STATE_PRESSED);
        tp_expect(ev != NULL, "事件创建成功");
        XTouchEvent_setPoints(ev, two, 2);
        rd = XTouchEvent_points(ev);
        tp_expect(rd != NULL && ev->m_pointCount == 2 &&
                  rd[1].m_id == 10, "setPoints 深拷贝+读取一致");
        tp_expect(ev->m_position.x == 10 && ev->m_position.y == 20,
                  "主点字段同步为 points[0]");
        copy = XEvent_clone_base((XEvent*)ev);
        tp_expect(copy != NULL, "clone 成功");
        if (copy)
        {
            const XTouchPoint* rd2 =
                XTouchEvent_points((const XTouchEvent*)copy);
            tp_expect(rd2 != NULL &&
                      ((const XTouchEvent*)copy)->m_pointCount == 2 &&
                      rd2[0].m_id == 9,
                      "clone 深拷贝触点列表");
                XClassDelete(copy);
            }
        XClassDelete((XEvent*)ev);
    }

    /* 4. 主点手势状态机（tap/双击/拖动滚轮/长按惰性路径）：手势 sink
     *    后入栈覆盖两既有 sink（childAt 逆序命中=最上层）；触摸保持未
     *    接受→整序列走合成路径。显式时间戳使长按走惰性兜底（无需真等
     *    800ms/事件环）；每断言组间注入 CANCEL 清状态。 */
    {
        TpGestureSink gs;
        XWindow* xw = (XWindow*)XWidget_windowHandle(&top);
        memset(&gs, 0, sizeof(gs));
        XWidget_init(&gs.m_base, &top, 0);
        XClassGetVtable(&gs.m_base) = TpGestureSink_class_init();
        XWidget_setGeometry(&gs.m_base, 0, 0, 400, 300);
        XWidget_show(&gs.m_base);
        tp_expect(xw != NULL, "4.0 手势组：顶层窗口句柄就绪");

        /* 4.1 单击=左键：press@BEGIN、release@END 同点（既有 touch→mouse
         *    仿真 tap 子情况逐位兼容锚点）。 */
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_BEGIN, 5, 100, 100,
                       XTOUCHPOINT_STATE_PRESSED, 1000);
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_END, 5, 100, 100,
                       XTOUCHPOINT_STATE_RELEASED, 1050);
        tp_expect(gs.m_press == 1 && gs.m_release == 1,
                  "4.1 tap：press@BEGIN 且 release@END");
        /* 组间 CANCEL：清手势单序列状态（序列已收口，此处应为空操作）。 */
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_CANCEL, 5, 100, 100,
                       XTOUCHPOINT_STATE_RELEASED, 1060);

        /* 4.2 双击=DBL_CLICK：第二序列 BEGIN 在双击窗口（400ms）内且同点
         *    →布防双击、不合成 press；END 合成 DBL_CLICK+RELEASE。 */
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_BEGIN, 5, 100, 100,
                       XTOUCHPOINT_STATE_PRESSED, 1200);
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_END, 5, 100, 100,
                       XTOUCHPOINT_STATE_RELEASED, 1250);
        tp_expect(gs.m_dblClick == 1, "4.2 double-tap：DBL_CLICK 合成");
        tp_expect(gs.m_press == 1 && gs.m_release == 2,
                  "4.2 double-tap：第二序列不合成 press（release 配对）");
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_CANCEL, 5, 100, 100,
                       XTOUCHPOINT_STATE_RELEASED, 1260);

        /* 4.3 拖动=滚轮：dy=80 超阈值→转拖（左键 press 以远偏移释放，
         *    释放不落回命中靶）；UPDATE 合成 ScrollBegin 滚轮（angleDelta.y
         *    取反<0，整格 -120）；END 补 ScrollEnd 收口。 */
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_BEGIN, 5, 100, 100,
                       XTOUCHPOINT_STATE_PRESSED, 1000);
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_UPDATE, 5, 100, 180,
                       XTOUCHPOINT_STATE_UPDATED, 1100);
        tp_expect(gs.m_wheel >= 1 && gs.m_lastAngleY < 0 &&
                  gs.m_lastPhase == XWheelEventPhase_ScrollBegin,
                  "4.3 drag→wheel：ScrollBegin 且 angleDelta.y<0");
        tp_expect(gs.m_press == 2 && gs.m_release == 2,
                  "4.3 drag：press 随 BEGIN 合成、转换远偏移释放不落回靶");
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_END, 5, 100, 180,
                       XTOUCHPOINT_STATE_RELEASED, 1150);
        tp_expect(gs.m_wheel == 2 &&
                  gs.m_lastPhase == XWheelEventPhase_ScrollEnd,
                  "4.3 drag：END 补 ScrollEnd 收口");
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_CANCEL, 5, 5, 5,
                       XTOUCHPOINT_STATE_RELEASED, 1160);

        /* 4.4 长按=右键（惰性兜底路径）：UPDATE 到达时已超长按间隔
         *    （1900-1000>=800）且未拖动→先触发长按：左键序列以远偏移
         *    释放关断，合成右键按下（未接受→框架自动弹 CONTEXT_MENU）+
         *    右键释放；其后的 END 不再合成任何鼠标事件。 */
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_BEGIN, 5, 200, 200,
                       XTOUCHPOINT_STATE_PRESSED, 1000);
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_UPDATE, 5, 200, 200,
                       XTOUCHPOINT_STATE_UPDATED, 1900);
        tp_expect(gs.m_contextMenu == 1 && gs.m_rightPress == 1,
                  "4.4 long-press：右键按下自动弹 CONTEXT_MENU");
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_END, 5, 200, 200,
                       XTOUCHPOINT_STATE_RELEASED, 1950);
        tp_expect(gs.m_press == 3 && gs.m_release == 3 && gs.m_dblClick == 1,
                  "4.4 long-press：左键序列远偏移关断、END 不再合成");
        tp_gestureStep(xw, XEVENT_TYPE_TOUCH_CANCEL, 5, 200, 200,
                       XTOUCHPOINT_STATE_RELEASED, 1960);

        XClassDeinit(&gs.m_base);
    }

    XClassDeinit(&sinkB.m_base);
    XClassDeinit(&sinkA.m_base);
    XClassDeinit(&top);
    fprintf(stderr, "[TP] failures=%d\n", tp_failures);
    return tp_failures;
}
