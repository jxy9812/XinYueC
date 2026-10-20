/**
 * @file XFreeRTOSCompat.c
 * @brief FreeRTOS V7.5.2 兼容层实现。
 * @details 见 XFreeRTOSCompat.h 的说明。本文件只在 XFREERTOS_V7_COMPAT=1
 * 时参与编译，ARM（V9.0.0）构建整段裁空。
 */

#include "XFreeRTOSCompat.h"

#if XFREERTOS_V7_COMPAT

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 任务 TLS 槽位表。V7.5.2 的 TCB 无 TLS 字段，只能在库外侧维护。
 * 按任务句柄索引，句柄即 TCB 指针，可用作稳定键。 */
#define XFREERTOS_TLS_SLOTS 16

typedef struct XFreeRTOSCompatTlsSlot
{
    xTaskHandle task;   /**< 任务句柄；NULL 表示空槽。 */
    void*       value;  /**< 线程局部存储值。 */
} XFreeRTOSCompatTlsSlot;

static XFreeRTOSCompatTlsSlot g_xFreeRTOSCompatTls[XFREERTOS_TLS_SLOTS];

/** @brief 在临界区内查找槽位；assign 为 true 时允许占用空槽。 */
static XFreeRTOSCompatTlsSlot* compatFindSlot(xTaskHandle task, bool assign)
{
    XFreeRTOSCompatTlsSlot* freeSlot = NULL;
    int i;

    for (i = 0; i < XFREERTOS_TLS_SLOTS; ++i) {
        if (g_xFreeRTOSCompatTls[i].task == task)
            return &g_xFreeRTOSCompatTls[i];
        if (!freeSlot && g_xFreeRTOSCompatTls[i].task == NULL)
            freeSlot = &g_xFreeRTOSCompatTls[i];
    }
    return assign ? freeSlot : NULL;
}

void vTaskSetThreadLocalStoragePointer(xTaskHandle task, uint32_t index, void* value)
{
    XFreeRTOSCompatTlsSlot* slot;

    if (!task)
        return;

    taskENTER_CRITICAL();
    slot = compatFindSlot(task, value != NULL);
    if (slot) {
        slot->task = task;
        slot->value = value;
    }
    taskEXIT_CRITICAL();
}

void* pvTaskGetThreadLocalStoragePointer(xTaskHandle task, uint32_t index)
{
    XFreeRTOSCompatTlsSlot* slot;
    void* value;

    if (!task)
        return NULL;

    taskENTER_CRITICAL();
    slot = compatFindSlot(task, false);
    value = slot ? slot->value : NULL;
    taskEXIT_CRITICAL();
    return value;
}

#endif /* XFREERTOS_V7_COMPAT */
