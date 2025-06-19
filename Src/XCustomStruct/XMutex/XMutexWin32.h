#ifdef WIN32
#ifndef XMUTEXWIN32_H
#define XMUTEXWIN32_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XMutexBase.h"
//互斥锁
typedef struct XMutexWin32
{
	XMutexBase m_parent;
	void* m_mutex;
}XMutexWin32;
XVtable* XMutexWin32_class_init();
XMutexWin32* XMutexWin32_create();
void XMutexWin32_init(XMutexWin32* mutex);
#define XMutexWin32_lock				XMutexBase_lock_base
#define	XMutexWin32_unlock				XMutexBase_unlock_base
#define XMutexWin32_lockISR				XMutexBase_lockISR_base
#define XMutexWin32_unlockISR			XMutexBase_unlockISR_base
#define XMutexWin32_delete				XMutexBase_delete_base
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
#endif
