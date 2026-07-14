#include "XMutex.h"
#include "XThread.h"
#include "XDateTime.h" 
#include <string.h>
typedef struct XMutex
{
    XLock_Type type;
    XALIGNAS(sizeof(void*)) char m_d[];//扩展数据
}XMutex;
typedef struct SpinPrivate
{
	XAtomic_bool state; // 用于自旋模式: false=unlocked, true=locked
    XALIGNAS(sizeof(void*)) char m_d[];//扩展数据
}SpinPrivate;
typedef struct RecursivePrivate
{
	uint32_t recursive_count;   // 递归计数
	XHandle owner_thread;        // 拥有者线程ID
}RecursivePrivate;
typedef struct PlatformPrivate PlatformPrivate;
#define GetSpinPrivate(mutex)   ((SpinPrivate*)mutex->m_d)
//平台抽象数据大小
size_t XMutex_PlatformPrivate_size();
void XMutex_platform_init(PlatformPrivate* p);
void XMutex_platform_deinit(PlatformPrivate* p);
void XMutex_platform_lock(PlatformPrivate* p);
bool XMutex_platform_tryLock(PlatformPrivate* p);
//bool _XMutex_platform_tryLockTimeout_private(XMutex_Private* priv, uint32_t timeout);
void XMutex_platform_unlock(PlatformPrivate* p);

static RecursivePrivate* XMutex_getRecursivePrivate(XMutex* mutex)
{
    if (!mutex)return NULL;
    if (mutex->type & XLock_Spin)
        return GetSpinPrivate(mutex)->m_d;//挂在SpinPrivate后面
    return (uint8_t*)(mutex->m_d) + XMutex_PlatformPrivate_size();//挂在PlatformPrivate后面
}
//在抽象平台层调用
PlatformPrivate* XMutex_getPlatformPrivate(XMutex* mutex)
{
    return GetSpinPrivate(mutex);//跟SpinPrivate是互斥的所以地址是一样的
}

// ========== 判断是否为自旋模式 ==========
static bool is_spin_mode(XLock_Type type) {
    return type & XLock_Spin;
}
//
// ========== 自旋模式内部函数 (跨平台) ==========
// 注意：现在直接操作 mutex->state

static void spin_lock(XMutex* mutex) 
{
    SpinPrivate* p = GetSpinPrivate(mutex);
    bool expected = false;
    // 循环尝试获取锁
    while (!XAtomic_compare_exchange_strong_bool(&p->state, &expected, true, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
        expected = false; // 重置 expected，为下一次尝试做准备
        XThread_yieldCurrentThread();
    }
}

static void spin_unlock(XMutex* mutex) 
{
    SpinPrivate* p = GetSpinPrivate(mutex);
    // 直接解锁原子状态
    XAtomic_store_bool(&p->state, false, XAtomic_MemoryOrder_Release);
}
//
//// ========== 公共API实现 ==========
//
size_t XMutex_typetSize(XLock_Type type)
{
    size_t size= sizeof(XMutex);
    if (type & XLock_Spin)
        size += sizeof(SpinPrivate);
    else
        size += XMutex_PlatformPrivate_size();
    if (type & XLock_Recursive)
        size += sizeof(RecursivePrivate);
    return size;
    //return sizeof(XMutex) + (type & XMutex_Spin)?sizeof(SpinPrivate): XMutex_PlatformPrivate_size() +
    //    (type & XMutex_Recursive) ? sizeof(RecursivePrivate) : 0
    //    ;
}
void XMutex_init(XMutex* mutex, XLock_Type type)
{
    if (!mutex) return;
    memset(mutex, 0, XMutex_typetSize(type));
    mutex->type = type;
    if (!is_spin_mode(type))
    {
        XMutex_platform_init(XMutex_getPlatformPrivate(mutex));
    }
}

void XMutex_deinit(XMutex* mutex)
{
    if (!mutex) return;

    if (!is_spin_mode(mutex->type))
    {
        XMutex_platform_deinit(XMutex_getPlatformPrivate(mutex));
    }
}

XMutex* XMutex_create(XLock_Type type)
{
    XMutex* mutex = (XMutex*)XMalloc_System(XMutex_typetSize(type));
    if (mutex) {
        XMutex_init(mutex, type);
    }
    return mutex;
}

void XMutex_delete(XMutex* mutex) 
{
    if (mutex) {
        XMutex_deinit(mutex);
        XFree_System(mutex);
    }
}

void XMutex_lock(XMutex* mutex) 
{
    if (!mutex) return;
    if (mutex->type & XLock_Recursive)
    {
        XHandle current_thread = XThread_currentThreadId();
        RecursivePrivate* recursive = XMutex_getRecursivePrivate(mutex);
        if (recursive->owner_thread == current_thread) {
            // 同一线程递归上锁
            recursive->recursive_count++;
            return;
        }
    }
    if (is_spin_mode(mutex->type)) 
        spin_lock(mutex);
    else  
        XMutex_platform_lock(XMutex_getPlatformPrivate(mutex));
    if (mutex->type & XLock_Recursive)
    {
        RecursivePrivate* recursive = XMutex_getRecursivePrivate(mutex);
        recursive->owner_thread = XThread_currentThreadId();
        recursive->recursive_count = 1;
    }
}

bool XMutex_tryLock(XMutex* mutex) 
{
    if (!mutex) return false;
    if (mutex->type & XLock_Recursive)
    {
        RecursivePrivate* recursive = XMutex_getRecursivePrivate(mutex);
        if (recursive->owner_thread == XThread_currentThreadId()) 
        {
            // 同一线程递归上锁
            recursive->recursive_count++;
            return true;
        }
    }

    bool is_lock = false;
    if (is_spin_mode(mutex->type))
    { 
        SpinPrivate* p = GetSpinPrivate(mutex);
        bool expected = false;
        // 尝试一次获取锁，不成功则立即返回
        is_lock= XAtomic_compare_exchange_strong_bool(&p->state, &expected, true, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed);
    }
    else 
    {
        is_lock=XMutex_platform_tryLock(XMutex_getPlatformPrivate(mutex));
    }
    if (is_lock)
    {
        RecursivePrivate* recursive = XMutex_getRecursivePrivate(mutex);
        recursive->owner_thread = XThread_currentThreadId();
        recursive->recursive_count = 1;
        return true;
    }
    return false;
}

bool XMutex_tryLockTimeout(XMutex* mutex, uint32_t timeout_ms) 
{
    if (!mutex) return false;
    if (timeout_ms == 0) return XMutex_tryLock(mutex);

    size_t start_time = XDateTime_currentMSecsSinceEpoch();
    while (true)
    {
        if (XMutex_tryLock(mutex))
            return true;
        size_t current_time = XDateTime_currentMSecsSinceEpoch();
        // 处理时间回绕的情况
        if (current_time - start_time >= timeout_ms) 
        {
            return false;
        }
        //if(is_spin_mode(mutex->type))
            XThread_yieldCurrentThread();
    }
}

void XMutex_unlock(XMutex* mutex) {
    if (!mutex) return;
    if (mutex->type & XLock_Recursive)
    {
        RecursivePrivate* recursive = XMutex_getRecursivePrivate(mutex);
        if (recursive->owner_thread == NULL)return;//当前根本没有拥有者
        if (recursive->owner_thread != XThread_currentThreadId())
        {
            // 不是锁的拥有者，不能解锁
            return;
        }

        // 先自减递归计数
        recursive->recursive_count--;

        // 如果递归计数仍大于 0，说明还在嵌套调用中，直接返回
        if (recursive->recursive_count > 0) {
            return;
        }

        // 递归完全退出，清空所属线程
        recursive->owner_thread = 0;
    }
    if (is_spin_mode(mutex->type)) {
        spin_unlock(mutex);
    }
    else {
        XMutex_platform_unlock(XMutex_getPlatformPrivate(mutex));
    }
}

bool XMutex_isRecursive(XMutex* mutex) 
{
    if (!mutex) return false;
    return mutex->type & XLock_Recursive;
}

XLock_Type XMutex_type(XMutex* mutex)
{
    return mutex ? mutex->type : XLock_NonRecursive;
}
