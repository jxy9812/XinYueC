#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XReadWriteLock.h"
#include "XMemory.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

// 线程ID获取函数(兼容不同系统)
static pthread_t XReadWriteLock_currentThreadId() {
    return pthread_self();
}

struct XReadWriteLock {
    XReadWriteLock_Type type;
    pthread_rwlock_t rwlock;

    // 递归模式支持
    pthread_t writeOwner;       // 写锁拥有者线程ID
    int writeRecursionCount;    // 写锁递归计数
    pthread_t* readOwners;      // 读锁拥有者线程ID列表
    int* readRecursionCounts;   // 读锁递归计数列表
    int readOwnerCount;         // 读锁拥有者数量
    int readOwnerCapacity;      // 读锁拥有者列表容量
};

size_t XReadWriteLock_getTypeSize() {
    return sizeof(struct XReadWriteLock);
}

static int findReadOwnerIndex(struct XReadWriteLock* rwlock, pthread_t threadId) {
    for (int i = 0; i < rwlock->readOwnerCount; i++) {
        if (pthread_equal(rwlock->readOwners[i], threadId)) {
            return i;
        }
    }
    return -1;
}

static bool addReadOwner(struct XReadWriteLock* rwlock, pthread_t threadId) {
    int index = findReadOwnerIndex(rwlock, threadId);
    if (index != -1) {
        rwlock->readRecursionCounts[index]++;
        return true;
    }

    // 需要扩容
    if (rwlock->readOwnerCount >= rwlock->readOwnerCapacity) {
        int newCapacity = rwlock->readOwnerCapacity + 4;
        pthread_t* newOwners = (pthread_t*)realloc(
            rwlock->readOwners, newCapacity * sizeof(pthread_t));
        int* newCounts = (int*)realloc(
            rwlock->readRecursionCounts, newCapacity * sizeof(int));

        if (!newOwners || !newCounts) {
           XMemory_free(newOwners);
           XMemory_free(newCounts);
            return false;
        }

        rwlock->readOwners = newOwners;
        rwlock->readRecursionCounts = newCounts;
        rwlock->readOwnerCapacity = newCapacity;
    }

    // 添加新的读锁拥有者
    rwlock->readOwners[rwlock->readOwnerCount] = threadId;
    rwlock->readRecursionCounts[rwlock->readOwnerCount] = 1;
    rwlock->readOwnerCount++;
    return true;
}

static bool removeReadOwner(struct XReadWriteLock* rwlock, pthread_t threadId) {
    int index = findReadOwnerIndex(rwlock, threadId);
    if (index == -1) {
        return false;
    }

    if (--rwlock->readRecursionCounts[index] > 0) {
        return true;
    }

    // 递归计数为0，移除该拥有者
    rwlock->readOwnerCount--;
    if (index < rwlock->readOwnerCount) {
        rwlock->readOwners[index] = rwlock->readOwners[rwlock->readOwnerCount];
        rwlock->readRecursionCounts[index] = rwlock->readRecursionCounts[rwlock->readOwnerCount];
    }

    return true;
}

void XReadWriteLock_init(XReadWriteLock* rwlock, XReadWriteLock_Type type) {
    if (!rwlock) return;

    rwlock->type = type;
    rwlock->writeOwner = 0;
    rwlock->writeRecursionCount = 0;
    rwlock->readOwnerCount = 0;
    rwlock->readOwnerCapacity = 4;
    rwlock->readOwners = (pthread_t*)malloc(rwlock->readOwnerCapacity * sizeof(pthread_t));
    rwlock->readRecursionCounts = (int*)malloc(rwlock->readOwnerCapacity * sizeof(int));

    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);

    // POSIX读写锁默认支持非递归，递归特性需手动实现
    pthread_rwlock_init(&rwlock->rwlock, &attr);
    pthread_rwlockattr_destroy(&attr);
}

void XReadWriteLock_deinit(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    pthread_rwlock_destroy(&rwlock->rwlock);
   XMemory_free(rwlock->readOwners);
   XMemory_free(rwlock->readRecursionCounts);

    rwlock->readOwners = NULL;
    rwlock->readRecursionCounts = NULL;
}

XReadWriteLock* XReadWriteLock_create(XReadWriteLock_Type type) {
    XReadWriteLock* rwlock = (XReadWriteLock*)XMemory_malloc(sizeof(struct XReadWriteLock));
    if (rwlock) {
        XReadWriteLock_init(rwlock, type);
    }
    return rwlock;
}

void XReadWriteLock_delete(XReadWriteLock* rwlock) {
    if (rwlock) {
        XReadWriteLock_deinit(rwlock);
        XMemory_free(rwlock);
    }
}

void XReadWriteLock_lockForRead(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    pthread_t threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程持有写锁，直接增加读锁计数(写锁降级)
        if (rwlock->writeRecursionCount > 0 && pthread_equal(rwlock->writeOwner, threadId)) {
            addReadOwner(rwlock, threadId);
            return;
        }

        // 如果当前线程已持有读锁，增加递归计数
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            rwlock->readRecursionCounts[index]++;
            return;
        }
    }

    // 普通模式或需要真正获取锁
    pthread_rwlock_rdlock(&rwlock->rwlock);

    // 记录读锁拥有者
    if (rwlock->type == XReadWriteLock_Recursive) {
        addReadOwner(rwlock, threadId);
    }
}

void XReadWriteLock_lockForWrite(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    pthread_t threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程已持有写锁，增加递归计数
        if (rwlock->writeRecursionCount > 0 && pthread_equal(rwlock->writeOwner, threadId)) {
            rwlock->writeRecursionCount++;
            return;
        }

        // 如果当前线程持有读锁，需要先释放所有读锁
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                pthread_rwlock_unlock(&rwlock->rwlock);
            }
            removeReadOwner(rwlock, threadId);
        }
    }

    // 普通模式或需要真正获取锁
    pthread_rwlock_wrlock(&rwlock->rwlock);

    // 记录写锁拥有者
    if (rwlock->type == XReadWriteLock_Recursive) {
        rwlock->writeOwner = threadId;
        rwlock->writeRecursionCount = 1;
    }
}

bool XReadWriteLock_tryLockForRead(XReadWriteLock* rwlock) {
    if (!rwlock) return false;

    pthread_t threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程持有写锁，直接增加读锁计数
        if (rwlock->writeRecursionCount > 0 && pthread_equal(rwlock->writeOwner, threadId)) {
            addReadOwner(rwlock, threadId);
            return true;
        }

        // 如果当前线程已持有读锁，增加递归计数
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            rwlock->readRecursionCounts[index]++;
            return true;
        }
    }

    // 尝试获取读锁
    if (pthread_rwlock_tryrdlock(&rwlock->rwlock) != 0) {
        return false;
    }

    // 记录读锁拥有者
    if (rwlock->type == XReadWriteLock_Recursive) {
        addReadOwner(rwlock, threadId);
    }

    return true;
}

bool XReadWriteLock_tryLockForWrite(XReadWriteLock* rwlock) {
    if (!rwlock) return false;

    pthread_t threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程已持有写锁，增加递归计数
        if (rwlock->writeRecursionCount > 0 && pthread_equal(rwlock->writeOwner, threadId)) {
            rwlock->writeRecursionCount++;
            return true;
        }

        // 如果当前线程持有读锁，需要先释放所有读锁
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                pthread_rwlock_unlock(&rwlock->rwlock);
            }
            removeReadOwner(rwlock, threadId);
        }
    }

    // 尝试获取写锁
    if (pthread_rwlock_trywrlock(&rwlock->rwlock) != 0) {
        return false;
    }

    // 记录写锁拥有者
    if (rwlock->type == XReadWriteLock_Recursive) {
        rwlock->writeOwner = threadId;
        rwlock->writeRecursionCount = 1;
    }

    return true;
}

bool XReadWriteLock_tryLockForReadTimeout(XReadWriteLock* rwlock, int32_t timeout) {
    if (!rwlock) return false;

    // 永久等待
    if (timeout < 0) {
        XReadWriteLock_lockForRead(rwlock);
        return true;
    }

    pthread_t threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程持有写锁，直接增加读锁计数
        if (rwlock->writeRecursionCount > 0 && pthread_equal(rwlock->writeOwner, threadId)) {
            addReadOwner(rwlock, threadId);
            return true;
        }

        // 如果当前线程已持有读锁，增加递归计数
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            rwlock->readRecursionCounts[index]++;
            return true;
        }
    }

    // 计算超时时间
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += timeout / 1000;
    ts.tv_nsec += (timeout % 1000) * 1000000;

    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    // 尝试超时获取读锁
    if (pthread_rwlock_timedrdlock(&rwlock->rwlock, &ts) != 0) {
        return false;
    }

    // 记录读锁拥有者
    if (rwlock->type == XReadWriteLock_Recursive) {
        addReadOwner(rwlock, threadId);
    }

    return true;
}

bool XReadWriteLock_tryLockForWriteTimeout(XReadWriteLock* rwlock, int32_t timeout) {
    if (!rwlock) return false;

    // 永久等待
    if (timeout < 0) {
        XReadWriteLock_lockForWrite(rwlock);
        return true;
    }

    pthread_t threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程已持有写锁，增加递归计数
        if (rwlock->writeRecursionCount > 0 && pthread_equal(rwlock->writeOwner, threadId)) {
            rwlock->writeRecursionCount++;
            return true;
        }

        // 如果当前线程持有读锁，需要先释放所有读锁
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                pthread_rwlock_unlock(&rwlock->rwlock);
            }
            removeReadOwner(rwlock, threadId);
        }
    }

    // 计算超时时间
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += timeout / 1000;
    ts.tv_nsec += (timeout % 1000) * 1000000;

    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    // 尝试超时获取写锁
    if (pthread_rwlock_timedwrlock(&rwlock->rwlock, &ts) != 0) {
        return false;
    }

    // 记录写锁拥有者
    if (rwlock->type == XReadWriteLock_Recursive) {
        rwlock->writeOwner = threadId;
        rwlock->writeRecursionCount = 1;
    }

    return true;
}

void XReadWriteLock_unlock(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    pthread_t threadId = XReadWriteLock_currentThreadId();

    if (rwlock->type == XReadWriteLock_Recursive) {
        // 检查是否持有写锁
        if (rwlock->writeRecursionCount > 0 && pthread_equal(rwlock->writeOwner, threadId)) {
            if (--rwlock->writeRecursionCount == 0) {
                pthread_rwlock_unlock(&rwlock->rwlock);
                rwlock->writeOwner = 0;
            }
            return;
        }

        // 检查是否持有读锁
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            if (removeReadOwner(rwlock, threadId)) {
                // 只有当递归计数真正为0时才释放底层锁
                if (rwlock->readRecursionCounts[index] == 0) {
                    pthread_rwlock_unlock(&rwlock->rwlock);
                }
            }
            return;
        }
    }

    // 非递归模式直接释放
    pthread_rwlock_unlock(&rwlock->rwlock);
}

XReadWriteLock_Type XReadWriteLock_type(XReadWriteLock* rwlock) {
    return rwlock ? rwlock->type : XReadWriteLock_NonRecursive;
}

bool XReadWriteLock_hasReadLock(XReadWriteLock* rwlock) {
    if (!rwlock || rwlock->type != XReadWriteLock_Recursive) {
        return false;
    }

    return findReadOwnerIndex(rwlock, XReadWriteLock_currentThreadId()) != -1;
}

bool XReadWriteLock_hasWriteLock(XReadWriteLock* rwlock) {
    if (!rwlock || rwlock->type != XReadWriteLock_Recursive) {
        return false;
    }

    pthread_t threadId = XReadWriteLock_currentThreadId();
    return rwlock->writeRecursionCount > 0 && pthread_equal(rwlock->writeOwner, threadId);
}

#endif
