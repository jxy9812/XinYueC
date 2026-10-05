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
 *             - 睡眠族：vTaskDelay（usleep 亚 tick 精度不可得，向上取整）。
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
	if (!thread || thread->m_priority == XThread_err) {
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

void XThreadStorage_set(void* p)
{
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	if (task)
		vTaskSetThreadLocalStoragePointer(task, XTHREAD_TLS_INDEX, p);
}

void* XThreadStorage_get(void)
{
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	return task ? pvTaskGetThreadLocalStoragePointer(task, XTHREAD_TLS_INDEX)
	            : NULL;
}

#endif /* XTHREADDATA_ON */
#endif /* XSYNC_ON */
#endif
