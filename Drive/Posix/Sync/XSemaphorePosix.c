#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XSemaphore.h"
#include "XMemory.h"
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#if XSYNC_ON
#if XSEMAPHORE_ON

// POSIX平台具体结构体定义（参考XWaitConditionPosix.c）
struct XSemaphore {
    sem_t handle; // 平台原生信号量句柄
};

// 实现类型大小获取接口
size_t XSemaphore_typeSize() {
    return sizeof(struct XSemaphore);
}

void XSemaphore_init(XSemaphore* sem, int32_t initial, int32_t maximum) {
    if (!sem || initial < 0 || maximum <= 0 || initial > maximum) return;
    sem_init(&sem->handle, 0, initial); // 线程间共享
    (void)maximum; // POSIX信号量不直接支持最大计数，由应用层保证
}

void XSemaphore_deinit(XSemaphore* sem) {
    if (sem) {
        sem_destroy(&sem->handle);
    }
}

XSemaphore* XSemaphore_create(int32_t initial, int32_t maximum) {
    XSemaphore* sem = (XSemaphore*)XMalloc_System(sizeof(struct XSemaphore));
    if (sem) {
        XSemaphore_init(sem, initial, maximum);
    }
    return sem;
}

void XSemaphore_delete(XSemaphore* sem) {
    if (sem) {
        XSemaphore_deinit(sem);
        XFree_System(sem);
    }
}

void XSemaphore_acquire(XSemaphore* sem, int32_t n) {
    if (!sem || n <= 0) return;
    for (int32_t i = 0; i < n; i++) {
        sem_wait(&sem->handle);
    }
}

void XSemaphore_release(XSemaphore* sem, int32_t n) {
    if (!sem || n <= 0) return;
    for (int32_t i = 0; i < n; i++) {
        sem_post(&sem->handle);
    }
}

bool XSemaphore_tryAcquire(XSemaphore* sem, int32_t n) {
    if (!sem || n <= 0) return false;
    for (int32_t i = 0; i < n; i++) {
        if (sem_trywait(&sem->handle) != 0) {
            // 部分获取失败时回滚
            if (i > 0) {
                for (int32_t j = 0; j < i; j++) {
                    sem_post(&sem->handle);
                }
            }
            return false;
        }
    }
    return true;
}

bool XSemaphore_tryAcquireTimeout(XSemaphore* sem, int32_t n, uint32_t timeout) {
    if (!sem || n <= 0) return false;

    struct timespec ts;
    /* sem_timedwait() uses CLOCK_REALTIME for its absolute deadline. */
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout / 1000;
    ts.tv_nsec += (timeout % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    for (int32_t i = 0; i < n; i++) {
        int result;
        do {
            result = sem_timedwait(&sem->handle, &ts);
        } while (result != 0 && errno == EINTR);
        if (result != 0) {
            // 部分获取失败时回滚
            if (i > 0) {
                for (int32_t j = 0; j < i; j++) {
                    sem_post(&sem->handle);
                }
            }
            return false;
        }
    }
    return true;
}

int32_t XSemaphore_available(XSemaphore* sem) {
    if (!sem) return -1;
    int value;
    sem_getvalue(&sem->handle, &value);
    return value;
}

#endif /* XSEMAPHORE_ON */
#endif /* XSYNC_ON */
#endif
