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
 *             - 录制中 Return 确认、Backspace 删最后一组、Esc 清空；
 *             - keySequenceChanged 只在 setKeySequence/clear 与收口
 *               （finishEditing）发射，录制期（逐键/Backspace）不发射
 *               （qkeysequenceedit.cpp:303-380 录制路径无 emit；P0-40）；
 *             - 释放已记录键后 1s 的 releaseTimer 收口 finishEditing，
 *               序列已满时释放立即收口（qkeysequenceedit.cpp:423-445
 *               keyReleaseEvent/timerEvent；P0-39）；
 *             - finish 后下一颗键先 clear 再录（qkeysequenceedit.cpp:
 *               298-301 `if (d->prevKey == -1) { clear(); ... }`）。
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

/** @brief 直发一次松键（P0-39：释放已记录键 ⇒ 起 1s 收口定时器）。 */
static void kse_keyRelease(XWidget* target, int key, XKeyboardModifiers mods)
{
    XKeyEvent event;
    if (!target) return;
    XKeyEvent_init(&event, XEVENT_TYPE_KEY_RELEASE, key, mods);
    XObject_event_base((XObject*)target, (XEvent*)&event);
}

/** @brief 直发一次定时器事件（1s 收口定时器到期的确定性替身：测试无
 *  事件循环，不真等 1s；timerId 用控件自报的 m_releaseTimer）。 */
static void kse_fireTimer(XKeySequenceEdit* edit, XTimerId id)
{
    XTimerEvent te;
    if (!edit) return;
    XEvent_init(&te.m_base, XEVENT_TYPE_TIMER);
    te.timerId = id;
    XObject_event_base((XObject*)edit, (XEvent*)&te);
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
    XObject_connect((XObject*)edit,
                      (size_t)XKeySequenceEdit_keySequenceChanged_signal(
                          NULL, NULL),
                      (XObject*)edit, kse_changedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)edit,
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
    XObject_connect((XObject*)edit,
                      (size_t)XKeySequenceEdit_keySequenceChanged_signal(
                          NULL, NULL),
                      (XObject*)edit, kse_changedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)edit,
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

    /* P0-39 对齐项：finish 后下一颗键先 clear 再录（qkeysequenceedit.cpp:
     * 298-301 `if (d->prevKey == -1) { clear(); d->prevKey = nextKey; }`）
     * ——旧序列 {Ctrl+S} 不与新录追加成「Ctrl+S, Ctrl+A」。 */
    XKeySequenceEdit_startCapturing(edit);
    {
        int changedBefore = g_kseChanged;
        kse_key((XWidget*)edit, (int)XKey_A,
                XKeyboardModifier_ControlModifier);
        XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 1);
        XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->combos[0].key,
                  (int)XKey_A);
        /* P0-40：该 clear() 自身发射一次 keySequenceChanged（Qt 的
         * clear() 走 setKeySequence 路径同发），随后的记录不再发射
         * （录制路径无 emit，qkeysequenceedit.cpp:303-380）。 */
        XQ_EQ_I64(g_kseChanged, changedBefore + 1);
        kse_key((XWidget*)edit, (int)XKey_Backspace,
                XKeyboardModifier_NoModifier);
        XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 0);
        /* Backspace 删组同样不发射（P0-40）。 */
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

/* ==================== ③ 释放键收口定时器（P0-39） ==================== */

XQ_TEST(XKeySequenceEdit, releaseFinish)
{
    XKeySequenceEdit* edit = XKeySequenceEdit_create(NULL, 0);
    int changedBefore;
    int finishedBefore;

    XQ_REQUIRE(edit != NULL);

    g_kseChanged = 0;
    g_kseFinished = 0;
    XObject_connect((XObject*)edit,
                      (size_t)XKeySequenceEdit_keySequenceChanged_signal(
                          NULL, NULL),
                      (XObject*)edit, kse_changedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)edit,
                      (size_t)XKeySequenceEdit_editingFinished_signal(NULL),
                      (XObject*)edit, kse_finishedSlot,
                      XConnectionType_Direct);

    /* 进入捕获并录一组（未满 4 组）。 */
    XWidget_setFocus((XWidget*)edit);
    if (!XKeySequenceEdit_isCapturing(edit))
        XKeySequenceEdit_startCapturing(edit);
    kse_key((XWidget*)edit, (int)XKey_O, XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 1);
    XQ_EQ_I64(g_kseFinished, 0); /* 录制本身不收口（P0-40）。 */

    /* 释放已记录键：起 1s 收口定时器，不立即发射（qkeysequenceedit.cpp:
     * 428-431 `if (d->keyNum < d->maximumSequenceLength)
     * d->releaseTimer = startTimer(1000);`）。 */
    kse_keyRelease((XWidget*)edit, (int)XKey_O,
                   XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(g_kseFinished, 0);
    XQ_EQ_I64(g_kseChanged, 0);
    XQ_CHECK(edit->m_releaseTimer != XTIMER_INVALID_ID);

    /* 到点收口（timerEvent 的 releaseTimer 分支，qkeysequenceedit.cpp:
     * 437-441）：finishEditing 成对发射两信号并退出捕获——旧实现
     * 「录完不点别处永不收口」的根修口径。测试无事件循环，用合成
     * 定时器事件确定性驱动，不真等 1s。 */
    kse_fireTimer(edit, edit->m_releaseTimer);
    XQ_EQ_I64(g_kseFinished, 1);
    XQ_EQ_I64(g_kseChanged, 1);
    XQ_CHECK(edit->m_releaseTimer == XTIMER_INVALID_ID);
    XQ_CHECK(!XKeySequenceEdit_isCapturing(edit));

    /* 序列已达 maximumSequenceLength 时释放立即收口（qkeysequenceedit.cpp:
     * 430-431 else 分支 finishEditing()），不再起定时器。 */
    XKeySequenceEdit_startCapturing(edit);
    XQ_CHECK(XKeySequenceEdit_isCapturing(edit));
    changedBefore = g_kseChanged;
    finishedBefore = g_kseFinished;
    /* 录满 4 组：只有第一颗键「先 clear 再录」的那次 clear 发射
     * （旧序列 {Ctrl+O} 非空），逐键记录不发射（P0-40）。 */
    kse_key((XWidget*)edit, (int)XKey_A, XKeyboardModifier_ControlModifier);
    kse_key((XWidget*)edit, (int)XKey_B, XKeyboardModifier_ControlModifier);
    kse_key((XWidget*)edit, (int)XKey_C, XKeyboardModifier_ControlModifier);
    kse_key((XWidget*)edit, (int)XKey_D, XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 4);
    XQ_EQ_I64(g_kseChanged, changedBefore + 1);
    XQ_EQ_I64(g_kseFinished, finishedBefore);
    XQ_CHECK(edit->m_releaseTimer == XTIMER_INVALID_ID); /* 只按不发。 */
    kse_keyRelease((XWidget*)edit, (int)XKey_D,
                   XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(g_kseFinished, finishedBefore + 1);
    XQ_EQ_I64(g_kseChanged, changedBefore + 2);
    XQ_CHECK(!XKeySequenceEdit_isCapturing(edit));
    XQ_EQ_I64(XKeySequenceEdit_keySequence(edit)->count, 4);

    XClassDelete((XClass*)edit);
}

#endif /* XGUI_ON && XWIDGET_ON && XKEYSEQUENCEEDIT_ON */
