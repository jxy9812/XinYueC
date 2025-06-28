#ifndef XSEMAPHORE_H
#define XSEMAPHORE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"XMutex.h"
XCLASS_DEFINE_BEGING(XSemaphore)
XCLASS_DEFINE_ENUM(XSemaphore, LockISR) = XCLASS_VTABLE_GET_SIZE(XMutex),
XCLASS_DEFINE_ENUM(XSemaphore, UnlockISR),
XCLASS_DEFINE_END(XSemaphore)
//互斥锁
typedef struct XSemaphore
{
	XMutex m_parent;
}XSemaphore;
void XSemaphore_init(XSemaphore* semaphore, const char* name);
/*             api                                 */
void XSemaphore_lockISR_base(XSemaphore* semaphore);
void XSemaphore_unlockISR_base(XSemaphore* semaphore);
#define XSemaphore_lock_base				XMutex_lock_base
#define XSemaphore_lock_wait_base			XMutex_lock_wait_base
#define	XSemaphore_unlock_base				XMutex_unlock_base
#define XSemaphore_delete_base				XMutex_delete_base
/*             平台具体实现                                 */
#ifdef WIN32
#include"XSemaphoreWin32.h"
#endif
#ifdef configUSE_FREERTOS
#include"XSemaphoreFreeRTOS.h"
#endif
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H