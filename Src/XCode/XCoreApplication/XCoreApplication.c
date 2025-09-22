#include "XCoreApplication.h"
#include "XMemory.h"
#include "XHashMap.h"
#include "XEvent.h"
#include "XHashFunc.h"
#include "XHashSet.h"
#include "XMutex.h"
#include "XObject.h"
#include "XTimerGroupWheel.h"
#include "XEventLoop.h"
#include "XCircularQueueAtomic.h"
//typedef struct XSignalSlot XEventData;
// 全局应用程序实例指针
static XCoreApplication* g_app = NULL;

XVtable* XCoreApplication_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		/*	void* table[] = { VXClass_delete };
		XVTABLE_ADD_FUNC_LIST_DEFAULT(table);*/

#if SHOWCONTAINERSIZE
		printf("XCoreApplication size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}

XCoreApplication* XCoreApplication_global()
{
	return g_app;
}

XCoreApplication* XCoreApplication_create(int argc, char** argv)
{
	if(g_app!=NULL)
		return g_app;
	XCoreApplication_init(XMemory_malloc(sizeof(XCoreApplication)), argc,argv);
	return g_app;
}

void XCoreApplication_init(XCoreApplication* app, int argc, char** argv)
{
	if (app == NULL)
		return;
	XObject_init(app);
	XClassGetVtable(app) = XCoreApplication_class_init();

	app->m_argc = argc;
	app->m_argv = argv;
	app->m_quit = false;
	app->m_eventLoop = XEventLoop_create();
	app->m_postQueue = app->m_eventLoop->m_postQueue;

	// 初始化定时器组
	XTimerGroupBase* group= XTimerGroupWheel_create(1);
	XTimerGroupWheel_setMutex(group, XMutex_create());
	XTimerGroupWheel_addTimeWheel_base(group, 100);//0~100ms    -1ms
	XTimerGroupWheel_addTimeWheel_base(group, 10);//100ms~1S    -100ms
	XTimerGroupWheel_addTimeWheel_base(group, 10);//1S~10S      -1s
	XTimerGroupWheel_addTimeWheel_base(group, 10);//10~100S     -10s
	XTimerGroupWheel_addTimeWheel_base(group, 10);//100~1000s   -100s
	app->m_timerGroup = group;
	app->m_eventLoop->m_timerGroup = group;

	g_app = app;
	XObject_addEventFilter(app, XEVENT_SLOT_RUN, XEventSlotFuncRunCB, NULL);//绑定信号与槽的功能
	XObject_addEventFilter(app, XEVENT_FUNC_RUN, XEventFuncRunCB, NULL);//绑定函数运行功能
}

XEventDispatcher* XCoreApplication_getDispatcher()
{
	XCoreApplication* app = XCoreApplication_global();
	return app ? (app->m_eventLoop? app->m_eventLoop->m_dispatcher:NULL) : NULL;
}

XEventLoop* XCoreApplication_getEventLoop()
{
	XCoreApplication* app = XCoreApplication_global();
	return app?app->m_eventLoop:NULL;
}

XTimerGroupBase* XCoreApplication_getTimerGroup()
{
	XCoreApplication* app = XCoreApplication_global();
	if (app == NULL || app->m_eventLoop == NULL)
		return NULL;
	return app->m_timerGroup;
}

void XCoreApplication_quit()
{
	XCoreApplication_global()->m_quit = true;
	XEventLoop_quit_base(XCoreApplication_global()->m_eventLoop,0);
}

void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags)
{
	XCoreApplication* app = XCoreApplication_global();
	if(app)
		XEventLoop_processEvents_base(app->m_eventLoop, flags);
}

int XCoreApplication_exec()
{
	XCoreApplication* app = XCoreApplication_global();
	if (app == NULL)
		return -1;
	app->m_quit = false;
	//准备启动事件循环
	if(!(app->m_quit))
	{
		return XEventLoop_exec_base(app->m_eventLoop);
	}
	return 0;
}
bool XCoreApplication_postSendSignal(void(*sendFunc)(XSignalSlot*, size_t, void*), XSignalSlot* signalSlot, size_t signal, void* args, XEventPriority priority)
{
	XCoreApplication* app = XCoreApplication_global();
	if (!app || app->m_postQueue == NULL)
		return false;
	return XEventLoop_postSendSignal(app->m_eventLoop, sendFunc,signalSlot,signal,args,priority);
}

bool XCoreApplication_postFunc(XObject* receiver, void(*func)(void*), void* args, XEventPriority priority)
{
	XCoreApplication* app = XCoreApplication_global();
	if (!app || app->m_postQueue == NULL)
		return false;
	return XEventLoop_postFunc(app->m_eventLoop, receiver, func, args,priority);
}

void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app)
{
	if (app)
		XObject_emitSignal(app, XCoreApplication_aboutToQuit_signal,NULL,XEVENT_PRIORITY_LOWEST);
	return XCoreApplication_aboutToQuit_signal;
}
