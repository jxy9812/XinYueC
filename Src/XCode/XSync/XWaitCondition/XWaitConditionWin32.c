#ifdef _WIN32
#include "XWaitCondition.h"
#include "XMemory.h"
#include <windows.h>

// Windows平台具体结构体定义
struct XWaitCondition {
    CONDITION_VARIABLE cond;
};

size_t XWaitCondition_getTypeSize()
{
    return sizeof(struct XWaitCondition);
}
void XWaitCondition_init(XWaitCondition* cond) {
    if (cond == NULL) return;
    InitializeConditionVariable(&cond->cond);
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

bool XWaitCondition_wait(XWaitCondition* cond, XMutex* mutex, int32_t timeout) {
    if (cond == NULL || mutex == NULL) return false;

    // 获取Windows临界区句柄
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)XMutex_getNativeHandle(mutex);
    if (!cs) return false;

    // 转换超时时间（INFINITE表示无限等待）
    DWORD wait_ms = (timeout == -1) ? INFINITE : (DWORD)timeout;

    // 原子操作：释放锁 -> 等待条件 -> 重新获取锁
    BOOL result = SleepConditionVariableCS(&cond->cond, cs, wait_ms);
    return (result != 0);  // WAIT_OBJECT_0返回TRUE，超时返回FALSE
}

void XWaitCondition_wakeOne(XWaitCondition* cond) {
    if (cond == NULL) return;
    WakeConditionVariable(&cond->cond);
}

void XWaitCondition_wakeAll(XWaitCondition* cond) {
    if (cond == NULL) return;
    WakeAllConditionVariable(&cond->cond);
}

#endif