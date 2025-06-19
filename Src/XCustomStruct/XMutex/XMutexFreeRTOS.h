#ifdef configUSE_FREERTOS
#ifndef XMUTEXFREERTOS_H
#define XMUTEXFREERTOS_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XMutexBase.h"
//互斥锁
typedef struct XMutexFreeRTOS
{
	XMutexBase m_parent;
	void* m_mutex;
}XMutexFreeRTOS;
XVtable* XMutexFreeRTOS_class_init();
XMutexFreeRTOS* XMutexFreeRTOS_create();
void XMutexFreeRTOS_init(XMutexFreeRTOS* mutex);
#define XMutexFreeRTOS_lock_base					XMutexBase_lock_base
#define XMutexFreeRTOS_lock_wait_base				XMutexBase_lock_wait_base
#define	XMutexFreeRTOS_unlock_base					XMutexBase_unlock_base
#define XMutexFreeRTOS_delete_base					XMutexBase_delete_base
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
#endif
