#ifdef WIN32
#ifndef XMUTEXWIN32_H
#define XMUTEXWIN32_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XMutex.h"
//互斥锁
typedef struct XMutexWin32
{
	XMutex m_parent;
	void* m_mutex;
}XMutexWin32;
XVtable* XMutexWin32_class_init();
XMutexWin32* XMutexWin32_create(const char* name);
void XMutexWin32_init(XMutexWin32* mutex, const char* name);
#define XMutex_create(name)  XMutexWin32_create(name)
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
#endif
