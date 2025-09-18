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
// 全局应用程序实例指针
static XCoreApplication* g_app = NULL;

XVtable* XCoreApplication_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
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
	XCoreApplication* app = XMemory_malloc(sizeof(XCoreApplication));
	XCoreApplication_init(app, argc,argv);
	g_app = app;
	return g_app;
}

void XCoreApplication_init(XCoreApplication* app, int argc, char** argv)
{
	if (app == NULL)
		return;
	XClass_init(app);
	XClassGetVtable(app) = XCoreApplication_class_init();
	app->m_argc = argc;
	app->m_argv = argv;
	app->m_quit = false;
	app->m_eventLoop = XEventLoop_create();


	// 初始化定时器组
	XTimerGroupBase* group= XTimerGroupWheel_create(1);
	XTimerGroupWheel_setMutex(group, XMutex_create());
	XTimerGroupWheel_addTimeWheel_base(group, 100);//0~100ms    -1ms
	XTimerGroupWheel_addTimeWheel_base(group, 10);//100ms~1S    -100ms
	XTimerGroupWheel_addTimeWheel_base(group, 10);//1S~10S      -1s
	XTimerGroupWheel_addTimeWheel_base(group, 10);//10~100S     -10s
	XTimerGroupWheel_addTimeWheel_base(group, 10);//100~1000s   -100s

	app->m_timerGroup = group;
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

void XCoreApplication_requestQuit()
{
	XCoreApplication_global()->m_quit = true;
	XEventLoop_quit_base(XCoreApplication_global()->m_eventLoop,0);
}

int XCoreApplication_exec()
{
	XCoreApplication* app = XCoreApplication_global();
	if (app == NULL)
		return -1;
	//准备启动事件循环
	app->m_eventLoop->m_state= XEventLoop_Running;
	while (!(app->m_quit)&& app->m_eventLoop->m_state == XEventLoop_Running)
	{
		// 处理定时器事件
		XTimerGroupWheel_handler_base(app->m_timerGroup);
		XEventLoop_processEvents_base(app->m_eventLoop, XEventLoop_AllEvents);
	}
	return 0;
}
