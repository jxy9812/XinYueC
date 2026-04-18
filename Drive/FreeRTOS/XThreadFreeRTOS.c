#ifdef __FreeRTOS__
#include "XThread.h"
#include "XEvent.h"
#include "XMemory.h"
#include "XEventLoop.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "CXinYueConfig.h"

void XThread_mapInsert(XThread* Object);
void XThread_mapRemove(XThread* Object);

// 前向声明虚函数
static bool VXThread_start(XThread* Object);
static bool VXThread_wait(XThread* Object, unsigned long time);
static bool VXThread_isFinished(const XThread* Object);
static bool VXThread_isRunning(const XThread* Object);
static int VXThread_loopLevel(const XThread* Object);
static XThread_Priority VXThread_priority(const XThread* Object);
static void VXThread_requestInterruption(XThread* Object);
static void VXThread_setPriority(XThread* Object, XThread_Priority priority);
static void VXThread_setStackSize(XThread* Object, uint32_t stackSize);
static void VXThread_deinit(XThread* Object);
static bool VXThread_terminate(XThread* Object);
// FreeRTOS 线程扩展数据结构
typedef struct {
	XThread m_class;
	SemaphoreHandle_t completion_sem; // 线程完成信号量
#if defined(configSUPPORT_STATIC_ALLOCATION)&&configSUPPORT_STATIC_ALLOCATION
	StaticSemaphore_t completion_sem_buf; // 静态信号量缓冲区
#endif
} XThreadFreeRTOS;
// 虚函数表初始化
XVtable* XThread_class_init() {
	XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XThread))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = {
	VXThread_start, VXThread_wait,
	VXThread_isFinished,
	VXThread_isRunning, VXThread_loopLevel, VXThread_priority, VXThread_terminate,
	VXThread_requestInterruption,
	VXThread_setPriority, VXThread_setStackSize
	};
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXThread_deinit);
#if SHOWCONTAINERSIZE
	DEBUG_PRINTF("XThread(FreeRTOS) size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
// 线程函数包装器
static void ThreadFunction(void* arg)
{
	XThread* Object = (XThread*)arg;
	XThread_mapInsert(Object);
	if (!Object) {
		DEBUG_PRINTF("Invalid thread object");
		vTaskDelete(NULL);
		return;
	}
	// 执行用户线程函数
	if (Object->m_start_routine) {
		Object->m_start_routine(Object->m_varList);
	}
	// 运行事件循环
	if (Object->loopLevel > 0 && Object->m_eventLoop) {
		XEventLoop_exec(Object->m_eventLoop);
	}
	// 标记线程结束并释放信号量
	Object->m_finished = true;
	if (((XThreadFreeRTOS*)Object)->completion_sem) {
		xSemaphoreGive(((XThreadFreeRTOS*)Object)->completion_sem);
	}
	// 从线程映射中移除
	XThread_mapRemove(Object);
	// 任务自删除
	vTaskDelete(NULL);
}
// 优先级映射（FreeRTOS 优先级：0 最低，configMAX_PRIORITIES-1 最高）
static UBaseType_t mapPriority(XThread_Priority priority) {
	UBaseType_t maxPriority = configMAX_PRIORITIES - 1;
	switch (priority) {
	case XThread_IdlePriority:
		return 0;
	case XThread_LowestPriority:
		return maxPriority / 6;
	case XThread_LowPriority:
		return maxPriority / 3;
	case XThread_NormalPriority:
		return maxPriority / 2;
	case XThread_HighPriority:
		return maxPriority * 2 / 3;
	case XThread_HighestPriority:
		return maxPriority * 5 / 6;
	case XThread_TimeCriticalPriority:
		return maxPriority;
	case XThread_InheritPriority:
		return uxTaskPriorityGet(NULL); // 继承当前任务优先级
	default:
		DEBUG_PRINTF("Unknown priority level, using normal");
		return maxPriority / 2;
	}
}
// 启动线程
static bool VXThread_start(XThread* Object) {
	if (!Object || Object->m_handle != 0) {
		DEBUG_PRINTF("Invalid thread object or already started");
		return false;
	}
#if defined(configSUPPORT_STATIC_ALLOCATION)&&configSUPPORT_STATIC_ALLOCATION
	// 使用静态信号量缓冲区创建二进制信号量
	((XThreadFreeRTOS*)Object)->completion_sem = xSemaphoreCreateBinaryStatic(&(((XThreadFreeRTOS*)Object)->completion_sem_buf));
#else
	((XThreadFreeRTOS*)Object)->completion_sem = xSemaphoreCreateBinary();
#endif
	if (!((XThreadFreeRTOS*)Object)->completion_sem) {
		DEBUG_PRINTF("Failed to create completion semaphore");
		return false;
	}

	// 转换栈大小（FreeRTOS 栈大小单位为字）
	uint32_t stackWords = Object->m_stackSize / sizeof(StackType_t);
	if (stackWords < configMINIMAL_STACK_SIZE) {
		stackWords = configMINIMAL_STACK_SIZE;
		DEBUG_PRINTF("Stack size too small, using minimal size: % u", stackWords * sizeof(StackType_t));
	}
	// 创建 FreeRTOS 任务
	TaskHandle_t taskHandle;
	BaseType_t result = xTaskCreate(
		ThreadFunction,
		"XThread",
		stackWords,
		Object,
		mapPriority(Object->m_priority),
		&taskHandle
	);
	if (result != pdPASS) {
		DEBUG_PRINTF("Failed to create task, error: %d", result);
		vSemaphoreDelete(((XThreadFreeRTOS*)Object)->completion_sem);
		((XThreadFreeRTOS*)Object)->completion_sem = NULL;
		return false;
	}
	Object->m_handle = (XHandle)taskHandle;
	DEBUG_PRINTF("Thread started, handle: %p", taskHandle);
	return true;
}
// 等待线程结束
static bool VXThread_wait(XThread* Object, unsigned long time) {
	if (!Object || Object->m_handle == 0) {
		DEBUG_PRINTF("Invalid thread object or not running");
		return false;
	}
	TickType_t ticks = (time == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(time);
	BaseType_t result = xSemaphoreTake(((XThreadFreeRTOS*)Object)->completion_sem, ticks);
	if (result != pdTRUE) {
		DEBUG_PRINTF("Thread wait timed out or failed");
		return false;
	}
	return true;
}
// 检查线程是否已结束
static bool VXThread_isFinished(const XThread* Object) {
	if (!Object) {
		return false;
	}
	return Object->m_finished;
}
// 检查线程是否正在运行
static bool VXThread_isRunning(const XThread* Object) {
	if (!Object || Object->m_handle == 0) {
		return false;
	}
	eTaskState state = eTaskGetState((TaskHandle_t)Object->m_handle);
	return (state != eDeleted) && (state != eInvalid);
}
// 获取循环级别
static int VXThread_loopLevel(const XThread* Object) {
	return Object ? Object->loopLevel : 0;
}
// 获取优先级
static XThread_Priority VXThread_priority(const XThread* Object) {
	return Object ? Object->m_priority : XThread_NormalPriority;
}
// 终止线程
static bool VXThread_terminate(XThread* Object) {
	if (!Object || Object->m_handle == 0) {
		DEBUG_PRINTF("Invalid thread object or not running");
		return false;
	}
	vTaskDelete((TaskHandle_t)Object->m_handle);
	Object->m_finished = true;
	Object->m_handle = 0;
	// 清理资源
	//if (Object->m_userData) {
	vSemaphoreDelete(((XThreadFreeRTOS*)Object)->completion_sem);
	((XThreadFreeRTOS*)Object)->completion_sem = NULL;
	//}
	DEBUG_PRINTF("Thread terminated");
	return true;
}
// 请求中断线程
static void VXThread_requestInterruption(XThread* Object) {
	if (!Object) {
		return;
	}
	Object->m_interruptionRequested = true;
	// 唤醒事件循环（如果存在）
	if (Object->m_eventLoop) {
		XEventLoop_wakeUp(Object->m_eventLoop);
	}
	// 发送任务通知唤醒线程
	if (Object->m_handle != 0) {
		xTaskNotifyGive((TaskHandle_t)Object->m_handle);
	}
	DEBUG_PRINTF("Interruption requested for thread");
}
// 设置线程优先级
static void VXThread_setPriority(XThread* Object, XThread_Priority priority) {
	if (!Object) {
		return;
	}
	Object->m_priority = priority;
	if (Object->m_handle != 0) {
		UBaseType_t newPriority = mapPriority(priority);
		vTaskPrioritySet((TaskHandle_t)Object->m_handle, newPriority);
		DEBUG_PRINTF("Thread priority set to %u", newPriority);
	}
}
// 设置栈大小（仅在启动前有效）
static void VXThread_setStackSize(XThread* Object, uint32_t stackSize) {
	if (Object) {
		Object->m_stackSize = stackSize;
		DEBUG_PRINTF("Thread stack size set to % u", stackSize);
	}
}
// 销毁线程对象
static void VXThread_deinit(XThread* Object) {
	if (!Object) return;
	if (XThread_isRunning(Object)) {
		XThread_requestInterruption(Object);
		// 等待线程结束，最长 100ms
		if (!XThread_wait(Object, 100)) {
			// 超时未结束，强制终止
			VXThread_terminate(Object);
			DEBUG_PRINTF("Thread forced termination");
		}
	}
	// 清理事件循环
	if (Object->m_eventLoop) {
		XEventLoop_deleteLater(Object->m_eventLoop);
		Object->m_eventLoop = NULL;
	}
	// 从线程映射中移除
	XThread_mapRemove(Object);
	DEBUG_PRINTF("Thread object deinitialized");
}
// 获取当前线程 ID（使用任务句柄作为唯一标识）
XHandle XThread_currentThreadId() {
	return (XHandle)xTaskGetCurrentTaskHandle();
}

// 创建 XThread 对象
XThread* XThread_create_func(void (*start_routine)(void*), void* arg)
{
	XThread* Object = (XThreadFreeRTOS*)XMemory_malloc(sizeof(XThreadFreeRTOS));
	if (Object == NULL) {
		return NULL;
	}
	XThread_init(Object);
	Set_Class_MemoryFree(Object);
	Object->m_start_routine = start_routine;
	Object->m_varList = arg;

	XThread_currentThread();//初始化
	((XThreadFreeRTOS*)Object)->completion_sem = NULL;
	return Object;
}
#endif