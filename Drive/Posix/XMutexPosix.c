#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XRecursiveMutex.h"
#include "XMemory.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>

// POSIX平台具体结构体定义
struct XMutex {
    pthread_mutex_t m_mutex;
    XMutex_Type type;
};
size_t XMutex_typetSize()
{
    return sizeof(struct XMutex);
}
//// 内部辅助函数：获取平台相关句柄（供XWaitCondition使用）
//void* XMutex_getNativeHandle(XMutex* mutex) {
//    return mutex ? &mutex->mutex : NULL;
//}
void XRecursiveMutex_init(XRecursiveMutex* m_mutex)
{
    if (!m_mutex) return;

    m_mutex->type = XMutex_Recursive;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    // 设置互斥锁类型
    if (m_mutex->type == XMutex_Recursive) {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    }
    else {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    }

    pthread_mutex_init(&m_mutex->m_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}
XRecursiveMutex* XRecursiveMutex_create()
{
    XMutex* m_mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (m_mutex)
    {
        XRecursiveMutex_init(m_mutex);
    }
    return m_mutex;
}
void XMutex_init(XMutex* m_mutex) {
    if (!m_mutex) return;

    m_mutex->type = XMutex_Normal;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    // 设置互斥锁类型
    if (m_mutex->type == XMutex_Recursive) {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    }
    else {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    }

    pthread_mutex_init(&m_mutex->m_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

void XMutex_deinit(XMutex* m_mutex) {
    if (!m_mutex) return;
    pthread_mutex_destroy(&m_mutex->m_mutex);
}

XMutex* XMutex_create() {
    XMutex* m_mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (m_mutex) {
        XMutex_init(m_mutex);
    }
    return m_mutex;
}

void XMutex_delete(XMutex* m_mutex) {
    if (m_mutex) {
        XMutex_deinit(m_mutex);
        XMemory_free(m_mutex);
    }
}

void XMutex_lock(XMutex* m_mutex) {
    if (!m_mutex) return;
    pthread_mutex_lock(&m_mutex->m_mutex);
}

bool XMutex_tryLock(XMutex* m_mutex) {
    if (!m_mutex) return false;
    return pthread_mutex_trylock(&m_mutex->m_mutex) == 0;
}

bool XMutex_tryLockTimeout(XMutex* m_mutex, uint32_t timeout) {
    if (!m_mutex) return false;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += timeout / 1000;
    ts.tv_nsec += (timeout % 1000) * 1000000;

    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    return pthread_mutex_timedlock(&m_mutex->m_mutex, &ts) == 0;
}

void XMutex_unlock(XMutex* m_mutex) {
    if (!m_mutex) return;
    pthread_mutex_unlock(&m_mutex->m_mutex);
}

bool XMutex_isRecursive(XMutex* m_mutex) {
    return m_mutex ? (m_mutex->type == XMutex_Recursive) : false;
}
XMutex_Type XMutex_type(XMutex* m_mutex)
{
    return m_mutex->type;
}
#endif