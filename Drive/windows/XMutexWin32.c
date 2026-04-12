#ifdef _WIN32
#include "XRecursiveMutex.h"
#include "XMemory.h"
#include <windows.h>

// Windows平台具体结构体定义
struct XMutex {
    CRITICAL_SECTION cs;       // 临界区
    XMutex_Type type;          // 锁类型
    uint32_t recursive_count;  // 递归计数
    DWORD owner_thread;        // 拥有者线程ID
};
size_t XMutex_typetSize()
{
    return sizeof(struct XMutex);
}
//// 内部辅助函数：获取平台相关句柄（供XWaitCondition使用）
//void* XMutex_getNativeHandle(XMutex* mutex) {
//    return mutex ? &mutex->cs : NULL;
//}
void XRecursiveMutex_init(XRecursiveMutex* mutex)
{
    if (!mutex) return;

    mutex->type = XMutex_Recursive;
    mutex->recursive_count = 0;
    mutex->owner_thread = 0;
    InitializeCriticalSection(&mutex->cs);
}
XRecursiveMutex* XRecursiveMutex_create()
{
    XMutex* mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (mutex) 
    {
        XRecursiveMutex_init(mutex);
    }
    return mutex;
}
void XMutex_init(XMutex* mutex)
{
    if (!mutex) return;

    mutex->type = XMutex_Normal;
    mutex->recursive_count = 0;
    mutex->owner_thread = 0;
    InitializeCriticalSection(&mutex->cs);
}

void XMutex_deinit(XMutex* mutex) {
    if (!mutex) return;
    DeleteCriticalSection(&mutex->cs);
}

XMutex* XMutex_create() 
{
    XMutex* mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (mutex) {
        XMutex_init(mutex);
    }
    return mutex;
}

void XMutex_delete(XMutex* mutex) {
    if (mutex) {
        XMutex_deinit(mutex);
        XMemory_free(mutex);
    }
}

void XMutex_lock(XMutex* mutex) {
    if (!mutex) return;

    if (mutex->type == XMutex_Recursive) {
        DWORD current_thread = GetCurrentThreadId();
        if (mutex->owner_thread == current_thread) {
            // 同一线程递归上锁
            mutex->recursive_count++;
            return;
        }
    }

    EnterCriticalSection(&mutex->cs);
    if (mutex->type == XMutex_Recursive) {
        mutex->owner_thread = GetCurrentThreadId();
        mutex->recursive_count = 1;
    }
}

bool XMutex_tryLock(XMutex* mutex) {
    if (!mutex) return false;

    if (mutex->type == XMutex_Recursive) {
        DWORD current_thread = GetCurrentThreadId();
        if (mutex->owner_thread == current_thread) {
            mutex->recursive_count++;
            return true;
        }
    }

    if (TryEnterCriticalSection(&mutex->cs)) {
        if (mutex->type == XMutex_Recursive) {
            mutex->owner_thread = GetCurrentThreadId();
            mutex->recursive_count = 1;
        }
        return true;
    }

    return false;
}

bool XMutex_tryLockTimeout(XMutex* mutex, uint32_t timeout) {
    if (!mutex) return false;

    if (mutex->type == XMutex_Recursive) {
        DWORD current_thread = GetCurrentThreadId();
        if (mutex->owner_thread == current_thread) {
            mutex->recursive_count++;
            return true;
        }
    }

    DWORD start_time = GetTickCount();
    while (true) {
        if (TryEnterCriticalSection(&mutex->cs)) {
            if (mutex->type == XMutex_Recursive) {
                mutex->owner_thread = GetCurrentThreadId();
                mutex->recursive_count = 1;
            }
            return true;
        }

        if (GetTickCount() - start_time >= timeout) {
            return false;
        }

        Sleep(1);
    }
}

void XMutex_unlock(XMutex* mutex) {
    if (!mutex) return;

    if (mutex->type == XMutex_Recursive) {
        DWORD current_thread = GetCurrentThreadId();
        if (mutex->owner_thread != current_thread) {
            // 不是锁的拥有者，不能解锁
            return;
        }

        if (--mutex->recursive_count > 0) {
            return;
        }

        mutex->owner_thread = 0;
    }

    LeaveCriticalSection(&mutex->cs);
}

bool XMutex_isRecursive(XMutex* mutex) {
    return mutex ? (mutex->type == XMutex_Recursive) : false;
}
XMutex_Type XMutex_type(XMutex* mutex)
{
    return mutex->type;
}
#endif