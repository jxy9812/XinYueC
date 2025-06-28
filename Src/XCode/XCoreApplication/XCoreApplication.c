#include "XCoreApplication.h"
#include "XMemory.h"
#include "XHashMap.h"
#include "XEvent.h"
#include "XHash.h"
#include "XEquality.h"
#include "XMutexBase.h"
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
	app->m_eventDispatcher = XHashMap_Create(size_t, XEventDispatcher*,XHash_murmur3_32,XEquality_size_t);
	app->m_mutex=XMutex_create("XCoreApplication");
	app->argc = argc;
	app->argv = argv;
}

XEventDispatcher* XCoreApplication_getEventDispatcher()
{
	XCoreApplication* app=XCoreApplication_create(NULL,NULL);
	if (app == NULL)
		return NULL;
	XMutexBase_lock_base(app->m_mutex);
	//XEventDispatcher* dispatcher=XMapBase_va
	return NULL;
}
