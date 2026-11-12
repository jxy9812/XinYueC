/**
 * @file       XProgressDialogAlignTest.c
 * @brief      XProgressDialog × Qt 6.8 QProgressDialog 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/
 *               qprogressdialog.h（setLabel/setCancelButton/setBar、
 *               wasCanceled/minimum/maximum/value、sizeHint 重写、
 *               labelText/minimumDuration、autoReset/autoClose、
 *               open(receiver,member)、cancel/reset/setRange/setValue/
 *               setLabelText/setCancelButtonText/setMinimumDuration、
 *               canceled() 信号、forceShow 槽）；
 *             - Qt 6.8 源码原文：
 *               * QProgressBar::reset（qprogressbar.cpp:220-228）：
 *                 `value = minimum - 1`（忙态哨兵）；
 *               * QProgressBar::setRange（:315-327）：
 *                 `maximum = qMax(minimum, maximum)`；值落在
 *                 `[minimum-1, maximum]` 之外时 reset()；
 *               * QProgressBar::setValue（:277-294）：越界值**不改变**
 *                 当前值（0..0 未定态范围例外）；
 *               * QProgressDialog::setValue（:613-664）：`progress ==
 *                 minimum()` 分支起定时器后**直接 return**（跳过
 *                 autoReset），末尾 `progress == maximum && autoReset`
 *                 才 reset；
 *               * setValue 的另两段（P0-03 补齐）：shownOnce 且模态时
 *                 processEvents()（:621-627，d->processingEvents 重入
 *                 闸）；未显示时按 elapsed/estimate 提前 show（:632-656，
 *                 minWaitTime=50ms 见 :33）；
 *               * QProgressDialog::sizeHint（:668-687）：
 *                 `width = max(200, labelW + left + right)`、
 *                 `height = bottom*2 + barH + labelH + spacing
 *                           + (cancel ? cancelH + spacing : 0)`；
 *               * setBar（:425-444）：**接管所有权**（`delete d->bar`），
 *                 NULL 入参打印警告后 return（不清除已有进度条）。
 *             - 本机 Qt 6.8.3 + offscreen 实测（/tmp 探针）：
 *               ① 构造 value()==-1、reset 后 -1、setValue(max) 且
 *                  autoReset 后 -1、cancel 后 -1；
 *               ② setRange(50,20) → min=max=50、value=49（max 钳到 min）；
 *               ③ setValue(150)/setValue(-5) **不改变**值（autoReset=false
 *                  时实测保持 -1）；
 *               ④ setRange(10,20) 后 value==9（minimum-1）；
 *               ⑤ setMinimum(60)（原 0..100、value -1）→ value 59；
 *               ⑥ 空标签 sizeHint == 200x102；
 *               ⑦ setBar：新条 parent 变对话框、替换时旧条被删、析构时
 *                  条被删、setBar(NULL) 无操作。
 * @note       r1 的三项偏差（value 构造为 0 / setRange 交换 /
 *             setBar 借用）已全部撤销（账本 230/231/232/233），旧 apitest
 *             断言见 testMigrations。项目有意保留：cancel() 直接发射
 *             canceled()（Qt 为 canceled→cancel 内部连接；既有公开契约
 *             与 apitest 锁）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON

#include "XProgressDialog.h"
#include "XDialog.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XThread.h" /* P0-03 estimate 用例：minWaitTime 观察窗实睡。 */
#if XPROGRESSBAR_ON
#include "XProgressBar.h"
#endif
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
#include "XAbstractButton.h"
#include "XPushButton.h"
#endif
#if XFRAME_ON && XLABEL_ON
#include "XLabel.h"
#endif

/* ==================== 信号探针 ==================== */

static int pd_canceled = 0;

static void pd_canceledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++pd_canceled;
}

static int pd_barDestroyed = 0;

static void pd_barDestroyedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++pd_barDestroyed;
}

/** @brief P0-03 模态泵事件探针：经 Queued 连接的 canceled 计数。
 *         setValue 的 processEvents 分支泵队列时才会 +1。 */
static int pd_queuedCanceled = 0;

static void pd_queuedCanceledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++pd_queuedCanceled;
}

/** @brief 取 labelText 文本副本（getter 返回新建 XString，需释放）。 */
static void pd_labelText(const XProgressDialog* self, char* buf, int cap)
{
    XString* s = XProgressDialog_labelText(self);
    const char* utf8 = s ? XString_toUtf8(s) : NULL;
    int n;
    if (buf && cap > 0) {
        buf[0] = '\0';
        if (utf8) {
            n = (int)strlen(utf8);
            if (n >= cap) n = cap - 1;
            memcpy(buf, utf8, (size_t)n);
            buf[n] = '\0';
        }
    }
    if (s) XClassDelete((XClass*)s);
}

/* ==================== 用例 ==================== */

/** @brief 缺省值 + 忙态哨兵 + setRange 钳制 + 越界不改变值。 */
XQ_TEST(XProgressDialog, defaultsValueAndRange)
{
    XProgressDialog* pd = XProgressDialog_create(NULL, 0);
    char text[128];
    XQ_REQUIRE(pd != NULL);

    /* 缺省（Qt 实测：min 0/max 100/minimumDuration 4000/auto 全 true）。 */
    XQ_EQ_I64(XProgressDialog_minimum(pd), 0);
    XQ_EQ_I64(XProgressDialog_maximum(pd), 100);
    XQ_EQ_I64(XProgressDialog_minimumDuration(pd), 4000);
    XQ_CHECK(XProgressDialog_autoReset(pd));
    XQ_CHECK(XProgressDialog_autoClose(pd));
    XQ_CHECK(!XProgressDialog_wasCanceled(pd));
    pd_labelText(pd, text, (int)sizeof(text));
    XQ_EQ_STR(text, "");
    /* 忙态哨兵：构造 value()==minimum-1==-1（QProgressBar 构造 value=-1；
     * 账本 230）。 */
    XQ_EQ_I64(XProgressDialog_value(pd), -1);

    /* setRange(10,20)：值 -1 落到 minimum-1=9 之外 ⇒ 复位为 9。 */
    XProgressDialog_setRange(pd, 10, 20);
    XQ_EQ_I64(XProgressDialog_minimum(pd), 10);
    XQ_EQ_I64(XProgressDialog_maximum(pd), 20);
    XQ_EQ_I64(XProgressDialog_value(pd), 9);
    /* setRange(min>max)：**max 钳到 min**，不是交换（账本 231；本机实测
     * min=max=50、value=49）。 */
    XProgressDialog_setRange(pd, 50, 20);
    XQ_EQ_I64(XProgressDialog_minimum(pd), 50);
    XQ_EQ_I64(XProgressDialog_maximum(pd), 50);
    XQ_EQ_I64(XProgressDialog_value(pd), 49);
    XProgressDialog_setRange(pd, 0, 100);
    XQ_EQ_I64(XProgressDialog_value(pd), 49); /* 在 [−1,100] 内 ⇒ 保持 */

    /* 越界值**不改变**当前值（Qt：outside-range has no effect）。 */
    XProgressDialog_setAutoReset(pd, false);
    XProgressDialog_setValue(pd, 150);
    XQ_EQ_I64(XProgressDialog_value(pd), 49);
    XProgressDialog_setValue(pd, -5);
    XQ_EQ_I64(XProgressDialog_value(pd), 49);
    XProgressDialog_setValue(pd, 40);
    XQ_EQ_I64(XProgressDialog_value(pd), 40);

    /* setMinimum 超上限时把 min 顶到原 max 之上再钳 max（Qt setRange
     * 同口径）：0..100、value 40 → setMinimum(120) ⇒ min=max=120。 */
    XProgressDialog_setMinimum(pd, 120);
    XQ_EQ_I64(XProgressDialog_minimum(pd), 120);
    XQ_EQ_I64(XProgressDialog_maximum(pd), 120);
    XQ_EQ_I64(XProgressDialog_value(pd), 119);
    XProgressDialog_setRange(pd, 0, 100);

    /* autoReset：达峰复位到 minimum-1（忙态哨兵）。 */
    XProgressDialog_setAutoReset(pd, true);
    XProgressDialog_setValue(pd, 100);
    XQ_EQ_I64(XProgressDialog_value(pd), -1);
    /* 关掉 autoReset 后保持峰值。 */
    XProgressDialog_setAutoReset(pd, false);
    XProgressDialog_setValue(pd, 100);
    XQ_EQ_I64(XProgressDialog_value(pd), 100);

    /* setValue(minimum) 起计时后直接返回，**不**触发 autoReset
     * （Qt 原文 `progress == minimum()` 分支 return；退化范围 5..5
     * 实测 value 停在 5 而非 4）。 */
    XProgressDialog_setRange(pd, 5, 5);
    XProgressDialog_setAutoReset(pd, true);
    XProgressDialog_setValue(pd, 5);
    XQ_EQ_I64(XProgressDialog_value(pd), 5);
    XProgressDialog_setRange(pd, 0, 100);

    /* reset：值回 minimum-1（缺省 -1），清 wasCanceled。 */
    XProgressDialog_setValue(pd, 50);
    XProgressDialog_reset(pd);
    XQ_EQ_I64(XProgressDialog_value(pd), -1);
    XQ_CHECK(!XProgressDialog_wasCanceled(pd));

    /* minimumDuration：int 原样往返（Qt 实测 100000/-5 均原样；
     * 旧实现收 uint16_t 会截断大值）。 */
    XProgressDialog_setMinimumDuration(pd, 100000);
    XQ_EQ_I64(XProgressDialog_minimumDuration(pd), 100000);
    XProgressDialog_setMinimumDuration(pd, -5);
    XQ_EQ_I64(XProgressDialog_minimumDuration(pd), -5);
    XProgressDialog_setMinimumDuration(pd, 4000);

    /* 文本往返。 */
    {
        XString* s = XString_create_utf8("复制中...");
        XProgressDialog_setLabelText(pd, s);
        XClassDelete((XClass*)s);
    }
    pd_labelText(pd, text, (int)sizeof(text));
    XQ_EQ_STR(text, "复制中...");

    XClassDelete((XClass*)pd);
}

/** @brief sizeHint == Qt 的 200x102 公式（空标签，账本 233）。 */
XQ_TEST(XProgressDialog, sizeHintMatchesQt)
{
    XProgressDialog* pd = XProgressDialog_create(NULL, 0);
    XSize hint;
    XQ_REQUIRE(pd != NULL);
    hint = XProgressDialog_sizeHint(pd);
    /* rowH = 字模行高 16（XFONT8X16）⇒ height = 12*2 + 20 + 16 + 8 + 26 + 8
     * = 102，与本机 Qt 6.8.3 实测 200x102 一致。 */
    XQ_EQ_I64(hint.width, 200);
    XQ_EQ_I64(hint.height, 102);
    /* 只是首选尺寸提示：不约束实际几何（Qt 允许拖小）。 */
    XWidget_resize((XWidget*)pd, 120, 80);
    XQ_EQ_I64(XWidget_width((XWidget*)pd), 120);
    XQ_EQ_I64(XWidget_height((XWidget*)pd), 80);
    XClassDelete((XClass*)pd);
}

/** @brief reset/cancel/forceShow 的可观察状态（隐藏、标志、信号）。 */
XQ_TEST(XProgressDialog, cancelAndReset)
{
    XProgressDialog* pd = XProgressDialog_create(NULL, 0);
    XQ_REQUIRE(pd != NULL);

    XObject_connect((XObject*)pd, XSignal(XProgressDialog_canceled_signal),
                      (XObject*)pd, pd_canceledSlot, XConnectionType_Direct);

    /* reset：值回 minimum-1、清 wasCanceled、autoClose 时隐藏。 */
    XProgressDialog_setAutoReset(pd, false);
    XProgressDialog_setValue(pd, 50);
    XProgressDialog_reset(pd);
    XQ_EQ_I64(XProgressDialog_value(pd), -1);
    XQ_CHECK(!XProgressDialog_wasCanceled(pd));
    XQ_CHECK(!XWidget_isVisible((XWidget*)pd));

    /* cancel：发射 canceled()、置 wasCanceled、复位（值 -1）并**无条件
     * 隐藏**（Qt 实测 autoClose=false 时 cancel 仍隐藏——forceHide）。 */
    XProgressDialog_setAutoClose(pd, false);
    XProgressDialog_setValue(pd, 70);
    XWidget_show((XWidget*)pd);
    XQ_CHECK(XWidget_isVisible((XWidget*)pd));
    pd_canceled = 0;
    XProgressDialog_cancel(pd);
    XQ_EQ_I64(pd_canceled, 1);
    XQ_CHECK(XProgressDialog_wasCanceled(pd));
    XQ_CHECK(!XWidget_isVisible((XWidget*)pd));
    XQ_EQ_I64(XProgressDialog_value(pd), -1);
    XProgressDialog_setAutoClose(pd, true);

    /* forceShow：只强显一次（shownOnce）；reset 后可再次强显。 */
    XProgressDialog_reset(pd);
    XWidget_hide((XWidget*)pd);
    XProgressDialog_forceShow(pd);
    XQ_CHECK(XWidget_isVisible((XWidget*)pd));
    XWidget_hide((XWidget*)pd);
    XProgressDialog_forceShow(pd); /* 已 shownOnce：不再强显 */
    XQ_CHECK(!XWidget_isVisible((XWidget*)pd));
    XProgressDialog_reset(pd); /* 复位 shownOnce */
    XProgressDialog_forceShow(pd);
    XQ_CHECK(XWidget_isVisible((XWidget*)pd));
    XWidget_hide((XWidget*)pd);

    XClassDelete((XClass*)pd);
}

/** @brief 子控件集成：setCancelButton 接管所有权并接线点击→cancel；
 *         setLabel 转发文本；NULL 入参清除。 */
XQ_TEST(XProgressDialog, childWidgets)
{
    XProgressDialog* pd = XProgressDialog_create(NULL, 0);
    XQ_REQUIRE(pd != NULL);

    /* canceled() 探针：本用例要断言「点取消按钮 → 组内 clicked 槽 →
     * cancel() → canceled()」整条链。 */
    XObject_connect((XObject*)pd, XSignal(XProgressDialog_canceled_signal),
                      (XObject*)pd, pd_canceledSlot, XConnectionType_Direct);

#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    {
        XPushButton* btn = XPushButton_create(NULL, 0);
        XQ_REQUIRE(btn != NULL);
        XProgressDialog_setCancelButton(pd, btn);
        XQ_CHECK(XProgressDialog_cancelButton(pd) == btn);
        XQ_CHECK(XWidget_parentWidget((XWidget*)btn) == (XWidget*)pd);

        /* apply 层级的点击→cancel 链（Qt clicked→canceled→cancel 同款）。 */
        pd_canceled = 0;
        XAbstractButton_click((XAbstractButton*)btn);
        XQ_EQ_I64(pd_canceled, 1);
        XQ_CHECK(XProgressDialog_wasCanceled(pd));

        XProgressDialog_setCancelButton(pd, NULL);
        XQ_CHECK(XProgressDialog_cancelButton(pd) == NULL);
    }
#endif

#if XFRAME_ON && XLABEL_ON
    {
        XLabel* label = XLabel_create(NULL, 0);
        char text[128];
        XQ_REQUIRE(label != NULL);
        XProgressDialog_setLabel(pd, label);
        XQ_CHECK(XProgressDialog_label(pd) == label);
        {
            XString* s = XString_create_utf8("自定义标签");
            XProgressDialog_setLabelText(pd, s);
            XClassDelete((XClass*)s);
        }
        pd_labelText(pd, text, (int)sizeof(text));
        XQ_EQ_STR(text, "自定义标签");
        /* label 所有权已转给对话框，不再手动释放。 */
    }
#endif

    XClassDelete((XClass*)pd);
}

/** @brief setBar 接管所有权（账本 232）：替换删旧条、析构删条、
 *         NULL 无操作、范围/值回灌。 */
XQ_TEST(XProgressDialog, barOwnership)
{
#if XPROGRESSBAR_ON
    XProgressDialog* pd = XProgressDialog_create(NULL, 0);
    XProgressBar* bar;
    XProgressBar* bar2;
    XQ_REQUIRE(pd != NULL);

    bar = XProgressBar_create(NULL, 0);
    XQ_REQUIRE(bar != NULL);
    XObject_connect((XObject*)bar, XSignal(XObject_destroyed_signal),
                      (XObject*)pd, pd_barDestroyedSlot,
                      XConnectionType_Direct);
    XProgressDialog_setBar(pd, bar);
    /* 新进度条成为对话框子控件（Qt adoptChildWidget）。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)bar) == (XWidget*)pd);
    /* 范围/值回灌：对话框值 -1（忙态）⇒ 条 reset 到 minimum-1。 */
    XQ_EQ_I64(XProgressBar_minimum(bar), XProgressDialog_minimum(pd));
    XQ_EQ_I64(XProgressBar_maximum(bar), XProgressDialog_maximum(pd));

    /* NULL 入参**无操作**（Qt 打印 "Cannot set a null progress bar" 后
     * return，不清除已有进度条）。 */
    pd_barDestroyed = 0;
    XProgressDialog_setBar(pd, NULL);
    XQ_EQ_I64(pd_barDestroyed, 0);

    /* 替换：旧条被接管方删除（Qt `delete d->bar`）。X 用延迟销毁，
     * 排空事件后 destroyed 信号计数为 1。 */
    bar2 = XProgressBar_create(NULL, 0);
    XQ_REQUIRE(bar2 != NULL);
    XProgressDialog_setBar(pd, bar2);
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    XQ_EQ_I64(pd_barDestroyed, 1);
    XQ_CHECK(XWidget_parentWidget((XWidget*)bar2) == (XWidget*)pd);

    /* 析构：对话框删除其拥有的进度条。 */
    XObject_connect((XObject*)bar2, XSignal(XObject_destroyed_signal),
                      (XObject*)pd, pd_barDestroyedSlot,
                      XConnectionType_Direct);
    pd_barDestroyed = 0;
    XClassDelete((XClass*)pd);
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    XQ_EQ_I64(pd_barDestroyed, 1);
#else
    XQ_CHECK(1); /* XPROGRESSBAR_ON=0：setBar 无实体类型可测。 */
#endif
}

/** @brief P0-03：setValue 的两段 Qt 行为。
 *         ① 未显示时按 elapsed/estimate 提前 show（qprogressdialog.cpp
 *            :632-656；minWaitTime=50ms 见 :33）——长任务不再一律等满
 *            minimumDuration 才弹窗；
 *         ② shownOnce 且模态时 setValue 内 processEvents 泵事件
 *            （:621-627），带重入闸（d->processingEvents）。 */
XQ_TEST(XProgressDialog, setValueEarlyShowAndModalPump)
{
    /* ① estimate 提前显示：计时起步（setValue(minimum)）后观察窗
     *    >minWaitTime(50ms)，按「已耗时/已步数」线性外推剩余耗时，
     *    预估总耗时 >= minimumDuration(4000ms) 即提前 show。 */
    {
        XProgressDialog* pd = XProgressDialog_create(NULL, 0);
        XQ_REQUIRE(pd != NULL);
        XProgressDialog_setMinimumDuration(pd, 4000);
        XProgressDialog_setRange(pd, 0, 1000000);
        /* 计时起步分支：progress==minimum ⇒ 起表 + 起定时器后 return，
         * 不显示（既有口径）。 */
        XProgressDialog_setValue(pd, 0);
        XQ_CHECK(!XWidget_isVisible((XWidget*)pd));
        /* 观察窗 60ms > 50ms：已推进 500000/1000000 步 ⇒ 预估剩余
         * ≈60ms < 4000ms（任务快完了）⇒ **不**提前 show。 */
        XThread_msleep(60);
        XProgressDialog_setValue(pd, 500000);
        XQ_CHECK(!XWidget_isVisible((XWidget*)pd));
        /* 只推进 5 步（500000→重置值不同即可，另起一轮计时）⇒ 外推
         * 剩余耗时 ≈ 60ms × 999995/5 ≈ 12s ≥ 4000ms ⇒ 提前 show。
         * （reset 重置计时后重新走计时起步。） */
        XProgressDialog_reset(pd);
        XProgressDialog_setValue(pd, 0);
        XQ_CHECK(!XWidget_isVisible((XWidget*)pd));
        XThread_msleep(60);
        XProgressDialog_setValue(pd, 5);
        XQ_CHECK(XWidget_isVisible((XWidget*)pd));
        XProgressDialog_reset(pd);
        XClassDelete((XClass*)pd);
    }

    /* ② shownOnce + 模态 ⇒ setValue 内泵事件（Queued 探针在 setValue
     *    返回前收到派发）；重入闸保证一次 setValue 只泵一遍。 */
    {
        XProgressDialog* pd = XProgressDialog_create(NULL, 0);
        XQ_REQUIRE(pd != NULL);
        XDialog_setModal((XDialog*)pd, true);
        pd_queuedCanceled = 0;
        XObject_connect((XObject*)pd,
                          XSignal(XProgressDialog_canceled_signal),
                          (XObject*)pd, pd_queuedCanceledSlot,
                          XConnectionType_Queued);
        /* 排队一枚 canceled（Queued 连接此刻不派发）。 */
        XProgressDialog_cancel(pd);
        XQ_EQ_I64(pd_queuedCanceled, 0);
        /* forceShow 置 shownOnce（cancel 已复位）；隐藏态不影响泵。 */
        XProgressDialog_forceShow(pd);
        XQ_EQ_I64(pd_queuedCanceled, 0);
        /* shownOnce + 模态：setValue 泵队列 ⇒ 排队的 canceled 派发。 */
        XProgressDialog_setValue(pd, 1);
        XQ_EQ_I64(pd_queuedCanceled, 1);
        /* 第二次 setValue（不同值）仍会泵，但队列已空 ⇒ 计数不变。 */
        XProgressDialog_setValue(pd, 2);
        XQ_EQ_I64(pd_queuedCanceled, 1);
        XProgressDialog_reset(pd);
        XClassDelete((XClass*)pd);
    }
}

#endif /* XGUI_ON && XWIDGET_ON && XDIALOG_ON */
