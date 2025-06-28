#ifdef configUSE_FREERTOS
#ifndef XSEMAPHOREFREERTOS_H
#define XSEMAPHOREFREERTOS_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XSemaphoreBase.h"
//互斥锁
typedef struct XSemaphoreFreeRTOS
{
	XSemaphoreBase m_parent;
	void* m_semaphore;
}XSemaphoreFreeRTOS;
XVtable* XSemaphoreFreeRTOS_class_init();
XSemaphoreFreeRTOS* XSemaphoreFreeRTOS_create(const char* name);
void XSemaphoreFreeRTOS_init(XSemaphoreFreeRTOS* semaphore, const char* name);
#define XSemaphoreFreeRTOS_lock_base				XMutexBase_lock_base
#define XSemaphoreFreeRTOS_lock_wait_base			XMutexBase_lock_wait_base
#define	XSemaphoreFreeRTOS_unlock_base				XMutexBase_unlock_base
#define XSemaphoreFreeRTOS_delete_base				XMutexBase_delete_base
#define XSemaphoreFreeRTOS_lockISR_base				XSemaphoreBase_lockISR_base
#define XSemaphoreFreeRTOS_unlockISR_base			XSemaphoreBase_unlockISR_base
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
#endif
