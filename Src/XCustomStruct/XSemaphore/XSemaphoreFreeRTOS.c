#ifdef configUSE_FREERTOS
#include "XSemaphoreFreeRTOS.h"
#include "XMemory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

static void XMutexBase_delete(XSemaphoreFreeRTOS* semaphore);
//上锁
static bool VXSemaphoreBase_lock(XSemaphoreFreeRTOS* semaphore);
static bool VXSemaphoreBase_lock_wait(XSemaphoreFreeRTOS* semaphore, size_t timerout);
//解锁
static bool VXSemaphoreBase_unlock(XSemaphoreFreeRTOS* semaphore);
static void VXSemaphoreBase_lockISR(XSemaphoreFreeRTOS* semaphore);
static void VXSemaphoreBase_unlockISR(XSemaphoreFreeRTOS* semaphore);
XVtable* XSemaphoreFreeRTOS_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSemaphoreBase))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());
    void* table[] = {
        VXSemaphoreBase_lock,VXSemaphoreBase_lock_wait,VXSemaphoreBase_unlock,VXSemaphoreBase_lockISR,VXSemaphoreBase_unlockISR };
    //追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, XMutexBase_delete);
#if SHOWCONTAINERSIZE
    printf("XSemaphoreFreeRTOS size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XSemaphoreFreeRTOS* XSemaphoreFreeRTOS_create(const char* name)
{
    XSemaphoreFreeRTOS* semaphore = XMemory_malloc(sizeof(XSemaphoreFreeRTOS));
    XSemaphoreFreeRTOS_init(semaphore, name);
    return semaphore;
}

void XSemaphoreFreeRTOS_init(XSemaphoreFreeRTOS* semaphore, const char* name)
{
    if (semaphore == NULL)
        return;
    memset(((XSemaphoreBase*)semaphore) + 1, 0, sizeof(XSemaphoreFreeRTOS) - sizeof(XSemaphoreBase));
    XSemaphoreBase_init(semaphore, NULL);
    XClassGetVtable(semaphore) = XSemaphoreFreeRTOS_class_init();
   // 创建二值信号量，初始状态为"已获取"（0）
    semaphore->m_semaphore = xSemaphoreCreateBinary();
    if (semaphore->m_semaphore == NULL) {
        printf("创建信号量失败\n");
        return 1;
    }
    // 若需要初始状态为"已释放"（1），调用：
    xSemaphoreGive(semaphore->m_semaphore);
}


void XMutexBase_delete(XSemaphoreFreeRTOS* semaphore)
{
    // 关闭信号量句柄
    if (semaphore->m_semaphore)
        vSemaphoreDelete(semaphore->m_semaphore);
    //调用父类释放方法
    XVtableGetFunc(XClass_class_init(), EXClass_Delete, void(*)(XClass*));
}

bool VXSemaphoreBase_lock(XSemaphoreFreeRTOS* semaphore)
{
    return xSemaphoreTake(semaphore->m_semaphore, portMAX_DELAY) == pdTRUE;
}

bool VXSemaphoreBase_lock_wait(XSemaphoreFreeRTOS* semaphore, size_t timerout)
{
     return xSemaphoreTake(semaphore->m_semaphore, pdMS_TO_TICKS(timerout)) == pdTRUE;
}

bool VXSemaphoreBase_unlock(XSemaphoreFreeRTOS* semaphore)
{
    return xSemaphoreGive(semaphore->m_semaphore) == pdTRUE;
}
void VXSemaphoreBase_lockISR(XSemaphoreFreeRTOS* semaphore)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreTakeFromISR(semaphore->m_semaphore, &xHigherPriorityTaskWoken);
    return xHigherPriorityTaskWoken==pdTRUE;
}
void VXSemaphoreBase_unlockISR(XSemaphoreFreeRTOS* semaphore)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(semaphore->m_semaphore, &xHigherPriorityTaskWoken);
    return xHigherPriorityTaskWoken==pdTRUE;
}
#endif