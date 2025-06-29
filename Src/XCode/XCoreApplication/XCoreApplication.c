#include "XCoreApplication.h"
#include "XMemory.h"
#include "XHashMap.h"
#include "XEvent.h"
#include "XHash.h"
#include "XHashSet.h"
#include "XEquality.h"
#include "XMutex.h"
#include "XObject.h"
#include "XTimerGroupWheel.h"
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
	g_app = XMemory_malloc(sizeof(XCoreApplication));
	XCoreApplication_init(g_app, argc,argv);
	return g_app;
}

void XCoreApplication_init(XCoreApplication* app, int argc, char** argv)
{
	if (app == NULL)
		return;
	XClass_init(app);
	XClassGetVtable(app) = XCoreApplication_class_init();
	app->m_Objects = XHashSet_Create(XObject*,XHash_murmur3_32,XEquality_ptr);
	app->m_argc = argc;
	app->m_argv = argv;
	app->m_quit = false;
	//初始化一些全局类
	XTimerGroupWheel_setGlobal();
}

XEventDispatcher* XCoreApplication_getEventDispatcher()
{
	XCoreApplication* app=XCoreApplication_create(NULL,NULL);
	if (app == NULL)
		return NULL;
	return NULL;
}

void XCoreApplication_requestQuit()
{
	XCoreApplication* app = XCoreApplication_create(NULL, NULL);
	if (app == NULL)
		return ;
	app->m_quit = true;
}

int XCoreApplication_exec()
{
	XCoreApplication* app = XCoreApplication_create(NULL, NULL);
	if (app == NULL)
		return -1;
	if (!XSetBase_isEmpty_base(app->m_Objects))
	{
		XObject* object = NULL;
		while (!(app->m_quit))
		{
			for_each_iterator(app->m_Objects, XHashSet, it)
			{
				object = *((XObject**)XHashSet_iterator_data(&it));
				if (XClassGetVirtualFunc(object, EXObject_Poll, void(*)(XObject*)))
					XObject_poll_base(object);
			}
		}
	}
	return 0;
}

XSetBase* XCoreApplication_getObjects()
{
	XCoreApplication* app = XCoreApplication_create(NULL, NULL);
	if (app == NULL)
		return NULL;
	return app->m_Objects;
}
