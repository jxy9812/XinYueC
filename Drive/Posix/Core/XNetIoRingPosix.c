/**
 * @file XNetIoRingPosix.c
 * @brief XAbstractNetIoRing Linux ????(io_uring/epoll ???)
 *
 * ?? XAbstractNetIoRing,Linux 5.1+ ???? io_uring ?????
 * I/O;init ? io_uring_setup ??(-ENOSYS,Linux 4.x ?????)
 * ???? epoll ????(??????????????? API ??
 * ????,????????)?
 *
 * ????????(XNetIoRingPosix.h ??):
 *   XNET_BUILD_IO_URING  ??????? linux/io_uring.h ???? epoll
 *   XNET_BUILD_EPOLL     ?? 1(???????,?????????)
 *
 * io_uring ??????:
 *   pollPlatform:  io_uring CQ ????? -> processOneCompletion ? CQ
 *   waitForEvents: io_uring_enter(timeout) ??;wakeUp ? eventfd
 *
 * epoll ??????(???? -> ????):
 *   ?? RECV/RECVMSG/SEND/ACCEPT/CONNECT:????? epoll ????,
 *     ???????? IO ?? CQ ??(?????????);
 *   ?? READ/WRITE/FSYNC:???????(???? epoll ???),
 *     ????? waitCqe ????;
 *   ??:eventfd ?? epoll ??;???:timerfd ? READ ???
 *
 * ?? API(getSqe/submitSqe/waitCqe/ringFd)?????,???
 * (XDeviceFile_posix_api.c / XDeviceNetwork_posix.c ???? SQE)
 * ???;? io_uring ??????????? SQE ????????
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
#include <unistd.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
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

/* ?? io_uring ??????? */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#endif /* XNET_BUILD_IO_URING */

/* ?? pollPlatform ????????,???????? */
#ifndef XNETIORING_POSIX_POLL_BATCH
#define XNETIORING_POSIX_POLL_BATCH  64
#endif

/* io_uring SQ ??? */
#ifndef XNETIORING_POSIX_QUEUE_DEPTH
#define XNETIORING_POSIX_QUEUE_DEPTH  256
#endif

/* epoll ?????????? */
#ifndef XNET_EPOLL_MAX_EVENTS
#define XNET_EPOLL_MAX_EVENTS 64
#endif

/* [2026-10-03 ???????] epoll ???????(?? 0):??
 * MCGS(5.2.9 ?? memlock ?? epoll)?????????, ???
 * -DXNET_EPOLL_IO_DEBUG=1 ?????? -- ???? XPWN_IME_DEBUG
 * ????(Drive/Posix/Graphics/XPlatformNativeWindow_posix.c:238)? */
#ifndef XNET_EPOLL_IO_DEBUG
#define XNET_EPOLL_IO_DEBUG 0
#endif

/* epoll ??/????(? XSocketActType ???????) */
#define XNET_EPOLL_IN_INTEREST   (uint32_t)EPOLLIN
#define XNET_EPOLL_OUT_INTEREST  (uint32_t)EPOLLOUT
#define XNET_EPOLL_WANT_IN       0
#define XNET_EPOLL_WANT_OUT      1

/* ================================================================
 * ???????
 * ================================================================ */
typedef enum XNetIoRingMode {
    XNET_MODE_NONE = 0,   /**< ???? */
    XNET_MODE_IOURING,    /**< io_uring ???(Linux 5.1+ ??) */
    XNET_MODE_EPOLL       /**< epoll ???(???????) */
} XNetIoRingMode;

/* ================================================================
 * ????(??????,?????)
 *
 * ???????(res + user_data ???)??? CQ ?????
 * ????????pollPlatform(?????)? waitForEvents
 * (????)????? Win32 ? processOneCompletion? */

/* ================================================================
 * [wake] ??????????(??, env XGUI_REMOTE_WAKE_PROF ??)
 *
 * ????(??? out/mcgs-campaign/udp-design.md??????????):
 *   ? ?????? ? fd ??????(????+syscall ??);
 *   ? fd ???? ? ???????(????????);
 *   ? ??? ? ??????(???, ? XAbstractNetIoRing_profMarkRecv/
 *      profLastRecvUs ? XGuiServer/XGuiClient ??, ? [wake][srv/cli] ?)?
 * ? 5s ???? [wake][ring](stderr, P50/P95, ?????)??????
 * ???????(on ?? getenv, ???????)?
 * ================================================================ */
#define XNET_WAKE_PROF_SAMPLES 256
#define XNET_WAKE_PROF_WIN_US  5000000ULL
typedef struct XNetWakeSampleSet {
    uint32_t v[XNET_WAKE_PROF_SAMPLES]; /* ??(µs; ??????)? */
    int      n;
} XNetWakeSampleSet;
static struct XNetWakeProfState {
    uint64_t wakeUs;         /* fd ??????(0=????)? */
    uint64_t winStartUs;     /* 5s ??????? */
    uint32_t fdWakes;        /* fd ??????? */
    uint32_t timeoutRounds;  /* ?????(?????????)? */
    XNetWakeSampleSet enterWake; /* ??????fd ????? */
    XNetWakeSampleSet wakeCqe;   /* ?????????? */
} xnet_wakeProf;
/* -1=??? env(??????????, ????? 0/1 ??)? */
static int xnet_wakeProfOn = -1;

/** @brief ????(env ????)? */
static int xnet_wakeProfEnabled(void) {
    if (xnet_wakeProfOn < 0) {
        const char* env = getenv("XGUI_REMOTE_WAKE_PROF");
        xnet_wakeProfOn = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return xnet_wakeProfOn;
}

/** @brief ????(??????; ????)? */
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

/** @brief ????(µs; ??????)? */
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

/** @brief 5s ?????????? */
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
    /* user_data==0 ?? wakeUp ??,??????(?? Win32 overlapped==NULL) */
    if (!ctx) return;

    ctx->result = res;
    /* [wake] ??: ????????(??? eventMask ??, SEND ???
     * ??????) + ?????????????? */
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
        /* ?????:????????????? */
        XFd timerFd = ctx->fd;
        if (timerFd != XFD_INVALID) {
            XTimerEvent* timerEv = XTimerEvent_create((XTimerId)timerFd);
            XEvent* timerEvent = (XEvent*)timerEv;
            if (timerEvent) {
                timerEvent->m_posted = true;
                timerEvent->m_spontaneous = true;
                /* ? fd ??? owner */
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
        /* Socket/File I/O ??:??? CQ ?? */
        XAbstractNetIoRing_CQEntry cqEntry;
        memset(&cqEntry, 0, sizeof(cqEntry));

        cqEntry.m_fd = ctx->fd;
        cqEntry.m_bytes = (res >= 0) ? (uint32_t)res : 0;
        cqEntry.m_error = (res < 0) ? -res : 0;
        cqEntry.m_sourceType = XAbstractNetIoRing_Source_NativeIO;
        cqEntry.m_fdType = XFd_type(ctx->fd);

        /* XSocketActType ???????? ctx->eventMask(? Windows ??,
         * io_uring ? eventMask ????????? XSocketActType ?) */
        cqEntry.m_events = ctx->eventMask;

        /* ????????? user_data,??? fd ?????????
         * ??????????????????,??????? CQE ?
         * ???? */
        if (ctx->eventMask == 0)
            return;

        /* I/O ???:??????????? Connect ?? */
        if (res < 0 && cqEntry.m_events == 0) {
            cqEntry.m_events = XSocketActType_Connect;
        }

        ctx->finishedBytes = (res >= 0) ? (size_t)res : 0;
        XAbstractNetIoRing_pushCompletion(self, &cqEntry);
    }
}

/* ================================================================
 * ????(??????,???????)
 * ================================================================ */

/** @brief ??(?? fd ??)?? SQE ??(epoll ??)? */
typedef struct XNetPendingOp {
    struct io_uring_sqe sqe;    /**< ?/? SQE ??(?????) */
    XEventContext* ctx;         /**< ?????(= sqe.user_data) */
    struct XNetPendingOp* next; /**< ???? */
} XNetPendingOp;

typedef struct XNetIoRingPosix {
    XAbstractNetIoRing m_class;   /**< ??(???????) */
    XNetIoRingMode m_mode;        /**< ??????? */
    int     m_wakeFd;             /**< ????? eventfd(?????) */
    bool    m_ownsRing;           /**< ???????? */
#if XNET_BUILD_IO_URING
    int     m_ringFd;             /**< io_uring ????? fd */
    /* io_uring ?????? */
    unsigned* m_sqHeadPtr;        /**< SQ head ??(????) */
    unsigned* m_sqTailPtr;        /**< SQ tail ??(??????) */
    unsigned* m_sqMaskPtr;        /**< SQ ring mask */
    unsigned* m_sqFlagsPtr;       /**< SQ ring flags */
    unsigned* m_sqArray;          /**< SQ ???? */
    struct io_uring_cqe* m_cqes;  /**< CQ ???? */
    unsigned* m_cqHeadPtr;        /**< CQ head ??(??????) */
    unsigned* m_cqTailPtr;        /**< CQ tail ??(????) */
    unsigned* m_cqMaskPtr;        /**< CQ ring mask */
    struct io_uring_sqe* m_sqes;  /**< SQ ???? */
    unsigned m_sqEntries;         /**< SQ ??? */
#endif /* XNET_BUILD_IO_URING */
#if XNET_BUILD_EPOLL
    int     m_epollFd;            /**< epoll ?? fd */
    struct io_uring_sqe m_currentSqe; /**< getSqe ????(????) */
    bool    m_hasCurrentSqe;      /**< ??????? */
    XNetPendingOp* m_pendingHead; /**< ??????? */
#endif /* XNET_BUILD_EPOLL */
} XNetIoRingPosix;

/* ????(?????? XNetIoRingPosix.c)--?? .h ?? */

/* ==================== ???? ==================== */
static XFd   VXNetIoRingPosix_getEventFd(XAbstractNetIoRing* self);
static void  VXNetIoRingPosix_pollPlatform(XAbstractNetIoRing* self);
static bool  VXNetIoRingPosix_registerEvent(XAbstractNetIoRing* self, XFd fd);
static void  VXNetIoRingPosix_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void  VXNetIoRingPosix_wakeUp(XAbstractNetIoRing* self);
static void  VXNetIoRingPosix_deinit(XAbstractNetIoRing* obj);

/* ================================================================
 * io_uring ??(XNET_BUILD_IO_URING ???)
 * ================================================================ */
#if XNET_BUILD_IO_URING

/* ?? SQ ??????,?? SQE ?? */
static struct io_uring_sqe* ioGetSqeSlot(XNetIoRingPosix* posix) {
    if (!posix->m_sqHeadPtr || !posix->m_sqTailPtr || !posix->m_sqMaskPtr || !posix->m_sqes)
        return NULL;
    unsigned head = __atomic_load_n(posix->m_sqHeadPtr, __ATOMIC_ACQUIRE);
    unsigned tail = __atomic_load_n(posix->m_sqTailPtr, __ATOMIC_RELAXED);
    unsigned next = tail + 1;

    /* SQ ?? */
    if (next - head > posix->m_sqEntries)
        return NULL;

    unsigned index = tail & (*posix->m_sqMaskPtr);
    return &posix->m_sqes[index];
}

/* ?????? SQE ??? */
static void ioSubmitEntries(XNetIoRingPosix* posix, int toSubmit) {
    unsigned tail = __atomic_load_n(posix->m_sqTailPtr, __ATOMIC_RELAXED);
    unsigned startIndex = tail & (*posix->m_sqMaskPtr);

    /* ?? SQ array ?? */
    for (int i = 0; i < toSubmit; i++) {
        posix->m_sqArray[(startIndex + i) & (*posix->m_sqMaskPtr)] =
            (startIndex + i) & (*posix->m_sqMaskPtr);
    }

    /* ?? SQ tail(?????) */
    __atomic_store_n(posix->m_sqTailPtr, tail + toSubmit, __ATOMIC_RELEASE);

    /* ? SQPOLL ?????? io_uring_enter ?????? SQE?
     * ?????(2026-10-02 ????): ????????--EINTR ? SQE
     * ????, RECV ????????? enter(????????)?
     * EINTR ??; ??????? tail(??? readPending ????,
     * ??????), ????????? */
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

/* ? CQ ????????,??? */
static struct io_uring_cqe* ioPeekCqe(XNetIoRingPosix* posix) {
    unsigned head = __atomic_load_n(posix->m_cqHeadPtr, __ATOMIC_ACQUIRE);
    unsigned tail = __atomic_load_n(posix->m_cqTailPtr, __ATOMIC_ACQUIRE);

    if (head == tail)
        return NULL;  /* CQ ?? */

    return &posix->m_cqes[head & (*posix->m_cqMaskPtr)];
}

/* ?? CQ head(?????) */
static void ioAdvanceCq(XNetIoRingPosix* posix) {
    unsigned head = __atomic_load_n(posix->m_cqHeadPtr, __ATOMIC_RELAXED);
    __atomic_store_n(posix->m_cqHeadPtr, head + 1, __ATOMIC_RELEASE);
}

/* ????? CQ(io ??) */
static void ioPollPlatform(XNetIoRingPosix* posix, XAbstractNetIoRing* self) {
    int batchCount = 0;
    if (posix->m_ringFd < 0) return;
    while (batchCount < XNETIORING_POSIX_POLL_BATCH) {
        struct io_uring_cqe* cqe = ioPeekCqe(posix);
        if (!cqe) break;
        processOneCompletion(self, cqe->res,
                             (void*)(uintptr_t)cqe->user_data);
        ioAdvanceCq(posix);
        batchCount++;
    }
}

/* ?? io_uring_setup ????? mmap ????? */
static bool ioSetupRing(XNetIoRingPosix* ring) {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    params.flags = 0;

    int ringFd = (int)syscall(__NR_io_uring_setup,
                              XNETIORING_POSIX_QUEUE_DEPTH, &params);
    if (ringFd < 0) return false;

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

    size_t cqRingSize = (size_t)params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
    void* cqPtr = mmap(0, cqRingSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_CQ_RING);
    if (cqPtr == MAP_FAILED) goto fail;

    ring->m_cqHeadPtr = (unsigned*)((char*)cqPtr + params.cq_off.head);
    ring->m_cqTailPtr = (unsigned*)((char*)cqPtr + params.cq_off.tail);
    ring->m_cqMaskPtr = (unsigned*)((char*)cqPtr + params.cq_off.ring_mask);
    ring->m_cqes      = (struct io_uring_cqe*)((char*)cqPtr + params.cq_off.cqes);

    size_t sqesSize = params.sq_entries * sizeof(struct io_uring_sqe);
    void* sqesPtr = mmap(0, sqesSize, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_SQES);
    if (sqesPtr == MAP_FAILED) goto fail;

    ring->m_sqes = (struct io_uring_sqe*)sqesPtr;

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

/* io ?????? */
static void ioDeinitCleanup(XNetIoRingPosix* posix) {
    if (posix->m_ownsRing && posix->m_ringFd >= 0) {
        size_t sqRingSize = (size_t)(posix->m_sqArray) -
                            (size_t)(posix->m_sqHeadPtr) +
                            posix->m_sqEntries * sizeof(unsigned);
        size_t cqRingSize = (size_t)(posix->m_cqMaskPtr) -
                            (size_t)(posix->m_cqHeadPtr) +
                            sizeof(unsigned) +
                            posix->m_sqEntries * sizeof(struct io_uring_cqe);
        size_t sqesSize = posix->m_sqEntries * sizeof(struct io_uring_sqe);

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
 * epoll ??(???:io_uring ????????)
 * ================================================================ */
#if XNET_BUILD_EPOLL

/** @brief ?????? fd ???????????(?? NULL)? */
static XNetPendingOp* epTakePending(XNetIoRingPosix* posix, int fd,
                                    int wantOut) {
    XNetPendingOp** pp = &posix->m_pendingHead;
    while (*pp) {
        XNetPendingOp* op = *pp;
        if (op->sqe.fd == fd) {
            unsigned want = (op->sqe.opcode == IORING_OP_SEND ||
                             op->sqe.opcode == IORING_OP_CONNECT)
                                ? XNET_EPOLL_WANT_OUT
                                : XNET_EPOLL_WANT_IN;
            if (want == wantOut) {
                *pp = op->next;
                return op;
            }
        }
        pp = &op->next;
    }
    return NULL;
}

/** @brief [2026-10-03 ???????????] ? fd ???????? epoll ???
 *  @details ????(?? ulimit -l 8 ?? epoll ????; ???? 5.2.9
 *           ???? epoll ????): ? fd ????????--????
 *           ????RECV(IN) ?? + ?? SEND(OUT) ???--????
 *           EPOLL_CTL_ADD ? EEXIST ?????, ????? IN, OUT ??
 *           ???? ? SEND ????, ????? connecting ??? XGR1
 *           ????; ??????? DEL ????? fd ?????????
 *           ?????????????(MOD/DEL): ?????????
 *           (?? ADD/?? DEL), ??????????? epoll ????
 *           ??, ?? io_uring ??????????? */
static void epRearmInterest(XNetIoRingPosix* posix, int fd) {
    XNetPendingOp* op;
    unsigned wantIn = 0;
    unsigned wantOut = 0;
    uint32_t mask = 0;
    struct epoll_event ev;
    for (op = posix->m_pendingHead; op; op = op->next) {
        if (op->sqe.fd != fd) continue;
        if (op->sqe.opcode == IORING_OP_SEND ||
            op->sqe.opcode == IORING_OP_CONNECT)
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

/** @brief ??????? IO(fd ???),??? CQ ???
 *  @return true ??????;false ????(EAGAIN)? */
static bool epExecutePending(XNetIoRingPosix* posix, XNetPendingOp* op,
                             XAbstractNetIoRing* self) {
    const struct io_uring_sqe* sqe = &op->sqe;
    ssize_t res;
    errno = 0;
    switch (sqe->opcode) {
    case IORING_OP_RECV:
        res = recv(sqe->fd, (void*)(uintptr_t)sqe->addr, sqe->len, 0);
        break;
    case IORING_OP_RECVMSG:
        res = recvmsg(sqe->fd, (struct msghdr*)(uintptr_t)sqe->addr, 0);
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

#if XNET_EPOLL_IO_DEBUG
    fprintf(stderr, "[XNET-EPOLL] exec op=%d fd=%d res=%zd errno=%d\n",
            (int)sqe->opcode, sqe->fd, res, errno);
#endif
    if (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /* [2026-10-03 ???????????] ????????????:
         * epTakePending ???????????, EAGAIN ?? return ??
         * ?????--?????(??/???????)??????,
         * ?? rearm ?? fd ???? DEL ??, ????????
         * (??: ?? ulimit ??, ??? PING EAGAIN ????
         *  ~30s ??????; ?? MCGS ??)?????, ???
         * ?????(????, ??????)? */
        op->next = posix->m_pendingHead;
        posix->m_pendingHead = op;
        return false; /* ????,?????? */
    }

    /* ??:????????????? CQ ?????
     * ([2026-10-03 ???????] ???? DEL ???? fd ????
     *   ??--? epRearmInterest ???; ????? DEL ??)? */
    epRearmInterest(posix, sqe->fd);
    processOneCompletion(self, (int64_t)res,
                         (void*)(uintptr_t)sqe->user_data);
    XFree_System(op);
    return true;
}

/** @brief ?????????(???? epoll ???),????? */
static void epExecuteFileSync(XNetIoRingPosix* posix,
                              XAbstractNetIoRing* self,
                              const struct io_uring_sqe* sqe) {
    ssize_t res;
    (void)posix;
    errno = 0;
    switch (sqe->opcode) {
    case IORING_OP_READ:
        /* ?????(????);ESPIPE(??/??fd)???????? */
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

/** @brief ??? epoll ?????????? */
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

/** @brief ?? epoll ??????? eventfd? */
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

/** @brief epoll ??????? */
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

/* ??????:?? XAbstractNetIoRing(vtable ???????) */
XCLASS_DEFINE_BEGING(XNetIoRingPosix)
XCLASS_DEFINE_EXTEND_END(XNetIoRingPosix, XAbstractNetIoRing)

/* ================================================================
 * ???????(??? mode ??)
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
    return true; /* io_uring SQE ?? fd;epoll ?????? ADD */
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

        /* [wake] 5s ?????????(?????)? */
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
            /* [wake] ??: fd ????(? ring fd, ? eventfd ????);
             * ??????(?????????)? */
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
    /* [wake] 5s ?????????(?????)? */
    if (xnet_wakeProfEnabled()) {
        uint64_t nowUs = XAbstractNetIoRing_profNowUs();
        if (!xnet_wakeProf.winStartUs)
            xnet_wakeProf.winStartUs = nowUs;
        else if (nowUs - xnet_wakeProf.winStartUs >= XNET_WAKE_PROF_WIN_US)
            xnet_wakeProfFlush("epoll", nowUs);
    }
    epPollPlatform(posix, self); /* ??????(??????????) */
    {
        struct epoll_event events[XNET_EPOLL_MAX_EVENTS];
        int n;
        int i;
        uint64_t enterUs = xnet_wakeProfEnabled()
                               ? XAbstractNetIoRing_profNowUs() : 0;
        do {
            n = epoll_wait(posix->m_epollFd, events, XNET_EPOLL_MAX_EVENTS,
                           timeoutMs);
        } while (n < 0 && errno == EINTR);
        /* [wake] ??: ?? fd ????(wakeFd ?????); ??????? */
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

/** @brief ???????(???????????)? */
XVtable* XNetIoRingPosix_class_init(void) {
    XVTABLE_INIT_DEFAULT(XNetIoRingPosix)
    XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing);

    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_GetEventFd,    VXNetIoRingPosix_getEventFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_PollPlatform,  VXNetIoRingPosix_pollPlatform);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_RegisterEvent, VXNetIoRingPosix_registerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WaitForEvents, VXNetIoRingPosix_waitForEvents);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WakeUp,        VXNetIoRingPosix_wakeUp);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetIoRingPosix_deinit);

    return XVTABLE_DEFAULT;
}

/* ================================================================
 * ??????(??????:io_uring ?? -> epoll)
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

    /* ?????(???????? + ?? SQ/CQ ??) */
    XAbstractNetIoRing_init((XAbstractNetIoRing*)ring);

    /* ?????????? */
    XClassSetVtable(ring, XNetIoRingPosix);

    /* ??????:?? io_uring(Linux 5.1+ ??);io_uring_setup
     * ??(-ENOSYS ?,? 4.x ??)???? epoll? */
#if XNET_BUILD_IO_URING
    if (ioSetupRing(ring)) {
        ring->m_mode = XNET_MODE_IOURING;
        enabled = true;
    }
#endif
#if XNET_BUILD_EPOLL
    if (!enabled && epSetup(ring)) {
        ring->m_mode = XNET_MODE_EPOLL;
        enabled = true;
    }
#endif
    ((XAbstractNetIoRing*)ring)->m_enabled = enabled;
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
 * ?? I/O ?? API(?????,????????)
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

struct io_uring_sqe* XNetIoRingPosix_getSqe(XNetIoRingPosix* ring) {
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
        struct io_uring_sqe* sqe;
        XNetPendingOp* op;
        struct epoll_event ev;
        if (!ring->m_hasCurrentSqe) return;
        sqe = &ring->m_currentSqe;
        ring->m_hasCurrentSqe = false;
        ctx = (XEventContext*)(uintptr_t)sqe->user_data;

        switch (sqe->opcode) {
        /* ???:????(???? epoll ???),??????? */
        case IORING_OP_READ:
        case IORING_OP_WRITE:
        case IORING_OP_FSYNC:
            epExecuteFileSync(ring, self, sqe);
            return;
        /* ??:???????????? -ECANCELED ??? */
        case IORING_OP_ASYNC_CANCEL: {
            XNetPendingOp** pp = &ring->m_pendingHead;
            XEventContext* target = (XEventContext*)(uintptr_t)sqe->addr;
            while (*pp) {
                XNetPendingOp* victim = *pp;
                if (victim->ctx == target) {
                    *pp = victim->next;
                    epoll_ctl(ring->m_epollFd, EPOLL_CTL_DEL,
                              victim->sqe.fd, NULL);
                    XFree_System(victim);
                    break;
                }
                pp = &victim->next;
            }
            if (target) target->result = -ECANCELED;
            processOneCompletion(self, target ? 0 : -ENOENT,
                                 (void*)(uintptr_t)sqe->user_data);
            return;
        }
        default:
            break;
        }

        /* ???:?? epoll ?????,?????????? */
        op = (XNetPendingOp*)XMalloc_System(sizeof(*op));
        if (!op) {
            processOneCompletion(self, -ENOMEM,
                                 (void*)(uintptr_t)sqe->user_data);
            return;
        }
        op->sqe = *sqe;
        op->ctx = ctx;
        op->next = ring->m_pendingHead;
        ring->m_pendingHead = op;
#if XNET_EPOLL_IO_DEBUG
        fprintf(stderr, "[XNET-EPOLL] submit op=%d fd=%d\n",
                (int)sqe->opcode, sqe->fd);
#endif

        memset(&ev, 0, sizeof(ev));
        ev.events = (sqe->opcode == IORING_OP_SEND ||
                     sqe->opcode == IORING_OP_CONNECT)
                        ? XNET_EPOLL_OUT_INTEREST
                        : XNET_EPOLL_IN_INTEREST;
        ev.data.fd = sqe->fd;
        if (epoll_ctl(ring->m_epollFd, EPOLL_CTL_ADD, sqe->fd, &ev) != 0) {
            if (errno == EEXIST) {
                /* [2026-10-03 ???????] fd ???(??:RECV ??
                 * ??? SEND/CONNECT)--????????????
                 * (IN|OUT, ?????????),??????? */
                epRearmInterest(ring, sqe->fd);
                return;
            }
            ring->m_pendingHead = op->next;
            XFree_System(op);
            processOneCompletion(self, -errno,
                                 (void*)(uintptr_t)sqe->user_data);
        }
        (void)toSubmit; /* epoll ???????? */
        return;
    }
#endif
}

int XNetIoRingPosix_waitCqe(XNetIoRingPosix* ring, uint64_t userData) {
#if XNET_BUILD_IO_URING
    if (ring && ring->m_mode == XNET_MODE_IOURING) {
        /* ?? I/O ??????????:????? GETEVENTS(??
           1ms),???? CQE ??(???????????????
           ?? -1)? */
        int retry;
        for (retry = 0; retry < 100; ++retry) {
            struct __kernel_timespec ts = { 0, 1000000 }; /* 1ms ???? */
            (void)syscall(__NR_io_uring_enter, ring->m_ringFd, 0, 1,
                          IORING_ENTER_GETEVENTS, &ts, NULL);
            {
                unsigned head = __atomic_load_n(ring->m_cqHeadPtr,
                                                __ATOMIC_ACQUIRE);
                unsigned tail = __atomic_load_n(ring->m_cqTailPtr,
                                                __ATOMIC_ACQUIRE);
                if (head != tail) {
                    struct io_uring_cqe* cqe =
                        &ring->m_cqes[head & (*ring->m_cqMaskPtr)];
                    if (userData == 0 || cqe->user_data == userData) {
                        __atomic_store_n(ring->m_cqHeadPtr, head + 1,
                                         __ATOMIC_RELEASE);
                        return cqe->res;
                    }
                    /* ?????:??(?? head ????) */
                    __atomic_store_n(ring->m_cqHeadPtr, head + 1,
                                     __ATOMIC_RELEASE);
                }
            }
        }
        return -1;
    }
#endif
#if XNET_BUILD_EPOLL
    /* ?????? submitSqe ????????? ctx->result;
       ????????? waitCqe(?????????)? */
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
 * ????:?? Linux ??(io_uring ? epoll ????????)
 * ================================================================ */
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void) {
    return (XAbstractNetIoRing*)XNetIoRingPosix_create();
}

#endif /* __linux__ */

#endif /* XAbstractNetIoRing_ON */
