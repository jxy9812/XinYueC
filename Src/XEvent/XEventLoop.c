#include "XEventLoop.h"
#include "XAbstractEventDispatcher.h"
#include "XEvent.h"
#include "XMemory.h"
#include "XTimer.h"
#include "XQueueBase.h"
#include "XThread.h"
#include "XThreadData.h"
#include "XCoreApplication.h"
#include "XLockFreeQueue.h"
#include "XTimerGroupWheel.h"
#include "XAtomic.h"
//投递类型
typedef enum PostType
{
    Post_SignalSlot,
    //Post_Event,
    Post_Func,
}PostType;
//投递的数据
typedef struct PostData
{
    PostType type;
    union
    {
        struct 
        {
            XSignalSlot* signalSlot;//控制区分是发送信号函数投递函数执行
            void (*sendSignalFunc)(XSignalSlot*, size_t, void*, XAtomic_int32_t*, XEventPriority);//发送信号的函数
            size_t signal;
        };
        struct
        {
            void (*run_func)(void* args);//需要被执行的函数
            XObject* object;
        };
    };
    void* args;
    void(*del)(void*);              // 参数释放方式
    XAtomic_int32_t* ref_count;//参数引用次数
    XEventPriority priority;
}PostData;//

static void VXEventLoop_deinit(XEventLoop* loop);

/**
 * @brief 初始化事件循环类的虚函数表
 * @return 初始化后的虚函数表
 */
XVtable* XEventLoop_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XEventLoop))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_DEFAULT(XObject_class_init());

    //void* table[] = {
    // 
    //};

    //XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXEventLoop_deinit);

#if SHOWCONTAINERSIZE
    printf("XEventLoop size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

/**
 * @brief 创建事件循环实例
 * @param dispatcher 关联的事件调度器，NULL则创建默认调度器
 * @return 新创建的事件循环实例
 */
XEventLoop* XEventLoop_create() {
    XEventLoop* loop = XMemory_malloc(sizeof(XEventLoop));
    XEventLoop_init(loop);
    Set_Class_MemoryFree(loop, XFree);
    return loop;
}

/**
 * @brief 初始化事件循环
 * @param loop 要初始化的事件循环
 * @param dispatcher 关联的事件调度器，NULL则创建默认调度器
 */
void XEventLoop_init(XEventLoop* loop)
{
    if (loop == NULL) return;

    XObject_init(&loop->m_class);
    XClassGetVtable(loop) = XEventLoop_class_init();

    // 初始化互斥锁和条件变量
    //loop->m_mutex = XMutex_create();
    loop->m_deley = NULL;
    //线程级别的事件循环
   
    loop->m_dispatcher= XThread_currentDispatcher();
    loop->m_state = XEventLoop_Suspended;
    loop->m_exitCode = 0;
   
}

void XEventLoop_delay(size_t msec)
{
   /* XEventLoop* loop = XThread_currentEventLoop();
    if (loop == NULL)
        return;
    XTimer* timer = loop->m_deley;
    if(loop->m_deley==NULL)
    {
        timer = XTimer_create();
        loop->m_deley = timer;
        XTimer_setAutoDelete(timer, false);
        XTimer_setSingleShot(timer, true);
        XObject_connect1(timer, XSignal(XTimer_timeout_signal), loop, XEventLoop_quit, XConnectionType_Auto);
    }
 
    XTimer_setTimeout(timer,msec);
    XTimer_setInterval(timer, msec);
    XTimer_start_base(timer);
    XEventLoop_exec(loop);*/
}

/**
 * @brief 启动事件循环
 * @param loop 事件循环实例
 * @return 退出代码
 */
int XEventLoop_exec(XEventLoop* loop) {
    if (!loop || loop->m_state == XEventLoop_Running) return -1;

    loop->m_state = XEventLoop_Running;
    loop->m_exitCode = 0;
    XThreadData* data = XThreadData_current();
    if (!data)return -1;
    XEventLoop* parent = XThreadData_currentEventLoop(data);
    XThreadData_pushEventloop(data,loop);
    while (loop->m_state == XEventLoop_Running)
    {
        // 处理事件
        XEventLoop_processEvents(loop, XEventLoop_AllEvents);
    }
    XThreadData_popEventloop(data,parent);
   
    return loop->m_exitCode;
}

/**
 * @brief 退出事件循环
 * @param loop 事件循环实例
 * @param exitCode 退出代码
 */
void XEventLoop_exit(XEventLoop* loop, int exitCode) {
    if (!loop) return;

    //XMutex_lock(loop->m_mutex);
    loop->m_state = XEventLoop_Quit;
    loop->m_exitCode = exitCode;
    //XMutex_unlock(loop->m_mutex);
}

void XEventLoop_quit(XEventLoop* loop)
{
    XEventLoop_exit(loop,0);
}

/**
 * @brief 唤醒事件循环
 * @param loop 事件循环实例
 */
void XEventLoop_wakeUp(XEventLoop* loop) {
    if (!loop) return;

    //XMutex_lock(loop->m_mutex);
    XAbstractEventDispatcher_wakeUp_base(loop->m_dispatcher);
    //XMutex_unlock(loop->m_mutex);
}

/**
 * @brief 处理当前待处理的事件
 * @param loop 事件循环对象
 * @param flags 事件处理标志
 */
void XEventLoop_processEvents(XEventLoop* loop, XEventLoopProcessEventsFlags flags)
{
    if (!loop || !loop->m_dispatcher /*|| loop->m_state != XEventLoop_Running*/) return;
    // 处理事件队列中的所有事件
    for (size_t i = 0; i < 1; i++)
    {
        if (XAbstractEventDispatcher_processEvents_base(loop->m_dispatcher, flags))
            return;

    }
    // 2. 【关键】如果没有事件，才考虑休眠
    XAbstractEventDispatcher_processEvents_base(loop->m_dispatcher, XEventLoop_WaitForMoreEvents); // 
}
/**
 * @brief 释放事件循环资源
 * @param loop 事件循环实例
 */
static void VXEventLoop_deinit(XEventLoop* loop)
{
    if (!loop) return;
    // 释放父对象
    XClass_Deinit_Parent(XObject, loop);
}