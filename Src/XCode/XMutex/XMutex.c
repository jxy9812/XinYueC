#include "XMutex.h"
#include "XMemory.h"
#include <string.h>
bool XMutex_lock_base(XMutex* mutex)
{
	if (ISNULL(mutex, "") || ISNULL(XClassGetVtable(mutex), ""))
		return false;
	return XClassGetVirtualFunc(mutex, EXMutex_Lock, bool(*)(XMutex*))(mutex);
}

bool XMutex_lock_wait_base(XMutex* mutex, size_t timerout)
{
	if (ISNULL(mutex, "") || ISNULL(XClassGetVtable(mutex), ""))
		return false;
	return XClassGetVirtualFunc(mutex, EXMutex_Lock_Wait, bool(*)(XMutex*))(mutex);
}

bool XMutex_unlock_base(XMutex* mutex)
{
	if (ISNULL(mutex, "") || ISNULL(XClassGetVtable(mutex), ""))
		return false;
	return XClassGetVirtualFunc(mutex, EXMutex_Unlock, bool(*)(XMutex*))(mutex);
}
