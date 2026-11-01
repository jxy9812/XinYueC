	#include"XRunnable.h"
#include<string.h>
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
	XCallableToRun m_function;  //运行的函数
	XVarList* m_argsList;       //运行的参数
	// QRunnable核心属性
	bool m_autoDelete;                  ///< 是否自动删除，默认为true
} XRunnable;
// 虚函数实现声明
static void VXRunnable_run(XRunnable* runnable);
static void VXRunnable_deinit(XRunnable* runnable);
// 虚函数实现
void VXRunnable_run(XRunnable* runnable)
{
	//XAssert(runnable, "runnable is NULL");

	if (runnable->m_function)
	{
		runnable->m_function(runnable->m_argsList);
	}
}


void VXRunnable_deinit(XRunnable* runnable)
{
	if (!runnable) return;

	if (runnable->m_argsList)
	{
		XVarList_delete(runnable->m_argsList);
		runnable->m_argsList = NULL;
	}

	// 调用父类析构
	//XClass_Deinit_Parent(XClass, runnable);
}

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

	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRunnable_deinit);

		XCLASS_SHOW_SIZE_DEFAULT(XRunnable);
		return XVTABLE_DEFAULT;
}

void XRunnable_init(XRunnable* runnable)
{
	XAssert(runnable, "runnable is NULL");

	// 清零内存
	memset(runnable, 0, sizeof(XRunnable));
	// 初始化基类
	XClass_init(runnable);
	XClassSetVtable(runnable, XRunnable);

	// 初始化成员变量
	runnable->m_function = NULL;
	runnable->m_argsList = NULL;
	runnable->m_autoDelete = true;
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

XRunnable* XRunnable_create_from_function(XCallableToRun function, XVarList* argsList, bool auto_delete)
{
	if (!function)
		return NULL;
	XRunnable* wrapper = XRunnable_create();
	if (!wrapper)
		return NULL;
	// 初始化成员
	wrapper->m_function = function;
	wrapper->m_argsList = argsList;
	((XRunnable*)wrapper)->m_autoDelete = auto_delete;

	return (XRunnable*)wrapper;
}

void XRunnable_run_base(XRunnable* runnable)
{
	if (ISNULL(runnable, "") || ISNULL(XClassGetVtable(runnable), ""))
		return;
	XClassGetVirtualFunc(runnable, EXRunnable_Run, void(*)(XRunnable*))(runnable);
}

bool XRunnable_autoDelete(const XRunnable* runnable)
{
	XAssert(runnable, "runnable is NULL");
	return runnable->m_autoDelete;
}

void XRunnable_setAutoDelete(XRunnable* runnable, bool autoDelete)
{
	XAssert(runnable, "runnable is NULL");
	runnable->m_autoDelete = autoDelete;
}
#endif // XTHREADPOOL_ON
#endif /* XSYNC_ON */
