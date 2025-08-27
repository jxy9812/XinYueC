#ifdef configUSE_FREERTOS
#include "XMutex.h"
#include "XMemory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <stdio.h>
typedef SemaphoreHandle_t XMutexHandle;  // FreeRTOS 用信号量（递归锁）

// 虚函数实现
static void VXMutex_deinit(XMutex* mutex);
static bool VXMutex_lock(XMutex* mutex);
static bool VXMutex_lock_wait(XMutex* mutex, size_t timeout);
static bool VXMutex_unlock(XMutex* mutex);

// 虚函数表初始化
XVtable* XMutex_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XMutex))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());

    void* table[] = {
        VXMutex_lock,
        VXMutex_lock_wait,
        VXMutex_unlock
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMutex_deinit);

#if SHOWCONTAINERSIZE
    printf("XMutexFreeRTOS size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// 创建堆对象
XMutex* XMutex_create(const char* name) {
    XMutex* mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (mutex) {
        XMutex_init(mutex, name);
    }
    return mutex;
}

// 初始化栈对象
void XMutex_init(XMutex* mutex, const char* name) {
    if (!mutex) return;
    XClass_init(&mutex->m_parent);
    XClassGetVtable(mutex) = XMutex_class_init();

    // 创建递归互斥锁（与 XWaitCondition 配合需要）
    mutex->m_mutex = xSemaphoreCreateRecursiveMutex();
    (void)name;  // 忽略名称
}

// 销毁对象
static void VXMutex_deinit(XMutex* mutex) {
    if (!mutex || !mutex->m_mutex) return;
    vSemaphoreDelete(mutex->m_mutex);  // 删除信号量
    mutex->m_mutex = NULL;
    // 调用父类析构
    XVtableGetFunc(XClass_class_init(), EXClass_Deinit, void(*)(XClass*))((XClass*)mutex);
}

// 无限等待上锁
static bool VXMutex_lock(XMutex* mutex) {
    if (!mutex || !mutex->m_mutex) return false;
    // FreeRTOS 递归锁上锁
    return xSemaphoreTakeRecursive(mutex->m_mutex, portMAX_DELAY) == pdTRUE;
}

// 超时等待上锁
static bool VXMutex_lock_wait(XMutex* mutex, size_t timeout) {
    if (!mutex || !mutex->m_mutex) return false;
    // 转换毫秒为 ticks
    TickType_t ticks = pdMS_TO_TICKS(timeout);
    return xSemaphoreTakeRecursive(mutex->m_mutex, ticks) == pdTRUE;
}

// 解锁
static bool VXMutex_unlock(XMutex* mutex) {
    if (!mutex || !mutex->m_mutex) return false;
    // FreeRTOS 递归锁解锁
    return xSemaphoreGiveRecursive(mutex->m_mutex) == pdTRUE;
}

#endif