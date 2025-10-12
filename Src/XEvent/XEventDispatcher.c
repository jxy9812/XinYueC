#include "XEventDispatcher.h"
#include "XMemory.h"
#include "XHashMap.h"
#include "XHashFunc.h"
#include "XObject.h"
#include "XListSLinked.h"
#include "XWaitCondition.h"
#include "XPriorityMapQueue.h"
#include "XTimerBase.h"
#include "XRecursiveMutex.h"
// 事件回调结构
typedef struct XEventCallback {
    XEventCB callback;             // 回调函数
    void* userData;                // 用户数据
} XEventCallback;
bool sendEvent(XEventDispatcher* dispatcher, XEvent* event);
// 静态函数声明
static void VXEventDispatcher_deinit(XEventDispatcher* dispatcher);
static bool VXEventDispatcher_sendEvent(XEventDispatcher* dispatcher, XEvent* event);
static bool VXEventDispatcher_postEvent(XEventDispatcher* dispatcher, XEvent* event, XEventPriority priority);
static bool VXEventDispatcher_addEventCb(XEventDispatcher* dispatcher, XObject* receiver, int code, XEventCB cb, void* userData);
static bool VXEventDispatcher_removeEventCb(XEventDispatcher* dispatcher, XObject* receiver, int code);
static void VXEventDispatcher_handler(XEventDispatcher* dispatcher);
static bool VXEventDispatcher_registerSocketNotifier(XEventDispatcher* dispatcher, XSocketNotifier* notifier);
static bool VXEventDispatcher_unregisterSocketNotifier(XEventDispatcher* dispatcher, XSocketNotifier* notifier);
static void VXEventDispatcher_wakeUp(XEventDispatcher* dispatcher);
static uint32_t VXEventDispatcher_getSupportedEvents(XEventDispatcher* dispatcher);

/**
 * @brief 初始化事件调度器的虚函数表
 * @return 初始化后的虚函数表
 */
XVtable* XEventDispatcher_class_init() {
    XVTABLE_CREAT_DEFAULT

#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XEventDispatcher))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif

        XVTABLE_INHERIT_DEFAULT(XClass_class_init());

    void* table[] = {
        VXEventDispatcher_sendEvent,
        VXEventDispatcher_postEvent,
        VXEventDispatcher_addEventCb,
        VXEventDispatcher_removeEventCb,
        VXEventDispatcher_handler,
        VXEventDispatcher_registerSocketNotifier,
        VXEventDispatcher_unregisterSocketNotifier,
        VXEventDispatcher_wakeUp,
        VXEventDispatcher_getSupportedEvents
    };

    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXEventDispatcher_deinit);

#if SHOWCONTAINERSIZE
    printf("XEventDispatcher size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif

    return XVTABLE_DEFAULT;
}

/**
 * @brief 创建事件调度器实例
 * @param queueSize 事件队列大小
 * @return 新创建的事件调度器实例
 */
XEventDispatcher* XEventDispatcher_create(size_t queueSize) 
{
    XEventDispatcher* dispatcher = XMemory_malloc(sizeof(XEventDispatcher));
    if (dispatcher) {
        XEventDispatcher_init(dispatcher,queueSize);
    }
    return dispatcher;
}

/**
 * @brief 初始化事件调度器
 * @param dispatcher 要初始化的事件调度器
 * @param queueSize 事件队列大小
 */
void XEventDispatcher_init(XEventDispatcher* dispatcher, size_t queueSize) {
    if (!dispatcher) return;

    XClass_init(&dispatcher->m_class);
    XClassGetVtable(dispatcher) = XEventDispatcher_class_init();

    // 初始化多个优先级事件队列（无锁环形队列）
    dispatcher->m_queue = XPriorityMapQueue_Create(sizeof(int), sizeof(XEvent*),XCompare_int, XSORT_DESC);
    int priority = XEVENT_PRIORITY_NORMAL;
    XPriorityMapQueue_addFifoQueue(dispatcher->m_queue,&priority, queueSize);
    priority = XEVENT_PRIORITY_HIGHEST;
    XPriorityMapQueue_addFifoQueue(dispatcher->m_queue, &priority, queueSize);

    // 初始化事件过滤器映射表
    dispatcher->m_filter_cb = XHashMap_Create(XObject*, XHashMap*,
        XCompare_ptr);

    // 初始化套接字通知器列表
    dispatcher->m_socketNotifiers = XListSLinked_create(sizeof(XSocketNotifier*));

    // 初始化互斥锁
    dispatcher->m_mutex = XRecursiveMutex_create();

    // 初始化事件循环指针
    dispatcher->m_eventLoop = NULL;
}

/**
 * @brief 发送事件（同步处理）
 * @param dispatcher 事件调度器
 * @param event 要发送的事件
 * @return 事件是否被处理
 */
static bool VXEventDispatcher_sendEvent(XEventDispatcher* dispatcher, XEvent* event)
{
    if (!dispatcher || !event) return false;
    bool handled = false;//事件是否被处理
    XMutex_lock(dispatcher->m_mutex);
    handled = sendEvent(dispatcher,event);
    XMutex_unlock(dispatcher->m_mutex);
    return handled;
}
bool sendEvent(XEventDispatcher* dispatcher, XEvent* event)
{
    if (!dispatcher || !event) return false;

    bool isDelete = true;
    bool handled = false;//事件是否被处理
    event->accept = false;

    // 查找事件过滤器
    if (event->receiver) {
        XMapBase** pvCodeMap = XMapBase_value_base(dispatcher->m_filter_cb, &(event->receiver));
        if (pvCodeMap != NULL) {
            XEventCallback* c = XMapBase_value_base(*pvCodeMap, &(event->code));
            if (c != NULL) {
                // 调用回调函数处理事件
                event->userData = c->userData;
                c->callback(event);
                handled = true;
            }

            // 如果事件未被接受，尝试使用通用过滤器
            if (!event->accept) {
                int allCode = XEVENT_ALL;
                XEventCallback* allCallback = XMapBase_value_base(*pvCodeMap, &allCode);
                if (allCallback != NULL) {
                    event->userData = allCallback->userData;
                    allCallback->callback(event);
                    handled = true;
                }
            }
            if (!XEvent_AcceptState(event) && XObject_isEventBubbling(event->receiver) && event->receiver->m_parent)//未被接受事件向上冒泡
            {
                XObject_postEvent(event->receiver->m_parent, event, XEVENT_PRIORITY_NORMAL);//向父对象投递事件
                isDelete = false;
            }
        }
    }
    else {
        // 广播事件到所有接收器
        for_each_iterator(dispatcher->m_filter_cb, XHashMap, it) {
            XMapBase* pvMap = XPair_Second(XHashMap_iterator_data(&it), XMapBase*);
            XEventCallback* c = XMapBase_value_base(pvMap, &(event->code));
            if (c != NULL) {
                event->userData = c->userData;
                c->callback(event);
                handled = true;
                if (event->accept) break;
            }

            // 尝试通用过滤器
            if (!event->accept) {
                int allCode = XEVENT_ALL;
                XEventCallback* allCallback = XMapBase_value_base(pvMap, &allCode);
                if (allCallback != NULL) {
                    event->userData = allCallback->userData;
                    allCallback->callback(event);
                    handled = true;
                    if (event->accept) break;
                }
            }
        }
    }

    
    if (isDelete)
        XEvent_delete(event);
    return handled;
}

/**
 * @brief 投递事件（异步处理）
 * @param dispatcher 事件调度器
 * @param event 要投递的事件
 * @return 事件是否成功加入队列
 */
static bool VXEventDispatcher_postEvent(XEventDispatcher* dispatcher, XEvent* event,XEventPriority priority) {
    if (!dispatcher || !event) return false;

    // 设置时间戳（如果未设置）
    if (event->timestamp == 0) {
        event->timestamp = XTimerBase_getCurrentTime();
    }

    // 确保优先级在有效范围内
    if (priority < 0 || priority >= XEVENT_PRIORITY_COUNT) 
    {
        priority = XEVENT_PRIORITY_NORMAL; // 默认使用正常优先级
    }

    // 将事件放入对应优先级的队列
    int p = priority;
    XMutex_lock(dispatcher->m_mutex);
    bool success = XPriorityMapQueue_push_base(dispatcher->m_queue,&p, &event);
    //接受线程事件数量+1
    if (success&&event->receiver)
        XAtomic_fetch_add_int32(&event->receiver->m_eventCount, 1);  // 原子加1
    XMutex_unlock(dispatcher->m_mutex);
    // 如果成功加入队列，唤醒事件循环
    if (success)
    {
        if(XPriorityMapQueue_size_base(dispatcher->m_queue) == 1)
            VXEventDispatcher_wakeUp(dispatcher);
    }
    else {
        // 队列已满，释放事件
        XMemory_free(event);
    }

    return success;
}

/**
 * @brief 添加事件过滤器
 * @param dispatcher 事件调度器
 * @param receiver 接收对象
 * @param code 事件类型
 * @param cb 事件回调函数
 * @param userData 用户数据
 * @return 是否添加成功
 */
static bool VXEventDispatcher_addEventCb(XEventDispatcher* dispatcher, XObject* receiver,
    int code, XEventCB cb, void* userData) {
    if (!dispatcher || !receiver || !cb) return false;

    XMutex_lock(dispatcher->m_mutex);

    // 查找或创建接收器的事件映射
    XMapBase** pvMap = XMapBase_value_base(dispatcher->m_filter_cb, &receiver);
    XMapBase* p = NULL;

    if (!pvMap) {
        p = XHashMap_Create(int, XEventCallback, XCompare_int);
        XMapBase_insert_base(dispatcher->m_filter_cb, &receiver, &p);
    }
    else {
        p = *pvMap;
    }

    // 添加事件回调
    XEventCallback c = { cb, userData };
    XMapBase_insert_base(p, &code, &c);

    XMutex_unlock(dispatcher->m_mutex);
    return true;
}

/**
 * @brief 移除事件过滤器
 * @param dispatcher 事件调度器
 * @param receiver 接收对象
 * @param code 事件类型
 * @return 是否移除成功
 */
static bool VXEventDispatcher_removeEventCb(XEventDispatcher* dispatcher, XObject* receiver, int code) {
    if (!dispatcher || !receiver) return false;

    XMutex_lock(dispatcher->m_mutex);

    XMapBase** pvMap = XMapBase_value_base(dispatcher->m_filter_cb, &receiver);
    if (!pvMap) {
        XMutex_unlock(dispatcher->m_mutex);
        return false;
    }

    bool result = XMapBase_remove_base(*pvMap, &code);

    // 如果接收器没有更多事件过滤器，移除整个映射
    if (XMapBase_isEmpty_base(*pvMap)) {
        XMapBase_delete_base(*pvMap);
        XMapBase_remove_base(dispatcher->m_filter_cb, &receiver);
    }

    XMutex_unlock(dispatcher->m_mutex);
    return result;
}

/**
 * @brief 处理事件队列中的事件
 * @param dispatcher 事件调度器
 */
static void VXEventDispatcher_handler(XEventDispatcher* dispatcher) 
{
    if (!dispatcher) return;
    
    //处理事件
    XMutex_lock(dispatcher->m_mutex);
    XEvent* event = NULL;
    while (XPriorityMapQueue_receive_base(dispatcher->m_queue, &event))
    {
        if (event)
        {
            if (event->receiver)
                XAtomic_fetch_sub_uint32(&event->receiver->m_eventCount, 1);
            sendEvent(dispatcher, event);
            //XMemory_free(event);
        }
    }
    XMutex_unlock(dispatcher->m_mutex);

    // 按优先级从高到低处理事件队列
   /* for (int i = 0; i < XEVENT_PRIORITY_COUNT; i++) {
        XCircularQueueAtomic* queue = dispatcher->m_queue[XEVENT_PRIORITY_COUNT-1-i];
        while (!XCircularQueueAtomic_isEmpty_base(queue)) {
            XEvent* event = NULL;
            if (XCircularQueueAtomic_receive_base(queue, &event)) {
                if (event) {
                    VXEventDispatcher_sendEvent(dispatcher, event);
                    XMemory_free(event);
                }
            }
        }
    }*/

    // 处理其他任务（如定时器、套接字通知等）
    // ...

    // 处理套接字通知器
    //XListSLinkedNode* node = XListSLinked_first(dispatcher->m_socketNotifiers);
    //while (node) {
    //    XSocketNotifier* notifier = *(XSocketNotifier**)XListSLinkedNode_data(node);
    //    if (notifier && notifier->enabled) {
    //        // 这里应该有平台相关的套接字检查代码
    //        // 简化实现：直接发送事件
    //        XEvent* event = XEvent_create(notifier->receiver, notifier->eventType,
    //            XTimerBase_getCurrentTime(), XEVENT_PRIORITY_NORMAL);
    //        if (event) {
    //            XMutex_unlock(dispatcher->m_mutex);
    //            VXEventDispatcher_sendEvent(dispatcher, event);
    //            XMemory_free(event);
    //            XMutex_lock(dispatcher->m_mutex);
    //        }
    //    }
    //    node = XListSLinkedNode_next(node);
    //}

}

/**
 * @brief 注册套接字通知器
 * @param dispatcher 事件调度器
 * @param notifier 套接字通知器
 * @return 是否注册成功
 */
static bool VXEventDispatcher_registerSocketNotifier(XEventDispatcher* dispatcher, XSocketNotifier* notifier) {
    if (!dispatcher || !notifier) return false;

    XMutex_lock(dispatcher->m_mutex);
    bool result = XListSLinked_push_back_base(dispatcher->m_socketNotifiers, &notifier);
    notifier->enabled = true;
    XMutex_unlock(dispatcher->m_mutex);

    return result;
}

/**
 * @brief 注销套接字通知器
 * @param dispatcher 事件调度器
 * @param notifier 套接字通知器
 * @return 是否注销成功
 */
static bool VXEventDispatcher_unregisterSocketNotifier(XEventDispatcher* dispatcher, XSocketNotifier* notifier) {
    if (!dispatcher || !notifier) return false;

    XMutex_lock(dispatcher->m_mutex);
    bool result = XListSLinked_remove_base(dispatcher->m_socketNotifiers, &notifier);
    notifier->enabled = false;
    XMutex_unlock(dispatcher->m_mutex);

    return result;
}

/**
 * @brief 唤醒事件循环
 * @param dispatcher 事件调度器
 */
static void VXEventDispatcher_wakeUp(XEventDispatcher* dispatcher) {
    if (!dispatcher || !dispatcher->m_eventLoop) return;
    XEventLoop_wakeUp_base(dispatcher->m_eventLoop);
}

/**
 * @brief 获取支持的事件类型
 * @param dispatcher 事件调度器
 * @return 支持的事件类型掩码
 */
static uint32_t VXEventDispatcher_getSupportedEvents(XEventDispatcher* dispatcher) {
    // 返回支持的所有事件类型
    return (1 << XEVENT_TIMEROUT) | (1 << XEVENT_SOCKET) | (1 << XEVENT_FUNC_RUN) | (1 << XEVENT_SLOT_RUN);
}

/**
 * @brief 释放事件调度器资源
 * @param dispatcher 事件调度器
 */
static void VXEventDispatcher_deinit(XEventDispatcher* dispatcher) {
    if (!dispatcher) return;

    // 释放所有事件队列
    //for (int i = 0; i < XEVENT_PRIORITY_COUNT; i++) {
    //    if (dispatcher->m_queue[i]) {
    //        // 清空队列并释放
    //        XCircularQueueAtomic_clear_base(dispatcher->m_queue[i]);
    //        XCircularQueueAtomic_delete_base(dispatcher->m_queue[i]);
    //        dispatcher->m_queue[i] = NULL;
    //    }
    //}
    //先把事件处理完
    if(dispatcher->m_queue)
    {
        VXEventDispatcher_handler(dispatcher);
        XQueueBase_delete_base(dispatcher->m_queue);
        dispatcher->m_queue = NULL;
    }

    // 清理事件过滤器
    if (dispatcher->m_filter_cb) {
        for_each_iterator(dispatcher->m_filter_cb, XHashMap, it) {
            XMapBase* pvMap = XPair_Second(XHashMap_iterator_data(&it), XMapBase*);
            XMapBase_delete_base(pvMap);
        }
        XMapBase_delete_base(dispatcher->m_filter_cb);
        dispatcher->m_filter_cb = NULL;
    }

    // 清理套接字通知器
    if (dispatcher->m_socketNotifiers) {
        XListSLinked_clear_base(dispatcher->m_socketNotifiers);
        XListSLinked_delete_base(dispatcher->m_socketNotifiers);
        dispatcher->m_socketNotifiers = NULL;
    }

    // 销毁互斥锁
    if (dispatcher->m_mutex) {
        XMutex_delete(dispatcher->m_mutex);
        dispatcher->m_mutex = NULL;
    }

    dispatcher->m_eventLoop = NULL;
}

/**
 * @brief 设置关联的事件循环
 * @param dispatcher 事件调度器
 * @param loop 事件循环
 */
void XEventDispatcher_setEventLoop(XEventDispatcher* dispatcher, XEventLoop* loop) {
    if (dispatcher) {
        dispatcher->m_eventLoop = loop;
    }
}

/**
 * @brief 获取关联的事件循环
 * @param dispatcher 事件调度器
 * @return 事件循环
 */
XEventLoop* XEventDispatcher_getEventLoop(XEventDispatcher* dispatcher) {
    return dispatcher ? dispatcher->m_eventLoop : NULL;
}

// 基础函数实现
bool XEventDispatcher_sendEvent_base(XEventDispatcher* dispatcher, XEvent* event) {
    if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
        return false;
    return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_SendEvent,
        bool (*)(XEventDispatcher*, XEvent*))(dispatcher, event);
}

bool XEventDispatcher_postEvent_base(XEventDispatcher* dispatcher, XEvent* event, XEventPriority priority) {
    if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
        return false;
    return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_PostEvent,
        bool (*)(XEventDispatcher*, XEvent*, XEventPriority))(dispatcher, event, priority);
}

bool XEventDispatcher_addEventCb_base(XEventDispatcher* dispatcher, XObject* receiver, int code, XEventCB cb, void* userData) {
    if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
        return false;
    return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_AddEventCb,
        bool (*)(XEventDispatcher*, XObject*, int, XEventCB, void*))(dispatcher, receiver, code, cb, userData);
}
bool XEventDispatcher_object_move(XEventDispatcher* source, XEventDispatcher* dest, XObject* object)
{
    if (!source || !dest || !object) return false;

    XMapBase* p = NULL;
    XMutex_lock(source->m_mutex);
    // 查找或创建接收器的事件映射
    XMapBase** pvMap = XMapBase_value_base(source->m_filter_cb, &object);
    if (pvMap)
    {
        p = *pvMap;
        XMapBase_remove_base(source->m_filter_cb,&p);//从源调度器删除
    }
    XMutex_unlock(source->m_mutex);
    if (p)
    {
        XMutex_lock(dest->m_mutex);
        XMapBase_insert_base(dest->m_filter_cb, &object,&p);//插入新的调度器
        XMutex_unlock(dest->m_mutex);
    }
}

bool XEventDispatcher_removeEventCb_base(XEventDispatcher* dispatcher, XObject* receiver, int code) {
    if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
        return false;
    return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_RemoveEventCb,
        bool (*)(XEventDispatcher*, XObject*, int))(dispatcher, receiver, code);
}

void XEventDispatcher_handler_base(XEventDispatcher* dispatcher) {
    if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
        return;
    XClassGetVirtualFunc(dispatcher, EXEventDispatcher_Handler,
        void (*)(XEventDispatcher*))(dispatcher);
}

bool XEventDispatcher_registerSocketNotifier_base(XEventDispatcher* dispatcher, XSocketNotifier* notifier) {
    if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
        return false;
    return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_RegisterSocketNotifier,
        bool (*)(XEventDispatcher*, XSocketNotifier*))(dispatcher, notifier);
}

bool XEventDispatcher_unregisterSocketNotifier_base(XEventDispatcher* dispatcher, XSocketNotifier* notifier) {
    if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
        return false;
    return XClassGetVirtualFunc(dispatcher, EXEventDispatcher_UnregisterSocketNotifier,
        bool (*)(XEventDispatcher*, XSocketNotifier*))(dispatcher, notifier);
}

void XEventDispatcher_wakeUp_base(XEventDispatcher* dispatcher) {
    if (ISNULL(dispatcher, "") || ISNULL(XClassGetVtable(dispatcher), ""))
        return;
    XClassGetVirtualFunc(dispatcher, EXEventDispatcher_WakeUp,
        void (*)(XEventDispatcher*))(dispatcher);
}