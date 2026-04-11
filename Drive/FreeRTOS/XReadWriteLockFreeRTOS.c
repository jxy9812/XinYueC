#ifdef __FreeRTOS__
#include "XReadWriteLock.h"
#include "XMemory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

struct XReadWriteLock {
    XReadWriteLock_Type type;

    // 核心同步原语
    SemaphoreHandle_t m_mutex;       // 保护内部状态
    SemaphoreHandle_t readSem;     // 读者信号量
    SemaphoreHandle_t writeSem;    // 写者信号量

    // 状态跟踪
    int32_t readCount;             // 读者计数
    int32_t writeWaiting;          // 等待的写者数
    int32_t readWaiting;           // 等待的读者数
    BaseType_t isWriting;          // 是否有写者在操作

    // 递归模式支持
    TaskHandle_t writeOwner;       // 写锁拥有者任务句柄
    int writeRecursionCount;       // 写锁递归计数
    TaskHandle_t* readOwners;      // 读锁拥有者任务句柄列表
    int* readRecursionCounts;      // 读锁递归计数列表
    int readOwnerCount;            // 读锁拥有者数量
    int readOwnerCapacity;         // 读锁拥有者列表容量
};

size_t XReadWriteLock_getTypeSize() {
    return sizeof(struct XReadWriteLock);
}

static int findReadOwnerIndex(struct XReadWriteLock* rwlock, TaskHandle_t task) {
    for (int i = 0; i < rwlock->readOwnerCount; i++) {
        if (rwlock->readOwners[i] == task) {
            return i;
        }
    }
    return -1;
}

static bool addReadOwner(struct XReadWriteLock* rwlock, TaskHandle_t task) {
    int index = findReadOwnerIndex(rwlock, task);
    if (index != -1) {
        rwlock->readRecursionCounts[index]++;
        return true;
    }

    // 需要扩容
    if (rwlock->readOwnerCount >= rwlock->readOwnerCapacity) {
        int newCapacity = rwlock->readOwnerCapacity + 4;
        TaskHandle_t* newOwners = (TaskHandle_t*)pvPortMalloc(newCapacity * sizeof(TaskHandle_t));
        int* newCounts = (int*)pvPortMalloc(newCapacity * sizeof(int));

        if (!newOwners || !newCounts) {
            vPortFree(newOwners);
            vPortFree(newCounts);
            return false;
        }

        // 复制现有数据
        for (int i = 0; i < rwlock->readOwnerCount; i++) {
            newOwners[i] = rwlock->readOwners[i];
            newCounts[i] = rwlock->readRecursionCounts[i];
        }

        vPortFree(rwlock->readOwners);
        vPortFree(rwlock->readRecursionCounts);

        rwlock->readOwners = newOwners;
        rwlock->readRecursionCounts = newCounts;
        rwlock->readOwnerCapacity = newCapacity;
    }

    // 添加新的读锁拥有者
    rwlock->readOwners[rwlock->readOwnerCount] = task;
    rwlock->readRecursionCounts[rwlock->readOwnerCount] = 1;
    rwlock->readOwnerCount++;
    return true;
}

static bool removeReadOwner(struct XReadWriteLock* rwlock, TaskHandle_t task) {
    int index = findReadOwnerIndex(rwlock, task);
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
    rwlock->m_mutex = xSemaphoreCreateMutex();
    rwlock->readSem = xSemaphoreCreateBinary();
    rwlock->writeSem = xSemaphoreCreateBinary();

    rwlock->readCount = 0;
    rwlock->writeWaiting = 0;
    rwlock->readWaiting = 0;
    rwlock->isWriting = pdFALSE;

    // 递归模式初始化
    rwlock->writeOwner = NULL;
    rwlock->writeRecursionCount = 0;
    rwlock->readOwnerCount = 0;
    rwlock->readOwnerCapacity = 4;
    rwlock->readOwners = (TaskHandle_t*)pvPortMalloc(rwlock->readOwnerCapacity * sizeof(TaskHandle_t));
    rwlock->readRecursionCounts = (int*)pvPortMalloc(rwlock->readOwnerCapacity * sizeof(int));

    // 初始化信号量为未触发状态
    xSemaphoreTake(rwlock->readSem, 0);
    xSemaphoreTake(rwlock->writeSem, 0);
}

void XReadWriteLock_deinit(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    vSemaphoreDelete(rwlock->m_mutex);
    vSemaphoreDelete(rwlock->readSem);
    vSemaphoreDelete(rwlock->writeSem);
    vPortFree(rwlock->readOwners);
    vPortFree(rwlock->readRecursionCounts);

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

    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前任务持有写锁，直接增加读锁计数(写锁降级)
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == task) {
            addReadOwner(rwlock, task);
            return;
        }

        // 如果当前任务已持有读锁，增加递归计数
        int index = findReadOwnerIndex(rwlock, task);
        if (index != -1) {
            rwlock->readRecursionCounts[index]++;
            return;
        }
    }

    // 普通模式或需要真正获取锁
    while (true) {
        xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);

        if (!rwlock->isWriting && rwlock->writeWaiting == 0) {
            rwlock->readCount++;
            xSemaphoreGive(rwlock->m_mutex);

            // 记录读锁拥有者
            if (rwlock->type == XReadWriteLock_Recursive) {
                addReadOwner(rwlock, task);
            }
            return;
        }

        rwlock->readWaiting++;
        xSemaphoreGive(rwlock->m_mutex);
        xSemaphoreTake(rwlock->readSem, portMAX_DELAY);
        xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
        rwlock->readWaiting--;
        xSemaphoreGive(rwlock->m_mutex);
    }
}

void XReadWriteLock_lockForWrite(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前任务已持有写锁，增加递归计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == task) {
            rwlock->writeRecursionCount++;
            return;
        }

        // 如果当前任务持有读锁，需要先释放所有读锁
        int index = findReadOwnerIndex(rwlock, task);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                XReadWriteLock_unlock(rwlock);
            }
        }
    }

    // 普通模式或需要真正获取锁
    while (true) {
        xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);

        if (rwlock->readCount == 0 && !rwlock->isWriting) {
            rwlock->isWriting = pdTRUE;
            xSemaphoreGive(rwlock->m_mutex);

            // 记录写锁拥有者
            if (rwlock->type == XReadWriteLock_Recursive) {
                rwlock->writeOwner = task;
                rwlock->writeRecursionCount = 1;
            }
            return;
        }

        rwlock->writeWaiting++;
        xSemaphoreGive(rwlock->m_mutex);
        xSemaphoreTake(rwlock->writeSem, portMAX_DELAY);
        xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
        rwlock->writeWaiting--;
        xSemaphoreGive(rwlock->m_mutex);
    }
}

bool XReadWriteLock_tryLockForRead(XReadWriteLock* rwlock) {
    if (!rwlock) return false;

    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前任务持有写锁，直接增加读锁计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == task) {
            addReadOwner(rwlock, task);
            return true;
        }

        // 如果当前任务已持有读锁，增加递归计数
        int index = findReadOwnerIndex(rwlock, task);
        if (index != -1) {
            rwlock->readRecursionCounts[index]++;
            return true;
        }
    }

    // 尝试获取读锁
    if (xSemaphoreTake(rwlock->m_mutex, 0) != pdTRUE) {
        return false;
    }

    bool success = false;
    if (!rwlock->isWriting && rwlock->writeWaiting == 0) {
        rwlock->readCount++;
        success = true;

        // 记录读锁拥有者
        if (rwlock->type == XReadWriteLock_Recursive) {
            addReadOwner(rwlock, task);
        }
    }

    xSemaphoreGive(rwlock->m_mutex);
    return success;
}

bool XReadWriteLock_tryLockForWrite(XReadWriteLock* rwlock) {
    if (!rwlock) return false;

    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前任务已持有写锁，增加递归计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == task) {
            rwlock->writeRecursionCount++;
            return true;
        }

        // 如果当前任务持有读锁，需要先释放所有读锁
        int index = findReadOwnerIndex(rwlock, task);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                XReadWriteLock_unlock(rwlock);
            }
        }
    }

    // 尝试获取写锁
    if (xSemaphoreTake(rwlock->m_mutex, 0) != pdTRUE) {
        return false;
    }

    bool success = false;
    if (rwlock->readCount == 0 && !rwlock->isWriting) {
        rwlock->isWriting = pdTRUE;
        success = true;

        // 记录写锁拥有者
        if (rwlock->type == XReadWriteLock_Recursive) {
            rwlock->writeOwner = task;
            rwlock->writeRecursionCount = 1;
        }
    }

    xSemaphoreGive(rwlock->m_mutex);
    return success;
}

bool XReadWriteLock_tryLockForReadTimeout(XReadWriteLock* rwlock, int32_t timeout) {
    if (!rwlock) return false;

    // 永久等待
    if (timeout < 0) {
        XReadWriteLock_lockForRead(rwlock);
        return true;
    }

    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    TickType_t ticks = pdMS_TO_TICKS(timeout);
    TickType_t start = xTaskGetTickCount();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前任务持有写锁，直接增加读锁计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == task) {
            addReadOwner(rwlock, task);
            return true;
        }

        // 如果当前任务已持有读锁，增加递归计数
        int index = findReadOwnerIndex(rwlock, task);
        if (index != -1) {
            rwlock->readRecursionCounts[index]++;
            return true;
        }
    }

    while (true) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= ticks) {
            return false;
        }

        if (xSemaphoreTake(rwlock->m_mutex, ticks - elapsed) != pdTRUE) {
            return false;
        }

        if (!rwlock->isWriting && rwlock->writeWaiting == 0) {
            rwlock->readCount++;
            xSemaphoreGive(rwlock->m_mutex);

            // 记录读锁拥有者
            if (rwlock->type == XReadWriteLock_Recursive) {
                addReadOwner(rwlock, task);
            }
            return true;
        }

        rwlock->readWaiting++;
        xSemaphoreGive(rwlock->m_mutex);

        elapsed = xTaskGetTickCount() - start;
        if (elapsed >= ticks) {
            xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
            rwlock->readWaiting--;
            xSemaphoreGive(rwlock->m_mutex);
            return false;
        }

        if (xSemaphoreTake(rwlock->readSem, ticks - elapsed) != pdTRUE) {
            xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
            rwlock->readWaiting--;
            xSemaphoreGive(rwlock->m_mutex);
            return false;
        }

        xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
        rwlock->readWaiting--;
        xSemaphoreGive(rwlock->m_mutex);
    }
}

bool XReadWriteLock_tryLockForWriteTimeout(XReadWriteLock* rwlock, int32_t timeout) {
    if (!rwlock) return false;

    // 永久等待
    if (timeout < 0) {
        XReadWriteLock_lockForWrite(rwlock);
        return true;
    }

    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    TickType_t ticks = pdMS_TO_TICKS(timeout);
    TickType_t start = xTaskGetTickCount();

    // 递归模式处理
    if (rwlock->type == XReadWriteLock_Recursive) {
        // 如果当前任务已持有写锁，增加递归计数
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == task) {
            rwlock->writeRecursionCount++;
            return true;
        }

        // 如果当前任务持有读锁，需要先释放所有读锁
        int index = findReadOwnerIndex(rwlock, task);
        if (index != -1) {
            int count = rwlock->readRecursionCounts[index];
            for (int i = 0; i < count; i++) {
                XReadWriteLock_unlock(rwlock);
            }
        }
    }

    while (true) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= ticks) {
            return false;
        }

        if (xSemaphoreTake(rwlock->m_mutex, ticks - elapsed) != pdTRUE) {
            return false;
        }

        if (rwlock->readCount == 0 && !rwlock->isWriting) {
            rwlock->isWriting = pdTRUE;
            xSemaphoreGive(rwlock->m_mutex);

            // 记录写锁拥有者
            if (rwlock->type == XReadWriteLock_Recursive) {
                rwlock->writeOwner = task;
                rwlock->writeRecursionCount = 1;
            }
            return true;
        }

        rwlock->writeWaiting++;
        xSemaphoreGive(rwlock->m_mutex);

        elapsed = xTaskGetTickCount() - start;
        if (elapsed >= ticks) {
            xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
            rwlock->writeWaiting--;
            xSemaphoreGive(rwlock->m_mutex);
            return false;
        }

        if (xSemaphoreTake(rwlock->writeSem, ticks - elapsed) != pdTRUE) {
            xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
            rwlock->writeWaiting--;
            xSemaphoreGive(rwlock->m_mutex);
            return false;
        }

        xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
        rwlock->writeWaiting--;
        xSemaphoreGive(rwlock->m_mutex);
    }
}

void XReadWriteLock_unlock(XReadWriteLock* rwlock) {
    if (!rwlock) return;

    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    if (rwlock->type == XReadWriteLock_Recursive) {
        // 检查是否持有写锁
        if (rwlock->writeRecursionCount > 0 && rwlock->writeOwner == task) {
            if (--rwlock->writeRecursionCount == 0) {
                xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
                rwlock->isWriting = pdFALSE;

                // 优先唤醒写者
                if (rwlock->writeWaiting > 0) {
                    xSemaphoreGive(rwlock->writeSem);
                }
                else if (rwlock->readWaiting > 0) {
                    xSemaphoreGive(rwlock->readSem);
                }

                rwlock->writeOwner = NULL;
                xSemaphoreGive(rwlock->m_mutex);
            }
            return;
        }

        // 检查是否持有读锁
        int index = findReadOwnerIndex(rwlock, task);
        if (index != -1) {
            bool fullyReleased = false;
            if (--rwlock->readRecursionCounts[index] == 0) {
                fullyReleased = true;
                removeReadOwner(rwlock, task);

                xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);
                if (--rwlock->readCount == 0 && rwlock->writeWaiting > 0) {
                    xSemaphoreGive(rwlock->writeSem);
                }
                else if (rwlock->readWaiting > 0) {
                    xSemaphoreGive(rwlock->readSem);
                }
                xSemaphoreGive(rwlock->m_mutex);
            }
            return;
        }
    }

    // 非递归模式释放
    xSemaphoreTake(rwlock->m_mutex, portMAX_DELAY);

    if (rwlock->isWriting) {
        rwlock->isWriting = pdFALSE;
        // 优先唤醒写者
        if (rwlock->writeWaiting > 0) {
            xSemaphoreGive(rwlock->writeSem);
        }
        else if (rwlock->readWaiting > 0) {
            xSemaphoreGive(rwlock->readSem);
        }
    }
    else {
        if (--rwlock->readCount == 0 && rwlock->writeWaiting > 0) {
            xSemaphoreGive(rwlock->writeSem);
        }
        else if (rwlock->readWaiting > 0) {
            xSemaphoreGive(rwlock->readSem);
        }
    }

    xSemaphoreGive(rwlock->m_mutex);
}

XReadWriteLock_Type XReadWriteLock_type(XReadWriteLock* rwlock) {
    return rwlock ? rwlock->type : XReadWriteLock_NonRecursive;
}

bool XReadWriteLock_hasReadLock(XReadWriteLock* rwlock) {
    if (!rwlock || rwlock->type != XReadWriteLock_Recursive) {
        return false;
    }

    return findReadOwnerIndex(rwlock, xTaskGetCurrentTaskHandle()) != -1;
}

bool XReadWriteLock_hasWriteLock(XReadWriteLock* rwlock) {
    if (!rwlock || rwlock->type != XReadWriteLock_Recursive) {
        return false;
    }

    return rwlock->writeRecursionCount > 0 && rwlock->writeOwner == xTaskGetCurrentTaskHandle();
}

#endif
