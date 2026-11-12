/**
 * @file       XAbstractSliderAlignTest.c
 * @brief      XAbstractSlider × Qt 6.8 QAbstractSlider 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qabstractslider.h
 *               （range/value/singleStep/pageStep/tracking/sliderPosition/
 *               invertedAppearance/invertedControls/triggerAction/
 *               SliderAction/SliderChange）；
 *             - Qt 6.8 源码 qabstractslider.cpp（jsdelivr
 *               qtbase@v6.8.0/src/widgets/widgets/qabstractslider.cpp）：
 *               setRange qMax 收敛（:207-220）、setSteps qAbs（:222-228）、
 *               setSliderDown 信号顺序（:425-441）、setSliderPosition
 *               tracking/blocktracking（:456-469）、setValue 只在值变化
 *               时发 valueChanged（:495-519）、triggerAction 先发
 *               actionTriggered 再 setValue（:579-610）、scrollByDelta
 *               Ctrl/Shift 按整格折算（:668-729）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测（逐条写进断言）：
 *               缺省 0..99/0/1/10、setRange(10,5)→(10,10)、signal 顺序
 *               "PM40,V40,R"、Ctrl 半格滚轮不动作。
 *
 *             覆盖：缺省值、范围收敛、步进绝对值、值钳位与信号次数、
 *             按下/拖动/释放信号序列、tracking=false 的释放提交、
 *             键盘步进与 invertedControls、滚轮（普通/Ctrl 半格/Ctrl 整格/
 *             一页上限）、父子所有权。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON

#include "XAbstractSlider.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XVarList.h"

/* ==================== 信号探针 ==================== */

static int as_valueChangedCount = 0;
static int as_lastValue = -1;
static int as_rangeChangedCount = 0;
static int as_pressedCount = 0;
static int as_releasedCount = 0;
static int as_lastMoved = -1;
static int as_movedCount = 0;
static int as_lastAction = -1;
static int as_actionCount = 0;

static void as_valueChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, v);
    as_lastValue = v;
    ++as_valueChangedCount;
}

static void as_rangeChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_2(args, int, mn, int, mx);
    (void)mn;
    (void)mx;
    ++as_rangeChangedCount;
}

static void as_pressedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++as_pressedCount;
}

static void as_releasedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++as_releasedCount;
}

static void as_movedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, p);
    as_lastMoved = p;
    ++as_movedCount;
}

static void as_actionSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, a);
    as_lastAction = a;
    ++as_actionCount;
}

/** @brief 清空全部探针计数（每个用例开始调用）。 */
static void as_resetProbes(void)
{
    as_valueChangedCount = 0;
    as_lastValue = -1;
    as_rangeChangedCount = 0;
    as_pressedCount = 0;
    as_releasedCount = 0;
    as_lastMoved = -1;
    as_movedCount = 0;
    as_lastAction = -1;
    as_actionCount = 0;
}

/** @brief 向滑块注入一次键盘按下（经事件入口分派）。 */
static void as_key(XAbstractSlider* slider, int key)
{
    XKeyEvent ke;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key, 0);
    XObject_event_base((XObject*)slider, (XEvent*)&ke);
}

/** @brief 向滑块注入一次滚轮；返回事件是否被接受。 */
static bool as_wheelPoint(XAbstractSlider* slider, int dx, int dy,
                          XKeyboardModifiers modifiers)
{
    XWheelEvent we;
    XPoint pos = { 5, 5 };
    XPoint gpos = { 50, 50 };
    XPoint delta = { dx, dy };
    XWheelEvent_init(&we, XEVENT_TYPE_WHEEL, &pos, &gpos, &delta,
                     XMouseButton_NoButton, modifiers);
    XObject_event_base((XObject*)slider, (XEvent*)&we);
    return XEvent_isAccepted((XEvent*)&we);
}

/** @brief 向滑块注入一次竖直滚轮；返回事件是否被接受。 */
static bool as_wheel(XAbstractSlider* slider, int dy,
                     XKeyboardModifiers modifiers)
{
    return as_wheelPoint(slider, 0, dy, modifiers);
}

/* ==================== 用例 ==================== */

/** @brief 缺省值 / 范围收敛 / 步进绝对值 / 值钳位（qabstractslider.cpp:178-228,495-519）。 */
XQ_TEST(XAbstractSlider, defaultsAndRange)
{
    XAbstractSlider* s = XAbstractSlider_create(NULL, 0);
    XQ_REQUIRE(s != NULL);
    as_resetProbes();

    /* Qt 6.8 实测 QSlider 缺省（QAbstractSliderPrivate 构造 :178-193）：
     * min=0 max=99 value=0 position=0 single=1 page=10 tracking=true。 */
    XQ_EQ_I64(XAbstractSlider_minimum(s), 0);
    XQ_EQ_I64(XAbstractSlider_maximum(s), 99);
    XQ_EQ_I64(XAbstractSlider_value(s), 0);
    XQ_EQ_I64(XAbstractSlider_sliderPosition(s), 0);
    XQ_EQ_I64(XAbstractSlider_singleStep(s), 1);
    XQ_EQ_I64(XAbstractSlider_pageStep(s), 10);
    XQ_CHECK(XAbstractSlider_hasTracking(s));
    XQ_CHECK(!XAbstractSlider_isSliderDown(s));
    XQ_CHECK(!XAbstractSlider_invertedAppearance(s));
    XQ_CHECK(!XAbstractSlider_invertedControls(s));
    XQ_EQ_I64(XAbstractSlider_orientation(s),
              (int)XGuiOrientation_Horizontal);

    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_valueChanged_signal(s, 0),
                      as_valueChangedSlot);
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_rangeChanged_signal(s, 0, 0),
                      as_rangeChangedSlot);
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_actionTriggered_signal(s, 0),
                      as_actionSlot);

    /* qabstractslider.cpp:207-220：maximum = qMax(min, max)。
     * 范围变化后 setValue(d->value) 重新钳位：初始 value=0 被抬到 10 ⇒
     * 发一次 valueChanged(10)。Qt 6.8.0 实测 setRange(10,5) 后
     * value/sliderPosition 均为 10、valueChanged 计数 1（last=10）——
     * 该计数是下面 setValue(50) 断言的基线。 */
    XAbstractSlider_setRange(s, 10, 5);
    XQ_EQ_I64(XAbstractSlider_minimum(s), 10);
    XQ_EQ_I64(XAbstractSlider_maximum(s), 10);
    XQ_EQ_I64(XAbstractSlider_value(s), 10);
    XQ_EQ_I64(XAbstractSlider_sliderPosition(s), 10);
    XQ_EQ_I64(as_rangeChangedCount, 1);
    XQ_EQ_I64(as_valueChangedCount, 1);
    XQ_EQ_I64(as_lastValue, 10);

    /* 同范围不重复发 rangeChanged（Qt 首行 oldMin/oldMax 比较，:211-213）。 */
    XAbstractSlider_setRange(s, 10, 10);
    XQ_EQ_I64(as_rangeChangedCount, 1);
    XQ_EQ_I64(as_valueChangedCount, 1);

    /* setValue 经 bound 钳位；同值不重复发 valueChanged
     * （qabstractslider.cpp:498-507 的 emitValueChanged 守卫）。
     * 注意 valueChanged 计数基线是 1：setRange(10,5) 的重钳位已发过一次
     * （Qt 6.8.0 实测 setValue(50) 后累计 2）。 */
    XAbstractSlider_setRange(s, 0, 99);
    XQ_EQ_I64(as_valueChangedCount, 1); /* 重钳位 10→10，值未变。 */
    XAbstractSlider_setValue(s, 50);
    XQ_EQ_I64(XAbstractSlider_value(s), 50);
    XQ_EQ_I64(XAbstractSlider_sliderPosition(s), 50);
    XQ_EQ_I64(as_valueChangedCount, 2);
    XQ_EQ_I64(as_lastValue, 50);
    XAbstractSlider_setValue(s, 50);
    XQ_EQ_I64(as_valueChangedCount, 2);
    XAbstractSlider_setValue(s, 500);
    XQ_EQ_I64(XAbstractSlider_value(s), 99);
    XQ_EQ_I64(as_valueChangedCount, 3); /* 50→99 一次。 */
    XAbstractSlider_setValue(s, -5);
    XQ_EQ_I64(XAbstractSlider_value(s), 0);
    XQ_EQ_I64(as_valueChangedCount, 4); /* 99→0 一次。 */

    /* setSingleStep/setPageStep 取绝对值（qabstractslider.cpp:222-228）。 */
    XAbstractSlider_setSingleStep(s, -3);
    XQ_EQ_I64(XAbstractSlider_singleStep(s), 3);
    XAbstractSlider_setPageStep(s, -7);
    XQ_EQ_I64(XAbstractSlider_pageStep(s), 7);

    /* triggerAction：先发 actionTriggered 再 valueChanged（:579-610；
     * Qt 实测顺序 "A1V"）。 */
    as_resetProbes();
    XAbstractSlider_setValue(s, 10);
    as_resetProbes();
    XAbstractSlider_triggerAction(s,
                                  XAbstractSliderSliderAction_SingleStepAdd);
    XQ_EQ_I64(XAbstractSlider_value(s), 13);
    XQ_EQ_I64(as_actionCount, 1);
    XQ_EQ_I64(as_lastAction, XAbstractSliderSliderAction_SingleStepAdd);
    XQ_EQ_I64(as_valueChangedCount, 1);
    XQ_EQ_I64(as_lastValue, 13);

    XClassDelete((XClass*)s);
}

/** @brief 按下/拖动/释放信号序列（qabstractslider.cpp:425-469）。
 *  @details Qt 6.8 实测：setSliderDown(true) → P；setSliderPosition(40) →
 *           M40,V40；setSliderDown(false) → R（位置已等于值不再提交）；
 *           tracking=false 时拖动只动位置，抬起时才提交
 *           （"PM40,V40,R" 与跟踪版一致，但拖动期无 V）。 */
XQ_TEST(XAbstractSlider, sliderDownSequence)
{
    XAbstractSlider* s = XAbstractSlider_create(NULL, 0);
    XQ_REQUIRE(s != NULL);
    XAbstractSlider_setRange(s, 0, 100);
    XAbstractSlider_setValue(s, 10);
    as_resetProbes();

    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_sliderPressed_signal(s),
                      as_pressedSlot);
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_sliderReleased_signal(s),
                      as_releasedSlot);
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_sliderMoved_signal(s, 0),
                      as_movedSlot);
    XObject_connect_2((XObject*)s,
                      (size_t)XAbstractSlider_valueChanged_signal(s, 0),
                      as_valueChangedSlot);

    XAbstractSlider_setSliderDown(s, true);
    XQ_EQ_I64(as_pressedCount, 1);
    XQ_CHECK(XAbstractSlider_isSliderDown(s));
    XAbstractSlider_setSliderPosition(s, 40);
    XQ_EQ_I64(as_movedCount, 1);
    XQ_EQ_I64(as_lastMoved, 40);
    XQ_EQ_I64(XAbstractSlider_value(s), 40);
    XQ_EQ_I64(as_valueChangedCount, 1);
    XAbstractSlider_setSliderDown(s, false);
    XQ_EQ_I64(as_releasedCount, 1);
    XQ_EQ_I64(as_valueChangedCount, 1);

    /* tracking=false：拖动期只动 position（不发 sliderMoved/valueChanged），
     * 抬起时 setSliderDown(false) 经 SliderMove 提交值。 */
    XAbstractSlider_setTracking(s, false);
    as_resetProbes();
    XAbstractSlider_setSliderDown(s, true);
    XAbstractSlider_setSliderPosition(s, 70);
    XQ_EQ_I64(XAbstractSlider_sliderPosition(s), 70);
    XQ_EQ_I64(XAbstractSlider_value(s), 40);
    XQ_EQ_I64(as_valueChangedCount, 0);
    XQ_EQ_I64(as_movedCount, 1); /* 按下期间 sliderMoved 照发（Qt :466-467）。 */
    XAbstractSlider_setSliderDown(s, false);
    XQ_EQ_I64(XAbstractSlider_value(s), 70);
    XQ_EQ_I64(as_valueChangedCount, 1);

    XClassDelete((XClass*)s);
}

/** @brief 键盘步进与滚轮（含 Ctrl/Shift 按整格折算）。
 *  @details qabstractslider.cpp:748-866（键盘）、:731-744 + :668-729（滚轮）。
 *           Qt 6.8 实测：普通滚轮一格 = wheelScrollLines(3) × singleStep；
 *           Ctrl + 半格（delta=30）→ int(0.25*pageStep)=2? 实测 Qt 取
 *           int(offset*pageStep)=0（pageStep=10）⇒ 不动作；Ctrl + 一格
 *           = 一页（qBound 到 ±pageStep）。 */
XQ_TEST(XAbstractSlider, keyboardAndWheel)
{
    XAbstractSlider* s = XAbstractSlider_create(NULL, 0);
    XQ_REQUIRE(s != NULL);
    XAbstractSlider_setRange(s, 0, 100);
    XAbstractSlider_setSingleStep(s, 1);
    XAbstractSlider_setPageStep(s, 10);
    XAbstractSlider_setValue(s, 50);

    /* 键盘：Left/Right 单步，PageUp/PageDown 翻页，Home/End 到边界
     * （Qt 无方向门控——竖直滑块按 Left 同样步进，实测 -1）。 */
    as_key(s, XKey_Left);
    XQ_EQ_I64(XAbstractSlider_value(s), 49);
    as_key(s, XKey_Right);
    XQ_EQ_I64(XAbstractSlider_value(s), 50);
    as_key(s, XKey_Up);
    XQ_EQ_I64(XAbstractSlider_value(s), 51);
    as_key(s, XKey_Down);
    XQ_EQ_I64(XAbstractSlider_value(s), 50);
    as_key(s, XKey_PageUp);
    XQ_EQ_I64(XAbstractSlider_value(s), 60);
    as_key(s, XKey_PageDown);
    XQ_EQ_I64(XAbstractSlider_value(s), 50);
    as_key(s, XKey_Home);
    XQ_EQ_I64(XAbstractSlider_value(s), 0);
    as_key(s, XKey_End);
    XQ_EQ_I64(XAbstractSlider_value(s), 100);

    /* invertedControls 翻转键盘方向（:748-866 的 invertedControls 分支）。 */
    XAbstractSlider_setInvertedControls(s, true);
    XAbstractSlider_setValue(s, 50);
    as_key(s, XKey_Right);
    XQ_EQ_I64(XAbstractSlider_value(s), 49);
    as_key(s, XKey_PageUp);
    XQ_EQ_I64(XAbstractSlider_value(s), 39);
    XAbstractSlider_setInvertedControls(s, false);

    /* 滚轮：一格（120）= 3 行 × singleStep 1 = +3。 */
    XAbstractSlider_setValue(s, 50);
    XQ_CHECK(as_wheel(s, 120, 0));
    XQ_EQ_I64(XAbstractSlider_value(s), 53);
    /* 反向一格 = -3。 */
    XQ_CHECK(as_wheel(s, -120, 0));
    XQ_EQ_I64(XAbstractSlider_value(s), 50);

    /* 水平份量：水平滑块（本类缺省 Horizontal）dx=+120 ⇒ 值减小 3
     * （scrollByDelta 内部对水平轴取负，qabstractslider.cpp:671-674）。 */
    XAbstractSlider_setValue(s, 50);
    XQ_CHECK(as_wheelPoint(s, 120, 0, 0));
    XQ_EQ_I64(XAbstractSlider_value(s), 47);
    XQ_CHECK(as_wheelPoint(s, -120, 0, 0));
    XQ_EQ_I64(XAbstractSlider_value(s), 50);
    /* 竖直滑块 dx=+120 同样按水平轴取负（Qt wheelEvent 只在 y 非零时
     * 走竖直，否则一律水平）。 */
    XAbstractSlider_setOrientation(s, XGuiOrientation_Vertical);
    XAbstractSlider_setValue(s, 50);
    XQ_CHECK(as_wheelPoint(s, 120, 0, 0));
    XQ_EQ_I64(XAbstractSlider_value(s), 47);
    XAbstractSlider_setOrientation(s, XGuiOrientation_Horizontal);
    XAbstractSlider_setValue(s, 50);

    /* Ctrl + 半格：Qt 按 int(offset*pageStep) 折算 = 0 步 ⇒ 不动作且不吃事件
     * （旧实现无条件翻一页，与 qabstractslider.cpp:682-686 相反）。 */
    XQ_CHECK(!as_wheel(s, 30, XKeyboardModifier_ControlModifier));
    XQ_EQ_I64(XAbstractSlider_value(s), 50);
    /* Ctrl + 一格 = 一页。 */
    XQ_CHECK(as_wheel(s, 120, XKeyboardModifier_ControlModifier));
    XQ_EQ_I64(XAbstractSlider_value(s), 60);
    /* Ctrl + 两格仍夹到一页（qBound(-pageStep, …, pageStep)）。 */
    XQ_CHECK(as_wheel(s, 240, XKeyboardModifier_ControlModifier));
    XQ_EQ_I64(XAbstractSlider_value(s), 70);

    XClassDelete((XClass*)s);
}

/** @brief RTL 键盘分支与方向→sizePolicy 转置
 *         （qabstractslider.cpp:748-866 的 isRightToLeft、:264-277 的
 *         transposed）。 */
XQ_TEST(XAbstractSlider, rtlAndOrientationPolicy)
{
    XAbstractSlider* s = XAbstractSlider_create(NULL, 0);
    XWidgetSizePolicy policy;
    XQ_REQUIRE(s != NULL);
    XAbstractSlider_setRange(s, 0, 100);
    XAbstractSlider_setValue(s, 50);

    /* Qt 的 Left/Right 有 isRightToLeft 分支：RTL 下 Left 增、Right 减；
     * Up/Down/Page 不受布局方向影响。 */
    XWidget_setLayoutDirection((XWidget*)s,
                               XWidgetLayoutDirection_RightToLeft);
    as_key(s, XKey_Left);
    XQ_EQ_I64(XAbstractSlider_value(s), 51);
    as_key(s, XKey_Right);
    XQ_EQ_I64(XAbstractSlider_value(s), 50);
    as_key(s, XKey_PageDown);
    XQ_EQ_I64(XAbstractSlider_value(s), 40);
    XAbstractSlider_setValue(s, 50);
    XWidget_setLayoutDirection((XWidget*)s,
                               XWidgetLayoutDirection_LeftToRight);
    as_key(s, XKey_Left);
    XQ_EQ_I64(XAbstractSlider_value(s), 49);
    as_key(s, XKey_Right);
    XQ_EQ_I64(XAbstractSlider_value(s), 50);

    /* 未显式设置 sizePolicy 时，方向切换转置两轴策略（qabstractslider.cpp:
     * 264-277）。先模拟构造期「setSizePolicy 后清 Own 位」的状态。 */
    XWidget_setSizePolicy((XWidget*)s, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Fixed);
    XWidget_setAttribute((XWidget*)s,
                         XWidgetAttribute_WState_OwnSizePolicy, false);
    XAbstractSlider_setOrientation(s, XGuiOrientation_Vertical);
    policy = XWidget_sizePolicy((XWidget*)s);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    XAbstractSlider_setOrientation(s, XGuiOrientation_Horizontal);
    policy = XWidget_sizePolicy((XWidget*)s);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);

    /* 显式 setSizePolicy 置 OwnSizePolicy 后方向不再转置。 */
    XWidget_setSizePolicy((XWidget*)s, XWidgetSizePolicy_Minimum,
                          XWidgetSizePolicy_Fixed);
    XAbstractSlider_setOrientation(s, XGuiOrientation_Vertical);
    policy = XWidget_sizePolicy((XWidget*)s);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Minimum);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);

    XClassDelete((XClass*)s);
}

/** @brief 父子所有权：滑块作为子控件随父析构释放。 */
XQ_TEST(XAbstractSlider, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XAbstractSlider* child = NULL;
    XQ_REQUIRE(host != NULL);

    child = XAbstractSlider_create(host, 0);
    XQ_REQUIRE(child != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)child) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)child));

    /* 子控件不手动删除：随 host 析构；漏删由运行器 [LEAK] 判失败。 */
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON */
