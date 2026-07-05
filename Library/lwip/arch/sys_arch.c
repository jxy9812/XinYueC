/**
 * @file sys_arch.c
 * @brief lwIP OS 抽象层 —— XSync 实现（始终编译，网络后端无关）
 */

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"
#include "lwip/tcpip.h"

#if !NO_SYS

/* XinYueC XSync */
#include "XMutex.h"
#include "XSemaphore.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XVarList.h"
#include <string.h>

/* ================================================================ */
/*  mutex                                                             */
/* ================================================================ */

err_t sys_mutex_new(sys_mutex_t *mutex)
{
    if (!mutex) return ERR_ARG;
    mutex->mut = XMutex_create(XLock_NonRecursive);
    return mutex->mut ? ERR_OK : ERR_MEM;
}
void sys_mutex_lock(sys_mutex_t *mutex)
{
    if (mutex && mutex->mut) XMutex_lock((XMutex*)mutex->mut);
}
void sys_mutex_unlock(sys_mutex_t *mutex)
{
    if (mutex && mutex->mut) XMutex_unlock((XMutex*)mutex->mut);
}
void sys_mutex_free(sys_mutex_t *mutex)
{
    if (mutex && mutex->mut) { XMutex_delete((XMutex*)mutex->mut); mutex->mut = NULL; }
}

/* ================================================================ */
/*  sem                                                               */
/* ================================================================ */

err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
    if (!sem) return ERR_ARG;
    sem->sem = XSemaphore_create(count, 0x7FFFFFFF);
    return sem->sem ? ERR_OK : ERR_MEM;
}
void sys_sem_signal(sys_sem_t *sem)
{
    if (sem && sem->sem) XSemaphore_release((XSemaphore*)sem->sem, 1);
}
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
    if (!sem || !sem->sem) return SYS_ARCH_TIMEOUT;
    if (timeout_ms == 0)
        return XSemaphore_tryAcquire((XSemaphore*)sem->sem, 1) ? 0 : SYS_ARCH_TIMEOUT;
    return XSemaphore_tryAcquireTimeout((XSemaphore*)sem->sem, 1, timeout_ms) ? 0 : SYS_ARCH_TIMEOUT;
}
void sys_sem_free(sys_sem_t *sem)
{
    if (sem && sem->sem) { XSemaphore_delete((XSemaphore*)sem->sem); sem->sem = NULL; }
}

/* ================================================================ */
/*  mbox（环形队列 + XMutex + XSemaphore）                              */
/* ================================================================ */

typedef struct {
    void       **queue;
    int          size, head, tail, count;
    XMutex      *lock;
    XSemaphore  *sem;
} mbox_t;

err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    if (!mbox || size <= 0) return ERR_ARG;
    mbox_t *m = (mbox_t*)XMalloc_Hybrid(sizeof(*m));
    if (!m) return ERR_MEM;
    memset(m, 0, sizeof(*m));
    m->size  = size;
    m->queue = (void**)XMalloc_Hybrid(sizeof(void*) * (size_t)size);
    if (!m->queue) { XFree_Hybrid(m); return ERR_MEM; }
    memset(m->queue, 0, sizeof(void*) * (size_t)size);
    m->lock = XMutex_create(XLock_NonRecursive);
    m->sem  = XSemaphore_create(0, size);
    if (!m->lock || !m->sem) { if (m->queue) XFree_Hybrid(m->queue); if (m->lock) XMutex_delete(m->lock); if (m->sem) XSemaphore_delete(m->sem); XFree_Hybrid(m); return ERR_MEM; }
    mbox->mbx = m;
    return ERR_OK;
}
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    if (!mbox || !mbox->mbx) return ERR_ABRT;
    mbox_t *m = (mbox_t*)mbox->mbx;
    XMutex_lock(m->lock);
    if (m->count >= m->size) { XMutex_unlock(m->lock); return ERR_MEM; }
    m->queue[m->head] = msg;
    m->head = (m->head + 1) % m->size;
    m->count++;
    XMutex_unlock(m->lock);
    XSemaphore_release(m->sem, 1);
    return ERR_OK;
}
err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
    return sys_mbox_trypost(mbox, msg);
}
void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    if (!mbox || !mbox->mbx) return;
    mbox_t *m = (mbox_t*)mbox->mbx;
    XMutex_lock(m->lock);
    if (m->count < m->size) {
        m->queue[m->head] = msg;
        m->head = (m->head + 1) % m->size;
        m->count++;
        XSemaphore_release(m->sem, 1);
    }
    XMutex_unlock(m->lock);
}
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
    if (!mbox || !mbox->mbx) return SYS_ARCH_TIMEOUT;
    mbox_t *m = (mbox_t*)mbox->mbx;
    if (timeout_ms == 0) {
        XSemaphore_acquire(m->sem, 1);
    } else {
        if (!XSemaphore_tryAcquireTimeout(m->sem, 1, timeout_ms)) return SYS_ARCH_TIMEOUT;
    }
    XMutex_lock(m->lock);
    void *d = m->queue[m->tail];
    m->tail = (m->tail + 1) % m->size;
    m->count--;
    XMutex_unlock(m->lock);
    if (msg) *msg = d;
    return 0;
}
void sys_mbox_free(sys_mbox_t *mbox)
{
    if (!mbox || !mbox->mbx) return;
    mbox_t *m = (mbox_t*)mbox->mbx;
    if (m->queue) XFree_Hybrid(m->queue);
    if (m->lock)  XMutex_delete(m->lock);
    if (m->sem)   XSemaphore_delete(m->sem);
    XFree_Hybrid(m);
    mbox->mbx = NULL;
}

/* ================================================================ */
/*  thread  ── XThreadFunc 包装 lwip_thread_fn                        */
/* ================================================================ */

typedef struct {
    lwip_thread_fn  fn;
    void           *arg;
} lwip_thread_wrap_t;

static void lwip_thread_wrapper(XThread *xthread, XVarList *vars)
{
    (void)xthread;
    XVarList_args_1(vars, lwip_thread_wrap_t*, w);
    w->fn(w->arg);
    XFree_System(w);
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
                            int stacksize, int prio)
{
    (void)name; (void)prio;
    sys_thread_t ret;
    lwip_thread_wrap_t *w = (lwip_thread_wrap_t*)XMalloc_System(sizeof(*w));
    if (!w) { ret.thread_handle = NULL; return ret; }
    w->fn  = thread;
    w->arg = arg;
    XVarList *vl = XVarList_Create(XVar(lwip_thread_wrap_t*, w));
    if (!vl) { XFree_System(w); ret.thread_handle = NULL; return ret; }
    XThread *t = XThread_create_func(lwip_thread_wrapper, vl);
    if (t) {
        XThread_setStackSize(t, (uint32_t)(stacksize > 0 ? (uint32_t)stacksize : 4096));
        XThread_start(t);
    }
    ret.thread_handle = t;
    return ret;
}

/* ================================================================ */
/*  time                                                              */
/* ================================================================ */

u32_t sys_now(void)
{
    return (u32_t)XDateTime_currentMSecsSinceEpoch();
}

void sys_init(void) {}

u32_t LWIP_RAND(void) { return (u32_t)(XDateTime_currentMSecsSinceEpoch() ^ 0xDEADBEEF); }

#if SYS_LIGHTWEIGHT_PROT
static XMutex *g_protect = NULL;
sys_prot_t sys_arch_protect(void)
{
    if (!g_protect) g_protect = XMutex_create(XLock_Recursive);
    XMutex_lock(g_protect);
    return 0;
}
void sys_arch_unprotect(sys_prot_t v)
{
    (void)v;
    if (g_protect) XMutex_unlock(g_protect);
}
#endif

/* ================================================================ */
/*  core locking                                                      */
/* ================================================================ */

#if LWIP_TCPIP_CORE_LOCKING
static XMutex *g_coreLock = NULL;
void sys_lock_tcpip_core(void)
{
    if (!g_coreLock) g_coreLock = XMutex_create(XLock_Recursive);
    XMutex_lock(g_coreLock);
}
void sys_unlock_tcpip_core(void)
{
    if (g_coreLock) XMutex_unlock(g_coreLock);
}
#endif

#endif /* !NO_SYS */