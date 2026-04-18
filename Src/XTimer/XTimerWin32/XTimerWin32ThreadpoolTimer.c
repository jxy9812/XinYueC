#ifdef WIN32
#include"XMemory.h"
#include <windows.h>
#include "XTimerWin32ThreadpoolTimer.h"
static void VXTimerBase_start(XTimerBase* timer);
static void VXTimerBase_stop(XTimerBase* timer);
static void VXTimerBase_deinit(XTimerBase* timer);
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")

// 定时器回调函数
static VOID CALLBACK TimerCallbackThreadpoolTimer(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_TIMER Timer)
{
	XTimerBase* timer = ((XTimerBase*)Context);
	XTimerBase_out(timer);
	if (timer->m_isSingleShot)
	{
		XTimerBase_stop_base(timer);
		if (((XTimerBase*)timer)->m_autoDelete)
			XObject_deleteLater(timer);
	}
	else if (!((XTimerWin32ThreadpoolTimer*)timer)->m_twoCb)
	{
		VXTimerBase_stop(timer);
		((XTimerWin32ThreadpoolTimer*)timer)->m_twoCb = true;
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
		timer->m_isRun = true;
	}
}
void VXTimerBase_start(XTimerBase* timer)
{
	//printf("启动定时器\n");
	XTimerBase_stop_base(timer);
	// 设置计时器立即启动，每1000ms触发一次
	if (timer == NULL || timer->timerId == 0)
		return;
	// 1. 提高系统定时器分辨率（关键：减少基础时钟偏差）
	UINT desiredResolution = 1;  // 1ms分辨率（范围1~1000）
	timeBeginPeriod(desiredResolution);

	// 2. 配置定时器参数
	ULONGLONG dueTime = 0;       // 立即启动（相对时间）
	DWORD period = timer->m_timeout;  // 周期（如1000ms）
	DWORD tolerance = 0;         // 不允许偏差（系统可能忽略）

	// 3. 转换dueTime为FILETIME（相对时间需用负值）
	FILETIME ftDueTime;
	// 转换公式：毫秒 -> 100纳秒单位（1ms = 10,000 * 100纳秒）
	dueTime = (ULONGLONG)dueTime * 10000;
	// 相对时间需取负（表示从现在开始计算）
	LARGE_INTEGER liDueTime;
	liDueTime.QuadPart = -dueTime;  // 负号表示相对时间
	ftDueTime.dwLowDateTime = liDueTime.LowPart;
	ftDueTime.dwHighDateTime = liDueTime.HighPart;

	// 4. 启动定时器
	SetThreadpoolTimer(timer->timerId, &ftDueTime, period, tolerance);
	timer->m_isRun = true;
	//timer->m_isPeriodic = true;
}
void VXTimerBase_stop(XTimerBase* timer)
{
	//printf("停止定时器\n");
	if (timer->timerId&& XTimerBase_isRunning(timer))
	{
		SetThreadpoolTimer(((PTP_TIMER)(timer->timerId)), NULL, 0, 0);
		timer->m_isRun = false;
		((XTimerWin32ThreadpoolTimer*)timer)->m_twoCb = false;
	}
}
void VXTimerBase_deinit(XTimerBase* timer)
{
	VXTimerBase_stop(timer);
	if (timer->timerId)
	{
		// 等待所有回调完成
		WaitForThreadpoolTimerCallbacks(((PTP_TIMER)(timer->timerId)), false);
		// 清理资源
		CloseThreadpoolTimer(((PTP_TIMER)(timer->timerId)));
	}
	// 释放父对象
	XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(timer);
}

XVtable* XTimerWin32ThreadpoolTimer_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XTIMERWIN32THREADPOOLTIMERE_VTABLE_SIZE)
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
	printf("XTimerWin32ThreadpoolTimer size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XTimerWin32ThreadpoolTimer* XTimerXTimerWin32ThreadpoolTimer_create()
{
	XTimerWin32ThreadpoolTimer* timer = XMemory_malloc(sizeof(XTimerWin32ThreadpoolTimer));
	if (timer == NULL)
		return NULL;
	XTimerWin32ThreadpoolTimer_class_init(timer);
	Set_Class_MemoryFree(timer, XFree);
	return timer;
}

void XTimerWin32ThreadpoolTimer_init(XTimerWin32ThreadpoolTimer* timer)
{
	if (ISNULL(timer, ""))
		return;
	//开始初始化
	//初始化父类以外的数据
	memset(((XTimerBase*)timer) + 1, 0, sizeof(XTimerWin32ThreadpoolTimer) - sizeof(XTimerBase));
	XTimerBase_init(timer, NULL);
	XClassGetVtable(timer) = XTimerWin32ThreadpoolTimer_class_init();

	if (timer == NULL)
		return;
	// 创建线程池计时器
	((XTimerBase*)timer)->timerId = CreateThreadpoolTimer(
		TimerCallbackThreadpoolTimer,      // 回调函数
		timer,         // 传递给回调函数的上下文
		NULL                // 使用默认环境
	);
}

#endif
