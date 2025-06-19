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
XCLASS_DEFINE_ENUM(XMutexBase, Unlock),
XCLASS_DEFINE_ENUM(XMutexBase, LockISR),
XCLASS_DEFINE_ENUM(XMutexBase, UnlockISR),
XCLASS_DEFINE_END(XMutexBase)
//互斥锁
typedef struct XMutexBase
{
	XClass m_parent;
}XMutexBase;
void XMutexBase_init(XMutexBase* mutex, XVtable* vtable);
//上锁
bool XMutexBase_lock_base(XMutexBase* mutex);
//解锁
bool XMutexBase_unlock_base(XMutexBase* mutex);
//中断中上锁
bool XMutexBase_lockISR_base(XMutexBase* mutex);
//中断中解锁
bool XMutexBase_unlockISR_base(XMutexBase* mutex);

#define XMutexBase_delete_base XClass_delete_base
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
