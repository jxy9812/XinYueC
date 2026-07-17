#include "XThreadData.h"
#include "XThread.h" // XThread_currentThreadId() / XThreadStorage_set/get
#include "XSort.h"
#include "XMutex.h"
#include "XAbstractEventDispatcher.h"
#include "XSemaphore.h"
#include "XCoreApplication.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

// Qt 6.8: 主线程数据 (对标 QCoreApplicationPrivate::theMainThread, QAtomicPointer<QThread>)
// 原子指针: initMainThread 写入(storeRelease), 其它线程读取(loadAcquire)
static XAtomic_uintptr_t main_thread_data = { NULL };

// 类初始化保护 (XVector/XStack/XLockFreeQueue 的 vtable 首次创建需要线程安全)
static XAtomic_uintptr_t classes_init_mutex = { 0 };
static XAtomic_bool classes_initialized = { false };

// ========================
// 类初始化（线程安全,对标 Qt 全局静态初始化）
// ========================
static XMutex* classesInitMutex(void)
{
    XMutex* mutex = (XMutex*)XAtomic_load_uintptr_t(
        &classes_init_mutex, XAtomic_MemoryOrder_Acquire);
    if (mutex)
        return mutex;

    XMutex* candidate = XMutex_create(XLock_NonRecursive);
    if (!candidate)
        return NULL;

    uintptr_t expected = 0;
    if (XAtomic_compare_exchange_strong_uintptr_t(
            &classes_init_mutex, &expected, (uintptr_t)candidate,
            XAtomic_MemoryOrder_AcqRel, XAtomic_MemoryOrder_Acquire)) {
        return candidate;
    }

    XMutex_delete(candidate);
    return (XMutex*)expected;
}

static bool ensure_classes_init(void)
{
    if (XAtomic_load_bool(&classes_initialized, XAtomic_MemoryOrder_Acquire))
        return true;

    XMutex* initMutex = classesInitMutex();
    if (!initMutex)
        return false;

    XMutex_lock(initMutex);
    if (!XAtomic_load_bool(&classes_initialized, XAtomic_MemoryOrder_Relaxed)) {
        bool ok = XVector_class_init()
            && XStack_class_init()
            && XLockFreeQueue_class_init();
        XAtomic_store_bool(&classes_initialized, ok, XAtomic_MemoryOrder_Release);
    }
    bool initialized = XAtomic_load_bool(
        &classes_initialized, XAtomic_MemoryOrder_Acquire);
    XMutex_unlock(initMutex);
    return initialized;
}

// ========================
// 稳定排序：按 priority 降序，同优先级保持原序
// 对标 Qt 6.8: QPostEvent::operator< 使用 std::upper_bound 保持稳定
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
    XThreadData* data = XAlignedMalloc_System(sizeof(XThreadData), CACHE_LINE_SIZE);
    if (!data)
        return NULL;
    XThreadData_init(data,thread);
    return data;
}

void XThreadData_delete(XThreadData* data)
{
    if (!data)return;
    
    if (data->m_eventDispatcher)
    {
        XClass_delete_base(data->m_eventDispatcher);
        data->m_eventDispatcher = NULL;
    }
    /* 清理 m_tryPostEventList 锁-free 队列中残留的事件，防止泄漏 */
    {
        XPostEvent pe;
        while (XLockFreeQueue_receive_base(&data->m_tryPostEventList, &pe))
        {
            if (pe.event)
            {
                if (pe.receiver)
                    XAtomic_fetch_sub_int32(&pe.receiver->m_posted_events, 1,
                                            XAtomic_MemoryOrder_Release);
                pe.event->posted = false;
                XEvent_delete_base(pe.event);
            }
        }
    }
    /* 遍历 m_postEventList 删除残留事件 */
    for_each_iterator(&data->m_postEventList, XVector, it)
    {
        XPostEvent* post = XVector_iterator_data(&it);
        if (post && post->event)
        {
            /* Qt 6.8: 递减接收者的投递事件计数 */
            if (post->receiver)
                XAtomic_fetch_sub_int32(&post->receiver->m_posted_events, 1, XAtomic_MemoryOrder_Release);
            post->event->posted = false;
            XEvent_delete_base(post->event);
        }
    }
    XLockFreeQueue_deinit_base(&data->m_tryPostEventList);
    XVector_deinit_base(&data->m_postEventList);
    XStack_deinit_base(&data->m_activePostEventLists);
    XStack_deinit_base(&data->m_eventLoops);
    if (data->m_wakeSemaphore)
    {
        XSemaphore_delete(data->m_wakeSemaphore);
        data->m_wakeSemaphore = NULL;
    }
    if (data->m_mutex)
    {
        XMutex_delete(data->m_mutex);
        data->m_mutex = NULL;
    }
    XStack_deinit_base(&data->m_senderStack);
    // Qt 6.8: 清理 TLS
    if (data->m_tls)
    {
        XVector_delete_base(data->m_tls);
        data->m_tls = NULL;
    }
    XAlignedFree_System(data);
}

// Qt 6.8: ref() / deref() (对标 QThreadData::ref / deref)
void XThreadData_ref(XThreadData* data)
{
    if (!data) return;
    XAtomic_fetch_add_int32(&data->m_ref, 1, XAtomic_MemoryOrder_Relaxed);
}

void XThreadData_deref(XThreadData* data)
{
    if (!data) return;
    if (XAtomic_fetch_sub_int32(&data->m_ref, 1, XAtomic_MemoryOrder_Release) == 1)
    {
        // 引用计数归零，释放
        XThreadData_delete(data);
    }
}

// Qt 6.8: get2(QThread*) (对标 QThreadData::get2)
XThreadData* XThreadData_get2(XThread* thread)
{
    if (!thread) return NULL;
    return thread->m_data;
}

// Qt 6.8: clearCurrentThreadData() (对标 QThreadData::clearCurrentThreadData)
// 仅清除当前线程 TLS 指针,不 deref (对标 Qt: clear_thread_data -> set_thread_data(nullptr))。
// deref 由调用方单独负责 (对标 Qt QThreadPrivate::cleanup: clearCurrentThreadData 后再 data->deref())。
void XThreadData_clearCurrentThreadData(void)
{
    XThreadStorage_set(NULL);
}

// Qt 6.8: hasEventDispatcher() (对标 QThreadData::hasEventDispatcher)
bool XThreadData_hasEventDispatcher(XThreadData* data)
{
    if (!data) return false;
    return data->m_eventDispatcher != NULL;
}

// Qt 6.8: createEventDispatcher() (对标 QThreadData::createEventDispatcher)
XAbstractEventDispatcher* XThreadData_createEventDispatcher(XThreadData* data)
{
    if (!data) return NULL;
    if (data->m_eventDispatcher) return data->m_eventDispatcher;
    // 平台相关: 由 XEventDispatcher_create 创建
    data->m_eventDispatcher = XEventDispatcher_create(NULL);
    if (data->m_eventDispatcher && data->m_eventDispatcher->d_ptr)
        data->m_eventDispatcher->d_ptr->m_threadData = data;
    return data->m_eventDispatcher;
}

// Qt 6.8: ensureEventDispatcher() (对标 QThreadData::ensureEventDispatcher)
XAbstractEventDispatcher* XThreadData_ensureEventDispatcher(XThreadData* data)
{
    if (!data) return NULL;
    if (data->m_eventDispatcher)
        return data->m_eventDispatcher;
    return XThreadData_createEventDispatcher(data);
}

// Qt 6.8: canWaitLocked() (对标 QThreadData::canWaitLocked)
bool XThreadData_canWaitLocked(XThreadData* data)
{
    if (!data) return false;
    XMutex_lock(data->m_mutex);
    bool result = XAtomic_load_bool(&data->m_canWait, XAtomic_MemoryOrder_Acquire);
    XMutex_unlock(data->m_mutex);
    return result;
}

void XThreadData_init(XThreadData* data, XThread* thread)
{
    if (!data)return;
    memset(data,0,sizeof(XThreadData));
    data->m_mutex = XMutex_create(XLock_NonRecursive);
    data->m_wakeSemaphore = XSemaphore_create(0, 0x7FFFFFFF);
    XLockFreeQueue_init(&data->m_tryPostEventList,sizeof(XPostEvent), TryPostEvent_QueueSize);
    XVector_init(&data->m_postEventList, sizeof(XPostEvent),false);
    XStack_init(&data->m_activePostEventLists, sizeof(XVector*));
    XStack_init(&data->m_eventLoops, sizeof(XEventLoop*));  // Qt 6.8: 事件循环栈
    XStack_init(&data->m_senderStack, sizeof(XSenderFrame));
    data->m_thread = thread;
    XAtomic_init(data->m_loopLevel, 0);
    XAtomic_init(data->m_canWait, true);  // Qt 6.8: 默认可以阻塞等待
    XAtomic_init(data->m_ref, 1);         // Qt 6.8: 初始引用计数 = 1
    data->m_quitNow = false;
    data->m_isAdopted = false;
    data->m_requiresCoreApplication = true;
    data->m_scopeLevel = 0;
    data->m_threadId = XThread_currentThreadId();
    data->m_tls = NULL;
    data->m_eventDispatcher = NULL;
}

// Qt 6.8: current(bool createIfNecessary = true) (对标 QThreadData::current)
// 纯 TLS,无 hash map,无锁 (对标 Qt: get_thread_data + set_thread_data)
XThreadData* XThreadData_current(void)
{
    // Qt 6.8: O(1) TLS 快速路径 (对标 get_thread_data() / thread_local currentThreadData)
    XThreadData* ptr = (XThreadData*)XThreadStorage_get();
    if (ptr)
        return ptr;

    // TLS 未命中: 首次调用或 FreeRTOS(无 TLS)
    if (!ensure_classes_init())
        return NULL;

    // Qt 6.8: 自动创建 adopted 线程数据 (对标 current(true) 的 createIfNecessary)
    XHandle id = XThread_currentThreadId();
    XThreadData* newData = XThreadData_create(NULL);
    if (!newData)
        return NULL;
    newData->m_isAdopted = true;
    newData->m_threadId = id;

    // Qt 6.8: 缓存到 TLS (对标 set_thread_data(data))
    XThreadStorage_set(newData);
    return newData;
}

// Qt 6.8: TLS 析构回调 (对标 destroy_current_thread_data)
// 由平台层 pthread_key 析构函数在线程退出时自动调用 (仅 Posix)。
// 清理 adopted 线程未显式调用 clearCurrentThreadData() 时残留的 XThreadData。
// XThread 管理的线程在 VXThread_run 末尾已 set TLS(NULL),不会触发此回调。
void XThreadData_destroyTlsData(void* p)
{
    XThreadData* data = (XThreadData*)p;
    if (!data)
        return;
    // 纯 deref: TLS 已由 pthreads 自动置 NULL,无需 hash map 操作
    // (对标 Qt destroy_current_thread_data: data->deref())
    XThreadData_deref(data);
}

XThreadData* XThreadData_initMainThread(XThread* thread)
{
    if (!ensure_classes_init())
        return NULL;

    // Qt 6.8: 原子读取主线程数据 (对标 QAtomicPointer::loadAcquire)
    XThreadData* existing = (XThreadData*)XAtomic_load_uintptr_t(
        &main_thread_data, XAtomic_MemoryOrder_Acquire);

    // 已初始化: 更新 m_thread 并返回 (对标 Qt QThread 构造器更新 data->thread)
    if (existing)
    {
        if (thread && !existing->m_thread)
            existing->m_thread = thread;
        XThreadData* data = existing;
        if (data->m_eventDispatcher) {
            XThreadData_ref(data);
            XThreadData* oldTd = (XThreadData*)XAtomic_exchange_uintptr_t(
                &((XObject*)data->m_eventDispatcher)->m_threadData,
                (uintptr_t)data, XAtomic_MemoryOrder_AcqRel);
            XThreadData_deref(oldTd);
        }
        XThreadData_ensureEventDispatcher(data);
        XThreadStorage_set(data);
        return data;
    }

    // Qt 6.8: 复用 TLS 中已有的 adopted 数据,或创建新的 (对标 Qt: current() 已创建则复用)
    XThreadData* data = (XThreadData*)XThreadStorage_get();
    if (data) {
        // 复用 current() 创建的 adopted 数据,绑定到主线程 XThread
        data->m_thread = thread;
        data->m_isAdopted = false;
    } else if (thread) {
        data = XThreadData_create(thread);
        if (!data)
            return NULL;
    }

    // Qt 6.8: 原子存储主线程数据 (对标 QAtomicPointer::storeRelease)
    XAtomic_store_uintptr_t(&main_thread_data, (uintptr_t)data,
        XAtomic_MemoryOrder_Release);

    if (data && thread) {
        if (data->m_eventDispatcher) {
            XThreadData_ref(data);
            XThreadData* oldTd = (XThreadData*)XAtomic_exchange_uintptr_t(
                &((XObject*)data->m_eventDispatcher)->m_threadData,
                (uintptr_t)data, XAtomic_MemoryOrder_AcqRel);
            XThreadData_deref(oldTd);
        }
        XThreadData_ensureEventDispatcher(data);
    }
    XThreadStorage_set(data);
    return data;
}

// Qt 6.8: 返回主线程数据 (对标 QCoreApplicationPrivate::theMainThread, QAtomicPointer)
XThreadData* XThreadData_mainThread(void)
{
    return (XThreadData*)XAtomic_load_uintptr_t(
        &main_thread_data, XAtomic_MemoryOrder_Acquire);
}

// Qt 6.8: 事件循环栈操作 (对标 QThreadData::eventLoops)
void XThreadData_pushEventloop(XThreadData* data, XEventLoop* loop)
{
    if (!data) return;
    XAtomic_fetch_add_size_t(&data->m_loopLevel, 1, XAtomic_MemoryOrder_Relaxed);
    XStack_push_base(&data->m_eventLoops, &loop);
}

void XThreadData_popEventloop(XThreadData* data, XEventLoop* loop)
{
    if (!data) return;
    if (XStack_isEmpty_base(&data->m_eventLoops)) return;

    XEventLoop** top = (XEventLoop**)XStack_top_base(&data->m_eventLoops);
    if (!top || *top != loop) return;

    XStack_pop_base(&data->m_eventLoops);
    XAtomic_fetch_sub_size_t(&data->m_loopLevel, 1, XAtomic_MemoryOrder_Relaxed);
}

// Qt 6.8: canWait 控制
bool XThreadData_canWait(XThreadData* data)
{
    if (!data) return false;
    return XAtomic_load_bool(&data->m_canWait, XAtomic_MemoryOrder_Acquire);
}

// Qt 6.8: 锁定接收者线程的 postEventList (对标 QCoreApplicationPrivate::lockThreadPostEventList)
// 返回目标线程的 XThreadData,调用者已持有 m_mutex 锁
XThreadData* XThreadData_lockPostEventList(XObject* receiver)
{
    if (!receiver) return NULL;

    for (;;) {
        XThreadData* data = (XThreadData*)XAtomic_load_uintptr_t(
            &receiver->m_threadData, XAtomic_MemoryOrder_Acquire);
        if (!data) return NULL;

        XMutex_lock(data->m_mutex);
        XThreadData* data2 = (XThreadData*)XAtomic_load_uintptr_t(
            &receiver->m_threadData, XAtomic_MemoryOrder_Acquire);
        if (data == data2)
            return data;
        XMutex_unlock(data->m_mutex);
    }
}

void XThreadData_postEvent(XObject* receiver, XEvent* event, int priority) 
{
    if (!event)
        return;

    XThreadData* td = XThreadData_lockPostEventList(receiver);
    if (!td)
    {
        XEvent_delete_base(event);
        return;
    }

    // Qt 6.8: 标记事件为已投递 (对标 event->m_posted = true)
    event->posted = true;

    // Qt 6.8: 递增接收者的投递事件计数 (对标 ++receiver->d_func()->postedEvents)
    XAtomic_fetch_add_int32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Relaxed);

    XPostEvent pe = { receiver, event, priority };
    if (!XVector_push_back_1_base(&td->m_postEventList, &pe)) {
        XAtomic_fetch_sub_int32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Relaxed);
        event->posted = false;
        XMutex_unlock(td->m_mutex);
        XEvent_delete_base(event);
        return;
    }

    // Qt 6.8: 设置 canWait = false,表示有新事件,事件循环不应阻塞 (对标 data->canWait = false)
    XAtomic_store_bool(&td->m_canWait, false, XAtomic_MemoryOrder_Release);

    XMutex_unlock(td->m_mutex);

    // Qt 6.8: 唤醒目标线程的事件分发器 (对标 dispatcher->wakeUp())
    if (td->m_eventDispatcher) 
    {
        XAbstractEventDispatcher_wakeUp_base(td->m_eventDispatcher);
    }
}

bool XThreadData_tryPostEvent(XObject* receiver, XEvent* event, int priority)
{
    if (!receiver || !event) return false;

    XThreadData* td = XObject_threadData(receiver);
    if (!td) return false;

    // Qt 6.8: 标记事件为已投递
    event->posted = true;

    // Qt 6.8: 递增接收者的投递事件计数
    XAtomic_fetch_add_int32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Relaxed);

    XPostEvent pe = { receiver, event, priority };
    if(XLockFreeQueue_push_base(&td->m_tryPostEventList, &pe))
    {
        // Qt 6.8: 设置 canWait = false
        XAtomic_store_bool(&td->m_canWait, false, XAtomic_MemoryOrder_Release);

        // 唤醒事件循环
        if (td->m_eventDispatcher) {
            XAbstractEventDispatcher_wakeUp_base(td->m_eventDispatcher);
        }
        return true;
    }
    // 入队失败：回滚计数
    XAtomic_fetch_sub_int32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Release);
    event->posted = false;
    XERROR_PRINTF("XThreadData_tryPostEvent,线程调度器中无锁队列满了,入队失败，count:%d\n",XQueueBase_size_base(&td->m_tryPostEventList));
    return false;
}

void XThreadData_push_front_list(const XVector* events)
{
    if (!events||!XVector_size_base(events))return;
    XThreadData* td = XThreadData_current();
    if (!td) return;

    XMutex_lock(td->m_mutex);
    XPostEvent* posts = (XPostEvent*)XContainerDataAddr(events);
    size_t count = XContainerSize(events);
    bool pushed = false;
    for (size_t i = count; i > 0; --i) {
        XPostEvent* post = &posts[i - 1];
        if (post->event && XVector_insert_1_base(&td->m_postEventList, 0, post, 1)) {
            post->event = NULL;
            pushed = true;
        }
    }
    if (pushed)
        XAtomic_store_bool(&td->m_canWait, false, XAtomic_MemoryOrder_Release);
    XMutex_unlock(td->m_mutex);

    for (size_t i = 0; i < count; ++i) {
        if (posts[i].event)
            XThreadData_discardPostedEvent(&posts[i]);
    }
}

XVector* XThreadData_takePostedEvents(void) 
{
    XThreadData* td = XThreadData_current();
    if (!td) return NULL;
    XVector* local = XVector_create_ex(sizeof(XPostEvent), false);
    if (!local) return NULL;

    XMutex_lock(td->m_mutex);
    bool hasEvents = XContainerSize(&td->m_postEventList) > 0
        || XLockFreeQueue_size_base(&td->m_tryPostEventList) > 0;
    if (hasEvents)
        XVector_swap_base(local, (&td->m_postEventList));
    XAtomic_store_bool(&td->m_canWait, true, XAtomic_MemoryOrder_Release);
    XMutex_unlock(td->m_mutex);

    if (!hasEvents) {
        XVector_delete_base(local);
        return NULL;
    }

    XPostEvent pe = { 0 };
    while (XLockFreeQueue_receive_base(&td->m_tryPostEventList, &pe))
    {
        XVector_push_back_1_base(local, &pe);
    }

    // 关键：稳定降序排序 (对标 Qt 6.8 QPostEventList::addEvent 使用 std::upper_bound)
    if (XContainerSize(local) > 1)
    {
        XInsertSort(XContainerDataAddr(local), XContainerSize(local), XContainerTypeSize(local), stable_sort_post_events_desc, XSORT_DESC);
    }
    return local;
}

bool XThreadData_deliverPostedEvent(XPostEvent* post)
{
    if (!post || !post->event) return false;

    XObject* receiver = post->receiver;
    XEvent* event = post->event;
    post->event = NULL;
    event->posted = false;
    if (receiver)
        XAtomic_fetch_sub_int32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Acquire);

    bool delivered = receiver ? XCoreApplication_sendEvent(receiver, event) : false;
    XEvent_delete_base(event);
    return delivered;
}

void XThreadData_discardPostedEvent(XPostEvent* post)
{
    if (!post || !post->event) return;

    XEvent* event = post->event;
    post->event = NULL;
    event->posted = false;
    if (post->receiver)
        XAtomic_fetch_sub_int32(&post->receiver->m_posted_events, 1, XAtomic_MemoryOrder_Acquire);
    XEvent_delete_base(event);
}

bool XThreadData_pushActivePostedEvents(XVector* events)
{
    XThreadData* data = XThreadData_current();
    return data && events
        && XStack_push_base(&data->m_activePostEventLists, &events);
}

void XThreadData_popActivePostedEvents(XVector* events)
{
    XThreadData* data = XThreadData_current();
    if (!data || XStack_isEmpty_base(&data->m_activePostEventLists))
        return;

    XVector** top = (XVector**)XStack_top_base(&data->m_activePostEventLists);
    if (top && *top == events)
        XStack_pop_base(&data->m_activePostEventLists);
}

void XThreadData_discardActivePostedEvents(XObject* receiver, XEventType eventType)
{
    XThreadData* data = XThreadData_current();
    if (!data) return;

    XVector** activeLists = (XVector**)XContainerDataAddr(&data->m_activePostEventLists);
    size_t activeCount = XContainerSize(&data->m_activePostEventLists);
    for (size_t i = 0; i < activeCount; ++i) {
        XVector* events = activeLists[i];
        if (!events) continue;

        XPostEvent* posts = (XPostEvent*)XContainerDataAddr(events);
        size_t postCount = XContainerSize(events);
        for (size_t j = 0; j < postCount; ++j) {
            XPostEvent* post = &posts[j];
            if (!post->event)
                continue;
            if (receiver && post->receiver != receiver)
                continue;
            if (eventType && post->event->type != eventType)
                continue;
            XThreadData_discardPostedEvent(post);
        }
    }
}

void XThreadData_waitForWake(XThreadData* td, int timeoutMs)
{
    if (!td || !td->m_wakeSemaphore) return;
    if (timeoutMs < 0) timeoutMs = 20;
    XSemaphore_tryAcquireTimeout(td->m_wakeSemaphore, 1, (uint32_t)timeoutMs);
}

void XThreadData_signalWake(XThreadData* td)
{
    if (!td || !td->m_wakeSemaphore) return;
    XSemaphore_release(td->m_wakeSemaphore, 1);
}

//发送者栈:仅当前线程访问,无需加锁
void XThreadData_pushSender(XObject* receiver, XObject* sender, size_t signal)
{
    XThreadData* td = XThreadData_current();
    if (!td) return;  //未注册线程(无事件循环)无法跟踪发送者,sender()将返回 NULL
    XSenderFrame frame = { receiver, sender, signal };
    XStack_push_base(&td->m_senderStack, &frame);
}

void XThreadData_popSender(void)
{
    XThreadData* td = XThreadData_current();
    if (!td || XStack_isEmpty_base(&td->m_senderStack)) return;
    XStack_pop_base(&td->m_senderStack);
}

XObject* XThreadData_currentSender(XObject* receiver)
{
    XThreadData* td = XThreadData_current();
    if (!td) return NULL;
    //XStack 底层为连续缓冲,自顶向下查找首个 receiver 匹配的帧(对标 Qt sender():从栈顶向下遍历)
    XSenderFrame* base = (XSenderFrame*)XContainerDataAddr(&td->m_senderStack);
    int64_t n = (int64_t)XContainerSize(&td->m_senderStack);
    for (int64_t i = n - 1; i >= 0; i--)
    {
        if (base[i].receiver == receiver)
            return base[i].sender;
    }
    return NULL;
}

int XThreadData_currentSenderSignalIndex(XObject* receiver)
{
    XThreadData* td = XThreadData_current();
    if (!td) return -1;
    // Qt 6.8: 从栈顶向下查找首个 receiver 匹配的帧,返回其 signal
    XSenderFrame* base = (XSenderFrame*)XContainerDataAddr(&td->m_senderStack);
    int64_t n = (int64_t)XContainerSize(&td->m_senderStack);
    for (int64_t i = n - 1; i >= 0; i--)
    {
        if (base[i].receiver == receiver)
            return (int)base[i].signal;
    }
    return -1;
}
