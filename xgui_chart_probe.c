/******************************************************************************
 * @file       xgui_chart_probe.c
 * @brief      图表时间轴交互探针：断言滚轮/拖拽只作用于横轴、缩放以光标
 *             为锚、绘图区已发布到模型、视口变更信号载荷正确。
 * @details    背景（2026-11-03 现场反馈）：变频器页趋势图「鼠标滚动缩放的
 *             是纵坐标」；且放大后拖拽不能左右移动。
 *             根因有三：
 *             1) 普通滚轮走 XChart_scroll（**域滚动**），把 Y 域也平移了；
 *                页面纵轴自动量程每拍又把 Y 拉回 ⇒ 净效果只剩纵轴在变。
 *             2) XChartView 布局算出的绘图区**从未发布到模型**
 *                （chart->m_plotArea 恒空）⇒ 鼠标/手势平移分支判
 *                plot.width>1.0 恒不成立 ⇒ 拖拽平移整条路径是死的；
 *                即便成立，位移量还被「域宽再乘一次」二次放大。
 *             3) 页面每拍重写 X 轴（跟随最新点），用户平移被立刻回滚。
 *             本探针固化框架侧口径：时间轴模式下滚轮=缩 X（光标锚点）、
 *             拖拽=只平移 X、Y 域全程不动；缺省模式行为保持不变（滚轮仍
 *             是双轴域滚动）。
 * @note       自带 main 的独立探针（与 xgui_hover_probe/xgui_testmenu_sweep
 *             同口径）：整包回归 XGuiRegression_Test 当前被并发会话在途
 *             改动（XTableView API 改名）卡在链接期，本探针脱离它单跑。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XWidget.h"
#include "XChart.h"
#include "XChartView.h"
#include "XValueAxis.h"
#include "XWindowEvent.h"
#include "XEvent.h"
#include "XMemory.h"

/* 视口变更信号收料。 */
static int g_vpCount = 0;      /**< viewportChanged 累计次数。 */
static int g_vpReason = -1;    /**< 最近一次 viewportChanged 载荷。 */
static int g_failCount = 0;    /**< 断言失败计数。 */

static void chart_probe_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[CHART-PROBE] FAIL: %s\n", what ? what : "");
        ++g_failCount;
    }
}

/** @brief viewportChanged 槽：记录载荷（connect_2 口径：sender=发信者）。 */
static void chart_probe_viewportSlot(XObject* sender, XVarList* args)
{
    int reason = -1;
    (void)sender;
    if (args) {
        XVarList_start(args);
        reason = XVarList_arg(args, int);
    }
    g_vpReason = reason;
    ++g_vpCount;
}

/** @brief 注入滚轮事件（控件局部坐标）。 */
static void chart_probe_wheel(XChartView* cv, int x, int y, int dy)
{
    XWheelEvent* we;
    XPoint pos;
    XPoint delta;
    pos.x = (short)x;
    pos.y = (short)y;
    delta.x = 0;
    delta.y = (short)dy;
    we = XWheelEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_WHEEL,
                               &pos, NULL, &delta, XMouseButton_NoButton,
                               XKeyboardModifier_NoModifier);
    if (!we) return;
    XCoreApplication_sendEvent((XObject*)cv, (XEvent*)we);
    XClassDelete((XEvent*)we);
}

/** @brief 注入鼠标事件（控件局部坐标）。 */
static void chart_probe_mouse(XChartView* cv, XEventType type, int x, int y)
{
    XMouseEvent* me;
    XPoint pos;
    pos.x = (short)x;
    pos.y = (short)y;
    me = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type,
                               XMouseButton_LeftButton,
                               XKeyboardModifier_NoModifier, pos);
    if (!me) return;
    XCoreApplication_sendEvent((XObject*)cv, (XEvent*)me);
    XClassDelete((XEvent*)me);
}

/**
 * @brief 时间轴模式用例：绘图区发布 + 滚轮缩 X（光标锚点）+ 拖拽平移 X
 *        + 视口变更信号。
 * @return 失败计数。
 */
static int chart_probe_timeAxis(void)
{
    XChartView cv;
    XChart* chart;
    XValueAxis* ax;
    XValueAxis* ay;
    XRectF area;
    double x0;
    double x1;
    double y0;
    double y1;
    int fails = 0;
    int signals;

    XChartView_init(&cv, NULL, 0);    XWidget_resize((XWidget*)&cv, 400, 200);
    chart = XChartView_chart(&cv);
    if (!chart) {
        fprintf(stderr, "[CHART-PROBE] FAIL: no chart\n");
        return 1;
    }
    XChart_createDefaultAxes(chart);
    ax = XChart_axisX(chart);
    ay = XChart_axisY(chart);
    XValueAxis_setRange(ax, 0.0, 30.0);
    XValueAxis_setRange(ay, 0.0, 60.0);
    XChartView_setTimeAxisMode(&cv, true);
    XChartView_setPanEnabled(&cv, true);
    XObject_connect_2((XObject*)&cv,
                      (size_t)XChartView_viewportChanged_signal(&cv, 0),
                      chart_probe_viewportSlot);

    /* 1. 绘图区必须已发布到模型（拖拽平移与光标锚点换算的前提）。 */
    area = XChart_plotArea(chart);
    chart_probe_expect(area.width > 1.0 && area.height > 1.0,
                       "绘图区已发布到模型（plotArea 非空）");
    {
        double vx = 0.0;
        double vy = 0.0;
        XChart_mapToValue(chart, (float)area.x, (float)area.y, &vx, &vy);
        chart_probe_expect(vx > -0.001 && vx < 0.001,
                           "mapToValue 左缘映射到 X 域下界");
        chart_probe_expect(vy > 59.9 && vy < 60.1,
                           "mapToValue 顶缘映射到 Y 域上界");
    }

    /* 2. 滚轮向上（放大）：横轴变窄、以光标为锚；纵轴不动。 */
    g_vpCount = 0;
    g_vpReason = -1;
    chart_probe_wheel(&cv, 218, 100, 120);
    x0 = XValueAxis_min(ax);
    x1 = XValueAxis_max(ax);
    y0 = XValueAxis_min(ay);
    y1 = XValueAxis_max(ay);
    chart_probe_expect(x1 - x0 < 30.0 && x1 - x0 > 23.0,
                       "滚轮放大：横轴变窄（30 → 24）");
    chart_probe_expect(x0 > 2.5 && x0 < 3.5,
                       "滚轮放大：光标处时间值保持原位（锚点不漂移）");
    chart_probe_expect(y0 == 0.0 && y1 == 60.0, "滚轮不动纵轴");
    chart_probe_expect(g_vpReason == XChartView_ViewportChange_Zoom,
                       "viewportChanged 载荷=缩放");

    /* 3. 滚轮向下（缩小）：横轴变宽回原值。 */
    chart_probe_wheel(&cv, 218, 100, -120);
    x0 = XValueAxis_min(ax);
    x1 = XValueAxis_max(ax);
    chart_probe_expect(x1 - x0 > 29.0, "滚轮缩小：横轴变宽");

    /* 4. 拖拽平移：只平移横轴，位移量＝像素比例（1:1 跟手），纵轴不动。 */
    signals = g_vpCount;
    g_vpCount = 0;
    g_vpReason = -1;
    chart_probe_mouse(&cv, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 218, 100);
    chart_probe_mouse(&cv, XEVENT_TYPE_MOUSE_MOVE, 118, 100);
    x0 = XValueAxis_min(ax);
    x1 = XValueAxis_max(ax);
    y0 = XValueAxis_min(ay);
    y1 = XValueAxis_max(ay);
    chart_probe_expect(x0 > 8.0 && x0 < 9.6,
                       "拖拽：横轴按像素比例平移（100px/340px×30s≈8.8s）");
    chart_probe_expect(y0 == 0.0 && y1 == 60.0, "拖拽不动纵轴");
    chart_probe_expect(g_vpReason == XChartView_ViewportChange_Pan,
                       "viewportChanged 载荷=平移");
    chart_probe_expect(g_vpCount == 1, "一次拖拽只发一次平移信号");
    chart_probe_mouse(&cv, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 118, 100);
    (void)signals;

    /* 5. 缺省模式（时间轴模式关闭）行为保持：普通滚轮仍是双轴域滚动。 */
    XChartView_setTimeAxisMode(&cv, false);
    XValueAxis_setRange(ax, 0.0, 30.0);
    XValueAxis_setRange(ay, 0.0, 60.0);
    chart_probe_wheel(&cv, 218, 100, 120);
    x0 = XValueAxis_min(ax);
    x1 = XValueAxis_max(ax);
    y0 = XValueAxis_min(ay);
    y1 = XValueAxis_max(ay);
    chart_probe_expect(x0 == 0.0 && x1 == 30.0,
                       "缺省模式：普通滚轮不缩放横轴（旧域滚动语义保持）");
    chart_probe_expect(y1 - y0 == 60.0 && y0 > 0.0,
                       "缺省模式：普通滚轮平移纵轴（旧域滚动语义保持，非缩放）");
    fails = g_failCount;
    return fails;
}

int main(int argc, char** argv)
{
    int rc = 0;
    (void)argc;
    (void)argv;
    g_failCount = 0;
    if (chart_probe_timeAxis() != 0) rc = 1;
    if (rc == 0) fprintf(stderr, "[CHART-PROBE] PASS\n");
    else fprintf(stderr, "[CHART-PROBE] FAIL (%d)\n", g_failCount);
    return rc;
}
