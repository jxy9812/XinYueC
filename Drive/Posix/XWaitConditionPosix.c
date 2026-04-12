#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XWaitCondition.h"
#include "XMemory.h"
#include <pthread.h>
#include <errno.h>
#include <time.h>

// POSIX平台具体结构体定义
struct XWaitCondition {
    pthread_cond_t cond;
};

size_t XWaitCondition_typeSize()
{
    return sizeof(struct XWaitCondition);
}

void XWaitCondition_init(XWaitCondition* cond) {
    if (cond == NULL) return;
    pthread_cond_init(&cond->cond, NULL);
}

void XWaitCondition_deinit(XWaitCondition* cond) {
    if (cond == NULL) return;
    pthread_cond_destroy(&cond->cond);
}

XWaitCondition* XWaitCondition_create() {
    XWaitCondition* cond = (XWaitCondition*)XMemory_malloc(sizeof(XWaitCondition));
    if (cond != NULL) {
        XWaitCondition_init(cond);
    }
    return cond;
}

void XWaitCondition_delete(XWaitCondition* cond) {
    if (cond == NULL) return;
    XWaitCondition_deinit(cond);
    XMemory_free(cond);
}

bool XWaitCondition_wait(XWaitCondition* cond, XMutex* m_mutex, int32_t timeout) {
    if (cond == NULL || m_mutex == NULL) return false;

    // 获取POSIX互斥锁句柄
    pthread_mutex_t* pthread_mutex = (pthread_mutex_t*)m_mutex;
    if (!pthread_mutex) return false;

    if (timeout == -1) {
        // 无限等待：原子释放锁并等待
        return pthread_cond_wait(&cond->cond, pthread_mutex) == 0;
    }
    else {
        // 超时等待：转换毫秒为timespec（绝对时间）
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += timeout / 1000;
        ts.tv_nsec += (timeout % 1000) * 1000000;  // 毫秒转纳秒

        // 处理纳秒溢出（超过1秒）
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }

        // 原子释放锁并等待超时
        return pthread_cond_timedwait(&cond->cond, pthread_mutex, &ts) == 0;
    }
}

void XWaitCondition_wakeOne(XWaitCondition* cond) {
    if (cond == NULL) return;
    pthread_cond_signal(&cond->cond);
}

void XWaitCondition_wakeAll(XWaitCondition* cond) {
    if (cond == NULL) return;
    pthread_cond_broadcast(&cond->cond);
}

#endif