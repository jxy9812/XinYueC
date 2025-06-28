#ifndef XMUTEX_H
#define XMUTEX_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XClass.h"
XCLASS_DEFINE_BEGING(XMutex)
XCLASS_DEFINE_ENUM(XMutex, Lock) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XMutex, Lock_Wait),
XCLASS_DEFINE_ENUM(XMutex, Unlock),
XCLASS_DEFINE_END(XMutex)
//互斥锁
typedef struct XMutex
{
	XClass m_parent;
	void* m_mutex;
}XMutex;
XVtable* XMutex_class_init();
XMutex* XMutex_create(const char* name);
void XMutex_init(XMutex* mutex, const char* name);
//上锁
bool XMutex_lock_base(XMutex* mutex);
//上锁
bool XMutex_lock_wait_base(XMutex* mutex,size_t timerout);
//解锁
bool XMutex_unlock_base(XMutex* mutex);
#define XMutex_delete_base XClass_delete_base

#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
