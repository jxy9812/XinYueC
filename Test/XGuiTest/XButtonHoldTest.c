/**
 * @file       XButtonHoldTest.c
 * @brief      按钮"按住"语义回归：按下 → 鼠标移出按钮 → 继续按住 → 松手。
 * @details    [为什么要有这个用例] 现场症状：点动按钮**长按**，鼠标轻微抖动
 *             就被当成松手 ⇒ 点动命令提前释放（电机随即停）。
 *             根因在 XAbstractButton：mouseMove 里"移出按钮即
 *             setDownFromInput(false)"，而 setDownFromInput(false) 会
 *             **立刻发 released**。
 *
 *             Qt 6.8 口径（对标基准）：
 *               · pressed   —— 按下即发；
 *               · released  —— **松手**才发（按住期间拖出按钮不算松手）；
 *               · clicked   —— 松手时鼠标在按钮内才发（拖出后松手不发）。
 *
 *             本用例断言：按住并拖出后，released 计数仍为 0；松手后为 1；
 *             拖出后松手时 clicked 不发（Qt 口径）。
 */
#include "XButtonHoldTest.h"

#include "XAbstractButton.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XObject.h"
#include "XPrintf.h"
#include "XWidget.h"

/* ==================== 计数 ==================== */

static int s_pressedCount;
static int s_releasedCount;
static int s_clickedCount;

static void btn_pressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_pressedCount;
}

static void btn_releasedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_releasedCount;
}

static void btn_clickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_clickedCount;
}

/* ==================== 注入 ==================== */

static void xbht_press(XWidget* w, int x, int y)
{
    XMouseEvent me;
    XPoint p = { x, y };
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

static void xbht_move(XWidget* w, int x, int y)
{
    XMouseEvent me;
    XPoint p = { x, y };
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
                     XMouseButton_LeftButton, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

static void xbht_release(XWidget* w, int x, int y)
{
    XMouseEvent me;
    XPoint p = { x, y };
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

/* ==================== 用例 ==================== */

bool XButtonHoldTest_runAll(void)
{
    XAbstractButton btn;
    XWidget* win;
    int failures = 0;
    int w = 100;
    int h = 30;

    XPrintf("---- 按钮按住语义（Qt 口径）----\n");

    win = XWidget_create(NULL, 0);
    if (!win) {
        XPrintf("[FAIL] 根控件创建失败\n");
        return 1;
    }
    XWidget_setGeometry(win, 0, 0, 400, 200);
    XAbstractButton_init(&btn, win, 0);
    XWidget_setGeometry((XWidget*)&btn, 10, 10, w, h);
    XWidget_show((XWidget*)&btn);
    XWidget_show(win);

    s_pressedCount = 0;
    s_releasedCount = 0;
    s_clickedCount = 0;
    XObject_connect_1((XObject*)&btn,
                      XSignal(XAbstractButton_pressed_signal),
                      (XObject*)&btn, btn_pressedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&btn,
                      XSignal(XAbstractButton_released_signal),
                      (XObject*)&btn, btn_releasedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&btn,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&btn, btn_clickedSlot,
                      XConnectionType_Direct);

    /* ① 按下（命中按钮内） */
    xbht_press((XWidget*)&btn, 50, 15);
    XPrintf("[INFO] 按下后 pressed=%d released=%d clicked=%d\n",
            s_pressedCount, s_releasedCount, s_clickedCount);
    if (s_pressedCount != 1) {
        XPrintf("[FAIL] 按下应发 pressed 一次（实发 %d 次）\n", s_pressedCount);
        ++failures;
    }
    if (s_releasedCount != 0) {
        XPrintf("[FAIL] 按下不应发 released（实发 %d 次）\n", s_releasedCount);
        ++failures;
    }

    /* ② 鼠标移出按钮（按住状态）——Qt 口径：保持按下，不发 released */
    xbht_move((XWidget*)&btn, 300, 150);
    xbht_move((XWidget*)&btn, 320, 160);
    XPrintf("[INFO] 移出后 pressed=%d released=%d clicked=%d\n",
            s_pressedCount, s_releasedCount, s_clickedCount);
    if (s_releasedCount != 0) {
        XPrintf("[FAIL] 移出按钮不应发 released（Qt 口径：松手才发）——"
                "实发 %d 次 ⇒ 长按点动会被提前释放\n", s_releasedCount);
        ++failures;
    }
    /* [现场症状对应] 按钮的**按下态**必须保持——现场报"鼠标还按住时按钮
     * 就恢复未点击状态了"：旧实现在这里把 down 清掉（视觉弹起），并且
     * 顺带发 released ⇒ 变频器点动随即停机。 */
    if (!XAbstractButton_isDown(&btn)) {
        XPrintf("[FAIL] 移出按钮后仍应保持按下态（现场：按钮会自己弹起）\n");
        ++failures;
    }

    /* ③ 在按钮外松手 —— released 应发（真正松手），clicked 不发 */
    xbht_release((XWidget*)&btn, 320, 160);
    XPrintf("[INFO] 松手后 pressed=%d released=%d clicked=%d\n",
            s_pressedCount, s_releasedCount, s_clickedCount);
    if (s_releasedCount != 1) {
        XPrintf("[FAIL] 松手应发 released 一次（实发 %d 次）\n", s_releasedCount);
        ++failures;
    }
    if (s_clickedCount != 0) {
        XPrintf("[FAIL] 按钮外松手不应发 clicked（Qt 口径）——实发 %d 次\n",
                s_clickedCount);
        ++failures;
    }

    /* ④ 按钮内按下+松手 ⇒ pressed/released/clicked 各一次 */
    s_pressedCount = 0;
    s_releasedCount = 0;
    s_clickedCount = 0;
    xbht_press((XWidget*)&btn, 50, 15);
    xbht_release((XWidget*)&btn, 50, 15);
    if (s_pressedCount != 1 || s_releasedCount != 1 || s_clickedCount != 1) {
        XPrintf("[FAIL] 按钮内点一次应 pressed/released/clicked 各 1"
                "（实 %d/%d/%d）\n", s_pressedCount, s_releasedCount,
                s_clickedCount);
        ++failures;
    }

    XClassDelete((XClass*)win);
    XPrintf("==== 按钮按住语义：%s（%d 项失败）====\n",
            failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0;
}
