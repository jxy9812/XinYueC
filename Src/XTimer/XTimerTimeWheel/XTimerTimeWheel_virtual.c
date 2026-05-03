#include"XTimerTimeWheel.h"
#include"XTimerGroupBase.h"
#include"XCoreApplication.h"
#include"XMemory.h"
static void VXTimerBase_start(XTimerTimeWheel* timer);
static void VXTimerBase_stop(XTimerTimeWheel* timer);
static void VXTimerBase_deinit(XTimerTimeWheel* timer);

XVtable* XTimerTimeWheel_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XTIMERTIMEWHEEL_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XObject_class_init());
	void* table[] = {
	VXTimerBase_start,VXTimerBase_stop
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimerBase_deinit);
#if SHOWCONTAINERSIZE
	printf("XTimerTimeWheel size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXTimerBase_deinit(XTimerTimeWheel* timer)
{
	//先设置不自动释放，否则可能在stop的时候自动释放导致指针越界
	//XTimerBase_setAutoDelete(timer,false);
	//XTimerTimeWheel_stop_base(timer);
	//调用父类释放函数
	XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(timer);
}

void VXTimerBase_start(XTimerTimeWheel* timer)
{
	if (timer->m_class.m_isRun)
		XTimerBase_stop_base(timer);
	//bool is_run = false;
	//if (!XObject_parent(timer))//如果没设置使用全局区的时间轮组管理
	//	is_run=XTimerTimeWheel_setGroup(timer,XCoreApplication_getTimerGroup());
	if(XTimerTimeWheel_group(timer))
		XTimerGroupBase_addTimer_base(XTimerTimeWheel_group(timer), timer);
	/*if (is_run)
		timer->m_class.m_isRun = true;*/
	
}

void VXTimerBase_stop(XTimerTimeWheel* timer)
{
	if(timer->m_class.m_isRun)
	{
		if (timer->m_list)
			XTimerGroupBase_removeTimer_base(XObject_parent(timer), timer);
	}
}



