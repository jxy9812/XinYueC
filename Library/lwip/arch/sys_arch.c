/**
 * @file sys_arch.c
 * @brief lwIP OS 抽象层 基于 XSync 实现（原 STM32F407+FreeRTOS 移植参考代码）
 *
 * 本文件将 lwIP 的 OS 抽象层接口（sys_arch_protect 等、信号量、互斥锁、
 * 邮箱、线程等）映射到 XSync 实现
 *
 * 对应关系:
 *   FreeRTOS xSemaphoreCreateRecursiveMutex -> XMutex (XLock_Recursive)
 *   FreeRTOS xSemaphoreCreateBinary        -> XSemaphore
 *   FreeRTOS xQueueCreate                  -> XVector + XMutex + XSemaphore(计数型)
 *   FreeRTOS xTaskCreate                   -> XThread
 *
 * 注意事项:
 *   - 所有内存分配走 XMalloc_System / XFree_System
 *   - sys_arch_protect 与 sys_lock_tcpip_core 使用同一把递归锁
 *   - 定时器已改为事件驱动 + 时间轮 + 无轮询
 */

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"
#include "lwip/tcpip.h"

#if SYS_LIGHTWEIGHT_PROT || !NO_SYS
#include "XMutex.h"        /* sys_arch_protect / sys_lock_tcpip_core 需要 */
#endif
#include "XDateTime.h"     /* 始终需要：sys_now / sys_jiffies */
#include "XMemory.h"       /* 始终需要：定时器 XMalloc/XFree */
#if !NO_SYS
/* 以下头文件仅在 OS 模式（NO_SYS=0）需要：
 *   XSemaphore - 信号量 + 邮箱实现
 *   XThread    - sys_thread_new + sys_arch_msleep
 *   XVarList   - sys_thread_new 参数传递
 * 裸机模式（NO_SYS=1）不编译，减少代码体积 */
#include "XSemaphore.h"
#include "XThread.h"
#include "XVarList.h"
#endif /* !NO_SYS */
#include <string.h>

/* ================================================================
 * 全局变量
 * ================================================================ */

/* ================================================================
 * 核心锁(sys_arch_protect 与 sys_lock_tcpip_core 共用)
 * 递归互斥锁,确保任意线程可重入保护,无死锁
 * NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=0 时不需要,不编译以减少体积
 * ================================================================ */
#if SYS_LIGHTWEIGHT_PROT || !NO_SYS
static XMutex* g_coreLock = NULL;
#endif

void sys_init(void)
{
#if SYS_LIGHTWEIGHT_PROT || !NO_SYS
    /* 在 lwip_init() 之前调用,此时可能未初始化,需创建 */
    if (!g_coreLock) {
        g_coreLock = XMutex_create(XLock_Recursive);
    }
#endif
}

/* ================================================================
 * 时间函数
 * ================================================================ */

/**
 * @brief 获取当前时间(毫秒)
 * @return 距 Epoch 的毫秒数(截断 32 位)
 * @note lwIP 要求返回 32 位毫秒数,约每 49.7 天溢出,不影响使用
 */
u32_t sys_now(void)
{
    return (u32_t)XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief 获取系统 jiffies(毫秒计数)
 * @return 毫秒计数
 */
u32_t sys_jiffies(void)
{
    return (u32_t)XDateTime_currentMSecsSinceEpoch();
}

#if !NO_SYS
/**
 * @brief 毫秒级睡眠(阻塞) - 仅 OS 模式需要
 * @param delay_ms 睡眠毫秒数
 * @note 裸机模式（NO_SYS=1）下：
 *   - lwIP 的 sys_msleep 是空宏（sys.h 中 #define sys_msleep(t) 为空）
 *   - sys_arch.h 不被包含，此函数声明不可见
 *   - 此函数为死代码，不编译以减少体积
 */
void sys_arch_msleep(u32_t delay_ms)
{
    XThread_msleep(delay_ms);
}
#endif /* !NO_SYS */

/* ================================================================
 * 临界区保护(SYS_LIGHTWEIGHT_PROT)
 *
 * 与 sys_lock_tcpip_core 共用同一把锁,确保一致性
 * 设计考虑:
 *   - Raw API 调用者需自行调用 sys_arch_protect
 *   - 中断上下文不适用本接口
 * ================================================================ */

#if SYS_LIGHTWEIGHT_PROT

/**
 * @brief 进入临界区
 * @return 保护值(传给 sys_arch_unprotect)
 * @note 递归锁可重入,支持嵌套调用
 */
sys_prot_t sys_arch_protect(void)
{
    XMutex_lock(g_coreLock);
    return 1;
}

/**
 * @brief 退出临界区
 * @param pval sys_arch_protect 的返回值
 */
void sys_arch_unprotect(sys_prot_t pval)
{
    (void)pval;
    XMutex_unlock(g_coreLock);
}

#endif /* SYS_LIGHTWEIGHT_PROT */

/* TCP_REG 宏调用此函数;自定义定时器下 TCP 定时器始终注册,此处为空操作 */
void tcp_timer_needed(void) {}


/* ================================================================
 * lwIP custom timer backend (LWIP_TIMERS_CUSTOM=1)
 * Direct integration with XTimeWheelGroup - no sys_check_timeouts polling
 *
 * sys_timeout()  -> register single-shot timer to XTimeWheelGroup
 * timer expires  -> wrapper acquires core lock -> call lwIP handler
 * sys_check_timeouts() -> no-op (event-driven, not polled)
 * ================================================================ */
#if LWIP_TIMERS && LWIP_TIMERS_CUSTOM

#include "lwip/timeouts.h"
#include "lwip/def.h"       /* LWIP_ARRAYSIZE */
#include "lwip/priv/tcp_priv.h"  /* tcp_tmr, TCP_TMR_INTERVAL */
#include "lwip/ip4_frag.h"  /* ip_reass_tmr */
#include "lwip/etharp.h"    /* etharp_tmr */
#include "lwip/dhcp.h"      /* dhcp_coarse_tmr, dhcp_fine_tmr */
#include "lwip/acd.h"       /* acd_tmr */
#include "lwip/igmp.h"      /* igmp_tmr */
#include "lwip/dns.h"       /* dns_tmr */
#include "lwip/nd6.h"       /* nd6_tmr */
#include "lwip/ip6_frag.h"  /* ip6_reass_tmr */
#include "lwip/mld6.h"      /* mld6_tmr */
#include "lwip/dhcp6.h"     /* dhcp6_tmr */
#include "XTimeWheelGroup.h"

/* Cyclic timer 定义 - 从 timeouts.c 迁移,使该文件可完全排除编译。 */
/* LWIP_TIMERS_CUSTOM 下 timeouts.c 只剩此数据数组 + tcp_timer_needed, */
/* 内联到此处后即可从构建中移除 timeouts.c。 */
#if LWIP_DEBUG_TIMERNAMES
#define HANDLER(x) x, #x
#else
#define HANDLER(x) x
#endif

const struct lwip_cyclic_timer lwip_cyclic_timers[] = {
#if LWIP_TCP
  {TCP_TMR_INTERVAL, HANDLER(tcp_tmr)},
#endif /* LWIP_TCP */
#if LWIP_IPV4
#if IP_REASSEMBLY
  {IP_TMR_INTERVAL, HANDLER(ip_reass_tmr)},
#endif /* IP_REASSEMBLY */
#if LWIP_ARP
  {ARP_TMR_INTERVAL, HANDLER(etharp_tmr)},
#endif /* LWIP_ARP */
#if LWIP_DHCP
  {DHCP_COARSE_TIMER_MSECS, HANDLER(dhcp_coarse_tmr)},
  {DHCP_FINE_TIMER_MSECS, HANDLER(dhcp_fine_tmr)},
#endif /* LWIP_DHCP */
#if LWIP_ACD
  {ACD_TMR_INTERVAL, HANDLER(acd_tmr)},
#endif /* LWIP_ACD */
#if LWIP_IGMP
  {IGMP_TMR_INTERVAL, HANDLER(igmp_tmr)},
#endif /* LWIP_IGMP */
#endif /* LWIP_IPV4 */
#if LWIP_DNS
  {DNS_TMR_INTERVAL, HANDLER(dns_tmr)},
#endif /* LWIP_DNS */
#if LWIP_IPV6
  {ND6_TMR_INTERVAL, HANDLER(nd6_tmr)},
#if LWIP_IPV6_REASS
  {IP6_REASS_TMR_INTERVAL, HANDLER(ip6_reass_tmr)},
#endif /* LWIP_IPV6_REASS */
#if LWIP_IPV6_MLD
  {MLD6_TMR_INTERVAL, HANDLER(mld6_tmr)},
#endif /* LWIP_IPV6_MLD */
#if LWIP_IPV6_DHCP6
  {DHCP6_TIMER_MSECS, HANDLER(dhcp6_tmr)},
#endif /* LWIP_IPV6_DHCP6 */
#endif /* LWIP_IPV6 */
};
const int lwip_num_cyclic_timers = LWIP_ARRAYSIZE(lwip_cyclic_timers);


/* Timer context - bridges lwIP (handler, arg) and XTimeWheelGroup (handle) */
typedef struct {
    sys_timeout_handler handler;
    void* arg;
    XHandle timerHandle;
    volatile int cancelled;
} lwip_timer_ctx_t;

/* Lookup table for sys_untimeout - finds timer by (handler, arg) */
#define LWIP_TIMER_LOOKUP_MAX 16
static lwip_timer_ctx_t* g_timerLookup[LWIP_TIMER_LOOKUP_MAX];

static int lwip_timer_lookup_find(sys_timeout_handler handler, void* arg) {
    int i;
    for (i = 0; i < LWIP_TIMER_LOOKUP_MAX; i++) {
        lwip_timer_ctx_t* e = g_timerLookup[i];
        if (e && e->handler == handler && e->arg == arg) return i;
    }
    return -1;
}

static int lwip_timer_lookup_add(lwip_timer_ctx_t* ctx) {
    int i;
    for (i = 0; i < LWIP_TIMER_LOOKUP_MAX; i++) {
        if (!g_timerLookup[i]) { g_timerLookup[i] = ctx; return i; }
    }
    return -1;
}

static void lwip_timer_lookup_remove(int idx) {
    if (idx >= 0 && idx < LWIP_TIMER_LOOKUP_MAX) g_timerLookup[idx] = NULL;
}

/* XTimeWheelGroup callback wrapper - runs in timer thread,
 * must acquire lwIP core lock before calling lwIP handler */
static void lwip_timer_wrapper(void* userData, XTimerData* timer) {
    (void)timer;
    lwip_timer_ctx_t* ctx = (lwip_timer_ctx_t*)userData;
    if (!ctx) return;

    /* Acquire lwIP core lock:
     * NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=1 -> sys_arch_protect (g_coreLock)
     * NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=0 -> no lock (single-threaded, zero overhead)
     * NO_SYS=0 -> LOCK_TCPIP_CORE (synchronize with tcpip_thread) */
#if NO_SYS && SYS_LIGHTWEIGHT_PROT
    sys_prot_t prot = sys_arch_protect();
#elif !NO_SYS
    LOCK_TCPIP_CORE();
#endif

    /* Remove from lookup so sys_untimeout wont find a fired timer */
    int idx = lwip_timer_lookup_find(ctx->handler, ctx->arg);
    if (idx >= 0) lwip_timer_lookup_remove(idx);

    if (!ctx->cancelled) {
        ctx->handler(ctx->arg);
    }

#if NO_SYS && SYS_LIGHTWEIGHT_PROT
    sys_arch_unprotect(prot);
#elif !NO_SYS
    UNLOCK_TCPIP_CORE();
#endif
    XFree_Hybrid(ctx);
}

/* Cyclic timer handler - calls lwIP internal handler and reschedules */
static void lwip_cyclic_timer_cb(void* arg) {
    const struct lwip_cyclic_timer* cyclic = (const struct lwip_cyclic_timer*)arg;
    cyclic->handler();
    sys_timeout(cyclic->interval_ms, lwip_cyclic_timer_cb, arg);
}

void sys_timeout(u32_t msecs, sys_timeout_handler handler, void* arg) {
    lwip_timer_ctx_t* ctx;
    XTimerData td;

    ctx = (lwip_timer_ctx_t*)XMalloc_Hybrid(sizeof(lwip_timer_ctx_t));
    if (!ctx) return;
    ctx->handler = handler;
    ctx->arg = arg;
    ctx->cancelled = 0;
    ctx->timerHandle = NULL;

    XTimerData_init(&td, NULL);
    XTimerData_setTimeout(&td, msecs);
    XTimerData_setInterval(&td, msecs);
    XTimerData_setTimerCallback(&td, lwip_timer_wrapper);
    XTimerData_setUserData(&td, ctx);
    XTimerData_setSingleShot(&td, true);
    ctx->timerHandle = XTimeWheelGroup_addTimerMs_base(
        (XTimerGroupBase*)XTimeWheelGroup_global(), td);

    if (lwip_timer_lookup_add(ctx) < 0) {
        XTimeWheelGroup_removeTimer_base(
            (XTimerGroupBase*)XTimeWheelGroup_global(), ctx->timerHandle);
        XFree_Hybrid(ctx);
    }
}

void sys_untimeout(sys_timeout_handler handler, void* arg) {
    int idx = lwip_timer_lookup_find(handler, arg);
    if (idx < 0) return;

    lwip_timer_ctx_t* ctx = g_timerLookup[idx];
    lwip_timer_lookup_remove(idx);
    ctx->cancelled = 1;

    bool removed = XTimeWheelGroup_removeTimer_base(
        (XTimerGroupBase*)XTimeWheelGroup_global(), ctx->timerHandle);

    if (removed) {
        XFree_Hybrid(ctx);
    }
}

void sys_check_timeouts(void) {
    /* No-op - timers are event-driven via XTimeWheelGroup */
}

void sys_timeouts_init(void) {
#if NO_SYS && SYS_LIGHTWEIGHT_PROT
    sys_prot_t prot = sys_arch_protect();
#elif !NO_SYS
    LOCK_TCPIP_CORE();
#endif
    size_t i;
    /* Register ALL cyclic timers including TCP (index 0).
     * Original lwIP skips TCP and starts on demand via tcp_timer_needed(),
     * but LWIP_TIMERS_CUSTOM makes tcp_timer_needed() a no-op,
     * so we register TCP timer directly (overhead is negligible). */
    for (i = 0; i < lwip_num_cyclic_timers; i++) {
        sys_timeout(lwip_cyclic_timers[i].interval_ms,
                    lwip_cyclic_timer_cb,
                    (void*)&lwip_cyclic_timers[i]);
    }
#if NO_SYS && SYS_LIGHTWEIGHT_PROT
    sys_arch_unprotect(prot);
#elif !NO_SYS
    UNLOCK_TCPIP_CORE();
#endif
}

void sys_restart_timeouts(void) {
    /* No-op - XTimeWheelGroup manages timing internally */
}

u32_t sys_timeouts_sleeptime(void) {
    return SYS_TIMEOUTS_SLEEPTIME_INFINITE;
}

#endif /* LWIP_TIMERS && LWIP_TIMERS_CUSTOM */

#if !NO_SYS
/* 完整 OS 模式(NO_SYS=0)专用:需要线程同步原语和邮箱
 * 裸机模式(NO_SYS=1)时 sys.h 不会引用这些函数,无需编译 */
/* ================================================================
 * 互斥锁
 *
 * 递归互斥锁,由 FreeRTOS xSemaphoreCreateRecursiveMutex 替代
 * 确保任意线程可重入保护,无死锁
 * ================================================================ */

err_t sys_mutex_new(sys_mutex_t* mutex)
{
    if (!mutex) return ERR_ARG;
    mutex->mut = XMutex_create(XLock_Recursive);
    return mutex->mut ? ERR_OK : ERR_MEM;
}

void sys_mutex_lock(sys_mutex_t* mutex)
{
    if (mutex && mutex->mut) XMutex_lock((XMutex*)mutex->mut);
}

void sys_mutex_unlock(sys_mutex_t* mutex)
{
    if (mutex && mutex->mut) XMutex_unlock((XMutex*)mutex->mut);
}

void sys_mutex_free(sys_mutex_t* mutex)
{
    if (mutex && mutex->mut) {
        XMutex_delete((XMutex*)mutex->mut);
        mutex->mut = NULL;
    }
}

/* ================================================================
 * 信号量
 *
 * 二值信号量用于事件通知,由 FreeRTOS xSemaphoreCreateBinary 替代
 * 计数型用于资源池管理
 * ================================================================ */

err_t sys_sem_new(sys_sem_t* sem, u8_t initial_count)
{
    if (!sem) return ERR_ARG;
    /* XSemaphore 最大计数为 1;>1 需特殊处理 */
    sem->sem = XSemaphore_create(initial_count ? 1 : 0, 1);
    return sem->sem ? ERR_OK : ERR_MEM;
}

void sys_sem_signal(sys_sem_t* sem)
{
    if (sem && sem->sem) XSemaphore_release((XSemaphore*)sem->sem, 1);
}

u32_t sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout_ms)
{
    if (!sem || !sem->sem) return SYS_ARCH_TIMEOUT;

    if (!timeout_ms) {
        /* 永久等待(阻塞) */
        XSemaphore_acquire((XSemaphore*)sem->sem, 1);
    } else {
        /* 超时等待 */
        if (!XSemaphore_tryAcquireTimeout((XSemaphore*)sem->sem, 1, timeout_ms)) {
            return SYS_ARCH_TIMEOUT;
        }
    }
    return 1;
}

void sys_sem_free(sys_sem_t* sem)
{
    if (sem && sem->sem) {
        XSemaphore_delete((XSemaphore*)sem->sem);
        sem->sem = NULL;
    }
}

/* ================================================================
 * 邮箱(消息队列)
 *
 * XSync 无原生邮箱,用互斥锁 + 信号量 + 环形缓冲实现
 * 特性:
 *   - 固定大小元素队列(void*)
 *   - 线程安全入队出队
 *   - 支持超时等待获取
 *   - 替代 FreeRTOS xQueueCreate
 * ================================================================ */

/* 邮箱实现: 环形缓冲 + 互斥锁 + 信号量 */
typedef struct {
    XMutex*   mutex;          /* 互斥锁:保护队列操作 */
    XSemaphore* sem;           /* 信号量:通知有消息到达 */
    void**    msgBuf;          /* 消息缓冲区数组 */
    uint32_t  head;            /* 队头索引(读取) */
    uint32_t  tail;            /* 队尾索引(写入) */
    uint32_t  count;           /* 当前消息数量 */
    uint32_t  maxSize;         /* 最大容量 */
} mbox_impl_t;

err_t sys_mbox_new(sys_mbox_t* mbox, int size)
{
    if (!mbox) return ERR_ARG;
    if (size <= 0) size = 128;  /* TCPIP_MBOX_SIZE=0 means use default size */

    /* 用 XMalloc_System 分配,因 lwipopts.h 中 MEM_CUSTOM_MALLOC 已开启 */
    mbox_impl_t* impl = (mbox_impl_t*)XMalloc_System(sizeof(mbox_impl_t));
    if (!impl) return ERR_MEM;

    memset(impl, 0, sizeof(*impl));
    impl->maxSize = (uint32_t)size;

    impl->msgBuf = (void**)XMalloc_System(sizeof(void*) * impl->maxSize);
    if (!impl->msgBuf) {
        XFree_System(impl);
        return ERR_MEM;
    }

    impl->mutex = XMutex_create(XLock_NonRecursive);
    impl->sem = XSemaphore_create(0, (int32_t)impl->maxSize);

    if (!impl->mutex || !impl->sem) {
        /* 创建失败,清理已分配资源 */
        if (impl->mutex) XMutex_delete(impl->mutex);
        if (impl->sem) XSemaphore_delete(impl->sem);
        XFree_System(impl->msgBuf);
        XFree_System(impl);
        return ERR_MEM;
    }

    mbox->mbx = impl;
    return ERR_OK;
}

void sys_mbox_post(sys_mbox_t* mbox, void* msg)
{
    if (!mbox || !mbox->mbx) return;
    mbox_impl_t* impl = (mbox_impl_t*)mbox->mbx;

    XMutex_lock(impl->mutex);
    if (impl->count < impl->maxSize) {
        /* 队列未满,入队 */
        impl->msgBuf[impl->tail] = msg;
        impl->tail = (impl->tail + 1) % impl->maxSize;
        impl->count++;
        XMutex_unlock(impl->mutex);
        /* 通知消费者,有新消息 */
        XSemaphore_release(impl->sem, 1);
    } else {
        XMutex_unlock(impl->mutex);
        /* 队列满,丢弃(与 FreeRTOS 行为:post 不阻塞) */
    }
}

err_t sys_mbox_trypost(sys_mbox_t* mbox, void* msg)
{
    if (!mbox || !mbox->mbx) return ERR_ARG;
    mbox_impl_t* impl = (mbox_impl_t*)mbox->mbx;

    XMutex_lock(impl->mutex);
    if (impl->count < impl->maxSize) {
        impl->msgBuf[impl->tail] = msg;
        impl->tail = (impl->tail + 1) % impl->maxSize;
        impl->count++;
        XMutex_unlock(impl->mutex);
        XSemaphore_release(impl->sem, 1);
        return ERR_OK;
    }
    XMutex_unlock(impl->mutex);
    return ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t* mbox, void* msg)
{
    /* Windows 无 ISR 概念,直接调用 trypost */
    return sys_mbox_trypost(mbox, msg);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t* mbox, void** msg, u32_t timeout_ms)
{
    if (!mbox || !mbox->mbx || !msg) return SYS_ARCH_TIMEOUT;
    mbox_impl_t* impl = (mbox_impl_t*)mbox->mbx;

    /* 永久等待(无超时) */
    if (!timeout_ms) {
        XSemaphore_acquire(impl->sem, 1);
    } else {
        if (!XSemaphore_tryAcquireTimeout(impl->sem, 1, timeout_ms)) {
            *msg = NULL;
            return SYS_ARCH_TIMEOUT;
        }
    }

    /* 取出消息 */
    XMutex_lock(impl->mutex);
    if (impl->count > 0) {
        *msg = impl->msgBuf[impl->head];
        impl->head = (impl->head + 1) % impl->maxSize;
        impl->count--;
    } else {
        *msg = NULL;
    }
    XMutex_unlock(impl->mutex);

    return 1;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t* mbox, void** msg)
{
    if (!mbox || !mbox->mbx || !msg) return SYS_MBOX_EMPTY;
    mbox_impl_t* impl = (mbox_impl_t*)mbox->mbx;

    /* 非阻塞获取(立即返回) */
    if (!XSemaphore_tryAcquire((XSemaphore*)impl->sem, 1)) {
        *msg = NULL;
        return SYS_MBOX_EMPTY;
    }

    XMutex_lock(impl->mutex);
    if (impl->count > 0) {
        *msg = impl->msgBuf[impl->head];
        impl->head = (impl->head + 1) % impl->maxSize;
        impl->count--;
    } else {
        *msg = NULL;
    }
    XMutex_unlock(impl->mutex);

    return 0;
}

void sys_mbox_free(sys_mbox_t* mbox)
{
    if (!mbox || !mbox->mbx) return;
    mbox_impl_t* impl = (mbox_impl_t*)mbox->mbx;

    /* 释放所有资源 */
    XMutex_delete(impl->mutex);
    XSemaphore_delete(impl->sem);
    XFree_System(impl->msgBuf);   /* 用 XFree_System 与 XMalloc_System 配对 */
    XFree_System(impl);
    mbox->mbx = NULL;
}

/* ================================================================
 * 线程
 *
 * 用 XThread 创建线程,替代 FreeRTOS xTaskCreate
 * 需要把 lwIP 线程函数包装成 XThread 可调用形式
 * ================================================================ */

/* 线程包装器: 用于包装 lwIP 线程函数 */
typedef struct {
    lwip_thread_fn func;    /* lwIP 线程入口函数 */
    void* arg;              /* 用户参数 */
} lwip_thread_wrap_t;

/**
 * @brief XThread 线程入口
 * @param xthread XThread 对象
 * @param vars 可变参数表(含 lwip_thread_wrap_t)
 * @note 线程结束后释放包装器内存
 */
static void lwip_xthread_wrapper(XThread* xthread, XVarList* vars)
{
    (void)xthread;
    XVarList_args_1(vars, lwip_thread_wrap_t*, wrap);
    if (wrap && wrap->func) {
        wrap->func(wrap->arg);
    }
    /* 线程结束后释放包装器内存 */
    XFree_System(wrap);
}

sys_thread_t sys_thread_new(const char* name, lwip_thread_fn thread, void* arg, int stacksize, int prio)
{
    (void)name; (void)prio;
    sys_thread_t st;

    /* 创建线程包装器 */
    lwip_thread_wrap_t* wrap = (lwip_thread_wrap_t*)XMalloc_System(sizeof(lwip_thread_wrap_t));
    if (!wrap) {
        st.thread_handle = NULL;
        return st;
    }
    wrap->func = thread;
    wrap->arg = arg;

    /* 用 XThread 创建 */
    XVarList* vl = XVarList_Create(XVar(lwip_thread_wrap_t*, wrap));
    XThread* xt = XThread_create_func(lwip_xthread_wrapper, vl);
    if (xt) {
        XThread_setStackSize(xt, stacksize > 0 ? (uint32_t)stacksize : 2048);
        XThread_start(xt);
        st.thread_handle = xt;
    } else {
        st.thread_handle = NULL;
        XFree_System(wrap);
    }

    return st;
}

/* ================================================================
 * 核心锁
 *
 * 与 sys_arch_protect 共用同一把 g_coreLock
 * 当 LWIP_TCPIP_CORE_LOCKING=1 时,允许调用者直接操作 Raw API,
 * 而不必通过邮箱向 tcpip_thread 发送请求
 * ================================================================ */

#if LWIP_TCPIP_CORE_LOCKING

/**
 * @brief 锁定 TCP/IP 核心
 * @note 与 sys_arch_protect 使用同一把锁
 */
void sys_lock_tcpip_core(void)
{
    XMutex_lock(g_coreLock);
}

/**
 * @brief 解锁 TCP/IP 核心
 */
void sys_unlock_tcpip_core(void)
{
    XMutex_unlock(g_coreLock);
}

#endif /* LWIP_TCPIP_CORE_LOCKING */

#endif /* !NO_SYS */
