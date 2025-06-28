#ifdef configUSE_FREERTOS
#ifndef XMUTEXFREERTOS_H
#define XMUTEXFREERTOS_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XMutex.h"
//互斥锁
typedef struct XMutexFreeRTOS
{
	XMutex m_parent;
	void* m_mutex;
}XMutexFreeRTOS;
XVtable* XMutexFreeRTOS_class_init();
XMutexFreeRTOS* XMutexFreeRTOS_create(const char* name);
void XMutexFreeRTOS_init(XMutexFreeRTOS* mutex, const char* name);
#define XMutex_create(name)  XMutexFreeRTOS_create(name)
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
#endif
