/**
 * @file       XScrollTouchProbe.c
 * @brief      滚动区触摸跟手+惯性独立探针（XAbstractScrollArea 触摸滚动
 *             通道的运行时验收；自带 main，脱离整包回归单独跑）。
 * @details     注入式验证（WSI handleTouchPoints_ex，与平台触摸分派同层，
 *             无需硬件）：
 *             ① 跟手 1:1+方向（移动端口径）：手指上滑 100px=value+100，
 *                Track 模式零滚轮合成；
 *             ② 惯性 fling：快速上甩 END 后 value 无事件继续增大，~1.2s
 *                内停稳不漂移，值域 600 钳口封顶；
 *             ③ tap 零滚动；
 *             ④ 不可滚内容（range 0）拖动零滚动（DragBegin 放行）；
 *             ⑤ touchScrollEnabled=false 回落合成滚轮量化语义（+60 格）
 *                且无惯性漂移。
 *             运行：XScrollTouchProbe_Test，退出码=失败数。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XWindowEvent.h"
#include "XWindowSystemInterface.h"
#include "XScrollArea.h"
#include "XDateTime.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>

static int sp_failures = 0;

static void sp_expect(bool cond, const char* what)
{
    fprintf(stderr, "[%s] %s\n", cond ? "OK" : "FAIL",
            what ? what : "");
    if (!cond) ++sp_failures;
}

/** @brief 构造单触点（id/坐标/状态）。 */
static XTouchPoint sp_point(int32_t id, int x, int y, int state)
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
static void sp_step(XWindow* xw, XEventType type, int32_t id,
                    int x, int y, int state, uint32_t ts)
{
    XTouchPoint one[1];
    one[0] = sp_point(id, x, y, state);
    XWindowSystemInterface_handleTouchPoints_ex(xw, type, one, 1, ts);
}

/** @brief 泵事件循环至墙钟期限（惯性拍/合成滚轮收口需要事件环）。 */
static void sp_pumpUntil(int64_t deadlineMs)
{
    while (XDateTime_currentMSecsSinceEpoch() < deadlineMs)
        XGuiApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 墙钟忙等（喂 DragUpdate 帧距=速度采样间隔）。 */
static void sp_busyMs(int ms)
{
    int64_t deadline = XDateTime_currentMSecsSinceEpoch() + ms;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) { }
}

int main(void)
{
    XWidget top2;
    XScrollArea* sa;
    XWidget* content;
    XScrollBar* vsb;
    XWindow* xw2;
    uint32_t ts = 10000;

    (void)XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);

    /* 舞台：400x300 顶层 + 200x200 滚动区 + 200x800 定尺内容（range
     * 600，横条恒关）。 */
    XWidget_init(&top2, NULL, 0);
    XWidget_setGeometry(&top2, 0, 0, 400, 300);
    sa = XScrollArea_create(&top2, 0);
    if (!sa) { fprintf(stderr, "[FAIL] 滚动区创建\n"); return 1; }
    XWidget_setGeometry((XWidget*)sa, 0, 0, 200, 200);
    content = XWidget_create(NULL, 0);
    XWidget_setGeometry(content, 0, 0, 200, 800);
    XScrollArea_setWidget(sa, content); /* resizable=false：内容定尺。 */
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)sa, (int)XScrollBarPolicy_AlwaysOff);
    XWidget_show(&top2);
    XWidget_show((XWidget*)sa);
    xw2 = (XWindow*)XWidget_windowHandle(&top2);
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)sa);
    sp_expect(xw2 != NULL && vsb != NULL && XScrollBar_maximum(vsb) == 600,
              "0 舞台就绪（内容 800 > 视口 200 → range 600）");

    /* ① 跟手 1:1+方向：手指上滑 25px×4 帧 → value 恰 +100（含越阈帧
     *    全量差分）；Track 模式跟手无量化。 */
    ts = 20000;
    sp_step(xw2, XEVENT_TYPE_TOUCH_BEGIN, 21, 100, 180,
            XTOUCHPOINT_STATE_PRESSED, ts);
    {
        int frame;
        for (frame = 0; frame < 4; ++frame) {
            ts += 16;
            sp_step(xw2, XEVENT_TYPE_TOUCH_UPDATE, 21, 100,
                    180 - 25 * (frame + 1),
                    XTOUCHPOINT_STATE_UPDATED, ts);
        }
    }
    sp_expect(XScrollBar_value(vsb) == 100,
              "1 跟手 1:1（100px 手指=100px 滚动；上滑=值增=内容随指）");
    sp_step(xw2, XEVENT_TYPE_TOUCH_END, 21, 100, 80,
            XTOUCHPOINT_STATE_RELEASED, ts += 16);
    sp_step(xw2, XEVENT_TYPE_TOUCH_CANCEL, 21, 5, 5,
            XTOUCHPOINT_STATE_RELEASED, ts += 16);

    /* ② 惯性 fling：3 帧 30px/帧（帧距 ~30ms 墙钟）快速上甩 → END 后
     *    value 无事件继续增大、停稳不漂移、≤600。 */
    {
        int vAfterDrag;
        int vSettled;
        ts = 40000;
        sp_step(xw2, XEVENT_TYPE_TOUCH_BEGIN, 22, 100, 180,
                XTOUCHPOINT_STATE_PRESSED, ts);
        for (ts = 40000; ts < 40120; ts += 16) {
            sp_busyMs(30);
            sp_step(xw2, XEVENT_TYPE_TOUCH_UPDATE, 22, 100,
                    180 - 30 * ((int)(ts - 40000) / 16 + 1),
                    XTOUCHPOINT_STATE_UPDATED, ts);
        }
        vAfterDrag = XScrollBar_value(vsb);
        sp_step(xw2, XEVENT_TYPE_TOUCH_END, 22, 100, 180 - 30 * 3,
                XTOUCHPOINT_STATE_RELEASED, ts += 16);
        sp_pumpUntil(XDateTime_currentMSecsSinceEpoch() + 1200);
        vSettled = XScrollBar_value(vsb);
        sp_expect(vSettled > vAfterDrag,
                  "2a fling：END 后惯性继续滚动（无需触摸事件）");
        sp_expect(vSettled <= 600, "2b fling：值域钳口封顶");
        {
            int vHold = XScrollBar_value(vsb);
            sp_pumpUntil(XDateTime_currentMSecsSinceEpoch() + 150);
            sp_expect(XScrollBar_value(vsb) == vHold,
                      "2c fling：停稳后不再漂移");
        }
    }

    /* ③ tap 零滚动：同点 BEGIN/END（点击语义），value 不动。 */
    {
        int v = XScrollBar_value(vsb);
        ts = 60000;
        sp_step(xw2, XEVENT_TYPE_TOUCH_BEGIN, 23, 100, 100,
                XTOUCHPOINT_STATE_PRESSED, ts);
        sp_step(xw2, XEVENT_TYPE_TOUCH_END, 23, 100, 100,
                XTOUCHPOINT_STATE_RELEASED, ts + 40);
        sp_expect(XScrollBar_value(vsb) == v, "3 tap：零滚动");
        sp_step(xw2, XEVENT_TYPE_TOUCH_CANCEL, 23, 5, 5,
                XTOUCHPOINT_STATE_RELEASED, ts + 60);
    }

    /* ④ 不可滚内容（range 0）不认领：拖动零滚动。 */
    {
        XWidget top3;
        XScrollArea* sb;
        XWidget* small;
        XScrollBar* vsb3;
        XWindow* xw3;
        XWidget_init(&top3, NULL, 0);
        XWidget_setGeometry(&top3, 0, 0, 400, 300);
        sb = XScrollArea_create(&top3, 0);
        XWidget_setGeometry((XWidget*)sb, 0, 0, 200, 200);
        small = XWidget_create(NULL, 0);
        XWidget_setGeometry(small, 0, 0, 200, 100);
        XScrollArea_setWidget(sb, small);
        XWidget_show(&top3);
        XWidget_show((XWidget*)sb);
        xw3 = (XWindow*)XWidget_windowHandle(&top3);
        vsb3 = XAbstractScrollArea_verticalScrollBar(
            (XAbstractScrollArea*)sb);
        sp_expect(XScrollBar_maximum(vsb3) == 0, "4a 无滚范围就绪");
        ts = 70000;
        sp_step(xw3, XEVENT_TYPE_TOUCH_BEGIN, 24, 100, 180,
                XTOUCHPOINT_STATE_PRESSED, ts);
        {
            int frame;
            for (frame = 0; frame < 4; ++frame) {
                ts += 16;
                sp_step(xw3, XEVENT_TYPE_TOUCH_UPDATE, 24, 100,
                        180 - 25 * (frame + 1),
                        XTOUCHPOINT_STATE_UPDATED, ts);
            }
        }
        sp_step(xw3, XEVENT_TYPE_TOUCH_END, 24, 100, 80,
                XTOUCHPOINT_STATE_RELEASED, ts += 16);
        sp_expect(XScrollBar_value(vsb3) == 0, "4b 不可滚内容：拖动零滚动");
        sp_step(xw3, XEVENT_TYPE_TOUCH_CANCEL, 24, 5, 5,
                XTOUCHPOINT_STATE_RELEASED, ts += 16);
        XClassDeinit(&top3);
    }

    /* ⑤ 开关关闭回落滚轮语义：100px 手指=整格 1 格=+60（量化），无
     *    惯性漂移。 */
    {
        int vBefore;
        XAbstractScrollArea_setTouchScrollEnabled((XAbstractScrollArea*)sa,
                                                  false);
        sp_expect(!XAbstractScrollArea_touchScrollEnabled(
                      (XAbstractScrollArea*)sa),
                  "5a 开关回读 false");
        vBefore = XScrollBar_value(vsb);
        ts = 80000;
        sp_step(xw2, XEVENT_TYPE_TOUCH_BEGIN, 25, 100, 180,
                XTOUCHPOINT_STATE_PRESSED, ts);
        {
            int frame;
            for (frame = 0; frame < 4; ++frame) {
                ts += 16;
                sp_step(xw2, XEVENT_TYPE_TOUCH_UPDATE, 25, 100,
                        180 - 25 * (frame + 1),
                        XTOUCHPOINT_STATE_UPDATED, ts);
            }
            sp_step(xw2, XEVENT_TYPE_TOUCH_END, 25, 100, 80,
                    XTOUCHPOINT_STATE_RELEASED, ts += 16);
        }
        sp_expect(XScrollBar_value(vsb) == vBefore + 60,
                  "5b 关闭：回落滚轮量化滚动（+60 而非 +100）");
        sp_pumpUntil(XDateTime_currentMSecsSinceEpoch() + 200);
        sp_expect(XScrollBar_value(vsb) == vBefore + 60,
                  "5c 关闭：无惯性漂移");
        XAbstractScrollArea_setTouchScrollEnabled((XAbstractScrollArea*)sa,
                                                  true);
    }

    XClassDeinit(&top2);
    fprintf(stderr, "[ScrollTouchProbe] failures=%d\n", sp_failures);
    return sp_failures;
}
