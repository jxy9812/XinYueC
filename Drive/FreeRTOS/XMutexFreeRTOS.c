#ifdef __FreeRTOS__
#include "XMutex.h"
#include "XThread.h" // For XThread_currentThreadId(), which should map to xTaskGetCurrentTaskHandle()
#include <string.h>
#include "semphr.h"  // FreeRTOS semaphore API

// PlatformPrivate 结构体定义
// 对于FreeRTOS，我们使用 SemaphoreHandle_t 来持有互斥信号量
typedef struct PlatformPrivate
{
    SemaphoreHandle_t mutex;
} PlatformPrivate;

// 声明外部函数，这些函数在 XMutex.c 中被调用
PlatformPrivate* XMutex_getPlatformPrivate(XMutex* mutex);

// ========== 平台抽象接口实现 ==========
size_t XMutex_PlatformPrivate_size()
{
    return sizeof(PlatformPrivate);
}

void XMutex_platform_init(PlatformPrivate* p)
{
    if (!p) return;

    // 创建一个互斥信号量 (Mutex Semaphore)
    // FreeRTOS 的互斥信号量默认支持优先级继承，可以有效防止优先级反转
    p->mutex = xSemaphoreCreateMutex();

    // 注意：在资源受限的嵌入式系统中，创建失败是可能的。
    // 根据您的错误处理策略，这里可以选择断言或设置一个无效状态。
    // 为了简单起见，我们假设创建总是成功。
    // 如果需要更健壮的处理，可以在 XMutex_init 中检查 p->mutex 是否为 NULL。
}

void XMutex_platform_deinit(PlatformPrivate* p)
{
    if (!p || !p->mutex) return;

    // 在FreeRTOS中，通常不建议在运行时删除内核对象，尤其是当它们可能还在被使用时。
    // 但为了API的完整性，我们在这里调用 vSemaphoreDelete。
    // 请确保在调用此函数时，该互斥锁未被任何任务持有。
    vSemaphoreDelete(p->mutex);
    p->mutex = NULL;
}

void XMutex_platform_lock(PlatformPrivate* p)
{
    if (!p || !p->mutex) return;

    // 获取互斥锁，portMAX_DELAY 表示永久阻塞直到成功
    xSemaphoreTake(p->mutex, portMAX_DELAY);
}

bool XMutex_platform_tryLock(PlatformPrivate* p)
{
    if (!p || !p->mutex) return false;

    // 尝试立即获取互斥锁，0 表示不等待
    return (xSemaphoreTake(p->mutex, 0) == pdTRUE);
}

void XMutex_platform_unlock(PlatformPrivate* p)
{
    if (!p || !p->mutex) return;

    // 释放互斥锁
    xSemaphoreGive(p->mutex);
}
#endif