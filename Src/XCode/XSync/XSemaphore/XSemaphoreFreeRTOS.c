#ifdef configUSE_FREERTOS
#include "XSemaphore.h"
#include "XMemory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

// FreeRTOS平台具体结构体定义（参考XWaitConditionFreeRTOS.c）
struct XSemaphore {
    SemaphoreHandle_t handle;         // FreeRTOS信号量句柄
    volatile int32_t available_count; // 手动维护计数（FreeRTOS原生不提供查询接口）
};

// 实现类型大小获取接口
size_t XSemaphore_getTypeSize() {
    return sizeof(struct XSemaphore);
}

void XSemaphore_init(XSemaphore* sem, int32_t initial, int32_t maximum) {
    if (!sem || initial < 0 || maximum <= 0 || initial > maximum) return;
    sem->handle = xSemaphoreCreateCounting(maximum, initial);
    sem->available_count = initial;
}

void XSemaphore_deinit(XSemaphore* sem) {
    if (sem && sem->handle) {
        vSemaphoreDelete(sem->handle);
        sem->handle = NULL;
        sem->available_count = 0;
    }
}

XSemaphore* XSemaphore_create(int32_t initial, int32_t maximum) {
    XSemaphore* sem = (XSemaphore*)XMemory_malloc(sizeof(struct XSemaphore));
    if (sem) {
        XSemaphore_init(sem, initial, maximum);
        if (!sem->handle) {
            XMemory_free(sem);
            return NULL;
        }
    }
    return sem;
}

void XSemaphore_delete(XSemaphore* sem) {
    if (sem) {
        XSemaphore_deinit(sem);
        XMemory_free(sem);
    }
}

void XSemaphore_acquire(XSemaphore* sem, int32_t n) {
    if (!sem || !sem->handle || n <= 0) return;
    for (int32_t i = 0; i < n; i++) {
        xSemaphoreTake(sem->handle, portMAX_DELAY);
        taskENTER_CRITICAL();
        sem->available_count--;
        taskEXIT_CRITICAL();
    }
}

void XSemaphore_release(XSemaphore* sem, int32_t n) {
    if (!sem || !sem->handle || n <= 0) return;
    for (int32_t i = 0; i < n; i++) {
        xSemaphoreGive(sem->handle);
        taskENTER_CRITICAL();
        sem->available_count++;
        taskEXIT_CRITICAL();
    }
}

bool XSemaphore_tryAcquire(XSemaphore* sem, int32_t n) {
    if (!sem || !sem->handle || n <= 0) return false;

    taskENTER_CRITICAL();
    if (sem->available_count < n) {
        taskEXIT_CRITICAL();
        return false;
    }
    taskEXIT_CRITICAL();

    bool success = true;
    for (int32_t i = 0; i < n; i++) {
        if (xSemaphoreTake(sem->handle, 0) != pdTRUE) {
            success = false;
            break;
        }
    }

    taskENTER_CRITICAL();
    if (success) {
        sem->available_count -= n;
    }
    else {
        // 部分获取失败时回滚
        for (int32_t i = 0; i < n; i++) {
            xSemaphoreGive(sem->handle);
        }
    }
    taskEXIT_CRITICAL();
    return success;
}

bool XSemaphore_tryAcquireTimeout(XSemaphore* sem, int32_t n, uint32_t timeout) {
    if (!sem || !sem->handle || n <= 0) return false;

    bool success = true;
    for (int32_t i = 0; i < n; i++) {
        if (xSemaphoreTake(sem->handle, pdMS_TO_TICKS(timeout)) != pdTRUE) {
            success = false;
            break;
        }
    }

    taskENTER_CRITICAL();
    if (success) {
        sem->available_count -= n;
    }
    else {
        // 部分获取失败时回滚
        for (int32_t i = 0; i < n; i++) {
            xSemaphoreGive(sem->handle);
        }
    }
    taskEXIT_CRITICAL();
    return success;
}

int32_t XSemaphore_available(XSemaphore* sem) {
    if (!sem || !sem->handle) return -1;
    taskENTER_CRITICAL();
    int32_t count = sem->available_count;
    taskEXIT_CRITICAL();
    return count;
}

#endif