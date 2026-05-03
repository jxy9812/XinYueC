#include"XRunnable.h"
#include"XMemory.h"
#include"XVarList.h"

// 虚函数实现声明
static void VXRunnable_run(XRunnable* runnable);
static void VXRunnableFunctionWrapper_deinit(XRunnable* runnable);
XVtable* XRunnable_class_init()
{
	XVTABLE_CREAT_DEFAULT
		// 虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XRunnable))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	void* table[] = {
		VXRunnable_run
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRunnableFunctionWrapper_deinit);

#if SHOWCONTAINERSIZE
		printf("XRunnable size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
		return XVTABLE_DEFAULT;
}

void XRunnable_init(XRunnable* runnable)
{
	XAssert(runnable, "runnable is NULL");

	// 初始化基类
	XClassSetVtable(runnable, XRunnable);
	Set_Class_MemoryFree(runnable, NULL);

	// 初始化成员变量
	runnable->function = NULL;
	runnable->argsList = NULL;
	runnable->auto_delete = true;
}

XRunnable* XRunnable_create()
{
	XRunnable* runnable = (XRunnable*)XMemory_malloc(sizeof(XRunnable));
	if (!runnable)
		return NULL;
	XRunnable_init(runnable);
	Set_Class_MemoryFree(runnable, XMemory_free);
	return runnable;
}

void XRunnable_run_base(XRunnable* runnable)
{
	XAssert(runnable, "runnable is NULL");
	XClassGetVirtualFunc(runnable, EXRunnable_Run, void(*)(XRunnable*))(runnable);
}

bool XRunnable_autoDelete(const XRunnable* runnable)
{
	XAssert(runnable, "runnable is NULL");
	return runnable->auto_delete;
}

void XRunnable_setAutoDelete(XRunnable* runnable, bool autoDelete)
{
	XAssert(runnable, "runnable is NULL");
	runnable->auto_delete = autoDelete;
}

// 虚函数实现
void VXRunnable_run(XRunnable* runnable)
{
	//XAssert(runnable, "runnable is NULL");

	if (runnable->function)
	{
		runnable->function(runnable->argsList);
	}
}


void VXRunnableFunctionWrapper_deinit(XRunnable* runnable)
{
	//XAssert(runnable, "runnable is NULL");

	if (runnable->argsList)
	{
		XVarList_delete(runnable->argsList);
		runnable->argsList = NULL;
	}

	// 调用父类析构
	XClass_Deinit_Parent(XRunnable, runnable);
}

XRunnable* XRunnable_create_from_function(XCallableToRun function, XVarList* argsList, bool auto_delete)
{
	if (!function)
		return NULL;
	XRunnable* wrapper = XRunnable_create();
	// 初始化成员
	wrapper->function = function;
	wrapper->argsList = argsList;
	((XRunnable*)wrapper)->auto_delete = auto_delete;

	return (XRunnable*)wrapper;
}