#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XMutex.h"
#include "XMemory.h"
#include <pthread.h>
#include <time.h>
#include <stdio.h>
typedef pthread_mutex_t XMutexHandle;    // POSIX 用 pthread 互斥锁

// 虚函数实现
static void VXMutex_deinit(XMutex* mutex);
static bool VXMutex_lock(XMutex* mutex);
static bool VXMutex_lock_wait(XMutex* mutex, size_t timeout);
static bool VXMutex_unlock(XMutex* mutex);

// 虚函数表初始化
XVtable* XMutex_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XMutex))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());

    void* table[] = {
        VXMutex_lock,
        VXMutex_lock_wait,
        VXMutex_unlock
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMutex_deinit);

#if SHOWCONTAINERSIZE
    printf("XMutexPosix size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// 创建堆对象
XMutex* XMutex_create(const char* name) {
    (void)name;  // 忽略名称（POSIX 互斥锁无需命名）
    XMutex* mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (mutex) {
        XMutex_init(mutex, name);
    }
    return mutex;
}

// 初始化栈对象
void XMutex_init(XMutex* mutex, const char* name) {
    (void)name;
    if (!mutex) return;
    XClass_init(&mutex->m_parent);
    XClassGetVtable(mutex) = XMutex_class_init();

    // 分配并初始化 pthread 互斥锁
    mutex->m_mutex = (XMutexHandle*)XMemory_malloc(sizeof(XMutexHandle));
    if (mutex->m_mutex) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);  // 支持递归锁
        pthread_mutex_init(mutex->m_mutex, &attr);
        pthread_mutexattr_destroy(&attr);
    }
}

// 销毁对象
static void VXMutex_deinit(XMutex* mutex) {
    if (!mutex || !mutex->m_mutex) return;
    pthread_mutex_destroy(mutex->m_mutex);  // 销毁互斥锁
    XMemory_free(mutex->m_mutex);
    mutex->m_mutex = NULL;
    // 调用父类析构
    XVtableGetFunc(XClass_class_init(), EXClass_Deinit, void(*)(XClass*))((XClass*)mutex);
}

// 无限等待上锁
static bool VXMutex_lock(XMutex* mutex) {
    if (!mutex || !mutex->m_mutex) return false;
    return pthread_mutex_lock(mutex->m_mutex) == 0;
}

// 超时等待上锁
static bool VXMutex_lock_wait(XMutex* mutex, size_t timeout) {
    if (!mutex || !mutex->m_mutex) return false;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // 用单调时钟（不受系统时间调整影响）
    ts.tv_sec += timeout / 1000;
    ts.tv_nsec += (timeout % 1000) * 1000000;  // 毫秒转纳秒
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    return pthread_mutex_timedlock(mutex->m_mutex, &ts) == 0;
}

// 解锁
static bool VXMutex_unlock(XMutex* mutex) {
    if (!mutex || !mutex->m_mutex) return false;
    return pthread_mutex_unlock(mutex->m_mutex) == 0;
}

#endif