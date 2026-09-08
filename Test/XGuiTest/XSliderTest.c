/**
 * @file       XSliderTest.c
 * @brief      XSlider 滑块控件回归测试（对齐 Qt 6.8 QSlider/QAbstractSlider）。
 * @details    覆盖：默认状态与继承结构、setValue 钳位 + valueChanged、
 *             setRange/setMinimum/setMaximum 联动收敛 + rangeChanged、
 *             单步/翻页步进、hasTracking、setSliderDown/isSliderDown
 *             （sliderPressed/sliderReleased）、sliderPosition/
 *             setSliderPosition（tracking 开关两种提交语义）、
 *             triggerAction 全部动作 + actionTriggered、stepBy/stepEnabled
 *             虚槽、setRepeatAction/repeatAction、键盘步进（Up/Right/
 *             Down/Left/PageUp/PageDown/Home/End 与 invertedControls
 *             翻转）、滚轮步进（120=1 步、余数累积、invertedControls
 *             翻转）、刻度 API、鼠标交互（handle 拖动/凹槽跳转/释放
 *             提交）、拷贝/移动（XCopy/XMove）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XSliderTest.h"
#include "XSlider.h"
#include "XAbstractSlider.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include <stdio.h>
#include <string.h>

static int sl_failures = 0;
static void sl_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[SL-FAIL] %s\n", what ? what : "");
        ++sl_failures;
    }
}

/** @brief 向滑块注入一次键盘按下（经事件入口分派，对标 le_key 模式）。 */
static void sl_key(XAbstractSlider* slider, int key)
{
    XKeyEvent ke;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key, 0);
    XObject_event_base((XObject*)slider, (XEvent*)&ke);
}

/** @brief 向滑块注入一次滚轮（垂直角度增量 dy，120=1 步）。 */
static void sl_wheel(XAbstractSlider* slider, int dy)
{
    XWheelEvent we;
    XPoint pos = { 5, 5 };
    XPoint gpos = { 50, 50 };
    XPoint delta = { 0, dy };
    XWheelEvent_init(&we, XEVENT_TYPE_WHEEL, &pos, &gpos, &delta,
                     XMouseButton_NoButton, 0);
    XObject_event_base((XObject*)slider, (XEvent*)&we);
}

/** @brief 向控件注入鼠标按下（左键）。 */
static void sl_mousePress(XWidget* w, int x, int y)
{
    XMouseEvent me;
    XPoint p = { x, y };
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

/** @brief 向控件注入鼠标移动（左键按住）。 */
static void sl_mouseMove(XWidget* w, int x, int y)
{
    XMouseEvent me;
    XPoint p = { x, y };
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
                     XMouseButton_LeftButton, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

/** @brief 向控件注入鼠标释放（左键）。 */
static void sl_mouseRelease(XWidget* w, int x, int y)
{
    XMouseEvent me;
    XPoint p = { x, y };
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

/* ---- 信号探针 ---- */
static int sl_valueChangedCount = 0;
static int sl_lastValue = -1;
static void sl_valueChangedSlot(void* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, v);
    sl_lastValue = v;
    ++sl_valueChangedCount;
}

static int sl_rangeChangedCount = 0;
static int sl_rangeMin = 0;
static int sl_rangeMax = 0;
static void sl_rangeChangedSlot(void* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_2(args, int, mn, int, mx);
    sl_rangeMin = mn;
    sl_rangeMax = mx;
    ++sl_rangeChangedCount;
}

static int sl_actionCount = 0;
static int sl_lastAction = -1;
static void sl_actionTriggeredSlot(void* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, a);
    sl_lastAction = a;
    ++sl_actionCount;
}

static int sl_movedCount = 0;
static int sl_lastMoved = -1;
static void sl_sliderMovedSlot(void* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, v);
    sl_lastMoved = v;
    ++sl_movedCount;
}

static int sl_pressedCount = 0;
static int sl_releasedCount = 0;
static void sl_pressedSlot(void* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++sl_pressedCount;
}
static void sl_releasedSlot(void* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++sl_releasedCount;
}

bool XSliderTest_runAll(void)
{
    XSlider* s = XSlider_create(NULL, 0);
    XAbstractSlider* base = (XAbstractSlider*)s;
    if (!s) { fprintf(stderr, "XSlider test: create failed\n"); return false; }

    /* 0. 继承结构：m_base 必须是第一个成员，XAbstractSlider/XWidget API
         均可直接作用于 XSlider 对象。 */
    sl_expect((void*)s == (void*)base, "XSlider.m_base 首成员（布局对齐）");

    /* 1. 默认值（对标 QAbstractSlider 默认构造 0..99）。 */
    sl_expect(XAbstractSlider_minimum(base) == 0 &&
              XAbstractSlider_maximum(base) == 99, "默认范围 0..99");
    sl_expect(XAbstractSlider_value(base) == 0, "默认值 0");
    sl_expect(XAbstractSlider_sliderPosition(base) == 0, "默认位置 0");
    sl_expect(XAbstractSlider_orientation(base) ==
              (int)XAbstractSliderOrientation_Horizontal, "默认水平");
    sl_expect(XAbstractSlider_singleStep(base) == 1, "默认单步 1");
    sl_expect(XAbstractSlider_pageStep(base) == 10, "默认翻页 10");
    sl_expect(XAbstractSlider_hasTracking(base), "默认 tracking");
    sl_expect(!XAbstractSlider_isSliderDown(base), "默认非按下");
    sl_expect(!XAbstractSlider_invertedAppearance(base), "默认外观不翻转");
    sl_expect(!XAbstractSlider_invertedControls(base), "默认控制不翻转");
    sl_expect(XSlider_tickPosition(s) == (int)XSliderTickPosition_NoTicks,
              "默认无刻度");
    sl_expect(XSlider_tickInterval(s) == 0, "默认刻度间隔 0");

    /* 2. setValue 钳位 + valueChanged 计数。 */
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_valueChanged_signal(base),
                      sl_valueChangedSlot);
    XAbstractSlider_setValue(base, 50);
    sl_expect(XAbstractSlider_value(base) == 50 &&
              XAbstractSlider_sliderPosition(base) == 50 &&
              sl_valueChangedCount == 1 && sl_lastValue == 50,
              "setValue(50) 同步位置并发射一次 valueChanged");
    XAbstractSlider_setValue(base, 50); /* 同值不发射 */
    sl_expect(sl_valueChangedCount == 1, "同值不重复发射");
    XAbstractSlider_setValue(base, 200); /* 越界钳位到 99 */
    sl_expect(XAbstractSlider_value(base) == 99 && sl_lastValue == 99,
              "越界钳位到 max");
    XAbstractSlider_setValue(base, -5);
    sl_expect(XAbstractSlider_value(base) == 0, "负值钳位到 min");

    /* 3. setRange/setMinimum/setMaximum：Qt 联动收敛语义。 */
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_rangeChanged_signal(base),
                      sl_rangeChangedSlot);
    XAbstractSlider_setRange(base, 10, 0);
    /* Qt qMax 语义：min 超过 max 时 max 收敛为 min。 */
    sl_expect(XAbstractSlider_minimum(base) == 10 &&
              XAbstractSlider_maximum(base) == 10,
              "setRange min>max 时 max 收敛为 min");
    sl_rangeChangedCount = 0; /* 上述收敛已发射一次，计数清零后测 0..99。 */
    XAbstractSlider_setRange(base, 0, 99);
    sl_expect(sl_rangeChangedCount == 1 && sl_rangeMin == 0 &&
              sl_rangeMax == 99, "rangeChanged(0,99) 发射一次");
    XAbstractSlider_setRange(base, 0, 99); /* 无变化不发射 */
    sl_expect(sl_rangeChangedCount == 1, "范围无变化不发射 rangeChanged");

    XAbstractSlider_setRange(base, 0, 200);
    XAbstractSlider_setValue(base, 150);
    XAbstractSlider_setMinimum(base, 30);
    sl_expect(XAbstractSlider_minimum(base) == 30 &&
              XAbstractSlider_maximum(base) == 200, "setMinimum 下限生效、上限保持");
    XAbstractSlider_setMaximum(base, 80);
    sl_expect(XAbstractSlider_minimum(base) == 30 &&
              XAbstractSlider_maximum(base) == 80 &&
              XAbstractSlider_value(base) == 80, "setMaximum 上限生效、值钳位");
    XAbstractSlider_setMinimum(base, 300);
    sl_expect(XAbstractSlider_minimum(base) == 300 &&
              XAbstractSlider_maximum(base) == 300, "setMinimum 高于上限时上限抬升");
    XAbstractSlider_setMaximum(base, 100);
    sl_expect(XAbstractSlider_minimum(base) == 100 &&
              XAbstractSlider_maximum(base) == 100, "setMaximum 低于下限时下限下压");
    /* 恢复标准范围 0..99。 */
    XAbstractSlider_setRange(base, 0, 99);
    XAbstractSlider_setValue(base, 0);

    /* 4. 步进字段。 */
    XAbstractSlider_setSingleStep(base, 2);
    sl_expect(XAbstractSlider_singleStep(base) == 2, "setSingleStep 生效");
    XAbstractSlider_setSingleStep(base, -3); /* Qt qAbs 语义 */
    sl_expect(XAbstractSlider_singleStep(base) == 3, "setSingleStep 负值取绝对值");
    XAbstractSlider_setSingleStep(base, 2);
    XAbstractSlider_setPageStep(base, 5);
    sl_expect(XAbstractSlider_pageStep(base) == 5, "setPageStep 生效");
    XAbstractSlider_setPageStep(base, -7); /* Qt qAbs 语义 */
    sl_expect(XAbstractSlider_pageStep(base) == 7, "setPageStep 负值取绝对值");
    XAbstractSlider_setPageStep(base, 5);

    /* 5. tracking / sliderDown 信号。 */
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_sliderPressed_signal(base),
                      sl_pressedSlot);
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_sliderReleased_signal(base),
                      sl_releasedSlot);
    XAbstractSlider_setTracking(base, false);
    sl_expect(!XAbstractSlider_hasTracking(base), "setTracking(false) 生效");
    XAbstractSlider_setTracking(base, true);
    XAbstractSlider_setSliderDown(base, true);
    sl_expect(XAbstractSlider_isSliderDown(base) && sl_pressedCount == 1,
              "setSliderDown(true) 发射 sliderPressed");
    XAbstractSlider_setSliderDown(base, true); /* 同状态不重复发射 */
    sl_expect(sl_pressedCount == 1, "setSliderDown(true) 同状态不重复发射");
    XAbstractSlider_setSliderDown(base, false);
    sl_expect(!XAbstractSlider_isSliderDown(base) && sl_releasedCount == 1,
              "setSliderDown(false) 发射 sliderReleased");

    /* 6. 外观/控制翻转。 */
    XAbstractSlider_setInvertedAppearance(base, true);
    sl_expect(XAbstractSlider_invertedAppearance(base),
              "setInvertedAppearance 生效");
    XAbstractSlider_setInvertedAppearance(base, false);
    XAbstractSlider_setInvertedControls(base, true);
    sl_expect(XAbstractSlider_invertedControls(base),
              "setInvertedControls 生效");
    XAbstractSlider_setInvertedControls(base, false);

    /* 7. 刻度 API。 */
    XSlider_setTickPosition(s, (int)XSliderTickPosition_TicksBelow);
    sl_expect(XSlider_tickPosition(s) == (int)XSliderTickPosition_TicksBelow,
              "setTickPosition 生效");
    XSlider_setTickPosition(s, (int)XSliderTickPosition_TicksBothSides);
    sl_expect(XSlider_tickPosition(s) ==
              (int)XSliderTickPosition_TicksBothSides, "TicksBothSides 生效");
    XSlider_setTickPosition(s, (int)XSliderTickPosition_NoTicks);
    XSlider_setTickInterval(s, 10);
    sl_expect(XSlider_tickInterval(s) == 10, "setTickInterval 生效");
    XSlider_setTickInterval(s, -5); /* Qt qMax(0, ts) 语义 */
    sl_expect(XSlider_tickInterval(s) == 0, "setTickInterval 负值按 0");
    XSlider_setTickInterval(s, 10);

    /* 8. sliderPosition/setSliderPosition：tracking 开关两种语义。 */
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_sliderMoved_signal(base),
                      sl_sliderMovedSlot);
    XAbstractSlider_setValue(base, 40);
    XAbstractSlider_setSliderPosition(base, 40);
    sl_expect(XAbstractSlider_sliderPosition(base) == 40 &&
              XAbstractSlider_value(base) == 40,
              "tracking=true 时 setSliderPosition 提交到值");
    /* tracking=false：位置与值分离，按下拖动发 sliderMoved，释放提交。 */
    XAbstractSlider_setTracking(base, false);
    XAbstractSlider_setSliderPosition(base, 25);
    sl_expect(XAbstractSlider_sliderPosition(base) == 25 &&
              XAbstractSlider_value(base) == 40,
              "tracking=false 时 setSliderPosition 只动位置");
    sl_movedCount = 0;
    XAbstractSlider_setSliderDown(base, true);
    XAbstractSlider_setSliderPosition(base, 30);
    sl_expect(XAbstractSlider_sliderPosition(base) == 30 &&
              XAbstractSlider_value(base) == 40 &&
              sl_movedCount == 1 && sl_lastMoved == 30,
              "按下拖动发 sliderMoved(30)，值不动");
    XAbstractSlider_setSliderDown(base, false);
    sl_expect(XAbstractSlider_value(base) == 30,
              "释放时位置提交到值");
    XAbstractSlider_setTracking(base, true);
    XAbstractSlider_setSliderPosition(base, 50);
    sl_expect(XAbstractSlider_value(base) == 50, "tracking=true 恢复");

    /* 9. triggerAction：全部动作 + actionTriggered。 */
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_actionTriggered_signal(base),
                      sl_actionTriggeredSlot);
    sl_actionCount = 0;
    XAbstractSlider_setSingleStep(base, 2);
    XAbstractSlider_setPageStep(base, 5);
    XAbstractSlider_setValue(base, 30);
    XAbstractSlider_triggerAction(base,
                                  XAbstractSliderSliderAction_SingleStepAdd);
    sl_expect(XAbstractSlider_value(base) == 32 && sl_lastAction == 1,
              "SingleStepAdd +1 步");
    XAbstractSlider_triggerAction(base,
                                  XAbstractSliderSliderAction_SingleStepSub);
    sl_expect(XAbstractSlider_value(base) == 30, "SingleStepSub -1 步");
    XAbstractSlider_triggerAction(base,
                                  XAbstractSliderSliderAction_PageStepAdd);
    sl_expect(XAbstractSlider_value(base) == 35, "PageStepAdd +1 页");
    XAbstractSlider_triggerAction(base,
                                  XAbstractSliderSliderAction_PageStepSub);
    sl_expect(XAbstractSlider_value(base) == 30, "PageStepSub -1 页");
    XAbstractSlider_triggerAction(base,
                                  XAbstractSliderSliderAction_ToMinimum);
    sl_expect(XAbstractSlider_value(base) == 0, "ToMinimum 到最小");
    XAbstractSlider_triggerAction(base,
                                  XAbstractSliderSliderAction_ToMaximum);
    sl_expect(XAbstractSlider_value(base) == 99, "ToMaximum 到最大");
    XAbstractSlider_setValue(base, 98);
    XAbstractSlider_triggerAction(base,
                                  XAbstractSliderSliderAction_SingleStepAdd);
    sl_expect(XAbstractSlider_value(base) == 99, "边界单步钳位到 max");
    XAbstractSlider_setValue(base, 1);
    XAbstractSlider_triggerAction(base,
                                  XAbstractSliderSliderAction_SingleStepSub);
    sl_expect(XAbstractSlider_value(base) == 0, "边界单步钳位到 min");
    /* SliderMove：提交位置到值。 */
    XAbstractSlider_setTracking(base, false);
    XAbstractSlider_setValue(base, 20);
    XAbstractSlider_setSliderPosition(base, 44); /* 只动位置 */
    XAbstractSlider_triggerAction(base, XAbstractSliderSliderAction_Move);
    sl_expect(XAbstractSlider_value(base) == 44 && sl_lastAction == 7,
              "SliderMove 提交位置到值");
    XAbstractSlider_setTracking(base, true);
    sl_expect(sl_actionCount == 9, "triggerAction 全部发射 actionTriggered");
    XAbstractSlider_setValue(base, 30);

    /* 10. setRepeatAction/repeatAction（存字段）。 */
    XAbstractSlider_setRepeatAction(base,
                                    XAbstractSliderSliderAction_PageStepAdd,
                                    500, 50);
    sl_expect(XAbstractSlider_repeatAction(base) ==
              (int)XAbstractSliderSliderAction_PageStepAdd,
              "setRepeatAction 存动作字段");
    XAbstractSlider_setRepeatAction(base,
                                    XAbstractSliderSliderAction_NoAction, 0, 0);
    sl_expect(XAbstractSlider_repeatAction(base) ==
              (int)XAbstractSliderSliderAction_NoAction,
              "setRepeatAction(NoAction) 清除");

    /* 11. 键盘步进（keyPressEvent 经事件入口分发）。 */
    XAbstractSlider_setSingleStep(base, 1);
    XAbstractSlider_setPageStep(base, 5);
    XAbstractSlider_setValue(base, 50);
    sl_key(base, XKey_Up);
    sl_expect(XAbstractSlider_value(base) == 51, "Up 单步增");
    sl_key(base, XKey_Right);
    sl_expect(XAbstractSlider_value(base) == 52, "Right 单步增");
    sl_key(base, XKey_Down);
    sl_expect(XAbstractSlider_value(base) == 51, "Down 单步减");
    sl_key(base, XKey_Left);
    sl_expect(XAbstractSlider_value(base) == 50, "Left 单步减");
    sl_key(base, XKey_PageUp);
    sl_expect(XAbstractSlider_value(base) == 55, "PageUp 页增");
    sl_key(base, XKey_PageDown);
    sl_expect(XAbstractSlider_value(base) == 50, "PageDown 页减");
    sl_key(base, XKey_Home);
    sl_expect(XAbstractSlider_value(base) == 0, "Home 到最小");
    sl_key(base, XKey_End);
    sl_expect(XAbstractSlider_value(base) == 99, "End 到最大");

    /* 11b. invertedControls 翻转键盘方向。 */
    XAbstractSlider_setInvertedControls(base, true);
    XAbstractSlider_setValue(base, 50);
    sl_key(base, XKey_Up);
    sl_expect(XAbstractSlider_value(base) == 49, "invertedControls Up 单步减");
    sl_key(base, XKey_Down);
    sl_expect(XAbstractSlider_value(base) == 50, "invertedControls Down 单步增");
    sl_key(base, XKey_PageUp);
    sl_expect(XAbstractSlider_value(base) == 45, "invertedControls PageUp 页减");
    sl_key(base, XKey_PageDown);
    sl_expect(XAbstractSlider_value(base) == 50, "invertedControls PageDown 页增");
    XAbstractSlider_setInvertedControls(base, false);

    /* 12. 滚轮步进（120 角度=1 单步，余数累积）。 */
    XAbstractSlider_setValue(base, 50);
    sl_wheel(base, 120);
    sl_expect(XAbstractSlider_value(base) == 51, "滚轮 +120 = 1 步");
    sl_wheel(base, 240);
    sl_expect(XAbstractSlider_value(base) == 53, "滚轮 +240 = 2 步");
    sl_wheel(base, -60); /* 余数 -60，不足一步 */
    sl_expect(XAbstractSlider_value(base) == 53, "滚轮 -60 余数累积");
    sl_wheel(base, -60); /* 余数 -120 → -1 步 */
    sl_expect(XAbstractSlider_value(base) == 52, "滚轮余数累积成步");
    /* invertedControls 翻转滚轮方向。 */
    XAbstractSlider_setInvertedControls(base, true);
    XAbstractSlider_setValue(base, 50);
    sl_wheel(base, 120);
    sl_expect(XAbstractSlider_value(base) == 49, "invertedControls 滚轮反向");
    XAbstractSlider_setInvertedControls(base, false);

    /* 13. stepBy/stepEnabled 虚槽。 */
    XAbstractSlider_setValue(base, 30);
    XAbstractSlider_stepBy_base(base, 5);
    sl_expect(XAbstractSlider_value(base) == 35, "stepBy(+5) 步进");
    XAbstractSlider_stepBy_base(base, -10);
    sl_expect(XAbstractSlider_value(base) == 25, "stepBy(-10) 步进");
    XAbstractSlider_setValue(base, 99);
    XAbstractSlider_stepBy_base(base, 10);
    sl_expect(XAbstractSlider_value(base) == 99, "stepBy 越界钳位到 max");
    XAbstractSlider_setValue(base, 0);
    sl_expect(XAbstractSlider_stepEnabled_base(base) ==
              (int)XAbstractSliderStepEnabledFlag_StepUpEnabled,
              "stepEnabled 在最小仅可增");
    XAbstractSlider_setValue(base, 99);
    sl_expect(XAbstractSlider_stepEnabled_base(base) ==
              (int)XAbstractSliderStepEnabledFlag_StepDownEnabled,
              "stepEnabled 在最大仅可减");
    XAbstractSlider_setValue(base, 50);
    sl_expect(XAbstractSlider_stepEnabled_base(base) ==
              ((int)XAbstractSliderStepEnabledFlag_StepUpEnabled |
               (int)XAbstractSliderStepEnabledFlag_StepDownEnabled),
              "stepEnabled 在中间双向可步进");

    /* 14. 鼠标交互（子控件带几何，对标 QSlider 鼠标语义）。 */
    {
        XWidget* host = XWidget_create(NULL, 0);
        XSlider* ms = XSlider_create(host, 0);
        XAbstractSlider* mb = (XAbstractSlider*)ms;
        XWidget_setGeometry((XWidget*)host, 0, 0, 300, 100);
        XWidget_setGeometry((XWidget*)ms, 0, 0, 200, 30);
        /* 值域 0..99；handle 宽 12，value=0 时 handle 中心 x=6。
           点击 x=106 处换算值 = (106-6)*99/188 = 52.6 → 52。 */

        /* A. 点击 handle → 进入拖动 + sliderPressed。 */
        sl_mousePress((XWidget*)ms, 6, 10);
        sl_expect(XAbstractSlider_isSliderDown(mb), "按下 handle 进入拖动");
        sl_mouseMove((XWidget*)ms, 106, 10);
        sl_expect(XAbstractSlider_sliderPosition(mb) == 52 &&
                  XAbstractSlider_value(mb) == 52,
                  "拖动实时提交值（tracking=true）");
        sl_mouseRelease((XWidget*)ms, 106, 10);
        sl_expect(!XAbstractSlider_isSliderDown(mb), "释放退出拖动");

        /* B. 点击 handle 外凹槽 → handle 跳到点击处。 */
        XAbstractSlider_setValue(mb, 0);
        sl_mousePress((XWidget*)ms, 106, 10);
        sl_expect(XAbstractSlider_value(mb) == 52,
                  "凹槽点击 handle 跳到点击处");
        sl_mouseRelease((XWidget*)ms, 106, 10);

        /* C. tracking=false：拖动只动位置，释放提交。 */
        XAbstractSlider_setTracking(mb, false);
        XAbstractSlider_setValue(mb, 0);
        sl_mousePress((XWidget*)ms, 6, 10);
        sl_mouseMove((XWidget*)ms, 106, 10);
        sl_expect(XAbstractSlider_sliderPosition(mb) == 52 &&
                  XAbstractSlider_value(mb) == 0,
                  "tracking=false 拖动只动位置");
        sl_mouseRelease((XWidget*)ms, 106, 10);
        sl_expect(XAbstractSlider_value(mb) == 52, "释放提交位置到值");
        XAbstractSlider_setTracking(mb, true);

        XSlider_delete_base(ms);
        XWidget_delete_base(host);
    }

    /* 15. 拷贝/移动（XCopy/XMove 多态分派）。 */
    {
        XSlider* c = XSlider_create(NULL, 0);
        XAbstractSlider_setValue((XAbstractSlider*)c, 42);
        XSlider_setTickPosition(c, (int)XSliderTickPosition_TicksBothSides);
        XSlider_setTickInterval(c, 7);
        XCopy(s, c);
        sl_expect(XAbstractSlider_value(base) == 42, "XCopy 复制值");
        sl_expect(XSlider_tickPosition(s) ==
                  (int)XSliderTickPosition_TicksBothSides, "XCopy 复制刻度位置");
        sl_expect(XSlider_tickInterval(s) == 7, "XCopy 复制刻度间隔");
        XAbstractSlider_setValue((XAbstractSlider*)c, 77);
        XMove(s, c);
        sl_expect(XAbstractSlider_value(base) == 77, "XMove 转移值");
        sl_expect(XAbstractSlider_value((XAbstractSlider*)c) == 0,
                  "XMove 源对象归默认值");
        XSlider_delete_base(c);
    }

    XSlider_delete_base(s);

    {
        int failures = sl_failures;
        sl_failures = 0;
        sl_valueChangedCount = 0;
        sl_lastValue = -1;
        sl_rangeChangedCount = 0;
        sl_actionCount = 0;
        sl_movedCount = 0;
        sl_pressedCount = 0;
        sl_releasedCount = 0;
        if (failures == 0) {
            fprintf(stderr, "XSlider test: PASS\n");
            return true;
        }
        fprintf(stderr, "XSlider test: %d assertion(s) failed\n", failures);
        return false;
    }
}
