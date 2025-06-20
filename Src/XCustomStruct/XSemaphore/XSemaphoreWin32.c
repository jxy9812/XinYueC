#ifdef WIN32
#include "XSemaphoreWin32.h"
#include "XMemory.h"
#include "Windows.h"
#include <string.h>

static void XMutexBase_delete(XSemaphoreWin32* mutex);
//上锁
static bool VXSemaphoreBase_lock(XSemaphoreWin32* mutex);
static bool VXSemaphoreBase_lock_wait(XSemaphoreWin32* mutex, size_t timerout);
//解锁
static bool VXSemaphoreBase_unlock(XSemaphoreWin32* mutex);

static void VXSemaphoreBase_lockISR(XSemaphoreWin32* mutex);
static void VXSemaphoreBase_unlockISR(XSemaphoreWin32* mutex);
XVtable* XSemaphoreWin32_class_init()
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
    printf("XSemaphoreWin32 size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XSemaphoreWin32* XSemaphoreWin32_create(const char* name)
{
    XSemaphoreWin32* mutex = XMemory_malloc(sizeof(XSemaphoreWin32));
    XSemaphoreWin32_init(mutex,name);
    return mutex;
}

void XSemaphoreWin32_init(XSemaphoreWin32* mutex, const char* name)
{
    if (mutex == NULL)
        return;
    memset(((XSemaphoreBase*)mutex) + 1, 0, sizeof(XSemaphoreWin32) - sizeof(XSemaphoreBase));
    XSemaphoreBase_init(mutex, NULL);
    XClassGetVtable(mutex) = XSemaphoreWin32_class_init();
    // 创建信号量（初始时无人拥有）
    mutex->m_semaphore = CreateSemaphore(
        NULL,                // 安全属性，通常为 NULL
        1,                   // 初始计数（1 表示可用）
        1,                   // 最大计数（必须为 1 以实现二值信号量）
        name  // 信号量名称（非 NULL 表示命名信号量）
    );
    if (mutex->m_semaphore == NULL) {
        printf("创建信号量失败，错误码: %d\n", GetLastError());
        return 1;
    }
}


void XMutexBase_delete(XSemaphoreWin32* mutex)
{
    // 关闭信号量句柄
    if (mutex->m_semaphore)
        CloseHandle(mutex->m_semaphore);
    //调用父类释放方法
    XVtableGetFunc(XClass_class_init(), EXClass_Delete, void(*)(XClass*));
}

bool VXSemaphoreBase_lock(XSemaphoreWin32* mutex)
{
    return WaitForSingleObject(mutex->m_semaphore, INFINITE) == WAIT_OBJECT_0;
}

bool VXSemaphoreBase_lock_wait(XSemaphoreWin32* mutex, size_t timerout)
{
    return WaitForSingleObject(mutex->m_semaphore, timerout) == WAIT_OBJECT_0;
}

bool VXSemaphoreBase_unlock(XSemaphoreWin32* mutex)
{
    return  ReleaseSemaphore(mutex->m_semaphore,1, NULL);
}
void VXSemaphoreBase_lockISR(XSemaphoreWin32* mutex)
{
    return WaitForSingleObject(mutex->m_semaphore, 0) == WAIT_OBJECT_0;
}
void VXSemaphoreBase_unlockISR(XSemaphoreWin32* mutex)
{
    return  ReleaseSemaphore(mutex->m_semaphore, 1, NULL);
}
#endif