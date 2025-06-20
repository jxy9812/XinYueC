#include "XMutexBase.h"
#include "XMemory.h"
#include <string.h>

void XMutexBase_init(XMutexBase* mutex, const char* name)
{
	if (mutex == NULL)
		return;
	XClass_init(mutex);
	memset(((XClass*)mutex) + 1, 0, sizeof(XMutexBase) - sizeof(XClass));
	//XClassGetVtable(mutex) = vtable;
}

bool XMutexBase_lock_base(XMutexBase* mutex)
{
	if (ISNULL(mutex, "") || ISNULL(XClassGetVtable(mutex), ""))
		return false;
	return XClassGetVirtualFunc(mutex, EXMutexBase_Lock, bool(*)(XMutexBase*))(mutex);
}

bool XMutexBase_lock_wait_base(XMutexBase* mutex, size_t timerout)
{
	if (ISNULL(mutex, "") || ISNULL(XClassGetVtable(mutex), ""))
		return false;
	return XClassGetVirtualFunc(mutex, EXMutexBase_Lock_Wait, bool(*)(XMutexBase*))(mutex);
}

bool XMutexBase_unlock_base(XMutexBase* mutex)
{
	if (ISNULL(mutex, "") || ISNULL(XClassGetVtable(mutex), ""))
		return false;
	return XClassGetVirtualFunc(mutex, EXMutexBase_Unlock, bool(*)(XMutexBase*))(mutex);
}
