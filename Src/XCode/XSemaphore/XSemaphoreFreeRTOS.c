#ifdef configUSE_FREERTOS
#include "XSemaphore.h"
#include "XMemory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

static void VXMutex_delete(XSemaphore* semaphore);
//上锁
static bool VXSemaphore_lock(XSemaphore* semaphore);
static bool VXSemaphore_lock_wait(XSemaphore* semaphore, size_t timerout);
//解锁
static bool VXSemaphore_unlock(XSemaphore* semaphore);
static void VXSemaphore_lockISR(XSemaphore* semaphore);
static void VXSemaphore_unlockISR(XSemaphore* semaphore);
XVtable* XSemaphore_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSemaphore))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());
    void* table[] = {
        VXSemaphore_lock,VXSemaphore_lock_wait,VXSemaphore_unlock,VXSemaphore_lockISR,VXSemaphore_unlockISR };
    //追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXMutex_delete);
#if SHOWCONTAINERSIZE
    printf("XSemaphore size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XSemaphore* XSemaphore_create(const char* name)
{
    XSemaphore* semaphore = XMemory_malloc(sizeof(XSemaphore));
    XSemaphore_init(semaphore, name);
    return semaphore;
}

void XSemaphore_init(XSemaphore* semaphore, const char* name)
{
    if (semaphore == NULL)
        return;
    memset(((XMutex*)semaphore) + 1, 0, sizeof(XSemaphore) - sizeof(XMutex));
    XMutex_init(semaphore, name);
    XClassGetVtable(semaphore) = XSemaphore_class_init();
   // 创建二值信号量，初始状态为"已获取"（0）
    semaphore->m_semaphore = xSemaphoreCreateBinary();
    if (semaphore->m_semaphore == NULL) {
        printf("创建信号量失败\n");
        return 1;
    }
    // 若需要初始状态为"已释放"（1），调用：
    xSemaphoreGive(semaphore->m_semaphore);
}


void VXMutex_delete(XSemaphore* semaphore)
{
    // 关闭信号量句柄
    if (semaphore->m_semaphore)
        vSemaphoreDelete(semaphore->m_semaphore);
    //调用父类释放方法
    XVtableGetFunc(XClass_class_init(), EXClass_Delete, void(*)(XClass*));
}

bool VXSemaphore_lock(XSemaphore* semaphore)
{
    return xSemaphoreTake(semaphore->m_semaphore, portMAX_DELAY) == pdTRUE;
}

bool VXSemaphore_lock_wait(XSemaphore* semaphore, size_t timerout)
{
     return xSemaphoreTake(semaphore->m_semaphore, pdMS_TO_TICKS(timerout)) == pdTRUE;
}

bool VXSemaphore_unlock(XSemaphore* semaphore)
{
    return xSemaphoreGive(semaphore->m_semaphore) == pdTRUE;
}
void VXSemaphore_lockISR(XSemaphore* semaphore)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreTakeFromISR(semaphore->m_semaphore, &xHigherPriorityTaskWoken);
    return xHigherPriorityTaskWoken==pdTRUE;
}
void VXSemaphore_unlockISR(XSemaphore* semaphore)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(semaphore->m_semaphore, &xHigherPriorityTaskWoken);
    return xHigherPriorityTaskWoken==pdTRUE;
}
#endif