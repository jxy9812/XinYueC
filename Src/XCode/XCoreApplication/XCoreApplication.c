#include "XCoreApplication.h"
#include "XMemory.h"
#include "XHashMap.h"
#include "XEvent.h"
#include "XHashFunc.h"
#include "XHashSet.h"
#include "XMutex.h"
#include "XObject.h"
#include "XString.h"
#include "XTimerGroupWheel.h"
#include "XEventLoop.h"
#include "XCircularQueueAtomic.h"
#include "XThreadData.h"
static XCoreApplication* g_app = NULL; // 全局应用程序实例
bool VXCoreApplication_notify(XObject* receiver, XEvent* e);
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
    SET_CLASS_HEAP(app);
    return g_app;
}

void XCoreApplication_init(XCoreApplication* app, int argc, char** argv) {
    if (app == NULL)
        return;
    XThreadData_initMainThread();
    // 初始化父类
    XObject_init(app);
    XClassGetVtable(app) = XCoreApplication_class_init();

    // 初始化成员变量
    app->m_argc = argc;
    app->m_argv = argv;
    app->m_quit = false;
    app->m_eventLoop = XEventLoop_create();
    XBitArray_init(&app->m_attribute, XCORE_APPLICATION_ATTRIBUTE_COUNT);



    //// 初始化命令行解析器
    //app->m_cmdParser = XCommandLineParser_create();

    //// 检查关键组件初始化
    //if (!app->m_eventLoop || !app->m_cmdParser) {
    //    // 初始化失败，清理资源
    //    XEventLoop_delete_base(app->m_eventLoop);
    //    XCommandLineParser_delete(app->m_cmdParser);
    //    app->m_eventLoop = NULL;
    //    app->m_cmdParser = NULL;
    //    return;
    //}

    // 设置全局实例
    g_app = app;

    // 添加事件过滤器
    //XObject_addEventFilter(app, XEVENT_SLOT_RUN, XEventMetaCall_handler, NULL);
    //XObject_addEventFilter(app, XEVENT_FUNC_RUN, XEventFunc_handler, NULL);
}

void XCoreApplication_delete(XCoreApplication* app) {
    if (!app) return;


    // 释放事件循环
    XEventLoop_delete_base(app->m_eventLoop);

    // 释放父类资源
    XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(app);

    // 清除全局实例
    if (g_app == app) {
        g_app = NULL;
    }

    XMemory_free(app);
}

void XCoreApplication_setApplicationName(const XString* applicationName)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (!app||!applicationName)return;
    if (!app->m_applicationName)
        app->m_applicationName = XString_create();
    XString_assign(app->m_applicationName, applicationName);
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
    XString_assign(app->m_version, version);
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
    XString_assign(app->m_orgName, orgName);
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
    XString_assign(app->m_orgDomain, orgDomain);
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
XEventDispatcher* XCoreApplication_dispatcher() {
    XCoreApplication* app = XCoreApplication_instance();
    return app ? (app->m_eventLoop ? app->m_eventLoop->m_dispatcher : NULL) : NULL;
}

XEventLoop* XCoreApplication_eventLoop() {
    XCoreApplication* app = XCoreApplication_instance();
    return app ? app->m_eventLoop : NULL;
}

int64_t XCoreApplication_applicationPid(void)
{
    return 0;
}

void XCoreApplication_quit() {
    XCoreApplication* app = XCoreApplication_instance();
    if (app) {
        app->m_quit = true;
        XEventLoop_quit(app->m_eventLoop, 0);
    }
}

void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags) {
    XAbstractEventDispatcher_processEvents_base(XThreadData_current()->m_dispatcher, flags);
}

void XCoreApplication_processEventsWithMaxTime(XEventLoopProcessEventsFlags flags, int maxtime)
{

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
    if (app == NULL || app->m_eventLoop == NULL)
        return -1;

    app->m_quit = false;
    int result = XEventLoop_exec(app->m_eventLoop);

    // 发送即将退出信号
    XCoreApplication_aboutToQuit_signal(app);

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

void XCoreApplication_sendPostedEvents(XObject * receiver, XEventType eventType)
{
    //XAbstractEventDispatcher_processEvents_base(XThreadData_initMainThread()->m_dispatcher, XEventLoop_AllEvents);
}

void XCoreApplication_removePostedEvents(XObject * receiver, XEventType eventType)
{

}

XAbstractEventDispatcher* XCoreApplication_eventDispatcher(void)
{
    XCoreApplication* app=XCoreApplication_instance();
    if (!app || !app->m_eventLoop)return NULL;
    return app->m_eventLoop->m_dispatcher;
}

void XCoreApplication_setEventDispatcher(XAbstractEventDispatcher* dispatcher)
{
    //未完成，实际要先停止事件循环
    XThreadData* data = XThreadData_initMainThread();
    if (!data)return;
    if (data->m_dispatcher) XObject_deleteLater(data->m_dispatcher);
    data->m_dispatcher = dispatcher;
    XCoreApplication* app = XCoreApplication_instance();
    if (app && app->m_eventLoop)app->m_eventLoop->m_dispatcher = dispatcher;

}

void XCoreApplication_setLibraryPaths(const XStringList* paths)
{}

const XStringList* XCoreApplication_libraryPaths(void)
{
    return NULL;
}

void XCoreApplication_addLibraryPath(const XString* path)
{}

void XCoreApplication_removeLibraryPath(const XString * path)
{}

void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app) 
{
    XEmitSignal(app, XCoreApplication_aboutToQuit_signal, NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
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
                event->accepted = XObject_eventFilter_base(filter, receiver, event);
            if (event->accepted)//如果被处理则不在传播
                goto del;
        }
    }
    if (!event->accepted)
    {//如果还未被接受
        event->accepted =XObject_event_base(receiver,event);
    }
    if (!event->accepted&& XObject_isWidgetType(receiver))
    {//如果还未被接受向上冒泡
        XObject* parent = XObject_parent(receiver);
        if(parent)
        {
            XCoreApplication_postEvent(parent, event, 0);
            return false;
        }
    }
    //释放事件
del:
    if (event->accepted)
    {
        XEvent_delete_base(event);
        return true;
    }
    return false;//事件未被处理
}
