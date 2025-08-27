#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XMutex.h"
#include "XMemory.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>

// POSIX平台具体结构体定义
struct XMutex {
    pthread_mutex_t mutex;
    XMutex_Type type;
};
size_t XMutex_geTypetSize()
{
    return sizeof(struct XMutex);
}
// 内部辅助函数：获取平台相关句柄（供XWaitCondition使用）
void* XMutex_getNativeHandle(XMutex* mutex) {
    return mutex ? &mutex->mutex : NULL;
}

void XMutex_init(XMutex* mutex, XMutex_Type type) {
    if (!mutex) return;

    mutex->type = type;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    // 设置互斥锁类型
    if (type == XMutex_Recursive) {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    }
    else {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    }

    pthread_mutex_init(&mutex->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

void XMutex_deinit(XMutex* mutex) {
    if (!mutex) return;
    pthread_mutex_destroy(&mutex->mutex);
}

XMutex* XMutex_create(XMutex_Type type) {
    XMutex* mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (mutex) {
        XMutex_init(mutex, type);
    }
    return mutex;
}

void XMutex_delete(XMutex* mutex) {
    if (mutex) {
        XMutex_deinit(mutex);
        XMemory_free(mutex);
    }
}

void XMutex_lock(XMutex* mutex) {
    if (!mutex) return;
    pthread_mutex_lock(&mutex->mutex);
}

bool XMutex_tryLock(XMutex* mutex) {
    if (!mutex) return false;
    return pthread_mutex_trylock(&mutex->mutex) == 0;
}

bool XMutex_tryLockTimeout(XMutex* mutex, uint32_t timeout) {
    if (!mutex) return false;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += timeout / 1000;
    ts.tv_nsec += (timeout % 1000) * 1000000;

    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    return pthread_mutex_timedlock(&mutex->mutex, &ts) == 0;
}

void XMutex_unlock(XMutex* mutex) {
    if (!mutex) return;
    pthread_mutex_unlock(&mutex->mutex);
}

bool XMutex_isRecursive(XMutex* mutex) {
    return mutex ? (mutex->type == XMutex_Recursive) : false;
}

#endif