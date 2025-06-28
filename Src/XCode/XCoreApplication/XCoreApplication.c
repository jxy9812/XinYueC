#include "XCoreApplication.h"
#include "XMemory.h"
#include "XHashMap.h"
#include "XEvent.h"
#include "XHash.h"
#include "XEquality.h"
#include "XMutex.h"
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
		/*	void* table[] = { VXClass_free };
		XVTABLE_ADD_FUNC_LIST_DEFAULT(table);*/

#if SHOWCONTAINERSIZE
		printf("XCoreApplication size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}

XCoreApplication* XCoreApplication_create(int argc, char** argv)
{
	if(g_app!=NULL)
		return g_app;
	XCoreApplication* app = XMemory_malloc(sizeof(XCoreApplication));
	XCoreApplication_init(app, argc,argv);
	return app;
}

void XCoreApplication_init(XCoreApplication* app, int argc, char** argv)
{
	if (app == NULL)
		return;
	app->m_eventDispatcher = XEventDispatcher_createDefault(30);
	app->argc = argc;
	app->argv = argv;
}

XEventDispatcher* XCoreApplication_getEventDispatcher()
{
	XCoreApplication* app=XCoreApplication_create(NULL,NULL);
	if (app == NULL)
		return NULL;
	return app->m_eventDispatcher;
}

int XCoreApplication_exec()
{
	XCoreApplication* app = XCoreApplication_create(NULL, NULL);
	if (app == NULL)
		return -1;
	if (XEventDispatcher_getObjectSize(app->m_eventDispatcher) > 0)
	{
		while (true)
		{
			XEventDispatcher_handler(app->m_eventDispatcher);
		}
	}
	XEventDispatcher_delete(app->m_eventDispatcher);
	return 0;
}
