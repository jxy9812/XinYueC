#include"XTimerWheel.h"
#include"XTimerGroupBase.h"
#include"XMemory.h"
static void VXTimerBase_start(XTimerWheel* timer);
static void VXTimerBase_stop(XTimerWheel* timer);
static void VXTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback);
static void VXTimerBase_setUserData(XTimerBase* timer, void* userData);
static void VXTimerBase_setTimeout(XTimerWheel* timer, size_t value);
static void VXTimerBase_setInterval(XTimerWheel* timer, size_t value);
static void VXTimerBase_deinit(XTimerWheel* timer);
static void VXTimerBase_out(XTimerBase* timer);
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
		XVTABLE_INHERIT_DEFAULT(XObject_class_init());
	void* table[] = {
	VXTimerBase_start,VXTimerBase_stop,VXTimerBase_setTimerCallback,VXTimerBase_setUserData,
	VXTimerBase_setTimeout,VXTimerBase_setInterval,
	VXTimerBase_out
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimerBase_deinit);
#if SHOWCONTAINERSIZE
	printf("XTimerWheel size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXTimerBase_deinit(XTimerWheel* timer)
{
	XTimerWheel_stop_base(timer);
	//调用父类释放函数
	XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(timer);
}

void VXTimerBase_out(XTimerBase* timer)
{
	if (timer == NULL)
		return;
	++timer->number;
	if (timer->m_timerCallback != NULL)
		timer->m_timerCallback(timer->m_userData);
}


void VXTimerBase_start(XTimerWheel* timer)
{
	if (timer->m_parent.m_isRun)
		XTimerBase_stop_base(timer);
	if (timer->m_parent.m_timerGroup)
		XTimerGroupBase_addTimer_base(timer->m_parent.m_timerGroup, timer);
	if (timer->m_list)
		timer->m_parent.m_isRun = true;
	
}

void VXTimerBase_stop(XTimerWheel* timer)
{
	if(timer->m_parent.m_isRun)
	{
		if (timer->m_list)
			XTimerGroupBase_removeTimer_base(timer->m_parent.m_timerGroup, timer);
		timer->m_parent.m_isRun = false;
	}
}

void VXTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback)
{
	timer->m_timerCallback = callback;
}

void VXTimerBase_setUserData(XTimerBase* timer, void* userData)
{
	timer->m_userData = userData;
}

void VXTimerBase_setTimeout(XTimerWheel* timer, size_t value)
{
	timer->m_parent.m_timeout = value;
}

void VXTimerBase_setInterval(XTimerWheel* timer, size_t value)
{
	timer->m_parent.m_interval = value;
}

