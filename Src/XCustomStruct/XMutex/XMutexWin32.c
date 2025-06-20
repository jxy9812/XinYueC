#ifdef WIN32
#include "XMutexWin32.h"
#include "XMemory.h"
#include "Windows.h"
#include <string.h>
static void XMutexBase_delete(XMutexWin32* mutex);
//上锁
static bool VXMutexBase_lock(XMutexWin32* mutex);
static bool VXMutexBase_lock_wait(XMutexWin32* mutex, size_t timerout);
//解锁
static bool VXMutexBase_unlock(XMutexWin32* mutex);
XVtable* XMutexWin32_class_init()
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
        VXMutexBase_lock,VXMutexBase_lock_wait,VXMutexBase_unlock};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, XMutexBase_delete);
#if SHOWCONTAINERSIZE
    printf("XMutexWin32 size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XMutexWin32* XMutexWin32_create(const char* name)
{
    XMutexWin32* mutex = XMemory_malloc(sizeof(XMutexWin32));
    XMutexWin32_init(mutex, name);
	return mutex;
}

void XMutexWin32_init(XMutexWin32* mutex, const char* name)
{
    if (mutex == NULL)
        return;
    memset(((XMutexBase*)mutex) + 1, 0, sizeof(XMutexWin32) - sizeof(XMutexBase));
    XMutexBase_init(mutex, name);
    XClassGetVtable(mutex) = XMutexWin32_class_init();
    // 创建互斥锁（初始时无人拥有）
    mutex->m_mutex = CreateMutex(NULL, FALSE, name);
    if (mutex->m_mutex == NULL) {
        printf("创建互斥锁失败，错误码: %d\n", GetLastError());
        return 1;
    }
}

void XMutexBase_delete(XMutexWin32* mutex)
{
    // 关闭互斥锁句柄
    if (mutex->m_mutex)
        CloseHandle(mutex->m_mutex);
    //调用父类释放方法
    XVtableGetFunc(XClass_class_init(), EXClass_Delete, void(*)(XClass*));
}

bool VXMutexBase_lock(XMutexWin32* mutex)
{
    return WaitForSingleObject(mutex->m_mutex, INFINITE) == WAIT_OBJECT_0;
}

bool VXMutexBase_lock_wait(XMutexWin32* mutex, size_t timerout)
{
    return WaitForSingleObject(mutex->m_mutex, timerout) == WAIT_OBJECT_0;
}

bool VXMutexBase_unlock(XMutexWin32* mutex)
{
    return  ReleaseMutex(mutex->m_mutex);
}
#endif