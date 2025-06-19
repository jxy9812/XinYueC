#ifdef WIN32
#ifndef XSEMAPHOREWIN32_H
#define XSEMAPHOREWIN32_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XMutexBase.h"
//互斥锁
typedef struct XSemaphoreWin32
{
	XMutexBase m_parent;
	void* m_mutex;
}XSemaphoreWin32;
XVtable* XSemaphoreWin32_class_init();
XSemaphoreWin32* XSemaphoreWin32_create(const char* name);
void XSemaphoreWin32_init(XSemaphoreWin32* mutex, const char* name);
#define XSemaphoreWin32_lock_base				XMutexBase_lock_base
#define XSemaphoreWin32_lock_wait_base			XMutexBase_lock_wait_base
#define	XSemaphoreWin32_unlock_base				XMutexBase_unlock_base
#define XSemaphoreWin32_delete_base				XMutexBase_delete_base
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
#endif
