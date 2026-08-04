#include"XTimerTest.h"
#include"XTimeWheelGroup.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XThread.h"
#include"XDateTime.h"
static void Callback1(void* userData)
{
	static size_t current = 0;

	XPrintf("定时器1触发:%d ms\n",XDateTime_currentMSecsSinceEpoch()-current);
	current = XDateTime_currentMSecsSinceEpoch();

	/*XTimerTimeWheel* timer = XTimer_create();
	XTimer_setUserData(timer, userData);
	XTimer_setTimeout(timer, 5);
	XTimer_setTimerCallback(timer, Callback1);
	XTimer_start_base(timer);
	XTimerGroupBase_addTimerMs(userData, timer);*/
}
static void Callback2(void* userData)
{
	static size_t current = 0;

	XPrintf("定时器2触发:%d ms\n", XDateTime_currentMSecsSinceEpoch() - current);
	current = XDateTime_currentMSecsSinceEpoch();
	//XTimer_deleteLater(userData);
}

static void CancelRegressionCallback(void* userData, XTimerData* timer)
{
	(void)userData;
	(void)timer;
}

static void XTimerTimeWheelCancelRegressionTest(XVariant* menuData)
{
	(void)menuData;
	XTimeWheelGroup* wheel = XTimeWheelGroup_global();
	bool passed = wheel != NULL;

	XPrintf("========== 时间轮取消回收回归测试开始 ==========\n");
	for (size_t i = 0; passed && i < 512; ++i)
	{
		XTimerData data = { 0 };
		XTimerData_setInterval(&data, 1000);
		XTimerData_setTimerCallback(&data, CancelRegressionCallback);

		XHandle handle = XTimeWheelGroup_addTimerMs_base(wheel, data);
		if (!handle || !XTimeWheelGroup_removeTimer_base(wheel, handle))
		{
			XPrintf("[失败] 第 %zu 次启动或取消定时器失败\n", i + 1);
			passed = false;
			break;
		}

		XTimeWheelGroup_handler_base(wheel);
		if (XTimeWheelGroup_count(wheel) != 0)
		{
			XPrintf("[失败] 第 %zu 次取消后仍有 %zu 个定时器未回收\n",
				i + 1, XTimeWheelGroup_count(wheel));
			passed = false;
		}
	}

	if (passed)
		XPrintf("[通过] 连续 512 次启动、取消和回收均正常\n");
	XPrintf("========== 时间轮取消回收回归测试结束 ==========\n");
}

#define TIMEWHEEL_MPSC_THREAD_COUNT 4
#define TIMEWHEEL_MPSC_ITERATIONS 2000
#define TIMEWHEEL_MPSC_MAX_PENDING 32

typedef struct XTimeWheelMpscTestContext
{
	XTimeWheelGroup* wheel;
	XAtomic_bool start;
	XAtomic_bool stop;
	XAtomic_int32_t ready;
	XAtomic_int32_t completed;
	XAtomic_int32_t addFailures;
	XAtomic_int32_t cancelFailures;
	XAtomic_int32_t unexpectedCallbacks;
} XTimeWheelMpscTestContext;

static void MpscRegressionCallback(void* userData, XTimerData* timer)
{
	(void)timer;
	XTimeWheelMpscTestContext* context = (XTimeWheelMpscTestContext*)userData;
	if (context)
		XAtomic_fetch_add_int32(&context->unexpectedCallbacks, 1,
			XAtomic_MemoryOrder_Relaxed);
}

static void XTimerTimeWheelMpscProducer(XThread* thread, XVarList* args)
{
	(void)thread;
	XVarList_args_1(args, XTimeWheelMpscTestContext*, contextArg);
	XTimeWheelMpscTestContext* context = (XTimeWheelMpscTestContext*)contextArg;
	if (!context) return;

	XAtomic_fetch_add_int32(&context->ready, 1, XAtomic_MemoryOrder_Release);
	while (!XAtomic_load_bool(&context->start, XAtomic_MemoryOrder_Acquire)
		&& !XAtomic_load_bool(&context->stop, XAtomic_MemoryOrder_Acquire))
		XThread_yieldCurrentThread();

	for (size_t i = 0; i < TIMEWHEEL_MPSC_ITERATIONS
		&& !XAtomic_load_bool(&context->stop, XAtomic_MemoryOrder_Acquire); ++i)
	{
		/* 给单消费者留下清扫窗口，避免测试本身用突发提交耗尽全局固定池。 */
		while (XTimeWheelGroup_count(context->wheel) >= TIMEWHEEL_MPSC_MAX_PENDING
			&& !XAtomic_load_bool(&context->stop, XAtomic_MemoryOrder_Acquire))
		{
			XThread_yieldCurrentThread();
		}
		if (XAtomic_load_bool(&context->stop, XAtomic_MemoryOrder_Acquire))
			break;

		XTimerData data = { 0 };
		XTimerData_setInterval(&data, 10000);
		XTimerData_setUserData(&data, context);
		XTimerData_setTimerCallback(&data, MpscRegressionCallback);

		XHandle handle = XTimeWheelGroup_addTimerMs_base(
			(XTimerGroupBase*)context->wheel, data);
		if (!handle)
		{
			XAtomic_fetch_add_int32(&context->addFailures, 1,
				XAtomic_MemoryOrder_Relaxed);
		}
		else if (!XTimeWheelGroup_removeTimer_base(
			(XTimerGroupBase*)context->wheel, handle))
		{
			XAtomic_fetch_add_int32(&context->cancelFailures, 1,
				XAtomic_MemoryOrder_Relaxed);
		}

		if ((i & 31U) == 0)
			XThread_yieldCurrentThread();
	}

	XAtomic_fetch_add_int32(&context->completed, 1, XAtomic_MemoryOrder_Release);
}

static void XTimerTimeWheelMpscRegressionTest(XVariant* menuData)
{
	(void)menuData;
	XTimeWheelMpscTestContext context = { 0 };
	XTimeWheelMpscTestContext* contextPtr = &context;
	XThread* threads[TIMEWHEEL_MPSC_THREAD_COUNT] = { 0 };
	size_t started = 0;
	bool waitsPassed = true;
	bool passed;
	int64_t deadline;

	context.wheel = XTimeWheelGroup_global();
	XAtomic_init(context.start, false);
	XAtomic_init(context.stop, false);
	XAtomic_init(context.ready, 0);
	XAtomic_init(context.completed, 0);
	XAtomic_init(context.addFailures, 0);
	XAtomic_init(context.cancelFailures, 0);
	XAtomic_init(context.unexpectedCallbacks, 0);

	XPrintf("========== 时间轮 MPSC 多线程回归测试开始 ==========\n");
	for (size_t i = 0; i < TIMEWHEEL_MPSC_THREAD_COUNT; ++i)
	{
		threads[i] = XThread_create_func(XTimerTimeWheelMpscProducer,
			XVarList_Create(XVar(XTimeWheelMpscTestContext*, contextPtr)));
		if (!threads[i] || !XThread_start(threads[i]))
			break;
		++started;
	}

	deadline = XDateTime_currentMSecsSinceEpoch() + 10000;
	while (XAtomic_load_int32(&context.ready, XAtomic_MemoryOrder_Acquire) < (int32_t)started
		&& XDateTime_currentMSecsSinceEpoch() < deadline)
	{
		XThread_yieldCurrentThread();
	}
	XAtomic_store_bool(&context.start, true, XAtomic_MemoryOrder_Release);

	deadline = XDateTime_currentMSecsSinceEpoch() + 15000;
	while (XAtomic_load_int32(&context.completed, XAtomic_MemoryOrder_Acquire) < (int32_t)started
		&& XDateTime_currentMSecsSinceEpoch() < deadline)
	{
		XTimeWheelGroup_handler_base((XTimerGroupBase*)context.wheel);
		XThread_yieldCurrentThread();
	}
	if (XAtomic_load_int32(&context.completed, XAtomic_MemoryOrder_Acquire) < (int32_t)started)
		XAtomic_store_bool(&context.stop, true, XAtomic_MemoryOrder_Release);

	for (size_t i = 0; i < started; ++i)
	{
		if (!XThread_wait(threads[i], 10000))
			waitsPassed = false;
		XClass_delete_base((XClass*)threads[i]);
	}
	for (size_t i = started; i < TIMEWHEEL_MPSC_THREAD_COUNT; ++i)
	{
		if (threads[i]) XClass_delete_base((XClass*)threads[i]);
	}

	/* 处理最后一批在线程退出前刚标记取消的节点。 */
	XTimeWheelGroup_handler_base((XTimerGroupBase*)context.wheel);
	passed = started == TIMEWHEEL_MPSC_THREAD_COUNT
		&& waitsPassed
		&& XAtomic_load_int32(&context.completed, XAtomic_MemoryOrder_Acquire)
			== TIMEWHEEL_MPSC_THREAD_COUNT
		&& XAtomic_load_int32(&context.addFailures, XAtomic_MemoryOrder_Relaxed) == 0
		&& XAtomic_load_int32(&context.cancelFailures, XAtomic_MemoryOrder_Relaxed) == 0
		&& XAtomic_load_int32(&context.unexpectedCallbacks, XAtomic_MemoryOrder_Relaxed) == 0
		&& XTimeWheelGroup_count(context.wheel) == 0;

	XPrintf("线程: %zu/%d，操作: %d，添加失败: %d，取消失败: %d，误回调: %d，剩余节点: %zu\n",
		started, TIMEWHEEL_MPSC_THREAD_COUNT,
		(int)(started * TIMEWHEEL_MPSC_ITERATIONS),
		XAtomic_load_int32(&context.addFailures, XAtomic_MemoryOrder_Relaxed),
		XAtomic_load_int32(&context.cancelFailures, XAtomic_MemoryOrder_Relaxed),
		XAtomic_load_int32(&context.unexpectedCallbacks, XAtomic_MemoryOrder_Relaxed),
		XTimeWheelGroup_count(context.wheel));
	XPrintf("[%s] 时间轮 MPSC 多线程回归测试\n", passed ? "通过" : "失败");
	XPrintf("========== 时间轮 MPSC 多线程回归测试结束 ==========\n");
}

void XTimerTimeWheelTest()
{
	XPrintf("时间轮定时器测试\n");
	/*XTimeWheelGroup* wheel= XTimeWheelGroup_create(1);
	wheel= XTimeWheelGroup_create(1);
	XTimeWheelGroup_addTimeWheel(wheel,100);
	XTimeWheelGroup_addTimeWheel(wheel,10);
	XTimeWheelGroup_addTimeWheel(wheel,10);*/
	XTimeWheelGroup* wheel = XTimeWheelGroup_global();
	size_t min_time = 0, max_time = 0;
	XTimeWheelGroup_timeRange(wheel,&min_time,&max_time);
	XPrintf("定时器时间轮时间范围 %ld-%ld ms\n",min_time,max_time);
	{
		XTimerData data = { 0 };
		XTimerData_setUserData(&data, wheel);
		XTimerData_setInterval(&data,2);
		XTimerData_setTimeout(&data, 50);
		XTimerData_setTimerCallback(&data,Callback1);
		XHandle handle = XTimeWheelGroup_addTimerMs_base(wheel, data);
	
	}
	{
		XTimerData data = { 0 };

		XTimerData_setInterval(&data, 20);
		XTimerData_setTimeout(&data, 15);
		XTimerData_setTimerCallback(&data, Callback2);
		//XTimerData_setUserData(&data, timer);
		XHandle handle = XTimeWheelGroup_addTimerMs_base(wheel, data);
		
	}
	XCoreApplication_exec();
	//while(true) XTimeWheelGroup_handler_base(wheel);
}

void XMenu_XTimerTimeWheelTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XTimerTimeWheel(时间轮定时器)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XTimerTimeWheelTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "取消回收回归测试");
		XAction_setAction(action, XTimerTimeWheelCancelRegressionTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "MPSC多线程回归测试");
		XAction_setAction(action, XTimerTimeWheelMpscRegressionTest);
	}
}
