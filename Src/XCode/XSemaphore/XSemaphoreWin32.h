#ifdef WIN32
#ifndef XSEMAPHOREWIN32_H
#define XSEMAPHOREWIN32_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XSemaphore.h"
//互斥锁
typedef struct XSemaphoreWin32
{
	XSemaphore m_parent;
	void* m_semaphore;
}XSemaphoreWin32;
XVtable* XSemaphoreWin32_class_init();
XSemaphoreWin32* XSemaphoreWin32_create(const char* name);
void XSemaphoreWin32_init(XSemaphoreWin32* mutex, const char* name);
#define XSemaphore(name)  XSemaphoreWin32_create(name)
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
#endif
