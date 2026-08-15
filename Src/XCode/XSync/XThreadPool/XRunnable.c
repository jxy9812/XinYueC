#include"XRunnable.h"
#include"XMemory.h"
#include"XVarList.h"
#if XSYNC_ON
#if XTHREADPOOL_ON
/**
 * @brief XRunnable类结构体定义，用于表示可运行的任务
 * @note 继承自XObject，提供类似Qt QRunnable的功能
 */
typedef struct XRunnable
{
	XClass m_class;                    ///< 继承的基类成员
	XCallableToRun function;    //运行的函数
	XVarList* argsList;         //运行的参数
	// QRunnable核心属性
	bool auto_delete;                   ///< 是否自动删除，默认为true
} XRunnable;
// 虚函数实现声明
static void VXRunnable_run(XRunnable* runnable);
static void VXRunnableFunctionWrapper_deinit(XRunnable* runnable);
XVtable* XRunnable_class_init()
{
	XVTABLE_INIT_DEFAULT(XRunnable)
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	void* table[] = {
		VXRunnable_run
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRunnableFunctionWrapper_deinit);

		XCLASS_SHOW_SIZE_DEFAULT(XRunnable);
		return XVTABLE_DEFAULT;
}

void XRunnable_init(XRunnable* runnable)
{
	XAssert(runnable, "runnable is NULL");

	// 初始化基类
	XClassSetVtable(runnable, XRunnable);

	// 初始化成员变量
	runnable->function = NULL;
	runnable->argsList = NULL;
	runnable->auto_delete = true;
}

XRunnable* XRunnable_create_ex(XMemoryType memory)
{
	XRunnable* runnable = (XRunnable*)XMemory_malloc(sizeof(XRunnable), memory);
	if (!runnable)
		return NULL;
	XRunnable_init(runnable);
	Set_Class_Memory(runnable, memory); Set_Class_IsHeap(runnable, true);
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
	//XClass_Deinit_Parent(XClass, runnable);
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
#endif // XTHREADPOOL_ON
#endif /* XSYNC_ON */
