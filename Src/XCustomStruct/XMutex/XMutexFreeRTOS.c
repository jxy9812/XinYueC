#ifdef configUSE_FREERTOS
#include "XMutexFreeRTOS.h"
#include "XMemory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>
static void XMutexBase_delete(XMutexFreeRTOS* mutex);
//上锁
static bool VXMutexBase_lock(XMutexFreeRTOS* mutex);
static bool VXMutexBase_lock_wait(XMutexFreeRTOS* mutex, size_t timerout);
//解锁
static bool VXMutexBase_unlock(XMutexFreeRTOS* mutex);
XVtable* XMutexFreeRTOS_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XMutexBase))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());
    void* table[] = {
        VXMutexBase_lock,VXMutexBase_lock_wait,VXMutexBase_unlock };
    //追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, XMutexBase_delete);
#if SHOWCONTAINERSIZE
    printf("XMutexFreeRTOS size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XMutexFreeRTOS* XMutexFreeRTOS_create()
{
    XMutexFreeRTOS* mutex = XMemory_malloc(sizeof(XMutexFreeRTOS));
    XMutexFreeRTOS_init(mutex);
    return mutex;
}

void XMutexFreeRTOS_init(XMutexFreeRTOS* mutex)
{
    if (mutex == NULL)
        return;
    memset(((XMutexBase*)mutex) + 1, 0, sizeof(XMutexFreeRTOS) - sizeof(XMutexBase));
    XMutexBase_init(mutex, NULL);
    XClassGetVtable(mutex) = XMutexFreeRTOS_class_init();

    // 在初始化代码中创建互斥锁
    mutex->m_mutex = xSemaphoreCreateMutex();
    if ( mutex->m_mutex == NULL) 
    {
        printf("互斥锁创建失败\n");
    }
}

void XMutexBase_delete(XMutexFreeRTOS* mutex)
{
    if(mutex->m_mutex)
        vSemaphoreDelete(mutex->m_mutex);  // 删除互斥锁
    //调用父类释放方法
    XVtableGetFunc(XClass_class_init(), EXClass_Delete, void(*)(XClass*));
}

bool VXMutexBase_lock(XMutexFreeRTOS* mutex)
{
    return xSemaphoreTake(mutex->m_mutex, portMAX_DELAY) == pdTRUE;
}

bool VXMutexBase_lock_wait(XMutexFreeRTOS* mutex, size_t timerout)
{
    return xSemaphoreTake(mutex->m_mutex, pdMS_TO_TICKS(timerout)) == pdTRUE;
}

bool VXMutexBase_unlock(XMutexFreeRTOS* mutex)
{
     // 释放互斥锁
    return xSemaphoreGive(mutex->m_mutex)==pdTRUE;
}
#endif