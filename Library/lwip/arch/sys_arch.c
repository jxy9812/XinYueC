/**
 * @file sys_arch.c
 * @brief lwIP OS ??? ??XSync ??(?? STM32F407+FreeRTOS ??????)
 *
 * ???? lwIP ? OS ????(sys_arch_protect?????????
 * ?????????)??? XSync ??
 *
 * ??????:
 *   FreeRTOS xSemaphoreCreateRecursiveMutex ??XMutex (XLock_Recursive)
 *   FreeRTOS xSemaphoreCreateBinary        ??XSemaphore
 *   FreeRTOS xQueueCreate                  ??XVector + XMutex + XSemaphore(????)
 *   FreeRTOS xTaskCreate                   ??XThread
 *
 * ????:
 *   - ?????????? XMalloc_System / XFree_System
 *   - sys_arch_protect ? sys_lock_tcpip_core ??????????
 *   - ??????????? + ??? + ?????
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
 * ?????
 * ================================================================ */

/* ================================================================
 * ?????(sys_arch_protect ? sys_lock_tcpip_core ??)
 * ???????,? FreeRTOS xSemaphoreCreateRecursiveMutex ??
 * ?????????????,????
 * ================================================================ */
static XMutex* g_coreLock = NULL;

void sys_init(void)
{
    /* ? lwip_init() ???,?????,????? */
    if (!g_coreLock) {
        g_coreLock = XMutex_create(XLock_Recursive);
    }
}

/* ================================================================
 * ????
 * ================================================================ */

/**
 * @brief ????????(??)
 * @return ? Epoch ??????(?? 32 ?)
 * @note lwIP ???? 32 ??????,????? 49.7 ?,??????
 */
u32_t sys_now(void)
{
    return (u32_t)XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief ???? jiffies(?????)
 * @return ?????
 */
u32_t sys_jiffies(void)
{
    return (u32_t)XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief ????(??)
 * @param delay_ms ?????
 */
void sys_arch_msleep(u32_t delay_ms)
{
    XThread_msleep(delay_ms);
}

/* ================================================================
 * ???????(SYS_LIGHTWEIGHT_PROT)
 *
 * ? sys_lock_tcpip_core ??????????,???????
 * ????????:
 *   - Raw API ????????? sys_arch_protect
 *   - ????????????????????
 * ================================================================ */

#if SYS_LIGHTWEIGHT_PROT

/**
 * @brief ???????
 * @return ???(?? sys_arch_unprotect)
 * @note ???????,?????????
 */
sys_prot_t sys_arch_protect(void)
{
    XMutex_lock(g_coreLock);
    return 1;
}

/**
 * @brief ???????
 * @param pval sys_arch_protect ????
 */
void sys_arch_unprotect(sys_prot_t pval)
{
    (void)pval;
    XMutex_unlock(g_coreLock);
}

#endif /* SYS_LIGHTWEIGHT_PROT */

#if !NO_SYS
/* ??? OS ??(NO_SYS=0)????:???????????????????
 * ????(NO_SYS=1)? sys.h ??????????,????? */
/* ================================================================
 * ?????
 *
 * ???????,? FreeRTOS xSemaphoreCreateRecursiveMutex ???
 * ?????????????,?????
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
 * ?????
 *
 * ??????????????,? FreeRTOS xSemaphoreCreateBinary ???
 * ????????????
 * ================================================================ */

err_t sys_sem_new(sys_sem_t* sem, u8_t initial_count)
{
    if (!sem) return ERR_ARG;
    /* XSemaphore ????? 1 ????????;>1 ??????? */
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
        /* ????(??) */
        XSemaphore_acquire((XSemaphore*)sem->sem, 1);
    } else {
        /* ???? */
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
 * ????(????)
 *
 * XSync ?????????,??????? + ??? + ??????
 * ????:
 *   - ???????????(void*)
 *   - ?????????
 *   - ????????????
 *   - ? FreeRTOS xQueueCreate ????
 * ================================================================ */

/* ??????? ??????? + ??? + ??? */
typedef struct {
    XMutex*   mutex;          /* ?????????? */
    XSemaphore* sem;           /* ???:????????? */
    void**    msgBuf;          /* ????????? */
    uint32_t  head;            /* ????(??) */
    uint32_t  tail;            /* ????(??) */
    uint32_t  count;           /* ?????????? */
    uint32_t  maxSize;         /* ?????? */
} mbox_impl_t;

err_t sys_mbox_new(sys_mbox_t* mbox, int size)
{
    if (!mbox) return ERR_ARG;
    if (size <= 0) size = 128;  /* TCPIP_MBOX_SIZE=0 means use default size */

    /* ?? XMalloc_System ????,? lwipopts.h ? MEM_CUSTOM_MALLOC ???? */
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
        /* ????????,????????? */
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
        /* ????,???? */
        impl->msgBuf[impl->tail] = msg;
        impl->tail = (impl->tail + 1) % impl->maxSize;
        impl->count++;
        XMutex_unlock(impl->mutex);
        /* ?????,????? */
        XSemaphore_release(impl->sem, 1);
    } else {
        XMutex_unlock(impl->mutex);
        /* ???,????(? FreeRTOS ????:post ?????) */
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
    /* Windows ??? ISR ??,???? trypost */
    return sys_mbox_trypost(mbox, msg);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t* mbox, void** msg, u32_t timeout_ms)
{
    if (!mbox || !mbox->mbx || !msg) return SYS_ARCH_TIMEOUT;
    mbox_impl_t* impl = (mbox_impl_t*)mbox->mbx;

    /* ?????(?????) */
    if (!timeout_ms) {
        XSemaphore_acquire(impl->sem, 1);
    } else {
        if (!XSemaphore_tryAcquireTimeout(impl->sem, 1, timeout_ms)) {
            *msg = NULL;
            return SYS_ARCH_TIMEOUT;
        }
    }

    /* ??????? */
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

    /* ???????(???) */
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

    /* ?????? */
    XMutex_delete(impl->mutex);
    XSemaphore_delete(impl->sem);
    XFree_System(impl->msgBuf);   /* ?? XFree_System ? XMalloc_System ?? */
    XFree_System(impl);
    mbox->mbx = NULL;
}

/* ================================================================
 * ????
 *
 * ?? XThread ????,? FreeRTOS xTaskCreate ???
 * ???????? lwIP ??????? XThread ?????
 * ================================================================ */

/* ??????? ???? lwIP ??????? */
typedef struct {
    lwip_thread_fn func;    /* lwIP ?????? */
    void* arg;              /* ???? */
} lwip_thread_wrap_t;

/**
 * @brief XThread ??????
 * @param xthread XThread ????
 * @param vars ??????(?? lwip_thread_wrap_t)
 * @note ??? lwIP ??????????????
 */
static void lwip_xthread_wrapper(XThread* xthread, XVarList* vars)
{
    (void)xthread;
    XVarList_args_1(vars, lwip_thread_wrap_t*, wrap);
    if (wrap && wrap->func) {
        wrap->func(wrap->arg);
    }
    /* ?????????????? */
    XFree_System(wrap);
}

sys_thread_t sys_thread_new(const char* name, lwip_thread_fn thread, void* arg, int stacksize, int prio)
{
    (void)name; (void)prio;
    sys_thread_t st;

    /* ????????? */
    lwip_thread_wrap_t* wrap = (lwip_thread_wrap_t*)XMalloc_System(sizeof(lwip_thread_wrap_t));
    if (!wrap) {
        st.thread_handle = NULL;
        return st;
    }
    wrap->func = thread;
    wrap->arg = arg;

    /* ?? XThread ??? */
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
 * ??????
 *
 * ? sys_arch_protect ?????????? g_coreLock?
 * ? LWIP_TCPIP_CORE_LOCKING=1 ?,?????????? Raw API,
 * ????????????????? tcpip_thread ????
 * ================================================================ */

#if LWIP_TCPIP_CORE_LOCKING

/**
 * @brief ?? TCP/IP ??
 * @note ? sys_arch_protect ??????????
 */
void sys_lock_tcpip_core(void)
{
    XMutex_lock(g_coreLock);
}

/**
 * @brief ?? TCP/IP ??
 */
void sys_unlock_tcpip_core(void)
{
    XMutex_unlock(g_coreLock);
}

#endif /* LWIP_TCPIP_CORE_LOCKING */

#endif /* !NO_SYS */
