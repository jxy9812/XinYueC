#ifdef WIN32
#include"XTimerWin32TimeSetEvent.h"
#include"XMemory.h"
#include <windows.h>
void VXTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback);
void VXTimerBase_setUserData(XTimerBase* timer, void* userData);
void VXTimerBase_setTimeout(XTimerBase* timer, size_t value);
static void VXTimerBase_setInterval(XTimerWin32TimeSetEvent* timer, size_t value);
void VXTimerBase_out(XTimerBase* timer);
static void VXTimerBase_start(XTimerBase* timer);
static void VXTimerBase_stop(XTimerBase* timer);
static void VXTimerBase_deinit(XTimerBase* timer);
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 定时器回调函数
static void CALLBACK TimerCallbackTimeSetEvent(UINT uID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
	XTimerBase* timer = ((XTimerBase*)dwUser);
	XTimerBase_out_base(timer);
	if (timer->m_singleShot)
	{
		XTimerBase_stop_base(timer);
		if (timer->m_autoDelete)
			XTimerBase_delete_base(timer);
	}
	else if(!((XTimerWin32TimeSetEvent*)timer)->m_twoCb)
	{
		VXTimerBase_stop(timer);
		((XTimerWin32TimeSetEvent*)timer)->m_twoCb = true;
		timer->timerId = timeSetEvent(
			timer->m_interval,           // 触发间隔毫秒
			1,             // 精度1毫秒
			TimerCallbackTimeSetEvent, // 回调函数
			timer,             // 不传递用户数据
			TIME_PERIODIC  // 周期性触发
		);
		timer->m_isRun = true;
	}
}
void VXTimerBase_start(XTimerBase* timer)
{
	//printf("启动定时器\n");
	XTimerBase_stop_base(timer);
	timer->timerId = timeSetEvent(
		timer->m_timeout,           // 触发间隔毫秒
		1,             // 精度1毫秒
		TimerCallbackTimeSetEvent, // 回调函数
		timer,             // 不传递用户数据
		TIME_PERIODIC  // 周期性触发
	);
	timer->m_isRun = true;
	//timer->m_isPeriodic = true;
}
void VXTimerBase_stop(XTimerBase* timer)
{
	//printf("停止定时器\n");
	if (timer->timerId && XTimerBase_isRunning(timer))
	{
		// 关闭定时器
		timeKillEvent(timer->timerId);
		timer->timerId = 0;
		timer->m_isRun = false;
		((XTimerWin32TimeSetEvent*)timer)->m_twoCb = false;
	}
}
void VXTimerBase_deinit(XTimerBase* timer)
{
	VXTimerBase_stop(timer);
}
void VXTimerBase_setInterval(XTimerWin32TimeSetEvent* timer, size_t value)
{
	((XTimerBase*)timer)->m_interval = value;
	if (XTimerBase_isRunning(timer))
	{
		XTimerBase_start_base(timer);
	}
}
XVtable* XTimerWin32TimeSetEvent_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XTIMERWIN32TIMESETEVENT_VTABLE_SIZE)
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
	printf("XTimerWin32TimeSetEvent size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XTimerWin32TimeSetEvent* XTimerWin32TimeSetEvent_create()
{
	XTimerWin32TimeSetEvent* timer = XMemory_malloc(sizeof(XTimerWin32TimeSetEvent));
	if (timer == NULL)
		return NULL;
	XTimerWin32TimeSetEvent_init(timer);
	return timer;
}
void XTimerWin32TimeSetEvent_init(XTimerWin32TimeSetEvent* timer)
{
	if (ISNULL(timer, ""))
		return;
	//初始化父类以外的数据
	memset(((XTimerBase*)timer) + 1, 0, sizeof(XTimerWin32TimeSetEvent) - sizeof(XTimerBase));
	XTimerBase_init(timer, NULL);
	XClassGetVtable(timer) = XTimerWin32TimeSetEvent_class_init();

}
#endif
