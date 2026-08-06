/*------------------------------------------------------------------------*/
/* A Sample Code of User Provided OS Dependent Functions for FatFs        */
/*------------------------------------------------------------------------*/

#include "XFileSystem_config.h"

#if defined(XFILE_USE_FATFS)

#include "ff.h"
#include <stdio.h>
#include <string.h>


#if FF_USE_LFN == 3	/* Use dynamic memory allocation */

/*------------------------------------------------------------------------*/
/* Allocate/Free a Memory Block                                           */
/*------------------------------------------------------------------------*/

#include <stdlib.h>		/* with POSIX API */
#include "XMemory.h"

//动态分配内存
void *ff_memalloc (UINT size)			
{
	return (void*)XMalloc_System((size_t)size);
}

//释放内存
void ff_memfree (void* mblock)		 
{
	XFree_System(mblock);
}

#endif




#if FF_FS_REENTRANT	/* Mutal exclusion */
/*------------------------------------------------------------------------*/
/* Definitions of Mutex                                                   */
/*------------------------------------------------------------------------*/
#ifdef _WIN32
#define OS_TYPE	0	/* 0:Win32, 1:uITRON4.0, 2:uC/OS-II, 3:FreeRTOS, 4:CMSIS-RTOS */
#elif defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#define OS_TYPE	5
#elif defined(__FreeRTOS__) 
#define OS_TYPE	3	/* 3:FreeRTOS */
#else
#define OS_TYPE	-1	/* 未定义系统 */
#endif

#if   OS_TYPE == 0	/* Win32 */
#include <windows.h>
static HANDLE Mutex[FF_VOLUMES + 1];	/* Table of mutex handle */

#elif OS_TYPE == 1	/* uITRON */
#include "itron.h"
#include "kernel.h"
static mtxid Mutex[FF_VOLUMES + 1];		/* Table of mutex ID */

#elif OS_TYPE == 2	/* uc/OS-II */
#include "includes.h"
static OS_EVENT *Mutex[FF_VOLUMES + 1];	/* Table of mutex pinter */

#elif OS_TYPE == 3	/* FreeRTOS */
#include "FreeRTOS.h"
#include "semphr.h"
static SemaphoreHandle_t Mutex[FF_VOLUMES + 1];	/* Table of mutex handle */

#elif OS_TYPE == 4	/* CMSIS-RTOS */
#include "cmsis_os.h"
static osMutexId Mutex[FF_VOLUMES + 1];	/* Table of mutex ID */
#elif OS_TYPE == 5	/* Linux/macOS/BSD（新增：POSIX 系统） */
#include <pthread.h>	/* 包含 POSIX 线程库头文件 */
#include <errno.h>

/* 定义互斥锁数组（pthread_mutex_t 是 POSIX 互斥锁类型） */
static pthread_mutex_t Mutex[FF_VOLUMES + 1];
/* 标记互斥锁是否已初始化（避免重复初始化/删除） */
static int MutexInited[FF_VOLUMES + 1] = {0};

#endif



/*------------------------------------------------------------------------*/
/* Create a Mutex                                                         */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount function to create a new mutex
/  or semaphore for the volume. When a 0 is returned, the f_mount function
/  fails with FR_INT_ERR.
*/

int ff_mutex_create (	/* Returns 1:Function succeeded or 0:Could not create the mutex */
	int vol				/* Mutex ID: Volume mutex (0 to FF_VOLUMES - 1) or system mutex (FF_VOLUMES) */
)
{
#if OS_TYPE == 0	/* Win32 */
	Mutex[vol] = CreateMutex(NULL, FALSE, NULL);
	return (int)(Mutex[vol] != NULL);

#elif OS_TYPE == 1	/* uITRON */
	T_CMTX cmtx = {TA_TPRI,1};

	Mutex[vol] = acre_mtx(&cmtx);
	return (int)(Mutex[vol] > 0);

#elif OS_TYPE == 2	/* uC/OS-II */
	OS_ERR err;

	Mutex[vol] = OSMutexCreate(0, &err);
	return (int)(err == OS_NO_ERR);

#elif OS_TYPE == 3	/* FreeRTOS */
	Mutex[vol] = xSemaphoreCreateMutex();
	return (int)(Mutex[vol] != NULL);

#elif OS_TYPE == 4	/* CMSIS-RTOS */
	osMutexDef(cmsis_os_mutex);

	Mutex[vol] = osMutexCreate(osMutex(cmsis_os_mutex));
	return (int)(Mutex[vol] != NULL);
#elif OS_TYPE == 5	/* Linux/macOS/BSD（新增：POSIX 互斥锁创建） */
	if (MutexInited[vol]) {
		return 1;	/* 已初始化，直接返回成功 */
	}

	/* 初始化互斥锁（默认属性） */
	int ret = pthread_mutex_init(&Mutex[vol], NULL);
	if (ret == 0) {
		MutexInited[vol] = 1;	/* 标记为已初始化 */
		return 1;
	} else {
		fprintf(stderr, "pthread_mutex_init failed: %s\n", strerror(ret));
		return 0;
	}
#endif
}


/*------------------------------------------------------------------------*/
/* Delete a Mutex                                                         */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount function to delete a mutex or
/  semaphore of the volume created with ff_mutex_create function.
*/

void ff_mutex_delete (	/* Returns 1:Function succeeded or 0:Could not delete due to an error */
	int vol				/* Mutex ID: Volume mutex (0 to FF_VOLUMES - 1) or system mutex (FF_VOLUMES) */
)
{
#if OS_TYPE == 0	/* Win32 */
	if (Mutex[vol]) {
		CloseHandle(Mutex[vol]);
		Mutex[vol] = NULL;
	}

#elif OS_TYPE == 1	/* uITRON */
	del_mtx(Mutex[vol]);

#elif OS_TYPE == 2	/* uC/OS-II */
	OS_ERR err;

	OSMutexDel(Mutex[vol], OS_DEL_ALWAYS, &err);

#elif OS_TYPE == 3	/* FreeRTOS */
	vSemaphoreDelete(Mutex[vol]);

#elif OS_TYPE == 4	/* CMSIS-RTOS */
	osMutexDelete(Mutex[vol]);
#elif OS_TYPE == 5	/* Linux/macOS/BSD（新增：POSIX 互斥锁删除） */
	if (!MutexInited[vol]) {
		return;	/* 未初始化，无需删除 */
	}

	/* 销毁互斥锁 */
	int ret = pthread_mutex_destroy(&Mutex[vol]);
	if (ret != 0) {
		fprintf(stderr, "pthread_mutex_destroy failed: %s\n", strerror(ret));
	}
	MutexInited[vol] = 0;	/* 标记为未初始化 */
#endif
}


/*------------------------------------------------------------------------*/
/* Request a Grant to Access the Volume                                   */
/*------------------------------------------------------------------------*/
/* This function is called on enter file functions to lock the volume.
/  When a 0 is returned, the file function fails with FR_TIMEOUT.
*/

int ff_mutex_take (	/* Returns 1:Succeeded or 0:Timeout */
	int vol			/* Mutex ID: Volume mutex (0 to FF_VOLUMES - 1) or system mutex (FF_VOLUMES) */
)
{
#if OS_TYPE == 0	/* Win32 */
	return (int)(WaitForSingleObject(Mutex[vol], FF_FS_TIMEOUT) == WAIT_OBJECT_0);

#elif OS_TYPE == 1	/* uITRON */
	return (int)(tloc_mtx(Mutex[vol], FF_FS_TIMEOUT) == E_OK);

#elif OS_TYPE == 2	/* uC/OS-II */
	OS_ERR err;

	OSMutexPend(Mutex[vol], FF_FS_TIMEOUT, &err));
	return (int)(err == OS_NO_ERR);

#elif OS_TYPE == 3	/* FreeRTOS */
	return (int)(xSemaphoreTake(Mutex[vol], FF_FS_TIMEOUT) == pdTRUE);

#elif OS_TYPE == 4	/* CMSIS-RTOS */
	return (int)(osMutexWait(Mutex[vol], FF_FS_TIMEOUT) == osOK);
#elif OS_TYPE == 5	/* Linux/macOS/BSD（新增：POSIX 互斥锁获取） */
	if (!MutexInited[vol]) {
		return 0;	/* 未初始化，获取失败 */
	}

	/* 等待互斥锁（带超时） */
	struct timespec timeout;
	clock_gettime(CLOCK_REALTIME, &timeout);	/* 获取当前时间 */
	/* 计算超时时间：当前时间 + FF_FS_TIMEOUT（单位：毫秒 → 纳秒） */
	timeout.tv_nsec += FF_FS_TIMEOUT * 1000000;
	/* 处理纳秒进位（1秒=1e9纳秒） */
	if (timeout.tv_nsec >= 1000000000) {
		timeout.tv_sec += 1;
		timeout.tv_nsec -= 1000000000;
	}

	/* 带超时获取互斥锁（pthread_mutex_timedlock 是 POSIX 标准函数） */
	int ret = pthread_mutex_timedlock(&Mutex[vol], &timeout);
	if (ret == 0) {
		return 1;	/* 获取成功 */
	} else if (ret == ETIMEDOUT) {
		fprintf(stderr, "pthread_mutex_timedlock timeout\n");
		return 0;	/* 超时失败 */
	} else {
		fprintf(stderr, "pthread_mutex_timedlock failed: %s\n", strerror(ret));
		return 0;	/* 其他错误 */
	}
#endif
}



/*------------------------------------------------------------------------*/
/* Release a Grant to Access the Volume                                   */
/*------------------------------------------------------------------------*/
/* This function is called on leave file functions to unlock the volume.
*/

void ff_mutex_give (
	int vol			/* Mutex ID: Volume mutex (0 to FF_VOLUMES - 1) or system mutex (FF_VOLUMES) */
)
{
#if OS_TYPE == 0	/* Win32 */
	ReleaseMutex(Mutex[vol]);

#elif OS_TYPE == 1	/* uITRON */
	unl_mtx(Mutex[vol]);

#elif OS_TYPE == 2	/* uC/OS-II */
	OSMutexPost(Mutex[vol]);

#elif OS_TYPE == 3	/* FreeRTOS */
	xSemaphoreGive(Mutex[vol]);

#elif OS_TYPE == 4	/* CMSIS-RTOS */
	osMutexRelease(Mutex[vol]);
#elif OS_TYPE == 5	/* Linux/macOS/BSD（新增：POSIX 互斥锁释放） */
	if (!MutexInited[vol]) {
		return;	/* 未初始化，无需释放 */
	}

	/* 释放互斥锁 */
	int ret = pthread_mutex_unlock(&Mutex[vol]);
	if (ret != 0) {
		fprintf(stderr, "pthread_mutex_unlock failed: %s\n", strerror(ret));
	}

#endif
}

#endif	/* FF_FS_REENTRANT */

#endif
