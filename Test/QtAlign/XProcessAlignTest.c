/**
 * @file       XProcessAlignTest.c
 * @brief      XProcess 与 Qt 6.8 QProcess 的通道信号对齐契约测试。
 * @details    覆盖本轮补齐的三条通道级信号（XIODevice 声明、QProcess 负责
 *             发射）：
 *             - **channelReadyRead**：`QProcessPrivate::tryReadFromChannel`
 *               在 readyRead 之后、readyReadStandardOutput/Error 之前发
 *               `channelReadyRead(int(channelIdx))`（qprocess.cpp:1167-1178，
 *               通道号 = QProcessChannel 枚举（stdout=0 / stderr=1）。
 *               本库的收口点是 XProcess_readyReadStandardOutput_signal /
 *               _StandardError_signal（Drive 后端读到数据时调用）。
 *             - **channelBytesWritten**：QIODevice 的通道级写入信号
 *               （qiodevice.h:110）。Qt 6.8 的 QProcess 自身不发射它（全树
 *               只有 QAbstractSocket/QSslSocket/TLS 插件发射，
 *               qabstractsocket.cpp:1240-1241），本库在 XProcess 的标准输入
 *               写边界（writeData 收下 payload 后）以 channel=0 补发。
 *             - **readChannelFinished**：Qt 在 processFinished() 里、
 *               finished() **之前**发射（qprocess.cpp:1246 "we received EOF
 *               now"）；本库在 XProcess_backend_notifyFinished 的同一位置
 *               发射，且同一次启动只发一次。
 *             - 顺带锁住信号 ID 契约：`XSignal(XProcess_xxx_signal)` 必须以
 *               self==NULL 调用取到**非 0** 的信号标识（XObject_signalIdOf
 *               的约定，见 XObject.c:1146-1163）。修复前 XProcess 的
 *               信号函数在 self==NULL 时返回 NULL ⇒ connect 报
 *               "No such signal (id=0)" ⇒ 进程信号永远连不上。
 * @note       第二个用例用「白盒置运行态 + 直接调 backend notify」验证
 *             退出信号顺序：不依赖子进程，跨平台确定。第三个用例在 POSIX
 *             上跑真实子进程做端到端验证（Windows 分支不启子进程）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XProcess.h"
#include "XProcess_Protected.h"
#include "XIODevice.h"
#include "XByteArray.h"
#include "XObject.h"
#include "XClass.h"
#include "XMemory.h"
#include "XVarList.h"

#include <stdio.h>
#include <string.h>

#if XProcess_ON

/* ==================== 事件记录 ==================== */

/** @brief 事件种类（顺序断言用）。 */
typedef enum XProcEventKind
{
    XProcEvent_ChannelReadyRead = 1,  /**< channelReadyRead(channel)。 */
    XProcEvent_ReadyReadStdout = 2,   /**< readyReadStandardOutput。 */
    XProcEvent_ReadyReadStderr = 3,   /**< readyReadStandardError。 */
    XProcEvent_ReadChannelFinished = 4,/**< readChannelFinished。 */
    XProcEvent_Finished = 5,          /**< finished(exitCode,status)。 */
    XProcEvent_ChannelBytesWritten = 6 /**< channelBytesWritten(channel,bytes)。 */
} XProcEventKind;

#define XPROC_EVENT_MAX 32
static int s_kind[XPROC_EVENT_MAX];
static int s_chan[XPROC_EVENT_MAX];
static long long s_bytes[XPROC_EVENT_MAX];
static int s_eventN;

static void xproc_resetEvents(void)
{
    s_eventN = 0;
}

static void xproc_push(int kind)
{
    if (s_eventN >= XPROC_EVENT_MAX) return;
    s_kind[s_eventN] = kind;
    s_chan[s_eventN] = -1;
    s_bytes[s_eventN] = 0;
    ++s_eventN;
}

static void xproc_slotChannelReadyRead(XObject* receiver, XVarList* args)
{
    (void)receiver;
    if (!args || s_eventN >= XPROC_EVENT_MAX) return;
    XVarList_args_1(args, int, channel);
    xproc_push(XProcEvent_ChannelReadyRead);
    s_chan[s_eventN - 1] = channel;
}

static void xproc_slotReadyReadStdout(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xproc_push(XProcEvent_ReadyReadStdout);
}

static void xproc_slotReadyReadStderr(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xproc_push(XProcEvent_ReadyReadStderr);
}

static void xproc_slotReadChannelFinished(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xproc_push(XProcEvent_ReadChannelFinished);
}

static void xproc_slotFinished(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xproc_push(XProcEvent_Finished);
}

static void xproc_slotChannelBytesWritten(XObject* receiver, XVarList* args)
{
    (void)receiver;
    if (!args || s_eventN >= XPROC_EVENT_MAX) return;
    XVarList_args_2(args, int, channel, int64_t, bytes);
    xproc_push(XProcEvent_ChannelBytesWritten);
    s_chan[s_eventN - 1] = channel;
    s_bytes[s_eventN - 1] = (long long)bytes;
}

/** @brief 统计某类事件出现次数。 */
static int xproc_count(int kind)
{
    int i;
    int n = 0;
    for (i = 0; i < s_eventN; ++i)
        if (s_kind[i] == kind) ++n;
    return n;
}

/** @brief 某类事件第一次出现的下标（没有返回 -1）。 */
static int xproc_first(int kind)
{
    int i;
    for (i = 0; i < s_eventN; ++i)
        if (s_kind[i] == kind) return i;
    return -1;
}

/** @brief 用 XSignal 挂接全部相关信号（同时验证信号 ID 契约）。 */
static void xproc_connectAll(XProcess* p)
{
    XQ_CHECK_MSG(XSignal(XProcess_readyReadStandardOutput_signal) != 0,
                 "XSignal(readyReadStandardOutput) 必须取到非 0 标识");
    XQ_CHECK_MSG(XSignal(XProcess_readyReadStandardError_signal) != 0,
                 "XSignal(readyReadStandardError) 必须取到非 0 标识");
    XQ_CHECK_MSG(XSignal(XProcess_finished_signal) != 0,
                 "XSignal(finished) 必须取到非 0 标识");
    XQ_CHECK_MSG(XSignal(XIODevice_channelReadyRead_signal) != 0,
                 "XSignal(channelReadyRead) 必须取到非 0 标识");
    XQ_CHECK_MSG(XSignal(XIODevice_channelBytesWritten_signal) != 0,
                 "XSignal(channelBytesWritten) 必须取到非 0 标识");
    XQ_CHECK_MSG(XSignal(XIODevice_readChannelFinished_signal) != 0,
                 "XSignal(readChannelFinished) 必须取到非 0 标识");
    XObject_connect((XObject*)p, XSignal(XProcess_readyReadStandardOutput_signal),
                      (XObject*)p, xproc_slotReadyReadStdout,
                      XConnectionType_Direct);
    XObject_connect((XObject*)p, XSignal(XProcess_readyReadStandardError_signal),
                      (XObject*)p, xproc_slotReadyReadStderr,
                      XConnectionType_Direct);
    XObject_connect((XObject*)p, XSignal(XIODevice_channelReadyRead_signal),
                      (XObject*)p, xproc_slotChannelReadyRead,
                      XConnectionType_Direct);
    XObject_connect((XObject*)p, XSignal(XIODevice_channelBytesWritten_signal),
                      (XObject*)p, xproc_slotChannelBytesWritten,
                      XConnectionType_Direct);
    XObject_connect((XObject*)p, XSignal(XIODevice_readChannelFinished_signal),
                      (XObject*)p, xproc_slotReadChannelFinished,
                      XConnectionType_Direct);
    XObject_connect((XObject*)p, XSignal(XProcess_finished_signal),
                      (XObject*)p, xproc_slotFinished,
                      XConnectionType_Direct);
}

/* ==================== 用例 ==================== */

/**
 * @brief 读到数据时的通道号与顺序（不启子进程，直接驱动发射点）。
 * @details Qt 顺序（qprocess.cpp:1167-1178）：
 *          readyRead(channelReadyRead(ch) → readyReadStandard{Output,Error}))；
 *          本库 Drive 后端的调用顺序是 readyReadStandard* → readyRead，
 *          故本用例只锁「channelReadyRead 先于 readyReadStandard*」这一段
 *          （与 Qt 一致的相对次序），通道号 0=stdout / 1=stderr。
 */
XQ_TEST(XProcess, channelReadyReadOnRead)
{
    XProcess* p = XProcess_create();
    XQ_REQUIRE(p != NULL);
    xproc_resetEvents();
    xproc_connectAll(p);

    /* stdout：channelReadyRead(0) → readyReadStandardOutput。 */
    XProcess_readyReadStandardOutput_signal(p);
    XQ_EQ_I64(s_eventN, 2);
    XQ_CHECK_MSG(s_kind[0] == XProcEvent_ChannelReadyRead,
                 "channelReadyRead 必须先于 readyReadStandardOutput");
    XQ_EQ_I64(s_chan[0], 0);           /* StandardOutput = 0 */
    XQ_CHECK(s_kind[1] == XProcEvent_ReadyReadStdout);

    /* stderr：channelReadyRead(1) → readyReadStandardError。 */
    xproc_resetEvents();
    XProcess_readyReadStandardError_signal(p);
    XQ_EQ_I64(s_eventN, 2);
    XQ_CHECK_MSG(s_kind[0] == XProcEvent_ChannelReadyRead,
                 "channelReadyRead 必须先于 readyReadStandardError");
    XQ_EQ_I64(s_chan[0], 1);           /* StandardError = 1 */
    XQ_CHECK(s_kind[1] == XProcEvent_ReadyReadStderr);

    /* self==NULL 取标识不得发射（XSignal 契约路径）。 */
    xproc_resetEvents();
    XQ_CHECK(XProcess_readyReadStandardOutput_signal(NULL) != NULL);
    XQ_EQ_I64(s_eventN, 0);

    XClassDelete((XClass*)p);
}

/**
 * @brief 进程退出：readChannelFinished 先于 finished 且只发一次。
 * @details Qt processFinished（qprocess.cpp:1226-1249）：
 *          setErrorAndEmit(Crashed) → `emit readChannelFinished()` →
 *          `emit finished(exitCode, status)`。
 *          本用例白盒把状态置 Running 后直接调后端通知入口
 *          （XProcess_backend_notifyFinished），不启真实子进程；重复通知
 *          在入口首行因状态已 NotRunning 早退 ⇒ 不重复发。
 */
XQ_TEST(XProcess, readChannelFinishedBeforeFinished)
{
    XProcess* p = XProcess_create();
    XQ_REQUIRE(p != NULL);
    xproc_resetEvents();
    xproc_connectAll(p);

    /* 白盒：模拟「子进程已在运行」（后端已启动才会走 notify 路径）。 */
    p->m_state = XProcessState_Running;
    XProcess_backend_notifyFinished(p, 0, XProcessExitStatus_NormalExit, false);

    XQ_EQ_I64(xproc_count(XProcEvent_ReadChannelFinished), 1);
    XQ_EQ_I64(xproc_count(XProcEvent_Finished), 1);
    XQ_CHECK_MSG(xproc_first(XProcEvent_ReadChannelFinished)
                     < xproc_first(XProcEvent_Finished),
                 "readChannelFinished 必须先于 finished（Qt processFinished）");
    XQ_EQ_I64(XProcess_state(p), XProcessState_NotRunning);
    XQ_EQ_I64(XProcess_exitCode(p), 0);

    /* 重复通知：状态已 NotRunning ⇒ 两条都不再发。 */
    xproc_resetEvents();
    XProcess_backend_notifyFinished(p, 0, XProcessExitStatus_NormalExit, false);
    XQ_EQ_I64(xproc_count(XProcEvent_ReadChannelFinished), 0);
    XQ_EQ_I64(xproc_count(XProcEvent_Finished), 0);

    XClassDelete((XClass*)p);
}

/**
 * @brief 端到端：写入 stdin 发 channelBytesWritten(0, n)，回读数据发
 *        channelReadyRead(0)+readyReadStandardOutput，退出发
 *        readChannelFinished+finished。
 * @details 真实子进程只在 POSIX 分支启动（`sh -c cat` 回显 stdin）；
 *          Windows 分支不启子进程——发射点与平台无关（Drive 后端调用同一
 *          组 XProcess_*_signal 入口），通道写入信号由公共层 writeData
 *          发射，已在 channelReadyReadOnRead/readChannelFinishedBeforeFinished
 *          两个无子进程用例里覆盖同一收口点。
 */
XQ_TEST(XProcess, channelSignalsEndToEnd)
{
    XProcess* p = XProcess_create();
    XQ_REQUIRE(p != NULL);
#if defined(_WIN32)
    /* Windows：不启动 cmd.exe 子进程（无 sh；测试宿主可能无控制台）。 */
    xproc_connectAll(p);   /* 仍锁信号 ID 契约 */
#else
    const char* args[2];
    XByteArray* out;
    args[0] = "-c";
    args[1] = "cat";
    xproc_resetEvents();
    xproc_connectAll(p);
    XQ_REQUIRE(XProcess_start_utf8(p, "sh", args, 2, XIODevice_ReadWrite));
    XQ_EQ_I64(XProcess_state(p), XProcessState_Running);

    /* 写入 stdin ⇒ channelBytesWritten(0, 3)。 */
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)p, "hi\n", 3), 3);
    XQ_CHECK_MSG(xproc_count(XProcEvent_ChannelBytesWritten) == 1,
                 "写入 3 字节应发 1 条 channelBytesWritten");
    XQ_EQ_I64(s_chan[xproc_first(XProcEvent_ChannelBytesWritten)], 0);
    XQ_EQ_I64(s_bytes[xproc_first(XProcEvent_ChannelBytesWritten)], 3);

    /* 关闭写通道让 cat 退出：回显数据到达 + 退出的三条信号。 */
    XProcess_closeWriteChannel(p);
    XQ_REQUIRE(XProcess_waitForFinished(p, 5000));
    XQ_CHECK_MSG(xproc_count(XProcEvent_ChannelReadyRead) >= 1,
                 "stdout 有数据必须发 channelReadyRead");
    XQ_EQ_I64(s_chan[xproc_first(XProcEvent_ChannelReadyRead)], 0);
    XQ_CHECK_MSG(xproc_count(XProcEvent_ReadyReadStdout) >= 1,
                 "stdout 有数据必须发 readyReadStandardOutput");
    XQ_EQ_I64(xproc_count(XProcEvent_ReadChannelFinished), 1);
    XQ_EQ_I64(xproc_count(XProcEvent_Finished), 1);
    XQ_CHECK_MSG(xproc_first(XProcEvent_ReadChannelFinished)
                     < xproc_first(XProcEvent_Finished),
                 "readChannelFinished 必须先于 finished");

    out = XProcess_readAllStandardOutput(p);
    XQ_REQUIRE(out != NULL);
    XQ_EQ_I64((long long)XByteArray_size_base((const XContainer*)out), 3);
    XQ_CHECK(XByteArray_data(out) != NULL
             && memcmp(XByteArray_data(out), "hi\n", 3) == 0);
    XClassDelete((XClass*)out);

    /* close 之后不得重发 readChannelFinished（基类 close 的通道收口不能
     * 与进程退出收口叠加）。 */
    XIODevice_close_base((XIODevice*)p);
    XQ_EQ_I64(xproc_count(XProcEvent_ReadChannelFinished), 1);
#endif

    XClassDelete((XClass*)p);
}

#endif /* XProcess_ON */
