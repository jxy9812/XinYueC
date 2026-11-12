/**
 * @file       XSplashScreenAlignTest.c
 * @brief      XSplashScreen × Qt 6.8 QSplashScreen 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qsplashscreen.h
 *             与 Qt 6.8 源码 src/widgets/widgets/qsplashscreen.cpp
 *             （jsdelivr v6.8.0 原文）。关键行为用本机 Qt 6.8.0
 *             （QT_QPA_PLATFORM=offscreen）探针实测：
 *             - 构造 windowFlags 恒含 SplashScreen 类型位 + Frameless：
 *               `QWidget(..., Qt::SplashScreen | Qt::FramelessWindowHint | f)`
 *               （qsplashscreen.cpp:99-103），实测默认 0x80f
 *               （SplashScreen|Window|FramelessWindowHint）——即启动画面
 *               恒为无边框、置顶、无任务栏窗口；
 *             - clearMessage 无条件发 messageChanged("")（Qt 不做空串判定）；
 *             - finish(widget) 先 createWinId + waitForWidgetMapped 再
 *               **无条件 close()**（qsplashscreen.cpp:236-243）：实测
 *               finish(未显示 widget) 返回后 isVisible()==false；等待循环
 *               以 1000ms 为上限（qsplashscreen.cpp:190-215）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XSPLASHSCREEN_ON

#include "XSplashScreen.h"
#include "XAlignment.h"
#include "XWidget.h"
#include "XObject.h"
#include "XClass.h"
#include "XVarList.h"
#include "XString.h"
#include "XDateTime.h"
#include "XEvent.h"
#include "XEventLoop.h"
#include "XThread.h" /* XThread_msleep（定时器到期等待）。 */
#include "XMemory.h"

/* ==================== 信号采样 ==================== */

static int g_ssMessageChanged;
static char g_ssLastMessage[64];

static void xssTestReset(void)
{
    g_ssMessageChanged = 0;
    g_ssLastMessage[0] = '\0';
}

static void xssTestMessageSlot(XObject* receiver, XVarList* args)
{
    const char* utf8;
    (void)receiver;
    XVarList_args_1(args, XString*, text);
    ++g_ssMessageChanged;
    utf8 = text ? XString_toUtf8((const XString*)text) : NULL;
    if (utf8) {
        size_t n = strlen(utf8);
        if (n >= sizeof(g_ssLastMessage)) n = sizeof(g_ssLastMessage) - 1;
        memcpy(g_ssLastMessage, utf8, n);
        g_ssLastMessage[n] = '\0';
    } else {
        g_ssLastMessage[0] = '\0';
    }
}

/* ==================== ① 窗口类型：SplashScreen + Frameless（置顶） ===== */

XQ_TEST(XSplashScreen, windowFlagsAreSplashFrameless)
{
    XSplashScreen* sp = XSplashScreen_create(NULL, 0);
    XQ_REQUIRE(sp != NULL);

    /* qsplashscreen.cpp:99-103：类型位与无边框位无条件并入。 */
    {
        XWidgetFlags flags = XWidget_windowFlags((XWidget*)sp);
        XQ_EQ_I64((long long)(flags & XWindowType_TypeMask),
                  (long long)XWindowType_SplashScreen);
        XQ_CHECK((flags & (XWidgetFlags)XWindowType_FramelessWindowHint) != 0);
    }
    /* 启动画面是顶层窗口（无父）。 */
    XQ_CHECK(XWidget_isWindow((XWidget*)sp));

    /* 调用方自带的风格位保留（Qt 的 `| f`）。 */
    {
        XSplashScreen* sp2 = XSplashScreen_create(
            NULL, (XWidgetFlags)XWindowType_WindowStaysOnBottomHint);
        XQ_REQUIRE(sp2 != NULL);
        XQ_CHECK((XWidget_windowFlags((XWidget*)sp2) &
                  (XWidgetFlags)XWindowType_WindowStaysOnBottomHint) != 0);
        XQ_EQ_I64((long long)(XWidget_windowFlags((XWidget*)sp2) &
                              XWindowType_TypeMask),
                  (long long)XWindowType_SplashScreen);
        XClassDelete((XClass*)sp2);
    }

    /* 默认几何与空状态（本库缺省 400x300，Qt 无图时为 0x0——已登记差异）。 */
    XQ_EQ_I64(XWidget_width((XWidget*)sp), 400);
    XQ_EQ_I64(XWidget_height((XWidget*)sp), 300);
    XQ_EQ_STR(XSplashScreen_message(sp), "");
    XQ_EQ_STR(XSplashScreen_message(NULL), "");
#if XPIXMAP_ON
    XQ_CHECK(XSplashScreen_pixmap(sp) == NULL);
    XQ_CHECK(XSplashScreen_pixmap(NULL) == NULL);
#endif

    XClassDelete((XClass*)sp);
}

/* ==================== ② 消息文本 + messageChanged 发射时机 ============= */

XQ_TEST(XSplashScreen, messageRoundTripAndSignal)
{
    XSplashScreen* sp = XSplashScreen_create(NULL, 0);
    XQ_REQUIRE(sp != NULL);
    xssTestReset();
    XObject_connect((XObject*)sp, XSignal(XSplashScreen_messageChanged_signal),
                      (XObject*)sp, xssTestMessageSlot,
                      XConnectionType_Direct);

    XSplashScreen_showMessage(sp, "loading", XAlignment_Left, 0xFF102030u);
    XQ_EQ_STR(XSplashScreen_message(sp), "loading");
    XQ_EQ_I64(g_ssMessageChanged, 1);
    XQ_EQ_STR(g_ssLastMessage, "loading");

    /* 再次设置同样文本：Qt 不做同值早退（showMessage 恒发）。 */
    XSplashScreen_showMessage(sp, "loading", XAlignment_Center, 0xFF000000u);
    XQ_EQ_I64(g_ssMessageChanged, 2);

    /* clearMessage：无条件清空并发 messageChanged("")（qsplashscreen.cpp
     * QSplashScreen::clearMessage 无空判定）。 */
    XSplashScreen_clearMessage(sp);
    XQ_EQ_STR(XSplashScreen_message(sp), "");
    XQ_EQ_I64(g_ssMessageChanged, 3);
    XQ_EQ_STR(g_ssLastMessage, "");

    /* NULL 文本按空串处理。 */
    XSplashScreen_showMessage(sp, NULL, XAlignment_Left, 0xFF000000u);
    XQ_EQ_STR(XSplashScreen_message(sp), "");
    XQ_EQ_I64(g_ssMessageChanged, 4);

    XClassDelete((XClass*)sp);
}

/* ==================== ③b repaint 泵事件队列（P0-53） ================== */
/* Qt qsplashscreen.cpp:`void QSplashScreen::repaint() { QWidget::repaint();
 * QCoreApplication::processEvents(); }`——showMessage/clearMessage 末尾都
 * 调它，故消息在**无事件循环**运行期也立即上屏。X 此前只 update +
 * flushBackingStore（不泵队列）。探针：0ms 定时器只在 processEvents
 * 里被派遣（dispatcher 的定时器处理在 processEvents 首段）。 */

XCLASS_DEFINE_BEGING(XssTimerProbe)
XCLASS_DEFINE_EXTEND_END(XssTimerProbe, XObject)

typedef struct XssTimerProbe
{
    XObject m_base; /**< 必须为首成员（多态布局）。 */
    int m_timerHits; /**< timerEvent 命中次数。 */
} XssTimerProbe;

static void VXssTimerProbe_timerEvent(XObject* self, XTimerEvent* event)
{
    (void)event;
    ++((XssTimerProbe*)self)->m_timerHits;
    XEvent_accept((XEvent*)event);
}

static XVtable* XssTimerProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XssTimerProbe)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXssTimerProbe_timerEvent);
    return XVTABLE_DEFAULT;
}

static XssTimerProbe* XssTimerProbe_create(void)
{
    XssTimerProbe* probe = (XssTimerProbe*)XMemory_malloc(
        sizeof(XssTimerProbe), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!probe) return NULL;
    XObject_init(&probe->m_base);
    XClassGetVtable(probe) = XssTimerProbe_class_init();
    probe->m_timerHits = 0;
    Set_Class_Memory(probe, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(probe, true);
    return probe;
}

XQ_TEST(XSplashScreen, showMessagePumpsEvents)
{
    /* 定时器要能被 processEvents 派遣，须先让本线程持有事件分发器
     * （XEventLoop 创建即 ensureEventDispatcher；不 exec，只提供派遣
     * 环境——被测路径是被 showMessage 内部的 processEvents 泵到的）。 */
    XEventLoop* loop = XEventLoop_create();
    XSplashScreen* sp = XSplashScreen_create(NULL, 0);
    XssTimerProbe* probe;
    XTimerId tid;
    int afterFirst;
    XQ_REQUIRE(loop != NULL);
    XQ_REQUIRE(sp != NULL);
    probe = XssTimerProbe_create();
    XQ_REQUIRE(probe != NULL);
    /* 5ms 定时器（间隔 0 的定时器在 dispatch 注册层被拒：registerTimer
     * 对 interval==0 早退，故用可观测的小间隔）：起表后先睡过到期点，
     * 之后**只有泵事件队列**才会把 timerEvent 派到探针（旧实现
     * update+flushBackingStore 不泵队列 ⇒ 计数恒 0）。 */
    tid = XObject_startTimer((XObject*)probe, 5, XTimerType_CoarseTimer);
    XQ_CHECK(tid != XTIMER_INVALID_ID);
    XQ_EQ_I64(probe->m_timerHits, 0);
    XThread_msleep(20);

    /* 隐藏状态也必须泵（XWidget_repaint 对隐藏控件是空操作，但
     * processEvents 照跑——启动画面常在 show 之前就发消息）。 */
    XSplashScreen_showMessage(sp, "booting", XAlignment_Left, 0xFF000000u);
    afterFirst = probe->m_timerHits;
    XQ_CHECK_MSG(afterFirst >= 1, "hits=%d", afterFirst);

    /* clearMessage 同样泵。 */
    XSplashScreen_clearMessage(sp);
    XQ_CHECK_MSG(probe->m_timerHits >= afterFirst,
                 "hits=%d first=%d", probe->m_timerHits, afterFirst);

    /* 显式 repaint 也可重复泵（幂等、不崩）。 */
    XSplashScreen_repaint(sp);
    XSplashScreen_repaint(NULL);

    XObject_killTimer((XObject*)probe, tid);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)sp);
    XClassDelete((XClass*)loop);
}

/* ==================== ③ finish 关闭 + repaint 幂等 ==================== */

XQ_TEST(XSplashScreen, finishClosesAndRepaintIsSafe)
{
    XSplashScreen* sp = XSplashScreen_create(NULL, 0);
    XQ_REQUIRE(sp != NULL);

    XWidget_show((XWidget*)sp);
    XQ_CHECK(XWidget_isVisible((XWidget*)sp));

    /* repaint 立即重绘：不改变可见性与几何。 */
    XSplashScreen_repaint(sp);
    XQ_CHECK(XWidget_isVisible((XWidget*)sp));

    /* finish(widget)：Qt 先 createWinId + 阻塞等映射（1000ms 上限）再
     * 无条件 close（qsplashscreen.cpp:236-243 / 190-215）。未显示的
     * widget 永远等不到映射 ⇒ 走满超时后关闭；这里同时验证「真等了」
     * （>=800ms）与「有上限」（<3000ms，避免驱动缺失时挂死）。 */
    {
        XWidget* mainWin = XWidget_create(NULL, 0);
        int64_t t0;
        int64_t elapsed;
        XQ_REQUIRE(mainWin != NULL);
        t0 = XDateTime_currentMSecsSinceEpoch();
        XSplashScreen_finish(sp, mainWin);
        elapsed = XDateTime_currentMSecsSinceEpoch() - t0;
        XQ_CHECK(!XWidget_isVisible((XWidget*)sp));
        XQ_CHECK_MSG(elapsed >= 800, "elapsed=%lldms",
                     (long long)elapsed);
        XQ_CHECK_MSG(elapsed < 3000, "elapsed=%lldms",
                     (long long)elapsed);
        XClassDelete((XClass*)mainWin);
    }

    /* finish(NULL widget)：不进入等待，直接关闭（Qt 同）。 */
    XSplashScreen_finish(NULL, NULL);
    XQ_CHECK(!XWidget_isVisible((XWidget*)sp));

    XClassDelete((XClass*)sp);
}

#endif /* XGUI_ON && XWIDGET_ON && XSPLASHSCREEN_ON */
