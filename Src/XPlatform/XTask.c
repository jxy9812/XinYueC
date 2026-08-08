/**
 * @file XTask.c
 * @brief XThread 任务注册表和跨平台快照实现。
 * @details
 * 注册表只保存 XThread 借用指针，线程生命周期由 XThread 平台后端负责。
 * 枚举时先在短临界区内复制固定快照，再释放自旋锁后调用访问者，避免 Shell
 * 输出阻塞线程创建或销毁。整个实现不使用 Linux、Windows 或 RTOS API，也不
 * 为注册表分配堆内存。
 */

#include "XTask.h"
#include "XThread.h"
#include "XString.h"
#include "XAtomic.h"
#include <string.h>

typedef struct XTaskSnapshot {
    XTaskInfo info;
    char name[32];
} XTaskSnapshot;

static XThread* g_threads[XTASK_REGISTRY_CAPACITY];
static XAtomic_bool g_registryLock = { false };

static void xtask_lock(void)
{
    bool expected;
    do {
        expected = false;
    } while (!XAtomic_compare_exchange_strong_bool(
        &g_registryLock, &expected, true,
        XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));
}

static void xtask_unlock(void)
{
    XAtomic_store_bool(&g_registryLock, false, XAtomic_MemoryOrder_Release);
}

bool XTask_registerThread(XThread* thread)
{
    size_t i;
    if (!thread) return false;
    xtask_lock();
    for (i = 0; i < XTASK_REGISTRY_CAPACITY; ++i) {
        if (g_threads[i] == thread) {
            xtask_unlock();
            return true;
        }
    }
    for (i = 0; i < XTASK_REGISTRY_CAPACITY; ++i) {
        if (!g_threads[i]) {
            g_threads[i] = thread;
            xtask_unlock();
            return true;
        }
    }
    xtask_unlock();
    return false;
}

void XTask_unregisterThread(XThread* thread)
{
    size_t i;
    if (!thread) return;
    xtask_lock();
    for (i = 0; i < XTASK_REGISTRY_CAPACITY; ++i) {
        if (g_threads[i] == thread) {
            g_threads[i] = NULL;
            break;
        }
    }
    xtask_unlock();
}

static void xtask_copy_snapshot(XTaskSnapshot* snapshot, XThread* thread, size_t id)
{
    const XString* objectName;
    const char* name;
    if (!snapshot || !thread) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->info.id = (uint32_t)(id + 1u);
    snapshot->info.priority = (int32_t)XThread_priority(thread);
    snapshot->info.stackSize = XThread_stackSize(thread);
    snapshot->info.stackFree = 0;
    snapshot->info.state = XThread_isRunning(thread) ? XTaskState_Running :
                           (XThread_isFinished(thread) ? XTaskState_Finished :
                            XTaskState_Ready);
    objectName = XObject_objectName((const XObject*)thread);
    name = objectName ? XString_toUtf8(objectName) : NULL;
    if (!name || !name[0]) name = "XThread";
    strncpy(snapshot->name, name, sizeof(snapshot->name) - 1u);
    snapshot->name[sizeof(snapshot->name) - 1u] = '\0';
    snapshot->info.name = snapshot->name;
}

bool XTask_enumerateThreads(XTaskVisitor visitor, void* userData)
{
    XTaskSnapshot snapshots[XTASK_REGISTRY_CAPACITY];
    size_t count = 0;
    size_t i;
    if (!visitor) return false;
    xtask_lock();
    for (i = 0; i < XTASK_REGISTRY_CAPACITY; ++i) {
        if (g_threads[i]) {
            xtask_copy_snapshot(&snapshots[count], g_threads[i], i);
            ++count;
        }
    }
    xtask_unlock();
    for (i = 0; i < count; ++i) {
        if (!visitor(userData, &snapshots[i].info)) return false;
    }
    return true;
}
