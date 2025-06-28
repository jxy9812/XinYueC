#include "XSemaphoreBase.h"
#include <string.h>
void XSemaphoreBase_init(XSemaphoreBase* semaphore, const char* name)
{
	if (semaphore == NULL)
		return;
	XMutexBase_init(semaphore, name);
	//memset(((XMutexBase*)semaphore) + 1, 0, sizeof(XSemaphoreBase) - sizeof(XMutexBase));
}

void XSemaphoreBase_lockISR_base(XSemaphoreBase* semaphore)
{
	if (ISNULL(semaphore, "") || ISNULL(XClassGetVtable(semaphore), ""))
		return false;
	return XClassGetVirtualFunc(semaphore, EXSemaphoreBase_LockISR, bool(*)(XSemaphoreBase*))(semaphore);
}

void XSemaphoreBase_unlockISR_base(XSemaphoreBase* semaphore)
{
	if (ISNULL(semaphore, "") || ISNULL(XClassGetVtable(semaphore), ""))
		return false;
	return XClassGetVirtualFunc(semaphore, EXSemaphoreBase_UnlockISR, bool(*)(XSemaphoreBase*))(semaphore);
}
