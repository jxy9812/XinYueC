/**
 * @file       XNetIoRingAndroid.c
 * @brief      安卓 io_uring_setup 探测的平台层实现（seccomp SIGSYS 兜底）。
 * @details    [2026-10-24 分层下沉] 原先这段逻辑内联在共享的
 *             Drive/Posix/Core/XNetIoRingPosix.c 里（`#if defined(__ANDROID__)`
 *             分支），共享驱动不该知道安卓的 seccomp 处置差异，现下沉到平台层。
 *
 *             背景：安卓 seccomp 对被禁系统调用的处置分两派——
 *               - bionic 白名单策略：返回 ENOSYS，可优雅回退 epoll；
 *               - WSA 等定制策略：SECCOMP_RET_TRAP 直发 SIGSYS 杀进程（真机实测）。
 *             探测前置 SIGSYS handler + siglongjmp 兜底，两派都安全落 epoll。
 *             SIGSYS 由本线程的 syscall 同步触发，与其他信号无竞态；
 *             sigsetjmp 的 savesigs=1 由 siglongjmp 恢复原屏蔽字。
 *
 *             注意：io_uring/epoll **双引擎在安卓上照常编译**（与 Linux 桌面
 *             同口径），引擎选择在运行时由 init 回退链决定（内核版本门
 *             ≥5.10 + 本探测），不是编译期一刀切。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if defined(__ANDROID__) && XAbstractNetIoRing_ON

#include "XNetIoRingPosix.h"

#if XNET_BUILD_IO_URING

#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>

/* 确保 io_uring 系统调用号可用（与 POSIX 驱动同口径） */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif

static sigjmp_buf g_ioProbeSigSysJmp;

static void xnetIoProbeSigSysHandler(int sig, siginfo_t* info, void* uctx)
{
    (void)sig; (void)info; (void)uctx;
    siglongjmp(g_ioProbeSigSysJmp, 1);
}

int XAndroid_netIoUringSetup(unsigned entries, struct io_uring_params* params)
{
    struct sigaction trapSa, oldSa;
    memset(&trapSa, 0, sizeof(trapSa));
    trapSa.sa_sigaction = xnetIoProbeSigSysHandler;
    trapSa.sa_flags = SA_SIGINFO;
    sigemptyset(&trapSa.sa_mask);
    if (sigaction(SIGSYS, &trapSa, &oldSa) != 0)
        return (int)syscall(__NR_io_uring_setup, entries, params);
    if (sigsetjmp(g_ioProbeSigSysJmp, 1) == 0) {
        int fd = (int)syscall(__NR_io_uring_setup, entries, params);
        sigaction(SIGSYS, &oldSa, NULL);
        return fd;
    }
    /* SIGSYS 路径：恢复原处置，按「内核不可用」走 epoll 回退 */
    sigaction(SIGSYS, &oldSa, NULL);
    errno = ENOSYS;
    return -1;
}

#endif /* XNET_BUILD_IO_URING */

#endif /* __ANDROID__ && XAbstractNetIoRing_ON */
