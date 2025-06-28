#ifdef configUSE_FREERTOS
#ifndef XSEMAPHOREFREERTOS_H
#define XSEMAPHOREFREERTOS_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XSemaphore.h"
//互斥锁
typedef struct XSemaphoreFreeRTOS
{
	XSemaphore m_parent;
	void* m_semaphore;
}XSemaphoreFreeRTOS;
XVtable* XSemaphoreFreeRTOS_class_init();
XSemaphoreFreeRTOS* XSemaphoreFreeRTOS_create(const char* name);
void XSemaphoreFreeRTOS_init(XSemaphoreFreeRTOS* semaphore, const char* name);
#define XSemaphore(name)  XSemaphoreFreeRTOS_create(name)
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
#endif
