#ifdef configUSE_FREERTOS
#include "XMutex.h"
#include "XMemory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

// FreeRTOS平台具体结构体定义
struct XMutex {
    SemaphoreHandle_t sem;
    XMutex_Type type;
    uint32_t recursive_count;
    TaskHandle_t owner_task;
};
size_t XMutex_geTypetSize()
{
    return sizeof(struct XMutex);
}
//// 内部辅助函数：获取平台相关句柄（供XWaitCondition使用）
//void* XMutex_getNativeHandle(XMutex* mutex) {
//    return mutex ? (void*)mutex->sem : NULL;
//}

void XMutex_init(XMutex* mutex) {
    if (!mutex) return;

    mutex->type = XMutex_Normal;
    mutex->recursive_count = 0;
    mutex->owner_task = NULL;

    if (type == XMutex_Recursive) {
        mutex->sem = xSemaphoreCreateRecursiveMutex();
    }
    else {
        mutex->sem = xSemaphoreCreateMutex();
    }
}

void XMutex_deinit(XMutex* mutex) {
    if (!mutex || !mutex->sem) return;
    vSemaphoreDelete(mutex->sem);
    mutex->sem = NULL;
}

XMutex* XMutex_create() 
{
    XMutex* mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (mutex) {
        XMutex_init(mutex);
        if (!mutex->sem) {
            XMemory_free(mutex);
            return NULL;
        }
    }
    return mutex;
}

void XMutex_delete(XMutex* mutex) {
    if (mutex) {
        XMutex_deinit(mutex);
        XMemory_free(mutex);
    }
}

void XMutex_lock(XMutex* mutex) {
    if (!mutex || !mutex->sem) return;

    if (mutex->type == XMutex_Recursive) {
        xSemaphoreTakeRecursive(mutex->sem, portMAX_DELAY);
    }
    else {
        xSemaphoreTake(mutex->sem, portMAX_DELAY);
    }
}

bool XMutex_tryLock(XMutex* mutex) {
    if (!mutex || !mutex->sem) return false;

    BaseType_t result;
    if (mutex->type == XMutex_Recursive) {
        result = xSemaphoreTakeRecursive(mutex->sem, 0);
    }
    else {
        result = xSemaphoreTake(mutex->sem, 0);
    }

    return result == pdTRUE;
}

bool XMutex_tryLockTimeout(XMutex* mutex, uint32_t timeout) {
    if (!mutex || !mutex->sem) return false;

    TickType_t ticks = pdMS_TO_TICKS(timeout);
    BaseType_t result;

    if (mutex->type == XMutex_Recursive) {
        result = xSemaphoreTakeRecursive(mutex->sem, ticks);
    }
    else {
        result = xSemaphoreTake(mutex->sem, ticks);
    }

    return result == pdTRUE;
}

void XMutex_unlock(XMutex* mutex) {
    if (!mutex || !mutex->sem) return;

    if (mutex->type == XMutex_Recursive) {
        xSemaphoreGiveRecursive(mutex->sem);
    }
    else {
        xSemaphoreGive(mutex->sem);
    }
}

bool XMutex_isRecursive(XMutex* mutex) {
    return mutex ? (mutex->type == XMutex_Recursive) : false;
}
XMutex_Type XMutex_type(XMutex* mutex)
{
    return mutex->type;
}
#endif