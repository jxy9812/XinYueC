#ifdef _WIN32
#include "XReadWriteLock.h"
#include "XMemory.h"
#include <windows.h>
#include <stdlib.h>

// 线程ID获取函数
static DWORD XReadWriteLock_currentThreadId() {
    return GetCurrentThreadId();
}

struct XReadWriteLock {
    XReadWriteLock_Type type;
    SRWLOCK srwlock;

    // 用于超时等待的事件对象
    HANDLE readEvent;
    HANDLE writeEvent;

    // 状态跟踪
    long readCount;
    long writeWaiting;
    long readWaiting;
    long isWriting;

    // 递归模式支持
    DWORD writeOwner;           // 写锁拥有者线程ID
    int writeRecursionCount;    // 写锁递归计数
    DWORD* readOwners;          // 读锁拥有者线程ID列表
    int* readRecursionCounts;   // 读锁递归计数列表
    int readOwnerCount;         // 读锁拥有者数量
    int readOwnerCapacity;      // 读锁拥有者列表容量
};

size_t XReadWriteLock_getTypeSize() {
    return sizeof(struct XReadWriteLock);
}

static int findReadOwnerIndex(struct XReadWriteLock* rwlock, DWORD threadId) {
    for (int i = 0; i < rwlock->readOwnerCount; i++) {
        if (rwlock->readOwners[i] == threadId) {
            return i;
        }
    }
    return -1;
}

static bool addReadOwner(struct XReadWriteLock* rwlock, DWORD threadId) {
    int index = findReadOwnerIndex(rwlock, threadId);
    if (index != -1) {
        rwlock->readRecursionCounts[index]++;
        return true;
    }

    // 需要扩容
    if (rwlock->readOwnerCount >= rwlock->readOwnerCapacity) {
        int newCapacity = rwlock->readOwnerCapacity + 4;
        DWORD* newOwners = (DWORD*)realloc(
            rwlock->readOwners, newCapacity * sizeof(DWORD));
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

static bool removeReadOwner(struct XReadWriteLock* rwlock, DWORD threadId) {
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
    InitializeSRWLock(&rwlock->srwlock);
    rwlock->readEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    rwlock->writeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    rwlock->readCount = 0;
    rwlock->writeWaiting = 0;
    rwlock->readWaiting = 0;
    rwlock->isWriting = 0;

    // 递归模式初始化
    rwlock->writeOwner = 0;
    rwlock->writeRecursionCount = 0;
    rwlock->readOwnerCount = 0;
    rwlock->readOwnerCapacity = 4;
    rwlock->readOwners = (DWORD*)malloc(rwlock->readOwnerCapacity * sizeof(DWORD));
    rwlock->readRecursionCounts = (int*)malloc(rwlock->readOwnerCapacity * sizeof(int));
}

void XReadWriteLock_deinit(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    CloseHandle(rwlock->readEvent);
    CloseHandle(rwlock->writeEvent);
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

    DWORD threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程持有写锁，直接增加读锁计数(写锁降级)
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
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
    while (true) {
        AcquireSRWLockShared(&rwlock->srwlock);
        if (rwlock->isWriting == 0 && rwlock->writeWaiting == 0) {
            InterlockedIncrement(&rwlock->readCount);
            ReleaseSRWLockShared(&rwlock->srwlock);

            // 记录读锁拥有者
            if (rwlock->type == XReadWriteLock_Recursive) {
                addReadOwner(rwlock, threadId);
            }
            return;
        }
        ReleaseSRWLockShared(&rwlock->srwlock);

        InterlockedIncrement(&rwlock->readWaiting);
        WaitForSingleObject(rwlock->readEvent, INFINITE);
        InterlockedDecrement(&rwlock->readWaiting);
    }
}

void XReadWriteLock_lockForWrite(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    DWORD threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程已持有写锁，增加递归计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            rwlock->writeRecursionCount++;
            return;
        }

        // 如果当前线程持有读锁，需要先释放所有读锁
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                XReadWriteLock_unlock(rwlock);
            }
        }
    }

    // 普通模式或需要真正获取锁
    while (true) {
        AcquireSRWLockExclusive(&rwlock->srwlock);
        if (rwlock->readCount == 0 && rwlock->isWriting == 0) {
            rwlock->isWriting = 1;
            ReleaseSRWLockExclusive(&rwlock->srwlock);

            // 记录写锁拥有者
            if (rwlock->type == XReadWriteLock_Recursive) {
                rwlock->writeOwner = threadId;
                rwlock->writeRecursionCount = 1;
            }
            return;
        }
        ReleaseSRWLockExclusive(&rwlock->srwlock);

        InterlockedIncrement(&rwlock->writeWaiting);
        WaitForSingleObject(rwlock->writeEvent, INFINITE);
        InterlockedDecrement(&rwlock->writeWaiting);
    }
}

bool XReadWriteLock_tryLockForRead(XReadWriteLock* rwlock) {
    if (!rwlock) return false;

    DWORD threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程持有写锁，直接增加读锁计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
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
    if (TryAcquireSRWLockShared(&rwlock->srwlock)) {
        bool success = (rwlock->isWriting == 0 && rwlock->writeWaiting == 0);
        if (success) {
            InterlockedIncrement(&rwlock->readCount);

            // 记录读锁拥有者
            if (rwlock->type == XReadWriteLock_Recursive) {
                addReadOwner(rwlock, threadId);
            }
        }
        ReleaseSRWLockShared(&rwlock->srwlock);
        return success;
    }

    return false;
}

bool XReadWriteLock_tryLockForWrite(XReadWriteLock* rwlock) {
    if (!rwlock) return false;

    DWORD threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程已持有写锁，增加递归计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            rwlock->writeRecursionCount++;
            return true;
        }

        // 如果当前线程持有读锁，需要先释放所有读锁
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                XReadWriteLock_unlock(rwlock);
            }
        }
    }

    // 尝试获取写锁
    if (TryAcquireSRWLockExclusive(&rwlock->srwlock)) {
        bool success = (rwlock->readCount == 0 && rwlock->isWriting == 0);
        if (success) {
            rwlock->isWriting = 1;

            // 记录写锁拥有者
            if (rwlock->type == XReadWriteLock_Recursive) {
                rwlock->writeOwner = threadId;
                rwlock->writeRecursionCount = 1;
            }
        }
        ReleaseSRWLockExclusive(&rwlock->srwlock);
        return success;
    }

    return false;
}

bool XReadWriteLock_tryLockForReadTimeout(XReadWriteLock* rwlock, int32_t timeout) {
    if (!rwlock) return false;

    // 永久等待
    if (timeout < 0) {
        XReadWriteLock_lockForRead(rwlock);
        return true;
    }

    DWORD threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程持有写锁，直接增加读锁计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
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

    DWORD start = GetTickCount();
    while (true) {
        if (TryAcquireSRWLockShared(&rwlock->srwlock)) {
            if (rwlock->isWriting == 0 && rwlock->writeWaiting == 0) {
                InterlockedIncrement(&rwlock->readCount);
                ReleaseSRWLockShared(&rwlock->srwlock);

                // 记录读锁拥有者
                if (rwlock->type == XReadWriteLock_Recursive) {
                    addReadOwner(rwlock, threadId);
                }
                return true;
            }
            ReleaseSRWLockShared(&rwlock->srwlock);
        }

        DWORD elapsed = GetTickCount() - start;
        DWORD remaining = (elapsed >= (DWORD)timeout) ? 0 : ((DWORD)timeout - elapsed);
        if (remaining == 0) {
            return false;
        }

        InterlockedIncrement(&rwlock->readWaiting);
        DWORD result = WaitForSingleObject(rwlock->readEvent, remaining);
        InterlockedDecrement(&rwlock->readWaiting);

        if (result != WAIT_OBJECT_0) {
            return false;
        }
    }
}

bool XReadWriteLock_tryLockForWriteTimeout(XReadWriteLock* rwlock, int32_t timeout) {
    if (!rwlock) return false;

    // 永久等待
    if (timeout < 0) {
        XReadWriteLock_lockForWrite(rwlock);
        return true;
    }

    DWORD threadId = XReadWriteLock_currentThreadId();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前线程已持有写锁，增加递归计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            rwlock->writeRecursionCount++;
            return true;
        }

        // 如果当前线程持有读锁，需要先释放所有读锁
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                XReadWriteLock_unlock(rwlock);
            }
        }
    }

    DWORD start = GetTickCount();
    while (true) {
        if (TryAcquireSRWLockExclusive(&rwlock->srwlock)) {
            if (rwlock->readCount == 0 && rwlock->isWriting == 0) {
                rwlock->isWriting = 1;
                ReleaseSRWLockExclusive(&rwlock->srwlock);

                // 记录写锁拥有者
                if (rwlock->type == XReadWriteLock_Recursive) {
                    rwlock->writeOwner = threadId;
                    rwlock->writeRecursionCount = 1;
                }
                return true;
            }
            ReleaseSRWLockExclusive(&rwlock->srwlock);
        }

        DWORD elapsed = GetTickCount() - start;
        DWORD remaining = (elapsed >= (DWORD)timeout) ? 0 : ((DWORD)timeout - elapsed);
        if (remaining == 0) {
            return false;
        }

        InterlockedIncrement(&rwlock->writeWaiting);
        DWORD result = WaitForSingleObject(rwlock->writeEvent, remaining);
        InterlockedDecrement(&rwlock->writeWaiting);

        if (result != WAIT_OBJECT_0) {
            return false;
        }
    }
}

void XReadWriteLock_unlock(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    DWORD threadId = XReadWriteLock_currentThreadId();

    if (rwlock->type == XReadWriteLock_Recursive) {
        // 检查是否持有写锁
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            if (--rwlock->writeRecursionCount == 0) {
                AcquireSRWLockExclusive(&rwlock->srwlock);
                rwlock->isWriting = 0;

                // 优先唤醒写者
                if (rwlock->writeWaiting > 0) {
                    SetEvent(rwlock->writeEvent);
                }
                else if (rwlock->readWaiting > 0) {
                    SetEvent(rwlock->readEvent);
                }

                rwlock->writeOwner = 0;
                ReleaseSRWLockExclusive(&rwlock->srwlock);
            }
            return;
        }

        // 检查是否持有读锁
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            bool fullyReleased = false;
            if (--rwlock->readRecursionCounts[index] == 0) {
                fullyReleased = true;
                removeReadOwner(rwlock, threadId);

                AcquireSRWLockExclusive(&rwlock->srwlock);
                if (--rwlock->readCount == 0 && rwlock->writeWaiting > 0) {
                    SetEvent(rwlock->writeEvent);
                }
                else if (rwlock->readWaiting > 0) {
                    SetEvent(rwlock->readEvent);
                }
                ReleaseSRWLockExclusive(&rwlock->srwlock);
            }
            return;
        }
    }

    // 非递归模式释放
    AcquireSRWLockExclusive(&rwlock->srwlock);
    if (rwlock->isWriting) {
        rwlock->isWriting = 0;
        // 优先唤醒写者
        if (rwlock->writeWaiting > 0) {
            SetEvent(rwlock->writeEvent);
        }
        else if (rwlock->readWaiting > 0) {
            SetEvent(rwlock->readEvent);
        }
    }
    else {
        if (--rwlock->readCount == 0 && rwlock->writeWaiting > 0) {
            SetEvent(rwlock->writeEvent);
        }
        else if (rwlock->readWaiting > 0) {
            SetEvent(rwlock->readEvent);
        }
    }
    ReleaseSRWLockExclusive(&rwlock->srwlock);
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

    return rwlock->writeRecursionCount > 0 && rwlock->writeOwner == XReadWriteLock_currentThreadId();
}

#endif
