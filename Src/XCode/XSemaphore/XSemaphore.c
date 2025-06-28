#include "XSemaphore.h"
#include <string.h>
void XSemaphore_init(XSemaphore* semaphore, const char* name)
{
	if (semaphore == NULL)
		return;
	XMutex_init(semaphore, name);
	//memset(((XMutex*)semaphore) + 1, 0, sizeof(XSemaphore) - sizeof(XMutex));
}

void XSemaphore_lockISR_base(XSemaphore* semaphore)
{
	if (ISNULL(semaphore, "") || ISNULL(XClassGetVtable(semaphore), ""))
		return false;
	return XClassGetVirtualFunc(semaphore, EXSemaphore_LockISR, bool(*)(XSemaphore*))(semaphore);
}

void XSemaphore_unlockISR_base(XSemaphore* semaphore)
{
	if (ISNULL(semaphore, "") || ISNULL(XClassGetVtable(semaphore), ""))
		return false;
	return XClassGetVirtualFunc(semaphore, EXSemaphore_UnlockISR, bool(*)(XSemaphore*))(semaphore);
}
