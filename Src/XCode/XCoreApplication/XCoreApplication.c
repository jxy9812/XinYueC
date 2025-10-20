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

static XCoreApplication* g_app = NULL; // 全局应用程序实例

XVtable* XCoreApplication_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
#if SHOWCONTAINERSIZE
        printf("XCoreApplication size:%d\n", XVtable_size(XClassVtable));
#endif
    return XVTABLE_DEFAULT;
}

XCoreApplication* XCoreApplication_global() {
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
    return g_app;
}

void XCoreApplication_init(XCoreApplication* app, int argc, char** argv) {
    if (app == NULL)
        return;

    // 初始化父类
    XObject_init(app);
    XClassGetVtable(app) = XCoreApplication_class_init();

    // 初始化成员变量
    app->m_argc = argc;
    app->m_argv = argv;
    app->m_quit = false;
    app->m_eventLoop = XEventLoop_create();

    // 初始化命令行解析器
    app->m_cmdParser = XCommandLineParser_create();

    // 检查关键组件初始化
    if (!app->m_eventLoop || !app->m_cmdParser) {
        // 初始化失败，清理资源
        XEventLoop_delete_base(app->m_eventLoop);
        XCommandLineParser_delete(app->m_cmdParser);
        app->m_eventLoop = NULL;
        app->m_cmdParser = NULL;
        return;
    }

    // 设置全局实例
    g_app = app;

    // 添加事件过滤器
    XObject_addEventFilter(app, XEVENT_SLOT_RUN, XEventSlotFuncRunCB, NULL);
    XObject_addEventFilter(app, XEVENT_FUNC_RUN, XEventFuncRunCB, NULL);
}

void XCoreApplication_delete(XCoreApplication* app) {
    if (!app) return;

    // 释放命令行解析器
    XCommandLineParser_delete(app->m_cmdParser);

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

void XCoreApplication_addOptionGroup(XCoreApplication* app, XCommandLineOptionGroup* group) {
    if (app && app->m_cmdParser) {
        XCommandLineParser_addOptionGroup(app->m_cmdParser, group);
    }
}

int XCoreApplication_optionCount(XCoreApplication* app, const char* option) {
    if (app && app->m_cmdParser) {
        return XCommandLineParser_optionCount(app->m_cmdParser, option);
    }
    return 0;
}

XVector* XCoreApplication_exclusiveGroupConflicts(XCoreApplication* app) {
    if (app && app->m_cmdParser) {
        return XCommandLineParser_exclusiveGroupConflicts(app->m_cmdParser);
    }
    return NULL;
}

void XCoreApplication_setApplicationDescription(XCoreApplication* app, const char* description) {
    if (app && app->m_cmdParser) {
        XCommandLineParser_setApplicationDescription(app->m_cmdParser, description);
    }
}

void XCoreApplication_addCommandLineOption(XCoreApplication* app,
    const char* shortName,
    const char* longName,
    const char* description,
    bool requiresValue,
    bool isHidden,
    const char* defaultValue) {
    if (app && app->m_cmdParser) {
        XCommandLineParser_addOption(app->m_cmdParser, shortName, longName, description,
            requiresValue, isHidden, defaultValue);
    }
}

XEventDispatcher* XCoreApplication_getDispatcher() {
    XCoreApplication* app = XCoreApplication_global();
    return app ? (app->m_eventLoop ? app->m_eventLoop->m_dispatcher : NULL) : NULL;
}

XEventLoop* XCoreApplication_getEventLoop() {
    XCoreApplication* app = XCoreApplication_global();
    return app ? app->m_eventLoop : NULL;
}

XTimerGroupBase* XCoreApplication_getTimerGroup() {
    XCoreApplication* app = XCoreApplication_global();
    if (app == NULL || app->m_eventLoop == NULL)
        return NULL;
    return app->m_eventLoop->m_timerGroup;
}

void XCoreApplication_quit() {
    XCoreApplication* app = XCoreApplication_global();
    if (app) {
        app->m_quit = true;
        XEventLoop_quit_base(app->m_eventLoop, 0);
    }
}

void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags) {
    XCoreApplication* app = XCoreApplication_global();
    if (app && app->m_eventLoop) {
        XEventLoop_processEvents_base(app->m_eventLoop, flags);
    }
}

int XCoreApplication_exec() {
    XCoreApplication* app = XCoreApplication_global();
    if (app == NULL || app->m_eventLoop == NULL)
        return -1;

    app->m_quit = false;
    int result = XEventLoop_exec_base(app->m_eventLoop);

    // 发送即将退出信号
    XCoreApplication_aboutToQuit_signal(app);

    return result;
}

bool XCoreApplication_postSendSignal(void(*sendFunc)(XSignalSlot*, size_t, void*),
    XSignalSlot* signalSlot, size_t signal, void* args, void(*del)(void*),
    XAtomic_int32_t* ref_count, XEventPriority priority) 
{
    XCoreApplication* app = XCoreApplication_global();
    if (!app || !app->m_eventLoop)
        return false;

    return XEventLoop_postSendSignal(app->m_eventLoop, sendFunc, signalSlot, signal,
        args, del,ref_count, priority);
}

bool XCoreApplication_postFunc(XObject* receiver, void(*func)(void*), void* args, void(*del)(void*), XEventPriority priority) {
    XCoreApplication* app = XCoreApplication_global();
    if (!app || !app->m_eventLoop)
        return false;

    return XEventLoop_postFunc(app->m_eventLoop, receiver, func, args, del,priority);
}

bool XCoreApplication_addFd(XObject* object, int fd, XEventType events) {
    XEventLoop* loop = XCoreApplication_getEventLoop();
    return loop ? XEventLoop_addFd(loop, object, fd, events) : false;
}

bool XCoreApplication_removeFd(int fd) {
    XEventLoop* loop = XCoreApplication_getEventLoop();
    return loop ? XEventLoop_removeFd(loop, fd) : false;
}

XCommandLineParser* XCoreApplication_getCommandLineParser(XCoreApplication* app) {
    return app ? app->m_cmdParser : NULL;
}

bool XCoreApplication_parseCommandLine(XCoreApplication* app) {
    if (!app || !app->m_cmdParser || app->m_argc <= 0 || !app->m_argv)
        return false;

    return XCommandLineParser_parse(app->m_cmdParser, app->m_argc, app->m_argv);
}

bool XCoreApplication_hasOption(XCoreApplication* app, const char* option) {
    if (!app || !app->m_cmdParser || !option)
        return false;

    return XCommandLineParser_hasOption(app->m_cmdParser, option);
}

const char* XCoreApplication_getOptionValue(XCoreApplication* app, const char* option) {
    if (!app || !app->m_cmdParser || !option)
        return NULL;

    return XCommandLineParser_getOptionValue(app->m_cmdParser, option);
}

XVector* XCoreApplication_positionalArguments(XCoreApplication* app) {
    if (!app || !app->m_cmdParser)
        return NULL;

    return XCommandLineParser_positionalArguments(app->m_cmdParser);
}

void XCoreApplication_printHelpAndExit(XCoreApplication* app, const char* description) {
    if (!app || !app->m_cmdParser)
        return;

    XString* help = XCommandLineParser_helpText(app->m_cmdParser, description);
    if (help) {
        XPrintf_string(help);
        XString_delete_base(help);
    }
    XCoreApplication_quit();
}

void XCoreApplication_printVersionAndExit(XCoreApplication* app, const char* version) {
    if (!app || !app->m_cmdParser)
        return;

    XString* ver = XCommandLineParser_versionText(app->m_cmdParser, version);
    if (ver) {
        XPrintf_string(ver);
        XString_delete_base(ver);
    }
    XCoreApplication_quit();
}

void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app) 
{
    EmitSignal(app, XCoreApplication_aboutToQuit_signal, NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
}
