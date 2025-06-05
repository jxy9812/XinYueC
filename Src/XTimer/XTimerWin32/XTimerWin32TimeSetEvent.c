#ifdef WIN32
#include"XTimerWin32TimeSetEvent.h"
#include"XMemory.h"
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 定时器回调函数
static void CALLBACK TimerCallbackTimeSetEvent(UINT uID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
	XTimerBase* timer = ((XTimerBase*)dwUser);
	XTimerBase_out(timer);
}
static void XTimerStartWin32TimeSetEvent(XTimerBase* timer)
{
	//printf("启动定时器\n");
	XTimerBase_stop_base(timer);
	timer->timerId = timeSetEvent(
		timer->m_interval,           // 触发间隔毫秒
		1,             // 精度1毫秒
		TimerCallbackTimeSetEvent, // 回调函数
		timer,             // 不传递用户数据
		TIME_PERIODIC  // 周期性触发
	);
	timer->m_isRun = true;
	//timer->m_isPeriodic = true;
}
static void XTimerStopWin32TimeSetEvent(XTimerBase* timer)
{
	//printf("停止定时器\n");
	if (timer->timerId && XTimerBase_isRunning(timer))
	{
		// 关闭定时器
		timeKillEvent(timer->timerId);
		timer->timerId = 0;
		timer->m_isRun = false;
	}
}
static void XTimerFreeWin32TimeSetEvent(XTimerBase* timer)
{
	XTimerStopWin32TimeSetEvent(timer);
}
static void XTimerSetIntervalWin32TimeSetEvent(XTimerBase* timer, size_t value)
{
	timer->m_interval = value;
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
	//XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = {
		XTimerFreeWin32TimeSetEvent,XTimerStartWin32TimeSetEvent,
		XTimerStopWin32TimeSetEvent,XTimerSetIntervalWin32TimeSetEvent };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
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
	
	//开始初始化
	memset(timer, 0, sizeof(XTimerWin32TimeSetEvent));
	XClassGetVtable(timer) = XTimerWin32TimeSetEvent_class_init();

}
#endif
