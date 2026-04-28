#ifdef _WIN32
#include "XReadWriteLock.h"
#include "XMemory.h"
#include <windows.h>
#include <stdlib.h>

static DWORD XReadWriteLock_currentThreadId() {
    return GetCurrentThreadId();
}

typedef struct XReadWriteLockWin32 {
    XReadWriteLock_Type type;
    SRWLOCK srwlock;

    // 使用 CONDITION_VARIABLE
    CONDITION_VARIABLE readCond;
    CONDITION_VARIABLE writeCond;

    // 核心状态，由 srwlock 保护
    long readCount;
    long isWriting;

    // 递归模式支持
    DWORD writeOwner;
    int writeRecursionCount;
    DWORD* readOwners;
    int* readRecursionCounts;
    int readOwnerCount;
    int readOwnerCapacity;
} XReadWriteLockWin32;

size_t XReadWriteLock_platform_getTypeSize()
{
    return sizeof(struct XReadWriteLockWin32);
}

// --- 辅助函数 ---
static int findReadOwnerIndex(struct XReadWriteLockWin32* rwlock, DWORD threadId) {
    for (int i = 0; i < rwlock->readOwnerCount; i++) {
        if (rwlock->readOwners[i] == threadId) {
            return i;
        }
    }
    return -1;
}

static bool addReadOwner(struct XReadWriteLockWin32* rwlock, DWORD threadId) {
    int index = findReadOwnerIndex(rwlock, threadId);
    if (index != -1) {
        rwlock->readRecursionCounts[index]++;
        return true;
    }

    if (rwlock->readOwnerCount >= rwlock->readOwnerCapacity) {
        int newCapacity = rwlock->readOwnerCapacity + 4;
        DWORD* newOwners = (DWORD*)XMalloc(newCapacity * sizeof(DWORD));
        int* newCounts = (int*)XMalloc(newCapacity * sizeof(int));

        if (!newOwners || !newCounts) {
            XMemory_free(newOwners);
            XMemory_free(newCounts);
            return false;
        }

        memcpy(newOwners, rwlock->readOwners, rwlock->readOwnerCount * sizeof(DWORD));
        memcpy(newCounts, rwlock->readRecursionCounts, rwlock->readOwnerCount * sizeof(int));

        XMemory_free(rwlock->readOwners);
        XMemory_free(rwlock->readRecursionCounts);

        rwlock->readOwners = newOwners;
        rwlock->readRecursionCounts = newCounts;
        rwlock->readOwnerCapacity = newCapacity;
    }

    rwlock->readOwners[rwlock->readOwnerCount] = threadId;
    rwlock->readRecursionCounts[rwlock->readOwnerCount] = 1;
    rwlock->readOwnerCount++;
    return true;
}

static bool removeReadOwner(struct XReadWriteLockWin32* rwlock, DWORD threadId) {
    int index = findReadOwnerIndex(rwlock, threadId);
    if (index == -1) {
        return false;
    }

    if (--rwlock->readRecursionCounts[index] > 0) {
        return true;
    }

    rwlock->readOwnerCount--;
    if (index < rwlock->readOwnerCount) {
        rwlock->readOwners[index] = rwlock->readOwners[rwlock->readOwnerCount];
        rwlock->readRecursionCounts[index] = rwlock->readRecursionCounts[rwlock->readOwnerCount];
    }

    return true;
}
// --- 辅助函数结束 ---

void XReadWriteLock_platform_init(XReadWriteLock* lock, XReadWriteLock_Type type)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return;

    rwlock->type = type;
    InitializeSRWLock(&rwlock->srwlock);
    InitializeConditionVariable(&rwlock->readCond);
    InitializeConditionVariable(&rwlock->writeCond);

    rwlock->readCount = 0;
    rwlock->isWriting = 0;

    rwlock->writeOwner = 0;
    rwlock->writeRecursionCount = 0;
    rwlock->readOwnerCount = 0;
    rwlock->readOwnerCapacity = 4;
    rwlock->readOwners = (DWORD*)XMalloc(rwlock->readOwnerCapacity * sizeof(DWORD));
    rwlock->readRecursionCounts = (int*)XMalloc(rwlock->readOwnerCapacity * sizeof(int));
}

void XReadWriteLock_platform_deinit(XReadWriteLock* lock)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return;

    XMemory_free(rwlock->readOwners);
    XMemory_free(rwlock->readRecursionCounts);

    rwlock->readOwners = NULL;
    rwlock->readRecursionCounts = NULL;
}

void XReadWriteLock_platform_delete(XReadWriteLock* lock)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return;
    XReadWriteLock_deinit(rwlock);
    XMemory_free(rwlock);
}

// ========== 核心: lockForRead ==========
void XReadWriteLock_platform_lockForRead(XReadWriteLock* lock)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return;

    DWORD threadId = XReadWriteLock_currentThreadId();

    // --- 递归模式快速路径 ---
    if (rwlock->type & XReadWriteLock_Recursive) {
        AcquireSRWLockExclusive(&rwlock->srwlock);
        // 情况1: 当前线程已持有写锁，可以直接获取读锁
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            addReadOwner(rwlock, threadId);
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return;
        }
        // 情况2: 当前线程已持有读锁，递归计数+1
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            rwlock->readRecursionCounts[index]++;
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return;
        }
        ReleaseSRWLockExclusive(&rwlock->srwlock);
    }

    // --- 主等待循环 ---
    while (true) {
        AcquireSRWLockExclusive(&rwlock->srwlock);

        // 允许读者进入的条件：没有写者正在写
        if (rwlock->isWriting == 0) {
            rwlock->readCount++;
            if (rwlock->type & XReadWriteLock_Recursive) {
                addReadOwner(rwlock, threadId);
            }
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return;
        }

        // 不能进入，直接等待
        SleepConditionVariableSRW(&rwlock->readCond, &rwlock->srwlock, INFINITE, 0);
    }
}

// ========== 核心: lockForWrite ==========
void XReadWriteLock_platform_lockForWrite(XReadWriteLock* lock)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return;

    DWORD threadId = XReadWriteLock_currentThreadId();

    // --- 递归模式快速路径 ---
    if (rwlock->type & XReadWriteLock_Recursive) {
        AcquireSRWLockExclusive(&rwlock->srwlock);
        // 情况1: 当前线程已持有写锁，递归计数+1
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            rwlock->writeRecursionCount++;
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return;
        }
        // 情况2: 当前线程持有读锁，需要先转换
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                removeReadOwner(rwlock, threadId);
            }
            rwlock->readCount -= count;
            // 转换后，继续走主逻辑
        }
        ReleaseSRWLockExclusive(&rwlock->srwlock);
    }

    // --- 主等待循环 ---
    while (true) {
        AcquireSRWLockExclusive(&rwlock->srwlock);

        // 允许写者进入的条件：没有读者，也没有写者
        if (rwlock->readCount == 0 && rwlock->isWriting == 0) {
            rwlock->isWriting = 1;
            if (rwlock->type & XReadWriteLock_Recursive) {
                rwlock->writeOwner = threadId;
                rwlock->writeRecursionCount = 1;
            }
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return;
        }

        // 不能进入，直接等待
        SleepConditionVariableSRW(&rwlock->writeCond, &rwlock->srwlock, INFINITE, 0);
    }
}

// ========== 非阻塞: tryLockForRead ==========
bool XReadWriteLock_platform_tryLockForRead(XReadWriteLock* lock)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return false;

    DWORD threadId = XReadWriteLock_currentThreadId();
    AcquireSRWLockExclusive(&rwlock->srwlock);

    bool success = false;
    if (rwlock->type & XReadWriteLock_Recursive) {
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            addReadOwner(rwlock, threadId);
            success = true;
        }
        else {
            int index = findReadOwnerIndex(rwlock, threadId);
            if (index != -1) {
                rwlock->readRecursionCounts[index]++;
                success = true;
            }
            else if (rwlock->isWriting == 0) {
                rwlock->readCount++;
                addReadOwner(rwlock, threadId);
                success = true;
            }
        }
    }
    else {
        if (rwlock->isWriting == 0) {
            rwlock->readCount++;
            success = true;
        }
    }

    ReleaseSRWLockExclusive(&rwlock->srwlock);
    return success;
}

// ========== 非阻塞: tryLockForWrite ==========
bool XReadWriteLock_platform_tryLockForWrite(XReadWriteLock* lock)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return false;

    DWORD threadId = XReadWriteLock_currentThreadId();
    AcquireSRWLockExclusive(&rwlock->srwlock);

    bool success = false;
    if (rwlock->type & XReadWriteLock_Recursive) {
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            rwlock->writeRecursionCount++;
            success = true;
        }
        else {
            int index = findReadOwnerIndex(rwlock, threadId);
            if (index != -1) {
                int count = rwlock->readRecursionCounts[index];
                for (int i = 0; i < count; i++) {
                    removeReadOwner(rwlock, threadId);
                }
                rwlock->readCount -= count;
            }
            if (rwlock->readCount == 0 && rwlock->isWriting == 0) {
                rwlock->isWriting = 1;
                rwlock->writeOwner = threadId;
                rwlock->writeRecursionCount = 1;
                success = true;
            }
        }
    }
    else {
        if (rwlock->readCount == 0 && rwlock->isWriting == 0) {
            rwlock->isWriting = 1;
            success = true;
        }
    }

    ReleaseSRWLockExclusive(&rwlock->srwlock);
    return success;
}

// ========== 超时: tryLockForReadTimeout ==========
bool XReadWriteLock_platform_tryLockForReadTimeout(XReadWriteLock* lock, int32_t timeout) {
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return false;

    if (timeout < 0) {
        XReadWriteLock_lockForRead(lock);
        return true;
    }

    DWORD threadId = XReadWriteLock_currentThreadId();
    DWORD start = GetTickCount();

    // --- 递归模式快速路径 ---
    if (rwlock->type & XReadWriteLock_Recursive) {
        AcquireSRWLockExclusive(&rwlock->srwlock);
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            addReadOwner(rwlock, threadId);
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return true;
        }
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            rwlock->readRecursionCounts[index]++;
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return true;
        }
        ReleaseSRWLockExclusive(&rwlock->srwlock);
    }

    while (true) {
        AcquireSRWLockExclusive(&rwlock->srwlock);

        if (rwlock->isWriting == 0) {
            rwlock->readCount++;
            if (rwlock->type & XReadWriteLock_Recursive) {
                addReadOwner(rwlock, threadId);
            }
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return true;
        }

        DWORD elapsed = GetTickCount() - start;
        DWORD remaining = (elapsed >= (DWORD)timeout) ? 0 : ((DWORD)timeout - elapsed);

        BOOL result = SleepConditionVariableSRW(&rwlock->readCond, &rwlock->srwlock, remaining, 0);
        if (!result) {
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return false;
        }
    }
}

// ========== 超时: tryLockForWriteTimeout ==========
bool XReadWriteLock_platform_tryLockForWriteTimeout(XReadWriteLock* lock, int32_t timeout) {
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return false;

    if (timeout < 0) {
        XReadWriteLock_lockForWrite(lock);
        return true;
    }

    DWORD threadId = XReadWriteLock_currentThreadId();
    DWORD start = GetTickCount();

    // --- 递归模式快速路径 ---
    if (rwlock->type & XReadWriteLock_Recursive) {
        AcquireSRWLockExclusive(&rwlock->srwlock);
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            rwlock->writeRecursionCount++;
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return true;
        }
        int index = findReadOwnerIndex(rwlock, threadId);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                removeReadOwner(rwlock, threadId);
            }
            rwlock->readCount -= count;
        }
        ReleaseSRWLockExclusive(&rwlock->srwlock);
    }

    while (true) {
        AcquireSRWLockExclusive(&rwlock->srwlock);

        if (rwlock->readCount == 0 && rwlock->isWriting == 0) {
            rwlock->isWriting = 1;
            if (rwlock->type & XReadWriteLock_Recursive) {
                rwlock->writeOwner = threadId;
                rwlock->writeRecursionCount = 1;
            }
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return true;
        }

        DWORD elapsed = GetTickCount() - start;
        DWORD remaining = (elapsed >= (DWORD)timeout) ? 0 : ((DWORD)timeout - elapsed);

        BOOL result = SleepConditionVariableSRW(&rwlock->writeCond, &rwlock->srwlock, remaining, 0);
        if (!result) {
            ReleaseSRWLockExclusive(&rwlock->srwlock);
            return false;
        }
    }
}

// ========== 核心: unlock ==========
void XReadWriteLock_platform_unlock(XReadWriteLock* lock)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return;

    DWORD threadId = XReadWriteLock_currentThreadId();
    AcquireSRWLockExclusive(&rwlock->srwlock);

    if (rwlock->type & XReadWriteLock_Recursive) {
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == threadId) {
            rwlock->writeRecursionCount--;
            if (rwlock->writeRecursionCount == 0) {
                rwlock->isWriting = 0;
                rwlock->writeOwner = 0;
            }
        }
        else {
            int index = findReadOwnerIndex(rwlock, threadId);
            if (index != -1) {
                if (--rwlock->readRecursionCounts[index] == 0) {
                    removeReadOwner(rwlock, threadId);
                    rwlock->readCount--;
                }
            }
        }
    }
    else {
        if (rwlock->isWriting) {
            rwlock->isWriting = 0;
        }
        else {
            rwlock->readCount--;
        }
    }

    // --- 关键修复: 无条件唤醒，不再依赖 waiting 计数器 ---
    // 策略: 写者优先
    // 如果现在没有活动的读者或写者，则锁已空闲，可以安全地唤醒一个写者。
    if (rwlock->readCount == 0 && rwlock->isWriting == 0) {
        WakeConditionVariable(&rwlock->writeCond);
    }
    // 注意：这里不再唤醒读者。因为如果有读者来，
    // 它们会发现 isWriting==0 并直接获取锁，无需被唤醒。

    ReleaseSRWLockExclusive(&rwlock->srwlock);
}

// 其他函数保持不变
bool XReadWriteLock_platform_hasReadLock(XReadWriteLock* lock)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return false;
    return findReadOwnerIndex(rwlock, XReadWriteLock_currentThreadId()) != -1;
}

bool XReadWriteLock_platform_hasWriteLock(XReadWriteLock* lock)
{
    XReadWriteLockWin32* rwlock = (XReadWriteLockWin32*)lock;
    if (!lock) return false;
    return rwlock->writeRecursionCount > 0 && rwlock->writeOwner == XReadWriteLock_currentThreadId();
}

#endif