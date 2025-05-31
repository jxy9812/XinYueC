#ifdef WIN32
#include"XTimerWin32TimeSetEvent.h"
#include"XMemory.h"
#include <windows.h>
#include "XTimerWin32ThreadpoolTimer.h"
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")

// 定时器回调函数
static VOID CALLBACK TimerCallbackThreadpoolTimer(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_TIMER Timer)
{
	XTimerBase* timer = ((XTimerBase*)Context);
	XTimerBase_out(timer);
}
static void XTimerCreateWin32ThreadpoolTimer(XTimerBase* timer)
{
	//printf("创建定时器\n");
	if (timer == NULL)
		return;
	// 创建线程池计时器
	timer->timerId = CreateThreadpoolTimer(
		TimerCallbackThreadpoolTimer,      // 回调函数
		timer,         // 传递给回调函数的上下文
		NULL                // 使用默认环境
	);
}
static void XTimerStartWin32ThreadpoolTimer(XTimerBase* timer)
{
	//printf("启动定时器\n");
	XTimerBase_stop_base(timer);
	// 设置计时器立即启动，每1000ms触发一次
	if (timer == NULL || timer->timerId == 0)
		return;
	ULONGLONG dueTime = 0; // 立即启动
	DWORD period = timer->m_interval;   // 1000ms = 1秒
	DWORD tolerance = 0;   // 允许的触发时间偏差（毫秒）

	// 转换dueTime为FILETIME格式（相对于1601年1月1日的100纳秒间隔数）
	FILETIME ftDueTime;
	dueTime = -((ULONGLONG)dueTime * 10000); // 转换为100纳秒间隔
	ftDueTime.dwLowDateTime = (DWORD)(dueTime & 0xFFFFFFFF);
	ftDueTime.dwHighDateTime = (DWORD)(dueTime >> 32);

	// 启动计时器
	SetThreadpoolTimer(((PTP_TIMER)(timer->timerId)), &ftDueTime, period, tolerance);
}
static void XTimerStopWin32ThreadpoolTimer(XTimerBase* timer)
{
	//printf("停止定时器\n");
	if (timer->timerId)
	{
		SetThreadpoolTimer(((PTP_TIMER)(timer->timerId)), NULL, 0, 0);
	}
}
static void XTimerFreeWin32ThreadpoolTimer(XTimerBase* timer)
{
	if (timer->timerId)
	{
		// 等待所有回调完成
		WaitForThreadpoolTimerCallbacks(((PTP_TIMER)(timer->timerId)), false);
		// 清理资源
		CloseThreadpoolTimer(((PTP_TIMER)(timer->timerId)));
	}
}
static void XTimerSetIntervalWin32ThreadpoolTimer(XTimerBase* timer, size_t value)
{
	timer->m_interval = value;
	if (XTimerBase_isRunning(timer))
	{
		XTimerBase_start_base(timer);
	}
}
XVtable* XTimerWin32ThreadpoolTimer_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XTIMERBASE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	//XVTABLE_INHERIT_DEFAULT(XClass_class_init());
		void* table[] = { 
		XTimerFreeWin32ThreadpoolTimer,XTimerStartWin32ThreadpoolTimer,
		XTimerStopWin32ThreadpoolTimer,XTimerSetIntervalWin32ThreadpoolTimer };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
#if SHOWCONTAINERSIZE
	printf("XTimerWin32ThreadpoolTimer size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XTimerWin32ThreadpoolTimer* XTimerXTimerWin32ThreadpoolTimer_new()
{
	XTimerWin32ThreadpoolTimer* timer = XMemory_malloc(sizeof(XTimerWin32ThreadpoolTimer));
	if (timer == NULL)
		return NULL;
	XTimerWin32ThreadpoolTimer_class_init(timer);
	XTimerCreateWin32ThreadpoolTimer(timer);
	return timer;
}

void XTimerWin32ThreadpoolTimer_init(XTimerWin32ThreadpoolTimer* timer)
{
	if (ISNULL(timer, ""))
		return;
	//开始初始化
	memset(timer, 0, sizeof(XTimerWin32ThreadpoolTimer));
	XClassGetVtable(timer) = XTimerWin32ThreadpoolTimer_class_init();
}

#endif
