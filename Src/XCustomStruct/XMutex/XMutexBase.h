#ifndef XMUTEXBASE_H
#define XMUTEXBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XClass.h"
XCLASS_DEFINE_BEGING(XMutexBase)
XCLASS_DEFINE_ENUM(XMutexBase, Lock) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XMutexBase, Lock_Wait),
XCLASS_DEFINE_ENUM(XMutexBase, Unlock),
XCLASS_DEFINE_END(XMutexBase)
//互斥锁
typedef struct XMutexBase
{
	XClass m_parent;
}XMutexBase;
void XMutexBase_init(XMutexBase* mutex, XVtable* vtable);
//上锁
bool XMutexBase_lock_base(XMutexBase* mutex);
//上锁
bool XMutexBase_lock_wait_base(XMutexBase* mutex,size_t timerout);
//解锁
bool XMutexBase_unlock_base(XMutexBase* mutex);
#define XMutexBase_delete_base XClass_delete_base

#ifdef WIN32
#include"XMutexWin32.h"
#endif
#ifdef configUSE_FREERTOS
#include"XMutexFreeRTOS.h"
#endif
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
