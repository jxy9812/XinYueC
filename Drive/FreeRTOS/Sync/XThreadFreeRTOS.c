/******************************************************************************
 * @file       XThreadFreeRTOS.c
 * @brief      XThread 的 FreeRTOS 平台后端（与 Drive/windows/Sync/
 *             XThreadWin32.c 同构：公共层承担运行体/线程数据/事件循环，
 *             本文件只提供任务原语映射）。
 * @details    - 线程体：公共层 VXThread_run（run()/start_routine 派发、
 *               XThreadData/TLS、事件派发器、收尾记账），本层 ThreadFunction
 *               仅包装 XThread_run_base + vTaskDelete(NULL)；
 *             - start：xTaskCreate（栈字节→字换算，下限 configMINIMAL_STACK_SIZE），
 *               优先级经 mapPriority 线性映射到 configMAX_PRIORITIES；
 *             - wait：轮询 m_finished（公共层收尾置位）+ eTaskGetState，
 *               tick 粒度休眠——不再依赖完成信号量，线程扩展结构随新
 *               架构取消（对象由公共层按 sizeof(XThread) 分配）；
 *             - terminate：vTaskDelete 外部删除；
 *             - 睡眠族：vTaskDelay（usleep 亚 tick 精度不可得，向上取整）；
 *             - TLS：TCB 原生槽 + portCLEAN_UP_TCB 登记表回收收养线程数据
 *               （XThreadStorage_taskCleanup 钩子经 FreeRTOSConfig.h 接线）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifdef __FreeRTOS__
#include "XThread.h"
#include "XEvent.h"
#include "XMemory.h"
#include "XObject.h"
#include "XEventLoop.h"
#include "XThreadData.h"
#include "XVarList.h"
#include "XTask.h"
#include "FreeRTOS.h"
#include "task.h"
#include "CXinYueConfig.h"
#include <stdint.h>
#if XSYNC_ON
#if XTHREAD_ON

static void VXThread_deinit(XThread* thread);
/* 公共层运行体（Src/XCode/XSync/XThread/XThread.c） */
void VXThread_run(XThread* thread);

// 虚函数表初始化（Run 入口在公共层 VXThread_run，仅重载析构）
XVtable* XThread_class_init() {
	XVTABLE_INIT_DEFAULT(XThread)
		XVTABLE_INHERIT_XCLASS(XObject);
	void* table[] = {
		VXThread_run
	};
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThread_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XThread);
	return XVTABLE_DEFAULT;
}

// 线程函数包装器：运行体归公共层，退出后自删除任务
static void ThreadFunction(void* arg)
{
	XThread* thread = (XThread*)arg;
	XThread_run_base(thread);
	vTaskDelete(NULL);
}

// 优先级映射（FreeRTOS：0 最低，configMAX_PRIORITIES-1 最高）
static UBaseType_t mapPriority(XThread_Priority priority) {
	UBaseType_t maxPriority = configMAX_PRIORITIES - 1;
	switch (priority) {
	case XThread_IdlePriority:			return 0;
	case XThread_LowestPriority:		return maxPriority / 6;
	case XThread_LowPriority:			return maxPriority / 3;
	case XThread_NormalPriority:		return maxPriority / 2;
	case XThread_HighPriority:			return maxPriority * 2 / 3;
	case XThread_HighestPriority:		return maxPriority * 5 / 6;
	case XThread_TimeCriticalPriority:	return maxPriority;
	case XThread_InheritPriority:		return uxTaskPriorityGet(NULL);
	default:							return maxPriority / 2;
	}
}

bool XThread_start(XThread* thread)
{
	if (!thread || thread->m_handle != 0) {
		return false; // 对象无效或已启动
	}
	// 栈大小换算：FreeRTOS 单位为字；下限为空闲任务栈
	uint32_t stackWords = thread->m_stackSize / sizeof(StackType_t);
	if (stackWords < configMINIMAL_STACK_SIZE) {
		stackWords = configMINIMAL_STACK_SIZE;
	}
	TaskHandle_t taskHandle = NULL;
	BaseType_t result = xTaskCreate(
		ThreadFunction,
		"XThread",
		(UBaseType_t)stackWords,
		thread,
		mapPriority(XThread_priority(thread)),
		&taskHandle
	);
	if (result != pdPASS) {
		return false;
	}
	thread->m_handle = (XHandle)taskHandle;
	return true;
}

bool XThread_wait(XThread* thread, uint32_t time)
{
	if (!thread || thread->m_handle == 0) {
		return false;
	}
	TickType_t start = xTaskGetTickCount();
	TickType_t timeout = (time == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(time);
	while (!thread->m_finished) {
		eTaskState state = eTaskGetState((TaskHandle_t)thread->m_handle);
		if (state == eDeleted || state == eInvalid) {
			return true;
		}
		if (timeout != portMAX_DELAY &&
			(xTaskGetTickCount() - start) >= timeout) {
			return false;
		}
		vTaskDelay(1);
	}
	return true;
}

bool XThread_isFinished(const XThread* thread)
{
	if (!thread) {
		return false;
	}
	return thread->m_finished;
}

bool XThread_isRunning(const XThread* thread)
{
	if (!thread || thread->m_handle == 0) {
		return false;
	}
	eTaskState state = eTaskGetState((TaskHandle_t)thread->m_handle);
	return (state != eDeleted) && (state != eInvalid);
}

XThread_Priority XThread_priority(const XThread* thread)
{
	if (!thread || thread->m_priority == XThread_InvalidPriority) {
		return XThread_NormalPriority;
	}
	return thread->m_priority;
}

uint32_t XThread_stackSize(const XThread* thread)
{
	return thread ? thread->m_stackSize : 0;
}

void XThread_setPriority(XThread* thread, XThread_Priority priority)
{
	if (!thread) {
		return;
	}
	thread->m_priority = priority;
	if (thread->m_handle != 0) {
		vTaskPrioritySet((TaskHandle_t)thread->m_handle, mapPriority(priority));
	}
}

void XThread_setStackSize(XThread* thread, uint32_t stackSize)
{
	if (thread) {
		thread->m_stackSize = stackSize;
	}
}

bool XThread_terminate(XThread* thread)
{
	if (!thread || thread->m_handle == 0 || !XThread_isRunning(thread)) {
		return false;
	}
	vTaskDelete((TaskHandle_t)thread->m_handle);
	thread->m_finished = true;
	return true;
}

void VXThread_deinit(XThread* thread)
{
	if (!thread) return;
	XTask_unregisterThread(thread);
	if (XThread_isRunning(thread))
		XThread_requestInterruption(thread);
	XThread_wait(thread, UINT32_MAX);
	// FreeRTOS 任务句柄无内核对象需关闭（自删/被删即终结）
	thread->m_handle = 0;
	if (thread->m_varList)
	{
		XVarList_delete(thread->m_varList);
		thread->m_varList = NULL;
	}
	if (thread->m_loop)
	{
		XObject_deleteLater(thread->m_loop);
		thread->m_loop = NULL;
	}
	XThreadData* data = thread->m_data;
	XClass_Deinit_Parent(XObject, thread);
	if (data)
	{
		XThreadData_delete(data);
		thread->m_data = NULL;
	}
}

int XThread_idealThreadCount()
{
	return 1; /* 单核口径（SMP 包络未启用）。 */
}

void XThread_msleep(uint32_t msecs)
{
	vTaskDelay(pdMS_TO_TICKS(msecs));
}

void XThread_sleep(uint32_t secs)
{
	XThread_msleep(secs * 1000);
}

void XThread_usleep(uint32_t usecs)
{
	/* 亚 tick 精度不可得：向上取整到 1 tick。 */
	vTaskDelay(pdMS_TO_TICKS((usecs + 999) / 1000) ? pdMS_TO_TICKS((usecs + 999) / 1000) : 1);
}

void XThread_yieldCurrentThread()
{
	taskYIELD();
}

#endif /* XTHREAD_ON */
#if XTHREADDATA_ON
// 获取当前线程 ID（使用任务句柄作为唯一标识）
XHandle XThread_currentThreadId() {
	return (XHandle)xTaskGetCurrentTaskHandle();
}
// XThreadData 的 TLS 载体：FreeRTOS TCB 原生槽 0
// （configNUM_THREAD_LOCAL_STORAGE_POINTERS=1，见 FreeRTOSConfig.h）。
// 原实现为 no-op——XThreadData_current 每次 TLS 未命中都新建 adopted
// 线程数据（XMutex/XSemaphore/容器全套，数百字节），调用即泄漏且旧数据
// 不可达，48KB 固件堆数秒耗尽；堆压力下的分配失败会沿框架空指针路径
// 引发野写。调度器未运行（pxCurrentTCB 为空）时保守放弃本次存取。
#define XTHREAD_TLS_INDEX 0

/* ===================== 收养线程数据回收：portCLEAN_UP_TCB 登记表 =====================
 * 对标 Posix pthread_key_create 析构 / Win32 FlsAlloc 回调（均转发到
 * XThreadData_destroyTlsData）。FreeRTOS V9.0.0 内核无 TLS 析构回调
 * （configUSE_THREAD_LOCAL_STORAGE_DELETE_CALLBACKS 为 V10.5.0+ 特性，本仓库
 * 内核不支持），改挂内核既有钩子 portCLEAN_UP_TCB——任务 TCB 释放前由
 * prvDeleteTCB 调用（tasks.c），宏在 FreeRTOSConfig.h 定义，转发到
 * XThreadStorage_taskCleanup()。
 *
 * 生命周期（与 FLS/pthread TLS 语义一致）：
 * - 登记：XThreadStorage_set() 只登记 m_isAdopted 的数据。XThread 管理的数据由
 *   VXThread_run 收尾 clear+deref（XThread.c），主线程数据随 main_thread_data
 *   常驻（对标 Qt theMainThread），均不入表；
 * - 注销：set(NULL) 仅注销不释放（deref 责任归调用方，对标显式
 *   clearCurrentThreadData 流程，不会与调用方的 deref 双重释放）；
 * - 回收：TCB 释放钩子把死任务的登记项标记为待清扫；真正的 deref/释放在
 *   XThreadStorage_get() 的延迟清扫中执行——钩子可能运行在内核临界区内
 *   （vTaskDelete 外部删除路径在临界区里直接调 prvDeleteTCB），禁止在其中
 *   调用堆释放/信号量删除等 FreeRTOS API，故只做标记、延迟释放。
 *
 * 表满时该任务退化为旧行为（退出泄漏一份 XThreadData），不影响其余任务；
 * 死任务数据在延迟清扫前暂存表中（与内核 idle 任务延迟释放 TCB 同类折中，
 * 下一次框架 TLS 访问即回收）。单核假定：登记表访问均在 taskENTER_CRITICAL
 * 之内，任务句柄为 TCB 堆地址、释放后可能被新任务复用，故 DEAD 槽位只能由
 * 清扫回收、不得当作空闲槽认领。
 */
#define XTHREAD_TLS_REGISTRY_CAPACITY 8
#define XTHREAD_TLS_TASK_DEAD ((TaskHandle_t)1)	/* 槽位标记：任务已删除待清扫；TCB 为堆对齐指针，取值恒不为 1 */

typedef struct
{
	TaskHandle_t task;	/**< NULL=空闲槽；XTHREAD_TLS_TASK_DEAD=待清扫；其余=存活任务句柄 */
	XThreadData* data;	/**< 该任务登记的收养 XThreadData（仅 m_isAdopted 数据入表） */
} XThreadTlsEntry;

static XThreadTlsEntry g_tlsRegistry[XTHREAD_TLS_REGISTRY_CAPACITY];
static volatile uint32_t g_tlsDeadPending = 0;	/**< 待清扫槽位计数（get 快速路径零成本检查） */

/* 延迟清扫：临界区内摘下全部 DEAD 槽位，临界区外 deref/释放（任务上下文） */
static void XThreadStorage_drainDead(void)
{
	XThreadData* victims[XTHREAD_TLS_REGISTRY_CAPACITY];
	size_t count = 0;
	size_t i;

	taskENTER_CRITICAL();
	{
		if (g_tlsDeadPending)
		{
			for (i = 0; i < XTHREAD_TLS_REGISTRY_CAPACITY; ++i)
			{
				if (g_tlsRegistry[i].task == XTHREAD_TLS_TASK_DEAD)
				{
					victims[count++] = g_tlsRegistry[i].data;
					g_tlsRegistry[i].task = NULL;
					g_tlsRegistry[i].data = NULL;
				}
			}
			g_tlsDeadPending = 0;
		}
	}
	taskEXIT_CRITICAL();

	while (count)
		XThreadData_destroyTlsData(victims[--count]);
}

/* portCLEAN_UP_TCB 钩子（FreeRTOSConfig.h 定义转发）：任务 TCB 释放前回调。
 * 只把登记项标记为待清扫，不做任何释放（调用点可能处于内核临界区）。 */
void XThreadStorage_taskCleanup(void* taskHandle)
{
	TaskHandle_t task = (TaskHandle_t)taskHandle;
	size_t i;

	if (!task || task == XTHREAD_TLS_TASK_DEAD)
		return;

	taskENTER_CRITICAL();
	{
		for (i = 0; i < XTHREAD_TLS_REGISTRY_CAPACITY; ++i)
		{
			if (g_tlsRegistry[i].task == task)
			{
				g_tlsRegistry[i].task = XTHREAD_TLS_TASK_DEAD;
				++g_tlsDeadPending;
				break;
			}
		}
	}
	taskEXIT_CRITICAL();
}

void XThreadStorage_set(void* p)
{
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	if (!task)
		return;

	vTaskSetThreadLocalStoragePointer(task, XTHREAD_TLS_INDEX, p);

	/* 仅收养数据需要登记回收（XThread 管理/主线程数据不入表，见上）。 */
	if (p == NULL || !((XThreadData*)p)->m_isAdopted)
		return;

	XThreadData* data = (XThreadData*)p;
	XThreadData* orphan = NULL;

	taskENTER_CRITICAL();
	{
		XThreadTlsEntry* slot = NULL;
		size_t i;
		for (i = 0; i < XTHREAD_TLS_REGISTRY_CAPACITY; ++i)
		{
			if (g_tlsRegistry[i].task == task)
			{
				slot = &g_tlsRegistry[i];
				break;
			}
		}
		if (slot)
		{
			if (slot->data != data)
			{
				orphan = slot->data;	/* 收养引用被换出且无人释放：临界区外回收 */
				slot->data = data;
			}
		}
		else
		{
			for (i = 0; i < XTHREAD_TLS_REGISTRY_CAPACITY; ++i)
			{
				if (g_tlsRegistry[i].task == NULL)
				{
					g_tlsRegistry[i].task = task;
					g_tlsRegistry[i].data = data;
					break;	/* 表满：无空闲槽，退化为旧行为（该任务退出泄漏一份） */
				}
			}
		}
	}
	taskEXIT_CRITICAL();

	if (orphan)
		XThreadData_destroyTlsData(orphan);
}

void* XThreadStorage_get(void)
{
	if (g_tlsDeadPending)	/* 延迟清扫：回收已删除任务的收养数据 */
		XThreadStorage_drainDead();

	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	return task ? pvTaskGetThreadLocalStoragePointer(task, XTHREAD_TLS_INDEX)
	            : NULL;
}

#endif /* XTHREADDATA_ON */
#endif /* XSYNC_ON */

#if !XTHREADDATA_ON
/* XTHREADDATA_ON=0（模块裁剪，含 XSYNC_ON=0）时 FreeRTOSConfig.h 的
 * portCLEAN_UP_TCB 钩子仍引用本符号：提供空实现，保证裁剪构建可链接。
 * 置于模块守卫之外，使内核在任何框架开关组合下都能解析到该符号。 */
void XThreadStorage_taskCleanup(void* task)
{
	(void)task;
}
#endif /* !XTHREADDATA_ON */
#endif /* __FreeRTOS__ */