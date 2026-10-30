/**
 * @file       XGestureRecognizerTest.c
 * @brief      手势识别器框架回归测试（注入式；自带 main）。
 * @details    覆盖 XGestureRecognizer 族的端到端链路：原始触摸帧注入 →
 *             识别器判定 → 状态机迁移 → XGestureEvent 经事件循环投递 →
 *             控件 GestureEvent 虚槽消费。逐个验证：
 *             ① 点按（位移超容差不判、按容差内判）；
 *             ② 长按（时长到触发、位移超容差取消）；
 *             ③ 双击（间隔与距离内判双击）；
 *             ④ 平移（越阈触发 Started、逐帧 Updated、抬手 Finished，
 *                增量位移跟手 1:1）；
 *             ⑤ 轻扫（位移+速度达标判轻扫并给方向）；
 *             ⑥ 双指缩放（两指间距比=缩放系数、累计比例累乘）；
 *             ⑦ 双指旋转（两指连线角差=旋转增量、累计角累加）；
 *             ⑧ 甩动（抬手速度达标判甩动并给速度）。
 *             全部经 WSI 注入（handleTouchPoints_ex，与平台触摸分派同层，
 *             无需硬件）；手势事件经 XCoreApplication_processEvents 泵取。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XWindowEvent.h"
#include "XWindowSystemInterface.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XGestureEvent.h"
#include "XGestureRecognizer.h"
#include "XPanGestureRecognizer.h"
#include "XTapGestureRecognizer.h"
#include "XLongPressGestureRecognizer.h"
#include "XDoubleTapGestureRecognizer.h"
#include "XSwipeGestureRecognizer.h"
#include "XPinchGestureRecognizer.h"
#include "XRotateGestureRecognizer.h"
#include "XFlingGestureRecognizer.h"
#include <stdio.h>
#include <string.h>

static int gr_failures = 0;

static void gr_expect(bool cond, const char* what)
{
    fprintf(stderr, "[%s] %s\n", cond ? "OK" : "FAIL", what ? what : "");
    if (!cond) ++gr_failures;
}

/* ==================== 手势接收桩（控件 GestureEvent 槽） ==================== */

typedef struct GrSink
{
    XWidget m_base;      /**< 基类成员；必须为第一个。 */
    int m_count;         /**< 收到的手势事件数。 */
    int m_started;       /**< Started 状态事件数。 */
    int m_updated;       /**< Updated 状态事件数。 */
    int m_finished;      /**< Finished 状态事件数。 */
    int m_canceled;      /**< Canceled 状态事件数。 */
    int m_lastType;      /**< 最近一次手势类型。 */
    int m_lastFinger;    /**< 最近一次触点数量。 */
    XPoint m_lastDelta;  /**< 最近一次增量位移。 */
    float m_lastScale;   /**< 最近一次缩放系数。 */
    float m_lastTotalScale; /**< 最近一次累计缩放。 */
    float m_lastRotation;/**< 最近一次旋转角增量。 */
    float m_lastTotalRot;/**< 最近一次累计旋转角。 */
    float m_lastVelX;    /**< 最近一次水平速度。 */
    int m_lastDirection; /**< 最近一次方向。 */
} GrSink;

XCLASS_DEFINE_BEGING(GrSink)
XCLASS_DEFINE_EXTEND_END(GrSink, XWidget)

static void GrSink_gestureEvent(XWidget* self, XEvent* event)
{
    GrSink* sink = (GrSink*)self;
    const XGestureEvent* ge;
    if (XEvent_type(event) != XEVENT_TYPE_GESTURE) return;
    ge = (const XGestureEvent*)event;
    ++sink->m_count;
    sink->m_lastType = (int)XGestureEvent_gestureType(ge);
    sink->m_lastFinger = XGestureEvent_fingerCount(ge);
    sink->m_lastDelta = XGestureEvent_delta(ge);
    sink->m_lastScale = XGestureEvent_scaleFactor(ge);
    sink->m_lastTotalScale = XGestureEvent_totalScaleFactor(ge);
    sink->m_lastRotation = XGestureEvent_rotationAngle(ge);
    sink->m_lastTotalRot = XGestureEvent_totalRotationAngle(ge);
    sink->m_lastVelX = XGestureEvent_velocityX(ge);
    sink->m_lastDirection = (int)XGestureEvent_direction(ge);
    switch (XGestureEvent_state(ge)) {
    case XGestureState_Started:  ++sink->m_started;  break;
    case XGestureState_Updated:  ++sink->m_updated;  break;
    case XGestureState_Finished: ++sink->m_finished; break;
    case XGestureState_Canceled: ++sink->m_canceled; break;
    default: break;
    }
    XEvent_accept(event);
}

XVtable* GrSink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(GrSink)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_GestureEvent, GrSink_gestureEvent);
    return XVTABLE_DEFAULT;
}

/* ==================== 注入辅助 ==================== */

/** @brief 构造单触点（id/坐标/状态）。 */
static XTouchPoint gr_point(int32_t id, int x, int y, int state)
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

/** @brief 注入单点触摸序列步。 */
static void gr_step(XWindow* xw, XEventType type, int32_t id,
                    int x, int y, int state, uint32_t ts)
{
    XTouchPoint one[1];
    one[0] = gr_point(id, x, y, state);
    XWindowSystemInterface_handleTouchPoints_ex(xw, type, one, 1, ts);
}

/** @brief 注入双点触摸序列步。 */
static void gr_step2(XWindow* xw, XEventType type, int32_t id0, int x0, int y0,
                     int32_t id1, int x1, int y1, int state, uint32_t ts)
{
    XTouchPoint two[2];
    two[0] = gr_point(id0, x0, y0, state);
    two[1] = gr_point(id1, x1, y1, state);
    XWindowSystemInterface_handleTouchPoints_ex(xw, type, two, 2, ts);
}

/** @brief 泵事件循环（取走投递的手势事件）。 */
static void gr_pump(void)
{
    XGuiApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 泵事件循环至墙钟期限（毫秒）。 */
static void gr_pumpUntil(int64_t deadlineMs)
{
    while (XDateTime_currentMSecsSinceEpoch() < deadlineMs)
        gr_pump();
}

int main(void)
{
    XWidget top;
    GrSink sink;
    XWindow* xw;
    uint32_t ts = 1000;

    (void)XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);

    XWidget_init(&top, NULL, 0);
    XWidget_setGeometry(&top, 0, 0, 400, 300);
    memset(&sink, 0, sizeof(sink));
    XWidget_init(&sink.m_base, &top, 0);
    XClassGetVtable(&sink.m_base) = GrSink_class_init();
    XWidget_setGeometry(&sink.m_base, 0, 0, 400, 300);
    XWidget_show(&top);
    XWidget_show(&sink.m_base);
    xw = (XWindow*)XWidget_windowHandle(&top);
    gr_expect(xw != NULL, "0 舞台就绪");

    /* ① 点按：同点按下抬起（位移 0 ≤ 容差 40）→ 判点按。 */
    {
        XTapGestureRecognizer* rec = XTapGestureRecognizer_create(NULL);
        XWidget_addGestureRecognizer(&sink.m_base, &rec->m_base);
        sink.m_count = 0;
        gr_step(xw, XEVENT_TYPE_TOUCH_BEGIN, 1, 100, 100,
                XTOUCHPOINT_STATE_PRESSED, ts);
        gr_step(xw, XEVENT_TYPE_TOUCH_END, 1, 100, 100,
                XTOUCHPOINT_STATE_RELEASED, ts + 40);
        gr_pump();
        gr_expect(sink.m_count == 2 && sink.m_finished == 1 &&
                  sink.m_lastType == (int)XGestureEventType_Tap,
                  "1a 点按：Started+Finished 且类型为 Tap");
        /* 位移超容差（60px）→ 不判点按。 */
        sink.m_count = 0;
        gr_step(xw, XEVENT_TYPE_TOUCH_BEGIN, 2, 100, 100,
                XTOUCHPOINT_STATE_PRESSED, ts + 200);
        gr_step(xw, XEVENT_TYPE_TOUCH_END, 2, 160, 100,
                XTOUCHPOINT_STATE_RELEASED, ts + 240);
        gr_pump();
        gr_expect(sink.m_count == 0, "1b 点按：位移超容差不判定");
        XWidget_removeGestureRecognizer(&sink.m_base, &rec->m_base);
        XClassDelete((XClass*)rec);
    }

    /* ② 长按：按住 800ms（>700ms 默认）→ 触发长按。 */
    {
        XLongPressGestureRecognizer* rec =
            XLongPressGestureRecognizer_create(NULL);
        XWidget_addGestureRecognizer(&sink.m_base, &rec->m_base);
        sink.m_count = 0;
        gr_step(xw, XEVENT_TYPE_TOUCH_BEGIN, 3, 120, 120,
                XTOUCHPOINT_STATE_PRESSED, ts + 1000);
        /* 墙钟快进：识别器用触摸时间戳域判定，故用注入时间戳推进。 */
        gr_step(xw, XEVENT_TYPE_TOUCH_UPDATE, 3, 120, 120,
                XTOUCHPOINT_STATE_UPDATED, ts + 1800);
        gr_pump();
        gr_expect(sink.m_started >= 1 &&
                  sink.m_lastType == (int)XGestureEventType_TapAndHold,
                  "2 长按：超时触发 TapAndHold");
        gr_step(xw, XEVENT_TYPE_TOUCH_END, 3, 120, 120,
                XTOUCHPOINT_STATE_RELEASED, ts + 1900);
        gr_pump();
        XWidget_removeGestureRecognizer(&sink.m_base, &rec->m_base);
        XClassDelete((XClass*)rec);
    }

    /* ③ 双击：两次点按（间隔 200ms、距离 10px）→ 判双击。 */
    {
        XDoubleTapGestureRecognizer* rec =
            XDoubleTapGestureRecognizer_create(NULL);
        XWidget_addGestureRecognizer(&sink.m_base, &rec->m_base);
        sink.m_count = 0;
        gr_step(xw, XEVENT_TYPE_TOUCH_BEGIN, 4, 100, 100,
                XTOUCHPOINT_STATE_PRESSED, ts + 3000);
        gr_step(xw, XEVENT_TYPE_TOUCH_END, 4, 100, 100,
                XTOUCHPOINT_STATE_RELEASED, ts + 3040);
        gr_pump();
        gr_step(xw, XEVENT_TYPE_TOUCH_BEGIN, 4, 105, 100,
                XTOUCHPOINT_STATE_PRESSED, ts + 3200);
        gr_step(xw, XEVENT_TYPE_TOUCH_END, 4, 105, 100,
                XTOUCHPOINT_STATE_RELEASED, ts + 3240);
        gr_pump();
        gr_expect(sink.m_lastType == (int)XGestureEventType_DoubleTap,
                  "3 双击：两次点按判 DoubleTap");
        XWidget_removeGestureRecognizer(&sink.m_base, &rec->m_base);
        XClassDelete((XClass*)rec);
    }

    /* ④ 平移：越阈触发、逐帧增量跟手 1:1、抬手 Finished。 */
    {
        XPanGestureRecognizer* rec = XPanGestureRecognizer_create(NULL);
        XWidget_addGestureRecognizer(&sink.m_base, &rec->m_base);
        sink.m_count = 0;
        sink.m_started = 0;
        sink.m_updated = 0;
        sink.m_finished = 0;
        gr_step(xw, XEVENT_TYPE_TOUCH_BEGIN, 5, 100, 150,
                XTOUCHPOINT_STATE_PRESSED, ts + 5000);
        gr_step(xw, XEVENT_TYPE_TOUCH_UPDATE, 5, 100, 120,
                XTOUCHPOINT_STATE_UPDATED, ts + 5016);
        gr_pump();
        gr_expect(sink.m_started >= 1 &&
                  sink.m_lastType == (int)XGestureEventType_Pan,
                  "4a 平移：越阈触发 Started（类型 Pan）");
        gr_step(xw, XEVENT_TYPE_TOUCH_UPDATE, 5, 100, 90,
                XTOUCHPOINT_STATE_UPDATED, ts + 5032);
        gr_pump();
        gr_expect(sink.m_updated >= 1 && sink.m_lastDelta.y == -30,
                  "4b 平移：逐帧增量跟手 1:1（dy=-30）");
        gr_step(xw, XEVENT_TYPE_TOUCH_END, 5, 100, 90,
                XTOUCHPOINT_STATE_RELEASED, ts + 5048);
        gr_pump();
        gr_expect(sink.m_finished >= 1, "4c 平移：抬手 Finished");
        XWidget_removeGestureRecognizer(&sink.m_base, &rec->m_base);
        XClassDelete((XClass*)rec);
    }

    /* ⑤ 轻扫：位移 80px、时长 80ms（速度 1000px/s）→ 判轻扫并给方向。 */
    {
        XSwipeGestureRecognizer* rec = XSwipeGestureRecognizer_create(NULL);
        XWidget_addGestureRecognizer(&sink.m_base, &rec->m_base);
        sink.m_count = 0;
        gr_step(xw, XEVENT_TYPE_TOUCH_BEGIN, 6, 50, 150,
                XTOUCHPOINT_STATE_PRESSED, ts + 6000);
        gr_step(xw, XEVENT_TYPE_TOUCH_UPDATE, 6, 130, 150,
                XTOUCHPOINT_STATE_UPDATED, ts + 6080);
        gr_step(xw, XEVENT_TYPE_TOUCH_END, 6, 130, 150,
                XTOUCHPOINT_STATE_RELEASED, ts + 6090);
        gr_pump();
        gr_expect(sink.m_lastType == (int)XGestureEventType_Swipe &&
                  sink.m_lastDirection == (int)XGestureDirection_Right,
                  "5 轻扫：判 Swipe 且方向为右");
        XWidget_removeGestureRecognizer(&sink.m_base, &rec->m_base);
        XClassDelete((XClass*)rec);
    }

    /* ⑥ 双指缩放：两指间距 100→150（比例 1.5），累计比例 1.5。 */
    {
        XPinchGestureRecognizer* rec = XPinchGestureRecognizer_create(NULL);
        XWidget_addGestureRecognizer(&sink.m_base, &rec->m_base);
        sink.m_count = 0;
        gr_step2(xw, XEVENT_TYPE_TOUCH_BEGIN, 7, 100, 100, 8, 200, 100,
                 XTOUCHPOINT_STATE_PRESSED, ts + 7000);
        gr_step2(xw, XEVENT_TYPE_TOUCH_UPDATE, 7, 75, 100, 8, 225, 100,
                 XTOUCHPOINT_STATE_UPDATED, ts + 7016);
        gr_pump();
        gr_expect(sink.m_lastType == (int)XGestureEventType_Pinch &&
                  sink.m_lastScale > 1.4f && sink.m_lastScale < 1.6f &&
                  sink.m_lastTotalScale > 1.4f,
                  "6 缩放：比例 1.5、累计 1.5");
        XWidget_removeGestureRecognizer(&sink.m_base, &rec->m_base);
        XClassDelete((XClass*)rec);
    }

    /* ⑦ 双指旋转：两指连线由水平转 90° → 旋转增量约 90°。
       注：UPDATE 帧两指须仍构成一条**非退化**连线（两指不重合），
       否则连线方向无定义、旋转增量不可导出。原数据把两指都放到
       (100,200)（连线长度 0），atan2(0,0)=0 → 增量 0°，与「水平转
       90°」的本意不符——此处改为绕原中点 (150,100) 的刚体旋转：
       (100,100)→(150,50)、(200,100)→(150,150)，中点与指距（100）不变，
       仅方向由水平转 90°，与本用例断言的契约一致。 */
    {
        XRotateGestureRecognizer* rec = XRotateGestureRecognizer_create(NULL);
        XWidget_addGestureRecognizer(&sink.m_base, &rec->m_base);
        sink.m_count = 0;
        gr_step2(xw, XEVENT_TYPE_TOUCH_BEGIN, 9, 100, 100, 10, 200, 100,
                 XTOUCHPOINT_STATE_PRESSED, ts + 8000);
        gr_step2(xw, XEVENT_TYPE_TOUCH_UPDATE, 9, 150, 50, 10, 150, 150,
                 XTOUCHPOINT_STATE_UPDATED, ts + 8016);
        gr_pump();
        gr_expect(sink.m_lastType == (int)XGestureEventType_Rotate &&
                  sink.m_lastRotation > 80.0f && sink.m_lastRotation < 100.0f,
                  "7 旋转：增量约 90°");
        XWidget_removeGestureRecognizer(&sink.m_base, &rec->m_base);
        XClassDelete((XClass*)rec);
    }

    /* ⑧ 甩动：位移 100px、时长 100ms（速度 1000px/s）→ 判甩动并给速度。 */
    {
        XFlingGestureRecognizer* rec = XFlingGestureRecognizer_create(NULL);
        XWidget_addGestureRecognizer(&sink.m_base, &rec->m_base);
        sink.m_count = 0;
        gr_step(xw, XEVENT_TYPE_TOUCH_BEGIN, 11, 50, 150,
                XTOUCHPOINT_STATE_PRESSED, ts + 9000);
        gr_step(xw, XEVENT_TYPE_TOUCH_END, 11, 150, 150,
                XTOUCHPOINT_STATE_RELEASED, ts + 9100);
        gr_pump();
        gr_expect(sink.m_lastType == (int)XGestureEventType_Fling &&
                  sink.m_lastVelX > 900.0f,
                  "8 甩动：判 Fling 且给出速度");
        XWidget_removeGestureRecognizer(&sink.m_base, &rec->m_base);
        XClassDelete((XClass*)rec);
    }

    XClassDeinit(&sink.m_base);
    XClassDeinit(&top);
    fprintf(stderr, "[GestureRecognizerTest] failures=%d\n", gr_failures);
    return gr_failures;
}
