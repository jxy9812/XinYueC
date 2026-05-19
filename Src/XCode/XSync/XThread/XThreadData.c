#include "XThreadData.h"
#include "XThread.h" // 需提供 XThread_currentThreadId()
#include "XSort.h"
#include "XHashMap.h"
#include "XMap.h"
#include "XMutex.h"
#include "XReadWriteLock.h"
#include "XAbstractEventDispatcher.h"
#include "XCoreApplication.h"
#include <stdlib.h>
#include <string.h>

static XHashMap* global_thread_map = NULL;
static XReadWriteLock* global_lock = NULL;//互斥锁
static size_t MainThread = NULL;//主线程句柄
// ========================
// 稳定排序：按 priority 降序，同优先级保持原序
// 使用插入排序（n 小，稳定，O(n²) 可接受）
// ========================
static int32_t  stable_sort_post_events_desc(const void* a, const void* b) 
{
    int prio_a = ((XPostEvent*)a)->priority;
    int prio_b = ((XPostEvent*)b)->priority;
    if (prio_a > prio_b) return XCompare_Greater;    // a 优先级更高
    if (prio_a < prio_b) return XCompare_Less;       // a 优先级更低
    return XCompare_Equality;                        // 优先级相等

}

// ========================
// 公共 API 实现
// ========================

XThreadData* XThreadData_create(XThread* thread)
{
    XThreadData* data = XMalloc_System(sizeof(XThreadData));
    XThreadData_init(data,thread);
    return data;
}

void XThreadData_delete(XThreadData* data)
{
    if (!data)return;
    
    if (data->m_dispatcher)
    {
        XClass_delete_base(data->m_dispatcher);
        //XObject_deleteLater(data->m_dispatcher);
        data->m_dispatcher = NULL;
    }
    //XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    /*线程结束前已经处理了所有删除事件
    遍历一遍丢弃其他所有事件*/
    for_each_iterator(&data->m_postEventList, XVector, it)
    {
        XPostEvent* post = XVector_iterator_data(&it);
        if (post)
            XEvent_delete_base(post->event);
    }
    XLockFreeQueue_deinit_base(&data->m_tryPostEventList);
    XVector_deinit_base(&data->m_postEventList);
    //XVector_deinit_base(&data->m_handlerEventList);
    if (data->m_mutex)
    {
        XMutex_delete(data->m_mutex);
        data->m_mutex = NULL;
    }
    XFree_System(data);
}

void XThreadData_init(XThreadData* data, XThread* thread)
{
    if (!data)return;
    memset(data,0,sizeof(XThreadData));
    data->m_mutex = XMutex_create(XLock_NonRecursive);
    XLockFreeQueue_init(&data->m_tryPostEventList,sizeof(XPostEvent),30);//中断队列大小按需修改
    XVector_init(&data->m_postEventList, sizeof(XPostEvent),false);
    //XVector_init(&data->m_handlerEventList, sizeof(XPostEvent));
    //data->m_postEventList=XVector_create(sizeof(XPostEvent));
    data->m_thread = thread;
    XAtomic_init(data->m_currentEventLoop, 0);
    XAtomic_init(data->m_loopLevel, 0);
    data->m_dispatcher = NULL;
    //data->m_dispatcher = XEventDispatcher_create(thread);

}
XThreadData* XThreadData_current(void) 
{
    if (global_thread_map == NULL)
        global_thread_map = XHashMap_Create(XHandle, XThreadData*, size_t_compare);
        //threadMap = XMap_Create(XHandle, XThreadData*, uintptr_t_compare);
    if (global_lock == NULL)
        global_lock = XReadWriteLock_create(XLock_NonRecursive);
    XHandle id = XThread_currentThreadId();
    //XMutex_lock(global_lock);
    XReadWriteLock_lockForRead(global_lock);
    XThreadData** lptr = XHashMap_value_base(global_thread_map, &id);
    XThreadData* ptr = lptr ? *lptr : NULL;
    //XMutex_unlock(global_lock);
    XReadWriteLock_unlock(global_lock);
    return ptr;
};

XHandle XThreadData_mapInsert(XThreadData* data)
{
    if (data == NULL)return 0;
    XHandle id = XThread_currentThreadId();
    //XPrintf("尝试写锁id:%d\n", id);
    XReadWriteLock_lockForWrite(global_lock);
    //XPrintf("写锁成功id:%d\n", id);
    XMapBase_insert_base(global_thread_map, &id, &data);
    XReadWriteLock_unlock(global_lock);
    //XPrintf("解锁id:%d\n", id);
    return id;
}
void XThreadData_mapRemove(XHandle id)
{
    if (id == 0)return;
    //XHandle id = XThread_currentThreadId();
    //XPrintf("尝试写锁id:%d\n", id);
    XReadWriteLock_lockForWrite(global_lock);
    //XPrintf("写锁成功id:%d\n", id);
    XMapBase_remove_base(global_thread_map, &id);
    XReadWriteLock_unlock(global_lock);
    //XPrintf("解锁id:%d\n", id);
}

XThreadData* XThreadData_initMainThread(XThread* thread)
{
    XReadWriteLock_lockForRead(global_lock);
    if (global_thread_map && XMapBase_contains(global_thread_map, &MainThread))
    {
        XThreadData** lptr = XHashMap_value_base(global_thread_map, &MainThread);
        XThreadData* ptr = lptr ? *lptr : NULL;
        XReadWriteLock_unlock(global_lock);
        return ptr;
    }
    if (!thread)return;
    XReadWriteLock_unlock(global_lock);
    XThreadData_current();
    XThreadData* data = XThreadData_create(thread);
    XThreadData_mapInsert(data);
    data->m_dispatcher= XEventDispatcher_create(thread);
    MainThread = XThread_currentThreadId();

    return data;
}

XThreadData* XThreadData_mainThread()
{
    return XThreadData_initMainThread(NULL);
}

XEventLoop* XThreadData_currentEventLoop(XThreadData* data)
{
    return XAtomic_load_uintptr_t(&data->m_currentEventLoop, XAtomic_MemoryOrder_Relaxed);
}

void XThreadData_pushEventloop(XThreadData* data, XEventLoop* loop)
{
    XAtomic_fetch_add_size_t(&data->m_loopLevel,1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_exchange_uintptr_t(&data->m_currentEventLoop,loop, XAtomic_MemoryOrder_Relaxed);
}

void XThreadData_popEventloop(XThreadData * data, XEventLoop * loop)
{
    XAtomic_fetch_sub_size_t(&data->m_loopLevel, 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_exchange_uintptr_t(&data->m_currentEventLoop, loop, XAtomic_MemoryOrder_Relaxed);
}

void XThreadData_postEvent(XObject* receiver, XEvent* event, int priority) 
{
    if (!receiver || !event) return;
    
    XThread* th = receiver->m_thread;
    if (!th|| !th->m_data) return;
    XThreadData* td = th->m_data;
   // if (!td->m_postEventList)return;
    XPostEvent pe = { receiver, event, priority };
    
    XMutex_lock(td->m_mutex);
    XVector* local = &td->m_postEventList;
    XVector_push_back_base(local, &pe);
    // 关键：稳定降序排序
    //XInsertSort(XContainerDataPtr(local), XContainerSize(local), XContainerTypeSize(local), stable_sort_post_events_desc, XSORT_DESC);
  
    XMutex_unlock(td->m_mutex);
    // 唤醒事件循环
    if (td->m_dispatcher) 
    {
        //XPrintf("XThread:%d 进行唤醒\n",XThread_currentThreadId() );
        XAbstractEventDispatcher_wakeUp_base(td->m_dispatcher);
    }
    
}

void XThreadData_tryPostEvent(XObject* receiver, XEvent* event, int priority)
{
    if (!receiver || !event) return;

    XThread* th = receiver->m_thread;
    if (!th || !th->m_data) return;
    XThreadData* td = th->m_data;
    XPostEvent pe = { receiver, event, priority };
    XLockFreeQueue_push_base(&td->m_tryPostEventList,&pe);
    // 唤醒事件循环
    if (td->m_dispatcher) {
        XAbstractEventDispatcher_wakeUp_base(td->m_dispatcher);
    }
}

void XThreadData_push_front_list(const XVector* events)
{
    if (!events||!XVector_size_base(events))return;
    XThreadData* td = XThreadData_current();
   
    for_each_iterator(events, XVector, it)
    {
        XPostEvent* ePost = XVector_iterator_data(&it);
        if (ePost->event)
        {
            XMutex_lock(td->m_mutex);
            XVector_push_back_base(&td->m_postEventList, ePost);
            XMutex_unlock(td->m_mutex);
        }
    }
  
    //XVector* temp = XVector_create(sizeof(XPostEvent));
    //XVector_resize_base(temp,XVector_size_base(events));
    //XVector_clear_base(temp);
    ////提取出有效的事件
    //for_each_iterator(events, XVector, it)
    //{
    //    XPostEvent* ePost = XVector_iterator_data(&it);
    //    if (ePost->event)
    //        XVector_push_back_base(temp,ePost);
    //}
    //if(XVector_size_base(temp))
    //{
    //    XMutex_lock(td->m_mutex);
    //    XVector* local = &td->m_postEventList;
    //    //整个一起插入到头部
    //    XVector_insert_array_base(local, 0, XContainerDataPtr(temp), XVector_size_base(temp));
    //    // 关键：稳定降序排序
    //    XInsertSort(XContainerDataPtr(local), XContainerSize(local), XContainerTypeSize(local), stable_sort_post_events_desc, XSORT_DESC);
    //    XMutex_unlock(td->m_mutex);
    //}
    //XVector_delete_base(temp);
}  

XVector* XThreadData_takePostedEvents(void) 
{
    XThreadData* td = XThreadData_current();
    if (!td) return NULL;
    XVector* local = NULL;

    XMutex_lock(td->m_mutex);
    if (XContainer_isEmpty_base(&td->m_postEventList))
    {
        XMutex_unlock(td->m_mutex);
        return NULL;
    }
    size_t count = XContainerSize(&td->m_postEventList) /*+ XLockFreeQueue_size_base(&td->m_tryPostEventList)*/;
    if (count > 0)
    {
        local = XVector_create_ex(sizeof(XPostEvent), false);
        XVector_resize_base(local, count);
        XContainerSize(local) = 0;
    }
    //把空的数组交换出来
    if(local)
        XVector_swap_base(local,(&td->m_postEventList));
    XMutex_unlock(td->m_mutex);

    XPostEvent pe = { 0 };
    while (XLockFreeQueue_receive_base(&td->m_tryPostEventList, &pe))
    {
        XVector_push_back_base(local, &pe);
    }
    // 关键：稳定降序排序
    if(local)
        XInsertSort(XContainerDataPtr(local), XContainerSize(local), XContainerTypeSize(local), stable_sort_post_events_desc, XSORT_DESC);

    return local;
}

void XThreadData_setEventDispatcher(XAbstractEventDispatcher* dispatcher) {
    XThreadData* td = XThreadData_current();
    if (td) {
        td->m_dispatcher = dispatcher;
    }
}