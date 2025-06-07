#include"XTimerWheel.h"
#include"XTimerGroupBase.h"
#include"XMemory.h"
static void VXTimerBase_start(XTimerWheel* timer);
static void VXTimerBase_stop(XTimerWheel* timer);
static void VXTimerBase_setTimeout(XTimerWheel* timer, size_t value);
static void VXTimerBase_setInterval(XTimerWheel* timer, size_t value);
static void VXTimerBase_delete(XTimerWheel* timer);
XVtable* XTimerWheel_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XTIMERWHEEL_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = {
	VXTimerBase_start,VXTimerBase_stop,
	VXTimerBase_setTimeout,VXTimerBase_setInterval

	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Free, VXTimerBase_delete);
#if SHOWCONTAINERSIZE
	printf("XTimerWheel size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXTimerBase_delete(XTimerWheel* timer)
{
	XTimerWheel_stop_base(timer);
	XMemory_free(timer);
}


void VXTimerBase_start(XTimerWheel* timer)
{
	if (timer->m_parent.m_isRun)
		XTimerBase_stop_base(timer);
	if (timer->m_parent.timerId)
		XTimerGroupBase_addTimer_base(timer->m_parent.timerId, timer);
	if (timer->m_list)
		timer->m_parent.m_isRun = true;
	
}

void VXTimerBase_stop(XTimerWheel* timer)
{
	if(timer->m_parent.m_isRun)
	{
		if (timer->m_list)
			XTimerGroupBase_removeTimer_base(timer->m_parent.timerId, timer);
		timer->m_parent.m_isRun = false;
	}
}

void VXTimerBase_setTimeout(XTimerWheel* timer, size_t value)
{
	timer->m_parent.m_timeout = value;
}

void VXTimerBase_setInterval(XTimerWheel* timer, size_t value)
{
	timer->m_parent.m_interval = value;
}

