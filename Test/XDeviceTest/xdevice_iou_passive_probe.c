/**
 * @file       xdevice_iou_passive_probe.c
 * @brief      io_uring 引擎「被动可读 fd」通道的独立验证程序。
 * @details    验证 Linux 异步后端（XNetIoRingPosix）在 **io_uring 引擎**下
 *             的被动 fd 通道：注册一个 pipe 读端为被动 fd → 另一端写入
 *             → 事件环应当被唤醒并回调 notify。
 *
 *             覆盖三件事：
 *             1. registerPassiveOsFd 返回 true（注册成功）；
 *             2. fd 就绪后 waitForEvents 返回且 notify 被调用；
 *             3. **一次性 POLL_ADD 的重挂语义**：第一次就绪回调后，
 *                第二次写入仍能再次唤醒（证明完成后重挂生效）。
 *
 *             用法：xdevice_iou_passive_probe [epoll|iouring]
 *                  不带参数=按引擎默认选择；显式参数强制对应引擎。
 * @note       诊断用程序，不属于框架运行路径。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "XNetIoRingPosix.h"
#include "XAbstractNetIoRing.h"

static int g_notifyCount = 0;

static void onPassiveReady(int osFd)
{
    (void)osFd;
    ++g_notifyCount;
}

static int fail(const char* msg)
{
    fprintf(stderr, "[IOU-PROBE] FAIL: %s (errno=%d %s)\n",
            msg, errno, strerror(errno));
    return 1;
}

int main(int argc, char** argv)
{
    int fds[2];
    XAbstractNetIoRing* ring;
    int regRc;
    int i;
    const char* engine = (argc > 1) ? argv[1] : "auto";
    if (strcmp(engine, "epoll") == 0) setenv("XNET_FORCE_EPOLL", "1", 1);
    else if (strcmp(engine, "iouring") == 0) setenv("XNET_FORCE_IOURING", "1", 1);

    if (pipe(fds) != 0) return fail("pipe");
    ring = XAbstractNetIoRing_createPlatform();
    if (!ring) return fail("createPlatform");
    XAbstractNetIoRing_setGlobal(ring);

    regRc = XAbstractNetIoRing_registerPassiveOsFd_base(ring, fds[0],
                                                         onPassiveReady);
    printf("[IOU-PROBE] engine=%s register rc=%d\n", engine, regRc);
    if (!regRc) {
        fprintf(stderr, "[IOU-PROBE] FAIL: 注册未成功（该引擎不支持？）\n");
        return 2;
    }

    /* 第一次就绪：写入 → 等待 → 应收到 notify。 */
    if (write(fds[1], "x", 1) != 1) return fail("write#1");
    for (i = 0; i < 200 && g_notifyCount == 0; ++i) {
        XAbstractNetIoRing_waitForEvents_base(ring, 20);
        XAbstractNetIoRing_processReady(ring);
    }
    printf("[IOU-PROBE] 第一次就绪 notifyCount=%d\n", g_notifyCount);
    if (g_notifyCount < 1) {
        fprintf(stderr, "[IOU-PROBE] FAIL: 第一次就绪未回调\n");
        return 3;
    }

    /* 第二次就绪：验证一次性 POLL_ADD 的重挂语义（完成后仍能再唤醒）。
     * 先排空首次数据（属主视角：回调里把数据读走），再写入新数据。 */
    {
        char buf[8];
        ssize_t n = read(fds[0], buf, sizeof(buf)); /* 排空首次数据。 */
        printf("[IOU-PROBE] 排空首次数据 %zd 字节\n", n);
    }
    /* 等一轮，让引擎把「待重挂」的 POLL_ADD 挂上（重挂在 waitForEvents
     * 开头进行，见 ioArmPassive 的调用点）。 */
    XAbstractNetIoRing_waitForEvents_base(ring, 20);
    XAbstractNetIoRing_processReady(ring);
    if (write(fds[1], "y", 1) != 1) return fail("write#2");
    for (i = 0; i < 200 && g_notifyCount < 2; ++i) {
        XAbstractNetIoRing_waitForEvents_base(ring, 20);
        XAbstractNetIoRing_processReady(ring);
    }
    printf("[IOU-PROBE] 第二次就绪 notifyCount=%d\n", g_notifyCount);
    if (g_notifyCount < 2) {
        fprintf(stderr, "[IOU-PROBE] FAIL: 第二次就绪未回调"
                        "（一次性 POLL_ADD 未重挂）\n");
        return 4;
    }

    XAbstractNetIoRing_unregisterPassiveOsFd_base(ring, fds[0]);
    printf("[IOU-PROBE] PASS: io_uring 被动 fd 通道工作正常"
           "（注册/唤醒/重挂/注销 全通）\n");
    return 0;
}
