#include"XTimer.h"
#include"XThread.h"
#include<string.h>
//static void VXTimerBase_start(XTimer* timer);
static void VXTimerBase_setTimerCallback(XTimer* timer, XTimerBaseCallback callback);
static void VXTimerBase_setUserData(XTimer* timer, void* userData);
static void VXTimerBase_deinit(XTimer* timer);
static void VXTimerBase_out(XTimer* timer);
XVtable* XTimer_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XTIMERWHEEL_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XTimerWheel_class_init());
	//void* table[] = 
	//{

	//};
	////追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimerBase_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXTimerBase_SetTimerCallback, VXTimerBase_setTimerCallback);
	XVTABLE_OVERLOAD_DEFAULT(EXTimerBase_SetUserData, VXTimerBase_setUserData);
	//XVTABLE_OVERLOAD_DEFAULT(EXTimerBase_Out, VXTimerBase_out);
#if SHOWCONTAINERSIZE
	printf("XTimer size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XTimer* XTimer_create()
{
	XTimer* timer = XMemory_malloc(sizeof(XTimer));
	if (timer == NULL)
		return timer;
	XTimer_init(timer);
	return timer;
}
static void TimerCallback(void* userData)
{
	XTimer* timer = userData;
	if (timer->callback)
		timer->callback(NULL);
	XTimer_timeout_signal(timer);
}
void XTimer_init(XTimer* timer)
{
	if (timer == NULL)
		return;
	//初始化父类以外的数据
	memset(((XTimerWheel*)timer) + 1, 0, sizeof(XTimer) - sizeof(XTimerWheel));
	XTimerWheel_init(timer);
	XClassGetVtable(timer) = XTimer_class_init();
	XTimerBase_setTimerGroup(timer, XThread_currentTimerGroup());

	((XTimerBase*)timer)->m_timerCallback = TimerCallback;
	((XTimerBase*)timer)->m_userData = timer;
}

void* XTimer_timeout_signal(XTimer* timer)
{
	if (timer)
		XSignalSlot_emit(((XObject*)timer)->m_signalSlot, XTimer_timeout_signal, NULL);
	return XTimer_timeout_signal;
}

void XTimer_callOnTimeout(XTimer* timer, XObject* receiver, XSlotFunc slot_func, XConnectionType type)
{
	if(timer&& slot_func)
		XObject_connect(timer, XTimer_timeout_signal(NULL), receiver, slot_func, XConnectionType_Auto);
}

void XTimer_singleShot(size_t msec, XObject* receiver, XSlotFunc slot_func, XConnectionType type)
{
	if (msec == 0 || slot_func == NULL)
		return;
	XTimer* timer = XTimer_create();
	XTimer_setTimeout_base(timer, msec);
	XTimerBase_setAutoDelete(timer,true);
	XTimerBase_setSingleShote(timer, true);
	XObject_connect(timer, XTimer_timeout_signal(NULL), receiver, slot_func, type);
	XTimer_start_base(timer);
}

void VXTimerBase_setTimerCallback(XTimer* timer, XTimerBaseCallback callback)
{
	timer->callback = callback;
}

void VXTimerBase_setUserData(XTimer* timer, void* userData)
{
	timer->m_userData = userData;
}

void VXTimerBase_deinit(XTimer* timer)
{
	//调用父类释放函数
	XVtableGetFunc(XTimerWheel_class_init(), EXClass_Deinit, void(*)(XTimerWheel*))(timer);
}

void VXTimerBase_out(XTimer* timer)
{
	//调用父类释放函数
	XVtableGetFunc(XTimerWheel_class_init(), EXTimerBase_Out, void(*)(XTimerWheel*))(timer);
	//XTimer_timeout_signal(timer);
}
