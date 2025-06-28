#include "XSemaphore.h"
#include <string.h>

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
