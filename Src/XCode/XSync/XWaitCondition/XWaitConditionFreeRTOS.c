#ifdef configUSE_FREERTOS
#include "XWaitCondition.h"
#include "XMemory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

// FreeRTOS平台具体结构体定义
struct XWaitCondition {
    SemaphoreHandle_t sem;
    volatile int32_t wait_count;
    StaticSemaphore_t sem_buf;
};

size_t XWaitCondition_getTypeSize()
{
    return sizeof(struct XWaitCondition);
}

void XWaitCondition_init(XWaitCondition* cond) {
    if (cond == NULL) return;
    cond->wait_count = 0;
    // 创建二进制信号量（初始为0）
    cond->sem = xSemaphoreCreateBinaryStatic(&cond->sem_buf);
}

void XWaitCondition_deinit(XWaitCondition* cond) {
    if (cond == NULL) return;
    vSemaphoreDelete(cond->sem);
    cond->sem = NULL;
    cond->wait_count = 0;
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

    // 等待前增加计数
    taskENTER_CRITICAL();
    cond->wait_count++;
    taskEXIT_CRITICAL();

    // 释放互斥锁
    XMutex_unlock(mutex);

    // 等待信号量（超时转换为FreeRTOS ticks）
    TickType_t ticks = (timeout == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    BaseType_t result = xSemaphoreTake(cond->sem, ticks);

    // 重新获取互斥锁
    XMutex_lock(mutex);

    // 等待后减少计数
    taskENTER_CRITICAL();
    cond->wait_count--;
    taskEXIT_CRITICAL();

    return (result == pdTRUE);
}

void XWaitCondition_wakeOne(XWaitCondition* cond) {
    if (cond == NULL || cond->wait_count <= 0) return;
    xSemaphoreGive(cond->sem);
}

void XWaitCondition_wakeAll(XWaitCondition* cond) {
    if (cond == NULL || cond->wait_count <= 0) return;
    // 唤醒所有等待线程（逐个释放信号量）
    taskENTER_CRITICAL();
    int32_t count = cond->wait_count;
    taskEXIT_CRITICAL();

    for (int32_t i = 0; i < count; i++) {
        xSemaphoreGive(cond->sem);
    }
}

#endif