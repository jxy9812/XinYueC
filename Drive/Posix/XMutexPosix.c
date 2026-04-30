#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XMutex.h"
#include "XThread.h" // For XThread_currentThreadId()
#include "XTimer.h"  // For time-related functions if needed, though we'll use clock_gettime directly for precision
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>

// PlatformPrivate 结构体定义
// 对于Linux，我们直接使用 pthread_mutex_t
typedef struct PlatformPrivate
{
    pthread_mutex_t mutex;
} PlatformPrivate;

// 声明外部函数，这些函数在 XMutex.c 中被调用
PlatformPrivate* XMutex_getPlatformPrivate(XMutex* mutex);

pthread_mutex_t* XMutex_get_pthread_mutex_t(XMutex* mutex)
{
    if (!mutex || mutex->type & XMutex_Spin)return NULL;
    PlatformPrivate* p = XMutex_getPlatformPrivate(mutex);
    return p ? &p->mutex : NULL;
}

// ========== 平台抽象接口实现 ==========
size_t XMutex_PlatformPrivate_size()
{
    return sizeof(PlatformPrivate);
}

void XMutex_platform_init(PlatformPrivate* p)
{
    if (!p) return;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    // 初始化为非递归（快速）互斥锁
    // 因为递归逻辑已经在 XMutex.c 中处理了
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);

    pthread_mutex_init(&p->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

void XMutex_platform_deinit(PlatformPrivate* p)
{
    if (!p) return;
    pthread_mutex_destroy(&p->mutex);
}

void XMutex_platform_lock(PlatformPrivate* p)
{
    if (!p) return;
    pthread_mutex_lock(&p->mutex);
}

bool XMutex_platform_tryLock(PlatformPrivate* p)
{
    if (!p) return false;
    int result = pthread_mutex_trylock(&p->mutex);
    return (result == 0);
}

void XMutex_platform_unlock(PlatformPrivate* p)
{
    if (!p) return;
    pthread_mutex_unlock(&p->mutex);
}

// ========== 超时锁的专用实现 ==========
// 注意：XMutex.c 中的 XMutex_tryLockTimeout 是一个通用的、基于轮询的实现。
// 为了获得更好的性能和精确性，我们可以在这里提供一个基于 pthread_mutex_timedlock 的原生实现。
// 但是，为了保持与现有架构的一致性，我们让 XMutex.c 继续使用其通用的超时逻辑，
// 它内部会循环调用 XMutex_platform_tryLock。
// 如果您希望使用更高效的原生超时，可以取消注释下面的函数，并在 XMutex.c 中调用它。

/*
static bool _XMutex_platform_tryLockTimeout_private(PlatformPrivate* p, uint32_t timeout_ms)
{
    if (!p || timeout_ms == 0)
        return XMutex_platform_tryLock(p);

    struct timespec abs_timeout;
    clock_gettime(CLOCK_REALTIME, &abs_timeout);

    // 将毫秒超时转换为 timespec
    abs_timeout.tv_sec += timeout_ms / 1000;
    abs_timeout.tv_nsec += (timeout_ms % 1000) * 1000000; // ms to ns

    // 处理纳秒溢出
    if (abs_timeout.tv_nsec >= 1000000000L) {
        abs_timeout.tv_sec++;
        abs_timeout.tv_nsec -= 1000000000L;
    }

    int result = pthread_mutex_timedlock(&p->mutex, &abs_timeout);
    return (result == 0);
}
*/

#endif