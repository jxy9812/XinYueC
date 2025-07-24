#ifdef WIN32
#include "XSemaphore.h"
#include "XMemory.h"
#include "Windows.h"
#include <string.h>

static void VXMutex_delete(XSemaphore* mutex);
//上锁
static bool VXSemaphore_lock(XSemaphore* mutex);
static bool VXSemaphore_lock_wait(XSemaphore* mutex, size_t timerout);
//解锁
static bool VXSemaphore_unlock(XSemaphore* mutex);

static void VXSemaphore_lockISR(XSemaphore* mutex);
static void VXSemaphore_unlockISR(XSemaphore* mutex);
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
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMutex_delete);
#if SHOWCONTAINERSIZE
    printf("XSemaphore size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XSemaphore* XSemaphore_create(const char* name)
{
    XSemaphore* mutex = XMemory_malloc(sizeof(XSemaphore));
    XSemaphore_init(mutex,name);
    return mutex;
}

void XSemaphore_init(XSemaphore* semaphore, const char* name)
{
    if (semaphore == NULL)
        return;
    memset(((XMutex*)semaphore) + 1, 0, sizeof(XSemaphore) - sizeof(XMutex));
    XMutex_init(semaphore, name);
    XClassGetVtable(semaphore) = XSemaphore_class_init();
    // 创建信号量（初始时无人拥有）
    semaphore->m_semaphore = CreateSemaphore(
        NULL,                // 安全属性，通常为 NULL
        1,                   // 初始计数（1 表示可用）
        1,                   // 最大计数（必须为 1 以实现二值信号量）
        name  // 信号量名称（非 NULL 表示命名信号量）
    );
    if (semaphore->m_semaphore == NULL) {
        printf("创建信号量失败，错误码: %d\n", GetLastError());
        return 1;
    }
}


void VXMutex_delete(XSemaphore* mutex)
{
    // 关闭信号量句柄
    if (mutex->m_semaphore)
        CloseHandle(mutex->m_semaphore);
    //调用父类释放方法
    XVtableGetFunc(XClass_class_init(), EXClass_Deinit, void(*)(XClass*));
}

bool VXSemaphore_lock(XSemaphore* mutex)
{
    return WaitForSingleObject(mutex->m_semaphore, INFINITE) == WAIT_OBJECT_0;
}

bool VXSemaphore_lock_wait(XSemaphore* mutex, size_t timerout)
{
    return WaitForSingleObject(mutex->m_semaphore, timerout) == WAIT_OBJECT_0;
}

bool VXSemaphore_unlock(XSemaphore* mutex)
{
    return  ReleaseSemaphore(mutex->m_semaphore,1, NULL);
}
void VXSemaphore_lockISR(XSemaphore* mutex)
{
    return WaitForSingleObject(mutex->m_semaphore, 0) == WAIT_OBJECT_0;
}
void VXSemaphore_unlockISR(XSemaphore* mutex)
{
    return  ReleaseSemaphore(mutex->m_semaphore, 1, NULL);
}
#endif