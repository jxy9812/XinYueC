#ifdef _WIN32
#include "XWaitCondition.h"
#include "XMemory.h"
#include <windows.h>

// 注意：Windows平台依赖XMutex内部为CRITICAL_SECTION
// 若XMutex使用内核Mutex，请修改为CRITICAL_SECTION实现

void XWaitCondition_init(XWaitCondition* cond) {
    if (cond == NULL) return;
    InitializeConditionVariable(&cond->handle);
}

void XWaitCondition_deinit(XWaitCondition* cond) {
    if (cond == NULL) return;
    // Windows条件变量无需显式销毁
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

bool XWaitCondition_wait(XWaitCondition* cond, XMutex* mutex, int timeout) {
    if (cond == NULL || mutex == NULL) return false;

    // 转换超时时间（INFINITE表示无限等待）
    DWORD wait_ms = (timeout == -1) ? INFINITE : (DWORD)timeout;

    // 假设XMutex内部存储的是CRITICAL_SECTION指针
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex->m_mutex;

    // 原子操作：释放锁 -> 等待条件 -> 重新获取锁
    BOOL result = SleepConditionVariableCS(&cond->handle, cs, wait_ms);
    return (result != 0);  // WAIT_OBJECT_0返回TRUE，超时返回FALSE
}

void XWaitCondition_wakeOne(XWaitCondition* cond) {
    if (cond == NULL) return;
    WakeConditionVariable(&cond->handle);
}

void XWaitCondition_wakeAll(XWaitCondition* cond) {
    if (cond == NULL) return;
    WakeAllConditionVariable(&cond->handle);
}
#endif