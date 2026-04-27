#include "XThreadData.h"
#include "XThread.h" // 需提供 XThread_currentThreadId()
#include "XSort.h"
#include "XHashMap.h"
#include "XMap.h"
#include "XMutex.h"
#include "XAbstractEventDispatcher.h"
#include "XCoreApplication.h"
#include <stdlib.h>
#include <string.h>

static XHashMap* threadMap = NULL;
static XMutex* mutex = NULL;//互斥锁
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
    XThreadData* data = XMalloc(sizeof(XThreadData));
    XThreadData_init(data,thread);
    return data;
}
//XThreadData* XThreadData_test(XThread* thread)
//{
//    //return NULL;
//    XThreadData* data = XMemory_malloc(sizeof(XThreadData));
//    memset(data, 0, sizeof(XThreadData));
//    //XThreadData_delete(data);
//    //return NULL;
//    return data;
//    data->m_mutex = XMutex_create();
//    data->m_mutex = NULL;
//    //XVector_init(&(data->m_postEventList), sizeof(XPostEvent));
//
//
//    data->m_thread = thread;
//    XAtomic_init(data->m_currentEventLoop, 0);
//    XAtomic_init(data->m_loopLevel, 0);
//    //data->m_dispatcher = XEventDispatcher_create(thread);
//    data->m_dispatcher = NULL;
//    return data;
//}
void XThreadData_delete(XThreadData* data)
{
    if (!data)return;
    
    //XThreadData_mapRemove(data);
    if (data->m_dispatcher)
    {
        XObject_deleteLater(data->m_dispatcher);
        data->m_dispatcher = NULL;
    }
    /*线程结束前已经处理了所有删除事件
    遍历一遍丢弃其他所有事件*/
    for_each_iterator(&data->m_postEventList, XVector, it)
    {
        XPostEvent* post = XVector_iterator_data(&it);
        if (post)
            XEvent_delete_base(post->event);
    }
    XVector_deinit_base(&data->m_postEventList);
    if (data->m_mutex)
    {
        XMutex_delete(data->m_mutex);
        data->m_mutex = NULL;
    }
    XMemory_free(data);
}

void XThreadData_init(XThreadData* data, XThread* thread)
{
    if (!data)return;
    data->m_mutex = XMutex_create();
    XVector_init(&data->m_postEventList,sizeof(XPostEvent));
    //data->m_postEventList=XVector_create(sizeof(XPostEvent));
    data->m_thread = thread;
    XAtomic_init(data->m_currentEventLoop, 0);
    XAtomic_init(data->m_loopLevel, 0);
    data->m_dispatcher = XEventDispatcher_create(thread);

}
XThreadData* XThreadData_current(void) 
{
    if (threadMap == NULL)
        threadMap = XHashMap_Create(XHandle, XThreadData*, size_t_compare);
        //threadMap = XMap_Create(XHandle, XThreadData*, ptr_compare);
    if (mutex == NULL)
        mutex = XMutex_create();
    XHandle id = XThread_currentThreadId();
    XMutex_lock(mutex);
    XThreadData** ptr = XHashMap_value_base(threadMap, &id);
    XMutex_unlock(mutex);
    if (ptr)
        return (*ptr);
    return NULL;
};

XHandle XThreadData_mapInsert(XThreadData* data)
{
    if (data == NULL)return 0;
    XHandle id = XThread_currentThreadId();
    XMutex_lock(mutex);
    //if(XMapBase_isEmpty_base(threadMap))
    XMapBase_insert_base(threadMap, &id, &data);
    XMutex_unlock(mutex);
    return id;
}
void XThreadData_mapRemove(XHandle id)
{
    if (id == 0)return;
    //XHandle id = XThread_currentThreadId();
    XMutex_lock(mutex);
    XMapBase_remove_base(threadMap, &id);
    XMutex_unlock(mutex);
}

XThreadData* XThreadData_initMainThread(XThread* thread)
{
    XMutex_lock(mutex);
    if (threadMap && XMapBase_contains(threadMap, &MainThread))
    {
        XThreadData** ptr = XHashMap_value_base(threadMap, &MainThread);
        XMutex_unlock(mutex);
        if (ptr)
            return (*ptr);
        return NULL;
    }
    XMutex_unlock(mutex);
    XThreadData_current();
    XThreadData* data = XThreadData_create(thread);
    XThreadData_mapInsert(data);
    MainThread = XThread_currentThreadId();

    return data;
}

XEventLoop* XThreadData_currentEventLoop(XThreadData* data)
{
    return XAtomic_load_ptr(&data->m_currentEventLoop);
}

void XThreadData_pushEventloop(XThreadData* data, XEventLoop* loop)
{
    XAtomic_fetch_add_size_t(&data->m_loopLevel,1);
    XAtomic_exchange_ptr(&data->m_currentEventLoop,loop);
}

void XThreadData_popEventloop(XThreadData * data, XEventLoop * loop)
{
    XAtomic_fetch_sub_size_t(&data->m_loopLevel, 1);
    XAtomic_exchange_ptr(&data->m_currentEventLoop, loop);
}

void XThreadData_postEvent(XObject* receiver, XEvent* event, int priority) {
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
    XInsertSort(XContainerDataPtr(local), XContainerSize(local), XContainerTypeSize(local), stable_sort_post_events_desc, XSORT_DESC);
    XMutex_unlock(td->m_mutex);

    // 唤醒事件循环
    if (td->m_dispatcher) {
        XAbstractEventDispatcher_wakeUp_base(td->m_dispatcher);
    }
}

void XThreadData_push_front_list(const XVector* events)
{
    if (!events||!XVector_size_base(events))return;
    XThreadData* td = XThreadData_current();
    //if (!td->m_postEventList)return;
    XVector* temp = XVector_create(sizeof(XPostEvent));
    XVector_resize_base(temp,XVector_size_base(events));
    XVector_clear_base(temp);
    //提取出有效的事件
    for_each_iterator(events, XVector, it)
    {
        XPostEvent* ePost = XVector_iterator_data(&it);
        if (ePost->event)
            XVector_push_back_base(temp,ePost);
    }
    if(XVector_size_base(temp))
    {
        XMutex_lock(td->m_mutex);
        XVector* local = &td->m_postEventList;
        //整个一起插入到头部
        XVector_insert_array_base(local, 0, XContainerDataPtr(temp), XVector_size_base(temp));
        // 关键：稳定降序排序
        XInsertSort(XContainerDataPtr(local), XContainerSize(local), XContainerTypeSize(local), stable_sort_post_events_desc, XSORT_DESC);
        XMutex_unlock(td->m_mutex);
    }
    XVector_delete_base(temp);
}  

XVector* XThreadData_takePostedEvents(void) 
{
    XThreadData* td = XThreadData_current();
    if (!td)return;
    XVector* local = NULL;
    if (!td) return local;
    XMutex_lock(td->m_mutex);
    if (XContainerSize(&td->m_postEventList))
    {
        local = XVector_create(sizeof(XPostEvent));
    }
    else
    {
        XMutex_unlock(td->m_mutex);
        return NULL;//为空的时候直接返回空，生成空副本无意义,提升性能
    }
    //把空的数组交换出来
    if(local)
        XVector_swap_base(local,(&td->m_postEventList));
    XMutex_unlock(td->m_mutex);

    return local;
}

void XThreadData_setEventDispatcher(XAbstractEventDispatcher* dispatcher) {
    XThreadData* td = XThreadData_current();
    if (td) {
        td->m_dispatcher = dispatcher;
    }
}