#include "XCoreApplication.h"
#include "XMemory.h"
#include "XHashMap.h"
#include "XEvent.h"
#include "XHashFunc.h"
#include "XHashSet.h"
#include "XMutex.h"
#include "XObject.h"
#include "XString.h"
#include "XTimeWheelGroup.h"
#include "XEventLoop.h"
#include "XLockFreeQueue.h"
#include "XThreadData.h"
#include "XThread.h"
#include "XTimer.h"
#include "XMultiPool.h"
#include <assert.h>
#include <string.h>
static XCoreApplication* g_app = NULL; // 全局应用程序实例
bool VXCoreApplication_notify(XObject* receiver, XEvent* e);
static void VXObject_timerEvent(XCoreApplication* app, XEventTimer* event);
static void VXCoreApplication_deinit(XCoreApplication* app);
XVtable* XCoreApplication_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XCoreApplication))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_DEFAULT(XObject_class_init());
    void* table[] = { VXCoreApplication_notify };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXObject_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCoreApplication_deinit);
#if SHOWCONTAINERSIZE
        printf("XCoreApplication size:%d\n", XVtable_size(XClassVtable));
#endif
    return XVTABLE_DEFAULT;
}

XCoreApplication* XCoreApplication_instance() {
    return g_app;
}

XCoreApplication* XCoreApplication_create(int argc, char** argv) {
    // 确保全局实例唯一
    if (g_app != NULL)
        return g_app;

    // 创建并初始化应用程序实例
    XCoreApplication* app = XMemory_malloc(sizeof(XCoreApplication));
    if (!app) return NULL;

    XCoreApplication_init(app, argc, argv);
    Set_Class_MemoryFree(app, XFree);
    return g_app;
}

void XCoreApplication_init(XCoreApplication* app, int argc, char** argv) {
    if (app == NULL)
        return;
    //初始化内存池
    XMultiPool_global();
    //初始化全局时间轮
    //XTimeWheelGroup_global();
    memset(((XObject*)app)+1,0,sizeof(XCoreApplication)-sizeof(XObject));
    // 初始化父类
    XObject_init(app);
    XClassGetVtable(app) = XCoreApplication_class_init();

    // 初始化成员变量
    app->m_argc = argc;
    app->m_argv = argv;
    ((XObject*)app)->m_thread = XThread_createMainThread(app);
    //app->m_eventLoop = XEventLoop_create();
    //app->m_eventLoop = NULL;
    XBitArray_init(&app->m_attribute, XCORE_APPLICATION_ATTRIBUTE_COUNT);


    // 设置全局实例
    g_app = app;
}


void XCoreApplication_setApplicationName(const XString* applicationName)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app||!applicationName)return;
    if (!app->m_applicationName)
        app->m_applicationName = XString_create();
    if(XString_compare(app->m_applicationName, applicationName)!=XCompare_Equality)
    {
        XString_assign(app->m_applicationName, applicationName);
        XCoreApplication_applicationNameChanged_signal(xApp);
    }

}

const XString* XCoreApplication_applicationName(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app?app->m_applicationName:NULL;
}

void XCoreApplication_setApplicationVersion(const XString* version)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !version)return;
    if (!app->m_version)
        app->m_version = XString_create();
    if (XString_compare(app->m_version, version) != XCompare_Equality)
    {
        XString_assign(app->m_version, version);
        XCoreApplication_applicationVersionChanged_signal(xApp);
    }
}

const XString* XCoreApplication_applicationVersion(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_version : NULL;
}

void XCoreApplication_setOrganizationName(const XString* orgName)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !orgName)return;
    if (!app->m_orgName)
        app->m_orgName = XString_create();
    if (XString_compare(app->m_orgName, orgName) != XCompare_Equality)
    {
        XString_assign(app->m_orgName, orgName);
        XCoreApplication_organizationNameChanged_signal(xApp);
    }
}

const XString* XCoreApplication_organizationName(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_orgName : NULL;
}

void XCoreApplication_setOrganizationDomain(const XString* orgDomain)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !orgDomain)return;
    if (!app->m_orgDomain)
        app->m_orgDomain = XString_create();
    if (XString_compare(app->m_orgDomain, orgDomain) != XCompare_Equality)
    {
        XString_assign(app->m_orgDomain, orgDomain);
        XCoreApplication_organizationDomainChanged_signal(xApp);
    }
}

const XString* XCoreApplication_organizationDomain(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_orgDomain : NULL;
}

void XCoreApplication_setAttribute(XCoreApplicationAttribute attribute, bool on)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)return;
    XBitArray_setBit(&app->m_attribute, (size_t)attribute,on);
}

bool XCoreApplication_testAttribute(XCoreApplicationAttribute attribute)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)return false;
    return XBitArray_getBit(&app->m_attribute, (size_t)attribute);
}

XStringList* XCoreApplication_arguments(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)return NULL;
    XStringList* list = XStringList_create();
    for (size_t i = 0; i < app->m_argc; i++)
    {
        XStringList_push_back_utf8(list, app->m_argv[i]);
    }
    return list;
}

const XString* XCoreApplication_applicationDirPath(void)
{
    return NULL;
}

const XString* XCoreApplication_applicationFilePath(void)
{
    return NULL;
}
int64_t XCoreApplication_applicationPid(void)
{
    return 0;
}

void XCoreApplication_exit(int returnCode)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (app)
        XThread_exit(((XObject*)app)->m_thread, returnCode);
}

void XCoreApplication_quit() {
    XCoreApplication* app = XCoreApplication_instance();
    if (app)
        XThread_quit(((XObject*)app)->m_thread);
}

void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags) 
{
    XThreadData* data = XThreadData_current();
    if(data)
        XAbstractEventDispatcher_processEvents_base(data->m_dispatcher, flags);
}

void XCoreApplication_processEventsWithMaxTime(XEventLoopProcessEventsFlags flags, int maxtime)
{
    XTimer* timer = XTimer_create();
    XTimer_setInterval(timer,maxtime);
    XTimer_setSingleShot(timer,true);
    XTimer_start_base(timer);
    while (XTimer_isRunning(timer))
    {
        XCoreApplication_processEvents(flags);
    }
    XTimer_deleteLater(timer);
}

void XCoreApplication_installNativeEventFilter(XAbstractNativeEventFilter* filter)
{
    XAbstractEventDispatcher_installNativeEventFilter(XCoreApplication_eventDispatcher(),filter);
}

void XCoreApplication_removeNativeEventFilter(XAbstractNativeEventFilter* filter)
{
    XAbstractEventDispatcher_removeNativeEventFilter(XCoreApplication_eventDispatcher(), filter);
}

bool XCoreApplication_notify_base(XObject* receiver, XEvent* e)
{
    if (ISNULL(receiver, "") || ISNULL(XClassGetVtable(receiver), ""))
        return false;
    return XClassGetVirtualFunc(XCoreApplication_instance(), EXCoreApplication_Notify, bool(*)(XObject*, XEvent*))(receiver, e);
}
int XCoreApplication_exec() 
{
    XCoreApplication* app = XCoreApplication_instance();
    if (app == NULL )
        return -1;
    int result = XThread_exec(((XObject*)app)->m_thread);
    // 发送即将退出信号
    XCoreApplication_aboutToQuit_signal(app);
    XCoreApplication_processEvents(XEventLoop_AllEvents);//处理事件，保证退出信号可以被调用
    return result;
}

bool XCoreApplication_sendEvent(XObject* receiver, XEvent* event)
{
    if (!receiver || !event) {
        return false;
    }
    // 【核心】将事件分发工作交给 notify
    return XCoreApplication_notify_base(receiver, event);
}

void XCoreApplication_postEvent(XObject* receiver, XEvent* event, int priority)
{
    XThreadData_postEvent(receiver,event,priority);
}

void XCoreApplication_tryPostEvent(XObject* receiver, XEvent* event, int priority)
{
    XThreadData_tryPostEvent(receiver, event, priority);
}

void XCoreApplication_sendPostedEvents(XObject * receiver, XEventType eventType)
{
    size_t size = 0;
    //处理事件
    XVector* events = XThreadData_takePostedEvents();
    if (!events)return;
    for_each_iterator(events, XVector, it)
    {
        XPostEvent* ePost = XVector_iterator_data(&it);
        if (!ePost|| !ePost->event) continue;
        if (receiver && ePost->receiver != receiver)
            continue;//如果有指定的接收者，跳过其他接收者
        if(eventType&& eventType!=ePost->event->type)
            continue;//如果有指定的事件类型，跳过其他事件
        if (XCoreApplication_sendEvent(ePost->receiver, ePost->event))
            ++size;
        ePost->event = NULL;//处理过的事件置空
    }
    //如果有未处理的事件，再次投递到事件队列头部，保证及时处理
    if (size < XVector_size_base(events))
        XThreadData_push_front_list(events);
    XVector_clear_base(events);
    //XVector_delete_base(events);
}

void XCoreApplication_removePostedEvents(XObject * receiver, XEventType eventType)
{
    size_t size = 0;
    //移除事件
    XVector* events = XThreadData_takePostedEvents();
    if (!events)return;
    for_each_iterator(events, XVector, it)
    {
        XPostEvent* ePost = XVector_iterator_data(&it);
        if (!ePost) continue;
        if (receiver && ePost->receiver != receiver)
            continue;//如果有指定的接收者，跳过其他接收者
        if (eventType && eventType != ePost->event->type)
            continue;//如果有指定的事件类型，跳过其他事件
        XEvent_delete_base(ePost->event);
        ++size;
        ePost->event = NULL;//移除的事件置空
    }
    //如果有未处理的事件，再次投递到事件队列头部，保证及时处理
    if (size < XVector_size_base(events))
        XThreadData_push_front_list(events);
    XVector_clear_base(events);
    //XVector_delete_base(events);
}

XAbstractEventDispatcher* XCoreApplication_eventDispatcher(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return ((XObject*)app)->m_thread->m_data->m_dispatcher;
}

void XCoreApplication_setEventDispatcher(XAbstractEventDispatcher* dispatcher)
{
    // 1. 获取全局应用实例和其关联的主线程数据
    XCoreApplication* app = XCoreApplication_instance();
    if (!app) {
        // 如果应用实例不存在，无法设置分发器
        return;
    }

    XThreadData* data = ((XObject*)app)->m_thread->m_data;
    if (!data) {
        // 理论上主线程的 m_data 不应为空，但做安全检查
        return;
    }

    // 2. 参数校验：允许传入 NULL 来清除分发器
    //    但通常不建议这样做，因为事件循环需要一个有效的分发器。
    //    如果传入 NULL，我们只是清理旧的，不设置新的。
    if (dispatcher == NULL) {
        // 可选：打印警告，因为这通常不是一个好主意
         printf("Warning: Setting event dispatcher to NULL.\n");
    }

    // 3. 清理旧的事件分发器
    if (data->m_dispatcher != NULL) {
        // 使用 deleteLater 确保在当前事件循环迭代结束后再销毁，
        // 避免在处理事件时删除正在使用的分发器。
        XObject_deleteLater((XObject*)(data->m_dispatcher));
        data->m_dispatcher = NULL; // 立即置空指针，防止悬空指针
    }

    // 4. 设置新的事件分发器并建立所有权关系
    if (dispatcher != NULL) {
        data->m_dispatcher = dispatcher;

        // --- 关键改进：设置父对象 ---
        // 将新的分发器的父对象设置为应用程序实例。
        // 这样，当应用程序实例被销毁时，分发器也会被自动清理，
        // 防止了内存泄漏，并明确了对象的生命周期。
        XObject_setParent((XObject*)dispatcher, (XObject*)app);
    }

}

void XCoreApplication_setLibraryPaths(const XStringList* paths)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app)return;
    if (!app->m_paths)app->m_paths = XStringList_create();
    XString_copy_base(app->m_paths,paths);
}

const XStringList* XCoreApplication_libraryPaths(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    return app?app->m_paths:NULL;
}

void XCoreApplication_addLibraryPath(const XString* path)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app|| !path)return;
    if (!app->m_paths)app->m_paths = XStringList_create();
    XStringList_push_back_base(app->m_paths, path);
}

void XCoreApplication_removeLibraryPath(const XString * path)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app || !app->m_paths||!path)return;
    XStringList_remove_base(app->m_paths, XStringList_indexOf(app->m_paths, path,0),1);
}

void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app) 
{
    XEmitSignal(app, XCoreApplication_aboutToQuit_signal, NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
}

void* XCoreApplication_applicationNameChanged_signal(XCoreApplication* app)
{
    XEmitSignal(app, XCoreApplication_applicationNameChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCoreApplication_applicationVersionChanged_signal(XCoreApplication* app)
{
    XEmitSignal(app, XCoreApplication_applicationVersionChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCoreApplication_organizationDomainChanged_signal(XCoreApplication* app)
{
    XEmitSignal(app, XCoreApplication_organizationDomainChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCoreApplication_organizationNameChanged_signal(XCoreApplication* app)
{
    XEmitSignal(app, XCoreApplication_organizationNameChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

bool VXCoreApplication_notify(XObject* receiver, XEvent* event)
{
    // 安全检查
    if (!receiver || !event) {
        return false;
    }

    //调用接收者的事件过滤器
    XVector* filters = receiver->filters;
    if (filters)
    {
        for_each_iterator(filters, XVector, it)
        {
            XObject* filter = *((XObject**)XVector_iterator_data(&it));
            if (filter)//调用事件过滤器的过滤方法
                XObject_eventFilter_base(filter, receiver, event);
            if (event->accepted)//如果被处理则不在传播
                goto del;
        }
    }
    if (!event->accepted)
    {//如果还未被接受
        XObject_event_base(receiver, event);
    }
    if (!event->accepted&& XObject_isWidgetType(receiver))
    {//如果还未被接受向上冒泡
        XObject* parent = XObject_parent(receiver);
        if(parent)
        {
            XCoreApplication_postEvent(parent, event, 0);
            return true;
        }
    }
    //释放事件
del:
    if (event->accepted)
    {
        XEvent_delete_base(event);
        return true;
    }
    assert(true,"事件发生内存泄漏\n");
    return false;//事件未被处理
}

//void VXObject_timerEvent(XCoreApplication* app, XEventTimer* event)
//{
//    app->m_quit = true;//XCoreApplication_processEventsWithMaxTime 定时简单处理，如果类中有多个定时就要添加标志位单独处理
//}

void VXCoreApplication_deinit(XCoreApplication* app)
{
    if (!app) return;


    // 释放事件循环
   /* if (app->m_eventLoop)
    {
        XEventLoop_deleteLater(app->m_eventLoop);
        app->m_eventLoop = NULL;
    }*/

    // 释放父类资源
    XClass_Deinit_Parent(XObject, app);

    // 清除全局实例
    if (g_app == app) {
        g_app = NULL;
    }
}
