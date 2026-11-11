/**
 * @file       XKeySequenceEditAlignTest.c
 * @brief      XKeySequenceEdit × Qt 6.8 QKeySequenceEdit 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qkeysequenceedit.h
 *             与本机 Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/widgets/qkeysequenceedit.cpp。
 *             关键语义：
 *             - 构造默认 maximumSequenceLength=4、clearButtonEnabled=false、
 *               空序列、结束键组合 {Tab, Backtab}
 *               （qkeysequenceedit.cpp:56-101 QKeySequenceEditPrivate::init）；
 *             - 带初值构造 = 默认构造 + setKeySequence
 *               （qkeysequenceedit.cpp:111-115）；
 *             - setKeySequence 超过上限时**告警并截断**
 *               （qkeysequenceedit.cpp:243-250）；同值早退不发射
 *               keySequenceChanged（qkeysequenceedit.cpp:240-241）；
 *             - setMaximumSequenceLength 对越界值**拒绝并告警**
 *               （qkeysequenceedit.cpp:191-207，不夹取），收缩时把已有
 *               序列截断；
 *             - clear() 等价 setKeySequence(空)（qkeysequenceedit.cpp:275-278）；
 *             - 录制中 Return 确认、Backspace 删最后一组、Esc 清空。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XKEYSEQUENCEEDIT_ON

#include "XKeySequenceEdit.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XVtable.h"
#include "XVarList.h"
#include "XClass.h"

/** @brief 变化/完成信号计数。 */
static int g_kseChanged;
static int g_kseFinished;

static void kse_changedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_kseChanged;
}

static void kse_finishedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_kseFinished;
}

/** @brief 直发一次按键（修饰位随事件注入，对标 Qt 的 KeyPress 派发）。 */
static void kse_key(XWidget* target, int key, XKeyboardModifiers mods)
{
    XKeyEvent event;
    if (!target) return;
    XKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, key, mods);
    XObject_event_base((XObject*)target, (XEvent*)&event);
}

/** @brief 造一个两组合序列（Ctrl+O、Ctrl+S）。 */
static void kse_fillTwo(XKeySequence* sequence)
{
    if (!sequence) return;
    sequence->count = 2;
    sequence->combos[0].modifiers = XKeyboardModifier_ControlModifier;
    sequence->combos[0].key = (int)XKey_O;
    sequence->combos[1].modifiers = XKeyboardModifier_ControlModifier;
    sequence->combos[1].key = (int)XKey_S;
}

/* ==================== ① 默认值 + 序列设置/截断 ==================== */

XQ_TEST(XKeySequenceEdit, defaultsAndSequenceSetter)
{
    XKeySequenceEdit* edit = XKeySequenceEdit_create(NULL, 0);
    const XKeySequence* sequence;
    XKeySequence input;

    XQ_REQUIRE(edit != NULL);

    /* 构造默认（qkeysequenceedit.cpp:56-101）。 */
    XQ_EQ_I64(XKeySequenceEdit_maximumSequenceLength(edit), 4);
    XQ_CHECK(!XKeySequenceEdit_isClearButtonEnabled(edit));
    XQ_CHECK(!XKeySequenceEdit_isCapturing(edit));
    sequence = XKeySequenceEdit_keySequence(edit);
    XQ_REQUIRE(sequence != NULL);
    XQ_EQ_I64(sequence->count, 0);
    XQ_EQ_I64(XKeySequenceEdit_finishingKeyCombinationCount(edit), 2);
    {
        const XKeyCombination* fin =
            XKeySequenceEdit_finishingKeyCombinations(edit);
        XQ_REQUIRE(fin != NULL);
        XQ_EQ_I64(fin[0].key, (int)XKey_Tab);
        XQ_EQ_I64(fin[0].modifiers, XKeyboardModifier_NoModifier);
        XQ_EQ_I64(fin[1].key, (int)XKey_Backtab);
    }

    g_kseChanged = 0;
    g_kseFinished = 0;
    XObject_connect_1((XObject*)edit,
                      (size_t)XKeySequenceEdit_keySequenceChanged_signal(
                          NULL, NULL),
                      (XObject*)edit, kse_changedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)edit,
                      (size_t)XKeySequenceEdit_editingFinished_signal(NULL),
                      (XObject*)edit, kse_finishedSlot,
                      XConnectionType_Direct);

    /* setKeySequence：多组合序列往返 + 一次变化信号。 */
    kse_fillTwo(&input);
    XKeySequenceEdit_setKeySequence(edit, &input);
    sequence = XKeySequenceEdit_keySequence(edit);
    XQ_EQ_I64(sequence->count, 2);
    XQ_EQ_I64(sequence->combos[0].key, (int)XKey_O);
    XQ_EQ_I64(sequence->combos[1].key, (int)XKey_S);
    XQ_EQ_I64(g_kseChanged, 1);
    /* 同值早退：不再发射（qkeysequenceedit.cpp:240-241）。 */
    XKeySequenceEdit_setKeySequence(edit, &input);
    XQ_EQ_I64(g_kseChanged, 1);

    /* setMaximumSequenceLength 越界被拒绝（不夹取，qkeysequenceedit.cpp:191-199）。 */
    XKeySequenceEdit_setMaximumSequenceLength(edit, 0);
    XQ_EQ_I64(XKeySequenceEdit_maximumSequenceLength(edit), 4);
    XKeySequenceEdit_setMaximumSequenceLength(edit, 5);
    XQ_EQ_I64(XKeySequenceEdit_maximumSequenceLength(edit), 4);
    /* 收缩到 1：已有序列按新上限截断（qkeysequenceedit.cpp:200-206）。 */
    XKeySequenceEdit_setMaximumSequenceLength(edit, 1);
    XQ_EQ_I64(XKeySequenceEdit_maximumSequenceLength(edit), 1);
    sequence = XKeySequenceEdit_keySequence(edit);
    XQ_EQ_I64(sequence->count, 1);
    XQ_EQ_I64(sequence->combos[0].key, (int)XKey_O);

    /* 超上限输入被截断（qkeysequenceedit.cpp:243-250）。 */
    XKeySequenceEdit_setMaximumSequenceLength(edit, 4);
    kse_fillTwo(&input);
    XKeySequenceEdit_setMaximumSequenceLength(edit, 1);
    XKeySequenceEdit_setKeySequence(edit, &input);
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 1);

    /* clear：清空并发射一次变化信号（qkeysequenceedit.cpp:275-278）。 */
    {
        int before = g_kseChanged;
        XKeySequenceEdit_clear(edit);
        XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 0);
        XQ_EQ_I64(g_kseChanged, before + 1);
        XKeySequenceEdit_clear(edit); /* 已空：无变化信号 */
        XQ_EQ_I64(g_kseChanged, before + 1);
    }

    /* 清除按钮开关往返。 */
    XKeySequenceEdit_setClearButtonEnabled(edit, true);
    XQ_CHECK(XKeySequenceEdit_isClearButtonEnabled(edit));
    XKeySequenceEdit_setClearButtonEnabled(edit, false);
    XQ_CHECK(!XKeySequenceEdit_isClearButtonEnabled(edit));

    /* 结束键组合可替换（XGui 以「数组+数量」承载 QList<QKeyCombination>）。 */
    {
        XKeyCombination custom[1];
        custom[0].modifiers = XKeyboardModifier_ShiftModifier;
        custom[0].key = (int)XKey_Return;
        XKeySequenceEdit_setFinishingKeyCombinations(edit, custom, 1);
        XQ_EQ_I64(XKeySequenceEdit_finishingKeyCombinationCount(edit), 1);
        XQ_CHECK(XKeySequenceEdit_finishingKeyCombinations(edit)[0].key ==
                 (int)XKey_Return);
        XKeySequenceEdit_setFinishingKeyCombinations(edit, NULL, 0);
        XQ_EQ_I64(XKeySequenceEdit_finishingKeyCombinationCount(edit), 0);
    }

    XClassDelete((XClass*)edit);
}

/* ==================== ② 带初值构造 + 捕获态机 ==================== */

XQ_TEST(XKeySequenceEdit, initialSequenceAndCapture)
{
    XKeySequence input;
    XKeySequenceEdit* edit;
    const XKeySequence* sequence;

    /* 带初值构造 = 默认构造 + setKeySequence（qkeysequenceedit.cpp:111-115）。 */
    kse_fillTwo(&input);
    edit = XKeySequenceEdit_create_2(&input, NULL, 0);
    XQ_REQUIRE(edit != NULL);
    sequence = XKeySequenceEdit_keySequence(edit);
    XQ_EQ_I64(sequence->count, 2);
    XQ_EQ_I64(sequence->combos[1].key, (int)XKey_S);

    g_kseChanged = 0;
    g_kseFinished = 0;
    XObject_connect_1((XObject*)edit,
                      (size_t)XKeySequenceEdit_keySequenceChanged_signal(
                          NULL, NULL),
                      (XObject*)edit, kse_changedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)edit,
                      (size_t)XKeySequenceEdit_editingFinished_signal(NULL),
                      (XObject*)edit, kse_finishedSlot,
                      XConnectionType_Direct);

    /* 获得焦点即进入捕获（Qt「recording is initiated when the widget
     * receives the focus」）。无头宿主若未真正授予焦点（无窗口系统
     * 激活），用公开的 startCapturing 显式武装，保证后续按键路径确定。 */
    XKeySequenceEdit_clear(edit);
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 0);
    XWidget_setFocus((XWidget*)edit);
    if (!XKeySequenceEdit_isCapturing(edit))
        XKeySequenceEdit_startCapturing(edit);
    XQ_CHECK(XKeySequenceEdit_isCapturing(edit));

    /* Ctrl+O 捕获第一组；Backspace 删除；再次捕获。 */
    kse_key((XWidget*)edit, (int)XKey_O, XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 1);
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->combos[0].key,
              (int)XKey_O);
    kse_key((XWidget*)edit, (int)XKey_Backspace, XKeyboardModifier_NoModifier);
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 0);
    kse_key((XWidget*)edit, (int)XKey_S, XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 1);

    /* Return 确认：finishEditing **成对发射** keySequenceChanged（最终序列）
     * 与 editingFinished（qkeysequenceedit.cpp:76-83；本机 Qt 6.8 探针
     * 失焦帧 changed+1/finished+1；gaps 209）。 */
    XQ_EQ_I64(g_kseFinished, 0);
    {
        int changedBefore = g_kseChanged;
        kse_key((XWidget*)edit, (int)XKey_Return,
                XKeyboardModifier_NoModifier);
        XQ_EQ_I64(g_kseFinished, 1);
        XQ_EQ_I64(g_kseChanged, changedBefore + 1);
    }

    /* 重新武装捕获后 Esc：清空序列、退出捕获，且 Esc 本身不被录进序列。
       本库的 Esc 是"反键盘陷阱"扩展（Qt 6.8 的 keyPressEvent 不特判
       Escape，会把它当普通键录下来），故这里只断言本库口径：
       清空 + 不再发 editingFinished（finishEditing 只由结束键/Return
       驱动，见 XKeySequenceEdit.c 的 xkse_stopCapture）。 */
    XKeySequenceEdit_startCapturing(edit);
    XQ_CHECK(XKeySequenceEdit_isCapturing(edit));
    kse_key((XWidget*)edit, (int)XKey_Escape, XKeyboardModifier_NoModifier);
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 0);
    XQ_EQ_I64(g_kseFinished, 1);
    XQ_CHECK(!XKeySequenceEdit_isCapturing(edit));

    XClassDelete((XClass*)edit);
}

#endif /* XGUI_ON && XWIDGET_ON && XKEYSEQUENCEEDIT_ON */
