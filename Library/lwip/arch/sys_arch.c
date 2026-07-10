/**
 * @file sys_arch.c
 * @brief lwIP OS 抽象层 鈥?XSync 实现（基于 STM32F407+FreeRTOS 成功移植经验）
 *
 * 本文件将 lwIP 的 OS 抽象接口（sys_arch_protect、互斥锁、信号量、
 * 邮箱队列、线程创建）映射到 XSync 库。
 *
 * 架构对应关系：
 *   FreeRTOS xSemaphoreCreateRecursiveMutex 鈫?XMutex (XLock_Recursive)
 *   FreeRTOS xSemaphoreCreateBinary        鈫?XSemaphore
 *   FreeRTOS xQueueCreate                  鈫?XVector + XMutex + XSemaphore（模拟邮箱）
 *   FreeRTOS xTaskCreate                   鈫?XThread
 *
 * 注意事项：
 *   - 所有内存分配统一使用 XMalloc_System / XFree_System
 *   - sys_arch_protect 与 sys_lock_tcpip_core 共享同一个递归互斥锁
 *   - 邮箱实现使用环形缓冲区 + 互斥锁 + 信号量模拟
 */

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"
#include "lwip/tcpip.h"

#include "XMutex.h"
#include "XSemaphore.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XVarList.h"
#include <string.h>

/* ================================================================
 * 一、初始化
 * ================================================================ */

/* ================================================================
 * 核心互斥锁（sys_arch_protect 和 sys_lock_tcpip_core 共享）
 * 使用递归互斥锁，与 FreeRTOS xSemaphoreCreateRecursiveMutex 一致
 * 递归锁允许同一线程多次加锁，避免死锁
 * ================================================================ */
static XMutex* g_coreLock = NULL;

void sys_init(void)
{
    /* 在 lwip_init() 中调用，此时单线程，安全初始化 */
    if (!g_coreLock) {
        g_coreLock = XMutex_create(XLock_Recursive);
    }
}

/* ================================================================
 * 二、时间
 * ================================================================ */

/**
 * @brief 获取当前系统时间（毫秒）
 * @return 自 Epoch 以来的毫秒数（取低 32 位）
 * @note lwIP 内部使用 32 位毫秒计数器，溢出周期约 49.7 天，属于正常行为
 */
u32_t sys_now(void)
{
    return (u32_t)XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief 获取系统 jiffies（毫秒滴答数）
 * @return 毫秒滴答数
 */
u32_t sys_jiffies(void)
{
    return (u32_t)XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief 线程休眠（毫秒）
 * @param delay_ms 休眠毫秒数
 */
void sys_arch_msleep(u32_t delay_ms)
{
    XThread_msleep(delay_ms);
}

/* ================================================================
 * 三、轻量级保护（SYS_LIGHTWEIGHT_PROT）
 *
 * 与 sys_lock_tcpip_core 共享同一个递归互斥锁，确保线程安全。
 * 使用递归锁的原因：
 *   - Raw API 回调中可能再次调用 sys_arch_protect
 *   - 同一线程在持有核心锁时可以安全地再次获取
 * ================================================================ */

#if SYS_LIGHTWEIGHT_PROT

/**
 * @brief 进入临界区保护
 * @return 保护值（用于 sys_arch_unprotect）
 * @note 使用递归互斥锁，同一线程可多次调用
 */
sys_prot_t sys_arch_protect(void)
{
    XMutex_lock(g_coreLock);
    return 1;
}

/**
 * @brief 退出临界区保护
 * @param pval sys_arch_protect 的返回值
 */
void sys_arch_unprotect(sys_prot_t pval)
{
    (void)pval;
    XMutex_unlock(g_coreLock);
}

#endif /* SYS_LIGHTWEIGHT_PROT */

/* ================================================================
 * 四、互斥锁
 *
 * 使用递归互斥锁，与 FreeRTOS xSemaphoreCreateRecursiveMutex 一致。
 * 递归锁允许同一线程多次加锁，避免死锁。
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
 * 五、信号量
 *
 * 使用二进制信号量或计数信号量，与 FreeRTOS xSemaphoreCreateBinary 一致。
 * 支持阻塞等待和超时等待。
 * ================================================================ */

err_t sys_sem_new(sys_sem_t* sem, u8_t initial_count)
{
    if (!sem) return ERR_ARG;
    /* XSemaphore 最大值设为 1 表示二进制信号量；>1 表示计数信号量 */
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
        /* 无限等待（阻塞） */
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
 * 六、邮箱（消息队列）
 *
 * XSync 没有原生的消息队列，使用环形缓冲区 + 互斥锁 + 信号量模拟。
 * 实现方式：
 *   - 环形缓冲区存储消息指针（void*）
 *   - 互斥锁保护队列访问
 *   - 信号量用于阻塞等待新消息
 *   - 与 FreeRTOS xQueueCreate 行为一致
 * ================================================================ */

/* 邮箱实现结构体 鈥?环形缓冲区 + 互斥锁 + 信号量 */
typedef struct {
    XMutex*   mutex;          /* 保护队列访问的互斥锁 */
    XSemaphore* sem;           /* 信号量：用于阻塞等待新消息 */
    void**    msgBuf;          /* 消息指针环形缓冲区 */
    uint32_t  head;            /* 读取位置（出队） */
    uint32_t  tail;            /* 写入位置（入队） */
    uint32_t  count;           /* 当前队列中的消息数量 */
    uint32_t  maxSize;         /* 队列最大容量 */
} mbox_impl_t;

err_t sys_mbox_new(sys_mbox_t* mbox, int size)
{
    if (!mbox || size <= 0) return ERR_ARG;

    /* 使用 XMalloc_System 分配内存，与 lwipopts.h 中 MEM_CUSTOM_MALLOC 保持一致 */
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
        /* 任何一个创建失败，清理所有已分配资源 */
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
        /* 队列未满，写入消息 */
        impl->msgBuf[impl->tail] = msg;
        impl->tail = (impl->tail + 1) % impl->maxSize;
        impl->count++;
        XMutex_unlock(impl->mutex);
        /* 释放信号量，通知等待者 */
        XSemaphore_release(impl->sem, 1);
    } else {
        XMutex_unlock(impl->mutex);
        /* 队列满，丢弃消息（与 FreeRTOS 接口一致：post 永远不阻塞） */
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
    /* Windows 上没有 ISR 概念，直接调用 trypost */
    return sys_mbox_trypost(mbox, msg);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t* mbox, void** msg, u32_t timeout_ms)
{
    if (!mbox || !mbox->mbx || !msg) return SYS_ARCH_TIMEOUT;
    mbox_impl_t* impl = (mbox_impl_t*)mbox->mbx;

    /* 等待信号量（阻塞或超时） */
    if (!timeout_ms) {
        XSemaphore_acquire(impl->sem, 1);
    } else {
        if (!XSemaphore_tryAcquireTimeout(impl->sem, 1, timeout_ms)) {
            *msg = NULL;
            return SYS_ARCH_TIMEOUT;
        }
    }

    /* 从队列取出消息 */
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

    /* 尝试获取信号量（非阻塞） */
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

    /* 清理所有资源 */
    XMutex_delete(impl->mutex);
    XSemaphore_delete(impl->sem);
    XFree_System(impl->msgBuf);   /* 使用 XFree_System 与 XMalloc_System 配对 */
    XFree_System(impl);
    mbox->mbx = NULL;
}

/* ================================================================
 * 七、线程
 *
 * 使用 XThread 创建线程，与 FreeRTOS xTaskCreate 一致。
 * 通过包装函数桥接 lwIP 线程函数签名和 XThread 函数签名。
 * ================================================================ */

/* 线程包装结构体 鈥?传递 lwIP 线程函数和参数 */
typedef struct {
    lwip_thread_fn func;    /* lwIP 线程函数指针 */
    void* arg;              /* 线程参数 */
} lwip_thread_wrap_t;

/**
 * @brief XThread 线程包装函数
 * @param xthread XThread 对象指针
 * @param vars 可变参数列表（包含 lwip_thread_wrap_t）
 * @note 执行完 lwIP 线程函数后自动释放包装结构体
 */
static void lwip_xthread_wrapper(XThread* xthread, XVarList* vars)
{
    (void)xthread;
    XVarList_args_1(vars, lwip_thread_wrap_t*, wrap);
    if (wrap && wrap->func) {
        wrap->func(wrap->arg);
    }
    /* 线程函数返回后释放包装结构体 */
    XFree_System(wrap);
}

sys_thread_t sys_thread_new(const char* name, lwip_thread_fn thread, void* arg, int stacksize, int prio)
{
    (void)name; (void)prio;
    sys_thread_t st;

    /* 创建线程包装结构体 */
    lwip_thread_wrap_t* wrap = (lwip_thread_wrap_t*)XMalloc_System(sizeof(lwip_thread_wrap_t));
    if (!wrap) {
        st.thread_handle = NULL;
        return st;
    }
    wrap->func = thread;
    wrap->arg = arg;

    /* 创建 XThread 并启动 */
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
 * 八、核心锁定
 *
 * 与 sys_arch_protect 共享同一个递归互斥锁 g_coreLock。
 * 当 LWIP_TCPIP_CORE_LOCKING=1 时，应用线程可以直接调用 Raw API，
 * 前提是持有核心锁。这避免了必须通过 tcpip_thread 的限制。
 * ================================================================ */

#if LWIP_TCPIP_CORE_LOCKING

/**
 * @brief 锁定 TCP/IP 核心
 * @note 与 sys_arch_protect 共享同一个递归互斥锁
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
