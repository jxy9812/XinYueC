/**
 * @file XNetIoRingPosix.c
 * @brief XAbstractNetIoRing Linux 后端实现（io_uring/epoll 双引擎）
 *
 * 继承 XAbstractNetIoRing，Linux 5.1+ 内核运行 io_uring 完成型异步
 * I/O；init 时 io_uring_setup 失败（-ENOSYS，Linux 4.x 及以下内核）
 * 自动回退 epoll 就绪模型（同一结构体、同一虚函数表、外部 API 签名
 * 完全一致，外部调用点零修改）。
 *
 * 双引擎编译期开关（XNetIoRingPosix.h 探测）：
 *   XNET_BUILD_IO_URING  工具链内核头含 linux/io_uring.h 且未强制 epoll
 *   XNET_BUILD_EPOLL     恒为 1（运行时回退引擎，保证低版本内核可用）
 *
 * io_uring 路径核心流程：
 *   pollPlatform:  io_uring CQ 非阻塞轮询 -> processOneCompletion 推 CQ
 *   waitForEvents: io_uring_enter(timeout) 阻塞；wakeUp 经 eventfd
 *
 * epoll 路径语义映射（就绪模型 -> 完成语义）：
 *   网络 RECV/RECVMSG/SEND/ACCEPT/CONNECT：提交时注册 epoll 兴趣挂起，
 *     就绪即执行非阻塞 IO 并推 CQ 条目（对上层等价完成通知）；
 *   文件 READ/WRITE/FSYNC：提交时同步执行（普通文件 epoll 不适用），
 *     完成入队供 waitCqe 立即取回；
 *   唤醒：eventfd 常驻 epoll 集合；定时器：timerfd 以 READ 提交。
 *
 * 外部 API（getSqe/submitSqe/waitCqe/ringFd）双模式分发，调用点
 * （XDeviceFile_posix_api.c / XDeviceNetwork_posix.c 直接构造 SQE）
 * 零修改；无 io_uring 头的老工具链由头文件伪 SQE 兼容层保证编译。
 */

#include "CXinYueConfig.h"
#if XAbstractNetIoRing_ON

#if defined(__linux__)

#include "XAbstractNetIoRing.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XNetIoRingPosix.h"
#include "XFileDescriptor.h"
#include "XSocketDescriptor.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include "XPrintf.h" /* [ILOOP 2026-10-29 诊断] stdout 通道对账打点。 */
#include <unistd.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/utsname.h>
#if defined(__ANDROID__)
#include <android/log.h>
#define XNET_AND_TRACE(...) \
    __android_log_print(ANDROID_LOG_INFO, "XDevNet", __VA_ARGS__)
#else
#define XNET_AND_TRACE(...) do {} while (0)
#endif
#if XNET_BUILD_EPOLL
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif /* XNET_BUILD_EPOLL */
#if XNET_BUILD_IO_URING
#include <sys/syscall.h>
#include <linux/io_uring.h>

/* 确保 io_uring 系统调用号可用 */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif

#if defined(__ANDROID__)
/* [2026-10-24 分层下沉] 安卓的 seccomp 处置差异（ENOSYS vs SIGSYS 直杀）
 * 收口到平台层 Drive/Android/Core/XNetIoRingAndroid.c——共享驱动不该内联
 * 安卓的 seccomp 行为。声明见 XNetIoRingPosix.h。 */
#define guardedIoUringSetup XAndroid_netIoUringSetup
#else
static int guardedIoUringSetup(unsigned entries, struct io_uring_params* params) {
    return (int)syscall(__NR_io_uring_setup, entries, params);
}
#endif /* __ANDROID__ */
#endif /* XNET_BUILD_IO_URING */

/* 单次 pollPlatform 最大处理的事件数，防止饿死其他任务 */
#ifndef XNETIORING_POSIX_POLL_BATCH
#define XNETIORING_POSIX_POLL_BATCH  64
#endif

/* io_uring SQ 条目数 */
#ifndef XNETIORING_POSIX_QUEUE_DEPTH
#define XNETIORING_POSIX_QUEUE_DEPTH  256
#endif

/* epoll 单次等待的最大事件数 */
#ifndef XNET_EPOLL_MAX_EVENTS
#define XNET_EPOLL_MAX_EVENTS 64
#endif

/* 被动可读 fd 记录表容量（2026-10-26 触摸唤醒通道）。 */
#ifndef XNET_PASSIVE_MAX
#define XNET_PASSIVE_MAX 8
#endif

/* [2026-10-03 互联测试指挥官] epoll 引擎逐操作追踪（默认 0）：真机
 * MCGS(5.2.9 内核 memlock 回退 epoll)互联联调缺陷定位用, 编译期
 * -DXNET_EPOLL_IO_DEBUG=1 重开全部输出 —— 仿本仓库 XPWN_IME_DEBUG
 * 收编先例(Drive/Posix/Graphics/XPlatformNativeWindow_posix.c:238)。 */
#ifndef XNET_EPOLL_IO_DEBUG
#define XNET_EPOLL_IO_DEBUG 0
#endif

/* epoll 兴趣/操作封装（与 XSocketActType 解耦的内部常量） */
#define XNET_EPOLL_IN_INTEREST   (uint32_t)EPOLLIN
#define XNET_EPOLL_OUT_INTEREST  (uint32_t)EPOLLOUT
#define XNET_EPOLL_WANT_IN       0
#define XNET_EPOLL_WANT_OUT      1

/* ================================================================
 * 运行时后端模式
 * ================================================================ */
typedef enum XNetIoRingMode {
    XNET_MODE_NONE = 0,   /**< 未初始化 */
    XNET_MODE_IOURING,    /**< io_uring 完成型（Linux 5.1+ 内核） */
    XNET_MODE_EPOLL       /**< epoll 就绪型（低版本内核回退） */
} XNetIoRingMode;

/* ================================================================
 * 完成处理（公共辅助函数，双引擎共用）
 *
 * 将一条完成结果（res + user_data 上下文）转换为 CQ 条目或直接
 * 投递定时器事件。pollPlatform（非阻塞批量）和 waitForEvents
 * （阻塞单条）共用。对应 Win32 的 processOneCompletion。 */

/* ================================================================
 * [wake] 事件循环唤醒延迟探针（诊断, env XGUI_REMOTE_WAKE_PROF 门控）
 *
 * 三段打点（设计稿 out/mcgs-campaign/udp-design.md「事件循环唤醒延迟」）：
 *   ① 阻塞原语进入 → fd 就绪唤醒返回（内核唤醒+syscall 延迟）；
 *   ② fd 就绪唤醒 → 首条读完成回收（环内完成处理延迟）；
 *   ③ 读完成 → 消息解析派发（消息层, 经 XAbstractNetIoRing_profMarkRecv/
 *      profLastRecvUs 与 XGuiServer/XGuiClient 协同, 见 [wake][srv/cli] 行）。
 * 每 5s 汇总一行 [wake][ring]（stderr, P50/P95, 防日志洪水）。探针未开时
 * 全部路径零成本（on 缓存 getenv, 每进程探测一次）。
 * ================================================================ */
#define XNET_WAKE_PROF_SAMPLES 128
#define XNET_WAKE_PROF_WIN_US  5000000ULL
typedef struct XNetWakeSampleSet {
    uint32_t v[XNET_WAKE_PROF_SAMPLES]; /* 样本(µs; 满后滚动覆盖)。 */
    int      n;
} XNetWakeSampleSet;
static struct XNetWakeProfState {
    uint64_t wakeUs;         /* fd 就绪唤醒时刻(0=无待采样)。 */
    uint64_t winStartUs;     /* 5s 汇总窗口起点。 */
    uint32_t fdWakes;        /* fd 就绪唤醒次数。 */
    uint32_t timeoutRounds;  /* 超时醒圈数(定时器节拍兜底证据)。 */
    XNetWakeSampleSet enterWake; /* ①进入阻塞→fd 就绪唤醒。 */
    XNetWakeSampleSet wakeCqe;   /* ②唤醒→首条读完成。 */
} xnet_wakeProf;
/* -1=未探测 env(静态零初始化≠已禁用, 探测一次后 0/1 定版)。 */
static int xnet_wakeProfOn = -1;

/** @brief 探针开关(env 探测一次)。 */
static int xnet_wakeProfEnabled(void) {
    if (xnet_wakeProfOn < 0) {
        const char* env = getenv("XGUI_REMOTE_WAKE_PROF");
        xnet_wakeProfOn = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return xnet_wakeProfOn;
}

/** @brief 样本入集(满后滚动覆盖; 诊断用途)。 */
static void xnet_wakeProfPush(XNetWakeSampleSet* set, uint32_t us) {
    set->v[(set->n < XNET_WAKE_PROF_SAMPLES)
               ? set->n
               : (set->n % XNET_WAKE_PROF_SAMPLES)] = us;
    ++set->n;
}

static int xnet_wakeProfCmpU32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

/** @brief 取百分位(µs; 集内排序副本)。 */
static uint32_t xnet_wakeProfPct(const XNetWakeSampleSet* set, int pctX100) {
    uint32_t buf[XNET_WAKE_PROF_SAMPLES];
    int n = set->n;
    int idx;
    if (n <= 0) return 0;
    if (n > XNET_WAKE_PROF_SAMPLES) n = XNET_WAKE_PROF_SAMPLES;
    memcpy(buf, set->v, (size_t)n * sizeof(uint32_t));
    qsort(buf, (size_t)n, sizeof(uint32_t), xnet_wakeProfCmpU32);
    idx = (n * pctX100) / 10000;
    if (idx >= n) idx = n - 1;
    return buf[idx];
}

/** @brief 5s 窗口汇总输出并复位。 */
static void xnet_wakeProfFlush(const char* mode, uint64_t nowUs) {
    fprintf(stderr,
            "[wake][ring] mode=%s fdWakes=%u enterToWakeUs p50=%u p95=%u "
            "wakeToCqeUs p50=%u p95=%u timeoutRounds=%u\n",
            mode, xnet_wakeProf.fdWakes,
            xnet_wakeProfPct(&xnet_wakeProf.enterWake, 5000),
            xnet_wakeProfPct(&xnet_wakeProf.enterWake, 9500),
            xnet_wakeProfPct(&xnet_wakeProf.wakeCqe, 5000),
            xnet_wakeProfPct(&xnet_wakeProf.wakeCqe, 9500),
            xnet_wakeProf.timeoutRounds);
    xnet_wakeProf.wakeUs = 0;
    xnet_wakeProf.winStartUs = nowUs;
    xnet_wakeProf.fdWakes = 0;
    xnet_wakeProf.timeoutRounds = 0;
    xnet_wakeProf.enterWake.n = 0;
    xnet_wakeProf.wakeCqe.n = 0;
}

static void processOneCompletion(XAbstractNetIoRing* self, int64_t res,
                                 void* userData) {
    XEventContext* ctx = (XEventContext*)(uintptr_t)userData;
    /* user_data==0 表示 wakeUp 唤醒，无完成需处理（对应 Win32 overlapped==NULL） */
    if (!ctx) return;

    ctx->result = res;
    /* [wake] 探针: 套接字读完成打点(方向以 eventMask 区分, SEND 完成不
     * 污染到达时戳) + 「唤醒→首条读完成」段采样。 */
    if (xnet_wakeProfEnabled() && res > 0 &&
        ctx->type == XEventContextType_Type_Socket &&
        ctx->eventMask == XSocketActType_Read) {
        XAbstractNetIoRing_profMarkRecv(ctx->fd);
        if (xnet_wakeProf.wakeUs) {
            xnet_wakeProfPush(&xnet_wakeProf.wakeCqe,
                              (uint32_t)(XAbstractNetIoRing_profNowUs() -
                                         xnet_wakeProf.wakeUs));
            xnet_wakeProf.wakeUs = 0;
        }
    }
    if (ctx->type == XEventContextType_Type_Timer) {
        /* 定时器完成：直接投递定时器事件到应用层 */
        XFd timerFd = ctx->fd;
        if (timerFd != XFD_INVALID) {
            XTimerEvent* timerEv = XTimerEvent_create((XTimerId)timerFd);
            XEvent* timerEvent = (XEvent*)timerEv;
            if (timerEvent) {
                timerEvent->m_posted = true;
                timerEvent->m_spontaneous = true;
                /* 从 fd 表获取 owner */
                XFileDescriptor* desc = XFd_get(timerFd);
                if (desc && desc->object) {
                    XCoreApplication_postEvent((XObject*)desc->object,
                                               timerEvent, XEVENT_PRIORITY_NORMAL);
                }
            }
        }
    }
    else if (ctx->type == XEventContextType_Type_Socket ||
             ctx->type == XEventContextType_Type_File) {
        /* Socket/File I/O 完成：转换为 CQ 条目 */
        XAbstractNetIoRing_CQEntry cqEntry;
        memset(&cqEntry, 0, sizeof(cqEntry));

        cqEntry.m_fd = ctx->fd;
        cqEntry.m_bytes = (res >= 0) ? (uint32_t)res : 0;
        cqEntry.m_error = (res < 0) ? -res : 0;
        cqEntry.m_sourceType = XAbstractNetIoRing_Source_NativeIO;
        cqEntry.m_fdType = XFd_type(ctx->fd);

        /* [2026-10-23 对端 FIN 误丢根修] 调度层假 EOF 复核此前拿
         * entry->m_fd（XFd fd 表槽位号）当 OS fd 做 MSG_PEEK——两者是
         * 两个编号空间且常不相等（本套接字实测槽位 2 / OS fd 8），PEEK
         * 打到无关 fd（如 stderr）恒 ENOTSOCK≠0，真对端 FIN 被当假 EOF
         * 丢弃，SOCK_CLOSE 永不下发（epoll 引擎 xmqtt-tcp-api 断开三例
         * 实证）。此处对流式套接字完成把真实 OS fd 带上（Type_Socket
         * 上下文按契约皆为 XEventContext_IO，socket 槽即 OS fd，网络
         * 设备四处提交点均填）；非流式（数据报 0 长报文不是 FIN）或
         * 上下文缺失时置 -1，调度层跳过关闭判定。 */
        cqEntry.m_osFd = -1;
        if (res == 0 && ctx->type == XEventContextType_Type_Socket &&
            ctx->eventMask == XSocketActType_Read) {
            XEventContext_IO* ioCtx = (XEventContext_IO*)ctx;
            int osFd = (int)XSocketDescriptor_toIntptr(ioCtx->socket);
            int soType = 0;
            socklen_t soLen = sizeof(soType);
            if (osFd >= 0 &&
                getsockopt(osFd, SOL_SOCKET, SO_TYPE,
                           &soType, &soLen) == 0 &&
                soType == SOCK_STREAM)
                cqEntry.m_osFd = osFd;
        }

        /* XSocketActType 事件掩码直接使用 ctx->eventMask（与 Windows 不同，
         * io_uring 下 eventMask 由调用者直接设置为 XSocketActType 值） */
        cqEntry.m_events = ctx->eventMask;

        /* 取消请求只用于回收 user_data，不能在 fd 槽位复用后变成一条
         * 新的连接事件。上下文结果仍在上面写回，供关闭路径确认 CQE 已
         * 被消费。 */
        if (ctx->eventMask == 0)
            return;

        /* I/O 失败时：连接丢失等错误也标记为 Connect 事件 */
        if (res < 0 && cqEntry.m_events == 0) {
            cqEntry.m_events = XSocketActType_Connect;
        }

        /* [2026-10-26 物理断链(拔网线)不恢复根修·平台层]
         *
         * 拔网线是「链路层静默」断链：对端不发 FIN/RST，本机既无读完成
         * （对端无数据）也无写错误（数据进内核发送缓冲，未收到 RST 即无
         * EPIPE），socket 永远停在 ESTABLISHED ⇒ 平台层无事件可报 ⇒
         * 框架层 disconnected 信号永不发出 ⇒ 上层自动重连永不启动。
         *
         * Windows 无此症状：其 IOCP 后端在链路失效时让挂起的 WSARecv/
         * WSASend 失败并投递错误完成，框架据此断链。本平台（POSIX）必须
         * 提供等价能力，否则 Modbus/远程屏/NTP/HTTP 全部卡在假连接态。
         *
         * 缺口定位：上方「I/O 失败时标记为 Connect 事件」判据是
         * `cqEntry.m_events == 0`，而本引擎的 m_events 直接取
         * ctx->eventMask（Read=1/Write=2，恒非 0）⇒ 该分支是死代码，
         * 套接字 I/O 错误（EPIPE/ECONNRESET/ETIMEDOUT…）被**静默丢弃**：
         * 实证拔线场景下写完成恒 result=-32(EPIPE)，而设备状态恒为
         * 「已连接」、断链事件永不产生。
         *
         * 修法（对齐 Windows 语义）：套接字的读/写完成若为「链路已死」
         * 类错误（见 XNetIoRingPosix_isLinkDeadErrno 白名单），一律按
         * 「连接已断」处理——把事件掩码补上 Connect 位，使框架的 Connect
         * 分支读到 m_connected=false 并走既有断开路径（disconnected 信号
         * → 上层自动重连），与 Windows 的「I/O 失败时：连接丢失等错误也
         * 标记为 Connect 事件」同构。
         *
         * 用 errno 白名单而非「任何 res<0」：EAGAIN/EWOULDBLOCK（资源暂
         * 不可用）、EINTR（被信号中断）、ENOMEM（资源不足）等瞬态错误
         * 不是链路死亡，误判会把正常连接当断链而反复重连。
         * 只处理网络套接字（ctx->type == Socket）：文件 I/O 失败语义不同
         * （读文件遇 EIO 不等于连接断开），不得混入断链路径。
         */
        if (res < 0 &&
            ctx->type == XEventContextType_Type_Socket &&
            XNetIoRingPosix_isLinkDeadErrno((int)(-res))) {
            cqEntry.m_events |= XSocketActType_Connect;
        }

        ctx->finishedBytes = (res >= 0) ? (size_t)res : 0;
        XAbstractNetIoRing_pushCompletion(self, &cqEntry);
    }
}

/* ================================================================
 * 主结构体（双引擎字段集，按编译开关收编）
 * ================================================================ */

/** @brief 挂起（等待 fd 就绪）的伪 SQE 请求（epoll 引擎）。 */
typedef struct XNetPendingOp {
    XNetSqe sqe;    /**< 伪/真 SQE 副本（提交时快照） */
    XEventContext* ctx;         /**< 完成上下文（= sqe.user_data） */
    struct XNetPendingOp* next; /**< 链表后继 */
} XNetPendingOp;

typedef struct XNetIoRingPosix {
    XAbstractNetIoRing m_class;   /**< 基类（必须位于第一位） */
    XNetIoRingMode m_mode;        /**< 运行时后端模式 */
    int     m_wakeFd;             /**< 跨线程唤醒 eventfd（双引擎共用） */
    bool    m_ownsRing;           /**< 是否拥有后端资源 */
#if XNET_BUILD_IO_URING
    int     m_ringFd;             /**< io_uring 环形缓冲区 fd */
    /* io_uring 内存映射区域 */
    unsigned* m_sqHeadPtr;        /**< SQ head 指针（内核写入） */
    unsigned* m_sqTailPtr;        /**< SQ tail 指针（用户空间写入） */
    unsigned* m_sqMaskPtr;        /**< SQ ring mask */
    unsigned* m_sqFlagsPtr;       /**< SQ ring flags */
    unsigned* m_sqArray;          /**< SQ 索引数组 */
    XNetCqe* m_cqes;  /**< CQ 条目数组 */
    unsigned* m_cqHeadPtr;        /**< CQ head 指针（用户空间写入） */
    unsigned* m_cqTailPtr;        /**< CQ tail 指针（内核写入） */
    unsigned* m_cqMaskPtr;        /**< CQ ring mask */
    XNetSqe* m_sqes;  /**< SQ 条目数组 */
    unsigned m_sqEntries;         /**< SQ 条目数 */
    /* 被动可读 fd（io_uring 侧，2026-10-26）：POLL_ADD 纳管，就绪时
     * 由内核产生 CQE（user_data 指向各自的 XEventContext，type=Custom
     * 标识「非网络完成」）。POLL_ADD 是一次性的，完成后须重新提交
     * 才能继续监听（见 ioArmPassive）。 */
    struct {
        int  m_osFd;               /**< 裸 OS fd（内核编号空间）。 */
        void (*m_notify)(int);    /**< 就绪回调（NULL=仅唤醒不通知）。 */
        XEventContext m_ctx;      /**< 专属完成上下文（user_data 指向它）。 */
        bool m_armed;              /**< multishot POLL_ADD 是否在飞。 */
        bool m_multiFailed;        /**< multishot 被内核拒绝（5.10~5.12）⇒ 不再重试。 */
    } m_passiveIo[XNET_PASSIVE_MAX];
    int     m_passiveIoCount;     /**< 已注册被动 fd 数（io_uring 侧）。 */
#endif /* XNET_BUILD_IO_URING */
#if XNET_BUILD_EPOLL
    int     m_epollFd;            /**< epoll 实例 fd */
    XNetSqe m_currentSqe; /**< getSqe 单槽暂存（外部填充） */
    bool    m_hasCurrentSqe;      /**< 暂存槽是否有效 */
    XNetPendingOp* m_pendingHead; /**< 挂起请求链表头 */
    /* 被动可读 fd 记录（2026-10-26 触摸唤醒通道）：这些 fd 由平台层
     * （fbdev 触摸泵）自己打开，不在 XFd 表内、也没有挂起的网络请求——
     * 只作为「可读即唤醒事件循环」的等待项。就绪时按记录回调 notify。 */
    struct {
        int  m_osFd;               /**< 裸 OS fd（内核编号空间）。 */
        void (*m_notify)(int);    /**< 就绪回调（NULL=仅唤醒不通知）。 */
    } m_passive[XNET_PASSIVE_MAX];
    int     m_passiveCount;       /**< 已注册被动 fd 数。 */
#endif /* XNET_BUILD_EPOLL */
} XNetIoRingPosix;

/* 前置声明（结构体定义在 XNetIoRingPosix.c）——保持 .h 兼容 */

/* ==================== 前向声明 ==================== */
static XFd   VXNetIoRingPosix_getEventFd(XAbstractNetIoRing* self);
static void  VXNetIoRingPosix_pollPlatform(XAbstractNetIoRing* self);
static bool  VXNetIoRingPosix_registerEvent(XAbstractNetIoRing* self, XFd fd);
static bool  VXNetIoRingPosix_registerPassiveOsFd(XAbstractNetIoRing* self,
                                                   int osFd,
                                                   void (*notify)(int osFd));
static bool  VXNetIoRingPosix_unregisterPassiveOsFd(XAbstractNetIoRing* self,
                                                     int osFd);
static void  VXNetIoRingPosix_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void  VXNetIoRingPosix_wakeUp(XAbstractNetIoRing* self);
static void  VXNetIoRingPosix_deinit(XAbstractNetIoRing* obj);

/* ================================================================
 * io_uring 引擎（XNET_BUILD_IO_URING 时编译）
 * ================================================================ */
#if XNET_BUILD_IO_URING

/* 获取 SQ 空闲槽位索引，返回 SQE 指针 */
static XNetSqe* ioGetSqeSlot(XNetIoRingPosix* posix) {
    if (!posix->m_sqHeadPtr || !posix->m_sqTailPtr || !posix->m_sqMaskPtr || !posix->m_sqes)
        return NULL;
    unsigned head = __atomic_load_n(posix->m_sqHeadPtr, __ATOMIC_ACQUIRE);
    unsigned tail = __atomic_load_n(posix->m_sqTailPtr, __ATOMIC_RELAXED);
    unsigned next = tail + 1;

    /* SQ 已满 */
    if (next - head > posix->m_sqEntries)
        return NULL;

    unsigned index = tail & (*posix->m_sqMaskPtr);
    return &posix->m_sqes[index];
}

/* 提交已填充的 SQE 到内核 */
static void ioSubmitEntries(XNetIoRingPosix* posix, int toSubmit) {
    unsigned tail = __atomic_load_n(posix->m_sqTailPtr, __ATOMIC_RELAXED);
    unsigned startIndex = tail & (*posix->m_sqMaskPtr);

    /* 写入 SQ array 映射 */
    for (int i = 0; i < toSubmit; i++) {
        posix->m_sqArray[(startIndex + i) & (*posix->m_sqMaskPtr)] =
            (startIndex + i) & (*posix->m_sqMaskPtr);
    }

    /* 推进 SQ tail（发布到内核） */
    __atomic_store_n(posix->m_sqTailPtr, tail + toSubmit, __ATOMIC_RELEASE);

    /* 非 SQPOLL 模式必须调用 io_uring_enter 才会真正提交 SQE。
     * 防御性修复(2026-10-02 延迟基准): 返回值此前被忽略——EINTR 时 SQE
     * 不会提交, RECV 将静默滞留至下一次 enter(表现收侧无界延迟)。
     * EINTR 重试; 其余短提交回滚 tail(调用方 readPending 状态保持,
     * 下一事件重试), 语义与提交前一致。 */
    {
        long done;
        do {
            done = syscall(__NR_io_uring_enter, posix->m_ringFd, toSubmit,
                           0, 0, NULL, 0);
        } while (done < 0 && errno == EINTR);
        if (done != toSubmit && done >= 0) {
            __atomic_store_n(posix->m_sqTailPtr, tail, __ATOMIC_RELEASE);
        }
    }
}

/* 从 CQ 获取一个完成条目，非阻塞 */
static XNetCqe* ioPeekCqe(XNetIoRingPosix* posix) {
    unsigned head = __atomic_load_n(posix->m_cqHeadPtr, __ATOMIC_ACQUIRE);
    unsigned tail = __atomic_load_n(posix->m_cqTailPtr, __ATOMIC_ACQUIRE);

    if (head == tail)
        return NULL;  /* CQ 为空 */

    return &posix->m_cqes[head & (*posix->m_cqMaskPtr)];
}

/* 推进 CQ head（标记已消费） */
static void ioAdvanceCq(XNetIoRingPosix* posix) {
    unsigned head = __atomic_load_n(posix->m_cqHeadPtr, __ATOMIC_RELAXED);
    __atomic_store_n(posix->m_cqHeadPtr, head + 1, __ATOMIC_RELEASE);
}

/**
 * @brief  挂载一个 multishot POLL_ADD（io_uring 侧被动 fd 通道，2026-10-26）。
 * @details IORING_POLL_ADD_MULTI（内核 5.13+）：**一次挂载持续生效**——fd
 *          每次就绪产生一条 CQE（带 IORING_CQE_F_MORE），**无需重挂**。
 *
 *          为什么不用一次性 POLL_ADD + 完成重挂（初版实现）：真机桌面
 *          （6.18）实测「同一 ring 上第 3 次挂等同 fd」不再触发（SQE
 *          字节级与裸程序一致、内核 enter 消费正常、CQE 却永不再来；
 *          同进程新建 ring 同序列正常；裸 ABI 复刻全序列正常）——一次性
 *          重挂路径在该内核上存在未定根因的坑。multishot 一次挂载绕开
 *          重挂语义，且天然无「重挂时数据未读空 → 立即就绪 → CQ 风暴」
 *          问题（每轮至多一条 CQE）。
 *
 *          版本兼容：multishot 需内核 ≥5.13；版本门 ≥5.10 的 5.10~5.12
 *          内核不支持 ⇒ ioArmPassive 失败（CQE res=-EINVAL）时回退登记
 *          为「不支持」，调用方保留轮询兜底。探测在首次挂载的 CQE 到达
 *          时完成（res<0 即 multishot 不可用）。
 *
 * @return true=已提交；false=SQ 无空槽（下次尝试）。
 */
static bool ioArmPassive(XNetIoRingPosix* posix, int idx) {
    XNetSqe* sqe;
    if (!posix || idx < 0 || idx >= posix->m_passiveIoCount) return false;
    sqe = ioGetSqeSlot(posix);
    if (!sqe) return false;               /* SQ 满：下次再挂。 */
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_POLL_ADD;
    sqe->fd = posix->m_passiveIo[idx].m_osFd;
    sqe->poll_events = POLLIN;
#ifndef IORING_POLL_ADD_MULTI
#define IORING_POLL_ADD_MULTI (1U << 0)   /* 内核 5.13+；老工具链头缺失时补定义 */
#endif
    sqe->len = IORING_POLL_ADD_MULTI;     /* 内核 5.13+：POLL_ADD 命令标志存 sqe->len */
    sqe->user_data = (uint64_t)(uintptr_t)&posix->m_passiveIo[idx].m_ctx;
    posix->m_passiveIo[idx].m_armed = true;
    ioSubmitEntries(posix, 1);
    return true;
}

/**
 * @brief  分派一条「被动 fd 完成」CQE（io_uring 侧）。
 * @details multishot 下**无需重挂**：挂载持续生效，回调只清「待重挂」
 *          语义不存在——armed 保持 true。若该完成携带错误（multishot
 *          不被内核支持，res<0），置 m_multiFailed ⇒ 不再重试挂载，
 *          registerPassiveOsFd 的调用方语义退化为轮询兜底。
 */
static void ioDispatchPassive(XNetIoRingPosix* posix, int idx, int res) {
    void (*notify)(int);
    int osFd;
    if (!posix || idx < 0 || idx >= posix->m_passiveIoCount) return;
    if (res < 0)
    {
        /* multishot 不被支持（-EINVAL 等，内核 5.10~5.12）：登记失败，
         * 注销该 fd 的等待（POLL_REMOVE），调用方语义回落轮询兜底。 */
        posix->m_passiveIo[idx].m_armed = false;
        posix->m_passiveIo[idx].m_multiFailed = true;
        if (posix->m_ringFd >= 0) {
            XNetSqe* sqe = ioGetSqeSlot(posix);
            if (sqe) {
                memset(sqe, 0, sizeof(*sqe));
                sqe->opcode = IORING_OP_POLL_REMOVE;
                sqe->fd = -1;
                sqe->addr = (uint64_t)(uintptr_t)
                            &posix->m_passiveIo[idx].m_ctx;
                sqe->user_data = 0;
                ioSubmitEntries(posix, 1);
            }
        }
        return;
    }
    notify = posix->m_passiveIo[idx].m_notify;
    osFd = posix->m_passiveIo[idx].m_osFd;
    if (notify) notify(osFd);
}

/* 非阻塞轮询 CQ（io 引擎） */
static void ioPollPlatform(XNetIoRingPosix* posix, XAbstractNetIoRing* self) {
    int batchCount = 0;
    if (posix->m_ringFd < 0) return;
    while (batchCount < XNETIORING_POSIX_POLL_BATCH) {
        XNetCqe* cqe = ioPeekCqe(posix);
        XEventContext* ctx;
        if (!cqe) break;
        ctx = (XEventContext*)(uintptr_t)cqe->user_data;
        /* 被动 fd 完成（2026-10-26）：ctx->type == Type_Custom 标识「非网络
         * 完成」（被动 fd 专属上下文）——按设备类型标注区分，不靠哨兵值。
         * 该完成不进入网络完成链（processOneCompletion 会把它当 socket/
         * file 完成解释），改走被动 fd 分派 + 重挂 POLL_ADD。 */
        if (ctx && ctx->type == XEventContextType_Type_Custom) {
            int idx;
            for (idx = 0; idx < posix->m_passiveIoCount; ++idx) {
                if (&posix->m_passiveIo[idx].m_ctx == ctx) break;
            }
            ioAdvanceCq(posix);
            if (idx < posix->m_passiveIoCount)
                ioDispatchPassive(posix, idx, (int)cqe->res);
            ++batchCount;
            continue;
        }
        processOneCompletion(self, cqe->res,
                             (void*)(uintptr_t)cqe->user_data);
        ioAdvanceCq(posix);
        batchCount++;
    }
}

/* 使用 io_uring_setup 创建实例并 mmap 环形缓冲区 */
static bool ioSetupRing(XNetIoRingPosix* ring) {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    params.flags = 0;

    int ringFd = guardedIoUringSetup(XNETIORING_POSIX_QUEUE_DEPTH, &params);
    if (ringFd < 0) return false;

    /* [2026-10-12 MCGS 屏 accept 僵死根修] 能力门：内核无
     * IORING_FEAT_FAST_POLL(<5.5，如 MCGS A33 5.2.9) 时，网络型 SQE
     * (ACCEPT/RECV/SEND/CONNECT) 缺内核内部 poll 重试——ACCEPT 无连接
     * 立即 -EINVAL 完成、连接到达也不产生 CQE，挂一次即永久失聪
     * (实测探针闭环)。该门必须回退 epoll 路径(2026-10-03 验证过的
     * 配置)，由调用方 epoll 回退建环。 */
    if (!(params.features & IORING_FEAT_FAST_POLL)) {
        close(ringFd);
        return false;
    }

    ring->m_ringFd = ringFd;
    ring->m_sqEntries = params.sq_entries;
    ring->m_ownsRing = true;

    size_t sqRingSize = (size_t)params.sq_off.array + params.sq_entries * sizeof(unsigned);
    void* sqPtr = mmap(0, sqRingSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_SQ_RING);
    if (sqPtr == MAP_FAILED) goto fail;

    ring->m_sqHeadPtr  = (unsigned*)((char*)sqPtr + params.sq_off.head);
    ring->m_sqTailPtr  = (unsigned*)((char*)sqPtr + params.sq_off.tail);
    ring->m_sqMaskPtr  = (unsigned*)((char*)sqPtr + params.sq_off.ring_mask);
    ring->m_sqFlagsPtr = (unsigned*)((char*)sqPtr + params.sq_off.flags);
    ring->m_sqArray    = (unsigned*)((char*)sqPtr + params.sq_off.array);

    size_t cqRingSize = (size_t)params.cq_off.cqes + params.cq_entries * sizeof(XNetCqe);
    void* cqPtr = mmap(0, cqRingSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_CQ_RING);
    if (cqPtr == MAP_FAILED) goto fail;

    ring->m_cqHeadPtr = (unsigned*)((char*)cqPtr + params.cq_off.head);
    ring->m_cqTailPtr = (unsigned*)((char*)cqPtr + params.cq_off.tail);
    ring->m_cqMaskPtr = (unsigned*)((char*)cqPtr + params.cq_off.ring_mask);
    ring->m_cqes      = (XNetCqe*)((char*)cqPtr + params.cq_off.cqes);

    size_t sqesSize = params.sq_entries * sizeof(XNetSqe);
    void* sqesPtr = mmap(0, sqesSize, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_SQES);
    if (sqesPtr == MAP_FAILED) goto fail;

    ring->m_sqes = (XNetSqe*)sqesPtr;

    ring->m_wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (ring->m_wakeFd < 0) goto fail;

    return true;

fail:
    if (ring->m_sqHeadPtr) { munmap(ring->m_sqHeadPtr, sqRingSize); ring->m_sqHeadPtr = NULL; }
    if (ring->m_cqHeadPtr) { munmap(ring->m_cqHeadPtr, cqRingSize); ring->m_cqHeadPtr = NULL; }
    if (ring->m_sqes)      { munmap(ring->m_sqes, sqesSize);      ring->m_sqes = NULL; }
    if (ring->m_wakeFd >= 0) { close(ring->m_wakeFd); ring->m_wakeFd = -1; }
    close(ringFd);
    ring->m_ringFd = -1;
    ring->m_ownsRing = false;
    return false;
}

/* io 引擎资源清理 */
static void ioDeinitCleanup(XNetIoRingPosix* posix) {
    if (posix->m_ownsRing && posix->m_ringFd >= 0) {
        size_t sqRingSize = (size_t)(posix->m_sqArray) -
                            (size_t)(posix->m_sqHeadPtr) +
                            posix->m_sqEntries * sizeof(unsigned);
        size_t cqRingSize = (size_t)(posix->m_cqMaskPtr) -
                            (size_t)(posix->m_cqHeadPtr) +
                            sizeof(unsigned) +
                            posix->m_sqEntries * sizeof(XNetCqe);
        size_t sqesSize = posix->m_sqEntries * sizeof(XNetSqe);

        if (posix->m_sqHeadPtr) {
            munmap(posix->m_sqHeadPtr, sqRingSize);
            posix->m_sqHeadPtr = NULL;
        }
        if (posix->m_cqHeadPtr) {
            munmap(posix->m_cqHeadPtr, cqRingSize);
            posix->m_cqHeadPtr = NULL;
        }
        if (posix->m_sqes) {
            munmap(posix->m_sqes, sqesSize);
            posix->m_sqes = NULL;
        }
        close(posix->m_ringFd);
    }
    posix->m_ringFd = -1;
    posix->m_ownsRing = false;
}
#endif /* XNET_BUILD_IO_URING */

/* ================================================================
 * epoll 引擎（恒编译：io_uring 失败的运行时回退）
 * ================================================================ */
#if XNET_BUILD_EPOLL

/** @brief 提交方向归类：SEND/SENDMSG/CONNECT 出向（EPOLLOUT），其余入向。
 *  @note  [2026-10-22 NTP 应答超时根修] SENDMSG（定向数据报，msg_name 携带
 *         目的地址）此前未列入出向——epoll 引擎三处方向判定（submitSqe/
 *         epRearmInterest/epTakePending）都会把它当入向挂 EPOLLIN，
 *         OUT 就绪永不上报，定向发送永久滞留。统一本入口避免三处漂移。 */
static unsigned xnetEpollDirection(unsigned opcode) {
    if (opcode == IORING_OP_SEND || opcode == IORING_OP_SENDMSG ||
        opcode == IORING_OP_CONNECT)
        return XNET_EPOLL_WANT_OUT;
    return XNET_EPOLL_WANT_IN;
}

/** @brief 从挂起链中按 fd 与兴趣方向摘除一个请求（无则 NULL）。 */
static XNetPendingOp* epTakePending(XNetIoRingPosix* posix, int fd,
                                    int wantOut) {
    XNetPendingOp** pp = &posix->m_pendingHead;
    while (*pp) {
        XNetPendingOp* op = *pp;
        if (op->sqe.fd == fd) {
            if (xnetEpollDirection(op->sqe.opcode) == wantOut) {
                *pp = op->next;
                return op;
            }
        }
        pp = &op->next;
    }
    return NULL;
}

/** @brief [2026-10-03 互联测试指挥官修复归因] 按 fd 挂起请求方向重铸 epoll 兴趣。
 *  @details 缺陷实证(桌面 ulimit -l 8 强制 epoll 引擎复现; 设备真机 5.2.9
 *           内核同走 epoll 回退同症): 同 fd 同时挂异向请求时——服务端建
 *           会话即「RECV(IN) 挂起 + 握手 SEND(OUT) 提交」——提交路径
 *           EPOLL_CTL_ADD 撞 EEXIST 被静默忽略, 兴趣仍停在 IN, OUT 事件
 *           永不上报 → SEND 永久滞留, 客户端停在 connecting 等不到 XGR1
 *           握手横幅; 完成路径无条件 DEL 也会饿死同 fd 其余方向挂起请求。
 *           本函数按挂起链方向并集重铸(MOD/DEL): 单请求场景行为不变
 *           (提交 ADD/完成 DEL), 多请求场景按并集。仅动 epoll 引擎兴趣
 *           管理, 不改 io_uring 路径与任何头文件声明。 */
static void epRearmInterest(XNetIoRingPosix* posix, int fd) {
    XNetPendingOp* op;
    unsigned wantIn = 0;
    unsigned wantOut = 0;
    uint32_t mask = 0;
    struct epoll_event ev;
    for (op = posix->m_pendingHead; op; op = op->next) {
        if (op->sqe.fd != fd) continue;
        if (xnetEpollDirection(op->sqe.opcode) == XNET_EPOLL_WANT_OUT)
            wantOut = 1;
        else
            wantIn = 1;
    }
    if (wantIn) mask |= XNET_EPOLL_IN_INTEREST;
    if (wantOut) mask |= XNET_EPOLL_OUT_INTEREST;
    if (mask == 0u) {
        epoll_ctl(posix->m_epollFd, EPOLL_CTL_DEL, fd, NULL);
        return;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = mask;
    ev.data.fd = fd;
    epoll_ctl(posix->m_epollFd, EPOLL_CTL_MOD, fd, &ev);
}

/** @brief 执行挂起请求的 IO（fd 已就绪），完成推 CQ 条目。
 *  @return true 已完成并释放；false 仍在挂起（EAGAIN）。 */
static bool epExecutePending(XNetIoRingPosix* posix, XNetPendingOp* op,
                             XAbstractNetIoRing* self) {
    const XNetSqe* sqe = &op->sqe;
    ssize_t res;
    errno = 0;
    switch (sqe->opcode) {
    case IORING_OP_RECV:
        res = recv(sqe->fd, (void*)(uintptr_t)sqe->addr, sqe->len, 0);
        break;
    case IORING_OP_RECVMSG:
        res = recvmsg(sqe->fd, (struct msghdr*)(uintptr_t)sqe->addr, 0);
        break;
    case IORING_OP_SENDMSG:
        /* [2026-10-22 NTP 应答超时根修] 定向数据报执行：msghdr 的 msg_name
         * 携带目的地址（sqe->addr 指向驻留在 socket 上下文中的 msghdr）。
         * 与 RECVMSG 对称，也是 SEND 携带目的地址这一 6.x 契约的全内核
         * （5.1+）等价替代——epoll 引擎由此获得定向发送能力。 */
        res = sendmsg(sqe->fd, (struct msghdr*)(uintptr_t)sqe->addr, 0);
        break;
    case IORING_OP_SEND:
        res = send(sqe->fd, (const void*)(uintptr_t)sqe->addr, sqe->len,
                   MSG_NOSIGNAL);
        break;
    case IORING_OP_ACCEPT:
        res = accept4(sqe->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        break;
    case IORING_OP_CONNECT: {
        int err = 0;
        socklen_t errLen = sizeof(err);
        if (getsockopt(sqe->fd, SOL_SOCKET, SO_ERROR, &err, &errLen) == 0)
            res = (err == 0) ? 0 : -err;
        else
            res = -errno;
        break;
    }
    default:
        res = -EINVAL;
        break;
    }

    /* [2026-10-26 物理断链根修·平台层] errno 归一化：recv/send/sendmsg/
     * accept4 等系统调用失败时返回 -1，真实原因在 errno 里。原实现直接把
     * -1 当完成码上抛，下游无法区分「链路已死」(EPIPE/ECONNRESET/…) 与
     * 「瞬态不可用」(EAGAIN)，断链判定因此失效——实测 epoll 引擎拔网线
     * 场景写完成恒 result=-1，断链事件永不产生，设备状态恒为「已连接」。
     * 此处归一为 -errno，与 io_uring 后端（CQE 直接给 -errno）口径一致，
     * 两引擎行为对齐。
     * 判据用「系统调用返回值恰为 -1」而非 res<0：default 分支的 -EINVAL
     * 与 CONNECT 分支的 -err/-errno 都已是归一化码，不可二次取反。 */
    if (res == -1 && errno != 0) {
        res = -errno;
    }

#if XNET_EPOLL_IO_DEBUG
    fprintf(stderr, "[XNET-EPOLL] exec op=%d fd=%d res=%zd errno=%d\n",
            (int)sqe->opcode, sqe->fd, res, errno);
    XNET_AND_TRACE("epoll exec: op=%d fd=%d res=%zd errno=%d",
                   (int)sqe->opcode, sqe->fd, (ssize_t)res, errno);
#endif
    if (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /* [2026-10-03 互联测试指挥官修复归因] 摘除后未完成必须挂回链头:
         * epTakePending 已把本请求从挂起链摘下, EAGAIN 原样 return 会令
         * 请求孤儿化——既不在链上(重铸/再派发都找不到)也无完成事件,
         * 后续 rearm 视该 fd 无挂起而 DEL 兴趣, 连接双向永久饿死
         * (实证: 桌面 ulimit 复现, 服务器 PING EAGAIN 孤儿化后
         *  ~30s 保活双端互踢; 真机 MCGS 同症)。挂回链头, 语义与
         * 摘除前一致(仍在挂起, 等下一次就绪)。 */
        op->next = posix->m_pendingHead;
        posix->m_pendingHead = op;
        return false; /* 仍在挂起，等下一次就绪 */
    }

    /* 完成：按剩余挂起请求重铸兴趣、推 CQ 条目并释放
     * （[2026-10-03 互联测试指挥官] 原无条件 DEL 会饿死同 fd 异向挂起
     *   请求——见 epRearmInterest 处注释; 单请求场景 DEL 等价）。 */
    epRearmInterest(posix, sqe->fd);
    processOneCompletion(self, (int64_t)res,
                         (void*)(uintptr_t)sqe->user_data);
    XFree_System(op);
    return true;
}

/** @brief 文件类操作同步执行（普通文件 epoll 不适用），完成入队。 */
static void epExecuteFileSync(XNetIoRingPosix* posix,
                              XAbstractNetIoRing* self,
                              const XNetSqe* sqe) {
    ssize_t res;
    (void)posix;
    errno = 0;
    switch (sqe->opcode) {
    case IORING_OP_READ:
        /* 先按偏移读（普通文件）；ESPIPE（管道/事件fd）回退当前位置读。 */
        res = pread(sqe->fd, (void*)(uintptr_t)sqe->addr, sqe->len,
                    (off_t)sqe->off);
        if (res < 0 && errno == ESPIPE)
            res = read(sqe->fd, (void*)(uintptr_t)sqe->addr, sqe->len);
        break;
    case IORING_OP_WRITE:
        res = pwrite(sqe->fd, (const void*)(uintptr_t)sqe->addr, sqe->len,
                     (off_t)sqe->off);
        if (res < 0 && errno == ESPIPE)
            res = write(sqe->fd, (const void*)(uintptr_t)sqe->addr, sqe->len);
        break;
    case IORING_OP_FSYNC:
        res = (fsync(sqe->fd) == 0) ? 0 : -errno;
        break;
    default:
        res = -EINVAL;
        break;
    }
    processOneCompletion(self, (int64_t)res,
                         (void*)(uintptr_t)sqe->user_data);
}

/** @brief 非阻塞 epoll 轮询并执行就绪请求。 */
static void epPollPlatform(XNetIoRingPosix* posix, XAbstractNetIoRing* self) {
    struct epoll_event events[XNET_EPOLL_MAX_EVENTS];
    int n;
    int i;
    if (posix->m_epollFd < 0) return;
    do {
        n = epoll_wait(posix->m_epollFd, events, XNET_EPOLL_MAX_EVENTS, 0);
    } while (n < 0 && errno == EINTR);
    for (i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        if (fd == posix->m_wakeFd) {
            uint64_t value;
            while (read(posix->m_wakeFd, &value, sizeof(value)) < 0 &&
                   errno == EINTR) {}
            continue;
        }
        {
            XNetPendingOp* op = epTakePending(posix, fd, XNET_EPOLL_WANT_IN);
            if (op) epExecutePending(posix, op, self);
        }
        if (events[i].events & XNET_EPOLL_OUT_INTEREST) {
            XNetPendingOp* op = epTakePending(posix, fd, XNET_EPOLL_WANT_OUT);
            if (op) epExecutePending(posix, op, self);
        }
    }
}

/** @brief 创建 epoll 实例并注册唤醒 eventfd。 */
static bool epSetup(XNetIoRingPosix* ring) {
    struct epoll_event ev;
    int epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd < 0) return false;
    ring->m_epollFd = epollFd;
    ring->m_ownsRing = true;

    ring->m_wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (ring->m_wakeFd < 0) {
        close(epollFd);
        ring->m_epollFd = -1;
        ring->m_ownsRing = false;
        return false;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = XNET_EPOLL_IN_INTEREST;
    ev.data.fd = ring->m_wakeFd;
    if (epoll_ctl(ring->m_epollFd, EPOLL_CTL_ADD, ring->m_wakeFd, &ev) != 0) {
        close(ring->m_wakeFd);
        ring->m_wakeFd = -1;
        close(epollFd);
        ring->m_epollFd = -1;
        ring->m_ownsRing = false;
        return false;
    }
    return true;
}

/** @brief epoll 引擎资源清理。 */
static void epDeinitCleanup(XNetIoRingPosix* posix) {
    XNetPendingOp* op;
    while ((op = posix->m_pendingHead) != NULL) {
        posix->m_pendingHead = op->next;
        XFree_System(op);
    }
    if (posix->m_ownsRing) {
        if (posix->m_epollFd >= 0) close(posix->m_epollFd);
        if (posix->m_wakeFd >= 0) close(posix->m_wakeFd);
    }
    posix->m_epollFd = -1;
    posix->m_ownsRing = false;
}
#endif /* XNET_BUILD_EPOLL */

/* 虚函数表枚举：继承 XAbstractNetIoRing（vtable 大小由本宏生成） */
XCLASS_DEFINE_BEGING(XNetIoRingPosix)
XCLASS_DEFINE_EXTEND_END(XNetIoRingPosix, XAbstractNetIoRing)

/* ================================================================
 * 重载虚函数实现（双引擎 mode 分发）
 * ================================================================ */

static void VXNetIoRingPosix_pollPlatform(XAbstractNetIoRing* self) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix) return;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING) {
        ioPollPlatform(posix, self);
        return;
    }
#endif
#if XNET_BUILD_EPOLL
    epPollPlatform(posix, self);
#endif
}

static XFd VXNetIoRingPosix_getEventFd(XAbstractNetIoRing* self) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix) return XFD_INVALID;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING)
        return (posix->m_ringFd >= 0) ? (XFd)(intptr_t)posix->m_ringFd
                                      : XFD_INVALID;
#endif
#if XNET_BUILD_EPOLL
    return (posix->m_epollFd >= 0) ? (XFd)(intptr_t)posix->m_epollFd
                                   : XFD_INVALID;
#else
    return XFD_INVALID;
#endif
}

static bool VXNetIoRingPosix_registerEvent(XAbstractNetIoRing* self, XFd fd) {
    (void)self;
    (void)fd;
    return true; /* io_uring SQE 自带 fd；epoll 在提交时按需 ADD */
}

/**
 * @brief  把裸 OS fd 注册为「被动可读事件源」（2026-10-26 触摸唤醒通道）。
 * @details epoll 引擎：epoll_ctl(ADD) 把 fd 纳入等待集——fd 可读即唤醒
 *          阻塞中的事件循环，触摸事件由此获得「事件驱动」而非「等下一圈
 *          边界」的响应（fbdev 真机实证：触摸 fd 未入等待集时，事件圈被
 *          拉长到 ~400ms，触摸注入后日志无 mousePress、画面静止）。
 *
 *          io_uring 引擎：**不实现**（返回 false）。POLL_ADD 的语义在
 *          内核 5.3 才完善，而本项目的 io_uring 版本门是 ≥5.10（5.2.9 的
 *          MCGS 板走不到该分支），无实测条件时不引入未验证路径；调用方
 *          见 false 即保留原有轮询回调链兜底，行为退化为改造前口径。
 *
 *          记录表容量上限 XNET_PASSIVE_MAX；重复注册同一 fd 幂等（更新
 *          回调指针，不重复 ADD）。
 */
static bool VXNetIoRingPosix_registerPassiveOsFd(XAbstractNetIoRing* self,
                                                  int osFd,
                                                  void (*notify)(int osFd)) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix || osFd < 0) return false;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING) {
        int i;
        if (posix->m_ringFd < 0) return false;
        for (i = 0; i < posix->m_passiveIoCount; ++i) {
            if (posix->m_passiveIo[i].m_osFd == osFd) {
                posix->m_passiveIo[i].m_notify = notify; /* 幂等。 */
                return true;
            }
        }
        if (posix->m_passiveIoCount >= XNET_PASSIVE_MAX) return false;
        i = posix->m_passiveIoCount++;
        posix->m_passiveIo[i].m_osFd = osFd;
        posix->m_passiveIo[i].m_notify = notify;
        posix->m_passiveIo[i].m_armed = false;
        /* 专属完成上下文：type=Custom 标识「非网络完成」（被动 fd），
         * fd 字段存 OS fd 号（供回调定位）。 */
        memset(&posix->m_passiveIo[i].m_ctx, 0,
               sizeof(posix->m_passiveIo[i].m_ctx));
        posix->m_passiveIo[i].m_ctx.type = XEventContextType_Type_Custom;
        posix->m_passiveIo[i].m_ctx.fd = (XFd)osFd;
        posix->m_passiveIo[i].m_multiFailed = false;
        /* 立即挂载 multishot POLL_ADD：此后 fd 每次就绪即产生一条 CQE
         * 唤醒事件循环（无需重挂）。 */
        if (!ioArmPassive(posix, i)) {
            --posix->m_passiveIoCount; /* SQ 满：撤销登记，下次再试。 */
            return false;
        }
        return true;
    }
#endif /* XNET_BUILD_IO_URING */
#if XNET_BUILD_EPOLL
    {
        int i;
        struct epoll_event ev;
        if (posix->m_mode != XNET_MODE_EPOLL) return false;
        if (posix->m_epollFd < 0) return false;
        for (i = 0; i < posix->m_passiveCount; ++i) {
            if (posix->m_passive[i].m_osFd == osFd) {
                posix->m_passive[i].m_notify = notify; /* 幂等：只更新回调。 */
                return true;
            }
        }
        if (posix->m_passiveCount >= XNET_PASSIVE_MAX) return false;
        memset(&ev, 0, sizeof(ev));
        ev.events = XNET_EPOLL_IN_INTEREST;
        ev.data.fd = osFd;
        if (epoll_ctl(posix->m_epollFd, EPOLL_CTL_ADD, osFd, &ev) != 0) {
            if (errno == EEXIST) {
                /* 已被别的路径注册（如网络提交路径）：兴趣已含 IN，直接记账。 */
                posix->m_passive[posix->m_passiveCount].m_osFd = osFd;
                posix->m_passive[posix->m_passiveCount].m_notify = notify;
                ++posix->m_passiveCount;
                return true;
            }
            return false;
        }
        posix->m_passive[posix->m_passiveCount].m_osFd = osFd;
        posix->m_passive[posix->m_passiveCount].m_notify = notify;
        ++posix->m_passiveCount;
        return true;
    }
#else
    (void)notify;
    return false;
#endif
}

/**
 * @brief  注销「被动可读 fd」（触摸唤醒通道，2026-10-26）。
 * @details 从 epoll 等待集摘除并清记录。属主关闭 fd 前必须先调用：
 *          等待集里留一个即将失效的 fd，会让事件循环在其他 fd 就绪时
 *          读到已关闭 fd（EBADF / fd 号复用风险）。
 */
static bool VXNetIoRingPosix_unregisterPassiveOsFd(XAbstractNetIoRing* self,
                                                    int osFd) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix) return true;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING) {
        int i;
        for (i = 0; i < posix->m_passiveIoCount; ++i) {
            if (posix->m_passiveIo[i].m_osFd != osFd) continue;
            /* 撤销在飞的 POLL_ADD：POLL_REMOVE 用 user_data 定位请求。
             * 已在飞的一次性 POLL_ADD 若不撤销，其 CQE 仍会到达——但记录
             * 已被移除，ioPollPlatform 的 ctx 匹配找不到 idx ⇒ 只推进 CQ
             * 不回调（见 ioPollPlatform 的 idx<count 判定），安全。 */
            if (posix->m_passiveIo[i].m_armed && posix->m_ringFd >= 0) {
                XNetSqe* sqe = ioGetSqeSlot(posix);
                if (sqe) {
                    memset(sqe, 0, sizeof(*sqe));
                    sqe->opcode = IORING_OP_POLL_REMOVE;
                    sqe->fd = -1;
                    sqe->addr = (uint64_t)(uintptr_t)
                                &posix->m_passiveIo[i].m_ctx;
                    sqe->user_data = 0;
                    posix->m_passiveIo[i].m_armed = false;
                    ioSubmitEntries(posix, 1);
                }
            }
            /* 前移后续记录，保持表紧凑。 */
            for (; i + 1 < posix->m_passiveIoCount; ++i)
                posix->m_passiveIo[i] = posix->m_passiveIo[i + 1];
            --posix->m_passiveIoCount;
            return true;
        }
        return true; /* 未注册：幂等成功。 */
    }
#endif /* XNET_BUILD_IO_URING */
#if XNET_BUILD_EPOLL
    {
        int i;
        if (posix->m_mode != XNET_MODE_EPOLL) return true;
        for (i = 0; i < posix->m_passiveCount; ++i) {
            if (posix->m_passive[i].m_osFd != osFd) continue;
            if (posix->m_epollFd >= 0)
                epoll_ctl(posix->m_epollFd, EPOLL_CTL_DEL, osFd, NULL);
            /* 前移后续记录，保持表紧凑。 */
            for (; i + 1 < posix->m_passiveCount; ++i)
                posix->m_passive[i] = posix->m_passive[i + 1];
            --posix->m_passiveCount;
            return true;
        }
        return true; /* 未注册：幂等成功。 */
    }
#else
    (void)osFd;
    return true;
#endif
}

/**
 * @brief  在 epoll 事件循环里分派被动 fd 就绪（触摸唤醒通道的回调侧）。
 * @details 被动 fd 就绪时：① 回调属主 notify（属主据此抽干设备数据）；
 *          ② 该 fd 的就绪**不**走网络挂起请求链（无 pending op），只
 *          触发回调——不派发网络完成事件。
 */
static void epDispatchPassive(XNetIoRingPosix* posix, int fd) {
    int i;
    if (!posix) return;
    for (i = 0; i < posix->m_passiveCount; ++i) {
        if (posix->m_passive[i].m_osFd == fd) {
            void (*notify)(int) = posix->m_passive[i].m_notify;
            if (notify) notify(fd);
            return;
        }
    }
}

static void VXNetIoRingPosix_waitForEvents(XAbstractNetIoRing* self,
                                           int timeoutMs) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix) return;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING) {
        struct pollfd fds[2];
        nfds_t count = 0;
        if (posix->m_ringFd < 0) return;

        /* [wake] 5s 汇总窗口到点即输出(探针开启时)。 */
        if (xnet_wakeProfEnabled()) {
            uint64_t nowUs = XAbstractNetIoRing_profNowUs();
            if (!xnet_wakeProf.winStartUs)
                xnet_wakeProf.winStartUs = nowUs;
            else if (nowUs - xnet_wakeProf.winStartUs >= XNET_WAKE_PROF_WIN_US) {
                const char* profMode = "epoll";
                if (posix->m_mode == XNET_MODE_IOURING) profMode = "io_uring";
                xnet_wakeProfFlush(profMode, nowUs);
            }
        }

        VXNetIoRingPosix_pollPlatform(self);
        if (ioPeekCqe(posix) != NULL) return;

        fds[count++] = (struct pollfd){ .fd = posix->m_ringFd, .events = POLLIN };
        if (posix->m_wakeFd >= 0)
            fds[count++] = (struct pollfd){ .fd = posix->m_wakeFd, .events = POLLIN };

        {
            int result;
            uint64_t enterUs = xnet_wakeProfEnabled()
                                   ? XAbstractNetIoRing_profNowUs() : 0;
            do {
                result = poll(fds, count, timeoutMs);
            } while (result < 0 && errno == EINTR);
            /* [wake] 段①: fd 就绪唤醒(仅 ring fd, 纯 eventfd 唤醒不计);
             * 超时醒计一轮(定时器节拍兜底证据)。 */
            if (enterUs) {
                if (result > 0 && (fds[0].revents & POLLIN)) {
                    xnet_wakeProf.wakeUs = XAbstractNetIoRing_profNowUs();
                    xnet_wakeProfPush(&xnet_wakeProf.enterWake,
                                      (uint32_t)(xnet_wakeProf.wakeUs -
                                                 enterUs));
                    ++xnet_wakeProf.fdWakes;
                } else if (result == 0) {
                    ++xnet_wakeProf.timeoutRounds;
                }
            }
            if (result > 0 && count > 1 && (fds[1].revents & POLLIN)) {
                uint64_t value;
                while (read(posix->m_wakeFd, &value, sizeof(value)) < 0 &&
                       errno == EINTR) {}
            }
        }
        VXNetIoRingPosix_pollPlatform(self);
        return;
    }
#endif
#if XNET_BUILD_EPOLL
    /* [wake] 5s 汇总窗口到点即输出(探针开启时)。 */
    if (xnet_wakeProfEnabled()) {
        uint64_t nowUs = XAbstractNetIoRing_profNowUs();
        if (!xnet_wakeProf.winStartUs)
            xnet_wakeProf.winStartUs = nowUs;
        else if (nowUs - xnet_wakeProf.winStartUs >= XNET_WAKE_PROF_WIN_US)
            xnet_wakeProfFlush("epoll", nowUs);
    }
    epPollPlatform(posix, self); /* 先非阻塞一轮（可能在等待前已有完成） */
    {
        struct epoll_event events[XNET_EPOLL_MAX_EVENTS];
        int n;
        int i;
        /* [ILOOP 2026-10-29 诊断] epoll 等待对账：EINTR 次数 + 总时长
         * （主循环 921ms 冻结定位——判 EINTR 重试风暴 vs 单次长睡）。 */
        struct timespec itb, ite;
        unsigned eintr = 0;
        clock_gettime(CLOCK_MONOTONIC, &itb);
        uint64_t enterUs = xnet_wakeProfEnabled()
                               ? XAbstractNetIoRing_profNowUs() : 0;
        do {
            n = epoll_wait(posix->m_epollFd, events, XNET_EPOLL_MAX_EVENTS,
                           timeoutMs);
            if (n < 0 && errno == EINTR)
                ++eintr;
        } while (n < 0 && errno == EINTR);
        clock_gettime(CLOCK_MONOTONIC, &ite);
        if ((ite.tv_sec - itb.tv_sec) * 1e3 +
                (double)(ite.tv_nsec - itb.tv_nsec) / 1e6 >
            50.0)
            XPrintf("[ILOOP] to=%d dur=%.1fms eintr=%u n=%d errno=%d\n",
                    timeoutMs,
                    (ite.tv_sec - itb.tv_sec) * 1e3 +
                        (double)(ite.tv_nsec - itb.tv_nsec) / 1e6,
                    eintr, n, errno);
        /* [wake] 段①: 网络 fd 就绪唤醒(wakeFd 纯唤醒不计); 超时醒计一轮。 */
        if (enterUs) {
            bool netReady = false;
            for (i = 0; i < n; ++i) {
                if (events[i].data.fd != posix->m_wakeFd) {
                    netReady = true;
                    break;
                }
            }
            if (netReady) {
                xnet_wakeProf.wakeUs = XAbstractNetIoRing_profNowUs();
                xnet_wakeProfPush(&xnet_wakeProf.enterWake,
                                  (uint32_t)(xnet_wakeProf.wakeUs - enterUs));
                ++xnet_wakeProf.fdWakes;
            } else if (n == 0) {
                ++xnet_wakeProf.timeoutRounds;
            }
        }
        for (i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == posix->m_wakeFd) {
                uint64_t value;
                while (read(posix->m_wakeFd, &value, sizeof(value)) < 0 &&
                       errno == EINTR) {}
                continue;
            }
            /* 被动可读 fd（触摸等）：只回调属主，不走网络挂起请求链。 */
            {
                int p;
                bool isPassive = false;
                for (p = 0; p < posix->m_passiveCount; ++p) {
                    if (posix->m_passive[p].m_osFd == fd) {
                        isPassive = true;
                        break;
                    }
                }
                if (isPassive) {
                    epDispatchPassive(posix, fd);
                    continue;
                }
            }
            {
                XNetPendingOp* op =
                    epTakePending(posix, fd, XNET_EPOLL_WANT_IN);
                if (op) epExecutePending(posix, op, self);
            }
            if (events[i].events & XNET_EPOLL_OUT_INTEREST) {
                XNetPendingOp* op =
                    epTakePending(posix, fd, XNET_EPOLL_WANT_OUT);
                if (op) epExecutePending(posix, op, self);
            }
        }
    }
#endif
}

static void VXNetIoRingPosix_wakeUp(XAbstractNetIoRing* self) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    uint64_t value = 1;
    if (!posix || posix->m_wakeFd < 0) return;
    while (write(posix->m_wakeFd, &value, sizeof(value)) < 0 &&
           errno == EINTR) {}
}

static void VXNetIoRingPosix_deinit(XAbstractNetIoRing* obj) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)obj;
    if (!posix) return;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING) ioDeinitCleanup(posix);
#endif
#if XNET_BUILD_EPOLL
    if (posix->m_mode == XNET_MODE_EPOLL) epDeinitCleanup(posix);
#endif
    if (posix->m_wakeFd >= 0) close(posix->m_wakeFd);
    posix->m_wakeFd = -1;
    posix->m_ownsRing = false;
    posix->m_mode = XNET_MODE_NONE;

    XClass_Deinit_Parent(XAbstractNetIoRing, obj);
}

/** @brief 虚函数表初始化（双引擎共用同一虚函数表）。 */
XVtable* XNetIoRingPosix_class_init(void) {
    XVTABLE_INIT_DEFAULT(XNetIoRingPosix)
    XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing);

    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_GetEventFd,    VXNetIoRingPosix_getEventFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_PollPlatform,  VXNetIoRingPosix_pollPlatform);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_RegisterEvent, VXNetIoRingPosix_registerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_RegisterPassiveOsFd, VXNetIoRingPosix_registerPassiveOsFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_UnregisterPassiveOsFd, VXNetIoRingPosix_unregisterPassiveOsFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WaitForEvents, VXNetIoRingPosix_waitForEvents);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WakeUp,        VXNetIoRingPosix_wakeUp);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetIoRingPosix_deinit);

    return XVTABLE_DEFAULT;
}

/* ================================================================
 * 构造与初始化（运行时回退链：io_uring 失败 -> epoll）
 * ================================================================ */
void XNetIoRingPosix_init(XNetIoRingPosix* ring) {
    bool enabled = false;
    if (!ring) return;

    memset(ring, 0, sizeof(XNetIoRingPosix));
    ring->m_wakeFd = -1;
#if XNET_BUILD_IO_URING
    ring->m_ringFd = -1;
#endif
#if XNET_BUILD_EPOLL
    ring->m_epollFd = -1;
#endif
    ring->m_mode = XNET_MODE_NONE;

    /* 初始化基类（设置基类虚函数表 + 创建 SQ/CQ 队列） */
    XAbstractNetIoRing_init((XAbstractNetIoRing*)ring);

    /* 重载为本类的虚函数表 */
    XClassSetVtable(ring, XNetIoRingPosix);

    /* 运行时回退链：优先 io_uring（Linux 5.1+ 内核）；io_uring_setup
     * 失败（-ENOSYS 等，如 4.x 内核）自动回退 epoll。
     * [2026-10-16 版本门(用户裁定)] 「明确声明支持 io_uring 的内核才用
     * io_uring」：uname 版本号 >= 5.10（io_uring 成熟 LTS 基线，发行版
     * 声明支持）才尝试；早期内核（如 5.2.9 MCGS 定制版——CONNECT 操作
     * 码被裁、epoll 回退引擎 CONNECT 又缺 connect() 执行的旧账已另修）
     * 直接落 epoll，不再依赖半残 io_uring + 操作级兜底，换板不再各自
     * 踩坑。XNET_FORCE_IOURING=1 可越过版本门强制 io_uring（调试用）。 */
#if XNET_BUILD_IO_URING
    {
        int ioAllowed = 0;
        const char* force = getenv("XNET_FORCE_IOURING");
        /* [2026-10-25 XNET_FORCE_EPOLL] 与 XNET_FORCE_IOURING 对称的桌面
         * 回归开关：置 1 跳过 io_uring 直接落 epoll 引擎。双引擎行为
         * 一致性测试（Modbus 断线重连战役）需要同机切换后端对照。 */
        const char* forceEpoll = getenv("XNET_FORCE_EPOLL");
        if (!(forceEpoll && forceEpoll[0] == '1')) {
            if (force && force[0] == '1') {
                ioAllowed = 1;
            } else {
                struct utsname uts;
                if (uname(&uts) == 0) {
                    int major = 0, minor = 0;
                    sscanf(uts.release, "%d.%d", &major, &minor);
                    ioAllowed = (major > 5) || (major == 5 && minor >= 10);
                } else {
                    ioAllowed = 1; /* 版本号不可得：维持原行为（setup 探测把关） */
                }
            }
        }
        if (ioAllowed && ioSetupRing(ring)) {
            ring->m_mode = XNET_MODE_IOURING;
            enabled = true;
        }
    }
#endif
#if XNET_BUILD_EPOLL
    if (!enabled && epSetup(ring)) {
        ring->m_mode = XNET_MODE_EPOLL;
        enabled = true;
    }
#endif
    ((XAbstractNetIoRing*)ring)->m_enabled = enabled;
    XNET_AND_TRACE("NetIoRing init: mode=%d enabled=%d",
                   (int)ring->m_mode, (int)enabled);
}

XNetIoRingPosix* XNetIoRingPosix_create_ex(XMemoryType memory) {
    XNetIoRingPosix* ring =
        (XNetIoRingPosix*)XMemory_malloc(sizeof(XNetIoRingPosix), memory);
    if (!ring) return NULL;
    XNetIoRingPosix_init(ring);
    Set_Class_Memory(ring, memory); Set_Class_IsHeap(ring, true);
    return ring;
}

/* ================================================================
 * 外部 I/O 提交 API（双模式分发，签名与调用点不变）
 * ================================================================ */
int XNetIoRingPosix_ringFd(const XNetIoRingPosix* ring) {
    if (!ring) return -1;
#if XNET_BUILD_IO_URING
    if (ring->m_mode == XNET_MODE_IOURING) return ring->m_ringFd;
#endif
#if XNET_BUILD_EPOLL
    if (ring->m_mode == XNET_MODE_EPOLL) return ring->m_epollFd;
#endif
    return -1;
}

int IoUring_getGlobalRingFd(void) {
    XNetIoRingPosix* ring = (XNetIoRingPosix*)XAbstractNetIoRing_global();
    return ring ? XNetIoRingPosix_ringFd(ring) : -1;
}

XNetSqe* XNetIoRingPosix_getSqe(XNetIoRingPosix* ring) {
#if XNET_BUILD_IO_URING
    if (ring && ring->m_mode == XNET_MODE_IOURING)
        return ioGetSqeSlot(ring);
#endif
#if XNET_BUILD_EPOLL
    if (ring && ring->m_mode == XNET_MODE_EPOLL) {
        memset(&ring->m_currentSqe, 0, sizeof(ring->m_currentSqe));
        ring->m_hasCurrentSqe = true;
        return &ring->m_currentSqe;
    }
#endif
    return NULL;
}

void XNetIoRingPosix_submitSqe(XNetIoRingPosix* ring, int toSubmit) {
    XAbstractNetIoRing* self = (XAbstractNetIoRing*)ring;
#if XNET_BUILD_IO_URING
    if (ring && ring->m_mode == XNET_MODE_IOURING) {
        ioSubmitEntries(ring, toSubmit);
        return;
    }
#endif
#if XNET_BUILD_EPOLL
    if (ring && ring->m_mode == XNET_MODE_EPOLL) {
        XEventContext* ctx;
        XNetSqe* sqe;
        XNetPendingOp* op;
        struct epoll_event ev;
        if (!ring->m_hasCurrentSqe) return;
        sqe = &ring->m_currentSqe;
        ring->m_hasCurrentSqe = false;
        ctx = (XEventContext*)(uintptr_t)sqe->user_data;

        switch (sqe->opcode) {
        /* 文件类：同步执行（普通文件 epoll 不适用），完成立即入队。 */
        case IORING_OP_READ:
        case IORING_OP_WRITE:
        case IORING_OP_FSYNC:
            epExecuteFileSync(ring, self, sqe);
            return;
        /* 取消：从挂起链移除目标请求并以 -ECANCELED 完成。 */
        case IORING_OP_ASYNC_CANCEL: {
            XNetPendingOp** pp = &ring->m_pendingHead;
            XEventContext* target = (XEventContext*)(uintptr_t)sqe->addr;
            bool flushed = false;
            while (*pp) {
                XNetPendingOp* victim = *pp;
                if (victim->ctx == target) {
                    *pp = victim->next;
                    /* [2026-10-23 关闭丢写根修] SEND 类挂起请求在取消前先
                     * 冲刷：io_uring 后端 SQE 提交即入内核，关闭路径
                     * waitForCancel 泵事件期间在途写照常发出、取消落空；
                     * epoll 后端挂在应用侧，原实现直接丢弃 = 数据静默丢失
                     * ——MQTT 客户端 DISCONNECT 包与服务端 CONNACK 拒绝
                     * 应答在「提交写后立即断开」时序下从未上线（epoll 引擎
                     * xmqtt-tcp-api 断开三例稳定失败实证，io_uring 同套件
                     * 全绿掩盖）。此时对端 fd 仍打开（断开先取消后 close），
                     * 同步补发一次：成功则以真实字节数完成（写回 ctx->
                     * result 唤醒 waitForCancel），EAGAIN/错误才落回取消
                     * 语义（真积压背压场景与旧行为一致）。 */
                    if (victim->sqe.opcode == IORING_OP_SEND ||
                        victim->sqe.opcode == IORING_OP_SENDMSG) {
                        ssize_t sent;
                        errno = 0;
                        /* MSG_NOSIGNAL 与设备层 SENDMSG 提交的 sqe->msg_flags
                         * （XDeviceNetwork_posix.c:722）对齐——对端 RST 后
                         * 补发不引入 SIGPIPE 杀进程风险。 */
                        if (victim->sqe.opcode == IORING_OP_SENDMSG)
                            sent = sendmsg(victim->sqe.fd,
                                           (struct msghdr*)(uintptr_t)
                                               victim->sqe.addr, MSG_NOSIGNAL);
                        else
                            sent = send(victim->sqe.fd,
                                        (const void*)(uintptr_t)
                                            victim->sqe.addr,
                                        victim->sqe.len, MSG_NOSIGNAL);
                        if (sent >= 0) {
                            flushed = true;
                            epRearmInterest(ring, victim->sqe.fd);
                            processOneCompletion(self, (int64_t)sent,
                                                 (void*)(uintptr_t)
                                                     victim->sqe.user_data);
                            XFree_System(victim);
                            break;
                        }
                    }
                    /* [2026-10-23] 原无条件 EPOLL_CTL_DEL 会连坐同 fd 其余
                     * 挂起请求（如 RECV）的兴趣致其饿死——统一走按挂起链
                     * 方向并集重铸（无挂起时内部即 DEL）。 */
                    epRearmInterest(ring, victim->sqe.fd);
                    XFree_System(victim);
                    break;
                }
                pp = &victim->next;
            }
            if (!flushed) {
                if (target) target->result = -ECANCELED;
                processOneCompletion(self, target ? 0 : -ENOENT,
                                     (void*)(uintptr_t)sqe->user_data);
            }
            return;
        }
        default:
            break;
        }

        /* 网络类：注册 epoll 兴趣并挂起，等事件循环就绪执行。 */
        op = (XNetPendingOp*)XMalloc_System(sizeof(*op));
        if (!op) {
            processOneCompletion(self, -ENOMEM,
                                 (void*)(uintptr_t)sqe->user_data);
            return;
        }
        op->sqe = *sqe;
        op->ctx = ctx;
        /* [2026-10-16 根修] epoll 引擎 CONNECT 伪 SQE 此前从未执行
         * connect(): SQE 挂起后 epExecutePending 只 getsockopt(SO_ERROR)
         * ——socket 从未发起连接, SO_ERROR 恒 0 → res=0 假完成 → 上层
         * 假连接(写被丢弃/读 0 字节被误判对端 FIN) → 断线重连死循环。
         * 昆仑通态 A33 5.2.9 真机 Modbus 主站「断线重连中」实证; 同机
         * 裸 connect()/wget 全通。真内核 io_uring 后端的 IORING_OP_CONNECT
         * 又被该定制内核拒绝(实测 -EINVAL), 两条后端都栽在此缺。此处补
         * connect() 执行: EINPROGRESS 照旧注册 EPOLLOUT 挂起(既有
         * SO_ERROR 判定语义闭合), 立即失败则直接完成报错。 */
        if (sqe->opcode == IORING_OP_CONNECT) {
            int rc = connect(sqe->fd, (const struct sockaddr*)(uintptr_t)sqe->addr,
                             (socklen_t)sqe->off);
            if (rc != 0 && errno != EINPROGRESS) {
                processOneCompletion(self, -errno,
                                     (void*)(uintptr_t)sqe->user_data);
                XFree_System(op);
                (void)toSubmit;
                return;
            }
            /* rc==0(本机即时完成)或 EINPROGRESS: 照旧挂起, EPOLLOUT 就绪
             * 后由 epExecutePending 以 SO_ERROR 判定真实结果。 */
        }
        op->next = ring->m_pendingHead;
        ring->m_pendingHead = op;
#if XNET_EPOLL_IO_DEBUG
        fprintf(stderr, "[XNET-EPOLL] submit op=%d fd=%d\n",
                (int)sqe->opcode, sqe->fd);
        XNET_AND_TRACE("epoll submit: op=%d fd=%d", (int)sqe->opcode, sqe->fd);
#endif

        memset(&ev, 0, sizeof(ev));
        ev.events = (xnetEpollDirection(sqe->opcode) == XNET_EPOLL_WANT_OUT)
                        ? XNET_EPOLL_OUT_INTEREST
                        : XNET_EPOLL_IN_INTEREST;
        ev.data.fd = sqe->fd;
        if (epoll_ctl(ring->m_epollFd, EPOLL_CTL_ADD, sqe->fd, &ev) != 0) {
            if (errno == EEXIST) {
                /* [2026-10-03 互联测试指挥官] fd 已注册（多见：RECV 挂起
                 * 中提交 SEND/CONNECT）——按挂起链方向并集重铸兴趣
                 * （IN|OUT, 旧方向请求不被饿死），不再静默忽略。 */
                epRearmInterest(ring, sqe->fd);
                return;
            }
            XNET_AND_TRACE("epoll ADD failed: op=%d fd=%d errno=%d",
                           (int)sqe->opcode, sqe->fd, errno);
            ring->m_pendingHead = op->next;
            XFree_System(op);
            processOneCompletion(self, -errno,
                                 (void*)(uintptr_t)sqe->user_data);
        }
        (void)toSubmit; /* epoll 模式恒为单条提交 */
        return;
    }
#endif
}

int XNetIoRingPosix_waitCqe(XNetIoRingPosix* ring, uint64_t userData) {
#if XNET_BUILD_IO_URING
    if (ring && ring->m_mode == XNET_MODE_IOURING) {
        /* 文件 I/O 的完成由内核异步产生：有限次重试 GETEVENTS（每次
           1ms），直到目标 CQE 出现（此前单次非阻塞会在完成未到达时
           误报 -1）。 */
        int retry;
        for (retry = 0; retry < 100; ++retry) {
            struct __kernel_timespec ts = { 0, 1000000 }; /* 1ms 阻塞等待 */
            (void)syscall(__NR_io_uring_enter, ring->m_ringFd, 0, 1,
                          IORING_ENTER_GETEVENTS, &ts, NULL);
            {
                unsigned head = __atomic_load_n(ring->m_cqHeadPtr,
                                                __ATOMIC_ACQUIRE);
                unsigned tail = __atomic_load_n(ring->m_cqTailPtr,
                                                __ATOMIC_ACQUIRE);
                if (head != tail) {
                    XNetCqe* cqe =
                        &ring->m_cqes[head & (*ring->m_cqMaskPtr)];
                    if (userData == 0 || cqe->user_data == userData) {
                        __atomic_store_n(ring->m_cqHeadPtr, head + 1,
                                         __ATOMIC_RELEASE);
                        return cqe->res;
                    }
                    /* 非目标完成：跳过（推进 head 避免阻塞） */
                    __atomic_store_n(ring->m_cqHeadPtr, head + 1,
                                     __ATOMIC_RELEASE);
                }
            }
        }
        return -1;
    }
#endif
#if XNET_BUILD_EPOLL
    /* 文件类操作在 submitSqe 时已同步完成并写回 ctx->result；
       网络类挂起请求不走 waitCqe（由事件循环异步完成）。 */
    {
        XEventContext* ctx = (XEventContext*)(uintptr_t)userData;
        if (!ctx) return -1;
        return (int)ctx->result;
    }
#else
    (void)ring; (void)userData;
    return -1;
#endif
}

/* ================================================================
 * 平台钩子：创建 Linux 后端（io_uring 或 epoll 由运行时回退决定）
 * ================================================================ */
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void) {
    return (XAbstractNetIoRing*)XNetIoRingPosix_create();
}

#endif /* __linux__ */

#endif /* XAbstractNetIoRing_ON */
