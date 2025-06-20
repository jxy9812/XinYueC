#ifndef XSEMAPHOREBASE_H
#define XSEMAPHOREBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XMutexBase.h"
XCLASS_DEFINE_BEGING(XSemaphoreBase)
XCLASS_DEFINE_ENUM(XSemaphoreBase, LockISR) = XCLASS_VTABLE_GET_SIZE(XMutexBase),
XCLASS_DEFINE_ENUM(XSemaphoreBase, UnlockISR),
XCLASS_DEFINE_END(XSemaphoreBase)
//互斥锁
typedef struct XSemaphoreBase
{
	XMutexBase m_parent;
}XSemaphoreBase;
//XVtable* XSemaphoreBase_class_init();
//XSemaphoreBase* XSemaphoreBase_create(const char* name);
void XSemaphoreBase_init(XSemaphoreBase* semaphore, const char* name);
void XSemaphoreBase_lockISR_base(XSemaphoreBase* semaphore);
void XSemaphoreBase_unlockISR_base(XSemaphoreBase* semaphore);
#define XSemaphoreBase_lock_base				XMutexBase_lock_base
#define XSemaphoreBase_lock_wait_base			XMutexBase_lock_wait_base
#define	XSemaphoreBase_unlock_base				XMutexBase_unlock_base
#define XSemaphoreBase_delete_base				XMutexBase_delete_base
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H